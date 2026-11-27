#include "mod_smooth_shared.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

using namespace edgemod;

// Extended smoothing state
uint8_t* g_extSubPtr   = nullptr;
int32_t  g_extPrevA    = 0;
int32_t  g_extLastA    = 0;
float    g_extEffA     = 0.0f;
int32_t  g_extPrevB    = 0;
int32_t  g_extLastB    = 0;
float    g_extEffB     = 0.0f;
uint8_t* g_extWsPtr    = nullptr;
uint8_t  g_wsPrevEc[2] = {0, 0};
int32_t  g_wsPrevEt[2] = {0, 0};
int32_t  g_wsPrevEr[2] = {0, 0};
int32_t  g_wsPrevX[2]  = {0, 0};
int32_t  g_wsPrevY[2]  = {0, 0};
int32_t  g_wsPrevElapsed   = 0;
int32_t  g_wsPrevTotalEdge = 0;
int32_t  g_wsPrevBuffTicks = 0;
float    g_etRemTmp    = 0.0f;
float    g_etTicksTmp  = 0.0f;
uint8_t  g_lsFlashPrev[64]{};
uint32_t g_hudMatOrig[16]{};
const uint8_t* g_creditsActiveObj = nullptr;
float    g_creditsSubFracY = 0.0f;
float    g_debNrSubFracX   = 0.0f;

static void SnapSubState(uint8_t* sub, uint32_t vt) {
    g_extSubPtr = sub;
    if (vt == 0x58d2fc /* InstructionsMenu */) {
        g_extPrevA = *reinterpret_cast<int32_t*>(sub + 0x28);
        g_extPrevB = *reinterpret_cast<int32_t*>(sub + 0x34);
    } else if (vt == 0x58d118 /* ScoresMenu */) {
        g_extPrevA = *reinterpret_cast<int32_t*>(sub + 0x6c);
        g_extPrevB = *reinterpret_cast<int32_t*>(sub + 0x74);
    } else if (vt == 0x58d51c /* LevelSelectMenu */) {
        g_extPrevA = *reinterpret_cast<int32_t*>(sub + 0x58);
        g_extPrevB = *reinterpret_cast<int32_t*>(sub + 0x28);
        uint8_t* flashArr = *reinterpret_cast<uint8_t**>(sub + 0x2c);
        if (flashArr != nullptr) {
            uint32_t cnt = *reinterpret_cast<uint32_t*>(sub + 0x18);
            if (cnt > 64) cnt = 64;
            for (uint32_t i = 0; i < cnt; ++i) {
                g_lsFlashPrev[i] = static_cast<uint8_t>(reinterpret_cast<uint32_t*>(flashArr)[i]);
            }
        }
    } else if (vt == 0x58ce58 /* DebriefMenu */) {
        g_extPrevA = *reinterpret_cast<int32_t*>(sub + 0x44);
    } else if (vt == 0x58cc18 /* ConfirmExitGameMenu */) {
        g_extPrevA = *reinterpret_cast<int32_t*>(sub + 0x18);
    }
}

static void ExtUpdPair(int slot, int32_t cur, bool snapDec, bool snapJump2) {
    int32_t& prev = (slot == 0) ? g_extPrevA : g_extPrevB;
    int32_t& last = (slot == 0) ? g_extLastA : g_extLastB;
    float&   eff  = (slot == 0) ? g_extEffA  : g_extEffB;

    if (g_justTicked) {
        last = cur;
        bool doSnap = false;
        if (snapDec && cur < prev) doSnap = true;
        if (!doSnap && snapJump2) {
            int32_t d = cur - prev;
            if (d > 2 || d < -2) doSnap = true;
        }
        if (doSnap) prev = cur;
    } else if (cur != last) {
        last = cur;
        prev = cur;
    }
    eff = static_cast<float>(prev) + static_cast<float>(cur - prev) * g_uiAlpha;
}

static inline uint32_t RgbToBgraFf(uint32_t rgb) {
    return ((rgb & 0xffu) << 16) | (rgb & 0xff00u) | ((rgb >> 16) & 0xffu) | 0xff000000u;
}

static inline void StoreRgbaBytes(uint8_t* dst, uint32_t rgb) {
    dst[0] = static_cast<uint8_t>((rgb >> 16) & 0xff);
    dst[1] = static_cast<uint8_t>((rgb >> 8) & 0xff);
    dst[2] = static_cast<uint8_t>(rgb & 0xff);
    dst[3] = 0xff;
}

} // namespace

namespace edgemod {

void __cdecl OnSavePrev_Ext() {
    if (g_subPtr != nullptr) {
        SnapSubState(g_subPtr, *reinterpret_cast<uint32_t*>(g_subPtr));
    }
    if (g_wsPtr != nullptr) {
        g_extWsPtr = g_wsPtr;
        g_wsPrevEc[0] = g_wsPtr[0x6c];
        g_wsPrevEc[1] = g_wsPtr[0x6d];
        g_wsPrevEt[0] = *reinterpret_cast<int32_t*>(g_wsPtr + 0x78);
        g_wsPrevEt[1] = *reinterpret_cast<int32_t*>(g_wsPtr + 0x7c);
        g_wsPrevEr[0] = *reinterpret_cast<int32_t*>(g_wsPtr + 0x80);
        g_wsPrevEr[1] = *reinterpret_cast<int32_t*>(g_wsPtr + 0x84);
        g_wsPrevX[0]  = *reinterpret_cast<int32_t*>(g_wsPtr + 0x2148);
        g_wsPrevY[0]  = *reinterpret_cast<int32_t*>(g_wsPtr + 0x214c);
        g_wsPrevX[1]  = *reinterpret_cast<int32_t*>(g_wsPtr + 0x2150);
        g_wsPrevY[1]  = *reinterpret_cast<int32_t*>(g_wsPtr + 0x2154);
        g_wsPrevElapsed = *reinterpret_cast<int32_t*>(g_wsPtr + 0x10c);
        int32_t commEdge = *reinterpret_cast<int32_t*>(g_wsPtr + 0x108);
        int32_t actEdge  = (g_wsPtr[0x6c] != 0) ? *reinterpret_cast<int32_t*>(g_wsPtr + 0x78) : 0;
        g_wsPrevTotalEdge = commEdge + actEdge;
        uint8_t* cube = *reinterpret_cast<uint8_t* const*>(g_wsPtr + 0x34);
        if (cube != nullptr) {
            g_wsPrevBuffTicks = *reinterpret_cast<const int32_t*>(cube + 0x168);
        } else {
            g_wsPrevBuffTicks = 0;
        }
    }
}

void __cdecl OnInterpolate_Ext(uint8_t* sm) {
    uint8_t* sub = nullptr;
    uint32_t vt  = 0;
    GetActiveSub(sm, &sub, &vt);
    if (sub == nullptr) return;
    if (g_extSubPtr != sub) SnapSubState(sub, vt);

    if (vt == 0x58d2fc) {
        ExtUpdPair(0, *reinterpret_cast<int32_t*>(sub + 0x28), false, false);
        ExtUpdPair(1, *reinterpret_cast<int32_t*>(sub + 0x34), true, false);
    } else if (vt == 0x58d118) {
        ExtUpdPair(0, *reinterpret_cast<int32_t*>(sub + 0x6c), false, false);
        ExtUpdPair(1, *reinterpret_cast<int32_t*>(sub + 0x74), false, true);
    } else if (vt == 0x58d51c) {
        ExtUpdPair(0, *reinterpret_cast<int32_t*>(sub + 0x58), false, false);
        ExtUpdPair(1, *reinterpret_cast<int32_t*>(sub + 0x28), true, false);
    } else if (vt == 0x58ce58) {
        ExtUpdPair(0, *reinterpret_cast<int32_t*>(sub + 0x44), false, false);
    } else if (vt == 0x58cc18) {
        ExtUpdPair(0, *reinterpret_cast<int32_t*>(sub + 0x18), false, false);
    }
}

void __cdecl SetSepGlyph() {
    uint32_t* glyphs = reinterpret_cast<uint32_t*>(0x5f3b88);
    if (g_timeFmt != 0) {
        glyphs[0] = 0x01010400;
        glyphs[1] = 0;
        glyphs[2] = 0;
    } else {
        glyphs[0] = 0x06010000;
        glyphs[1] = 0x05010001;
        glyphs[2] = 0x04010002;
    }
}

} // namespace edgemod

