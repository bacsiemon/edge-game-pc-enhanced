#include "mod_state.h"

#include <algorithm>

namespace edgemod {

uint8_t g_displayMode = DISPLAY_MODE_FULLSCREEN;
int32_t g_customWinW  = 0;
int32_t g_customWinH  = 0;
int32_t g_lastIniWinW = -1;
int32_t g_lastIniWinH = -1;

} // namespace edgemod

namespace {

using namespace edgemod;

constexpr DWORD kWindowedStyle   = WS_OVERLAPPEDWINDOW | WS_VISIBLE; // 0x10cf0000
constexpr DWORD kBorderlessStyle = WS_POPUP | WS_VISIBLE;            // 0x90000000
constexpr DWORD kBorderExMask =
    WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE;

constexpr int32_t kMinClientW = 384;
constexpr int32_t kMinClientH = 240;

bool            g_startupDone = false;
bool            g_inModeTransition = false;
uint8_t         g_lastFullscreenMode = DISPLAY_MODE_FULLSCREEN;
int32_t         g_savedWinClientW = 0;
int32_t         g_savedWinClientH = 0;
WINDOWPLACEMENT g_savedWinPlacement{};
bool            g_hasSavedWinPlacement = false;
DWORD           g_savedWinExStyle = 0;
bool            g_hasSavedWinExStyle = false;

struct TransitionGuard {
    bool prev;
    TransitionGuard() : prev(g_inModeTransition) { g_inModeTransition = true; }
    ~TransitionGuard() { g_inModeTransition = prev; }
};

inline void* GetDXUTState() {
    return reinterpret_cast<void*(__cdecl*)()>(0x40e5e0)();
}

inline uint8_t DXUTIsWindowed() {
    return reinterpret_cast<uint8_t(__cdecl*)()>(0x40ee50)();
}

inline HWND DXUTGetHWND() {
    return reinterpret_cast<HWND(__cdecl*)()>(0x414820)();
}

inline void DXUTCheckForWindowSizeChange() {
    reinterpret_cast<void(__cdecl*)()>(0x419b60)();
}

inline void DXUTCheckForWindowChangingMonitors() {
    reinterpret_cast<void(__cdecl*)()>(0x419c80)();
}

inline void DXUTSetWinBbW(void* st, uint32_t w) {
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(0x41d870)(st, w);
}

inline void DXUTSetWinBbH(void* st, uint32_t h) {
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(0x41d8b0)(st, h);
}

inline void DXUTSetWinStyleAtModeChange(void* st, DWORD style) {
    reinterpret_cast<void(__thiscall*)(void*, DWORD)>(0x41d930)(st, style);
}

inline void MouseUpdateWindowRect(HWND hwnd) {
    reinterpret_cast<void(__cdecl*)(HWND)>(0x44c5f0)(hwnd);
}

inline void* GetApp() {
    return reinterpret_cast<void*(__cdecl*)()>(0x4eecd0)();
}

inline uint8_t AppIsFullScreen(void* app) {
    using Fn = uint8_t(__thiscall*)(void*);
    return (*reinterpret_cast<Fn**>(app))[0x14 / 4](app);
}

inline void AppSetFullScreen(void* app, int fullScreen) {
    using Fn = void(__thiscall*)(void*, int);
    (*reinterpret_cast<Fn**>(app))[0x10 / 4](app, fullScreen);
}

HWND GetActiveHwnd() {
    HWND hwnd = *reinterpret_cast<HWND*>(0x5f8bc8);
    if (hwnd == nullptr) {
        hwnd = DXUTGetHWND();
    }
    return hwnd;
}

bool GetMonitorInfoForHwnd(HWND hwnd, MONITORINFO* outMi) {
    if (outMi == nullptr) return false;
    std::memset(outMi, 0, sizeof(*outMi));
    outMi->cbSize = sizeof(*outMi);
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (hMon != nullptr && GetMonitorInfoW(hMon, outMi)) {
        return true;
    }
    outMi->rcMonitor = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &outMi->rcWork, 0)) {
        outMi->rcWork = outMi->rcMonitor;
    }
    return true;
}

