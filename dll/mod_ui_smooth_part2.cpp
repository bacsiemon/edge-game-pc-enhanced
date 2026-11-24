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
float    g_etRemTmp    = 0.0f;
float    g_etTicksTmp  = 0.0f;
uint8_t  g_lsFlashPrev[64]{};
uint32_t g_hudMatOrig[16]{};

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
    float saved4c = *reinterpret_cast<float*>(cm + 0x4c);
    if (cm[0x48] != 0 && cm[0x49] == 0 && *reinterpret_cast<int32_t*>(cm + 0x4) >= 5) {
        float speed = *reinterpret_cast<float*>(cm + 0x50);
        float eff = saved4c + speed * g_uiAlpha;
        if (speed < 0.0f) {
            float minY = *reinterpret_cast<const float*>(0x58b9c8);
            if (eff < minY) eff = minY;
        }
        *reinterpret_cast<float*>(cm + 0x4c) = eff;
    }
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(0x4a1a70)(cm, arg1);
    *reinterpret_cast<float*>(cm + 0x4c) = saved4c;
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
        call GetSmoothMenuRainbow
        add esp, 4
        mov edi, OFFSET g_rbTmp
        mov ecx, 0x4b1c8e
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b1d5a() {
    __asm {
        push edi
        call GetSmoothMenuRainbow
        add esp, 4
        mov edx, eax
        mov ecx, 0x4b1d70
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b1dfd() {
    __asm {
        push edi
        call GetSmoothMenuRainbow
        add esp, 4
        mov ecx, 0x4b1e13
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b1f6a() {
    __asm {
        push edi
        call GetSmoothMenuRainbow
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
        push ecx
        push edi
        call GetLsThumbY
        add esp, 4
        pop ecx
        mov edx, eax
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

static uint32_t __cdecl LsTileBadgeRainbow_Impl(int32_t frame, uint32_t tileIdx) {
    float f = (static_cast<float>(frame) + g_uiAlpha) / 3.0f - static_cast<float>(tileIdx >> 1) + 130.0f;
    return CalcSmoothRainbow(f, 1.0f);
}
static void __declspec(naked) hk_lsTileBadgeRainbow() {
    __asm {
        push ebx
        push DWORD PTR [ebp - 0x54]
        call LsTileBadgeRainbow_Impl
        add esp, 8
        mov edx, eax
        push esi
        mov eax, 0x4b828a
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
        call GetSmoothMenuRainbow
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
        __asm { call GetSmoothMenuRainbow } \
        __asm { add esp, 4 } \
        __asm { mov ecx, eax } \
        __asm { mov eax, ResumeImm } \
        __asm { jmp eax } \
    }

#define DEFINE_RB_CEG_EAX_HOOK(Name, ResumeImm) \
    static void __declspec(naked) Name() { \
        __asm { push edi } \
        __asm { call GetSmoothMenuRainbow } \
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
    if (width <= 0) return 0;
    double eff = static_cast<double>(GetDebEff44(deb));
    double f = eff * 4.0 - 76.0;
    if (f <= 0.0) return 0;
    double rem = std::fmod(f, static_cast<double>(width));
    int32_t d = static_cast<int32_t>(std::floor(rem));
    if (d < 0) d = 0;
    if (d >= width) d %= width;
    return d;
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
        call GetSmoothMenuRainbow
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

    uint32_t col = CalcSmoothRainbow(GetSmoothWsFrame(ws), 1.0f);
    if (slot == 1 && ws[0x48] != 0) {
        col = ((*reinterpret_cast<const uint32_t*>(ws + 0x4) & 1u) != 0) ? 0x999999u : 0xaaaaaau;
    }
    if (*reinterpret_cast<const int32_t*>(ws + slot * 4 + 0x78) != 0x55da && rem < 1.0f) {
        col = Game_ColorLerp(0xffffff, col, ClampPct0_100(rem * 100.0f));
    }
    *outCol = col;
}

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

static void __cdecl RenderHudTimerOverlay(uint8_t* renderer, uint8_t* ws, uint32_t expectedState) {
    uint8_t mode = g_timerMode;
    float size = g_timerSize;
    uint8_t pos = g_timerPos;
    bool forceSample = false;
    bool hasMenuPreview = GetHudTimerMenuPreview(expectedState, &mode, &size, &pos, &forceSample);
    if (expectedState == 9u && !hasMenuPreview) return;
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
    int rank = 5;
    int32_t mmMaxAlpha = 255;

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
        double effNet = effEl - effEd;
        if (effNet < 0.0) effNet = 0.0;

        uint32_t th[5];
        for (int i = 0; i < 5; ++i) {
            th[i] = *reinterpret_cast<const uint32_t*>(level + 0x80 + i * 4);
        }

        rank = 0;
        remTicks = 0.0;
        barFrac = 0.0;
        for (int i = 0; i < 5; ++i) {
            double hi = static_cast<double>(th[i]);
            if (effNet <= hi) {
                rank = 5 - i;
                remTicks = hi - effNet;
                double lo = (i == 0) ? 0.0 : static_cast<double>(th[i - 1]);
                double span = hi - lo;
                if (span <= 0.0) span = hi;
                barFrac = (span > 0.0) ? std::clamp(remTicks / span, 0.0, 1.0) : 0.0;
                break;
            }
        }

        mmMaxAlpha = *reinterpret_cast<const int32_t*>(ws + 0x210c);
        if (mmMaxAlpha <= 0 || mmMaxAlpha > 255) mmMaxAlpha = 255;
    }

    uint32_t rankRgb = 0xff5555u;
    if (rank >= 4) {
        rankRgb = CalcSmoothRainbow(static_cast<float>(GetHudTimerClockSec() * 22.0), 1.0f);
    } else if (rank == 3) {
        rankRgb = 0x55ff77u;
    } else if (rank == 2) {
        rankRgb = 0x55ccffu;
    } else if (rank == 1) {
        rankRgb = 0xffaa33u;
    }
    uint8_t fgAlpha = (forceSample && g_timerMode == 0 && expectedState == 9u) ? 140u : 255u;
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
    constexpr float kMargin = 14.0f;
    float scaledW = static_cast<float>(kPanelW) * s;
    float scaledH = static_cast<float>(panelH) * s;

    float ox = kMargin;
    float oy = kMargin;
    if (pos == 1 || pos == 3) {
        ox = static_cast<float>(virtW) - kMargin - scaledW;
    }
    if (pos == 2 || pos == 3) {
        oy = static_cast<float>(virtH) - kMargin - scaledH;
    }
    float maxX = (std::max)(0.0f, static_cast<float>(virtW) - scaledW);
    float maxY = (std::max)(0.0f, static_cast<float>(virtH) - scaledH);
    ox = std::clamp(ox, 0.0f, maxX);
    oy = std::clamp(oy, 0.0f, maxY);

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

    uint8_t bgAlpha = static_cast<uint8_t>(std::lround((static_cast<double>(mmMaxAlpha) / 255.0) * 191.0 * (static_cast<double>(fgAlpha) / 255.0)));
    uint8_t brdAlpha = static_cast<uint8_t>((48u * fgAlpha) / 255u);
    uint8_t trkAlpha = static_cast<uint8_t>((42u * fgAlpha) / 255u);
    uint32_t bgRgba     = MakeRgba(0x21, 0x21, 0x21, bgAlpha);
    uint32_t borderRgba = MakeRgba(255, 255, 255, brdAlpha);
    uint32_t trackRgba  = MakeRgba(255, 255, 255, trkAlpha);
    Game_DrawQuad(0, 0, kPanelW, panelH, &bgRgba);
    Game_DrawQuad(0, 0, kPanelW, 1, &borderRgba);
    Game_DrawQuad(0, panelH - 1, kPanelW, 1, &borderRgba);
    Game_DrawQuad(0, 1, 1, panelH - 2, &borderRgba);
    Game_DrawQuad(kPanelW - 1, 1, 1, panelH - 2, &borderRgba);

    if (showElapsed) {
        wchar_t elBuf[32];
        EdgeWString elWs;
        FormatTicksWstr(effEl, elBuf, 32, &elWs);
        *pTextCol = MakeRgba(255, 255, 255, fgAlpha);
        Game_DrawText(&elWs, kPadX, elY, 0x2000, 0x2000, 1, 0, 0x14);
    }

    if (showRank) {
        wchar_t remBuf[32];
        EdgeWString remWs;
        FormatTicksWstr(remTicks, remBuf, 32, &remWs);
        *pTextCol = rankRgba;
        Game_DrawText(&remWs, kPadX, rkY, 0x2000, 0x2000, 1, 0, 0x14);
    }

    {
        static const wchar_t* const kRankTexts[6] = {L"D", L"C", L"B", L"A", L"S", L"S+"};
        EdgeWString rankWs;
        rankWs.set_literal(kRankTexts[rank], (rank == 5) ? 2u : 1u);
        *pTextCol = rankRgba;
        int rankTextY = showRank ? rkY : elY;
        Game_DrawText(&rankWs, kPanelW - kPadX, rankTextY, 0x2000, 0x2000, 1, 0, 0x18);
    }

    Game_DrawQuad(kPadX, barY, kBarW, kBarH, &trackRgba);
    int fillW = static_cast<int>(std::lround(barFrac * static_cast<double>(kBarW)));
    if (fillW < 0) fillW = 0;
    if (fillW > kBarW) fillW = kBarW;
    if (fillW > 0) {
        Game_DrawQuad(kPadX, barY, fillW, kBarH, &rankRgba);
    }

    *pTextCol = savedTextCol;
    Game_SetBlend(renderer, 0);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f610)(mstack);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);
    stackMode = savedMode;
}

static void __declspec(naked) hk_worldEnd2DHudTimer() {
    __asm {
        pushfd
        pushad
        push 0x11
        push edi
        push ecx
        call RenderHudTimerOverlay
        add esp, 12
        popad
        popfd
        mov eax, 0x406690
        jmp eax
    }
}

static void __cdecl RenderHudTimerPause_Impl(uint8_t* renderer) {
    RenderHudTimerOverlay(renderer, GetWorldScreen(), 0x12u);
    RenderHudTimerOverlay(renderer, GetWorldScreen(), 9u);
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

void RegisterUiSmoothPart2() {
    // ScoresMenu & CreditsMenu vtable[2]
    RegU32(0x58d120, 0x4af660, reinterpret_cast<uint32_t>(&ScoresRenderWrap_Impl));
    RegU32(0x58b9d8, 0x4a1a70, reinterpret_cast<uint32_t>(&CreditsRenderWrap_Impl));

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
        RegRelJmp(0x4b825f, exBadge, 12, 5, &hk_lsTileBadgeRainbow, 0x90, 0x4b828a);

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
        // Note: 0x4aaf17 needs `mov [ebp-0xdc], ecx; lea edi, [eax+0xa]; cmp [edx+0x44], 19; jl skip; call hk_debNrMarquee`
        // Let's write a small naked bridge `hk_debNrMarqueeGate` via RegRelJmp at 0x4aaf17!
        RegRelJmp(0x4aaf17, exNrM, 8, 5, &hk_debNrMarquee, 0x90, 0x4aaf37);

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
