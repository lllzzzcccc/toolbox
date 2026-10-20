# install-service.ps1 — 安装 sync-cal 为「登录自启 + 崩溃自动重启」的常驻服务
#
# 等价于 Mac 版的 launchd（com.sync-cal.plist）：
#   - 登录自启（计划任务 AtLogOn 触发器）
#   - 崩溃自动重启（RestartCount/RestartInterval）
#   - 无时间限制常驻
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File install-service.ps1
#   （可选参数 -Exe 指定编译产物路径，默认用本脚本同目录的 sync-cal.exe）

param(
    [string]$Exe = "$PSScriptRoot\sync-cal.exe",
    [string]$InstallDir = "$env:LOCALAPPDATA\sync_cal"
)

$ErrorActionPreference = "Stop"
$TaskName = "sync_cal"

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "找不到可执行文件: $Exe  （请先运行 build.bat）"
}

# 1. 复制到安装目录（此后可随意移动源码仓库，不影响服务）
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$Target = Join-Path $InstallDir "sync-cal.exe"
Copy-Item -LiteralPath $Exe -Destination $Target -Force
Write-Host "[1/3] 已复制到 $Target"

# 2. 注册计划任务（登录自启 + 崩溃重启）
$User = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$Action   = New-ScheduledTaskAction   -Execute $Target -Argument "--watch --hidden"
$Trigger  = New-ScheduledTaskTrigger  -AtLogOn -User $User
$Principal = New-ScheduledTaskPrincipal -UserId $User -LogonType Interactive -RunLevel Limited
$Settings = New-ScheduledTaskSettingsSet `
    -StartWhenAvailable `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -ExecutionTimeLimit ([TimeSpan]::Zero)

Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger `
    -Principal $Principal -Settings $Settings -Force | Out-Null
Write-Host "[2/3] 已注册计划任务 '$TaskName'（登录自启 + 崩溃重启）"

# 3. 立即启动一次（首次运行会全量上传建立基线）
Start-ScheduledTask -TaskName $TaskName
Write-Host "[3/3] 已启动"

Write-Host ""
Write-Host "完成。日志: $InstallDir\sync_cal.log"
Write-Host "常用命令:"
Write-Host "  查看状态   Get-ScheduledTask $TaskName | Select State"
Write-Host "  停止       Stop-ScheduledTask $TaskName"
Write-Host "  启动       Start-ScheduledTask $TaskName"
Write-Host "  卸载       powershell -ExecutionPolicy Bypass -File uninstall-service.ps1"
