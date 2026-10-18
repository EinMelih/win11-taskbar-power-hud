# 🤖 AI Agent Hardware-Adaptation Guide (`AGENTS.md`)

> **Instruction for AI Coding Assistants (Antigravity, Cursor, Windsurf, Cline, Copilot, ChatGPT):**
> When a user asks you to check if this HUD supports their PC or asks you to install/adapt it for their hardware, follow this exact 4-step workflow.

---

## Step 1: Run the Hardware Diagnostic Scanner
Execute the included PowerShell scanner to inspect the user's CPU, GPU, telemetry DLLs, RAM, display resolution/DPI, and compiler:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check_hardware_for_ai.ps1
```

---

## Step 2: Check Sensor Compatibility & Adapt `src/taskbar-power-hud.wh.cpp`

Open `src/taskbar-power-hud.wh.cpp` and inspect `ReadAllSensors(TelemetryData& out, bool updateProcesses)`:

### A) CPU Frequency (`GHz` matching Windows Task Manager)
* In `QueryTaskMgrCpuGhzAndLoad()`, update `baseFreqGhz` to match the user's CPU base clock from `Win32_Processor.MaxClockSpeed / 1000.0` (default is `3.801` for AMD Ryzen 7 5800X):
  ```cpp
  const double baseFreqGhz = 3.801; // <-- Replace with user's MaxClockSpeed / 1000.0
  ```
* The Windows PDH counter `\Processor Information(_Total)\% Processor Performance` works universally on **all Windows 10/11 CPUs (AMD & Intel)** without any extra drivers.

### B) CPU Power (`Watt`) & Temperature (`°C`)
1. **If the user has an AMD Ryzen CPU:**
   * Check if `CPUMetricsServer.exe` (`AMDRyzenMasterSDKTask`, installed with AMD Chipset Drivers / Adrenalin / Ryzen Master) is running.
   * If yes, the existing Shared Memory reader (`Local\954b280e-5529-4492-bab2-bb7a502552f8`) works out of the box with zero ring-0 driver overhead!
   * Update the thermal throttle threshold (`out.cpuTempC >= 89.0` for Ryzen 5000 `90°C TjMax`, or `>= 94.0` for Ryzen 7000/9000 `95°C TjMax`) and PBO PPT limit (`135.0` W default).
2. **If the user has an Intel CPU (or AMD without `CPUMetricsServer`):**
   * Replace Section `1. CPU via AMD Ryzen Master SDK Shared Memory` in `ReadAllSensors()` with either:
     * **Option 1 (LibreHardwareMonitor / HWiNFO Shared Memory or WMI):** Read `CPU Package` Power (`W`) and `CPU Package` Temperature (`°C`).
     * **Option 2 (Zero-Driver Fallback Estimation):** Estimate CPU Package Power dynamically from `out.cpuLoadPct` and TDP:
       ```cpp
       out.cpuPptW  = idleWatt + (tdpWatt - idleWatt) * std::pow(out.cpuLoadPct / 100.0, 0.75);
       out.cpuTempC = 42.0 + (out.cpuLoadPct * 0.38);
       ```

### C) GPU Telemetry (`Watt`, `Temp °C`, `Load %`)
1. **If the user has an AMD Radeon GPU (`atiadlxx.dll` present):**
   * Works 100% natively out of the box via `ADL2_New_QueryPMLogData_Get` (`Sensor[73]` Board Power, `Sensor[8]` Edge Temp, `Sensor[27]` Hotspot Temp, `Sensor[19]` Load %).
2. **If the user has an NVIDIA GeForce RTX / GTX GPU (`nvml.dll` present):**
   * Replace the `atiadlxx.dll` block in `ReadAllSensors()` with dynamic `nvml.dll` calls (`C:\Windows\System32\nvml.dll`):
     ```cpp
     typedef int (*nvmlInit_t)();
     typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
     typedef int (*nvmlDeviceGetPowerUsage_t)(void*, unsigned int*); // returns milliwatts
     typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*); // sensor 0 = GPU
     typedef int (*nvmlDeviceGetUtilizationRates_t)(void*, struct { unsigned int gpu; unsigned int memory; }*);
     ```
     * `nvmlDeviceGetPowerUsage` returns milliwatts (`mW / 1000 = Watt`).
     * `nvmlDeviceGetTemperature(dev, 0, &tempC)` returns GPU temperature in `°C`.
     * `nvmlDeviceGetUtilizationRates` returns GPU core load `%`.

### D) Rest-System Base Power & PSU Efficiency Curve
In `ReadAllSensors()` (Section 4):
* Adjust `out.restSystemW` (`36.0 + loadFactor * 9.0` W default for ATX Motherboard + 4 DIMMs + AIO Pump + 5 Fans + 2 SSDs) to match the user's fans/drives.
* Adjust `eff` (`0.885` / `0.915` / `0.900` default for 80 PLUS Gold) if the user has an 80 PLUS Bronze (`~0.85`), Platinum (`~0.93`), or Titanium (`~0.95`) PSU.

### E) Live Network / WiFi Speed (`MB/s` · `Mbit/s` · `GB/s`) & UI Language / Currency
* **Network Telemetry:** Uses Windows native `GetIfTable2` (`iphlpapi.dll`, linked via `-liphlpapi`) and works on **100% of Windows 10/11 PCs out of the box** (both WiFi and Ethernet). Users can switch units (`MB/s`, `Mbit/s`, `GB/s`) directly via the WiFi hover capsule or by clicking the WiFi card.
* **Language & Currency Localization:** The flyout dashboard strings in `RenderFlyout()` are in German (`€`) by default. If the user prefers English (or another language / currency such as `$`, `£`, `CHF`), translate the UI string literals inside `RenderFlyout()` and `OpenSettingsDialog()` in `src/taskbar-power-hud.wh.cpp` to match the user's language.

---

## Step 3: Verify Compiler & Build
* If Windhawk is installed (`C:\Program Files\Windhawk\Compiler\bin\clang++.exe`), use it directly.
* Otherwise, use system `clang++` (MinGW-w64) or install Windhawk (`winget install RamenSoftware.Windhawk`).
* Run the installer script:
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_and_install.ps1
  ```

---

## Step 4: Configure Persistent Energy Log Path (`power_config.json`)
Click the taskbar pill once to open the **Power Analytics Flyout** and click the log-path button (or edit `power_config.json`) to set the desired `power_log.json` path (e.g. a NAS/SMB share like `Z:\` or local disk), electricity tariff (`eurPerKwh`, default `0.32`), and default network unit (`"netUnit": "MB" | "Mbit" | "GB"`).