void ClampClientSizeToWorkArea(HWND hwnd, DWORD style, DWORD exStyle, int32_t* ioW, int32_t* ioH) {
    int32_t w = (std::max)(*ioW, kMinClientW);
    int32_t h = (std::max)(*ioH, kMinClientH);
    MONITORINFO mi{};
    if (GetMonitorInfoForHwnd(hwnd, &mi)) {
        int32_t workW = mi.rcWork.right - mi.rcWork.left;
        int32_t workH = mi.rcWork.bottom - mi.rcWork.top;
        RECT borderRc{0, 0, 0, 0};
        AdjustWindowRectEx(&borderRc, style, FALSE, exStyle);
        int32_t extraW = borderRc.right - borderRc.left;
        int32_t extraH = borderRc.bottom - borderRc.top;
        int32_t maxClientW = (std::max)(kMinClientW, workW - extraW);
        int32_t maxClientH = (std::max)(kMinClientH, workH - extraH);
        if (w > maxClientW) w = maxClientW;
        if (h > maxClientH) h = maxClientH;
    }
    *ioW = w;
    *ioH = h;
}

void ResolveTargetWindowedClientSize(HWND hwnd, int32_t* outW, int32_t* outH) {
    int32_t w = 0;
    int32_t h = 0;
    if (g_savedWinClientW >= kMinClientW && g_savedWinClientH >= kMinClientH) {
        w = g_savedWinClientW;
        h = g_savedWinClientH;
    } else if (g_customWinW >= kMinClientW && g_customWinH >= kMinClientH) {
        w = g_customWinW;
        h = g_customWinH;
    } else {
        const uint8_t* st = *reinterpret_cast<const uint8_t* const*>(0x5f9058);
        if (st != nullptr) {
            int32_t iniW = *reinterpret_cast<const int32_t*>(st + 0x08);
            int32_t iniH = *reinterpret_cast<const int32_t*>(st + 0x0c);
            if (iniW > 0 && iniH > 0) {
                w = iniW;
                h = iniH;
            }
        }
    }
    if ((w < kMinClientW || h < kMinClientH) && hwnd != nullptr) {
        RECT rc{};
        if (GetClientRect(hwnd, &rc) && rc.right >= kMinClientW && rc.bottom >= kMinClientH) {
            w = rc.right;
            h = rc.bottom;
        }
    }
    if (w < kMinClientW || h < kMinClientH) {
        w = 1024;
        h = 768;
    }
    DWORD exStyle = g_hasSavedWinExStyle ? g_savedWinExStyle : (hwnd ? static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE)) : 0);
    ClampClientSizeToWorkArea(hwnd, kWindowedStyle, exStyle, &w, &h);
    *outW = w;
    *outH = h;
}

void CaptureWindowedState(HWND hwnd) {
    if (hwnd == nullptr) return;
    if (g_displayMode != DISPLAY_MODE_WINDOWED || DXUTIsWindowed() == 0) return;

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp)) {
        g_savedWinPlacement = wp;
        g_hasSavedWinPlacement = true;
    }
    g_savedWinExStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    g_hasSavedWinExStyle = true;

    if (!IsIconic(hwnd) && !IsZoomed(hwnd)) {
        RECT rc{};
        if (GetClientRect(hwnd, &rc) && rc.right >= kMinClientW && rc.bottom >= kMinClientH) {
            g_savedWinClientW = rc.right;
            g_savedWinClientH = rc.bottom;
        }
    }
}

