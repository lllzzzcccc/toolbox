/*
 * sync_cal.c — CalendarTask（日历清单）自动上传客户端（C 版，无解释器依赖）
 *
 * 逻辑与 Python 版一致：监控本地 desktopcal.sqlite 的数据变更，
 * 变更后把本地改动上传到 https://data.xdiarys.com。
 * 下载方向（服务端→本地）由 App 自身处理，本程序只做上传。
 *
 * 编译：
 *   clang -O2 sync_cal.c -o sync-cal -lsqlite3 -lcurl -framework IOKit -framework CoreFoundation
 *
 * 用法：
 *   sync-cal --once      检查并上传一次
 *   sync-cal --watch     监控 DB 变更自动上传（常驻）
 *   sync-cal --dry-run   只打印将上传什么
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#include <sqlite3.h>
#include <curl/curl.h>
#include <CommonCrypto/CommonDigest.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

#define UA          "XDiarys-macOS, 1.79.190"
#define BASE        "https://data.xdiarys.com"
#define UPLOAD_PATH "api/data/v1/upload"
#define MAX_PATH    1024

/* 全局路径（启动时展开 ~） */
static char g_db_path[MAX_PATH];
static char g_state_path[MAX_PATH];

/* ---------------- 日志 ---------------- */
static void log_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    /* GUI 内 stdout 不可见，写日志文件便于排查 */
    FILE *f = fopen("/tmp/toolbox_sync.log", "a");
    if (f) {
        fputs("[sync-cal] ", f);
        vfprintf(f, fmt, ap);
        fputs("\n", f);
        fclose(f);
    }
    va_end(ap);
}

/* ---------------- 路径展开 ---------------- */
static void expand_tilde(const char *in, char *out, size_t outsz) {
    if (in[0] == '~') {
        const char *home = getenv("HOME");
        if (home)
            snprintf(out, outsz, "%s%s", home, in + 1);
        else
            snprintf(out, outsz, "%s", in);
    } else {
        snprintf(out, outsz, "%s", in);
    }
}

/* ---------------- 字符串缓冲（动态增长） ---------------- */
typedef struct { char *data; size_t len; size_t cap; } strbuf;

static void sb_init(strbuf *b) {
    b->cap = 1024;
    b->data = malloc(b->cap);
    b->data[0] = '\0';
    b->len = 0;
}
static void sb_free(strbuf *b) { free(b->data); b->data = NULL; }
static void sb_append(strbuf *b, const char *s) {
    size_t l = strlen(s);
    while (b->len + l + 1 > b->cap) {
        b->cap *= 2;
        char *p = realloc(b->data, b->cap);
        if (!p) return;  /* OOM：放弃本次追加，避免空指针崩溃 */
        b->data = p;
    }
    memcpy(b->data + b->len, s, l + 1);
    b->len += l;
}
/* ---------------- URL 编码（percent-encode，保留未保留字符） ---------------- */
static char *url_encode(const char *src) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(src);
    char *out = malloc(n * 3 + 1);
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
    return out;
}

/* ---------------- SHA-256 十六进制 ---------------- */
static void sha256_hex(const unsigned char *data, size_t len, char out[65]) {
    unsigned char d[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data, (CC_LONG)len, d);
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++)
        sprintf(out + 2 * i, "%02x", d[i]);
    out[64] = '\0';
}

/* 流式哈希三个字段（\1 分隔），避免固定缓冲区截断导致长内容漏同步 */
static void hash3_hex(const char *a, const char *b, const char *c, char out[65]) {
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    CC_SHA256_Update(&ctx, a, (CC_LONG)strlen(a));
    CC_SHA256_Update(&ctx, "\1", 1);
    CC_SHA256_Update(&ctx, b, (CC_LONG)strlen(b));
    CC_SHA256_Update(&ctx, "\1", 1);
    CC_SHA256_Update(&ctx, c, (CC_LONG)strlen(c));
    unsigned char d[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(d, &ctx);
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++)
        sprintf(out + 2 * i, "%02x", d[i]);
    out[64] = '\0';
}

