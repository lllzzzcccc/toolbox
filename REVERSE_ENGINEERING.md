# CalendarTask（日历清单）同步协议逆向笔记

> 逆向 `CalendarTask` 同步协议的过程记录，供自制客户端（`sync_cal.py`）参考。
> 目标：用脚本替代「手动点免费同步按钮」，在本地数据变更后自动同步。

## 0. 目标软件身份

- App：**CalendarTask**（App Store 显示名「日历清单」），bundle `com.xdiarys.www`，版本 1.79 (build 190)
- 二进制：`/Applications/CalendarTask.app/Contents/MacOS/CalendarTask`（Swift + Alamofire + GRDB + CryptoSwift + HandyJSON）
- 数据库：`~/Library/Containers/com.xdiarys.www/Data/desktopcal.sqlite`

## 1. 关键结论：`v2sign` 不是客户端生成的签名

原文档把 `v2sign` 当作「客户端每次请求前生成、服务端校验」的签名，并列为硬骨头。**这是误读。** 证据：

1. `v2sign`/`v2token` 两个字面量在 17MB 主程序里**不存在**（若客户端要按名读写/计算它们，字面量必然出现）。
2. 它们只是**登录时服务端下发的 cookie** 的一部分（32 位 hex），存在 `setting.login_key_user_cookie`（327 字符），客户端原样回传。
3. **鉴权 = 直接回传登录 cookie。没有 per-request 签名。** 同步 URL 不带 query string，身份在 cookie 的 `dcumid` 里。

CryptoSwift 的 AES/MD5/HMAC/SHA1 是登录时加密账号凭据用的，与「已登录状态的同步」无关。

## 2. 数据库 schema

| 表 | 用途 | 关键字段 |
|---|---|---|
| `setting` | 配置+鉴权+游标 | `login_key_user_cookie`(cookie)、`login_key_user_uid`、`last_sync_{data,event,note}_timespan`(各类型游标，nval)、`last_sync_time` |
| `item_table` | data = 每天一格清单 | `it_unique_id`(`dkcal_mdays_YYYYMMDD`)、`it_content`、`it_stime`(=服务端 cd_mdtoken)、`it_mtimex`、`it_bgcolor`、`it_appinfo`、`u_mid` |
| `event_table` | event = 日程 | `ev_unique_id`(`event-...`)、`ev_mid`(服务端40位id)、`ev_stime`、`ev_recurrence`、`ev_reminder` |
| `note_table` | note = 便签 | `nt_unique_id`(`note_...`)、`nt_mid`、`nt_stime`、`nt_content`、`nt_bgcolor` |
| `subs_table` | 订阅 | （未用，0 行） |

- `u_mid` = `login_key_user_uid` = cookie 的 `dcumid`，都是 38 位 hex 用户 id。
- `*_stime`（本地）↔ `cd_mdtoken`（服务端）是增量比对关键。
- journal 模式是 `delete`（非 WAL），外部写 DB 需 `busy_timeout` 重试。

## 3. 接口面（已从二进制字符串表确认）

- 基址：`https://data.xdiarys.com`（同步端点 URL **不带 query string**）
- 统一响应信封：`{"result":1,"errorid":0,"errordesc":"","vdata":...}`（result=1 成功）
- 所有同步请求：`POST` + `Content-Type: application/x-www-form-urlencoded; charset=utf-8` + `Cookie` 头
- `User-Agent: XDiarys-macOS, 1.79.190`，`sDeviceID` = Mac 序列号（IOPlatformSerialNumber）

| 端点 | 方法 | 用途 |
|---|---|---|
| `/api/sync/server/info` | POST | 服务器信息 |
| `/api/sync/v2/index/{data,event,note 或逗号合并}` | POST | 增量索引 |
| `/api/data/v2/download` / `/api/data/v1/upload` | POST | data 下载/上传 |
| `/api/event/v1/download` / `upload` | POST | event 下载/上传 |
| `/api/note/download` / `upload` | POST | note 下载/上传 |
| `/api/{type}/delhistory` | POST | 删除 |

## 4. 完整协议（实测抓包，全部验证）✅

### 4.1 server/info

```
POST /api/sync/server/info
body: client=macos
→ vdata: {"ver":1000,"sync":1,"srvno":0,"list":[]}
```

### 4.2 index（增量索引）

```
POST /api/sync/v2/index/data,event,note
body: client=macos&data_timestamp=<游标>&event_timestamp=<游标>&isVip=0&note_timestamp=<游标>
```
（也可单类型：`/api/sync/v2/index/data`，body 只带 `client&data_timestamp&isVip`）

响应（只返回 `cd_mdtoken >= 游标` 的条目）：
```json
{"result":1,"vdata":{
  "timestamp":1792331992,
  "event":{"count":0,"list":[],"data_limited":false},
  "data":{"count":2,"list":[
    {"cd_unique_id":"dkcal_mdays_20261018","cd_cdate":"...","cd_mdtoken":1792331698,"s_account_id":0},
    ...
  ],"data_limited":false},
  "note":{"count":0,"list":[],"data_limited":false}}}
```
- 游标 `last_sync_*_timespan` = 上次响应的 `vdata.timestamp`（服务端时钟）。
- 增量比对在客户端本地做：`cd_mdtoken` vs 本地 `*_stime`。

