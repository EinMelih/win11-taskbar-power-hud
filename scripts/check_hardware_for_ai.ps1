# =============================================================================
# Win11 Taskbar Power & Telemetry HUD - AI Hardware & Sensor Compatibility Scanner
# Run this script so your AI Agent (Antigravity, Cursor, Windsurf, ChatGPT)
# can inspect your exact hardware and adapt `src/taskbar-power-hud.wh.cpp`.
# =============================================================================
$ErrorActionPreference = "SilentlyContinue"

Write-Output "=== 1. OPERATING SYSTEM & DISPLAY DPI ==="
$os = Get-CimInstance Win32_OperatingSystem
Write-Output "OS: $($os.Caption) (Build $($os.BuildNumber))"

Add-Type -AssemblyName System.Windows.Forms
$screen = [System.Windows.Forms.Screen]::PrimaryScreen
Write-Output "Primary Screen Bounds: $($screen.Bounds.Width)x$($screen.Bounds.Height)"

Write-Output "`n=== 2. CPU HARDWARE & TELEMETRY INTERFACES ==="
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
Write-Output "CPU Name: $($cpu.Name)"
Write-Output "Manufacturer: $($cpu.Manufacturer) | Cores: $($cpu.NumberOfCores) | Logical: $($cpu.NumberOfLogicalProcessors)"
Write-Output "MaxClockSpeed (Base MHz): $($cpu.MaxClockSpeed)"

# Check AMD Ryzen Master SDK Shared Memory (CPUMetricsServer)
$ryzenTask = Get-ScheduledTask -TaskName "AMDRyzenMasterSDKTask" -ErrorAction SilentlyContinue
$ryzenProc = Get-Process -Name "CPUMetricsServer" -ErrorAction SilentlyContinue
Write-Output "AMD Ryzen Master SDK Task Present: $([bool]$ryzenTask) | CPUMetricsServer Running: $([bool]$ryzenProc)"

# Check PDH Processor Performance Counter
try {
    $perf = (Get-Counter "\Processor Information(_Total)\% Processor Performance" -SampleInterval 1 -MaxSamples 1).CounterSamples[0].CookedValue
    Write-Output "Windows PDH '% Processor Performance' Available: True (Current: $([math]::Round($perf, 1))%)"
} catch {
    Write-Output "Windows PDH '% Processor Performance' Available: False"
}

Write-Output "`n=== 3. GPU HARDWARE & TELEMETRY INTERFACES ==="
$gpus = Get-CimInstance Win32_VideoController
foreach ($g in $gpus) {
    Write-Output "GPU: $($g.Name) | Driver: $($g.DriverVersion) | PNP: $($g.PNPDeviceID)"
}
$hasAdl = Test-Path "$env:SystemRoot\System32\atiadlxx.dll"
$hasNvml = (Test-Path "$env:SystemRoot\System32\nvml.dll") -or (Test-Path "$env:ProgramFiles\NVIDIA Corporation\NVSMI\nvml.dll")
Write-Output "AMD ADL2 (atiadlxx.dll) Present: $hasAdl"
Write-Output "NVIDIA NVML (nvml.dll) Present:  $hasNvml"

Write-Output "`n=== 4. RAM CONFIGURATION ==="
$ramSticks = Get-CimInstance Win32_PhysicalMemory
$totalRamGb = [math]::Round(($ramSticks | Measure-Object -Property Capacity -Sum).Sum / 1GB, 1)
$ramSpeed = ($ramSticks | Select-Object -First 1).ConfiguredClockSpeed
Write-Output "Total RAM: $totalRamGb GB ($($ramSticks.Count) DIMMs @ $ramSpeed MHz)"

Write-Output "`n=== 5. COMPILER & WINDHAWK ENVIRONMENT ==="
$whClang = Test-Path "C:\Program Files\Windhawk\Compiler\bin\clang++.exe"
$sysClang = [bool](Get-Command clang++ -ErrorAction SilentlyContinue)
$msvcCl   = [bool](Get-Command cl.exe -ErrorAction SilentlyContinue)
Write-Output "Windhawk Clang++ (C:\Program Files\Windhawk\Compiler\bin\clang++.exe): $whClang"
Write-Output "System PATH clang++: $sysClang | MSVC cl.exe: $msvcCl"
Write-Output "=== END OF AI HARDWARE REPORT ==="
