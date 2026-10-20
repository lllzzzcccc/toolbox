#define __USE_MINGW_ANSI_STDIO 1   /* 让 MinGW 的 printf 支持 %lld（C99） */

/*
 * sync_cal_win.c — 日历清单（Desktop Calendar / desktopcal.exe）Windows 自动上传客户端
 *
 * 与 Mac 版 sync_cal.c 逻辑一致：监控本地 calendar.db 的 data 类型（每天一格清单）变更，
 * 变更后把本地改动上传到 https://data.xdiarys.com。
 * 下载方向（服务端 → 本地）由 App 自身处理，本程序只做上传。
 *
 * 与 Mac 版的差异：
 *   - DB：%APPDATA%\CalendarTask\Db\calendar.db（SQLite，UTF-16 编码，sqlite3 自动转换）
 *   - cookie：setting_table.st_name='user_usercookie'（Mac 是 setting.login_key_user_cookie）
 *   - 协议：client=pc（Mac 是 client=macos）、UA "XDiarys-PC, <版本>"、
 *           session "endsession-pc_<毫秒>"
 *   - 设备 ID：Windows 无序列号，用 MachineGuid（HKLM\...\Cryptography\MachineGuid）
 *   - HTTP：WinHTTP（系统自带，无 libcurl 依赖）；SHA-256：OpenSSL libcrypto
 *
 * 编译（见 windows/build.bat）：
 *   gcc -O2 sync_cal_win.c -o sync-cal.exe \
 *     -I"D:/Program Files/mingw64/opt/include" \
 *     "D:/Program Files/mingw64/opt/lib/libsqlite3.a" \
 *     "D:/Program Files/mingw64/opt/lib/libcrypto.a" \
 *     -lwinhttp -lcrypt32 -ladvapi32
 *
 * 用法：
 *   sync-cal.exe --once       检查并上传一次
 *   sync-cal.exe --watch      监控 DB 变更自动上传（常驻）
 *   sync-cal.exe --dry-run    只打印将上传什么（不发请求）
 */

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>

#include <sqlite3.h>
#include <openssl/sha.h>

#define BASE        "https://data.xdiarys.com"
#define BASE_HOST   L"data.xdiarys.com"
#define UPLOAD_PATH "api/data/v1/upload"
#define INFO_PATH   "api/sync/server/info"
#define PATH_SZ     1024

/* 全局路径（启动时确定） */
static char g_db_path[PATH_SZ];
static char g_state_path[PATH_SZ];
static char g_log_path[PATH_SZ];
static char g_ua[256];           /* "XDiarys-PC, <版本>" */
static FILE *g_log_fp = NULL;

/* ---------------- 日志（stdout + 可选文件） ---------------- */
static void log_msg(const char *fmt, ...) {
    char msg[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    printf("[sync-cal] %s\n", msg);
    fflush(stdout);

    if (g_log_fp) {
        time_t t = time(NULL);
        struct tm *tmv = localtime(&t);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tmv);
        fprintf(g_log_fp, "[%s] %s\n", ts, msg);
        fflush(g_log_fp);
    }
}

/* ---------------- UTF-8 → wide（WinHTTP 用） ---------------- */
static wchar_t *utf8_to_wide(const char *s) {
    if (!s) s = "";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = malloc(n * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
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

/* ---------------- SHA-256 十六进制（OpenSSL） ---------------- */
static void sha256_hex(const unsigned char *data, size_t len, char out[65]) {
    unsigned char d[SHA256_DIGEST_LENGTH];
    SHA256(data, len, d);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(out + 2 * i, "%02x", d[i]);
    out[64] = '\0';
}

/* 流式哈希三个字段（\1 分隔），避免固定缓冲区截断导致长内容漏同步 */
static void hash3_hex(const char *a, const char *b, const char *c, char out[65]) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, a, strlen(a));
    SHA256_Update(&ctx, "\1", 1);
    SHA256_Update(&ctx, b, strlen(b));
    SHA256_Update(&ctx, "\1", 1);
    SHA256_Update(&ctx, c, strlen(c));
    unsigned char d[SHA256_DIGEST_LENGTH];
    SHA256_Final(d, &ctx);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(out + 2 * i, "%02x", d[i]);
    out[64] = '\0';
}

/* ---------------- 设备 ID（Windows：MachineGuid，稳定唯一） ----------------
 * Mac 版用序列号；Windows 用系统机器 GUID（重装系统才变）。
 * 仅用于云端「最后修改设备」标记，不参与鉴权/去重，换别的稳定值也可。 */
