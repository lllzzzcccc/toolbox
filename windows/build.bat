@echo off
setlocal
REM 编译 Windows 自动上传客户端 sync-cal.exe
REM 依赖：MinGW-w64（gcc），及 opt 目录下的静态 libsqlite3.a / libcrypto.a
REM 若 MinGW 装在别处，改下面两个变量即可。

set "GCC=D:\Program Files\mingw64\bin\gcc.exe"
set "OPT=D:\Program Files\mingw64\opt"

if not exist "%GCC%" (
    echo [错误] 找不到 gcc: %GCC%
    exit /b 1
)

"%GCC%" -O2 -Wall sync_cal_win.c -o sync-cal.exe ^
  -I"%OPT%\include" ^
  "%OPT%\lib\libsqlite3.a" ^
  "%OPT%\lib\libcrypto.a" ^
  -lwinhttp -lcrypt32 -ladvapi32

if errorlevel 1 (
    echo [失败] 编译出错
    exit /b 1
)

echo [成功] 已生成 sync-cal.exe
endlocal
