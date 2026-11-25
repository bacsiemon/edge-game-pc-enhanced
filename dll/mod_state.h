#pragma once

#include "patch_utils.h"

namespace edgemod {

// Shared UI scale & auto scale state (mod_ui_scale_rt.cpp)
extern uint32_t g_autoScale;
extern float    g_uiScale;
extern float    g_iniUiScale;
extern float    g_lastIniScale;
extern int8_t   g_iniAutoScale;
extern int8_t   g_lastIniAuto;
extern const float g_uiScalePresets[9];

bool  __cdecl ClampUiScale(float* inOutVal);
float __cdecl GetEffectiveUiScale();
void  __cdecl ApplyUiScale(void* screenMgr);
void  __cdecl VirtualRectToUiScissorLtrb(
    const uint8_t* renderer, int32_t x, int32_t y, int32_t w, int32_t h,
    int32_t* outLtrb, int32_t* outDrawW, int32_t* outDrawH
);
EdgeWString* __cdecl FmtFloatWstr(double val, EdgeWString* out);

// Shared Camera, Time Format & Skip Intro state (mod_menu_camera_options.cpp)
extern float   g_camFov;
extern float   g_iniCamFov;
extern float   g_lastIniFov;
extern float   g_camDist;
extern float   g_iniCamDist;
extern float   g_lastIniDist;
extern uint8_t g_camInited;
extern float   g_activeMul;
extern bool    g_inRenderCamInterp;

extern uint8_t g_timeFmt;
extern uint8_t g_skipIntro;
extern uint8_t g_musicStart;
extern uint8_t g_musicPlaylist; // 0=pc, 1=java
extern uint8_t g_muteUnfocused; // 0=off, 1=on
extern uint8_t g_prismVfx;    // 0=off, 1=short (vanilla), 2=medium (1s), 3=long (2s)
extern uint8_t g_chromaAb;    // 0=off, 1=on
extern int32_t g_aaSamples;   // 0=off, 2/4/8/16 MSAA samples
extern uint8_t g_finishEffect;   // 0=pc, 1=mobile, 2=java
extern uint8_t g_finishPlatform; // 0=pc, 1=mobile
extern uint8_t g_colorTheme;     // 0=pc, 1=mobile, 2=java
extern uint8_t g_cubeDistortion; // 0=off, 1=on
extern uint8_t g_timerMode;   // 0=off, 1=level, 2=rank, 3=both
extern float   g_timerSize;   // 0.25..2.0 (presets: 0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2)
extern uint8_t g_timerPos;    // 0=TL, 1=TR, 2=BL, 3=BR
extern uint8_t g_edgeTimerMode; // 0=2D, 1=2D scaled, 2=3D
extern uint8_t g_prismCounterType; // 0=off, 1=short, 2=full
extern float   g_prismCounterSize; // 0.25..2.0 (presets matching g_timerSize)
extern uint8_t g_prismCounterPos;  // 0=TL, 1=TR, 2=BL, 3=BR
extern int32_t g_timerBgOpacity;   // 0..100, step 10 (default 70)
extern int32_t g_prismBgOpacity;   // 0..100, step 10 (default 70)
extern int32_t g_timerShadowOpacity; // 0..100, step 10 (default 0)
extern int32_t g_prismShadowOpacity; // 0..100, step 10 (default 0)

// Shared smoothing state used across UI smooth modules & time format (mod_ui_smooth_part1/2.cpp)
extern float   g_uiAlpha;
extern uint8_t g_justTicked;
extern int32_t g_pauseSubSwipeX;
extern int32_t g_pauseSubSwipePrevX;
constexpr int32_t kPauseOpenReadyTick = 9;
int32_t __cdecl GetPauseContentShiftY();

void __cdecl FmtInt0to100Wstr(int val, EdgeWString* out);
void __cdecl FormatPrismRatioWstr(int32_t collected, int32_t total, wchar_t* wbuf, size_t cap, EdgeWString* out);

// Shared layout constant (spacious vanilla stride for nested Options categories)
constexpr int kOptionsRowTextY(int row) {
    return 0x61 + 0x32 * row;
}

// Engine functions called from C++
inline void Game_Free(void* p) {
    reinterpret_cast<void(__cdecl*)(void*)>(0x4ba62a)(p);
}

inline void Game_PlaySfx(void* screenMgr, int sfxId) {
    reinterpret_cast<void(__thiscall*)(void*, int)>(0x4815f0)(screenMgr, sfxId);
}

inline void Game_SaveUserSettings(void* userSettings) {
    reinterpret_cast<void(__thiscall*)(void*)>(0x4927d0)(userSettings);
}

inline int Game_DrawText(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int shadowFlag, int a7, int alignFlags
) {
    return reinterpret_cast<int(__cdecl*)(const EdgeWString*, int, int, int, int, int, int, int)>(0x4a6c80)(
        ws, x, y, scaleX, scaleY, shadowFlag, a7, alignFlags
    );
}

inline void Game_GetLocalizedWString(EdgeWString* out, int strId) {
    reinterpret_cast<EdgeWString*(__cdecl*)(EdgeWString*, int)>(0x49fd70)(out, strId);
}

inline void Game_DrawQuad(int x, int y, int w, int h, const void* rgba) {
    reinterpret_cast<void(__cdecl*)(int, int, int, int, const void*)>(0x488670)(x, y, w, h, rgba);
}

inline void Game_DrawTriangle(
    float x0, float y0, float x1, float y1, float x2, float y2,
    const void* rgba0, const void* rgba1, const void* rgba2
) {
    reinterpret_cast<void(__cdecl*)(
        float, float, float, float, float, float, const void*, const void*, const void*
    )>(0x488800)(x0, y0, x1, y1, x2, y2, rgba0, rgba1, rgba2);
}

inline uint32_t Game_ColorLerp(uint32_t c0, uint32_t c1, int pct) {
    return reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, int)>(0x488530)(c0, c1, pct);
}

