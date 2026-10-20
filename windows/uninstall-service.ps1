# uninstall-service.ps1 — 卸载 sync-cal 常驻服务（计划任务）
# 用法：powershell -ExecutionPolicy Bypass -File uninstall-service.ps1

$ErrorActionPreference = "Stop"
$TaskName = "sync_cal"

$task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($task) {
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    Write-Host "已删除计划任务 '$TaskName'"
} else {
    Write-Host "未找到计划任务 '$TaskName'（可能已卸载）"
}

$InstallDir = "$env:LOCALAPPDATA\sync_cal"
if (Test-Path -LiteralPath $InstallDir) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
    Write-Host "已删除 $InstallDir"
}
Write-Host "卸载完成。"
