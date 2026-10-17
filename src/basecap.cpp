// EnshroudedBaseCap - caps Enshrouded's real (pre-frame-generation) frame rate.
//
// Loaded by EnshroudedVKFG as an add-on (Addons= in EnshroudedVKFG.ini). The game
// hands every frame it renders to vkQueuePresentKHR exported by Streamline's
// sl.interposer.dll. DLSS-G sets that call's "skip" flag and presents its own
// generated frames on a separate path, so waiting inside the export paces only the
// real frames, and frame generation multiplies whatever rate comes out.
//
// F9 / F11 (configurable) step the cap down / up while the game has focus, and a
// small readout shows the new value for a moment. The value is saved to the ini.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <immintrin.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "MinHook.h"
#include "version.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace {

typedef int32_t(__stdcall* PFN_vkQueuePresentKHR)(void* queue, const void* presentInfo);

constexpr wchar_t kSection[] = L"EnshroudedBaseCap";
constexpr wchar_t kReadoutClass[] = L"EnshroudedBaseCapReadout";
constexpr UINT_PTR kTimerTick = 1;
constexpr UINT_PTR kTimerHide = 2;

struct Config {
    int cap = 40;  // 0 = off
    int step = 5;
    int minCap = 20;
    int maxCap = 120;
    int keyDown = VK_F9;
    int keyUp = VK_F11;
    int keyToggle = 0;
    int waitAfterPresent = 0;
    int readout = 1;
    int readoutMs = 2000;
};

HMODULE g_self;
Config g_cfg;
wchar_t g_iniPath[MAX_PATH];
FILE* g_log;
SRWLOCK g_logLock = SRWLOCK_INIT;
LONGLONG g_freq;

std::atomic<int> g_cap{0};
std::atomic<int64_t> g_interval{0};  // QPC ticks per real frame, 0 = no cap
std::atomic<uint64_t> g_presents{0};
std::atomic<int64_t> g_waitTicks{0};
PFN_vkQueuePresentKHR g_origPresent;

// Presenting thread only.
LONGLONG g_next;
thread_local HANDLE t_timer;

LONGLONG Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    AcquireSRWLockExclusive(&g_logLock);
    fprintf(g_log, "%02u:%02u:%02u.%03u | ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    ReleaseSRWLockExclusive(&g_logLock);
}

int ReadInt(const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(kSection, key, def, g_iniPath);
}

void LoadConfig() {
    Config d;
    g_cfg.cap = ReadInt(L"BaseFpsCap", d.cap);
    g_cfg.step = ReadInt(L"Step", d.step);
    g_cfg.minCap = ReadInt(L"MinCap", d.minCap);
    g_cfg.maxCap = ReadInt(L"MaxCap", d.maxCap);
    g_cfg.keyDown = ReadInt(L"KeyDown", d.keyDown);
    g_cfg.keyUp = ReadInt(L"KeyUp", d.keyUp);
    g_cfg.keyToggle = ReadInt(L"KeyToggle", d.keyToggle);
    g_cfg.waitAfterPresent = ReadInt(L"WaitAfterPresent", d.waitAfterPresent);
    g_cfg.readout = ReadInt(L"ShowReadout", d.readout);
    g_cfg.readoutMs = ReadInt(L"ReadoutMs", d.readoutMs);

    // Below 10 fps a single wait would outlast the frame-gen mod's 200 ms "presents
    // stopped" check and it would tear DLSS-G down.
    if (g_cfg.minCap < 10) g_cfg.minCap = 10;
    if (g_cfg.maxCap < g_cfg.minCap) g_cfg.maxCap = g_cfg.minCap;
    if (g_cfg.maxCap > 1000) g_cfg.maxCap = 1000;
    if (g_cfg.step < 1) g_cfg.step = 1;
    if (g_cfg.cap != 0 && g_cfg.cap < g_cfg.minCap) g_cfg.cap = g_cfg.minCap;
    if (g_cfg.cap > g_cfg.maxCap) g_cfg.cap = g_cfg.maxCap;
    if (g_cfg.readoutMs < 300) g_cfg.readoutMs = 300;
}

void ApplyCap(int cap) {
    g_cap.store(cap);
    g_interval.store(cap > 0 ? g_freq / cap : 0);
}

void SaveCap() {
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", g_cap.load());
    WritePrivateProfileStringW(kSection, L"BaseFpsCap", buf, g_iniPath);
}