### 4.3 download（data 类型）

```
POST /api/data/v2/download
body: client=macos&history=0&jid[]=<cd_unique_id>-<s_account_id>   (每个要下载的条目一个 jid[])
```

响应：
```json
{"result":1,"vdata":{"count":1,"list":[{
  "cd_mid":"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
  "cd_unique_id":"dkcal_mdays_20261021",
  "cd_data_curr":{"it_unique_id":"dkcal_mdays_20261021",
    "json_data":{"sDeviceID":"...","uType":0,"sUniqueID":"...","s_account_id":0,
      "lnItemId":0,"lnUserId":0,"lnProjectId":0,
      "sContent":"%E6%96%B0%E5%87%AF...",   ← URL 编码！
      "sBgColor":"","sAppInfo":"","sCDate":"","sMDate":"...","nSTime":1792331991}},
  "cd_data_full":null,
  "cd_cdate":"...","cd_mdtoken":1792331991,"s_account_id":0}]}}
```
- 写入本地：`it_content = unquote(json_data.sContent)`，`it_stime = cd_mdtoken`，其余字段直接映射。

### 4.4 upload（data 类型）

```
POST /api/data/v1/upload
body: client=macos&jdata[]=<JSON条目>&notification=server2&session=endsession-macos_<毫秒时间戳>
```
（每个要上传的条目一个 `jdata[]`，值为 JSON 字符串）

`jdata[]` 条目 JSON：
```json
{"sCDate":"","s_account_id":0,"sDeviceID":"XXXXXXXXXXX",
 "sContent":"%E6%B5%8B%E8%AF%95...",   ← URL 编码！
 "sBgColor":"","lnProjectId":0,"lnItemId":0,"lnUserId":0,"uType":0,
 "sMDate":"","sUniqueID":"dkcal_mdays_20261018","nSTime":1792307908}
```
- `sContent = quote(it_content)`（服务端存的是 URL 编码串）。
- `nSTime` = 本地 `it_stime`（上传时带旧值，服务端重新分配 cd_mdtoken）。

响应（服务端分配新 cd_mdtoken）：
```json
{"result":1,"vdata":{"timestamp":1792331698,"count":2,"list":[
  {"cd_unique_id":"dkcal_mdays_20261021","cd_mdtoken":1792331698,"u_id":0,"pj_id":0},
  ...],"s_account_id":0}}
```

## 5. 字段名（来自二进制 __cstring，0x6305e2 起）

```
mUploadList
cd_unique_id  cd_cdate  cd_mdtoken  cd_mid  s_account_id  group_id
umid  uname  sDeviceID  uType
sUniqueID  lnItemId  lnUserId  lnProjectId
sBgColor  sContent  sAppInfo  sCDate  sMDate  nSTime
editorInfo  timestamp  has_more  next_timestamp  list
sync  srvno  it_unique_id  json_data  cd_data_curr  cd_data_full
u_id  pj_id  result  errorid
```
- data 条目字段：`sUniqueID`/`cd_unique_id`、`sBgColor`、`sContent`、`sAppInfo`、`sCDate`/`sMDate`、`nSTime`、`cd_mid`、`umid`、`lnItemId`/`lnUserId`/`lnProjectId`、`sDeviceID`、`uType`
- 分页字段（download 响应）：`list`、`has_more`、`next_timestamp`、`timestamp`、`cd_data_curr`/`cd_data_full`
- event 字段：`e_mid`、`e_contents`、`e_date`、`e_start`、`e_end`、`e_type`、`e_status`、`e_recurrence`、`e_reminder`、`e_reminder_v2`、`e_subs`、`e_info`、`e_text`、`e_id`、`e_cid`、`e_last_time`
- note 字段：`n_type`
- 表单字段：`client`、`isVip`、`history`、`jid[]`、`jdata[]`、`notification`、`session`

## 6. 关键结论（供实现）

1. **无签名**：cookie 回传即可鉴权。
2. **表单编码**：所有同步 body 是 `application/x-www-form-urlencoded`，不是 JSON。
3. **内容双重编码**：`sContent` 在 JSON 里是 URL 编码的（上传 `quote`、下载 `unquote`）。
4. **增量**：index 用 `*_timestamp` 游标做服务端过滤；客户端本地再比 `cd_mdtoken` vs `*_stime` 决定 download/upload。
5. **变更检测**：App 的「哪些本地条目要上传」是内存脏标记，DB 里没有持久标志；客户端用「内容哈希快照比对」实现（见 `sync_cal.py`）。

## 7. 已交付

- `sync_cal.py`：完整 data 类型同步（index + download + upload + 游标）+ `--watch` 监控 + `--dry-run`。
- `com.sync-cal.plist`：launchd 常驻配置。
- event/note 的 download/upload 字段映射已依本节字段名推断，但未逐一抓包验证，暂未启用（客户端只推进其游标）。
