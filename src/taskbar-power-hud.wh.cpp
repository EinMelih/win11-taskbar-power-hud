// ==WindhawkMod==
// @id              main-pc-live-telemetry
// @name            Main PC Live Power, Cost Analytics & Hardware HUD (Claude v3.0 Edition)
// @description     Echtes Claude-Design Taskleisten-HUD mit Task-Manager-GHz, Drossel-Erkennung, Top-3 App-Icons in der RAM-Pille, 60-FPS Hover-Popouts (CPU/GPU/RAM Top-3 Programme) & Z:-Langzeit-Logging.
// @version         3.0.0
// @author          EinMelih & Antigravity
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lgdi32 -luser32 -lshell32 -lgdiplus -lole32 -lcomdlg32 -lpdh -lpsapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Main PC Live Power, Cost Analytics & Hardware HUD (v3.0 – Claude Design Edition)

Maßgeschneiderte Taskleisten-Mod & Energie-Analyse-Suite für **EinMelihs Main PC**:
- **Echtes Claude-Design (Anthropic Warm Dark Palette):** Edles Warm-Obsidian (`#191918` / `#242321`), Pergament-Typografie (`#F5F4EF`) und Claude-Terracotta (`#D97757`) Akzente.
- **Exakter Task-Manager GHz-Takt:** Liest über die Windows PDH-Engine (`% Processor Performance`) den identischen aktiven Boost-Takt wie der Windows Task-Manager (`z.B. 4.58 GHz`) plus den SMU Peak-Takt.
- **Hardware-kalibrierte Temperatur-Farben & Live Drossel-Anzeige (Throttling):**
  - `<= 66 °C`: Smaragdgrün (Kühl / Optimal für Ryzen 7 5800X & RX 9070 XT)
  - `67–77 °C`: Warm-Gold (Normaler Gaming-Betrieb)
  - `78–86 °C`: Vivid Orange (Hohe Volllast)
  - `>= 87 °C`: Rubinrot (Nahe am `90 °C` TjMax-Drossellimit)
  - Zeigt live an, ob gedrosselt wird (`✓ Keine Drosselung` vs. `⚡ PBO-Limit` vs. `⚠️ Thermal Throttle`).
- **Top-3 RAM App-Icons direkt in der Taskleiste + 60-FPS Hover-Animation:**
  - Direkt in der RAM-Karte auf der Taskleiste erscheinen die **echten Programm-Icons der Top-3 RAM-Apps**.
  - Beim Drüberfahren mit der Maus (`Hover`) gleitet mit einer **60-FPS Animation** eine Claude-Hover-Card über `CPU`, `GPU` oder `RAM` auf und zeigt die **Top-3 Programme mit echten Icons, Verbrauch und allen Temperaturen/Limits**.