// ---------------------------------------------------------------------------
// Pacing

void WaitUntil(LONGLONG target) {
    if (!t_timer) {
        t_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!t_timer) Log("WARN no high-resolution timer on this system - spinning instead");
    }
    // The timer sleeps to within ~1 ms of the target; the rest is spun for accuracy.
    const LONGLONG spin = g_freq / 1000;
    for (;;) {
        LONGLONG remain = target - Now();
        if (remain <= 0) return;
        if (t_timer && remain > spin) {
            LARGE_INTEGER due;
            due.QuadPart = -((remain - spin) * 10000000LL / g_freq);
            if (due.QuadPart < 0 && SetWaitableTimerEx(t_timer, &due, 0, nullptr, nullptr, nullptr, 0))
                WaitForSingleObject(t_timer, 100);
            else
                _mm_pause();
        } else {
            _mm_pause();
        }
    }
}

void Pace() {
    const int64_t interval = g_interval.load(std::memory_order_relaxed);
    if (interval <= 0) {
        g_next = 0;
        return;
    }
    LONGLONG now = Now();
    if (g_next && now < g_next) {
        // A target more than one interval away is left over from a lower cap.
        if (g_next - now > interval) g_next = now + interval;
        g_waitTicks.fetch_add(g_next - now, std::memory_order_relaxed);
        WaitUntil(g_next);
        now = g_next;  // count from the target, not from when the spin ended, so there is no drift
    }
    // A late frame starts a fresh schedule instead of rushing the next one to catch up:
    // frame generation looks best when real frames arrive evenly spaced.
    g_next = now + interval;
}

void LogStatus() {
    static LONGLONG last;
    static uint64_t lastPresents;
    static int64_t lastWait;
    const LONGLONG now = Now();
    const uint64_t presents = g_presents.load(std::memory_order_relaxed);
    const int64_t wait = g_waitTicks.load(std::memory_order_relaxed);
    if (!last) {
        last = now;
        lastPresents = presents;
        lastWait = wait;
        return;
    }
    if (now - last < 10 * g_freq) return;
    const double secs = (double)(now - last) / g_freq;
    const uint64_t n = presents - lastPresents;
    const double avgWaitMs = n ? (double)(wait - lastWait) * 1000.0 / g_freq / n : 0.0;
    Log("STATUS real_fps=%.1f cap=%d avg_wait=%.2fms", n / secs, g_cap.load(), avgWaitMs);
    last = now;
    lastPresents = presents;
    lastWait = wait;
}

int32_t __stdcall HookedQueuePresentKHR(void* queue, const void* presentInfo) {
    static bool first = true;
    if (first) {
        first = false;
        Log("first real frame presented (thread %lu) - pacing is live, cap=%d", GetCurrentThreadId(), g_cap.load());
    }
    if (!g_cfg.waitAfterPresent) Pace();
    const int32_t result = g_origPresent(queue, presentInfo);
    if (g_cfg.waitAfterPresent) Pace();
    g_presents.fetch_add(1, std::memory_order_relaxed);
    LogStatus();
    return result;
}

DWORD WINAPI InstallHook(void*) {
    // EnshroudedVKFG loads add-ons before it brings Streamline up, so wait for the
    // interposer. The game does not present for several seconds after that.
    HMODULE sl = nullptr;
    const ULONGLONG start = GetTickCount64();
    while (!(sl = GetModuleHandleW(L"sl.interposer.dll"))) {
        if (GetTickCount64() - start > 180000) {
            Log("FAIL sl.interposer.dll never loaded - is EnshroudedVKFG (powrprof.dll + Streamline folder) installed?");
            return 0;
        }
        Sleep(5);
    }
    Sleep(250);  // let the loader finish with the module before patching it

    void* target = (void*)GetProcAddress(sl, "vkQueuePresentKHR");
    if (!target) {
        Log("FAIL sl.interposer.dll does not export vkQueuePresentKHR");
        return 0;
    }
    MH_STATUS s = MH_Initialize();
    if (s == MH_OK) s = MH_CreateHook(target, (void*)&HookedQueuePresentKHR, (void**)&g_origPresent);
    if (s == MH_OK) s = MH_EnableHook(target);
    if (s != MH_OK) {
        Log("FAIL hooking sl.interposer.dll!vkQueuePresentKHR at %p: %s", target, MH_StatusToString(s));
        return 0;
    }
    Log("hooked sl.interposer.dll!vkQueuePresentKHR at %p (%lu ms after load)", target,
        (unsigned long)(GetTickCount64() - start));
    return 0;
}