static void get_device_id(char *out, size_t outsz) {
    out[0] = '\0';
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ, &hk) == ERROR_SUCCESS) {
        wchar_t val[128];
        DWORD len = sizeof(val);
        if (RegQueryValueExW(hk, L"MachineGuid", NULL, NULL, (LPBYTE)val, &len) == ERROR_SUCCESS)
            WideCharToMultiByte(CP_UTF8, 0, val, -1, out, (int)outsz, NULL, NULL);
        RegCloseKey(hk);
    }
    if (out[0] == '\0')
        snprintf(out, outsz, "unknown-device");
}

/* ---------------- 读取 setting_table ---------------- */
static char *read_setting_str(sqlite3 *db, const char *name) {
    sqlite3_stmt *st;
    char *result = NULL;
    if (sqlite3_prepare_v2(db, "SELECT st_sval FROM setting_table WHERE st_name=?",
                           -1, &st, NULL) == SQLITE_OK) {
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

/* ---------------- HTTP POST（WinHTTP） ---------------- */
typedef struct { char *data; size_t len; size_t cap; } resp_buf;

static void rb_append(resp_buf *b, const char *p, size_t n) {
    while (b->len + n + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4096;
        char *q = realloc(b->data, b->cap);
        if (!q) return;  /* OOM：放弃本次追加 */
        b->data = q;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = '\0';
}

/* 返回 HTTP 状态码；-1=网络错误。resp_out 存响应体（调用方 free）。 */
static int http_post(const char *path, const char *body, const char *cookie, char **resp_out) {
    if (resp_out) *resp_out = NULL;

    wchar_t *ua_w = utf8_to_wide(g_ua);
    HINTERNET hSession = WinHttpOpen(ua_w, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    free(ua_w);
    if (!hSession) return -1;

    WinHttpSetTimeouts(hSession, 30000, 30000, 30000, 30000);

    HINTERNET hConnect = WinHttpConnect(hSession, BASE_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return -1; }

    wchar_t *path_w = utf8_to_wide(path);
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"POST", path_w, NULL,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        WINHTTP_FLAG_SECURE);
    free(path_w);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return -1; }

    /* 组装请求头（UTF-8 → wide）。cookie 本身是 ASCII。
     * 用 _snwprintf_s（签名固定，不依赖 __USE_MINGW_ANSI_STDIO），超长时截断而非崩溃。 */
    wchar_t *cookie_w = utf8_to_wide(cookie);
    wchar_t headers[16384];
    _snwprintf_s(headers, sizeof(headers) / sizeof(wchar_t), _TRUNCATE,
                 L"Content-Type: application/x-www-form-urlencoded; charset=utf-8\r\n"
                 L"Cookie: %ls\r\n"
                 L"Accept: application/json\r\n",
                 cookie_w);
    free(cookie_w);

    WinHttpAddRequestHeaders(hReq, headers, -1L, WINHTTP_ADDREQ_FLAG_ADD);

    DWORD body_len = (DWORD)strlen(body);
    BOOL ok = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 (LPVOID)body, body_len, body_len, 0);
    if (!ok) { WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return -1; }

    ok = WinHttpReceiveResponse(hReq, NULL);
    if (!ok) { WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return -1; }

    DWORD status = 0, status_size = sizeof(status);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);

    resp_buf buf = {0};
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        char tmp[4096];
        DWORD nread = 0;
        if (avail > sizeof(tmp)) avail = sizeof(tmp);
        if (!WinHttpReadData(hReq, tmp, avail, &nread) || nread == 0) break;
        rb_append(&buf, tmp, nread);
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (resp_out) *resp_out = buf.data;
    else free(buf.data);
    return (int)status;
}

