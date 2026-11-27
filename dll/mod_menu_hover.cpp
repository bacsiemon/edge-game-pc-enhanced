#include "mod_state.h"
#include <algorithm>
#include <cstring>

namespace {

using namespace edgemod;

constexpr uint32_t kVtblCredits     = 0x58b9d0u;
constexpr uint32_t kVtblConfirmExit = 0x58cc18u;
constexpr uint32_t kVtblDebrief     = 0x58ce58u;
constexpr uint32_t kVtblPause       = 0x58cf44u;
constexpr uint32_t kVtblScores      = 0x58d118u;
constexpr uint32_t kVtblScoresSel   = 0x58d20cu;
constexpr uint32_t kVtblOptions     = 0x58d24cu;
constexpr uint32_t kVtblInstr       = 0x58d2fcu;
constexpr uint32_t kVtblLevelSel    = 0x58d51cu;
constexpr uint32_t kVtblModeSel     = 0x58d578u;
constexpr uint32_t kVtblMainMenu    = 0x58d610u;

constexpr int kSubtleRainbowPct = 25;

enum class HoverInputMode {
    Keyboard = 0,
    Mouse    = 1,
};

struct MenuHoverState {
    HoverInputMode lastInputMode = HoverInputMode::Keyboard;

    const uint8_t* activeMenu     = nullptr;
    uint32_t       activeState    = 0;
    int            activeSubPage  = -1;
    int            activeOptCat   = -1;

    bool slotVisible      = true;
    bool slotPressed      = false;
    int  lastSlot         = 0;
    bool needsInitialSync = false;
    bool prevPopupOpen    = false;
    bool prevScrollbarDrag = false;

    int32_t lsHoveredTile    = -1;
    int32_t lsPrevPlayedTile = -1;
    int     lsHoveredBtn     = -1; // -1=none, 1=back/skip, 2=go
    int32_t lsPrevScrollY    = 0;
    uint32_t optPrevScrollRev = 0;

    int prevMx = -99999;
    int prevMy = -99999;

    bool renderingMenu     = false;
    bool renderInteractive = false;
    bool renderForceStrong = false;
    bool renderForceWhite  = false;
    int32_t renderFrame    = 0;
    bool lastDrawBtnHovered = false;
    bool lastDrawBtnPressed = false;
};

MenuHoverState g_hover{};

static inline void GetMenuBasePt(int* outPt) {
    reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(outPt);
}

static inline int GetMenuPanelH() {
    return reinterpret_cast<int(__cdecl*)()>(0x496060)();
}

static int GetMenuSelOffset(uint32_t vtbl, int pausePage) {
    if (vtbl == kVtblPause) {
        if (pausePage == 0) return 0x10;
        if (pausePage == 1) return 0x50;
        if (pausePage == 2) return 0x54;
        return -1;
    }
    if (vtbl == kVtblMainMenu || vtbl == kVtblModeSel || vtbl == kVtblOptions ||
        vtbl == kVtblScoresSel || vtbl == kVtblConfirmExit || vtbl == kVtblDebrief) {
        return 0x10;
    }
    return -1;
}

static bool IsStandardSlotMenuReady(const uint8_t* menu, uint32_t vtbl, int pausePage) {
    int enterFrames = *reinterpret_cast<const int32_t*>(menu + 4);
    int exitTimer   = *reinterpret_cast<const int32_t*>(menu + 8);
    if (exitTimer >= 0) return false;

    if (vtbl == kVtblOptions) {
        return IsOptionsMenuReadyForHover(menu, false);
    }
    if (vtbl == kVtblPause) {
        if (enterFrames < kPauseOpenReadyTick) return false;
        int scrollX = *reinterpret_cast<const int32_t*>(menu + 0x4c);
        if (pausePage == 0) return scrollX == 0;
        if (pausePage == 1) return IsOptionsMenuReadyForHover(menu, true);
        if (pausePage == 2) return scrollX >= 0x170;
        return false;
    }
    if (vtbl == kVtblConfirmExit) {
        return menu[0x14] == 0 && enterFrames >= 8;
    }
    if (vtbl == kVtblDebrief) {
        return *reinterpret_cast<const int32_t*>(menu + 0x44) >= 0x2f;
    }
    return enterFrames >= 8;
}

static int HitTestMenuSlot(const uint8_t* menu, uint32_t vtbl, int pausePage, int mx, int my) {
    int basePt[2] = {0, 0};
    GetMenuBasePt(basePt);
    int bx = basePt[0];
    int by = basePt[1];
    int menuW = *reinterpret_cast<const int32_t*>(0x589ddc);

    if (vtbl == kVtblMainMenu) {
        if (mx < bx || mx >= bx + menuW) return -1;
        for (int i = 0; i < 6; ++i) {
            int top = by + 0x51 + i * 0x32;
            if (my >= top && my < top + 0x32) return i;
        }
        return -1;
    }

    if (vtbl == kVtblModeSel) {
        if (mx < bx || mx >= bx + menuW) return -1;
        int panelH = GetMenuPanelH();
        const int count = GetModeSelectRowCount();
        for (int i = 0; i < count; ++i) {
            bool visible = (i < 1 || i > 3 || menu[0x14] != 0);
            if (!visible) continue;
            MenuRowRect r = GetModeSelectRowRect(i, bx, by, panelH, menuW);
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) return i;
        }
        return -1;
    }

