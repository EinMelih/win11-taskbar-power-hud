# =============================================================================
# Win11 Taskbar Power & Telemetry HUD - Build & Autostart Installer
# Compiles `src/taskbar-power-hud.wh.cpp` into `TaskbarPowerHud.exe`
# and registers an interactive Windows Logon Scheduled Task + Run Registry Key.
# =============================================================================
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$srcFile  = Join-Path $repoRoot "src\taskbar-power-hud.wh.cpp"
$outExe   = Join-Path $repoRoot "TaskbarPowerHud.exe"

$clang = "C:\Program Files\Windhawk\Compiler\bin\clang++.exe"
if (-not (Test-Path $clang)) {
    $cmd = Get-Command clang++ -ErrorAction SilentlyContinue
    if ($cmd) { $clang = $cmd.Source }
    else { throw "clang++.exe not found! Install Windhawk or LLVM/MinGW-w64 clang++." }
}

Stop-Process -Name "TaskbarPowerHud" -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

Write-Host "[1/2] Compiling TaskbarPowerHud.exe..." -ForegroundColor Cyan
& $clang --target=x86_64-w64-mingw32 -std=c++23 -O2 -mwindows -municode -static $srcFile -lgdi32 -luser32 -lshell32 -lgdiplus -lole32 -lcomdlg32 -lpdh -lpsapi -liphlpapi -ldwmapi -lshlwapi -ladvapi32 -luuid -o $outExe
if ($LASTEXITCODE -ne 0) { throw "Compilation failed with exit code $LASTEXITCODE" }

Write-Host "[2/2] Registering Windows Autostart & Starting HUD..." -ForegroundColor Cyan
$act  = New-ScheduledTaskAction -Execute $outExe -WorkingDirectory $repoRoot
$trig = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$prin = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited
$set  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero)
Register-ScheduledTask -TaskName "TaskbarPowerHud" -Action $act -Trigger $trig -Principal $prin -Settings $set -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "TaskbarPowerHud" -Value "`"$outExe`"" -Force
Start-ScheduledTask -TaskName "TaskbarPowerHud"
Write-Host "Done! TaskbarPowerHud.exe is now live in your Windows 11 taskbar." -ForegroundColor Green