void ApplyBorderlessWindowRect(HWND hwnd) {
    if (hwnd == nullptr) return;
    if (IsZoomed(hwnd) || IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    MONITORINFO mi{};
    GetMonitorInfoForHwnd(hwnd, &mi);
    const int32_t monX = mi.rcMonitor.left;
    const int32_t monY = mi.rcMonitor.top;
    const int32_t monW = mi.rcMonitor.right - mi.rcMonitor.left;
    const int32_t monH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(kBorderlessStyle));
    DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    SetWindowLongW(hwnd, GWL_EXSTYLE, static_cast<LONG>((exStyle & ~kBorderExMask) | WS_EX_APPWINDOW));

    if (void* dxutSt = GetDXUTState()) {
        DXUTSetWinStyleAtModeChange(dxutSt, kBorderlessStyle & ~WS_VISIBLE);
    }

    SetWindowPos(
        hwnd, HWND_NOTOPMOST, monX, monY, monW, monH,
        SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW
    );
    DXUTCheckForWindowSizeChange();
    DXUTCheckForWindowChangingMonitors();
    MouseUpdateWindowRect(hwnd);
}

void ApplyWindowedWindowRect(HWND hwnd, int32_t clientW, int32_t clientH, bool useSavedPos) {
    if (hwnd == nullptr) return;
    if (IsZoomed(hwnd) || IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    DWORD exStyle = g_hasSavedWinExStyle
        ? g_savedWinExStyle
        : static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    SetWindowLongW(hwnd, GWL_EXSTYLE, static_cast<LONG>(exStyle));
    SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(kWindowedStyle));

    ClampClientSizeToWorkArea(hwnd, kWindowedStyle, exStyle, &clientW, &clientH);
    RECT rc{0, 0, clientW, clientH};
    AdjustWindowRectEx(&rc, kWindowedStyle, FALSE, exStyle);
    const int32_t outerW = rc.right - rc.left;
    const int32_t outerH = rc.bottom - rc.top;

    MONITORINFO mi{};
    GetMonitorInfoForHwnd(hwnd, &mi);
    const int32_t workW = mi.rcWork.right - mi.rcWork.left;
    const int32_t workH = mi.rcWork.bottom - mi.rcWork.top;

    int32_t posX = mi.rcWork.left + (std::max)(0, (workW - outerW) / 2);
    int32_t posY = mi.rcWork.top + (std::max)(0, (workH - outerH) / 2);
    if (useSavedPos && g_hasSavedWinPlacement) {
        int32_t minX = mi.rcWork.left;
        int32_t maxX = (std::max)(mi.rcWork.left, mi.rcWork.right - outerW);
        int32_t minY = mi.rcWork.top;
        int32_t maxY = (std::max)(mi.rcWork.top, mi.rcWork.bottom - outerH);
        posX = std::clamp<int32_t>(g_savedWinPlacement.rcNormalPosition.left, minX, maxX);
        posY = std::clamp<int32_t>(g_savedWinPlacement.rcNormalPosition.top, minY, maxY);
    }

    if (void* dxutSt = GetDXUTState()) {
        DXUTSetWinStyleAtModeChange(dxutSt, WS_OVERLAPPEDWINDOW);
    }

    SetWindowPos(
        hwnd, HWND_NOTOPMOST, posX, posY, outerW, outerH,
        SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW
    );
    DXUTCheckForWindowSizeChange();
    DXUTCheckForWindowChangingMonitors();
    MouseUpdateWindowRect(hwnd);
}