    if (vtbl == kVtblScoresSel) {
        if (mx < bx || mx >= bx + menuW) return -1;
        int panelH = GetMenuPanelH();
        const int count = GetScoresSelectRowCount();
        for (int i = 0; i < count; ++i) {
            MenuRowRect r = GetScoresSelectRowRect(i, bx, by, panelH, menuW);
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) return i;
        }
        return -1;
    }

    if (vtbl == kVtblOptions) {
        return HitTestOptionsSlot(false, mx, my);
    }

    if (vtbl == kVtblConfirmExit) {
        int top = by + 0x2e;
        if (my < top || my >= top + 0x3f) return -1;
        if (mx >= bx + 0x24 && mx < bx + 0x24 + 0x76) return 0;
        if (mx >= bx + 0xd0 && mx < bx + 0xd0 + 0x76) return 1;
        return -1;
    }

    if (vtbl == kVtblPause) {
        const int pauseShiftY = GetPauseContentShiftY();
        if (pausePage == 0) {
            int ox = *reinterpret_cast<const int32_t*>(0x5f9718);
            int oy = *reinterpret_cast<const int32_t*>(0x5f971c);
            int left = bx + 2 + ox;
            if (mx < left || mx >= left + 0x16c) return -1;
            for (int i = 0; i < 5; ++i) {
                int top = by + 0x55 + pauseShiftY + oy + i * 0x26;
                if (my >= top && my < top + 0x25) return i;
            }
            return -1;
        }
        if (pausePage == 1) {
            return HitTestOptionsSlot(true, mx, my);
        }
        if (pausePage == 2) {
            int top = by + 0x81 + pauseShiftY;
            if (my < top || my >= top + 0x3f) return -1;
            if (mx >= bx + 0x24 && mx < bx + 0x24 + 0x76) return 0;
            if (mx >= bx + 0xd0 && mx < bx + 0xd0 + 0x76) return 1;
            return -1;
        }
        return -1;
    }

    if (vtbl == kVtblDebrief) {
        if (mx < bx || mx >= bx + 0x170) return -1;
        const int count = GetDebriefButtonCount();
        for (int i = 0; i < count; ++i) {
            int top = by + 0x18c + i * 0x21;
            if (my >= top && my < top + 0x20) return i;
        }
        return -1;
    }

    return -1;
}

static int32_t HitTestLevelSelectTile(const uint8_t* ls, int mx, int my) {
    int basePt[2] = {0, 0};
    GetMenuBasePt(basePt);
    int bx = basePt[0];
    int by = basePt[1];
    int panelH = GetMenuPanelH();

    if (my < by + 0x9a || my > by + panelH - 0x50) return -1;

    int32_t count = *reinterpret_cast<const int32_t*>(ls + 0x18);
    const uint8_t* tileFlags = *reinterpret_cast<const uint8_t* const*>(ls + 0x4c);
    int32_t scrollY = *reinterpret_cast<const int32_t*>(ls + 0x50);
    if (tileFlags == nullptr || count <= 0) return -1;

    for (int32_t i = 0; i < count; ++i) {
        if (tileFlags[i * 2] == 0) continue;
        int row = i / 6;
        int col = i % 6;
        int tx = bx + 0x38 + col * 0x2d;
        int ty = by + 0xac + row * 0x2d - scrollY;
        if (mx >= tx && mx < tx + 0x22 && my >= ty && my < ty + 0x22) {
            return i;
        }
    }
    return -1;
}

static int HitTestMenuButton(uint32_t vtbl, int mx, int my) {
    int basePt[2] = {0, 0};
    GetMenuBasePt(basePt);
    int bx = basePt[0];
    int by = basePt[1];
    int panelH = GetMenuPanelH();
    int btnTop = by + panelH - 0x3a;
    if (my < btnTop || my >= btnTop + 0x2f) return -1;

    if (vtbl == kVtblLevelSel) {
        if (mx >= bx + 0x0b && mx < bx + 0x0b + 0x66) return 1; // back
        if (mx >= bx + 0xff && mx < bx + 0xff + 0x66) return 2; // go
        return -1;
    }
    if (vtbl == kVtblScores || vtbl == kVtblInstr) {
        if (mx >= bx + 0x10 && mx < bx + 0x10 + 0x66) return 1; // back
        return -1;
    }
    if (vtbl == kVtblCredits) {
        if (mx >= bx + 0xff && mx < bx + 0xff + 0x66) return 1; // skip
        return -1;
    }
    return -1;
}

static void DrawHoveredSlotBorder(const uint8_t* menu, uint32_t vtbl, int pausePage, int sel, int frame) {
    if (sel < 0) return;
    int basePt[2] = {0, 0};
    GetMenuBasePt(basePt);
    int bx = basePt[0];
    int by = basePt[1];
    int menuW = *reinterpret_cast<const int32_t*>(0x589ddc);

    if (vtbl == kVtblMainMenu) {
        if (sel < 6) {
            Game_DrawRainbowRectBorder(frame, bx + 2, by + 0x51 + sel * 0x32, menuW - 4, 0x32, 2, 255);
        }
    } else if (vtbl == kVtblModeSel) {
        if (sel < GetModeSelectRowCount() && (sel < 1 || sel > 3 || menu[0x14] != 0)) {
            int panelH = GetMenuPanelH();
            MenuRowRect r = GetModeSelectRowRect(sel, bx, by, panelH, menuW);
            Game_DrawRainbowRectBorder(frame, r.x, r.y, r.w, r.h, 2, 255);
        }
    } else if (vtbl == kVtblScoresSel) {
        if (sel < GetScoresSelectRowCount()) {
            int panelH = GetMenuPanelH();
            MenuRowRect r = GetScoresSelectRowRect(sel, bx, by, panelH, menuW);
            Game_DrawRainbowRectBorder(frame, r.x, r.y, r.w, r.h, 2, 255);
        }
    } else if (vtbl == kVtblConfirmExit) {
        if (sel == 0) {
            Game_DrawRainbowRectBorder(frame, bx + 0x24, by + 0x2e, 0x76, 0x3f, 2, 255);
        } else if (sel == 1) {
            Game_DrawRainbowRectBorder(frame, bx + 0xd0, by + 0x2e, 0x76, 0x3f, 2, 255);
        }
    } else if (vtbl == kVtblPause) {
        const int pauseShiftY = GetPauseContentShiftY();
        if (pausePage == 0 && sel < 5) {
            Game_DrawRainbowRectBorder(frame, bx + 2, by + 0x55 + pauseShiftY + sel * 0x26, 0x16c, 0x25, 2, 255);
        } else if (pausePage == 2) {
            if (sel == 0) {
                Game_DrawRainbowRectBorder(frame, bx + 0x24, by + 0x81 + pauseShiftY, 0x76, 0x3f, 2, 255);
            } else if (sel == 1) {
                Game_DrawRainbowRectBorder(frame, bx + 0xd0, by + 0x81 + pauseShiftY, 0x76, 0x3f, 2, 255);
            }
        }
    } else if (vtbl == kVtblDebrief) {
        if (sel >= 0 && sel < GetDebriefButtonCount()) {
            Game_DrawRainbowRectBorder(frame, bx, by + 0x18c + sel * 0x21, 0x170, 0x20, 2, 255);
        }
    }
}