namespace {

// ------------------------------------------------------------------
// 1. ScoresMenu, CreditsMenu, InstructionsMenu, LevelSelect, Exit overlays, ConfirmExitGame
// ------------------------------------------------------------------
static void __fastcall ScoresRenderWrap_Impl(uint8_t* scoresMenu, void*, uint32_t arg1) {
    int32_t saved6c = *reinterpret_cast<int32_t*>(scoresMenu + 0x6c);
    if (g_extSubPtr == scoresMenu) {
        *reinterpret_cast<int32_t*>(scoresMenu + 0x6c) = static_cast<int32_t>(std::lrint(g_extEffA));
    }
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(0x4af660)(scoresMenu, arg1);
    *reinterpret_cast<int32_t*>(scoresMenu + 0x6c) = saved6c;
}

static void __fastcall CreditsRenderWrap_Impl(uint8_t* cm, void*, uint32_t arg1) {
    g_creditsActiveObj = cm;
    g_creditsSubFracY = 0.0f;
    float saved4c = *reinterpret_cast<float*>(cm + 0x4c);
    if (cm[0x48] != 0 && cm[0x49] == 0 && *reinterpret_cast<int32_t*>(cm + 0x4) >= 5) {
        float speed = *reinterpret_cast<float*>(cm + 0x50);
        float eff = saved4c + speed * g_uiAlpha;
        if (speed < 0.0f) {
            float minY = *reinterpret_cast<const float*>(0x58b9c8);
            if (eff < minY) eff = minY;
        }
        *reinterpret_cast<float*>(cm + 0x4c) = eff;
        g_creditsSubFracY = eff - static_cast<float>(static_cast<int32_t>(eff));
    }
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(0x4a1a70)(cm, arg1);
    *reinterpret_cast<float*>(cm + 0x4c) = saved4c;
    g_creditsSubFracY = 0.0f;
    g_creditsActiveObj = nullptr;
}

static int32_t __cdecl CreditsIntroFadePct_Impl(const uint8_t* cm) {
    float sp = *reinterpret_cast<const float*>(cm + 0x4c);
    if (sp >= 8.0f) return -1;
    return ClampPct0_100(sp * 12.5f);
}

static void __declspec(naked) hk_creditsIntroFade() {
    __asm {
        push ebx
        call CreditsIntroFadePct_Impl
        add esp, 4
        test eax, eax
        jl cif_done
        mov edx, 0x4a1ac4
        jmp edx
    cif_done:
        mov cl, BYTE PTR [ebx + 0x18]
        mov edx, 0x4a1b1d
        jmp edx
    }
}

static int __cdecl CreditsDrawText_Impl(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int shadowFlag, int a7, int alignFlags
) {
    ScopedUiMatrixTranslate matGuard(0.0f, g_creditsSubFracY);
    return Game_DrawText(ws, x, y, scaleX, scaleY, shadowFlag, a7, alignFlags);
}

static int __cdecl CreditsDrawGameOver_Impl(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int shadowFlag, int a7, int alignFlags
) {
    if (g_creditsActiveObj != nullptr && g_creditsActiveObj[0x49] == 0) {
        return CreditsDrawText_Impl(ws, x, y, scaleX, scaleY, shadowFlag, a7, alignFlags);
    }
    return Game_DrawText(ws, x, y, scaleX, scaleY, shadowFlag, a7, alignFlags);
}

static uint32_t __cdecl CreditsHalfSpeedRb_Impl(int32_t frame, int32_t idx) {
    float f = (static_cast<float>(frame) + g_uiAlpha) * 0.5f + static_cast<float>(idx);
    return CalcSmoothRainbow(f, 1.0f);
}

static void __declspec(naked) hk_creditsHdrRb() {
    __asm {
        mov eax, DWORD PTR [ebp - 0x40]
        lea ecx, [eax + 1]
        mov DWORD PTR [ebp - 0x40], ecx
        push eax
        push DWORD PTR [ebp - 0x58]
        call CreditsHalfSpeedRb_Impl
        add esp, 8
        mov edx, eax
        mov eax, DWORD PTR [ebp - 0x4c]
        mov ecx, 0x4a1c45
        jmp ecx
    }
}

static void __declspec(naked) hk_creditsEndRb() {
    __asm {
        push DWORD PTR [ebp - 0x40]
        push DWORD PTR [ebp - 0x58]
        call CreditsHalfSpeedRb_Impl
        add esp, 8
        mov edx, eax
        mov BYTE PTR [ebp - 0x3d], 0xff
        mov ecx, 0x4a1d04
        jmp ecx
    }
}

static void __declspec(naked) hk_creditsCompRb() {
    __asm {
        push ecx
        push edi
        call GetSmoothMenuRainbow
        add esp, 4
        pop ecx
        mov edx, 0x4a1e0b
        jmp edx
    }
}

static void __cdecl ScoresCardFlip_Impl(uint8_t* sm, int32_t cardIdx, uint8_t* outBack, int32_t* outScale70, int32_t* outScaleTxt) {
    int32_t scale = 4096;
    if (g_extSubPtr == sm) {
        float diff = g_extEffB - static_cast<float>(cardIdx);
        float scaleF = 4096.0f;
        if (diff < 0.0f) {
            *outBack = 0;
            if (diff < -1.0f) {
                scaleF = (3.0f - diff) * 512.0f;
            } else {
                scaleF = diff * 2048.0f + 4096.0f;
            }
        } else {
            float p = std::fmod(diff, 200.0f);
            if (p < 0.0f) p += 200.0f;
            bool back = (p >= 100.0f);
            *outBack = back ? 1 : 0;
            if (!back) {
                if (diff > 8.0f && p < 8.0f) scaleF = p * 512.0f;
                else if (p >= 91.0f) {
                    float t = 99.0f - p;
                    scaleF = (t > 0.0f ? t : 0.0f) * 512.0f;
                }
            } else {
                if (p < 108.0f) scaleF = (p - 100.0f) * 512.0f;
                else if (p >= 191.0f) {
                    float t = 199.0f - p;
                    scaleF = (t > 0.0f ? t : 0.0f) * 512.0f;
                }
            }
        }
        if (scaleF > 4096.0f) scaleF = 4096.0f;
        scale = static_cast<int32_t>(std::lrint(scaleF));
    } else {
        int32_t rawDiff = *reinterpret_cast<int32_t*>(sm + 0x74) - cardIdx;
        int32_t p = static_cast<uint32_t>(rawDiff) % 200u;
        bool back = (p >= 100);
        *outBack = back ? 1 : 0;
        if (!back) {
            if (rawDiff > 8 && p < 8) scale = (p << 12) >> 3;
            else if (p >= 92) scale = ((99 - p) << 12) >> 3;
        } else {
            if (p < 108) scale = ((p - 100) << 12) >> 3;
            else if (p >= 192) scale = ((199 - p) << 12) >> 3;
        }
    }
    *outScale70 = (scale >= 0x3e8) ? scale : 0;
    *outScaleTxt = (scale >= 0x100) ? scale : 0x100;
}

static void __declspec(naked) hk_scoresCardFlip() {
    __asm {
        sub esp, 4
        push esp
        lea eax, [ebp - 0x70]
        push eax
        lea eax, [ebp - 0x91]
        push eax
        push DWORD PTR [ebp - 0x50]
        push DWORD PTR [ebp - 0x5c]
        call ScoresCardFlip_Impl
        add esp, 20
        pop ebx
        mov eax, 0x4afe40
        jmp eax
    }
}

static void __declspec(naked) hk_scoresDotRainbow() {
    __asm {
        mov eax, DWORD PTR [ebp - 0xa4]
        sub eax, ecx
        sub eax, DWORD PTR [ebp - 0x50]
        push eax
        call GetSmoothMenuRainbow
        add esp, 4
        bswap eax
        shr eax, 8
        or eax, 0xff000000
        mov DWORD PTR [ebp - 0xb4], eax
        mov DWORD PTR [ebp - 0x88], eax
        mov edx, DWORD PTR [ebp - 0x9c]
        mov eax, 0x4b0ab5
        jmp eax
    }
}

static void __declspec(naked) hk_instrSubScrollY() {
    __asm {
        mov edx, DWORD PTR [ebp - 0x44]
        cmp g_extSubPtr, edx
        jne iss_raw
        cvtss2si edx, g_extEffA
        sub edi, edx
        ret
    iss_raw:
        sub edi, DWORD PTR [edx + 0x28]
        ret
    }
}

static void __declspec(naked) hk_instrSRainbow() {
    __asm {
        mov eax, DWORD PTR [ebp - 0x70]
        inc eax
        push ecx
        push eax
        call GetSmoothMenuRainbow
        add esp, 4
        pop ecx
        mov edx, eax
        ret
    }
}

static void __declspec(naked) hk_drawBtnRainbow() {
    __asm {
        sub esp, 8
        movss DWORD PTR [esp], xmm0
        movss DWORD PTR [esp + 4], xmm1
        push eax
        push edx
        push DWORD PTR [ebp + 0x18]
        call GetSmoothMenuRainbow
        add esp, 4
        mov ecx, eax
        pop edx
        pop eax
        movss xmm0, DWORD PTR [esp]
        movss xmm1, DWORD PTR [esp + 4]
        add esp, 8
        ret
    }
}

// ScoresSelectMenu rainbow hooks
static void __declspec(naked) hk_rb_4b1c71() {
    __asm {
        push edi
        call GetSmoothSlotRainbow
        add esp, 4
        mov edi, OFFSET g_rbTmp
        mov ecx, 0x4b1c8e
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b1d5a() {
    __asm {
        push edi
        call GetSmoothSlotRainbow
        add esp, 4
        mov edx, eax
        mov ecx, 0x4b1d70
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b1dfd() {
    __asm {
        push edi
        call GetSmoothSlotRainbow
        add esp, 4
        mov ecx, 0x4b1e13
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b1f6a() {
    __asm {
        push edi
        call GetSmoothSlotRainbow
        add esp, 4
        mov ecx, 0x4b1f80
        jmp ecx
    }
}

// LevelSelectMenu hooks
static int32_t __cdecl GetLsThumbY(const uint8_t* ls) {
    if (g_extSubPtr == ls) return static_cast<int32_t>(std::lrint(g_extEffA));
    return *reinterpret_cast<const int32_t*>(ls + 0x58);
}
static void __declspec(naked) hk_lsThumbEdx() {
    __asm {
        push eax
        push ecx
        push edi
        call GetLsThumbY
        add esp, 4
        pop ecx
        mov edx, eax
        pop eax
        lea ecx, [ebp - 0x40]
        ret
    }
}
static void __declspec(naked) hk_lsThumbEcx() {
    __asm {
        push edx
        push edi
        call GetLsThumbY
        add esp, 4
        pop edx
        mov ecx, eax
        sar edx, 5
        ret
    }
}

static int32_t __cdecl LsTileFlashPct_Impl(const uint8_t* ls, int32_t curTimer, uint32_t tileIdx, int32_t* outPct) {
    if (*reinterpret_cast<const int32_t*>(ls + 0x8) >= 0) return 0;
    float eff = static_cast<float>(curTimer);
    if (g_extSubPtr == ls && tileIdx < 64) {
        int32_t prev = g_lsFlashPrev[tileIdx];
        if (curTimer <= prev) {
            eff = static_cast<float>(prev) + static_cast<float>(curTimer - prev) * g_uiAlpha;
        }
    }
    if (eff <= 0.0f) return 0;
    int32_t pct = ClampPct0_100((eff * 100.0f) / 15.0f);
    if (pct <= 0) return 0;
    *outPct = pct;
    return 1;
}
static void __declspec(naked) hk_lsTileFlashPct() {
    __asm {
        sub esp, 4
        push esp
        push ebx
        push eax
        push edi
        call LsTileFlashPct_Impl
        add esp, 16
        pop ecx
        test eax, eax
        je ltf_skip
        mov edx, 0x4b81c5
        jmp edx
    ltf_skip:
        mov edx, 0x4b8205
        jmp edx
    }
}

static void __cdecl RenderLsTileBadgeDots_Impl(
    const uint8_t* ls,
    uint32_t tileIdx,
    int32_t frame,
    int32_t fadePct,
    int32_t tileX,
    int32_t tileY
) {
    if (ls == nullptr) return;
    int32_t count = *reinterpret_cast<const int32_t*>(ls + 0x18);
    if (tileIdx >= static_cast<uint32_t>(count)) return;
    const uint8_t* tileFlags = *reinterpret_cast<const uint8_t* const*>(ls + 0x4c);
    if (tileFlags == nullptr) return;

    int32_t dotCount = static_cast<int32_t>(tileFlags[tileIdx * 2 + 1]);
    if (dotCount <= 0) return;
    if (dotCount > 3) dotCount = 3;

    // Vanilla dot 0 is 4x4 at (tileX + 3, tileY + 3).
    // Dots sit at left (+3), center (+15), and right (+27) with an 8px gap (12px pitch),
    // fitting symmetrically inside the 30x30 inner square [tileX+2..tileX+32) with 1px side margins.
    for (int32_t i = 0; i < dotCount; ++i) {
        float f = (static_cast<float>(frame) + g_uiAlpha) / 3.0f
                - static_cast<float>(tileIdx >> 1)
                - static_cast<float>(i)
                + 130.0f;
        uint32_t rawRgb = CalcSmoothRainbow(f, 1.0f);
        uint32_t fadedRgb = Game_ColorLerp(0xffffffu, rawRgb, fadePct);
        uint8_t rgba[4] = {
            static_cast<uint8_t>((fadedRgb >> 16) & 0xffu),
            static_cast<uint8_t>((fadedRgb >> 8) & 0xffu),
            static_cast<uint8_t>(fadedRgb & 0xffu),
            0xffu
        };
        Game_DrawQuad(tileX + 3 + i * 12, tileY + 3, 4, 4, rgba);
    }
}

static void __declspec(naked) hk_lsTileBadgeRainbow() {
    __asm {
        push DWORD PTR [ebp - 0x60] // tileY
        push DWORD PTR [ebp - 0x44] // tileX
        push esi                    // fadePct
        push DWORD PTR [ebp - 0x54] // frame
        push ebx                    // tileIdx
        push edi                    // ls
        call RenderLsTileBadgeDots_Impl
        add esp, 24
        mov eax, 0x4b82d9
        jmp eax
    }
}

static float GetLsEff28(const uint8_t* ls) {
    float v = (g_extSubPtr == ls) ? g_extEffB : static_cast<float>(*reinterpret_cast<const int32_t*>(ls + 0x28));
    return v > 0.0f ? v : 0.0f;
}

static int32_t __cdecl LsInfoCardPct7_Impl(const uint8_t* ls, int32_t fallbackPct) {
    if (*reinterpret_cast<const int32_t*>(ls + 0x8) >= 0) return fallbackPct;
    return ClampPct0_100(GetLsEff28(ls) * (100.0f / 7.0f));
}
static void __declspec(naked) hk_lsInfoCardPct7() {
    __asm {
        push esi
        push edi
        call LsInfoCardPct7_Impl
        add esp, 8
        mov ebx, eax
        mov eax, 0x4b8566
        jmp eax
    }
}

static int32_t __cdecl LsInfoCardSlideX_Impl(const uint8_t* ls) {
    float t = GetLsEff28(ls) / 5.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return static_cast<int32_t>(std::lrint(t * 321.0f - 50.0f));
}
static void __declspec(naked) hk_lsInfoCardSlideX() {
    __asm {
        push edi
        call LsInfoCardSlideX_Impl
        add esp, 4
        mov DWORD PTR [ebp - 0x58], eax
        add esp, 0x20
        push 0xffffff
        push ebx
        mov edx, DWORD PTR [ebp - 0x48]
        add edx, 0x2d
        mov ecx, 0x4b868c
        jmp ecx
    }
}

static void __declspec(naked) hk_rb_4b86fc() {
    __asm {
        mov eax, DWORD PTR [ebp - 0x54]
        sub eax, ecx
        push eax
        call GetSmoothMenuRainbow
        add esp, 4
        mov edx, eax
        mov ecx, 0x4b8711
        jmp ecx
    }
}

static void __declspec(naked) hk_rb_4b6c3e() {
    __asm {
        push ebx
        call GetSmoothMenuRainbow
        add esp, 4
        mov ecx, 0x4b6c54
        jmp ecx
    }
}

static int32_t __cdecl CalcSubExitOverlayAlpha_Impl(const uint8_t* sub) {
    int32_t cur8 = *reinterpret_cast<const int32_t*>(sub + 0x8);
    int32_t prev8 = (g_subPtr == sub) ? g_subPrev8 : cur8;
    float a = 255.0f - SmoothCounterPair(cur8, prev8, true) * 31.875f;
    if (a < 0.0f) a = 0.0f;
    if (a > 255.0f) a = 255.0f;
    return static_cast<int32_t>(std::lrint(a));
}

#define DEFINE_SUB_EXIT_OVERLAY_HOOK(Name, ResumeImm) \
    static void __declspec(naked) Name() { \
        __asm { push edi } \
        __asm { call CalcSubExitOverlayAlpha_Impl } \
        __asm { add esp, 4 } \
        __asm { mov ecx, eax } \
        __asm { mov eax, ResumeImm } \
        __asm { jmp eax } \
    }

DEFINE_SUB_EXIT_OVERLAY_HOOK(hk_subExitOv_4b9689, 0x4b96a5)
DEFINE_SUB_EXIT_OVERLAY_HOOK(hk_subExitOv_4b896b, 0x4b8987)

// ConfirmExitGameMenu
static int32_t __cdecl CegBoxHeight_Impl(const uint8_t* ceg) {
    int32_t cur8 = *reinterpret_cast<const int32_t*>(ceg + 0x8);
    float sm = 0.0f;
    if (cur8 >= 0) {
        int32_t prev8 = (g_subPtr == ceg) ? g_subPrev8 : cur8;
        sm = SmoothCounterPair(cur8, prev8, true);
        if (sm >= 4.0f) return 0x7e;
    } else {
        int32_t cur4 = *reinterpret_cast<const int32_t*>(ceg + 0x4);
        int32_t prev4 = (g_subPtr == ceg) ? g_subPrev4 : cur4;
        if (cur4 >= 4 && prev4 >= 4) return 0x7e;
        sm = SmoothCounterPair(cur4, prev4, false);
    }
    float t = 1.0f - sm * 0.25f;
    if (t < 0.0f) t = 0.0f;
    int32_t fy = reinterpret_cast<int32_t(__cdecl*)()>(0x496060)();
    return static_cast<int32_t>(std::lrint(static_cast<float>(fy - 0x7e) * t)) + 0x7e;
}
static void __declspec(naked) hk_cegBoxHeight() {
    __asm {
        push ebx
        call CegBoxHeight_Impl
        add esp, 4
        mov DWORD PTR [ebp - 0x54], eax
        mov eax, 0x4a8e9b
        jmp eax
    }
}

static int32_t __cdecl CegFadePct_Impl(const uint8_t* ceg) {
    int32_t cur4 = *reinterpret_cast<const int32_t*>(ceg + 0x4);
    if (cur4 < 8) {
        uint8_t* mgr = GetScreenMgr();
        int32_t prev = *reinterpret_cast<int32_t*>(mgr + 0x54);
        if (prev == 2 || prev == 9) {
            int32_t prev4 = (g_subPtr == ceg) ? g_subPrev4 : cur4;
            return DivPct8(SmoothCounterPair(cur4, prev4, false));
        }
        if (cur4 <= 4) return 100;
        return CalcSubFadePct(ceg, 1);
    }
    int32_t cur8 = *reinterpret_cast<const int32_t*>(ceg + 0x8);
    if (cur8 < 0) return CalcSubFadePct(ceg, 1);
    int32_t prev8 = (g_subPtr == ceg) ? g_subPrev8 : cur8;
    return DivPct4M4(SmoothCounterPair(cur8, prev8, true));
}
static void __declspec(naked) hk_cegFadePct() {
    __asm {
        push ebx
        call CegFadePct_Impl
        add esp, 4
        mov esi, eax
        mov eax, 0x4a8f3f
        jmp eax
    }
}

static uint8_t __cdecl CegBlackoutAlpha_Impl(const uint8_t* ceg) {
    float eff = (g_extSubPtr == ceg) ? g_extEffA : static_cast<float>(*reinterpret_cast<const int32_t*>(ceg + 0x18));
    float a = eff * 31.875f;
    if (a < 0.0f) a = 0.0f;
    if (a > 255.0f) a = 255.0f;
    return static_cast<uint8_t>(std::lrint(a));
}
static void __declspec(naked) hk_cegBlackoutAlpha() {
    __asm {
        push DWORD PTR [ebp - 0x48]
        call CegBlackoutAlpha_Impl
        add esp, 4
        mov edx, 0x4a9511
        jmp edx
    }
}

static void __declspec(naked) hk_rb_4a8f84() {
    __asm {
        push edi
        call GetSmoothSlotRainbow
        add esp, 4
        mov ecx, eax
        mov ebx, OFFSET g_rbTmp
        mov eax, 0x4a8fa3
        jmp eax
    }
}
#define DEFINE_RB_CEG_ECX_HOOK(Name, ResumeImm) \
    static void __declspec(naked) Name() { \
        __asm { push edi } \
        __asm { call GetSmoothSlotRainbow } \
        __asm { add esp, 4 } \
        __asm { mov ecx, eax } \
        __asm { mov eax, ResumeImm } \
        __asm { jmp eax } \
    }

#define DEFINE_RB_CEG_EAX_HOOK(Name, ResumeImm) \
    static void __declspec(naked) Name() { \
        __asm { push edi } \
        __asm { call GetSmoothSlotRainbow } \
        __asm { add esp, 4 } \
        __asm { mov edx, ResumeImm } \
        __asm { jmp edx } \
    }

DEFINE_RB_CEG_ECX_HOOK(hk_rb_4a9045, 0x4a905d)
DEFINE_RB_CEG_ECX_HOOK(hk_rb_4a90f8, 0x4a9110)
DEFINE_RB_CEG_EAX_HOOK(hk_rb_4a926b, 0x4a9283)
DEFINE_RB_CEG_EAX_HOOK(hk_rb_4a9390, 0x4a93a8)

// ------------------------------------------------------------------
// 2. DebriefMenu (Stages 1..15)
// ------------------------------------------------------------------
static float GetDebEff44(const uint8_t* deb) {
    return (g_extSubPtr == deb) ? g_extEffA : static_cast<float>(*reinterpret_cast<const int32_t*>(deb + 0x44));
}

static uint32_t __cdecl DebTitleRb_Impl(const uint8_t* deb, int32_t exitFadePct) {
    uint32_t rb = CalcSmoothRainbow(GetDebEff44(deb), 1.0f);
    return Game_ColorLerp(0xffffff, rb, exitFadePct);
}
static void __declspec(naked) hk_debTitleRb() {
    __asm {
        push ebx
        push esi
        call DebTitleRb_Impl
        add esp, 8
        add esp, 0x14
        mov edx, 0x4a9b45
        jmp edx
    }
}

static uint32_t __cdecl DebColorStage_Impl(
    const uint8_t* deb, uint8_t* ebpFrame, int32_t startTick, uint32_t targetCol,
    bool whiteEnter, int32_t rbOff, int32_t exitFadePct
) {
    float eff = GetDebEff44(deb);
    uint32_t tgt = targetCol;
    if (tgt == 0) {
        tgt = CalcSmoothRainbow(eff + static_cast<float>(rbOff), 1.0f);
    }
    uint32_t finalCol = tgt;
    if (*reinterpret_cast<const int32_t*>(deb + 0x8) < 0) {
        int32_t fadePct = static_cast<int32_t>(std::lrint((eff - static_cast<float>(startTick)) * 20.0f));
        if (fadePct < 100) {
            if (fadePct < 0) fadePct = 0;
            uint32_t fromCol = whiteEnter ? 0xffffff : 0x3b3b3b;
            finalCol = Game_ColorLerp(fromCol, tgt, fadePct);
        }
    }
    uint32_t outRgb = Game_ColorLerp(0xffffff, finalCol, exitFadePct);
    StoreRgbaBytes(ebpFrame - 0xdc, outRgb);
    return outRgb;
}

static uint32_t __cdecl DebNrScale_Impl(const uint8_t* deb, int32_t* outScale) {
    g_debNrSubFracX = 0.0f;
    float eff = GetDebEff44(deb);
    if (eff < 14.0f) eff = 14.0f;
    if (eff > 19.0f) eff = 19.0f;
    *outScale = static_cast<int32_t>(std::lrint(((19.0f - eff) / 5.0f) * 40960.0f)) + 0x1000;
    return *reinterpret_cast<const uint32_t*>(0x589ddc);
}
static void __declspec(naked) hk_debNrScale() {
    __asm {
        sub esp, 4
        push esp
        push DWORD PTR [ebp - 0xec]
        call DebNrScale_Impl
        add esp, 8
        pop esi
        ret
    }
}

static int32_t __cdecl CalcDebNrMarqueeDelta(const uint8_t* deb, int32_t width) {
    if (width <= 0) {
        g_debNrSubFracX = 0.0f;
        return 0;
    }
    double eff = static_cast<double>(GetDebEff44(deb));
    double f = eff * 4.0 - 76.0;
    if (f <= 0.0) {
        g_debNrSubFracX = 0.0f;
        return 0;
    }
    double rem = std::fmod(f, static_cast<double>(width));
    if (rem < 0.0) rem += static_cast<double>(width);
    double fl = std::floor(rem);
    g_debNrSubFracX = static_cast<float>(rem - fl);
    int32_t d = static_cast<int32_t>(fl);
    if (d < 0) d = 0;
    if (d >= width) d %= width;
    return d;
}

static int __cdecl DebNrDrawText_Impl(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int shadowFlag, int a7, int alignFlags
) {
    ScopedUiMatrixTranslate matGuard(-g_debNrSubFracX, 0.0f);
    return Game_DrawText(ws, x, y, scaleX, scaleY, shadowFlag, a7, alignFlags);
}

static void __declspec(naked) hk_debNrMarquee() {
    __asm {
        mov DWORD PTR [ebp - 0xdc], ecx
        lea edi, [eax + 0xa]
        cmp DWORD PTR [edx + 0x44], 19
        jl dnm_skip
        push ecx
        push edx
        call CalcDebNrMarqueeDelta
        add esp, 8
        sub edi, eax
    dnm_skip:
        mov eax, 0x4aaf37
        jmp eax
    }
}

static int32_t __cdecl DebRankSlideX_Impl(const uint8_t* deb) {
    float eff = GetDebEff44(deb);
    if (eff < 25.0f) eff = 25.0f;
    if (eff > 30.0f) eff = 30.0f;
    return static_cast<int32_t>(std::lrint(((eff - 30.0f) / 5.0f) * 145.0f));
}

static int32_t __cdecl DebBoxHeight_Impl(const uint8_t* deb) {
    float eff = GetDebEff44(deb);
    float t = eff - 34.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 5.0f) t = 5.0f;
    int32_t h = static_cast<int32_t>(std::lrint((t / 5.0f) * 119.0f));
    return h < 10 ? 10 : h;
}

static int32_t __cdecl DebBarWidth_Impl(const uint8_t* deb) {
    float eff = GetDebEff44(deb);
    float t = eff - 35.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 7.0f) t = 7.0f;
    int32_t full = (static_cast<int32_t>(*reinterpret_cast<const uint8_t*>(0x5f91e8)) << 15) + 10;
    return static_cast<int32_t>(std::lrint((t / 7.0f) * static_cast<float>(full)));
}

static void __cdecl DebRankStamp_Impl(const uint8_t* deb, int32_t* outEsi, int32_t* outSy, int32_t* outEdiPct) {
    float eff = GetDebEff44(deb);
    if (eff > 43.0f) eff = 43.0f;
    if (eff < 42.0f) eff = 42.0f;
    float t = eff - 38.0f; // [4.0, 5.0]
    *outEsi = static_cast<int32_t>(t);
    float norm = t / 5.0f;
    int32_t sy = static_cast<int32_t>(std::lrint(norm * 28672.0f));
    int32_t minSy = 0x1000 / static_cast<int32_t>(*reinterpret_cast<const uint8_t*>(0x5f91e9));
    if (sy < minSy) sy = minSy;
    *outSy = sy;
    *outEdiPct = ClampPct0_100(norm * 100.0f);
}

static void __cdecl DebRankOverlay_Impl(const uint8_t* deb, uint8_t* ebpFrame, int32_t exitFadePct, int32_t ediVal) {
    float eff = GetDebEff44(deb);
    const uint8_t* rankPtr = *reinterpret_cast<const uint8_t* const*>(ebpFrame - 0xf8);
    uint32_t rank = *reinterpret_cast<const uint32_t*>(rankPtr + 0x114);
    int32_t cur8 = *reinterpret_cast<const int32_t*>(deb + 0x8);
    if (rank < 4) {
        if (cur8 >= 0 || *reinterpret_cast<const int32_t*>(deb + 0x44) < 43) {
            *reinterpret_cast<uint32_t*>(ebpFrame - 0xe0) = 0xff000000u;
            return;
        }
        static const uint8_t kRankP[4] = {50, 20, 8, 2};
        float p = static_cast<float>(kRankP[rank]);
        float t = eff - 43.0f;
        if (t < 0.0f) t = 0.0f;
        float ph = std::fmod(t, p);
        if (ph < 0.0f) ph += p;
        float norm = (ph / p) * 2.0f;
        if (norm > 1.0f) norm = 2.0f - norm;
        uint32_t col = Game_ColorLerp(0, 0xffffff, ClampPct0_100(norm * 100.0f));
        StoreRgbaBytes(ebpFrame - 0xe0, col);
        return;
    }
    int32_t rbOff = *reinterpret_cast<const int32_t*>(ebpFrame - 0xf4);
    int32_t pct = exitFadePct;
    if (cur8 < 0) {
        ++rbOff;
        pct = ediVal;
    }
    uint32_t rb = CalcSmoothRainbow(eff + static_cast<float>(rbOff), 1.0f);
    uint32_t col = Game_ColorLerp(0, rb, pct);
    StoreRgbaBytes(ebpFrame - 0xe0, col);
}

static int32_t __cdecl DebBtnQuadAlpha_Impl(const uint8_t* deb, int32_t exitPct) {
    float eff = GetDebEff44(deb);
    float pct = (eff - 41.0f) * 20.0f;
    if (*reinterpret_cast<const int32_t*>(deb + 0x8) >= 0) {
        pct = static_cast<float>(exitPct);
    } else {
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;
    }
    return static_cast<int32_t>(std::lrint((pct * pct * 255.0f) / 10000.0f));
}

static uint32_t __cdecl DebriefFillExitRgba_Impl(const uint8_t* deb, uint8_t* ebpFrame) {
    int32_t alpha = CalcSubExitOverlayAlpha_Impl(deb);
    const uint8_t* bg = reinterpret_cast<const uint8_t*>(0x5f88e0);
    ebpFrame[-0xe0] = bg[0];
    ebpFrame[-0xdf] = bg[1];
    ebpFrame[-0xde] = bg[2];
    ebpFrame[-0xdd] = static_cast<uint8_t>(alpha);
    return reinterpret_cast<uint32_t>(GetScreenMgr());
}

// Debrief Stage 2..13 naked bridges
static void __declspec(naked) hk_debColor_4a9bdd() {
    __asm {
        push ebx
        push 1
        push 1
        push 0
        push 1
        push ebp
        push DWORD PTR [ebp - 0xec]
        call DebColorStage_Impl
        add esp, 28
        mov edx, 0x4a9c9f
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4a9cf5() {
    __asm {
        push ebx
        push 2
        push 1
        push 0
        push 2
        push ebp
        push DWORD PTR [ebp - 0xec]
        call DebColorStage_Impl
        add esp, 28
        mov edx, 0x4a9db0
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4a9e80() {
    __asm {
        cmp eax, 3
        jl dc_4a9e80_skip
        push ebx
        push 0
        push 1
        push 0x3b3b3b
        push 3
        push ebp
        push DWORD PTR [ebp - 0xec]
        call DebColorStage_Impl
        add esp, 28
        mov edx, 0x4a9f24
        jmp edx
    dc_4a9e80_skip:
        mov edx, 0x4a9ff7
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4a9ff7() {
    __asm {
        cmp DWORD PTR [esi + 0x44], 4
        jl dc_4a9ff7_skip
        push ebx
        push 0
        push 1
        push 0xa1a1a1
        push 4
        push ebp
        push DWORD PTR [ebp - 0xec]
        call DebColorStage_Impl
        add esp, 28
        mov ecx, DWORD PTR [ebp - 0xdc]
        mov DWORD PTR [ebp - 0xe0], ecx
        mov edx, 0x4aa0bc
        jmp edx
    dc_4a9ff7_skip:
        mov edx, 0x4aa227
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4aa227() {
    __asm {
        mov eax, DWORD PTR [ebp - 0xec]
        cmp DWORD PTR [eax + 0x44], 5
        jl dc_4aa227_skip
        push ebx
        push 0
        push 1
        push 0xa1a1a1
        push 5
        push ebp
        push eax
        call DebColorStage_Impl
        add esp, 28
        mov edx, 0x4aa2d6
        jmp edx
    dc_4aa227_skip:
        mov edx, 0x4aa6db
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4aa401() {
    __asm {
        push ebx
        push edi
        push 1
        push 0
        push 5
        push ebp
        push DWORD PTR [ebp - 0xec]
        call DebColorStage_Impl
        add esp, 28
        inc edi
        mov DWORD PTR [ebp - 0xf4], edi
        mov edx, 0x4aa5ab
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4aa50d() {
    __asm {
        push ebx
        push 0
        push 1
        push 0xa1a1a1
        push 5
        push ebp
        push DWORD PTR [ebp - 0xec]
        call DebColorStage_Impl
        add esp, 28
        mov edx, 0x4aa5ab
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4aa6db() {
    __asm {
        mov eax, DWORD PTR [ebp - 0xec]
        cmp DWORD PTR [eax + 0x44], 6
        jl dc_4aa6db_skip
        push ebx
        push edi
        push 1
        push 0
        push 6
        push ebp
        push eax
        call DebColorStage_Impl
        add esp, 28
        mov ecx, DWORD PTR [ebp - 0xdc]
        mov DWORD PTR [ebp - 0xf0], ecx
        mov edx, 0x4aa7eb
        jmp edx
    dc_4aa6db_skip:
        mov edx, 0x4aaa64
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4aa87f() {
    __asm {
        push ebx
        push 0
        push 1
        push 0x3b3b3b
        push 6
        push ebp
        push DWORD PTR [ebp - 0xec]
        call DebColorStage_Impl
        add esp, 28
        mov ecx, DWORD PTR [ebp - 0xdc]
        mov DWORD PTR [ebp - 0xf0], ecx
        mov edx, 0x4aa943
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4aadbf() {
    __asm {
        mov ecx, DWORD PTR [ebp - 0xec]
        cmp DWORD PTR [ecx + 0x44], 14
        jl dc_4aadbf_skip
        push ebx
        push edi
        push 1
        push 0
        push 14
        push ebp
        push ecx
        call DebColorStage_Impl
        add esp, 28
        mov ecx, DWORD PTR [ebp - 0xdc]
        mov DWORD PTR [ebp - 0xe0], ecx
        mov edx, 0x4aaebe
        jmp edx
    dc_4aadbf_skip:
        mov edx, 0x4ab00e
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4ab722() {
    __asm {
        cmp eax, 39
        jl dc_4ab722_skip
        push ebx
        push 0
        push 1
        push 0x3b3b3b
        push 39
        push ebp
        push DWORD PTR [ebp - 0xec]
        call DebColorStage_Impl
        add esp, 28
        mov ecx, DWORD PTR [ebp - 0xdc]
        mov DWORD PTR [ebp - 0xe0], ecx
        mov edx, 0x4ab7c5
        jmp edx
    dc_4ab722_skip:
        mov edx, 0x4ab93a
        jmp edx
    }
}
static void __declspec(naked) hk_debColor_4ab93f() {
    __asm {
        mov edx, DWORD PTR [ebp - 0xec]
        cmp DWORD PTR [edx + 0x44], 40
        jl dc_4ab93f_skip
        push ebx
        push 0
        push 1
        push 0x3b3b3b
        push 40
        push ebp
        push edx
        call DebColorStage_Impl
        add esp, 28
        mov ecx, DWORD PTR [ebp - 0xdc]
        mov DWORD PTR [ebp - 0xe0], ecx
        mov edx, 0x4ab9ed
        jmp edx
    dc_4ab93f_skip:
        mov edx, 0x4abc65
        jmp edx
    }
}

static void __declspec(naked) hk_debRankSlideX() {
    __asm {
        cmp eax, 25
        jl drs_skip
        push DWORD PTR [ebp - 0xec]
        call DebRankSlideX_Impl
        add esp, 4
        mov esi, eax
        push ebx
        push 0x3b3b3b
        push 0xffffff
        mov edx, 0x4aac1b
        jmp edx
    drs_skip:
        mov edx, 0x4aadad
        jmp edx
    }
}
static void __declspec(naked) hk_debBoxHeight() {
    __asm {
        cmp DWORD PTR [eax + 0x44], 34
        jl dbh_skip
        push eax
        call DebBoxHeight_Impl
        add esp, 4
        mov esi, eax
        mov edx, 0x4ab04a
        jmp edx
    dbh_skip:
        mov edx, 0x4ab5a1
        jmp edx
    }
}
static void __declspec(naked) hk_debBarWidth() {
    __asm {
        push DWORD PTR [ebp - 0xec]
        call DebBarWidth_Impl
        add esp, 4
        mov edx, 0x4ab152
        jmp edx
    }
}
static void __declspec(naked) hk_debRankStamp() {
    __asm {
        sub esp, 12
        lea eax, [esp + 8]
        push eax
        lea eax, [esp + 8]
        push eax
        lea eax, [esp + 8]
        push eax
        push DWORD PTR [ebp - 0xec]
        call DebRankStamp_Impl
        add esp, 16
        pop esi
        pop ecx
        pop edi
        mov DWORD PTR [ebp - 0xdc], ecx
        mov eax, 0x4ab201
        jmp eax
    }
}
static void __declspec(naked) hk_debRankOverlay() {
    __asm {
        push edi
        push ebx
        push ebp
        push DWORD PTR [ebp - 0xec]
        call DebRankOverlay_Impl
        add esp, 16
        mov edx, DWORD PTR [ebp - 0xec]
        cmp DWORD PTR [edx + 0x8], 0
        jge dro_done
        mov ebx, edi
    dro_done:
        mov eax, 0x4ab480
        jmp eax
    }
}
static void __declspec(naked) hk_debBtnQuadAlpha() {
    __asm {
        push ebx
        push DWORD PTR [ebp - 0xec]
        call DebBtnQuadAlpha_Impl
        add esp, 8
        mov ebx, eax
        xor edi, edi
        xor esi, esi
        mov eax, 0x4abcd1
        jmp eax
    }
}
static void __declspec(naked) hk_debBtnRainbow() {
    __asm {
        mov edx, DWORD PTR [ebp - 0x104]
        lea eax, [esi + edx + 3]
        push eax
        call GetSmoothSlotRainbow
        add esp, 4
        mov edx, eax
        mov eax, 0x4abd10
        jmp eax
    }
}
static void __declspec(naked) hk_debriefFillExitRgba() {
    __asm {
        push ebp
        push DWORD PTR [ebp - 0xec]
        call DebriefFillExitRgba_Impl
        add esp, 8
        mov edx, 0x4abea9
        jmp edx
    }
}

// ------------------------------------------------------------------
// 3. WorldScreen HUD Edge Time & Instructions Demo
// ------------------------------------------------------------------
static void __cdecl HudTimerSetup_Impl(const uint8_t* ws, int32_t slot, int32_t xInt, int32_t yInt, float aspect, uint8_t* ebpFrame) {
    int32_t curT = *reinterpret_cast<const int32_t*>(ws + slot * 4 + 0x78);
    *reinterpret_cast<int32_t*>(ebpFrame - 0xc) = 5;
    float effT = static_cast<float>(curT);
    if (g_extWsPtr == ws && g_wsPrevEc[slot] != 0) {
        int32_t prevT = g_wsPrevEt[slot];
        int32_t d = curT - prevT;
        if (d >= 0 && d <= 2) {
            effT = static_cast<float>(prevT) + static_cast<float>(d) * g_uiAlpha;
        }
    }
    g_etTicksTmp = effT;

    float bounceF = 5.0f;
    if (curT > 10) {
        float rem = std::fmod(effT + 0.0001f, 22.0f);
        if (rem < 0.0f) rem = 0.0f;
        if (rem < 1.0f) bounceF = 8.0f;
        else if (rem < 2.0f) bounceF = 8.0f - (rem - 1.0f);
        else if (rem < 3.0f) bounceF = 7.0f - (rem - 2.0f) * 2.0f;
    }

    float xF = static_cast<float>(xInt);
    float yF = static_cast<float>(yInt);
    if (g_extWsPtr == ws) {
        float invA = 1.0f - g_uiAlpha;
        if (g_wsPrevEc[slot] != 0) {
            xF -= static_cast<float>(*reinterpret_cast<const int32_t*>(ws + slot * 8 + 0x2148) - g_wsPrevX[slot]) * invA;
            yF -= static_cast<float>(*reinterpret_cast<const int32_t*>(ws + slot * 8 + 0x214c) - g_wsPrevY[slot]) * invA;
        } else {
            yF += static_cast<float>(*reinterpret_cast<const int32_t*>(ws + slot * 4 + 0x80)) * invA;
        }
    }
    yF = -yF;

    float s = bounceF / 5.0f;
    float* mat = reinterpret_cast<float*>(ebpFrame - 0x5c);
    std::memset(mat, 0, 64);
    mat[0] = 1.0f; mat[5] = 1.0f; mat[10] = 1.0f; mat[15] = 1.0f;
    mat[12] = xF; mat[13] = yF;
    std::memcpy(g_hudMatOrig, mat, 64);

    float adj = (s - 1.0f) * aspect;
    mat[0] = s;
    mat[5] = s;
    mat[12] = xF + 2.0f * adj;
    mat[13] = yF + 20.0f * adj;

    void* mstack = GetMatrixStack();
    *reinterpret_cast<void**>(ebpFrame - 0x18) = mstack;
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f750)(mstack);
    reinterpret_cast<void(__thiscall*)(void*, void*)>(0x41f520)(mstack, mat);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);
}

static void __declspec(naked) hk_hudTimerSetup() {
    __asm {
        push ebp
        push DWORD PTR [ebp - 0x4]
        push ebx
        push DWORD PTR [ebp - 0x8]
        push DWORD PTR [ebp + 0x8]
        push esi
        call HudTimerSetup_Impl
        add esp, 24
        ret
    }
}

static void __cdecl HudTimerDigitsAndColor_Impl(
    const uint8_t* ws, int32_t slot, int32_t* outSec, int32_t* outCs, uint32_t* outCol
) {
    float t = g_etTicksTmp + 0.0001f;
    int32_t sec = static_cast<int32_t>(t / 22.0f);
    *outSec = sec;
    float rem = t - static_cast<float>(sec) * 22.0f;
    if (rem < 0.0f) rem = 0.0f;
    g_etRemTmp = rem;
    int32_t cs = static_cast<int32_t>((rem * 100.0f) / 22.0f);
    if (cs > 99) cs = 99;
    *outCs = cs;

    uint32_t col = CalcSmoothRainbow(GetVisualWsFrame(ws), 1.0f);
    if (slot == 1 && ws[0x48] != 0) {
        float pulse = 0.5f - 0.5f * std::cos(GetVisualWsFrame(ws) * 0.25f);
        col = Game_ColorLerp(0x999999u, 0xaaaaaau, ClampPct0_100(pulse * 100.0f));
    }
    if (*reinterpret_cast<const int32_t*>(ws + slot * 4 + 0x78) != 0x55da && rem < 1.0f) {
        col = Game_ColorLerp(0xffffff, col, ClampPct0_100(rem * 100.0f));
    }
    *outCol = col;
}

} // namespace

namespace edgemod {

void __cdecl GetEdgeTimerSmoothState(
    const uint8_t* ws, int32_t slot, float* outEffTicks, float* outEffRise, uint32_t* outRgb
) {
    if (ws == nullptr || slot < 0 || slot > 1) {
        if (outEffTicks) *outEffTicks = 0.0f;
        if (outEffRise)  *outEffRise  = 0.0f;
        if (outRgb)      *outRgb      = 0xffffffu;
        return;
    }

    int32_t curT = *reinterpret_cast<const int32_t*>(ws + slot * 4 + 0x78);
    float effT = static_cast<float>(curT);
    if (g_extWsPtr == ws && g_wsPrevEc[slot] != 0) {
        int32_t prevT = g_wsPrevEt[slot];
        int32_t d = curT - prevT;
        if (d >= 0 && d <= 2) {
            effT = static_cast<float>(prevT) + static_cast<float>(d) * g_uiAlpha;
        }
    }

    int32_t curRise = *reinterpret_cast<const int32_t*>(ws + slot * 4 + 0x80);
    float effRise = static_cast<float>(curRise);
    if (g_extWsPtr == ws) {
        int32_t prevRise = (g_wsPrevEc[slot] != 0) ? g_wsPrevEr[slot] : 0;
        int32_t dRise = curRise - prevRise;
        if (dRise >= -2 && dRise <= 2) {
            effRise = static_cast<float>(prevRise) + static_cast<float>(dRise) * g_uiAlpha;
        }
    }

    float t = effT + 0.0001f;
    int32_t sec = static_cast<int32_t>(t / 22.0f);
    float rem = t - static_cast<float>(sec) * 22.0f;
    if (rem < 0.0f) rem = 0.0f;

    uint32_t col = CalcSmoothRainbow(GetVisualWsFrame(ws), 1.0f);
    if (slot == 1 && ws[0x48] != 0) {
        float pulse = 0.5f - 0.5f * std::cos(GetVisualWsFrame(ws) * 0.25f);
        col = Game_ColorLerp(0x999999u, 0xaaaaaau, ClampPct0_100(pulse * 100.0f));
    }
    if (curT != 0x55da && rem < 1.0f) {
        col = Game_ColorLerp(0xffffffu, col, ClampPct0_100(rem * 100.0f));
    }

    if (outEffTicks) *outEffTicks = effT;
    if (outEffRise)  *outEffRise  = effRise;
    if (outRgb)      *outRgb      = col;
}

} // namespace edgemod

namespace {

static void __declspec(naked) hk_hudTimerDigitsAndColor() {
    __asm {
        sub esp, 8
        lea eax, [esp + 4]
        push eax
        lea eax, [esp + 4]
        push eax
        lea eax, [ebp - 0x8]
        push eax
        push DWORD PTR [ebp + 0x8]
        push esi
        call HudTimerDigitsAndColor_Impl
        add esp, 20
        pop ebx
        pop edi
        ret
    }
}

static void __cdecl HudRestoreMat_Impl(uint8_t* ws, float aspect, uint32_t col, int32_t* outIntAsp) {
    void* mstack = GetMatrixStack();
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f610)(mstack);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f750)(mstack);
    reinterpret_cast<void(__thiscall*)(void*, void*)>(0x41f520)(mstack, g_hudMatOrig);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);
    SetSepGlyph();

    int32_t iAsp = static_cast<int32_t>(aspect);
    *outIntAsp = iAsp;
    auto drawDigit = reinterpret_cast<void(__thiscall*)(void*, int32_t, int32_t, int32_t, int32_t, uint32_t, int32_t, int32_t)>(0x479f00);
    if (g_timeFmt != 0) {
        int32_t yOff = static_cast<int32_t>(aspect * 4.0f);
        drawDigit(ws, 0xa, 0, iAsp, yOff, col, 0xff, 0xaa);
    } else {
        double mul = *reinterpret_cast<const double*>(0x579c48);
        int32_t xOff = static_cast<int32_t>(static_cast<double>(aspect) * mul);
        drawDigit(ws, 0xa, xOff, 0, iAsp, col, 0xff, 0xaa);
    }
}

static void __declspec(naked) hk_hudRestoreMat() {
    __asm {
        push ecx
        lea eax, [ebp + 0x8]
        push eax
        push edi
        push DWORD PTR [ebp - 0x4]
        push esi
        call HudRestoreMat_Impl
        add esp, 16
        pop ecx
        movss xmm0, DWORD PTR [ebp - 0x4]
        ret
    }
}

static void __declspec(naked) hk_demoSepPush() {
    __asm {
        push eax
        lea ecx, [esi + 0xa]
        mov eax, 1
        cmp BYTE PTR g_timeFmt, 0
        je dsp_go
        lea ecx, [esi + 0xb]
        mov eax, 4
    dsp_go:
        xchg DWORD PTR [esp + 4], eax
        xchg DWORD PTR [esp], eax
        ret
    }
}

static void __cdecl EdgeDemoSetup_Impl(
    int32_t tick, int32_t* pFrameCol, int32_t* outSec, int32_t* outCs, int32_t* outRemInt
) {
    SetSepGlyph();
    float tF = static_cast<float>(tick);
    if (g_extSubPtr != nullptr && *reinterpret_cast<uint32_t*>(g_extSubPtr) == 0x58d2fc) {
        tF = g_extEffB;
    }
    tF += 0.0001f;
    int32_t sec = static_cast<int32_t>(tF / 22.0f);
    float rem = tF - static_cast<float>(sec) * 22.0f;
    if (rem < 0.0f) rem = 0.0f;
    *outSec = sec;
    *outRemInt = static_cast<int32_t>(rem);
    int32_t cs = static_cast<int32_t>((rem * 100.0f) / 22.0f);
    if (cs > 99) cs = 99;
    *outCs = cs;

    uint32_t col = GetSmoothMenuRainbow(*pFrameCol);
    if (tick != 0x55da && rem < 1.0f) {
        col = Game_ColorLerp(0xffffff, col, ClampPct0_100(rem * 100.0f));
    }
    *pFrameCol = static_cast<int32_t>(col);
}

static void __declspec(naked) hk_edgeDemoSetup() {
    __asm {
        push edi
        mov ebx, ecx
        sub esp, 8
        lea eax, [esp + 4] // remInt
        push eax
        lea eax, [ebp - 0x8]
        push eax           // cs
        lea eax, [esp + 8] // sec
        push eax
        lea eax, [ebp + 0x14]
        push eax
        push DWORD PTR [ebp + 0x10]
        call EdgeDemoSetup_Impl
        add esp, 20
        pop edi
        pop esi
        mov eax, 0x47a341
        jmp eax
    }
}

static inline uint32_t MakeRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r)
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

static inline uint32_t RgbToRgba(uint32_t rgb, uint8_t a) {
    return ((rgb & 0xff0000u) >> 16)
         | (rgb & 0x00ff00u)
         | ((rgb & 0x0000ffu) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

static double GetHudTimerClockSec() {
    static LARGE_INTEGER s_freq{};
    static LARGE_INTEGER s_base{};
    if (s_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&s_freq);
        QueryPerformanceCounter(&s_base);
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return (s_freq.QuadPart > 0)
        ? (static_cast<double>(now.QuadPart - s_base.QuadPart) / static_cast<double>(s_freq.QuadPart))
        : 0.0;
}

static void FormatTicksWstr(double ticks, wchar_t* wbuf, size_t cap, EdgeWString* out) {
    if (ticks < 0.0) ticks = 0.0;
    int64_t totalSec = static_cast<int64_t>(std::floor(ticks / 22.0 + 1e-6));
    double rem = ticks - static_cast<double>(totalSec) * 22.0;
    if (rem < 0.0) rem = 0.0;
    int cs = static_cast<int>(std::floor((rem * 100.0) / 22.0 + 1e-6));
    if (cs < 0) cs = 0;
    if (cs > 99) cs = 99;
    int64_t mins = totalSec / 60;
    int64_t secs = totalSec % 60;
    const wchar_t* fmt = (g_timeFmt != 0) ? L"%lld:%02lld.%02d" : L"%lld'%02lld\"%02d";
    int n = std::swprintf(wbuf, cap, fmt, mins, secs, cs);
    if (n < 0) {
        wbuf[0] = L'\0';
        n = 0;
    }
    std::memset(out, 0, sizeof(*out));
    out->ptr = wbuf;
    out->size = static_cast<uint32_t>(n);
    out->cap = static_cast<uint32_t>(cap - 1);
}

struct HudCornerStack {
    bool  hasAny[4]  = {};
    float leftX[4]   = {};
    float rightX[4]  = {};
    float topY[4]    = {};
    float bottomY[4] = {};
};

struct SpeedoSample {
    double dt;
    float  dist;
};

struct SpeedometerTracker {
    const uint8_t* lastCube = nullptr;
    bool         hasPos = false;
    float        lastX = 0.0f;
    float        lastZ = 0.0f;
    float        displaySpeed = 0.0f;
    bool         rollsInit = false;
    float        smoothRollsLeft = 0.0f;
    SpeedoSample ring[256]{};
    uint32_t     head = 0;
    uint32_t     count = 0;
    double       totalDt = 0.0;
    float        totalDist = 0.0f;

    void ResetWindow() {
        head = 0;
        count = 0;
        totalDt = 0.0;
        totalDist = 0.0f;
        displaySpeed = 0.0f;
    }

    void ResetAll() {
        lastCube = nullptr;
        hasPos = false;
        lastX = 0.0f;
        lastZ = 0.0f;
        rollsInit = false;
        smoothRollsLeft = 0.0f;
        ResetWindow();
    }
};

static SpeedometerTracker g_speedTracker{};

static void GetPlayerCubeWorldXZ(const uint8_t* cube, float* outX, float* outZ) {
    if (cube == nullptr) {
        *outX = 0.0f;
        *outZ = 0.0f;
        return;
    }
    uint8_t* model = *reinterpret_cast<uint8_t* const*>(cube + 0x1fc);
    if (model == nullptr) {
        model = *reinterpret_cast<uint8_t* const*>(cube + 0x1f4);
    }
    if (model != nullptr) {
        float pos[3]{};
        reinterpret_cast<float*(__thiscall*)(uint8_t*, float*)>(0x421400)(model, pos);
        if (std::isfinite(pos[0]) && std::isfinite(pos[2]) &&
            std::fabs(pos[0]) < 1.0e6f && std::fabs(pos[2]) < 1.0e6f) {
            *outX = pos[0];
            *outZ = pos[2];
            return;
        }
    }
    float scaleDiv = 1.0f;
    if (*reinterpret_cast<const int32_t*>(cube + 0x138) != 1) {
        scaleDiv = static_cast<float>(*reinterpret_cast<const int32_t*>(cube + 0x134)) * 0.000244140625f;
        if (scaleDiv <= 0.1f) scaleDiv = 1.0f;
    }
    float halfSz = 15.0f / scaleDiv;
    *outX = static_cast<float>(*reinterpret_cast<const int32_t*>(cube + 0x4c)) + halfSz;
    *outZ = static_cast<float>(*reinterpret_cast<const int32_t*>(cube + 0x50)) + halfSz;
}

static void UpdateSpeedometerTracker(uint8_t* renderer, uint8_t* ws) {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr || *reinterpret_cast<const uint32_t*>(sm + 0x50) != 0x11u) {
        return;
    }
    if (ws == nullptr || ws[0x2120] == 0) {
        g_speedTracker.ResetAll();
        return;
    }
    uint8_t* cube = *reinterpret_cast<uint8_t* const*>(ws + 0x34);
    if (cube == nullptr) {
        g_speedTracker.ResetAll();
        return;
    }
    if (cube != g_speedTracker.lastCube) {
        g_speedTracker.ResetAll();
        g_speedTracker.lastCube = cube;
    }
    if (cube[0x171] != 0) {
        g_speedTracker.hasPos = false;
        g_speedTracker.ResetWindow();
        return;
    }

    float dt = (renderer != nullptr) ? *reinterpret_cast<const float*>(renderer + 0x08) : 0.0f;
    if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;

    float curX = 0.0f;
    float curZ = 0.0f;
    GetPlayerCubeWorldXZ(cube, &curX, &curZ);

    if (!g_speedTracker.hasPos) {
        g_speedTracker.hasPos = true;
        g_speedTracker.lastX = curX;
        g_speedTracker.lastZ = curZ;
    } else if (dt > 0.0f) {
        float dx = curX - g_speedTracker.lastX;
        float dz = curZ - g_speedTracker.lastZ;
        float stepDist = std::hypot(dx, dz);
        g_speedTracker.lastX = curX;
        g_speedTracker.lastZ = curZ;

        const float maxStepDist = (g_turboMode != 0) ? 90.0f : 45.0f;
        if (!std::isfinite(stepDist) || stepDist > maxStepDist) {
            g_speedTracker.ResetWindow();
        } else {
            constexpr double kWindowSec = 0.20;
            constexpr float kMaxNormalUnitsPerSec = 150.0f;

            if (g_speedTracker.count >= 256u) {
                uint32_t tail = (g_speedTracker.head + 256u - g_speedTracker.count) & 255u;
                g_speedTracker.totalDt -= g_speedTracker.ring[tail].dt;
                g_speedTracker.totalDist -= g_speedTracker.ring[tail].dist;
                --g_speedTracker.count;
            }
            g_speedTracker.ring[g_speedTracker.head] = {static_cast<double>(dt), stepDist};
            g_speedTracker.head = (g_speedTracker.head + 1u) & 255u;
            ++g_speedTracker.count;
            g_speedTracker.totalDt += static_cast<double>(dt);
            g_speedTracker.totalDist += stepDist;

            while (g_speedTracker.count > 1u) {
                uint32_t tail = (g_speedTracker.head + 256u - g_speedTracker.count) & 255u;
                if (g_speedTracker.totalDt - g_speedTracker.ring[tail].dt < kWindowSec) break;
                g_speedTracker.totalDt -= g_speedTracker.ring[tail].dt;
                g_speedTracker.totalDist -= g_speedTracker.ring[tail].dist;
                --g_speedTracker.count;
            }
            if (g_speedTracker.totalDt < 0.0) g_speedTracker.totalDt = 0.0;
            if (g_speedTracker.totalDist < 0.0f) g_speedTracker.totalDist = 0.0f;

            double denom = (std::max)(g_speedTracker.totalDt, kWindowSec);
            float targetSpeed = (g_speedTracker.totalDist / static_cast<float>(denom)) * (100.0f / kMaxNormalUnitsPerSec);
            float alpha = 1.0f - std::exp(-dt / 0.05f);
            g_speedTracker.displaySpeed += (targetSpeed - g_speedTracker.displaySpeed) * alpha;
            if (g_speedTracker.displaySpeed < 0.25f && targetSpeed < 0.25f) {
                g_speedTracker.displaySpeed = 0.0f;
            }
        }
    }

    int32_t rollsUsed = *reinterpret_cast<const int32_t*>(cube + 0x160);
    float targetRollsLeft = static_cast<float>(std::clamp<int32_t>(22 - rollsUsed, 0, 22));
    if (!g_speedTracker.rollsInit || targetRollsLeft > g_speedTracker.smoothRollsLeft) {
        g_speedTracker.smoothRollsLeft = targetRollsLeft;
        g_speedTracker.rollsInit = true;
    } else if (dt > 0.0f) {
        float rAlpha = 1.0f - std::exp(-dt / 0.06f);
        g_speedTracker.smoothRollsLeft += (targetRollsLeft - g_speedTracker.smoothRollsLeft) * rAlpha;
        if (std::fabs(targetRollsLeft - g_speedTracker.smoothRollsLeft) < 0.01f) {
            g_speedTracker.smoothRollsLeft = targetRollsLeft;
        }
    }
}

static void DrawSmoothRainbowSpriteAtFrac(
    uint8_t** sprites, uint32_t ucnt, double frameF, int w, int h, int x, int y, int alpha
) {
    if (sprites == nullptr || ucnt == 0u || w <= 0 || h <= 0 || alpha <= 0) return;
    int a = std::clamp(alpha, 0, 255);
    double fl = std::floor(frameF);
    double tD = frameF - fl;
    float t = std::clamp(static_cast<float>(tD), 0.0f, 1.0f);

    int64_t baseIdx = static_cast<int64_t>(fl);
    uint32_t cnt32 = ucnt;
    uint32_t idx0 = static_cast<uint32_t>((baseIdx % static_cast<int64_t>(cnt32) + static_cast<int64_t>(cnt32)) % static_cast<int64_t>(cnt32));
    uint32_t idx1 = (idx0 + 1u) % cnt32;

    using DrawSpriteAlphaFn = void(__thiscall*)(void*, int, int, int, int, int, int, int);
    auto drawSpriteAlpha = reinterpret_cast<DrawSpriteAlphaFn>(0x4a7e30);

    float bigA = static_cast<float>(a) / 255.0f;
    float a2f = t * bigA;
    int a2 = std::clamp(static_cast<int>(std::lrint(a2f * 255.0f)), 0, 255);
    if (a2 <= 0) {
        drawSpriteAlpha(sprites[idx0], 0, 0, w, h, x, y, a);
        return;
    }
    if (t >= 0.999f) {
        drawSpriteAlpha(sprites[idx1], 0, 0, w, h, x, y, a);
        return;
    }
    float denom = 1.0f - a2f;
    float a1f = (denom > 1e-4f) ? ((1.0f - t) * bigA / denom) : 1.0f;
    int a1 = std::clamp(static_cast<int>(std::lrint(a1f * 255.0f)), 0, 255);
    if (a1 > 0) {
        drawSpriteAlpha(sprites[idx0], 0, 0, w, h, x, y, a1);
    }
    drawSpriteAlpha(sprites[idx1], 0, 0, w, h, x, y, a2);
}

static inline uint32_t ScaleRgb(uint32_t rgb, float factor) {
    int r = std::clamp(static_cast<int>(std::lround(static_cast<float>((rgb >> 16) & 0xffu) * factor)), 0, 255);
    int g = std::clamp(static_cast<int>(std::lround(static_cast<float>((rgb >> 8) & 0xffu) * factor)), 0, 255);
    int b = std::clamp(static_cast<int>(std::lround(static_cast<float>(rgb & 0xffu) * factor)), 0, 255);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

static void FormatSpeedIntWstr(int32_t val, wchar_t* wbuf, size_t cap, EdgeWString* out) {
    if (val < 0) val = 0;
    int n = std::swprintf(wbuf, cap, L"%d", val);
    if (n < 0) {
        wbuf[0] = L'\0';
        n = 0;
    }
    std::memset(out, 0, sizeof(*out));
    out->ptr = wbuf;
    out->size = static_cast<uint32_t>(n);
    out->cap = static_cast<uint32_t>(cap - 1);
}

struct MinimapHudBox {
    bool  active;
    float vis;
    float x;
    float y;
    float w;
    float h;
};

static MinimapHudBox ComputeMinimapHudBox(
    uint8_t* sm, uint8_t* ws, int32_t virtW, int32_t virtH, uint32_t expectedState
) {
    MinimapHudBox box{false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    if (sm == nullptr) return box;

    EffectiveMinimapMetrics eff{};
    GetEffectiveMinimapMetrics(&eff);
    int32_t mmW = static_cast<int32_t>(std::lround(eff.width));
    int32_t mmH = static_cast<int32_t>(std::lround(eff.height));
    if (mmW <= 0 || mmH <= 0) return box;

    if (expectedState == 9u || expectedState == 0x12u) {
        uint8_t previewPos = 0;
        bool mmSample = false;
        bool mmDockBeside = false;
        if (!GetMinimapMenuPreview(expectedState, &previewPos, &mmSample, &mmDockBeside) || mmDockBeside) {
            return box;
        }
        if (!mmSample && ws != nullptr && ws[0x2120] != 0) {
            double camAngle = reinterpret_cast<double(__thiscall*)(void*)>(0x478950)(sm + 0xa0);
            double diffDeg = *reinterpret_cast<const double*>(0x586ae0) - camAngle;
            int rot = static_cast<int>(diffDeg) / 90;
            rot &= 0x80000003;
            if (rot < 0) rot = ((rot - 1) | ~3) + 1;
            if (rot == 1 || rot == 3) {
                std::swap(mmW, mmH);
            }
        }
        bool useCorner = (g_minimapPosOverride != 0) || IsOptionsPopupOpen(expectedState == 0x12u);
        int32_t alignX = useCorner ? ((previewPos == 1u || previewPos == 3u) ? 2 : 0) : eff.alignX;
        int32_t alignY = useCorner ? ((previewPos == 2u || previewPos == 3u) ? 2 : 0) : eff.alignY;
        int32_t offX   = useCorner ? 0 : eff.offX;
        int32_t offY   = useCorner ? 0 : eff.offY;

        int32_t mmX = 0;
        if (alignX == 1) mmX = (virtW - mmW) / 2;
        else if (alignX == 2) mmX = virtW - mmW;
        mmX = std::clamp(mmX + offX, 0, (std::max)(0, virtW - mmW));

        int32_t mmY = 0;
        if (alignY == 1) mmY = (virtH - mmH) / 2;
        else if (alignY == 2) mmY = virtH - mmH;
        mmY = std::clamp(mmY + offY, 0, (std::max)(0, virtH - mmH));

        box.active = true;
        box.vis = 1.0f;
        box.x = static_cast<float>(mmX);
        box.y = static_cast<float>(mmY);
        box.w = static_cast<float>(mmW);
        box.h = static_cast<float>(mmH);
        return box;
    }

    if (!eff.enabled || ws == nullptr || ws[0x2120] == 0) return box;

    int32_t curAlpha = *reinterpret_cast<const int32_t*>(ws + 0x2108);
    float effAlpha = static_cast<float>(curAlpha);
    if (g_wsPtr == ws && g_wsTgt2108 == curAlpha && g_wsTgt2108 != g_wsPrev2108) {
        effAlpha = static_cast<float>(g_wsPrev2108)
                 + static_cast<float>(g_wsTgt2108 - g_wsPrev2108) * g_uiAlpha;
    }
    if (effAlpha <= 0.5f) return box;

    int32_t mmMaxAlpha = *reinterpret_cast<const int32_t*>(ws + 0x210c);
    if (mmMaxAlpha <= 0 || mmMaxAlpha > 255) mmMaxAlpha = 255;
    float vis = std::clamp(effAlpha / static_cast<float>(mmMaxAlpha), 0.0f, 1.0f);
    if (vis <= 0.0f) return box;

    double camAngle = reinterpret_cast<double(__thiscall*)(void*)>(0x478950)(sm + 0xa0);
    double diffDeg = *reinterpret_cast<const double*>(0x586ae0) - camAngle;
    int rot = static_cast<int>(diffDeg) / 90;
    rot &= 0x80000003;
    if (rot < 0) rot = ((rot - 1) | ~3) + 1;
    if (rot == 1 || rot == 3) {
        std::swap(mmW, mmH);
    }

    int32_t alignX = eff.alignX;
    int32_t mmX = 0;
    if (alignX == 1) mmX = (virtW - mmW) / 2;
    else if (alignX == 2) mmX = virtW - mmW;
    mmX += eff.offX;
    mmX = std::clamp(mmX, 0, (std::max)(0, virtW - mmW));

    int32_t alignY = eff.alignY;
    int32_t mmY = 0;
    if (alignY == 1) mmY = (virtH - mmH) / 2;
    else if (alignY == 2) mmY = virtH - mmH;
    mmY += eff.offY;
    mmY = std::clamp(mmY, 0, (std::max)(0, virtH - mmH));

    box.active = true;
    box.vis = vis;
    box.x = static_cast<float>(mmX);
    box.y = static_cast<float>(mmY);
    box.w = static_cast<float>(mmW);
    box.h = static_cast<float>(mmH);
    return box;
}

static void AvoidMinimapOverlap(
    const MinimapHudBox& mm, uint8_t pos, float w, float h,
    int32_t virtW, int32_t virtH, float* ioX, float* ioY
) {
    if (!mm.active || mm.vis <= 0.0f || ioX == nullptr || ioY == nullptr) return;
    constexpr float kGap = 4.0f;
    constexpr float kMargin = 0.0f;

    float x = *ioX;
    float y = *ioY;
    bool overlap = (x < mm.x + mm.w + kGap) && (x + w > mm.x - kGap)
                && (y < mm.y + mm.h + kGap) && (y + h > mm.y - kGap);
    if (!overlap) return;

    float targetX = x;
    float targetY = y;
    float candY = (pos == 0 || pos == 1) ? (mm.y + mm.h + kGap) : (mm.y - kGap - h);
    float candX = (pos == 0 || pos == 2) ? (mm.x + mm.w + kGap) : (mm.x - kGap - w);

    if (candY >= kMargin && candY + h <= static_cast<float>(virtH) - kMargin) {
        targetY = candY;
    } else if (candX >= kMargin && candX + w <= static_cast<float>(virtW) - kMargin) {
        targetX = candX;
    } else {
        targetY = std::clamp(candY, 0.0f, (std::max)(0.0f, static_cast<float>(virtH) - h));
    }

    float ease = 1.0f - (1.0f - mm.vis) * (1.0f - mm.vis);
    *ioX = x + (targetX - x) * ease;
    *ioY = y + (targetY - y) * ease;
}

static void PlaceCornerHudBox(
    HudCornerStack* stack, const MinimapHudBox& mm, uint8_t pos,
    float scaledW, float scaledH, int32_t virtW, int32_t virtH,
    float* outX, float* outY
) {
    constexpr float kMargin = 0.0f;
    uint8_t p = (pos <= 3u) ? pos : 0u;
    float ox = (p == 1u || p == 3u) ? (static_cast<float>(virtW) - kMargin - scaledW) : kMargin;
    float oy = (p == 2u || p == 3u) ? (static_cast<float>(virtH) - kMargin - scaledH) : kMargin;
    if (stack != nullptr && stack->hasAny[p]) {
        ox = (p == 1u || p == 3u) ? (stack->rightX[p] - scaledW) : stack->leftX[p];
        oy = (p == 0u || p == 1u) ? (stack->bottomY[p] + 4.0f) : (stack->topY[p] - 4.0f - scaledH);
    }
    AvoidMinimapOverlap(mm, p, scaledW, scaledH, virtW, virtH, &ox, &oy);
    float maxX = (std::max)(0.0f, static_cast<float>(virtW) - scaledW);
    float maxY = (std::max)(0.0f, static_cast<float>(virtH) - scaledH);
    ox = std::clamp(ox, 0.0f, maxX);
    oy = std::clamp(oy, 0.0f, maxY);
    *outX = ox;
    *outY = oy;
    if (stack != nullptr) {
        if (!stack->hasAny[p]) {
            stack->hasAny[p] = true;
            stack->leftX[p] = ox;
            stack->rightX[p] = ox + scaledW;
            stack->topY[p] = oy;
            stack->bottomY[p] = oy + scaledH;
        } else {
            stack->leftX[p] = (std::min)(stack->leftX[p], ox);
            stack->rightX[p] = (std::max)(stack->rightX[p], ox + scaledW);
            stack->topY[p] = (std::min)(stack->topY[p], oy);
            stack->bottomY[p] = (std::max)(stack->bottomY[p], oy + scaledH);
        }
    }
}

static void DrawHudPanelBgAndBorder(
    int panelW, int panelH, int32_t opacityPct, uint8_t fgAlpha,
    bool drawTop, bool drawBottom, bool drawLeft, bool drawRight
) {
    double bgScale = static_cast<double>(std::clamp(opacityPct, 0, 100)) / 100.0;
    double fgScale = static_cast<double>(fgAlpha) / 255.0;
    uint8_t bgAlpha  = static_cast<uint8_t>(std::lround(255.0 * bgScale * fgScale));
    uint8_t brdAlpha = static_cast<uint8_t>(std::lround(64.0 * bgScale * fgScale));
    if (bgAlpha > 0) {
        uint32_t bgRgba = MakeRgba(0x21, 0x21, 0x21, bgAlpha);
        Game_DrawQuad(0, 0, panelW, panelH, &bgRgba);
    }
    if (brdAlpha > 0) {
        uint32_t borderRgba = MakeRgba(255, 255, 255, brdAlpha);
        if (drawTop)    Game_DrawQuad(0, 0, panelW, 1, &borderRgba);
        if (drawBottom) Game_DrawQuad(0, panelH - 1, panelW, 1, &borderRgba);
        int sideY = drawTop ? 1 : 0;
        int sideH = panelH - (drawTop ? 1 : 0) - (drawBottom ? 1 : 0);
        if (sideH > 0) {
            if (drawLeft)  Game_DrawQuad(0, sideY, 1, sideH, &borderRgba);
            if (drawRight) Game_DrawQuad(panelW - 1, sideY, 1, sideH, &borderRgba);
        }
    }
}

static uint8_t CalcHudShadowAlpha(int32_t shadowOpacityPct, uint8_t fgAlpha) {
    double shScale = static_cast<double>(std::clamp(shadowOpacityPct, 0, 100)) / 100.0;
    double fgScale = static_cast<double>(fgAlpha) / 255.0;
    return static_cast<uint8_t>(std::lround(255.0 * shScale * fgScale));
}

constexpr int kRankD  = 0;
constexpr int kRankC  = 1;
constexpr int kRankB  = 2;
constexpr int kRankA  = 3;
constexpr int kRankS  = 4;
constexpr int kRankSP = 5;
constexpr int kRankPB = 6;

static uint32_t GetCurrentLevelPbTicks() {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return 0u;

    int32_t levelType = *reinterpret_cast<const int32_t*>(sm + 0x1e0);
    int32_t levelIdx  = *reinterpret_cast<const int32_t*>(sm + 0x1c0);
    if (levelType < 0 || levelType > 2 || levelIdx < 0) return 0u;

    void* levelList = sm + 0x24c;
    using HasLevelFn = bool(__thiscall*)(void*, int32_t, int32_t);
    if (!reinterpret_cast<HasLevelFn>(0x4a45d0)(levelList, levelType, levelIdx)) return 0u;

    using GetLevelInfoFn = uint8_t*(__thiscall*)(void*, int32_t, int32_t);
    uint8_t* info = reinterpret_cast<GetLevelInfoFn>(0x4a4620)(levelList, levelType, levelIdx);
    if (info == nullptr) return 0u;

    int32_t levelId = *reinterpret_cast<const int32_t*>(info + 0x3c);
    using GetLevelSaveInfoFn = uint8_t*(__thiscall*)(void*, int32_t);
    uint8_t* save = reinterpret_cast<GetLevelSaveInfoFn>(0x4920e0)(sm + 0x298, levelId);
    if (save == nullptr || save[0] == 0) return 0u;

    return *reinterpret_cast<const uint32_t*>(save + 0x4);
}

static void SetTimerRankTextWstr(int rank, EdgeWString* out) {
    static const wchar_t* const kRankTexts[7] = {L"D", L"C", L"B", L"A", L"S", L"S+", L"PB"};
    int idx = std::clamp(rank, 0, 6);
    out->set_literal(kRankTexts[idx], (idx >= 5) ? 2u : 1u);
}

static uint32_t GetTimerRankRgb(int rank) {
    if (rank == kRankPB) {
        double s = 0.5 + 0.5 * std::sin(GetHudTimerClockSec() * 2.0 * 3.14159265358979323846 * 1.15);
        uint32_t g = static_cast<uint32_t>(std::lround(215.0 + (242.0 - 215.0) * s));
        uint32_t b = static_cast<uint32_t>(std::lround(168.0 * s));
        return (0xffu << 16) | ((g & 0xffu) << 8) | (b & 0xffu);
    }
    if (rank >= kRankS) {
        return CalcSmoothRainbow(static_cast<float>(GetHudTimerClockSec() * 22.0), 1.0f);
    }
    if (rank == kRankA) return 0x55ff77u; // A
    if (rank == kRankB) return 0x55ccffu; // B
    if (rank == kRankC) return 0xffaa33u; // C
    return 0xff5555u;                     // D
}

static void ComputeTimerStats(
    const uint8_t* level, uint32_t pbTicks, double effEl, double effEd,
    int* outRank, int* outUnderRank, double* outRemTicks, double* outBarFrac, uint32_t* outRankRgb
) {
    double effNet = (std::max)(0.0, effEl - effEd);

    struct RankStep {
        double thresh;
        int    rank;
    };
    RankStep steps[6]{};
    int count = 0;
    bool pbInserted = false;
    double pbThresh = static_cast<double>(pbTicks);

    for (int i = 0; i < 5; ++i) {
        double th = static_cast<double>(*reinterpret_cast<const uint32_t*>(level + 0x80 + i * 4));
        if (pbTicks > 0u && !pbInserted && pbThresh <= th) {
            steps[count++] = {pbThresh, kRankPB};
            pbInserted = true;
        }
        steps[count++] = {th, kRankSP - i};
    }
    if (pbTicks > 0u && !pbInserted) {
        steps[count++] = {pbThresh, kRankPB};
    }

    int rank = kRankD;
    int underRank = -1;
    double remTicks = 0.0;
    double barFrac = 0.0;
    for (int idx = 0; idx < count; ++idx) {
        double hi = steps[idx].thresh;
        if (effNet <= hi) {
            rank = steps[idx].rank;
            remTicks = hi - effNet;
            double lo = (idx == 0) ? 0.0 : steps[idx - 1].thresh;
            double span = (hi > lo) ? (hi - lo) : hi;
            barFrac = (span > 0.0) ? std::clamp(remTicks / span, 0.0, 1.0) : 0.0;
            underRank = kRankD;
            for (int k = idx + 1; k < count; ++k) {
                if (steps[k].thresh > hi) {
                    underRank = steps[k].rank;
                    break;
                }
            }
            break;
        }
    }

    *outRank = rank;
    *outUnderRank = underRank;
    *outRemTicks = remTicks;
    *outBarFrac = barFrac;
    *outRankRgb = GetTimerRankRgb(rank);
}

static void ComputeMenuPreviewBesidePos(
    uint32_t expectedState, float panelW, float panelH, int32_t virtW, int32_t virtH,
    float* outX, float* outY
) {
    int basePt[2] = {0, 0};
    reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
    int menuW = *reinterpret_cast<const int*>(0x589ddc);
    float menuRight = static_cast<float>(basePt[0] + menuW + 6);
    float maxX = (std::max)(0.0f, static_cast<float>(virtW) - panelW);
    float maxY = (std::max)(0.0f, static_cast<float>(virtH) - panelH);
    *outX = std::clamp(menuRight + 20.0f, 0.0f, maxX);
    float targetY = (expectedState == 0x12u)
        ? static_cast<float>(4 + 0x75 + GetPauseContentShiftY())
        : 90.0f;
    *outY = std::clamp(targetY, 0.0f, maxY);
}

static bool DrawSPlusBarSprite(
    uint8_t* renderer, uint8_t* sm, int x, int y, int w, int h, uint8_t fgAlpha
) {
    if (renderer == nullptr || sm == nullptr || w <= 0 || h <= 0) return false;
    uint8_t* hGrp = *reinterpret_cast<uint8_t* const*>(sm + 0x58);
    if (hGrp == nullptr) return false;
    int16_t cnt = *reinterpret_cast<const int16_t*>(hGrp + 8);
    uint8_t** hSprites = *reinterpret_cast<uint8_t** const*>(hGrp + 0x20);
    if (cnt <= 0 || hSprites == nullptr) return false;

    int32_t savedSrc = *reinterpret_cast<const int32_t*>(renderer + 0x18);
    int32_t savedDst = *reinterpret_cast<const int32_t*>(renderer + 0x1c);
    uint8_t* mstack = GetMatrixStack();
    uint32_t savedMode = (mstack != nullptr) ? *reinterpret_cast<const uint32_t*>(mstack + 0x4) : 1u;
    Game_SetBlend(renderer, 0);
    DrawSmoothRainbowSpriteAtFrac(
        hSprites, static_cast<uint32_t>(cnt), GetHudTimerClockSec() * 30.0,
        w, h, x, y, fgAlpha
    );
    if (mstack != nullptr) {
        *reinterpret_cast<uint32_t*>(mstack + 0x4) = 1u;
        reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);
        *reinterpret_cast<uint32_t*>(mstack + 0x4) = savedMode;
    }
    Game_SetD3DBlend(renderer, savedSrc, savedDst);
    return true;
}

static void DrawTimerRankBar(
    uint8_t* renderer, uint8_t* sm, int rank, int underRank, double barFrac,
    int x, int y, int barW, int barH, uint32_t rankRgba, uint8_t fgAlpha
) {
    if (barW <= 0 || barH <= 0 || fgAlpha == 0) return;

    if (underRank < 0) {
        // Bottom rank (D): no lower rank underneath; fills bar with solid D color.
        Game_DrawQuad(x, y, barW, barH, &rankRgba);
        return;
    }

    int fillW = std::clamp(static_cast<int>(std::lround(barFrac * static_cast<double>(barW))), 0, barW);

    // Underlying next-rank layer (dimmed to 65% while the current rank layer is active,
    // matching the speedometer's 2-layer buff bar stacking rule).
    if (underRank == kRankSP && DrawSPlusBarSprite(renderer, sm, x, y, barW, barH, fgAlpha)) {
        if (fillW > 0) {
            uint32_t dimRgba = MakeRgba(0, 0, 0, static_cast<uint8_t>(std::lround(0.35 * static_cast<double>(fgAlpha))));
            Game_DrawQuad(x, y, barW, barH, &dimRgba);
        }
    } else {
        uint32_t underRgb = GetTimerRankRgb(underRank);
        if (fillW > 0) {
            underRgb = ScaleRgb(underRgb, 0.65f);
        }
        uint32_t underRgba = RgbToRgba(underRgb, fgAlpha);
        Game_DrawQuad(x, y, barW, barH, &underRgba);
    }

    if (fillW > 0) {
        if (!(rank == kRankSP && DrawSPlusBarSprite(renderer, sm, x, y, fillW, barH, fgAlpha))) {
            Game_DrawQuad(x, y, fillW, barH, &rankRgba);
        }
        if (fillW > 1 && fillW < barW) {
            uint32_t capRgba = MakeRgba(255, 255, 255, fgAlpha);
            Game_DrawQuad(x + fillW - 1, y, 1, barH, &capRgba);
        }
    }
}

static void __cdecl RenderHudTimerOverlay(
    uint8_t* renderer, uint8_t* ws, uint32_t expectedState, HudCornerStack* stack = nullptr
) {
    uint8_t mode = g_timerMode;
    float size = g_timerSize;
    uint8_t pos = g_timerPos;
    bool forceSample = false;
    bool dockBesideMenu = false;
    bool hasMenuPreview = GetHudTimerMenuPreview(expectedState, &mode, &size, &pos, &forceSample, &dockBesideMenu);
    if ((expectedState == 9u || expectedState == 0x12u) && !hasMenuPreview) return;
    if (mode == 0 || renderer == nullptr) return;

    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;

    uint32_t state = *reinterpret_cast<const uint32_t*>(sm + 0x50);
    if (state != expectedState) return;

    int32_t virtW = *reinterpret_cast<const int32_t*>(sm + 0x24);
    int32_t virtH = *reinterpret_cast<const int32_t*>(sm + 0x28);
    if (virtW <= 0 || virtH <= 0) return;

    double effEl = 415.0;
    double remTicks = 185.0;
    double barFrac = 0.62;
    int rank = kRankSP;
    int underRank = kRankS;
    uint32_t rankRgb = GetTimerRankRgb(kRankSP);

    if (!forceSample) {
        uint8_t* level = GetLevel();
        if (ws == nullptr || ws[0x2120] == 0 || level == nullptr) return;

        int32_t curEl = *reinterpret_cast<const int32_t*>(ws + 0x10c);
        int32_t curEd = *reinterpret_cast<const int32_t*>(ws + 0x108)
                      + ((ws[0x6c] != 0) ? *reinterpret_cast<const int32_t*>(ws + 0x78) : 0);

        effEl = static_cast<double>(curEl);
        double effEd = static_cast<double>(curEd);
        if (state == 0x11 && g_extWsPtr == ws) {
            int32_t dEl = curEl - g_wsPrevElapsed;
            if (dEl >= 0 && dEl <= 2) {
                effEl = static_cast<double>(g_wsPrevElapsed) + static_cast<double>(dEl) * static_cast<double>(g_uiAlpha);
            }
            int32_t dEd = curEd - g_wsPrevTotalEdge;
            if (dEd >= 0 && dEd <= 2) {
                effEd = static_cast<double>(g_wsPrevTotalEdge) + static_cast<double>(dEd) * static_cast<double>(g_uiAlpha);
            }
        }
        ComputeTimerStats(level, GetCurrentLevelPbTicks(), effEl, effEd, &rank, &underRank, &remTicks, &barFrac, &rankRgb);
    }

    uint8_t fgAlpha = ((expectedState == 9u || expectedState == 0x12u) && !dockBesideMenu) ? 140u : 255u;
    uint32_t rankRgba = RgbToRgba(rankRgb, fgAlpha);

    constexpr int kPanelW = 200;
    constexpr int kPadX   = 10;
    constexpr int kPadY   = 8;
    constexpr int kRowH   = 18;
    constexpr int kRowGap = 6;
    constexpr int kBarH   = 6;
    constexpr int kBarW   = kPanelW - 2 * kPadX;

    bool showElapsed = (mode == 1 || mode == 3);
    bool showRank    = (mode == 2 || mode == 3);

    int curY = kPadY;
    int elY = curY; if (showElapsed) curY += kRowH + kRowGap;
    int rkY = curY; if (showRank)    curY += kRowH + kRowGap;
    int barY = curY; curY += kBarH + kPadY;
    int panelH = curY;

    float s = std::clamp(size, 0.25f, 2.0f);
    float scaledW = static_cast<float>(kPanelW) * s;
    float scaledH = static_cast<float>(panelH) * s;

    float ox = 0.0f;
    float oy = 0.0f;
    if (dockBesideMenu) {
        ComputeMenuPreviewBesidePos(expectedState, scaledW, scaledH, virtW, virtH, &ox, &oy);
    } else {
        MinimapHudBox mm = ComputeMinimapHudBox(sm, ws, virtW, virtH, expectedState);
        PlaceCornerHudBox(stack, mm, pos, scaledW, scaledH, virtW, virtH, &ox, &oy);
    }

    bool drawLeft   = (ox > 0.5f);
    bool drawRight  = (ox + scaledW < static_cast<float>(virtW) - 0.5f);
    bool drawTop    = (oy > 0.5f);
    bool drawBottom = (oy + scaledH < static_cast<float>(virtH) - 0.5f);

    uint8_t* mstack = GetMatrixStack();
    if (mstack == nullptr) return;
    uint32_t& stackMode = *reinterpret_cast<uint32_t*>(mstack + 0x4);
    uint32_t savedMode = stackMode;
    stackMode = 1;

    float mat[16] = {
        s,    0.0f, 0.0f, 0.0f,
        0.0f, s,    0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        ox,   -oy,  0.0f, 1.0f
    };
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f750)(mstack);
    reinterpret_cast<void(__thiscall*)(void*, const float*)>(0x41f520)(mstack, mat);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);

    Game_SetBlend(renderer, 0);
    uint32_t* pTextCol = reinterpret_cast<uint32_t*>(0x5f4268);
    uint32_t savedTextCol = *pTextCol;

    DrawHudPanelBgAndBorder(kPanelW, panelH, g_timerBgOpacity, fgAlpha, drawTop, drawBottom, drawLeft, drawRight);
    uint8_t trkAlpha = static_cast<uint8_t>((42u * fgAlpha) / 255u);
    uint32_t trackRgba = MakeRgba(255, 255, 255, trkAlpha);
    uint8_t shAlpha = CalcHudShadowAlpha(g_timerShadowOpacity, fgAlpha);
    uint32_t shadowRgba = MakeRgba(0, 0, 0, shAlpha);

    if (showElapsed) {
        wchar_t elBuf[32];
        EdgeWString elWs;
        FormatTicksWstr(effEl, elBuf, 32, &elWs);
        if (shAlpha > 0) {
            *pTextCol = shadowRgba;
            Game_DrawText(&elWs, kPadX + 2, elY + 2, 0x2000, 0x2000, 1, 0, 0x14);
        }
        *pTextCol = MakeRgba(255, 255, 255, fgAlpha);
        Game_DrawText(&elWs, kPadX, elY, 0x2000, 0x2000, 1, 0, 0x14);
    }

    if (showRank) {
        wchar_t remBuf[32];
        EdgeWString remWs;
        FormatTicksWstr(remTicks, remBuf, 32, &remWs);
        if (shAlpha > 0) {
            *pTextCol = shadowRgba;
            Game_DrawText(&remWs, kPadX + 2, rkY + 2, 0x2000, 0x2000, 1, 0, 0x14);
        }
        *pTextCol = rankRgba;
        Game_DrawText(&remWs, kPadX, rkY, 0x2000, 0x2000, 1, 0, 0x14);
    }

    {
        EdgeWString rankWs;
        SetTimerRankTextWstr(rank, &rankWs);
        int rankTextY = showRank ? rkY : elY;
        if (shAlpha > 0) {
            *pTextCol = shadowRgba;
            Game_DrawText(&rankWs, kPanelW - kPadX + 2, rankTextY + 2, 0x2000, 0x2000, 1, 0, 0x18);
        }
        *pTextCol = rankRgba;
        Game_DrawText(&rankWs, kPanelW - kPadX, rankTextY, 0x2000, 0x2000, 1, 0, 0x18);
    }

    if (shAlpha > 0) {
        Game_DrawQuad(kPadX + 2, barY + 2, kBarW, kBarH, &shadowRgba);
    }
    Game_DrawQuad(kPadX, barY, kBarW, kBarH, &trackRgba);
    DrawTimerRankBar(renderer, sm, rank, underRank, barFrac, kPadX, barY, kBarW, kBarH, rankRgba, fgAlpha);

    *pTextCol = savedTextCol;
    Game_SetBlend(renderer, 0);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f610)(mstack);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);
    stackMode = savedMode;
}

static void __cdecl RenderHudPrismCounterOverlay(
    uint8_t* renderer, uint8_t* ws, uint32_t expectedState, HudCornerStack* stack
) {
    uint8_t type = g_prismCounterType;
    float size = g_prismCounterSize;
    uint8_t pos = g_prismCounterPos;
    bool forceSample = false;
    bool dockBesideMenu = false;
    bool hasMenuPreview = GetHudPrismCounterMenuPreview(expectedState, &type, &size, &pos, &forceSample, &dockBesideMenu);
    if ((expectedState == 9u || expectedState == 0x12u) && !hasMenuPreview) return;
    if (type == 0 || renderer == nullptr) return;

    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;

    uint32_t state = *reinterpret_cast<const uint32_t*>(sm + 0x50);
    if (state != expectedState) return;

    int32_t virtW = *reinterpret_cast<const int32_t*>(sm + 0x24);
    int32_t virtH = *reinterpret_cast<const int32_t*>(sm + 0x28);
    if (virtW <= 0 || virtH <= 0) return;

    int32_t collected = 7;
    int32_t total = 10;

    if (!forceSample) {
        uint8_t* level = GetLevel();
        if (ws == nullptr || ws[0x2120] == 0 || level == nullptr) return;
        total = *reinterpret_cast<const int16_t*>(level + 0x7c);
        collected = *reinterpret_cast<const int32_t*>(ws + 0x11c);
        if (total <= 0) {
            if (expectedState == 0x12u && hasMenuPreview) {
                collected = 7;
                total = 10;
            } else {
                return;
            }
        }
        if (collected < 0) collected = 0;
    }

    uint8_t fgAlpha = ((expectedState == 9u || expectedState == 0x12u) && !dockBesideMenu) ? 140u : 255u;

    bool showGrid = (type == 2 && total <= 20);
    constexpr int kPadX = 10;
    constexpr int kPadY = 8;
    constexpr int kDot = 10;
    constexpr int kDotGap = 4;
    constexpr int kRowH = 18;
    constexpr int kIconGap = 8;

    wchar_t cntBuf[32];
    EdgeWString cntWs;
    int panelW = 0;
    int panelH = 0;
    if (showGrid) {
        int cols = (total <= 10) ? total : 10;
        int rows = (total <= 10) ? 1 : 2;
        panelW = 2 * kPadX + cols * kDot + (cols - 1) * kDotGap;
        panelH = 2 * kPadY + rows * kDot + (rows - 1) * kDotGap;
    } else {
        FormatPrismRatioWstr(collected, total, cntBuf, 32, &cntWs);
        int textW = MeasureTextWidth(&cntWs, 0x2000, 1);
        if (textW <= 0) textW = static_cast<int>(cntWs.size) * 12;
        panelW = 2 * kPadX + kDot + kIconGap + textW;
        panelH = 2 * kPadY + kRowH;
    }

    float s = std::clamp(size, 0.25f, 2.0f);
    float scaledW = static_cast<float>(panelW) * s;
    float scaledH = static_cast<float>(panelH) * s;
    float ox = 0.0f;
    float oy = 0.0f;
    if (dockBesideMenu) {
        ComputeMenuPreviewBesidePos(expectedState, scaledW, scaledH, virtW, virtH, &ox, &oy);
    } else {
        MinimapHudBox mm = ComputeMinimapHudBox(sm, ws, virtW, virtH, expectedState);
        PlaceCornerHudBox(stack, mm, pos, scaledW, scaledH, virtW, virtH, &ox, &oy);
    }

    bool drawLeft   = (ox > 0.5f);
    bool drawRight  = (ox + scaledW < static_cast<float>(virtW) - 0.5f);
    bool drawTop    = (oy > 0.5f);
    bool drawBottom = (oy + scaledH < static_cast<float>(virtH) - 0.5f);

    uint8_t* mstack = GetMatrixStack();
    if (mstack == nullptr) return;
    uint32_t& stackMode = *reinterpret_cast<uint32_t*>(mstack + 0x4);
    uint32_t savedMode = stackMode;
    stackMode = 1;

    float mat[16] = {
        s,    0.0f, 0.0f, 0.0f,
        0.0f, s,    0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        ox,   -oy,  0.0f, 1.0f
    };
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f750)(mstack);
    reinterpret_cast<void(__thiscall*)(void*, const float*)>(0x41f520)(mstack, mat);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);

    Game_SetBlend(renderer, 0);
    uint32_t* pTextCol = reinterpret_cast<uint32_t*>(0x5f4268);
    uint32_t savedTextCol = *pTextCol;

    DrawHudPanelBgAndBorder(panelW, panelH, g_prismBgOpacity, fgAlpha, drawTop, drawBottom, drawLeft, drawRight);
    uint8_t shAlpha = CalcHudShadowAlpha(g_prismShadowOpacity, fgAlpha);
    uint32_t shadowRgba = MakeRgba(0, 0, 0, shAlpha);

    float clockTicks = static_cast<float>(GetHudTimerClockSec() * 15.0);
    if (showGrid) {
        uint32_t uncollectedRgba = MakeRgba(0x7d, 0x7d, 0x7d, fgAlpha);
        for (int i = 0; i < total; ++i) {
            int r = i / 10;
            int c = i % 10;
            int dx = kPadX + c * (kDot + kDotGap);
            int dy = kPadY + r * (kDot + kDotGap);
            if (shAlpha > 0) {
                Game_DrawQuad(dx + 2, dy + 2, kDot, kDot, &shadowRgba);
            }
            uint32_t dotRgba = (i < collected)
                ? RgbToRgba(CalcSmoothRainbow(clockTicks - static_cast<float>(i), 1.0f), fgAlpha)
                : uncollectedRgba;
            Game_DrawQuad(dx, dy, kDot, kDot, &dotRgba);
        }
    } else {
        uint32_t rainbowRgba = RgbToRgba(CalcSmoothRainbow(clockTicks, 1.0f), fgAlpha);
        uint32_t iconRgba = (collected > 0) ? rainbowRgba : MakeRgba(0x7d, 0x7d, 0x7d, fgAlpha);
        int iconY = kPadY + (kRowH - kDot) / 2;
        if (shAlpha > 0) {
            Game_DrawQuad(kPadX + 2, iconY + 2, kDot, kDot, &shadowRgba);
        }
        Game_DrawQuad(kPadX, iconY, kDot, kDot, &iconRgba);
        if (shAlpha > 0) {
            *pTextCol = shadowRgba;
            Game_DrawText(&cntWs, kPadX + kDot + kIconGap + 2, kPadY + 2, 0x2000, 0x2000, 1, 0, 0x14);
        }
        *pTextCol = (collected >= total) ? rainbowRgba : MakeRgba(255, 255, 255, fgAlpha);
        Game_DrawText(&cntWs, kPadX + kDot + kIconGap, kPadY, 0x2000, 0x2000, 1, 0, 0x14);
    }

    *pTextCol = savedTextCol;
    Game_SetBlend(renderer, 0);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f610)(mstack);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);
    stackMode = savedMode;
}

static void __cdecl RenderHudSpeedometerOverlay(
    uint8_t* renderer, uint8_t* ws, uint32_t expectedState, HudCornerStack* stack
) {
    uint8_t enable = g_speedometerEnable;
    float size = g_speedometerSize;
    uint8_t pos = g_speedometerPos;
    bool forceSample = false;
    bool dockBesideMenu = false;
    bool hasMenuPreview = GetHudSpeedometerMenuPreview(expectedState, &enable, &size, &pos, &forceSample, &dockBesideMenu);
    if ((expectedState == 9u || expectedState == 0x12u) && !hasMenuPreview) return;
    if (enable == 0 || renderer == nullptr) return;

    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;

    uint32_t state = *reinterpret_cast<const uint32_t*>(sm + 0x50);
    if (state != expectedState) return;

    int32_t virtW = *reinterpret_cast<const int32_t*>(sm + 0x24);
    int32_t virtH = *reinterpret_cast<const int32_t*>(sm + 0x28);
    if (virtW <= 0 || virtH <= 0) return;

    uint8_t* cube = nullptr;
    int32_t curActiveTicks = 0;
    int32_t curRollsUsed = 22;
    if (!forceSample) {
        if (ws == nullptr || ws[0x2120] == 0) {
            if (!(expectedState == 0x12u && hasMenuPreview)) return;
        } else {
            cube = *reinterpret_cast<uint8_t* const*>(ws + 0x34);
            if (cube != nullptr) {
                curActiveTicks = *reinterpret_cast<const int32_t*>(cube + 0x168);
                curRollsUsed = *reinterpret_cast<const int32_t*>(cube + 0x160);
            }
        }
    }

    double animSec = GetHudTimerClockSec();
    int32_t displaySpeed = 0;
    double effActiveTicks = 0.0;
    double effRollsLeft = 0.0;

    bool useSampleAnim = forceSample || (expectedState == 0x12u && dockBesideMenu && curActiveTicks <= 0 && curRollsUsed >= 22);
    if (useSampleAnim) {
        double phase = std::fmod(animSec, 6.0);
        displaySpeed = 100;
        if (phase < 3.5) {
            effActiveTicks = 100.0 * (1.0 - phase / 3.5);
            effRollsLeft = 22.0;
        } else {
            effActiveTicks = 0.0;
            effRollsLeft = 22.0 * (1.0 - (phase - 3.5) / 2.5);
        }
    } else {
        if (cube == nullptr) return;
        displaySpeed = static_cast<int32_t>(std::lround(g_speedTracker.displaySpeed));
        if (displaySpeed < 0) displaySpeed = 0;

        double effActive = static_cast<double>((std::max)(curActiveTicks, 0));
        if (state == 0x11u && g_extWsPtr == ws && curActiveTicks >= 0 &&
            g_wsPrevBuffTicks > curActiveTicks && (g_wsPrevBuffTicks - curActiveTicks) <= 2) {
            effActive = static_cast<double>(g_wsPrevBuffTicks)
                      + static_cast<double>(curActiveTicks - g_wsPrevBuffTicks) * static_cast<double>(g_uiAlpha);
        }
        effActiveTicks = std::clamp(effActive, 0.0, 100.0);

        if (g_speedTracker.rollsInit) {
            effRollsLeft = std::clamp(static_cast<double>(g_speedTracker.smoothRollsLeft), 0.0, 22.0);
        } else {
            effRollsLeft = static_cast<double>(std::clamp<int32_t>(22 - curRollsUsed, 0, 22));
        }
    }

    uint8_t fgAlpha = ((expectedState == 9u || expectedState == 0x12u) && !dockBesideMenu) ? 140u : 255u;

    constexpr int kPanelW = 200;
    constexpr int kPadX   = 10;
    constexpr int kPadY   = 8;
    constexpr int kRowH   = 18;
    constexpr int kRowGap = 6;
    constexpr int kBarH   = 6;
    constexpr int kBarW   = kPanelW - 2 * kPadX;
    constexpr int kRowY   = kPadY;
    constexpr int kBarY   = kPadY + kRowH + kRowGap;
    constexpr int kPanelH = kBarY + kBarH + kPadY;

    float s = std::clamp(size, 0.25f, 2.0f);
    float scaledW = static_cast<float>(kPanelW) * s;
    float scaledH = static_cast<float>(kPanelH) * s;

    float ox = 0.0f;
    float oy = 0.0f;
    if (dockBesideMenu) {
        ComputeMenuPreviewBesidePos(expectedState, scaledW, scaledH, virtW, virtH, &ox, &oy);
    } else {
        MinimapHudBox mm = ComputeMinimapHudBox(sm, ws, virtW, virtH, expectedState);
        PlaceCornerHudBox(stack, mm, pos, scaledW, scaledH, virtW, virtH, &ox, &oy);
    }

    bool drawLeft   = (ox > 0.5f);
    bool drawRight  = (ox + scaledW < static_cast<float>(virtW) - 0.5f);
    bool drawTop    = (oy > 0.5f);
    bool drawBottom = (oy + scaledH < static_cast<float>(virtH) - 0.5f);

    uint8_t* mstack = GetMatrixStack();
    if (mstack == nullptr) return;
    uint32_t& stackMode = *reinterpret_cast<uint32_t*>(mstack + 0x4);
    uint32_t savedMode = stackMode;
    stackMode = 1;

    float mat[16] = {
        s,    0.0f, 0.0f, 0.0f,
        0.0f, s,    0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        ox,   -oy,  0.0f, 1.0f
    };
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f750)(mstack);
    reinterpret_cast<void(__thiscall*)(void*, const float*)>(0x41f520)(mstack, mat);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);

    Game_SetBlend(renderer, 0);
    uint32_t* pTextCol = reinterpret_cast<uint32_t*>(0x5f4268);
    uint32_t savedTextCol = *pTextCol;

    DrawHudPanelBgAndBorder(kPanelW, kPanelH, g_speedometerBgOpacity, fgAlpha, drawTop, drawBottom, drawLeft, drawRight);
    uint8_t trkAlpha = static_cast<uint8_t>((42u * fgAlpha) / 255u);
    uint32_t trackRgba = MakeRgba(255, 255, 255, trkAlpha);
    uint8_t shAlpha = CalcHudShadowAlpha(g_speedometerShadowOpacity, fgAlpha);
    uint32_t shadowRgba = MakeRgba(0, 0, 0, shAlpha);

    bool hasActiveBuff = (effActiveTicks > 0.05);
    bool hasRollBuff   = (!hasActiveBuff && effRollsLeft > 0.05);

    uint32_t row1Rgba = MakeRgba(255, 255, 255, fgAlpha);
    if (hasActiveBuff) {
        row1Rgba = RgbToRgba(CalcSmoothRainbow(static_cast<float>(animSec * 36.0), 1.0f), fgAlpha);
    } else if (hasRollBuff) {
        row1Rgba = RgbToRgba(CalcSmoothRainbow(static_cast<float>(animSec * 10.0), 1.0f), fgAlpha);
    }

    wchar_t spdBuf[32];
    EdgeWString spdWs;
    FormatSpeedIntWstr(displaySpeed, spdBuf, 32, &spdWs);
    if (shAlpha > 0) {
        *pTextCol = shadowRgba;
        Game_DrawText(&spdWs, kPadX + 2, kRowY + 2, 0x2000, 0x2000, 1, 0, 0x14);
    }
    *pTextCol = row1Rgba;
    Game_DrawText(&spdWs, kPadX, kRowY, 0x2000, 0x2000, 1, 0, 0x14);

    if (hasActiveBuff || hasRollBuff) {
        EdgeWString arrowWs;
        if (hasActiveBuff) {
            arrowWs.set_literal(L">>", 2u);
        } else {
            arrowWs.set_literal(L">", 1u);
        }
        if (shAlpha > 0) {
            *pTextCol = shadowRgba;
            Game_DrawText(&arrowWs, kPanelW - kPadX + 2, kRowY + 2, 0x2000, 0x2000, 1, 0, 0x18);
        }
        *pTextCol = row1Rgba;
        Game_DrawText(&arrowWs, kPanelW - kPadX, kRowY, 0x2000, 0x2000, 1, 0, 0x18);
    }

    if (shAlpha > 0) {
        Game_DrawQuad(kPadX + 2, kBarY + 2, kBarW, kBarH, &shadowRgba);
    }
    Game_DrawQuad(kPadX, kBarY, kBarW, kBarH, &trackRgba);

    int rollFillW = std::clamp(
        static_cast<int>(std::lround((effRollsLeft / 22.0) * static_cast<double>(kBarW))), 0, kBarW
    );
    if (rollFillW > 0) {
        uint32_t rollRgb = CalcSmoothRainbow(static_cast<float>(animSec * 10.0), 1.0f);
        if (hasActiveBuff) {
            rollRgb = ScaleRgb(rollRgb, 0.65f);
        }
        uint32_t rollRgba = RgbToRgba(rollRgb, fgAlpha);
        Game_DrawQuad(kPadX, kBarY, rollFillW, kBarH, &rollRgba);
    }

    int activeFillW = std::clamp(
        static_cast<int>(std::lround((effActiveTicks / 100.0) * static_cast<double>(kBarW))), 0, kBarW
    );
    if (activeFillW > 0) {
        bool drewSprite = false;
        uint8_t* hGrp = *reinterpret_cast<uint8_t* const*>(sm + 0x58);
        if (hGrp != nullptr) {
            int16_t cnt = *reinterpret_cast<const int16_t*>(hGrp + 8);
            uint8_t** hSprites = *reinterpret_cast<uint8_t** const*>(hGrp + 0x20);
            if (cnt > 0 && hSprites != nullptr) {
                DrawSmoothRainbowSpriteAtFrac(
                    hSprites, static_cast<uint32_t>(cnt), animSec * 30.0,
                    activeFillW, kBarH, kPadX, kBarY, fgAlpha
                );
                Game_SetBlend(renderer, 0);
                drewSprite = true;
            }
        }
        if (!drewSprite) {
            uint32_t actRgba = RgbToRgba(CalcSmoothRainbow(static_cast<float>(animSec * 36.0), 1.0f), fgAlpha);
            Game_DrawQuad(kPadX, kBarY, activeFillW, kBarH, &actRgba);
        }
        if (activeFillW > 1 && activeFillW < kBarW) {
            uint32_t capRgba = MakeRgba(255, 255, 255, fgAlpha);
            Game_DrawQuad(kPadX + activeFillW - 1, kBarY, 1, kBarH, &capRgba);
        }
    }

    *pTextCol = savedTextCol;
    Game_SetBlend(renderer, 0);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f610)(mstack);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);
    stackMode = savedMode;
}

static void __cdecl RenderHudOverlaysWorld_Impl(uint8_t* renderer, uint8_t* ws) {
    UpdateSpeedometerTracker(renderer, ws);
    HudCornerStack stack{};
    RenderHudTimerOverlay(renderer, ws, 0x11u, &stack);
    RenderHudPrismCounterOverlay(renderer, ws, 0x11u, &stack);
    RenderHudSpeedometerOverlay(renderer, ws, 0x11u, &stack);
}

static void __declspec(naked) hk_worldEnd2DHudTimer() {
    __asm {
        pushfd
        pushad
        push edi
        push ecx
        call RenderHudOverlaysWorld_Impl
        add esp, 8
        popad
        popfd
        mov eax, 0x406690
        jmp eax
    }
}

static void __cdecl RenderHudTimerPause_Impl(uint8_t* renderer) {
    uint8_t* ws = GetWorldScreen();
    RenderMinimapMenuPreview(renderer, ws, 0x12u);
    HudCornerStack stack12{};
    RenderHudTimerOverlay(renderer, ws, 0x12u, &stack12);
    RenderHudPrismCounterOverlay(renderer, ws, 0x12u, &stack12);
    RenderHudSpeedometerOverlay(renderer, ws, 0x12u, &stack12);
    RenderMinimapMenuPreview(renderer, ws, 9u);
    HudCornerStack stack9{};
    RenderHudTimerOverlay(renderer, ws, 9u, &stack9);
    RenderHudPrismCounterOverlay(renderer, ws, 9u, &stack9);
    RenderHudSpeedometerOverlay(renderer, ws, 9u, &stack9);
}

static void __declspec(naked) hk_menuEnd2DHudTimer() {
    __asm {
        pushfd
        pushad
        push ecx
        call RenderHudTimerPause_Impl
        add esp, 4
        popad
        popfd
        mov eax, 0x406690
        jmp eax
    }
}

} // namespace

namespace edgemod {

void __cdecl ResetSpeedometerState() {
    g_speedTracker.ResetAll();
    g_wsPrevBuffTicks = 0;
}

void __cdecl RenderPauseHeaderTimer(int x, int y, int width) {
    uint8_t* ws = GetWorldScreen();
    uint8_t* level = GetLevel();
    if (ws == nullptr || ws[0x2120] == 0 || level == nullptr || width <= 0) return;

    int32_t curEl = *reinterpret_cast<const int32_t*>(ws + 0x10c);
    int32_t curEd = *reinterpret_cast<const int32_t*>(ws + 0x108)
                  + ((ws[0x6c] != 0) ? *reinterpret_cast<const int32_t*>(ws + 0x78) : 0);

    int rank = kRankSP;
    int underRank = kRankS;
    double remTicks = 0.0;
    double barFrac = 0.0;
    uint32_t rankRgb = 0xffffffu;
    ComputeTimerStats(level, GetCurrentLevelPbTicks(), static_cast<double>(curEl), static_cast<double>(curEd), &rank, &underRank, &remTicks, &barFrac, &rankRgb);

    uint32_t rankRgba = RgbToRgba(rankRgb, 255u);
    uint32_t* pTextCol = reinterpret_cast<uint32_t*>(0x5f4268);
    uint32_t savedTextCol = *pTextCol;

    wchar_t elBuf[32];
    EdgeWString elWs;
    FormatTicksWstr(static_cast<double>(curEl), elBuf, 32, &elWs);
    *pTextCol = 0xffffffffu;
    Game_DrawText(&elWs, x, y, 0x2000, 0x2000, 1, 0, 0x14);

    wchar_t remBuf[32];
    EdgeWString remWs;
    FormatTicksWstr(remTicks, remBuf, 32, &remWs);
    *pTextCol = rankRgba;
    Game_DrawText(&remWs, x, y + 18, 0x2000, 0x2000, 1, 0, 0x14);

    EdgeWString rankWs;
    SetTimerRankTextWstr(rank, &rankWs);
    *pTextCol = rankRgba;
    Game_DrawText(&rankWs, x + width, y + 18, 0x2000, 0x2000, 1, 0, 0x18);

    uint32_t trackRgba = MakeRgba(255, 255, 255, 42);
    Game_DrawQuad(x, y + 38, width, 5, &trackRgba);
    DrawTimerRankBar(GetRenderer(), GetScreenMgr(), rank, underRank, barFrac, x, y + 38, width, 5, rankRgba, 255u);

    *pTextCol = savedTextCol;
}

} // namespace edgemod

void RegisterUiSmoothPart2() {
    // ScoresMenu & CreditsMenu vtable[2] + CreditsMenu smooth fade/scroll/rainbows
    RegU32(0x58d120, 0x4af660, reinterpret_cast<uint32_t>(&ScoresRenderWrap_Impl));
    RegU32(0x58b9d8, 0x4a1a70, reinterpret_cast<uint32_t>(&CreditsRenderWrap_Impl));
    {
        const uint8_t exCif[10] = {0xf3, 0x0f, 0x2c, 0x43, 0x4c, 0x83, 0xf8, 0x08, 0x7d, 0x65};
        RegRelJmp(0x4a1aae, exCif, 10, 5, &hk_creditsIntroFade, 0x90, 0x4a1ac4);

        const uint8_t exCdt1[5] = {0xe8, 0xf2, 0x4f, 0x00, 0x00};
        RegRelCall(0x4a1c89, exCdt1, 5, 5, reinterpret_cast<void*>(&CreditsDrawText_Impl));

        const uint8_t exCdt2[5] = {0xe8, 0x10, 0x4f, 0x00, 0x00};
        RegRelCall(0x4a1d6b, exCdt2, 5, 5, reinterpret_cast<void*>(&CreditsDrawGameOver_Impl));

        const uint8_t exChdr[10] = {0x8b, 0x45, 0xa8, 0xd1, 0xe8, 0x03, 0x45, 0xc0, 0x33, 0xd2};
        RegRelJmp(0x4a1c27, exChdr, 10, 5, &hk_creditsHdrRb, 0x90, 0x4a1c45);
        RegRelJmp(0x4a1ce8, exChdr, 10, 5, &hk_creditsEndRb, 0x90, 0x4a1d04);

        const uint8_t exCcomp[10] = {0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe7, 0xc1, 0xea, 0x02};
        RegRelJmp(0x4a1df3, exCcomp, 10, 5, &hk_creditsCompRb, 0x90, 0x4a1e0b);
    }

    // ScoresMenu card flip & dots
    {
        const uint8_t exFlip[8] = {0x8b, 0x55, 0xa4, 0x8b, 0x72, 0x74, 0x2b, 0x75};
        RegRelJmp(0x4afdb0, exFlip, 8, 5, &hk_scoresCardFlip, 0x90, 0x4afe40);
        const uint8_t exDot[8] = {0x8b, 0x85, 0x5c, 0xff, 0xff, 0xff, 0x2b, 0xc1};
        RegRelJmp(0x4b0a6b, exDot, 8, 5, &hk_scoresDotRainbow, 0x90, 0x4b0ab5);
    }

    // InstructionsMenu & drawButton & ScoresSelectMenu
    {
        const uint8_t exIss[6] = {0x8b, 0x55, 0xbc, 0x2b, 0x7a, 0x28};
        RegRelCall(0x4b4246, exIss, 6, 6, &hk_instrSubScrollY);
        const uint8_t exIsr[7] = {0x8b, 0x14, 0x95, 0xb8, 0x9b, 0x58, 0x00};
        RegRelCall(0x4b4e4c, exIsr, 7, 7, &hk_instrSRainbow);
        const uint8_t exDbr[7] = {0x8b, 0x0c, 0x8d, 0xb8, 0x9b, 0x58, 0x00};
        RegRelCall(0x48873f, exDbr, 7, 7, &hk_drawBtnRainbow);

        const uint8_t exMulEdi[8] = {0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe7, 0xc1};
        RegRelJmp(0x4b1c71, exMulEdi, 8, 5, &hk_rb_4b1c71, 0x90, 0x4b1c8e);
        RegRelJmp(0x4b1d5a, exMulEdi, 8, 5, &hk_rb_4b1d5a, 0x90, 0x4b1d70);
        RegRelJmp(0x4b1dfd, exMulEdi, 8, 5, &hk_rb_4b1dfd, 0x90, 0x4b1e13);
        RegRelJmp(0x4b1f6a, exMulEdi, 8, 5, &hk_rb_4b1f6a, 0x90, 0x4b1f80);
    }

    // LevelSelectMenu & fullscreen exit overlays
    {
        const uint8_t exLte[6] = {0x8b, 0x57, 0x58, 0x8d, 0x4d, 0xc0};
        RegRelCall(0x4b8453, exLte, 6, 6, &hk_lsThumbEdx);
        const uint8_t exLce[6] = {0x8b, 0x4f, 0x58, 0xc1, 0xfa, 0x05};
        RegRelCall(0x4b84a1, exLce, 6, 6, &hk_lsThumbEcx);

        const uint8_t exFlash[12] = {0x85, 0xc0, 0x7e, 0x5e, 0x83, 0x7f, 0x08, 0x00, 0x7d, 0x58, 0x6b, 0xc0};
        RegRelJmp(0x4b81a3, exFlash, 12, 5, &hk_lsTileFlashPct, 0x90, 0x4b81c5);

        const uint8_t exBadge[12] = {0x8b, 0xc3, 0x99, 0x2b, 0xc2, 0x8b, 0xc8, 0xb8, 0xab, 0xaa, 0xaa, 0xaa};
        RegRelJmp(0x4b825f, exBadge, 12, 5, &hk_lsTileBadgeRainbow, 0x90, 0x4b82d9);

        const uint8_t exPct7[8] = {0x8b, 0x4f, 0x28, 0x6b, 0xc9, 0x64, 0xb8, 0x93};
        RegRelJmp(0x4b8527, exPct7, 8, 5, &hk_lsInfoCardPct7, 0x90, 0x4b8566);

        const uint8_t exSlideX[10] = {0x8b, 0x4d, 0xb0, 0x69, 0xc9, 0x11, 0x01, 0x00, 0x00, 0xb8};
        RegRelJmp(0x4b865e, exSlideX, 10, 5, &hk_lsInfoCardSlideX, 0x90, 0x4b868c);

        const uint8_t ex86fc[8] = {0x8b, 0x45, 0xac, 0x2b, 0xc1, 0x33, 0xd2, 0xb9};
        RegRelJmp(0x4b86fc, ex86fc, 8, 5, &hk_rb_4b86fc, 0x90, 0x4b8711);

        const uint8_t ex6c3e[8] = {0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe3, 0xc1};
        RegRelJmp(0x4b6c3e, ex6c3e, 8, 5, &hk_rb_4b6c3e, 0x90, 0x4b6c54);

        const uint8_t exExOv[11] = {0x69, 0xc9, 0xff, 0x00, 0x00, 0x00, 0xb8, 0x1f, 0x85, 0xeb, 0x51};
        RegRelJmp(0x4b9689, exExOv, 11, 5, &hk_subExitOv_4b9689, 0x90, 0x4b96a5);
        RegRelJmp(0x4b896b, exExOv, 11, 5, &hk_subExitOv_4b896b, 0x90, 0x4b8987);
    }

    // ConfirmExitGameMenu
    {
        const uint8_t exCbh[10] = {0x8b, 0x43, 0x08, 0xc7, 0x45, 0xac, 0x7e, 0x00, 0x00, 0x00};
        RegRelJmp(0x4a8df1, exCbh, 10, 5, &hk_cegBoxHeight, 0x90, 0x4a8e98);
        const uint8_t exCfp[8] = {0xbe, 0x64, 0x00, 0x00, 0x00, 0x83, 0xf8, 0x08};
        RegRelJmp(0x4a8ee3, exCfp, 8, 5, &hk_cegFadePct, 0x90, 0x4a8f3f);

        const uint8_t exMulEdi[8] = {0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe7, 0xc1};
        RegRelJmp(0x4a8f84, exMulEdi, 8, 5, &hk_rb_4a8f84, 0x90, 0x4a8fa3);
        RegRelJmp(0x4a9045, exMulEdi, 8, 5, &hk_rb_4a9045, 0x90, 0x4a905d);
        RegRelJmp(0x4a90f8, exMulEdi, 8, 5, &hk_rb_4a90f8, 0x90, 0x4a9110);
        RegRelJmp(0x4a926b, exMulEdi, 8, 5, &hk_rb_4a926b, 0x90, 0x4a9283);
        RegRelJmp(0x4a9390, exMulEdi, 8, 5, &hk_rb_4a9390, 0x90, 0x4a93a8);

        const uint8_t exCba[8] = {0x0f, 0x57, 0xc0, 0xf3, 0x0f, 0x2a, 0xc0, 0x0f};
        RegRelJmp(0x4a94f3, exCba, 8, 5, &hk_cegBlackoutAlpha, 0x90, 0x4a9511);
    }

    // DebriefMenu Stages 1..15
    {
        const uint8_t exS1[6] = {0x8b, 0x46, 0x44, 0x99, 0xb9, 0x0d};
        RegRelJmp(0x4a9b24, exS1, 6, 5, &hk_debTitleRb, 0x90, 0x4a9b45);

        const uint8_t exS2[6] = {0x83, 0x7e, 0x08, 0x00, 0x7c, 0x34};
        RegRelJmp(0x4a9bdd, exS2, 6, 5, &hk_debColor_4a9bdd, 0x90, 0x4a9c9f);
        const uint8_t exS3[6] = {0x83, 0x7e, 0x08, 0x00, 0xb9, 0x0d};
        RegRelJmp(0x4a9cf5, exS3, 6, 5, &hk_debColor_4a9cf5, 0x90, 0x4a9db0);
        const uint8_t exS4[6] = {0x83, 0xf8, 0x03, 0x0f, 0x8c, 0x6e};
        RegRelJmp(0x4a9e80, exS4, 6, 5, &hk_debColor_4a9e80, 0x90, 0x4a9f24);
        const uint8_t exS5[6] = {0x8b, 0x76, 0x44, 0x83, 0xfe, 0x04};
        RegRelJmp(0x4a9ff7, exS5, 6, 5, &hk_debColor_4a9ff7, 0x90, 0x4aa0bc);
        const uint8_t exS6a[8] = {0x8b, 0x85, 0x14, 0xff, 0xff, 0xff, 0x8b, 0x48};
        RegRelJmp(0x4aa227, exS6a, 8, 5, &hk_debColor_4aa227, 0x90, 0x4aa2d6);
        const uint8_t exS6bP[8] = {0x83, 0x78, 0x08, 0x00, 0x8b, 0x40, 0x44, 0x7c};
        RegRelJmp(0x4aa401, exS6bP, 8, 5, &hk_debColor_4aa401, 0x90, 0x4aa5ab);
        const uint8_t exS6bN[6] = {0x83, 0x78, 0x08, 0x00, 0x7c, 0x38};
        RegRelJmp(0x4aa50d, exS6bN, 6, 5, &hk_debColor_4aa50d, 0x90, 0x4aa5ab);
        const uint8_t exS7a[8] = {0x8b, 0x85, 0x14, 0xff, 0xff, 0xff, 0x8b, 0x40};
        RegRelJmp(0x4aa6db, exS7a, 8, 5, &hk_debColor_4aa6db, 0x90, 0x4aa7eb);
        const uint8_t exS7b[6] = {0x83, 0x79, 0x08, 0x00, 0x7c, 0x44};
        RegRelJmp(0x4aa87f, exS7b, 6, 5, &hk_debColor_4aa87f, 0x90, 0x4aa943);
        const uint8_t exS8a[8] = {0x8b, 0x8d, 0x14, 0xff, 0xff, 0xff, 0x8b, 0x41};
        RegRelJmp(0x4aadbf, exS8a, 8, 5, &hk_debColor_4aadbf, 0x90, 0x4aaebe);
        const uint8_t exS13a[6] = {0x83, 0xf8, 0x27, 0x0f, 0x8c, 0x0f};
        RegRelJmp(0x4ab722, exS13a, 6, 5, &hk_debColor_4ab722, 0x90, 0x4ab7c5);
        const uint8_t exS13b[8] = {0x8b, 0x95, 0x14, 0xff, 0xff, 0xff, 0x8b, 0x42};
        RegRelJmp(0x4ab93f, exS13b, 8, 5, &hk_debColor_4ab93f, 0x90, 0x4ab9ed);

        const uint8_t exNrS[6] = {0x83, 0xf8, 0x13, 0x7e, 0x05, 0xb8};
        RegRelCall(0x4aaece, exNrS, 6, 0x4aaeff - 0x4aaece, &hk_debNrScale);

        const uint8_t exNrM[8] = {0x83, 0x7a, 0x44, 0x13, 0x89, 0x8d, 0x24, 0xff};
        RegRelJmp(0x4aaf17, exNrM, 8, 5, &hk_debNrMarquee, 0x90, 0x4aaf37);

        const uint8_t exNrDt1[5] = {0xe8, 0xee, 0xbc, 0xff, 0xff};
        RegRelCall(0x4aaf8d, exNrDt1, 5, 5, reinterpret_cast<void*>(&DebNrDrawText_Impl));
        const uint8_t exNrDt2[5] = {0xe8, 0x8d, 0xbc, 0xff, 0xff};
        RegRelCall(0x4aafee, exNrDt2, 5, 5, reinterpret_cast<void*>(&DebNrDrawText_Impl));

        const uint8_t exRsx[9] = {0x83, 0xf8, 0x19, 0x0f, 0x8c, 0xc2, 0x01, 0x00, 0x00};
        RegRelJmp(0x4aabe2, exRsx, 9, 5, &hk_debRankSlideX, 0x90, 0x4aac1b);
        const uint8_t exDbh[6] = {0x8b, 0x40, 0x44, 0x83, 0xf8, 0x22};
        RegRelJmp(0x4ab014, exDbh, 6, 5, &hk_debBoxHeight, 0x90, 0x4ab04a);
        const uint8_t exDbw[7] = {0x0f, 0xb6, 0x35, 0xe8, 0x91, 0x5f, 0x00};
        RegRelJmp(0x4ab12c, exDbw, 7, 5, &hk_debBarWidth, 0x90, 0x4ab152);
        const uint8_t exDrs[6] = {0x83, 0xf9, 0x2b, 0x7e, 0x05, 0xb9};
        RegRelJmp(0x4ab197, exDrs, 6, 5, &hk_debRankStamp, 0x90, 0x4ab201);
        const uint8_t exDro[10] = {0x8b, 0x85, 0x14, 0xff, 0xff, 0xff, 0x83, 0x78, 0x08, 0x00};
        RegRelJmp(0x4ab33d, exDro, 10, 5, &hk_debRankOverlay, 0x90, 0x4ab480);
        const uint8_t exBqa[6] = {0x39, 0x79, 0x08, 0x7d, 0x22, 0x83};
        RegRelJmp(0x4abc79, exBqa, 6, 5, &hk_debBtnQuadAlpha, 0x90, 0x4abcd1);
        const uint8_t exBrb[6] = {0x8b, 0x95, 0xfc, 0xfe, 0xff, 0xff};
        RegRelJmp(0x4abcef, exBrb, 6, 5, &hk_debBtnRainbow, 0x90, 0x4abd0b);
        const uint8_t exExD[7] = {0x0f, 0xb6, 0x15, 0xe0, 0x88, 0x5f, 0x00};
        RegRelJmp(0x4abe4f, exExD, 7, 5, &hk_debriefFillExitRgba, 0x90, 0x4abea9);
    }

    // WorldScreen HUD Edge Time & Instructions Demo
    {
        const uint8_t exHts[7] = {0x8b, 0x44, 0xbe, 0x78, 0x83, 0xf8, 0x0a};
        RegRelCall(0x47ac9c, exHts, 7, 0x47ad55 - 0x47ac9c, &hk_hudTimerSetup);

        const uint8_t exHtd[6] = {0x8b, 0x44, 0x8e, 0x78, 0x99, 0xb9};
        RegRelCall(0x47ad62, exHtd, 6, 0x47adde - 0x47ad62, &hk_hudTimerDigitsAndColor);

        const uint8_t exHrm[10] = {0xf3, 0x0f, 0x10, 0x45, 0xfc, 0x68, 0xaa, 0x00, 0x00, 0x00};
        RegRelCall(0x47af27, exHrm, 10, 0x47af5a - 0x47af27, &hk_hudRestoreMat);

        const uint8_t exEds[8] = {0x8b, 0x75, 0x10, 0xb8, 0xe9, 0xa2, 0x8b, 0x2e};
        RegRelJmp(0x47a2d8, exEds, 8, 5, &hk_edgeDemoSetup, 0x90, 0x47a341);

        const uint8_t exDsp[5] = {0x6a, 0x01, 0x8d, 0x4e, 0x0a};
        RegRelCall(0x47a43e, exDsp, 5, 5, &hk_demoSepPush);

        const uint8_t exEnd2d[5] = {0xe8, 0x11, 0xb0, 0xf8, 0xff};
        RegRelCall(0x47b67a, exEnd2d, 5, 5, &hk_worldEnd2DHudTimer);

        const uint8_t exMenuEnd2d[5] = {0xe8, 0xd3, 0x9d, 0xf6, 0xff};
        RegRelCall(0x49c8b8, exMenuEnd2d, 5, 5, &hk_menuEnd2DHudTimer);
    }
}
