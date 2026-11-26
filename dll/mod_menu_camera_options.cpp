#include "mod_state.h"
#include "mod_smooth_shared.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace edgemod {

float   g_camFov      = -1.0f;
float   g_iniCamFov   = -99.0f;
float   g_lastIniFov  = -99.0f;
float   g_camDist     = 1.0f;
float   g_iniCamDist  = 0.0f;
float   g_lastIniDist = 0.0f;
uint8_t g_camInited   = 0;
float   g_activeMul   = 1.0f;
bool    g_inRenderCamInterp = false;

uint8_t g_timeFmt     = 0;
uint8_t g_skipIntro   = 0;
uint8_t g_musicStart  = 0;
uint8_t g_musicPlaylist = 0; // 0=pc, 1=java
uint8_t g_muteUnfocused = 0; // 0=off, 1=on
uint8_t g_prismVfx    = 1;
uint8_t g_chromaAb    = 1;
int32_t g_aaSamples   = -1; // sentinel until loaded/migrated
uint8_t g_finishEffect   = 0; // 0=pc, 1=mobile, 2=java
uint8_t g_finishPlatform = 0; // 0=pc, 1=mobile, 2=java
uint8_t g_cubeDistortion = 0; // 0=off, 1=on
uint8_t g_timerMode   = 3;    // default: both
float   g_timerSize   = 1.0f; // default: 1x
uint8_t g_timerPos    = 1;    // default: TR (top right)
uint8_t g_edgeTimerMode = 0;  // 0=2D, 1=2D scaled, 2=3D
uint8_t g_prismCounterType = 2; // 0=off, 1=short, 2=full
float   g_prismCounterSize = 1.0f; // default: 1x
uint8_t g_prismCounterPos  = 1; // 0=TL, 1=TR, 2=BL, 3=BR
int32_t g_timerBgOpacity   = 70; // 0..100, step 10
int32_t g_prismBgOpacity   = 70; // 0..100, step 10
int32_t g_timerShadowOpacity = 0;  // 0..100, step 10
int32_t g_prismShadowOpacity = 0;  // 0..100, step 10
uint8_t g_speedometerEnable  = 0;  // 0=off, 1=on
float   g_speedometerSize    = 1.0f; // default: 1x
int32_t g_speedometerBgOpacity     = 70; // 0..100, step 10
int32_t g_speedometerShadowOpacity = 0;  // 0..100, step 10
uint8_t g_speedometerPos     = 3;  // 0=TL, 1=TR, 2=BL, 3=BR (default: BR)

uint8_t g_minimapEnable         = 1;
uint8_t g_minimapEnableOverride = 0;
float   g_minimapWidth          = 1.0f;
float   g_minimapHeight         = 1.0f;
float   g_minimapRange          = 1.0f;
uint8_t g_minimapWidthOverride  = 0;
uint8_t g_minimapHeightOverride = 0;
uint8_t g_minimapRangeOverride  = 0;
int32_t g_minimapBgOpacity      = 70;
uint8_t g_minimapPos            = 0; // 0=TL, 1=TR, 2=BL, 3=BR
uint8_t g_minimapPosOverride    = 0;

constexpr int32_t kDefMinimapWidth  = 160;
constexpr int32_t kDefMinimapHeight = 160;
constexpr float   kDefMinimapCellW  = 19.5f;
constexpr float   kDefMinimapCellH  = 19.5f;

static int8_t  g_iniMinimapVisible      = 1;
static int8_t  g_lastIniMinimapVisible  = -1;
static int32_t g_iniMinimapWidth        = kDefMinimapWidth;
static int32_t g_iniMinimapHeight       = kDefMinimapHeight;
static float   g_iniMinimapCellW        = kDefMinimapCellW;
static float   g_iniMinimapCellH        = kDefMinimapCellH;
static int32_t g_iniMinimapAlignX       = 0;
static int32_t g_iniMinimapAlignY       = 0;
static int32_t g_iniMinimapOffX         = 0;
static int32_t g_iniMinimapOffY         = 0;
static int32_t g_lastIniMinimapWidth    = 0;
static int32_t g_lastIniMinimapHeight   = 0;
static float   g_lastIniMinimapCellW    = 0.0f;
static float   g_lastIniMinimapCellH    = 0.0f;
static int32_t g_lastIniMinimapAlignX   = -1;
static int32_t g_lastIniMinimapAlignY   = -1;

int32_t g_pauseSubSwipeX     = 0;
int32_t g_pauseSubSwipePrevX = 0;

static double GetMarqueeTimeSec() {
    static LARGE_INTEGER s_freq{};
    if (s_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&s_freq);
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return (s_freq.QuadPart > 0)
        ? (static_cast<double>(now.QuadPart) / static_cast<double>(s_freq.QuadPart))
        : 0.0;
}

static double GetMarqueeElapsedSec(uint64_t key) {
    struct MarqueeSlot {
        uint64_t key;
        double   startSec;
        double   lastSeenSec;
    };
    constexpr int kSlots = 16;
    static MarqueeSlot s_slots[kSlots]{};

    double now = GetMarqueeTimeSec();
    int oldestIdx = 0;
    for (int i = 0; i < kSlots; ++i) {
        if (s_slots[i].key == key && (now - s_slots[i].lastSeenSec) < 0.25) {
            s_slots[i].lastSeenSec = now;
            return now - s_slots[i].startSec;
        }
        if (i > 0 && s_slots[i].lastSeenSec < s_slots[oldestIdx].lastSeenSec) {
            oldestIdx = i;
        }
    }
    s_slots[oldestIdx].key = key;
    s_slots[oldestIdx].startSec = now;
    s_slots[oldestIdx].lastSeenSec = now;
    return 0.0;
}

int __cdecl MeasureTextWidth(const EdgeWString* ws, int scaleX, int extraX) {
    if (ws == nullptr || ws->size == 0 || scaleX <= 0) {
        return 0;
    }

    const uint8_t* mapHead     = *reinterpret_cast<const uint8_t* const*>(0x5f9214);
    const uint8_t* glyphWidths = *reinterpret_cast<const uint8_t* const*>(0x5f91f4);
    int16_t numGlyphs          = *reinterpret_cast<const int16_t*>(0x5f91ec);
    uint8_t spaceW             = *reinterpret_cast<const uint8_t*>(0x5f91e8);
    uint8_t glyphH             = *reinterpret_cast<const uint8_t*>(0x5f91e9);
    const wchar_t* s           = (ws->cap >= 8) ? ws->ptr : ws->buf;
    if (mapHead == nullptr || glyphWidths == nullptr || numGlyphs <= 0 || glyphH == 0 || s == nullptr) {
        return 0;
    }

    int origW = 0;
    int curW = 0;
    uint32_t len = ws->size;
    int kern = (scaleX + 0xfff) >> 12;

    for (uint32_t i = 0; i < len; ++i) {
        uint16_t ch = static_cast<uint16_t>(s[i]);
        if (ch == L'\n') {
            if (curW > origW) origW = curW;
            curW = 0;
        } else {
            const uint8_t* node = *reinterpret_cast<const uint8_t* const*>(mapHead + 4);
            const uint8_t* lb = mapHead;
            while (node != nullptr && node[0x15] == 0) {
                uint16_t key = *reinterpret_cast<const uint16_t*>(node + 0xc);
                if (key < ch) {
                    node = *reinterpret_cast<const uint8_t* const*>(node + 8);
                } else {
                    lb = node;
                    node = *reinterpret_cast<const uint8_t* const*>(node + 0);
                }
            }
            int32_t gIdx = -1;
            if (lb != mapHead && *reinterpret_cast<const uint16_t*>(lb + 0xc) == ch) {
                gIdx = *reinterpret_cast<const int32_t*>(lb + 0x10);
            }
            if (gIdx >= 0 && gIdx < numGlyphs) {
                curW += ((static_cast<int>(glyphWidths[gIdx]) * scaleX + 0xfff) >> 12) + extraX;
                if (i + 1 < len) curW += kern;
            } else {
                curW += ((static_cast<int>(spaceW) * scaleX + 0xfff) >> 12) + extraX;
            }
        }
    }
    if (curW > origW) origW = curW;
    return origW;
}

int __cdecl DrawTextAutoFit(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int extraX, int extraY, int alignFlags, int maxWidth
) {
    if (ws == nullptr || ws->size == 0 || maxWidth <= 0 || scaleX <= 0 || scaleY <= 0) {
        return Game_DrawText(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags);
    }

    const wchar_t* s = (ws->cap >= 8) ? ws->ptr : ws->buf;
    if (s == nullptr) {
        return Game_DrawText(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags);
    }
    uint32_t len = ws->size;
    int origW = MeasureTextWidth(ws, scaleX, extraX);
    if (origW <= 0 || origW <= maxWidth) {
        return Game_DrawText(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags);
    }

    uint8_t* renderer = GetRenderer();
    uint8_t* mstack   = GetMatrixStack();
    void* dev         = (renderer != nullptr) ? *reinterpret_cast<void**>(renderer + 0xcc) : nullptr;
    if (renderer == nullptr || mstack == nullptr || dev == nullptr) {
        return Game_DrawText(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags);
    }

    uint64_t key = 14695981039346656037ull;
    for (uint32_t i = 0; i < len; ++i) {
        key ^= static_cast<uint64_t>(static_cast<uint16_t>(s[i]));
        key *= 1099511628211ull;
    }
    key ^= (static_cast<uint64_t>(static_cast<uint32_t>(maxWidth)) << 8)
         | static_cast<uint64_t>(alignFlags & 0x09);
    key *= 1099511628211ull;
    if (key == 0) key = 1;

    int overflow = origW - maxWidth;
    constexpr double kPauseSec      = 1.2;
    constexpr double kSpeedPxPerSec = 42.0;
    constexpr double kMinTravelSec  = 0.65;
    double travelSec = static_cast<double>(overflow) / kSpeedPxPerSec;
    if (travelSec < kMinTravelSec) travelSec = kMinTravelSec;
    double halfCycle = kPauseSec + travelSec;
    double fullCycle = 2.0 * halfCycle;
    double t = std::fmod(GetMarqueeElapsedSec(key), fullCycle);
    if (t < 0.0) t += fullCycle;
    double frac = 0.0;
    if (t < kPauseSec) {
        frac = 0.0;
    } else if (t < halfCycle) {
        frac = (t - kPauseSec) / travelSec;
    } else if (t < halfCycle + kPauseSec) {
        frac = 1.0;
    } else {
        frac = 1.0 - (t - halfCycle - kPauseSec) / travelSec;
    }
    float scrollPx = static_cast<float>(frac * static_cast<double>(overflow));

    int clipLeft = x;
    if (alignFlags & 0x01)      clipLeft = x - (maxWidth / 2);
    else if (alignFlags & 0x08) clipLeft = x - maxWidth;
    int drawX = clipLeft;
    int drawAlignFlags = (alignFlags & ~0x09) | 0x04;

    float worldScaleX = 1.0f;
    float worldTransX = 0.0f;
    const uint8_t* beginPtr = *reinterpret_cast<const uint8_t* const*>(mstack + 0x8);
    const uint8_t* endPtr   = *reinterpret_cast<const uint8_t* const*>(mstack + 0xc);
    if (beginPtr != nullptr && endPtr != nullptr && endPtr >= beginPtr + 0x40) {
        const float* topMat = reinterpret_cast<const float*>(endPtr - 0x40);
        worldScaleX = topMat[0];
        worldTransX = topMat[12];
    }

    int32_t scrLeft  = static_cast<int32_t>(std::floor(static_cast<float>(clipLeft) * worldScaleX + worldTransX));
    int32_t scrRight = static_cast<int32_t>(std::ceil(static_cast<float>(clipLeft + maxWidth) * worldScaleX + worldTransX));
    if (scrRight <= scrLeft) return 0;

    int32_t ltrb[4]{};
    int32_t drawW = 0, drawH = 0;
    VirtualRectToUiScissorLtrb(renderer, scrLeft, 0, scrRight - scrLeft, 0x4000, ltrb, &drawW, &drawH);

    auto** vt        = *reinterpret_cast<void***>(dev);
    auto setRS       = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, uint32_t)>(vt[0xe4 / 4]);
    auto getRS       = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, DWORD*)>(vt[0xe8 / 4]);
    auto setScissor  = reinterpret_cast<HRESULT(__stdcall*)(void*, const RECT*)>(vt[0x12c / 4]);
    auto getScissor  = reinterpret_cast<HRESULT(__stdcall*)(void*, RECT*)>(vt[0x130 / 4]);

    DWORD oldScissorEn = 0;
    RECT oldRect{};
    bool hasOldScissor = false;
    if (getRS(dev, 0xae, &oldScissorEn) >= 0 && oldScissorEn != 0 && getScissor(dev, &oldRect) >= 0) {
        hasOldScissor = true;
    }

    RECT clipRect{ltrb[0], ltrb[1], ltrb[2], ltrb[3]};
    if (hasOldScissor) {
        if (oldRect.left   > clipRect.left)   clipRect.left   = oldRect.left;
        if (oldRect.top    > clipRect.top)    clipRect.top    = oldRect.top;
        if (oldRect.right  < clipRect.right)  clipRect.right  = oldRect.right;
        if (oldRect.bottom < clipRect.bottom) clipRect.bottom = oldRect.bottom;
    }
    if (clipRect.left >= clipRect.right || clipRect.top >= clipRect.bottom) {
        return 0;
    }

    setScissor(dev, &clipRect);
    setRS(dev, 0xae, 1);

    float mat[16] = {
         1.0f,     0.0f, 0.0f, 0.0f,
         0.0f,     1.0f, 0.0f, 0.0f,
         0.0f,     0.0f, 1.0f, 0.0f,
        -scrollPx, 0.0f, 0.0f, 1.0f
    };
    uint32_t& stackMode = *reinterpret_cast<uint32_t*>(mstack + 0x4);
    uint32_t savedMode = stackMode;
    stackMode = 1;
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f750)(mstack);
    reinterpret_cast<void(__thiscall*)(void*, const float*)>(0x41f520)(mstack, mat);
    Game_DrawText(ws, drawX, y, scaleX, scaleY, extraX, extraY, drawAlignFlags);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f610)(mstack);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack);
    stackMode = savedMode;

    if (hasOldScissor) {
        setScissor(dev, &oldRect);
    } else {
        setRS(dev, 0xae, 0);
    }
    return maxWidth;
}

struct ScopedUiScissor {
    void* dev = nullptr;
    HRESULT(__stdcall* setRS)(void*, uint32_t, uint32_t) = nullptr;
    HRESULT(__stdcall* setScissor)(void*, const RECT*) = nullptr;
    bool hasOldScissor = false;
    RECT oldRect{};
    bool active = false;
    bool empty = false;

    ScopedUiScissor(int x, int y, int w, int h) {
        if (w <= 0 || h <= 0) {
            empty = true;
            return;
        }
        uint8_t* renderer = GetRenderer();
        uint8_t* mstack   = GetMatrixStack();
        dev = (renderer != nullptr) ? *reinterpret_cast<void**>(renderer + 0xcc) : nullptr;
        if (renderer == nullptr || mstack == nullptr || dev == nullptr) {
            return;
        }

        float scaleX = 1.0f, scaleY = 1.0f, transX = 0.0f, transY = 0.0f;
        const uint8_t* beginPtr = *reinterpret_cast<const uint8_t* const*>(mstack + 0x8);
        const uint8_t* endPtr   = *reinterpret_cast<const uint8_t* const*>(mstack + 0xc);
        if (beginPtr != nullptr && endPtr != nullptr && endPtr >= beginPtr + 0x40) {
            const float* topMat = reinterpret_cast<const float*>(endPtr - 0x40);
            scaleX = topMat[0];
            scaleY = topMat[5];
            transX = topMat[12];
            transY = topMat[13];
        }

        int32_t scrLeft   = static_cast<int32_t>(std::floor(static_cast<float>(x) * scaleX + transX));
        int32_t scrTop    = static_cast<int32_t>(std::floor(static_cast<float>(y) * scaleY - transY));
        int32_t scrRight  = static_cast<int32_t>(std::ceil(static_cast<float>(x + w) * scaleX + transX));
        int32_t scrBottom = static_cast<int32_t>(std::ceil(static_cast<float>(y + h) * scaleY - transY));
        if (scrRight <= scrLeft || scrBottom <= scrTop) {
            empty = true;
            return;
        }

        int32_t ltrb[4]{};
        int32_t drawW = 0, drawH = 0;
        VirtualRectToUiScissorLtrb(renderer, scrLeft, scrTop, scrRight - scrLeft, scrBottom - scrTop, ltrb, &drawW, &drawH);

        auto** vt = *reinterpret_cast<void***>(dev);
        setRS      = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, uint32_t)>(vt[0xe4 / 4]);
        auto getRS = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, DWORD*)>(vt[0xe8 / 4]);
        setScissor = reinterpret_cast<HRESULT(__stdcall*)(void*, const RECT*)>(vt[0x12c / 4]);
        auto getSc = reinterpret_cast<HRESULT(__stdcall*)(void*, RECT*)>(vt[0x130 / 4]);

        DWORD oldEn = 0;
        if (getRS(dev, 0xae, &oldEn) >= 0 && oldEn != 0 && getSc(dev, &oldRect) >= 0) {
            hasOldScissor = true;
        }

        RECT clipRect{ltrb[0], ltrb[1], ltrb[2], ltrb[3]};
        if (hasOldScissor) {
            if (oldRect.left   > clipRect.left)   clipRect.left   = oldRect.left;
            if (oldRect.top    > clipRect.top)    clipRect.top    = oldRect.top;
            if (oldRect.right  < clipRect.right)  clipRect.right  = oldRect.right;
            if (oldRect.bottom < clipRect.bottom) clipRect.bottom = oldRect.bottom;
        }
        if (clipRect.left >= clipRect.right || clipRect.top >= clipRect.bottom) {
            empty = true;
            return;
        }

        setScissor(dev, &clipRect);
        setRS(dev, 0xae, 1);
        active = true;
    }

    ~ScopedUiScissor() {
        if (!active || dev == nullptr) return;
        if (hasOldScissor) {
            setScissor(dev, &oldRect);
        } else {
            setRS(dev, 0xae, 0);
        }
    }
};

} // namespace edgemod

namespace {

using namespace edgemod;

constexpr int kMaxOptRows = 24;

uint8_t g_graphicsLoaded   = 0;
float   g_prismOverrideSec = 0.0f;
int32_t g_aaLevels[5]      = {0, 0, 0, 0, 0};
int     g_aaLevelCount     = 0;

constexpr float kTan11 = 0.194380309f; // tan(11 deg)
constexpr float kTan10 = 0.176326981f; // tan(10 deg)
constexpr float kPiOver360 = 0.00872664626f;
constexpr float k360OverPi = 114.591559f;
constexpr float kRad22 = 22.0f * (3.14159265358979323846f / 180.0f);

const float g_distPresets[8] = {-1.0f, 0.5f, 0.65f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
const float g_fovPresets[11] = {-1.0f, -2.0f, 0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f, 40.0f, 50.0f};
const float g_timerSizePresets[8] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};

EDGE_STATIC_WSTR(g_wstrPrismVfx,  "prism vfx");
EDGE_STATIC_WSTR(g_wstrChromaAb,  "chromatic aberration");
EDGE_STATIC_WSTR(g_wstrAntiAlias, "anti-aliasing");
EDGE_STATIC_WSTR(g_wstrShort,     "short");
EDGE_STATIC_WSTR(g_wstrMedium,    "medium");
EDGE_STATIC_WSTR(g_wstrLong,      "long");
EDGE_STATIC_WSTR(g_wstrUiScale,   "ui scale");
EDGE_STATIC_WSTR(g_wstrAuto,      "auto scale");
EDGE_STATIC_WSTR(g_wstrCamDist,   "cam distance");
EDGE_STATIC_WSTR(g_wstrCamFov,    "dolly zoom");
EDGE_STATIC_WSTR(g_wstrTimeFmt,   "time format");
EDGE_STATIC_WSTR(g_wstrSkipIntro, "skip intro");
EDGE_STATIC_WSTR(g_wstrMusicStart,   "music start");
EDGE_STATIC_WSTR(g_wstrMusicPlaylist, "music playlist");
EDGE_STATIC_WSTR(g_wstrMuteUnfocused, "mute when unfocused");
EDGE_STATIC_WSTR(g_wstrTimer,        "timer");
EDGE_STATIC_WSTR(g_wstrTimerType,    "type");
EDGE_STATIC_WSTR(g_wstrTimerSizeLbl, "size");
EDGE_STATIC_WSTR(g_wstrTimerPosLbl,  "position");
EDGE_STATIC_WSTR(g_wstrTimerLevel,   "level");
EDGE_STATIC_WSTR(g_wstrTimerRank,    "rank");
EDGE_STATIC_WSTR(g_wstrTimerBoth,    "both");
EDGE_STATIC_WSTR(g_wstrTimerPosTL,   "TL");
EDGE_STATIC_WSTR(g_wstrTimerPosTR,   "TR");
EDGE_STATIC_WSTR(g_wstrTimerPosBL,   "BL");
EDGE_STATIC_WSTR(g_wstrTimerPosBR,   "BR");
EDGE_STATIC_WSTR(g_wstrResetScore,   "reset score");
EDGE_STATIC_WSTR(g_wstrResetConfirm, "reset score?");
EDGE_STATIC_WSTR(g_wstrCatDisplay,   "display");
EDGE_STATIC_WSTR(g_wstrCatGraphics,  "graphics");
EDGE_STATIC_WSTR(g_wstrCatAudio,     "audio");
EDGE_STATIC_WSTR(g_wstrCatUi,        "ui");
EDGE_STATIC_WSTR(g_wstrCatGameplay,  "gameplay");
EDGE_STATIC_WSTR(g_wstrSubmenuArrow, ">");
EDGE_STATIC_WSTR(g_wstrLand,         "land");
EDGE_STATIC_WSTR(g_wstrFall,         "fall");
EDGE_STATIC_WSTR(g_wstrPc,           "pc");
EDGE_STATIC_WSTR(g_wstrMobile,       "mobile");
EDGE_STATIC_WSTR(g_wstrJava,         "java");
EDGE_STATIC_WSTR(g_wstrCustom,       "custom");
EDGE_STATIC_WSTR(g_wstrFinishEffect,   "finish effect");
EDGE_STATIC_WSTR(g_wstrFinishPlatform, "finish platform");
EDGE_STATIC_WSTR(g_wstrColorTheme,     "theme");
EDGE_STATIC_WSTR(g_wstrCustomizeTheme, "customize theme");
EDGE_STATIC_WSTR(g_wstrCubeDistortion, "cube idle distortion");
EDGE_STATIC_WSTR(g_wstrThemeTerrain,   "terrain");
EDGE_STATIC_WSTR(g_wstrThemeWallGrad,  "wall gradient");
EDGE_STATIC_WSTR(g_wstrThemePcJava,    "pc/java");
EDGE_STATIC_WSTR(g_wstrThemePlanets,   "planets");
EDGE_STATIC_WSTR(g_wstrThemeStars,     "stars");
EDGE_STATIC_WSTR(g_wstrThemePcMobile,  "pc/mobile");
EDGE_STATIC_WSTR(g_wstrThemePlayerCube,"player cube");
EDGE_STATIC_WSTR(g_wstrThemeDarkCube,  "dark cube");
EDGE_STATIC_WSTR(g_wstrThemePrisms,    "prisms");
EDGE_STATIC_WSTR(g_wstrThemeMovPlat,   "moving platforms");
EDGE_STATIC_WSTR(g_wstrThemePlatEdge,  "platform borders");
EDGE_STATIC_WSTR(g_wstrThemeFallPlat,  "falling platforms");
EDGE_STATIC_WSTR(g_wstrThemeBumpers,   "bumpers");
EDGE_STATIC_WSTR(g_wstrThemeSwitches,  "switches");
EDGE_STATIC_WSTR(g_wstrThemeHoloSwitch,"holo switches");
EDGE_STATIC_WSTR(g_wstrThemeShrinkers, "shrinkers");
EDGE_STATIC_WSTR(g_wstrDef,          "def");
EDGE_STATIC_WSTR(g_wstrNew,          "new");
EDGE_STATIC_WSTR(g_wstrDot,          ".");
EDGE_STATIC_WSTR(g_wstrEdgeTimer,         "edge timer");
EDGE_STATIC_WSTR(g_wstrEdgeTimer2D,       "2D");
EDGE_STATIC_WSTR(g_wstrEdgeTimer2DScaled, "2D scaled");
EDGE_STATIC_WSTR(g_wstrEdgeTimer3D,       "3D");
EDGE_STATIC_WSTR(g_wstrPrismCounter,      "prism counter");
EDGE_STATIC_WSTR(g_wstrFull,              "full");
EDGE_STATIC_WSTR(g_wstrBgOpacity,         "background");
EDGE_STATIC_WSTR(g_wstrShadowOpacity,     "drop shadow");
EDGE_STATIC_WSTR(g_wstrMinimap,           "minimap");
EDGE_STATIC_WSTR(g_wstrMinimapEnable,     "enable");
EDGE_STATIC_WSTR(g_wstrMinimapWidth,      "width");
EDGE_STATIC_WSTR(g_wstrMinimapHeight,     "height");
EDGE_STATIC_WSTR(g_wstrMinimapRange,      "range");
EDGE_STATIC_WSTR(g_wstrSpeedometer,       "speedometer");

static int NearestMinimapPresetIdx(float v) {
    int best = 2; // 1.0x
    float bestDiff = std::fabs(v - g_uiScalePresets[2]);
    for (int i = 0; i < 9; ++i) {
        float d = std::fabs(v - g_uiScalePresets[i]);
        if (d < bestDiff) {
            bestDiff = d;
            best = i;
        }
    }
    return best;
}

static int NearestTimerSizeIdx(float v) {
    int best = 3; // 1.0x
    float bestDiff = std::fabs(v - g_timerSizePresets[3]);
    for (int i = 0; i < 8; ++i) {
        float d = std::fabs(v - g_timerSizePresets[i]);
        if (d < bestDiff) {
            bestDiff = d;
            best = i;
        }
    }
    return best;
}

static const EdgeWString* GetTimerSizeValWstr(float v) {
    EDGE_STATIC_WSTR(s_val0, "0.25");
    EDGE_STATIC_WSTR(s_val1, "0.5");
    EDGE_STATIC_WSTR(s_val2, "0.75");
    EDGE_STATIC_WSTR(s_val3, "1");
    EDGE_STATIC_WSTR(s_val4, "1.25");
    EDGE_STATIC_WSTR(s_val5, "1.5");
    EDGE_STATIC_WSTR(s_val6, "1.75");
    EDGE_STATIC_WSTR(s_val7, "2");
    static const EdgeWString* const kVals[8] = {
        &s_val0, &s_val1, &s_val2, &s_val3, &s_val4, &s_val5, &s_val6, &s_val7
    };
    return kVals[NearestTimerSizeIdx(v)];
}

enum OptCategory : int {
    OPT_CAT_ROOT = 0,
    OPT_CAT_DISPLAY,
    OPT_CAT_GRAPHICS,
    OPT_CAT_CUSTOMIZE_THEME,
    OPT_CAT_AUDIO,
    OPT_CAT_UI,
    OPT_CAT_TIMER,
    OPT_CAT_PRISM_COUNTER,
    OPT_CAT_MINIMAP,
    OPT_CAT_SPEEDOMETER,
    OPT_CAT_GAMEPLAY
};

enum OptItem : int {
    OPT_NONE = 0,
    OPT_CAT_BTN_DISPLAY,
    OPT_CAT_BTN_GRAPHICS,
    OPT_CAT_BTN_CUSTOMIZE_THEME,
    OPT_CAT_BTN_AUDIO,
    OPT_CAT_BTN_UI,
    OPT_CAT_BTN_TIMER,
    OPT_CAT_BTN_PRISM_COUNTER,
    OPT_CAT_BTN_MINIMAP,
    OPT_CAT_BTN_SPEEDOMETER,
    OPT_CAT_BTN_GAMEPLAY,
    OPT_FULLSCREEN,
    OPT_PRISM_VFX,
    OPT_CHROMA_AB,
    OPT_AA,
    OPT_UI_SCALE,
    OPT_AUTO_SCALE,
    OPT_CAM_DIST,
    OPT_DOLLY_ZOOM,
    OPT_FINISH_EFFECT,
    OPT_FINISH_PLATFORM,
    OPT_COLOR_THEME,
    OPT_CUBE_DISTORTION,
    OPT_THEME_TERRAIN,
    OPT_THEME_WALL_GRADIENT,
    OPT_THEME_PLANETS,
    OPT_THEME_STARS,
    OPT_THEME_PLAYER_CUBE,
    OPT_THEME_DARK_CUBE,
    OPT_THEME_PRISMS,
    OPT_THEME_MOVING_PLATFORMS,
    OPT_THEME_PLATFORM_BORDERS,
    OPT_THEME_FALLING_PLATFORMS,
    OPT_THEME_BUMPERS,
    OPT_THEME_SWITCHES,
    OPT_THEME_HOLO_SWITCHES,
    OPT_THEME_SHRINKERS,
    OPT_SOUNDS,
    OPT_MUSIC,
    OPT_MUSIC_START,
    OPT_MUSIC_PLAYLIST,
    OPT_MUTE_UNFOCUSED,
    OPT_UP_KEY,
    OPT_SKIP_INTRO,
    OPT_TIME_FMT,
    OPT_EDGE_TIMER,
    OPT_TIMER,
    OPT_TIMER_SIZE,
    OPT_TIMER_POS,
    OPT_TIMER_BG_OPACITY,
    OPT_TIMER_SHADOW_OPACITY,
    OPT_PRISM_COUNTER_TYPE,
    OPT_PRISM_COUNTER_SIZE,
    OPT_PRISM_COUNTER_POS,
    OPT_PRISM_BG_OPACITY,
    OPT_PRISM_SHADOW_OPACITY,
    OPT_MINIMAP_ENABLE,
    OPT_MINIMAP_WIDTH,
    OPT_MINIMAP_HEIGHT,
    OPT_MINIMAP_RANGE,
    OPT_MINIMAP_BG_OPACITY,
    OPT_MINIMAP_POS,
    OPT_SPEEDOMETER_ENABLE,
    OPT_SPEEDOMETER_SIZE,
    OPT_SPEEDOMETER_BG_OPACITY,
    OPT_SPEEDOMETER_SHADOW_OPACITY,
    OPT_SPEEDOMETER_POS,
    OPT_RESET_SCORE,
    OPT_BACK
};

constexpr int kOptCatEnterBase = 0x100; // sentinel exitDest = 0x100 + targetCat

int     g_mainOptCat               = OPT_CAT_ROOT;
int     g_mainOptPendingReturnCat  = -1;
int     g_pauseOptCat              = OPT_CAT_ROOT;
int     g_pauseSubCatAtDepth[3]    = {OPT_CAT_ROOT, OPT_CAT_DISPLAY, OPT_CAT_TIMER};
int     g_pauseSubRow[3]           = {0, 0, 0};

constexpr int kMainOptRowH  = 0x2a; // expanded uniformly from 0x28 (40px) -> 42px
constexpr int kPauseOptRowH = 0x26; // expanded uniformly from 0x21 (33px) -> 38px

enum OptCtrlKind : int {
    CTRL_NAV = 0,
    CTRL_TOGGLE,
    CTRL_POPUP,
    CTRL_CONFIRM_POPUP,
    CTRL_SLIDER
};

struct OptUiState {
    bool    popupOpen;
    bool    popupInPause;
    OptItem popupItem;
    int     popupRow;
    int     popupHighlight;
    int     popupChoiceCount;
    int     popupRectX;
    int     popupRectY;
    int     popupRectW;
    int     popupRectH;
    int     popupItemH;
    int     popupPadY;
    bool    popupSlotVisible;
    bool    popupSlotPressed;
    bool    popupNeedsInitialSync;

    bool    dragActive;
    bool    dragInPause;
    OptItem dragItem;
    int     dragRow;
    int     dragTrackX;
    int     dragTrackW;
    bool    suppressNextClickAction;

    bool    uiScalePending;
    float   uiScalePreview;

    int     lastMouseX;
    int     lastMouseY;
    bool    prevResetKeyDown;
};

OptUiState g_optUi{};

struct OptScrollState {
    float    targetY = 0.0f;
    float    renderY = 0.0f;
    float    maxScroll = 0.0f;
    double   lastRenderSec = 0.0;
    bool     dragActive = false;
    int      dragGrabOffsetY = 0;
    int      dragTrackY = 0;
    int      dragTrackH = 1;
    int      dragThumbH = 28;
    uint32_t revision = 0;

    void Reset() {
        targetY = 0.0f;
        renderY = 0.0f;
        maxScroll = 0.0f;
        lastRenderSec = 0.0;
        dragActive = false;
        ++revision;
    }
};

struct OptScrollMetrics {
    bool  scrollable = false;
    int   nRows = 0;
    int   nScrollRows = 0;
    int   backIdx = 0;
    int   stride = 0;
    int   rowH = 0;
    int   slotPad = 0;
    int   listTop = 0;
    int   listBottom = 0;
    int   viewH = 0;
    int   contentH = 0;
    int   backTop = 0;
    int   backH = 0;
    float maxScroll = 0.0f;
    bool  hasScrollbar = false;
    int   sbX = 0, sbY = 0, sbW = 10, sbH = 0;
    int   thumbY = 0, thumbH = 0;
    float thumbYf = 0.0f;
    int   rightBound = 0;
};

OptScrollState g_mainOptScroll{};
OptScrollState g_pauseOptScroll{};
std::atomic<int32_t> g_optWheelAccum{0};

static bool IsOptCategoryScrollable(int cat) {
    return cat == OPT_CAT_CUSTOMIZE_THEME;
}

static OptScrollState& GetOptScrollState(bool inPause) {
    return inPause ? g_pauseOptScroll : g_mainOptScroll;
}

static void ResetAllScores_Impl();
static void ApplyOptionScaleChange();

// ------------------------------------------------------------------
// 1. Unified INI & ttsav persistence (ui_scale_menu_ini + camera + time_fmt + skip_intro)
// ------------------------------------------------------------------
static bool MatchAsciiIc(const EdgeString& s, const char* literal) {
    const char* p = s.c_str();
    for (size_t i = 0;; ++i) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') c |= 0x20;
        if (c != literal[i]) return false;
        if (literal[i] == '\0') return true;
    }
}

static bool QueryIniKey(void* iniMap, uint8_t* statusObj, const EdgeString* sec, const char* keyName, EdgeString* outVal) {
    reinterpret_cast<void(__thiscall*)(void*)>(0x4458c0)(statusObj);
    EdgeString key;
    key.ctor_cstr(keyName);
    reinterpret_cast<EdgeString*(__thiscall*)(void*, EdgeString*, const EdgeString*, const EdgeString*)>(0x4f5d70)(
        iniMap, outVal, sec, &key
    );
    key.dtor();
    return outVal->size != 0;
}

static bool ParseIniFloat(const EdgeString* val, uint8_t* statusObj, float* out) {
    float v = static_cast<float>(reinterpret_cast<double(__cdecl*)(const EdgeString*, uint8_t*)>(0x4f4fd0)(val, statusObj));
    if (statusObj[8] != 0 || std::isnan(v)) return false;
    *out = v;
    return true;
}

struct CamIniDesc {
    float* gVal;
    float* gIni;
    float* gLastIni;
    float  defFlt;
    float  unsetFlt;
    float  s1;
    float  s2;
    float  minFlt;
    float  maxFlt;
    const char* iniKey;
    const char* kw1;
    const char* kw2;
    const char* savKey;
    const char* savIniKey;
};

const CamIniDesc kDistDesc = {
    &g_camDist, &g_iniCamDist, &g_lastIniDist,
    1.0f, 0.0f, -1.0f, 1.0f, 0.1f, 10.0f,
    "camera_distance", "mobile", "pc", "camDist", "camDistIni"
};

const CamIniDesc kFovDesc = {
    &g_camFov, &g_iniCamFov, &g_lastIniFov,
    -1.0f, -99.0f, -1.0f, -2.0f, 0.0f, 90.0f,
    "camera_fov", "pc", "mobile", "camFov", "camFovIni"
};

static void ParseIniCamOne(void* iniMap, uint8_t* statusObj, const EdgeString* sec, const CamIniDesc& d) {
    *d.gIni = d.unsetFlt;
    if (!g_camInited) {
        *d.gLastIni = d.unsetFlt;
        *d.gVal = d.defFlt;
    }
    EdgeString val;
    if (QueryIniKey(iniMap, statusObj, sec, d.iniKey, &val)) {
        float chosen = 0.0f;
        bool ok = true;
        if (MatchAsciiIc(val, d.kw1)) chosen = d.s1;
        else if (MatchAsciiIc(val, d.kw2)) chosen = d.s2;
        else {
            ok = ParseIniFloat(&val, statusObj, &chosen);
            if (ok) {
                if (chosen < d.minFlt) chosen = d.minFlt;
                if (chosen > d.maxFlt) chosen = d.maxFlt;
            }
        }
        if (ok) {
            *d.gVal = chosen;
            *d.gIni = chosen;
        }
    }
    val.dtor();
}

static void __cdecl ReadIniSettings_Impl(uint8_t* ebpFrame) {
    void* iniMap = ebpFrame - 0x110;
    uint8_t* statusObj = ebpFrame - 0x100;
    reinterpret_cast<void(__thiscall*)(void*)>(0x4458c0)(statusObj);

    g_iniUiScale = 0.0f;
    g_iniAutoScale = -1;

    EdgeString sec;
    sec.ctor_cstr("graphics");
    EdgeString val;

    if (QueryIniKey(iniMap, statusObj, &sec, "ui_scale", &val)) {
        float v = 0.0f;
        if (ParseIniFloat(&val, statusObj, &v) && ClampUiScale(&v)) {
            g_iniUiScale = v;
        }
    }
    val.dtor();

    if (QueryIniKey(iniMap, statusObj, &sec, "ui_auto_scale", &val)) {
        bool b = reinterpret_cast<bool(__cdecl*)(const EdgeString*, uint8_t*)>(0x4f4980)(&val, statusObj);
        if (statusObj[8] == 0) {
            g_iniAutoScale = static_cast<int8_t>(b ? 1 : 0);
            g_autoScale = static_cast<uint32_t>(g_iniAutoScale);
        }
    }
    val.dtor();

    ParseIniCamOne(iniMap, statusObj, &sec, kDistDesc);
    ParseIniCamOne(iniMap, statusObj, &sec, kFovDesc);
    g_camInited = 1;

    sec.dtor();
    reinterpret_cast<void(__thiscall*)(void*)>(0x4458c0)(statusObj);
}

static void __declspec(naked) hk_readIniSettings() {
    __asm {
        push ebp
        call ReadIniSettings_Impl
        add esp, 4
        ret
    }
}

static bool ReadTtsavDouble(void* saveObj, const char* key, double* outVal) {
    if (saveObj == nullptr || key == nullptr || outVal == nullptr) return false;
    if (*reinterpret_cast<const uint8_t*>(static_cast<const uint8_t*>(saveObj) + 8) != 7) return false;
    if (!reinterpret_cast<bool(__thiscall*)(void*, const char*)>(0x51c660)(saveObj, key)) return false;
    void* var = reinterpret_cast<void*(__thiscall*)(void*, const char*)>(0x51c540)(saveObj, key);
    if (var == nullptr) return false;
    if (!reinterpret_cast<bool(__thiscall*)(void*, int)>(0x51b620)(var, 3)) return false;
    double val = reinterpret_cast<double(__thiscall*)(void*)>(0x51b3f0)(var);
    if (!std::isfinite(val)) return false;
    *outVal = val;
    return true;
}

static bool IsValidCamValue(const CamIniDesc& d, float x) {
    if (!std::isfinite(x)) return false;
    if (x == d.s1 || x == d.s2) return true;
    return x >= d.minFlt && x <= d.maxFlt;
}

static int GetLegacyDefaultAa(uint8_t legacyFx) {
    const uint8_t* cfg = *reinterpret_cast<const uint8_t* const*>(0x5f9058);
    if (cfg != nullptr) {
        int v = *reinterpret_cast<const int*>(cfg + (legacyFx ? 0x10 : 0x18));
        if (v >= 0 && v <= 16) return v;
    }
    return legacyFx ? 8 : 2;
}

// ------------------------------------------------------------------
// Standalone %LocalAppData%\Two Tribes\EDGE\edgemod_settings.ini persistence
// ------------------------------------------------------------------
constexpr int32_t kModIniSchemaVersion = 1;

static bool GetModSettingsPathW(wchar_t* outPath, size_t cap, bool createDirs) {
    if (outPath == nullptr || cap == 0) return false;
    outPath[0] = L'\0';

    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        while (n > 0 && (base[n - 1] == L'\\' || base[n - 1] == L'/')) {
            base[--n] = L'\0';
        }
        wchar_t dir1[MAX_PATH]{};
        wchar_t dir2[MAX_PATH]{};
        int r1 = std::swprintf(dir1, MAX_PATH, L"%ls\\Two Tribes", base);
        int r2 = std::swprintf(dir2, MAX_PATH, L"%ls\\Two Tribes\\EDGE", base);
        int r3 = std::swprintf(outPath, cap, L"%ls\\Two Tribes\\EDGE\\edgemod_settings.ini", base);
        if (r1 > 0 && r2 > 0 && r3 > 0 && static_cast<size_t>(r3) < cap) {
            if (createDirs) {
                CreateDirectoryW(dir1, nullptr);
                CreateDirectoryW(dir2, nullptr);
            }
            return true;
        }
    }

    DWORD exeLen = GetModuleFileNameW(nullptr, base, MAX_PATH);
    if (exeLen > 0 && exeLen < MAX_PATH) {
        wchar_t* slash = std::wcsrchr(base, L'\\');
        wchar_t* fslash = std::wcsrchr(base, L'/');
        if (fslash != nullptr && (slash == nullptr || fslash > slash)) slash = fslash;
        if (slash != nullptr) {
            *slash = L'\0';
            int r = std::swprintf(outPath, cap, L"%ls\\edgemod_settings.ini", base);
            if (r > 0 && static_cast<size_t>(r) < cap) return true;
        }
    }
    outPath[0] = L'\0';
    return false;
}

static bool ModSettingsFileExists() {
    wchar_t path[MAX_PATH]{};
    if (!GetModSettingsPathW(path, MAX_PATH, false)) return false;
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static bool IsAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void TrimAsciiInPlace(const char*& p, const char*& end) {
    while (p < end && IsAsciiSpace(*p)) ++p;
    while (end > p && IsAsciiSpace(end[-1])) --end;
}

static bool AsciiEqIc(const char* p, const char* end, const char* lit) {
    size_t i = 0;
    while (p < end) {
        char c = *p++;
        if (c >= 'A' && c <= 'Z') c |= 0x20;
        if (lit[i] == '\0' || c != lit[i]) return false;
        ++i;
    }
    return lit[i] == '\0';
}

static bool ParseAsciiDouble(const char* p, const char* end, double* out) {
    TrimAsciiInPlace(p, end);
    if (p >= end) return false;

    bool neg = false;
    if (*p == '-') {
        neg = true;
        ++p;
    } else if (*p == '+') {
        ++p;
    }
    if (p >= end) return false;

    double val = 0.0;
    bool hasDigits = false;
    while (p < end && *p >= '0' && *p <= '9') {
        hasDigits = true;
        val = val * 10.0 + static_cast<double>(*p - '0');
        ++p;
    }
    if (p < end && *p == '.') {
        ++p;
        double place = 0.1;
        while (p < end && *p >= '0' && *p <= '9') {
            hasDigits = true;
            val += static_cast<double>(*p - '0') * place;
            place *= 0.1;
            ++p;
        }
    }
    if (!hasDigits) return false;

    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        bool expNeg = false;
        if (p < end && (*p == '-' || *p == '+')) {
            expNeg = (*p == '-');
            ++p;
        }
        int expVal = 0;
        bool hasExpDigits = false;
        while (p < end && *p >= '0' && *p <= '9') {
            hasExpDigits = true;
            if (expVal < 1000) expVal = expVal * 10 + (*p - '0');
            ++p;
        }
        if (!hasExpDigits) return false;
        val *= std::pow(10.0, expNeg ? -expVal : expVal);
    }

    if (p != end) return false;
    if (neg) val = -val;
    if (!std::isfinite(val)) return false;
    *out = val;
    return true;
}

static bool ParseAsciiInt(const char* p, const char* end, int32_t* out) {
    double dv = 0.0;
    if (!ParseAsciiDouble(p, end, &dv)) return false;
    if (dv < -2147483648.0 || dv > 2147483647.0) return false;
    *out = static_cast<int32_t>(std::llround(dv));
    return true;
}

static void AppendAsciiFloat(std::string& out, double v) {
    if (!std::isfinite(v)) {
        out.push_back('0');
        return;
    }
    if (std::fabs(v) < 0.0000005) {
        out.push_back('0');
        return;
    }
    if (std::fabs(v - std::round(v)) < 1e-6 && std::fabs(v) < 1e9) {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(std::llround(v)));
        out += buf;
        return;
    }
    bool neg = (v < 0.0);
    double av = std::fabs(v);
    if (av < 1e9) {
        unsigned long long scaled = static_cast<unsigned long long>(std::llround(av * 1000000.0));
        unsigned long long whole = scaled / 1000000ull;
        unsigned long long frac = scaled % 1000000ull;
        if (neg && (whole > 0 || frac > 0)) out.push_back('-');
        char buf[48]{};
        if (frac == 0) {
            std::snprintf(buf, sizeof(buf), "%llu", whole);
            out += buf;
        } else {
            std::snprintf(buf, sizeof(buf), "%llu.%06llu", whole, frac);
            size_t len = std::strlen(buf);
            while (len > 0 && buf[len - 1] == '0') --len;
            if (len > 0 && buf[len - 1] == '.') --len;
            out.append(buf, len);
        }
        return;
    }
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    for (char* q = buf; *q != '\0'; ++q) {
        if (*q == ',') *q = '.';
    }
    out += buf;
}

static void SaveModSettings() {
    wchar_t iniPath[MAX_PATH + 16]{};
    if (!GetModSettingsPathW(iniPath, MAX_PATH, true)) return;

    float uiScale = GetEffectiveUiScale();
    float uiScaleIni = std::isfinite(g_lastIniScale) ? g_lastIniScale : 0.0f;
    uint32_t uiAuto = g_autoScale ? 1u : 0u;
    int32_t uiAutoIni = (g_lastIniAuto == 0 || g_lastIniAuto == 1) ? static_cast<int32_t>(g_lastIniAuto) : -1;

    float camDist = IsValidCamValue(kDistDesc, g_camDist) ? g_camDist : kDistDesc.defFlt;
    float camDistIni = IsValidCamValue(kDistDesc, g_lastIniDist) ? g_lastIniDist : kDistDesc.unsetFlt;
    float camFov = IsValidCamValue(kFovDesc, g_camFov) ? g_camFov : kFovDesc.defFlt;
    float camFovIni = IsValidCamValue(kFovDesc, g_lastIniFov) ? g_lastIniFov : kFovDesc.unsetFlt;

    uint8_t timeFmt = (g_timeFmt != 0) ? 1u : 0u;
    uint8_t skipIntro = (g_skipIntro != 0) ? 1u : 0u;
    uint8_t musicStart = (g_musicStart != 0) ? 1u : 0u;
    uint8_t musicPlaylist = (g_musicPlaylist != 0) ? 1u : 0u;
    uint8_t muteUnfocused = (g_muteUnfocused != 0) ? 1u : 0u;
    uint8_t prismVfx = (g_prismVfx <= 3u) ? g_prismVfx : 1u;
    uint8_t chromaAb = (g_chromaAb != 0) ? 1u : 0u;
    int32_t aaSamples = (g_aaSamples >= 0 && g_aaSamples <= 16) ? g_aaSamples : 0;
    uint8_t themeMode = (g_colorTheme <= 3u) ? g_colorTheme : 0u;
    uint8_t colorTheme = (themeMode <= 2u) ? themeMode : 0u;
    uint8_t customThemeInited = (g_customThemeInited != 0) ? 1u : 0u;
    CustomThemeSettings effTheme = GetEffectiveTheme();
    uint8_t cubeDistortion = (effTheme.cubeDistortion != 0) ? 1u : 0u;
    uint8_t timerMode = (g_timerMode <= 3u) ? g_timerMode : 3u;
    float timerSize = std::isfinite(g_timerSize) ? g_timerSizePresets[NearestTimerSizeIdx(g_timerSize)] : 1.0f;
    uint8_t timerPos = (g_timerPos <= 3u) ? g_timerPos : 1u;
    uint8_t edgeTimer = (g_edgeTimerMode <= 2u) ? g_edgeTimerMode : 0u;
    uint8_t prismCounterType = (g_prismCounterType <= 2u) ? g_prismCounterType : 2u;
    float prismCounterSize = std::isfinite(g_prismCounterSize) ? g_timerSizePresets[NearestTimerSizeIdx(g_prismCounterSize)] : 1.0f;
    uint8_t prismCounterPos = (g_prismCounterPos <= 3u) ? g_prismCounterPos : 1u;
    int32_t timerBgOpacity = std::clamp(g_timerBgOpacity, 0, 100);
    int32_t prismBgOpacity = std::clamp(g_prismBgOpacity, 0, 100);
    int32_t timerShadowOpacity = std::clamp(g_timerShadowOpacity, 0, 100);
    int32_t prismShadowOpacity = std::clamp(g_prismShadowOpacity, 0, 100);
    uint8_t minimapEnable = (g_minimapEnable != 0) ? 1u : 0u;
    uint8_t minimapEnableOverride = (g_minimapEnableOverride != 0) ? 1u : 0u;
    int32_t minimapVisibleIni = (g_lastIniMinimapVisible == 0 || g_lastIniMinimapVisible == 1)
        ? static_cast<int32_t>(g_lastIniMinimapVisible) : -1;
    float   minimapWidth = g_uiScalePresets[NearestMinimapPresetIdx(g_minimapWidth)];
    float   minimapHeight = g_uiScalePresets[NearestMinimapPresetIdx(g_minimapHeight)];
    float   minimapRange = g_uiScalePresets[NearestMinimapPresetIdx(g_minimapRange)];
    uint8_t minimapWidthOverride = (g_minimapWidthOverride != 0) ? 1u : 0u;
    uint8_t minimapHeightOverride = (g_minimapHeightOverride != 0) ? 1u : 0u;
    uint8_t minimapRangeOverride = (g_minimapRangeOverride != 0) ? 1u : 0u;
    int32_t minimapWidthIni = (g_lastIniMinimapWidth > 0) ? g_lastIniMinimapWidth : g_iniMinimapWidth;
    int32_t minimapHeightIni = (g_lastIniMinimapHeight > 0) ? g_lastIniMinimapHeight : g_iniMinimapHeight;
    float   minimapCellWIni = (g_lastIniMinimapCellW > 0.0f) ? g_lastIniMinimapCellW : g_iniMinimapCellW;
    float   minimapCellHIni = (g_lastIniMinimapCellH > 0.0f) ? g_lastIniMinimapCellH : g_iniMinimapCellH;
    int32_t minimapBgOpacity = std::clamp(g_minimapBgOpacity, 0, 100);
    uint8_t minimapPos = (g_minimapPos <= 3u) ? g_minimapPos : 0u;
    uint8_t minimapPosOverride = (g_minimapPosOverride != 0) ? 1u : 0u;
    int32_t minimapAlignXIni = (g_lastIniMinimapAlignX >= 0 && g_lastIniMinimapAlignX <= 2) ? g_lastIniMinimapAlignX : -1;
    int32_t minimapAlignYIni = (g_lastIniMinimapAlignY >= 0 && g_lastIniMinimapAlignY <= 2) ? g_lastIniMinimapAlignY : -1;
    uint8_t speedometerEnable = (g_speedometerEnable != 0) ? 1u : 0u;
    float   speedometerSize = std::isfinite(g_speedometerSize) ? g_timerSizePresets[NearestTimerSizeIdx(g_speedometerSize)] : 1.0f;
    int32_t speedometerBgOpacity = std::clamp(g_speedometerBgOpacity, 0, 100);
    int32_t speedometerShadowOpacity = std::clamp(g_speedometerShadowOpacity, 0, 100);
    uint8_t speedometerPos = (g_speedometerPos <= 3u) ? g_speedometerPos : 3u;

    std::string out;
    out.reserve(1536);
    out += "; EDGE PC Mod settings (auto-managed)\r\n";
    out += "[edgemod]\r\n";
    auto writeInt = [&](const char* k, int32_t v) {
        char num[24]{};
        std::snprintf(num, sizeof(num), "%d", v);
        out += k;
        out += " = ";
        out += num;
        out += "\r\n";
    };
    auto writeFloat = [&](const char* k, double v) {
        out += k;
        out += " = ";
        AppendAsciiFloat(out, v);
        out += "\r\n";
    };

    writeInt("schema_version", kModIniSchemaVersion);
    writeFloat("ui_scale", uiScale);
    writeFloat("ui_scale_ini", uiScaleIni);
    writeInt("ui_auto", static_cast<int32_t>(uiAuto));
    writeInt("ui_auto_ini", uiAutoIni);
    writeFloat("cam_dist", camDist);
    writeFloat("cam_dist_ini", camDistIni);
    writeFloat("cam_fov", camFov);
    writeFloat("cam_fov_ini", camFovIni);
    writeInt("time_fmt", timeFmt);
    writeInt("skip_intro", skipIntro);
    writeInt("music_start", musicStart);
    writeInt("music_playlist", musicPlaylist);
    writeInt("mute_unfocused", muteUnfocused);
    writeInt("prism_vfx", prismVfx);
    writeInt("chroma_ab", chromaAb);
    writeInt("aa_samples", aaSamples);
    writeInt("finish_effect", effTheme.finishEffect);
    writeInt("finish_platform", effTheme.finishPlatform);
    writeInt("color_theme", colorTheme);
    writeInt("cube_distortion", cubeDistortion);
    writeInt("theme_mode", themeMode);
    writeInt("theme_custom_inited", customThemeInited);
    writeInt("theme_custom_terrain", std::clamp<int>(g_customTheme.terrain, 0, 2));
    writeInt("theme_custom_wall_gradient", g_customTheme.wallGradient ? 1 : 0);
    writeInt("theme_custom_planets", std::clamp<int>(g_customTheme.planets, 0, 2));
    writeInt("theme_custom_stars", g_customTheme.stars ? 1 : 0);
    writeInt("theme_custom_player_cube", std::clamp<int>(g_customTheme.playerCube, 0, 2));
    writeInt("theme_custom_dark_cube", std::clamp<int>(g_customTheme.darkCube, 0, 2));
    writeInt("theme_custom_cube_distortion", g_customTheme.cubeDistortion ? 1 : 0);
    writeInt("theme_custom_prisms", std::clamp<int>(g_customTheme.prisms, 0, 2));
    writeInt("theme_custom_moving_platforms", std::clamp<int>(g_customTheme.movingPlatforms, 0, 2));
    writeInt("theme_custom_platform_borders", std::clamp<int>(g_customTheme.platformBorders, 0, 2));
    writeInt("theme_custom_falling_platforms", std::clamp<int>(g_customTheme.fallingPlatforms, 0, 2));
    writeInt("theme_custom_bumpers", std::clamp<int>(g_customTheme.bumpers, 0, 2));
    writeInt("theme_custom_switches", std::clamp<int>(g_customTheme.switches, 0, 2));
    writeInt("theme_custom_holo_switches", std::clamp<int>(g_customTheme.holoSwitches, 0, 2));
    writeInt("theme_custom_shrinkers", std::clamp<int>(g_customTheme.shrinkers, 0, 2));
    writeInt("theme_custom_finish_platform", std::clamp<int>(g_customTheme.finishPlatform, 0, 2));
    writeInt("theme_custom_finish_effect", std::clamp<int>(g_customTheme.finishEffect, 0, 2));
    writeInt("timer_mode", timerMode);
    writeFloat("timer_size", timerSize);
    writeInt("timer_pos", timerPos);
    writeInt("edge_timer", edgeTimer);
    writeInt("prism_counter_type", prismCounterType);
    writeFloat("prism_counter_size", prismCounterSize);
    writeInt("prism_counter_pos", prismCounterPos);
    writeInt("timer_bg_opacity", timerBgOpacity);
    writeInt("prism_bg_opacity", prismBgOpacity);
    writeInt("timer_shadow_opacity", timerShadowOpacity);
    writeInt("prism_shadow_opacity", prismShadowOpacity);
    writeFloat("minimap_width", minimapWidth);
    writeInt("minimap_width_override", minimapWidthOverride);
    writeInt("minimap_width_ini", minimapWidthIni);
    writeFloat("minimap_height", minimapHeight);
    writeInt("minimap_height_override", minimapHeightOverride);
    writeInt("minimap_height_ini", minimapHeightIni);
    writeFloat("minimap_range", minimapRange);
    writeInt("minimap_range_override", minimapRangeOverride);
    writeFloat("minimap_cell_w_ini", minimapCellWIni);
    writeFloat("minimap_cell_h_ini", minimapCellHIni);
    writeInt("minimap_bg_opacity", minimapBgOpacity);
    writeInt("minimap_enable", minimapEnable);
    writeInt("minimap_enable_override", minimapEnableOverride);
    writeInt("minimap_visible_ini", minimapVisibleIni);
    writeInt("minimap_pos", minimapPos);
    writeInt("minimap_pos_override", minimapPosOverride);
    writeInt("minimap_align_x_ini", minimapAlignXIni);
    writeInt("minimap_align_y_ini", minimapAlignYIni);
    writeInt("speedometer_enable", speedometerEnable);
    writeFloat("speedometer_size", speedometerSize);
    writeInt("speedometer_bg_opacity", speedometerBgOpacity);
    writeInt("speedometer_shadow_opacity", speedometerShadowOpacity);
    writeInt("speedometer_pos", speedometerPos);

    wchar_t tmpPath[MAX_PATH + 16]{};
    if (std::swprintf(tmpPath, MAX_PATH + 16, L"%ls.tmp", iniPath) <= 0) return;

    HANDLE hFile = CreateFileW(
        tmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) return;

    const char* ptr = out.data();
    size_t remaining = out.size();
    bool writeOk = true;
    while (remaining > 0) {
        DWORD chunk = static_cast<DWORD>((std::min)(remaining, static_cast<size_t>(65536)));
        DWORD written = 0;
        if (!WriteFile(hFile, ptr, chunk, &written, nullptr) || written != chunk) {
            writeOk = false;
            break;
        }
        ptr += written;
        remaining -= written;
    }
    if (writeOk) {
        FlushFileBuffers(hFile);
    }
    CloseHandle(hFile);

    if (!writeOk) {
        DeleteFileW(tmpPath);
        return;
    }

    bool moved = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (MoveFileExW(tmpPath, iniPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            moved = true;
            break;
        }
        Sleep(5);
    }
    if (!moved) {
        DeleteFileW(tmpPath);
    }
}

struct LegacyTtsavSnapshot {
    bool hasUiScale = false;      double uiScale = 1.0;
    bool hasUiScaleIni = false;   double uiScaleIni = 0.0;
    bool hasUiAuto = false;       double uiAuto = 1.0;
    bool hasUiAutoIni = false;    double uiAutoIni = -1.0;
    bool hasCamDist = false;      double camDist = 1.0;
    bool hasCamDistIni = false;   double camDistIni = 0.0;
    bool hasCamFov = false;       double camFov = -1.0;
    bool hasCamFovIni = false;    double camFovIni = -99.0;
    bool hasTimeFmt = false;      double timeFmt = 0.0;
    bool hasSkipIntro = false;    double skipIntro = 0.0;
    bool hasMusicStart = false;   double musicStart = 0.0;
    bool hasMusicPlaylist = false;double musicPlaylist = 0.0;
    bool hasPrismVfx = false;     double prismVfx = 1.0;
    bool hasChromaAb = false;     double chromaAb = 1.0;
    bool hasAaSamples = false;    double aaSamples = 8.0;
    bool hasFinishEffect = false; double finishEffect = 0.0;
    bool hasFinishPlatform = false;double finishPlatform = 0.0;
    bool hasColorTheme = false;   double colorTheme = 0.0;
    bool hasTimerMode = false;    double timerMode = 3.0;
    bool hasTimerSize = false;    double timerSize = 1.0;
    bool hasTimerPos = false;     double timerPos = 1.0;
    bool hasEdgeTimer = false;    double edgeTimer = 0.0;
};

LegacyTtsavSnapshot g_legacySnap{};
bool g_legacyTtsavCaptured = false;

static bool LoadModSettingsFromIni(bool* outNeedsResave) {
    if (outNeedsResave != nullptr) *outNeedsResave = true;

    wchar_t iniPath[MAX_PATH]{};
    if (!GetModSettingsPathW(iniPath, MAX_PATH, false)) return false;

    HANDLE hFile = CreateFileW(
        iniPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < 0 || fileSize.QuadPart > 1048576) {
        CloseHandle(hFile);
        return false;
    }

    std::string data(static_cast<size_t>(fileSize.QuadPart), '\0');
    size_t totalRead = 0;
    while (totalRead < data.size()) {
        DWORD toRead = static_cast<DWORD>((std::min)(data.size() - totalRead, static_cast<size_t>(65536)));
        DWORD got = 0;
        if (!ReadFile(hFile, &data[totalRead], toRead, &got, nullptr) || got == 0) break;
        totalRead += got;
    }
    CloseHandle(hFile);
    data.resize(totalRead);

    // Strip UTF-8 BOM if user edited the INI in Notepad
    size_t pos = 0;
    if (data.size() >= 3 &&
        static_cast<uint8_t>(data[0]) == 0xEF &&
        static_cast<uint8_t>(data[1]) == 0xBB &&
        static_cast<uint8_t>(data[2]) == 0xBF) {
        pos = 3;
    }

    bool inEdgeModSec = false;
    int32_t schemaVer = 0;
    uint64_t seenMask = 0;
    uint64_t seenMask2 = 0;
    constexpr uint64_t kAllModKeysMask = (1ull << 54) - 1ull;
    constexpr uint64_t kAllModKeysMask2 = (1ull << 19) - 1ull;
    uint8_t legacyColorTheme = 0;
    bool hasThemeMode = false;

    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        size_t lineEnd = (nl == std::string::npos) ? data.size() : nl;
        const char* p = data.data() + pos;
        const char* end = data.data() + lineEnd;
        pos = (nl == std::string::npos) ? data.size() : (nl + 1);

        TrimAsciiInPlace(p, end);
        if (p >= end || *p == ';' || *p == '#') continue;

        if (*p == '[') {
            const char* close = static_cast<const char*>(std::memchr(p + 1, ']', static_cast<size_t>(end - (p + 1))));
            if (close != nullptr) {
                const char* secP = p + 1;
                const char* secE = close;
                TrimAsciiInPlace(secP, secE);
                inEdgeModSec = AsciiEqIc(secP, secE, "edgemod");
            }
            continue;
        }

        if (!inEdgeModSec) continue;

        const char* eq = static_cast<const char*>(std::memchr(p, '=', static_cast<size_t>(end - p)));
        if (eq == nullptr) continue;

        const char* kP = p;
        const char* kE = eq;
        TrimAsciiInPlace(kP, kE);
        const char* vP = eq + 1;
        const char* vE = end;
        TrimAsciiInPlace(vP, vE);
        if (kP >= kE) continue;

        double dv = 0.0;
        int32_t iv = 0;

        if (AsciiEqIc(kP, kE, "schema_version")) {
            if (ParseAsciiInt(vP, vE, &iv)) schemaVer = iv;
        } else if (AsciiEqIc(kP, kE, "ui_scale")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = static_cast<float>(dv);
                if (ClampUiScale(&fv)) { g_uiScale = fv; seenMask |= (1u << 0); }
            }
        } else if (AsciiEqIc(kP, kE, "ui_scale_ini")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = static_cast<float>(dv);
                if (std::isfinite(fv)) { g_lastIniScale = fv; seenMask |= (1u << 1); }
            }
        } else if (AsciiEqIc(kP, kE, "ui_auto")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_autoScale = (iv != 0) ? 1u : 0u;
                seenMask |= (1u << 2);
            }
        } else if (AsciiEqIc(kP, kE, "ui_auto_ini")) {
            if (ParseAsciiInt(vP, vE, &iv) && (iv == -1 || iv == 0 || iv == 1)) {
                g_lastIniAuto = static_cast<int8_t>(iv);
                seenMask |= (1u << 3);
            }
        } else if (AsciiEqIc(kP, kE, "cam_dist")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = static_cast<float>(dv);
                if (IsValidCamValue(kDistDesc, fv)) { g_camDist = fv; seenMask |= (1u << 4); }
            }
        } else if (AsciiEqIc(kP, kE, "cam_dist_ini")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = static_cast<float>(dv);
                if (fv == kDistDesc.unsetFlt || IsValidCamValue(kDistDesc, fv)) {
                    g_lastIniDist = fv;
                    seenMask |= (1u << 5);
                }
            }
        } else if (AsciiEqIc(kP, kE, "cam_fov")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = static_cast<float>(dv);
                if (IsValidCamValue(kFovDesc, fv)) { g_camFov = fv; seenMask |= (1u << 6); }
            }
        } else if (AsciiEqIc(kP, kE, "cam_fov_ini")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = static_cast<float>(dv);
                if (fv == kFovDesc.unsetFlt || IsValidCamValue(kFovDesc, fv)) {
                    g_lastIniFov = fv;
                    seenMask |= (1u << 7);
                }
            }
        } else if (AsciiEqIc(kP, kE, "time_fmt")) {
            if (ParseAsciiInt(vP, vE, &iv) && (iv == 0 || iv == 1)) {
                g_timeFmt = static_cast<uint8_t>(iv);
                seenMask |= (1u << 8);
            }
        } else if (AsciiEqIc(kP, kE, "skip_intro")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_skipIntro = (iv != 0) ? 1u : 0u;
                seenMask |= (1u << 9);
            }
        } else if (AsciiEqIc(kP, kE, "music_start")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_musicStart = (iv != 0) ? 1u : 0u;
                seenMask |= (1u << 10);
            }
        } else if (AsciiEqIc(kP, kE, "music_playlist")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_musicPlaylist = (iv != 0) ? 1u : 0u;
                seenMask |= (1u << 11);
            }
        } else if (AsciiEqIc(kP, kE, "mute_unfocused")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_muteUnfocused = (iv != 0) ? 1u : 0u;
                seenMask |= (1u << 22);
            }
        } else if (AsciiEqIc(kP, kE, "prism_vfx")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 3) {
                g_prismVfx = static_cast<uint8_t>(iv);
                seenMask |= (1u << 12);
            }
        } else if (AsciiEqIc(kP, kE, "chroma_ab")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_chromaAb = (iv != 0) ? 1u : 0u;
                seenMask |= (1u << 13);
            }
        } else if (AsciiEqIc(kP, kE, "aa_samples")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 16) {
                g_aaSamples = iv;
                seenMask |= (1u << 14);
            }
        } else if (AsciiEqIc(kP, kE, "finish_effect")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) {
                g_finishEffect = static_cast<uint8_t>(iv);
                seenMask |= (1u << 15);
            }
        } else if (AsciiEqIc(kP, kE, "finish_platform")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) {
                g_finishPlatform = static_cast<uint8_t>(iv);
                seenMask |= (1u << 16);
            }
        } else if (AsciiEqIc(kP, kE, "color_theme")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) {
                legacyColorTheme = static_cast<uint8_t>(iv);
                if (!hasThemeMode) g_colorTheme = legacyColorTheme;
                seenMask |= (1u << 17);
            }
        } else if (AsciiEqIc(kP, kE, "theme_mode")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 3) {
                g_colorTheme = static_cast<uint8_t>(iv);
                hasThemeMode = true;
                seenMask2 |= (1ull << 0);
            }
        } else if (AsciiEqIc(kP, kE, "theme_custom_inited")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_customThemeInited = (iv != 0) ? 1u : 0u;
                seenMask2 |= (1ull << 1);
            }
        } else if (AsciiEqIc(kP, kE, "theme_custom_terrain")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.terrain = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 2); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_wall_gradient")) {
            if (ParseAsciiInt(vP, vE, &iv)) { g_customTheme.wallGradient = (iv != 0) ? 1u : 0u; seenMask2 |= (1ull << 3); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_planets")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.planets = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 4); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_stars")) {
            if (ParseAsciiInt(vP, vE, &iv)) { g_customTheme.stars = (iv != 0) ? 1u : 0u; seenMask2 |= (1ull << 5); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_player_cube")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.playerCube = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 6); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_dark_cube")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.darkCube = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 7); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_cube_distortion")) {
            if (ParseAsciiInt(vP, vE, &iv)) { g_customTheme.cubeDistortion = (iv != 0) ? 1u : 0u; seenMask2 |= (1ull << 8); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_prisms")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.prisms = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 9); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_moving_platforms")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.movingPlatforms = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 10); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_platform_borders")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.platformBorders = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 11); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_falling_platforms")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.fallingPlatforms = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 12); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_bumpers")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.bumpers = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 13); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_switches")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.switches = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 14); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_holo_switches")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.holoSwitches = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 15); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_shrinkers")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.shrinkers = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 16); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_finish_platform")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.finishPlatform = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 17); }
        } else if (AsciiEqIc(kP, kE, "theme_custom_finish_effect")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) { g_customTheme.finishEffect = static_cast<uint8_t>(iv); seenMask2 |= (1ull << 18); }
        } else if (AsciiEqIc(kP, kE, "timer_mode")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 3) {
                g_timerMode = static_cast<uint8_t>(iv);
                seenMask |= (1u << 18);
            }
        } else if (AsciiEqIc(kP, kE, "timer_size")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = static_cast<float>(dv);
                if (fv < 0.25f) fv = 0.25f;
                if (fv > 2.0f) fv = 2.0f;
                g_timerSize = g_timerSizePresets[NearestTimerSizeIdx(fv)];
                seenMask |= (1u << 19);
            }
        } else if (AsciiEqIc(kP, kE, "timer_pos")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 3) {
                g_timerPos = static_cast<uint8_t>(iv);
                seenMask |= (1u << 20);
            }
        } else if (AsciiEqIc(kP, kE, "edge_timer")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) {
                g_edgeTimerMode = static_cast<uint8_t>(iv);
                seenMask |= (1u << 21);
            }
        } else if (AsciiEqIc(kP, kE, "prism_counter_type")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 2) {
                g_prismCounterType = static_cast<uint8_t>(iv);
                seenMask |= (1u << 23);
            }
        } else if (AsciiEqIc(kP, kE, "prism_counter_size")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = static_cast<float>(dv);
                if (fv < 0.25f) fv = 0.25f;
                if (fv > 2.0f) fv = 2.0f;
                g_prismCounterSize = g_timerSizePresets[NearestTimerSizeIdx(fv)];
                seenMask |= (1u << 29);
            }
        } else if (AsciiEqIc(kP, kE, "prism_counter_pos")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 3) {
                g_prismCounterPos = static_cast<uint8_t>(iv);
                seenMask |= (1u << 24);
            }
        } else if (AsciiEqIc(kP, kE, "timer_bg_opacity")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 100) {
                g_timerBgOpacity = std::clamp(((iv + 5) / 10) * 10, 0, 100);
                seenMask |= (1u << 25);
            }
        } else if (AsciiEqIc(kP, kE, "prism_bg_opacity")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 100) {
                g_prismBgOpacity = std::clamp(((iv + 5) / 10) * 10, 0, 100);
                seenMask |= (1u << 26);
            }
        } else if (AsciiEqIc(kP, kE, "timer_shadow_opacity")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 100) {
                g_timerShadowOpacity = std::clamp(((iv + 5) / 10) * 10, 0, 100);
                seenMask |= (1u << 27);
            }
        } else if (AsciiEqIc(kP, kE, "prism_shadow_opacity")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 100) {
                g_prismShadowOpacity = std::clamp(((iv + 5) / 10) * 10, 0, 100);
                seenMask |= (1u << 28);
            }
        } else if (AsciiEqIc(kP, kE, "cube_distortion")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_cubeDistortion = (iv != 0) ? 1u : 0u;
                seenMask |= (1ull << 30);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_width")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = std::clamp(static_cast<float>(dv), 0.5f, 2.5f);
                g_minimapWidth = g_uiScalePresets[NearestMinimapPresetIdx(fv)];
                seenMask |= (1ull << 31);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_width_override")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_minimapWidthOverride = (iv != 0) ? 1u : 0u;
                seenMask |= (1ull << 32);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_width_ini")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv > 0) {
                g_lastIniMinimapWidth = iv;
                seenMask |= (1ull << 33);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_height")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = std::clamp(static_cast<float>(dv), 0.5f, 2.5f);
                g_minimapHeight = g_uiScalePresets[NearestMinimapPresetIdx(fv)];
                seenMask |= (1ull << 34);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_height_override")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_minimapHeightOverride = (iv != 0) ? 1u : 0u;
                seenMask |= (1ull << 35);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_height_ini")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv > 0) {
                g_lastIniMinimapHeight = iv;
                seenMask |= (1ull << 36);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_range")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = std::clamp(static_cast<float>(dv), 0.5f, 2.5f);
                g_minimapRange = g_uiScalePresets[NearestMinimapPresetIdx(fv)];
                seenMask |= (1ull << 37);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_range_override")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_minimapRangeOverride = (iv != 0) ? 1u : 0u;
                seenMask |= (1ull << 38);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_cell_w_ini")) {
            if (ParseAsciiDouble(vP, vE, &dv) && dv > 0.0) {
                g_lastIniMinimapCellW = static_cast<float>(dv);
                seenMask |= (1ull << 39);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_cell_h_ini")) {
            if (ParseAsciiDouble(vP, vE, &dv) && dv > 0.0) {
                g_lastIniMinimapCellH = static_cast<float>(dv);
                seenMask |= (1ull << 40);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_bg_opacity")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 100) {
                g_minimapBgOpacity = std::clamp(((iv + 5) / 10) * 10, 0, 100);
                seenMask |= (1ull << 41);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_enable")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_minimapEnable = (iv != 0) ? 1u : 0u;
                seenMask |= (1ull << 42);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_enable_override")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_minimapEnableOverride = (iv != 0) ? 1u : 0u;
                seenMask |= (1ull << 43);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_visible_ini")) {
            if (ParseAsciiInt(vP, vE, &iv) && (iv == -1 || iv == 0 || iv == 1)) {
                g_lastIniMinimapVisible = static_cast<int8_t>(iv);
                seenMask |= (1ull << 44);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_pos")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 3) {
                g_minimapPos = static_cast<uint8_t>(iv);
                seenMask |= (1ull << 45);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_pos_override")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_minimapPosOverride = (iv != 0) ? 1u : 0u;
                seenMask |= (1ull << 46);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_align_x_ini")) {
            if (ParseAsciiInt(vP, vE, &iv) && (iv == -1 || (iv >= 0 && iv <= 2))) {
                g_lastIniMinimapAlignX = iv;
                seenMask |= (1ull << 47);
            }
        } else if (AsciiEqIc(kP, kE, "minimap_align_y_ini")) {
            if (ParseAsciiInt(vP, vE, &iv) && (iv == -1 || (iv >= 0 && iv <= 2))) {
                g_lastIniMinimapAlignY = iv;
                seenMask |= (1ull << 48);
            }
        } else if (AsciiEqIc(kP, kE, "speedometer_enable")) {
            if (ParseAsciiInt(vP, vE, &iv)) {
                g_speedometerEnable = (iv != 0) ? 1u : 0u;
                seenMask |= (1ull << 49);
            }
        } else if (AsciiEqIc(kP, kE, "speedometer_size")) {
            if (ParseAsciiDouble(vP, vE, &dv)) {
                float fv = static_cast<float>(dv);
                if (fv < 0.25f) fv = 0.25f;
                if (fv > 2.0f) fv = 2.0f;
                g_speedometerSize = g_timerSizePresets[NearestTimerSizeIdx(fv)];
                seenMask |= (1ull << 50);
            }
        } else if (AsciiEqIc(kP, kE, "speedometer_bg_opacity")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 100) {
                g_speedometerBgOpacity = std::clamp(((iv + 5) / 10) * 10, 0, 100);
                seenMask |= (1ull << 51);
            }
        } else if (AsciiEqIc(kP, kE, "speedometer_shadow_opacity")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 100) {
                g_speedometerShadowOpacity = std::clamp(((iv + 5) / 10) * 10, 0, 100);
                seenMask |= (1ull << 52);
            }
        } else if (AsciiEqIc(kP, kE, "speedometer_pos")) {
            if (ParseAsciiInt(vP, vE, &iv) && iv >= 0 && iv <= 3) {
                g_speedometerPos = static_cast<uint8_t>(iv);
                seenMask |= (1ull << 53);
            }
        }
    }

    if (!hasThemeMode) {
        g_customTheme = MakePresetThemeSettings(legacyColorTheme);
        g_customTheme.finishEffect   = (g_finishEffect <= 2u) ? g_finishEffect : 0u;
        g_customTheme.finishPlatform = (g_finishPlatform <= 2u) ? g_finishPlatform : 0u;
        g_customTheme.cubeDistortion = (g_cubeDistortion != 0) ? 1u : 0u;
        bool isExactPc     = (legacyColorTheme == 0u && g_customTheme.finishEffect == 0u && g_customTheme.finishPlatform == 0u && g_customTheme.cubeDistortion == 0u);
        bool isExactMobile = (legacyColorTheme == 1u && g_customTheme.finishEffect == 1u && g_customTheme.finishPlatform == 1u && g_customTheme.cubeDistortion == 0u);
        bool isExactJava   = (legacyColorTheme == 2u && g_customTheme.finishEffect == 2u && g_customTheme.finishPlatform == 2u && g_customTheme.cubeDistortion == 1u);
        if (isExactPc || isExactMobile || isExactJava) {
            g_colorTheme = legacyColorTheme;
            g_customThemeInited = 0;
        } else {
            g_colorTheme = 3u;
            g_customThemeInited = 1u;
        }
    }

    if (outNeedsResave != nullptr) {
        *outNeedsResave = (schemaVer != kModIniSchemaVersion) || (seenMask != kAllModKeysMask) || (seenMask2 != kAllModKeysMask2);
    }
    return true;
}

static void ApplyLegacyTtsavSnapshot() {
    if (!g_legacyTtsavCaptured) return;
    const auto& s = g_legacySnap;

    if (s.hasUiScale) {
        float fv = static_cast<float>(s.uiScale);
        if (ClampUiScale(&fv)) g_uiScale = fv;
    }
    if (s.hasUiScaleIni) {
        float fv = static_cast<float>(s.uiScaleIni);
        if (std::isfinite(fv)) g_lastIniScale = fv;
    }
    if (s.hasUiAuto) {
        g_autoScale = (static_cast<int>(std::lrint(s.uiAuto)) != 0) ? 1u : 0u;
    }
    if (s.hasUiAutoIni) {
        int iv = static_cast<int>(std::lrint(s.uiAutoIni));
        if (iv == 0 || iv == 1) g_lastIniAuto = static_cast<int8_t>(iv);
    }
    if (s.hasCamDist) {
        float fv = static_cast<float>(s.camDist);
        if (IsValidCamValue(kDistDesc, fv)) g_camDist = fv;
    }
    if (s.hasCamDistIni) {
        float fv = static_cast<float>(s.camDistIni);
        if (IsValidCamValue(kDistDesc, fv)) g_lastIniDist = fv;
    }
    if (s.hasCamFov) {
        float fv = static_cast<float>(s.camFov);
        if (IsValidCamValue(kFovDesc, fv)) g_camFov = fv;
    }
    if (s.hasCamFovIni) {
        float fv = static_cast<float>(s.camFovIni);
        if (IsValidCamValue(kFovDesc, fv)) g_lastIniFov = fv;
    }
    if (s.hasTimeFmt) {
        int iv = static_cast<int>(std::lrint(s.timeFmt));
        if (iv == 0 || iv == 1) g_timeFmt = static_cast<uint8_t>(iv);
    }
    if (s.hasSkipIntro) {
        g_skipIntro = (static_cast<int>(std::lrint(s.skipIntro)) != 0) ? 1u : 0u;
    }
    if (s.hasMusicStart) {
        g_musicStart = (static_cast<int>(std::lrint(s.musicStart)) != 0) ? 1u : 0u;
    }
    if (s.hasMusicPlaylist) {
        g_musicPlaylist = (static_cast<int>(std::lrint(s.musicPlaylist)) != 0) ? 1u : 0u;
    }
    if (s.hasPrismVfx) {
        int iv = static_cast<int>(std::lrint(s.prismVfx));
        if (iv >= 0 && iv <= 3) g_prismVfx = static_cast<uint8_t>(iv);
    }
    if (s.hasChromaAb) {
        g_chromaAb = (static_cast<int>(std::lrint(s.chromaAb)) != 0) ? 1u : 0u;
    }
    if (s.hasAaSamples) {
        int iv = static_cast<int>(std::lrint(s.aaSamples));
        if (iv >= 0 && iv <= 16) g_aaSamples = iv;
    }
    if (s.hasFinishEffect) {
        int iv = static_cast<int>(std::lrint(s.finishEffect));
        if (iv >= 0 && iv <= 2) g_finishEffect = static_cast<uint8_t>(iv);
    }
    if (s.hasFinishPlatform) {
        int iv = static_cast<int>(std::lrint(s.finishPlatform));
        if (iv >= 0 && iv <= 2) g_finishPlatform = static_cast<uint8_t>(iv);
    }
    if (s.hasColorTheme) {
        int iv = static_cast<int>(std::lrint(s.colorTheme));
        if (iv >= 0 && iv <= 2) g_colorTheme = static_cast<uint8_t>(iv);
    }
    if (s.hasTimerMode) {
        int iv = static_cast<int>(std::lrint(s.timerMode));
        if (iv >= 0 && iv <= 3) g_timerMode = static_cast<uint8_t>(iv);
    }
    if (s.hasTimerSize) {
        float fv = static_cast<float>(s.timerSize);
        if (fv < 0.25f) fv = 0.25f;
        if (fv > 2.0f) fv = 2.0f;
        g_timerSize = g_timerSizePresets[NearestTimerSizeIdx(fv)];
    }
    if (s.hasTimerPos) {
        int iv = static_cast<int>(std::lrint(s.timerPos));
        if (iv >= 0 && iv <= 3) g_timerPos = static_cast<uint8_t>(iv);
    }
    if (s.hasEdgeTimer) {
        int iv = static_cast<int>(std::lrint(s.edgeTimer));
        if (iv >= 0 && iv <= 2) g_edgeTimerMode = static_cast<uint8_t>(iv);
    }
}

static bool ReconcileCamIniOverride(const CamIniDesc& d) {
    bool changed = false;
    if (*d.gIni != d.unsetFlt) {
        if (std::fabs(*d.gIni - *d.gLastIni) > 0.0005f) {
            *d.gLastIni = *d.gIni;
            *d.gVal = *d.gIni;
            changed = true;
        }
    } else if (*d.gLastIni != d.unsetFlt) {
        *d.gLastIni = d.unsetFlt;
        changed = true;
    }
    return changed;
}

static uint8_t __cdecl CaptureLegacyTtsav_Impl(void* saveObj, const char* nextKey) {
    if (saveObj == nullptr) return 0;

    if (!ModSettingsFileExists() &&
        *reinterpret_cast<const uint8_t*>(static_cast<const uint8_t*>(saveObj) + 8) == 7) {
        LegacyTtsavSnapshot snap{};
        snap.hasUiScale       = ReadTtsavDouble(saveObj, "uiScaleF", &snap.uiScale);
        snap.hasUiScaleIni    = ReadTtsavDouble(saveObj, "uiScaleFIni", &snap.uiScaleIni);
        snap.hasUiAuto        = ReadTtsavDouble(saveObj, "uiAuto", &snap.uiAuto);
        snap.hasUiAutoIni     = ReadTtsavDouble(saveObj, "uiAutoIni", &snap.uiAutoIni);
        snap.hasCamDist       = ReadTtsavDouble(saveObj, "camDist", &snap.camDist);
        snap.hasCamDistIni    = ReadTtsavDouble(saveObj, "camDistIni", &snap.camDistIni);
        snap.hasCamFov        = ReadTtsavDouble(saveObj, "camFov", &snap.camFov);
        snap.hasCamFovIni     = ReadTtsavDouble(saveObj, "camFovIni", &snap.camFovIni);
        snap.hasTimeFmt       = ReadTtsavDouble(saveObj, "timeFmt", &snap.timeFmt);
        snap.hasSkipIntro     = ReadTtsavDouble(saveObj, "skipIntro", &snap.skipIntro);
        snap.hasMusicStart    = ReadTtsavDouble(saveObj, "musicStart", &snap.musicStart);
        snap.hasMusicPlaylist = ReadTtsavDouble(saveObj, "musicPlaylist", &snap.musicPlaylist);
        snap.hasPrismVfx      = ReadTtsavDouble(saveObj, "prismVfx", &snap.prismVfx);
        snap.hasChromaAb      = ReadTtsavDouble(saveObj, "chromaAb", &snap.chromaAb);
        snap.hasAaSamples     = ReadTtsavDouble(saveObj, "aaSamples", &snap.aaSamples);
        snap.hasFinishEffect  = ReadTtsavDouble(saveObj, "finishEffect", &snap.finishEffect);
        snap.hasFinishPlatform= ReadTtsavDouble(saveObj, "finishPlatform", &snap.finishPlatform);
        snap.hasColorTheme    = ReadTtsavDouble(saveObj, "colorTheme", &snap.colorTheme);
        snap.hasTimerMode     = ReadTtsavDouble(saveObj, "timerMode", &snap.timerMode);
        snap.hasTimerSize     = ReadTtsavDouble(saveObj, "timerSize", &snap.timerSize);
        snap.hasTimerPos      = ReadTtsavDouble(saveObj, "timerPos", &snap.timerPos);
        snap.hasEdgeTimer     = ReadTtsavDouble(saveObj, "edgeTimer", &snap.edgeTimer);
        g_legacySnap = snap;
        g_legacyTtsavCaptured = true;
    }

    return reinterpret_cast<uint8_t(__thiscall*)(void*, const char*)>(0x51c660)(saveObj, nextKey);
}

static void __declspec(naked) hk_loadTtsav() {
    // At 0x492558: stack has [esp+4] = nextKey ("fullScreen" pushed at 0x492553), esi = saveObj
    __asm {
        push DWORD PTR [esp + 4]
        push esi
        call CaptureLegacyTtsav_Impl
        add esp, 8
        ret 4
    }
}

static void LoadModSettingsOnStartup_Impl(uint8_t* userSettings) {
    uint8_t legacyFx = (userSettings != nullptr) ? userSettings[0x1d] : 1u;

    g_uiScale       = 1.0f;
    g_lastIniScale  = 0.0f;
    g_autoScale     = 1;
    g_lastIniAuto   = -1;
    g_camDist       = kDistDesc.defFlt;
    g_lastIniDist   = kDistDesc.unsetFlt;
    g_camFov        = kFovDesc.defFlt;
    g_lastIniFov    = kFovDesc.unsetFlt;
    g_timeFmt       = 0;
    g_skipIntro     = 0;
    g_musicStart    = 0;
    g_musicPlaylist = 0;
    g_muteUnfocused = 0;
    g_prismVfx      = legacyFx ? 1u : 0u;
    g_chromaAb      = legacyFx ? 1u : 0u;
    g_aaSamples     = GetLegacyDefaultAa(legacyFx);
    g_finishEffect  = 0;
    g_finishPlatform= 0;
    g_colorTheme    = 0;
    g_cubeDistortion= 0;
    g_customTheme   = MakePresetThemeSettings(0);
    g_customThemeInited = 0;
    g_timerMode     = 3;
    g_timerSize     = 1.0f;
    g_timerPos      = 1;
    g_edgeTimerMode = 0;
    g_prismCounterType   = 2;
    g_prismCounterSize   = 1.0f;
    g_prismCounterPos    = 1;
    g_timerBgOpacity     = 70;
    g_prismBgOpacity     = 70;
    g_timerShadowOpacity = 0;
    g_prismShadowOpacity = 0;
    g_speedometerEnable        = 0;
    g_speedometerSize          = 1.0f;
    g_speedometerBgOpacity     = 70;
    g_speedometerShadowOpacity = 0;
    g_speedometerPos           = 3;
    g_minimapEnable         = 1;
    g_minimapEnableOverride = 0;
    g_minimapWidth          = 1.0f;
    g_minimapHeight         = 1.0f;
    g_minimapRange          = 1.0f;
    g_minimapWidthOverride  = 0;
    g_minimapHeightOverride = 0;
    g_minimapRangeOverride  = 0;
    g_minimapBgOpacity      = 70;
    g_minimapPos            = 0;
    g_minimapPosOverride    = 0;
    g_lastIniMinimapVisible = -1;
    g_lastIniMinimapWidth   = 0;
    g_lastIniMinimapHeight  = 0;
    g_lastIniMinimapCellW   = 0.0f;
    g_lastIniMinimapCellH   = 0.0f;
    g_lastIniMinimapAlignX  = -1;
    g_lastIniMinimapAlignY  = -1;

    bool needsResave = true;
    bool fileExisted = LoadModSettingsFromIni(&needsResave);
    bool dirty = !fileExisted || needsResave;

    if (!fileExisted && g_legacyTtsavCaptured) {
        ApplyLegacyTtsavSnapshot();
        g_customTheme = MakePresetThemeSettings((g_colorTheme <= 2u) ? g_colorTheme : 0u);
        g_customTheme.finishEffect   = (g_finishEffect <= 2u) ? g_finishEffect : 0u;
        g_customTheme.finishPlatform = (g_finishPlatform <= 2u) ? g_finishPlatform : 0u;
        g_customTheme.cubeDistortion = (g_cubeDistortion != 0) ? 1u : 0u;
        bool isExactPc     = (g_colorTheme == 0u && g_customTheme.finishEffect == 0u && g_customTheme.finishPlatform == 0u && g_customTheme.cubeDistortion == 0u);
        bool isExactMobile = (g_colorTheme == 1u && g_customTheme.finishEffect == 1u && g_customTheme.finishPlatform == 1u && g_customTheme.cubeDistortion == 0u);
        bool isExactJava   = (g_colorTheme == 2u && g_customTheme.finishEffect == 2u && g_customTheme.finishPlatform == 2u && g_customTheme.cubeDistortion == 1u);
        if (!isExactPc && !isExactMobile && !isExactJava) {
            g_colorTheme = 3u;
            g_customThemeInited = 1u;
        }
        dirty = true;
    }

    if (g_iniUiScale == 0.0f) {
        if (g_lastIniScale != 0.0f) {
            g_lastIniScale = 0.0f;
            dirty = true;
        }
    } else if (std::fabs(g_iniUiScale - g_lastIniScale) > 0.0005f) {
        g_uiScale = g_iniUiScale;
        g_lastIniScale = g_iniUiScale;
        dirty = true;
    }

    if (g_iniAutoScale < 0) {
        if (g_lastIniAuto != -1) {
            g_lastIniAuto = -1;
            dirty = true;
        }
    } else if (g_iniAutoScale != g_lastIniAuto) {
        g_autoScale = static_cast<uint32_t>(g_iniAutoScale);
        g_lastIniAuto = g_iniAutoScale;
        dirty = true;
    }

    if (ReconcileCamIniOverride(kDistDesc)) dirty = true;
    if (ReconcileCamIniOverride(kFovDesc))  dirty = true;

    {
        const uint8_t* st = *reinterpret_cast<const uint8_t* const*>(0x5f9058);
        float stCellW  = (st != nullptr) ? *reinterpret_cast<const float*>(st + 0x24) : 0.0f;
        float stCellH  = (st != nullptr) ? *reinterpret_cast<const float*>(st + 0x28) : 0.0f;
        int32_t stW    = (st != nullptr) ? *reinterpret_cast<const int32_t*>(st + 0x2c) : 0;
        int32_t stH    = (st != nullptr) ? *reinterpret_cast<const int32_t*>(st + 0x30) : 0;
        int32_t stOffX = (st != nullptr) ? *reinterpret_cast<const int32_t*>(st + 0x34) : 0;
        int32_t stOffY = (st != nullptr) ? *reinterpret_cast<const int32_t*>(st + 0x38) : 0;
        int32_t stAlX  = (st != nullptr) ? *reinterpret_cast<const int32_t*>(st + 0x3c) : 0;
        int32_t stAlY  = (st != nullptr) ? *reinterpret_cast<const int32_t*>(st + 0x40) : 0;
        int8_t  stVis  = (st != nullptr) ? (st[0x45] != 0 ? 1 : 0) : 1;

        g_iniMinimapCellW  = (std::isfinite(stCellW) && stCellW > 0.001f) ? stCellW : kDefMinimapCellW;
        g_iniMinimapCellH  = (std::isfinite(stCellH) && stCellH > 0.001f) ? stCellH : kDefMinimapCellH;
        g_iniMinimapWidth  = (stW > 0) ? stW : kDefMinimapWidth;
        g_iniMinimapHeight = (stH > 0) ? stH : kDefMinimapHeight;
        g_iniMinimapOffX   = stOffX;
        g_iniMinimapOffY   = stOffY;
        g_iniMinimapAlignX = (stAlX >= 0 && stAlX <= 2) ? stAlX : 0;
        g_iniMinimapAlignY = (stAlY >= 0 && stAlY <= 2) ? stAlY : 0;
        g_iniMinimapVisible = stVis;

        if (g_lastIniMinimapVisible < 0) {
            g_lastIniMinimapVisible = g_iniMinimapVisible;
            dirty = true;
        } else if (g_iniMinimapVisible != g_lastIniMinimapVisible) {
            g_minimapEnableOverride = 0;
            g_lastIniMinimapVisible = g_iniMinimapVisible;
            dirty = true;
        }

        if (g_lastIniMinimapWidth <= 0) {
            g_lastIniMinimapWidth = g_iniMinimapWidth;
            dirty = true;
        } else if (g_iniMinimapWidth != g_lastIniMinimapWidth) {
            g_minimapWidthOverride = 0;
            g_lastIniMinimapWidth = g_iniMinimapWidth;
            dirty = true;
        }

        if (g_lastIniMinimapHeight <= 0) {
            g_lastIniMinimapHeight = g_iniMinimapHeight;
            dirty = true;
        } else if (g_iniMinimapHeight != g_lastIniMinimapHeight) {
            g_minimapHeightOverride = 0;
            g_lastIniMinimapHeight = g_iniMinimapHeight;
            dirty = true;
        }

        if (g_lastIniMinimapCellW <= 0.0f || g_lastIniMinimapCellH <= 0.0f) {
            g_lastIniMinimapCellW = g_iniMinimapCellW;
            g_lastIniMinimapCellH = g_iniMinimapCellH;
            dirty = true;
        } else if (std::fabs(g_iniMinimapCellW - g_lastIniMinimapCellW) > 0.0005f ||
                   std::fabs(g_iniMinimapCellH - g_lastIniMinimapCellH) > 0.0005f) {
            g_minimapRangeOverride = 0;
            g_lastIniMinimapCellW = g_iniMinimapCellW;
            g_lastIniMinimapCellH = g_iniMinimapCellH;
            dirty = true;
        }

        if (g_lastIniMinimapAlignX < 0 || g_lastIniMinimapAlignY < 0) {
            g_lastIniMinimapAlignX = g_iniMinimapAlignX;
            g_lastIniMinimapAlignY = g_iniMinimapAlignY;
            dirty = true;
        } else if (g_iniMinimapAlignX != g_lastIniMinimapAlignX || g_iniMinimapAlignY != g_lastIniMinimapAlignY) {
            g_minimapPosOverride = 0;
            g_lastIniMinimapAlignX = g_iniMinimapAlignX;
            g_lastIniMinimapAlignY = g_iniMinimapAlignY;
            dirty = true;
        }
    }

    g_graphicsLoaded = 1;
    SyncEffectiveThemeGlobals();

    if (dirty) {
        SaveModSettings();
    }

    EnsureMusicPlaylistReady();
    OnColorThemeChanged();
    if (userSettings != nullptr) {
        ApplyUiScale(userSettings - 0x298);
    }
}

static uint8_t __cdecl LoadUserSettingsLocal_Impl(uint8_t* userSettings) {
    g_legacyTtsavCaptured = false;
    g_legacySnap = {};
    uint8_t ok = reinterpret_cast<uint8_t(__thiscall*)(void*)>(0x4923f0)(userSettings);
    LoadModSettingsOnStartup_Impl(userSettings);
    return ok;
}

static void __declspec(naked) hk_loadUserSettingsLocal() {
    // At 0x492ef6: ecx = userSettings (replaces `call 0x4923f0`)
    __asm {
        push ecx
        call LoadUserSettingsLocal_Impl
        add esp, 4
        ret
    }
}

// ------------------------------------------------------------------
// 2. Options toggle actions
// ------------------------------------------------------------------
static void ApplyOptionScaleChange(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        ApplyUiScale(sm);
    }
    SaveModSettings();
}

static void AutoScaleAction_Impl(int sfxId) {
    g_autoScale ^= 1u;
    ApplyOptionScaleChange(sfxId);
}

static void TimeFmtAction_Impl(int sfxId) {
    g_timeFmt ^= 1u;
    ApplyOptionScaleChange(sfxId);
}

static void SkipIntroAction_Impl(int sfxId) {
    g_skipIntro ^= 1u;
    ApplyOptionScaleChange(sfxId);
}

static int GetLevelThemeIdx() {
    uint8_t* lvl = GetLevel();
    if (lvl == nullptr) return 0;
    uint32_t t = *reinterpret_cast<uint32_t*>(lvl + 0x6c);
    return (t <= 3u) ? static_cast<int>(t) : 0;
}

static float GetBcfgFloat(uint32_t handle) {
    void** ppBcfg = reinterpret_cast<void**(__cdecl*)()>(0x4764c0)();
    if (ppBcfg == nullptr || *ppBcfg == nullptr) return 0.0f;
    return reinterpret_cast<float(__thiscall*)(void*, uint32_t)>(0x4408c0)(*ppBcfg, handle);
}

static void ProbeSupportedAaLevels() {
    g_aaLevels[0] = 0;
    g_aaLevelCount = 1;
    uint8_t* renderer = GetRenderer();
    if (renderer == nullptr) return;
    using SetAaFn = int(__thiscall*)(void*, int, int);
    auto setAa = reinterpret_cast<SetAaFn>(0x405a60);
    uint8_t oldEn = renderer[0x2];
    int32_t oldSamples = *reinterpret_cast<int32_t*>(renderer + 0x14);
    static const int32_t kCandidates[4] = {2, 4, 8, 16};
    for (int32_t cand : kCandidates) {
        int got = setAa(renderer, 1, cand);
        if (got == cand && g_aaLevelCount < 5) {
            g_aaLevels[g_aaLevelCount++] = cand;
        }
    }
    setAa(renderer, oldEn ? 1 : 0, oldSamples);
}

static void SyncCubeScreenEffectNow(uint8_t* sm) {
    if (sm == nullptr) return;
    uint8_t* fx = *reinterpret_cast<uint8_t**>(sm + 0x304);
    if (fx == nullptr) return;

    float baseSepCfg = GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x40));
    float baseSep = (g_chromaAb != 0) ? baseSepCfg : 0.0f;
    float baseWhite = GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x50));
    if (baseWhite <= 0.0f) baseWhite = 1.0f;

    float* pCurSep = reinterpret_cast<float*>(fx + 0x4c);
    float* pCurWhite = reinterpret_cast<float*>(fx + 0x68);
    if (g_prismVfx == 0) {
        g_prismOverrideSec = 0.0f;
        *pCurSep = baseSep;
        *pCurWhite = baseWhite;
    } else {
        if (*pCurSep < baseSep || (g_chromaAb == 0 && *pCurSep <= baseSepCfg + 0.01f)) {
            *pCurSep = baseSep;
        }
        if (*pCurWhite < baseWhite) {
            *pCurWhite = baseWhite;
        }
    }
}

static void SyncGraphicsRuntime(uint8_t* sm, bool forceAaReset) {
    if (sm == nullptr) return;
    bool needPp = (g_chromaAb != 0 || g_prismVfx != 0);
    uint8_t* renderer = GetRenderer();
    uint8_t* pp = (renderer != nullptr) ? *reinterpret_cast<uint8_t**>(renderer + 0xb0) : nullptr;
    bool wasPp = (pp != nullptr && pp[0x21] != 0);

    sm[0x2b5] = needPp ? 1u : 0u;
    if (needPp != wasPp) {
        reinterpret_cast<void(__thiscall*)(void*, int)>(0x482600)(sm, needPp ? 1 : 0);
    }

    pp = (renderer != nullptr) ? *reinterpret_cast<uint8_t**>(renderer + 0xb0) : nullptr;
    bool nowPp = (pp != nullptr && pp[0x21] != 0);
    bool needReset = forceAaReset || ((nowPp != wasPp) && g_aaSamples > 0);
    if (needReset && renderer != nullptr) {
        using SetAaFn = int(__thiscall*)(void*, int, int);
        auto setAa = reinterpret_cast<SetAaFn>(0x405a60);
        int req = (g_aaSamples > 0) ? g_aaSamples : 0;
        int actual = setAa(renderer, req > 0 ? 1 : 0, req);
        if (actual <= 0) {
            g_aaSamples = 0;
            setAa(renderer, 0, 0);
        } else {
            g_aaSamples = actual;
        }
        reinterpret_cast<void(__thiscall*)(void*, int, int)>(0x4816d0)(sm, g_aaSamples, 1);
        ApplyUiScale(sm);
    }
    SyncCubeScreenEffectNow(sm);
}

static void __cdecl CubeFxUpdate_Impl(uint8_t* fx) {
    if (fx == nullptr) return;
    constexpr double kTickDt = 0.03333333507180214; // matches 0x578108

    float baseSepCfg = GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x40));
    float prismSep   = GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x44));
    float sepTime    = (g_prismOverrideSec > 0.0f) ? g_prismOverrideSec : GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x48));
    float targetSep  = (g_chromaAb != 0) ? baseSepCfg : 0.0f;

    float curSep = *reinterpret_cast<float*>(fx + 0x4c);
    if (g_prismVfx == 0) {
        curSep = targetSep;
    } else if (curSep > targetSep) {
        if (sepTime > 0.0001f) {
            double ticks = static_cast<double>(sepTime) / kTickDt;
            double step = (static_cast<double>(prismSep) - static_cast<double>(targetSep)) / ticks;
            if (step > 0.0) {
                curSep = static_cast<float>(static_cast<double>(curSep) - step);
                if (curSep < targetSep) curSep = targetSep;
            } else {
                curSep = targetSep;
            }
        } else {
            curSep = targetSep;
        }
    } else {
        curSep = targetSep;
    }
    *reinterpret_cast<float*>(fx + 0x4c) = curSep;

    int theme        = GetLevelThemeIdx();
    float baseWhite  = GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x50));
    float prismWhite = GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x54 + theme * 4));
    float whiteTime  = (g_prismOverrideSec > 0.0f) ? g_prismOverrideSec : GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x64));

    float curWhite = *reinterpret_cast<float*>(fx + 0x68);
    if (curWhite > baseWhite) {
        if (whiteTime > 0.0001f) {
            double ticks = static_cast<double>(whiteTime) / kTickDt;
            double step = (static_cast<double>(prismWhite) - static_cast<double>(baseWhite)) / ticks;
            if (step > 0.0) {
                curWhite = static_cast<float>(static_cast<double>(curWhite) - step);
                if (curWhite < baseWhite) curWhite = baseWhite;
            } else {
                curWhite = baseWhite;
            }
        } else {
            curWhite = baseWhite;
        }
    } else {
        curWhite = baseWhite;
    }
    *reinterpret_cast<float*>(fx + 0x68) = curWhite;

    if (curSep <= targetSep && curWhite <= baseWhite) {
        g_prismOverrideSec = 0.0f;
    }
}

static void __declspec(naked) hk_cubeFxUpdate() {
    __asm {
        push ecx
        call CubeFxUpdate_Impl
        add esp, 4
        ret
    }
}

static void __cdecl CubeFxTrigger_Impl(uint8_t* fx, int type) {
    if (fx == nullptr) return;
    if (type == 1) {
        if (g_prismVfx == 0) return;
        g_prismOverrideSec = (g_prismVfx == 3) ? 2.0f : ((g_prismVfx == 2) ? 1.0f : 0.0f);
        *reinterpret_cast<float*>(fx + 0x4c) = GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x44));
        int theme = GetLevelThemeIdx();
        *reinterpret_cast<float*>(fx + 0x68) = GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x54 + theme * 4));
    } else if (type == 2) {
        g_prismOverrideSec = 0.0f;
        int theme = GetLevelThemeIdx();
        *reinterpret_cast<float*>(fx + 0x68) = GetBcfgFloat(*reinterpret_cast<uint32_t*>(fx + 0x54 + theme * 4));
    }
}

static void __declspec(naked) hk_cubeFxTrigger() {
    __asm {
        push DWORD PTR [esp + 4]
        push ecx
        call CubeFxTrigger_Impl
        add esp, 8
        ret 4
    }
}

static void __cdecl CreatePostEffectsTail_Impl(uint8_t* sm) {
    if (sm == nullptr) return;
    // Route ScreenMgr slot 2 (Pause / Level Finished, normally BoxBlur) to CubeScreenEffect (no blur).
    reinterpret_cast<void(__thiscall*)(void*, void*)>(0x4ecfd0)(sm + 0x2fc, sm + 0x304);
    SyncCubeScreenEffectNow(sm);
}

static void __declspec(naked) hk_createPostEffectsTail() {
    __asm {
        push edi
        call CreatePostEffectsTail_Impl
        add esp, 4
        pop esi
        pop edi
        ret
    }
}

static void __cdecl ApplyStartupGraphics_Impl(uint8_t* sm) {
    if (sm == nullptr) return;
    if (!g_graphicsLoaded) {
        uint8_t legacyFx = sm[0x2b5];
        g_prismVfx = legacyFx ? 1u : 0u;
        g_chromaAb = legacyFx ? 1u : 0u;
        g_aaSamples = GetLegacyDefaultAa(legacyFx);
        g_graphicsLoaded = 1;
    }
    ProbeSupportedAaLevels();

    bool needPp = (g_chromaAb != 0 || g_prismVfx != 0);
    sm[0x2b5] = needPp ? 1u : 0u;
    reinterpret_cast<void(__thiscall*)(void*, int)>(0x482600)(sm, needPp ? 1 : 0);

    uint8_t* renderer = GetRenderer();
    if (renderer != nullptr) {
        using SetAaFn = int(__thiscall*)(void*, int, int);
        auto setAa = reinterpret_cast<SetAaFn>(0x405a60);
        int req = (g_aaSamples > 0) ? g_aaSamples : 0;
        int actual = setAa(renderer, req > 0 ? 1 : 0, req);
        if (actual <= 0) {
            g_aaSamples = 0;
            setAa(renderer, 0, 0);
        } else {
            g_aaSamples = actual;
        }
    }
}

static void __declspec(naked) hk_startupGraphics() {
    __asm {
        push ebx
        push esi
        push edi
        push esi
        call ApplyStartupGraphics_Impl
        add esp, 4
        pop edi
        pop esi
        pop ebx
        ret
    }
}

static void ChromaAbAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    g_chromaAb ^= 1u;
    if (sm != nullptr) {
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        SyncGraphicsRuntime(sm, false);
    }
    SaveModSettings();
}

static void CubeDistortionAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    g_customTheme.cubeDistortion = (g_customTheme.cubeDistortion != 0u) ? 0u : 1u;
    g_customThemeInited = 1u;
    SyncEffectiveThemeGlobals();
    if (sm != nullptr && sfxId > 0) {
        Game_PlaySfx(sm, sfxId);
    }
    SaveModSettings();
}

static void MusicStartAction_Impl(int sfxId) {
    g_musicStart ^= 1u;
    ApplyOptionScaleChange(sfxId);
}

static void MusicPlaylistAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    g_musicPlaylist ^= 1u;
    if (sm != nullptr && sfxId > 0) {
        Game_PlaySfx(sm, sfxId);
    }
    SaveModSettings();
    OnMusicPlaylistChanged();
}

static void MuteUnfocusedAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    g_muteUnfocused ^= 1u;
    if (sm != nullptr && sfxId > 0) {
        Game_PlaySfx(sm, sfxId);
    }
    SaveModSettings();
    SyncFocusAudioMute();
}

// ------------------------------------------------------------------
// 3. Nested Options & Pause Page 1 state, dispatch, hitTest & rendering
// ------------------------------------------------------------------
static int GetOptTargetCategory(OptItem item) {
    switch (item) {
    case OPT_CAT_BTN_DISPLAY:         return OPT_CAT_DISPLAY;
    case OPT_CAT_BTN_GRAPHICS:        return OPT_CAT_GRAPHICS;
    case OPT_CAT_BTN_CUSTOMIZE_THEME: return OPT_CAT_CUSTOMIZE_THEME;
    case OPT_CAT_BTN_AUDIO:           return OPT_CAT_AUDIO;
    case OPT_CAT_BTN_UI:              return OPT_CAT_UI;
    case OPT_CAT_BTN_TIMER:           return OPT_CAT_TIMER;
    case OPT_CAT_BTN_PRISM_COUNTER:   return OPT_CAT_PRISM_COUNTER;
    case OPT_CAT_BTN_MINIMAP:         return OPT_CAT_MINIMAP;
    case OPT_CAT_BTN_SPEEDOMETER:     return OPT_CAT_SPEEDOMETER;
    case OPT_CAT_BTN_GAMEPLAY:        return OPT_CAT_GAMEPLAY;
    default:                          return -1;
    }
}

static bool IsOptSubmenuItem(OptItem item) {
    return GetOptTargetCategory(item) >= 0;
}

static int GetOptParentCategory(int cat) {
    switch (cat) {
    case OPT_CAT_CUSTOMIZE_THEME:
        return OPT_CAT_GRAPHICS;
    case OPT_CAT_TIMER:
    case OPT_CAT_PRISM_COUNTER:
    case OPT_CAT_MINIMAP:
    case OPT_CAT_SPEEDOMETER:
        return OPT_CAT_UI;
    case OPT_CAT_DISPLAY:
    case OPT_CAT_GRAPHICS:
    case OPT_CAT_AUDIO:
    case OPT_CAT_UI:
    case OPT_CAT_GAMEPLAY:
        return OPT_CAT_ROOT;
    case OPT_CAT_ROOT:
    default:
        return -1;
    }
}

static int GetOptCategoryDepth(int cat) {
    if (cat == OPT_CAT_ROOT) return 0;
    if (cat == OPT_CAT_CUSTOMIZE_THEME || cat == OPT_CAT_TIMER ||
        cat == OPT_CAT_PRISM_COUNTER || cat == OPT_CAT_MINIMAP || cat == OPT_CAT_SPEEDOMETER) {
        return 2;
    }
    return 1;
}

static int GetOptParentRowForCategory(int cat) {
    switch (cat) {
    case OPT_CAT_DISPLAY:         return 0;
    case OPT_CAT_GRAPHICS:        return 1;
    case OPT_CAT_AUDIO:           return 2;
    case OPT_CAT_UI:              return 3;
    case OPT_CAT_GAMEPLAY:        return 4;
    case OPT_CAT_CUSTOMIZE_THEME: return 5; // row 5 in OPT_CAT_GRAPHICS (below theme)
    case OPT_CAT_TIMER:           return 1; // row 1 in OPT_CAT_UI (below time format)
    case OPT_CAT_PRISM_COUNTER:   return 2; // row 2 in OPT_CAT_UI (below timer)
    case OPT_CAT_MINIMAP:         return 3; // row 3 in OPT_CAT_UI (below prism counter)
    case OPT_CAT_SPEEDOMETER:     return 4; // row 4 in OPT_CAT_UI (below minimap)
    default:                      return 0;
    }
}

static int GetOptItems(int cat, bool inPause, OptItem* outItems) {
    switch (cat) {
    case OPT_CAT_DISPLAY:
        outItems[0] = OPT_FULLSCREEN;
        outItems[1] = OPT_UI_SCALE;
        outItems[2] = OPT_AUTO_SCALE;
        outItems[3] = OPT_BACK;
        return 4;
    case OPT_CAT_GRAPHICS: {
        int n = 0;
        outItems[n++] = OPT_AA;
        outItems[n++] = OPT_CHROMA_AB;
        outItems[n++] = OPT_PRISM_VFX;
        outItems[n++] = OPT_EDGE_TIMER;
        outItems[n++] = OPT_COLOR_THEME;
        if (g_colorTheme == 3u) {
            outItems[n++] = OPT_CAT_BTN_CUSTOMIZE_THEME;
        }
        outItems[n++] = OPT_BACK;
        return n;
    }
    case OPT_CAT_CUSTOMIZE_THEME:
        outItems[0]  = OPT_THEME_TERRAIN;
        outItems[1]  = OPT_THEME_WALL_GRADIENT;
        outItems[2]  = OPT_THEME_PLANETS;
        outItems[3]  = OPT_THEME_STARS;
        outItems[4]  = OPT_THEME_PLAYER_CUBE;
        outItems[5]  = OPT_THEME_DARK_CUBE;
        outItems[6]  = OPT_CUBE_DISTORTION;
        outItems[7]  = OPT_THEME_PRISMS;
        outItems[8]  = OPT_THEME_MOVING_PLATFORMS;
        outItems[9]  = OPT_THEME_PLATFORM_BORDERS;
        outItems[10] = OPT_THEME_FALLING_PLATFORMS;
        outItems[11] = OPT_THEME_BUMPERS;
        outItems[12] = OPT_THEME_SWITCHES;
        outItems[13] = OPT_THEME_HOLO_SWITCHES;
        outItems[14] = OPT_THEME_SHRINKERS;
        outItems[15] = OPT_FINISH_PLATFORM;
        outItems[16] = OPT_FINISH_EFFECT;
        outItems[17] = OPT_BACK;
        return 18;
    case OPT_CAT_AUDIO:
        outItems[0] = OPT_SOUNDS;
        outItems[1] = OPT_MUSIC;
        outItems[2] = OPT_MUSIC_START;
        outItems[3] = OPT_MUSIC_PLAYLIST;
        outItems[4] = OPT_MUTE_UNFOCUSED;
        outItems[5] = OPT_BACK;
        return 6;
    case OPT_CAT_UI:
        outItems[0] = OPT_TIME_FMT;
        outItems[1] = OPT_CAT_BTN_TIMER;
        outItems[2] = OPT_CAT_BTN_PRISM_COUNTER;
        outItems[3] = OPT_CAT_BTN_MINIMAP;
        outItems[4] = OPT_CAT_BTN_SPEEDOMETER;
        outItems[5] = OPT_BACK;
        return 6;
    case OPT_CAT_TIMER:
        outItems[0] = OPT_TIMER;
        outItems[1] = OPT_TIMER_SIZE;
        outItems[2] = OPT_TIMER_BG_OPACITY;
        outItems[3] = OPT_TIMER_SHADOW_OPACITY;
        outItems[4] = OPT_TIMER_POS;
        outItems[5] = OPT_BACK;
        return 6;
    case OPT_CAT_PRISM_COUNTER:
        outItems[0] = OPT_PRISM_COUNTER_TYPE;
        outItems[1] = OPT_PRISM_COUNTER_SIZE;
        outItems[2] = OPT_PRISM_BG_OPACITY;
        outItems[3] = OPT_PRISM_SHADOW_OPACITY;
        outItems[4] = OPT_PRISM_COUNTER_POS;
        outItems[5] = OPT_BACK;
        return 6;
    case OPT_CAT_MINIMAP:
        outItems[0] = OPT_MINIMAP_ENABLE;
        outItems[1] = OPT_MINIMAP_WIDTH;
        outItems[2] = OPT_MINIMAP_HEIGHT;
        outItems[3] = OPT_MINIMAP_RANGE;
        outItems[4] = OPT_MINIMAP_BG_OPACITY;
        outItems[5] = OPT_MINIMAP_POS;
        outItems[6] = OPT_BACK;
        return 7;
    case OPT_CAT_SPEEDOMETER:
        outItems[0] = OPT_SPEEDOMETER_ENABLE;
        outItems[1] = OPT_SPEEDOMETER_SIZE;
        outItems[2] = OPT_SPEEDOMETER_BG_OPACITY;
        outItems[3] = OPT_SPEEDOMETER_SHADOW_OPACITY;
        outItems[4] = OPT_SPEEDOMETER_POS;
        outItems[5] = OPT_BACK;
        return 6;
    case OPT_CAT_GAMEPLAY: {
        int n = 0;
        outItems[n++] = OPT_UP_KEY;
        outItems[n++] = OPT_SKIP_INTRO;
        outItems[n++] = OPT_CAM_DIST;
        outItems[n++] = OPT_DOLLY_ZOOM;
        if (!inPause) {
            outItems[n++] = OPT_RESET_SCORE;
        }
        outItems[n++] = OPT_BACK;
        return n;
    }
    case OPT_CAT_ROOT:
    default:
        outItems[0] = OPT_CAT_BTN_DISPLAY;
        outItems[1] = OPT_CAT_BTN_GRAPHICS;
        outItems[2] = OPT_CAT_BTN_AUDIO;
        outItems[3] = OPT_CAT_BTN_UI;
        outItems[4] = OPT_CAT_BTN_GAMEPLAY;
        outItems[5] = OPT_BACK;
        return 6;
    }
}

static int GetOptRowCount(int cat, bool inPause) {
    OptItem tmp[kMaxOptRows];
    return GetOptItems(cat, inPause, tmp);
}

static int __cdecl GetPauseOptRowCount() {
    return GetOptRowCount(g_pauseOptCat, true);
}

static bool IsOptItemAdjustable(OptItem item) {
    switch (item) {
    case OPT_FULLSCREEN:
    case OPT_PRISM_VFX:
    case OPT_CHROMA_AB:
    case OPT_AA:
    case OPT_UI_SCALE:
    case OPT_AUTO_SCALE:
    case OPT_CAM_DIST:
    case OPT_DOLLY_ZOOM:
    case OPT_FINISH_EFFECT:
    case OPT_FINISH_PLATFORM:
    case OPT_COLOR_THEME:
    case OPT_CUBE_DISTORTION:
    case OPT_THEME_TERRAIN:
    case OPT_THEME_WALL_GRADIENT:
    case OPT_THEME_PLANETS:
    case OPT_THEME_STARS:
    case OPT_THEME_PLAYER_CUBE:
    case OPT_THEME_DARK_CUBE:
    case OPT_THEME_PRISMS:
    case OPT_THEME_MOVING_PLATFORMS:
    case OPT_THEME_PLATFORM_BORDERS:
    case OPT_THEME_FALLING_PLATFORMS:
    case OPT_THEME_BUMPERS:
    case OPT_THEME_SWITCHES:
    case OPT_THEME_HOLO_SWITCHES:
    case OPT_THEME_SHRINKERS:
    case OPT_SOUNDS:
    case OPT_MUSIC:
    case OPT_MUSIC_START:
    case OPT_MUSIC_PLAYLIST:
    case OPT_MUTE_UNFOCUSED:
    case OPT_UP_KEY:
    case OPT_SKIP_INTRO:
    case OPT_TIME_FMT:
    case OPT_EDGE_TIMER:
    case OPT_TIMER:
    case OPT_TIMER_SIZE:
    case OPT_TIMER_POS:
    case OPT_TIMER_BG_OPACITY:
    case OPT_TIMER_SHADOW_OPACITY:
    case OPT_PRISM_COUNTER_TYPE:
    case OPT_PRISM_COUNTER_SIZE:
    case OPT_PRISM_COUNTER_POS:
    case OPT_PRISM_BG_OPACITY:
    case OPT_PRISM_SHADOW_OPACITY:
    case OPT_MINIMAP_ENABLE:
    case OPT_MINIMAP_WIDTH:
    case OPT_MINIMAP_HEIGHT:
    case OPT_MINIMAP_RANGE:
    case OPT_MINIMAP_BG_OPACITY:
    case OPT_MINIMAP_POS:
    case OPT_SPEEDOMETER_ENABLE:
    case OPT_SPEEDOMETER_SIZE:
    case OPT_SPEEDOMETER_BG_OPACITY:
    case OPT_SPEEDOMETER_SHADOW_OPACITY:
    case OPT_SPEEDOMETER_POS:
        return true;
    default:
        return false;
    }
}

static const EdgeWString* GetOptHeaderWstr(int cat, EdgeWString* tmp, bool* outFreeHeap) {
    *outFreeHeap = false;
    switch (cat) {
    case OPT_CAT_DISPLAY:         return &g_wstrCatDisplay;
    case OPT_CAT_GRAPHICS:        return &g_wstrCatGraphics;
    case OPT_CAT_CUSTOMIZE_THEME: return &g_wstrCustomizeTheme;
    case OPT_CAT_AUDIO:           return &g_wstrCatAudio;
    case OPT_CAT_UI:            return &g_wstrCatUi;
    case OPT_CAT_TIMER:         return &g_wstrTimer;
    case OPT_CAT_PRISM_COUNTER: return &g_wstrPrismCounter;
    case OPT_CAT_MINIMAP:       return &g_wstrMinimap;
    case OPT_CAT_SPEEDOMETER:   return &g_wstrSpeedometer;
    case OPT_CAT_GAMEPLAY:      return &g_wstrCatGameplay;
    case OPT_CAT_ROOT:
    default:
        Game_GetLocalizedWString(tmp, 8);
        *outFreeHeap = true;
        return tmp;
    }
}

static const EdgeWString* GetOptLabelWstr(OptItem item, EdgeWString* tmp, bool* outFreeHeap) {
    *outFreeHeap = false;
    switch (item) {
    case OPT_CAT_BTN_DISPLAY:         return &g_wstrCatDisplay;
    case OPT_CAT_BTN_GRAPHICS:        return &g_wstrCatGraphics;
    case OPT_CAT_BTN_CUSTOMIZE_THEME: return &g_wstrCustomizeTheme;
    case OPT_CAT_BTN_AUDIO:           return &g_wstrCatAudio;
    case OPT_CAT_BTN_UI:            return &g_wstrCatUi;
    case OPT_CAT_BTN_TIMER:         return &g_wstrTimer;
    case OPT_CAT_BTN_PRISM_COUNTER: return &g_wstrPrismCounter;
    case OPT_CAT_BTN_MINIMAP:       return &g_wstrMinimap;
    case OPT_CAT_BTN_SPEEDOMETER:   return &g_wstrSpeedometer;
    case OPT_CAT_BTN_GAMEPLAY:      return &g_wstrCatGameplay;
    case OPT_PRISM_VFX:        return &g_wstrPrismVfx;
    case OPT_CHROMA_AB:        return &g_wstrChromaAb;
    case OPT_AA:               return &g_wstrAntiAlias;
    case OPT_UI_SCALE:         return &g_wstrUiScale;
    case OPT_AUTO_SCALE:       return &g_wstrAuto;
    case OPT_CAM_DIST:         return &g_wstrCamDist;
    case OPT_DOLLY_ZOOM:       return &g_wstrCamFov;
    case OPT_FINISH_EFFECT:           return &g_wstrFinishEffect;
    case OPT_FINISH_PLATFORM:         return &g_wstrFinishPlatform;
    case OPT_COLOR_THEME:             return &g_wstrColorTheme;
    case OPT_CUBE_DISTORTION:         return &g_wstrCubeDistortion;
    case OPT_THEME_TERRAIN:           return &g_wstrThemeTerrain;
    case OPT_THEME_WALL_GRADIENT:     return &g_wstrThemeWallGrad;
    case OPT_THEME_PLANETS:           return &g_wstrThemePlanets;
    case OPT_THEME_STARS:             return &g_wstrThemeStars;
    case OPT_THEME_PLAYER_CUBE:       return &g_wstrThemePlayerCube;
    case OPT_THEME_DARK_CUBE:         return &g_wstrThemeDarkCube;
    case OPT_THEME_PRISMS:            return &g_wstrThemePrisms;
    case OPT_THEME_MOVING_PLATFORMS:  return &g_wstrThemeMovPlat;
    case OPT_THEME_PLATFORM_BORDERS:  return &g_wstrThemePlatEdge;
    case OPT_THEME_FALLING_PLATFORMS: return &g_wstrThemeFallPlat;
    case OPT_THEME_BUMPERS:           return &g_wstrThemeBumpers;
    case OPT_THEME_SWITCHES:          return &g_wstrThemeSwitches;
    case OPT_THEME_HOLO_SWITCHES:     return &g_wstrThemeHoloSwitch;
    case OPT_THEME_SHRINKERS:         return &g_wstrThemeShrinkers;
    case OPT_MUSIC_START:      return &g_wstrMusicStart;
    case OPT_MUSIC_PLAYLIST:   return &g_wstrMusicPlaylist;
    case OPT_MUTE_UNFOCUSED:   return &g_wstrMuteUnfocused;
    case OPT_SKIP_INTRO:       return &g_wstrSkipIntro;
    case OPT_TIME_FMT:         return &g_wstrTimeFmt;
    case OPT_EDGE_TIMER:       return &g_wstrEdgeTimer;
    case OPT_TIMER:            return &g_wstrTimerType;
    case OPT_TIMER_SIZE:       return &g_wstrTimerSizeLbl;
    case OPT_TIMER_POS:        return &g_wstrTimerPosLbl;
    case OPT_PRISM_COUNTER_TYPE: return &g_wstrTimerType;
    case OPT_PRISM_COUNTER_SIZE:
    case OPT_SPEEDOMETER_SIZE:   return &g_wstrTimerSizeLbl;
    case OPT_PRISM_COUNTER_POS:  return &g_wstrTimerPosLbl;
    case OPT_TIMER_BG_OPACITY:
    case OPT_PRISM_BG_OPACITY:
    case OPT_MINIMAP_BG_OPACITY:
    case OPT_SPEEDOMETER_BG_OPACITY: return &g_wstrBgOpacity;
    case OPT_TIMER_SHADOW_OPACITY:
    case OPT_PRISM_SHADOW_OPACITY:
    case OPT_SPEEDOMETER_SHADOW_OPACITY: return &g_wstrShadowOpacity;
    case OPT_MINIMAP_ENABLE:
    case OPT_SPEEDOMETER_ENABLE:   return &g_wstrMinimapEnable;
    case OPT_MINIMAP_WIDTH:        return &g_wstrMinimapWidth;
    case OPT_MINIMAP_HEIGHT:       return &g_wstrMinimapHeight;
    case OPT_MINIMAP_RANGE:        return &g_wstrMinimapRange;
    case OPT_MINIMAP_POS:
    case OPT_SPEEDOMETER_POS:      return &g_wstrTimerPosLbl;
    case OPT_RESET_SCORE:      return &g_wstrResetScore;
    case OPT_FULLSCREEN:
        Game_GetLocalizedWString(tmp, 0x34);
        *outFreeHeap = true;
        return tmp;
    case OPT_SOUNDS:
        Game_GetLocalizedWString(tmp, 0xa);
        *outFreeHeap = true;
        return tmp;
    case OPT_MUSIC:
        Game_GetLocalizedWString(tmp, 6);
        *outFreeHeap = true;
        return tmp;
    case OPT_UP_KEY:
        Game_GetLocalizedWString(tmp, 0x42);
        *outFreeHeap = true;
        return tmp;
    case OPT_BACK:
    default:
        Game_GetLocalizedWString(tmp, 0);
        *outFreeHeap = true;
        return tmp;
    }
}

static OptCtrlKind GetOptCtrlKind(OptItem item) {
    switch (item) {
    case OPT_FULLSCREEN:
    case OPT_CHROMA_AB:
    case OPT_AUTO_SCALE:
    case OPT_CUBE_DISTORTION:
    case OPT_THEME_WALL_GRADIENT:
    case OPT_THEME_STARS:
    case OPT_MUSIC_START:
    case OPT_MUSIC_PLAYLIST:
    case OPT_MUTE_UNFOCUSED:
    case OPT_UP_KEY:
    case OPT_SKIP_INTRO:
    case OPT_TIME_FMT:
    case OPT_MINIMAP_ENABLE:
    case OPT_SPEEDOMETER_ENABLE:
        return CTRL_TOGGLE;
    case OPT_AA:
    case OPT_CAM_DIST:
    case OPT_DOLLY_ZOOM:
    case OPT_COLOR_THEME:
    case OPT_FINISH_EFFECT:
    case OPT_FINISH_PLATFORM:
    case OPT_THEME_TERRAIN:
    case OPT_THEME_PLANETS:
    case OPT_THEME_PLAYER_CUBE:
    case OPT_THEME_DARK_CUBE:
    case OPT_THEME_PRISMS:
    case OPT_THEME_MOVING_PLATFORMS:
    case OPT_THEME_PLATFORM_BORDERS:
    case OPT_THEME_FALLING_PLATFORMS:
    case OPT_THEME_BUMPERS:
    case OPT_THEME_SWITCHES:
    case OPT_THEME_HOLO_SWITCHES:
    case OPT_THEME_SHRINKERS:
    case OPT_EDGE_TIMER:
    case OPT_TIMER:
    case OPT_TIMER_POS:
    case OPT_PRISM_COUNTER_TYPE:
    case OPT_PRISM_COUNTER_POS:
    case OPT_MINIMAP_POS:
    case OPT_SPEEDOMETER_POS:
        return CTRL_POPUP;
    case OPT_RESET_SCORE:
        return CTRL_CONFIRM_POPUP;
    case OPT_PRISM_VFX:
    case OPT_UI_SCALE:
    case OPT_SOUNDS:
    case OPT_MUSIC:
    case OPT_TIMER_SIZE:
    case OPT_PRISM_COUNTER_SIZE:
    case OPT_SPEEDOMETER_SIZE:
    case OPT_TIMER_BG_OPACITY:
    case OPT_PRISM_BG_OPACITY:
    case OPT_SPEEDOMETER_BG_OPACITY:
    case OPT_TIMER_SHADOW_OPACITY:
    case OPT_PRISM_SHADOW_OPACITY:
    case OPT_SPEEDOMETER_SHADOW_OPACITY:
    case OPT_MINIMAP_WIDTH:
    case OPT_MINIMAP_HEIGHT:
    case OPT_MINIMAP_RANGE:
    case OPT_MINIMAP_BG_OPACITY:
        return CTRL_SLIDER;
    default:
        return CTRL_NAV;
    }
}

static inline uint32_t RgbToRgbaOpaque(uint32_t rgb) {
    return ((rgb & 0xff0000u) >> 16) | (rgb & 0x00ff00u) | ((rgb & 0x0000ffu) << 16) | 0xff000000u;
}

static uint32_t LerpRgb(uint32_t rgbA, uint32_t rgbB, float t) {
    if (t <= 0.0f) return rgbA;
    if (t >= 1.0f) return rgbB;
    int rA = static_cast<int>((rgbA >> 16) & 0xffu);
    int gA = static_cast<int>((rgbA >> 8) & 0xffu);
    int bA = static_cast<int>(rgbA & 0xffu);
    int rB = static_cast<int>((rgbB >> 16) & 0xffu);
    int gB = static_cast<int>((rgbB >> 8) & 0xffu);
    int bB = static_cast<int>(rgbB & 0xffu);
    int r = static_cast<int>(std::lround(static_cast<float>(rA) + static_cast<float>(rB - rA) * t));
    int g = static_cast<int>(std::lround(static_cast<float>(gA) + static_cast<float>(gB - gA) * t));
    int b = static_cast<int>(std::lround(static_cast<float>(bA) + static_cast<float>(bB - bA) * t));
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

static float GetEffectiveUiScaleForDisplay() {
    return g_optUi.uiScalePending ? g_optUi.uiScalePreview : GetEffectiveUiScale();
}

static void CommitPendingUiScale() {
    if (!g_optUi.uiScalePending) return;
    g_optUi.uiScalePending = false;
    g_uiScale = g_optUi.uiScalePreview;
    SaveModSettings();
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        ApplyUiScale(sm);
    }
}

static void ResetOptUiState(bool commitUiScale) {
    g_optUi.popupOpen = false;
    g_optUi.popupSlotVisible = false;
    g_optUi.popupSlotPressed = false;
    g_optUi.popupNeedsInitialSync = false;
    g_optUi.dragActive = false;
    g_optUi.suppressNextClickAction = false;
    g_mainOptScroll.dragActive = false;
    g_pauseOptScroll.dragActive = false;
    g_optWheelAccum.store(0, std::memory_order_release);
    if (commitUiScale) {
        CommitPendingUiScale();
    } else {
        g_optUi.uiScalePending = false;
    }
}

static bool GetMinimapDisplayEnabled() {
    return (g_minimapEnableOverride != 0) ? (g_minimapEnable != 0) : (g_iniMinimapVisible != 0);
}

static uint8_t GetMinimapDisplayPos() {
    if (g_minimapPosOverride != 0) {
        return (g_minimapPos <= 3u) ? g_minimapPos : 0u;
    }
    uint8_t cx = (g_iniMinimapAlignX == 2) ? 1u : 0u;
    uint8_t cy = (g_iniMinimapAlignY == 2) ? 2u : 0u;
    return static_cast<uint8_t>(cx | cy);
}

static uint8_t* GetCustomThemeFieldPtr(OptItem item) {
    switch (item) {
    case OPT_THEME_TERRAIN:           return &g_customTheme.terrain;
    case OPT_THEME_WALL_GRADIENT:     return &g_customTheme.wallGradient;
    case OPT_THEME_PLANETS:           return &g_customTheme.planets;
    case OPT_THEME_STARS:             return &g_customTheme.stars;
    case OPT_THEME_PLAYER_CUBE:       return &g_customTheme.playerCube;
    case OPT_THEME_DARK_CUBE:         return &g_customTheme.darkCube;
    case OPT_CUBE_DISTORTION:         return &g_customTheme.cubeDistortion;
    case OPT_THEME_PRISMS:            return &g_customTheme.prisms;
    case OPT_THEME_MOVING_PLATFORMS:  return &g_customTheme.movingPlatforms;
    case OPT_THEME_PLATFORM_BORDERS:  return &g_customTheme.platformBorders;
    case OPT_THEME_FALLING_PLATFORMS: return &g_customTheme.fallingPlatforms;
    case OPT_THEME_BUMPERS:           return &g_customTheme.bumpers;
    case OPT_THEME_SWITCHES:          return &g_customTheme.switches;
    case OPT_THEME_HOLO_SWITCHES:     return &g_customTheme.holoSwitches;
    case OPT_THEME_SHRINKERS:         return &g_customTheme.shrinkers;
    case OPT_FINISH_PLATFORM:         return &g_customTheme.finishPlatform;
    case OPT_FINISH_EFFECT:           return &g_customTheme.finishEffect;
    default:                          return nullptr;
    }
}

static int GetToggleState(OptItem item) {
    uint8_t* sm = GetScreenMgr();
    switch (item) {
    case OPT_FULLSCREEN: {
        void* app = reinterpret_cast<void*(__cdecl*)()>(0x4eecd0)();
        using GetFsFn = uint8_t(__thiscall*)(void*);
        uint8_t isFs = (*reinterpret_cast<GetFsFn**>(app))[0x14 / 4](app);
        return isFs ? 1 : 0;
    }
    case OPT_CHROMA_AB:       return g_chromaAb ? 1 : 0;
    case OPT_AUTO_SCALE:      return g_autoScale ? 1 : 0;
    case OPT_CUBE_DISTORTION:     return g_customTheme.cubeDistortion ? 1 : 0;
    case OPT_THEME_WALL_GRADIENT: return g_customTheme.wallGradient ? 1 : 0;
    case OPT_THEME_STARS:         return g_customTheme.stars ? 1 : 0;
    case OPT_MUSIC_START:     return g_musicStart ? 1 : 0;
    case OPT_MUSIC_PLAYLIST:  return g_musicPlaylist ? 1 : 0;
    case OPT_MUTE_UNFOCUSED:  return g_muteUnfocused ? 1 : 0;
    case OPT_UP_KEY:      return (sm != nullptr && sm[0x2b4] != 0) ? 1 : 0;
    case OPT_SKIP_INTRO:  return g_skipIntro ? 1 : 0;
    case OPT_TIME_FMT:    return g_timeFmt ? 1 : 0;
    case OPT_MINIMAP_ENABLE: return GetMinimapDisplayEnabled() ? 1 : 0;
    case OPT_SPEEDOMETER_ENABLE: return g_speedometerEnable ? 1 : 0;
    default:              return 0;
    }
}

static int GetPopupChoiceCount(OptItem item) {
    switch (item) {
    case OPT_AA:
        if (g_aaLevelCount <= 0) ProbeSupportedAaLevels();
        return (g_aaLevelCount > 0) ? g_aaLevelCount : 1;
    case OPT_CAM_DIST:                return 8;
    case OPT_DOLLY_ZOOM:              return 11;
    case OPT_COLOR_THEME:             return 4;
    case OPT_FINISH_EFFECT:
    case OPT_FINISH_PLATFORM:
    case OPT_THEME_TERRAIN:
    case OPT_THEME_PLANETS:
    case OPT_THEME_PLAYER_CUBE:
    case OPT_THEME_DARK_CUBE:
    case OPT_THEME_PRISMS:
    case OPT_THEME_MOVING_PLATFORMS:
    case OPT_THEME_PLATFORM_BORDERS:
    case OPT_THEME_FALLING_PLATFORMS:
    case OPT_THEME_BUMPERS:
    case OPT_THEME_SWITCHES:
    case OPT_THEME_HOLO_SWITCHES:
    case OPT_THEME_SHRINKERS:         return 3;
    case OPT_EDGE_TIMER:              return 3;
    case OPT_TIMER:                   return 4;
    case OPT_TIMER_POS:               return 4;
    case OPT_PRISM_COUNTER_TYPE:      return 3;
    case OPT_PRISM_COUNTER_POS:       return 4;
    case OPT_MINIMAP_POS:             return 4;
    case OPT_SPEEDOMETER_POS:         return 4;
    case OPT_RESET_SCORE:             return 2;
    default:                          return 0;
    }
}

static int GetPopupCurrentChoice(OptItem item) {
    if (const uint8_t* field = GetCustomThemeFieldPtr(item)) {
        return (*field <= 2u) ? static_cast<int>(*field) : 0;
    }
    switch (item) {
    case OPT_AA: {
        int cnt = GetPopupChoiceCount(OPT_AA);
        int best = 0;
        for (int i = 0; i < cnt; ++i) {
            if (g_aaLevels[i] == g_aaSamples) return i;
            if (g_aaLevels[i] < g_aaSamples) best = i;
        }
        return best;
    }
    case OPT_CAM_DIST: {
        if (g_camDist < 0.0f) return 0;
        int best = 4;
        float bestDiff = 1e9f;
        for (int i = 1; i < 8; ++i) {
            float d = std::fabs(g_camDist - g_distPresets[i]);
            if (d < bestDiff) { bestDiff = d; best = i; }
        }
        return best;
    }
    case OPT_DOLLY_ZOOM: {
        if (std::memcmp(&g_camFov, "\x00\x00\x80\xbf", 4) == 0 || g_camFov == -1.0f) return 0;
        if (g_camFov == -2.0f) return 1;
        int best = 2;
        float bestDiff = 1e9f;
        for (int i = 2; i < 11; ++i) {
            float d = std::fabs(g_camFov - g_fovPresets[i]);
            if (d < bestDiff) { bestDiff = d; best = i; }
        }
        return best;
    }
    case OPT_COLOR_THEME:        return (g_colorTheme <= 3u) ? static_cast<int>(g_colorTheme) : 0;
    case OPT_EDGE_TIMER:         return (g_edgeTimerMode <= 2u) ? static_cast<int>(g_edgeTimerMode) : 0;
    case OPT_TIMER:              return std::clamp<int>(g_timerMode, 0, 3);
    case OPT_TIMER_POS:          return std::clamp<int>(g_timerPos, 0, 3);
    case OPT_PRISM_COUNTER_TYPE: return std::clamp<int>(g_prismCounterType, 0, 2);
    case OPT_PRISM_COUNTER_POS:  return std::clamp<int>(g_prismCounterPos, 0, 3);
    case OPT_MINIMAP_POS:        return std::clamp<int>(GetMinimapDisplayPos(), 0, 3);
    case OPT_SPEEDOMETER_POS:    return std::clamp<int>(g_speedometerPos, 0, 3);
    case OPT_RESET_SCORE:        return 0; // default highlight on "no"
    default:                     return 0;
    }
}

static int GetPopupDefaultChoice(OptItem item) {
    switch (item) {
    case OPT_AA: {
        int cnt = GetPopupChoiceCount(OPT_AA);
        int defAa = GetLegacyDefaultAa(1);
        int best = 0;
        for (int i = 0; i < cnt; ++i) {
            if (g_aaLevels[i] == defAa) return i;
            if (g_aaLevels[i] < defAa) best = i;
        }
        return best;
    }
    case OPT_CAM_DIST:           return 4; // 1
    case OPT_DOLLY_ZOOM:         return 0; // pc (-1)
    case OPT_COLOR_THEME:        return 0; // pc
    case OPT_FINISH_EFFECT:      return 0; // pc
    case OPT_FINISH_PLATFORM:    return 0; // pc
    case OPT_EDGE_TIMER:         return 0; // 2D
    case OPT_TIMER:              return 3; // both
    case OPT_TIMER_POS:          return 1; // TR
    case OPT_PRISM_COUNTER_TYPE: return 2; // full
    case OPT_PRISM_COUNTER_POS:  return 1; // TR
    case OPT_MINIMAP_POS:        return 0; // TL
    case OPT_SPEEDOMETER_POS:    return 3; // BR
    case OPT_RESET_SCORE:        return 0; // no
    default:                     return 0;
    }
}

static const EdgeWString* GetPopupChoiceWstr(OptItem item, int idx, EdgeWString* tmp, bool* outFreeHeap) {
    *outFreeHeap = false;
    switch (item) {
    case OPT_AA: {
        int cnt = GetPopupChoiceCount(OPT_AA);
        idx = std::clamp(idx, 0, cnt - 1);
        int samples = g_aaLevels[idx];
        if (samples <= 0) {
            Game_GetLocalizedWString(tmp, 0xc);
            *outFreeHeap = true;
            return tmp;
        }
        tmp->clear_sso();
        uint32_t n = 0;
        if (samples >= 10) {
            tmp->buf[n++] = static_cast<wchar_t>(L'0' + (samples / 10));
        }
        tmp->buf[n++] = static_cast<wchar_t>(L'0' + (samples % 10));
        tmp->buf[n++] = L'x';
        tmp->buf[n] = L'\0';
        tmp->size = n;
        return tmp;
    }
    case OPT_CAM_DIST:
        idx = std::clamp(idx, 0, 7);
        return (g_distPresets[idx] < 0.0f) ? &g_wstrMobile : FmtFloatWstr(g_distPresets[idx], tmp);
    case OPT_DOLLY_ZOOM:
        idx = std::clamp(idx, 0, 10);
        if (g_fovPresets[idx] == -1.0f) return &g_wstrPc;
        if (g_fovPresets[idx] == -2.0f) return &g_wstrMobile;
        return FmtFloatWstr(g_fovPresets[idx], tmp);
    case OPT_COLOR_THEME:
        idx = std::clamp(idx, 0, 3);
        if (idx == 1) return &g_wstrMobile;
        if (idx == 2) return &g_wstrJava;
        if (idx == 3) return &g_wstrCustom;
        return &g_wstrPc;
    case OPT_FINISH_EFFECT:
    case OPT_FINISH_PLATFORM:
    case OPT_THEME_TERRAIN:
    case OPT_THEME_PLANETS:
    case OPT_THEME_PLAYER_CUBE:
    case OPT_THEME_DARK_CUBE:
    case OPT_THEME_PRISMS:
    case OPT_THEME_MOVING_PLATFORMS:
    case OPT_THEME_PLATFORM_BORDERS:
    case OPT_THEME_FALLING_PLATFORMS:
    case OPT_THEME_BUMPERS:
    case OPT_THEME_SWITCHES:
    case OPT_THEME_HOLO_SWITCHES:
    case OPT_THEME_SHRINKERS:
        idx = std::clamp(idx, 0, 2);
        if (idx == 1) return &g_wstrMobile;
        if (idx == 2) return &g_wstrJava;
        return &g_wstrPc;
    case OPT_EDGE_TIMER:
        idx = std::clamp(idx, 0, 2);
        if (idx == 1) return &g_wstrEdgeTimer2DScaled;
        if (idx == 2) return &g_wstrEdgeTimer3D;
        return &g_wstrEdgeTimer2D;
    case OPT_TIMER:
        idx = std::clamp(idx, 0, 3);
        if (idx == 0) {
            Game_GetLocalizedWString(tmp, 0xc);
            *outFreeHeap = true;
            return tmp;
        }
        if (idx == 1) return &g_wstrTimerLevel;
        if (idx == 2) return &g_wstrTimerRank;
        return &g_wstrTimerBoth;
    case OPT_PRISM_COUNTER_TYPE:
        idx = std::clamp(idx, 0, 2);
        if (idx == 0) {
            Game_GetLocalizedWString(tmp, 0xc);
            *outFreeHeap = true;
            return tmp;
        }
        if (idx == 1) return &g_wstrShort;
        return &g_wstrFull;
    case OPT_TIMER_POS:
    case OPT_PRISM_COUNTER_POS:
    case OPT_MINIMAP_POS:
    case OPT_SPEEDOMETER_POS:
        idx = std::clamp(idx, 0, 3);
        if (idx == 0) return &g_wstrTimerPosTL;
        if (idx == 1) return &g_wstrTimerPosTR;
        if (idx == 2) return &g_wstrTimerPosBL;
        return &g_wstrTimerPosBR;
    case OPT_RESET_SCORE:
        Game_GetLocalizedWString(tmp, (idx == 1) ? 0x1c : 0x1d); // yes / no
        *outFreeHeap = true;
        return tmp;
    default:
        return nullptr;
    }
}

static void OpenPopupForRow(OptItem item, int row, bool inPause) {
    CommitPendingUiScale();
    int cnt = GetPopupChoiceCount(item);
    if (cnt <= 0) return;
    bool kbd = IsMenuInputKeyboardMode();
    g_optUi.popupOpen = true;
    g_optUi.popupInPause = inPause;
    g_optUi.popupItem = item;
    g_optUi.popupRow = row;
    g_optUi.popupChoiceCount = cnt;
    g_optUi.popupHighlight = GetPopupCurrentChoice(item);
    g_optUi.popupItemH = 0;
    g_optUi.popupSlotVisible = kbd;
    g_optUi.popupSlotPressed = false;
    g_optUi.popupNeedsInitialSync = !kbd;
    g_optUi.lastMouseX = -99999;
    g_optUi.lastMouseY = -99999;
    Game_PlaySfx(GetScreenMgr(), 2);
}

static void ApplyPopupChoice(OptItem item, int idx, int sfxId) {
    int cnt = GetPopupChoiceCount(item);
    if (cnt <= 0) {
        g_optUi.popupOpen = false;
        return;
    }
    idx = std::clamp(idx, 0, cnt - 1);
    uint8_t* sm = GetScreenMgr();
    switch (item) {
    case OPT_RESET_SCORE:
        if (idx == 1) {
            ResetAllScores_Impl();
            Game_PlaySfx(sm, 0x1);
        } else {
            Game_PlaySfx(sm, 0xb);
        }
        break;
    case OPT_AA:
        g_aaSamples = g_aaLevels[idx];
        if (sm != nullptr) {
            if (sfxId > 0) Game_PlaySfx(sm, sfxId);
            SyncGraphicsRuntime(sm, true);
        }
        SaveModSettings();
        break;
    case OPT_CAM_DIST:
        g_camDist = g_distPresets[idx];
        ApplyOptionScaleChange(sfxId);
        break;
    case OPT_DOLLY_ZOOM:
        g_camFov = g_fovPresets[idx];
        ApplyOptionScaleChange(sfxId);
        break;
    case OPT_COLOR_THEME: {
        uint8_t prev = (g_colorTheme <= 3u) ? g_colorTheme : 0u;
        uint8_t next = static_cast<uint8_t>(std::clamp(idx, 0, 3));
        if (next == 3u && g_customThemeInited == 0u) {
            g_customTheme = MakePresetThemeSettings((prev <= 2u) ? prev : 0u);
            g_customThemeInited = 1u;
        }
        g_colorTheme = next;
        SyncEffectiveThemeGlobals();
        if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
        SaveModSettings();
        break;
    }
    case OPT_FINISH_EFFECT:
    case OPT_FINISH_PLATFORM:
    case OPT_THEME_TERRAIN:
    case OPT_THEME_PLANETS:
    case OPT_THEME_PLAYER_CUBE:
    case OPT_THEME_DARK_CUBE:
    case OPT_THEME_PRISMS:
    case OPT_THEME_MOVING_PLATFORMS:
    case OPT_THEME_PLATFORM_BORDERS:
    case OPT_THEME_FALLING_PLATFORMS:
    case OPT_THEME_BUMPERS:
    case OPT_THEME_SWITCHES:
    case OPT_THEME_HOLO_SWITCHES:
    case OPT_THEME_SHRINKERS:
        if (uint8_t* field = GetCustomThemeFieldPtr(item)) {
            *field = static_cast<uint8_t>(std::clamp(idx, 0, 2));
            g_customThemeInited = 1u;
            SyncEffectiveThemeGlobals();
            if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
            SaveModSettings();
        }
        break;
    case OPT_EDGE_TIMER:
        g_edgeTimerMode = static_cast<uint8_t>(idx);
        if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
        SaveModSettings();
        break;
    case OPT_TIMER:
        g_timerMode = static_cast<uint8_t>(idx);
        if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
        SaveModSettings();
        break;
    case OPT_TIMER_POS:
        g_timerPos = static_cast<uint8_t>(idx);
        if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
        SaveModSettings();
        break;
    case OPT_PRISM_COUNTER_TYPE:
        g_prismCounterType = static_cast<uint8_t>(idx);
        if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
        SaveModSettings();
        break;
    case OPT_PRISM_COUNTER_POS:
        g_prismCounterPos = static_cast<uint8_t>(idx);
        if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
        SaveModSettings();
        break;
    case OPT_MINIMAP_POS:
        g_minimapPos = static_cast<uint8_t>(idx);
        g_minimapPosOverride = 1;
        g_lastIniMinimapAlignX = g_iniMinimapAlignX;
        g_lastIniMinimapAlignY = g_iniMinimapAlignY;
        if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
        SyncLiveWorldMinimapFields();
        SaveModSettings();
        break;
    case OPT_SPEEDOMETER_POS:
        g_speedometerPos = static_cast<uint8_t>(idx);
        if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
        SaveModSettings();
        break;
    default:
        break;
    }
    g_optUi.popupOpen = false;
}

static void StepPopupQuick(OptItem item, bool left, int sfxId) {
    int cnt = GetPopupChoiceCount(item);
    if (cnt <= 0) return;
    int cur = GetPopupCurrentChoice(item);
    int next = left ? ((cur + cnt - 1) % cnt) : ((cur + 1) % cnt);
    ApplyPopupChoice(item, next, sfxId);
}

static float GetMinimapDisplayMultiplier(OptItem item) {
    switch (item) {
    case OPT_MINIMAP_WIDTH: {
        if (g_minimapWidthOverride != 0) return g_minimapWidth;
        int32_t iniW = (g_iniMinimapWidth > 0) ? g_iniMinimapWidth : kDefMinimapWidth;
        return static_cast<float>(iniW) / static_cast<float>(kDefMinimapWidth);
    }
    case OPT_MINIMAP_HEIGHT: {
        if (g_minimapHeightOverride != 0) return g_minimapHeight;
        int32_t iniH = (g_iniMinimapHeight > 0) ? g_iniMinimapHeight : kDefMinimapHeight;
        return static_cast<float>(iniH) / static_cast<float>(kDefMinimapHeight);
    }
    case OPT_MINIMAP_RANGE: {
        if (g_minimapRangeOverride != 0) return g_minimapRange;
        float cw = (g_iniMinimapCellW > 0.001f) ? g_iniMinimapCellW : kDefMinimapCellW;
        float ch = (g_iniMinimapCellH > 0.001f) ? g_iniMinimapCellH : kDefMinimapCellH;
        float avg = 0.5f * (cw + ch);
        return (avg > 0.001f) ? (kDefMinimapCellW / avg) : 1.0f;
    }
    default:
        return 1.0f;
    }
}

static float GetSliderNormalized(OptItem item) {
    uint8_t* sm = GetScreenMgr();
    switch (item) {
    case OPT_PRISM_VFX:
        return static_cast<float>(std::clamp<int>(g_prismVfx, 0, 3)) / 3.0f;
    case OPT_UI_SCALE: {
        float cur = GetEffectiveUiScaleForDisplay();
        if (cur <= g_uiScalePresets[0]) return 0.0f;
        if (cur >= g_uiScalePresets[8]) return 1.0f;
        for (int i = 0; i < 8; ++i) {
            if (cur <= g_uiScalePresets[i + 1]) {
                float t = (cur - g_uiScalePresets[i]) / (g_uiScalePresets[i + 1] - g_uiScalePresets[i]);
                return (static_cast<float>(i) + t) / 8.0f;
            }
        }
        return 1.0f;
    }
    case OPT_SOUNDS: {
        int v = (sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x2b0) : 100;
        return static_cast<float>(std::clamp(v, 0, 100)) / 100.0f;
    }
    case OPT_MUSIC: {
        int v = (sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x2ac) : 100;
        return static_cast<float>(std::clamp(v, 0, 100)) / 100.0f;
    }
    case OPT_TIMER_SIZE:
        return static_cast<float>(NearestTimerSizeIdx(g_timerSize)) / 7.0f;
    case OPT_PRISM_COUNTER_SIZE:
        return static_cast<float>(NearestTimerSizeIdx(g_prismCounterSize)) / 7.0f;
    case OPT_SPEEDOMETER_SIZE:
        return static_cast<float>(NearestTimerSizeIdx(g_speedometerSize)) / 7.0f;
    case OPT_TIMER_BG_OPACITY:
        return static_cast<float>(std::clamp(g_timerBgOpacity, 0, 100)) / 100.0f;
    case OPT_PRISM_BG_OPACITY:
        return static_cast<float>(std::clamp(g_prismBgOpacity, 0, 100)) / 100.0f;
    case OPT_SPEEDOMETER_BG_OPACITY:
        return static_cast<float>(std::clamp(g_speedometerBgOpacity, 0, 100)) / 100.0f;
    case OPT_TIMER_SHADOW_OPACITY:
        return static_cast<float>(std::clamp(g_timerShadowOpacity, 0, 100)) / 100.0f;
    case OPT_PRISM_SHADOW_OPACITY:
        return static_cast<float>(std::clamp(g_prismShadowOpacity, 0, 100)) / 100.0f;
    case OPT_SPEEDOMETER_SHADOW_OPACITY:
        return static_cast<float>(std::clamp(g_speedometerShadowOpacity, 0, 100)) / 100.0f;
    case OPT_MINIMAP_WIDTH:
    case OPT_MINIMAP_HEIGHT:
    case OPT_MINIMAP_RANGE: {
        float cur = std::clamp(GetMinimapDisplayMultiplier(item), 0.5f, 2.5f);
        return (cur - 0.5f) / 2.0f;
    }
    case OPT_MINIMAP_BG_OPACITY:
        return static_cast<float>(std::clamp(g_minimapBgOpacity, 0, 100)) / 100.0f;
    default:
        return 0.0f;
    }
}

static float GetSliderDefaultNormalized(OptItem item) {
    switch (item) {
    case OPT_PRISM_VFX:            return 1.0f / 3.0f; // short
    case OPT_UI_SCALE:             return 2.0f / 8.0f; // 1.0
    case OPT_SOUNDS:               return 1.0f;        // 100
    case OPT_MUSIC:                return 1.0f;        // 100
    case OPT_TIMER_SIZE:           return 3.0f / 7.0f; // 1.0
    case OPT_PRISM_COUNTER_SIZE:   return 3.0f / 7.0f; // 1.0
    case OPT_SPEEDOMETER_SIZE:     return 3.0f / 7.0f; // 1.0
    case OPT_TIMER_BG_OPACITY:     return 0.7f;        // 70
    case OPT_PRISM_BG_OPACITY:     return 0.7f;        // 70
    case OPT_SPEEDOMETER_BG_OPACITY: return 0.7f;      // 70
    case OPT_TIMER_SHADOW_OPACITY: return 0.0f;        // 0
    case OPT_PRISM_SHADOW_OPACITY: return 0.0f;        // 0
    case OPT_SPEEDOMETER_SHADOW_OPACITY: return 0.0f;  // 0
    case OPT_MINIMAP_WIDTH:        return 2.0f / 8.0f; // 1.0
    case OPT_MINIMAP_HEIGHT:       return 2.0f / 8.0f; // 1.0
    case OPT_MINIMAP_RANGE:        return 2.0f / 8.0f; // 1.0
    case OPT_MINIMAP_BG_OPACITY:   return 0.7f;        // 70
    default:                       return 0.0f;
    }
}

static void SetPrismVfxValue(int idx, int sfxId, bool save) {
    idx = std::clamp(idx, 0, 3);
    if (idx == static_cast<int>(g_prismVfx)) {
        if (save) {
            SaveModSettings();
        }
        return;
    }
    g_prismVfx = static_cast<uint8_t>(idx);
    if (g_prismVfx <= 1) {
        g_prismOverrideSec = 0.0f;
    } else if (g_prismOverrideSec > 0.0f) {
        g_prismOverrideSec = (g_prismVfx == 3) ? 2.0f : 1.0f;
    }
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        if (sfxId > 0) {
            Game_PlaySfx(sm, sfxId);
        }
        SyncGraphicsRuntime(sm, false);
    }
    if (save) {
        SaveModSettings();
    }
}

static void StepSlider(OptItem item, int dir, int sfxId) {
    uint8_t* sm = GetScreenMgr();
    switch (item) {
    case OPT_PRISM_VFX: {
        int next = std::clamp(static_cast<int>(g_prismVfx) + dir, 0, 3);
        if (next != static_cast<int>(g_prismVfx)) {
            SetPrismVfxValue(next, sfxId, true);
        }
        break;
    }
    case OPT_UI_SCALE: {
        float cur = GetEffectiveUiScaleForDisplay();
        int nearest = 2;
        float bestDiff = std::fabs(cur - g_uiScalePresets[2]);
        for (int i = 0; i < 9; ++i) {
            float d = std::fabs(cur - g_uiScalePresets[i]);
            if (d < bestDiff) { bestDiff = d; nearest = i; }
        }
        int nextIdx = std::clamp(nearest + dir, 0, 8);
        float newVal = g_uiScalePresets[nextIdx];
        if (std::fabs(newVal - cur) > 0.001f && sfxId > 0) {
            Game_PlaySfx(sm, sfxId);
        }
        g_optUi.uiScalePreview = newVal;
        g_optUi.uiScalePending = true;
        break;
    }
    case OPT_SOUNDS: {
        int cur = (sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x2b0) : 100;
        int next = (dir > 0)
            ? std::clamp((cur / 5) * 5 + 5, 0, 100)
            : ((cur <= 0) ? 0 : std::clamp(((cur - 1) / 5) * 5, 0, 100));
        if (next != cur) {
            SetSfxVolume(next, sfxId, true);
        }
        break;
    }
    case OPT_MUSIC: {
        int cur = (sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x2ac) : 100;
        int next = (dir > 0)
            ? std::clamp((cur / 5) * 5 + 5, 0, 100)
            : ((cur <= 0) ? 0 : std::clamp(((cur - 1) / 5) * 5, 0, 100));
        if (next != cur) {
            SetMusicVolume(next, sfxId, true);
        }
        break;
    }
    case OPT_TIMER_SIZE:
    case OPT_PRISM_COUNTER_SIZE:
    case OPT_SPEEDOMETER_SIZE: {
        float& valRef = (item == OPT_TIMER_SIZE)         ? g_timerSize
                      : (item == OPT_PRISM_COUNTER_SIZE) ? g_prismCounterSize
                                                         : g_speedometerSize;
        int curIdx = NearestTimerSizeIdx(valRef);
        int nextIdx = std::clamp(curIdx + dir, 0, 7);
        if (nextIdx != curIdx) {
            valRef = g_timerSizePresets[nextIdx];
            if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
            SaveModSettings();
        }
        break;
    }
    case OPT_TIMER_BG_OPACITY:
    case OPT_PRISM_BG_OPACITY:
    case OPT_MINIMAP_BG_OPACITY:
    case OPT_SPEEDOMETER_BG_OPACITY:
    case OPT_TIMER_SHADOW_OPACITY:
    case OPT_PRISM_SHADOW_OPACITY:
    case OPT_SPEEDOMETER_SHADOW_OPACITY: {
        int32_t& valRef = (item == OPT_TIMER_BG_OPACITY)       ? g_timerBgOpacity
                        : (item == OPT_PRISM_BG_OPACITY)       ? g_prismBgOpacity
                        : (item == OPT_MINIMAP_BG_OPACITY)     ? g_minimapBgOpacity
                        : (item == OPT_SPEEDOMETER_BG_OPACITY) ? g_speedometerBgOpacity
                        : (item == OPT_TIMER_SHADOW_OPACITY)   ? g_timerShadowOpacity
                        : (item == OPT_PRISM_SHADOW_OPACITY)   ? g_prismShadowOpacity
                                                               : g_speedometerShadowOpacity;
        int cur = std::clamp<int>(valRef, 0, 100);
        int next = (dir > 0)
            ? std::clamp((cur / 10) * 10 + 10, 0, 100)
            : ((cur <= 0) ? 0 : std::clamp(((cur - 1) / 10) * 10, 0, 100));
        if (next != cur) {
            valRef = next;
            if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
            SaveModSettings();
        }
        break;
    }
    case OPT_MINIMAP_WIDTH:
    case OPT_MINIMAP_HEIGHT:
    case OPT_MINIMAP_RANGE: {
        float& valRef = (item == OPT_MINIMAP_WIDTH)  ? g_minimapWidth
                      : (item == OPT_MINIMAP_HEIGHT) ? g_minimapHeight
                                                     : g_minimapRange;
        uint8_t& ovrRef = (item == OPT_MINIMAP_WIDTH)  ? g_minimapWidthOverride
                        : (item == OPT_MINIMAP_HEIGHT) ? g_minimapHeightOverride
                                                       : g_minimapRangeOverride;
        float cur = GetMinimapDisplayMultiplier(item);
        int curIdx = NearestMinimapPresetIdx(cur);
        int nextIdx = std::clamp(curIdx + dir, 0, 8);
        float newVal = g_uiScalePresets[nextIdx];
        if (nextIdx != curIdx || std::fabs(newVal - cur) > 0.001f) {
            valRef = newVal;
            ovrRef = 1;
            if (item == OPT_MINIMAP_WIDTH)  g_lastIniMinimapWidth  = g_iniMinimapWidth;
            else if (item == OPT_MINIMAP_HEIGHT) g_lastIniMinimapHeight = g_iniMinimapHeight;
            else {
                g_lastIniMinimapCellW = g_iniMinimapCellW;
                g_lastIniMinimapCellH = g_iniMinimapCellH;
            }
            if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
            SyncLiveWorldMinimapFields();
            SaveModSettings();
        }
        break;
    }
    default:
        break;
    }
}

static void SetSliderFromNormalized(OptItem item, float norm, bool isDrag) {
    norm = std::clamp(norm, 0.0f, 1.0f);
    uint8_t* sm = GetScreenMgr();
    switch (item) {
    case OPT_PRISM_VFX: {
        int idx = std::clamp(static_cast<int>(std::lround(norm * 3.0f)), 0, 3);
        SetPrismVfxValue(idx, 2, !isDrag);
        break;
    }
    case OPT_UI_SCALE: {
        int idx = std::clamp(static_cast<int>(std::lround(norm * 8.0f)), 0, 8);
        float newVal = g_uiScalePresets[idx];
        float oldVal = GetEffectiveUiScaleForDisplay();
        if (std::fabs(newVal - oldVal) > 0.001f) {
            Game_PlaySfx(sm, 2);
        }
        g_optUi.uiScalePreview = newVal;
        g_optUi.uiScalePending = true;
        break;
    }
    case OPT_SOUNDS: {
        int v = std::clamp(static_cast<int>(std::lround((norm * 100.0f) / 5.0f)) * 5, 0, 100);
        SetSfxVolume(v, 2, !isDrag);
        break;
    }
    case OPT_MUSIC: {
        int v = std::clamp(static_cast<int>(std::lround((norm * 100.0f) / 5.0f)) * 5, 0, 100);
        SetMusicVolume(v, 2, !isDrag);
        break;
    }
    case OPT_TIMER_SIZE:
    case OPT_PRISM_COUNTER_SIZE:
    case OPT_SPEEDOMETER_SIZE: {
        float& valRef = (item == OPT_TIMER_SIZE)         ? g_timerSize
                      : (item == OPT_PRISM_COUNTER_SIZE) ? g_prismCounterSize
                                                         : g_speedometerSize;
        int idx = std::clamp(static_cast<int>(std::lround(norm * 7.0f)), 0, 7);
        float newVal = g_timerSizePresets[idx];
        if (std::fabs(newVal - valRef) > 0.001f) {
            valRef = newVal;
            if (sm != nullptr) Game_PlaySfx(sm, 2);
        }
        if (!isDrag) {
            SaveModSettings();
        }
        break;
    }
    case OPT_TIMER_BG_OPACITY:
    case OPT_PRISM_BG_OPACITY:
    case OPT_MINIMAP_BG_OPACITY:
    case OPT_SPEEDOMETER_BG_OPACITY:
    case OPT_TIMER_SHADOW_OPACITY:
    case OPT_PRISM_SHADOW_OPACITY:
    case OPT_SPEEDOMETER_SHADOW_OPACITY: {
        int32_t& valRef = (item == OPT_TIMER_BG_OPACITY)       ? g_timerBgOpacity
                        : (item == OPT_PRISM_BG_OPACITY)       ? g_prismBgOpacity
                        : (item == OPT_MINIMAP_BG_OPACITY)     ? g_minimapBgOpacity
                        : (item == OPT_SPEEDOMETER_BG_OPACITY) ? g_speedometerBgOpacity
                        : (item == OPT_TIMER_SHADOW_OPACITY)   ? g_timerShadowOpacity
                        : (item == OPT_PRISM_SHADOW_OPACITY)   ? g_prismShadowOpacity
                                                               : g_speedometerShadowOpacity;
        int v = std::clamp(static_cast<int>(std::lround((norm * 100.0f) / 10.0f)) * 10, 0, 100);
        if (v != valRef) {
            valRef = v;
            if (sm != nullptr) Game_PlaySfx(sm, 2);
        }
        if (!isDrag) {
            SaveModSettings();
        }
        break;
    }
    case OPT_MINIMAP_WIDTH:
    case OPT_MINIMAP_HEIGHT:
    case OPT_MINIMAP_RANGE: {
        float& valRef = (item == OPT_MINIMAP_WIDTH)  ? g_minimapWidth
                      : (item == OPT_MINIMAP_HEIGHT) ? g_minimapHeight
                                                     : g_minimapRange;
        uint8_t& ovrRef = (item == OPT_MINIMAP_WIDTH)  ? g_minimapWidthOverride
                        : (item == OPT_MINIMAP_HEIGHT) ? g_minimapHeightOverride
                                                       : g_minimapRangeOverride;
        int idx = std::clamp(static_cast<int>(std::lround(norm * 8.0f)), 0, 8);
        float newVal = g_uiScalePresets[idx];
        float cur = GetMinimapDisplayMultiplier(item);
        if (ovrRef == 0 || std::fabs(newVal - cur) > 0.001f || std::fabs(newVal - valRef) > 0.001f) {
            valRef = newVal;
            ovrRef = 1;
            if (item == OPT_MINIMAP_WIDTH)  g_lastIniMinimapWidth  = g_iniMinimapWidth;
            else if (item == OPT_MINIMAP_HEIGHT) g_lastIniMinimapHeight = g_iniMinimapHeight;
            else {
                g_lastIniMinimapCellW = g_iniMinimapCellW;
                g_lastIniMinimapCellH = g_iniMinimapCellH;
            }
            if (sm != nullptr && std::fabs(newVal - cur) > 0.001f) Game_PlaySfx(sm, 2);
            SyncLiveWorldMinimapFields();
        }
        if (!isDrag) {
            SaveModSettings();
        }
        break;
    }
    default:
        break;
    }
}

static void ResetSliderToDefault(OptItem item, int sfxId) {
    uint8_t* sm = GetScreenMgr();
    switch (item) {
    case OPT_PRISM_VFX:
        SetPrismVfxValue(1, sfxId, true);
        break;
    case OPT_UI_SCALE: {
        float cur = GetEffectiveUiScaleForDisplay();
        g_optUi.uiScalePending = false;
        g_uiScale = 1.0f;
        ApplyOptionScaleChange((std::fabs(cur - 1.0f) > 0.001f) ? sfxId : 0);
        break;
    }
    case OPT_SOUNDS:
        SetSfxVolume(100, sfxId, true);
        break;
    case OPT_MUSIC:
        SetMusicVolume(100, sfxId, true);
        break;
    case OPT_TIMER_SIZE:
    case OPT_PRISM_COUNTER_SIZE:
    case OPT_SPEEDOMETER_SIZE: {
        float& valRef = (item == OPT_TIMER_SIZE)         ? g_timerSize
                      : (item == OPT_PRISM_COUNTER_SIZE) ? g_prismCounterSize
                                                         : g_speedometerSize;
        if (std::fabs(valRef - 1.0f) > 0.001f) {
            valRef = 1.0f;
            if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
            SaveModSettings();
        }
        break;
    }
    case OPT_TIMER_BG_OPACITY:
    case OPT_PRISM_BG_OPACITY:
    case OPT_MINIMAP_BG_OPACITY:
    case OPT_SPEEDOMETER_BG_OPACITY:
    case OPT_TIMER_SHADOW_OPACITY:
    case OPT_PRISM_SHADOW_OPACITY:
    case OPT_SPEEDOMETER_SHADOW_OPACITY: {
        int defVal = (item == OPT_TIMER_BG_OPACITY || item == OPT_PRISM_BG_OPACITY ||
                      item == OPT_MINIMAP_BG_OPACITY || item == OPT_SPEEDOMETER_BG_OPACITY) ? 70 : 0;
        int32_t& valRef = (item == OPT_TIMER_BG_OPACITY)       ? g_timerBgOpacity
                        : (item == OPT_PRISM_BG_OPACITY)       ? g_prismBgOpacity
                        : (item == OPT_MINIMAP_BG_OPACITY)     ? g_minimapBgOpacity
                        : (item == OPT_SPEEDOMETER_BG_OPACITY) ? g_speedometerBgOpacity
                        : (item == OPT_TIMER_SHADOW_OPACITY)   ? g_timerShadowOpacity
                        : (item == OPT_PRISM_SHADOW_OPACITY)   ? g_prismShadowOpacity
                                                               : g_speedometerShadowOpacity;
        if (valRef != defVal) {
            valRef = defVal;
            if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
            SaveModSettings();
        }
        break;
    }
    case OPT_MINIMAP_WIDTH:
    case OPT_MINIMAP_HEIGHT:
    case OPT_MINIMAP_RANGE: {
        float& valRef = (item == OPT_MINIMAP_WIDTH)  ? g_minimapWidth
                      : (item == OPT_MINIMAP_HEIGHT) ? g_minimapHeight
                                                     : g_minimapRange;
        uint8_t& ovrRef = (item == OPT_MINIMAP_WIDTH)  ? g_minimapWidthOverride
                        : (item == OPT_MINIMAP_HEIGHT) ? g_minimapHeightOverride
                                                       : g_minimapRangeOverride;
        float cur = GetMinimapDisplayMultiplier(item);
        if (ovrRef == 0 || std::fabs(valRef - 1.0f) > 0.001f) {
            bool changed = (std::fabs(cur - 1.0f) > 0.001f);
            valRef = 1.0f;
            ovrRef = 1;
            if (item == OPT_MINIMAP_WIDTH)  g_lastIniMinimapWidth  = g_iniMinimapWidth;
            else if (item == OPT_MINIMAP_HEIGHT) g_lastIniMinimapHeight = g_iniMinimapHeight;
            else {
                g_lastIniMinimapCellW = g_iniMinimapCellW;
                g_lastIniMinimapCellH = g_iniMinimapCellH;
            }
            if (changed && sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
            SyncLiveWorldMinimapFields();
            SaveModSettings();
        }
        break;
    }
    default:
        break;
    }
}

static const EdgeWString* GetOptValueWstr(OptItem item, EdgeWString* tmp, bool* outFreeHeap) {
    *outFreeHeap = false;
    uint8_t* sm = GetScreenMgr();
    switch (item) {
    case OPT_FULLSCREEN: {
        void* app = reinterpret_cast<void*(__cdecl*)()>(0x4eecd0)();
        using GetFsFn = uint8_t(__thiscall*)(void*);
        uint8_t isFs = (*reinterpret_cast<GetFsFn**>(app))[0x14 / 4](app);
        Game_GetLocalizedWString(tmp, isFs ? 0xb : 0xc);
        *outFreeHeap = true;
        return tmp;
    }
    case OPT_PRISM_VFX:
        if (g_prismVfx == 0) {
            Game_GetLocalizedWString(tmp, 0xc);
            *outFreeHeap = true;
            return tmp;
        }
        return (g_prismVfx == 3) ? &g_wstrLong : ((g_prismVfx == 2) ? &g_wstrMedium : &g_wstrShort);
    case OPT_CHROMA_AB:
        Game_GetLocalizedWString(tmp, g_chromaAb ? 0xb : 0xc);
        *outFreeHeap = true;
        return tmp;
    case OPT_AA:
        if (g_aaSamples <= 0) {
            Game_GetLocalizedWString(tmp, 0xc);
            *outFreeHeap = true;
            return tmp;
        }
        tmp->clear_sso();
        {
            uint32_t n = 0;
            int v = g_aaSamples;
            if (v >= 10) {
                tmp->buf[n++] = static_cast<wchar_t>(L'0' + (v / 10));
            }
            tmp->buf[n++] = static_cast<wchar_t>(L'0' + (v % 10));
            tmp->buf[n++] = L'x';
            tmp->buf[n] = L'\0';
            tmp->size = n;
        }
        return tmp;
    case OPT_UI_SCALE:
        return FmtFloatWstr(GetEffectiveUiScaleForDisplay(), tmp);
    case OPT_AUTO_SCALE:
        Game_GetLocalizedWString(tmp, g_autoScale ? 0xb : 0xc);
        *outFreeHeap = true;
        return tmp;
    case OPT_CAM_DIST:
        return (g_camDist < 0.0f) ? &g_wstrMobile : FmtFloatWstr(g_camDist, tmp);
    case OPT_DOLLY_ZOOM:
        if (std::memcmp(&g_camFov, "\x00\x00\x80\xbf", 4) == 0 || g_camFov == -1.0f) return &g_wstrPc;
        if (g_camFov == -2.0f) return &g_wstrMobile;
        return FmtFloatWstr(g_camFov, tmp);
    case OPT_COLOR_THEME:
        if (g_colorTheme == 1) return &g_wstrMobile;
        if (g_colorTheme == 2) return &g_wstrJava;
        if (g_colorTheme == 3) return &g_wstrCustom;
        return &g_wstrPc;
    case OPT_THEME_WALL_GRADIENT:
        return g_customTheme.wallGradient ? &g_wstrMobile : &g_wstrThemePcJava;
    case OPT_THEME_STARS:
        return g_customTheme.stars ? &g_wstrJava : &g_wstrThemePcMobile;
    case OPT_CUBE_DISTORTION:
        Game_GetLocalizedWString(tmp, g_customTheme.cubeDistortion ? 0xb : 0xc);
        *outFreeHeap = true;
        return tmp;
    case OPT_FINISH_EFFECT:
    case OPT_FINISH_PLATFORM:
    case OPT_THEME_TERRAIN:
    case OPT_THEME_PLANETS:
    case OPT_THEME_PLAYER_CUBE:
    case OPT_THEME_DARK_CUBE:
    case OPT_THEME_PRISMS:
    case OPT_THEME_MOVING_PLATFORMS:
    case OPT_THEME_PLATFORM_BORDERS:
    case OPT_THEME_FALLING_PLATFORMS:
    case OPT_THEME_BUMPERS:
    case OPT_THEME_SWITCHES:
    case OPT_THEME_HOLO_SWITCHES:
    case OPT_THEME_SHRINKERS:
        if (const uint8_t* field = GetCustomThemeFieldPtr(item)) {
            if (*field == 1) return &g_wstrMobile;
            if (*field == 2) return &g_wstrJava;
        }
        return &g_wstrPc;
    case OPT_SOUNDS:
        FmtInt0to100Wstr((sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x2b0) : 100, tmp);
        return tmp;
    case OPT_MUSIC:
        FmtInt0to100Wstr((sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x2ac) : 100, tmp);
        return tmp;
    case OPT_MUSIC_START:
        return g_musicStart ? &g_wstrFall : &g_wstrLand;
    case OPT_MUSIC_PLAYLIST:
        return (g_musicPlaylist != 0) ? &g_wstrJava : &g_wstrPc;
    case OPT_MUTE_UNFOCUSED:
        Game_GetLocalizedWString(tmp, g_muteUnfocused ? 0xb : 0xc);
        *outFreeHeap = true;
        return tmp;
    case OPT_UP_KEY: {
        uint8_t upVal = (sm != nullptr) ? sm[0x2b4] : 0;
        Game_GetLocalizedWString(tmp, (upVal != 0) ? 0x44 : 0x43);
        *outFreeHeap = true;
        return tmp;
    }
    case OPT_SKIP_INTRO:
        Game_GetLocalizedWString(tmp, g_skipIntro ? 0xb : 0xc);
        *outFreeHeap = true;
        return tmp;
    case OPT_TIME_FMT:
        return g_timeFmt ? &g_wstrNew : &g_wstrDef;
    case OPT_EDGE_TIMER:
        if (g_edgeTimerMode == 1) return &g_wstrEdgeTimer2DScaled;
        if (g_edgeTimerMode == 2) return &g_wstrEdgeTimer3D;
        return &g_wstrEdgeTimer2D;
    case OPT_TIMER:
        if (g_timerMode == 0) {
            Game_GetLocalizedWString(tmp, 0xc);
            *outFreeHeap = true;
            return tmp;
        }
        if (g_timerMode == 1) return &g_wstrTimerLevel;
        if (g_timerMode == 2) return &g_wstrTimerRank;
        return &g_wstrTimerBoth;
    case OPT_TIMER_SIZE:
        return GetTimerSizeValWstr(g_timerSize);
    case OPT_TIMER_POS:
        if (g_timerPos == 0) return &g_wstrTimerPosTL;
        if (g_timerPos == 1) return &g_wstrTimerPosTR;
        if (g_timerPos == 2) return &g_wstrTimerPosBL;
        return &g_wstrTimerPosBR;
    case OPT_PRISM_COUNTER_TYPE:
        if (g_prismCounterType == 0) {
            Game_GetLocalizedWString(tmp, 0xc);
            *outFreeHeap = true;
            return tmp;
        }
        if (g_prismCounterType == 1) return &g_wstrShort;
        return &g_wstrFull;
    case OPT_PRISM_COUNTER_SIZE:
        return GetTimerSizeValWstr(g_prismCounterSize);
    case OPT_PRISM_COUNTER_POS:
        if (g_prismCounterPos == 0) return &g_wstrTimerPosTL;
        if (g_prismCounterPos == 1) return &g_wstrTimerPosTR;
        if (g_prismCounterPos == 2) return &g_wstrTimerPosBL;
        return &g_wstrTimerPosBR;
    case OPT_TIMER_BG_OPACITY:
        FmtInt0to100Wstr(std::clamp(g_timerBgOpacity, 0, 100), tmp);
        return tmp;
    case OPT_PRISM_BG_OPACITY:
        FmtInt0to100Wstr(std::clamp(g_prismBgOpacity, 0, 100), tmp);
        return tmp;
    case OPT_TIMER_SHADOW_OPACITY:
        FmtInt0to100Wstr(std::clamp(g_timerShadowOpacity, 0, 100), tmp);
        return tmp;
    case OPT_PRISM_SHADOW_OPACITY:
        FmtInt0to100Wstr(std::clamp(g_prismShadowOpacity, 0, 100), tmp);
        return tmp;
    case OPT_MINIMAP_ENABLE:
        Game_GetLocalizedWString(tmp, GetMinimapDisplayEnabled() ? 0xb : 0xc);
        *outFreeHeap = true;
        return tmp;
    case OPT_MINIMAP_WIDTH:
    case OPT_MINIMAP_HEIGHT:
    case OPT_MINIMAP_RANGE:
        return FmtFloatWstr(GetMinimapDisplayMultiplier(item), tmp);
    case OPT_MINIMAP_BG_OPACITY:
        FmtInt0to100Wstr(std::clamp(g_minimapBgOpacity, 0, 100), tmp);
        return tmp;
    case OPT_MINIMAP_POS: {
        uint8_t pos = GetMinimapDisplayPos();
        if (pos == 0) return &g_wstrTimerPosTL;
        if (pos == 1) return &g_wstrTimerPosTR;
        if (pos == 2) return &g_wstrTimerPosBL;
        return &g_wstrTimerPosBR;
    }
    case OPT_SPEEDOMETER_ENABLE:
        Game_GetLocalizedWString(tmp, g_speedometerEnable ? 0xb : 0xc);
        *outFreeHeap = true;
        return tmp;
    case OPT_SPEEDOMETER_SIZE:
        return GetTimerSizeValWstr(g_speedometerSize);
    case OPT_SPEEDOMETER_BG_OPACITY:
        FmtInt0to100Wstr(std::clamp(g_speedometerBgOpacity, 0, 100), tmp);
        return tmp;
    case OPT_SPEEDOMETER_SHADOW_OPACITY:
        FmtInt0to100Wstr(std::clamp(g_speedometerShadowOpacity, 0, 100), tmp);
        return tmp;
    case OPT_SPEEDOMETER_POS:
        if (g_speedometerPos == 0) return &g_wstrTimerPosTL;
        if (g_speedometerPos == 1) return &g_wstrTimerPosTR;
        if (g_speedometerPos == 2) return &g_wstrTimerPosBL;
        return &g_wstrTimerPosBR;
    default:
        return nullptr;
    }
}

static void ToggleFullscreenWithSfx(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    void* app = reinterpret_cast<void*(__cdecl*)()>(0x4eecd0)();
    if (app != nullptr) {
        using GetFsFn = uint8_t(__thiscall*)(void*);
        using SetFsFn = void(__thiscall*)(void*, int);
        uint8_t isFs = (*reinterpret_cast<GetFsFn**>(app))[0x14 / 4](app);
        (*reinterpret_cast<SetFsFn**>(app))[0x10 / 4](app, isFs ? 0 : 1);
    }
    if (sm != nullptr) {
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        Game_SaveUserSettings(sm + 0x298);
    }
}

static void ToggleUpKeyWithSfx(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
    sm[0x2b4] ^= 1u;
    if (sfxId > 0) Game_PlaySfx(sm, sfxId);
    Game_SaveUserSettings(sm + 0x298);
}

static void MinimapEnableAction_Impl(int sfxId) {
    bool next = !GetMinimapDisplayEnabled();
    g_minimapEnable = next ? 1u : 0u;
    g_minimapEnableOverride = 1;
    g_lastIniMinimapVisible = g_iniMinimapVisible;
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
    SyncLiveWorldMinimapFields();
    SaveModSettings();
}

static void SpeedometerEnableAction_Impl(int sfxId) {
    g_speedometerEnable = g_speedometerEnable ? 0u : 1u;
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr && sfxId > 0) Game_PlaySfx(sm, sfxId);
    SaveModSettings();
}

static void ExecuteOptToggle(OptItem item, int sfxId) {
    switch (item) {
    case OPT_FULLSCREEN:         ToggleFullscreenWithSfx(sfxId); break;
    case OPT_CHROMA_AB:          ChromaAbAction_Impl(sfxId); break;
    case OPT_AUTO_SCALE:         AutoScaleAction_Impl(sfxId); break;
    case OPT_CUBE_DISTORTION:    CubeDistortionAction_Impl(sfxId); break;
    case OPT_THEME_WALL_GRADIENT:
    case OPT_THEME_STARS:
        if (uint8_t* field = GetCustomThemeFieldPtr(item)) {
            *field = (*field != 0u) ? 0u : 1u;
            g_customThemeInited = 1u;
            SyncEffectiveThemeGlobals();
            if (uint8_t* sm = GetScreenMgr()) {
                if (sfxId > 0) Game_PlaySfx(sm, sfxId);
            }
            SaveModSettings();
        }
        break;
    case OPT_MUSIC_START:     MusicStartAction_Impl(sfxId); break;
    case OPT_MUSIC_PLAYLIST:  MusicPlaylistAction_Impl(sfxId); break;
    case OPT_MUTE_UNFOCUSED:  MuteUnfocusedAction_Impl(sfxId); break;
    case OPT_UP_KEY:          ToggleUpKeyWithSfx(sfxId); break;
    case OPT_SKIP_INTRO:      SkipIntroAction_Impl(sfxId); break;
    case OPT_TIME_FMT:        TimeFmtAction_Impl(sfxId); break;
    case OPT_MINIMAP_ENABLE:     MinimapEnableAction_Impl(sfxId); break;
    case OPT_SPEEDOMETER_ENABLE: SpeedometerEnableAction_Impl(sfxId); break;
    default: break;
    }
}

static int* __cdecl GetMenuBasePt_Impl(int* outPt) {
    uint8_t* sm = GetScreenMgr();
    int virtW = (sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x24) : 640;
    int origX = static_cast<int>(static_cast<double>(virtW) * 0.125);
    int expand = std::clamp(origX - 6, 0, 48);
    if (outPt != nullptr) {
        outPt[0] = origX - expand;
        outPt[1] = 4;
    }
    return outPt;
}

static void __stdcall DrawMenuStrip_Impl(int unusedArg) {
    (void)unusedArg;
    int basePt[2] = {0, 0};
    GetMenuBasePt_Impl(basePt);
    uint8_t* sm = GetScreenMgr();
    int virtH = (sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x28) : 480;
    int menuW = *reinterpret_cast<const int*>(0x589ddc);
    uint32_t col = 0xff3b3b3bu;
    Game_DrawQuad(basePt[0] - 6, 0, menuW + 12, virtH, &col);
}

static void __cdecl DrawMenuRainbowBorder_Impl(int frame) {
    uint8_t* renderer = GetRenderer();
    uint8_t* sm = GetScreenMgr();
    if (renderer == nullptr || sm == nullptr) return;
    uint8_t* hGrp = *reinterpret_cast<uint8_t**>(sm + 0x58);
    uint8_t* vGrp = *reinterpret_cast<uint8_t**>(sm + 0x5c);
    if (hGrp == nullptr || vGrp == nullptr) return;
    int16_t cnt = *reinterpret_cast<int16_t*>(hGrp + 8);
    if (cnt <= 0) return;

    uint8_t** hSprites = *reinterpret_cast<uint8_t***>(hGrp + 0x20);
    uint8_t** vSprites = *reinterpret_cast<uint8_t***>(vGrp + 0x20);
    if (hSprites == nullptr || vSprites == nullptr) return;

    int basePt[2] = {0, 0};
    GetMenuBasePt_Impl(basePt);
    int bx = basePt[0];
    int virtH = *reinterpret_cast<int*>(sm + 0x28);
    int menuW = *reinterpret_cast<const int*>(0x589ddc);
    uint32_t u = static_cast<uint32_t>(frame);
    uint32_t ucnt = static_cast<uint32_t>(cnt);

    int32_t savedSrc = *reinterpret_cast<const int32_t*>(renderer + 0x18);
    int32_t savedDst = *reinterpret_cast<const int32_t*>(renderer + 0x1c);
    Game_SetBlend(renderer, 0);
    DrawSmoothRainbowSprite(hSprites, ucnt, u + 0u, menuW, 2, bx, 4, 255);
    DrawSmoothRainbowSprite(hSprites, ucnt, u + 1u, menuW, 2, bx, virtH - 0xf, 255);
    DrawSmoothRainbowSprite(vSprites, ucnt, u + 2u, 2, virtH - 0x11, bx, 4, 255);
    DrawSmoothRainbowSprite(vSprites, ucnt, u + 3u, 2, virtH - 0x11, bx + menuW - 2, 4, 255);
    Game_SetD3DBlend(renderer, savedSrc, savedDst);
}

constexpr int kMenuWideW         = 0x170; // 368px (320 + 48 leftward expansion)
constexpr int kMainOptValRelX    = 0xf0;  // 120px value column (rightBound - valX)
constexpr int kPauseOptValRelX   = 0x101; // 105px value column (rightBound - valX)

static void ComputeOptScrollMetrics(
    bool inPause, int cat, int nRows, int paneLeft, int topOriginY, int menuW, float scrollY, OptScrollMetrics* out
) {
    if (out == nullptr) return;
    *out = OptScrollMetrics{};
    if (!IsOptCategoryScrollable(cat) || nRows < 2) return;

    out->scrollable = true;
    out->nRows = nRows;
    out->nScrollRows = nRows - 1;
    out->backIdx = nRows - 1;

    if (!inPause) {
        int virtH = reinterpret_cast<int(__cdecl*)()>(0x496060)();
        out->rowH = 0x26;
        out->stride = 0x2a;
        out->slotPad = 2;
        out->listTop = topOriginY + 0x56;
        out->backH = kMainOptRowH;
        out->backTop = topOriginY + (std::max)(0x56 + out->stride, virtH - 0x3b);
        out->listBottom = (std::max)(out->listTop + out->stride, out->backTop - 6);
        out->viewH = out->listBottom - out->listTop;
        out->contentH = out->nScrollRows * out->stride;
        out->maxScroll = static_cast<float>((std::max)(0, out->contentH - out->viewH));
        out->hasScrollbar = (out->maxScroll > 0.5f);
        out->sbW = 18;
        out->sbX = paneLeft + menuW - 22;
        out->sbY = out->listTop;
        out->sbH = out->viewH;
        out->rightBound = out->hasScrollbar ? (out->sbX - 6) : (paneLeft + menuW - 8);
    } else {
        uint8_t* sm = GetScreenMgr();
        int scrH = (sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x28) : 720;
        out->rowH = kPauseOptRowH;
        out->stride = kPauseOptRowH;
        out->slotPad = 0;
        out->listTop = topOriginY + 0x75;
        out->backH = kPauseOptRowH;
        int maxBottom = scrH - 8;
        int availH = (std::max)(2 * kPauseOptRowH, maxBottom - out->listTop);
        int maxVisScroll = (std::max)(kPauseOptRowH, availH - out->backH);
        out->contentH = out->nScrollRows * out->stride;
        out->viewH = (std::min)(out->contentH, maxVisScroll);
        out->listBottom = out->listTop + out->viewH;
        out->backTop = out->listBottom;
        out->maxScroll = static_cast<float>((std::max)(0, out->contentH - out->viewH));
        out->hasScrollbar = (out->maxScroll > 0.5f);
        out->sbW = 18;
        out->sbX = paneLeft + kMenuWideW - 22;
        out->sbY = out->listTop;
        out->sbH = out->viewH;
        out->rightBound = out->hasScrollbar ? (out->sbX - 6) : (paneLeft + kMenuWideW - 6);
    }

    if (out->hasScrollbar && out->contentH > 0) {
        out->thumbH = (std::min)(30, out->sbH);
        float frac = std::clamp(scrollY / out->maxScroll, 0.0f, 1.0f);
        out->thumbYf = frac * static_cast<float>((std::max)(0, out->sbH - out->thumbH));
        out->thumbY = out->sbY + static_cast<int>(std::lround(out->thumbYf));
    }
}

static void EnsureOptRowVisible(bool inPause, int cat, int selRow) {
    if (!IsOptCategoryScrollable(cat)) return;
    int nRows = GetOptRowCount(cat, inPause);
    int basePt[2] = {0, 0};
    reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
    int menuW = *reinterpret_cast<const int*>(0x589ddc);
    int topOriginY = inPause ? (basePt[1] + GetPauseContentShiftY()) : basePt[1];
    OptScrollState& st = GetOptScrollState(inPause);
    OptScrollMetrics m{};
    ComputeOptScrollMetrics(inPause, cat, nRows, basePt[0], topOriginY, menuW, st.targetY, &m);
    if (!m.scrollable || m.maxScroll <= 0.5f || selRow < 0 || selRow >= m.nScrollRows) return;

    float rowTop = static_cast<float>(selRow * m.stride);
    float rowBot = rowTop + static_cast<float>(m.stride);
    if (rowTop < st.targetY) {
        st.targetY = rowTop;
    } else if (rowBot > st.targetY + static_cast<float>(m.viewH)) {
        st.targetY = rowBot - static_cast<float>(m.viewH);
    }
    st.targetY = std::clamp(st.targetY, 0.0f, m.maxScroll);
}

static void AdvanceOptScrollRenderY(OptScrollState& st, float maxScroll) {
    double nowSec = GetMarqueeTimeSec();
    float dt = (st.lastRenderSec > 0.0)
        ? std::clamp(static_cast<float>(nowSec - st.lastRenderSec), 0.0f, 0.05f)
        : 0.0f;
    st.lastRenderSec = nowSec;

    st.maxScroll = maxScroll;
    st.targetY = std::clamp(st.targetY, 0.0f, maxScroll);
    st.renderY = std::clamp(st.renderY, 0.0f, maxScroll);

    int prevPx = static_cast<int>(std::floor(st.renderY));
    if (std::fabs(st.targetY - st.renderY) <= 0.02f) {
        st.renderY = st.targetY;
    } else if (dt > 0.0f) {
        float rate = st.dragActive ? 30.0f : 14.0f;
        float alpha = 1.0f - std::exp(-dt * rate);
        st.renderY += (st.targetY - st.renderY) * alpha;
        if (std::fabs(st.targetY - st.renderY) <= 0.02f) {
            st.renderY = st.targetY;
        }
    }
    if (static_cast<int>(std::floor(st.renderY)) != prevPx) {
        ++st.revision;
    }
}

struct EdgeStdString24 {
    char buf[16];
    uint32_t size;
    uint32_t cap;
};

static void* g_optSliderSprite = nullptr;

static void* GetOptSliderSprite() {
    if (g_optSliderSprite == nullptr) {
        EdgeStdString24 name{};
        std::memcpy(name.buf, "slider", 7);
        name.size = 6;
        name.cap = 15;
        g_optSliderSprite = reinterpret_cast<void*(__cdecl*)(const void*)>(0x4a88c0)(&name);
    }
    return g_optSliderSprite;
}

static void DrawOptScrollbar(const OptScrollMetrics& m, int fadePct) {
    if (!m.hasScrollbar || m.sbH <= 0) return;

    int clampedFade = std::clamp(fadePct, 0, 100);
    uint32_t barRgb = (clampedFade >= 100) ? 0x7d7d7du : Game_ColorLerp(0xffffffu, 0x7d7d7du, clampedFade);
    uint32_t barCol = RgbToRgbaOpaque(barRgb);

    Game_DrawQuad(m.sbX + 4, m.sbY, 9, m.sbH, &barCol);
    if (m.thumbH <= 0) return;

    int thumbBaseY = m.sbY + static_cast<int>(std::floor(m.thumbYf));
    float thumbSubY = m.thumbYf - std::floor(m.thumbYf);

    ScopedUiMatrixTranslate handleMat(0.0f, -thumbSubY);
    Game_DrawQuad(m.sbX, thumbBaseY, m.sbW, m.thumbH, &barCol);

    int spriteAlpha = (clampedFade * 255) / 100;
    void* spr = GetOptSliderSprite();
    void* renderer = *reinterpret_cast<void**>(0x5f8770);
    if (spr != nullptr && renderer != nullptr && spriteAlpha > 0) {
        Game_SetBlend(renderer, 0);
        reinterpret_cast<void(__thiscall*)(void*, int, int, int, int)>(0x4a7bb0)(
            spr, m.sbX, thumbBaseY, spriteAlpha, 0x14
        );
        Game_SetBlend(renderer, 3);
    }
}

static void DrawOptRowControlIndicator(
    OptItem item, int trackX, int rightBound, int trackY,
    uint32_t activeRgba, uint32_t dimRgba, uint32_t defMarkRgba
) {
    OptCtrlKind kind = GetOptCtrlKind(item);
    int trackW = rightBound - trackX;
    if (trackW <= 8) return;

    if (kind == CTRL_SLIDER) {
        Game_DrawQuad(trackX, trackY, trackW, 2, &dimRgba);
        float norm = std::clamp(GetSliderNormalized(item), 0.0f, 1.0f);
        int fillW = std::clamp(static_cast<int>(std::lround(norm * static_cast<float>(trackW))), 0, trackW);
        if (fillW > 0) {
            Game_DrawQuad(trackX, trackY, fillW, 2, &activeRgba);
        }
        float defNorm = std::clamp(GetSliderDefaultNormalized(item), 0.0f, 1.0f);
        int defX = std::clamp(trackX + static_cast<int>(std::lround(defNorm * static_cast<float>(trackW))) - 1, trackX, rightBound - 2);
        Game_DrawQuad(defX, trackY - 2, 2, 6, &defMarkRgba);
        int knobX = std::clamp(trackX + fillW - 2, trackX, rightBound - 4);
        Game_DrawQuad(knobX, trackY - 3, 4, 8, &activeRgba);
    } else if (kind == CTRL_POPUP) {
        int count = GetPopupChoiceCount(item);
        if (count <= 0) return;
        int cur = GetPopupCurrentChoice(item);
        int dotSize = (count * 6 <= trackW) ? 3 : 2;
        int gap = (count * 6 <= trackW) ? 3 : 2;
        int pitch = dotSize + gap;
        for (int i = 0; i < count; ++i) {
            int dx = trackX + i * pitch;
            if (dx + dotSize > rightBound) break;
            if (i == cur) {
                Game_DrawQuad(dx, trackY - 1, dotSize, dotSize + 1, &activeRgba);
            } else {
                Game_DrawQuad(dx, trackY, dotSize, dotSize, &dimRgba);
            }
        }
    } else if (kind == CTRL_TOGGLE) {
        int state = GetToggleState(item);
        constexpr int kToggleW = 22;
        constexpr int kKnobW = 11;
        Game_DrawQuad(trackX, trackY - 1, kToggleW, 5, &dimRgba);
        Game_DrawQuad(trackX + (state ? (kToggleW - kKnobW) : 0), trackY - 1, kKnobW, 5, &activeRgba);
    }
}

static void DrawOptPopupOverlay(
    OptItem item, int rightBoundDraw, int rowTopY, int rowH,
    bool inPause, int scrollXForScreen, uint32_t hiBarRgb
) {
    int nChoices = GetPopupChoiceCount(item);
    if (nChoices <= 0) return;
    g_optUi.popupChoiceCount = nChoices;
    if (g_optUi.popupHighlight < 0 || g_optUi.popupHighlight >= nChoices) {
        g_optUi.popupHighlight = GetPopupCurrentChoice(item);
    }

    int popupW = inPause ? ((item == OPT_RESET_SCORE) ? 104 : 132)
                         : ((item == OPT_RESET_SCORE) ? 116 : 148);
    int itemH = (nChoices >= 9) ? 20 : 22;
    int padY = 4;
    int popupH = 2 * padY + nChoices * itemH;

    uint8_t* sm = GetScreenMgr();
    int scrH = (sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x28)
                               : (reinterpret_cast<int(__cdecl*)()>(0x496060)() + 0x11);
    int belowY = rowTopY + rowH + 2;
    int aboveY = rowTopY - popupH - 2;
    int popupY = belowY;
    if (belowY + popupH > scrH - 8 && aboveY >= 4) {
        popupY = aboveY;
    } else if (popupY + popupH > scrH - 4) {
        popupY = (std::max)(4, scrH - popupH - 4);
    }
    int drawX = rightBoundDraw - popupW;

    g_optUi.popupRectX = drawX - scrollXForScreen;
    g_optUi.popupRectY = popupY;
    g_optUi.popupRectW = popupW;
    g_optUi.popupRectH = popupH;
    g_optUi.popupItemH = itemH;
    g_optUi.popupPadY  = padY;

    uint32_t brdCol = RgbToRgbaOpaque(0x3b3b3bu);
    uint32_t bgCol  = RgbToRgbaOpaque(0xffffffu);
    uint32_t popHiRgb = g_optUi.popupSlotPressed
        ? hiBarRgb
        : Game_ColorLerp(0xffffffu, hiBarRgb, 25);
    uint32_t hiCol  = RgbToRgbaOpaque(popHiRgb);
    Game_DrawQuad(drawX - 2, popupY - 2, popupW + 4, popupH + 4, &brdCol);
    Game_DrawQuad(drawX, popupY, popupW, popupH, &bgCol);
    if (g_optUi.popupSlotVisible && g_optUi.popupHighlight >= 0 && g_optUi.popupHighlight < nChoices) {
        Game_DrawQuad(drawX, popupY + padY + g_optUi.popupHighlight * itemH, popupW, itemH, &hiCol);
    }

    uint32_t textCol = RgbToRgbaOpaque(0x3b3b3bu);
    uint32_t* pFontCol = reinterpret_cast<uint32_t*>(0x5f4268);
    uint32_t savedFontCol = *pFontCol;
    int defChoice = GetPopupDefaultChoice(item);

    for (int i = 0; i < nChoices; ++i) {
        int choiceTopY = popupY + padY + i * itemH;
        if (i == defChoice) {
            Game_DrawQuad(drawX + 8, choiceTopY + (itemH - 4) / 2, 4, 4, &textCol);
        }
        *pFontCol = textCol;
        EdgeWString cTmp;
        bool cFree = false;
        const EdgeWString* cWs = GetPopupChoiceWstr(item, i, &cTmp, &cFree);
        if (cWs != nullptr) {
            int choiceTextY = choiceTopY + (itemH - 18) / 2;
            DrawTextAutoFit(cWs, drawX + 18, choiceTextY, 0x2000, 0x2000, 0, 0, 0x14, popupW - 24);
        }
        if (cFree) cTmp.free_if_heap();
    }
    *pFontCol = savedFontCol;

    if (g_optUi.popupSlotVisible && g_optUi.popupHighlight >= 0 && g_optUi.popupHighlight < nChoices) {
        Game_DrawRainbowRectBorder(
            GetCurrentMenuRenderFrame(),
            drawX, popupY + padY + g_optUi.popupHighlight * itemH, popupW, itemH, 2, 255
        );
    }
}

static void UpdateOptControlsPerFrame(bool inPause, int cat, int selRow, int valXScreen, int rightBoundScreen) {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
    int trackW = rightBoundScreen - valXScreen;

    OptScrollState& scrollSt = GetOptScrollState(inPause);
    int32_t wheelDelta = g_optWheelAccum.exchange(0, std::memory_order_acq_rel);
    bool scrollableCat = IsOptCategoryScrollable(cat);
    if (!scrollableCat) {
        scrollSt.dragActive = false;
    } else {
        int basePt[2] = {0, 0};
        reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
        int menuW = *reinterpret_cast<const int*>(0x589ddc);
        int nRows = GetOptRowCount(cat, inPause);
        int topOriginY = inPause ? (basePt[1] + GetPauseContentShiftY()) : basePt[1];
        OptScrollMetrics m{};
        ComputeOptScrollMetrics(inPause, cat, nRows, basePt[0], topOriginY, menuW, scrollSt.renderY, &m);
        scrollSt.maxScroll = m.maxScroll;

        if (scrollSt.dragActive) {
            if ((sm[0] != 0 || sm[0xca] != 0) && m.hasScrollbar) {
                int my = *reinterpret_cast<int*>(sm + 0xa4);
                float trackRange = static_cast<float>((std::max)(1, scrollSt.dragTrackH - scrollSt.dragThumbH));
                float frac = std::clamp(
                    static_cast<float>(my - scrollSt.dragGrabOffsetY - scrollSt.dragTrackY) / trackRange,
                    0.0f, 1.0f
                );
                scrollSt.targetY = frac * m.maxScroll;
                ++scrollSt.revision;
            } else {
                scrollSt.dragActive = false;
            }
        } else if (wheelDelta != 0 && m.hasScrollbar &&
                   !(g_optUi.popupOpen && g_optUi.popupInPause == inPause) &&
                   !(g_optUi.dragActive && g_optUi.dragInPause == inPause)) {
            float stepPx = static_cast<float>(m.stride) * 2.0f;
            float nextY = std::clamp(
                scrollSt.targetY - (static_cast<float>(wheelDelta) / 120.0f) * stepPx,
                0.0f, m.maxScroll
            );
            if (std::fabs(nextY - scrollSt.targetY) > 0.01f) {
                scrollSt.targetY = nextY;
                ++scrollSt.revision;
            }
        }
    }

    if (g_optUi.dragActive && g_optUi.dragInPause == inPause) {
        if (sm[0] != 0 || sm[0xca] != 0) {
            int mx = *reinterpret_cast<int*>(sm + 0xa0);
            float norm = (g_optUi.dragTrackW > 0)
                ? std::clamp(static_cast<float>(mx - g_optUi.dragTrackX) / static_cast<float>(g_optUi.dragTrackW), 0.0f, 1.0f)
                : 0.0f;
            SetSliderFromNormalized(g_optUi.dragItem, norm, true);
        } else {
            g_optUi.dragActive = false;
            if (g_optUi.uiScalePending) {
                CommitPendingUiScale();
            } else if (g_optUi.dragItem == OPT_SOUNDS || g_optUi.dragItem == OPT_MUSIC) {
                Game_SaveUserSettings(sm + 0x298);
            } else {
                SaveModSettings();
            }
        }
    }

    if (g_optUi.uiScalePending && !g_optUi.dragActive &&
        sm[0xb2] == 0 && sm[0xb5] == 0 && sm[0xb3] == 0 && sm[0xb6] == 0) {
        CommitPendingUiScale();
    }

    if (g_optUi.popupOpen && g_optUi.popupInPause == inPause) {
        if (sm[0xa8] != 0 && g_optUi.popupItemH > 0) {
            int mx = *reinterpret_cast<int*>(sm + 0xa0);
            int my = *reinterpret_cast<int*>(sm + 0xa4);
            bool moved = (mx != g_optUi.lastMouseX || my != g_optUi.lastMouseY);
            if (moved) {
                g_optUi.lastMouseX = mx;
                g_optUi.lastMouseY = my;
                SetMenuInputKeyboardMode(false);
            }
            bool insidePopup =
                mx >= g_optUi.popupRectX && mx < g_optUi.popupRectX + g_optUi.popupRectW &&
                my >= g_optUi.popupRectY + g_optUi.popupPadY &&
                my < g_optUi.popupRectY + g_optUi.popupRectH - g_optUi.popupPadY;
            if (moved || g_optUi.popupNeedsInitialSync) {
                bool silent = g_optUi.popupNeedsInitialSync && !moved;
                g_optUi.popupNeedsInitialSync = false;
                if (insidePopup) {
                    int idx = (my - (g_optUi.popupRectY + g_optUi.popupPadY)) / g_optUi.popupItemH;
                    if (idx >= 0 && idx < g_optUi.popupChoiceCount) {
                        bool changed = (!g_optUi.popupSlotVisible || idx != g_optUi.popupHighlight);
                        g_optUi.popupHighlight = idx;
                        g_optUi.popupSlotVisible = true;
                        if (changed && !silent) {
                            Game_PlaySfx(sm, 2);
                        }
                    }
                } else if (!IsMenuInputKeyboardMode()) {
                    g_optUi.popupSlotVisible = false;
                }
            }
            bool mouseDown = (sm[0] != 0 || sm[0xca] != 0);
            g_optUi.popupSlotPressed = g_optUi.popupSlotVisible && mouseDown && insidePopup;
        } else {
            if (g_optUi.popupItemH > 0 && !IsMenuInputKeyboardMode() && sm[0] == 0 && sm[0xca] == 0) {
                g_optUi.popupSlotVisible = false;
            }
            g_optUi.popupSlotPressed = false;
        }
    }

    (void)trackW;
    bool resetDown = ((GetAsyncKeyState('R') & 0x8000) != 0)
                  || ((GetAsyncKeyState(VK_BACK) & 0x8000) != 0)
                  || ((GetAsyncKeyState(VK_DELETE) & 0x8000) != 0);
    if (resetDown && !g_optUi.prevResetKeyDown && !g_optUi.popupOpen && !g_optUi.dragActive && IsCurrentMenuSlotVisible()) {
        OptItem items[kMaxOptRows];
        int nRows = GetOptItems(cat, inPause, items);
        if (selRow >= 0 && selRow < nRows && GetOptCtrlKind(items[selRow]) == CTRL_SLIDER) {
            ResetSliderToDefault(items[selRow], 2);
        }
    }
    g_optUi.prevResetKeyDown = resetDown;
}

static int HandleOptMouseHitTest(
    bool inPause, int cat, int& selRow, const int* pt,
    int nRows, const OptItem* items, const int* rowTops, int rowH,
    int minX, int maxX, int valXScreen, int rightBoundScreen,
    const OptScrollMetrics* scrollM = nullptr
) {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr || pt == nullptr) return -1;
    int px = pt[0];
    int py = pt[1];
    bool isPress = (sm[0xcb] != 0);
    bool isRelease = (sm[0xca] == 0 && sm[0xcc] != 0);
    OptScrollState& scrollSt = GetOptScrollState(inPause);

    if (g_optUi.popupOpen && g_optUi.popupInPause == inPause) {
        if (g_optUi.popupItemH > 0 &&
            px >= g_optUi.popupRectX && px < g_optUi.popupRectX + g_optUi.popupRectW &&
            py >= g_optUi.popupRectY && py < g_optUi.popupRectY + g_optUi.popupRectH) {
            int relY = py - (g_optUi.popupRectY + g_optUi.popupPadY);
            int idx = std::clamp(relY / g_optUi.popupItemH, 0, g_optUi.popupChoiceCount - 1);
            bool changed = (!g_optUi.popupSlotVisible || idx != g_optUi.popupHighlight);
            g_optUi.popupHighlight = idx;
            g_optUi.popupSlotVisible = true;
            g_optUi.popupNeedsInitialSync = false;
            g_optUi.popupSlotPressed = !isRelease;
            if (isPress) {
                SetMenuInputKeyboardMode(false);
                g_optUi.suppressNextClickAction = false;
            }
            if (changed) {
                Game_PlaySfx(sm, 2);
            }
            return g_optUi.popupRow;
        }
        if (isPress) {
            g_optUi.popupOpen = false;
            g_optUi.suppressNextClickAction = true;
            Game_PlaySfx(sm, 0xb);
        }
        return -1;
    }

    if (g_optUi.dragActive && g_optUi.dragInPause == inPause) {
        float norm = (g_optUi.dragTrackW > 0)
            ? std::clamp(static_cast<float>(px - g_optUi.dragTrackX) / static_cast<float>(g_optUi.dragTrackW), 0.0f, 1.0f)
            : 0.0f;
        SetSliderFromNormalized(g_optUi.dragItem, norm, !isRelease);
        if (isRelease) {
            g_optUi.dragActive = false;
            if (g_optUi.uiScalePending) {
                CommitPendingUiScale();
            } else if (g_optUi.dragItem == OPT_SOUNDS || g_optUi.dragItem == OPT_MUSIC) {
                Game_SaveUserSettings(sm + 0x298);
            } else {
                SaveModSettings();
            }
            g_optUi.suppressNextClickAction = true;
            return -1;
        }
        return g_optUi.dragRow;
    }

    if (scrollSt.dragActive) {
        if (scrollM != nullptr && scrollM->hasScrollbar) {
            float trackRange = static_cast<float>((std::max)(1, scrollSt.dragTrackH - scrollSt.dragThumbH));
            float frac = std::clamp(
                static_cast<float>(py - scrollSt.dragGrabOffsetY - scrollSt.dragTrackY) / trackRange,
                0.0f, 1.0f
            );
            scrollSt.targetY = frac * scrollM->maxScroll;
            ++scrollSt.revision;
        }
        if (isRelease) {
            scrollSt.dragActive = false;
            g_optUi.suppressNextClickAction = true;
            return -1;
        }
        return -1;
    }

    if (scrollM != nullptr && scrollM->scrollable && scrollM->hasScrollbar &&
        px >= scrollM->sbX - 2 && px <= scrollM->sbX + scrollM->sbW + 2 &&
        py >= scrollM->listTop && py < scrollM->listBottom) {
        if (isPress) {
            scrollSt.dragActive = true;
            scrollSt.dragTrackY = scrollM->sbY;
            scrollSt.dragTrackH = scrollM->sbH;
            scrollSt.dragThumbH = scrollM->thumbH;
            scrollSt.maxScroll = scrollM->maxScroll;
            if (py >= scrollM->thumbY && py < scrollM->thumbY + scrollM->thumbH) {
                scrollSt.dragGrabOffsetY = py - scrollM->thumbY;
            } else {
                scrollSt.dragGrabOffsetY = scrollM->thumbH / 2;
                float trackRange = static_cast<float>((std::max)(1, scrollSt.dragTrackH - scrollSt.dragThumbH));
                float frac = std::clamp(
                    static_cast<float>(py - scrollSt.dragGrabOffsetY - scrollSt.dragTrackY) / trackRange,
                    0.0f, 1.0f
                );
                scrollSt.targetY = frac * scrollM->maxScroll;
            }
            ++scrollSt.revision;
            g_optUi.suppressNextClickAction = true;
        } else if (isRelease) {
            g_optUi.suppressNextClickAction = true;
        }
        return -1;
    }

    if (isPress) {
        g_optUi.suppressNextClickAction = false;
    }

    int hitRow = -1;
    if (px >= minX && px < maxX) {
        for (int row = 0; row < nRows; ++row) {
            int rH = rowH;
            if (scrollM != nullptr && scrollM->scrollable) {
                if (row == scrollM->backIdx) {
                    rH = scrollM->backH + 2 * scrollM->slotPad;
                } else if (py < scrollM->listTop || py >= scrollM->listBottom ||
                           (scrollM->hasScrollbar && px >= scrollM->sbX - 2)) {
                    continue;
                }
            }
            if (py >= rowTops[row] && py < rowTops[row] + rH) {
                hitRow = row;
                break;
            }
        }
    }
    if (hitRow < 0) return -1;

    if (selRow != hitRow) {
        CommitPendingUiScale();
        Game_PlaySfx(sm, 2);
        selRow = hitRow;
    }

    OptItem item = items[hitRow];
    if (GetOptCtrlKind(item) == CTRL_SLIDER) {
        if (px >= valXScreen - 8) {
            int tw = rightBoundScreen - valXScreen;
            g_optUi.dragTrackX = valXScreen;
            g_optUi.dragTrackW = (tw > 1) ? tw : 1;
            g_optUi.dragItem = item;
            g_optUi.dragRow = hitRow;
            g_optUi.dragInPause = inPause;
            g_optUi.dragActive = !isRelease;
            float norm = std::clamp(static_cast<float>(px - valXScreen) / static_cast<float>(g_optUi.dragTrackW), 0.0f, 1.0f);
            SetSliderFromNormalized(item, norm, !isRelease);
            if (isRelease) {
                g_optUi.dragActive = false;
                if (g_optUi.uiScalePending) {
                    CommitPendingUiScale();
                } else if (item == OPT_SOUNDS || item == OPT_MUSIC) {
                    Game_SaveUserSettings(sm + 0x298);
                } else {
                    SaveModSettings();
                }
                g_optUi.suppressNextClickAction = true;
                return -1;
            }
            return hitRow;
        }
        // Clicked on label side of slider: focus row without mutating value
        if (isRelease) {
            g_optUi.suppressNextClickAction = true;
            return -1;
        }
        return hitRow;
    }

    (void)cat;
    return hitRow;
}

static void ComputeMainOptLayout(int nRows, int virtH, int* outStride, int* outBackTopRel, int* outRowH) {
    int backIdx = nRows - 1;
    int rowH = kMainOptRowH;
    if (nRows > 0 && (0x56 + nRows * rowH) > (virtH - 4)) {
        rowH = std::clamp((virtH - 4 - 0x56) / nRows, 24, kMainOptRowH);
    }
    int stride = 0x32;
    int backTopRel = virtH - 0x3b;
    if (backIdx > 0 && (0x56 + stride * backIdx) > backTopRel) {
        int avail = backTopRel - 0x56;
        stride = (avail > 0) ? (avail / backIdx) : rowH;
        if (stride < rowH) stride = rowH;
    }
    int minBackRel = 0x56 + stride * backIdx;
    if (backTopRel < minBackRel) backTopRel = minBackRel;
    if (backTopRel + rowH > virtH - 2 && backIdx > 0) {
        int maxLastTop = (std::max)(0x56, virtH - 2 - rowH);
        stride = (maxLastTop - 0x56) / backIdx;
        if (stride < rowH) stride = rowH;
        backTopRel = 0x56 + stride * backIdx;
    }
    *outStride = stride;
    *outBackTopRel = backTopRel;
    if (outRowH != nullptr) *outRowH = rowH;
}

// --- Main OptionsMenu (state 9) hooks ---
static void __cdecl OptionsOnEnter_Impl(uint8_t* optionsMenu) {
    reinterpret_cast<void(__thiscall*)(void*)>(0x495ed0)(optionsMenu);
    ResetOptUiState(false);
    g_mainOptScroll.Reset();
    g_mainOptPendingReturnCat = -1;
    g_mainOptCat = OPT_CAT_ROOT;
    *reinterpret_cast<int*>(optionsMenu + 0x10) = 0;
}

static void __declspec(naked) hk_optOnEnter() {
    __asm {
        push ecx
        call OptionsOnEnter_Impl
        add esp, 4
        ret
    }
}

static int __cdecl OptionsHitTest_Impl(uint8_t* optionsMenu, const int* pt) {
    if (*reinterpret_cast<int*>(optionsMenu + 4) < 8 || *reinterpret_cast<int*>(optionsMenu + 8) >= 0) {
        return -1;
    }
    int basePt[2] = {0, 0};
    reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
    int virtH = reinterpret_cast<int(__cdecl*)()>(0x496060)();
    int menuW = *reinterpret_cast<const int*>(0x589ddc);
    OptItem items[kMaxOptRows];
    int nRows = GetOptItems(g_mainOptCat, false, items);
    int backIdx = nRows - 1;

    OptScrollMetrics scrollM{};
    ComputeOptScrollMetrics(false, g_mainOptCat, nRows, basePt[0], basePt[1], menuW, g_mainOptScroll.renderY, &scrollM);

    int minX = basePt[0] + 2;
    int maxX = basePt[0] + menuW - 2;
    int valXScreen = basePt[0] + kMainOptValRelX;
    int rightBoundScreen = scrollM.scrollable ? scrollM.rightBound : (basePt[0] + menuW - 8);
    int rowTops[kMaxOptRows];
    int hitRowH = 0;

    if (scrollM.scrollable) {
        int scrollOff = static_cast<int>(std::lround(g_mainOptScroll.renderY));
        for (int row = 0; row < nRows; ++row) {
            int yPos = (row == backIdx) ? scrollM.backTop : (scrollM.listTop + scrollM.stride * row - scrollOff);
            rowTops[row] = yPos - scrollM.slotPad;
        }
        hitRowH = scrollM.rowH + 2 * scrollM.slotPad;
    } else {
        int stride = 0x32;
        int backTopRel = 0;
        int rowH = kMainOptRowH;
        ComputeMainOptLayout(nRows, virtH, &stride, &backTopRel, &rowH);
        int slotPad = (std::max)(0, (stride - rowH) / 2);
        for (int row = 0; row < nRows; ++row) {
            int yRel = (row == backIdx) ? backTopRel : (0x56 + stride * row);
            rowTops[row] = basePt[1] + yRel - slotPad;
        }
        hitRowH = rowH + 2 * slotPad;
    }

    int& sel = *reinterpret_cast<int*>(optionsMenu + 0x10);
    return HandleOptMouseHitTest(
        false, g_mainOptCat, sel, pt,
        nRows, items, rowTops, hitRowH,
        minX, maxX, valXScreen, rightBoundScreen,
        scrollM.scrollable ? &scrollM : nullptr
    );
}

static void __declspec(naked) hk_optHitTest() {
    __asm {
        push DWORD PTR [esp + 4]
        push ecx
        call OptionsHitTest_Impl
        add esp, 8
        ret 4
    }
}

static void __cdecl OptionsCancel_Impl(uint8_t* optionsMenu) {
    if (*reinterpret_cast<int*>(optionsMenu + 8) >= 0) return;
    Game_PlaySfx(GetScreenMgr(), 0xb);
    if (g_optUi.popupOpen && !g_optUi.popupInPause) {
        g_optUi.popupOpen = false;
        return;
    }
    ResetOptUiState(true);
    int parentCat = GetOptParentCategory(g_mainOptCat);
    if (parentCat >= 0) {
        g_mainOptPendingReturnCat = parentCat;
        *reinterpret_cast<int*>(optionsMenu + 4) = 8;
        *reinterpret_cast<int*>(optionsMenu + 8) = 8;
        *reinterpret_cast<int*>(optionsMenu + 0xc) = 2; // vanilla fade-out style (no flash/slide)
        return;
    }
    reinterpret_cast<void(__thiscall*)(void*, int)>(0x495f10)(optionsMenu, 2);
}

static void __declspec(naked) hk_optOnCancel() {
    __asm {
        push ecx
        call OptionsCancel_Impl
        add esp, 4
        ret
    }
}

static uint32_t __cdecl OptionsIsAdj_Impl(int row) {
    OptItem items[kMaxOptRows];
    int cnt = GetOptItems(g_mainOptCat, false, items);
    if (row < 0 || row >= cnt) return 0;
    return IsOptItemAdjustable(items[row]) ? 1u : 0u;
}

static void __declspec(naked) hk_optIsAdj() {
    __asm {
        push ecx
        push edx
        push DWORD PTR [esp + 12]
        call OptionsIsAdj_Impl
        add esp, 4
        pop edx
        pop ecx
        ret 4
    }
}

static void __cdecl OptionsAction_Impl(uint8_t* optionsMenu) {
    if (*reinterpret_cast<int*>(optionsMenu + 8) >= 0) return;
    uint8_t* sm = GetScreenMgr();
    bool isLr = (sm != nullptr && (sm[0xb3] != 0 || sm[0xb6] != 0));
    if (g_optUi.popupOpen && !g_optUi.popupInPause) {
        if (!isLr) {
            ApplyPopupChoice(g_optUi.popupItem, g_optUi.popupHighlight, 0xc);
        }
        return;
    }
    if (g_optUi.suppressNextClickAction && !isLr) {
        g_optUi.suppressNextClickAction = false;
        return;
    }

    int row = *reinterpret_cast<int*>(optionsMenu + 0x10);
    OptItem items[kMaxOptRows];
    int cnt = GetOptItems(g_mainOptCat, false, items);
    if (row < 0 || row >= cnt) return;
    OptItem item = items[row];
    int targetCat = GetOptTargetCategory(item);
    if (targetCat >= 0) {
        ResetOptUiState(true);
        Game_PlaySfx(sm, 0xc);
        *reinterpret_cast<int*>(optionsMenu + 4) = 8;
        *reinterpret_cast<int*>(optionsMenu + 8) = 8;
        *reinterpret_cast<int*>(optionsMenu + 0xc) = kOptCatEnterBase + targetCat;
        return;
    }
    if (item == OPT_BACK) {
        OptionsCancel_Impl(optionsMenu);
        return;
    }

    OptCtrlKind kind = GetOptCtrlKind(item);
    if (kind == CTRL_TOGGLE) {
        ExecuteOptToggle(item, 2);
    } else if (kind == CTRL_POPUP) {
        if (isLr) {
            StepPopupQuick(item, sm[0xb3] != 0, 2);
        } else {
            OpenPopupForRow(item, row, false);
        }
    } else if (kind == CTRL_CONFIRM_POPUP) {
        if (!isLr) {
            OpenPopupForRow(item, row, false);
        }
    } else if (kind == CTRL_SLIDER) {
        int dir = (sm != nullptr && sm[0xb3] != 0) ? -1 : 1;
        StepSlider(item, dir, 2);
    }
}

static void __declspec(naked) hk_optOnAction() {
    __asm {
        push ecx
        call OptionsAction_Impl
        add esp, 4
        ret
    }
}

static void __cdecl OptionsUpdate_Impl(uint8_t* optionsMenu, float dt) {
    int& enterFrames = *reinterpret_cast<int*>(optionsMenu + 4);
    int& exitTimer = *reinterpret_cast<int*>(optionsMenu + 8);
    int& selRow = *reinterpret_cast<int*>(optionsMenu + 0x10);

    if (enterFrames >= 8 && exitTimer < 0) {
        uint8_t* sm = GetScreenMgr();
        int nRows = GetOptRowCount(g_mainOptCat, false);
        if (g_optUi.popupOpen && !g_optUi.popupInPause) {
            if (sm[0xad] != 0) {
                SetMenuInputKeyboardMode(true);
                g_optUi.popupNeedsInitialSync = false;
                g_optUi.popupSlotPressed = false;
                Game_PlaySfx(sm, 2);
                if (!g_optUi.popupSlotVisible) {
                    g_optUi.popupSlotVisible = true;
                } else if (g_optUi.popupChoiceCount > 0) {
                    g_optUi.popupHighlight = (g_optUi.popupHighlight + g_optUi.popupChoiceCount - 1) % g_optUi.popupChoiceCount;
                }
            } else if (sm[0xb0] != 0) {
                SetMenuInputKeyboardMode(true);
                g_optUi.popupNeedsInitialSync = false;
                g_optUi.popupSlotPressed = false;
                Game_PlaySfx(sm, 2);
                if (!g_optUi.popupSlotVisible) {
                    g_optUi.popupSlotVisible = true;
                } else if (g_optUi.popupChoiceCount > 0) {
                    g_optUi.popupHighlight = (g_optUi.popupHighlight + 1) % g_optUi.popupChoiceCount;
                }
            } else if (sm[0xc8] != 0) {
                SetMenuInputKeyboardMode(true);
                OptionsCancel_Impl(optionsMenu);
            } else if (sm[0xc5] != 0) {
                SetMenuInputKeyboardMode(true);
                g_optUi.popupNeedsInitialSync = false;
                g_optUi.popupSlotPressed = false;
                if (!g_optUi.popupSlotVisible) {
                    g_optUi.popupSlotVisible = true;
                    Game_PlaySfx(sm, 2);
                } else {
                    ApplyPopupChoice(g_optUi.popupItem, g_optUi.popupHighlight, 0xc);
                }
            }
        } else {
            if (sm[0xad] != 0) {
                CommitPendingUiScale();
                Game_PlaySfx(sm, 2);
                if (--selRow < 0) selRow = nRows - 1;
                EnsureOptRowVisible(false, g_mainOptCat, selRow);
            } else if (sm[0xb0] != 0) {
                CommitPendingUiScale();
                Game_PlaySfx(sm, 2);
                if (++selRow >= nRows) selRow = 0;
                EnsureOptRowVisible(false, g_mainOptCat, selRow);
            } else {
                bool lr = (sm[0xb3] != 0 || sm[0xb6] != 0);
                if (lr && OptionsIsAdj_Impl(selRow) != 0) {
                    OptionsAction_Impl(optionsMenu);
                } else if (sm[0xc8] != 0) {
                    OptionsCancel_Impl(optionsMenu);
                } else if (sm[0xc5] != 0) {
                    OptionsAction_Impl(optionsMenu);
                }
            }
        }

        int basePt[2] = {0, 0};
        reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
        int menuW = *reinterpret_cast<const int*>(0x589ddc);
        UpdateOptControlsPerFrame(false, g_mainOptCat, selRow, basePt[0] + kMainOptValRelX, basePt[0] + menuW - 8);
    }

    if (enterFrames < 8) {
        ++enterFrames;
    }
    int& exitDest = *reinterpret_cast<int*>(optionsMenu + 0xc);
    if (exitTimer >= 0 && exitDest >= kOptCatEnterBase) {
        if (exitTimer == 0) {
            ResetOptUiState(false);
            g_mainOptScroll.Reset();
            g_mainOptCat = exitDest - kOptCatEnterBase;
            selRow = 0;
            enterFrames = 0;
            exitTimer = -1;
            exitDest = 0;
            uint8_t* sm = GetScreenMgr();
            if (sm != nullptr) {
                *reinterpret_cast<uint32_t*>(sm + 0x54) = 2; // child fade-in from arrived header
            }
        } else {
            --exitTimer;
        }
    } else if (exitTimer >= 0 && g_mainOptPendingReturnCat >= 0) {
        if (exitTimer == 0) {
            ResetOptUiState(false);
            g_mainOptScroll.Reset();
            int prevCat = g_mainOptCat;
            int targetCat = g_mainOptPendingReturnCat;
            g_mainOptPendingReturnCat = -1;
            g_mainOptCat = targetCat;
            selRow = GetOptParentRowForCategory(prevCat);
            enterFrames = 0;
            exitTimer = -1;
            exitDest = 0;
            uint8_t* sm = GetScreenMgr();
            if (sm != nullptr) {
                *reinterpret_cast<uint32_t*>(sm + 0x54) = 0x0a; // parent return: slide bar down from header
            }
        } else {
            --exitTimer;
        }
    } else {
        reinterpret_cast<void(__thiscall*)(void*, float)>(0x495e70)(optionsMenu, dt);
    }
}

static void __declspec(naked) hk_optUpdate() {
    __asm {
        push DWORD PTR [esp + 4]
        push ecx
        call OptionsUpdate_Impl
        add esp, 8
        ret 4
    }
}

static void __cdecl RenderMainOptionsBody_Impl(uint8_t* ebpFrame, uint32_t barOuterRgb) {
    uint8_t* menuObj = *reinterpret_cast<uint8_t**>(ebpFrame - 0x5c);
    int selRow = *reinterpret_cast<int*>(menuObj + 0x10);
    bool drawUnselected = (ebpFrame[-0x41] != 0);
    int xBase = *reinterpret_cast<int*>(ebpFrame - 0x4c);
    int yBase = *reinterpret_cast<int*>(ebpFrame - 0x48);
    uint32_t selRgb = *reinterpret_cast<uint32_t*>(ebpFrame - 0x50);
    uint32_t normRgb = *reinterpret_cast<uint32_t*>(ebpFrame - 0x64);
    uint32_t barInnerRgb = *reinterpret_cast<uint32_t*>(ebpFrame - 0x68);
    int barSlideY = *reinterpret_cast<int*>(ebpFrame - 0x60);

    OptItem items[kMaxOptRows];
    int nRows = GetOptItems(g_mainOptCat, false, items);
    int backIdx = nRows - 1;
    int virtH = reinterpret_cast<int(__cdecl*)()>(0x496060)();
    int menuW = *reinterpret_cast<const int*>(0x589ddc);

    OptScrollMetrics scrollM{};
    ComputeOptScrollMetrics(false, g_mainOptCat, nRows, xBase, yBase, menuW, g_mainOptScroll.renderY, &scrollM);
    if (scrollM.scrollable) {
        AdvanceOptScrollRenderY(g_mainOptScroll, scrollM.maxScroll);
        ComputeOptScrollMetrics(false, g_mainOptCat, nRows, xBase, yBase, menuW, g_mainOptScroll.renderY, &scrollM);
    }

    int stride = 0x32;
    int backTopRel = 0;
    int rowH = kMainOptRowH;
    if (scrollM.scrollable) {
        stride = scrollM.stride;
        rowH   = scrollM.rowH;
    } else {
        ComputeMainOptLayout(nRows, virtH, &stride, &backTopRel, &rowH);
    }
    int slotPad = scrollM.scrollable ? scrollM.slotPad : (std::max)(0, (stride - rowH) / 2);

    // Header text uses pre-computed RGBA at [ebp - 0x40]
    const int rightBound = scrollM.scrollable ? scrollM.rightBound : (xBase + menuW - 8);
    const int labelX     = xBase + 0x1e;
    const int valX       = xBase + kMainOptValRelX;
    constexpr int kMinColGap = 6;

    uint32_t hdrRgba = *reinterpret_cast<uint32_t*>(ebpFrame - 0x40);
    *reinterpret_cast<uint32_t*>(0x5f4268) = hdrRgba;
    EdgeWString hdrTmp;
    bool hdrFree = false;
    const EdgeWString* hdrWs = GetOptHeaderWstr(g_mainOptCat, &hdrTmp, &hdrFree);
    DrawTextAutoFit(hdrWs, labelX, yBase + 0xa, 0x2000, 0x2000, 1, 0, 4, rightBound - labelX);
    if (hdrFree) hdrTmp.free_if_heap();

    // Highlight bar
    uint32_t rawBarInnerRgb = barInnerRgb;
    uint32_t rawBarOuterRgb = barOuterRgb;
    uint32_t effBarInnerRgb = (barInnerRgb == 0xffffffu || barInnerRgb == 0x808080u)
        ? barInnerRgb
        : ApplyMenuSlotRainbowStyle(barInnerRgb);
    uint32_t effBarOuterRgb = (barOuterRgb == barInnerRgb)
        ? effBarInnerRgb
        : barOuterRgb;

    auto drawRowHighlight = [&](int topY, int rH, int rowW) {
        if (effBarInnerRgb != effBarOuterRgb) {
            uint32_t outerRgba = RgbToRgbaOpaque(effBarOuterRgb);
            Game_DrawQuad(xBase + 2, topY - slotPad - 2, rowW, rH + 2 * slotPad + 4, &outerRgba);
            uint32_t innerRgba = RgbToRgbaOpaque(effBarInnerRgb);
            Game_DrawQuad(xBase + 4, topY - slotPad, rowW - 4, rH + 2 * slotPad, &innerRgba);
        } else {
            uint32_t fillRgba = RgbToRgbaOpaque(effBarInnerRgb);
            Game_DrawQuad(xBase + 2, topY - slotPad, rowW, rH + 2 * slotPad, &fillRgba);
        }
    };

    auto drawRowContent = [&](int row, int rowTopY, int rH) {
        if (selRow != row && !drawUnselected) return;
        uint32_t fgRgb = (selRow == row) ? selRgb : normRgb;
        uint32_t bgRgb = (selRow == row) ? effBarInnerRgb : 0xffffffu;
        uint32_t rgba = RgbToRgbaOpaque(fgRgb);
        *reinterpret_cast<uint32_t*>(ebpFrame - 0x40) = rgba;
        *reinterpret_cast<uint32_t*>(0x5f4268) = rgba;

        int rDy = (kMainOptRowH - rH) / 2;
        OptCtrlKind kind = GetOptCtrlKind(items[row]);
        bool hasIndicator = (kind == CTRL_TOGGLE || kind == CTRL_POPUP || kind == CTRL_SLIDER);
        int lblTextY = rowTopY + 0xc - rDy;
        int valTextY = hasIndicator ? (rowTopY + 0x7 - rDy) : (rowTopY + 0xc - rDy);

        EdgeWString lblTmp, valTmp;
        bool lblFree = false, valFree = false;
        const EdgeWString* lblWs = GetOptLabelWstr(items[row], &lblTmp, &lblFree);
        const EdgeWString* valWs = GetOptValueWstr(items[row], &valTmp, &valFree);
        bool hasArrow = IsOptSubmenuItem(items[row]);

        int lblMaxW = (valWs != nullptr) ? ((valX - kMinColGap) - labelX)
                                         : (hasArrow ? ((rightBound - 22) - labelX) : (rightBound - labelX));
        DrawTextAutoFit(lblWs, labelX, lblTextY, 0x2000, 0x2000, 1, 0, 0x14, lblMaxW);
        if (valWs != nullptr) {
            DrawTextAutoFit(valWs, valX, valTextY, 0x2000, 0x2000, 1, 0, 0x14, rightBound - valX);
        } else if (hasArrow) {
            Game_DrawText(&g_wstrSubmenuArrow, rightBound - 6, lblTextY, 0x2000, 0x2000, 1, 0, 0x18);
        }
        if (lblFree) lblTmp.free_if_heap();
        if (valFree) valTmp.free_if_heap();

        if (hasIndicator) {
            uint32_t activeRgba  = RgbToRgbaOpaque(fgRgb);
            uint32_t dimRgba     = RgbToRgbaOpaque(LerpRgb(bgRgb, fgRgb, 0.28f));
            uint32_t defMarkRgba = RgbToRgbaOpaque(LerpRgb(bgRgb, fgRgb, 0.65f));
            int indY = rowTopY + (std::max)(0x16, 0x20 - (kMainOptRowH - rH));
            DrawOptRowControlIndicator(items[row], valX, rightBound, indY, activeRgba, dimRgba, defMarkRgba);
        }
    };

    auto getRowTopY = [&](int row) -> int {
        int y = scrollM.scrollable
            ? ((row == backIdx) ? scrollM.backTop : (scrollM.listTop + stride * row))
            : (yBase + ((row == backIdx) ? backTopRel : (0x56 + stride * row)));
        if (selRow == row) y += barSlideY;
        return y;
    };

    if (!scrollM.scrollable) {
        if (selRow >= 0 && selRow < nRows) {
            drawRowHighlight(getRowTopY(selRow), rowH, menuW - 4);
        }
        for (int row = 0; row < nRows; ++row) {
            drawRowContent(row, getRowTopY(row), rowH);
        }
        if (IsMenuRenderInteractive() && selRow >= 0 && selRow < nRows) {
            Game_DrawRainbowRectBorder(
                GetCurrentMenuRenderFrame(),
                xBase + 2, getRowTopY(selRow) - slotPad, menuW - 4, rowH + 2 * slotPad, 2, 255
            );
        }
    } else {
        int scrollRowW = scrollM.hasScrollbar ? (scrollM.sbX - 2 - (xBase + 2)) : (menuW - 4);
        {
            ScopedUiScissor clip(xBase + 2, scrollM.listTop, scrollRowW, scrollM.viewH);
            if (!clip.empty) {
                ScopedUiMatrixTranslate scrollMat(0.0f, g_mainOptScroll.renderY);
                if (selRow >= 0 && selRow < scrollM.nScrollRows) {
                    drawRowHighlight(getRowTopY(selRow), rowH, scrollRowW);
                }
                for (int row = 0; row < scrollM.nScrollRows; ++row) {
                    int topY = getRowTopY(row);
                    float visTop = static_cast<float>(topY - slotPad) - g_mainOptScroll.renderY;
                    float visBot = static_cast<float>(topY + rowH + slotPad) - g_mainOptScroll.renderY;
                    if (visBot <= static_cast<float>(scrollM.listTop) || visTop >= static_cast<float>(scrollM.listBottom)) {
                        continue;
                    }
                    drawRowContent(row, topY, rowH);
                }
                if (IsMenuRenderInteractive() && selRow >= 0 && selRow < scrollM.nScrollRows) {
                    Game_DrawRainbowRectBorder(
                        GetCurrentMenuRenderFrame(),
                        xBase + 2, getRowTopY(selRow) - slotPad, scrollRowW, rowH + 2 * slotPad, 2, 255
                    );
                }
            }
        }
        if (scrollM.hasScrollbar && drawUnselected) {
            int sbFade = std::clamp((255 - static_cast<int>(normRgb & 0xffu)) * 100 / (255 - 0x80), 0, 100);
            DrawOptScrollbar(scrollM, sbFade);
        }
        if (selRow == backIdx) {
            drawRowHighlight(getRowTopY(backIdx), scrollM.backH, menuW - 4);
        }
        drawRowContent(backIdx, getRowTopY(backIdx), scrollM.backH);
        if (IsMenuRenderInteractive() && selRow == backIdx) {
            Game_DrawRainbowRectBorder(
                GetCurrentMenuRenderFrame(),
                xBase + 2, getRowTopY(backIdx) - slotPad, menuW - 4, scrollM.backH + 2 * slotPad, 2, 255
            );
        }
    }

    if (g_optUi.popupOpen && !g_optUi.popupInPause &&
        g_optUi.popupRow >= 0 && g_optUi.popupRow < nRows) {
        int popRow = g_optUi.popupRow;
        int popRowTopY = getRowTopY(popRow);
        if (scrollM.scrollable && popRow != backIdx) {
            popRowTopY -= static_cast<int>(std::lround(g_mainOptScroll.renderY));
        }
        int popRowH = (scrollM.scrollable && popRow == backIdx) ? scrollM.backH : rowH;
        uint32_t hiBarRgb = (rawBarInnerRgb == 0xffffffu) ? rawBarOuterRgb : rawBarInnerRgb;
        DrawOptPopupOverlay(g_optUi.popupItem, rightBound, popRowTopY, popRowH, false, 0, hiBarRgb);
    }
}

static void __declspec(naked) hk_renderMainOptionsBody() {
    __asm {
        add esp, 0x28
        pushad
        push ebx
        push ebp
        call RenderMainOptionsBody_Impl
        add esp, 8
        popad
        mov eax, 0x4b37a3
        jmp eax
    }
}

// --- PauseMenu Page 1 (state 0x12) hooks ---
static void __cdecl PauseEnterP1_Impl(uint8_t* pauseMenu) {
    ResetOptUiState(false);
    g_pauseOptScroll.Reset();
    g_pauseOptCat = OPT_CAT_ROOT;
    g_pauseSubSwipeX = 0;
    g_pauseSubSwipePrevX = 0;
    g_pauseSubCatAtDepth[0] = OPT_CAT_ROOT;
    g_pauseSubCatAtDepth[1] = OPT_CAT_DISPLAY;
    g_pauseSubCatAtDepth[2] = OPT_CAT_TIMER;
    g_pauseSubRow[0] = 0;
    g_pauseSubRow[1] = 0;
    g_pauseSubRow[2] = 0;
    int prev = *reinterpret_cast<int*>(pauseMenu + 0x44);
    *reinterpret_cast<int*>(pauseMenu + 0x50) = 0;
    *reinterpret_cast<int*>(pauseMenu + 0x48) = prev;
    *reinterpret_cast<int*>(pauseMenu + 0x44) = 1;
}

static void __declspec(naked) hk_pauseEnterP1() {
    __asm {
        push esi
        call PauseEnterP1_Impl
        add esp, 4
        pop esi
        pop ebx
        ret
    }
}

static void __cdecl PauseP1Cancel_Impl(uint8_t* pauseMenu) {
    Game_PlaySfx(GetScreenMgr(), 0xb);
    if (g_optUi.popupOpen && g_optUi.popupInPause) {
        g_optUi.popupOpen = false;
        return;
    }
    ResetOptUiState(true);
    int parentCat = GetOptParentCategory(g_pauseOptCat);
    if (parentCat >= 0) {
        int depth = GetOptCategoryDepth(g_pauseOptCat);
        g_pauseSubRow[depth] = *reinterpret_cast<int*>(pauseMenu + 0x50);
        g_pauseSubCatAtDepth[depth] = g_pauseOptCat;
        int parentRow = GetOptParentRowForCategory(g_pauseOptCat);
        g_pauseOptCat = parentCat;
        g_pauseSubRow[depth - 1] = parentRow;
        *reinterpret_cast<int*>(pauseMenu + 0x50) = parentRow;
        return;
    }
    int prev = *reinterpret_cast<int*>(pauseMenu + 0x44);
    *reinterpret_cast<int*>(pauseMenu + 0x48) = prev;
    *reinterpret_cast<int*>(pauseMenu + 0x44) = 0;
}

static void __declspec(naked) hk_pauseP1Cancel() {
    __asm {
        push esi
        call PauseP1Cancel_Impl
        add esp, 4
        pop esi
        ret
    }
}

static uint32_t __cdecl PauseIsAdj_Impl(int row) {
    OptItem items[kMaxOptRows];
    int cnt = GetOptItems(g_pauseOptCat, true, items);
    if (row < 0 || row >= cnt) return 0;
    return IsOptItemAdjustable(items[row]) ? 1u : 0u;
}

static void __declspec(naked) hk_pauseIsAdj() {
    __asm {
        push ecx
        push edx
        push DWORD PTR [esp + 12]
        call PauseIsAdj_Impl
        add esp, 4
        pop edx
        pop ecx
        ret 4
    }
}

static void __cdecl PauseP1StepCursor_Impl(uint8_t* pauseMenu, int delta) {
    if (g_optUi.popupOpen && g_optUi.popupInPause) {
        SetMenuInputKeyboardMode(true);
        g_optUi.popupNeedsInitialSync = false;
        g_optUi.popupSlotPressed = false;
        if (!g_optUi.popupSlotVisible) {
            g_optUi.popupSlotVisible = true;
            return;
        }
        int cnt = g_optUi.popupChoiceCount;
        if (cnt > 0) {
            g_optUi.popupHighlight = (g_optUi.popupHighlight + delta + cnt) % cnt;
        }
        return;
    }
    CommitPendingUiScale();
    int nRows = GetOptRowCount(g_pauseOptCat, true);
    int& sel = *reinterpret_cast<int*>(pauseMenu + 0x50);
    sel += delta;
    if (sel < 0) sel = nRows - 1;
    else if (sel >= nRows) sel = 0;
    EnsureOptRowVisible(true, g_pauseOptCat, sel);
}

static void __declspec(naked) hk_pauseP1Up() {
    __asm {
        push -1
        push esi
        call PauseP1StepCursor_Impl
        add esp, 8
        mov eax, 0x4acd93
        jmp eax
    }
}

static void __declspec(naked) hk_pauseP1Down() {
    __asm {
        push 1
        push esi
        call PauseP1StepCursor_Impl
        add esp, 8
        mov eax, 0x4acd93
        jmp eax
    }
}

static int ComputePauseOptRowH(int yBase, int nRows) {
    uint8_t* sm = GetScreenMgr();
    int virtH = (sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x28) : 720;
    int topY = yBase + 0x75 + GetPauseContentShiftY();
    if (nRows > 0 && (topY + nRows * kPauseOptRowH) > (virtH - 8)) {
        return std::clamp((virtH - 8 - topY) / nRows, 24, kPauseOptRowH);
    }
    return kPauseOptRowH;
}

static int __cdecl PauseP1HitTest_Impl(uint8_t* pauseMenu, const int* pt) {
    if (*reinterpret_cast<int*>(pauseMenu + 4) < kPauseOpenReadyTick || *reinterpret_cast<int*>(pauseMenu + 8) >= 0) {
        return -1;
    }
    if (*reinterpret_cast<int*>(pauseMenu + 0x44) != 1 || *reinterpret_cast<int*>(pauseMenu + 0x4c) < kMenuWideW) {
        return -1;
    }
    int basePt[2] = {0, 0};
    reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
    int subStride = kMenuWideW;
    int targetSub = GetOptCategoryDepth(g_pauseOptCat) * subStride;
    if (g_pauseSubSwipeX != targetSub) {
        return -1;
    }

    OptItem items[kMaxOptRows];
    int nRows = GetOptItems(g_pauseOptCat, true, items);
    const int pauseShiftY = GetPauseContentShiftY();
    OptScrollMetrics scrollM{};
    ComputeOptScrollMetrics(true, g_pauseOptCat, nRows, basePt[0], basePt[1] + pauseShiftY, kMenuWideW, g_pauseOptScroll.renderY, &scrollM);

    int rowH = scrollM.scrollable ? scrollM.rowH : ComputePauseOptRowH(basePt[1], nRows);
    int rowTops[kMaxOptRows];
    if (scrollM.scrollable) {
        int scrollOff = static_cast<int>(std::lround(g_pauseOptScroll.renderY));
        for (int row = 0; row < nRows; ++row) {
            rowTops[row] = (row == scrollM.backIdx)
                ? scrollM.backTop
                : (scrollM.listTop + row * scrollM.stride - scrollOff);
        }
    } else {
        for (int row = 0; row < nRows; ++row) {
            rowTops[row] = basePt[1] + 0x75 + pauseShiftY + row * rowH;
        }
    }

    int minX = basePt[0] + 2;
    int maxX = basePt[0] + kMenuWideW - 2;
    int valXScreen = basePt[0] + kPauseOptValRelX;
    int rightBoundScreen = scrollM.scrollable ? scrollM.rightBound : (basePt[0] + kMenuWideW - 6);
    int& sel = *reinterpret_cast<int*>(pauseMenu + 0x50);
    return HandleOptMouseHitTest(
        true, g_pauseOptCat, sel, pt,
        nRows, items, rowTops, rowH,
        minX, maxX, valXScreen, rightBoundScreen,
        scrollM.scrollable ? &scrollM : nullptr
    );
}

static void __declspec(naked) hk_pauseP1HitTest() {
    __asm {
        push DWORD PTR [esp + 4]
        push ecx
        call PauseP1HitTest_Impl
        add esp, 8
        ret 12
    }
}

static void __declspec(naked) hk_pauseP1HitCmp() {
    __asm {
        push ecx
        push edx
        push eax
        call GetPauseOptRowCount
        mov ecx, eax
        pop eax
        cmp eax, ecx
        pop edx
        pop ecx
        jl hit_next
        mov eax, 0x4acfe6
        jmp eax
    hit_next:
        mov eax, 0x4acfa0
        jmp eax
    }
}

static void __cdecl PauseUpdateSwipe_Impl(uint8_t* pm) {
    int page = *reinterpret_cast<int*>(pm + 0x44);
    int& scrollX = *reinterpret_cast<int*>(pm + 0x4c);
    if (page != 0 && scrollX < kMenuWideW) {
        scrollX += 0x44;
    } else if (page == 0 && scrollX > 0) {
        scrollX -= 0x44;
    }
    if (scrollX < 0) scrollX = 0;
    else if (scrollX > kMenuWideW) scrollX = kMenuWideW;

    int basePt[2] = {0, 0};
    reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
    int subStride = kMenuWideW;
    int targetSub = GetOptCategoryDepth(g_pauseOptCat) * subStride;
    if (g_pauseSubSwipeX < targetSub) {
        g_pauseSubSwipeX += 0x44;
        if (g_pauseSubSwipeX > targetSub) g_pauseSubSwipeX = targetSub;
    } else if (g_pauseSubSwipeX > targetSub) {
        g_pauseSubSwipeX -= 0x44;
        if (g_pauseSubSwipeX < targetSub) g_pauseSubSwipeX = targetSub;
    }

    if (page == 1 && *reinterpret_cast<int*>(pm + 4) >= kPauseOpenReadyTick && *reinterpret_cast<int*>(pm + 8) < 0 &&
        scrollX >= kMenuWideW && g_pauseSubSwipeX == targetSub) {
        int selRow = *reinterpret_cast<int*>(pm + 0x50);
        UpdateOptControlsPerFrame(
            true, g_pauseOptCat, selRow,
            basePt[0] + kPauseOptValRelX,
            basePt[0] + kMenuWideW - 6
        );
    } else if (page != 1 && (g_optUi.popupInPause || g_optUi.dragInPause || g_optUi.uiScalePending)) {
        ResetOptUiState(true);
    }
}

static void __declspec(naked) hk_pauseUpdateSwipe() {
    __asm {
        push esi
        call PauseUpdateSwipe_Impl
        add esp, 4
        pop edi
        pop esi
        ret 4
    }
}

static void __cdecl PauseP1Action_Impl(uint8_t* pauseMenu) {
    uint8_t* sm = GetScreenMgr();
    bool isLr = (sm != nullptr && (sm[0xb3] != 0 || sm[0xb6] != 0));
    if (g_optUi.popupOpen && g_optUi.popupInPause) {
        if (!isLr) {
            if (!g_optUi.popupSlotVisible && sm != nullptr && sm[0xc5] != 0) {
                SetMenuInputKeyboardMode(true);
                g_optUi.popupNeedsInitialSync = false;
                g_optUi.popupSlotPressed = false;
                g_optUi.popupSlotVisible = true;
                Game_PlaySfx(sm, 2);
                return;
            }
            ApplyPopupChoice(g_optUi.popupItem, g_optUi.popupHighlight, 0xc);
        }
        return;
    }
    if (g_optUi.suppressNextClickAction && !isLr) {
        g_optUi.suppressNextClickAction = false;
        return;
    }

    int row = *reinterpret_cast<int*>(pauseMenu + 0x50);
    OptItem items[kMaxOptRows];
    int cnt = GetOptItems(g_pauseOptCat, true, items);
    if (row < 0 || row >= cnt) return;
    OptItem item = items[row];
    int targetCat = GetOptTargetCategory(item);
    if (targetCat >= 0) {
        ResetOptUiState(true);
        g_pauseOptScroll.Reset();
        int curDepth = GetOptCategoryDepth(g_pauseOptCat);
        int nextDepth = GetOptCategoryDepth(targetCat);
        Game_PlaySfx(sm, 0xc);
        g_pauseSubRow[curDepth] = row;
        g_pauseOptCat = targetCat;
        g_pauseSubCatAtDepth[nextDepth] = targetCat;
        g_pauseSubRow[nextDepth] = 0;
        *reinterpret_cast<int*>(pauseMenu + 0x50) = 0;
        return;
    }
    if (item == OPT_BACK) {
        PauseP1Cancel_Impl(pauseMenu);
        return;
    }

    OptCtrlKind kind = GetOptCtrlKind(item);
    if (kind == CTRL_TOGGLE) {
        ExecuteOptToggle(item, 2);
    } else if (kind == CTRL_POPUP) {
        if (isLr) {
            StepPopupQuick(item, sm[0xb3] != 0, 2);
        } else {
            OpenPopupForRow(item, row, true);
        }
    } else if (kind == CTRL_SLIDER) {
        int dir = (sm != nullptr && sm[0xb3] != 0) ? -1 : 1;
        StepSlider(item, dir, 2);
    }
}

static void __declspec(naked) hk_pausePage1Dispatch() {
    __asm {
        push esi
        call PauseP1Action_Impl
        add esp, 4
        pop esi
        pop ebx
        ret
    }
}

static void DrawPauseOptionsPane(int cat, int selRow, int xBase, int yBase, int slideY, int frame, int xOffset, bool isActivePane) {
    OptItem items[kMaxOptRows];
    int nRows = GetOptItems(cat, true, items);
    int paneLeft  = xBase + kMenuWideW + xOffset;
    int paneW     = kMenuWideW;
    int innerLeft = paneLeft + 2;
    int innerW    = paneW - 4;

    OptScrollMetrics scrollM{};
    ComputeOptScrollMetrics(true, cat, nRows, paneLeft, yBase + slideY, paneW, g_pauseOptScroll.renderY, &scrollM);
    if (scrollM.scrollable && isActivePane) {
        AdvanceOptScrollRenderY(g_pauseOptScroll, scrollM.maxScroll);
        ComputeOptScrollMetrics(true, cat, nRows, paneLeft, yBase + slideY, paneW, g_pauseOptScroll.renderY, &scrollM);
    }

    int rowH = scrollM.scrollable ? scrollM.rowH : ComputePauseOptRowH(yBase, nRows);
    int panelBodyH = scrollM.scrollable ? (scrollM.viewH + scrollM.backH) : (nRows * rowH);

    uint32_t whiteRgba = 0xffffffffu;
    Game_DrawQuad(innerLeft, yBase + slideY + 0x55, innerW, 0x20 + panelBodyH, &whiteRgba);
    uint32_t hdrBgRgba = 0xffcbcbcbu;
    Game_DrawQuad(innerLeft, yBase + slideY + 0x55, innerW, 0x20, &hdrBgRgba);

    const int labelX     = paneLeft + 0x18;
    const int rightBound = scrollM.scrollable ? scrollM.rightBound : (paneLeft + kMenuWideW - 6);
    const int valX       = paneLeft + kPauseOptValRelX;
    constexpr int kMinColGap = 6;

    uint32_t* pFontCol = reinterpret_cast<uint32_t*>(0x5f4268);
    *pFontCol = 0xff3b3b3bu;
    EdgeWString hdrTmp;
    bool hdrFree = false;
    const EdgeWString* hdrWs = GetOptHeaderWstr(cat, &hdrTmp, &hdrFree);
    DrawTextAutoFit(hdrWs, labelX, yBase + slideY + 0x6b, 0x2000, 0x2000, 0, 0, 0x44, rightBound - labelX);
    if (hdrFree) hdrTmp.free_if_heap();

    uint32_t rawRainbowRgb = GetSmoothMenuRainbow(frame + 3);
    uint32_t rainbowRgb = isActivePane
        ? ApplyMenuSlotRainbowStyle(rawRainbowRgb)
        : Game_ColorLerp(0xffffffu, rawRainbowRgb, 25);
    uint32_t rainbowRgba = RgbToRgbaOpaque(rainbowRgb);
    const void* dividerCol = reinterpret_cast<const void*>(0x5f93a4);

    auto drawPauseRow = [&](int row, int divAbsY, int rH) {
        int rDy = (kPauseOptRowH - rH) / 2;
        Game_DrawQuad(paneLeft, divAbsY, paneW, 1, dividerCol);

        *pFontCol = 0xff3b3b3bu;
        OptCtrlKind kind = GetOptCtrlKind(items[row]);
        bool hasIndicator = (kind == CTRL_TOGGLE || kind == CTRL_POPUP || kind == CTRL_SLIDER);
        int lblTxtAbsY = divAbsY + 0x1a - rDy;
        int valTxtAbsY = hasIndicator ? (divAbsY + 0x15 - rDy) : (divAbsY + 0x1a - rDy);

        EdgeWString lblTmp, valTmp;
        bool lblFree = false, valFree = false;
        const EdgeWString* lblWs = GetOptLabelWstr(items[row], &lblTmp, &lblFree);
        const EdgeWString* valWs = GetOptValueWstr(items[row], &valTmp, &valFree);
        bool hasArrow = IsOptSubmenuItem(items[row]);

        int lblMaxW = (valWs != nullptr) ? ((valX - kMinColGap) - labelX)
                                         : (hasArrow ? ((rightBound - 22) - labelX) : (rightBound - labelX));
        DrawTextAutoFit(lblWs, labelX, lblTxtAbsY, 0x2000, 0x2000, 0, 0, 0x44, lblMaxW);
        if (valWs != nullptr) {
            DrawTextAutoFit(valWs, valX, valTxtAbsY, 0x2000, 0x2000, 0, 0, 0x44, rightBound - valX);
        } else if (hasArrow) {
            Game_DrawText(&g_wstrSubmenuArrow, rightBound - 6, lblTxtAbsY, 0x2000, 0x2000, 0, 0, 0x48);
        }
        if (lblFree) lblTmp.free_if_heap();
        if (valFree) valTmp.free_if_heap();

        if (hasIndicator) {
            uint32_t bgRgb = (selRow == row) ? rainbowRgb : 0xffffffu;
            uint32_t fgRgb = 0x3b3b3bu;
            uint32_t activeRgba  = RgbToRgbaOpaque(fgRgb);
            uint32_t dimRgba     = RgbToRgbaOpaque(LerpRgb(bgRgb, fgRgb, 0.32f));
            uint32_t defMarkRgba = RgbToRgbaOpaque(LerpRgb(bgRgb, fgRgb, 0.72f));
            int indY = divAbsY + (std::max)(0x16, 0x1d - (kPauseOptRowH - rH));
            DrawOptRowControlIndicator(items[row], valX, rightBound, indY, activeRgba, dimRgba, defMarkRgba);
        }
    };

    if (!scrollM.scrollable) {
        if (selRow >= 0 && selRow < nRows) {
            Game_DrawQuad(innerLeft, yBase + slideY + 0x76 + selRow * rowH, innerW, rowH, &rainbowRgba);
        }
        for (int row = 0; row < nRows; ++row) {
            drawPauseRow(row, yBase + slideY + 0x75 + row * rowH, rowH);
        }
        Game_DrawQuad(paneLeft, yBase + slideY + 0x75 + nRows * rowH, paneW, 1, dividerCol);

        if (isActivePane && IsMenuRenderInteractive() && selRow >= 0 && selRow < nRows) {
            Game_DrawRainbowRectBorder(
                frame, innerLeft, yBase + slideY + 0x76 + selRow * rowH, innerW, rowH, 2, 255
            );
        }
    } else {
        int scrollInnerW = scrollM.hasScrollbar ? (scrollM.sbX - 2 - innerLeft) : innerW;
        {
            ScopedUiScissor clip(innerLeft, scrollM.listTop, scrollInnerW, scrollM.viewH);
            if (!clip.empty) {
                ScopedUiMatrixTranslate scrollMat(0.0f, g_pauseOptScroll.renderY);
                if (selRow >= 0 && selRow < scrollM.nScrollRows) {
                    int hlY = scrollM.listTop + selRow * scrollM.stride + 1;
                    Game_DrawQuad(innerLeft, hlY, scrollInnerW, rowH, &rainbowRgba);
                }
                for (int row = 0; row < scrollM.nScrollRows; ++row) {
                    int divAbsY = scrollM.listTop + row * scrollM.stride;
                    float visTop = static_cast<float>(divAbsY) - g_pauseOptScroll.renderY;
                    float visBot = static_cast<float>(divAbsY + rowH) - g_pauseOptScroll.renderY;
                    if (visBot <= static_cast<float>(scrollM.listTop) || visTop >= static_cast<float>(scrollM.listBottom)) {
                        continue;
                    }
                    drawPauseRow(row, divAbsY, rowH);
                }
                if (isActivePane && IsMenuRenderInteractive() && selRow >= 0 && selRow < scrollM.nScrollRows) {
                    Game_DrawRainbowRectBorder(
                        frame, innerLeft, scrollM.listTop + selRow * scrollM.stride + 1, scrollInnerW, rowH, 2, 255
                    );
                }
            }
        }
        Game_DrawQuad(paneLeft, scrollM.listTop, paneW, 1, dividerCol);
        if (scrollM.hasScrollbar) {
            DrawOptScrollbar(scrollM, 100);
        }
        if (selRow == scrollM.backIdx) {
            Game_DrawQuad(innerLeft, scrollM.backTop + 1, innerW, scrollM.backH, &rainbowRgba);
        }
        drawPauseRow(scrollM.backIdx, scrollM.backTop, scrollM.backH);
        Game_DrawQuad(paneLeft, scrollM.backTop + scrollM.backH, paneW, 1, dividerCol);
        if (isActivePane && IsMenuRenderInteractive() && selRow == scrollM.backIdx) {
            Game_DrawRainbowRectBorder(
                frame, innerLeft, scrollM.backTop + 1, innerW, scrollM.backH, 2, 255
            );
        }
    }
}

static void __cdecl RenderPauseOptionsBody_Impl(uint8_t* ebpFrame, int slideY) {
    int xBase = *reinterpret_cast<int*>(ebpFrame - 0x48);
    int yBase = *reinterpret_cast<int*>(ebpFrame - 0x44);
    int frame = *reinterpret_cast<int*>(ebpFrame - 0x54);
    uint8_t* pauseMenu = *reinterpret_cast<uint8_t**>(ebpFrame - 0x58);
    int curSelRow = *reinterpret_cast<int*>(pauseMenu + 0x50);
    int scrollX = *reinterpret_cast<int*>(pauseMenu + 0x4c);
    void* mstack = *reinterpret_cast<void**>(ebpFrame - 0x50);

    int subStride = kMenuWideW;

    float subSwipeX = static_cast<float>(g_pauseSubSwipeX);
    if (g_pauseSubSwipeX != g_pauseSubSwipePrevX) {
        subSwipeX = static_cast<float>(g_pauseSubSwipePrevX)
            + static_cast<float>(g_pauseSubSwipeX - g_pauseSubSwipePrevX) * g_uiAlpha;
    }

    if (subSwipeX > 0.0f) {
        reinterpret_cast<void(__thiscall*)(void*)>(0x41f750)(mstack);
        float mat[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -subSwipeX, 0.0f, 0.0f, 1.0f
        };
        reinterpret_cast<void(__thiscall*)(void*, const float*)>(0x41f520)(mstack, mat);
    }

    int activeDepth = GetOptCategoryDepth(g_pauseOptCat);
    for (int d = 0; d < 3; ++d) {
        float paneStart = static_cast<float>(d * subStride);
        float paneEnd = static_cast<float>((d + 1) * subStride);
        if (subSwipeX < paneEnd && (subSwipeX + static_cast<float>(subStride)) > paneStart) {
            int paneSel = (activeDepth == d) ? curSelRow : g_pauseSubRow[d];
            DrawPauseOptionsPane(g_pauseSubCatAtDepth[d], paneSel, xBase, yBase, slideY, frame, d * subStride, activeDepth == d);
        }
    }
    if (subSwipeX > 0.0f) {
        reinterpret_cast<void(__thiscall*)(void*)>(0x41f610)(mstack);
    }

    if (g_optUi.popupOpen && g_optUi.popupInPause && g_optUi.popupRow >= 0) {
        int xOffset = activeDepth * subStride;
        int popNRows = GetOptRowCount(g_pauseOptCat, true);
        int paneLeft = xBase + kMenuWideW + xOffset;
        OptScrollMetrics popM{};
        ComputeOptScrollMetrics(true, g_pauseOptCat, popNRows, paneLeft, yBase + slideY, kMenuWideW, g_pauseOptScroll.renderY, &popM);

        int popRowH = popM.scrollable
            ? ((g_optUi.popupRow == popM.backIdx) ? popM.backH : popM.rowH)
            : ComputePauseOptRowH(yBase, popNRows);
        int popRightBound = (popM.scrollable ? popM.rightBound : (xBase + (2 * kMenuWideW) - 6 + xOffset))
            - static_cast<int>(std::lround(subSwipeX));
        int popRowTopY = popM.scrollable
            ? ((g_optUi.popupRow == popM.backIdx)
                ? popM.backTop
                : (popM.listTop + g_optUi.popupRow * popM.stride - static_cast<int>(std::lround(g_pauseOptScroll.renderY))))
            : (yBase + slideY + 0x75 + g_optUi.popupRow * popRowH);
        uint32_t rainbowRgb = GetSmoothMenuRainbow(frame + 3);
        DrawOptPopupOverlay(g_optUi.popupItem, popRightBound, popRowTopY, popRowH, true, scrollX, rainbowRgb);
    }
}

static void __declspec(naked) hk_renderPauseOptionsBody() {
    __asm {
        pushad
        push esi
        push ebp
        call RenderPauseOptionsBody_Impl
        add esp, 8
        popad
        mov ecx, DWORD PTR [ebp - 0x50]
        mov eax, 0x4adf59
        jmp eax
    }
}

// ------------------------------------------------------------------
// 4. Camera Angle, Vertigo FOV, Near/Far & Skybox Wrap (camera_angle.py)
// ------------------------------------------------------------------
static uint8_t* g_lastCamLevelPtr        = nullptr;
static bool     g_hasLastCamState        = false;
static float    g_lastTickPreAspectFov   = 22.0f;
static float    g_lastTickK              = 0.19438031f; // tan(11 deg)
static bool     g_fovTransActive         = false;
static uint8_t* g_fovTransSeg            = nullptr;
static float    g_fovTransStartFov       = 22.0f;
static float    g_fovTransStartK         = 0.19438031f;
static float    g_fovTransCamFovSetting  = -1.0f;
constexpr float kOrthoDistBoost          = 16.0f;

static bool CheckInLevel() {
    uint8_t* ws = GetWorldScreen();
    if (ws == nullptr || ws[0x2120] == 0) return false;
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr || *reinterpret_cast<uint32_t*>(sm + 0x50) < 0x11) return false;
    return true;
}

static float ComputeDeff(float aspect) {
    if (g_camDist > 0.0f) return g_camDist;
    float baseNum = aspect * 0.11558293f + 0.01733744f;
    if (baseNum < 0.19071183f) baseNum = 0.19071183f;
    if (baseNum > 0.26699657f) baseNum = 0.26699657f;
    float denom = (aspect > 1.0f ? aspect : 1.0f) * kTan11;
    return baseNum / denom;
}

static float MapFov(float inFov, bool is568) {
    if (inFov == 22.0f) {
        if (is568) {
            return (g_camFov == -2.0f) ? 35.0f : 22.0f;
        }
        if (g_camFov == -2.0f || g_camFov == 0.0f) {
            return 0.0f;
        }
        return std::clamp(g_camFov, 0.5f, 90.0f);
    }
    if (inFov <= 0.0f) return 0.0f;
    return std::clamp(inFov, 0.5f, 90.0f);
}

static float ComputeVanillaAspectFov(float rawFov, float aspect) {
    float f = rawFov;
    if (aspect > 1.0f && rawFov > 22.0f) {
        double top = 90.0 / static_cast<double>(aspect);
        f = static_cast<float>(22.0 + (top - 22.0) * (static_cast<double>(rawFov) - 22.0) / (90.0 - 22.0));
    }
    if (f < 1.0f) f = 1.0f;
    if (f > 90.0f) f = 90.0f;
    return f;
}

static double ComputeTargetK(float preAspectFov, float aspect, bool vertFlag) {
    if (g_camFov == -2.0f) {
        double tEff = std::tan(static_cast<double>(preAspectFov) * kPiOver360);
        if (aspect > 1.0f) tEff /= static_cast<double>(aspect);
        if (!vertFlag && preAspectFov > 1.0f) {
            double t10 = static_cast<double>(kTan10);
            if (aspect > 1.0f) t10 /= static_cast<double>(aspect);
            return (static_cast<double>(kTan11) / t10) * tEff;
        }
        return static_cast<double>(kTan11);
    }
    if (!vertFlag && preAspectFov > 1.0f) {
        return std::tan(static_cast<double>(preAspectFov) * kPiOver360);
    }
    return static_cast<double>(kTan11);
}

static void __cdecl OnScreenMgrSetFov_Impl(uint8_t* sm, uint32_t useDefault, float targetFov, int32_t duration, uint32_t vertigo) {
    (void)useDefault;
    (void)targetFov;
    (void)vertigo;
    uint8_t* lvl = GetLevel();
    if (duration > 0 && CheckInLevel() && g_hasLastCamState && lvl != nullptr && lvl == g_lastCamLevelPtr) {
        g_fovTransActive = true;
        g_fovTransSeg = sm + 0x78;
        g_fovTransStartFov = g_lastTickPreAspectFov;
        g_fovTransStartK = g_lastTickK;
        g_fovTransCamFovSetting = g_camFov;
    } else {
        g_fovTransActive = false;
        g_fovTransSeg = nullptr;
    }
}

static void __declspec(naked) hk_setFov() {
    __asm {
        push ebp
        mov ebp, esp
        pushad
        movzx eax, BYTE PTR [ebp + 0x14]
        push eax
        movsx eax, WORD PTR [ebp + 0x10]
        push eax
        push DWORD PTR [ebp + 0xc]
        movzx eax, BYTE PTR [ebp + 0x8]
        push eax
        push ecx
        call OnScreenMgrSetFov_Impl
        add esp, 20
        popad
        cmp BYTE PTR [ebp + 0x8], 0
        mov eax, 0x481bb7
        jmp eax
    }
}

static void __cdecl PerspScaleY_Impl(
    float* pOutMat, const uint8_t* cam, float aspect, float zn, float zf, float fovyRad
) {
    if (!CheckInLevel()) return;
    if (fovyRad <= kOrthoSentinelRadThreshold) {
        if (cam == nullptr || *reinterpret_cast<const void* const*>(cam + 0x304) != pOutMat || aspect <= 0.01f) {
            return;
        }
        double sumSq = 0.0;
        for (int i = 0; i < 3; ++i) {
            float diff = *reinterpret_cast<const float*>(cam + 0x108 + i * 4) -
                         *reinterpret_cast<const float*>(cam + 0x208 + i * 4);
            sumSq += static_cast<double>(diff) * static_cast<double>(diff);
        }
        float dist = static_cast<float>(std::sqrt(sumSq));
        if (dist > 1.0f) {
            float dEff = ComputeDeff(aspect);
            float halfHUnstretched = (dist / kOrthoDistBoost) * dEff * kTan11;
            float w = 2.0f * halfHUnstretched * aspect;
            float stretch = (g_camFov == -2.0f) ? (1.65f / 1.55f) : 1.0f;
            float h = (2.0f * halfHUnstretched) / stretch;
            Game_D3DXMatrixOrthoRH(pOutMat, w, h, zn, zf);
        }
        return;
    }
    if (g_camFov == -2.0f) {
        pOutMat[5] *= (1.65f / 1.55f);
    }
}

static void __declspec(naked) hk_perspScaleY() {
    __asm {
        push eax
        push DWORD PTR [ebp + 0x14]
        push DWORD PTR [ebp + 0x10]
        push DWORD PTR [ebp + 0xc]
        push DWORD PTR [ebp + 0x8]
        push esi
        push eax
        call PerspScaleY_Impl
        add esp, 24
        pop eax
        pop ebp
        ret 0x10
    }
}

static void __cdecl FrustumSetFov_Impl(float* frust, float fovyRad, float aspect) {
    if (fovyRad <= kOrthoSentinelRadThreshold && CheckInLevel()) {
        frust[0] = 100.0f;
    } else {
        frust[0] = static_cast<float>(std::tan(static_cast<double>(fovyRad) * 0.5));
    }
    frust[3] = aspect;
}

static void __declspec(naked) hk_frustumSetFov() {
    __asm {
        push DWORD PTR [esp + 8]
        push DWORD PTR [esp + 8]
        push ecx
        call FrustumSetFov_Impl
        add esp, 12
        ret 8
    }
}

static float __cdecl ComputeCamEffectiveTanHalfFov(const uint8_t* cam) {
    float fovyRad = *reinterpret_cast<const float*>(cam + 0x1c0);
    if (fovyRad <= kOrthoSentinelRadThreshold && CheckInLevel()) {
        float aspect = *reinterpret_cast<const float*>(cam + 0x1dc);
        return (ComputeDeff(aspect) * kTan11) / kOrthoDistBoost;
    }
    return static_cast<float>(std::tan(static_cast<double>(fovyRad) * 0.5));
}

static void __declspec(naked) hk_edgeTimeTanHalfFov() {
    __asm {
        fstp st(0)
        push eax
        push ecx
        push edx
        push edi
        call ComputeCamEffectiveTanHalfFov
        add esp, 4
        pop edx
        pop ecx
        pop eax
        ret
    }
}

static void __declspec(naked) hk_panTanHalfFov() {
    __asm {
        fstp st(0)
        push eax
        push ecx
        push edx
        push eax
        call ComputeCamEffectiveTanHalfFov
        add esp, 4
        pop edx
        pop ecx
        pop eax
        ret
    }
}

static int __cdecl CamFovVertigo_Impl(uint8_t* ebpFrame, float fovIn, float aspect) {
    if (!CheckInLevel()) {
        g_lastCamLevelPtr = nullptr;
        g_hasLastCamState = false;
        g_fovTransActive = false;
        g_fovTransSeg = nullptr;
        return 0; // run original
    }

    uint8_t* lvl = GetLevel();
    if (lvl != g_lastCamLevelPtr) {
        g_lastCamLevelPtr = lvl;
        g_hasLastCamState = false;
        g_fovTransActive = false;
        g_fovTransSeg = nullptr;
    }

    uint8_t* ws = GetWorldScreen();
    bool isWinProbe = (ws != nullptr && ws[0xb5] != 0 && ws[0xf0] == 0);
    uint8_t* seg = *reinterpret_cast<uint8_t**>(ebpFrame - 0xc);
    float sFov = *reinterpret_cast<float*>(seg + 0);
    float tFov = *reinterpret_cast<float*>(seg + 4);
    float curFov = *reinterpret_cast<float*>(ebpFrame - 0x8);
    bool vertFlag = (seg[0x10] != 0);

    if (ws != nullptr && ws[0xb5] != 0 && ws[0xf0] != 0 && seg == ws + 0xe0 && (!g_fovTransActive || g_fovTransSeg != seg)) {
        if (sFov != tFov && g_hasLastCamState) {
            g_fovTransActive = true;
            g_fovTransSeg = seg;
            g_fovTransStartFov = g_lastTickPreAspectFov;
            g_fovTransStartK = g_lastTickK;
            g_fovTransCamFovSetting = g_camFov;
        }
    }

    bool useTransStart = (sFov != tFov && g_fovTransActive && g_fovTransSeg == seg && g_fovTransCamFovSetting == g_camFov);
    float frac = 1.0f;
    if (sFov != tFov) {
        frac = std::clamp((curFov - sFov) / (tFov - sFov), 0.0f, 1.0f);
    }

    if (g_camFov == -1.0f) {
        double tEff = std::tan(static_cast<double>(fovIn) * kPiOver360);
        double endK = vertFlag
            ? static_cast<double>(kTan11)
            : std::tan(static_cast<double>(ComputeVanillaAspectFov(tFov, aspect)) * kPiOver360);
        double curK;
        if (useTransStart && std::fabs(static_cast<double>(g_fovTransStartK) - endK) > 1e-6) {
            curK = static_cast<double>(g_fovTransStartK)
                + (endK - static_cast<double>(g_fovTransStartK)) * static_cast<double>(frac);
        } else {
            curK = vertFlag ? static_cast<double>(kTan11) : tEff;
        }
        double vertMul = curK / tEff;
        *reinterpret_cast<float*>(ebpFrame - 0xc) = static_cast<float>(vertMul * static_cast<double>(ComputeDeff(aspect)));
        if (!g_inRenderCamInterp && !isWinProbe) {
            g_lastTickPreAspectFov = fovIn;
            g_lastTickK = static_cast<float>(curK);
            g_hasLastCamState = true;
        }
        return 1;
    }

    bool is568 = (lvl != nullptr && *reinterpret_cast<int32_t*>(lvl + 0x78) == 568);
    float mappedT = MapFov(tFov, is568);
    float preAspectFov = mappedT;
    float mappedS = mappedT;
    if (sFov != tFov) {
        mappedS = useTransStart ? g_fovTransStartFov : MapFov(sFov, is568);
        preAspectFov = mappedS + (mappedT - mappedS) * frac;
        if (preAspectFov < 0.0f) preAspectFov = 0.0f;
        if (preAspectFov > 90.0f) preAspectFov = 90.0f;
    }

    if (preAspectFov < 0.25f) {
        *reinterpret_cast<float*>(ebpFrame - 0x4) = kOrthoSentinelDeg;
        *reinterpret_cast<float*>(ebpFrame - 0xc) = kOrthoDistBoost;
        if (!g_inRenderCamInterp && !isWinProbe) {
            g_lastTickPreAspectFov = 0.0f;
            g_lastTickK = kTan11;
            g_hasLastCamState = true;
        }
        return 1;
    }

    double tEff = std::tan(static_cast<double>(preAspectFov) * kPiOver360);
    float fovOut = preAspectFov;
    if (g_camFov == -2.0f && aspect > 1.0f) {
        tEff /= static_cast<double>(aspect);
        fovOut = static_cast<float>(std::atan(tEff) * k360OverPi);
    }
    *reinterpret_cast<float*>(ebpFrame - 0x4) = fovOut;

    double endK = ComputeTargetK(mappedT, aspect, vertFlag);
    double curK = endK;
    if (sFov != tFov) {
        double startK = useTransStart
            ? static_cast<double>(g_fovTransStartK)
            : ComputeTargetK(mappedS, aspect, vertFlag);
        curK = startK + (endK - startK) * static_cast<double>(frac);
    }

    double vertMul = curK / tEff;
    *reinterpret_cast<float*>(ebpFrame - 0xc) = static_cast<float>(vertMul * static_cast<double>(ComputeDeff(aspect)));
    if (!g_inRenderCamInterp && !isWinProbe) {
        g_lastTickPreAspectFov = preAspectFov;
        g_lastTickK = static_cast<float>(curK);
        g_hasLastCamState = true;
    }
    return 1;
}

static void __declspec(naked) hk_camFovVertigo() {
    __asm {
        push ebx
        push esi
        push edi
        push DWORD PTR [ebp - 0x10]
        push DWORD PTR [ebp - 0x4]
        push ebp
        call CamFovVertigo_Impl
        add esp, 12
        pop edi
        pop esi
        pop ebx
        test eax, eax
        je cfv_orig
        mov eax, 0x47ba5f
        jmp eax
    cfv_orig:
        movss xmm0, DWORD PTR [ebp - 0x4]
        movss xmm3, DWORD PTR ds:[0x577c74]
        mov edx, DWORD PTR [ebp - 0xc]
        cmp BYTE PTR [edx + 0x10], 0
        mov eax, 0x47ba42
        jmp eax
    }
}

static void __declspec(naked) hk_saveActiveMul() {
    __asm {
        mov eax, DWORD PTR [ebp - 0xc]
        mov g_activeMul, eax
        mov eax, DWORD PTR ds:[0x5f9080]
        fld DWORD PTR [eax + 0x34]
        mov edx, 0x47ba67
        jmp edx
    }
}

static void __cdecl AdjustNearFar_Impl(const uint8_t* cam, float* pNear, float* pFar) {
    if (!CheckInLevel()) return;
    bool isOrtho = (*reinterpret_cast<const float*>(cam + 0x1c0) <= kOrthoSentinelRadThreshold);
    if (!isOrtho && g_activeMul <= 1.0f) return;
    double sumSq = 0.0;
    for (int i = 0; i < 3; ++i) {
        float diff = *reinterpret_cast<const float*>(cam + 0x108 + i * 4) -
                     *reinterpret_cast<const float*>(cam + 0x208 + i * 4);
        sumSq += static_cast<double>(diff) * static_cast<double>(diff);
    }
    float dist = static_cast<float>(std::sqrt(sumSq));
    if (isOrtho) {
        *pNear = (dist > 8010.0f) ? (dist - 8000.0f) : 10.0f;
        *pFar  = dist + 8000.0f;
        return;
    }
    float invMul = dist / g_activeMul;
    float newNear = dist * 0.80f - invMul;
    if (newNear > *pNear) *pNear = newNear;
    *pFar = (*pFar / g_activeMul) + dist;
}

static void __declspec(naked) hk_adjustNearFar() {
    __asm {
        // Replaces call 0x423840 (which is ret 8: pops [esp+4]=near, [esp+8]=far)
        push ecx
        push edx
        lea eax, [esp + 16] // far ptr
        push eax
        lea eax, [esp + 16] // near ptr
        push eax
        push esi
        call AdjustNearFar_Impl
        add esp, 12
        pop edx
        pop ecx
        mov ecx, esi
        mov eax, 0x423840
        jmp eax
    }
}

static void __cdecl SkyboxProjWrap_Impl(const uint8_t* renderer, void (__cdecl* origRender)(const uint8_t*)) {
    if (*reinterpret_cast<const uint32_t*>(renderer + 0x58) == 0 || !CheckInLevel()) {
        origRender(renderer);
        return;
    }
    uint8_t* cam = *reinterpret_cast<uint8_t* const*>(renderer + 0x48);
    if (cam == nullptr || *reinterpret_cast<uint32_t*>(cam + 0x2b0) != 1) {
        origRender(renderer);
        return;
    }
    float fovy = *reinterpret_cast<float*>(cam + 0x1c0);
    float zn   = *reinterpret_cast<float*>(cam + 0x1c4);
    float zf   = *reinterpret_cast<float*>(cam + 0x1c8);
    if (fovy >= kRad22 && zn <= 500.0f && zf >= 2000.0f) {
        origRender(renderer);
        return;
    }

    float fovySky = (fovy < kRad22) ? kRad22 : fovy;
    float aspect = *reinterpret_cast<float*>(cam + 0x1dc);
    float mSky[16];
    float savedFrustum[4];
    std::memcpy(savedFrustum, cam + 0x30c, 16);

    Game_D3DXMatrixPerspectiveFovRH(mSky, fovySky, aspect, 100.0f, 8000.0f);
    Game_SetProjectionTransform(mSky);
    *reinterpret_cast<float*>(cam + 0x30c) = 1.0f;
    *reinterpret_cast<float*>(cam + 0x310) = 8000.0f;
    *reinterpret_cast<float*>(cam + 0x314) = 100.0f;

    origRender(renderer);

    std::memcpy(cam + 0x30c, savedFrustum, 16);
    void* origProjMat = *reinterpret_cast<void**>(cam + 0x304);
    Game_SetProjectionTransform(origProjMat);
}

static void __cdecl CallOrigRenderSkybox(const uint8_t* r) {
    reinterpret_cast<void(__thiscall*)(const void*)>(0x4075f0)(r);
}

static void __declspec(naked) hk_skyboxProjWrap() {
    __asm {
        push OFFSET CallOrigRenderSkybox
        push ecx
        call SkyboxProjWrap_Impl
        add esp, 8
        ret
    }
}

// ------------------------------------------------------------------
// 5. Time Format hooks (time_format.py)
// ------------------------------------------------------------------
static const char kFmtLvlModern[] = "%d:%.2d.%.2d";
static const char kFmtEdgeNeg[]   = "-%d.%.2d";
static const char kFmtEdgePos[]   = "%d.%.2d";

static const char* __cdecl PickLevelFmt() {
    return g_timeFmt ? kFmtLvlModern : reinterpret_cast<const char*>(0x58cc58);
}

static void __declspec(naked) hk_timeFmtPush() {
    __asm {
        push eax
        push ecx
        push edx
        call PickLevelFmt
        pop edx
        pop ecx
        xchg DWORD PTR [esp + 4], eax
        xchg DWORD PTR [esp], eax
        ret
    }
}

static void __declspec(naked) hk_fmtSelEdge() {
    __asm {
        mov ecx, 0x58cc70
        mov edx, 0x58cc80
        cmp BYTE PTR g_timeFmt, 0
        je fse_go
        mov ecx, OFFSET kFmtEdgeNeg
        mov edx, OFFSET kFmtEdgePos
    fse_go:
        cmp DWORD PTR [ebp - 0xfc], 0
        jle fse_out
        mov ecx, edx
    fse_out:
        ret
    }
}

static void __declspec(naked) hk_sepScaleYPush() {
    __asm {
        push eax
        mov eax, 0x1000
        cmp BYTE PTR g_timeFmt, 0
        je ssy_go
        mov eax, 0x2000
    ssy_go:
        xchg DWORD PTR [esp + 4], eax
        xchg DWORD PTR [esp], eax
        ret
    }
}

static const EdgeWString* __cdecl PickSep2Wstr() {
    return g_timeFmt ? &g_wstrDot : reinterpret_cast<const EdgeWString*>(0x58d0d8);
}

static void __declspec(naked) hk_sep2WstrPush() {
    __asm {
        push eax
        push ecx
        push edx
        call PickSep2Wstr
        pop edx
        pop ecx
        xchg DWORD PTR [esp + 4], eax
        xchg DWORD PTR [esp], eax
        ret
    }
}

// ------------------------------------------------------------------
// 6. Skip Intro hooks (skip_intro.py)
// ------------------------------------------------------------------
static void __cdecl InitSkipIntro_Impl(uint8_t* ms) {
    if (!g_skipIntro) return;
    constexpr float kFrame253 = 253.0f;
    *reinterpret_cast<float*>(ms + 0x164) = kFrame253;
    ms[0x11c] = 1; // silence logo_splash SFX
    uint8_t* node0 = *reinterpret_cast<uint8_t**>(ms + 0x120);
    if (node0 != nullptr) *reinterpret_cast<float*>(node0 + 0x20) = kFrame253;
    uint8_t* node1 = *reinterpret_cast<uint8_t**>(ms + 0x128);
    if (node1 != nullptr) *reinterpret_cast<float*>(node1 + 0x20) = kFrame253;
}

static void __declspec(naked) hk_initSkipIntro() {
    __asm {
        push ecx
        push esi
        call InitSkipIntro_Impl
        add esp, 4
        pop ecx
        mov eax, 0x421ff0
        jmp eax
    }
}

static void __declspec(naked) hk_getCamIntroT() {
    __asm {
        movss xmm0, DWORD PTR [esi + 0x164]
        cmp BYTE PTR g_skipIntro, 0
        je gcit_ret
        movss xmm0, DWORD PTR [esi + 0x158]
    gcit_ret:
        ret
    }
}

static void __declspec(naked) hk_getCamIntroT1() {
    __asm {
        movss xmm1, DWORD PTR [esi + 0x164]
        cmp BYTE PTR g_skipIntro, 0
        je gcit1_ret
        movss xmm1, DWORD PTR [esi + 0x158]
    gcit1_ret:
        ret
    }
}

static void __declspec(naked) hk_evalSplineCurve() {
    __asm {
        cmp BYTE PTR g_skipIntro, 0
        je esc_go
        test ecx, ecx
        je esc_go
        mov eax, DWORD PTR [ecx + 0x30]
        test eax, eax
        je esc_go
        cmp DWORD PTR [eax], 0x3fc00000 // 1.5f
        jne esc_go
        mov DWORD PTR [esp + 4], 0x43a00000 // 320.0f
    esc_go:
        mov eax, 0x4392e0
        jmp eax
    }
}

// Helper to emit a SkipIntro intro gate (`0x49c26b`, `0x49c30f`, `0x49c884`, `0x49c97d`) referencing &g_skipIntro
static void RegIntroGate(uint32_t siteVa, uint8_t rmModRm, uint32_t runVa, uint32_t skipVa) {
    // Original 37B:
    // +0: 80 b8/be 5c 01 00 00 00 (7B) cmp byte ptr [reg+0x15c], 0
    // +7: 75 rel8 (2B -> skipVa)
    // +9: f3 0f 10 80/86 64 01 00 00 (8B)
    // +17: f3 0f 10 88/8e 58 01 00 00 (8B)
    // +25: 0f 5a c0 0f 5a c9 (6B)
    // +31: 66 0f 2f c1 (4B)
    // +35: 76 rel8 (2B -> skipVa)
    uint8_t exp[37] = {
        0x80, rmModRm, 0x5c, 0x01, 0x00, 0x00, 0x00,
        0x75, static_cast<uint8_t>(skipVa - (siteVa + 9)),
        0xf3, 0x0f, 0x10, static_cast<uint8_t>(rmModRm == 0xb9 ? 0x81 : 0x86), 0x64, 0x01, 0x00, 0x00,
        0xf3, 0x0f, 0x10, static_cast<uint8_t>(rmModRm == 0xb9 ? 0x89 : 0x8e), 0x58, 0x01, 0x00, 0x00,
        0x0f, 0x5a, 0xc0, 0x0f, 0x5a, 0xc9,
        0x66, 0x0f, 0x2f, 0xc1,
        0x76, static_cast<uint8_t>(skipVa - (siteVa + 37))
    };
    uint32_t gAddr = reinterpret_cast<uint32_t>(&g_skipIntro);
    // Replacement (35B + 2 NOPs = 37B):
    // +0: 80 3d <addr> 00 (7B)
    // +7: 75 rel8 (2B -> runVa)
    // +9: 80 rm 5c 01 00 00 00 (7B)
    // +16: 75 rel8 (2B -> skipVa)
    // +18: f3 0f 10 81/86 64 01 00 00 (8B)
    // +26: 0f 2e 81/86 58 01 00 00 (7B) ucomiss xmm0, [reg+0x158]
    // +33: 76 rel8 (2B -> skipVa)
    // +35: 90 90
    uint8_t rep[37] = {
        0x80, 0x3d, 0, 0, 0, 0, 0x00,
        0x75, static_cast<uint8_t>(runVa - (siteVa + 9)),
        0x80, rmModRm, 0x5c, 0x01, 0x00, 0x00, 0x00,
        0x75, static_cast<uint8_t>(skipVa - (siteVa + 18)),
        0xf3, 0x0f, 0x10, static_cast<uint8_t>(rmModRm == 0xb9 ? 0x81 : 0x86), 0x64, 0x01, 0x00, 0x00,
        0x0f, 0x2e, static_cast<uint8_t>(rmModRm == 0xb9 ? 0x81 : 0x86), 0x58, 0x01, 0x00, 0x00,
        0x76, static_cast<uint8_t>(skipVa - (siteVa + 35)),
        0x90, 0x90
    };
    std::memcpy(&rep[2], &gAddr, 4);
    RegBytes(siteVa, exp, rep, 37);
}

// ------------------------------------------------------------------
// 7. Music Start hook (Cube::update 0x490302: bypass cube->state==3 wait when "fall")
// ------------------------------------------------------------------
static void __declspec(naked) hk_musicStartGate() {
    __asm {
        cmp BYTE PTR g_musicStart, 0
        jne msg_go
        cmp eax, 3
        je msg_skip
    msg_go:
        mov eax, 0x490307
        jmp eax
    msg_skip:
        mov eax, 0x49034b
        jmp eax
    }
}

// ------------------------------------------------------------------
// 8. Reset Score helper (used by in-place Reset Score confirm popup)
// ------------------------------------------------------------------
static void ResetAllScores_Impl() {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
    uint8_t* saveData = sm + 0x298;
    // Clear std::map<int, LevelSaveInfo> values (completed, time, prismCount, rank) at saveData+0x4
    uint8_t* head = *reinterpret_cast<uint8_t**>(saveData + 0x8);
    if (head != nullptr) {
        uint8_t* node = *reinterpret_cast<uint8_t**>(head + 0x0); // leftmost node
        while (node != nullptr && node != head && node[0x21] == 0) {
            std::memset(node + 0x10, 0, 16);
            uint8_t* r = *reinterpret_cast<uint8_t**>(node + 0x8);
            if (r[0x21] == 0) {
                node = r;
                while ((*reinterpret_cast<uint8_t**>(node + 0x0))[0x21] == 0) {
                    node = *reinterpret_cast<uint8_t**>(node + 0x0);
                }
            } else {
                uint8_t* p = *reinterpret_cast<uint8_t**>(node + 0x4);
                while (p[0x21] == 0 && node == *reinterpret_cast<uint8_t**>(p + 0x8)) {
                    node = p;
                    p = *reinterpret_cast<uint8_t**>(p + 0x4);
                }
                node = p;
            }
        }
    }
    *reinterpret_cast<int32_t*>(saveData + 0x2c) = 0; // lastPlayedLevelType
    *reinterpret_cast<int32_t*>(saveData + 0x30) = 0; // lastPlayedLevelIndex[standard]
    *reinterpret_cast<int32_t*>(saveData + 0x34) = 0; // lastPlayedLevelIndex[extended]
    *reinterpret_cast<int32_t*>(saveData + 0x38) = 0; // lastPlayedLevelIndex[bonus]
    Game_SaveUserSettings(saveData);
}

static int __cdecl MainMenuRowStrId_Impl(int row) {
    static constexpr int kRowStrIds[6] = {0x4, 0x9, 0x8, 0x5, 0x3, 0x1a};
    return (row >= 0 && row < 6) ? kRowStrIds[row] : 0x1b;
}

static void __declspec(naked) hk_mainMenuGetRowStrId() {
    __asm {
        push DWORD PTR [esp + 4]
        call MainMenuRowStrId_Impl
        add esp, 4
        ret
    }
}

static void __declspec(naked) hk_mainMenuRenderRowStrId() {
    __asm {
        push ecx
        push edx
        push ebx
        call MainMenuRowStrId_Impl
        add esp, 4
        pop edx
        pop ecx
        push 0x4b9f1c
        ret
    }
}

static void __cdecl MainMenuAction_Impl(uint8_t* mainMenu) {
    if (mainMenu == nullptr) return;
    int row = *reinterpret_cast<int*>(mainMenu + 0x10);
    static constexpr int kDest[6] = {3, 0xb, 9, 8, 0xa, 0xf};
    if (row < 0 || row >= 6) return;
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        Game_PlaySfx(sm, 0xc);
    }
    reinterpret_cast<void(__thiscall*)(void*, int)>(0x495f10)(mainMenu, kDest[row]);
}

static void __declspec(naked) hk_mainMenuAction() {
    __asm {
        push ecx
        call MainMenuAction_Impl
        add esp, 4
        ret
    }
}

static int __cdecl MainMenuFocusRow_Impl(uint32_t prevScr) {
    switch (prevScr) {
    case 0xb: return 1; // scores
    case 0x9: return 2; // options
    case 0x8: return 3; // instructions
    case 0xa: return 4; // credits
    case 0xf: return 5; // exit game
    default:  return 0; // edge challenge
    }
}

static void __declspec(naked) hk_mainMenuFocus() {
    __asm {
        push ecx
        push edx
        push eax
        call MainMenuFocusRow_Impl
        add esp, 4
        mov DWORD PTR [esi + 0x10], eax
        pop edx
        pop ecx
        mov eax, 0x4ba30e
        jmp eax
    }
}

static int __cdecl MenuListDrawRowText_Impl(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int extraX, int extraY, int alignFlags, int hasSub
) {
    int maxW = (hasSub != 0) ? 308 : 330;
    int res = DrawTextAutoFit(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags, maxW);
    if (hasSub != 0) {
        int menuW = *reinterpret_cast<const int*>(0x589ddc);
        int arrowX = (x - 0x1e) + menuW - 14;
        Game_DrawText(&g_wstrSubmenuArrow, arrowX, y, scaleX, scaleY, extraX, extraY, (alignFlags & ~0x0d) | 0x08);
    }
    return res;
}

static int __cdecl MainMenuDrawRowText_Impl(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int extraX, int extraY, int alignFlags, int row
) {
    int hasSub = (row == 0 || row == 1 || row == 2) ? 1 : 0; // edge challenge, scores, options
    return MenuListDrawRowText_Impl(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags, hasSub);
}

static int __cdecl ModeSelectDrawRowText_Impl(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int extraX, int extraY, int alignFlags, int row
) {
    int hasSub = (row == 1 || row == 2 || row == 3) ? 1 : 0; // normal, extended, bonus levels
    return MenuListDrawRowText_Impl(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags, hasSub);
}

static int __cdecl ScoresSelectDrawRowText_Impl(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int extraX, int extraY, int alignFlags, int row
) {
    int hasSub = (row == 0 || row == 1 || row == 2) ? 1 : 0; // normal, extended, bonus scores
    return MenuListDrawRowText_Impl(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags, hasSub);
}

static void __declspec(naked) hk_mainMenuDrawRowText() {
    __asm {
        push ebx
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        call MainMenuDrawRowText_Impl
        add esp, 36
        ret
    }
}

static void __declspec(naked) hk_modeSelectDrawRowText() {
    __asm {
        push ebx
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        call ModeSelectDrawRowText_Impl
        add esp, 36
        ret
    }
}

static void __declspec(naked) hk_scoresSelectDrawRowText() {
    __asm {
        push esi
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        call ScoresSelectDrawRowText_Impl
        add esp, 36
        ret
    }
}

static int __cdecl PauseP0DrawRowText_Impl(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int extraX, int extraY, int alignFlags, int row, int xBase
) {
    bool hasSub = (row == 3); // options (Pause P0 jtbl 0x4ae430: 0=resume, 1=last checkpoint, 2=retry, 3=options, 4=main menu)
    int maxW = hasSub ? ((xBase + kMenuWideW - 28) - x) : 336;
    int res = DrawTextAutoFit(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags, maxW);
    if (hasSub) {
        int arrowX = xBase + kMenuWideW - 12;
        Game_DrawText(&g_wstrSubmenuArrow, arrowX, y, scaleX, scaleY, extraX, extraY, (alignFlags & ~0x0d) | 0x08);
    }
    return res;
}

static void __declspec(naked) hk_pauseP0DrawRowText() {
    __asm {
        push DWORD PTR [ebp - 0x48]
        push edi
        push DWORD PTR [esp + 40]
        push DWORD PTR [esp + 40]
        push DWORD PTR [esp + 40]
        push DWORD PTR [esp + 40]
        push DWORD PTR [esp + 40]
        push DWORD PTR [esp + 40]
        push DWORD PTR [esp + 40]
        push DWORD PTR [esp + 40]
        call PauseP0DrawRowText_Impl
        add esp, 40
        ret
    }
}

template <int MaxW>
static int __cdecl DrawTextAutoFitConst(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int extraX, int extraY, int alignFlags
) {
    return DrawTextAutoFit(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags, MaxW);
}

static void __declspec(naked) hk_drawButtonText() {
    __asm {
        lea eax, [edi - 10]
        push eax
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        push DWORD PTR [esp + 36]
        call DrawTextAutoFit
        add esp, 36
        ret
    }
}

static uint32_t __cdecl ModeSelectRow0StrId_Impl() {
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        void* pm = sm + 0x2e8;
        using HasAnyFn = uint8_t(__thiscall*)(void*, int);
        auto fn = reinterpret_cast<HasAnyFn>(0x494450);
        if (fn(pm, 0) != 0 || fn(pm, 1) != 0 || fn(pm, 2) != 0) {
            return 2; // "continue"
        }
    }
    return 7; // "new game"
}

static void __declspec(naked) hk_modeSelectRow0StrId() {
    // 0x4b8b02..0x4b8b14 (ModeSelect::getRowStringId case 0):
    // Function prologue only pushed ebp; return 2 ("continue") if any level completed else 7 ("new game").
    __asm {
        call ModeSelectRow0StrId_Impl
        pop ebp
        ret 4
    }
}

static uint8_t __stdcall LevelSelectTileFlag_Impl(int mode, int idx) {
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        uint8_t* lvlInfo = reinterpret_cast<uint8_t*(__thiscall*)(void*, int, int)>(0x4a4620)(sm + 0x24c, mode, idx);
        if (lvlInfo != nullptr) {
            int32_t levelId = *reinterpret_cast<int32_t*>(lvlInfo + 0x3c);
            uint8_t* saveInfo = reinterpret_cast<uint8_t*(__thiscall*)(void*, int32_t)>(0x4920e0)(sm + 0x298, levelId);
            if (saveInfo != nullptr && saveInfo[0] != 0) {
                return 2; // unlocked & completed (raised white tile)
            }
        }
    }
    return 1; // unlocked & not played (raised white/#BFBFBF diagonal tile when unselected)
}

static void __cdecl LsTileInnerQuad_Impl(
    uint8_t* ls, int32_t tileIdx, const uint8_t* ebpFrame,
    int32_t innerX, int32_t innerY, int32_t w, int32_t h, const uint8_t* innerRgba
) {
    uint8_t drawRgba[4] = {innerRgba[0], innerRgba[1], innerRgba[2], innerRgba[3]};
    bool isHovered = false;
    uint32_t hoverRainbowRgb = 0xffffffu;

    const bool isFocusedHover = IsMenuInputKeyboardMode()
        ? (tileIdx == *reinterpret_cast<const int32_t*>(ls + 0x38))
        : (tileIdx == GetLevelSelectHoveredTile());
    if (ls != nullptr &&
        *reinterpret_cast<const int32_t*>(ls + 4) >= 8 &&
        *reinterpret_cast<const int32_t*>(ls + 8) < 0 &&
        isFocusedHover) {
        isHovered = true;
        int32_t frame = *reinterpret_cast<const int32_t*>(ebpFrame - 0x54);
        hoverRainbowRgb = GetSmoothMenuRainbow(frame);
        if (drawRgba[0] == 0xffu && drawRgba[1] == 0xffu && drawRgba[2] == 0xffu) {
            uint32_t subtleRgb = Game_ColorLerp(0xffffffu, hoverRainbowRgb, 25);
            drawRgba[0] = static_cast<uint8_t>((subtleRgb >> 16) & 0xffu);
            drawRgba[1] = static_cast<uint8_t>((subtleRgb >> 8) & 0xffu);
            drawRgba[2] = static_cast<uint8_t>(subtleRgb & 0xffu);
        }
    }

    Game_DrawQuad(innerX, innerY, w, h, drawRgba);

    if (isHovered) {
        int32_t frame = *reinterpret_cast<const int32_t*>(ebpFrame - 0x54);
        Game_DrawRainbowRectBorder(frame, innerX - 2, innerY - 2, w + 4, h + 4, 2, 255);
    }

    if (ls == nullptr || tileIdx < 0 || tileIdx >= *reinterpret_cast<int32_t*>(ls + 0x18)) return;
    const uint8_t* tileFlags = *reinterpret_cast<const uint8_t* const*>(ls + 0x4c);
    if (tileFlags == nullptr || tileFlags[tileIdx * 2] != 1) return;

    // Unlocked & not played: draw lower-right diagonal #BFBFBF triangle inside the 30x30 inner square.
    // [ebp-0x74] holds #BFBFBF already faded by LevelSelect::render's enter/exit fadePct (esi).
    uint32_t triRgb = *reinterpret_cast<const uint32_t*>(ebpFrame - 0x74);

    // If this tile is in its post-selection rainbow trail fade ([ls+8] < 0 and innerRgba != white),
    // blend triRgb toward flashColors[tileIdx] at the same ratio as innerRgba for a seamless transition.
    if (*reinterpret_cast<int32_t*>(ls + 0x8) < 0 &&
        (innerRgba[0] != 0xffu || innerRgba[1] != 0xffu || innerRgba[2] != 0xffu)) {
        const uint32_t* colors = *reinterpret_cast<const uint32_t* const*>(ls + 0x30);
        if (colors != nullptr) {
            uint32_t flashCol = colors[tileIdx];
            int fr = static_cast<int>((flashCol >> 16) & 0xffu);
            int fg = static_cast<int>((flashCol >> 8) & 0xffu);
            int fb = static_cast<int>(flashCol & 0xffu);
            int dr = 255 - fr, dg = 255 - fg, db = 255 - fb;
            int maxD = dr, curD = 255 - static_cast<int>(innerRgba[0]);
            if (dg > maxD) { maxD = dg; curD = 255 - static_cast<int>(innerRgba[1]); }
            if (db > maxD) { maxD = db; curD = 255 - static_cast<int>(innerRgba[2]); }
            if (maxD > 0 && curD > 0) {
                int pct = (curD * 100 + maxD / 2) / maxD;
                if (pct > 100) pct = 100;
                triRgb = Game_ColorLerp(triRgb, flashCol, pct);
            }
        }
    } else if (isHovered) {
        triRgb = Game_ColorLerp(triRgb, hoverRainbowRgb, 25);
    }

    uint8_t triRgba[4] = {
        static_cast<uint8_t>((triRgb >> 16) & 0xffu),
        static_cast<uint8_t>((triRgb >> 8) & 0xffu),
        static_cast<uint8_t>(triRgb & 0xffu),
        0xffu
    };

    float xL = static_cast<float>(innerX);
    float xR = static_cast<float>(innerX + w);
    float yT = static_cast<float>(innerY);
    float yB = static_cast<float>(innerY + h);
    Game_DrawTriangle(xR, yT, xR, yB, xL, yB, triRgba, triRgba, triRgba);
}

static void __declspec(naked) hk_lsTileInnerQuad() {
    __asm {
        // Entry stack: [esp+0]=ret (0x4b8252), [esp+4]=innerX, [esp+8]=innerY,
        //              [esp+12]=w, [esp+16]=h, [esp+20]=innerRgba.
        // Each push shifts esp down by 4, so [esp+20] walks innerRgba -> h -> w -> innerY -> innerX.
        push DWORD PTR [esp + 20]
        push DWORD PTR [esp + 20]
        push DWORD PTR [esp + 20]
        push DWORD PTR [esp + 20]
        push DWORD PTR [esp + 20]
        push ebp
        push ebx
        push edi
        call LsTileInnerQuad_Impl
        add esp, 32
        ret
    }
}

static void __declspec(naked) hk_lsScrollTotalH() {
    __asm {
        add ecx, 5
        mov eax, ecx
        cdq
        mov edi, 6
        idiv edi
        imul edi, eax, 0x2d
        mov eax, 0x4b61ff
        jmp eax
    }
}

static void __declspec(naked) hk_lsScrollSelY() {
    __asm {
        mov eax, DWORD PTR [esi + 0x38]
        cdq
        mov ecx, 6
        idiv ecx
        mov ecx, eax
        imul eax, eax, 0x2d
        mov edx, 0x4b621d
        jmp edx
    }
}

static void __declspec(naked) hk_lsScrollbarMaxH() {
    __asm {
        add ecx, 5
        mov eax, ecx
        cdq
        mov edi, 6
        idiv edi
        imul edi, eax, 0x2d
        mov eax, 0x4b6498
        jmp eax
    }
}

static void __declspec(naked) hk_lsHitTestTileXY() {
    __asm {
        mov eax, esi
        cdq
        mov ecx, 6
        idiv ecx
        imul eax, eax, 0x2d
        sub eax, DWORD PTR [edi + 0x50]
        mov ecx, DWORD PTR [ebp - 0x4]
        lea ecx, [eax + ecx + 0xac]
        imul edx, edx, 0x2d
        mov eax, DWORD PTR [ebp - 0x8]
        lea eax, [edx + eax + 0x38]
        mov edx, ecx
        mov ecx, 0x4b66a2
        jmp ecx
    }
}

static void __declspec(naked) hk_lsRenderTileXY() {
    __asm {
        push edi
        call GetSmoothLevelSelectScrollY
        add esp, 4
        push eax
        mov eax, ebx
        cdq
        mov ecx, 6
        idiv ecx
        pop ecx
        imul eax, eax, 0x2d
        sub eax, ecx
        mov ecx, DWORD PTR [ebp - 0x48]
        lea eax, [eax + ecx + 0xaa]
        imul edx, edx, 0x2d
        mov ecx, DWORD PTR [ebp - 0x4c]
        lea ecx, [edx + ecx + 0x36]
        mov edx, 0x4b80c1
        jmp edx
    }
}

static void __declspec(naked) hk_lsPrismDotY() {
    __asm {
        mov eax, DWORD PTR [edi + 0x3c]
        movsx eax, WORD PTR [eax + 0x70]
        cmp eax, 10
        jle single_row
        lea eax, [ecx + edx + 0x6d]
        jmp done
    single_row:
        lea eax, [ecx + edx + 0x75]
    done:
        mov ecx, DWORD PTR [ebp - 0x4c]
        mov edx, 0x4b874a
        jmp edx
    }
}

static int32_t __cdecl GetPauseHeaderCoverH() {
    return 0x53 + GetPauseContentShiftY();
}

static void __declspec(naked) hk_pauseHdrResetSlideY() {
    __asm {
        call GetPauseHeaderCoverH
        lea edx, [ebp - 0x40]
        push edx
        push eax
        xor esi, esi
        mov ecx, DWORD PTR [ebp - 0x44]
        mov edx, DWORD PTR [ebp - 0x48]
        mov eax, 0x4ae267
        jmp eax
    }
}

static void __declspec(naked) hk_pauseP0HitY() {
    __asm {
        push 0x4ace62
        push eax
        push ecx
        push edx
        call GetPauseContentShiftY
        lea esi, [eax + 0x55]
        pop edx
        pop ecx
        pop eax
        ret
    }
}

static void __declspec(naked) hk_pauseP2HitY() {
    __asm {
        push 0x4ac88a
        push eax
        push ecx
        push edx
        call GetPauseContentShiftY
        pop edx
        lea edx, [edx + eax + 0x81]
        pop ecx
        pop eax
        ret
    }
}

static void __cdecl RenderPauseHeaderTimerAndPrisms_Impl(uint8_t* ebpFrame) {
    if (ebpFrame == nullptr) return;
    const uint8_t* ws        = *reinterpret_cast<const uint8_t* const*>(ebpFrame - 0x64);
    const uint8_t* level     = *reinterpret_cast<const uint8_t* const*>(ebpFrame - 0x5c);
    const uint8_t* pauseMenu = *reinterpret_cast<const uint8_t* const*>(ebpFrame - 0x58);
    int32_t xBase = *reinterpret_cast<const int32_t*>(ebpFrame - 0x48);
    int32_t yBase = *reinterpret_cast<const int32_t*>(ebpFrame - 0x44);
    int32_t frame = *reinterpret_cast<const int32_t*>(ebpFrame - 0x54);

    EdgeWString pauseWs;
    Game_GetLocalizedWString(&pauseWs, 0x15);
    int pauseW = MeasureTextWidth(&pauseWs, 0x3000, 0);
    pauseWs.free_if_heap();

    int lvlW = 0;
    if (pauseMenu != nullptr) {
        const auto* lvlWs = reinterpret_cast<const EdgeWString*>(pauseMenu + 0x24);
        lvlW = MeasureTextWidth(lvlWs, 0x8000, 0);
    }

    constexpr int kMaxPrismGridW = 10 * 12 + 9 * 6; // 174
    int leftRight = xBase + 2 + (std::max)(pauseW, lvlW);
    int rightEdge = xBase + 362;
    int rightLeft = std::clamp(leftRight + 14, rightEdge - kMaxPrismGridW, rightEdge - 140);
    int timerW = rightEdge - rightLeft;

    if (ws == nullptr || level == nullptr) {
        RenderPauseHeaderTimer(rightLeft, yBase + 18, timerW);
        return;
    }

    int32_t collected = *reinterpret_cast<const int32_t*>(ws + 0x11c);
    int32_t total = *reinterpret_cast<const int16_t*>(level + 0x7c);
    if (total <= 0) {
        RenderPauseHeaderTimer(rightLeft, yBase + 18, timerW);
        return;
    }

    if (total <= 20) {
        const bool twoRows = (total > 10);
        RenderPauseHeaderTimer(rightLeft, yBase + (twoRows ? 6 : 8), timerW);

        int cols = twoRows ? 10 : total;
        int gridW = cols * 12 + (cols - 1) * 6;
        int gridLeft = rightEdge - gridW;
        int yTop = twoRows ? (yBase + 56) : (yBase + 60);
        for (int i = 0; i < total; ++i) {
            int r = i / 10;
            int c = i % 10;
            uint32_t rgb = (i < collected) ? GetSmoothMenuRainbow(frame - i) : 0x7d7d7du;
            uint32_t rgba = RgbToRgbaOpaque(rgb);
            Game_DrawQuad(gridLeft + c * 18, yTop + r * 16, 12, 12, &rgba);
        }
        return;
    }

    RenderPauseHeaderTimer(rightLeft, yBase + 8, timerW);

    wchar_t wbuf[32];
    EdgeWString wsTxt;
    edgemod::FormatPrismRatioWstr(collected, total, wbuf, 32, &wsTxt);
    int tw = MeasureTextWidth(&wsTxt, 0x2000, 1);

    int centerY = yBase + 65;

    uint32_t* pFontCol = reinterpret_cast<uint32_t*>(0x5f4268);
    uint32_t savedCol = *pFontCol;

    uint32_t rainbowRgb = GetSmoothMenuRainbow(frame);
    uint32_t iconRgba = RgbToRgbaOpaque((collected > 0) ? rainbowRgb : 0x7d7d7du);
    Game_DrawQuad(rightEdge - tw - 18, centerY - 6, 12, 12, &iconRgba);

    *pFontCol = RgbToRgbaOpaque((collected >= total) ? rainbowRgb : 0xffffffu);
    Game_DrawText(&wsTxt, rightEdge, centerY, 0x2000, 0x2000, 1, 0, 0x0a);

    *pFontCol = savedCol;
}

static void __declspec(naked) hk_pausePrismGridOrCount() {
    __asm {
        mov DWORD PTR [ebp - 0x5c], eax
        pushad
        push ebp
        call RenderPauseHeaderTimerAndPrisms_Impl
        add esp, 4
        popad
        mov eax, 0x4ae412
        jmp eax
    }
}

static void __cdecl RenderLevelSelectPrismCount_Impl(uint8_t* ls, uint8_t* ebpFrame, int32_t fadePct) {
    if (ls == nullptr || ebpFrame == nullptr) return;
    const uint8_t* lvlInfo = *reinterpret_cast<const uint8_t* const*>(ls + 0x3c);
    const uint8_t* saveInfo = *reinterpret_cast<const uint8_t* const*>(ls + 0x40);
    if (lvlInfo == nullptr) return;
    int32_t total = *reinterpret_cast<const int16_t*>(lvlInfo + 0x70);
    int32_t collected = (saveInfo != nullptr) ? *reinterpret_cast<const int32_t*>(saveInfo + 0x8) : 0;

    int32_t xBase = *reinterpret_cast<const int32_t*>(ebpFrame - 0x4c);
    int32_t yBase = *reinterpret_cast<const int32_t*>(ebpFrame - 0x48);
    int32_t frame = *reinterpret_cast<const int32_t*>(ebpFrame - 0x54);
    int32_t cardSlideX = *reinterpret_cast<const int32_t*>(ebpFrame - 0x58);
    if (cardSlideX < 253) return;

    wchar_t wbuf[32];
    EdgeWString wsTxt;
    edgemod::FormatPrismRatioWstr(collected, total, wbuf, 32, &wsTxt);
    int tw = MeasureTextWidth(&wsTxt, 0x2000, 1);

    int rightX = xBase + 253;
    int centerY = yBase + 0x79;

    uint32_t* pFontCol = reinterpret_cast<uint32_t*>(0x5f4268);
    uint32_t savedCol = *pFontCol;

    uint32_t rainbowRgb = GetSmoothMenuRainbow(frame);
    uint32_t rawIconRgb = (collected > 0) ? rainbowRgb : 0x7d7d7du;
    uint32_t iconRgba = RgbToRgbaOpaque(Game_ColorLerp(0xffffffu, rawIconRgb, fadePct));
    Game_DrawQuad(rightX - tw - 16, centerY - 5, 10, 10, &iconRgba);

    uint32_t rawTextRgb = (collected >= total) ? rainbowRgb : 0x3b3b3bu;
    *pFontCol = RgbToRgbaOpaque(Game_ColorLerp(0xffffffu, rawTextRgb, fadePct));
    Game_DrawText(&wsTxt, rightX, centerY, 0x2000, 0x2000, 1, 0, 0x0a);

    *pFontCol = savedCol;
}

static void __declspec(naked) hk_lsPrismGridOrCount() {
    __asm {
        mov eax, DWORD PTR [edi + 0x3c]
        xor ecx, ecx
        mov DWORD PTR [ebp - 0x50], ecx
        movsx edx, WORD PTR [eax + 0x70]
        test edx, edx
        jle skip_ls_prisms
        cmp edx, 20
        jle vanilla_ls_prisms
        pushad
        push ebx
        push ebp
        push edi
        call RenderLevelSelectPrismCount_Impl
        add esp, 12
        popad
    skip_ls_prisms:
        mov eax, 0x4b877a
        jmp eax
    vanilla_ls_prisms:
        xor edx, edx
        mov eax, 0x4b86b0
        jmp eax
    }
}

static void __cdecl RenderScoresPrismCount_Impl(
    uint8_t* ebpFrame, int32_t flipScale, int32_t cardCenterRelX, int32_t cardTopRelY
) {
    if (ebpFrame == nullptr) return;
    const uint8_t* entry = *reinterpret_cast<const uint8_t* const*>(ebpFrame - 0x54);
    const uint8_t* scoresMenu = *reinterpret_cast<const uint8_t* const*>(ebpFrame - 0x5c);
    if (entry == nullptr || scoresMenu == nullptr) return;

    int32_t total = *reinterpret_cast<const int32_t*>(entry + 0x78);
    int32_t collected = *reinterpret_cast<const int32_t*>(entry + 0x7c);
    int32_t textFlipScale = *reinterpret_cast<const int32_t*>(ebpFrame - 0x70);
    int32_t xBase = *reinterpret_cast<const int32_t*>(ebpFrame - 0x4c);
    int32_t yBase = *reinterpret_cast<const int32_t*>(ebpFrame - 0x48);
    int32_t cardIdx = *reinterpret_cast<const int32_t*>(ebpFrame - 0x50);
    int32_t frame = *reinterpret_cast<const int32_t*>(ebpFrame - 0xa4);
    int32_t exitTimer = *reinterpret_cast<const int32_t*>(scoresMenu + 0x8);

    int centerX = cardCenterRelX + xBase;
    int centerY = cardTopRelY + yBase + 0x3b;

    uint32_t accentRgb = (exitTimer < 0)
        ? GetSmoothMenuRainbow(frame - cardIdx)
        : *reinterpret_cast<const uint32_t*>(ebpFrame - 0x84);
    uint32_t neutralRgb = (exitTimer < 0)
        ? *reinterpret_cast<const uint32_t*>(ebpFrame - 0xa8)
        : ((static_cast<uint32_t>(ebpFrame[-0xa0]) << 16)
           | (static_cast<uint32_t>(ebpFrame[-0x98]) << 8)
           | static_cast<uint32_t>(ebpFrame[-0x78]));

    int iconW = (12 * flipScale) >> 12;
    if (iconW > 0) {
        uint32_t iconRgba = RgbToRgbaOpaque((collected > 0) ? accentRgb : neutralRgb);
        Game_DrawQuad(centerX - iconW / 2, centerY - 19, iconW, 12, &iconRgba);
    }

    if (textFlipScale > 0) {
        wchar_t wbuf[32];
        EdgeWString wsTxt;
        edgemod::FormatPrismRatioWstr(collected, total, wbuf, 32, &wsTxt);
        int baseScaleX = 0x2000;
        int fullW = MeasureTextWidth(&wsTxt, 0x2000, 0);
        if (fullW > 74) {
            baseScaleX = (0x2000 * 74) / fullW;
        }
        int drawScaleX = (textFlipScale * baseScaleX) >> 12;
        if (drawScaleX > 0) {
            uint32_t* pFontCol = reinterpret_cast<uint32_t*>(0x5f4268);
            uint32_t savedCol = *pFontCol;
            uint32_t textRgb = (collected >= total)
                ? 0xffffffu
                : *reinterpret_cast<const uint32_t*>(ebpFrame - 0x110);
            *pFontCol = RgbToRgbaOpaque(textRgb);
            Game_DrawText(&wsTxt, centerX, centerY + 10, drawScaleX, 0x2000, 0, 0, 0x03);
            *pFontCol = savedCol;
        }
    }
}

static void __declspec(naked) hk_scoresPrismGridOrCount() {
    __asm {
        mov DWORD PTR [ebp - 0x64], 0
        mov DWORD PTR [ebp - 0x54], eax
        mov ecx, DWORD PTR [eax + 0x78]
        test ecx, ecx
        jle skip_scores_prisms
        cmp ecx, 20
        jle vanilla_scores_prisms
        pushad
        push edi
        push esi
        push ebx
        push ebp
        call RenderScoresPrismCount_Impl
        add esp, 16
        popad
    skip_scores_prisms:
        mov eax, 0x4b0cc7
        jmp eax
    vanilla_scores_prisms:
        mov eax, 0x4b09a5
        jmp eax
    }
}

} // namespace

namespace edgemod {

int32_t __cdecl GetPauseContentShiftY() {
    const uint8_t* level = GetLevel();
    if (level == nullptr) return 0;
    int16_t total = *reinterpret_cast<const int16_t*>(level + 0x7c);
    return (total > 10 && total <= 20) ? 12 : 0;
}

void __cdecl FormatPrismRatioWstr(int32_t collected, int32_t total, wchar_t* wbuf, size_t cap, EdgeWString* out) {
    if (collected < 0) collected = 0;
    if (total < 0) total = 0;
    int n = std::swprintf(wbuf, cap, L"%d/%d", collected, total);
    if (n < 0) {
        wbuf[0] = L'\0';
        n = 0;
    }
    std::memset(out, 0, sizeof(*out));
    out->ptr = wbuf;
    out->size = static_cast<uint32_t>(n);
    out->cap = static_cast<uint32_t>(cap - 1);
}

static uint8_t* GetMenuSubForState(uint32_t expectedState, uint32_t expectedVtbl) {
    if (expectedState > 0x17u) return nullptr;
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr || *reinterpret_cast<const uint32_t*>(sm + 0x50) != expectedState) {
        return nullptr;
    }
    uint8_t* ms = GetMenuScreen();
    if (ms == nullptr) return nullptr;
    uint8_t* sub = *reinterpret_cast<uint8_t**>(ms + expectedState * 8u + 0x44u);
    if (sub == nullptr || *reinterpret_cast<const uint32_t*>(sub) != expectedVtbl) {
        return nullptr;
    }
    return sub;
}

static OptItem GetActiveOptItemInCat(int cat, bool inPause, int selRow) {
    if (g_optUi.popupOpen && g_optUi.popupInPause == inPause) {
        return g_optUi.popupItem;
    }
    if (g_optUi.dragActive && g_optUi.dragInPause == inPause) {
        return g_optUi.dragItem;
    }
    OptItem items[kMaxOptRows];
    int nRows = GetOptItems(cat, inPause, items);
    if (selRow >= 0 && selRow < nRows) {
        return items[selRow];
    }
    return OPT_NONE;
}

bool GetHudTimerMenuPreview(
    uint32_t state, uint8_t* outMode, float* outSize, uint8_t* outPos,
    bool* outForceSample, bool* outDockBesideMenu
) {
    if (outMode == nullptr || outSize == nullptr || outPos == nullptr ||
        outForceSample == nullptr || outDockBesideMenu == nullptr) {
        return false;
    }
    *outMode = g_timerMode;
    *outSize = g_timerSize;
    *outPos  = g_timerPos;
    *outForceSample = false;
    *outDockBesideMenu = false;

    if (state == 9u || state == 0x12u) {
        const bool inPause = (state == 0x12u);
        uint8_t* sub = inPause
            ? GetMenuSubForState(0x12u, 0x58cf44u)
            : GetMenuSubForState(9u, 0x58d24cu);
        if (sub == nullptr || *reinterpret_cast<const int*>(sub + 8) >= 0) {
            return false;
        }
        if (inPause && *reinterpret_cast<const int*>(sub + 0x44) != 1) {
            return false;
        }

        const int cat = inPause ? g_pauseOptCat : g_mainOptCat;
        const int selRow = inPause
            ? *reinterpret_cast<const int*>(sub + 0x50)
            : *reinterpret_cast<const int*>(sub + 0x10);
        OptItem activeItem = GetActiveOptItemInCat(cat, inPause, selRow);

        if (cat == OPT_CAT_TIMER) {
            *outForceSample = !inPause;
            const bool isPos = (activeItem == OPT_TIMER_POS);
            *outDockBesideMenu = !isPos;
            if (g_optUi.popupOpen && g_optUi.popupInPause == inPause && g_optUi.popupItem == OPT_TIMER) {
                *outMode = static_cast<uint8_t>(std::clamp(g_optUi.popupHighlight, 0, 3));
            } else if (g_optUi.popupOpen && g_optUi.popupInPause == inPause && g_optUi.popupItem == OPT_TIMER_POS) {
                *outPos  = static_cast<uint8_t>(std::clamp(g_optUi.popupHighlight, 0, 3));
                *outMode = (g_timerMode == 0) ? 3u : g_timerMode;
            } else if (activeItem == OPT_TIMER_SIZE || activeItem == OPT_TIMER_POS ||
                       activeItem == OPT_TIMER_BG_OPACITY || activeItem == OPT_TIMER_SHADOW_OPACITY) {
                *outMode = (g_timerMode == 0) ? 3u : g_timerMode;
            } else {
                *outMode = g_timerMode;
            }
            return (*outMode != 0);
        }

        // Companion corner preview when positioning Prism Counter, Minimap, or Speedometer
        if ((cat == OPT_CAT_PRISM_COUNTER && activeItem == OPT_PRISM_COUNTER_POS) ||
            (cat == OPT_CAT_MINIMAP && activeItem == OPT_MINIMAP_POS) ||
            (cat == OPT_CAT_SPEEDOMETER && activeItem == OPT_SPEEDOMETER_POS)) {
            *outForceSample = !inPause;
            *outDockBesideMenu = false;
            *outMode = g_timerMode;
            return (*outMode != 0);
        }

        return false;
    }

    return (*outMode != 0);
}

bool GetHudPrismCounterMenuPreview(
    uint32_t state, uint8_t* outType, float* outSize, uint8_t* outPos,
    bool* outForceSample, bool* outDockBesideMenu
) {
    if (outType == nullptr || outSize == nullptr || outPos == nullptr ||
        outForceSample == nullptr || outDockBesideMenu == nullptr) {
        return false;
    }
    *outType = g_prismCounterType;
    *outSize = g_prismCounterSize;
    *outPos  = g_prismCounterPos;
    *outForceSample = false;
    *outDockBesideMenu = false;

    if (state == 9u || state == 0x12u) {
        const bool inPause = (state == 0x12u);
        uint8_t* sub = inPause
            ? GetMenuSubForState(0x12u, 0x58cf44u)
            : GetMenuSubForState(9u, 0x58d24cu);
        if (sub == nullptr || *reinterpret_cast<const int*>(sub + 8) >= 0) {
            return false;
        }
        if (inPause && *reinterpret_cast<const int*>(sub + 0x44) != 1) {
            return false;
        }

        const int cat = inPause ? g_pauseOptCat : g_mainOptCat;
        const int selRow = inPause
            ? *reinterpret_cast<const int*>(sub + 0x50)
            : *reinterpret_cast<const int*>(sub + 0x10);
        OptItem activeItem = GetActiveOptItemInCat(cat, inPause, selRow);

        if (cat == OPT_CAT_PRISM_COUNTER) {
            *outForceSample = !inPause;
            const bool isPos = (activeItem == OPT_PRISM_COUNTER_POS);
            *outDockBesideMenu = !isPos;
            if (g_optUi.popupOpen && g_optUi.popupInPause == inPause && g_optUi.popupItem == OPT_PRISM_COUNTER_TYPE) {
                *outType = static_cast<uint8_t>(std::clamp(g_optUi.popupHighlight, 0, 2));
            } else if (g_optUi.popupOpen && g_optUi.popupInPause == inPause && g_optUi.popupItem == OPT_PRISM_COUNTER_POS) {
                *outPos  = static_cast<uint8_t>(std::clamp(g_optUi.popupHighlight, 0, 3));
                *outType = (g_prismCounterType == 0) ? 2u : g_prismCounterType;
            } else if (activeItem == OPT_PRISM_COUNTER_SIZE || activeItem == OPT_PRISM_COUNTER_POS ||
                       activeItem == OPT_PRISM_BG_OPACITY || activeItem == OPT_PRISM_SHADOW_OPACITY) {
                *outType = (g_prismCounterType == 0) ? 2u : g_prismCounterType;
            } else {
                *outType = g_prismCounterType;
            }
            return (*outType != 0);
        }

        // Companion corner preview when positioning Timer, Minimap, or Speedometer
        if ((cat == OPT_CAT_TIMER && activeItem == OPT_TIMER_POS) ||
            (cat == OPT_CAT_MINIMAP && activeItem == OPT_MINIMAP_POS) ||
            (cat == OPT_CAT_SPEEDOMETER && activeItem == OPT_SPEEDOMETER_POS)) {
            *outForceSample = !inPause;
            *outDockBesideMenu = false;
            *outType = g_prismCounterType;
            return (*outType != 0);
        }

        return false;
    }

    return (*outType != 0);
}

bool __cdecl GetMinimapMenuPreview(
    uint32_t state, uint8_t* outPos, bool* outForceSample, bool* outDockBesideMenu
) {
    if (outPos == nullptr || outForceSample == nullptr || outDockBesideMenu == nullptr) {
        return false;
    }
    *outPos = GetMinimapDisplayPos();
    *outForceSample = false;
    *outDockBesideMenu = false;
    if (state != 9u && state != 0x12u) return false;
    const bool inPause = (state == 0x12u);
    uint8_t* sub = inPause
        ? GetMenuSubForState(0x12u, 0x58cf44u)
        : GetMenuSubForState(9u, 0x58d24cu);
    if (sub == nullptr || *reinterpret_cast<const int*>(sub + 8) >= 0) {
        return false;
    }
    if (inPause && *reinterpret_cast<const int*>(sub + 0x44) != 1) {
        return false;
    }

    const int cat = inPause ? g_pauseOptCat : g_mainOptCat;
    const int selRow = inPause
        ? *reinterpret_cast<const int*>(sub + 0x50)
        : *reinterpret_cast<const int*>(sub + 0x10);
    OptItem activeItem = GetActiveOptItemInCat(cat, inPause, selRow);

    if (cat == OPT_CAT_MINIMAP) {
        *outForceSample = !inPause;
        const bool isPos = (activeItem == OPT_MINIMAP_POS);
        *outDockBesideMenu = !isPos;
        if (g_optUi.popupOpen && g_optUi.popupInPause == inPause && g_optUi.popupItem == OPT_MINIMAP_POS) {
            *outPos = static_cast<uint8_t>(std::clamp(g_optUi.popupHighlight, 0, 3));
            return true;
        }
        if (activeItem == OPT_MINIMAP_ENABLE || activeItem == OPT_BACK) {
            return GetMinimapDisplayEnabled();
        }
        return true;
    }

    // Companion corner preview when positioning Timer, Prism Counter, or Speedometer
    if ((cat == OPT_CAT_TIMER && activeItem == OPT_TIMER_POS) ||
        (cat == OPT_CAT_PRISM_COUNTER && activeItem == OPT_PRISM_COUNTER_POS) ||
        (cat == OPT_CAT_SPEEDOMETER && activeItem == OPT_SPEEDOMETER_POS)) {
        *outForceSample = !inPause;
        *outDockBesideMenu = false;
        return GetMinimapDisplayEnabled();
    }

    return false;
}

bool GetHudSpeedometerMenuPreview(
    uint32_t state, uint8_t* outEnable, float* outSize, uint8_t* outPos,
    bool* outForceSample, bool* outDockBesideMenu
) {
    if (outEnable == nullptr || outSize == nullptr || outPos == nullptr ||
        outForceSample == nullptr || outDockBesideMenu == nullptr) {
        return false;
    }
    *outEnable = g_speedometerEnable;
    *outSize   = g_speedometerSize;
    *outPos    = g_speedometerPos;
    *outForceSample = false;
    *outDockBesideMenu = false;

    if (state == 9u || state == 0x12u) {
        const bool inPause = (state == 0x12u);
        uint8_t* sub = inPause
            ? GetMenuSubForState(0x12u, 0x58cf44u)
            : GetMenuSubForState(9u, 0x58d24cu);
        if (sub == nullptr || *reinterpret_cast<const int*>(sub + 8) >= 0) {
            return false;
        }
        if (inPause && *reinterpret_cast<const int*>(sub + 0x44) != 1) {
            return false;
        }

        const int cat = inPause ? g_pauseOptCat : g_mainOptCat;
        const int selRow = inPause
            ? *reinterpret_cast<const int*>(sub + 0x50)
            : *reinterpret_cast<const int*>(sub + 0x10);
        OptItem activeItem = GetActiveOptItemInCat(cat, inPause, selRow);

        if (cat == OPT_CAT_SPEEDOMETER) {
            *outForceSample = !inPause;
            const bool isPos = (activeItem == OPT_SPEEDOMETER_POS);
            *outDockBesideMenu = !isPos;
            if (g_optUi.popupOpen && g_optUi.popupInPause == inPause && g_optUi.popupItem == OPT_SPEEDOMETER_POS) {
                *outPos = static_cast<uint8_t>(std::clamp(g_optUi.popupHighlight, 0, 3));
                *outEnable = 1u;
            } else if (activeItem == OPT_SPEEDOMETER_SIZE || activeItem == OPT_SPEEDOMETER_POS ||
                       activeItem == OPT_SPEEDOMETER_BG_OPACITY || activeItem == OPT_SPEEDOMETER_SHADOW_OPACITY) {
                *outEnable = 1u;
            } else {
                *outEnable = g_speedometerEnable;
            }
            return (*outEnable != 0);
        }

        // Companion corner preview when positioning Timer, Prism Counter, or Minimap
        if ((cat == OPT_CAT_TIMER && activeItem == OPT_TIMER_POS) ||
            (cat == OPT_CAT_PRISM_COUNTER && activeItem == OPT_PRISM_COUNTER_POS) ||
            (cat == OPT_CAT_MINIMAP && activeItem == OPT_MINIMAP_POS)) {
            *outForceSample = !inPause;
            *outDockBesideMenu = false;
            *outEnable = g_speedometerEnable;
            return (*outEnable != 0);
        }

        return false;
    }

    return (*outEnable != 0);
}

void __cdecl GetEffectiveMinimapMetrics(EffectiveMinimapMetrics* out) {
    if (out == nullptr) return;
    int32_t iniW = (g_iniMinimapWidth > 0) ? g_iniMinimapWidth : kDefMinimapWidth;
    int32_t iniH = (g_iniMinimapHeight > 0) ? g_iniMinimapHeight : kDefMinimapHeight;
    float   iniCW = (g_iniMinimapCellW > 0.001f) ? g_iniMinimapCellW : kDefMinimapCellW;
    float   iniCH = (g_iniMinimapCellH > 0.001f) ? g_iniMinimapCellH : kDefMinimapCellH;

    out->enabled = GetMinimapDisplayEnabled();
    out->width  = (g_minimapWidthOverride != 0)
        ? std::round(static_cast<float>(kDefMinimapWidth) * g_minimapWidth)
        : static_cast<float>(iniW);
    out->height = (g_minimapHeightOverride != 0)
        ? std::round(static_cast<float>(kDefMinimapHeight) * g_minimapHeight)
        : static_cast<float>(iniH);
    out->cellW  = (g_minimapRangeOverride != 0)
        ? (kDefMinimapCellW / g_minimapRange)
        : iniCW;
    out->cellH  = (g_minimapRangeOverride != 0)
        ? (kDefMinimapCellH / g_minimapRange)
        : iniCH;
    out->posCorner = GetMinimapDisplayPos();
    if (g_minimapPosOverride != 0) {
        out->alignX = (out->posCorner == 1u || out->posCorner == 3u) ? 2 : 0;
        out->alignY = (out->posCorner == 2u || out->posCorner == 3u) ? 2 : 0;
        out->offX = 0;
        out->offY = 0;
    } else {
        out->alignX = g_iniMinimapAlignX;
        out->alignY = g_iniMinimapAlignY;
        out->offX = g_iniMinimapOffX;
        out->offY = g_iniMinimapOffY;
    }
}

void __cdecl SyncLiveWorldMinimapFields() {
    EffectiveMinimapMetrics eff{};
    GetEffectiveMinimapMetrics(&eff);
    uint8_t* ws = GetWorldScreen();
    if (ws != nullptr && ws[0x2120] != 0) {
        ws[0x2110] = eff.enabled ? 1u : 0u;
        if (!eff.enabled) {
            *reinterpret_cast<int32_t*>(ws + 0x2108) = 0;
        }
        *reinterpret_cast<int32_t*>(ws + 0x1a0) = eff.offX;
        *reinterpret_cast<int32_t*>(ws + 0x1a4) = eff.offY;
        *reinterpret_cast<int32_t*>(ws + 0x1a8) = eff.alignX;
        *reinterpret_cast<int32_t*>(ws + 0x1ac) = eff.alignY;
        *reinterpret_cast<float*>(ws + 0x2114) = eff.width;
        *reinterpret_cast<float*>(ws + 0x2118) = eff.height;
        *reinterpret_cast<float*>(ws + 0x1b0)  = eff.cellW;
        *reinterpret_cast<float*>(ws + 0x1b4)  = eff.cellH;
    }
}

int __cdecl GetActiveOptionsCategory(bool inPause) {
    return inPause ? g_pauseOptCat : g_mainOptCat;
}

bool __cdecl IsOptionsPopupOpen(bool inPause) {
    return g_optUi.popupOpen && g_optUi.popupInPause == inPause;
}

bool __cdecl IsOptionsSliderDragging(bool inPause) {
    return g_optUi.dragActive && g_optUi.dragInPause == inPause;
}

bool __cdecl IsOptionsScrollbarDragging(bool inPause) {
    return GetOptScrollState(inPause).dragActive;
}

void __cdecl OnOptionsMouseWheel(int32_t wheelDelta) {
    if (wheelDelta == 0) return;
    const uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
    uint32_t state = *reinterpret_cast<const uint32_t*>(sm + 0x50);
    if (state == 9u && IsOptCategoryScrollable(g_mainOptCat)) {
        g_optWheelAccum.fetch_add(wheelDelta, std::memory_order_acq_rel);
    } else if (state == 0x12u && IsOptCategoryScrollable(g_pauseOptCat)) {
        g_optWheelAccum.fetch_add(wheelDelta, std::memory_order_acq_rel);
    }
}

uint32_t __cdecl GetOptionsScrollRevision(bool inPause) {
    return GetOptScrollState(inPause).revision;
}

bool __cdecl IsOptionsMenuReadyForHover(const uint8_t* menuObj, bool inPause) {
    if (menuObj == nullptr) return false;
    int enterFrames = *reinterpret_cast<const int*>(menuObj + 4);
    int exitTimer   = *reinterpret_cast<const int*>(menuObj + 8);
    if (exitTimer >= 0) return false;
    if (!inPause) {
        return enterFrames >= 8;
    }
    if (enterFrames < kPauseOpenReadyTick) return false;
    int page    = *reinterpret_cast<const int*>(menuObj + 0x44);
    int scrollX = *reinterpret_cast<const int*>(menuObj + 0x4c);
    if (page != 1 || scrollX < kMenuWideW) return false;
    return g_pauseSubSwipeX == GetOptCategoryDepth(g_pauseOptCat) * kMenuWideW;
}

int __cdecl HitTestOptionsSlot(bool inPause, int mx, int my) {
    int basePt[2] = {0, 0};
    GetMenuBasePt_Impl(basePt);
    int menuW = *reinterpret_cast<const int*>(0x589ddc);
    int cat = inPause ? g_pauseOptCat : g_mainOptCat;
    int nRows = GetOptRowCount(cat, inPause);
    if (nRows <= 0) return -1;

    const int pauseShiftY = inPause ? GetPauseContentShiftY() : 0;
    OptScrollState& scrollSt = GetOptScrollState(inPause);
    OptScrollMetrics scrollM{};
    ComputeOptScrollMetrics(inPause, cat, nRows, basePt[0], basePt[1] + pauseShiftY, menuW, scrollSt.renderY, &scrollM);

    if (scrollM.scrollable) {
        int paneW = inPause ? kMenuWideW : menuW;
        int minX = basePt[0] + 2;
        int maxX = basePt[0] + paneW - 2;
        if (mx < minX || mx >= maxX) return -1;
        if (scrollM.hasScrollbar && mx >= scrollM.sbX - 2 && mx <= scrollM.sbX + scrollM.sbW + 2 &&
            my >= scrollM.listTop && my < scrollM.listBottom) {
            return -1;
        }
        int backTop = scrollM.backTop - scrollM.slotPad;
        if (my >= backTop && my < backTop + scrollM.backH + 2 * scrollM.slotPad) {
            return scrollM.backIdx;
        }
        if (scrollM.hasScrollbar && mx >= scrollM.sbX - 2) return -1;
        if (my < scrollM.listTop || my >= scrollM.listBottom) return -1;
        int scrollOff = static_cast<int>(std::lround(scrollSt.renderY));
        for (int row = 0; row < scrollM.nScrollRows; ++row) {
            int top = scrollM.listTop + row * scrollM.stride - scrollOff - scrollM.slotPad;
            if (my >= top && my < top + scrollM.rowH + 2 * scrollM.slotPad) return row;
        }
        return -1;
    }

    if (!inPause) {
        int minX = basePt[0] + 2;
        int maxX = basePt[0] + menuW - 2;
        if (mx < minX || mx >= maxX) return -1;
        int virtH = reinterpret_cast<int(__cdecl*)()>(0x496060)();
        int stride = 0x32;
        int backTopRel = 0;
        int rowH = kMainOptRowH;
        ComputeMainOptLayout(nRows, virtH, &stride, &backTopRel, &rowH);
        int backIdx = nRows - 1;
        int slotPad = (std::max)(0, (stride - rowH) / 2);
        for (int row = 0; row < nRows; ++row) {
            int top = basePt[1] + ((row == backIdx) ? backTopRel : (0x56 + stride * row)) - slotPad;
            if (my >= top && my < top + rowH + 2 * slotPad) return row;
        }
        return -1;
    }

    int minX = basePt[0] + 2;
    int maxX = basePt[0] + kMenuWideW - 2;
    if (mx < minX || mx >= maxX) return -1;
    int rowH = ComputePauseOptRowH(basePt[1], nRows);
    for (int row = 0; row < nRows; ++row) {
        int top = basePt[1] + 0x75 + pauseShiftY + row * rowH;
        if (my >= top && my < top + rowH) return row;
    }
    return -1;
}

} // namespace edgemod

void RegisterMenuCameraOptions() {
    // 1. Nested OptionsMenu (state 9): enter, hitTest, cancel, adjustable, action, update, render
    {
        static const uint8_t exEnter[8] = {0x56, 0x8b, 0xf1, 0xe8, 0xf8, 0x38, 0xfe, 0xff};
        RegRelJmp(0x4b25d0, exEnter, 8, 5, &hk_optOnEnter, 0x90, 0x4b25ef);

        static const uint8_t exHit[8] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x0c, 0x53, 0x56};
        RegRelJmp(0x4b25f0, exHit, 8, 5, &hk_optHitTest, 0x90, 0x4b2687);

        static const uint8_t exCancel[9] = {0x56, 0x8b, 0xf1, 0x8b, 0x0d, 0x80, 0x90, 0x5f, 0x00};
        RegRelJmp(0x4b2690, exCancel, 9, 5, &hk_optOnCancel, 0x90, 0x4b26aa);

        static const uint8_t exAdj[8] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x85, 0xc0};
        RegRelJmp(0x4b26b0, exAdj, 8, 5, &hk_optIsAdj, 0x90, 0x4b26c8);

        static const uint8_t exAct[9] = {0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x10, 0x83, 0xf8, 0x06};
        RegRelJmp(0x4b26f0, exAct, 9, 5, &hk_optOnAction, 0x90, 0x4b278f);

        static const uint8_t exUpd[8] = {0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0x83, 0x7e};
        RegRelJmp(0x4b27b0, exUpd, 8, 5, &hk_optUpdate, 0x90, 0x4b286a);

        static const uint8_t exRen[10] = {0x8b, 0x45, 0xc0, 0x6a, 0x08, 0xa3, 0x68, 0x42, 0x5f, 0x00};
        RegRelJmp(0x4b2d7b, exRen, 10, 5, &hk_renderMainOptionsBody, 0x90, 0x4b37a3);
    }

    // 2. MainMenu 6 rows (edge challenge, scores, options, instructions, credits, exit game), ModeSelect 5 rows, Instructions -> MainMenu, Credits -> MainMenu
    {
        // MainMenu row count 5 -> 6
        const uint8_t exHit5[5] = {0x83, 0xfe, 0x05, 0x7c, 0xd6};
        const uint8_t rpHit6[5] = {0x83, 0xfe, 0x06, 0x7c, 0xd6};
        RegBytes(0x4b97c5, exHit5, rpHit6, 5);

        const uint8_t exRenCmp5[10] = {0x43, 0x83, 0xfb, 0x05, 0x0f, 0x8c, 0xd0, 0xfe, 0xff, 0xff};
        const uint8_t rpRenCmp6[10] = {0x43, 0x83, 0xfb, 0x06, 0x0f, 0x8c, 0xd0, 0xfe, 0xff, 0xff};
        RegBytes(0x4b9fd8, exRenCmp5, rpRenCmp6, 10);

        const uint8_t exUpWrap[7] = {0xc7, 0x46, 0x10, 0x04, 0x00, 0x00, 0x00};
        const uint8_t rpUpWrap[7] = {0xc7, 0x46, 0x10, 0x05, 0x00, 0x00, 0x00};
        RegBytes(0x4ba1a4, exUpWrap, rpUpWrap, 7);

        const uint8_t exDnWrap[6] = {0x83, 0x7e, 0x10, 0x05, 0x7c, 0x2e};
        const uint8_t rpDnWrap[6] = {0x83, 0x7e, 0x10, 0x06, 0x7c, 0x2e};
        RegBytes(0x4ba1bd, exDnWrap, rpDnWrap, 6);

        // MainMenu row string IDs, action dispatch, and return focus
        const uint8_t exGetStrId[9] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x83, 0xf8, 0x04};
        RegRelJmp(0x4b9800, exGetStrId, 9, 5, &hk_mainMenuGetRowStrId, 0x90, 0x4b983c);

        // NOTE: keep totalSpan=5 at 0x4b9ee3 so 0x4b9eb5's jump target at 0x4b9eef stays intact!
        const uint8_t exRenStrId[12] = {0x83, 0xfb, 0x04, 0x77, 0x2f, 0xff, 0x24, 0x9d, 0x40, 0xa0, 0x4b, 0x00};
        RegRelJmp(0x4b9ee3, exRenStrId, 12, 5, &hk_mainMenuRenderRowStrId, 0x90, 0x4b9f1c);

        const uint8_t exAct[12] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x20, 0xa1, 0xa0, 0x44, 0x5f, 0x00, 0x33};
        RegRelJmp(0x4ba060, exAct, 12, 5, &hk_mainMenuAction, 0x90, 0x4ba125);

        const uint8_t exFocus[45] = {
            0x83, 0xf8, 0x0b, 0x75, 0x09, 0xc7, 0x46, 0x10, 0x01, 0x00, 0x00, 0x00, 0xeb, 0x22,
            0x83, 0xf8, 0x09, 0x75, 0x09, 0xc7, 0x46, 0x10, 0x02, 0x00, 0x00, 0x00, 0xeb, 0x14,
            0x33, 0xc9, 0x83, 0xf8, 0x0f, 0x0f, 0x95, 0xc1, 0x49, 0x83, 0xe1, 0x04, 0x89, 0x4e, 0x10, 0xeb, 0x03
        };
        RegRelJmp(0x4ba2de, exFocus, 45, 5, &hk_mainMenuFocus, 0x90, 0x4ba30b);

        // InstructionsMenu returns to MainMenu (2) instead of ModeSelect (3)
        const uint8_t exInstr3[2] = {0x6a, 0x03};
        const uint8_t rpInstr2[2] = {0x6a, 0x02};
        RegBytes(0x4b3860, exInstr3, rpInstr2, 2);
        RegBytes(0x4b3a15, exInstr3, rpInstr2, 2);

        // ModeSelect ("edge challenge"): remove instructions (6 rows -> 5 rows; row 4 is back)
        const uint8_t exMsCase4[5] = {0xb8, 0x05, 0x00, 0x00, 0x00};
        const uint8_t rpMsCase4[5] = {0xb8, 0x00, 0x00, 0x00, 0x00};
        RegBytes(0x4b8b2f, exMsCase4, rpMsCase4, 5);
        RegBytes(0x4b956d, exMsCase4, rpMsCase4, 5);

        const uint8_t exMsHit5[5] = {0x83, 0xff, 0x05, 0x75, 0x0d};
        const uint8_t rpMsHit4[5] = {0x83, 0xff, 0x04, 0x75, 0x0d};
        RegBytes(0x4b8b82, exMsHit5, rpMsHit4, 5);

        const uint8_t exMsHit6[5] = {0x83, 0xff, 0x06, 0x7c, 0xa9};
        const uint8_t rpMsHit5[5] = {0x83, 0xff, 0x05, 0x7c, 0xa9};
        RegBytes(0x4b8bd4, exMsHit6, rpMsHit5, 5);

        const uint8_t exMsAct[7] = {0x83, 0xf8, 0x04, 0x75, 0x18, 0x8b, 0x0d};
        const uint8_t rpMsAct[7] = {0x83, 0xf8, 0x04, 0x74, 0x1d, 0x5e, 0xc3};
        RegBytes(0x4b8cf4, exMsAct, rpMsAct, 7);

        const uint8_t exMsUp6[3] = {0x8d, 0x4a, 0x06};
        const uint8_t rpMsUp5[3] = {0x8d, 0x4a, 0x05};
        RegBytes(0x4b8d63, exMsUp6, rpMsUp5, 3);

        const uint8_t exMsDn6[4] = {0x83, 0x7e, 0x10, 0x06};
        const uint8_t rpMsDn5[4] = {0x83, 0x7e, 0x10, 0x05};
        RegBytes(0x4b8d97, exMsDn6, rpMsDn5, 4);

        const uint8_t exMsBar5[4] = {0x83, 0x7f, 0x10, 0x05};
        const uint8_t rpMsBar4[4] = {0x83, 0x7f, 0x10, 0x04};
        RegBytes(0x4b93f9, exMsBar5, rpMsBar4, 4);

        const uint8_t exMsLoop5[3] = {0x83, 0xfb, 0x05};
        const uint8_t rpMsLoop4[3] = {0x83, 0xfb, 0x04};
        RegBytes(0x4b94f2, exMsLoop5, rpMsLoop4, 3);

        const uint8_t exMsLoop6[3] = {0x83, 0xfb, 0x06};
        const uint8_t rpMsLoop5[3] = {0x83, 0xfb, 0x05};
        RegBytes(0x4b9634, exMsLoop6, rpMsLoop5, 3);

        auto b3c = [](uint32_t va, uint8_t op1, uint8_t op2, uint8_t eo, uint8_t en) {
            uint8_t ex[3] = {op1, op2, eo}, rp[3] = {op1, op2, en};
            RegBytes(va, ex, rp, 3);
        };
        b3c(0x4a17e1, 0x83, 0xe0, 0x07, 0x00);
        b3c(0x4a18f5, 0x83, 0xe2, 0x07, 0x00);
        b3c(0x4a192a, 0x83, 0xe0, 0x07, 0x00);
        b3c(0x4a1977, 0x83, 0xe0, 0x07, 0x00);
        b3c(0x4a1999, 0x83, 0xe2, 0x07, 0x00);
        {
            const uint8_t ex[2] = {0x6a, 0x09}, rp[2] = {0x6a, 0x02};
            RegBytes(0x4a19ae, ex, rp, 2);
            RegBytes(0x4a1a5d, ex, rp, 2);
        }
    }

    // 3. Nested PauseMenu Page 1 (state 0x12) + INI + ttsav
    {
        static const uint8_t exPEnter[9] = {0x8b, 0x46, 0x44, 0x89, 0x5e, 0x50, 0x89, 0x46, 0x48};
        RegRelJmp(0x4acae1, exPEnter, 9, 5, &hk_pauseEnterP1, 0x90, 0x4acaf3);

        static const uint8_t exPCancel[8] = {0x8b, 0x0d, 0x80, 0x90, 0x5f, 0x00, 0x6a, 0x0b};
        RegRelJmp(0x4acb6d, exPCancel, 8, 5, &hk_pauseP1Cancel, 0x90, 0x4acb88);

        static const uint8_t exPAdj[8] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x85, 0xc0};
        RegRelJmp(0x4acbb0, exPAdj, 8, 5, &hk_pauseIsAdj, 0x90, 0x4acbc8);

        static const uint8_t exPUp[9] = {0xff, 0x4e, 0x50, 0x0f, 0x89, 0x24, 0x01, 0x00, 0x00};
        RegRelJmp(0x4acc66, exPUp, 9, 5, &hk_pauseP1Up, 0x90, 0x4acc76);

        static const uint8_t exPDn[7] = {0x01, 0x7e, 0x50, 0x83, 0x7e, 0x50, 0x06};
        RegRelJmp(0x4accb7, exPDn, 7, 5, &hk_pauseP1Down, 0x90, 0x4acccb);

        static const uint8_t exPHitEntry[6] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x18};
        RegRelJmp(0x4acf60, exPHitEntry, 6, 5, &hk_pauseP1HitTest, 0x90);

        static const uint8_t exPHit[5] = {0x83, 0xf8, 0x06, 0x7c, 0xba};
        RegRelJmp(0x4acfe1, exPHit, 5, 5, &hk_pauseP1HitCmp, 0x90, 0x4acfe6);

        static const uint8_t exPSwipe[7] = {0x8b, 0x46, 0x44, 0x85, 0xc0, 0x74, 0x17};
        RegRelJmp(0x4acdd7, exPSwipe, 7, 5, &hk_pauseUpdateSwipe, 0x90, 0x4ace23);

        const uint8_t exDisp[19] = {
            0x8b, 0x46, 0x50, 0x83, 0xf8, 0x05, 0x0f, 0x87, 0x7c, 0x01, 0x00, 0x00, 0xff, 0x24, 0x85, 0x14, 0xcb, 0x4a, 0x00
        };
        RegRelJmp(0x4ac989, exDisp, 19, 19, &hk_pausePage1Dispatch, 0xCC);

        static const uint8_t exPRen[10] = {0x8b, 0x45, 0xbc, 0x8d, 0x55, 0xc0, 0x52, 0x8b, 0x55, 0xb8};
        RegRelJmp(0x4ad519, exPRen, 10, 5, &hk_renderPauseOptionsBody, 0x90, 0x4adf53);

        const uint8_t exIni[5] = {0xe8, 0xd1, 0xe8, 0xfc, 0xff};
        RegRelCall(0x476fea, exIni, 5, 5, &hk_readIniSettings);

        const uint8_t exLoad[7] = {0x8b, 0xce, 0xe8, 0x01, 0xa1, 0x08, 0x00};
        RegRelCall(0x492558, exLoad, 7, 7, &hk_loadTtsav);

        const uint8_t exLoadLocal[5] = {0xe8, 0xf5, 0xf4, 0xff, 0xff};
        RegRelCall(0x492ef6, exLoadLocal, 5, 5, &hk_loadUserSettingsLocal);
    }

    // 4. Camera Angle, Near/Far & Skybox
    {
        const uint8_t exSetFov[7] = {0x55, 0x8b, 0xec, 0x80, 0x7d, 0x08, 0x00};
        RegRelJmp(0x481bb0, exSetFov, 7, 7, &hk_setFov, 0x90);

        const uint8_t exPsy[5] = {0x5d, 0xc2, 0x10, 0x00, 0xcc};
        RegRelJmp(0x4357f7, exPsy, 5, 5, &hk_perspScaleY, 0x90);

        const uint8_t exFrust[6] = {0x55, 0x8b, 0xec, 0xd9, 0x45, 0x08};
        RegRelJmp(0x435750, exFrust, 6, 6, &hk_frustumSetFov, 0x90);

        const uint8_t exCfv[7] = {0x8b, 0x55, 0xf4, 0x80, 0x7a, 0x10, 0x00};
        RegRelJmp(0x47ba3b, exCfv, 7, 7, &hk_camFovVertigo, 0x90);

        const uint8_t exMul[8] = {0xa1, 0x80, 0x90, 0x5f, 0x00, 0xd9, 0x40, 0x34};
        RegRelJmp(0x47ba5f, exMul, 8, 8, &hk_saveActiveMul, 0x90);

        const uint8_t exAnf[5] = {0xe8, 0x78, 0x7c, 0xfa, 0xff};
        RegRelCall(0x47bbc3, exAnf, 5, 5, &hk_adjustNearFar);

        const uint8_t exSbw[5] = {0xe8, 0xe6, 0xfd, 0xff, 0xff};
        RegRelCall(0x407805, exSbw, 5, 5, &hk_skyboxProjWrap);

        const uint8_t exEtTan[5] = {0xe8, 0xdb, 0xf3, 0x03, 0x00};
        RegRelCall(0x47b826, exEtTan, 5, 5, &hk_edgeTimeTanHalfFov);

        const uint8_t exPanTan[5] = {0xe8, 0xa8, 0x55, 0x02, 0x00};
        RegRelCall(0x495659, exPanTan, 5, 5, &hk_panTanHalfFov);
    }

    // 5. Time Format
    {
        const uint8_t exA[5] = {0x68, 0x58, 0xcc, 0x58, 0x00};
        RegRelCall(0x4aa139, exA, 5, 5, &hk_timeFmtPush);
        const uint8_t exB[5] = {0x68, 0x88, 0xcc, 0x58, 0x00};
        RegRelCall(0x4aa96f, exB, 5, 5, &hk_timeFmtPush);

        const uint8_t exEdge[14] = {0x7e, 0x07, 0x68, 0x70, 0xcc, 0x58, 0x00, 0xeb, 0x05, 0x68, 0x80, 0xcc, 0x58, 0x00};
        // Wait: after calling hk_fmtSelEdge, original flow needs `push ecx`! Put call at +0 (5B), push ecx (1B, 0x51), pad 8 NOPs!
        std::vector<uint8_t> repEdge(14, 0x90);
        repEdge[0] = 0xe8;
        int32_t relEdge = static_cast<int32_t>(reinterpret_cast<uint32_t>(&hk_fmtSelEdge)) - static_cast<int32_t>(0x4aa5e6 + 5);
        std::memcpy(&repEdge[1], &relEdge, 4);
        repEdge[5] = 0x51; // push ecx
        RegBytes(0x4aa5e6, exEdge, repEdge.data(), 14);

        const uint8_t exSy[5] = {0x68, 0x00, 0x10, 0x00, 0x00};
        for (uint32_t va : {0x4b022c, 0x4b034b, 0x4b743f, 0x4b75aa}) {
            RegRelCall(va, exSy, 5, 5, &hk_sepScaleYPush);
        }
        const uint8_t exD8[5] = {0x68, 0xd8, 0xd0, 0x58, 0x00};
        RegRelCall(0x4b031e, exD8, 5, 5, &hk_sep2WstrPush);
        const uint8_t exF8[5] = {0x68, 0xf8, 0xd4, 0x58, 0x00};
        RegRelCall(0x4b757c, exF8, 5, 5, &hk_sep2WstrPush);
    }

    // 6. Skip Intro
    {
        const uint8_t exCtor[5] = {0xe8, 0x89, 0x2d, 0xf8, 0xff};
        RegRelCall(0x49f262, exCtor, 5, 5, &hk_initSkipIntro);

        RegIntroGate(0x49c26b, 0xb9, 0x49c290, 0x49c2a9);
        RegIntroGate(0x49c30f, 0xb9, 0x49c334, 0x49c353);
        RegIntroGate(0x49c884, 0xbe, 0x49c8a9, 0x49c8c0);
        RegIntroGate(0x49c97d, 0xbe, 0x49c9a2, 0x49c9ba);

        const uint8_t exUpdSkip[47] = {
            0x80, 0xbe, 0x5c, 0x01, 0x00, 0x00, 0x00, 0x75, 0x1c,
            0xf3, 0x0f, 0x10, 0x86, 0x64, 0x01, 0x00, 0x00,
            0xf3, 0x0f, 0x10, 0x8e, 0x58, 0x01, 0x00, 0x00,
            0x0f, 0x5a, 0xc0, 0x0f, 0x5a, 0xc9, 0x66, 0x0f, 0x2f, 0xc1, 0x77, 0x06,
            0xc6, 0x45, 0x9b, 0x00, 0xeb, 0x04, 0xc6, 0x45, 0x9b, 0x01
        };
        uint8_t rpUpdSkip[47] = {
            0xc6, 0x45, 0x9b, 0x01,                               // mov byte ptr [ebp-0x65], 1
            0x80, 0x3d, 0, 0, 0, 0, 0x00,                         // cmp byte ptr [g_skipIntro], 0
            0x75, static_cast<uint8_t>(0x49ca86 - (0x49ca01 + 13)), // jnz 0x49ca86
            0x80, 0xbe, 0x5c, 0x01, 0x00, 0x00, 0x00,             // cmp byte ptr [esi+0x15c], 0
            0x75, 0x11,                                           // jne +17 (.not_done)
            0xf3, 0x0f, 0x10, 0x86, 0x64, 0x01, 0x00, 0x00,       // movss xmm0, [esi+0x164]
            0x0f, 0x2e, 0x86, 0x58, 0x01, 0x00, 0x00,             // ucomiss xmm0, [esi+0x158]
            0x77, static_cast<uint8_t>(0x49ca30 - (0x49ca01 + 39)), // ja 0x49ca30
            0xc6, 0x45, 0x9b, 0x00,                               // .not_done: mov byte ptr [ebp-0x65], 0
            0x90, 0x90, 0x90, 0x90
        };
        uint32_t gAddr = reinterpret_cast<uint32_t>(&g_skipIntro);
        std::memcpy(&rpUpdSkip[6], &gAddr, 4);
        RegBytes(0x49ca01, exUpdSkip, rpUpdSkip, 47);

        const uint8_t exT_esi64[8] = {0xf3, 0x0f, 0x10, 0x86, 0x64, 0x01, 0x00, 0x00};
        RegRelCall(0x49cb21, exT_esi64, 8, 8, &hk_getCamIntroT);
        RegRelCall(0x49cb34, exT_esi64, 8, 8, &hk_getCamIntroT);
        const uint8_t exT1_esi64[8] = {0xf3, 0x0f, 0x10, 0x8e, 0x64, 0x01, 0x00, 0x00};
        RegRelCall(0x49ccf7, exT1_esi64, 8, 8, &hk_getCamIntroT1);

        const uint8_t exSpl[5] = {0xe8, 0x0c, 0x6f, 0x00, 0x00};
        RegRelCall(0x4323cf, exSpl, 5, 5, &hk_evalSplineCurve);
    }

    // 7. Music Start
    {
        const uint8_t exMs[5] = {0x83, 0xf8, 0x03, 0x74, 0x44};
        RegRelJmp(0x490302, exMs, 5, 5, &hk_musicStartGate, 0x90);
    }

    // 8. Unlock All Levels & Modes (without triggering Steam achievements)
    {
        // ProgressMgr::isModeAndBonusSlotUnlocked (0x4944fa): after valid check, return 1
        const uint8_t exMode[2] = {0x85, 0xf6};
        const uint8_t rpMode[2] = {0xeb, 0x30}; // jmp short 0x49452c
        RegBytes(0x4944fa, exMode, rpMode, 2);

        // ModeSelect::onEnter (0x4b8a56): call 0x494450 (ret 4) -> add esp, 4; mov al, 1
        const uint8_t exMs[5] = {0xe8, 0xf5, 0xb9, 0xfd, 0xff};
        const uint8_t rpMs[5] = {0x83, 0xc4, 0x04, 0xb0, 0x01};
        RegBytes(0x4b8a56, exMs, rpMs, 5);

        // LevelSelect::refreshTiles (0x4b5fcd): call 0x494990 (ret 8) -> return 2 if completed, 1 if unlocked & unplayed
        const uint8_t exLs[5] = {0xe8, 0xbe, 0xe9, 0xfd, 0xff};
        RegRelCall(0x4b5fcd, exLs, 5, 5, &LevelSelectTileFlag_Impl);

        // ScoresMenu::buildRows (0x4b1625): call 0x494990 (ret 8) -> add esp, 8; mov al, byte ptr [edi] (LevelSaveInfo::completed)
        const uint8_t exSc[5] = {0xe8, 0x66, 0x33, 0xfe, 0xff};
        const uint8_t rpSc[5] = {0x83, 0xc4, 0x08, 0x8a, 0x07};
        RegBytes(0x4b1625, exSc, rpSc, 5);

        // LevelSelect::renderCard (0x4b6cb0..0x4b6cca): show score card only when LevelSaveInfo::completed != 0
        const uint8_t exLsCard[26] = {
            0x8b, 0x42, 0x38,
            0x85, 0xc0,
            0x78, 0x13,
            0x3b, 0x42, 0x18,
            0x7d, 0x0e,
            0x8b, 0x5a, 0x4c,
            0x8a, 0x04, 0x43,
            0x88, 0x85, 0x73, 0xff, 0xff, 0xff,
            0xeb, 0x07
        };
        const uint8_t rpLsCard[26] = {
            0x8b, 0x42, 0x40,                   // mov eax, [edx+40h] (LevelSaveInfo*)
            0x85, 0xc0,                         // test eax, eax
            0x74, 0x13,                         // je 0x4b6cca (0x4b6cb7 + 0x13 = 0x4b6cca)
            0x8a, 0x00,                         // mov al, byte ptr [eax] (completed)
            0x88, 0x85, 0x73, 0xff, 0xff, 0xff, // mov [ebp-8Dh], al
            0xeb, 0x10,                         // jmp 0x4b6cd1 (0x4b6cc1 + 0x10 = 0x4b6cd1)
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
        };
        RegBytes(0x4b6cb0, exLsCard, rpLsCard, 26);

        // LevelSelect::render (0x4b824d): wrap inner 30x30 quad draw to add bottom-right #BFBFBF diagonal triangle for unlocked-unplayed tiles (flag == 1)
        const uint8_t exLsInner[5] = {0xe8, 0x1e, 0x04, 0xfd, 0xff};
        RegRelCall(0x4b824d, exLsInner, 5, 5, &hk_lsTileInnerQuad);

        // ModeSelect::getRowStringId case 0 (0x4b8b02..0x4b8b14): show "new game" when no level completed, "continue" otherwise
        const uint8_t exMsRow0[18] = {
            0x0f, 0xb6, 0x41, 0x14, 0xf7, 0xd8, 0x1b, 0xc0,
            0x83, 0xe0, 0xfb, 0x83, 0xc0, 0x07, 0x5d, 0xc2, 0x04, 0x00
        };
        RegRelJmp(0x4b8b02, exMsRow0, 18, 5, &hk_modeSelectRow0StrId, 0x90, 0x4b8b14);
    }

    // 9. Split Effects (Prism VFX, Chromatic Aberration, Anti-Aliasing) & Remove Pause/Debrief Blur
    {
        static const uint8_t exCubeFxUpd[12] = {
            0x55, 0x8b, 0xec, 0x83, 0xec, 0x1c, 0x53, 0x56, 0x57, 0x8b, 0xf1, 0xe8
        };
        RegRelJmp(0x402ec0, exCubeFxUpd, 12, 5, &hk_cubeFxUpdate, 0x90, 0x403004);

        static const uint8_t exCubeFxTrig[12] = {
            0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x83, 0xe8, 0x00, 0x57, 0x8b, 0xf9
        };
        RegRelJmp(0x403010, exCubeFxTrig, 12, 5, &hk_cubeFxTrigger, 0x90, 0x403078);

        static const uint8_t exNoBlur[16] = {
            0x6a, 0x3c, 0xe8, 0xad, 0x87, 0x03, 0x00, 0x83, 0xc4, 0x04, 0x5e, 0x85, 0xc0, 0x74, 0x15, 0x8b
        };
        RegRelJmp(0x481e8c, exNoBlur, 16, 5, &hk_createPostEffectsTail, 0x90, 0x481ebe);

        static const uint8_t exStartFx[16] = {
            0x8b, 0x3d, 0x58, 0x90, 0x5f, 0x00, 0x38, 0x9e, 0xb5, 0x02, 0x00, 0x00, 0x74, 0x05, 0x83, 0xc7
        };
        RegRelCall(0x482bb0, exStartFx, 16, 0x39, &hk_startupGraphics, 0x90);
    }

    // 10. Global Menu Widening (320px -> 368px leftward, preserving right edge & rainbow border)
    {
        // Shared menu width constant (.rdata 0x589ddc: 320 -> 368)
        static const uint8_t exMenuW[4] = {0x40, 0x01, 0x00, 0x00};
        static const uint8_t rpMenuW[4] = {0x70, 0x01, 0x00, 0x00};
        RegBytes(0x589ddc, exMenuW, rpMenuW, 4);

        // Shared base point, dark background strip, and rainbow border hooks
        static const uint8_t exBasePt[8] = {0x55, 0x8b, 0xec, 0xa1, 0x80, 0x90, 0x5f, 0x00};
        RegRelJmp(0x496030, exBasePt, 8, 5, reinterpret_cast<void*>(&GetMenuBasePt_Impl), 0x90);

        static const uint8_t exStrip[8] = {0x55, 0x8b, 0xec, 0x51, 0x8d, 0x45, 0xfc, 0x50};
        RegRelJmp(0x496070, exStrip, 8, 5, reinterpret_cast<void*>(&DrawMenuStrip_Impl), 0x90);

        static const uint8_t exBorder[8] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0xa1, 0x80};
        RegRelJmp(0x4960c0, exBorder, 8, 5, reinterpret_cast<void*>(&DrawMenuRainbowBorder_Impl), 0x90);

        auto patchPush32 = [](uint32_t va, uint8_t oldLo, uint8_t newLo) {
            uint8_t ex[5] = {0x68, oldLo, 0x01, 0x00, 0x00};
            uint8_t rp[5] = {0x68, newLo, 0x01, 0x00, 0x00};
            RegBytes(va, ex, rp, 5);
        };
        auto patchMovEbp32 = [](uint32_t va, int8_t disp, uint8_t oldLo, uint8_t newLo) {
            uint8_t ex[7] = {0xc7, 0x45, static_cast<uint8_t>(disp), oldLo, 0x01, 0x00, 0x00};
            uint8_t rp[7] = {0xc7, 0x45, static_cast<uint8_t>(disp), newLo, 0x01, 0x00, 0x00};
            RegBytes(va, ex, rp, 7);
        };

        // Header outline (0x13c -> 0x16c) & Instruction dividers (0x13c -> 0x16c)
        for (uint32_t va : {
            0x4b9d01u, 0x4b2d13u, 0x4b92d9u, 0x4b2046u, 0x4af9a3u,
            0x4b59efu, 0x4b7ec1u, 0x4b85c1u, 0x4b85f9u,
            0x4b40efu, 0x4b4654u, 0x4b467du, 0x4b47d0u, 0x4b47feu,
            0x4b494du, 0x4b497au, 0x4b4ac2u, 0x4b4aefu, 0x4b4c3du,
            0x4b4c6bu, 0x4b4dddu, 0x4b4e06u, 0x4b4fdau,
            0x4a914bu, 0x4ad318u, 0x4ae04au, 0x4ae073u
        }) {
            patchPush32(va, 0x3c, 0x6c);
        }

        // Header fill (0x138 -> 0x168)
        for (uint32_t va : {
            0x4b9d39u, 0x4b2d4bu, 0x4b9311u, 0x4b207eu, 0x4af9edu,
            0x4b5a2au, 0x4b7f02u, 0x4b4126u, 0x4a9186u
        }) {
            patchPush32(va, 0x38, 0x68);
        }

        // Full menu width pushes (0x140 -> 0x170)
        for (uint32_t va : {0x4afb36u, 0x4abd41u, 0x4ad296u, 0x4ae022u, 0x4ae0f6u, 0x4ae267u}) {
            patchPush32(va, 0x40, 0x70);
        }

        // Stack-slot width writes (header scissor, scroll rect, Pause P0 hit rect)
        patchMovEbp32(0x4b9d91u, -0x0c, 0x38, 0x68);
        patchMovEbp32(0x4afb00u, -0x0c, 0x40, 0x70);
        patchMovEbp32(0x4b4229u, -0x0c, 0x40, 0x70);
        patchMovEbp32(0x4ace74u, -0x10, 0x3c, 0x6c);

        // Debrief, Scores, and Instructions 0x140 -> 0x170 adjustments
        {
            const uint8_t exDebHit[6] = {0x8d, 0x8b, 0x40, 0x01, 0x00, 0x00};
            const uint8_t rpDebHit[6] = {0x8d, 0x8b, 0x70, 0x01, 0x00, 0x00};
            RegBytes(0x4a95b4, exDebHit, rpDebHit, 6);

            const uint8_t exDebTxt[5] = {0x05, 0x40, 0x01, 0x00, 0x00};
            const uint8_t rpDebTxt[5] = {0x05, 0x70, 0x01, 0x00, 0x00};
            RegBytes(0x4a9de2, exDebTxt, rpDebTxt, 5);

            const uint8_t exScTouch[6] = {0x81, 0xc3, 0x40, 0x01, 0x00, 0x00};
            const uint8_t rpScTouch[6] = {0x81, 0xc3, 0x70, 0x01, 0x00, 0x00};
            RegBytes(0x4af356, exScTouch, rpScTouch, 6);
            RegBytes(0x4b3942, exScTouch, rpScTouch, 6);
        }

        // Pause Page 2 pane shift (+0x30) & centered Yes/No boxes (+0x18 within pane)
        {
            auto patchAddEax = [](uint32_t va, uint8_t oldLo, uint8_t oldHi, uint8_t newLo, uint8_t newHi) {
                uint8_t ex[5] = {0x05, oldLo, oldHi, 0x00, 0x00};
                uint8_t rp[5] = {0x05, newLo, newHi, 0x00, 0x00};
                RegBytes(va, ex, rp, 5);
            };
            auto patchAddReg = [](uint32_t va, uint8_t modrm, uint8_t oldLo, uint8_t oldHi, uint8_t newLo, uint8_t newHi) {
                uint8_t ex[6] = {0x81, modrm, oldLo, oldHi, 0x00, 0x00};
                uint8_t rp[6] = {0x81, modrm, newLo, newHi, 0x00, 0x00};
                RegBytes(va, ex, rp, 6);
            };
            patchAddEax(0x4ae02c, 0x40, 0x01, 0x70, 0x01);
            patchAddReg(0x4ae054, 0xc1, 0x42, 0x01, 0x72, 0x01);
            patchAddReg(0x4ae07d, 0xc2, 0x42, 0x01, 0x72, 0x01);
            patchAddEax(0x4ae0bb, 0x58, 0x01, 0x88, 0x01);
            patchAddEax(0x4ae100, 0x40, 0x01, 0x70, 0x01);
            patchAddReg(0x4ae140, 0xc2, 0x4c, 0x01, 0x94, 0x01);
            patchAddReg(0x4ae171, 0xc2, 0xf8, 0x01, 0x40, 0x02);
            patchAddEax(0x4ae1ca, 0x87, 0x01, 0xcf, 0x01);
            patchAddEax(0x4ae224, 0x33, 0x02, 0x7b, 0x02);

            const uint8_t exHitY[3] = {0x83, 0xc1, 0x0c};
            const uint8_t rpHitY[3] = {0x83, 0xc1, 0x24};
            RegBytes(0x4ac87e, exHitY, rpHitY, 3);
            patchAddReg(0x4ac8cc, 0xc1, 0xb8, 0x00, 0xd0, 0x00);

            static const uint8_t exPausePrism[13] = {
                0x89, 0x45, 0xa4, 0x66, 0x3b, 0x48, 0x7c, 0x0f, 0x8d, 0x9e, 0x00, 0x00, 0x00
            };
            RegRelJmp(0x4ae367, exPausePrism, 13, 5, &hk_pausePrismGridOrCount, 0x90, 0x4ae374);

            static const uint8_t exPauseHdrStart[12] = {
                0x8b, 0x4d, 0xbc, 0x8b, 0x55, 0xb8, 0x8d, 0x45, 0xc0, 0x50, 0x6a, 0x37
            };
            RegRelJmp(0x4ae25b, exPauseHdrStart, 12, 5, &hk_pauseHdrResetSlideY, 0x90, 0x4ae267);

            static const uint8_t exP0HitY[5] = {0xbe, 0x55, 0x00, 0x00, 0x00};
            RegRelJmp(0x4ace5d, exP0HitY, 5, 5, &hk_pauseP0HitY, 0x90, 0x4ace62);

            static const uint8_t exP2HitY[6] = {0x81, 0xc2, 0x81, 0x00, 0x00, 0x00};
            RegRelJmp(0x4ac884, exP2HitY, 6, 5, &hk_pauseP2HitY, 0x90, 0x4ac88a);

            static const uint8_t exLsPrism[20] = {
                0x8b, 0x47, 0x3c, 0x33, 0xc9, 0x33, 0xd2, 0x89, 0x4d, 0xb0,
                0x66, 0x3b, 0x50, 0x70, 0x0f, 0x8d, 0xca, 0x00, 0x00, 0x00
            };
            RegRelJmp(0x4b869c, exLsPrism, 20, 5, &hk_lsPrismGridOrCount, 0x90, 0x4b86b0);

            static const uint8_t exScoresPrism[20] = {
                0x83, 0x78, 0x78, 0x00, 0xc7, 0x45, 0x9c, 0x00, 0x00, 0x00,
                0x00, 0x89, 0x45, 0xac, 0x0f, 0x8e, 0x22, 0x03, 0x00, 0x00
            };
            RegRelJmp(0x4b0991, exScoresPrism, 20, 5, &hk_scoresPrismGridOrCount, 0x90, 0x4b09a5);

            // Pause P0 parent slots match Options tab slot size (0x26 stride, 0x25 fill, +0x6e text Y)
            {
                static const uint8_t exP0BoxH[5] = {0x68, 0xa8, 0x00, 0x00, 0x00};
                static const uint8_t rpP0BoxH[5] = {0x68, 0xc1, 0x00, 0x00, 0x00};
                RegBytes(0x4ad291, exP0BoxH, rpP0BoxH, 5);

                static const uint8_t exP0Mul[7] = {0x8b, 0xc7, 0xc1, 0xe0, 0x05, 0x03, 0xc7};
                static const uint8_t rpP0Mul[7] = {0x6b, 0xc7, 0x26, 0x90, 0x90, 0x90, 0x90};
                RegBytes(0x4ad2b9, exP0Mul, rpP0Mul, 7);

                static const uint8_t exP0FillH[2] = {0x6a, 0x20};
                static const uint8_t rpP0FillH[2] = {0x6a, 0x25};
                RegBytes(0x4ad310, exP0FillH, rpP0FillH, 2);

                static const uint8_t exP0TxtY[4] = {0x8d, 0x54, 0x31, 0x6b};
                static const uint8_t rpP0TxtY[4] = {0x8d, 0x54, 0x31, 0x6e};
                RegBytes(0x4ad403, exP0TxtY, rpP0TxtY, 4);

                static const uint8_t exP0HitStride[7] = {0xc7, 0x45, 0xfc, 0x21, 0x00, 0x00, 0x00};
                static const uint8_t rpP0HitStride[7] = {0xc7, 0x45, 0xfc, 0x26, 0x00, 0x00, 0x00};
                RegBytes(0x4ace6a, exP0HitStride, rpP0HitStride, 7);

                static const uint8_t exP0HitH[7] = {0xc7, 0x45, 0xf4, 0x20, 0x00, 0x00, 0x00};
                static const uint8_t rpP0HitH[7] = {0xc7, 0x45, 0xf4, 0x25, 0x00, 0x00, 0x00};
                RegBytes(0x4ace7b, exP0HitH, rpP0HitH, 7);
            }
        }

        // Alignment fixes for widened (320 -> 368, +0x30) sibling & debrief menus
        {
            auto patchOp1Imm8 = [](uint32_t va, uint8_t op, uint8_t modrm, uint8_t oldV, uint8_t newV) {
                uint8_t ex[3] = {op, modrm, oldV};
                uint8_t rp[3] = {op, modrm, newV};
                RegBytes(va, ex, rp, 3);
            };
            auto patchAddImm32 = [](uint32_t va, uint8_t modrm, uint32_t oldV, uint32_t newV) {
                uint8_t ex[6] = {0x81, modrm,
                                 static_cast<uint8_t>(oldV), static_cast<uint8_t>(oldV >> 8),
                                 static_cast<uint8_t>(oldV >> 16), static_cast<uint8_t>(oldV >> 24)};
                uint8_t rp[6] = {0x81, modrm,
                                 static_cast<uint8_t>(newV), static_cast<uint8_t>(newV >> 8),
                                 static_cast<uint8_t>(newV >> 16), static_cast<uint8_t>(newV >> 24)};
                RegBytes(va, ex, rp, 6);
            };
            auto patchAddEaxImm32 = [](uint32_t va, uint32_t oldV, uint32_t newV) {
                uint8_t ex[5] = {0x05, static_cast<uint8_t>(oldV), static_cast<uint8_t>(oldV >> 8),
                                 static_cast<uint8_t>(oldV >> 16), static_cast<uint8_t>(oldV >> 24)};
                uint8_t rp[5] = {0x05, static_cast<uint8_t>(newV), static_cast<uint8_t>(newV >> 8),
                                 static_cast<uint8_t>(newV >> 16), static_cast<uint8_t>(newV >> 24)};
                RegBytes(va, ex, rp, 5);
            };
            auto patchLeaImm32 = [](uint32_t va, uint8_t modrm, uint32_t oldV, uint32_t newV) {
                uint8_t ex[6] = {0x8d, modrm,
                                 static_cast<uint8_t>(oldV), static_cast<uint8_t>(oldV >> 8),
                                 static_cast<uint8_t>(oldV >> 16), static_cast<uint8_t>(oldV >> 24)};
                uint8_t rp[6] = {0x8d, modrm,
                                 static_cast<uint8_t>(newV), static_cast<uint8_t>(newV >> 8),
                                 static_cast<uint8_t>(newV >> 16), static_cast<uint8_t>(newV >> 24)};
                RegBytes(va, ex, rp, 6);
            };
            auto patchLeaSibImm32 = [](uint32_t va, uint8_t modrm, uint8_t sib, uint32_t oldV, uint32_t newV) {
                uint8_t ex[7] = {0x8d, modrm, sib,
                                 static_cast<uint8_t>(oldV), static_cast<uint8_t>(oldV >> 8),
                                 static_cast<uint8_t>(oldV >> 16), static_cast<uint8_t>(oldV >> 24)};
                uint8_t rp[7] = {0x8d, modrm, sib,
                                 static_cast<uint8_t>(newV), static_cast<uint8_t>(newV >> 8),
                                 static_cast<uint8_t>(newV >> 16), static_cast<uint8_t>(newV >> 24)};
                RegBytes(va, ex, rp, 7);
            };
            auto patchLeaSibImm8 = [](uint32_t va, uint8_t modrm, uint8_t sib, uint8_t oldV, uint8_t newV) {
                uint8_t ex[4] = {0x8d, modrm, sib, oldV};
                uint8_t rp[4] = {0x8d, modrm, sib, newV};
                RegBytes(va, ex, rp, 4);
            };

            struct ArrowVtx { uint32_t va; uint8_t op; uint8_t modrm; uint32_t oldV; };
            static constexpr ArrowVtx kArrowTable[] = {
                // ScoresMenu scroll arrows (+0x18)
                {0x4b0dfe, 0x8d, 0x90, 0x87}, {0x4b0e1a, 0x8d, 0x90, 0xb9}, {0x4b0e3a, 0x05, 0x00, 0xa0},
                {0x4b0e91, 0x8d, 0x90, 0x8c}, {0x4b0ead, 0x8d, 0x90, 0xb4}, {0x4b0ecd, 0x05, 0x00, 0xa0},
                {0x4b0f1b, 0x8d, 0x90, 0x94}, {0x4b0f37, 0x8d, 0x90, 0xac}, {0x4b0f57, 0x05, 0x00, 0xa0},
                {0x4b0fbb, 0x8d, 0x88, 0xa0}, {0x4b0fdb, 0x8d, 0x90, 0xb9}, {0x4b0ff4, 0x05, 0x00, 0x87},
                {0x4b1054, 0x8d, 0x90, 0xa0}, {0x4b106d, 0x8d, 0x88, 0xb4}, {0x4b1086, 0x05, 0x00, 0x8c},
                {0x4b10dd, 0x8d, 0x90, 0xa0}, {0x4b10f6, 0x8d, 0x88, 0xac}, {0x4b110f, 0x05, 0x00, 0x94},
                // InstructionsMenu scroll arrows (+0x18)
                {0x4b5110, 0x8d, 0x90, 0x87}, {0x4b512c, 0x8d, 0x90, 0xb9}, {0x4b514c, 0x05, 0x00, 0xa0},
                {0x4b519f, 0x8d, 0x90, 0x8c}, {0x4b51bb, 0x8d, 0x90, 0xb4}, {0x4b51db, 0x05, 0x00, 0xa0},
                {0x4b522a, 0x8d, 0x90, 0x94}, {0x4b5246, 0x8d, 0x90, 0xac}, {0x4b5266, 0x05, 0x00, 0xa0},
                {0x4b52ca, 0x8d, 0x88, 0xa0}, {0x4b52ea, 0x8d, 0x90, 0xb9}, {0x4b5303, 0x05, 0x00, 0x87},
                {0x4b535f, 0x8d, 0x90, 0xa0}, {0x4b5378, 0x8d, 0x88, 0xb4}, {0x4b5391, 0x05, 0x00, 0x8c},
                {0x4b53e9, 0x8d, 0x88, 0xa0}, {0x4b5402, 0x8d, 0x90, 0xac}, {0x4b541b, 0x05, 0x00, 0x94},
            };
            for (const auto& v : kArrowTable) {
                if (v.op == 0x05) patchAddEaxImm32(v.va, v.oldV, v.oldV + 0x18u);
                else              patchLeaImm32(v.va, v.modrm, v.oldV, v.oldV + 0x18u);
            }

            // ScoresMenu: arrow hitTests (+0x18), symmetric 3-col cards (margin 0x1a, stride 0x72), cube card (+0x0a)
            patchAddImm32(0x4af243, 0xc1, 0x82, 0x9a);
            patchAddEaxImm32(0x4af2a3, 0x82, 0x9a);
            patchAddImm32(0x4af48d, 0xc1, 0x82, 0x9a);
            patchAddEaxImm32(0x4af4ca, 0x82, 0x9a);
            patchOp1Imm8(0x4afb78, 0x83, 0xc2, 0x10, 0x1a);
            patchOp1Imm8(0x4afba0, 0x83, 0xc2, 0x12, 0x1c);
            patchOp1Imm8(0x4afbe1, 0x8d, 0x51, 0x50, 0x5a);
            patchOp1Imm8(0x4afbfa, 0x83, 0xc1, 0x29, 0x33);
            patchOp1Imm8(0x4afc5a, 0x83, 0xc1, 0x50, 0x5a);
            patchOp1Imm8(0x4afc92, 0x83, 0xc0, 0x10, 0x1a);
            patchLeaSibImm8(0x4afcf8, 0x54, 0x02, 0xf9, 0x03);
            patchOp1Imm8(0x4afd57, 0x83, 0xc0, 0x29, 0x33);
            patchOp1Imm8(0x4afd84, 0x83, 0xc0, 0x3e, 0x48);
            patchOp1Imm8(0x4afe7c, 0x6b, 0xf6, 0x64, 0x72);
            patchOp1Imm8(0x4afe7f, 0x83, 0xc6, 0x10, 0x1a);

            // LevelSelect: centered 6-col grid (box x=0x26..+0x124 [38..330], pitchX=pitchY=0x2d, tile0=0x36), right-centered prisms (x=0x77, y centered for 1 row), centered scrollbar in right margin
            {
                static const uint8_t exTotH[22] = {0x83, 0xc1, 0x04, 0xb8, 0x67, 0x66, 0x66, 0x66, 0xf7, 0xe9, 0xd1, 0xfa, 0x8b, 0xfa, 0xc1, 0xef, 0x1f, 0x03, 0xfa, 0x6b, 0xff, 0x2d};
                RegRelJmp(0x4b61e9, exTotH, 22, 5, &hk_lsScrollTotalH, 0x90, 0x4b61ff);

                static const uint8_t exSelY[22] = {0xb8, 0x67, 0x66, 0x66, 0x66, 0xf7, 0x6e, 0x38, 0xd1, 0xfa, 0x8b, 0xca, 0xc1, 0xe9, 0x1f, 0x03, 0xca, 0x8b, 0xc1, 0x6b, 0xc0, 0x2d};
                RegRelJmp(0x4b6207, exSelY, 22, 5, &hk_lsScrollSelY, 0x90, 0x4b621d);

                static const uint8_t exMaxH[22] = {0x83, 0xc1, 0x04, 0xb8, 0x67, 0x66, 0x66, 0x66, 0xf7, 0xe9, 0xd1, 0xfa, 0x8b, 0xfa, 0xc1, 0xef, 0x1f, 0x03, 0xfa, 0x6b, 0xff, 0x2d};
                RegRelJmp(0x4b6482, exMaxH, 22, 5, &hk_lsScrollbarMaxH, 0x90, 0x4b6498);

                static const uint8_t exHit[48] = {
                    0xb8, 0x67, 0x66, 0x66, 0x66, 0xf7, 0xee, 0xd1, 0xfa, 0x8b, 0xc2, 0xc1, 0xe8, 0x1f, 0x03, 0xc2,
                    0x8b, 0xd0, 0x6b, 0xd2, 0x2d, 0x2b, 0x57, 0x50, 0x8d, 0x04, 0x80, 0x8d, 0x94, 0x0a, 0xac, 0x00,
                    0x00, 0x00, 0x8b, 0xce, 0x2b, 0xc8, 0x8b, 0x45, 0xf8, 0x6b, 0xc9, 0x2d, 0x8d, 0x44, 0x01, 0x31
                };
                RegRelJmp(0x4b6672, exHit, 48, 5, &hk_lsHitTestTileXY, 0x90, 0x4b66a2);

                static const uint8_t exRen[49] = {
                    0xb8, 0x67, 0x66, 0x66, 0x66, 0xf7, 0xeb, 0xd1, 0xfa, 0x8b, 0xc2, 0xc1, 0xe8, 0x1f, 0x03, 0xc2,
                    0x8d, 0x0c, 0x80, 0x6b, 0xc0, 0x2d, 0x2b, 0x47, 0x50, 0x8b, 0xd3, 0x2b, 0xd1, 0x8b, 0x4d, 0xb4,
                    0x6b, 0xd2, 0x2d, 0x8d, 0x4c, 0x0a, 0x2f, 0x8b, 0x55, 0xb8, 0x8d, 0x84, 0x10, 0xaa, 0x00, 0x00, 0x00
                };
                RegRelJmp(0x4b8090, exRen, 49, 5, &hk_lsRenderTileXY, 0x90, 0x4b80c1);

                static const uint8_t exPrismY[7] = {0x8d, 0x44, 0x11, 0x6d, 0x8b, 0x4d, 0xb4};
                RegRelJmp(0x4b8743, exPrismY, 7, 5, &hk_lsPrismDotY, 0x90, 0x4b874a);

                static const uint8_t exBoxW[5] = {0x68, 0xf3, 0x00, 0x00, 0x00};
                static const uint8_t rpBoxW[5] = {0x68, 0x24, 0x01, 0x00, 0x00};
                RegBytes(0x4b801f, exBoxW, rpBoxW, 5);

                static const uint8_t exSciW[7] = {0xc7, 0x45, 0xf4, 0xf3, 0x00, 0x00, 0x00};
                static const uint8_t rpSciW[7] = {0xc7, 0x45, 0xf4, 0x24, 0x01, 0x00, 0x00};
                RegBytes(0x4b805a, exSciW, rpSciW, 7);
            }
            patchOp1Imm8(0x4b6348, 0x83, 0xf8, 0x05, 0x06);
            patchOp1Imm8(0x4b6351, 0x83, 0xc0, 0xfb, 0xfa);
            patchOp1Imm8(0x4b637c, 0x83, 0xe8, 0x05, 0x06);
            patchOp1Imm8(0x4b6398, 0x83, 0xe9, 0x05, 0x06);
            patchOp1Imm8(0x4b63a3, 0x83, 0xc0, 0x05, 0x06);
            patchOp1Imm8(0x4b63c8, 0x83, 0xc2, 0x05, 0x06);
            patchAddEaxImm32(0x4b67aa, 0xca, 0xfa);
            patchLeaImm32(0x4b6894, 0xb9, 0x115, 0x14a);
            patchOp1Imm8(0x4b802b, 0x83, 0xc2, 0x22, 0x26);
            patchOp1Imm8(0x4b803e, 0x83, 0xc0, 0x22, 0x26);
            patchAddImm32(0x4b8435, 0xc1, 0x126, 0x157);
            patchAddImm32(0x4b846f, 0xc2, 0x122, 0x153);
            patchAddImm32(0x4b84be, 0xc1, 0x122, 0x153);
            patchOp1Imm8(0x4b86df, 0x83, 0xc2, 0x37, 0x7f);
            patchLeaSibImm8(0x4b8753, 0x44, 0x0a, 0x2f, 0x77);
            patchAddImm32(0x4b88f5, 0xc1, 0xcf, 0xff);

            // InstructionsMenu: scroll arrow hitTests (+0x18), center 316px images (0x2 -> 0x1a) & "10.14" (0xdc -> 0xf4)
            patchLeaImm32(0x4b3a2d, 0x8b, 0x82, 0x9a);
            patchLeaImm32(0x4b3cb7, 0x8b, 0x82, 0x9a);
            patchAddEaxImm32(0x4b3d04, 0x82, 0x9a);
            patchOp1Imm8(0x4b4364, 0x83, 0xc1, 0x02, 0x1a);
            patchOp1Imm8(0x4b4459, 0x83, 0xc2, 0x02, 0x1a);
            patchOp1Imm8(0x4b4547, 0x83, 0xc2, 0x02, 0x1a);
            patchAddImm32(0x4b4569, 0xc2, 0xdc, 0xf4);

            // DebriefMenu: right-aligned times, rank card & text (+0x30)
            patchAddEaxImm32(0x4aa1b3, 0x13a, 0x16a);
            patchAddEaxImm32(0x4aa667, 0x13a, 0x16a);
            patchAddImm32(0x4aa9ef, 0xc2, 0x13a, 0x16a);
            patchAddEaxImm32(0x4ab0a1, 0x9c, 0xcc);
            patchAddEaxImm32(0x4ab100, 0x9d, 0xcd);
            patchAddImm32(0x4ab179, 0xc1, 0xe2, 0x112);
            patchAddEaxImm32(0x4ab31d, 0xe2, 0x112);
            patchAddImm32(0x4ab4d1, 0xc2, 0xdc, 0x10c);
            patchLeaSibImm32(0x4ab540, 0x84, 0x10, 0xdc, 0x10c);
            patchAddImm32(0x4ab580, 0xc2, 0xe2, 0x112);

            // ScoresSelect (+0x30 right values), ExitConfirm (prompt +0x18, right btn +0x30), Credits back (+0x30), ConfirmDialog (+0x18)
            patchAddImm32(0x4b3026, 0xc1, 0xe6, 0x116);
            patchAddImm32(0x4b31ad, 0xc1, 0xe6, 0x116);
            patchAddImm32(0x4b330c, 0xc2, 0xe6, 0x116);
            patchAddImm32(0x4b3448, 0xc2, 0xe6, 0x116);
            patchAddImm32(0x4b359d, 0xc1, 0xe6, 0x116);
            patchAddEaxImm32(0x4b56fb, 0xca, 0xfa);
            patchAddImm32(0x4b5b96, 0xc1, 0xa0, 0xb8);
            patchAddImm32(0x4b5d3a, 0xc1, 0xcf, 0xff);
            patchAddEaxImm32(0x4a16e7, 0xca, 0xfa);
            patchAddEaxImm32(0x4a1767, 0xca, 0xfa);
            patchAddEaxImm32(0x4a18a4, 0xca, 0xfa);
            patchAddImm32(0x4a1eea, 0xc1, 0xcf, 0xff);
            patchOp1Imm8(0x4a8b72, 0x8d, 0x4f, 0xf8, 0x10);
            patchLeaImm32(0x4a8bc2, 0x8f, 0xa4, 0xbc);
            patchOp1Imm8(0x4a92a8, 0x83, 0xc2, 0x0c, 0x24);
            patchOp1Imm8(0x4a9352, 0x83, 0xc0, 0x47, 0x5f);
            patchAddImm32(0x4a93cd, 0xc1, 0xb8, 0xd0);
            patchAddImm32(0x4a9476, 0xc2, 0xf3, 0x10b);
        }
    }

    // 11. Menu text auto-fit when overflow or column overlap is detected
    {
        struct AutoFitSite { uint32_t va; uint8_t exp[5]; void* fn; };
        const AutoFitSite sites[21] = {
            {0x4b9dc6, {0xe8, 0xb5, 0xce, 0xfe, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<324>)},
            {0x4b9fbe, {0xe8, 0xbd, 0xcc, 0xfe, 0xff}, reinterpret_cast<void*>(&hk_mainMenuDrawRowText)},
            {0x4b93dc, {0xe8, 0x9f, 0xd8, 0xfe, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<330>)},
            {0x4b9615, {0xe8, 0x66, 0xd6, 0xfe, 0xff}, reinterpret_cast<void*>(&hk_modeSelectDrawRowText)},
            {0x4b214c, {0xe8, 0x2f, 0x4b, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<330>)},
            {0x4b2357, {0xe8, 0x24, 0x49, 0xff, 0xff}, reinterpret_cast<void*>(&hk_scoresSelectDrawRowText)},
            {0x4a936d, {0xe8, 0x0e, 0xd9, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<106>)},
            {0x4a948f, {0xe8, 0xec, 0xd7, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<106>)},
            {0x4b7fcc, {0xe8, 0xaf, 0xec, 0xfe, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<330>)},
            {0x4b8659, {0xe8, 0x22, 0xe6, 0xfe, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<256>)},
            {0x4ad41d, {0xe8, 0x5e, 0x98, 0xff, 0xff}, reinterpret_cast<void*>(&hk_pauseP0DrawRowText)},
            {0x4ae0d0, {0xe8, 0xab, 0x8b, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<334>)},
            {0x4ae1df, {0xe8, 0x9c, 0x8a, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<106>)},
            {0x4ae239, {0xe8, 0x42, 0x8a, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<106>)},
            {0x496274, {0xe8, 0x07, 0x0a, 0x01, 0x00}, reinterpret_cast<void*>(&hk_drawButtonText)},
            {0x4a9fa2, {0xe8, 0xd9, 0xcc, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<286>)},
            {0x4aa358, {0xe8, 0x23, 0xc9, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<286>)},
            {0x4abe08, {0xe8, 0x73, 0xae, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<338>)},
            {0x4afabc, {0xe8, 0xbf, 0x71, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<330>)},
            {0x4b41ec, {0xe8, 0x8f, 0x2a, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<330>)},
            {0x4b5aeb, {0xe8, 0x90, 0x11, 0xff, 0xff}, reinterpret_cast<void*>(&DrawTextAutoFitConst<330>)},
        };
        for (const auto& s : sites) {
            RegRelCall(s.va, s.exp, 5, 5, s.fn);
        }
    }
}