/* ---------------- 设备序列号（IOKit） ---------------- */
static void get_serial(char *out, size_t outsz) {
    out[0] = '\0';
    io_service_t s = IOServiceGetMatchingService(kIOMainPortDefault,
                                                 IOServiceMatching("IOPlatformExpertDevice"));
    if (s) {
        CFStringRef ser = IORegistryEntryCreateCFProperty(s, CFSTR("IOPlatformSerialNumber"),
                                                          kCFAllocatorDefault, 0);
        if (ser) {
            if (!CFStringGetCString(ser, out, outsz, kCFStringEncodingUTF8))
                out[0] = '\0';
            CFRelease(ser);
        }
        IOObjectRelease(s);
    }
    if (out[0] == '\0')
        snprintf(out, outsz, "unknown-device");
}

/* ---------------- 读取 setting 表 ---------------- */
static char *read_setting_str(sqlite3 *db, const char *name) {
    sqlite3_stmt *st;
    char *result = NULL;
    if (sqlite3_prepare_v2(db, "SELECT sval FROM setting WHERE name=?", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *v = sqlite3_column_text(st, 0);
            if (v) result = strdup((const char *)v);
        }
        sqlite3_finalize(st);
    }
    return result;
}

/* ---------------- 快照（state 文件） ---------------- */
typedef struct { char *uid; char hash[65]; } state_ent;
typedef struct { state_ent *ents; int n; int cap; } state_t;

static void state_init(state_t *s) { s->ents = NULL; s->n = 0; s->cap = 0; }

static const char *state_get(state_t *s, const char *uid) {
    for (int i = 0; i < s->n; i++)
        if (strcmp(s->ents[i].uid, uid) == 0)
            return s->ents[i].hash;
    return NULL;
}

static void state_put(state_t *s, const char *uid, const char *hash) {
    if (s->n >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 256;
        state_ent *p = realloc(s->ents, s->cap * sizeof(state_ent));
        if (!p) return;  /* OOM */
        s->ents = p;
    }
    s->ents[s->n].uid = strdup(uid);
    snprintf(s->ents[s->n].hash, sizeof(s->ents[s->n].hash), "%s", hash);
    s->n++;
}

static void state_free(state_t *s) {
    for (int i = 0; i < s->n; i++) free(s->ents[i].uid);
    free(s->ents);
    s->ents = NULL; s->n = 0; s->cap = 0;
}

/* 返回 1=读到快照, 0=无快照 */
static int state_load(state_t *s, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        char *nl = strchr(tab + 1, '\n');
        if (nl) *nl = '\0';
        state_put(s, line, tab + 1);
    }
    fclose(f);
    return 1;
}

static int state_save(state_t *s, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < s->n; i++)
        fprintf(f, "%s\t%s\n", s->ents[i].uid, s->ents[i].hash);
    fclose(f);
    return 0;
}

/* ---------------- HTTP POST（libcurl） ---------------- */
typedef struct { char *data; size_t len; } resp_buf;

static size_t write_cb(void *ptr, size_t sz, size_t nmemb, void *ud) {
    resp_buf *b = (resp_buf *)ud;
    size_t add = sz * nmemb;
    char *p = realloc(b->data, b->len + add + 1);
    if (!p) return 0;  /* OOM：返回 0 让 curl 报 CURLE_WRITE_ERROR */
    b->data = p;
    memcpy(b->data + b->len, ptr, add);
    b->len += add;
    b->data[b->len] = '\0';
    return add;
}

