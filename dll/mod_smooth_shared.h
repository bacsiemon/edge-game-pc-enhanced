#pragma once

#include "mod_state.h"
#include <cmath>

namespace edgemod {

// State defined in mod_ui_smooth_part1.cpp and shared with part2
extern float    g_uiAlpha;
extern uint8_t  g_justTicked;
extern uint8_t* g_subPtr;
extern int32_t  g_subPrev4;
extern int32_t  g_subPrev8;
extern uint8_t* g_wsPtr;
extern int32_t  g_wsPrev2108;
extern int32_t  g_wsTgt2108;
extern uint32_t g_rbTmp;

// Helpers defined in mod_ui_smooth_part1.cpp
void     __cdecl GetActiveSub(uint8_t* sm, uint8_t** outSub, uint32_t* outVtable);
float    __cdecl SmoothCounterPair(int32_t cur, int32_t prev, bool isOut);
int32_t  __cdecl ClampPct0_100(float val);
int32_t  __cdecl DivPct8(float val);
int32_t  __cdecl DivPct4M4(float val);
int32_t  __cdecl CalcSubFadePct(const uint8_t* sub, int32_t mode);
float    __cdecl GetSmoothWsFrame(const uint8_t* ws);
uint32_t __cdecl CalcSmoothRainbow(float frame, float ticksPerColor);
uint32_t __cdecl GetSmoothMenuRainbow(int32_t frame);
uint32_t __cdecl GetSmoothSlotRainbow(int32_t frame);
int32_t  __cdecl CalcLevelLoadNameFadePct(const uint8_t* menu);
// Push/pop 2D MatrixStack translation (in mode 1, +tx moves right, +ty moves UP / -ty moves DOWN).
void     __cdecl PushUiMatrixTranslate(uint8_t* mstack, float tx, float ty, uint32_t* outSavedMode);
void     __cdecl PopUiMatrixTranslate(uint8_t* mstack, uint32_t savedMode);

struct ScopedUiMatrixTranslate {
    uint8_t* mstack = nullptr;
    uint32_t savedMode = 0;

    ScopedUiMatrixTranslate(float tx, float ty) {
        if (std::fabs(tx) > 0.0005f || std::fabs(ty) > 0.0005f) {
            uint8_t* ms = GetMatrixStack();
            if (ms != nullptr) {
                PushUiMatrixTranslate(ms, tx, ty, &savedMode);
                mstack = ms;
            }
        }
    }

    ~ScopedUiMatrixTranslate() {
        if (mstack != nullptr) {
            PopUiMatrixTranslate(mstack, savedMode);
        }
    }

    ScopedUiMatrixTranslate(const ScopedUiMatrixTranslate&) = delete;
    ScopedUiMatrixTranslate& operator=(const ScopedUiMatrixTranslate&) = delete;
};

// Hooks called from part1 into part2
void __cdecl OnSavePrev_Ext();
void __cdecl OnInterpolate_Ext(uint8_t* sm);

// Helpers defined in mod_ui_smooth_part2.cpp called from part1 / time format
void __cdecl SetSepGlyph();

} // namespace edgemod