void __cdecl OnDisplayModeStartup_Impl(uint8_t* sm) {
    HWND hwnd = GetActiveHwnd();
    if (hwnd != nullptr) {
        g_savedWinExStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
        g_hasSavedWinExStyle = true;
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(hwnd, &wp)) {
            g_savedWinPlacement = wp;
            g_hasSavedWinPlacement = true;
        }
    }

    if (g_customWinW >= kMinClientW && g_customWinH >= kMinClientH) {
        g_savedWinClientW = g_customWinW;
        g_savedWinClientH = g_customWinH;
    } else if (hwnd != nullptr) {
        RECT rc{};
        if (GetClientRect(hwnd, &rc) && rc.right >= kMinClientW && rc.bottom >= kMinClientH) {
            g_savedWinClientW = rc.right;
            g_savedWinClientH = rc.bottom;
        }
    }

    g_lastFullscreenMode = (g_displayMode == DISPLAY_MODE_BORDERLESS)
        ? DISPLAY_MODE_BORDERLESS
        : DISPLAY_MODE_FULLSCREEN;

    {
        TransitionGuard guard;
        if (g_displayMode == DISPLAY_MODE_FULLSCREEN) {
            ApplyDisplayMode(DISPLAY_MODE_FULLSCREEN, false);
        } else if (g_displayMode == DISPLAY_MODE_BORDERLESS) {
            ApplyDisplayMode(DISPLAY_MODE_BORDERLESS, false);
        } else {
            if (hwnd != nullptr) {
                DWORD curStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
                if ((curStyle & kWindowedStyle) != kWindowedStyle) {
                    SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(kWindowedStyle));
                    SetWindowPos(
                        hwnd, nullptr, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED
                    );
                }
                if (g_customWinW >= kMinClientW && g_customWinH >= kMinClientH) {
                    RECT rc{};
                    GetClientRect(hwnd, &rc);
                    if (rc.right != g_customWinW || rc.bottom != g_customWinH) {
                        ApplyWindowedWindowRect(hwnd, g_customWinW, g_customWinH, false);
                    } else {
                        MouseUpdateWindowRect(hwnd);
                    }
                }
            }
            if (sm != nullptr) {
                sm[0x2b6] = 0;
            }
        }
    }

    g_startupDone = true;
}

void __declspec(naked) hk_startupDisplayMode() {
    // Replaces 22B at 0x482c77..0x482c8d in ScreenMgr ctor (esi = ScreenMgr*, ebx = 0).
    __asm {
        push esi
        call OnDisplayModeStartup_Impl
        add esp, 4
        ret
    }
}

void __cdecl OnPostD3D9ResetDevice_Impl(uint8_t* appObj) {
    if (appObj != nullptr) {
        uint8_t* subObj = appObj + 0x14;
        auto** vt = *reinterpret_cast<void***>(subObj);
        reinterpret_cast<void(__thiscall*)(void*)>(vt[0x2c / 4])(subObj);
    }

    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        *reinterpret_cast<int32_t*>(sm + 0x2c) = *reinterpret_cast<int32_t*>(sm + 0x24);
        *reinterpret_cast<int32_t*>(sm + 0x30) = *reinterpret_cast<int32_t*>(sm + 0x28);
    }

    if (g_startupDone && !g_inModeTransition && g_displayMode == DISPLAY_MODE_WINDOWED && DXUTIsWindowed() != 0) {
        HWND hwnd = GetActiveHwnd();
        if (hwnd != nullptr && !IsIconic(hwnd) && !IsZoomed(hwnd)) {
            RECT rc{};
            if (GetClientRect(hwnd, &rc) && rc.right >= kMinClientW && rc.bottom >= kMinClientH) {
                WINDOWPLACEMENT wp{};
                wp.length = sizeof(wp);
                if (GetWindowPlacement(hwnd, &wp)) {
                    g_savedWinPlacement = wp;
                    g_hasSavedWinPlacement = true;
                }
                if (rc.right != g_savedWinClientW || rc.bottom != g_savedWinClientH) {
                    g_savedWinClientW = rc.right;
                    g_savedWinClientH = rc.bottom;
                    g_customWinW = rc.right;
                    g_customWinH = rc.bottom;
                    SaveModSettingsPublic();
                }
            }
        }
    }
}

void __declspec(naked) hk_postD3D9ResetDevice() {
    // Replaces 10B at 0x44db55..0x44db5f inside OnD3D9ResetDevice (eax = WinApp*).
    __asm {
        push eax
        call OnPostD3D9ResetDevice_Impl
        add esp, 4
        ret
    }
}

} // namespace