static void SetLevelSelectDisplayedTile(uint8_t* ls, int32_t tileIdx) {
    if (ls == nullptr) return;
    int32_t count = *reinterpret_cast<const int32_t*>(ls + 0x18);
    if (tileIdx < 0 || tileIdx >= count) return;
    if (*reinterpret_cast<const int32_t*>(ls + 0x38) == tileIdx) return;

    *reinterpret_cast<int32_t*>(ls + 0x38) = tileIdx;
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;

    int32_t mode = *reinterpret_cast<const int32_t*>(ls + 0x14);
    using GetLevelInfoFn = uint8_t*(__thiscall*)(void*, int32_t, int32_t);
    uint8_t* levelInfo = reinterpret_cast<GetLevelInfoFn>(0x4a4620)(sm + 0x24c, mode, tileIdx);
    *reinterpret_cast<uint8_t**>(ls + 0x3c) = levelInfo;

    uint8_t* saveInfo = nullptr;
    if (levelInfo != nullptr) {
        int32_t levelId = *reinterpret_cast<const int32_t*>(levelInfo + 0x3c);
        using GetSaveInfoFn = uint8_t*(__thiscall*)(void*, int32_t);
        saveInfo = reinterpret_cast<GetSaveInfoFn>(0x4920a0)(sm + 0x298, levelId);
    }
    *reinterpret_cast<uint8_t**>(ls + 0x40) = saveInfo;
}

static int32_t __cdecl LsTileShouldUsePressedColor_Impl(const uint8_t* ls, int32_t tileIdx) {
    if (ls == nullptr) return 0;
    int32_t exitTimer = *reinterpret_cast<const int32_t*>(ls + 0x8);
    int32_t exitDest  = *reinterpret_cast<const int32_t*>(ls + 0xc);
    if (exitTimer >= 0 && exitDest == 0x10) {
        return (tileIdx == *reinterpret_cast<const int32_t*>(ls + 0x38)) ? 1 : 0;
    }

    int32_t prevPlayed = (ls == g_hover.activeMenu && g_hover.lsPrevPlayedTile >= 0)
        ? g_hover.lsPrevPlayedTile
        : *reinterpret_cast<const int32_t*>(ls + 0x38);
    if (tileIdx == prevPlayed) return 1;

    if (*reinterpret_cast<const int32_t*>(ls + 0x4) >= 8 &&
        exitTimer < 0 &&
        ls[0x44] != 0 &&
        tileIdx == *reinterpret_cast<const int32_t*>(ls + 0x38)) {
        return 1;
    }
    return 0;
}

static void __declspec(naked) hk_lsTilePressedCheck() {
    __asm {
        push ebx
        push edi
        call LsTileShouldUsePressedColor_Impl
        add esp, 8
        test eax, eax
        je ls_tile_not_pressed
        mov eax, 0x4b812a
        jmp eax
    ls_tile_not_pressed:
        mov eax, 0x4b8161
        jmp eax
    }
}

static void CallSubmenuUpdate(uint8_t* submenu, float dt) {
    using Fn = void(__thiscall*)(void*, float);
    (*reinterpret_cast<Fn**>(submenu))[1](submenu, dt);
}

static void CallSubmenuRender(uint8_t* submenu, int frame) {
    using Fn = void(__thiscall*)(void*, int);
    (*reinterpret_cast<Fn**>(submenu))[2](submenu, frame);
}

static bool AnyRelevantKeyPressed(const uint8_t* sm, uint32_t vtbl, int pausePage) {
    bool enter = (sm[0xc5] != 0);
    bool ud = (sm[0xad] != 0 || sm[0xb0] != 0);
    bool lr = (sm[0xb3] != 0 || sm[0xb6] != 0);

    if (vtbl == kVtblConfirmExit || (vtbl == kVtblPause && pausePage == 2)) {
        return lr || enter;
    }
    if (vtbl == kVtblOptions || vtbl == kVtblModeSel || vtbl == kVtblScoresSel ||
        (vtbl == kVtblPause && pausePage == 1)) {
        return ud || lr || enter;
    }
    return ud || enter;
}