/* 从 JSON 里提取第一个 "timestamp":<数字> */
static long long parse_timestamp(const char *json) {
    const char *p = strstr(json, "\"timestamp\":");
    if (!p) return 0;
    p += strlen("\"timestamp\":");
    return atoll(p);
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

    char *cookie = read_setting_str(db, "user_usercookie");
    if (!cookie || !*cookie) {
        log_msg("未找到 user_usercookie（未登录？）");
        if (cookie) free(cookie);
        sqlite3_close(db);
        return;
    }

    char device_id[128];
    get_device_id(device_id, sizeof(device_id));

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
    sb_append(&body, "client=pc&notification=server2&session=endsession-pc_");
    FILETIME ft; ULARGE_INTEGER ull;
    GetSystemTimeAsFileTime(&ft);
    ull.LowPart = ft.dwLowDateTime; ull.HighPart = ft.dwHighDateTime;
    long long ms = (long long)((ull.QuadPart - 116444736000000000LL) / 10000);
    char msbuf[32];
    snprintf(msbuf, sizeof(msbuf), "%lld", ms);
    sb_append(&body, msbuf);

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
        sqlite3_int64 stime = sqlite3_column_int64(q, 7);
        if (!cdate) cdate = "";
        if (!content) content = "";
        if (!bgcolor) bgcolor = "";
        if (!mdate) mdate = "";
        if (!uid) uid = "";

        char *enc_content = url_encode(content);

        /* 构造 JSON 条目（与 Mac 版一致） */
        strbuf j;
        sb_init(&j);
        sb_append(&j, "{\"sCDate\":\"");
        sb_append(&j, cdate);
        sb_append(&j, "\",\"s_account_id\":");
        char num[32];
        snprintf(num, sizeof(num), "%d", u_id); sb_append(&j, num);
        sb_append(&j, ",\"sDeviceID\":\"");
        sb_append(&j, device_id);
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
        snprintf(num, sizeof(num), "%lld", stime); sb_append(&j, num);
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
        log_msg("上传失败：服务端返回错误: %.400s", resp);
        sb_free(&body);
        free(resp);
        state_free(&cur); state_free(&prev); free(cookie);
        sqlite3_close(db);
        return;
    }

    /* 6. 用服务端新 token 更新本地 it_stime */
    long long new_token = parse_timestamp(resp);
    log_msg("上传成功，服务端 token=%lld", new_token);
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

/* ---------------- 探测（只读，无副作用） ----------------
 * POST /api/sync/server/info，验证 HTTPS/WinHTTP/cookie/client=pc 链路是否通。 */
static void do_probe(void) {
    sqlite3 *db = NULL;
    if (sqlite3_open(g_db_path, &db) != SQLITE_OK) {
        log_msg("打开 DB 失败: %s", db ? sqlite3_errmsg(db) : "unknown");
        if (db) sqlite3_close(db);
        return;
    }
    sqlite3_busy_timeout(db, 15000);
    char *cookie = read_setting_str(db, "user_usercookie");
    if (!cookie || !*cookie) {
        log_msg("未找到 user_usercookie（未登录？）");
        if (cookie) free(cookie);
        sqlite3_close(db);
        return;
    }
    char *resp = NULL;
    int code = http_post(INFO_PATH, "client=pc", cookie, &resp);
    log_msg("server/info → HTTP %d，响应: %s", code, resp ? resp : "(空)");
    free(resp);
    free(cookie);
    sqlite3_close(db);
}

/* ---------------- 监控 ---------------- */
static void watch(int interval, int debounce) {
    if (interval < 1) interval = 1;   /* 防止 Sleep(0) 退化热循环 */
    if (debounce < 0) debounce = 0;
    log_msg("开始监控 %s（间隔 %ds，去抖 %ds）", g_db_path, interval, debounce);

    /* 启动先做一次同步：首次运行建立基线（全量上传），之后才走增量。
     * （比 Mac 版多这一步，装好服务即可用，无需手动先跑 --once。） */
    do_sync(0);

    struct stat st;
    if (stat(g_db_path, &st) != 0) {
        log_msg("无法访问 DB 文件");
        return;
    }
    time_t last = st.st_mtime;
    time_t pending = 0;

    for (;;) {
        Sleep((DWORD)interval * 1000);
        if (stat(g_db_path, &st) != 0)
            continue;
        if (st.st_mtime != last) {
            last = st.st_mtime;
            pending = time(NULL);
        }
        if (pending && (time(NULL) - pending) >= debounce) {
            pending = 0;
            log_msg("检测到数据变更，触发上传…");
            do_sync(0);
        }
    }
}

/* ---------------- 路径展开（Windows 环境变量） ---------------- */
static void path_from_env(char *out, size_t outsz, const char *env, const char *suffix) {
    const char *base = getenv(env);
    if (base) snprintf(out, outsz, "%s\\%s", base, suffix);
    else snprintf(out, outsz, "%s", suffix);
}

/* 解析数据根目录：先看 %APPDATA%\CalendarTask\config.ini 的 [inst] path= 重定向，
 * 没有则默认 %APPDATA%\CalendarTask。DB 在其 Db\calendar.db 下。 */