inline void Game_SetBlend(void* renderer, int mode) {
    reinterpret_cast<void(__thiscall*)(void*, int)>(0x405de0)(renderer, mode);
}

inline void Game_SetD3DBlend(void* renderer, int srcBlend, int dstBlend) {
    reinterpret_cast<void(__thiscall*)(void*, int, int)>(0x405910)(renderer, srcBlend, dstBlend);
}

// Engine D3DXMatrixOrthoRH & D3DXMatrixPerspectiveFovRH
// Note: 0x4bb2ca / 0x4bb2d0 are thunks jmp DWORD PTR [IAT]; both are __stdcall (5 args)!
inline void Game_D3DXMatrixOrthoRH(void* pOut, float w, float h, float zn, float zf) {
    reinterpret_cast<void(__stdcall*)(void*, float, float, float, float)>(0x4bb2ca)(pOut, w, h, zn, zf);
}

inline void Game_D3DXMatrixPerspectiveFovRH(void* pOut, float fovy, float aspect, float zn, float zf) {
    reinterpret_cast<void(__stdcall*)(void*, float, float, float, float)>(0x4bb2d0)(pOut, fovy, aspect, zn, zf);
}

// True orthographic sentinel (0.01 deg -> fovy_rad = 0.000174533 rad <= kOrthoSentinelRadThreshold)
constexpr float kOrthoSentinelDeg          = 0.01f;
constexpr float kOrthoSentinelRadThreshold = 0.0005f;

// Renderer::SetTransform(int slot, int typeUnused): ecx=mat*, [esp+4]=0 __thiscall (ret 4)
inline void Game_SetProjectionTransform(const void* mat) {
    reinterpret_cast<void(__thiscall*)(const void*, int)>(0x435780)(mat, 0);
}

inline void Game_DrawRainbowRectBorder(int frame, int x, int y, int w, int h, int thickness, int alpha) {
    reinterpret_cast<void(__cdecl*)(int, int, int, int, int, int, int)>(0x495f30)(
        frame, x, y, w, h, thickness, alpha
    );
}

// Shared helpers callable from mod_ui_smooth_part2.cpp & mod_menu_camera_options.cpp
uint32_t __cdecl GetSmoothMenuRainbow(int frame);
int32_t  __cdecl GetSmoothLevelSelectScrollY(const uint8_t* ls);
void     __cdecl DrawSmoothRainbowSprite(
    uint8_t** sprites, uint32_t ucnt, uint32_t idx, int w, int h, int x, int y, int alpha
);
int __cdecl MeasureTextWidth(const EdgeWString* ws, int scaleX, int extraX);
int __cdecl DrawTextAutoFit(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int extraX, int extraY, int alignFlags, int maxWidth
);
void SetSfxVolume(int newVol, int sfxId, bool save);
void SetMusicVolume(int newVol, int sfxId, bool save);
void SyncFocusAudioMute();
bool GetHudTimerMenuPreview(uint32_t state, uint8_t* outMode, float* outSize, uint8_t* outPos, bool* outForceSample, bool* outDockBesideMenu);
bool GetHudPrismCounterMenuPreview(uint32_t state, uint8_t* outType, float* outSize, uint8_t* outPos, bool* outForceSample, bool* outDockBesideMenu);
void __cdecl RenderPauseHeaderTimer(int x, int y, int width);
void OnColorThemeChanged();
uint32_t __fastcall RenderPlayerCubeWithColorTheme(void* cube);
void __cdecl BeginPlayerCubeDistortion(void* cube);
void __cdecl EndCubeDistortion();
void __cdecl BeginGhostCubeDistortion(void* worldScreen);
void __cdecl ResetCubeDistortionState();
void EnsureMusicPlaylistReady();
void OnMusicPlaylistChanged();
float __cdecl ComputeEdgeTimerZoomScale(const uint8_t* ws, int32_t slot);
void  __cdecl RenderEdgeTimer3D();
void  __cdecl GetEdgeTimerSmoothState(const uint8_t* ws, int32_t slot, float* outEffTicks, float* outEffRise, uint32_t* outBaseRgb);

// Menu hover state & helpers (mod_menu_hover.cpp)
uint32_t __cdecl ApplyMenuSlotRainbowStyle(uint32_t rainbowRgb);
bool     __cdecl IsCurrentMenuSlotVisible();
bool     __cdecl IsCurrentMenuSlotPressed();
int32_t  __cdecl GetLevelSelectHoveredTile();
bool     __cdecl IsMenuInputKeyboardMode();
void     __cdecl SetMenuInputKeyboardMode(bool isKeyboard);
int32_t  __cdecl GetCurrentMenuRenderFrame();
bool     __cdecl IsMenuRenderInteractive();

// OptionsMenu / PauseMenu P1 layout & control queries (mod_menu_camera_options.cpp)
int      __cdecl GetActiveOptionsCategory(bool inPause);
int      __cdecl HitTestOptionsSlot(bool inPause, int mx, int my);
bool     __cdecl IsOptionsPopupOpen(bool inPause);
bool     __cdecl IsOptionsSliderDragging(bool inPause);
bool     __cdecl IsOptionsMenuReadyForHover(const uint8_t* menuObj, bool inPause);

} // namespace edgemod