static void __cdecl MenuSubUpdate_Impl(uint8_t* submenu, float dt) {
    uint8_t* sm = GetScreenMgr();
    if (submenu == nullptr || sm == nullptr) return;

    uint32_t state = *reinterpret_cast<const uint32_t*>(sm + 0x50);
    uint32_t vtbl = *reinterpret_cast<const uint32_t*>(submenu);
    int pausePage = (vtbl == kVtblPause) ? *reinterpret_cast<const int32_t*>(submenu + 0x44) : -1;
    int optCat = -1;
    if (vtbl == kVtblOptions) optCat = GetActiveOptionsCategory(false);
    else if (vtbl == kVtblPause && pausePage == 1) optCat = GetActiveOptionsCategory(true);

    int selOff = GetMenuSelOffset(vtbl, pausePage);

    int mx = *reinterpret_cast<const int32_t*>(sm + 0xa0);
    int my = *reinterpret_cast<const int32_t*>(sm + 0xa4);
    bool mouseValid = (sm[0xa8] != 0);
    bool mouseDown = (sm[0] != 0 || sm[0xca] != 0);
    bool mouseJustPressed = (sm[0xcb] != 0);
    bool mouseMoved = mouseValid && (mx != g_hover.prevMx || my != g_hover.prevMy);
    if (mouseValid) {
        g_hover.prevMx = mx;
        g_hover.prevMy = my;
    }

    if (submenu != g_hover.activeMenu || state != g_hover.activeState ||
        pausePage != g_hover.activeSubPage || optCat != g_hover.activeOptCat) {
        g_hover.activeMenu = submenu;
        g_hover.activeState = state;
        g_hover.activeSubPage = pausePage;
        g_hover.activeOptCat = optCat;
        g_hover.lsHoveredTile = -1;
        g_hover.lsHoveredBtn = -1;
        g_hover.slotPressed = false;
        g_hover.prevPopupOpen = false;
        g_hover.prevScrollbarDrag = false;
        g_hover.lastSlot = (selOff >= 0) ? *reinterpret_cast<const int32_t*>(submenu + selOff) : 0;
        if (vtbl == kVtblLevelSel) {
            g_hover.lsPrevScrollY = *reinterpret_cast<const int32_t*>(submenu + 0x50);
            g_hover.lsPrevPlayedTile = *reinterpret_cast<const int32_t*>(submenu + 0x38);
        } else {
            g_hover.lsPrevPlayedTile = -1;
        }
        bool inOptsInit = (vtbl == kVtblOptions) || (vtbl == kVtblPause && pausePage == 1);
        g_hover.optPrevScrollRev = inOptsInit ? GetOptionsScrollRevision(vtbl == kVtblPause && pausePage == 1) : 0u;
        if (g_hover.lastInputMode == HoverInputMode::Mouse) {
            g_hover.needsInitialSync = true;
            g_hover.slotVisible = false;
        } else {
            g_hover.needsInitialSync = false;
            g_hover.slotVisible = true;
        }
    }

    // 1. Standard slot menus
    if (selOff >= 0) {
        bool ready = IsStandardSlotMenuReady(submenu, vtbl, pausePage);
        bool inOpts = (vtbl == kVtblOptions) || (vtbl == kVtblPause && pausePage == 1);
        bool inPauseOpts = (vtbl == kVtblPause && pausePage == 1);

        if (inOpts && IsOptionsSliderDragging(inPauseOpts)) {
            g_hover.slotVisible = true;
            g_hover.slotPressed = true;
            g_hover.needsInitialSync = false;
            CallSubmenuUpdate(submenu, dt);
            g_hover.lastSlot = *reinterpret_cast<const int32_t*>(submenu + selOff);
            return;
        }

        if (inOpts && IsOptionsScrollbarDragging(inPauseOpts)) {
            g_hover.prevScrollbarDrag = true;
            g_hover.slotVisible = false;
            g_hover.slotPressed = false;
            g_hover.needsInitialSync = false;
            CallSubmenuUpdate(submenu, dt);
            return;
        }
        if (g_hover.prevScrollbarDrag) {
            g_hover.prevScrollbarDrag = false;
            if (g_hover.lastInputMode == HoverInputMode::Mouse) {
                g_hover.needsInitialSync = true;
            }
        }

        if (inOpts && IsOptionsPopupOpen(inPauseOpts)) {
            g_hover.prevPopupOpen = true;
            g_hover.slotVisible = true;
            g_hover.slotPressed = false;
            CallSubmenuUpdate(submenu, dt);
            return;
        }
        if (g_hover.prevPopupOpen) {
            g_hover.prevPopupOpen = false;
            if (g_hover.lastInputMode == HoverInputMode::Mouse) {
                g_hover.needsInitialSync = true;
            }
        }

        if (ready) {
            bool keyPressed = AnyRelevantKeyPressed(sm, vtbl, pausePage);
            if (keyPressed) {
                g_hover.lastInputMode = HoverInputMode::Keyboard;
                g_hover.needsInitialSync = false;
                g_hover.slotPressed = false;
                if (!g_hover.slotVisible) {
                    *reinterpret_cast<int32_t*>(submenu + selOff) = g_hover.lastSlot;
                    g_hover.slotVisible = true;
                    Game_PlaySfx(sm, 2);

                    uint8_t savedAd = sm[0xad], savedB0 = sm[0xb0], savedB3 = sm[0xb3],
                            savedB6 = sm[0xb6], savedC5 = sm[0xc5];
                    sm[0xad] = 0; sm[0xb0] = 0; sm[0xb3] = 0; sm[0xb6] = 0; sm[0xc5] = 0;
                    CallSubmenuUpdate(submenu, dt);
                    sm[0xad] = savedAd; sm[0xb0] = savedB0; sm[0xb3] = savedB3;
                    sm[0xb6] = savedB6; sm[0xc5] = savedC5;
                    return;
                }
                CallSubmenuUpdate(submenu, dt);
                g_hover.lastSlot = *reinterpret_cast<const int32_t*>(submenu + selOff);
                return;
            }

            uint32_t curOptScrollRev = inOpts ? GetOptionsScrollRevision(inPauseOpts) : 0u;
            bool optScrollChanged = inOpts && (curOptScrollRev != g_hover.optPrevScrollRev);
            g_hover.optPrevScrollRev = curOptScrollRev;

            if (sm[0xc8] != 0) {
                g_hover.lastInputMode = HoverInputMode::Keyboard;
                g_hover.needsInitialSync = false;
            } else if (mouseMoved || mouseJustPressed ||
                       (g_hover.lastInputMode == HoverInputMode::Mouse && (g_hover.needsInitialSync || optScrollChanged))) {
                bool silent = (g_hover.needsInitialSync || optScrollChanged) && !mouseMoved && !mouseJustPressed;
                if (mouseMoved || mouseJustPressed) {
                    g_hover.lastInputMode = HoverInputMode::Mouse;
                }
                g_hover.needsInitialSync = false;
                int hit = mouseValid ? HitTestMenuSlot(submenu, vtbl, pausePage, mx, my) : -1;
                if (hit >= 0) {
                    int32_t& sel = *reinterpret_cast<int32_t*>(submenu + selOff);
                    bool changed = (!g_hover.slotVisible || sel != hit);
                    sel = hit;
                    g_hover.lastSlot = hit;
                    g_hover.slotVisible = true;
                    if (changed && !silent) {
                        Game_PlaySfx(sm, 2);
                    }
                } else {
                    g_hover.slotVisible = false;
                }
            } else if (g_hover.lastInputMode == HoverInputMode::Mouse && !mouseValid && !mouseDown) {
                g_hover.slotVisible = false;
            }

            g_hover.slotPressed = g_hover.slotVisible && mouseDown && mouseValid &&
                (HitTestMenuSlot(submenu, vtbl, pausePage, mx, my) == *reinterpret_cast<const int32_t*>(submenu + selOff));
        } else {
            g_hover.slotPressed = false;
        }

        CallSubmenuUpdate(submenu, dt);
        if (ready && g_hover.slotVisible) {
            g_hover.lastSlot = *reinterpret_cast<const int32_t*>(submenu + selOff);
        }
        return;
    }

    // 2. LevelSelectMenu
    if (vtbl == kVtblLevelSel) {
        bool ready = (*reinterpret_cast<const int32_t*>(submenu + 4) >= 8) &&
                     (*reinterpret_cast<const int32_t*>(submenu + 8) < 0);
        if (submenu[0x60] != 0) {
            g_hover.lsHoveredTile = -1;
            g_hover.lsHoveredBtn = -1;
        } else if (ready) {
            bool anyKey = (sm[0xad] != 0 || sm[0xb0] != 0 || sm[0xb3] != 0 ||
                           sm[0xb6] != 0 || sm[0xc5] != 0 || sm[0xc8] != 0);
            if (anyKey) {
                g_hover.lastInputMode = HoverInputMode::Keyboard;
                g_hover.lsHoveredTile = -1;
                g_hover.lsHoveredBtn = -1;
                g_hover.needsInitialSync = false;
            } else {
                bool scrollChanged = (*reinterpret_cast<const int32_t*>(submenu + 0x50) != g_hover.lsPrevScrollY);
                if (mouseMoved || mouseJustPressed ||
                    (g_hover.lastInputMode == HoverInputMode::Mouse && (g_hover.needsInitialSync || scrollChanged))) {
                    bool silent = (g_hover.needsInitialSync || scrollChanged) && !mouseMoved && !mouseJustPressed;
                    if (mouseMoved || mouseJustPressed) {
                        g_hover.lastInputMode = HoverInputMode::Mouse;
                    }
                    g_hover.needsInitialSync = false;
                    if (mouseValid) {
                        int32_t hitTile = HitTestLevelSelectTile(submenu, mx, my);
                        if (hitTile >= 0) {
                            g_hover.lsHoveredBtn = -1;
                            if (hitTile != g_hover.lsHoveredTile) {
                                g_hover.lsHoveredTile = hitTile;
                                if (!silent && !mouseJustPressed) {
                                    Game_PlaySfx(sm, 2);
                                }
                            }
                            if (submenu[0x44] != 0 && hitTile != *reinterpret_cast<const int32_t*>(submenu + 0x38)) {
                                submenu[0x44] = 0;
                            }
                            SetLevelSelectDisplayedTile(submenu, hitTile);
                        } else {
                            g_hover.lsHoveredTile = -1;
                            submenu[0x44] = 0;
                            int hitBtn = HitTestMenuButton(vtbl, mx, my);
                            if (hitBtn != g_hover.lsHoveredBtn) {
                                if (hitBtn > 0 && !silent && !mouseJustPressed) {
                                    Game_PlaySfx(sm, 2);
                                }
                                g_hover.lsHoveredBtn = hitBtn;
                            }
                        }
                    } else {
                        g_hover.lsHoveredTile = -1;
                        g_hover.lsHoveredBtn = -1;
                        submenu[0x44] = 0;
                    }
                } else if (g_hover.lastInputMode == HoverInputMode::Mouse && !mouseValid && !mouseDown) {
                    g_hover.lsHoveredTile = -1;
                    g_hover.lsHoveredBtn = -1;
                }
            }
        } else {
            g_hover.lsHoveredTile = -1;
            g_hover.lsHoveredBtn = -1;
        }

        CallSubmenuUpdate(submenu, dt);
        g_hover.lsPrevScrollY = *reinterpret_cast<const int32_t*>(submenu + 0x50);
        return;
    }

    // 3. Button-only menus (ScoresMenu, InstructionsMenu, CreditsMenu)
    if (vtbl == kVtblScores || vtbl == kVtblInstr || vtbl == kVtblCredits) {
        int minEnter = (vtbl == kVtblCredits) ? 0 : 8;
        bool ready = (*reinterpret_cast<const int32_t*>(submenu + 4) >= minEnter) &&
                     (*reinterpret_cast<const int32_t*>(submenu + 8) < 0);
        if (vtbl == kVtblCredits && submenu[0x48] != 0) {
            ready = false;
        }
        if (ready) {
            if (sm[0xad] != 0 || sm[0xb0] != 0 || sm[0xc5] != 0 || sm[0xc8] != 0) {
                g_hover.lastInputMode = HoverInputMode::Keyboard;
                g_hover.lsHoveredBtn = -1;
                g_hover.needsInitialSync = false;
            } else if (mouseMoved || mouseJustPressed ||
                       (g_hover.lastInputMode == HoverInputMode::Mouse && g_hover.needsInitialSync)) {
                bool silent = g_hover.needsInitialSync && !mouseMoved && !mouseJustPressed;
                if (mouseMoved || mouseJustPressed) {
                    g_hover.lastInputMode = HoverInputMode::Mouse;
                }
                g_hover.needsInitialSync = false;
                int hitBtn = mouseValid ? HitTestMenuButton(vtbl, mx, my) : -1;
                if (hitBtn != g_hover.lsHoveredBtn) {
                    if (hitBtn > 0 && !silent && !mouseJustPressed) {
                        Game_PlaySfx(sm, 2);
                    }
                    g_hover.lsHoveredBtn = hitBtn;
                }
            } else if (g_hover.lastInputMode == HoverInputMode::Mouse && !mouseValid && !mouseDown) {
                g_hover.lsHoveredBtn = -1;
            }
        } else {
            g_hover.lsHoveredBtn = -1;
        }
    }

    CallSubmenuUpdate(submenu, dt);
}