// ---------------------------------------------------------------------------
// Hotkeys and readout

struct Key {
    int vk;
    bool down;
    ULONGLONG nextRepeat;
};

HWND g_readout;
HFONT g_fontBig, g_fontSmall;
int g_fontFor;  // window height the fonts were made for
double g_measuredFps;
LONGLONG g_saveAt;
int g_lastCap;
wchar_t g_downName[32], g_upName[32];

bool GameHasFocus(HWND* out) {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg) return false;
    GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) return false;
    if (out) *out = fg;
    return true;
}

bool Fired(Key& k, bool repeat) {
    if (!k.vk) return false;
    const bool now = (GetAsyncKeyState(k.vk) & 0x8000) != 0;
    const ULONGLONG t = GetTickCount64();
    bool fire = false;
    if (now && !k.down) {
        fire = true;
        k.nextRepeat = t + 400;
    } else if (now && repeat && t >= k.nextRepeat) {
        fire = true;
        k.nextRepeat = t + 110;
    }
    k.down = now;
    return fire;
}

void KeyName(int vk, wchar_t* out, int len) {
    out[0] = 0;
    if (!vk) return;
    LONG scan = (LONG)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16;
    if (!GetKeyNameTextW(scan, out, len)) swprintf(out, len, L"key %d", vk);
}

