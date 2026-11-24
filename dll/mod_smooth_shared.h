#pragma once

#include "mod_state.h"

namespace edgemod {

struct FloatTracker {
    float* ptr;
    float  prev;
    float  tgt;
    float  last;
};

struct MsModelTracker {
    uint8_t* animNode;
    float    prevAnim;
    float    tgtAnim;
    uint8_t* nodeTimer;
    float    prevNodeT;
    float    tgtNodeT;
};

// State defined in mod_ui_smooth_part1.cpp and shared with part2
extern float    g_uiAlpha;
extern uint8_t  g_justTicked;
extern uint8_t* g_subPtr;
extern int32_t  g_subPrev4;
extern int32_t  g_subPrev8;
extern int32_t  g_subPrev4c;
extern int32_t  g_endPrev3c;
extern int32_t  g_endPrev40;
extern uint8_t* g_wsPtr;
extern int32_t  g_wsPrev4;
extern int32_t  g_wsPrev2108;
extern int32_t  g_wsTgt2108;
extern int32_t  g_wsBuilt2108;
extern uint8_t  g_wsMmInterp;
extern uint32_t g_wsPrevTst2b;
extern int32_t  g_wsPrevTst34;
extern uint32_t g_rbTmp;
extern int32_t  g_msPrevC;
extern int32_t  g_subPrev50;
extern MsModelTracker g_mmTrackers[4];

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

// Hooks called from part1 into part2
void __cdecl OnSavePrev_Ext();
void __cdecl OnInterpolate_Ext(uint8_t* sm);

// Helpers defined in mod_ui_smooth_part2.cpp called from part1 / time format
void __cdecl SetSepGlyph();

} // namespace edgemod