static void __declspec(naked) hk_menuSubUpdate() {
    __asm {
        push DWORD PTR [ebp + 0x8]
        push ecx
        call MenuSubUpdate_Impl
        add esp, 8
        ret
    }
}

static bool IsHeaderFallEnterPhase(const uint8_t* submenu, uint32_t vtbl, const uint8_t* sm) {
    if (submenu == nullptr || sm == nullptr) return false;
    int enterFrames = *reinterpret_cast<const int32_t*>(submenu + 4);
    int exitTimer   = *reinterpret_cast<const int32_t*>(submenu + 8);
    if (exitTimer >= 0 || enterFrames >= 8) return false;
    uint32_t prevScr = *reinterpret_cast<const uint32_t*>(sm + 0x54);
    if (vtbl == kVtblMainMenu) {
        return prevScr != 1 && prevScr != 0x12 && (prevScr < 0x14 || prevScr > 0x16);
    }
    if (vtbl == kVtblModeSel || vtbl == kVtblScoresSel || vtbl == kVtblOptions) {
        return prevScr != 2;
    }
    return false;
}

static bool IsHeaderRiseExitPhase(const uint8_t* submenu, uint32_t vtbl) {
    if (submenu == nullptr) return false;
    int exitTimer = *reinterpret_cast<const int32_t*>(submenu + 8);
    if (exitTimer < 0) return false;
    if (vtbl == kVtblMainMenu) return true;
    if (vtbl == kVtblModeSel || vtbl == kVtblScoresSel || vtbl == kVtblOptions) {
        int exitDest = *reinterpret_cast<const int32_t*>(submenu + 0xc);
        return exitDest != 2;
    }
    return false;
}