/* 返回 HTTP 状态码；-1=网络错误。resp_out 存响应体（调用方 free）。 */
static int http_post(const char *path, const char *body, const char *cookie, char **resp_out) {
    CURL *c = curl_easy_init();
    if (!c) return -1;
    char url[2048];
    snprintf(url, sizeof(url), "%s/%s", BASE, path);

    char cookie_hdr[8192];
    if (strlen(cookie) >= sizeof(cookie_hdr) - 8) {
        log_msg("cookie 过长，跳过上传");
        curl_easy_cleanup(c);
        return -1;
    }
    snprintf(cookie_hdr, sizeof(cookie_hdr), "Cookie: %s", cookie);

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/x-www-form-urlencoded; charset=utf-8");
    hdrs = curl_slist_append(hdrs, cookie_hdr);
    hdrs = curl_slist_append(hdrs, "User-Agent: " UA);
    hdrs = curl_slist_append(hdrs, "Accept: application/json");

    resp_buf buf = {0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    if (rc == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    else
        log_msg("HTTP 错误: %s", curl_easy_strerror(rc));

    curl_easy_cleanup(c);
    curl_slist_free_all(hdrs);

    if (rc != CURLE_OK) {
        free(buf.data);
        return -1;
    }
    if (resp_out) *resp_out = buf.data;
    else free(buf.data);
    return (int)code;
}

/* 从 JSON 里提取第一个 "timestamp":<数字> */
static long parse_timestamp(const char *json) {
    const char *p = strstr(json, "\"timestamp\":");
    if (!p) return 0;
    p += strlen("\"timestamp\":");
    return atol(p);
}

/* 判断 JSON 里 result 是否为 1 */
static int result_ok(const char *json) {
    return strstr(json, "\"result\":1") != NULL;
}

/* ---------------- 同步 ---------------- */
static void do_sync(int dry_run) {
    sqlite3 *db = NULL;
    if (sqlite3_open(g_db_path, &db) != SQLITE_OK) {
        log_msg("打开 DB 失败: %s", db ? sqlite3_errmsg(db) : "unknown");
        if (db) sqlite3_close(db);
        return;
    }
    sqlite3_busy_timeout(db, 15000);

    char *cookie = read_setting_str(db, "login_key_user_cookie");
    if (!cookie || !*cookie) {
        log_msg("未找到 login_key_user_cookie（未登录？）");
        if (cookie) free(cookie);
        sqlite3_close(db);
        return;
    }

    char serial[128];
    get_serial(serial, sizeof(serial));

    /* 1. 当前内容哈希（it_content / it_bgcolor / it_appinfo） */
    state_t cur;
    state_init(&cur);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db,
            "SELECT it_unique_id, it_content, it_bgcolor, it_appinfo FROM item_table",
            -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *uid = sqlite3_column_text(st, 0);
            const unsigned char *content = sqlite3_column_text(st, 1);
            const unsigned char *bg = sqlite3_column_text(st, 2);
            const unsigned char *app = sqlite3_column_text(st, 3);
            if (!uid) continue;
            char h[65];
            hash3_hex(content ? (char *)content : "",
                      bg ? (char *)bg : "",
                      app ? (char *)app : "", h);
            state_put(&cur, (const char *)uid, h);
        }
        sqlite3_finalize(st);
    }

    /* 2. 读旧快照 */
    state_t prev;
    state_init(&prev);
    int has_state = state_load(&prev, g_state_path);

    /* 3. 计算变更；无快照则全量上传 */
    int changed_count = 0;
    for (int i = 0; i < cur.n; i++) {
        const char *h = state_get(&prev, cur.ents[i].uid);
        if (!has_state || !h || strcmp(h, cur.ents[i].hash) != 0)
            changed_count++;
    }

    if (changed_count == 0) {
        log_msg("无本地变更");
        state_free(&cur);
        state_free(&prev);
        free(cookie);
        sqlite3_close(db);
        return;
    }

    log_msg("检测到 %d 条本地变更%s", changed_count,
            !has_state ? "（首次运行，全量上传）" : "");

    if (dry_run) {
        for (int i = 0; i < cur.n; i++) {
            const char *h = state_get(&prev, cur.ents[i].uid);
            if (!has_state || !h || strcmp(h, cur.ents[i].hash) != 0)
                log_msg("  [dry-run] 将上传 %s", cur.ents[i].uid);
        }
        state_free(&cur);
        state_free(&prev);
        free(cookie);
        sqlite3_close(db);
        return;
    }

    /* 4. 构造上传 body */
    strbuf body;
    sb_init(&body);
    sb_append(&body, "client=macos&notification=server2&session=endsession-macos_");
    char ms[32];
    snprintf(ms, sizeof(ms), "%lld", (long long)(time(NULL) * 1000LL));
    sb_append(&body, ms);

    for (int i = 0; i < cur.n; i++) {
        const char *h = state_get(&prev, cur.ents[i].uid);
        if (has_state && h && strcmp(h, cur.ents[i].hash) == 0)
            continue;  /* 未变更 */

        sqlite3_stmt *q;
        if (sqlite3_prepare_v2(db,
                "SELECT it_cdate, u_id, it_content, it_bgcolor, pj_id, it_mdate, "
                "it_unique_id, it_stime FROM item_table WHERE it_unique_id=?",
                -1, &q, NULL) != SQLITE_OK)
            continue;
        sqlite3_bind_text(q, 1, cur.ents[i].uid, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(q) != SQLITE_ROW) { sqlite3_finalize(q); continue; }

        const char *cdate = (const char *)sqlite3_column_text(q, 0);
        int u_id = sqlite3_column_int(q, 1);
        const char *content = (const char *)sqlite3_column_text(q, 2);
        const char *bgcolor = (const char *)sqlite3_column_text(q, 3);
        int pj_id = sqlite3_column_int(q, 4);
        const char *mdate = (const char *)sqlite3_column_text(q, 5);
        const char *uid = (const char *)sqlite3_column_text(q, 6);
        long stime = sqlite3_column_int64(q, 7);
        if (!cdate) cdate = ""; if (!content) content = "";
        if (!bgcolor) bgcolor = ""; if (!mdate) mdate = ""; if (!uid) uid = "";

        char *enc_content = url_encode(content);

        /* 构造 JSON 条目 */
        strbuf j;
        sb_init(&j);
        sb_append(&j, "{\"sCDate\":\"");
        sb_append(&j, cdate);
        sb_append(&j, "\",\"s_account_id\":");
        char num[32];
        snprintf(num, sizeof(num), "%d", u_id); sb_append(&j, num);
        sb_append(&j, ",\"sDeviceID\":\"");
        sb_append(&j, serial);
        sb_append(&j, "\",\"sContent\":\"");
        sb_append(&j, enc_content);
        sb_append(&j, "\",\"sBgColor\":\"");
        sb_append(&j, bgcolor);
        sb_append(&j, "\",\"lnProjectId\":");
        snprintf(num, sizeof(num), "%d", pj_id); sb_append(&j, num);
        sb_append(&j, ",\"lnItemId\":0,\"lnUserId\":0,\"uType\":0,\"sMDate\":\"");
        sb_append(&j, mdate);
        sb_append(&j, "\",\"sUniqueID\":\"");
        sb_append(&j, uid);
        sb_append(&j, "\",\"nSTime\":");
        snprintf(num, sizeof(num), "%ld", stime); sb_append(&j, num);
        sb_append(&j, "}");
        sqlite3_finalize(q);

        char *enc_json = url_encode(j.data);
        sb_append(&body, "&jdata%5B%5D=");
        sb_append(&body, enc_json);
        free(enc_json);
        free(enc_content);
        sb_free(&j);
    }

    /* 5. 上传 */
    char *resp = NULL;
    int code = http_post(UPLOAD_PATH, body.data, cookie, &resp);
    if (code != 200 || !resp) {
        log_msg("上传失败（HTTP %d）", code);
        sb_free(&body);
        if (resp) free(resp);
        state_free(&cur); state_free(&prev); free(cookie);
        sqlite3_close(db);
        return;
    }

    if (!result_ok(resp)) {
        log_msg("上传失败：服务端返回错误");
        sb_free(&body);
        free(resp);
        state_free(&cur); state_free(&prev); free(cookie);
        sqlite3_close(db);
        return;
    }

    /* 6. 用服务端新 token 更新本地 it_stime */
    long new_token = parse_timestamp(resp);
    log_msg("上传成功，服务端 token=%ld", new_token);
    if (new_token > 0) {
        for (int i = 0; i < cur.n; i++) {
            const char *h = state_get(&prev, cur.ents[i].uid);
            if (has_state && h && strcmp(h, cur.ents[i].hash) == 0)
                continue;
            sqlite3_stmt *u;
            if (sqlite3_prepare_v2(db, "UPDATE item_table SET it_stime=? WHERE it_unique_id=?",
                                   -1, &u, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(u, 1, new_token);
                sqlite3_bind_text(u, 2, cur.ents[i].uid, -1, SQLITE_TRANSIENT);
                sqlite3_step(u);
                sqlite3_finalize(u);
            }
        }
    }

    /* 7. 保存新快照 */
    state_save(&cur, g_state_path);
    log_msg("同步完成");

    free(resp);
    sb_free(&body);
    state_free(&cur);
    state_free(&prev);
    free(cookie);
    sqlite3_close(db);
}

/* ---------------- 库接口（供 Swift 调用） ---------------- */
void sync_cal_init(void) {
    expand_tilde("~/Library/Containers/com.xdiarys.www/Data/desktopcal.sqlite",
                 g_db_path, sizeof(g_db_path));
    expand_tilde("~/Library/Application Support/toolbox/calendar_state.txt",
                 g_state_path, sizeof(g_state_path));
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

/* 执行一次同步：读库、比对快照、上传变更。内部已做「无变更即跳过」。 */
int sync_cal_do_sync(void) {
    do_sync(0);
    return 0;
}