void ShowReadout(HWND game) {
    if (!g_cfg.readout || !g_readout) return;
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST), &mi);
    const int monW = mi.rcMonitor.right - mi.rcMonitor.left;
    const int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
    const int h = monH * 7 / 100;
    const int w = monH * 40 / 100;
    if (g_fontFor != h) {
        if (g_fontBig) DeleteObject(g_fontBig);
        if (g_fontSmall) DeleteObject(g_fontSmall);
        g_fontBig = CreateFontW(-(h * 42 / 100), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_fontSmall = CreateFontW(-(h * 21 / 100), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                  CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_fontFor = h;
    }
    SetWindowPos(g_readout, HWND_TOPMOST, mi.rcMonitor.left + (monW - w) / 2, mi.rcMonitor.top + monH * 6 / 100,
                 w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_readout, nullptr, FALSE);
    SetTimer(g_readout, kTimerHide, (UINT)g_cfg.readoutMs, nullptr);
}

void PaintReadout(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(18, 20, 26));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    RECT bar = {0, 0, rc.bottom / 14 + 1, rc.bottom};
    HBRUSH accent = CreateSolidBrush(RGB(118, 185, 0));
    FillRect(mem, &bar, accent);
    DeleteObject(accent);

    wchar_t line1[64], line2[128];
    const int cap = g_cap.load();
    if (cap > 0)
        swprintf(line1, 64, L"Base FPS cap: %d", cap);
    else
        swprintf(line1, 64, L"Base FPS cap: off");
    swprintf(line2, 128, L"game rendering %.0f fps   \x00B7   %ls / %ls to change", g_measuredFps, g_downName, g_upName);

    SetBkMode(mem, TRANSPARENT);
    RECT r1 = {0, rc.bottom * 8 / 100, rc.right, rc.bottom * 60 / 100};
    RECT r2 = {0, rc.bottom * 60 / 100, rc.right, rc.bottom * 92 / 100};
    HGDIOBJ oldFont = SelectObject(mem, g_fontBig);
    SetTextColor(mem, RGB(240, 240, 245));
    DrawTextW(mem, line1, -1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, g_fontSmall);
    SetTextColor(mem, RGB(160, 164, 176));
    DrawTextW(mem, line2, -1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, oldFont);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void SetCapFromKey(int cap, HWND game) {
    if (cap == g_cap.load()) {
        ShowReadout(game);  // still show where it is, e.g. pressing F9 at the minimum
        return;
    }
    if (g_cap.load() > 0) g_lastCap = g_cap.load();
    ApplyCap(cap);
    if (cap > 0)
        Log("cap set to %d fps", cap);
    else
        Log("cap turned off");
    g_saveAt = Now() + g_freq;  // save once the keys have been quiet for a second
    ShowReadout(game);
}

void OnTick() {
    static Key down = {g_cfg.keyDown, false, 0}, up = {g_cfg.keyUp, false, 0}, toggle = {g_cfg.keyToggle, false, 0};
    static LONGLONG fpsT;
    static uint64_t fpsN;

    const LONGLONG now = Now();
    if (now - fpsT >= g_freq / 2) {
        const uint64_t n = g_presents.load(std::memory_order_relaxed);
        if (fpsT) g_measuredFps = (double)(n - fpsN) * g_freq / (now - fpsT);
        fpsT = now;
        fpsN = n;
        if (g_readout && IsWindowVisible(g_readout)) InvalidateRect(g_readout, nullptr, FALSE);
    }
    if (g_saveAt && now >= g_saveAt) {
        g_saveAt = 0;
        SaveCap();
    }

    HWND game = nullptr;
#ifdef BASECAP_SELFTEST
    // Test builds only: step the cap once, as if F11 was pressed, to exercise the readout.
    static bool selfTested;
    if (!selfTested && g_presents.load() > 30) {
        selfTested = true;
        SetCapFromKey(g_cap.load() + g_cfg.step, GetDesktopWindow());
    }
#endif
    if (!GameHasFocus(&game)) {
        down.down = up.down = toggle.down = false;
        return;
    }
    const int cap = g_cap.load();
    if (Fired(down, true)) {
        SetCapFromKey(cap == 0 ? g_cfg.maxCap : (cap - g_cfg.step < g_cfg.minCap ? g_cfg.minCap : cap - g_cfg.step), game);
    } else if (Fired(up, true)) {
        // Off sits just above the top of the range.
        SetCapFromKey(cap == 0 || cap >= g_cfg.maxCap ? 0 : (cap + g_cfg.step > g_cfg.maxCap ? g_cfg.maxCap : cap + g_cfg.step), game);
    } else if (Fired(toggle, false)) {
        SetCapFromKey(cap > 0 ? 0 : (g_lastCap > 0 ? g_lastCap : g_cfg.maxCap), game);
    }
}

LRESULT CALLBACK ReadoutProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerTick) {
            OnTick();
        } else if (wp == kTimerHide) {
            KillTimer(hwnd, kTimerHide);
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;
    case WM_PAINT:
        PaintReadout(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

DWORD WINAPI UiThread(void*) {
    KeyName(g_cfg.keyDown, g_downName, 32);
    KeyName(g_cfg.keyUp, g_upName, 32);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = ReadoutProc;
    wc.hInstance = g_self;
    wc.lpszClassName = kReadoutClass;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    g_readout = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                kReadoutClass, L"EnshroudedBaseCap", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, g_self,
                                nullptr);
    if (!g_readout) {
        Log("FAIL could not create the hotkey window (error %lu) - hotkeys are off, the cap from the ini still applies",
            GetLastError());
        return 0;
    }
    SetLayeredWindowAttributes(g_readout, 0, 225, LWA_ALPHA);
    SetTimer(g_readout, kTimerTick, 25, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}

DWORD WINAPI Start(void*) {
    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(g_self, dir, MAX_PATH);
    if (wchar_t* slash = wcsrchr(dir, L'\\')) slash[1] = 0;
    swprintf(g_iniPath, MAX_PATH, L"%lsEnshroudedBaseCap.ini", dir);
    wchar_t logPath[MAX_PATH];
    swprintf(logPath, MAX_PATH, L"%lsEnshroudedBaseCap.log", dir);
    g_log = _wfopen(logPath, L"w");

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;

    LoadConfig();
    ApplyCap(g_cfg.cap);
    g_lastCap = g_cfg.cap > 0 ? g_cfg.cap : 40;
    Log("EnshroudedBaseCap " BASECAP_VERSION_STR " (built " __DATE__ " " __TIME__ ")");
    Log("config: cap=%d step=%d range=%d..%d keys down=%d up=%d toggle=%d waitAfterPresent=%d readout=%d",
        g_cfg.cap, g_cfg.step, g_cfg.minCap, g_cfg.maxCap, g_cfg.keyDown, g_cfg.keyUp, g_cfg.keyToggle,
        g_cfg.waitAfterPresent, g_cfg.readout);

    if (HANDLE t = CreateThread(nullptr, 0, UiThread, nullptr, 0, nullptr)) CloseHandle(t);
    return InstallHook(nullptr);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        if (HANDLE t = CreateThread(nullptr, 0, Start, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