static void __cdecl MenuSubRender_Impl(uint8_t* submenu, int frame) {
    if (submenu == nullptr) return;
    uint32_t vtbl = *reinterpret_cast<const uint32_t*>(submenu);
    int pausePage = (vtbl == kVtblPause) ? *reinterpret_cast<const int32_t*>(submenu + 0x44) : -1;
    int selOff = GetMenuSelOffset(vtbl, pausePage);
    int exitTimer = *reinterpret_cast<const int32_t*>(submenu + 8);

    bool ready = false;
    if (selOff >= 0) {
        ready = IsStandardSlotMenuReady(submenu, vtbl, pausePage);
    } else if (vtbl == kVtblLevelSel || vtbl == kVtblScores || vtbl == kVtblInstr || vtbl == kVtblCredits) {
        int minEnter = (vtbl == kVtblCredits) ? 0 : 8;
        ready = (*reinterpret_cast<const int32_t*>(submenu + 4) >= minEnter) && (exitTimer < 0);
        if (vtbl == kVtblCredits && submenu[0x48] != 0) ready = false;
    }

    bool inOpts = (vtbl == kVtblOptions) || (vtbl == kVtblPause && pausePage == 1);
    bool inPauseOpts = (vtbl == kVtblPause && pausePage == 1);
    bool popupOpen = inOpts && IsOptionsPopupOpen(inPauseOpts);
    bool dragging = inOpts && IsOptionsSliderDragging(inPauseOpts);

    g_hover.renderingMenu = true;
    g_hover.renderInteractive = ready;
    g_hover.renderForceStrong = dragging;
    g_hover.renderForceWhite = false;
    g_hover.renderFrame = frame;

    bool swappedSel = false;
    int32_t savedSel = 0;
    const uint8_t* sm = GetScreenMgr();
    bool headerFall = IsHeaderFallEnterPhase(submenu, vtbl, sm);
    bool headerRise = IsHeaderRiseExitPhase(submenu, vtbl);
    if (selOff >= 0 && !popupOpen && !dragging && !headerRise) {
        // During enter animations (not yet ready) the menu object may be new/stale relative to the
        // hover context; in Mouse mode never show a vanilla-selected row until the cursor actually hovers one.
        // During header-fall returns (frames 0..7), keep sel intact so the box lands, its gray border
        // fades smoothly to white, and its text darkens #808080 -> #3b3b3b without blinking, while
        // suppressing the rainbow fill to pure white.
        uint32_t rState = sm ? *reinterpret_cast<const uint32_t*>(sm + 0x50) : 0;
        int rOptCat = -1;
        if (vtbl == kVtblOptions) rOptCat = GetActiveOptionsCategory(false);
        else if (vtbl == kVtblPause && pausePage == 1) rOptCat = GetActiveOptionsCategory(true);
        bool ctxStale = (submenu != g_hover.activeMenu || rState != g_hover.activeState ||
                         pausePage != g_hover.activeSubPage || rOptCat != g_hover.activeOptCat);
        bool hideSel = ctxStale ? (g_hover.lastInputMode == HoverInputMode::Mouse) : !g_hover.slotVisible;
        if (hideSel) {
            if (headerFall) {
                g_hover.renderForceWhite = true;
            } else {
                savedSel = *reinterpret_cast<const int32_t*>(submenu + selOff);
                *reinterpret_cast<int32_t*>(submenu + selOff) = -1;
                swappedSel = true;
            }
        }
    }

    CallSubmenuRender(submenu, frame);

    if (selOff >= 0 && ready && g_hover.slotVisible && !popupOpen && !inOpts) {
        int32_t curSel = *reinterpret_cast<const int32_t*>(submenu + selOff);
        DrawHoveredSlotBorder(submenu, vtbl, pausePage, curSel, frame);
    }

    if (swappedSel) {
        *reinterpret_cast<int32_t*>(submenu + selOff) = savedSel;
    }
    g_hover.renderingMenu = false;
    g_hover.renderInteractive = false;
    g_hover.renderForceStrong = false;
    g_hover.renderForceWhite = false;
}

