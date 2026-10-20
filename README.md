# Toolbox — macOS 小工具平台

一个菜单栏小平台，把零零散散的 macOS 小工具集成到一处：侧边栏 + 主页面 + 按需开关。

- **原生 Swift + SwiftUI**，编译成独立 `.app`，无 Electron / 无解释器依赖
- **无需 Xcode**：SwiftPM 编译 + 脚本打包（见下文「构建」）
- 每个小工具是一个 `Tool` 子类，开关控制启停、持久化到 UserDefaults

## 当前工具

| 工具 | 说明 |
|---|---|
| 系统监控 | 菜单栏显示 CPU / 内存占用（每 2 秒采样，Mach API） |
| 日历同步 | 监控「日历清单」DB 变更，自动上传到 data.xdiarys.com |

## 目录结构

```
Sources/
  CSysInfo/        C 库：CPU/内存采样（Mach API）
  CSyncCal/        C 库：日历同步（复用原 sync_cal 代码）
  Toolbox/         SwiftUI 平台（工具基类 + 各工具 + 主界面 + 设置）
Scripts/make_app.sh   无 Xcode 打包脚本
windows/           Windows 版日历同步工具（独立，见下文）
REVERSE_ENGINEERING.md  日历同步协议逆向笔记
```

## 构建 & 运行

依赖：macOS 14+、Swift 工具链（CommandLineTools 自带 `swift`/`clang`）。

```bash
# 编译 + 打包成 Toolbox.app
./Scripts/make_app.sh

# 运行
open Toolbox.app
```

> 打包脚本做的事：`swift build -c release` → 把可执行文件裹成 `.app`（写 Info.plist + ad-hoc 签名）。

## 使用

- **菜单栏**：显示系统监控摘要（CPU/内存），下拉可「打开主界面」、开关各工具、退出。
- **主界面**：左侧边栏列工具（带开关），右侧是选中工具的详情；底部「设置」。
- **设置 → 开机自启**：用系统 `SMAppService` 注册登录项（登录 macOS 自动启动 Toolbox）。

## 新增一个小工具

1. 新建 `Tool` 子类（实现 `makeMainView()`，重写 `setEnabled` 做启停逻辑）
2. 在 `ToolboxApp.swift` 的 `ToolRegistry(tools: [...])` 里加一行注册

## Windows 版日历同步工具

`windows/` 目录是**独立的 Windows 版日历同步工具**（与 macOS 版功能一致，逻辑复用）：

- `sync_cal_win.c` — C 源码（WinHTTP + OpenSSL，无 libcurl 依赖）
- `build.bat` — 编译脚本（MinGW gcc）
- `install-service.ps1` / `uninstall-service.ps1` — 安装/卸载为 Windows 服务

它与 macOS 版的关键差异（DB 路径、cookie 字段名、`client=pc`、设备 ID 用 MachineGuid 等）都写在 `sync_cal_win.c` 头注释里。

## 协议笔记

日历同步的完整协议（鉴权 = 回传 cookie、表单编码、`jdata[]`/`jid[]`、`sContent` URL 编码等）见 `REVERSE_ENGINEERING.md`。