namespace edgemod {

uint8_t __cdecl GetDisplayMode() {
    return (g_displayMode <= DISPLAY_MODE_BORDERLESS) ? g_displayMode : DISPLAY_MODE_FULLSCREEN;
}

void __cdecl ApplyDisplayMode(uint8_t mode, bool saveSettings) {
    if (mode > DISPLAY_MODE_BORDERLESS) mode = DISPLAY_MODE_FULLSCREEN;

    HWND hwnd = GetActiveHwnd();
    void* app = GetApp();
    uint8_t* sm = GetScreenMgr();
    bool curExclFs = (app != nullptr && AppIsFullScreen(app) != 0);

    if (mode != DISPLAY_MODE_WINDOWED) {
        g_lastFullscreenMode = mode;
    }

    if (g_displayMode == DISPLAY_MODE_WINDOWED && mode != DISPLAY_MODE_WINDOWED && !curExclFs) {
        CaptureWindowedState(hwnd);
    }

    {
        TransitionGuard guard;

        if (mode == DISPLAY_MODE_FULLSCREEN) {
            g_displayMode = DISPLAY_MODE_FULLSCREEN;
            if (!curExclFs && app != nullptr) {
                if (void* dxutSt = GetDXUTState()) {
                    DXUTSetWinStyleAtModeChange(dxutSt, WS_OVERLAPPEDWINDOW);
                    if (g_savedWinClientW >= kMinClientW && g_savedWinClientH >= kMinClientH) {
                        DXUTSetWinBbW(dxutSt, static_cast<uint32_t>(g_savedWinClientW));
                        DXUTSetWinBbH(dxutSt, static_cast<uint32_t>(g_savedWinClientH));
                    }
                }
                AppSetFullScreen(app, 1);
                if (AppIsFullScreen(app) == 0) {
                    // Exclusive fullscreen transition failed; fall back cleanly to windowed.
                    g_displayMode = DISPLAY_MODE_WINDOWED;
                    int32_t w = 0, h = 0;
                    ResolveTargetWindowedClientSize(hwnd, &w, &h);
                    ApplyWindowedWindowRect(hwnd, w, h, g_hasSavedWinPlacement);
                }
            }
        } else if (mode == DISPLAY_MODE_WINDOWED) {
            g_displayMode = DISPLAY_MODE_WINDOWED;
            int32_t targetW = 0, targetH = 0;
            ResolveTargetWindowedClientSize(hwnd, &targetW, &targetH);
            if (curExclFs && app != nullptr) {
                if (void* dxutSt = GetDXUTState()) {
                    DXUTSetWinStyleAtModeChange(dxutSt, WS_OVERLAPPEDWINDOW);
                    DXUTSetWinBbW(dxutSt, static_cast<uint32_t>(targetW));
                    DXUTSetWinBbH(dxutSt, static_cast<uint32_t>(targetH));
                }
                AppSetFullScreen(app, 0);
            }
            ApplyWindowedWindowRect(hwnd, targetW, targetH, g_hasSavedWinPlacement);
            g_savedWinClientW = targetW;
            g_savedWinClientH = targetH;
        } else {
            g_displayMode = DISPLAY_MODE_BORDERLESS;
            MONITORINFO mi{};
            GetMonitorInfoForHwnd(hwnd, &mi);
            const int32_t monW = mi.rcMonitor.right - mi.rcMonitor.left;
            const int32_t monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
            if (curExclFs && app != nullptr) {
                if (void* dxutSt = GetDXUTState()) {
                    DXUTSetWinStyleAtModeChange(dxutSt, kBorderlessStyle & ~WS_VISIBLE);
                    DXUTSetWinBbW(dxutSt, static_cast<uint32_t>(monW));
                    DXUTSetWinBbH(dxutSt, static_cast<uint32_t>(monH));
                }
                AppSetFullScreen(app, 0);
            }
            ApplyBorderlessWindowRect(hwnd);
        }

        if (sm != nullptr) {
            sm[0x2b6] = (g_displayMode == DISPLAY_MODE_FULLSCREEN) ? 1u : 0u;
            ApplyUiScale(sm);
        }
    }

    if (saveSettings) {
        SaveModSettingsPublic();
        if (sm != nullptr) {
            Game_SaveUserSettings(sm + 0x298);
        }
    }
}

bool __cdecl HandleDisplayModeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* outResult) {
    switch (msg) {
    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN && (lParam & 0x20000000u) != 0) {
            if ((lParam & 0x40000000u) == 0 && g_startupDone && !g_inModeTransition) {
                if (g_displayMode == DISPLAY_MODE_WINDOWED) {
                    ApplyDisplayMode(g_lastFullscreenMode, true);
                } else {
                    g_lastFullscreenMode = g_displayMode;
                    ApplyDisplayMode(DISPLAY_MODE_WINDOWED, true);
                }
            }
            if (outResult != nullptr) *outResult = 0;
            return true;
        }
        break;
    case WM_SYSCOMMAND:
        if (g_displayMode == DISPLAY_MODE_BORDERLESS) {
            UINT cmd = static_cast<UINT>(wParam & 0xFFF0u);
            if (cmd == SC_MOVE || cmd == SC_SIZE || cmd == SC_MAXIMIZE) {
                if (outResult != nullptr) *outResult = 0;
                return true;
            }
        }
        break;
    case WM_GETMINMAXINFO:
        if (g_displayMode == DISPLAY_MODE_WINDOWED && lParam != 0) {
            LRESULT baseRes = reinterpret_cast<WNDPROC>(0x44d4e0)(hwnd, msg, wParam, lParam);
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
            DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
            RECT minRc{0, 0, kMinClientW, kMinClientH};
            AdjustWindowRectEx(&minRc, style, FALSE, exStyle);
            LONG minW = minRc.right - minRc.left;
            LONG minH = minRc.bottom - minRc.top;
            if (mmi->ptMinTrackSize.x < minW) mmi->ptMinTrackSize.x = minW;
            if (mmi->ptMinTrackSize.y < minH) mmi->ptMinTrackSize.y = minH;
            if (outResult != nullptr) *outResult = baseRes;
            return true;
        }
        break;
    case WM_MOVE:
        if (g_startupDone && !g_inModeTransition && g_displayMode == DISPLAY_MODE_WINDOWED && !IsIconic(hwnd)) {
            MouseUpdateWindowRect(hwnd);
            if (!IsZoomed(hwnd)) {
                WINDOWPLACEMENT wp{};
                wp.length = sizeof(wp);
                if (GetWindowPlacement(hwnd, &wp)) {
                    g_savedWinPlacement = wp;
                    g_hasSavedWinPlacement = true;
                }
            }
        }
        break;
    case WM_DISPLAYCHANGE:
        if (g_startupDone && !g_inModeTransition && g_displayMode == DISPLAY_MODE_BORDERLESS) {
            TransitionGuard guard;
            ApplyBorderlessWindowRect(hwnd);
        }
        break;
    default:
        break;
    }
    return false;
}

} // namespace edgemod