static void __declspec(naked) hk_menuSubRender() {
    __asm {
        push DWORD PTR [esi + 0x4]
        push ecx
        call MenuSubRender_Impl
        add esp, 8
        ret
    }
}

static void __declspec(naked) hk_mainMenuBarGuard() {
    __asm {
        mov eax, DWORD PTR [ebp - 0x60]
        mov BYTE PTR [ebp - 0x3d], 0xff
        cmp DWORD PTR [edi + 0x10], 0
        jl mm_skip_bar
        cmp eax, ebx
        je mm_eq_bar
        mov edx, 0x4b9de8
        jmp edx
    mm_eq_bar:
        mov edx, 0x4b9e70
        jmp edx
    mm_skip_bar:
        mov edx, 0x4b9eb0
        jmp edx
    }
}

static int __cdecl CalcDrawButtonFillAlpha_Impl(int x, int y, int w, int h, int alpha, uint8_t pressed) {
    bool hovered = false;
    if (g_hover.renderInteractive && g_hover.lastInputMode == HoverInputMode::Mouse) {
        uint8_t* sm = GetScreenMgr();
        if (sm != nullptr && sm[0xa8] != 0) {
            int mx = *reinterpret_cast<const int32_t*>(sm + 0xa0);
            int my = *reinterpret_cast<const int32_t*>(sm + 0xa4);
            if (mx >= x && mx < x + w && my >= y && my < y + h) {
                hovered = true;
            }
        }
    }
    g_hover.lastDrawBtnPressed = (pressed != 0);
    g_hover.lastDrawBtnHovered = hovered;

    if (pressed != 0) return alpha;
    if (hovered) return (alpha * 25) / 100;
    return 0;
}

static void __cdecl DrawButtonBorder_Impl(int frame, int x, int y, int w, int h, int thickness, int alpha) {
    if (g_hover.lastDrawBtnPressed || g_hover.lastDrawBtnHovered) {
        Game_DrawRainbowRectBorder(frame, x, y, w, h, thickness, alpha);
        return;
    }
    uint8_t a = static_cast<uint8_t>(std::clamp(alpha, 0, 255));
    uint8_t col[4] = {0x7du, 0x7du, 0x7du, a};
    Game_DrawQuad(x, y, w, thickness, col);
    Game_DrawQuad(x, y + h - thickness, w, thickness, col);
    Game_DrawQuad(x, y, thickness, h, col);
    Game_DrawQuad(x + w - thickness, y, thickness, h, col);
}

static void __declspec(naked) hk_drawButtonAlpha() {
    __asm {
        push DWORD PTR [ebp + 0x24]
        push DWORD PTR [ebp + 0x20]
        push DWORD PTR [ebp + 0x1c]
        push DWORD PTR [ebp + 0x18]
        push DWORD PTR [ebp + 0x14]
        push DWORD PTR [ebp + 0x10]
        call CalcDrawButtonFillAlpha_Impl
        add esp, 24
        mov edx, 0x4961f9
        jmp edx
    }
}

} // namespace

namespace edgemod {

uint32_t __cdecl ApplyMenuSlotRainbowStyle(uint32_t rainbowRgb) {
    if (g_hover.renderForceWhite) {
        return 0xffffffu;
    }
    if (g_hover.renderForceStrong || g_hover.slotPressed) {
        return rainbowRgb;
    }
    return Game_ColorLerp(0xffffffu, rainbowRgb, kSubtleRainbowPct);
}

bool __cdecl IsCurrentMenuSlotVisible() {
    return g_hover.slotVisible;
}

bool __cdecl IsCurrentMenuSlotPressed() {
    return g_hover.slotPressed;
}

int32_t __cdecl GetLevelSelectHoveredTile() {
    return g_hover.lsHoveredTile;
}

bool __cdecl IsMenuInputKeyboardMode() {
    return g_hover.lastInputMode == HoverInputMode::Keyboard;
}

void __cdecl SetMenuInputKeyboardMode(bool isKeyboard) {
    g_hover.lastInputMode = isKeyboard ? HoverInputMode::Keyboard : HoverInputMode::Mouse;
}

int32_t __cdecl GetCurrentMenuRenderFrame() {
    return g_hover.renderFrame;
}

bool __cdecl IsMenuRenderInteractive() {
    return g_hover.renderInteractive;
}

bool __cdecl IsMenuRenderForceWhite() {
    return g_hover.renderForceWhite;
}

} // namespace edgemod

