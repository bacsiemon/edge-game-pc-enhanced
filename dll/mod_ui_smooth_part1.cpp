#include "mod_smooth_shared.h"
#include <cmath>

namespace edgemod {

float    g_uiAlpha      = 0.0f;
uint8_t  g_justTicked   = 0;
uint8_t* g_subPtr       = nullptr;
int32_t  g_subPrev4     = 0;
int32_t  g_subPrev8     = 0;
int32_t  g_subPrev4c    = 0;
int32_t  g_endPrev3c    = 0;
int32_t  g_endPrev40    = 0;
uint8_t* g_wsPtr        = nullptr;
int32_t  g_wsPrev4      = 0;
int32_t  g_wsPrev2108   = 0;
int32_t  g_wsTgt2108    = 0;
int32_t  g_wsBuilt2108  = -1;
uint8_t  g_wsMmInterp   = 0;
uint32_t g_wsPrevTst2b  = 0;
int32_t  g_wsPrevTst34  = 0;
uint32_t g_rbTmp        = 0;
int32_t  g_msPrevC      = 0;
int32_t  g_subPrev50    = 0;
MsModelTracker g_mmTrackers[4]{};

static FloatTracker g_trkPp304_4c{};
static FloatTracker g_trkPp304_68{};
static FloatTracker g_trkSm34{};
static float        g_camPrevFov = 22.0f;
static float        g_camTgtFov  = 22.0f;
static bool         g_camFovValid = false;

void __cdecl GetActiveSub(uint8_t* sm, uint8_t** outSub, uint32_t* outVtable) {
    uint8_t* ms = *reinterpret_cast<uint8_t**>(sm + 0x4c);
    if (ms != nullptr && *reinterpret_cast<uint32_t*>(ms) == 0x58af10) {
        uint32_t idx = *reinterpret_cast<uint32_t*>(sm + 0x50);
        if (idx <= 0x17) {
            uint8_t* sub = *reinterpret_cast<uint8_t**>(ms + idx * 8 + 0x44);
            if (sub != nullptr) {
                *outSub = sub;
                *outVtable = *reinterpret_cast<uint32_t*>(sub);
                return;
            }
        }
    }
    *outSub = nullptr;
    *outVtable = 0;
}

float __cdecl SmoothCounterPair(int32_t cur, int32_t prev, bool isOut) {
    if (isOut) {
        if (cur < 0 || prev < 0) return static_cast<float>(cur);
    } else {
        if (cur < prev) return static_cast<float>(cur);
    }
    if (cur == prev) return static_cast<float>(prev);
    return static_cast<float>(prev) + static_cast<float>(cur - prev) * g_uiAlpha;
}

int32_t __cdecl ClampPct0_100(float val) {
    if (val < 0.0f) val = 0.0f;
    if (val > 100.0f) val = 100.0f;
    return static_cast<int32_t>(std::lrint(val));
}

int32_t __cdecl DivPct8(float val) {
    return ClampPct0_100(val * 12.5f);
}

int32_t __cdecl DivPct4M4(float val) {
    return ClampPct0_100((val - 4.0f) * 25.0f);
}

int32_t __cdecl CalcSubFadePct(const uint8_t* sub, int32_t mode) {
    int32_t cur4 = *reinterpret_cast<const int32_t*>(sub + 0x4);
    if (cur4 < 8) {
        int32_t prev4 = (g_subPtr == sub) ? g_subPrev4 : cur4;
        float smVal = SmoothCounterPair(cur4, prev4, false);
        uint8_t* sm = GetScreenMgr();
        int32_t prevScreen = sm ? *reinterpret_cast<int32_t*>(sm + 0x54) : 0;
        if (mode == 1) {
            return (prevScreen == 2) ? DivPct8(smVal) : DivPct4M4(smVal);
        }
        if (mode == 2) {
            if (prevScreen == 1 || prevScreen == 0x12 || (prevScreen >= 0x14 && prevScreen <= 0x16)) {
                return DivPct8(smVal);
            }
            return DivPct4M4(smVal);
        }
        return DivPct8(smVal);
    }

    int32_t cur8 = *reinterpret_cast<const int32_t*>(sub + 0x8);
    if (cur8 < 0) return 100;
    int32_t curC = *reinterpret_cast<const int32_t*>(sub + 0xc);
    if (mode == 3 && curC != 3) return 100;

    int32_t prev8 = (g_subPtr == sub) ? g_subPrev8 : cur8;
    float smVal = SmoothCounterPair(cur8, prev8, true);
    if (mode == 4) return ClampPct0_100(smVal * 25.0f);
    if (mode == 2) return DivPct4M4(smVal);
    if (mode == 1) {
        return (curC == 2) ? DivPct8(smVal) : DivPct4M4(smVal);
    }
    return DivPct8(smVal);
}

float __cdecl GetSmoothWsFrame(const uint8_t* ws) {
    uint32_t cur = *reinterpret_cast<const uint32_t*>(ws + 0x4);
    if (g_wsPtr == ws && cur > static_cast<uint32_t>(g_wsPrev4)) {
        return static_cast<float>(static_cast<uint32_t>(g_wsPrev4)) + g_uiAlpha;
    }
    return static_cast<float>(cur);
}

uint32_t __cdecl CalcSmoothRainbow(float frame, float ticksPerColor) {
    const uint32_t* pal = reinterpret_cast<const uint32_t*>(0x589bb8);
    float stepsF = frame / ticksPerColor;
    int32_t totalSteps = static_cast<int32_t>(stepsF);
    float frac = stepsF - static_cast<float>(totalSteps);
    int32_t pct = static_cast<int32_t>(std::lrint(frac * 100.0f));
    uint32_t idx = static_cast<uint32_t>(totalSteps) % 13u;
    uint32_t nextIdx = (idx + 1u < 13u) ? (idx + 1u) : 0u;
    return Game_ColorLerp(pal[idx], pal[nextIdx], pct);
}

uint32_t __cdecl GetSmoothMenuRainbow(int32_t frame) {
    uint32_t col = CalcSmoothRainbow(static_cast<float>(frame + 130) + g_uiAlpha, 1.0f);
    g_rbTmp = col;
    return col;
}

} // namespace edgemod