- **1-Klick Analyse-Flyout & `Z:\`-Langzeit-Logger:** Linksklick öffnet das große Diagramm-Dashboard (Tages- & Monatsverlauf in `€` und `Wh` aus `Z:\_inbox\PC_Stromverbrauch\power_log.json`).
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- UpdateIntervalMs: 1000
  $name: Aktualisierungs-Intervall (ms)
  $description: Wie oft die Sensoren abgefragt werden (Standard: 1000 ms = 1 Sekunde)
- RightMarginPx: 12
  $name: Abstand nach rechts zum System-Tray (DIP)
  $description: Horizontaler Abstand links neben der System-Tray-Pille in der rechten Luecke
- PillOpacity: 248
  $name: Hintergrund-Deckkraft (0-255)
  $description: Deckkraft der Claude-Pille (Standard: 248)
- ShowCpuClock: true
  $name: CPU-Takt (GHz) anzeigen
  $description: Zeigt zusätzlich zu Watt und Temperatur den aktuellen Task-Manager Boost-Takt in GHz an
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <gdiplus.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <deque>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cwchar>
#include <cstdio>

using namespace Gdiplus;

struct Settings {
    int updateIntervalMs = 1000;
    int rightMarginDip = 12;
    int pillOpacity = 248;
    bool showCpuClock = true;
} g_settings;

struct TopAppEntry {
    std::wstring name;
    std::wstring exePath;
    double valuePrimary = 0.0;   // RAM: MB | CPU: % | GPU: VRAM MB
    double valueSecondary = 0.0; // RAM: %  | CPU: W | GPU: GPU %
    HICON hIcon = nullptr;
};

struct TelemetryData {
    double wallAcW = 0.0;
    double totalDcW = 0.0;
    double restSystemW = 36.0;
    double cpuPptW = 0.0;
    double cpuTempC = 0.0;
    double cpuClockGhz = 0.0;    // Task-Manager exact GHz
    double cpuSmuPeakGhz = 0.0;  // SMU Peak GHz
    double cpuLoadPct = 0.0;     // Task-Manager CPU %
    double cpuVoltV = 0.0;
    double cpuTdcA = 0.0;
    double cpuEdcA = 0.0;
    bool cpuThermalThrottle = false;
    bool cpuPowerLimitActive = false;

    int gpuBoardW = 0;
    int gpuAsicW = 0;
    int gpuSocW = 0;
    int gpuTempC = 0;
    int gpuHotspotC = 0;
    int gpuVramTempC = 0;
    int gpuLoadPct = 0;
    int gpuFanRpm = 0;
    bool gpuThermalThrottle = false;

    double ramUsedGb = 0.0;
    double ramTotalGb = 32.0;
    int ramUsedPct = 0;

    std::vector<TopAppEntry> topRamApps; // Top 3 by RAM MB
    std::vector<TopAppEntry> topCpuApps; // Top 3 by CPU %
    std::vector<TopAppEntry> topGpuApps; // Top 3 by GPU % / VRAM MB

    bool valid = false;
};

struct DayRecord {
    std::string date; // YYYY-MM-DD
    double wh = 0.0;
    double sec = 0.0;
    double peakW = 0.0;
};

struct MonthRecord {
    std::string month; // YYYY-MM
    double wh = 0.0;
    double sec = 0.0;
};

struct PowerLogState {
    std::wstring configFilePath = L".\\power_config.json";
    std::wstring logFilePath    = L"Z:\\_inbox\\PC_Stromverbrauch\\power_log.json";
    std::wstring csvFilePath    = L"Z:\\_inbox\\PC_Stromverbrauch\\power_daily.csv";
    std::wstring backupFilePath = L".\\power_log_backup.json";
    double eurPerKwh = 0.32;

    std::string currentDate;  // YYYY-MM-DD
    std::string currentMonth; // YYYY-MM
    double totalWh = 0.0;
    double totalSeconds = 0.0;
    double peakWallW = 0.0;
    double hourlyWhToday[24] = {0};
    std::vector<DayRecord> daily;
    std::vector<MonthRecord> monthly;
};

static std::atomic<bool> g_stopThread{false};
static std::thread g_workerThread;
static HWND g_hudHwnd    = nullptr;
static HWND g_flyoutHwnd = nullptr;
static HWND g_hoverHwnd  = nullptr;
static bool g_flyoutVisible = false;
static std::mutex g_dataMutex;
static TelemetryData g_data;
static std::deque<float> g_wattHistory; // last 60 seconds
static PowerLogState g_log;

// Hover state & 60-FPS animation state
// Sections: -1 = None, 0 = Strom, 1 = CPU, 2 = GPU, 3 = RAM, 4 = Euro
static int   g_hoveredCardIdx   = -1;
static int   g_activeHoverIdx   = 1;
static float g_hoverAnimProgress = 0.0f; // 0.0 (hidden) .. 1.0 (fully shown)
static float g_hoverAnimCenterX  = 0.0f; // smoothly interpolated screen X
static RECT  g_cardRectsLocal[5] = {};   // local X ranges of the 5 micro-cards inside HUD
static RECT  g_lastPillScreenRect = {3000, 2088, 3470, 2144};
static bool  g_mouseTracking = false;

// Interactive hitboxes inside the Flyout window
static RECT g_rcBtnClose      = {};
static RECT g_rcBtnChangePath = {};
static RECT g_rcBtnOpenFolder = {};
static RECT g_rcBtnTariff     = {};
static RECT g_rcBtnPumpMax    = {};
static RECT g_rcBtnConsole    = {};

// AMD ADL2 Function Pointers
typedef void* (__stdcall *ADL_MAIN_MALLOC_CALLBACK)(int);
typedef int (*ADL2_MAIN_CONTROL_CREATE)(ADL_MAIN_MALLOC_CALLBACK, int, void**);
typedef int (*ADL2_MAIN_CONTROL_DESTROY)(void*);
struct ADLPMLogDataOutput {
    int size;
    int sensors[512];
};
typedef int (*ADL2_NEW_QUERYPMLOGDATA_GET)(void*, int, ADLPMLogDataOutput*);

static void* __stdcall AdlMainMemoryAlloc(int iSize) {
    return malloc(iSize);
}

static UINT SafeGetDpiForWindow(HWND hWnd) {
    typedef UINT(WINAPI* fnGetDpiForWindow)(HWND);
    static fnGetDpiForWindow pFn = (fnGetDpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (pFn && hWnd) {
        UINT d = pFn(hWnd);
        if (d >= 96) return d;
    }
    return 144;
}

static void SafeEnableDpiAwareness() {
    typedef BOOL(WINAPI* fnSetProcessDpiAwarenessContext)(HANDLE);
    static fnSetProcessDpiAwarenessContext pFn = (fnSetProcessDpiAwarenessContext)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (pFn) {
        pFn((HANDLE)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }
}

static HMODULE g_hAdlDll = nullptr;
static void* g_adlContext = nullptr;
static ADL2_MAIN_CONTROL_CREATE pADL2_Main_Control_Create = nullptr;
static ADL2_MAIN_CONTROL_DESTROY pADL2_Main_Control_Destroy = nullptr;
static ADL2_NEW_QUERYPMLOGDATA_GET pADL2_New_QueryPMLogData_Get = nullptr;

// PDH Query Handles for exact Task-Manager GHz & GPU Process metrics
static PDH_HQUERY   g_pdhQuery      = nullptr;
static PDH_HCOUNTER g_pdhCpuPerf    = nullptr;
static PDH_HCOUNTER g_pdhCpuUtil    = nullptr;
static PDH_HCOUNTER g_pdhGpuProcMem = nullptr;
static PDH_HCOUNTER g_pdhGpuEngUtil = nullptr;
static double       g_lastTaskMgrGhz = 4.55;
static double       g_lastCpuUtilPct = 12.0;

// Process CPU tracking & Icon Cache
static std::unordered_map<DWORD, ULONGLONG> g_prevProcKernelUser;
static ULONGLONG g_prevSnapTickMs = 0;
static std::unordered_map<std::wstring, HICON> g_iconCache;

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string res(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &res[0], sz, nullptr, nullptr);
    return res;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring res(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &res[0], sz);
    return res;
}

static std::string EscapeJsonString(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\"') out += "\\\"";
        else out += c;
    }
    return out;
}

static std::string UnescapeJsonString(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            out += s[i + 1];
            i++;
        } else {
            out += s[i];
        }
    }
    return out;
}

static double ExtractJsonNumber(const std::string& json, const std::string& key, double defVal) {
    std::string pat = "\"" + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return defVal;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return defVal;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
    try {
        return std::stod(json.substr(pos));
    } catch (...) {
        return defVal;
    }
}

static std::string ExtractJsonString(const std::string& json, const std::string& key, const std::string& defVal) {
    std::string pat = "\"" + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return defVal;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return defVal;
    pos = json.find('\"', pos + 1);
    if (pos == std::string::npos) return defVal;
    pos++;
    std::string raw;
    while (pos < json.size()) {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            raw += json[pos];
            raw += json[pos + 1];
            pos += 2;
        } else if (json[pos] == '\"') {
            break;
        } else {
            raw += json[pos];
            pos++;
        }
    }
    return UnescapeJsonString(raw);
}

static void EnsureParentDirectory(const std::wstring& filePath) {
    size_t slash = filePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    std::wstring dir = filePath.substr(0, slash);
    CreateDirectoryW(dir.c_str(), nullptr);
}

static void GetCurrentDateStrings(std::string& outDate, std::string& outMonth, int& outHour) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char bufDate[32], bufMonth[16];
    snprintf(bufDate, sizeof(bufDate), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    snprintf(bufMonth, sizeof(bufMonth), "%04d-%02d", st.wYear, st.wMonth);
    outDate  = bufDate;
    outMonth = bufMonth;
    outHour  = st.wHour;
}

static void LoadPowerConfig() {
    std::ifstream f(g_log.configFilePath.c_str());
    if (!f.is_open()) return;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();
    std::string logP = ExtractJsonString(content, "logFilePath", "");
    std::string csvP = ExtractJsonString(content, "csvFilePath", "");
    double tariff = ExtractJsonNumber(content, "eurPerKwh", g_log.eurPerKwh);
    if (!logP.empty()) g_log.logFilePath = Utf8ToWide(logP);
    if (!csvP.empty()) g_log.csvFilePath = Utf8ToWide(csvP);
    if (tariff > 0.05 && tariff < 2.0) g_log.eurPerKwh = tariff;
}

static void SavePowerConfig() {
    EnsureParentDirectory(g_log.configFilePath);
    std::ofstream f(g_log.configFilePath.c_str(), std::ios::trunc);
    if (!f.is_open()) return;
    f << "{\n"
      << "  \"logFilePath\": \"" << EscapeJsonString(WideToUtf8(g_log.logFilePath)) << "\",\n"
      << "  \"csvFilePath\": \"" << EscapeJsonString(WideToUtf8(g_log.csvFilePath)) << "\",\n"
      << "  \"eurPerKwh\": " << std::fixed << std::setprecision(4) << g_log.eurPerKwh << "\n"
      << "}\n";
}

static bool ParsePowerLogFile(const std::wstring& path) {
    std::ifstream f(path.c_str());
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    if (s.size() < 10) return false;

    g_log.totalWh      = ExtractJsonNumber(s, "totalWh", 0.0);
    g_log.totalSeconds = ExtractJsonNumber(s, "totalSeconds", 0.0);
    g_log.peakWallW    = ExtractJsonNumber(s, "peakWallW", 0.0);
    std::string savedDate = ExtractJsonString(s, "currentDate", "");

    std::string nowDate, nowMonth;
    int nowHour = 0;
    GetCurrentDateStrings(nowDate, nowMonth, nowHour);
    g_log.currentDate  = nowDate;
    g_log.currentMonth = nowMonth;

    if (savedDate == nowDate) {
        size_t hp = s.find("\"hourlyWhToday\"");
        if (hp != std::string::npos) {
            size_t lb = s.find('[', hp);
            size_t rb = s.find(']', hp);
            if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
                std::string arr = s.substr(lb + 1, rb - lb - 1);
                std::stringstream as(arr);
                std::string item;
                int idx = 0;
                while (std::getline(as, item, ',') && idx < 24) {
                    try { g_log.hourlyWhToday[idx] = std::stod(item); } catch (...) {}
                    idx++;
                }
            }
        }
    }

    g_log.daily.clear();
    size_t dp = s.find("\"dailyHistory\"");
    if (dp != std::string::npos) {
        size_t lb = s.find('[', dp);
        size_t rb = s.find(']', dp);
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::string arr = s.substr(lb + 1, rb - lb - 1);
            size_t pos = 0;
            while (true) {
                size_t ob = arr.find('{', pos);
                size_t cb = arr.find('}', ob);
                if (ob == std::string::npos || cb == std::string::npos) break;
                std::string obj = arr.substr(ob, cb - ob + 1);
                DayRecord dr;
                dr.date  = ExtractJsonString(obj, "date", "");
                dr.wh    = ExtractJsonNumber(obj, "wh", 0.0);
                dr.sec   = ExtractJsonNumber(obj, "sec", 0.0);
                dr.peakW = ExtractJsonNumber(obj, "peakW", 0.0);
                if (!dr.date.empty()) g_log.daily.push_back(dr);
                pos = cb + 1;
            }
        }
    }

    g_log.monthly.clear();
    size_t mp = s.find("\"monthlyHistory\"");
    if (mp != std::string::npos) {
        size_t lb = s.find('[', mp);
        size_t rb = s.find(']', mp);
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::string arr = s.substr(lb + 1, rb - lb - 1);
            size_t pos = 0;
            while (true) {
                size_t ob = arr.find('{', pos);
                size_t cb = arr.find('}', ob);
                if (ob == std::string::npos || cb == std::string::npos) break;
                std::string obj = arr.substr(ob, cb - ob + 1);
                MonthRecord mr;
                mr.month = ExtractJsonString(obj, "month", "");
                mr.wh    = ExtractJsonNumber(obj, "wh", 0.0);
                mr.sec   = ExtractJsonNumber(obj, "sec", 0.0);
                if (!mr.month.empty()) g_log.monthly.push_back(mr);
                pos = cb + 1;
            }
        }
    }
    return true;
}

static void LoadPowerLog() {
    LoadPowerConfig();
    SavePowerConfig();
    std::string nowDate, nowMonth;
    int nowHour = 0;
    GetCurrentDateStrings(nowDate, nowMonth, nowHour);
    g_log.currentDate  = nowDate;
    g_log.currentMonth = nowMonth;

    if (!ParsePowerLogFile(g_log.logFilePath)) {
        ParsePowerLogFile(g_log.backupFilePath);
    }
}

static void SavePowerLogToDisk() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char tsBuf[64];
    snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    double totalKwh = g_log.totalWh / 1000.0;
    double totalEur = totalKwh * g_log.eurPerKwh;

    std::ostringstream js;
    js << "{\n"
       << "  \"updatedAt\": \"" << tsBuf << "\",\n"
       << "  \"logFilePath\": \"" << EscapeJsonString(WideToUtf8(g_log.logFilePath)) << "\",\n"
       << "  \"eurPerKwh\": " << std::fixed << std::setprecision(4) << g_log.eurPerKwh << ",\n"
       << "  \"totalWh\": " << std::fixed << std::setprecision(4) << g_log.totalWh << ",\n"
       << "  \"totalKwh\": " << std::fixed << std::setprecision(4) << totalKwh << ",\n"
       << "  \"totalEur\": " << std::fixed << std::setprecision(4) << totalEur << ",\n"
       << "  \"totalSeconds\": " << std::fixed << std::setprecision(0) << g_log.totalSeconds << ",\n"
       << "  \"peakWallW\": " << std::fixed << std::setprecision(1) << g_log.peakWallW << ",\n"
       << "  \"currentDate\": \"" << g_log.currentDate << "\",\n"
       << "  \"hourlyWhToday\": [";
    for (int i = 0; i < 24; i++) {
        js << std::fixed << std::setprecision(2) << g_log.hourlyWhToday[i];
        if (i < 23) js << ", ";
    }
    js << "],\n  \"dailyHistory\": [\n";
    for (size_t i = 0; i < g_log.daily.size(); i++) {
        const auto& d = g_log.daily[i];
        double dEur = (d.wh / 1000.0) * g_log.eurPerKwh;
        js << "    {\"date\": \"" << d.date << "\", \"wh\": " << std::fixed << std::setprecision(2) << d.wh
           << ", \"eur\": " << std::fixed << std::setprecision(4) << dEur
           << ", \"sec\": " << std::fixed << std::setprecision(0) << d.sec
           << ", \"peakW\": " << std::fixed << std::setprecision(1) << d.peakW << "}";
        if (i + 1 < g_log.daily.size()) js << ",";
        js << "\n";
    }
    js << "  ],\n  \"monthlyHistory\": [\n";
    for (size_t i = 0; i < g_log.monthly.size(); i++) {
        const auto& m = g_log.monthly[i];
        double mEur = (m.wh / 1000.0) * g_log.eurPerKwh;
        js << "    {\"month\": \"" << m.month << "\", \"wh\": " << std::fixed << std::setprecision(2) << m.wh
           << ", \"eur\": " << std::fixed << std::setprecision(4) << mEur
           << ", \"sec\": " << std::fixed << std::setprecision(0) << m.sec << "}";
        if (i + 1 < g_log.monthly.size()) js << ",";
        js << "\n";
    }
    js << "  ]\n}\n";

    std::string payload = js.str();

    EnsureParentDirectory(g_log.logFilePath);
    {
        std::ofstream fPrimary(g_log.logFilePath.c_str(), std::ios::trunc);
        if (fPrimary.is_open()) {
            fPrimary << payload;
        }
    }
    EnsureParentDirectory(g_log.backupFilePath);
    {
        std::ofstream fBackup(g_log.backupFilePath.c_str(), std::ios::trunc);
        if (fBackup.is_open()) {
            fBackup << payload;
        }
    }

    EnsureParentDirectory(g_log.csvFilePath);
    {
        std::ofstream fCsv(g_log.csvFilePath.c_str(), std::ios::trunc);
        if (fCsv.is_open()) {
            fCsv << "Datum;Wattstunden_Wh;Kilowattstunden_kWh;Kosten_EUR;Laufzeit_Stunden;Spitzenlast_Watt\n";
            for (const auto& d : g_log.daily) {
                double kwh = d.wh / 1000.0;
                double eur = kwh * g_log.eurPerKwh;
                double hrs = d.sec / 3600.0;
                fCsv << d.date << ";"
                     << std::fixed << std::setprecision(2) << d.wh << ";"
                     << std::setprecision(4) << kwh << ";"
                     << std::setprecision(4) << eur << ";"
                     << std::setprecision(2) << hrs << ";"
                     << std::setprecision(1) << d.peakW << "\n";
            }
        }
    }
}

static void AccumulateEnergySample(double wallAcW, double dtSec) {
    if (dtSec <= 0.0 || dtSec > 15.0 || wallAcW <= 0.0) return;

    std::string nowDate, nowMonth;
    int nowHour = 0;
    GetCurrentDateStrings(nowDate, nowMonth, nowHour);

    if (g_log.currentDate != nowDate) {
        g_log.currentDate = nowDate;
        for (int i = 0; i < 24; i++) g_log.hourlyWhToday[i] = 0.0;
    }
    g_log.currentMonth = nowMonth;

    double dWh = wallAcW * (dtSec / 3600.0);
    g_log.totalWh += dWh;
    g_log.totalSeconds += dtSec;
    if (wallAcW > g_log.peakWallW) g_log.peakWallW = wallAcW;
    if (nowHour >= 0 && nowHour < 24) {
        g_log.hourlyWhToday[nowHour] += dWh;
    }

    DayRecord* pDay = nullptr;
    for (auto& d : g_log.daily) {
        if (d.date == nowDate) { pDay = &d; break; }
    }
    if (!pDay) {
        DayRecord nd;
        nd.date = nowDate;
        g_log.daily.push_back(nd);
        if (g_log.daily.size() > 90) g_log.daily.erase(g_log.daily.begin());
        pDay = &g_log.daily.back();
    }
    pDay->wh  += dWh;
    pDay->sec += dtSec;
    if (wallAcW > pDay->peakW) pDay->peakW = wallAcW;

    MonthRecord* pMon = nullptr;
    for (auto& m : g_log.monthly) {
        if (m.month == nowMonth) { pMon = &m; break; }
    }
    if (!pMon) {
        MonthRecord nm;
        nm.month = nowMonth;
        g_log.monthly.push_back(nm);
        if (g_log.monthly.size() > 24) g_log.monthly.erase(g_log.monthly.begin());
        pMon = &g_log.monthly.back();
    }
    pMon->wh  += dWh;
    pMon->sec += dtSec;
}

static void GetTodayAndMonthStats(double& todayWh, double& todayEur, double& todaySec, double& todayPeakW,
                                   double& monthWh, double& monthEur, double& monthSec) {
    todayWh = todayEur = todaySec = todayPeakW = 0.0;
    monthWh = monthEur = monthSec = 0.0;
    for (const auto& d : g_log.daily) {
        if (d.date == g_log.currentDate) {
            todayWh = d.wh;
            todayEur = (d.wh / 1000.0) * g_log.eurPerKwh;
            todaySec = d.sec;
            todayPeakW = d.peakW;
            break;
        }
    }
    for (const auto& m : g_log.monthly) {
        if (m.month == g_log.currentMonth) {
            monthWh = m.wh;
            monthEur = (m.wh / 1000.0) * g_log.eurPerKwh;
            monthSec = m.sec;
            break;
        }
    }
}

static void InitAdl() {
    if (g_hAdlDll) return;
    g_hAdlDll = LoadLibraryW(L"atiadlxx.dll");
    if (!g_hAdlDll) return;
    pADL2_Main_Control_Create = (ADL2_MAIN_CONTROL_CREATE)GetProcAddress(g_hAdlDll, "ADL2_Main_Control_Create");
    pADL2_Main_Control_Destroy = (ADL2_MAIN_CONTROL_DESTROY)GetProcAddress(g_hAdlDll, "ADL2_Main_Control_Destroy");
    pADL2_New_QueryPMLogData_Get = (ADL2_NEW_QUERYPMLOGDATA_GET)GetProcAddress(g_hAdlDll, "ADL2_New_QueryPMLogData_Get");
    if (pADL2_Main_Control_Create) {
        pADL2_Main_Control_Create(AdlMainMemoryAlloc, 1, &g_adlContext);
    }
}

static void CleanupAdl() {
    if (g_adlContext && pADL2_Main_Control_Destroy) {
        pADL2_Main_Control_Destroy(g_adlContext);
        g_adlContext = nullptr;
    }
    if (g_hAdlDll) {
        FreeLibrary(g_hAdlDll);
        g_hAdlDll = nullptr;
    }
}

static void InitPdhCounters() {
    if (g_pdhQuery) return;
    if (PdhOpenQueryW(nullptr, 0, &g_pdhQuery) == ERROR_SUCCESS) {
        PdhAddEnglishCounterW(g_pdhQuery, L"\\Processor Information(_Total)\\% Processor Performance", 0, &g_pdhCpuPerf);
        PdhAddEnglishCounterW(g_pdhQuery, L"\\Processor Information(_Total)\\% Processor Utility", 0, &g_pdhCpuUtil);
        PdhAddEnglishCounterW(g_pdhQuery, L"\\GPU Process Memory(*)\\Local Usage", 0, &g_pdhGpuProcMem);
        PdhAddEnglishCounterW(g_pdhQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_pdhGpuEngUtil);
        PdhCollectQueryData(g_pdhQuery);
    }
}

static std::wstring PrettifyAppName(std::wstring exeName) {
    if (exeName.size() > 4 && _wcsicmp(exeName.c_str() + exeName.size() - 4, L".exe") == 0) {
        exeName.resize(exeName.size() - 4);
    }
    if (_wcsicmp(exeName.c_str(), L"msedgewebview2") == 0) return L"Edge WebView2";
    if (_wcsicmp(exeName.c_str(), L"ApplicationFrameHost") == 0) return L"WinApp Host";
    if (_wcsicmp(exeName.c_str(), L"dwm") == 0) return L"Desktop Manager";
    if (_wcsicmp(exeName.c_str(), L"explorer") == 0) return L"Windows Explorer";
    if (_wcsicmp(exeName.c_str(), L"steamwebhelper") == 0) return L"Steam";
    if (_wcsicmp(exeName.c_str(), L"taskmgr") == 0) return L"Task Manager";
    if (_wcsicmp(exeName.c_str(), L"wallpaper64") == 0 || _wcsicmp(exeName.c_str(), L"wallpaper32") == 0) return L"Wallpaper Engine";
    if (_wcsnicmp(exeName.c_str(), L"language_server", 15) == 0) return L"Language Server";
    if (!exeName.empty() && exeName[0] >= L'a' && exeName[0] <= L'z') {
        exeName[0] = exeName[0] - L'a' + L'A';
    }
    return exeName;
}

static HICON GetOrCacheAppIcon(const std::wstring& exePath) {
    if (exePath.empty()) return nullptr;
    auto it = g_iconCache.find(exePath);
    if (it != g_iconCache.end()) return it->second;

    SHFILEINFOW sfi = {};
    HICON hIco = nullptr;
    if (SHGetFileInfoW(exePath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
        hIco = sfi.hIcon;
    }
    if (!hIco) {
        hIco = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    }
    if (g_iconCache.size() > 250) {
        for (auto& kv : g_iconCache) {
            if (kv.second) DestroyIcon(kv.second);
        }
        g_iconCache.clear();
    }
    g_iconCache[exePath] = hIco;
    return hIco;
}

static DWORD ExtractPidFromInstanceName(const wchar_t* inst) {
    if (!inst) return 0;
    const wchar_t* p = wcsstr(inst, L"pid_");
    if (!p) return 0;
    return (DWORD)_wtoi(p + 4);
}

static void UpdateTopProcesses(std::vector<TopAppEntry>& outRam,
                               std::vector<TopAppEntry>& outCpu,
                               std::vector<TopAppEntry>& outGpu,
                               double totalCpuPptW) {
    // 1. Collect per-PID GPU VRAM & GPU 3D/Engine % from PDH
    std::unordered_map<DWORD, double> pidGpuMemMb;
    std::unordered_map<DWORD, double> pidGpuUtilPct;

    if (g_pdhGpuProcMem) {
        DWORD bufSize = 0, itemCount = 0;
        if (PdhGetFormattedCounterArrayW(g_pdhGpuProcMem, PDH_FMT_DOUBLE, &bufSize, &itemCount, nullptr) == (PDH_STATUS)PDH_MORE_DATA && bufSize > 0) {
            std::vector<BYTE> buf(bufSize);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
            if (PdhGetFormattedCounterArrayW(g_pdhGpuProcMem, PDH_FMT_DOUBLE, &bufSize, &itemCount, items) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < itemCount; i++) {
                    DWORD pid = ExtractPidFromInstanceName(items[i].szName);
                    if (pid > 4 && items[i].FmtValue.doubleValue > 0.0) {
                        double mb = items[i].FmtValue.doubleValue / (1024.0 * 1024.0);
                        if (mb > pidGpuMemMb[pid]) pidGpuMemMb[pid] = mb;
                    }
                }
            }
        }
    }

    if (g_pdhGpuEngUtil) {
        DWORD bufSize = 0, itemCount = 0;
        if (PdhGetFormattedCounterArrayW(g_pdhGpuEngUtil, PDH_FMT_DOUBLE, &bufSize, &itemCount, nullptr) == (PDH_STATUS)PDH_MORE_DATA && bufSize > 0) {
            std::vector<BYTE> buf(bufSize);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
            if (PdhGetFormattedCounterArrayW(g_pdhGpuEngUtil, PDH_FMT_DOUBLE, &bufSize, &itemCount, items) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < itemCount; i++) {
                    DWORD pid = ExtractPidFromInstanceName(items[i].szName);
                    if (pid > 4 && items[i].FmtValue.doubleValue > 0.01) {
                        pidGpuUtilPct[pid] += items[i].FmtValue.doubleValue;
                    }
                }
            }
        }
    }

    // 2. Snapshot all processes & group by application executable name
    ULONGLONG nowTickMs = GetTickCount64();
    double dtSec = (g_prevSnapTickMs > 0 && nowTickMs > g_prevSnapTickMs)
                       ? (double)(nowTickMs - g_prevSnapTickMs) / 1000.0
                       : 1.0;
    if (dtSec < 0.2) dtSec = 0.2;
    g_prevSnapTickMs = nowTickMs;

    struct GroupAgg {
        std::wstring displayName;
        std::wstring bestExePath;
        double ramMb = 0.0;
        double cpuPct = 0.0;
        double gpuMemMb = 0.0;
        double gpuUtilPct = 0.0;
    };
    std::unordered_map<std::wstring, GroupAgg> groups;
    std::unordered_map<DWORD, ULONGLONG> nextProcTimes;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID <= 4) continue;
                std::wstring exeName = pe.szExeFile;
                if (_wcsicmp(exeName.c_str(), L"MemCompression") == 0 ||
                    _wcsicmp(exeName.c_str(), L"System") == 0 ||
                    _wcsicmp(exeName.c_str(), L"Registry") == 0 ||
                    _wcsicmp(exeName.c_str(), L"svchost.exe") == 0 ||
                    _wcsicmp(exeName.c_str(), L"conhost.exe") == 0) {
                    continue;
                }

                std::wstring key = exeName;
                for (auto& ch : key) ch = (wchar_t)towlower(ch);
                if (key == L"steamwebhelper.exe") key = L"steam.exe";

                auto& grp = groups[key];
                if (grp.displayName.empty()) {
                    grp.displayName = PrettifyAppName(exeName);
                }

                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProc) {
                    if (grp.bestExePath.empty()) {
                        wchar_t fullPath[MAX_PATH] = {};
                        DWORD pathLen = MAX_PATH;
                        if (QueryFullProcessImageNameW(hProc, 0, fullPath, &pathLen)) {
                            grp.bestExePath = fullPath;
                        }
                    }

                    PROCESS_MEMORY_COUNTERS_EX pmc = {};
                    pmc.cb = sizeof(pmc);
                    if (K32GetProcessMemoryInfo(hProc, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                        grp.ramMb += (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
                    }

                    FILETIME ftCreate = {}, ftExit = {}, ftKernel = {}, ftUser = {};
                    if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                        ULARGE_INTEGER uk, uu;
                        uk.LowPart = ftKernel.dwLowDateTime; uk.HighPart = ftKernel.dwHighDateTime;
                        uu.LowPart = ftUser.dwLowDateTime;   uu.HighPart = ftUser.dwHighDateTime;
                        ULONGLONG total100ns = uk.QuadPart + uu.QuadPart;
                        nextProcTimes[pe.th32ProcessID] = total100ns;

                        auto itPrev = g_prevProcKernelUser.find(pe.th32ProcessID);
                        if (itPrev != g_prevProcKernelUser.end() && total100ns >= itPrev->second) {
                            double deltaSec = (double)(total100ns - itPrev->second) / 10000000.0;
                            // 16 logical threads on Ryzen 7 5800X
                            double pct = (deltaSec / (dtSec * 16.0)) * 100.0;
                            if (pct > 0.0 && pct <= 100.0) {
                                grp.cpuPct += pct;
                            }
                        }
                    }
                    CloseHandle(hProc);
                }

                auto itGm = pidGpuMemMb.find(pe.th32ProcessID);
                if (itGm != pidGpuMemMb.end()) grp.gpuMemMb += itGm->second;

                auto itGu = pidGpuUtilPct.find(pe.th32ProcessID);
                if (itGu != pidGpuUtilPct.end()) grp.gpuUtilPct += itGu->second;

            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    g_prevProcKernelUser.swap(nextProcTimes);

    std::vector<GroupAgg> all;
    all.reserve(groups.size());
    for (auto& kv : groups) {
        if (!kv.second.bestExePath.empty()) {
            all.push_back(kv.second);
        }
    }

    // Top 3 RAM
    std::sort(all.begin(), all.end(), [](const GroupAgg& a, const GroupAgg& b) {
        return a.ramMb > b.ramMb;
    });
    outRam.clear();
    for (size_t i = 0; i < all.size() && outRam.size() < 3; i++) {
        TopAppEntry e;
        e.name = all[i].displayName;
        e.exePath = all[i].bestExePath;
        e.valuePrimary = all[i].ramMb; // MB
        e.valueSecondary = (all[i].ramMb / (32.0 * 1024.0)) * 100.0; // % of 32GB
        e.hIcon = GetOrCacheAppIcon(e.exePath);
        outRam.push_back(e);
    }

    // Top 3 CPU
    std::sort(all.begin(), all.end(), [](const GroupAgg& a, const GroupAgg& b) {
        if (std::abs(a.cpuPct - b.cpuPct) > 0.05) return a.cpuPct > b.cpuPct;
        return a.ramMb > b.ramMb;
    });
    outCpu.clear();
    for (size_t i = 0; i < all.size() && outCpu.size() < 3; i++) {
        TopAppEntry e;
        e.name = all[i].displayName;
        e.exePath = all[i].bestExePath;
        e.valuePrimary = all[i].cpuPct; // %
        // Estimate CPU dynamic watt share above 22W idle floor
        double dynCpuW = (std::max)(5.0, totalCpuPptW - 20.0);
        e.valueSecondary = (std::min)(totalCpuPptW, (all[i].cpuPct / 100.0) * dynCpuW * 2.2 + 0.4);
        e.hIcon = GetOrCacheAppIcon(e.exePath);
        outCpu.push_back(e);
    }

    // Top 3 GPU (score combining active 3D % and dedicated VRAM MB)
    std::sort(all.begin(), all.end(), [](const GroupAgg& a, const GroupAgg& b) {
        double scoreA = a.gpuUtilPct * 25.0 + a.gpuMemMb;
        double scoreB = b.gpuUtilPct * 25.0 + b.gpuMemMb;
        if (std::abs(scoreA - scoreB) > 1.0) return scoreA > scoreB;
        return a.ramMb > b.ramMb;
    });
    outGpu.clear();
    for (size_t i = 0; i < all.size() && outGpu.size() < 3; i++) {
        TopAppEntry e;
        e.name = all[i].displayName;
        e.exePath = all[i].bestExePath;
        e.valuePrimary = all[i].gpuMemMb; // VRAM MB
        e.valueSecondary = (std::min)(100.0, all[i].gpuUtilPct); // GPU %
        e.hIcon = GetOrCacheAppIcon(e.exePath);
        outGpu.push_back(e);
    }
}

static void ReadAllSensors(TelemetryData& out, bool updateProcesses) {
    // 0. Exact Task-Manager CPU GHz & CPU % via PDH
    InitPdhCounters();
    if (g_pdhQuery) {
        PdhCollectQueryData(g_pdhQuery);
        if (g_pdhCpuPerf) {
            PDH_FMT_COUNTERVALUE val = {};
            if (PdhGetFormattedCounterValue(g_pdhCpuPerf, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
                double ghz = 3.801 * (val.doubleValue / 100.0);
                if (ghz > 0.8 && ghz < 5.5) {
                    g_lastTaskMgrGhz = ghz;
                }
            }
        }
        if (g_pdhCpuUtil) {
            PDH_FMT_COUNTERVALUE val = {};
            if (PdhGetFormattedCounterValue(g_pdhCpuUtil, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
                double u = val.doubleValue;
                if (u >= 0.0 && u <= 100.0) g_lastCpuUtilPct = u;
            }
        }
    }
    out.cpuClockGhz = g_lastTaskMgrGhz;
    out.cpuLoadPct  = g_lastCpuUtilPct;

    // 1. CPU via AMD Ryzen Master SMU Shared Memory
    HANDLE hMap = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, L"Local\\954b280e-5529-4492-bab2-bb7a502552f8");
    if (!hMap) {
        hMap = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, L"954b280e-5529-4492-bab2-bb7a502552f8");
    }
    if (hMap) {
        BYTE* pBuf = (BYTE*)MapViewOfFile(hMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 512);
        if (pBuf) {
            pBuf[0] = 1;
            HANDLE hEv = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\954b280e-5529-4492-bab2-bb7a502552f8_event");
            if (!hEv) {
                hEv = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"954b280e-5529-4492-bab2-bb7a502552f8_event");
            }
            if (hEv) {
                SetEvent(hEv);
                CloseHandle(hEv);
            }
            Sleep(75);
            double clkMhz = *(double*)(pBuf + 0x1C8);
            double voltV  = *(double*)(pBuf + 0x1D0);
            double tempC  = *(double*)(pBuf + 0x1D8);
            float pptW    = *(float*)(pBuf + 0x1E0);
            float edcA    = *(float*)(pBuf + 0x1E4);
            float tdcA    = *(float*)(pBuf + 0x1E8);
            if (clkMhz > 500.0 && clkMhz < 6000.0) out.cpuSmuPeakGhz = clkMhz / 1000.0;
            if (voltV > 0.2 && voltV < 2.2) out.cpuVoltV = voltV;
            if (tempC > 5.0 && tempC < 125.0) out.cpuTempC = tempC;
            if (pptW > 1.0f && pptW < 250.0f) out.cpuPptW = (double)pptW;
            if (edcA >= 0.0f && edcA < 300.0f) out.cpuEdcA = (double)edcA;
            if (tdcA >= 0.0f && tdcA < 300.0f) out.cpuTdcA = (double)tdcA;
            UnmapViewOfFile(pBuf);
        }
        CloseHandle(hMap);
    }

    // Check CPU Throttling:
    // Ryzen 7 5800X TjMax = 90.0 C (thermal throttling begins at >= 88.5 C)
    // PBO Manual Limits: PPT = 135 W, TDC = 90 A, EDC = 130 A
    out.cpuThermalThrottle  = (out.cpuTempC >= 88.5);
    out.cpuPowerLimitActive = (out.cpuPptW >= 132.5 || out.cpuTdcA >= 88.0 || out.cpuEdcA >= 127.5);

    // 2. GPU via AMD ADL2
    InitAdl();
    if (g_adlContext && pADL2_New_QueryPMLogData_Get) {
        ADLPMLogDataOutput pmData = {};
        if (pADL2_New_QueryPMLogData_Get(g_adlContext, 0, &pmData) == 0) {
            out.gpuTempC     = pmData.sensors[8 * 2 + 1];
            out.gpuVramTempC = pmData.sensors[9 * 2 + 1];
            out.gpuFanRpm    = pmData.sensors[14 * 2 + 1];
            out.gpuLoadPct   = pmData.sensors[19 * 2 + 1];
            out.gpuHotspotC  = pmData.sensors[27 * 2 + 1];
            out.gpuSocW      = pmData.sensors[40 * 2 + 1];
            out.gpuAsicW     = pmData.sensors[41 * 2 + 1];
            out.gpuBoardW    = pmData.sensors[73 * 2 + 1];
        }
    }
    out.gpuThermalThrottle = (out.gpuHotspotC >= 102 || out.gpuTempC >= 87);

    // 3. RAM via GlobalMemoryStatusEx
    MEMORYSTATUSEX memInfo = {};
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        out.ramTotalGb = (double)memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        out.ramUsedGb  = (double)(memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        out.ramUsedPct = (int)memInfo.dwMemoryLoad;
    }

    // 4. Restsystem (MSI B550 + 4x RAM + ROG Ryujin AIO + 5 Luefter + 2x SSD: 36-45 W) & EVGA 650 G2 Efficiency
    double loadFactor = ((out.cpuPptW - 35.0) / 100.0) * 0.5 + ((double)out.gpuLoadPct / 100.0) * 0.5;
    if (loadFactor < 0.0) loadFactor = 0.0;
    if (loadFactor > 1.0) loadFactor = 1.0;
    out.restSystemW = 36.0 + (loadFactor * 9.0);
    out.totalDcW    = out.cpuPptW + (double)out.gpuBoardW + out.restSystemW;
    double eff      = (out.totalDcW < 100.0) ? 0.885 : ((out.totalDcW < 400.0) ? 0.915 : 0.900);
    out.wallAcW     = out.totalDcW / eff;

    // 5. Top 3 Processes for RAM, CPU, GPU
    if (updateProcesses || out.topRamApps.empty()) {
        UpdateTopProcesses(out.topRamApps, out.topCpuApps, out.topGpuApps, out.cpuPptW);
    }

    out.valid = true;
}

static void AddRoundedRect(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL radius) {
    if (radius <= 0.5f) {
        path.AddRectangle(RectF(x, y, w, h));
        return;
    }
    REAL d = radius * 2.0f;
    if (d > w) d = w;
    if (d > h) d = h;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// Calibrated for Ryzen 7 5800X (TjMax 90°C) & RX 9070 XT:
// <= 66.0°C = Emerald Green (60-66°C is completely cool & unthrottled!)
// 66.1-77.0°C = Warm Amber/Gold (Normal Gaming)
// 77.1-86.0°C = Vivid Orange (Heavy Load)
// >= 86.1°C = Ruby Red (Near 90°C Thermal Limit)
static void GetTempBadgeColors(double tempC, Color& outBg, Color& outBorder, Color& outText, BYTE alphaScale = 255) {
    auto sc = [&](BYTE a) -> BYTE { return (BYTE)((int)a * (int)alphaScale / 255); };
    if (tempC <= 66.0) {
        // Cool Mint Emerald Green
        outBg     = Color(sc(195), 8, 72, 54);
        outBorder = Color(sc(230), 16, 185, 129);
        outText   = Color(sc(255), 125, 248, 192);
    } else if (tempC <= 77.0) {
        // Normal Gaming Warm Amber / Gold
        outBg     = Color(sc(195), 92, 52, 10);
        outBorder = Color(sc(230), 245, 158, 11);
        outText   = Color(sc(255), 253, 218, 105);
    } else if (tempC <= 86.0) {
        // Heavy Load Vivid Orange
        outBg     = Color(sc(205), 115, 38, 12);
        outBorder = Color(sc(235), 249, 115, 22);
        outText   = Color(sc(255), 255, 170, 110);
    } else {
        // Hot / Throttling Ruby Red
        outBg     = Color(sc(220), 125, 18, 24);
        outBorder = Color(sc(245), 239, 68, 68);
        outText   = Color(sc(255), 255, 135, 135);
    }
}

static bool IsFullscreenAppActive(HWND hTray) {
    HWND hFg = GetForegroundWindow();
    if (!hFg || hFg == hTray || hFg == g_hudHwnd || hFg == g_flyoutHwnd || hFg == g_hoverHwnd) return false;
    wchar_t cls[128] = {};
    GetClassNameW(hFg, cls, 127);
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Shell_TrayWnd") == 0) {
        return false;
    }
    LONG style = GetWindowLongW(hFg, GWL_STYLE);
    if (style & WS_CAPTION) return false;
    RECT rcFg = {}, rcMon = {};
    GetWindowRect(hFg, &rcFg);
    HMONITOR hMon = MonitorFromWindow(hTray, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi)) {
        rcMon = mi.rcMonitor;
        if (rcFg.left <= rcMon.left && rcFg.top <= rcMon.top &&
            rcFg.right >= rcMon.right && rcFg.bottom >= rcMon.bottom) {
            return true;
        }
    }
    return false;
}

static void DrawHIconInGraphics(Graphics& g, HICON hIcon, int x, int y, int sizePx) {
    if (!hIcon) return;
    HDC hdc = g.GetHDC();
    if (hdc) {
        DrawIconEx(hdc, x, y, hIcon, sizePx, sizePx, 0, nullptr, DI_NORMAL);
        g.ReleaseHDC(hdc);
    }
}

// ============================================================================
// 1. TASKBAR PILL RENDERER (Authentic Warm Dark Design + RAM App Icons)
// ============================================================================
static void RenderHud(HWND hwnd) {
    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hTray || !IsWindowVisible(hTray) || IsFullscreenAppActive(hTray)) {
        ShowWindow(hwnd, SW_HIDE);
        return;
    }

    RECT rcTray = {};
    GetWindowRect(hTray, &rcTray);
    HWND hNotify = FindWindowExW(hTray, nullptr, L"TrayNotifyWnd", nullptr);
    RECT rcNotify = {};
    if (hNotify) {
        GetWindowRect(hNotify, &rcNotify);
    } else {
        rcNotify = rcTray;
        rcNotify.left = rcTray.right - 350;
    }

    UINT dpi = SafeGetDpiForWindow(hTray);
    if (dpi < 96) dpi = 96;
    float scale = (float)dpi / 96.0f;

    int trayH = rcTray.bottom - rcTray.top;
    if (trayH <= 20) trayH = (int)std::round(48.0f * scale);

    int marginV = (int)std::round(5.0f * scale);
    int pillH   = trayH - (marginV * 2);
    if (pillH < 26) pillH = 26;

    TelemetryData d;
    std::vector<float> spark;
    double todayWh = 0, todayEur = 0, todaySec = 0, todayPeakW = 0;
    double monthWh = 0, monthEur = 0, monthSec = 0;
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        d = g_data;
        spark.assign(g_wattHistory.begin(), g_wattHistory.end());
        GetTodayAndMonthStats(todayWh, todayEur, todaySec, todayPeakW, monthWh, monthEur, monthSec);
    }
    if (!d.valid) return;

    wchar_t sWallW[32], sCpuW[32], sCpuTemp[32], sCpuGhz[32];
    wchar_t sGpuW[32], sGpuTemp[32], sGpuLoad[32];
    wchar_t sRamGb[32], sRamPct[32], sMonthEur[32];

    swprintf(sWallW, 32, L"%.0f W", d.wallAcW);
    swprintf(sCpuW,  32, L"%.0fW", d.cpuPptW);
    if (d.cpuThermalThrottle) {
        swprintf(sCpuTemp, 32, L"\x26A0 %.0f\x00B0" L"C", d.cpuTempC);
    } else {
        swprintf(sCpuTemp, 32, L"%.0f\x00B0" L"C", d.cpuTempC);
    }
    swprintf(sCpuGhz, 32, L"%.2fG", d.cpuClockGhz);

    swprintf(sGpuW,    32, L"%dW", d.gpuBoardW);
    swprintf(sGpuTemp, 32, L"%d\x00B0" L"C", d.gpuTempC);
    swprintf(sGpuLoad, 32, L"%d%%", d.gpuLoadPct);

    swprintf(sRamGb,   32, L"%.1fG", d.ramUsedGb);
    swprintf(sRamPct,  32, L"%d%%", d.ramUsedPct);
    swprintf(sMonthEur, 32, L"%.2f \x20AC", monthEur);

    HDC hdcScreen = GetDC(nullptr);
    FontFamily fontFam(L"Segoe UI Variable Display");
    const FontFamily* pFam = fontFam.IsAvailable() ? &fontFam : FontFamily::GenericSansSerif();
    Font fontBold(pFam, 9.2f * scale, FontStyleBold, UnitPixel);
    Font fontMed(pFam,  8.6f * scale, FontStyleRegular, UnitPixel);
    Font fontTag(pFam,  7.6f * scale, FontStyleBold, UnitPixel);

    REAL c1W = 0, c2W = 0, c3W = 0, c4W = 0, c5W = 0;
    REAL sparkW = 34.0f * scale;
    REAL badgePadX = 6.0f * scale;

    RectF bWallW, bCpuLbl, bCpuW, bCpuTemp, bCpuGhz;
    RectF bGpuLbl, bGpuW, bGpuTemp, bGpuLoad;
    RectF bRamLbl, bRamGb, bRamPct, bEur;

    {
        Graphics gMeasure(hdcScreen);
        gMeasure.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        PointF origin(0, 0);
        gMeasure.MeasureString(sWallW, -1, &fontBold, origin, &bWallW);

        gMeasure.MeasureString(L"CPU", -1, &fontTag, origin, &bCpuLbl);
        gMeasure.MeasureString(sCpuW, -1, &fontBold, origin, &bCpuW);
        gMeasure.MeasureString(sCpuTemp, -1, &fontBold, origin, &bCpuTemp);
        gMeasure.MeasureString(sCpuGhz, -1, &fontMed, origin, &bCpuGhz);

        gMeasure.MeasureString(L"GPU", -1, &fontTag, origin, &bGpuLbl);
        gMeasure.MeasureString(sGpuW, -1, &fontBold, origin, &bGpuW);
        gMeasure.MeasureString(sGpuTemp, -1, &fontBold, origin, &bGpuTemp);
        gMeasure.MeasureString(sGpuLoad, -1, &fontMed, origin, &bGpuLoad);

        gMeasure.MeasureString(L"RAM", -1, &fontTag, origin, &bRamLbl);
        gMeasure.MeasureString(sRamGb, -1, &fontBold, origin, &bRamGb);
        gMeasure.MeasureString(sRamPct, -1, &fontBold, origin, &bRamPct);

        gMeasure.MeasureString(sMonthEur, -1, &fontBold, origin, &bEur);
    }

    c1W = (18.0f * scale) + bWallW.Width + (6.0f * scale) + sparkW + (10.0f * scale);
    c2W = (10.0f * scale) + bCpuLbl.Width + (4.0f * scale) + bCpuW.Width + (5.0f * scale)
          + (bCpuTemp.Width + badgePadX * 2.0f) + (5.0f * scale) + bCpuGhz.Width + (9.0f * scale);
    c3W = (10.0f * scale) + bGpuLbl.Width + (4.0f * scale) + bGpuW.Width + (5.0f * scale)
          + (bGpuTemp.Width + badgePadX * 2.0f) + (5.0f * scale) + bGpuLoad.Width + (9.0f * scale);
    c4W = (10.0f * scale) + bRamLbl.Width + (4.0f * scale) + bRamGb.Width + (5.0f * scale)
          + (bRamPct.Width + badgePadX * 1.6f) + (9.0f * scale);
    c5W = (20.0f * scale) + bEur.Width + (9.0f * scale);

    REAL outerPadX = 5.0f * scale;
    REAL cardGap   = 4.0f * scale;
    int pillW = (int)std::ceil(outerPadX * 2.0f + c1W + c2W + c3W + c4W + c5W + cardGap * 4.0f);

    int rightGapPx = (int)std::round((float)g_settings.rightMarginDip * scale);
    int posX = rcNotify.left - pillW - rightGapPx;
    int posY = rcTray.top + marginV;

    g_lastPillScreenRect = {posX, posY, posX + pillW, posY + pillH};

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = pillW;
    bmi.bmiHeader.biHeight = -pillH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    {
        Graphics g(hdcMem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(0, 0, 0, 0));

        // Authentic Warm Obsidian Outer Container (#191918)
        GraphicsPath outerPath;
        AddRoundedRect(outerPath, 0.5f, 0.5f, (REAL)pillW - 1.0f, (REAL)pillH - 1.0f, 6.5f * scale);
        SolidBrush outerBg(Color((BYTE)g_settings.pillOpacity, 25, 25, 24));
        g.FillPath(&outerBg, &outerPath);

        Pen outerBorder(g_flyoutVisible ? Color(245, 217, 119, 87) : Color(220, 62, 59, 54), 1.2f * scale);
        g.DrawPath(&outerBorder, &outerPath);

        // Warm Palette Brushes
        SolidBrush brTextPrimary(Color(255, 245, 244, 239)); // #F5F4EF Warm Parchment
        SolidBrush brTextMuted(Color(240, 172, 167, 158));   // #ACA79E Warm Stone
        SolidBrush brGold(Color(255, 245, 185, 65));         // Electric Warm Gold
        SolidBrush brCpuTag(Color(255, 95, 228, 168));       // Mint
        SolidBrush brGpuTag(Color(255, 196, 152, 255));      // Lavender
        SolidBrush brRamTag(Color(255, 112, 200, 255));      // Sky
        SolidBrush brAccentTerra(Color(255, 217, 119, 87));  // #D97757 Warm Terracotta

        StringFormat sfLeft;
        sfLeft.SetAlignment(StringAlignmentNear);
        sfLeft.SetLineAlignment(StringAlignmentCenter);
        sfLeft.SetFormatFlags(StringFormatFlagsNoWrap);

        StringFormat sfCenter;
        sfCenter.SetAlignment(StringAlignmentCenter);
        sfCenter.SetLineAlignment(StringAlignmentCenter);
        sfCenter.SetFormatFlags(StringFormatFlagsNoWrap);

        REAL cardY = 3.0f * scale;
        REAL cardH = (REAL)pillH - 6.0f * scale;
        REAL curX  = outerPadX;
        REAL midY  = (REAL)pillH * 0.48f;

        auto drawMicroCard = [&](int idx, REAL x, REAL w, Color barColor, double loadRatio) {
            g_cardRectsLocal[idx] = {(LONG)x, 0, (LONG)(x + w), pillH};
            bool isHov = (g_hoveredCardIdx == idx);

            GraphicsPath cp;
            AddRoundedRect(cp, x, cardY, w, cardH, 4.5f * scale);
            SolidBrush cb(isHov ? Color(252, 48, 45, 41) : Color(242, 36, 35, 33));
            g.FillPath(&cb, &cp);

            Pen cPen(isHov ? Color(245, 217, 119, 87) : Color(185, 58, 55, 50), isHov ? 1.3f * scale : 1.0f);
            g.DrawPath(&cPen, &cp);

            REAL barMargin = 5.0f * scale;
            REAL maxBarW = w - barMargin * 2.0f;
            if (maxBarW > 4.0f) {
                if (loadRatio < 0.04) loadRatio = 0.04;
                if (loadRatio > 1.0) loadRatio = 1.0;
                REAL fillW = (REAL)(maxBarW * loadRatio);
                REAL barY = cardY + cardH - 2.6f * scale;
                SolidBrush brTrack(Color(110, 20, 20, 19));
                g.FillRectangle(&brTrack, x + barMargin, barY, maxBarW, 1.8f * scale);
                SolidBrush brFill(barColor);
                g.FillRectangle(&brFill, x + barMargin, barY, fillW, 1.8f * scale);
            }
        };

        // --- CARD 0: WALL POWER + SPARKLINE ---
        drawMicroCard(0, curX, c1W, Color(235, 245, 185, 65), d.wallAcW / 550.0);
        {
            REAL lx = curX + 6.0f * scale;
            PointF boltPts[6] = {
                PointF(lx + 5.5f * scale, midY - 6.5f * scale),
                PointF(lx + 1.0f * scale, midY + 0.5f * scale),
                PointF(lx + 4.5f * scale, midY + 0.5f * scale),
                PointF(lx + 3.0f * scale, midY + 6.5f * scale),
                PointF(lx + 8.5f * scale, midY - 1.0f * scale),
                PointF(lx + 5.0f * scale, midY - 1.0f * scale)
            };
            g.FillPolygon(&brGold, boltPts, 6);
            lx += 11.0f * scale;

            g.DrawString(sWallW, -1, &fontBold, PointF(lx, midY), &sfLeft, &brTextPrimary);
            lx += bWallW.Width + 4.0f * scale;

            REAL spH = 12.0f * scale;
            REAL spY = midY - spH * 0.5f;
            if (spark.size() >= 2) {
                float minW = 70.0f, maxW = 280.0f;
                for (float v : spark) {
                    if (v < minW) minW = v - 10.0f;
                    if (v > maxW) maxW = v + 20.0f;
                }
                if (maxW - minW < 40.0f) maxW = minW + 40.0f;

                std::vector<PointF> pts;
                int n = (int)spark.size();
                pts.reserve(n);
                for (int i = 0; i < n; i++) {
                    float px = lx + (sparkW * (float)i / (float)(n - 1));
                    float norm = (spark[i] - minW) / (maxW - minW);
                    if (norm < 0.0f) norm = 0.0f;
                    if (norm > 1.0f) norm = 1.0f;
                    float py = (spY + spH) - norm * spH;
                    pts.push_back(PointF(px, py));
                }
                Pen sparkPen(Color(245, 245, 185, 65), 1.4f * scale);
                g.DrawLines(&sparkPen, pts.data(), (INT)pts.size());
            }
        }
        curX += c1W + cardGap;

        // --- CARD 1: CPU ---
        Color cpuBg, cpuBorder, cpuText;
        GetTempBadgeColors(d.cpuTempC, cpuBg, cpuBorder, cpuText);
        drawMicroCard(1, curX, c2W, cpuBorder, d.cpuPptW / 135.0);
        {
            REAL lx = curX + 7.0f * scale;
            g.DrawString(L"CPU", -1, &fontTag, PointF(lx, midY), &sfLeft, &brCpuTag);
            lx += bCpuLbl.Width + 3.0f * scale;

            g.DrawString(sCpuW, -1, &fontBold, PointF(lx, midY), &sfLeft, &brTextPrimary);
            lx += bCpuW.Width + 4.0f * scale;

            REAL bw = bCpuTemp.Width + badgePadX * 1.8f;
            REAL bh = 15.0f * scale;
            REAL by = midY - bh * 0.5f;
            GraphicsPath bp;
            AddRoundedRect(bp, lx, by, bw, bh, 4.0f * scale);
            SolidBrush bBrush(cpuBg);
            Pen bPen(cpuBorder, 1.0f * scale);
            g.FillPath(&bBrush, &bp);
            g.DrawPath(&bPen, &bp);
            SolidBrush bTxt(cpuText);
            g.DrawString(sCpuTemp, -1, &fontBold, RectF(lx, by, bw, bh), &sfCenter, &bTxt);
            lx += bw + 4.5f * scale;

            g.DrawString(sCpuGhz, -1, &fontMed, PointF(lx, midY), &sfLeft, &brTextMuted);
        }
        curX += c2W + cardGap;

        // --- CARD 2: GPU ---
        Color gpuBg, gpuBorder, gpuText;
        GetTempBadgeColors((double)d.gpuTempC, gpuBg, gpuBorder, gpuText);
        drawMicroCard(2, curX, c3W, Color(235, 186, 135, 255), (double)d.gpuBoardW / 330.0);
        {
            REAL lx = curX + 7.0f * scale;
            g.DrawString(L"GPU", -1, &fontTag, PointF(lx, midY), &sfLeft, &brGpuTag);
            lx += bGpuLbl.Width + 3.0f * scale;

            g.DrawString(sGpuW, -1, &fontBold, PointF(lx, midY), &sfLeft, &brTextPrimary);
            lx += bGpuW.Width + 4.0f * scale;

            REAL bw = bGpuTemp.Width + badgePadX * 1.8f;
            REAL bh = 15.0f * scale;
            REAL by = midY - bh * 0.5f;
            GraphicsPath bp;
            AddRoundedRect(bp, lx, by, bw, bh, 4.0f * scale);
            SolidBrush bBrush(gpuBg);
            Pen bPen(gpuBorder, 1.0f * scale);
            g.FillPath(&bBrush, &bp);
            g.DrawPath(&bPen, &bp);
            SolidBrush bTxt(gpuText);
            g.DrawString(sGpuTemp, -1, &fontBold, RectF(lx, by, bw, bh), &sfCenter, &bTxt);
            lx += bw + 4.5f * scale;

            g.DrawString(sGpuLoad, -1, &fontMed, PointF(lx, midY), &sfLeft, &brTextMuted);
        }
        curX += c3W + cardGap;

        // --- CARD 3: RAM (Clean & Minimalist) ---
        drawMicroCard(3, curX, c4W, Color(235, 95, 195, 255), (double)d.ramUsedPct / 100.0);
        {
            REAL lx = curX + 7.0f * scale;
            g.DrawString(L"RAM", -1, &fontTag, PointF(lx, midY), &sfLeft, &brRamTag);
            lx += bRamLbl.Width + 3.0f * scale;

            g.DrawString(sRamGb, -1, &fontBold, PointF(lx, midY), &sfLeft, &brTextPrimary);
            lx += bRamGb.Width + 4.0f * scale;

            REAL bw = bRamPct.Width + badgePadX * 1.4f;
            REAL bh = 15.0f * scale;
            REAL by = midY - bh * 0.5f;
            GraphicsPath bp;
            AddRoundedRect(bp, lx, by, bw, bh, 4.0f * scale);
            SolidBrush bBrush(Color(175, 14, 52, 82));
            Pen bPen(Color(215, 56, 189, 248), 1.0f * scale);
            g.FillPath(&bBrush, &bp);
            g.DrawPath(&bPen, &bp);
            SolidBrush bTxt(Color(255, 165, 230, 255));
            g.DrawString(sRamPct, -1, &fontBold, RectF(lx, by, bw, bh), &sfCenter, &bTxt);
        }
        curX += c4W + cardGap;

        // --- CARD 4: MONTHLY EURO + CHART ICON ---
        drawMicroCard(4, curX, c5W, Color(245, 217, 119, 87), (std::min)(1.0, monthEur / 15.0 + 0.15));
        {
            REAL lx = curX + 6.5f * scale;
            REAL iconBaseY = midY + 5.0f * scale;
            g.FillRectangle(&brAccentTerra, lx, iconBaseY - 4.5f * scale, 2.5f * scale, 4.5f * scale);
            g.FillRectangle(&brAccentTerra, lx + 3.5f * scale, iconBaseY - 7.5f * scale, 2.5f * scale, 7.5f * scale);
            g.FillRectangle(&brAccentTerra, lx + 7.0f * scale, iconBaseY - 10.5f * scale, 2.5f * scale, 10.5f * scale);
            lx += 12.5f * scale;

            g.DrawString(sMonthEur, -1, &fontBold, PointF(lx, midY), &sfLeft, &brTextPrimary);
        }
    }

    if (GetParent(hwnd) != hTray) {
        SetParent(hwnd, hTray);
    }
    POINT ptSrc = {0, 0};
    POINT ptDst = (GetParent(hwnd) == hTray)
        ? POINT{posX - rcTray.left, posY - rcTray.top}
        : POINT{posX, posY};
    SIZE sizeWnd = {pillW, pillH};
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    if (!IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

// ============================================================================
// 2. ULTRA-MINIMALIST POWER HOVER CAPSULE (Top-3 App Icon + Name + Live Numbers)
// ============================================================================
static void RenderHoverCard(HWND hwnd) {
    if (g_flyoutVisible || g_hoverAnimProgress <= 0.02f ||
        (g_activeHoverIdx != 1 && g_activeHoverIdx != 2 && g_activeHoverIdx != 3)) {
        ShowWindow(hwnd, SW_HIDE);
        return;
    }

    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    UINT dpi = SafeGetDpiForWindow(hTray);
    if (dpi < 96) dpi = 96;
    float scale = (float)dpi / 96.0f;

    // Ease-out cubic curve for buttery 60-FPS motion
    float t = g_hoverAnimProgress;
    float ease = 1.0f - std::pow(1.0f - t, 3.0f);
    BYTE masterAlpha = (BYTE)std::round(255.0f * ease);
    auto scA = [&](int a) -> BYTE { return (BYTE)((a * (int)masterAlpha) / 255); };

    int wndW = (int)std::round(224.0f * scale);
    int wndH = (int)std::round(104.0f * scale);

    // Center directly above the active CPU / GPU / RAM card
    int targetX = (int)std::round(g_hoverAnimCenterX - wndW * 0.5f);
    if (targetX < 20) targetX = 20;
    if (targetX + wndW > 3820) targetX = 3820 - wndW;

    int slideOffsetPx = (int)std::round((1.0f - ease) * 12.0f * scale);
    int targetY = g_lastPillScreenRect.top - wndH - (int)std::round(8.0f * scale) + slideOffsetPx;

    TelemetryData d;
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        d = g_data;
    }
    if (!d.valid) return;

    const std::vector<TopAppEntry>* pApps = nullptr;
    Color barCol(scA(240), 56, 189, 248);
    if (g_activeHoverIdx == 1) {
        pApps = &d.topCpuApps;
        barCol = Color(scA(240), 16, 185, 129);
    } else if (g_activeHoverIdx == 2) {
        pApps = &d.topGpuApps;
        barCol = Color(scA(240), 186, 135, 255);
    } else {
        pApps = &d.topRamApps;
        barCol = Color(scA(240), 56, 189, 248);
    }

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = wndW;
    bmi.bmiHeader.biHeight = -wndH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    {
        Graphics g(hdcMem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(0, 0, 0, 0));

        FontFamily fontFam(L"Segoe UI Variable Display");
        const FontFamily* pFam = fontFam.IsAvailable() ? &fontFam : FontFamily::GenericSansSerif();
        Font fName(pFam, 8.8f * scale, FontStyleBold, UnitPixel);
        Font fVal(pFam,  8.8f * scale, FontStyleBold, UnitPixel);

        // Minimalist Warm Obsidian Capsule (#191918)
        GraphicsPath bgPath;
        AddRoundedRect(bgPath, 1.0f, 1.0f, (REAL)wndW - 2.0f, (REAL)wndH - 2.0f, 9.0f * scale);
        SolidBrush brBg(Color(scA(248), 25, 25, 24));
        g.FillPath(&brBg, &bgPath);

        Pen penBorder(Color(scA(225), 217, 119, 87), 1.2f * scale);
        g.DrawPath(&penBorder, &bgPath);

        SolidBrush brWhite(Color(scA(255), 245, 244, 239));
        SolidBrush brMuted(Color(scA(245), 205, 200, 190));
        SolidBrush brCard(Color(scA(235), 36, 35, 33));
        Pen penCard(Color(scA(170), 58, 55, 50), 1.0f);

        REAL pad  = 6.5f * scale;
        REAL curY = pad;
        REAL rowH = 28.0f * scale;
        REAL rowW = (REAL)wndW - pad * 2.0f;

        StringFormat sfLeft;
        sfLeft.SetAlignment(StringAlignmentNear);
        sfLeft.SetLineAlignment(StringAlignmentCenter);
        sfLeft.SetFormatFlags(StringFormatFlagsNoWrap);
        sfLeft.SetTrimming(StringTrimmingEllipsisCharacter);

        StringFormat sfRight;
        sfRight.SetAlignment(StringAlignmentFar);
        sfRight.SetLineAlignment(StringAlignmentCenter);
        sfRight.SetFormatFlags(StringFormatFlagsNoWrap);

        int count = pApps ? (int)(std::min)((size_t)3, pApps->size()) : 0;
        for (int i = 0; i < count; i++) {
            const auto& app = (*pApps)[i];
            GraphicsPath rp;
            AddRoundedRect(rp, pad, curY, rowW, rowH, 5.5f * scale);
            g.FillPath(&brCard, &rp);
            g.DrawPath(&penCard, &rp);

            int icoSz = (int)std::round(18.0f * scale);
            int icoX  = (int)std::round(pad + 6.5f * scale);
            int icoY  = (int)std::round(curY + (rowH - icoSz) * 0.5f);
            if (masterAlpha > 90) {
                DrawHIconInGraphics(g, app.hIcon, icoX, icoY, icoSz);
            }

            // App Name right next to the icon
            RectF rcName(pad + 29.0f * scale, curY, 96.0f * scale, rowH - 2.5f * scale);
            g.DrawString(app.name.c_str(), -1, &fName, rcName, &sfLeft, &brWhite);

            wchar_t sData[64];
            double barRatio = 0.1;
            if (g_activeHoverIdx == 1) { // CPU: % + W
                swprintf(sData, 64, L"%.1f%% \x00B7 %.0fW", app.valuePrimary, app.valueSecondary);
                barRatio = app.valuePrimary / 35.0;
            } else if (g_activeHoverIdx == 2) { // GPU: VRAM + %
                if (app.valuePrimary >= 1024.0) {
                    swprintf(sData, 64, L"%.1fG \x00B7 %.0f%%", app.valuePrimary / 1024.0, app.valueSecondary);
                } else {
                    swprintf(sData, 64, L"%.0fM \x00B7 %.0f%%", app.valuePrimary, app.valueSecondary);
                }
                barRatio = app.valuePrimary / 2048.0;
            } else { // RAM: GB/MB + %
                if (app.valuePrimary >= 1024.0) {
                    swprintf(sData, 64, L"%.2fG \x00B7 %.0f%%", app.valuePrimary / 1024.0, app.valueSecondary);
                } else {
                    swprintf(sData, 64, L"%.0fM \x00B7 %.0f%%", app.valuePrimary, app.valueSecondary);
                }
                barRatio = app.valuePrimary / 4096.0;
            }

            RectF rcTxt(pad + 118.0f * scale, curY, rowW - 124.5f * scale, rowH - 2.5f * scale);
            g.DrawString(sData, -1, &fVal, rcTxt, &sfRight, &brMuted);

            if (barRatio < 0.05) barRatio = 0.05;
            if (barRatio > 1.0) barRatio = 1.0;
            REAL barX = pad + 29.0f * scale;
            REAL barW = rowW - 35.5f * scale;
            REAL barY = curY + rowH - 3.8f * scale;
            SolidBrush brTrack(Color(scA(130), 20, 20, 19));
            g.FillRectangle(&brTrack, barX, barY, barW, 2.0f * scale);
            SolidBrush brFill(barCol);
            g.FillRectangle(&brFill, barX, barY, (REAL)(barW * barRatio), 2.0f * scale);

            curY += rowH + 3.5f * scale;
        }
    }

    POINT ptSrc = {0, 0};
    POINT ptDst = {targetX, targetY};
    SIZE sizeWnd = {wndW, wndH};
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    SetWindowPos(hwnd, HWND_TOPMOST, targetX, targetY, wndW, wndH, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

// ============================================================================
// 3. CLICK FLYOUT DASHBOARD (Daily & Monthly Euro Charts + Z: Config)
// ============================================================================
static void RenderFlyout(HWND hwnd) {
    if (!g_flyoutVisible) {
        ShowWindow(hwnd, SW_HIDE);
        return;
    }

    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    UINT dpi = SafeGetDpiForWindow(hTray);
    if (dpi < 96) dpi = 96;
    float scale = (float)dpi / 96.0f;

    int wndW = (int)std::round(760.0f * scale);
    int wndH = (int)std::round(530.0f * scale);

    int posX = g_lastPillScreenRect.right - wndW;
    if (posX < 20) posX = 20;
    int posY = g_lastPillScreenRect.top - wndH - (int)std::round(14.0f * scale);

    TelemetryData d;
    std::vector<float> spark;
    double todayWh = 0, todayEur = 0, todaySec = 0, todayPeakW = 0;
    double monthWh = 0, monthEur = 0, monthSec = 0;
    double hourlyWh[24] = {0};
    std::vector<DayRecord> dailyHist;
    std::wstring logPathW;
    double tariff = 0.32;
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        d = g_data;
        spark.assign(g_wattHistory.begin(), g_wattHistory.end());
        GetTodayAndMonthStats(todayWh, todayEur, todaySec, todayPeakW, monthWh, monthEur, monthSec);
        for (int i = 0; i < 24; i++) hourlyWh[i] = g_log.hourlyWhToday[i];
        dailyHist = g_log.daily;
        logPathW  = g_log.logFilePath;
        tariff    = g_log.eurPerKwh;
    }

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = wndW;
    bmi.bmiHeader.biHeight = -wndH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    {
        Graphics g(hdcMem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(0, 0, 0, 0));

        FontFamily fontFam(L"Segoe UI Variable Display");
        const FontFamily* pFam = fontFam.IsAvailable() ? &fontFam : FontFamily::GenericSansSerif();

        Font fTitle(pFam,    13.5f * scale, FontStyleBold, UnitPixel);
        Font fSub(pFam,       9.0f * scale, FontStyleRegular, UnitPixel);
        Font fKpiLbl(pFam,    8.2f * scale, FontStyleBold, UnitPixel);
        Font fKpiBig(pFam,   16.0f * scale, FontStyleBold, UnitPixel);
        Font fKpiSub(pFam,    8.5f * scale, FontStyleRegular, UnitPixel);
        Font fSecTitle(pFam,  9.2f * scale, FontStyleBold, UnitPixel);
        Font fAxis(pFam,      7.8f * scale, FontStyleRegular, UnitPixel);
        Font fBtn(pFam,       8.5f * scale, FontStyleBold, UnitPixel);

        // Warm Obsidian Window Surface (#191918)
        GraphicsPath wndPath;
        AddRoundedRect(wndPath, 1.0f, 1.0f, (REAL)wndW - 2.0f, (REAL)wndH - 2.0f, 12.0f * scale);
        LinearGradientBrush wndGrad(PointF(0, 0), PointF(0, (REAL)wndH),
                                    Color(252, 28, 27, 25), Color(254, 20, 19, 18));
        g.FillPath(&wndGrad, &wndPath);

        Pen wndBorder(Color(230, 217, 119, 87), 1.4f * scale);
        g.DrawPath(&wndBorder, &wndPath);

        SolidBrush brWhite(Color(255, 245, 244, 239));
        SolidBrush brMuted(Color(225, 168, 163, 154));
        SolidBrush brGold(Color(255, 245, 185, 65));
        SolidBrush brMint(Color(255, 52, 211, 153));
        SolidBrush brTerra(Color(255, 217, 119, 87));
        SolidBrush brPurple(Color(255, 192, 145, 255));
        SolidBrush brPanel(Color(235, 34, 33, 31));
        Pen penPanel(Color(185, 58, 55, 50), 1.0f * scale);

        REAL pad = 18.0f * scale;
        REAL curY = 15.0f * scale;

        g.DrawString(L"\x26A1 MAIN PC \x2013 LIVE STROMVERBRAUCH & KOSTEN-ANALYSE (CLAUDE EDITION)", -1, &fTitle, PointF(pad, curY), &brWhite);
        g.DrawString(L"EVGA SuperNOVA 650 G2 (80+ Gold) \x00B7 Ryzen 7 5800X \x00B7 XFX RX 9070 XT \x00B7 32GB DDR4 \x00B7 ROG Ryujin AIO (95%) + 5 L\u00FCfter",
                     -1, &fSub, PointF(pad, curY + 21.0f * scale), &brMuted);

        // Close button [X]
        REAL closeSz = 26.0f * scale;
        REAL closeX  = (REAL)wndW - pad - closeSz;
        REAL closeY  = curY + 2.0f * scale;
        g_rcBtnClose = {(LONG)closeX, (LONG)closeY, (LONG)(closeX + closeSz), (LONG)(closeY + closeSz)};
        {
            GraphicsPath cp;
            AddRoundedRect(cp, closeX, closeY, closeSz, closeSz, 6.0f * scale);
            SolidBrush cb(Color(200, 48, 45, 42));
            g.FillPath(&cb, &cp);
            g.DrawPath(&penPanel, &cp);
            StringFormat sfC;
            sfC.SetAlignment(StringAlignmentCenter);
            sfC.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"\x2715", -1, &fSecTitle, RectF(closeX, closeY, closeSz, closeSz), &sfC, &brWhite);
        }

        curY += 46.0f * scale;

        // --- 4 KPI CARDS ---
        REAL kpiGap = 10.0f * scale;
        REAL kpiW   = ((REAL)wndW - pad * 2.0f - kpiGap * 3.0f) / 4.0f;
        REAL kpiH   = 78.0f * scale;

        auto drawKpi = [&](int idx, const wchar_t* lbl, const wchar_t* bigVal, const wchar_t* sub1, const wchar_t* sub2, Brush* accentBr, Color topBarCol) {
            REAL kx = pad + idx * (kpiW + kpiGap);
            GraphicsPath kp;
            AddRoundedRect(kp, kx, curY, kpiW, kpiH, 8.0f * scale);
            g.FillPath(&brPanel, &kp);
            g.DrawPath(&penPanel, &kp);

            SolidBrush topBr(topBarCol);
            g.FillRectangle(&topBr, kx + 10.0f * scale, curY + 1.0f * scale, kpiW - 20.0f * scale, 2.2f * scale);

            g.DrawString(lbl, -1, &fKpiLbl, PointF(kx + 10.0f * scale, curY + 7.0f * scale), accentBr);
            g.DrawString(bigVal, -1, &fKpiBig, PointF(kx + 10.0f * scale, curY + 21.0f * scale), &brWhite);
            g.DrawString(sub1, -1, &fKpiSub, PointF(kx + 10.0f * scale, curY + 46.0f * scale), &brMuted);
            g.DrawString(sub2, -1, &fKpiSub, PointF(kx + 10.0f * scale, curY + 60.0f * scale), &brMuted);
        };

        wchar_t k1Big[64], k1S1[64], k1S2[64];
        double effPct = (d.wallAcW > 1.0) ? (d.totalDcW / d.wallAcW) * 100.0 : 91.5;
        swprintf(k1Big, 64, L"%.1f Watt", d.wallAcW);
        swprintf(k1S1,  64, L"Intern DC: %.1f W (%.1f%% Eff.)", d.totalDcW, effPct);
        swprintf(k1S2,  64, L"CPU %.0fW + GPU %dW + Rest %.0fW", d.cpuPptW, d.gpuBoardW, d.restSystemW);
        drawKpi(0, L"STECKDOSE JETZT (AC)", k1Big, k1S1, k1S2, &brGold, Color(230, 245, 185, 65));

        wchar_t k2Big[64], k2S1[64], k2S2[64];
        int tHrs = (int)(todaySec / 3600.0);
        int tMin = ((int)todaySec % 3600) / 60;
        swprintf(k2Big, 64, L"%.3f \x20AC", todayEur);
        swprintf(k2S1,  64, L"Verbrauch: %.1f Wh (%.3f kWh)", todayWh, todayWh / 1000.0);
        swprintf(k2S2,  64, L"Aktiv: %dh %02dm \x00B7 Peak: %.0f W", tHrs, tMin, todayPeakW);
        drawKpi(1, L"HEUTE (TAGESKOSTEN)", k2Big, k2S1, k2S2, &brMint, Color(230, 52, 211, 153));

        wchar_t k3Big[64], k3S1[64], k3S2[64];
        double avgW = (g_log.totalSeconds > 0) ? (g_log.totalWh / (g_log.totalSeconds / 3600.0)) : d.wallAcW;
        double projMonthEur = ((avgW * 6.0 * 30.0) / 1000.0) * tariff;
        swprintf(k3Big, 64, L"%.2f \x20AC", monthEur);
        swprintf(k3S1,  64, L"Monat bisher: %.3f kWh", monthWh / 1000.0);
        swprintf(k3S2,  64, L"Prognose (6h/Tag): ~%.2f \x20AC/Mon", projMonthEur);
        drawKpi(2, L"DIESER MONAT (SUMME)", k3Big, k3S1, k3S2, &brTerra, Color(230, 217, 119, 87));

        wchar_t k4Big[64], k4S1[64], k4S2[64];
        swprintf(k4Big, 64, L"%.0f\x00B0" L"C / %d\x00B0" L"C", d.cpuTempC, d.gpuTempC);
        swprintf(k4S1,  64, L"5800X: %.2f GHz \x00B7 %.0f%% Last", d.cpuClockGhz, d.cpuLoadPct);
        swprintf(k4S2,  64, L"9070XT: Hotspot %d\x00B0" L"C \x00B7 RAM %.1fG", d.gpuHotspotC, d.ramUsedGb);
        drawKpi(3, L"CPU & GPU (KEIN DROSSELN)", k4Big, k4S1, k4S2, &brPurple, Color(230, 186, 135, 255));

        curY += kpiH + 12.0f * scale;

        // --- CHART 1: 60-SECOND LIVE WATTAGE CURVE ---
        REAL ch1W = (REAL)wndW - pad * 2.0f;
        REAL ch1H = 110.0f * scale;
        {
            GraphicsPath p1;
            AddRoundedRect(p1, pad, curY, ch1W, ch1H, 8.0f * scale);
            g.FillPath(&brPanel, &p1);
            g.DrawPath(&penPanel, &p1);

            g.DrawString(L"LIVE-LEISTUNGSKURVE (LETZTE 60 SEKUNDEN IN WATT AN DER STECKDOSE)", -1, &fSecTitle, PointF(pad + 12.0f * scale, curY + 8.0f * scale), &brWhite);

            REAL gx0 = pad + 42.0f * scale;
            REAL gy0 = curY + 30.0f * scale;
            REAL gw  = ch1W - 56.0f * scale;
            REAL gh  = ch1H - 42.0f * scale;

            float minW = 80.0f, maxW = 300.0f;
            for (float v : spark) {
                if (v < minW) minW = std::floor(v / 20.0f) * 20.0f;
                if (v > maxW) maxW = std::ceil(v / 50.0f) * 50.0f;
            }
            if (maxW - minW < 60.0f) maxW = minW + 60.0f;

            Pen gridPen(Color(70, 100, 96, 88), 1.0f);
            for (int r = 0; r <= 2; r++) {
                REAL ry = gy0 + gh * ((float)r / 2.0f);
                g.DrawLine(&gridPen, gx0, ry, gx0 + gw, ry);
                float val = maxW - (maxW - minW) * ((float)r / 2.0f);
                wchar_t sAxis[16];
                swprintf(sAxis, 16, L"%.0fW", val);
                g.DrawString(sAxis, -1, &fAxis, PointF(pad + 8.0f * scale, ry - 6.0f * scale), &brMuted);
            }

            if (spark.size() >= 2) {
                int n = (int)spark.size();
                std::vector<PointF> pts(n);
                for (int i = 0; i < n; i++) {
                    float px = gx0 + gw * ((float)i / (float)(n - 1));
                    float norm = (spark[i] - minW) / (maxW - minW);
                    if (norm < 0.0f) norm = 0.0f;
                    if (norm > 1.0f) norm = 1.0f;
                    float py = (gy0 + gh) - norm * gh;
                    pts[i] = PointF(px, py);
                }

                GraphicsPath areaPath;
                areaPath.AddLines(pts.data(), n);
                areaPath.AddLine(pts[n - 1].X, gy0 + gh, pts[0].X, gy0 + gh);
                areaPath.CloseFigure();
                LinearGradientBrush areaGrad(PointF(gx0, gy0), PointF(gx0, gy0 + gh),
                                             Color(115, 245, 185, 65), Color(8, 245, 185, 65));
                g.FillPath(&areaGrad, &areaPath);

                Pen linePen(Color(255, 245, 185, 65), 2.0f * scale);
                g.DrawLines(&linePen, pts.data(), n);
            }
        }

        curY += ch1H + 12.0f * scale;

        // --- CHART 2 (LEFT): 24-HOUR TODAY CONSUMPTION & CHART 3 (RIGHT): DAILY/MONTHLY EURO PROGRESSION ---
        REAL halfW = (ch1W - 12.0f * scale) * 0.5f;
        REAL ch2H  = 186.0f * scale;

        // Left: 24-Hour Bar Chart
        {
            GraphicsPath pL;
            AddRoundedRect(pL, pad, curY, halfW, ch2H, 8.0f * scale);
            g.FillPath(&brPanel, &pL);
            g.DrawPath(&penPanel, &pL);

            g.DrawString(L"HEUTE NACH UHRZEIT (00\u201323 UHR IN WH / \u20AC)", -1, &fSecTitle, PointF(pad + 12.0f * scale, curY + 8.0f * scale), &brWhite);

            REAL bx0 = pad + 14.0f * scale;
            REAL by0 = curY + 34.0f * scale;
            REAL bwTotal = halfW - 28.0f * scale;
            REAL bhTotal = ch2H - 58.0f * scale;

            double maxHWh = 50.0;
            for (int h = 0; h < 24; h++) {
                if (hourlyWh[h] > maxHWh) maxHWh = hourlyWh[h] * 1.15;
            }

            SYSTEMTIME stNow;
            GetLocalTime(&stNow);

            REAL slotW = bwTotal / 24.0f;
            REAL barW  = slotW * 0.68f;
            for (int h = 0; h < 24; h++) {
                REAL x = bx0 + h * slotW + (slotW - barW) * 0.5f;
                SolidBrush brSlot(Color(55, 52, 50, 46));
                g.FillRectangle(&brSlot, x, by0, barW, bhTotal);

                if (hourlyWh[h] > 0.01) {
                    REAL fillH = (REAL)((hourlyWh[h] / maxHWh) * bhTotal);
                    if (fillH < 3.0f * scale) fillH = 3.0f * scale;
                    if (fillH > bhTotal) fillH = bhTotal;
                    REAL y = by0 + bhTotal - fillH;
                    SolidBrush brBar((h == stNow.wHour) ? Color(255, 52, 211, 153) : Color(210, 38, 165, 125));
                    g.FillRectangle(&brBar, x, y, barW, fillH);
                }

                if (h % 4 == 0 || h == 23) {
                    wchar_t sHr[8];
                    swprintf(sHr, 8, L"%02d", h);
                    g.DrawString(sHr, -1, &fAxis, PointF(x - 2.0f * scale, by0 + bhTotal + 4.0f * scale), &brMuted);
                }
            }
        }

        // Right: Daily & Monthly Euro Progression Chart
        REAL rx0 = pad + halfW + 12.0f * scale;
        {
            GraphicsPath pR;
            AddRoundedRect(pR, rx0, curY, halfW, ch2H, 8.0f * scale);
            g.FillPath(&brPanel, &pR);
            g.DrawPath(&penPanel, &pR);

            g.DrawString(L"TAGES- & MONATSVERLAUF IN EURO (AUF Z: GELOGGT)", -1, &fSecTitle, PointF(rx0 + 12.0f * scale, curY + 8.0f * scale), &brWhite);

            REAL bx0 = rx0 + 14.0f * scale;
            REAL by0 = curY + 34.0f * scale;
            REAL bwTotal = halfW - 28.0f * scale;
            REAL bhTotal = ch2H - 58.0f * scale;

            int numBars = 10;
            int startIdx = (int)dailyHist.size() > numBars ? (int)dailyHist.size() - numBars : 0;
            int count = (int)dailyHist.size() - startIdx;

            double maxDayEur = 0.25;
            for (int i = startIdx; i < (int)dailyHist.size(); i++) {
                double e = (dailyHist[i].wh / 1000.0) * tariff;
                if (e * 1.2 > maxDayEur) maxDayEur = e * 1.2;
            }

            REAL slotW = bwTotal / (REAL)numBars;
            REAL barW  = slotW * 0.62f;

            StringFormat sfC;
            sfC.SetAlignment(StringAlignmentCenter);

            for (int slot = 0; slot < numBars; slot++) {
                REAL x = bx0 + slot * slotW + (slotW - barW) * 0.5f;
                SolidBrush brSlot(Color(55, 52, 50, 46));
                g.FillRectangle(&brSlot, x, by0, barW, bhTotal);

                if (slot < count) {
                    const auto& rec = dailyHist[startIdx + slot];
                    double eur = (rec.wh / 1000.0) * tariff;
                    REAL fillH = (REAL)((eur / maxDayEur) * bhTotal);
                    if (fillH < 4.0f * scale) fillH = 4.0f * scale;
                    if (fillH > bhTotal) fillH = bhTotal;
                    REAL y = by0 + bhTotal - fillH;

                    GraphicsPath bp;
                    AddRoundedRect(bp, x, y, barW, fillH, 3.0f * scale);
                    SolidBrush brBar(Color(245, 217, 119, 87));
                    g.FillPath(&brBar, &bp);

                    wchar_t sVal[16];
                    if (eur < 0.10) swprintf(sVal, 16, L"%.2f\x20AC", eur);
                    else swprintf(sVal, 16, L"%.2f\x20AC", eur);
                    g.DrawString(sVal, -1, &fAxis, PointF(x + barW * 0.5f, y - 13.0f * scale), &sfC, &brWhite);

                    std::wstring shortDate = Utf8ToWide(rec.date.size() >= 10 ? rec.date.substr(5, 5) : rec.date);
                    g.DrawString(shortDate.c_str(), -1, &fAxis, PointF(x + barW * 0.5f, by0 + bhTotal + 4.0f * scale), &sfC, &brMuted);
                }
            }
        }

        curY += ch2H + 12.0f * scale;

        // --- FOOTER: LOG PATH & INTERACTIVE BUTTONS ---
        std::wstring dispPath = L"Z:-Log: " + logPathW;
        if (dispPath.size() > 48) {
            dispPath = L"Z:-Log: ..." + logPathW.substr(logPathW.size() - 40);
        }
        g.DrawString(dispPath.c_str(), -1, &fSub, PointF(pad, curY + 6.0f * scale), &brMuted);

        auto drawFooterBtn = [&](REAL bx, REAL bw, const wchar_t* txt, RECT& outRc, bool accent) {
            REAL bh = 26.0f * scale;
            outRc = {(LONG)bx, (LONG)curY, (LONG)(bx + bw), (LONG)(curY + bh)};
            GraphicsPath bp;
            AddRoundedRect(bp, bx, curY, bw, bh, 6.0f * scale);
            SolidBrush bFill(accent ? Color(215, 165, 82, 55) : Color(220, 44, 42, 39));
            Pen bPen(accent ? Color(245, 225, 130, 95) : Color(200, 75, 72, 66), 1.0f * scale);
            g.FillPath(&bFill, &bp);
            g.DrawPath(&bPen, &bp);
            StringFormat sfC;
            sfC.SetAlignment(StringAlignmentCenter);
            sfC.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(txt, -1, &fBtn, RectF(bx, curY, bw, bh), &sfC, &brWhite);
        };

        REAL rightX = (REAL)wndW - pad;
        REAL wBtn4 = 68.0f * scale;
        rightX -= wBtn4;
        drawFooterBtn(rightX, wBtn4, L"Konsole", g_rcBtnConsole, false);

        rightX -= (6.0f * scale + 104.0f * scale);
        wchar_t sTariffBtn[32];
        swprintf(sTariffBtn, 32, L"Tarif: %.2f \x20AC", tariff);
        drawFooterBtn(rightX, 104.0f * scale, sTariffBtn, g_rcBtnTariff, false);

        rightX -= (6.0f * scale + 112.0f * scale);
        drawFooterBtn(rightX, 112.0f * scale, L"AIO-Pumpe 95%", g_rcBtnPumpMax, false);

        rightX -= (6.0f * scale + 104.0f * scale);
        drawFooterBtn(rightX, 104.0f * scale, L"Z:-Ordner", g_rcBtnOpenFolder, false);

        rightX -= (6.0f * scale + 124.0f * scale);
        drawFooterBtn(rightX, 124.0f * scale, L"Log-Pfad \x00E4ndern...", g_rcBtnChangePath, true);
    }

    POINT ptSrc = {0, 0};
    POINT ptDst = {posX, posY};
    SIZE sizeWnd = {wndW, wndH};
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    SetWindowPos(hwnd, HWND_TOPMOST, posX, posY, wndW, wndH, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

static void PromptChangeLogFilePath(HWND hwndOwner) {
    wchar_t fileBuf[MAX_PATH] = {};
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        wcsncpy(fileBuf, g_log.logFilePath.c_str(), MAX_PATH - 1);
    }
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndOwner;
    ofn.lpstrFilter = L"JSON Strom-Logdatei (*.json)\0*.json\0Alle Dateien (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Speicherort fuer PC-Stromverbrauch Log-Datei waehlen (z.B. auf Z:)";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"json";

    if (GetSaveFileNameW(&ofn)) {
        std::wstring newLog = fileBuf;
        std::wstring newCsv = newLog;
        size_t dot = newCsv.find_last_of(L'.');
        if (dot != std::wstring::npos) {
            newCsv = newCsv.substr(0, dot) + L"_daily.csv";
        } else {
            newCsv += L"_daily.csv";
        }
        {
            std::lock_guard<std::mutex> lock(g_dataMutex);
            g_log.logFilePath = newLog;
            g_log.csvFilePath = newCsv;
            SavePowerConfig();
            SavePowerLogToDisk();
        }
        if (g_flyoutHwnd) RenderFlyout(g_flyoutHwnd);
    }
}

static LRESULT CALLBACK FlyoutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_LBUTTONUP) {
        POINT pt = {LOWORD(lParam), HIWORD(lParam)};
        if (PtInRect(&g_rcBtnClose, pt)) {
            g_flyoutVisible = false;
            ShowWindow(hwnd, SW_HIDE);
            if (g_hudHwnd) RenderHud(g_hudHwnd);
            return 0;
        }
        if (PtInRect(&g_rcBtnChangePath, pt)) {
            PromptChangeLogFilePath(hwnd);
            return 0;
        }
        if (PtInRect(&g_rcBtnOpenFolder, pt)) {
            std::wstring folder;
            {
                std::lock_guard<std::mutex> lock(g_dataMutex);
                folder = g_log.logFilePath;
            }
            size_t slash = folder.find_last_of(L"\\/");
            if (slash != std::wstring::npos) folder = folder.substr(0, slash);
            ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (PtInRect(&g_rcBtnPumpMax, pt)) {
            ShellExecuteW(nullptr, L"open", L".\\8_FanControl_AIO_Pumpe_Max_Kurve_Aktivieren.bat", nullptr, L".", SW_SHOWNORMAL);
            return 0;
        }
        if (PtInRect(&g_rcBtnTariff, pt)) {
            {
                std::lock_guard<std::mutex> lock(g_dataMutex);
                const double rates[] = {0.28, 0.30, 0.32, 0.35, 0.38, 0.40};
                int nextIdx = 0;
                for (int i = 0; i < 6; i++) {
                    if (std::abs(g_log.eurPerKwh - rates[i]) < 0.009) {
                        nextIdx = (i + 1) % 6;
                        break;
                    }
                }
                g_log.eurPerKwh = rates[nextIdx];
                SavePowerConfig();
                SavePowerLogToDisk();
            }
            RenderFlyout(hwnd);
            if (g_hudHwnd) RenderHud(g_hudHwnd);
            return 0;
        }
        if (PtInRect(&g_rcBtnConsole, pt)) {
            ShellExecuteW(nullptr, L"open", L"6_Live_Stromverbrauch_Monitor.bat", nullptr, L".", SW_SHOWNORMAL);
            return 0;
        }
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        g_flyoutVisible = false;
        ShowWindow(hwnd, SW_HIDE);
        if (g_hudHwnd) RenderHud(g_hudHwnd);
        return 0;
    }
    if (msg == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK HoverWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int g_forcedHoverIdx = -1;

static LRESULT CALLBACK HudWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_WINDOWPOSCHANGING) {
        // Keep HUD at HWND_TOP inside Shell_TrayWnd
        WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(lParam);
        if (wp && !g_stopThread.load()) {
            wp->flags &= ~SWP_HIDEWINDOW;
            wp->hwndInsertAfter = HWND_TOP;
        }
        return 0;
    }
    if (msg == WM_SHOWWINDOW && wParam == FALSE && !g_stopThread.load()) {
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        g_flyoutVisible = !g_flyoutVisible;
        if (g_flyoutVisible && g_hoverHwnd) {
            ShowWindow(g_hoverHwnd, SW_HIDE);
        }
        if (g_flyoutHwnd) {
            RenderFlyout(g_flyoutHwnd);
        }
        RenderHud(hwnd);
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        ShellExecuteW(nullptr, L"open", L"6_Live_Stromverbrauch_Monitor.bat", nullptr, L".", SW_SHOWNORMAL);
        return 0;
    }
    if (msg == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_HAND));
        return TRUE;
    }
    if (msg == WM_USER + 10) {
        RenderHud(hwnd);
        if (g_flyoutVisible && g_flyoutHwnd) {
            RenderFlyout(g_flyoutHwnd);
        }
        return 0;
    }
    if (msg == WM_USER + 20) {
        g_forcedHoverIdx = (int)wParam;
        return 0;
    }
    if (msg == WM_TIMER && wParam == 2) {
        // Keep HUD at HWND_TOP inside Shell_TrayWnd's child Z-order (0.000 ms flicker on Win+D)
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        // 60 FPS Cursor Hit-Testing + Smooth Hover Animation Tick
        POINT ptCur = {};
        GetCursorPos(&ptCur);
        int foundIdx = -1;
        if (!g_flyoutVisible && PtInRect(&g_lastPillScreenRect, ptCur)) {
            int localX = ptCur.x - g_lastPillScreenRect.left;
            for (int i = 0; i < 5; i++) {
                if (localX >= g_cardRectsLocal[i].left - 3 && localX <= g_cardRectsLocal[i].right + 3) {
                    foundIdx = i;
                    break;
                }
            }
        }
        if (g_forcedHoverIdx >= 0 && g_forcedHoverIdx < 5) {
            foundIdx = g_forcedHoverIdx;
        }

        if (foundIdx != g_hoveredCardIdx) {
            g_hoveredCardIdx = foundIdx;
            if (foundIdx >= 1 && foundIdx <= 3) {
                g_activeHoverIdx = foundIdx;
            }
            RenderHud(hwnd);
            if (foundIdx >= 1 && foundIdx <= 3 && g_hoverHwnd) {
                RenderHoverCard(g_hoverHwnd);
            }
        }

        float targetProg = (g_hoveredCardIdx >= 1 && g_hoveredCardIdx <= 3 && !g_flyoutVisible) ? 1.0f : 0.0f;
        if (g_activeHoverIdx >= 1 && g_activeHoverIdx <= 3) {
            RECT rcCard = g_cardRectsLocal[g_activeHoverIdx];
            float targetCenterX = (float)g_lastPillScreenRect.left + (rcCard.left + rcCard.right) * 0.5f;
            if (g_hoverAnimCenterX <= 10.0f) {
                g_hoverAnimCenterX = targetCenterX;
            } else if (std::abs(targetCenterX - g_hoverAnimCenterX) > 0.5f) {
                g_hoverAnimCenterX += (targetCenterX - g_hoverAnimCenterX) * 0.28f;
                if (g_hoverAnimProgress > 0.02f && g_hoverHwnd) {
                    RenderHoverCard(g_hoverHwnd);
                }
            }
        }

        if (std::abs(g_hoverAnimProgress - targetProg) > 0.008f) {
            float step = (targetProg > g_hoverAnimProgress) ? 0.14f : 0.16f;
            if (targetProg > g_hoverAnimProgress) {
                g_hoverAnimProgress = (std::min)(1.0f, g_hoverAnimProgress + step);
            } else {
                g_hoverAnimProgress = (std::max)(0.0f, g_hoverAnimProgress - step);
            }
            if (g_hoverHwnd) RenderHoverCard(g_hoverHwnd);
        } else {
            g_hoverAnimProgress = targetProg;
            if (g_hoverAnimProgress <= 0.01f && g_hoverHwnd && IsWindowVisible(g_hoverHwnd)) {
                ShowWindow(g_hoverHwnd, SW_HIDE);
            } else if (g_hoverAnimProgress > 0.9f && g_hoverHwnd) {
                static int s_hoverTick = 0;
                if (++s_hoverTick % 30 == 0) {
                    RenderHoverCard(g_hoverHwnd);
                }
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

#ifdef WH_MOD
static void LoadWhSettings() {
    g_settings.updateIntervalMs = Wh_GetIntSetting(L"UpdateIntervalMs");
    if (g_settings.updateIntervalMs < 250) g_settings.updateIntervalMs = 1000;
    g_settings.rightMarginDip = Wh_GetIntSetting(L"RightMarginPx");
    if (g_settings.rightMarginDip < 0) g_settings.rightMarginDip = 12;
    g_settings.pillOpacity = Wh_GetIntSetting(L"PillOpacity");
    if (g_settings.pillOpacity <= 0 || g_settings.pillOpacity > 255) g_settings.pillOpacity = 248;
    g_settings.showCpuClock = Wh_GetIntSetting(L"ShowCpuClock") != 0;
}
#endif

static void WorkerThreadProc(bool isStandalone) {
    SafeEnableDpiAwareness();

    HANDLE hTakeoverEvent = CreateEventW(nullptr, TRUE, FALSE, L"Local\\MainPcCleanSlateHudTakeoverEvent");
    if (!isStandalone && hTakeoverEvent) {
        SetEvent(hTakeoverEvent);
    }

    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        LoadPowerLog();
    }

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wcHud = {};
    wcHud.cbSize = sizeof(wcHud);
    wcHud.lpfnWndProc = HudWndProc;
    wcHud.hInstance = hInst;
    wcHud.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_HAND);
    wcHud.lpszClassName = L"MainPcCleanSlateHudClass";
    RegisterClassExW(&wcHud);

    WNDCLASSEXW wcFly = {};
    wcFly.cbSize = sizeof(wcFly);
    wcFly.lpfnWndProc = FlyoutWndProc;
    wcFly.hInstance = hInst;
    wcFly.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wcFly.lpszClassName = L"MainPcCleanSlateFlyoutClass";
    RegisterClassExW(&wcFly);

    WNDCLASSEXW wcHov = {};
    wcHov.cbSize = sizeof(wcHov);
    wcHov.lpfnWndProc = HoverWndProc;
    wcHov.hInstance = hInst;
    wcHov.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wcHov.lpszClassName = L"MainPcPowerHoverClass";
    RegisterClassExW(&wcHov);

    HWND hTrayOwner = FindWindowW(L"Shell_TrayWnd", nullptr);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"MainPcCleanSlateHudClass",
        L"Main PC Live Power HUD",
        WS_POPUP,
        0, 0, 10, 10,
        hTrayOwner, nullptr, hInst, nullptr
    );
    if (hTrayOwner && hwnd) {
        SetParent(hwnd, hTrayOwner);
    }
    g_hudHwnd = hwnd;

    HWND hFlyout = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"MainPcCleanSlateFlyoutClass",
        L"Main PC Power Analytics Flyout",
        WS_POPUP,
        0, 0, 10, 10,
        nullptr, nullptr, hInst, nullptr
    );
    g_flyoutHwnd = hFlyout;

    HWND hHover = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        L"MainPcPowerHoverClass",
        L"Main PC Power Hover Card",
        WS_POPUP,
        0, 0, 10, 10,
        nullptr, nullptr, hInst, nullptr
    );
    g_hoverHwnd = hHover;

    SetTimer(hwnd, 1, 1000, nullptr); // 1s Sensor & Energy Loop
    SetTimer(hwnd, 2, 16,   nullptr); // 60 FPS Smooth Hover Animation Loop

    // Initial sensor & process sample
    {
        TelemetryData sample = {};
        ReadAllSensors(sample, true);
        std::lock_guard<std::mutex> lock(g_dataMutex);
        g_data = sample;
        for (int i = 0; i < 60; i++) g_wattHistory.push_back((float)sample.wallAcW);
        AccumulateEnergySample(sample.wallAcW, 1.0);
        SavePowerLogToDisk();
    }
    RenderHud(hwnd);

    ULONGLONG lastSampleTick = GetTickCount64();
    int tickCounter = 0;

    MSG msg;
    while (!g_stopThread.load()) {
        if (isStandalone && hTakeoverEvent && WaitForSingleObject(hTakeoverEvent, 0) == WAIT_OBJECT_0) {
            break;
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_stopThread.store(true);
                break;
            }
            if (msg.message == WM_TIMER && msg.wParam == 1) {
                ULONGLONG nowTick = GetTickCount64();
                double dtSec = (double)(nowTick - lastSampleTick) / 1000.0;
                lastSampleTick = nowTick;
                tickCounter++;

                TelemetryData sample = {};
                {
                    std::lock_guard<std::mutex> lock(g_dataMutex);
                    sample.topRamApps = g_data.topRamApps;
                    sample.topCpuApps = g_data.topCpuApps;
                    sample.topGpuApps = g_data.topGpuApps;
                }
                bool needProcScan = (g_hoveredCardIdx >= 1 && g_hoveredCardIdx <= 3) ||
                                    (g_hoverAnimProgress > 0.05f) ||
                                    (tickCounter % 8 == 0);
                ReadAllSensors(sample, needProcScan);

                {
                    std::lock_guard<std::mutex> lock(g_dataMutex);
                    g_data = sample;
                    g_wattHistory.push_back((float)sample.wallAcW);
                    while (g_wattHistory.size() > 60) g_wattHistory.pop_front();

                    AccumulateEnergySample(sample.wallAcW, dtSec);
                    if (tickCounter % 15 == 0) {
                        SavePowerLogToDisk();
                    }
                }
                RenderHud(hwnd);
                if (g_flyoutVisible && g_flyoutHwnd) {
                    RenderFlyout(g_flyoutHwnd);
                }
                if (g_hoverAnimProgress > 0.05f && g_hoverHwnd) {
                    RenderHoverCard(g_hoverHwnd);
                }
            } else {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        Sleep(10);
    }

    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        SavePowerLogToDisk();
    }

    if (hHover) DestroyWindow(hHover);
    if (hFlyout) DestroyWindow(hFlyout);
    if (hwnd) DestroyWindow(hwnd);
    g_hoverHwnd  = nullptr;
    g_flyoutHwnd = nullptr;
    g_hudHwnd    = nullptr;
    UnregisterClassW(L"MainPcPowerHoverClass", hInst);
    UnregisterClassW(L"MainPcCleanSlateFlyoutClass", hInst);
    UnregisterClassW(L"MainPcCleanSlateHudClass", hInst);
    if (g_pdhQuery) {
        PdhCloseQuery(g_pdhQuery);
        g_pdhQuery = nullptr;
    }
    CleanupAdl();
    GdiplusShutdown(gdiplusToken);
    if (hTakeoverEvent) CloseHandle(hTakeoverEvent);
}

#ifdef WH_MOD
BOOL Wh_ModInit() {
    Wh_Log(L"Initializing Main PC Live Power & Hardware HUD Mod v3.0");
    LoadWhSettings();
    g_stopThread.store(false);
    g_workerThread = std::thread(WorkerThreadProc, false);
    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"Uninitializing Main PC Live Power & Hardware HUD Mod v3.0");
    g_stopThread.store(true);
    if (g_workerThread.joinable()) {
        g_workerThread.join();
    }
}

void Wh_ModSettingsChanged() {
    LoadWhSettings();
    if (g_hudHwnd) {
        PostMessageW(g_hudHwnd, WM_USER + 10, 0, 0);
    }
}
#else
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    HANDLE hSingleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\MainPcCleanSlateHudStandaloneMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }
    WorkerThreadProc(true);
    if (hSingleInstanceMutex) CloseHandle(hSingleInstanceMutex);
    return 0;
}
#endif