void RegisterMenuHover() {
    using namespace edgemod;

    // 1. MenuScreen::update submenu dispatch (0x49c9a2, 18 bytes)
    static const uint8_t exSubUpd[18] = {
        0x8b, 0x01, 0xf3, 0x0f, 0x10, 0x45, 0x08, 0x8b, 0x50,
        0x04, 0x51, 0xf3, 0x0f, 0x11, 0x04, 0x24, 0xff, 0xd2
    };
    RegRelCall(0x49c9a2, exSubUpd, 18, 18, &hk_menuSubUpdate, 0x90);

    // 2. MenuScreen::render submenu dispatch (0x49c8a9, 11 bytes)
    static const uint8_t exSubRen[11] = {
        0x8b, 0x01, 0x8b, 0x56, 0x04, 0x8b, 0x40, 0x08, 0x52, 0xff, 0xd0
    };
    RegRelCall(0x49c8a9, exSubRen, 11, 11, &hk_menuSubRender, 0x90);

    // 3. MainMenu highlight bar guard when sel < 0 (0x4b9dd9, 15 bytes)
    static const uint8_t exMmBar[15] = {
        0x8b, 0x45, 0xa0, 0xc6, 0x45, 0xc3, 0xff, 0x3b,
        0xc3, 0x0f, 0x84, 0x88, 0x00, 0x00, 0x00
    };
    RegRelJmp(0x4b9dd9, exMmBar, 15, 5, &hk_mainMenuBarGuard, 0x90, 0x4b9de8);

    // 4. Menu::drawButton hover alpha (0x4961dd, 28 bytes) & border (0x49628d, 5 bytes)
    static const uint8_t exBtnAlpha[28] = {
        0x80, 0x7d, 0x24, 0x00, 0x74, 0x05, 0x8b, 0x45, 0x20, 0xeb, 0x11, 0xb8, 0x67, 0x66,
        0x66, 0x66, 0xf7, 0x6d, 0x20, 0xd1, 0xfa, 0x8b, 0xc2, 0xc1, 0xe8, 0x1f, 0x03, 0xc2
    };
    RegRelJmp(0x4961dd, exBtnAlpha, 28, 5, &hk_drawButtonAlpha, 0x90, 0x4961f9);

    static const uint8_t exBtnBorder[5] = {0xe8, 0x9e, 0xfc, 0xff, 0xff};
    RegRelCall(0x49628d, exBtnBorder, 5, 5, reinterpret_cast<void*>(&DrawButtonBorder_Impl));

    // 7. Silence redundant mouse press-down SFX 2 on Menu::drawButton buttons (hover already plays SFX 2)
    static const uint8_t nop13[13] = {
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    static const uint8_t exLsBtnSfx[13] = {
        0x8b, 0x0d, 0x80, 0x90, 0x5f, 0x00, 0x6a, 0x02, 0xe8, 0xfa, 0xad, 0xfc, 0xff
    };
    static const uint8_t exScBtnSfx[13] = {
        0x8b, 0x0d, 0x80, 0x90, 0x5f, 0x00, 0x6a, 0x02, 0xe8, 0xc2, 0x23, 0xfd, 0xff
    };
    static const uint8_t exInBtnSfx[13] = {
        0x8b, 0x0d, 0x80, 0x90, 0x5f, 0x00, 0x6a, 0x02, 0xe8, 0x4e, 0xd9, 0xfc, 0xff
    };
    static const uint8_t exCrBtnSfx[13] = {
        0x8b, 0x0d, 0x80, 0x90, 0x5f, 0x00, 0x6a, 0x02, 0xe8, 0xd4, 0xfe, 0xfd, 0xff
    };
    RegBytes(0x4b67e9, exLsBtnSfx, nop13, 13); // LevelSelect back/go
    RegBytes(0x4af221, exScBtnSfx, nop13, 13); // Scores back
    RegBytes(0x4b3c95, exInBtnSfx, nop13, 13); // Instructions back
    RegBytes(0x4a170f, exCrBtnSfx, nop13, 13); // Credits skip

    // 8. Flush 50px slots (stride 0x32): grow bar/hit rect 5px up & 5px down (0x28 -> 0x32 high, top 0x56 -> 0x51)
    auto patchBytes = [](uint32_t va, std::initializer_list<uint8_t> ex, std::initializer_list<uint8_t> rp) {
        std::vector<uint8_t> e(ex), r(rp);
        RegBytes(va, e.data(), r.data(), e.size());
    };
    // MainMenu hit test + render bars (outer / inner / steady)
    patchBytes(0x4b979b, {0x83, 0xc0, 0x56},       {0x83, 0xc0, 0x51});
    patchBytes(0x4b97ba, {0x8d, 0x58, 0x28},       {0x8d, 0x58, 0x32});
    patchBytes(0x4b9e11, {0x6a, 0x2c},             {0x6a, 0x36});
    patchBytes(0x4b9e17, {0x8d, 0x44, 0x32, 0x54}, {0x8d, 0x44, 0x32, 0x4f});
    patchBytes(0x4b9e53, {0x6a, 0x28},             {0x6a, 0x32});
    patchBytes(0x4b9e59, {0x8d, 0x54, 0x31, 0x56}, {0x8d, 0x54, 0x31, 0x51});
    patchBytes(0x4b9e96, {0x6a, 0x28},             {0x6a, 0x32});
    patchBytes(0x4b9e9c, {0x8d, 0x4c, 0x02, 0x56}, {0x8d, 0x4c, 0x02, 0x51});

    // 9. Keep PauseMenu P0 & DebriefMenu option labels stationary (disable vanilla selected left-shift)
    patchBytes(0x4ad2ce, {0x83, 0xe2, 0x0e}, {0x83, 0xe2, 0x00}); // PauseMenu P0: and edx, 0 (was 14)
    patchBytes(0x4abce2, {0x83, 0xe1, 0x0c}, {0x83, 0xe1, 0x00}); // DebriefMenu: and ecx, 0 (was 12)

    // 10. LevelSelect: skip info-card slide reset on tile click (keep onEnter), launch on single click,
    //     and keep previous played level with pressed-down color
    patchBytes(0x4b672a, {0x74, 0x46}, {0xeb, 0x46}); // skip [edi+0x28] = -3 & old tile flash
    patchBytes(0x4b68eb, {0x39, 0x7e, 0x38, 0x75, 0x10}, {0x90, 0x90, 0x90, 0x90, 0x90}); // arm single-click launch
    static const uint8_t exLsPressed[5] = {0x3b, 0x5f, 0x38, 0x75, 0x37};
    RegRelJmp(0x4b8125, exLsPressed, 5, 5, &hk_lsTilePressedCheck);
}