void RegisterDisplayMode() {
    // 1. DXUTCreateWindow windowed style: WS_OVERLAPPEDWINDOW (0x00cf0000) instead of 0x00ca0000 (no thickframe/maximizebox)
    {
        static const uint8_t expStyle[5] = {0xbb, 0x00, 0x00, 0xca, 0x00};
        static const uint8_t repStyle[5] = {0xbb, 0x00, 0x00, 0xcf, 0x00};
        RegBytes(0x41befd, expStyle, repStyle, 5);
    }

    // 2. ScreenMgr constructor startup display mode application (replaces App::setFullScreen([esi+0x2b6]))
    {
        static const uint8_t expStartDm[22] = {
            0xe8, 0x54, 0xc0, 0x06, 0x00, 0x0f, 0xb6, 0x8e, 0xb6, 0x02, 0x00, 0x00,
            0x8b, 0x10, 0x8b, 0x52, 0x10, 0x51, 0x8b, 0xc8, 0xff, 0xd2
        };
        RegRelCall(0x482c77, expStartDm, 22, 22, &hk_startupDisplayMode, 0x90);
    }

    // 3. OnD3D9ResetDevice post-reset ScreenMgr sync & windowed resize capture
    {
        static const uint8_t expPostReset[10] = {
            0x8d, 0x48, 0x14, 0x8b, 0x01, 0x8b, 0x50, 0x2c, 0xff, 0xd2
        };
        RegRelCall(0x44db55, expPostReset, 10, 10, &hk_postD3D9ResetDevice, 0x90);
    }
}