namespace {

using namespace edgemod;

// ------------------------------------------------------------------
// Float trackers & frame savePrev / interpolate hooks
// ------------------------------------------------------------------
static float* ResolveSmEntry(uint8_t* sm, uint16_t baseOff, uint16_t valOff) {
    if (baseOff == 0) {
        return reinterpret_cast<float*>(sm + valOff);
    }
    uint8_t* subObj = *reinterpret_cast<uint8_t**>(sm + baseOff);
    if (subObj == nullptr) return nullptr;
    return reinterpret_cast<float*>(subObj + valOff);
}

static void SaveFloatTracker(float* ptr, FloatTracker* trk) {
    if (ptr == nullptr) {
        trk->ptr = nullptr;
        return;
    }
    float val = *ptr;
    if (trk->ptr == ptr && std::memcmp(&val, &trk->last, 4) == 0) {
        val = trk->tgt;
        *ptr = val;
    }
    trk->ptr = ptr;
    trk->prev = val;
    trk->tgt = val;
    trk->last = val;
}

static bool InterpFloatTracker(float* ptr, FloatTracker* trk) {
    if (ptr == nullptr) {
        trk->ptr = nullptr;
        return false;
    }
    float cur = *ptr;
    if (trk->ptr == ptr) {
        if (g_justTicked) {
            trk->tgt = cur;
        } else if (std::memcmp(&cur, &trk->last, 4) != 0) {
            trk->ptr = ptr;
            trk->prev = cur;
            trk->tgt = cur;
            trk->last = cur;
            return false;
        }
    } else {
        trk->ptr = ptr;
        trk->prev = cur;
        trk->tgt = cur;
        trk->last = cur;
        return false;
    }
    if (std::memcmp(&trk->prev, &trk->tgt, 4) == 0) {
        trk->last = trk->prev;
        return false;
    }
    float mixed = trk->prev + (trk->tgt - trk->prev) * g_uiAlpha;
    *ptr = mixed;
    trk->last = mixed;
    return true;
}

static void SavePrevMS() {
    uint8_t* ms = GetMenuScreen();
    if (ms == nullptr) {
        for (int i = 0; i < 4; ++i) {
            g_mmTrackers[i].animNode = nullptr;
            g_mmTrackers[i].nodeTimer = nullptr;
        }
        return;
    }
    g_msPrevC = *reinterpret_cast<int32_t*>(ms + 0xc);
    if (g_subPtr != nullptr && *reinterpret_cast<uint32_t*>(g_subPtr) == 0x58d51c /* VT_LEVELSELECT */) {
        g_subPrev50 = *reinterpret_cast<int32_t*>(g_subPtr + 0x50);
    }
    for (int i = 0; i < 4; ++i) {
        uint8_t* node = *reinterpret_cast<uint8_t**>(ms + 0x120 + i * 8);
        g_mmTrackers[i].animNode = node;
        g_mmTrackers[i].nodeTimer = nullptr;
        if (node != nullptr) {
            float t = *reinterpret_cast<float*>(node + 0x20);
            g_mmTrackers[i].prevAnim = t;
            g_mmTrackers[i].tgtAnim = t;
            uint8_t* model = *reinterpret_cast<uint8_t**>(node);
            if (model != nullptr) {
                uint8_t* nt = *reinterpret_cast<uint8_t**>(model + 0xdc);
                if (nt != nullptr) {
                    g_mmTrackers[i].nodeTimer = nt;
                    float ntVal = *reinterpret_cast<float*>(nt + 0x10);
                    g_mmTrackers[i].prevNodeT = ntVal;
                    g_mmTrackers[i].tgtNodeT = ntVal;
                }
            }
        }
    }
}

static void InterpolateMS(uint8_t* sm) {
    uint8_t* ms = GetMenuScreen();
    if (ms == nullptr || *reinterpret_cast<uint8_t**>(sm + 0x4c) != ms) return;
    if (*reinterpret_cast<uint32_t*>(sm + 0x50) >= 0x10) return;

    void* mstack = GetMatrixStack();
    for (int i = 0; i < 4; ++i) {
        uint8_t* node = *reinterpret_cast<uint8_t**>(ms + 0x120 + i * 8);
        if (node == nullptr || g_mmTrackers[i].animNode != node) continue;
        uint8_t* nt = g_mmTrackers[i].nodeTimer;
        if (nt != nullptr && g_justTicked) {
            uint8_t* model = *reinterpret_cast<uint8_t**>(node);
            if (model != nullptr && *reinterpret_cast<uint8_t**>(model + 0xdc) == nt) {
                g_mmTrackers[i].tgtNodeT = *reinterpret_cast<float*>(nt + 0x10);
            }
        }
        void* spline = *reinterpret_cast<void**>(node + 0x8);
        if (spline == nullptr) continue;
        float curT = *reinterpret_cast<float*>(node + 0x20);
        if (g_justTicked) g_mmTrackers[i].tgtAnim = curT;
        float prev = g_mmTrackers[i].prevAnim;
        float tgt  = g_mmTrackers[i].tgtAnim;
        if (prev == tgt) continue;
        float diff = tgt - prev;
        float evalT = (diff <= 10.0f && diff >= -10.0f) ? (prev + diff * g_uiAlpha) : tgt;

        reinterpret_cast<void(__thiscall*)(void*, float)>(0x432390)(spline, evalT);
        reinterpret_cast<void(__thiscall*)(void*)>(0x41f750)(mstack);
        reinterpret_cast<void(__thiscall*)(void*, void*)>(0x41f4d0)(mstack, node + 0x54);
        uint8_t* model = *reinterpret_cast<uint8_t**>(node);
        if (model != nullptr) {
            auto** vt = *reinterpret_cast<void***>(model);
            reinterpret_cast<void(__thiscall*)(void*, void*, void*)>(vt[0xc / 4])(model, node + 0x8, node);
        }
        reinterpret_cast<void(__thiscall*)(void*)>(0x41f610)(mstack);
    }
}

static void __cdecl OnSavePrev_Impl(uint8_t* sm) {
    SaveFloatTracker(ResolveSmEntry(sm, 0x304, 0x4c), &g_trkPp304_4c);
    SaveFloatTracker(ResolveSmEntry(sm, 0x304, 0x68), &g_trkPp304_68);
    SaveFloatTracker(ResolveSmEntry(sm, 0, 0x34),     &g_trkSm34);
    g_camPrevFov = *reinterpret_cast<float*>(sm + 0x8c);
    g_camTgtFov  = g_camPrevFov;
    g_camFovValid = true;

    uint8_t* ws = GetWorldScreen();
    if (ws != nullptr && ws[0x2120] != 0) {
        if (g_wsPtr != ws) {
            g_wsPtr = ws;
            g_wsBuilt2108 = -1;
        }
        g_wsPrev4 = *reinterpret_cast<int32_t*>(ws + 0x4);
        g_wsPrev2108 = *reinterpret_cast<int32_t*>(ws + 0x2108);
        g_wsTgt2108 = g_wsPrev2108;
        g_wsPrevTst2b = ws[0x2158 + 0x2b];
        g_wsPrevTst34 = *reinterpret_cast<int32_t*>(ws + 0x2158 + 0x34);
    } else {
        g_wsPtr = nullptr;
    }

    uint8_t* sub = nullptr;
    uint32_t vt = 0;
    GetActiveSub(sm, &sub, &vt);
    g_subPtr = sub;
    if (sub != nullptr) {
        g_subPrev4 = *reinterpret_cast<int32_t*>(sub + 0x4);
        g_subPrev8 = *reinterpret_cast<int32_t*>(sub + 0x8);
        if (vt == 0x58cf44 /* PauseMenu */) {
            g_subPrev4c = *reinterpret_cast<int32_t*>(sub + 0x4c);
            g_pauseSubSwipePrevX = g_pauseSubSwipeX;
        } else if (vt == 0x58ce58 /* EndLevel/Debrief */) {
            g_endPrev3c = *reinterpret_cast<int32_t*>(sub + 0x3c);
            g_endPrev40 = *reinterpret_cast<int32_t*>(sub + 0x40);
        }
    }

    SavePrevMS();
    OnSavePrev_Ext();
    g_justTicked = 1;
}

static void __declspec(naked) hk_onSavePrev() {
    __asm {
        push ecx
        push edx
        push esi
        call OnSavePrev_Impl
        add esp, 4
        pop edx
        pop ecx
        mov eax, DWORD PTR [esi + 0x330]
        ret
    }
}

static void __cdecl OnInterpolate_Impl(uint8_t* sm, float alpha) {
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    g_uiAlpha = alpha;

    InterpFloatTracker(ResolveSmEntry(sm, 0x304, 0x4c), &g_trkPp304_4c);
    InterpFloatTracker(ResolveSmEntry(sm, 0x304, 0x68), &g_trkPp304_68);
    bool sm34Chg = InterpFloatTracker(ResolveSmEntry(sm, 0, 0x34), &g_trkSm34);

    if (sm34Chg) {
        float scale = *reinterpret_cast<float*>(sm + 0x34);
        *reinterpret_cast<int32_t*>(sm + 0x2c) = static_cast<int32_t>(
            static_cast<float>(*reinterpret_cast<int32_t*>(sm + 0x24)) * scale
        );
        *reinterpret_cast<int32_t*>(sm + 0x30) = static_cast<int32_t>(
            static_cast<float>(*reinterpret_cast<int32_t*>(sm + 0x28)) * scale
        );
    }

    float curFov = *reinterpret_cast<float*>(sm + 0x8c);
    if (!g_camFovValid) {
        g_camPrevFov = curFov;
        g_camTgtFov = curFov;
        g_camFovValid = true;
    } else if (g_justTicked) {
        g_camTgtFov = curFov;
    } else if (std::memcmp(&curFov, &g_camTgtFov, 4) != 0) {
        g_camPrevFov = curFov;
        g_camTgtFov = curFov;
    }
    bool fovChg = (std::memcmp(&g_camPrevFov, &g_camTgtFov, 4) != 0);

    uint8_t* ws = GetWorldScreen();
    if (ws != nullptr && ws[0x2120] != 0) {
        if (g_justTicked || g_wsPtr != ws) {
            g_wsTgt2108 = *reinterpret_cast<int32_t*>(ws + 0x2108);
        }
        bool orthoMismatch = false;
        uint32_t curIdx = *reinterpret_cast<uint32_t*>(sm + 0x330);
        if (curIdx <= 1u) {
            uint32_t prevIdx = curIdx ^ 1u;
            const uint8_t* slotCur  = sm + (curIdx * 3u + 0xc3u) * 4u;
            const uint8_t* slotPrev = sm + (prevIdx * 3u + 0xc3u) * 4u;
            const uint8_t* camCur   = *reinterpret_cast<const uint8_t* const*>(slotCur);
            const uint8_t* camPrev  = *reinterpret_cast<const uint8_t* const*>(slotPrev);
            if (camCur != nullptr && camPrev != nullptr) {
                bool prevOrtho = (*reinterpret_cast<const float*>(camPrev + 0x1c0) <= kOrthoSentinelRadThreshold);
                bool curOrtho  = (*reinterpret_cast<const float*>(camCur + 0x1c0) <= kOrthoSentinelRadThreshold);
                orthoMismatch = (prevOrtho != curOrtho);
            }
        }
        if (fovChg || sm34Chg || orthoMismatch) {
            if (curIdx <= 1u) {
                uint32_t prevIdx = curIdx ^ 1u;
                const uint8_t* slotCur  = sm + (curIdx * 3u + 0xc3u) * 4u;
                const uint8_t* slotPrev = sm + (prevIdx * 3u + 0xc3u) * 4u;
                uint8_t* renderCam = *reinterpret_cast<uint8_t**>(sm + 0x324);
                // Match 0x4a0370's guard: only interpolate when no camera cut/teleport occurred
                if (renderCam != nullptr && slotPrev[8] != 0 && slotPrev[9] != 0 && slotCur[9] != 0) {
                    uint8_t* seg = (ws[0xb5] != 0) ? (ws + 0xe0) : (sm + 0x78);
                    float sFov = *reinterpret_cast<float*>(seg + 0);
                    float tFov = *reinterpret_cast<float*>(seg + 4);
                    bool segActive = (sFov != tFov);
                    float fovAlpha = (fovChg && segActive)
                        ? (g_camPrevFov + (g_camTgtFov - g_camPrevFov) * alpha)
                        : curFov;

                    float savedLookAt[3];
                    std::memcpy(savedLookAt, ws + 0x1c, 12);
                    // Use 0x4a0370's already-interpolated lookAt so cube follow / pause pan stay smooth
                    std::memcpy(ws + 0x1c, renderCam + 0x208, 12);
                    *reinterpret_cast<float*>(sm + 0x8c) = fovAlpha;
                    *reinterpret_cast<uint32_t*>(sm + 0x330) = 2u; // 2*3 + 0xc3 = 0xc9 -> sm + 0x324 (renderCam)
                    g_inRenderCamInterp = true;
                    reinterpret_cast<void(__thiscall*)(void*)>(0x47b8d0)(ws);
                    g_inRenderCamInterp = false;
                    *reinterpret_cast<uint32_t*>(sm + 0x330) = curIdx;
                    *reinterpret_cast<float*>(sm + 0x8c) = curFov;
                    std::memcpy(ws + 0x1c, savedLookAt, 12);
                }
            }
        }
    }

    InterpolateMS(sm);
    OnInterpolate_Ext(sm);
    g_justTicked = 0;
}

static void __declspec(naked) hk_onInterpolate() {
    __asm {
        call eax
        push DWORD PTR [ebp + 0x8]
        push esi
        call OnInterpolate_Impl
        add esp, 8
        pop edi
        pop esi
        pop ebp
        ret 4
    }
}

// ------------------------------------------------------------------
// PauseMenu, EndLevel, PauseConfirm, ModelNode, Scroll & Submenu helpers
// ------------------------------------------------------------------
static int32_t __cdecl PauseSmoothSlideY_Impl(const uint8_t* pm) {
    int32_t cur8 = *reinterpret_cast<const int32_t*>(pm + 0x8);
    float pct = 100.0f;
    if (cur8 >= 0) {
        int32_t prev8 = (g_subPtr == pm) ? g_subPrev8 : cur8;
        float sm = SmoothCounterPair(cur8, prev8, true);
        pct = (4.0f - sm) * 25.0f;
    } else {
        int32_t cur4 = *reinterpret_cast<const int32_t*>(pm + 0x4);
        if (cur4 >= 5) return 0;
        int32_t prev4 = (g_subPtr == pm) ? g_subPrev4 : cur4;
        float sm = SmoothCounterPair(cur4, prev4, false);
        pct = (5.0f - sm) * 20.0f;
    }
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return static_cast<int32_t>(std::lrint((pct * -223.0f) / 100.0f));
}

static float __cdecl PauseSmoothX_Impl(const uint8_t* pm) {
    int32_t cur4c = *reinterpret_cast<const int32_t*>(pm + 0x4c);
    float x = static_cast<float>(cur4c);
    if (g_subPtr == pm && cur4c != g_subPrev4c) {
        x = static_cast<float>(g_subPrev4c) + static_cast<float>(cur4c - g_subPrev4c) * g_uiAlpha;
    }
    return -x;
}

static void __declspec(naked) hk_pauseSmoothX_P0() {
    __asm {
        push ecx
        push eax
        call PauseSmoothX_Impl
        add esp, 4
        sub esp, 4
        fstp DWORD PTR [esp]
        movss xmm2, DWORD PTR [esp]
        add esp, 4
        pop ecx
        movss xmm1, DWORD PTR ds:[0x577c74] // 1.0f
        xorps xmm0, xmm0
        mov eax, 0x4ad20e
        jmp eax
    }
}

static void __declspec(naked) hk_pauseSmoothX_P1() {
    __asm {
        push ecx
        push edx
        push edi
        call PauseSmoothX_Impl
        add esp, 4
        sub esp, 4
        fstp DWORD PTR [esp]
        movss xmm2, DWORD PTR [esp]
        add esp, 4
        pop edx
        pop ecx
        movss xmm1, DWORD PTR ds:[0x577c74]
        xorps xmm0, xmm0
        lea ecx, [ebp - 0xa4]
        push ecx
        mov ecx, DWORD PTR [ebp - 0x50]
        mov eax, 0x4ad4a9
        jmp eax
    }
}

static void __declspec(naked) hk_pauseP2X() {
    __asm {
        push eax
        push ecx
        push edx
        push edi
        call PauseSmoothX_Impl
        add esp, 4
        sub esp, 4
        fstp DWORD PTR [esp]
        movss xmm2, DWORD PTR [esp]
        add esp, 4
        pop edx
        pop ecx
        pop eax
        xorps xmm0, xmm0
        movss xmm1, DWORD PTR ds:[0x577c74]
        ret
    }
}

static bool __cdecl PauseSkipPageCheck(const uint8_t* pm, int32_t expectedVal) {
    if (*reinterpret_cast<const int32_t*>(pm + 0x4c) != expectedVal) return false;
    if (g_subPtr != pm || g_subPrev4c == expectedVal) return true;
    return false;
}

static void __declspec(naked) hk_pauseP0Skip() {
    __asm {
        push 0x170
        push edi
        call PauseSkipPageCheck
        add esp, 8
        test al, al
        jne p0_skip
        mov eax, 0x4ad1ea
        jmp eax
    p0_skip:
        mov eax, 0x4ad44c
        jmp eax
    }
}

static void __declspec(naked) hk_pauseP1Skip() {
    __asm {
        push ebx
        push edi
        call PauseSkipPageCheck
        add esp, 8
        test al, al
        jne p1_skip
        mov eax, 0x4ad468
        jmp eax
    p1_skip:
        mov eax, 0x4adf61
        jmp eax
    }
}

static void __declspec(naked) hk_pauseP2Skip() {
    __asm {
        push ebx
        push edi
        call PauseSkipPageCheck
        add esp, 8
        test al, al
        jne p2_skip
        mov eax, 0x4adf7d
        jmp eax
    p2_skip:
        mov eax, 0x4ae25b
        jmp eax
    }
}

static void __declspec(naked) hk_pauseSmoothSlideY() {
    __asm {
        push edi
        call PauseSmoothSlideY_Impl
        add esp, 4
        xor ebx, ebx
        mov esi, eax
        mov eax, 0x4ad1a0
        jmp eax
    }
}

static void __cdecl PauseConfirmSmooth_Impl(const uint8_t* pc, int32_t* outY, int32_t* outFadePct) {
    int32_t cur4 = *reinterpret_cast<const int32_t*>(pc + 0x4);
    if (cur4 <= 3) {
        int32_t prev4 = (g_subPtr == pc) ? g_subPrev4 : cur4;
        float sm = SmoothCounterPair(cur4, prev4, false);
        float t = (3.0f - sm) / 3.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        *outY = static_cast<int32_t>(std::lrint(t * -44.0f));
    } else {
        *outY = 0;
    }

    int32_t cur8 = *reinterpret_cast<const int32_t*>(pc + 0x8);
    if (cur8 < 0) {
        *outFadePct = 100;
    } else {
        int32_t prev8 = (g_subPtr == pc) ? g_subPrev8 : cur8;
        *outFadePct = DivPct8(SmoothCounterPair(cur8, prev8, true));
    }
}

static void __declspec(naked) hk_pauseConfirmSmooth() {
    __asm {
        add esp, 0x14
        sub esp, 8
        lea eax, [esp + 4]
        push eax
        lea eax, [esp + 4]
        push eax
        push esi
        call PauseConfirmSmooth_Impl
        add esp, 12
        pop ebx
        pop edi
        mov eax, 0x4ae7e2
        jmp eax
    }
}

static uint8_t __cdecl EndFlashAlpha_Impl(const uint8_t* el) {
    int32_t cur3c = *reinterpret_cast<const int32_t*>(el + 0x3c);
    int32_t prev3c = (g_subPtr == el) ? g_endPrev3c : cur3c;
    float a = SmoothCounterPair(cur3c, prev3c, false) * 51.0f;
    if (a < 0.0f) a = 0.0f;
    if (a > 255.0f) a = 255.0f;
    return static_cast<uint8_t>(std::lrint(a));
}

static int32_t __cdecl EndFadePct_Impl(const uint8_t* el) {
    int32_t cur40 = *reinterpret_cast<const int32_t*>(el + 0x40);
    if (cur40 > 7) return 100;
    int32_t prev40 = (g_subPtr == el) ? g_endPrev40 : cur40;
    return ClampPct0_100(SmoothCounterPair(cur40, prev40, false) * (100.0f / 7.0f));
}

static void __declspec(naked) hk_endFlashAlpha() {
    __asm {
        add esp, 0x4
        cmp DWORD PTR [esi + 0x3c], 5
        jg efa_skip
        push esi
        call EndFlashAlpha_Impl
        add esp, 4
        mov cl, al
        mov eax, 0x4a9a0b
        jmp eax
    efa_skip:
        mov eax, 0x4a9a5f
        jmp eax
    }
}

static void __declspec(naked) hk_endFadePct() {
    __asm {
        push ebx
        push edi
        push esi
        call EndFadePct_Impl
        add esp, 4
        mov ebx, eax
        mov eax, 0x4a9a86
        jmp eax
    }
}

static int32_t __cdecl CalcSubSlideY_Impl(const uint8_t* sub, int32_t mode, int32_t rowArg) {
    int32_t cur8 = *reinterpret_cast<const int32_t*>(sub + 0x8);
    bool isOut = false;
    float sm = 0.0f;
    if (cur8 >= 0 && cur8 < 4 && (mode == 0 || *reinterpret_cast<const int32_t*>(sub + 0xc) != 2)) {
        isOut = true;
        int32_t prev8 = (g_subPtr == sub) ? g_subPrev8 : cur8;
        sm = SmoothCounterPair(cur8, prev8, true);
    } else {
        int32_t cur4 = *reinterpret_cast<const int32_t*>(sub + 0x4);
        if (cur4 >= 4) return 0;
        uint8_t* mgr = GetScreenMgr();
        int32_t prevScr = *reinterpret_cast<int32_t*>(mgr + 0x54);
        if (mode == 0) {
            if (prevScr == 1 || prevScr == 0x12 || (prevScr >= 0x14 && prevScr <= 0x16)) return 0;
        } else {
            if (prevScr == 2) return 0;
        }
        int32_t prev4 = (g_subPtr == sub) ? g_subPrev4 : cur4;
        sm = SmoothCounterPair(cur4, prev4, false);
    }

    float pctRem = (4.0f - sm);
    if (pctRem < 0.0f) pctRem = 0.0f;
    if (pctRem > 4.0f) pctRem = 4.0f;
    pctRem /= 4.0f;

    int32_t sel = *reinterpret_cast<const int32_t*>(sub + 0x10);
    int32_t base = 0;
    if (mode == 0) {
        base = sel * -50 - 82;
    } else if (mode == 2) {
        base = sel * -50 - 82;
    } else if ((mode == 1 && sel == 4) || (mode == 3 && sel == 4)) {
        int32_t fy = reinterpret_cast<int32_t(__cdecl*)()>(0x496060)();
        base = -fy + 0x3d;
    } else {
        int32_t r = (mode == 1) ? rowArg : sel;
        base = r * -50 - 82;
    }
    int32_t off = static_cast<int32_t>(static_cast<float>(base) * pctRem);
    if (!isOut) off += 4;
    return off;
}

static void __declspec(naked) hk_subSlide_MainMenu() {
    __asm {
        push edx
        push 0
        push 0
        push edi
        call CalcSubSlideY_Impl
        add esp, 12
        pop edx
        mov ecx, 0x4b99ab
        jmp ecx
    }
}

static void __declspec(naked) hk_subSlide_ModeSelect() {
    __asm {
        push edi
        push 1
        push ebx
        call CalcSubSlideY_Impl
        add esp, 12
        mov ecx, eax
        mov eax, 0x4b8f4a
        jmp eax
    }
}

static void __declspec(naked) hk_subSlide_Options() {
    __asm {
        push 0
        push 2
        push edi
        call CalcSubSlideY_Impl
        add esp, 12
        mov ecx, 0x4b299d
        jmp ecx
    }
}

static void __declspec(naked) hk_subSlide_s4b1aa0() {
    __asm {
        push 0
        push 3
        push ebx
        call CalcSubSlideY_Impl
        add esp, 12
        mov ecx, eax
        mov eax, 0x4b1b84
        jmp eax
    }
}

static void __cdecl InterpModelNodeT_Impl(const uint8_t* timerPtr, float* outT) {
    float cur = *reinterpret_cast<const float*>(timerPtr + 0x10);
    for (int i = 0; i < 4; ++i) {
        if (g_mmTrackers[i].nodeTimer == timerPtr) {
            float prev = g_mmTrackers[i].prevNodeT;
            if (prev == cur) break;
            if (cur < prev) {
                float minT = *reinterpret_cast<const float*>(timerPtr);
                float maxT = *reinterpret_cast<const float*>(timerPtr + 0x4);
                float span = maxT - minT;
                if (span <= 0.0f) break;
                float diff = cur + span - prev;
                if (diff > 10.0f) break;
                float val = prev + diff * g_uiAlpha;
                if (val >= maxT) val -= span;
                *outT = val;
                return;
            } else {
                float diff = cur - prev;
                if (diff > 10.0f) break;
                *outT = prev + diff * g_uiAlpha;
                return;
            }
        }
    }
    *outT = cur;
}

static void __declspec(naked) hk_interpModelNodeT() {
    __asm {
        push ecx
        push edx
        lea ecx, [ebp + 0x8]
        push ecx
        push eax
        call InterpModelNodeT_Impl
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

static int32_t __cdecl CalcMsScroll_Impl(const uint8_t* ms) {
    int32_t cur = *reinterpret_cast<const int32_t*>(ms + 0xc);
    if (ms == GetMenuScreen()) {
        int32_t diff = cur - g_msPrevC;
        if (diff >= 0 && diff <= 20) {
            return static_cast<int32_t>(std::lrint(static_cast<float>(g_msPrevC) + static_cast<float>(diff) * g_uiAlpha));
        }
    }
    return cur;
}

static void __declspec(naked) hk_msScrollSmooth() {
    __asm {
        sub eax, 0xb
        mov DWORD PTR [ebp + 0x8], eax
        push ecx
        push edx
        push edi
        call CalcMsScroll_Impl
        add esp, 4
        pop edx
        pop ecx
        mov esi, ecx
        sub esi, eax
        ret
    }
}

static int32_t __cdecl CalcLsScroll_Impl(const uint8_t* ls, int32_t row) {
    int32_t cur = *reinterpret_cast<const int32_t*>(ls + 0x50);
    if (g_subPtr == ls && cur != g_subPrev50) {
        cur = static_cast<int32_t>(std::lrint(
            static_cast<float>(g_subPrev50) + static_cast<float>(cur - g_subPrev50) * g_uiAlpha
        ));
    }
    return row * 0x2d - cur;
}

static void __declspec(naked) hk_lsScrollSmooth() {
    __asm {
        push ecx
        push edx
        push eax
        push edi
        call CalcLsScroll_Impl
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// ------------------------------------------------------------------
// WorldScreen HUD flash, rainbow triangle, Toast, Minimap
// ------------------------------------------------------------------
static uint8_t __cdecl HudFlashSmooth_Impl(uint8_t* ws, void* renderer, uint8_t* outRgba) {
    int32_t cur = *reinterpret_cast<int32_t*>(ws + 0x211c);
    if (cur <= 0) return 0;
    float eff = static_cast<float>(cur);
    if (g_wsPtr == ws) {
        eff -= g_uiAlpha;
        if (eff < 0.0f) eff = 0.0f;
        if (eff > 3.0f) eff = 3.0f;
    }
    float alphaF = (eff > 2.0f) ? ((eff - 2.0f) * 100.0f + 100.0f) : (eff * 50.0f);
    if (alphaF < 0.0f) alphaF = 0.0f;
    if (alphaF > 200.0f) alphaF = 200.0f;
    int32_t alphaU8 = static_cast<int32_t>(std::lrint(alphaF));
    if (alphaU8 <= 0) return 0;

    Game_SetBlend(renderer, 1);
    uint32_t rb = CalcSmoothRainbow(GetSmoothWsFrame(ws), 1.0f);
    uint32_t col = Game_ColorLerp(rb, 0xffffff, 0x4b);
    outRgba[0] = static_cast<uint8_t>((col >> 16) & 0xff);
    outRgba[1] = static_cast<uint8_t>((col >> 8) & 0xff);
    outRgba[2] = static_cast<uint8_t>(col & 0xff);
    outRgba[3] = static_cast<uint8_t>(alphaU8);
    return 1;
}

static void __declspec(naked) hk_hudFlashSmooth() {
    __asm {
        lea eax, [ebp - 0x8]
        push eax
        push ebx
        push edi
        call HudFlashSmooth_Impl
        add esp, 12
        test al, al
        je hfs_skip
        mov eax, 0x47b4ea
        jmp eax
    hfs_skip:
        mov eax, 0x47b507
        jmp eax
    }
}

static void __cdecl HudRainbowSmooth_Impl(const uint8_t* ws, uint32_t* outRgb, int32_t* outAlpha) {
    float fFrame = GetSmoothWsFrame(ws);
    *outRgb = CalcSmoothRainbow(fFrame, 10.0f);
    float mod200 = std::fmod(fFrame, 200.0f);
    if (mod200 < 0.0f) mod200 += 200.0f;
    float tri = (mod200 < 100.0f) ? mod200 : (200.0f - mod200);
    *outAlpha = static_cast<int32_t>(std::lrint(tri * 0.5f)) + 200;
}

static void __declspec(naked) hk_hudRainbowSmooth() {
    __asm {
        sub esp, 8
        lea eax, [esp + 4]
        push eax
        lea eax, [esp + 4]
        push eax
        push edi
        call HudRainbowSmooth_Impl
        add esp, 12
        pop ecx // rgb
        pop eax // alpha
        mov edx, 0x47b5af
        jmp edx
    }
}

static uint32_t __cdecl ToastAlphaSmooth_Impl(const uint8_t* toast) {
    bool active = (g_wsPtr != nullptr && toast == (g_wsPtr + 0x2158));
    if (*reinterpret_cast<const int32_t*>(toast + 0x34) <= 0) {
        if (!active || g_wsPrevTst34 <= 0 || g_wsPrevTst2b == 0) return 0;
    }
    uint32_t cur2b = toast[0x2b];
    float sm = static_cast<float>(cur2b);
    if (active && cur2b != g_wsPrevTst2b) {
        sm = static_cast<float>(g_wsPrevTst2b) + static_cast<float>(static_cast<int32_t>(cur2b) - static_cast<int32_t>(g_wsPrevTst2b)) * g_uiAlpha;
    }
    float outF = sm * (170.0f / 255.0f);
    if (outF < 0.0f) outF = 0.0f;
    if (outF > 255.0f) outF = 255.0f;
    return static_cast<uint32_t>(std::lrint(outF));
}

static void __declspec(naked) hk_toastAlphaSmooth() {
    __asm {
        push esi
        call ToastAlphaSmooth_Impl
        add esp, 4
        test eax, eax
        je tas_skip
        mov ecx, DWORD PTR ds:[0x5f88e0]
        mov DWORD PTR [ebp - 0x4], ecx
        mov BYTE PTR [ebp - 0x1], al
        push 0x18
        push 0
        mov eax, 0x4953c1
        jmp eax
    tas_skip:
        mov eax, 0x49540a
        jmp eax
    }
}

static void __cdecl MinimapSmoothDraw_Impl(uint8_t* ws) {
    int32_t cur2108 = *reinterpret_cast<int32_t*>(ws + 0x2108);
    if (g_wsPtr != ws || cur2108 != g_wsTgt2108) {
        g_wsPtr = ws;
        g_wsTgt2108 = cur2108;
        g_wsPrev2108 = cur2108;
    }
    int32_t drawAlpha = g_wsTgt2108;
    if (drawAlpha != g_wsPrev2108) {
        float aF = static_cast<float>(g_wsPrev2108) + static_cast<float>(drawAlpha - g_wsPrev2108) * g_uiAlpha;
        if (aF < 0.0f) aF = 0.0f;
        if (aF > 255.0f) aF = 255.0f;
        drawAlpha = static_cast<int32_t>(std::lrint(aF));
    }
    if (drawAlpha <= 0) return;

    uint8_t* sm = GetScreenMgr();
    int32_t sm34 = *reinterpret_cast<int32_t*>(sm + 0x34);
    if (drawAlpha != g_wsBuilt2108 || *reinterpret_cast<int32_t*>(ws + 0x1c0) != sm34) {
        g_wsMmInterp = 1;
        *reinterpret_cast<int32_t*>(ws + 0x2108) = drawAlpha;
        reinterpret_cast<void(__thiscall*)(void*, int)>(0x47e620)(ws, 0);
        g_wsMmInterp = 0;
    }
    *reinterpret_cast<int32_t*>(ws + 0x2108) = drawAlpha;
    reinterpret_cast<void(__thiscall*)(void*)>(0x47a960)(ws);
    *reinterpret_cast<int32_t*>(ws + 0x2108) = g_wsTgt2108;
}

static void __declspec(naked) hk_minimapSmoothDraw_bridge() {
    __asm {
        push edx
        push edi
        call MinimapSmoothDraw_Impl
        add esp, 4
        pop edx
        ret
    }
}

static void __declspec(naked) hk_mmSkipIn() {
    __asm {
        cmp BYTE PTR g_wsMmInterp, 0
        jne mmsi_skip
        mov edx, DWORD PTR [ebx + 0x2108]
        mov eax, 0x47e670
        jmp eax
    mmsi_skip:
        mov eax, 0x47e6a9
        jmp eax
    }
}

static void __declspec(naked) hk_mmSkipOut() {
    __asm {
        cmp BYTE PTR g_wsMmInterp, 0
        jne mmso_skip
        mov eax, DWORD PTR [ebx + 0x2108]
        mov edx, 0x47e69a
        jmp edx
    mmso_skip:
        mov eax, 0x47e6a9
        jmp eax
    }
}

static void __declspec(naked) hk_mmForceVb() {
    __asm {
        cmp BYTE PTR g_wsMmInterp, 0
        je mmfv_chk
        mov cl, 1
    mmfv_chk:
        test cl, cl
        je mmfv_done
        mov eax, DWORD PTR [ebx + 0x2108]
        mov g_wsBuilt2108, eax
    mmfv_done:
        mov edi, DWORD PTR ds:[0x5f9040]
        ret
    }
}

// ------------------------------------------------------------------
// Submenu fade pct & 21 rainbow site wrappers
// ------------------------------------------------------------------
#define DEFINE_SUB_FADE_SIMPLE_HOOK(Name, ModeImm, RegName, ResumeImm) \
    static void __declspec(naked) Name() { \
        __asm { push ModeImm } \
        __asm { push RegName } \
        __asm { call CalcSubFadePct } \
        __asm { add esp, 8 } \
        __asm { mov esi, eax } \
        __asm { mov eax, ResumeImm } \
        __asm { jmp eax } \
    }

DEFINE_SUB_FADE_SIMPLE_HOOK(hk_subFade_4af68a, 0, edi, 0x4af6ab)
DEFINE_SUB_FADE_SIMPLE_HOOK(hk_subFade_4b3df4, 0, ebx, 0x4b3e15)
DEFINE_SUB_FADE_SIMPLE_HOOK(hk_subFade_4b57b1, 0, ebx, 0x4b57d2)
DEFINE_SUB_FADE_SIMPLE_HOOK(hk_subFade_4b29a0, 1, edi, 0x4b2a13)
DEFINE_SUB_FADE_SIMPLE_HOOK(hk_subFade_4b8f4d, 1, ebx, 0x4b8fb1)
DEFINE_SUB_FADE_SIMPLE_HOOK(hk_subFade_4b99fc, 2, edi, 0x4b9a5e)

static void __declspec(naked) hk_subFade_4b7b54() {
    __asm {
        mov DWORD PTR [edi + 0x68], esi
        push 3
        push edi
        call CalcSubFadePct
        add esp, 8
        mov esi, eax
        mov eax, 0x4b7b7e
        jmp eax
    }
}

static void __cdecl Calc4b1b87(uint8_t* ebxSub, int32_t* outFadePct, uint32_t* outEbp64, int32_t* outEbp68) {
    *outEbp64 = 0xffffff;
    int32_t mode = 1;
    if (*reinterpret_cast<int32_t*>(ebxSub + 0x4) >= 8 &&
        *reinterpret_cast<int32_t*>(ebxSub + 0x8) >= 0 &&
        *reinterpret_cast<int32_t*>(ebxSub + 0xc) == 0x10) {
        *outEbp68 = 0;
        *outEbp64 = 0;
        mode = 4;
    }
    *outFadePct = CalcSubFadePct(ebxSub, mode);
}

static void __declspec(naked) hk_subFade_4b1b87() {
    __asm {
        sub esp, 4
        mov eax, esp
        lea ecx, [ebp - 0x68]
        push ecx
        lea ecx, [ebp - 0x64]
        push ecx
        push eax
        push ebx
        call Calc4b1b87
        add esp, 16
        pop esi
        mov eax, 0x4b1c1b
        jmp eax
    }
}

static uint32_t __cdecl GetSmoothSlotRainbow(int32_t frame) {
    uint32_t col = ApplyMenuSlotRainbowStyle(GetSmoothMenuRainbow(frame));
    g_rbTmp = col;
    return col;
}

static void __declspec(naked) hk_rb_4b9b06() {
    __asm {
        mov ebx, eax
        push DWORD PTR [ebp - 0x68]
        call GetSmoothSlotRainbow
        add esp, 4
        mov ecx, 0x4b9b21
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b9b8b() {
    __asm {
        push ebx
        call GetSmoothSlotRainbow
        add esp, 4
        mov ecx, eax
        mov eax, OFFSET g_rbTmp
        mov edx, 0x4b9ba8
        jmp edx
    }
}
static void __declspec(naked) hk_rb_4b9ca2() {
    __asm {
        push ebx
        call GetSmoothSlotRainbow
        add esp, 4
        mov ecx, eax
        mov edx, 0x4b9cb8
        jmp edx
    }
}
static void __declspec(naked) hk_rb_4b9002() {
    __asm {
        push DWORD PTR [ebp - 0x68]
        call GetSmoothSlotRainbow
        add esp, 4
        mov edx, eax
        mov edi, OFFSET g_rbTmp
        mov ecx, 0x4b9022
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b90e3() {
    __asm {
        mov ebx, eax
        push DWORD PTR [ebp - 0x68]
        call GetSmoothSlotRainbow
        add esp, 4
        mov ecx, eax
        mov edx, 0x4b90fe
        jmp edx
    }
}
static void __declspec(naked) hk_rb_4b9171() {
    __asm {
        push DWORD PTR [ebp - 0x68]
        call GetSmoothSlotRainbow
        add esp, 4
        mov edx, eax
        mov ecx, 0x4b918a
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b9238() {
    __asm {
        push DWORD PTR [ebp - 0x68]
        call GetSmoothSlotRainbow
        add esp, 4
        mov ecx, 0x4b9251
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b7bf2() {
    __asm {
        push DWORD PTR [ebp - 0x54]
        call GetSmoothMenuRainbow
        add esp, 4
        mov ebx, OFFSET g_rbTmp
        mov ecx, 0x4b7c12
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b7d4d() {
    __asm {
        push ecx
        call GetSmoothMenuRainbow
        add esp, 4
        mov edx, eax
        mov ecx, 0x4b7d63
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b7e1b() {
    __asm {
        push DWORD PTR [ebp - 0x54]
        call GetSmoothMenuRainbow
        add esp, 4
        mov ecx, 0x4b7e34
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b3e47() {
    __asm {
        add esp, 0x18
        push edi
        call GetSmoothMenuRainbow
        add esp, 4
        mov edi, OFFSET g_rbTmp
        cmp DWORD PTR [ebx + 0x4], 0x8
        mov ecx, 0x4b3e6b
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4af6e1() {
    __asm {
        add esp, 0x18
        push DWORD PTR [ebp - 0xa4]
        call GetSmoothMenuRainbow
        add esp, 4
        mov eax, OFFSET g_rbTmp
        cmp DWORD PTR [edi + 0x4], 0x8
        mov ecx, 0x4af704
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4b5813() {
    __asm {
        push edi
        call GetSmoothMenuRainbow
        add esp, 4
        mov ecx, eax
        mov ebx, OFFSET g_rbTmp
        mov edx, 0x4b5832
        jmp edx
    }
}
static void __declspec(naked) hk_rb_4b58d9() {
    __asm {
        push edi
        call GetSmoothMenuRainbow
        add esp, 4
        mov ecx, eax
        mov edx, 0x4b58ec
        jmp edx
    }
}
static void __declspec(naked) hk_rb_4b5957() {
    __asm {
        push edi
        call GetSmoothMenuRainbow
        add esp, 4
        mov ecx, eax
        mov edx, 0x4b596a
        jmp edx
    }
}
static void __declspec(naked) hk_rb_4ad2d8() {
    __asm {
        mov eax, DWORD PTR [ebp - 0x54]
        lea eax, [eax + edi + 3]
        push eax
        call GetSmoothSlotRainbow
        add esp, 4
        mov edx, eax
        mov ecx, 0x4ad2ef
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4ae11c() {
    __asm {
        mov eax, DWORD PTR [ebp - 0x54]
        add eax, 3
        push eax
        call GetSmoothSlotRainbow
        add esp, 4
        mov edx, eax
        mov ecx, 0x4ae132
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4ae14d() {
    __asm {
        mov eax, DWORD PTR [ebp - 0x54]
        add eax, 3
        push eax
        call GetSmoothSlotRainbow
        add esp, 4
        mov edx, eax
        mov ecx, 0x4ae163
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4ae27c() {
    __asm {
        mov eax, DWORD PTR [ebp - 0x54]
        inc eax
        push eax
        call GetSmoothMenuRainbow
        add esp, 4
        add esp, 0x14
        push 0x44
        push ebx
        push ebx
        push 0x3000
        mov BYTE PTR [ebp - 0x3d], 0xff
        push 0x3000
        mov edx, eax
        mov ecx, 0x4ae2a5
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4ae2f9() {
    __asm {
        mov eax, DWORD PTR [ebp - 0x54]
        add eax, 2
        push eax
        call GetSmoothMenuRainbow
        add esp, 4
        push 0x44
        push ebx
        push ebx
        push 0x8000
        mov BYTE PTR [ebp - 0x3d], 0xff
        push 0x8000
        add edi, 0x24
        mov edx, eax
        mov ecx, 0x4ae324
        jmp ecx
    }
}
static void __declspec(naked) hk_rb_4ae39c() {
    __asm {
        push ecx
        mov eax, DWORD PTR [ebp - 0x54]
        sub eax, ebx
        push eax
        call GetSmoothMenuRainbow
        add esp, 4
        pop ecx
        mov edx, eax
        mov edi, DWORD PTR [ebp - 0x60]
        mov eax, 0x4ae3b4
        jmp eax
    }
}

} // namespace

void RegisterUiSmoothPart1() {
    // Frame savePrev & interpolate
    {
        const uint8_t exSp[6] = {0x8b, 0x86, 0x30, 0x03, 0x00, 0x00};
        RegRelCall(0x481303, exSp, 6, 6, &hk_onSavePrev);
        const uint8_t exIp[8] = {0xff, 0xd0, 0x5f, 0x5e, 0x5d, 0xc2, 0x04, 0x00};
        RegRelJmp(0x48140b, exIp, 8, 8, &hk_onInterpolate, 0x90);
    }

    // Pause P2 X & Skip
    {
        const uint8_t exP2S[9] = {0x39, 0x5f, 0x4c, 0x0f, 0x84, 0xde, 0x02, 0x00, 0x00};
        RegRelJmp(0x4adf74, exP2S, 9, 9, &hk_pauseP2Skip, 0x90);
        const uint8_t exP2X[11] = {0x8b, 0x4f, 0x4c, 0xf3, 0x0f, 0x10, 0x0d, 0x74, 0x7c, 0x57, 0x00};
        RegRelCall(0x4adf88, exP2X, 11, 20, &hk_pauseP2X);
    }

    // ModelNode timer, MenuScreen scroll, LevelSelect scroll
    {
        const uint8_t exMnt[6] = {0xd9, 0x40, 0x10, 0xd9, 0x5d, 0x08};
        RegRelCall(0x425715, exMnt, 6, 6, &hk_interpModelNodeT);
        const uint8_t exMss[11] = {0x8b, 0xf1, 0x2b, 0x77, 0x0c, 0x83, 0xe8, 0x0b, 0x89, 0x45, 0x08};
        RegRelCall(0x49c52b, exMss, 11, 11, &hk_msScrollSmooth);
        const uint8_t exLss[6] = {0x6b, 0xc0, 0x2d, 0x2b, 0x47, 0x50};
        RegRelCall(0x4b80a3, exLss, 6, 6, &hk_lsScrollSmooth);
    }

    // Submenu selection bar vertical Y slides
    {
        const uint8_t exMm[10] = {0x8b, 0x47, 0x08, 0xc7, 0x45, 0xac, 0x00, 0x00, 0x00, 0x00};
        RegRelJmp(0x4b9901, exMm, 10, 5, &hk_subSlide_MainMenu, 0x90, 0x4b99ab);
        const uint8_t exMs[7] = {0x8b, 0x43, 0x08, 0x85, 0xc0, 0x78, 0x55};
        RegRelJmp(0x4b8e93, exMs, 7, 5, &hk_subSlide_ModeSelect, 0x90, 0x4b8f4a);
        const uint8_t exOpt[8] = {0x89, 0x75, 0xa0, 0x8d, 0x4e, 0x02, 0x3b, 0xde};
        RegRelJmp(0x4b2907, exOpt, 8, 5, &hk_subSlide_Options, 0x90, 0x4b299d);
        const uint8_t ex4b[10] = {0x8b, 0x43, 0x08, 0xc7, 0x45, 0x98, 0x00, 0x00, 0x00, 0x00};
        RegRelJmp(0x4b1ac4, ex4b, 10, 5, &hk_subSlide_s4b1aa0, 0x90, 0x4b1b84);
    }

    // 21 menu rainbow highlights
    {
        const uint8_t ex4b9b06[8] = {0x8b, 0x4d, 0x98, 0x8b, 0xd8, 0xb8, 0x4f, 0xec};
        RegRelJmp(0x4b9b06, ex4b9b06, 8, 5, &hk_rb_4b9b06, 0x90, 0x4b9b21);
        const uint8_t ex4b9b8b[8] = {0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe3, 0xc1};
        RegRelJmp(0x4b9b8b, ex4b9b8b, 8, 5, &hk_rb_4b9b8b, 0x90, 0x4b9ba8);
        const uint8_t ex4b9ca2[8] = {0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe3, 0xc1};
        RegRelJmp(0x4b9ca2, ex4b9ca2, 8, 5, &hk_rb_4b9ca2, 0x90, 0x4b9cb8);
        const uint8_t ex4b9002[10] = {0x8b, 0x4d, 0x98, 0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe1};
        RegRelJmp(0x4b9002, ex4b9002, 10, 5, &hk_rb_4b9002, 0x90, 0x4b9022);
        const uint8_t ex4b90e3[8] = {0x8b, 0x4d, 0x98, 0x8b, 0xd8, 0xb8, 0x4f, 0xec};
        RegRelJmp(0x4b90e3, ex4b90e3, 8, 5, &hk_rb_4b90e3, 0x90, 0x4b90fe);
        const uint8_t ex4b9171[10] = {0x8b, 0x4d, 0x98, 0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe1};
        RegRelJmp(0x4b9171, ex4b9171, 10, 5, &hk_rb_4b9171, 0x90, 0x4b918a);
        const uint8_t ex4b9238[10] = {0x8b, 0x4d, 0x98, 0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe1};
        RegRelJmp(0x4b9238, ex4b9238, 10, 5, &hk_rb_4b9238, 0x90, 0x4b9251);
        const uint8_t ex4b7bf2[10] = {0x8b, 0x4d, 0xac, 0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe1};
        RegRelJmp(0x4b7bf2, ex4b7bf2, 10, 5, &hk_rb_4b7bf2, 0x90, 0x4b7c12);
        const uint8_t ex4b7d4d[8] = {0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe1, 0xc1};
        RegRelJmp(0x4b7d4d, ex4b7d4d, 8, 5, &hk_rb_4b7d4d, 0x90, 0x4b7d63);
        const uint8_t ex4b7e1b[8] = {0x8b, 0x4d, 0xac, 0xb8, 0x4f, 0xec, 0xc4, 0x4e};
        RegRelJmp(0x4b7e1b, ex4b7e1b, 8, 5, &hk_rb_4b7e1b, 0x90, 0x4b7e34);
        const uint8_t ex4b3e47[8] = {0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe7, 0xc1};
        RegRelJmp(0x4b3e47, ex4b3e47, 8, 5, &hk_rb_4b3e47, 0x90, 0x4b3e6b);
        const uint8_t ex4af6e1[10] = {0x8b, 0x8d, 0x5c, 0xff, 0xff, 0xff, 0xb8, 0x4f, 0xec, 0xc4};
        RegRelJmp(0x4af6e1, ex4af6e1, 10, 5, &hk_rb_4af6e1, 0x90, 0x4af704);
        const uint8_t ex4b5813[8] = {0xb8, 0x4f, 0xec, 0xc4, 0x4e, 0xf7, 0xe7, 0xc1};
        RegRelJmp(0x4b5813, ex4b5813, 8, 5, &hk_rb_4b5813, 0x90, 0x4b5832);
        const uint8_t ex4b58d9[6] = {0xf7, 0xe7, 0xc1, 0xea, 0x02, 0x6b};
        RegRelJmp(0x4b58d9, ex4b58d9, 6, 5, &hk_rb_4b58d9, 0x90, 0x4b58ec);
        RegRelJmp(0x4b5957, ex4b58d9, 6, 5, &hk_rb_4b5957, 0x90, 0x4b596a);
        const uint8_t ex4ad2d8[8] = {0x8b, 0x45, 0xac, 0x33, 0xd2, 0x8d, 0x44, 0x38};
        RegRelJmp(0x4ad2d8, ex4ad2d8, 8, 5, &hk_rb_4ad2d8, 0x90, 0x4ad2ef);
        const uint8_t ex4ae11c[8] = {0x8b, 0x45, 0xac, 0x33, 0xd2, 0x83, 0xc0, 0x03};
        RegRelJmp(0x4ae11c, ex4ae11c, 8, 5, &hk_rb_4ae11c, 0x90, 0x4ae132);
        RegRelJmp(0x4ae14d, ex4ae11c, 8, 5, &hk_rb_4ae14d, 0x90, 0x4ae163);
        const uint8_t ex4ae27c[6] = {0x8b, 0x45, 0xac, 0x40, 0x33, 0xd2};
        RegRelJmp(0x4ae27c, ex4ae27c, 6, 5, &hk_rb_4ae27c, 0x90, 0x4ae2a5);
        const uint8_t ex4ae2f9[8] = {0x8b, 0x45, 0xac, 0x33, 0xd2, 0x83, 0xc0, 0x02};
        RegRelJmp(0x4ae2f9, ex4ae2f9, 8, 5, &hk_rb_4ae2f9, 0x90, 0x4ae324);
        const uint8_t ex4ae39c[8] = {0x8b, 0x45, 0xac, 0x33, 0xd2, 0xbf, 0x0d, 0x00};
        RegRelJmp(0x4ae39c, ex4ae39c, 8, 5, &hk_rb_4ae39c, 0x90, 0x4ae3b4);
    }

    // 8 submenu fade pct blocks
    {
        const uint8_t exEdi[8] = {0x8b, 0x47, 0x04, 0xbe, 0x64, 0x00, 0x00, 0x00};
        const uint8_t exEbx[8] = {0x8b, 0x43, 0x04, 0xbe, 0x64, 0x00, 0x00, 0x00};
        RegRelJmp(0x4af68a, exEdi, 8, 5, &hk_subFade_4af68a, 0x90, 0x4af6ab);
        RegRelJmp(0x4b3df4, exEbx, 8, 5, &hk_subFade_4b3df4, 0x90, 0x4b3e15);
        RegRelJmp(0x4b57b1, exEbx, 8, 5, &hk_subFade_4b57b1, 0x90, 0x4b57d2);
        RegRelJmp(0x4b29a0, exEdi, 8, 5, &hk_subFade_4b29a0, 0x90, 0x4b2a13);
        RegRelJmp(0x4b8f4d, exEbx, 8, 5, &hk_subFade_4b8f4d, 0x90, 0x4b8fb1);

        const uint8_t ex4b99fc[8] = {0xbe, 0x64, 0x00, 0x00, 0x00, 0x83, 0xf8, 0x08};
        RegRelJmp(0x4b99fc, ex4b99fc, 8, 5, &hk_subFade_4b99fc, 0x90, 0x4b9a5e);

        const uint8_t ex4b7b54[8] = {0x8b, 0x47, 0x04, 0x89, 0x77, 0x68, 0xbe, 0x64};
        RegRelJmp(0x4b7b54, ex4b7b54, 8, 5, &hk_subFade_4b7b54, 0x90, 0x4b7b7e);

        const uint8_t ex4b1b87[10] = {0x8b, 0x43, 0x04, 0xbe, 0x64, 0x00, 0x00, 0x00, 0xc7, 0x45};
        RegRelJmp(0x4b1b87, ex4b1b87, 10, 5, &hk_subFade_4b1b87, 0x90, 0x4b1c1b);
    }

    // Pause slide, Pause X, PauseConfirm, EndLevel flash/fade, Pause skips
    {
        const uint8_t exPs[8] = {0x8b, 0x57, 0x04, 0x33, 0xdb, 0x33, 0xc0, 0x83};
        RegRelJmp(0x4ad147, exPs, 8, 5, &hk_pauseSmoothSlideY, 0x90, 0x4ad1a0);

        const uint8_t exP0x[8] = {0x8b, 0x48, 0x4c, 0xf3, 0x0f, 0x10, 0x0d, 0x74};
        RegRelJmp(0x4ad1fa, exP0x, 8, 5, &hk_pauseSmoothX_P0, 0x90, 0x4ad20e);

        const uint8_t exP1x[8] = {0x8b, 0x47, 0x4c, 0xf3, 0x0f, 0x10, 0x0d, 0x74};
        RegRelJmp(0x4ad48b, exP1x, 8, 5, &hk_pauseSmoothX_P1, 0x90, 0x4ad4a9);

        const uint8_t exP0S[13] = {0x81, 0x7f, 0x4c, 0x40, 0x01, 0x00, 0x00, 0x0f, 0x84, 0x62, 0x02, 0x00, 0x00};
        RegRelJmp(0x4ad1dd, exP0S, 13, 13, &hk_pauseP0Skip, 0x90);

        const uint8_t exP1S[9] = {0x39, 0x5f, 0x4c, 0x0f, 0x84, 0xf9, 0x0a, 0x00, 0x00};
        RegRelJmp(0x4ad45f, exP1S, 9, 9, &hk_pauseP1Skip, 0x90);

        const uint8_t exPc[8] = {0x8b, 0x46, 0x04, 0x83, 0xc4, 0x14, 0xbb, 0xd4};
        RegRelJmp(0x4ae793, exPc, 8, 5, &hk_pauseConfirmSmooth, 0x90, 0x4ae7e2);

        const uint8_t exEf[8] = {0x8b, 0x46, 0x3c, 0x83, 0xc4, 0x04, 0x83, 0xf8};
        RegRelJmp(0x4a99de, exEf, 8, 5, &hk_endFlashAlpha, 0x90, 0x4a9a0b);

        const uint8_t exEfd[8] = {0x8b, 0x46, 0x40, 0x53, 0x57, 0xbb, 0x64, 0x00};
        RegRelJmp(0x4a9a5f, exEfd, 8, 5, &hk_endFadePct, 0x90, 0x4a9a86);
    }

    // WorldScreen flash, rainbow triangle, Toast, Minimap
    {
        const uint8_t exHf[9] = {0x83, 0xbf, 0x1c, 0x21, 0x00, 0x00, 0x00, 0x0f, 0x8e};
        RegRelJmp(0x47b45c, exHf, 9, 5, &hk_hudFlashSmooth, 0x90, 0x47b4ea);

        const uint8_t exHr[8] = {0x8b, 0x47, 0x04, 0x33, 0xd2, 0xb9, 0x82, 0x00};
        RegRelJmp(0x47b531, exHr, 8, 5, &hk_hudRainbowSmooth, 0x90, 0x47b5af);

        const uint8_t exTst[8] = {0x83, 0x7e, 0x34, 0x00, 0x0f, 0x8e, 0x89, 0x00};
        RegRelJmp(0x495377, exTst, 8, 5, &hk_toastAlphaSmooth, 0x90, 0x4953c1);

        const uint8_t exMm[7] = {0x8b, 0xcf, 0xe8, 0x48, 0xf4, 0xff, 0xff};
        RegRelCall(0x47b511, exMm, 7, 7, &hk_minimapSmoothDraw_bridge);

        const uint8_t exMmIn[6] = {0x8b, 0x93, 0x08, 0x21, 0x00, 0x00};
        RegRelJmp(0x47e66a, exMmIn, 6, 6, &hk_mmSkipIn, 0x90);

        const uint8_t exMmOut[6] = {0x8b, 0x83, 0x08, 0x21, 0x00, 0x00};
        RegRelJmp(0x47e694, exMmOut, 6, 6, &hk_mmSkipOut, 0x90);

        const uint8_t exMmVb[6] = {0x8b, 0x3d, 0x40, 0x90, 0x5f, 0x00};
        RegRelCall(0x47e8d0, exMmVb, 6, 6, &hk_mmForceVb);
    }
}