static void resolve_data_root(char *root, size_t rootsz) {
    path_from_env(root, rootsz, "APPDATA", "CalendarTask");   /* 默认 */

    char cfg[PATH_SZ];
    path_from_env(cfg, sizeof(cfg), "APPDATA", "CalendarTask\\config.ini");
    FILE *f = fopen(cfg, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, "path=", 5) != 0) continue;
        char *v = s + 5;
        char *e = v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) e--;
        *e = '\0';
        if (*v) snprintf(root, rootsz, "%s", v);
        break;
    }
    fclose(f);
}

/* 确保文件路径的父目录存在（递归创建单层；已存在则忽略）。 */
static void ensure_parent_dir(const char *filepath) {
    char dir[PATH_SZ];
    snprintf(dir, sizeof(dir), "%s", filepath);
    char *p = strrchr(dir, '\\');
    if (!p) return;
    *p = '\0';
    wchar_t *w = utf8_to_wide(dir);
    CreateDirectoryW(w, NULL);
    free(w);
}

/* ---------------- 用法 ---------------- */
static void usage(void) {
    printf(
        "sync_cal_win — 日历清单（Desktop Calendar）自动上传客户端\n"
        "用法:\n"
        "  sync-cal.exe --once             检查并上传一次\n"
        "  sync-cal.exe --watch            监控 DB 变更自动上传（常驻）\n"
        "  sync-cal.exe --hidden           （配合 --watch）静默后台运行，不弹控制台窗口\n"
        "  sync-cal.exe --dry-run          只打印将上传什么（不发请求）\n"
        "  sync-cal.exe --probe            探测 server/info（只读，验证链路）\n"
        "  --interval N                    监控轮询间隔秒（默认 2）\n"
        "  --debounce N                    变更去抖秒（默认 3）\n"
        "  --db PATH                       覆盖 DB 路径（默认 %%APPDATA%%\\CalendarTask\\Db\\calendar.db）\n"
        "  --state PATH                    覆盖快照路径\n"
        "  --log PATH                      覆盖日志路径\n");
}

/* ---------------- main ---------------- */
int main(int argc, char **argv) {
    /* --hidden：静默后台运行（计划任务用），立即脱离控制台、关闭弹出的窗口 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hidden") == 0) { FreeConsole(); break; }
    }

    /* 让 printf 的 UTF-8 中文在 Windows 控制台不乱码 */
    SetConsoleOutputCP(CP_UTF8);

    {
        char root[PATH_SZ];
        resolve_data_root(root, sizeof(root));
        snprintf(g_db_path, sizeof(g_db_path), "%s\\Db\\calendar.db", root);
    }
    path_from_env(g_state_path, sizeof(g_state_path), "LOCALAPPDATA", "sync_cal\\state.txt");
    path_from_env(g_log_path, sizeof(g_log_path), "LOCALAPPDATA", "sync_cal\\sync_cal.log");
    snprintf(g_ua, sizeof(g_ua), "XDiarys-PC, 3.32.310.9371");

    /* 确保 state/log 所在目录存在（直接 --once/--watch 而不经安装脚本时） */
    ensure_parent_dir(g_state_path);
    ensure_parent_dir(g_log_path);

    int watch_mode = 0, dry_run = 0, probe = 0;
    int interval = 2, debounce = 3;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0) {}
        else if (strcmp(argv[i], "--watch") == 0) watch_mode = 1;
        else if (strcmp(argv[i], "--dry-run") == 0) dry_run = 1;
        else if (strcmp(argv[i], "--probe") == 0) probe = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(); return 0; }
        else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) interval = atoi(argv[++i]);
        else if (strcmp(argv[i], "--debounce") == 0 && i + 1 < argc) debounce = atoi(argv[++i]);
        else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) snprintf(g_db_path, sizeof(g_db_path), "%s", argv[++i]);
        else if (strcmp(argv[i], "--state") == 0 && i + 1 < argc) snprintf(g_state_path, sizeof(g_state_path), "%s", argv[++i]);
        else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) snprintf(g_log_path, sizeof(g_log_path), "%s", argv[++i]);
    }

    /* 打开日志文件（追加） */
    g_log_fp = fopen(g_log_path, "a");

    log_msg("启动：DB=%s 状态=%s UA=%s", g_db_path, g_state_path, g_ua);

    if (watch_mode)
        watch(interval, debounce);
    else if (probe)
        do_probe();
    else
        do_sync(dry_run);

    if (g_log_fp) fclose(g_log_fp);
    return 0;
}
