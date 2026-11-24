#include "mod_state.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
uint8_t g_prismVfx    = 1;
uint8_t g_chromaAb    = 1;
int32_t g_aaSamples   = -1; // sentinel until loaded/migrated
uint8_t g_finishEffect   = 0; // 0=pc, 1=mobile, 2=java
uint8_t g_finishPlatform = 0; // 0=pc, 1=mobile
uint8_t g_timerMode   = 3;    // default: both
float   g_timerSize   = 1.0f; // default: 1x
uint8_t g_timerPos    = 1;    // default: TR (top right)
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

int __cdecl DrawTextAutoFit(
    const EdgeWString* ws, int x, int y, int scaleX, int scaleY,
    int extraX, int extraY, int alignFlags, int maxWidth
) {
    if (ws == nullptr || ws->size == 0 || maxWidth <= 0 || scaleX <= 0 || scaleY <= 0) {
        return Game_DrawText(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags);
    }

    const uint8_t* mapHead     = *reinterpret_cast<const uint8_t* const*>(0x5f9214);
    const uint8_t* glyphWidths = *reinterpret_cast<const uint8_t* const*>(0x5f91f4);
    int16_t numGlyphs          = *reinterpret_cast<const int16_t*>(0x5f91ec);
    uint8_t spaceW             = *reinterpret_cast<const uint8_t*>(0x5f91e8);
    uint8_t glyphH             = *reinterpret_cast<const uint8_t*>(0x5f91e9);
    const wchar_t* s           = (ws->cap >= 8) ? ws->ptr : ws->buf;
    if (mapHead == nullptr || glyphWidths == nullptr || numGlyphs <= 0 || glyphH == 0 || s == nullptr) {
        return Game_DrawText(ws, x, y, scaleX, scaleY, extraX, extraY, alignFlags);
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

    if (origW <= maxWidth) {
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

} // namespace edgemod

namespace {

using namespace edgemod;

constexpr int kMaxOptRows = 16;

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
EDGE_STATIC_WSTR(g_wstrFinishEffect,   "finish effect");
EDGE_STATIC_WSTR(g_wstrFinishPlatform, "finish platform");
EDGE_STATIC_WSTR(g_wstrColorTheme,     "color theme");
EDGE_STATIC_WSTR(g_wstrDef,          "def");
EDGE_STATIC_WSTR(g_wstrNew,          "new");
EDGE_STATIC_WSTR(g_wstrDot,          ".");

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
    OPT_CAT_AUDIO,
    OPT_CAT_UI,
    OPT_CAT_TIMER,
    OPT_CAT_GAMEPLAY
};

enum OptItem : int {
    OPT_NONE = 0,
    OPT_CAT_BTN_DISPLAY,
    OPT_CAT_BTN_GRAPHICS,
    OPT_CAT_BTN_AUDIO,
    OPT_CAT_BTN_UI,
    OPT_CAT_BTN_TIMER,
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
    OPT_SOUNDS,
    OPT_MUSIC,
    OPT_MUSIC_START,
    OPT_MUSIC_PLAYLIST,
    OPT_UP_KEY,
    OPT_SKIP_INTRO,
    OPT_TIME_FMT,
    OPT_TIMER,
    OPT_TIMER_SIZE,
    OPT_TIMER_POS,
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

    bool    dragActive;
    bool    dragInPause;
    OptItem dragItem;
    int     dragRow;
    int     dragTrackX;
    int     dragTrackW;
    bool    suppressNextClickAction;

    bool    uiScalePending;
    float   uiScalePreview;

    int     mainSelRow;
    int     lastMouseX;
    int     lastMouseY;
    bool    prevResetKeyDown;
};

OptUiState g_optUi{};

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
    if (!reinterpret_cast<bool(__thiscall*)(void*, const char*)>(0x51c660)(saveObj, key)) return false;
    void* var = reinterpret_cast<void*(__thiscall*)(void*, const char*)>(0x51c540)(saveObj, key);
    if (!reinterpret_cast<bool(__thiscall*)(void*, int)>(0x51b620)(var, 3)) return false;
    *outVal = reinterpret_cast<double(__thiscall*)(void*)>(0x51b3f0)(var);
    return true;
}

static void SaveTtsavDouble(void* saveObj, void* varTmp, const char* key, double val) {
    reinterpret_cast<void*(__thiscall*)(void*, double)>(0x51b290)(varTmp, val);
    void* slot = reinterpret_cast<void*(__thiscall*)(void*, const char*)>(0x51db80)(saveObj, key);
    reinterpret_cast<void*(__thiscall*)(void*, void*)>(0x51d1b0)(slot, varTmp);
    reinterpret_cast<void(__thiscall*)(void*)>(0x51c800)(varTmp);
}

static void LoadOneCamTtsav(void* saveObj, const CamIniDesc& d) {
    *d.gVal = d.defFlt;
    double dv = 0.0;
    auto isValid = [&](float x) {
        if (std::isnan(x)) return false;
        if (x == d.s1 || x == d.s2) return true;
        return x >= d.minFlt && x <= d.maxFlt;
    };
    if (ReadTtsavDouble(saveObj, d.savKey, &dv)) {
        float fv = static_cast<float>(dv);
        if (isValid(fv)) *d.gVal = fv;
    }
    *d.gLastIni = d.unsetFlt;
    if (ReadTtsavDouble(saveObj, d.savIniKey, &dv)) {
        float fv = static_cast<float>(dv);
        if (isValid(fv)) *d.gLastIni = fv;
    }
    if (*d.gIni != d.unsetFlt) {
        if (std::fabs(*d.gIni - *d.gLastIni) > 0.0005f) {
            *d.gLastIni = *d.gIni;
            *d.gVal = *d.gIni;
        }
    } else {
        *d.gLastIni = d.unsetFlt;
    }
}

static int GetLegacyDefaultAa(uint8_t legacyFx) {
    const uint8_t* cfg = *reinterpret_cast<const uint8_t* const*>(0x5f9058);
    if (cfg != nullptr) {
        int v = *reinterpret_cast<const int*>(cfg + (legacyFx ? 0x10 : 0x18));
        if (v >= 0 && v <= 16) return v;
    }
    return legacyFx ? 8 : 2;
}

static uint8_t __cdecl LoadTtsav_Impl(void* saveObj, uint8_t* userSettings, const char* nextKey) {
    // ui_scale
    if (g_uiScale == 0.0f) g_uiScale = 1.0f;
    double dv = 0.0;
    if (ReadTtsavDouble(saveObj, "uiScaleF", &dv)) {
        float fv = static_cast<float>(dv);
        if (ClampUiScale(&fv)) g_uiScale = fv;
    }
    g_lastIniScale = 0.0f;
    if (ReadTtsavDouble(saveObj, "uiScaleFIni", &dv)) {
        g_lastIniScale = static_cast<float>(dv);
    }
    if (g_iniUiScale == 0.0f) {
        g_lastIniScale = 0.0f;
    } else if (std::fabs(g_iniUiScale - g_lastIniScale) > 0.0005f) {
        g_uiScale = g_iniUiScale;
        g_lastIniScale = g_iniUiScale;
    }

    // auto_scale
    g_autoScale = 1;
    if (ReadTtsavDouble(saveObj, "uiAuto", &dv)) {
        g_autoScale = (static_cast<int>(std::lrint(dv)) != 0) ? 1u : 0u;
    }
    g_lastIniAuto = -1;
    if (ReadTtsavDouble(saveObj, "uiAutoIni", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        if (iv == 0 || iv == 1) g_lastIniAuto = static_cast<int8_t>(iv);
    }
    if (g_iniAutoScale < 0) {
        g_lastIniAuto = -1;
    } else if (g_iniAutoScale != g_lastIniAuto) {
        g_autoScale = static_cast<uint32_t>(g_iniAutoScale);
        g_lastIniAuto = g_iniAutoScale;
    }

    LoadOneCamTtsav(saveObj, kDistDesc);
    LoadOneCamTtsav(saveObj, kFovDesc);

    g_timeFmt = 0;
    if (ReadTtsavDouble(saveObj, "timeFmt", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        if (iv == 0 || iv == 1) g_timeFmt = static_cast<uint8_t>(iv);
    }

    g_skipIntro = 0;
    if (ReadTtsavDouble(saveObj, "skipIntro", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        g_skipIntro = (iv != 0) ? 1u : 0u;
    }

    g_musicStart = 0;
    if (ReadTtsavDouble(saveObj, "musicStart", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        g_musicStart = (iv != 0) ? 1u : 0u;
    }

    g_musicPlaylist = 0;
    if (ReadTtsavDouble(saveObj, "musicPlaylist", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        g_musicPlaylist = (iv != 0) ? 1u : 0u;
    }
    EnsureMusicPlaylistReady();

    uint8_t legacyFx = userSettings[0x1d];
    g_prismVfx = legacyFx ? 1u : 0u;
    if (ReadTtsavDouble(saveObj, "prismVfx", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        if (iv >= 0 && iv <= 3) g_prismVfx = static_cast<uint8_t>(iv);
    }

    g_chromaAb = legacyFx ? 1u : 0u;
    if (ReadTtsavDouble(saveObj, "chromaAb", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        g_chromaAb = (iv != 0) ? 1u : 0u;
    }

    g_aaSamples = GetLegacyDefaultAa(legacyFx);
    if (ReadTtsavDouble(saveObj, "aaSamples", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        if (iv >= 0 && iv <= 16) g_aaSamples = iv;
    }

    g_finishEffect = 0;
    if (ReadTtsavDouble(saveObj, "finishEffect", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        if (iv >= 0 && iv <= 2) g_finishEffect = static_cast<uint8_t>(iv);
    }

    g_finishPlatform = 0;
    if (ReadTtsavDouble(saveObj, "finishPlatform", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        g_finishPlatform = (iv != 0) ? 1u : 0u;
    }

    g_colorTheme = 0;
    if (ReadTtsavDouble(saveObj, "colorTheme", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        g_colorTheme = (iv != 0) ? 1u : 0u;
    }
    OnColorThemeChanged();

    g_timerMode = 3;
    if (ReadTtsavDouble(saveObj, "timerMode", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        if (iv >= 0 && iv <= 3) g_timerMode = static_cast<uint8_t>(iv);
    }

    g_timerSize = 1.0f;
    if (ReadTtsavDouble(saveObj, "timerSize", &dv)) {
        float fv = static_cast<float>(dv);
        if (fv < 0.25f) fv = 0.25f;
        if (fv > 2.0f) fv = 2.0f;
        g_timerSize = g_timerSizePresets[NearestTimerSizeIdx(fv)];
    }

    g_timerPos = 1;
    if (ReadTtsavDouble(saveObj, "timerPos", &dv)) {
        int iv = static_cast<int>(std::lrint(dv));
        if (iv >= 0 && iv <= 3) g_timerPos = static_cast<uint8_t>(iv);
    }
    g_graphicsLoaded = 1;

    ApplyUiScale(userSettings - 0x298);
    return reinterpret_cast<uint8_t(__thiscall*)(void*, const char*)>(0x51c660)(saveObj, nextKey);
}

static void __declspec(naked) hk_loadTtsav() {
    // At 0x492558: stack has [esp+4] = nextKey (pushed at 0x492553), esi = saveObj, edi = userSettings
    __asm {
        push DWORD PTR [esp + 4]
        push edi
        push esi
        call LoadTtsav_Impl
        add esp, 12
        ret 4
    }
}

static void __cdecl SaveTtsav_Impl(void* varTmp, void* saveObj) {
    reinterpret_cast<void(__thiscall*)(void*)>(0x51c800)(varTmp);
    SaveTtsavDouble(saveObj, varTmp, "uiScaleF", static_cast<double>(GetEffectiveUiScale()));
    SaveTtsavDouble(saveObj, varTmp, "uiScaleFIni", static_cast<double>(g_lastIniScale));
    SaveTtsavDouble(saveObj, varTmp, "uiAuto", static_cast<double>(g_autoScale));
    SaveTtsavDouble(saveObj, varTmp, "uiAutoIni", static_cast<double>(g_lastIniAuto));
    SaveTtsavDouble(saveObj, varTmp, "camDist", static_cast<double>(g_camDist));
    SaveTtsavDouble(saveObj, varTmp, "camDistIni", static_cast<double>(g_lastIniDist));
    SaveTtsavDouble(saveObj, varTmp, "camFov", static_cast<double>(g_camFov));
    SaveTtsavDouble(saveObj, varTmp, "camFovIni", static_cast<double>(g_lastIniFov));
    SaveTtsavDouble(saveObj, varTmp, "timeFmt", static_cast<double>(g_timeFmt));
    SaveTtsavDouble(saveObj, varTmp, "skipIntro", static_cast<double>(g_skipIntro));
    SaveTtsavDouble(saveObj, varTmp, "musicStart", static_cast<double>(g_musicStart));
    SaveTtsavDouble(saveObj, varTmp, "musicPlaylist", static_cast<double>(g_musicPlaylist));
    SaveTtsavDouble(saveObj, varTmp, "prismVfx", static_cast<double>(g_prismVfx));
    SaveTtsavDouble(saveObj, varTmp, "chromaAb", static_cast<double>(g_chromaAb));
    SaveTtsavDouble(saveObj, varTmp, "aaSamples", static_cast<double>(g_aaSamples >= 0 ? g_aaSamples : 0));
    SaveTtsavDouble(saveObj, varTmp, "finishEffect", static_cast<double>(g_finishEffect));
    SaveTtsavDouble(saveObj, varTmp, "finishPlatform", static_cast<double>(g_finishPlatform));
    SaveTtsavDouble(saveObj, varTmp, "colorTheme", static_cast<double>(g_colorTheme));
    SaveTtsavDouble(saveObj, varTmp, "timerMode", static_cast<double>(g_timerMode));
    SaveTtsavDouble(saveObj, varTmp, "timerSize", static_cast<double>(g_timerSize));
    SaveTtsavDouble(saveObj, varTmp, "timerPos", static_cast<double>(g_timerPos));
}

static void __declspec(naked) hk_saveTtsav() {
    // At 0x4926e7: replaces `lea ecx, [ebp-0x50]; call 0x51c800`, edi = saveObj
    __asm {
        lea ecx, [ebp - 0x50]
        push edi
        push ecx
        call SaveTtsav_Impl
        add esp, 8
        ret
    }
}

// ------------------------------------------------------------------
// 2. Options actions & 12-entry unified jump table
// ------------------------------------------------------------------
static float CycleFloatPreset(const float* tbl, size_t count, float cur) {
    uint8_t* sm = GetScreenMgr();
    bool left = (sm != nullptr && sm[0xb3] != 0);
    for (size_t i = 0; i < count; ++i) {
        if (std::fabs(tbl[i] - cur) < 0.001f) {
            size_t next = left ? (i == 0 ? count - 1 : i - 1) : ((i + 1) % count);
            return tbl[next];
        }
    }
    if (!left) {
        for (size_t i = 0; i < count; ++i) {
            if (tbl[i] >= 0.0f && (cur + 0.001f) < tbl[i]) return tbl[i];
        }
        return tbl[0];
    }
    for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
        if (tbl[i] >= 0.0f && (cur - 0.001f) > tbl[i]) return tbl[i];
    }
    return tbl[0];
}

static void ApplyOptionScaleChange(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    if (sfxId > 0) Game_PlaySfx(sm, sfxId);
    Game_SaveUserSettings(sm + 0x298);
    ApplyUiScale(sm);
}

static void AutoScaleAction_Impl(int sfxId) {
    g_autoScale ^= 1u;
    ApplyOptionScaleChange(sfxId);
}

static void CamDistAction_Impl(int sfxId) {
    g_camDist = CycleFloatPreset(g_distPresets, 8, g_camDist);
    ApplyOptionScaleChange(sfxId);
}

static void CamFovAction_Impl(int sfxId) {
    g_camFov = CycleFloatPreset(g_fovPresets, 11, g_camFov);
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

static void PrismVfxAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    bool left = (sm != nullptr && sm[0xb3] != 0);
    if (left) {
        g_prismVfx = (g_prismVfx == 0) ? 3u : static_cast<uint8_t>(g_prismVfx - 1);
    } else {
        g_prismVfx = static_cast<uint8_t>((g_prismVfx + 1) % 4);
    }
    if (g_prismVfx <= 1) {
        g_prismOverrideSec = 0.0f;
    } else if (g_prismOverrideSec > 0.0f) {
        g_prismOverrideSec = (g_prismVfx == 3) ? 2.0f : 1.0f;
    }
    if (sfxId > 0) Game_PlaySfx(sm, sfxId);
    SyncGraphicsRuntime(sm, false);
    Game_SaveUserSettings(sm + 0x298);
}

static void ChromaAbAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    g_chromaAb ^= 1u;
    if (sfxId > 0) Game_PlaySfx(sm, sfxId);
    SyncGraphicsRuntime(sm, false);
    Game_SaveUserSettings(sm + 0x298);
}

static void AaAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    if (g_aaLevelCount <= 0) ProbeSupportedAaLevels();
    bool left = (sm != nullptr && sm[0xb3] != 0);
    int curIdx = 0;
    for (int i = 0; i < g_aaLevelCount; ++i) {
        if (g_aaLevels[i] == g_aaSamples) {
            curIdx = i;
            break;
        }
        if (g_aaLevels[i] < g_aaSamples) {
            curIdx = i;
        }
    }
    int nextIdx = left ? ((curIdx - 1 + g_aaLevelCount) % g_aaLevelCount)
                       : ((curIdx + 1) % g_aaLevelCount);
    g_aaSamples = g_aaLevels[nextIdx];
    if (sfxId > 0) Game_PlaySfx(sm, sfxId);
    SyncGraphicsRuntime(sm, true);
    Game_SaveUserSettings(sm + 0x298);
}

static void FinishEffectAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    bool left = (sm != nullptr && sm[0xb3] != 0);
    g_finishEffect = left
        ? static_cast<uint8_t>((g_finishEffect + 2u) % 3u)
        : static_cast<uint8_t>((g_finishEffect + 1u) % 3u);
    if (sm != nullptr) {
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        Game_SaveUserSettings(sm + 0x298);
    }
}

static void FinishPlatformAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    g_finishPlatform ^= 1u;
    if (sm != nullptr) {
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        Game_SaveUserSettings(sm + 0x298);
    }
}

static void ColorThemeAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    g_colorTheme ^= 1u;
    OnColorThemeChanged();
    if (sm != nullptr) {
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        Game_SaveUserSettings(sm + 0x298);
    }
}

static void MusicStartAction_Impl(int sfxId) {
    g_musicStart ^= 1u;
    ApplyOptionScaleChange(sfxId);
}

static void MusicPlaylistAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    g_musicPlaylist ^= 1u;
    if (sm != nullptr) {
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        Game_SaveUserSettings(sm + 0x298);
    }
    OnMusicPlaylistChanged();
}

static void TimerModeAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    bool left = (sm != nullptr && sm[0xb3] != 0);
    g_timerMode = left ? static_cast<uint8_t>((g_timerMode + 3u) & 3u)
                       : static_cast<uint8_t>((g_timerMode + 1u) & 3u);
    if (sfxId > 0) Game_PlaySfx(sm, sfxId);
    Game_SaveUserSettings(sm + 0x298);
}

static void TimerSizeAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    bool left = (sm != nullptr && sm[0xb3] != 0);
    int idx = NearestTimerSizeIdx(g_timerSize);
    int next = left ? ((idx + 7) & 7) : ((idx + 1) & 7);
    g_timerSize = g_timerSizePresets[next];
    if (sfxId > 0) Game_PlaySfx(sm, sfxId);
    Game_SaveUserSettings(sm + 0x298);
}

static void TimerPosAction_Impl(int sfxId) {
    uint8_t* sm = GetScreenMgr();
    bool left = (sm != nullptr && sm[0xb3] != 0);
    g_timerPos = left ? static_cast<uint8_t>((g_timerPos + 3u) & 3u)
                      : static_cast<uint8_t>((g_timerPos + 1u) & 3u);
    if (sfxId > 0) Game_PlaySfx(sm, sfxId);
    Game_SaveUserSettings(sm + 0x298);
}

static void ResetScoreAction_Impl(void* optionsMenu) {
    (void)optionsMenu;
    Game_PlaySfx(GetScreenMgr(), 0xc);
}

// ------------------------------------------------------------------
// 3. Nested Options & Pause Page 1 state, dispatch, hitTest & rendering
// ------------------------------------------------------------------
static int GetOptTargetCategory(OptItem item) {
    switch (item) {
    case OPT_CAT_BTN_DISPLAY:  return OPT_CAT_DISPLAY;
    case OPT_CAT_BTN_GRAPHICS: return OPT_CAT_GRAPHICS;
    case OPT_CAT_BTN_AUDIO:    return OPT_CAT_AUDIO;
    case OPT_CAT_BTN_UI:       return OPT_CAT_UI;
    case OPT_CAT_BTN_TIMER:    return OPT_CAT_TIMER;
    case OPT_CAT_BTN_GAMEPLAY: return OPT_CAT_GAMEPLAY;
    default:                   return -1;
    }
}

static bool IsOptSubmenuItem(OptItem item) {
    return GetOptTargetCategory(item) >= 0;
}

static int GetOptParentCategory(int cat) {
    switch (cat) {
    case OPT_CAT_TIMER:
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
    if (cat == OPT_CAT_ROOT)  return 0;
    if (cat == OPT_CAT_TIMER) return 2;
    return 1;
}

static int GetOptParentRowForCategory(int cat) {
    switch (cat) {
    case OPT_CAT_DISPLAY:  return 0;
    case OPT_CAT_GRAPHICS: return 1;
    case OPT_CAT_AUDIO:    return 2;
    case OPT_CAT_UI:       return 3;
    case OPT_CAT_GAMEPLAY: return 4;
    case OPT_CAT_TIMER:    return 1; // row 1 in OPT_CAT_UI (below time format)
    default:               return 0;
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
    case OPT_CAT_GRAPHICS:
        outItems[0] = OPT_AA;
        outItems[1] = OPT_CHROMA_AB;
        outItems[2] = OPT_PRISM_VFX;
        outItems[3] = OPT_COLOR_THEME;
        outItems[4] = OPT_FINISH_EFFECT;
        outItems[5] = OPT_FINISH_PLATFORM;
        outItems[6] = OPT_BACK;
        return 7;
    case OPT_CAT_AUDIO:
        outItems[0] = OPT_SOUNDS;
        outItems[1] = OPT_MUSIC;
        outItems[2] = OPT_MUSIC_START;
        outItems[3] = OPT_MUSIC_PLAYLIST;
        outItems[4] = OPT_BACK;
        return 5;
    case OPT_CAT_UI:
        outItems[0] = OPT_TIME_FMT;
        outItems[1] = OPT_CAT_BTN_TIMER;
        outItems[2] = OPT_BACK;
        return 3;
    case OPT_CAT_TIMER:
        outItems[0] = OPT_TIMER;
        outItems[1] = OPT_TIMER_SIZE;
        outItems[2] = OPT_TIMER_POS;
        outItems[3] = OPT_BACK;
        return 4;
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
    case OPT_SOUNDS:
    case OPT_MUSIC:
    case OPT_MUSIC_START:
    case OPT_MUSIC_PLAYLIST:
    case OPT_UP_KEY:
    case OPT_SKIP_INTRO:
    case OPT_TIME_FMT:
    case OPT_TIMER:
    case OPT_TIMER_SIZE:
    case OPT_TIMER_POS:
        return true;
    default:
        return false;
    }
}

static const EdgeWString* GetOptHeaderWstr(int cat, EdgeWString* tmp, bool* outFreeHeap) {
    *outFreeHeap = false;
    switch (cat) {
    case OPT_CAT_DISPLAY:  return &g_wstrCatDisplay;
    case OPT_CAT_GRAPHICS: return &g_wstrCatGraphics;
    case OPT_CAT_AUDIO:    return &g_wstrCatAudio;
    case OPT_CAT_UI:       return &g_wstrCatUi;
    case OPT_CAT_TIMER:    return &g_wstrTimer;
    case OPT_CAT_GAMEPLAY: return &g_wstrCatGameplay;
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
    case OPT_CAT_BTN_DISPLAY:  return &g_wstrCatDisplay;
    case OPT_CAT_BTN_GRAPHICS: return &g_wstrCatGraphics;
    case OPT_CAT_BTN_AUDIO:    return &g_wstrCatAudio;
    case OPT_CAT_BTN_UI:       return &g_wstrCatUi;
    case OPT_CAT_BTN_TIMER:    return &g_wstrTimer;
    case OPT_CAT_BTN_GAMEPLAY: return &g_wstrCatGameplay;
    case OPT_PRISM_VFX:        return &g_wstrPrismVfx;
    case OPT_CHROMA_AB:        return &g_wstrChromaAb;
    case OPT_AA:               return &g_wstrAntiAlias;
    case OPT_UI_SCALE:         return &g_wstrUiScale;
    case OPT_AUTO_SCALE:       return &g_wstrAuto;
    case OPT_CAM_DIST:         return &g_wstrCamDist;
    case OPT_DOLLY_ZOOM:       return &g_wstrCamFov;
    case OPT_FINISH_EFFECT:    return &g_wstrFinishEffect;
    case OPT_FINISH_PLATFORM:  return &g_wstrFinishPlatform;
    case OPT_COLOR_THEME:      return &g_wstrColorTheme;
    case OPT_MUSIC_START:      return &g_wstrMusicStart;
    case OPT_MUSIC_PLAYLIST:   return &g_wstrMusicPlaylist;
    case OPT_SKIP_INTRO:       return &g_wstrSkipIntro;
    case OPT_TIME_FMT:         return &g_wstrTimeFmt;
    case OPT_TIMER:            return &g_wstrTimerType;
    case OPT_TIMER_SIZE:       return &g_wstrTimerSizeLbl;
    case OPT_TIMER_POS:        return &g_wstrTimerPosLbl;
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
    case OPT_FINISH_PLATFORM:
    case OPT_COLOR_THEME:
    case OPT_MUSIC_START:
    case OPT_MUSIC_PLAYLIST:
    case OPT_UP_KEY:
    case OPT_SKIP_INTRO:
    case OPT_TIME_FMT:
        return CTRL_TOGGLE;
    case OPT_AA:
    case OPT_CAM_DIST:
    case OPT_DOLLY_ZOOM:
    case OPT_FINISH_EFFECT:
    case OPT_TIMER:
    case OPT_TIMER_POS:
        return CTRL_POPUP;
    case OPT_RESET_SCORE:
        return CTRL_CONFIRM_POPUP;
    case OPT_PRISM_VFX:
    case OPT_UI_SCALE:
    case OPT_SOUNDS:
    case OPT_MUSIC:
    case OPT_TIMER_SIZE:
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
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        Game_SaveUserSettings(sm + 0x298);
        ApplyUiScale(sm);
    }
}

static void ResetOptUiState(bool commitUiScale) {
    g_optUi.popupOpen = false;
    g_optUi.dragActive = false;
    g_optUi.suppressNextClickAction = false;
    if (commitUiScale) {
        CommitPendingUiScale();
    } else {
        g_optUi.uiScalePending = false;
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
    case OPT_FINISH_PLATFORM: return g_finishPlatform ? 1 : 0;
    case OPT_COLOR_THEME:     return g_colorTheme ? 1 : 0;
    case OPT_MUSIC_START:     return g_musicStart ? 1 : 0;
    case OPT_MUSIC_PLAYLIST:  return g_musicPlaylist ? 1 : 0;
    case OPT_UP_KEY:      return (sm != nullptr && sm[0x2b4] != 0) ? 1 : 0;
    case OPT_SKIP_INTRO:  return g_skipIntro ? 1 : 0;
    case OPT_TIME_FMT:    return g_timeFmt ? 1 : 0;
    default:              return 0;
    }
}

static int GetPopupChoiceCount(OptItem item) {
    switch (item) {
    case OPT_AA:
        if (g_aaLevelCount <= 0) ProbeSupportedAaLevels();
        return (g_aaLevelCount > 0) ? g_aaLevelCount : 1;
    case OPT_CAM_DIST:     return 8;
    case OPT_DOLLY_ZOOM:   return 11;
    case OPT_FINISH_EFFECT: return 3;
    case OPT_TIMER:        return 4;
    case OPT_TIMER_POS:   return 4;
    case OPT_RESET_SCORE: return 2;
    default:              return 0;
    }
}

static int GetPopupCurrentChoice(OptItem item) {
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
    case OPT_FINISH_EFFECT: return (g_finishEffect <= 2u) ? static_cast<int>(g_finishEffect) : 0;
    case OPT_TIMER:         return std::clamp<int>(g_timerMode, 0, 3);
    case OPT_TIMER_POS:   return std::clamp<int>(g_timerPos, 0, 3);
    case OPT_RESET_SCORE: return 0; // default highlight on "no"
    default:              return 0;
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
    case OPT_CAM_DIST:      return 4; // 1
    case OPT_DOLLY_ZOOM:    return 0; // pc (-1)
    case OPT_FINISH_EFFECT: return 0; // pc
    case OPT_TIMER:         return 3; // both
    case OPT_TIMER_POS:   return 1; // TR
    case OPT_RESET_SCORE: return 0; // no
    default:              return 0;
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
    case OPT_FINISH_EFFECT:
        idx = std::clamp(idx, 0, 2);
        if (idx == 1) return &g_wstrMobile;
        if (idx == 2) return &g_wstrJava;
        return &g_wstrPc;
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
    case OPT_TIMER_POS:
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
    g_optUi.popupOpen = true;
    g_optUi.popupInPause = inPause;
    g_optUi.popupItem = item;
    g_optUi.popupRow = row;
    g_optUi.popupChoiceCount = cnt;
    g_optUi.popupHighlight = GetPopupCurrentChoice(item);
    Game_PlaySfx(GetScreenMgr(), 0xc);
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
            Game_PlaySfx(sm, 0xc);
        } else {
            Game_PlaySfx(sm, 0xb);
        }
        break;
    case OPT_AA:
        g_aaSamples = g_aaLevels[idx];
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        SyncGraphicsRuntime(sm, true);
        if (sm != nullptr) Game_SaveUserSettings(sm + 0x298);
        break;
    case OPT_CAM_DIST:
        g_camDist = g_distPresets[idx];
        ApplyOptionScaleChange(sfxId);
        break;
    case OPT_DOLLY_ZOOM:
        g_camFov = g_fovPresets[idx];
        ApplyOptionScaleChange(sfxId);
        break;
    case OPT_FINISH_EFFECT:
        g_finishEffect = static_cast<uint8_t>(idx);
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        if (sm != nullptr) Game_SaveUserSettings(sm + 0x298);
        break;
    case OPT_TIMER:
        g_timerMode = static_cast<uint8_t>(idx);
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        if (sm != nullptr) Game_SaveUserSettings(sm + 0x298);
        break;
    case OPT_TIMER_POS:
        g_timerPos = static_cast<uint8_t>(idx);
        if (sfxId > 0) Game_PlaySfx(sm, sfxId);
        if (sm != nullptr) Game_SaveUserSettings(sm + 0x298);
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
    default:
        return 0.0f;
    }
}

static float GetSliderDefaultNormalized(OptItem item) {
    switch (item) {
    case OPT_PRISM_VFX:  return 1.0f / 3.0f; // short
    case OPT_UI_SCALE:   return 2.0f / 8.0f; // 1.0
    case OPT_SOUNDS:     return 1.0f;        // 100
    case OPT_MUSIC:      return 1.0f;        // 100
    case OPT_TIMER_SIZE: return 3.0f / 7.0f; // 1.0
    default:             return 0.0f;
    }
}

static void SetPrismVfxValue(int idx, int sfxId, bool save) {
    idx = std::clamp(idx, 0, 3);
    if (idx == static_cast<int>(g_prismVfx)) {
        if (save) {
            uint8_t* sm = GetScreenMgr();
            if (sm != nullptr) Game_SaveUserSettings(sm + 0x298);
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
    if (sfxId > 0) {
        Game_PlaySfx(sm, sfxId);
    }
    SyncGraphicsRuntime(sm, false);
    if (save && sm != nullptr) {
        Game_SaveUserSettings(sm + 0x298);
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
    case OPT_TIMER_SIZE: {
        int curIdx = NearestTimerSizeIdx(g_timerSize);
        int nextIdx = std::clamp(curIdx + dir, 0, 7);
        if (nextIdx != curIdx) {
            g_timerSize = g_timerSizePresets[nextIdx];
            if (sfxId > 0) Game_PlaySfx(sm, sfxId);
            if (sm != nullptr) Game_SaveUserSettings(sm + 0x298);
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
    case OPT_TIMER_SIZE: {
        int idx = std::clamp(static_cast<int>(std::lround(norm * 7.0f)), 0, 7);
        float newVal = g_timerSizePresets[idx];
        if (std::fabs(newVal - g_timerSize) > 0.001f) {
            g_timerSize = newVal;
            Game_PlaySfx(sm, 2);
        }
        if (!isDrag && sm != nullptr) {
            Game_SaveUserSettings(sm + 0x298);
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
        if (std::fabs(g_timerSize - 1.0f) > 0.001f) {
            g_timerSize = 1.0f;
            if (sfxId > 0) Game_PlaySfx(sm, sfxId);
            if (sm != nullptr) Game_SaveUserSettings(sm + 0x298);
        }
        break;
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
    case OPT_FINISH_EFFECT:
        if (g_finishEffect == 1) return &g_wstrMobile;
        if (g_finishEffect == 2) return &g_wstrJava;
        return &g_wstrPc;
    case OPT_FINISH_PLATFORM:
        return (g_finishPlatform != 0) ? &g_wstrMobile : &g_wstrPc;
    case OPT_COLOR_THEME:
        return (g_colorTheme != 0) ? &g_wstrMobile : &g_wstrPc;
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

static void ExecuteOptSetting(OptItem item, void* menuObj, int sfxId) {
    switch (item) {
    case OPT_FULLSCREEN:  ToggleFullscreenWithSfx(sfxId); break;
    case OPT_PRISM_VFX:   PrismVfxAction_Impl(sfxId); break;
    case OPT_CHROMA_AB:   ChromaAbAction_Impl(sfxId); break;
    case OPT_AA:          AaAction_Impl(sfxId); break;
    case OPT_AUTO_SCALE:  AutoScaleAction_Impl(sfxId); break;
    case OPT_CAM_DIST:        CamDistAction_Impl(sfxId); break;
    case OPT_DOLLY_ZOOM:      CamFovAction_Impl(sfxId); break;
    case OPT_FINISH_EFFECT:   FinishEffectAction_Impl(sfxId); break;
    case OPT_FINISH_PLATFORM: FinishPlatformAction_Impl(sfxId); break;
    case OPT_COLOR_THEME:     ColorThemeAction_Impl(sfxId); break;
    case OPT_MUSIC_START:     MusicStartAction_Impl(sfxId); break;
    case OPT_MUSIC_PLAYLIST:  MusicPlaylistAction_Impl(sfxId); break;
    case OPT_UP_KEY:      ToggleUpKeyWithSfx(sfxId); break;
    case OPT_SKIP_INTRO:  SkipIntroAction_Impl(sfxId); break;
    case OPT_TIME_FMT:    TimeFmtAction_Impl(sfxId); break;
    case OPT_TIMER:       TimerModeAction_Impl(sfxId); break;
    case OPT_TIMER_SIZE:  TimerSizeAction_Impl(sfxId); break;
    case OPT_TIMER_POS:   TimerPosAction_Impl(sfxId); break;
    case OPT_RESET_SCORE: ResetScoreAction_Impl(menuObj); break;
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
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
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

    using DrawSpriteFn = void(__thiscall*)(void*, int, int, int, int, int, int);
    auto drawSprite = reinterpret_cast<DrawSpriteFn>(0x4a8140);
    drawSprite(hSprites[(u + 0u) % ucnt], 0, 0, menuW, 2, bx, 4);
    drawSprite(hSprites[(u + 1u) % ucnt], 0, 0, menuW, 2, bx, virtH - 0xf);
    drawSprite(vSprites[(u + 2u) % ucnt], 0, 0, 2, virtH - 0x11, bx, 4);
    drawSprite(vSprites[(u + 3u) % ucnt], 0, 0, 2, virtH - 0x11, bx + menuW - 2, 4);
}

constexpr int kMenuWideW         = 0x170; // 368px (320 + 48 leftward expansion)
constexpr int kMainOptValRelX    = 0xf0;  // 120px value column (rightBound - valX)
constexpr int kPauseOptValRelX   = 0x101; // 105px value column (rightBound - valX)

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
    uint32_t hiCol  = RgbToRgbaOpaque(hiBarRgb);
    Game_DrawQuad(drawX - 2, popupY - 2, popupW + 4, popupH + 4, &brdCol);
    Game_DrawQuad(drawX, popupY, popupW, popupH, &bgCol);
    if (g_optUi.popupHighlight >= 0 && g_optUi.popupHighlight < nChoices) {
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
}

static void UpdateOptControlsPerFrame(bool inPause, int cat, int selRow, int valXScreen, int rightBoundScreen) {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
    int trackW = rightBoundScreen - valXScreen;

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
            } else {
                Game_SaveUserSettings(sm + 0x298);
            }
        }
    }

    if (g_optUi.uiScalePending && !g_optUi.dragActive &&
        sm[0xb2] == 0 && sm[0xb5] == 0 && sm[0xb3] == 0 && sm[0xb6] == 0) {
        CommitPendingUiScale();
    }

    if (g_optUi.popupOpen && g_optUi.popupInPause == inPause && sm[0xa8] != 0) {
        int mx = *reinterpret_cast<int*>(sm + 0xa0);
        int my = *reinterpret_cast<int*>(sm + 0xa4);
        if (mx != g_optUi.lastMouseX || my != g_optUi.lastMouseY) {
            g_optUi.lastMouseX = mx;
            g_optUi.lastMouseY = my;
            if (g_optUi.popupItemH > 0 &&
                mx >= g_optUi.popupRectX && mx < g_optUi.popupRectX + g_optUi.popupRectW &&
                my >= g_optUi.popupRectY + g_optUi.popupPadY &&
                my < g_optUi.popupRectY + g_optUi.popupRectH - g_optUi.popupPadY) {
                int idx = (my - (g_optUi.popupRectY + g_optUi.popupPadY)) / g_optUi.popupItemH;
                if (idx >= 0 && idx < g_optUi.popupChoiceCount && idx != g_optUi.popupHighlight) {
                    g_optUi.popupHighlight = idx;
                    Game_PlaySfx(sm, 2);
                }
            }
        }
    }

    (void)trackW;
    bool resetDown = ((GetAsyncKeyState('R') & 0x8000) != 0)
                  || ((GetAsyncKeyState(VK_BACK) & 0x8000) != 0)
                  || ((GetAsyncKeyState(VK_DELETE) & 0x8000) != 0);
    if (resetDown && !g_optUi.prevResetKeyDown && !g_optUi.popupOpen && !g_optUi.dragActive) {
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
    int minX, int maxX, int valXScreen, int rightBoundScreen
) {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr || pt == nullptr) return -1;
    int px = pt[0];
    int py = pt[1];
    bool isPress = (sm[0xcb] != 0);
    bool isRelease = (sm[0xca] == 0 && sm[0xcc] != 0);

    if (g_optUi.popupOpen && g_optUi.popupInPause == inPause) {
        if (g_optUi.popupItemH > 0 &&
            px >= g_optUi.popupRectX && px < g_optUi.popupRectX + g_optUi.popupRectW &&
            py >= g_optUi.popupRectY && py < g_optUi.popupRectY + g_optUi.popupRectH) {
            int relY = py - (g_optUi.popupRectY + g_optUi.popupPadY);
            int idx = std::clamp(relY / g_optUi.popupItemH, 0, g_optUi.popupChoiceCount - 1);
            if (idx != g_optUi.popupHighlight) {
                g_optUi.popupHighlight = idx;
                Game_PlaySfx(sm, 2);
            }
            if (isPress) g_optUi.suppressNextClickAction = false;
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
            } else {
                Game_SaveUserSettings(sm + 0x298);
            }
            g_optUi.suppressNextClickAction = true;
            return -1;
        }
        return g_optUi.dragRow;
    }

    if (isPress) {
        g_optUi.suppressNextClickAction = false;
    }

    int hitRow = -1;
    if (px >= minX && px < maxX) {
        for (int row = 0; row < nRows; ++row) {
            if (py >= rowTops[row] && py < rowTops[row] + rowH) {
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
                } else {
                    Game_SaveUserSettings(sm + 0x298);
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
    g_mainOptPendingReturnCat = -1;
    g_mainOptCat = OPT_CAT_ROOT;
    *reinterpret_cast<int*>(optionsMenu + 0x10) = 0;
    g_optUi.mainSelRow = 0;
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
    int stride = 0x32;
    int backTopRel = 0;
    int rowH = kMainOptRowH;
    ComputeMainOptLayout(nRows, virtH, &stride, &backTopRel, &rowH);

    int rowTops[kMaxOptRows];
    for (int row = 0; row < nRows; ++row) {
        int yRel = (row == backIdx) ? backTopRel : (0x56 + stride * row);
        rowTops[row] = basePt[1] + yRel;
    }

    int minX = basePt[0] + 2;
    int maxX = basePt[0] + menuW - 2;
    int valXScreen = basePt[0] + kMainOptValRelX;
    int rightBoundScreen = basePt[0] + menuW - 8;
    int& sel = *reinterpret_cast<int*>(optionsMenu + 0x10);
    int res = HandleOptMouseHitTest(
        false, g_mainOptCat, sel, pt,
        nRows, items, rowTops, rowH,
        minX, maxX, valXScreen, rightBoundScreen
    );
    g_optUi.mainSelRow = sel;
    return res;
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
    bool fromKey = (sm != nullptr && (sm[0xc5] != 0 || isLr));
    int sfxId = fromKey ? 2 : 0xc;
    if (g_optUi.popupOpen && !g_optUi.popupInPause) {
        if (!isLr) {
            ApplyPopupChoice(g_optUi.popupItem, g_optUi.popupHighlight, sfxId);
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
        ExecuteOptSetting(item, optionsMenu, sfxId);
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
    g_optUi.mainSelRow = selRow;

    if (enterFrames >= 8 && exitTimer < 0) {
        uint8_t* sm = GetScreenMgr();
        int nRows = GetOptRowCount(g_mainOptCat, false);
        if (g_optUi.popupOpen && !g_optUi.popupInPause) {
            if (sm[0xad] != 0) {
                Game_PlaySfx(sm, 2);
                if (g_optUi.popupChoiceCount > 0) {
                    g_optUi.popupHighlight = (g_optUi.popupHighlight + g_optUi.popupChoiceCount - 1) % g_optUi.popupChoiceCount;
                }
            } else if (sm[0xb0] != 0) {
                Game_PlaySfx(sm, 2);
                if (g_optUi.popupChoiceCount > 0) {
                    g_optUi.popupHighlight = (g_optUi.popupHighlight + 1) % g_optUi.popupChoiceCount;
                }
            } else if (sm[0xc8] != 0) {
                OptionsCancel_Impl(optionsMenu);
            } else if (sm[0xc5] != 0) {
                ApplyPopupChoice(g_optUi.popupItem, g_optUi.popupHighlight, 2);
            }
        } else {
            if (sm[0xad] != 0) {
                CommitPendingUiScale();
                Game_PlaySfx(sm, 2);
                if (--selRow < 0) selRow = nRows - 1;
            } else if (sm[0xb0] != 0) {
                CommitPendingUiScale();
                Game_PlaySfx(sm, 2);
                if (++selRow >= nRows) selRow = 0;
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
        g_optUi.mainSelRow = selRow;
    }

    if (enterFrames < 8) {
        ++enterFrames;
    }
    int& exitDest = *reinterpret_cast<int*>(optionsMenu + 0xc);
    if (exitTimer >= 0 && exitDest >= kOptCatEnterBase) {
        if (exitTimer == 0) {
            ResetOptUiState(false);
            g_mainOptCat = exitDest - kOptCatEnterBase;
            selRow = 0;
            g_optUi.mainSelRow = 0;
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
            int prevCat = g_mainOptCat;
            int targetCat = g_mainOptPendingReturnCat;
            g_mainOptPendingReturnCat = -1;
            g_mainOptCat = targetCat;
            selRow = GetOptParentRowForCategory(prevCat);
            g_optUi.mainSelRow = selRow;
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
    g_optUi.mainSelRow = selRow;
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
    int stride = 0x32;
    int backTopRel = 0;
    int rowH = kMainOptRowH;
    ComputeMainOptLayout(nRows, virtH, &stride, &backTopRel, &rowH);
    int dy = (kMainOptRowH - rowH) / 2;

    // Header text uses pre-computed RGBA at [ebp - 0x40]
    const int rightBound = xBase + menuW - 8;
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
    if (selRow >= 0 && selRow < nRows) {
        int selTopRel = (selRow == backIdx) ? backTopRel : (0x56 + stride * selRow);
        if (barInnerRgb != barOuterRgb) {
            uint32_t outerRgba = RgbToRgbaOpaque(barOuterRgb);
            Game_DrawQuad(xBase + 2, yBase + selTopRel + barSlideY - 2, menuW - 4, rowH + 4, &outerRgba);
            uint32_t innerRgba = RgbToRgbaOpaque(barInnerRgb);
            Game_DrawQuad(xBase + 4, yBase + selTopRel + barSlideY, menuW - 8, rowH, &innerRgba);
        } else {
            uint32_t fillRgba = RgbToRgbaOpaque(barInnerRgb);
            Game_DrawQuad(xBase + 2, yBase + selTopRel + barSlideY, menuW - 4, rowH, &fillRgba);
        }
    }

    // Option rows
    for (int row = 0; row < nRows; ++row) {
        if (selRow != row && !drawUnselected) continue;
        uint32_t fgRgb = (selRow == row) ? selRgb : normRgb;
        uint32_t bgRgb = (selRow == row) ? barInnerRgb : 0xffffffu;
        uint32_t rgba = RgbToRgbaOpaque(fgRgb);
        *reinterpret_cast<uint32_t*>(ebpFrame - 0x40) = rgba;
        *reinterpret_cast<uint32_t*>(0x5f4268) = rgba;

        int rowTopY = yBase + ((row == backIdx) ? backTopRel : (0x56 + stride * row));
        if (selRow == row) {
            rowTopY += barSlideY;
        }
        OptCtrlKind kind = GetOptCtrlKind(items[row]);
        bool hasIndicator = (kind == CTRL_TOGGLE || kind == CTRL_POPUP || kind == CTRL_SLIDER);
        int lblTextY = rowTopY + 0xc - dy;
        int valTextY = hasIndicator ? (rowTopY + 0x7 - dy) : (rowTopY + 0xc - dy);

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
            int indY = rowTopY + (std::max)(0x16, 0x20 - (kMainOptRowH - rowH));
            DrawOptRowControlIndicator(items[row], valX, rightBound, indY, activeRgba, dimRgba, defMarkRgba);
        }
    }

    if (g_optUi.popupOpen && !g_optUi.popupInPause &&
        g_optUi.popupRow >= 0 && g_optUi.popupRow < nRows) {
        int popRow = g_optUi.popupRow;
        int popRowTopY = yBase + ((popRow == backIdx) ? backTopRel : (0x56 + stride * popRow));
        if (selRow == popRow) {
            popRowTopY += barSlideY;
        }
        uint32_t hiBarRgb = (barInnerRgb == 0xffffffu) ? barOuterRgb : barInnerRgb;
        DrawOptPopupOverlay(g_optUi.popupItem, rightBound, popRowTopY, rowH, false, 0, hiBarRgb);
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

static int __cdecl PauseP1HitTest_Impl(uint8_t* pauseMenu, const int* pt) {
    if (*reinterpret_cast<int*>(pauseMenu + 4) < 5 || *reinterpret_cast<int*>(pauseMenu + 8) >= 0) {
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
    int virtH = reinterpret_cast<int(__cdecl*)()>(0x496060)() + 0x11;
    int rowH = kPauseOptRowH;
    if (nRows > 0 && (basePt[1] + 0x75 + nRows * rowH) > (virtH - 8)) {
        rowH = std::clamp((virtH - 8 - (basePt[1] + 0x75)) / nRows, 28, kPauseOptRowH);
    }
    int rowTops[kMaxOptRows];
    for (int row = 0; row < nRows; ++row) {
        rowTops[row] = basePt[1] + 0x75 + row * rowH;
    }

    int minX = basePt[0] + 2;
    int maxX = basePt[0] + kMenuWideW - 2;
    int valXScreen = basePt[0] + kPauseOptValRelX;
    int rightBoundScreen = basePt[0] + kMenuWideW - 6;
    int& sel = *reinterpret_cast<int*>(pauseMenu + 0x50);
    return HandleOptMouseHitTest(
        true, g_pauseOptCat, sel, pt,
        nRows, items, rowTops, rowH,
        minX, maxX, valXScreen, rightBoundScreen
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

    if (page == 1 && *reinterpret_cast<int*>(pm + 4) >= 5 && *reinterpret_cast<int*>(pm + 8) < 0 &&
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
    bool fromKey = (sm != nullptr && (sm[0xc5] != 0 || isLr));
    int sfxId = fromKey ? 2 : 0xc;
    if (g_optUi.popupOpen && g_optUi.popupInPause) {
        if (!isLr) {
            ApplyPopupChoice(g_optUi.popupItem, g_optUi.popupHighlight, sfxId);
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
        ExecuteOptSetting(item, pauseMenu, sfxId);
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

static int ComputePauseOptRowH(int yBase, int nRows) {
    uint8_t* sm = GetScreenMgr();
    int virtH = (sm != nullptr) ? *reinterpret_cast<int*>(sm + 0x28) : 720;
    if (nRows > 0 && (yBase + 0x75 + nRows * kPauseOptRowH) > (virtH - 8)) {
        return std::clamp((virtH - 8 - (yBase + 0x75)) / nRows, 24, kPauseOptRowH);
    }
    return kPauseOptRowH;
}

static void DrawPauseOptionsPane(int cat, int selRow, int xBase, int yBase, int slideY, int frame, int xOffset) {
    OptItem items[kMaxOptRows];
    int nRows = GetOptItems(cat, true, items);
    int rowH = ComputePauseOptRowH(yBase, nRows);
    int dy = (kPauseOptRowH - rowH) / 2;

    int paneLeft  = xBase + kMenuWideW + xOffset;
    int paneW     = kMenuWideW;
    int innerLeft = paneLeft + 2;
    int innerW    = paneW - 4;

    uint32_t whiteRgba = 0xffffffffu;
    Game_DrawQuad(innerLeft, yBase + slideY + 0x55, innerW, 0x20 + nRows * rowH, &whiteRgba);
    uint32_t hdrBgRgba = 0xffcbcbcbu;
    Game_DrawQuad(innerLeft, yBase + slideY + 0x55, innerW, 0x20, &hdrBgRgba);

    const int labelX     = paneLeft + 0x18;
    const int rightBound = paneLeft + kMenuWideW - 6;
    const int valX       = paneLeft + kPauseOptValRelX;
    constexpr int kMinColGap = 6;

    uint32_t* pFontCol = reinterpret_cast<uint32_t*>(0x5f4268);
    *pFontCol = 0xff3b3b3bu;
    EdgeWString hdrTmp;
    bool hdrFree = false;
    const EdgeWString* hdrWs = GetOptHeaderWstr(cat, &hdrTmp, &hdrFree);
    DrawTextAutoFit(hdrWs, labelX, yBase + slideY + 0x6b, 0x2000, 0x2000, 0, 0, 0x44, rightBound - labelX);
    if (hdrFree) hdrTmp.free_if_heap();

    uint32_t rainbowRgb = GetSmoothMenuRainbow(frame + 3);
    uint32_t rainbowRgba = RgbToRgbaOpaque(rainbowRgb);
    if (selRow >= 0 && selRow < nRows) {
        Game_DrawQuad(innerLeft, yBase + slideY + 0x76 + selRow * rowH, innerW, rowH, &rainbowRgba);
    }

    *pFontCol = 0xff3b3b3bu;
    const void* dividerCol = reinterpret_cast<const void*>(0x5f93a4);
    for (int row = 0; row < nRows; ++row) {
        int divY = 0x75 + row * rowH;
        Game_DrawQuad(paneLeft, yBase + slideY + divY, paneW, 1, dividerCol);

        OptCtrlKind kind = GetOptCtrlKind(items[row]);
        bool hasIndicator = (kind == CTRL_TOGGLE || kind == CTRL_POPUP || kind == CTRL_SLIDER);
        int lblTxtY = divY + 0x1a - dy;
        int valTxtY = hasIndicator ? (divY + 0x15 - dy) : (divY + 0x1a - dy);

        EdgeWString lblTmp, valTmp;
        bool lblFree = false, valFree = false;
        const EdgeWString* lblWs = GetOptLabelWstr(items[row], &lblTmp, &lblFree);
        const EdgeWString* valWs = GetOptValueWstr(items[row], &valTmp, &valFree);
        bool hasArrow = IsOptSubmenuItem(items[row]);

        int lblMaxW = (valWs != nullptr) ? ((valX - kMinColGap) - labelX)
                                         : (hasArrow ? ((rightBound - 22) - labelX) : (rightBound - labelX));
        DrawTextAutoFit(lblWs, labelX, yBase + slideY + lblTxtY, 0x2000, 0x2000, 0, 0, 0x44, lblMaxW);
        if (valWs != nullptr) {
            DrawTextAutoFit(valWs, valX, yBase + slideY + valTxtY, 0x2000, 0x2000, 0, 0, 0x44, rightBound - valX);
        } else if (hasArrow) {
            Game_DrawText(&g_wstrSubmenuArrow, rightBound - 6, yBase + slideY + lblTxtY, 0x2000, 0x2000, 0, 0, 0x48);
        }
        if (lblFree) lblTmp.free_if_heap();
        if (valFree) valTmp.free_if_heap();

        if (hasIndicator) {
            uint32_t bgRgb = (selRow == row) ? rainbowRgb : 0xffffffu;
            uint32_t fgRgb = 0x3b3b3bu;
            uint32_t activeRgba  = RgbToRgbaOpaque(fgRgb);
            uint32_t dimRgba     = RgbToRgbaOpaque(LerpRgb(bgRgb, fgRgb, 0.32f));
            uint32_t defMarkRgba = RgbToRgbaOpaque(LerpRgb(bgRgb, fgRgb, 0.72f));
            int indY = yBase + slideY + divY + (std::max)(0x16, 0x1d - (kPauseOptRowH - rowH));
            DrawOptRowControlIndicator(items[row], valX, rightBound, indY, activeRgba, dimRgba, defMarkRgba);
        }
    }

    Game_DrawQuad(paneLeft, yBase + slideY + 0x75 + nRows * rowH, paneW, 1, dividerCol);
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
            DrawPauseOptionsPane(g_pauseSubCatAtDepth[d], paneSel, xBase, yBase, slideY, frame, d * subStride);
        }
    }
    if (subSwipeX > 0.0f) {
        reinterpret_cast<void(__thiscall*)(void*)>(0x41f610)(mstack);
    }

    if (g_optUi.popupOpen && g_optUi.popupInPause && g_optUi.popupRow >= 0) {
        int xOffset = activeDepth * subStride;
        int popNRows = GetOptRowCount(g_pauseOptCat, true);
        int popRowH = ComputePauseOptRowH(yBase, popNRows);
        int popRightBound = xBase + (2 * kMenuWideW) - 6 + xOffset - static_cast<int>(std::lround(subSwipeX));
        int popRowTopY = yBase + slideY + 0x75 + g_optUi.popupRow * popRowH;
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
    Game_DrawQuad(innerX, innerY, w, h, innerRgba);

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

static void __declspec(naked) hk_pausePrismGridX() {
    __asm {
        lea edx, [ecx + eax * 2 + 0x30]
        push edx
        mov BYTE PTR [ebp - 0x3d], 0xff
        mov eax, 0x4ae3fa
        jmp eax
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
        mov eax, ebx
        cdq
        mov ecx, 6
        idiv ecx
        imul eax, eax, 0x2d
        sub eax, DWORD PTR [edi + 0x50]
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

} // namespace

namespace edgemod {

bool GetHudTimerMenuPreview(uint32_t state, uint8_t* outMode, float* outSize, uint8_t* outPos, bool* outForceSample) {
    if (outMode == nullptr || outSize == nullptr || outPos == nullptr || outForceSample == nullptr) {
        return false;
    }
    *outMode = g_timerMode;
    *outSize = g_timerSize;
    *outPos  = g_timerPos;
    *outForceSample = false;

    if (state == 9u) {
        uint8_t* sm = GetScreenMgr();
        if (sm == nullptr) return false;
        uint8_t* menuObj = *reinterpret_cast<uint8_t**>(sm + 0x4c);
        if (menuObj == nullptr) return false;
        int exitTimer = *reinterpret_cast<const int*>(menuObj + 8);
        if (exitTimer >= 0) return false;
        if (g_mainOptCat != OPT_CAT_TIMER) return false;

        OptItem items[kMaxOptRows];
        int nRows = GetOptItems(OPT_CAT_TIMER, false, items);
        OptItem activeItem = OPT_NONE;
        if (g_optUi.popupOpen && !g_optUi.popupInPause) {
            activeItem = g_optUi.popupItem;
        } else {
            int selRow = g_optUi.mainSelRow;
            if (selRow >= 0 && selRow < nRows) activeItem = items[selRow];
        }
        if (activeItem == OPT_TIMER || activeItem == OPT_TIMER_SIZE || activeItem == OPT_TIMER_POS) {
            *outForceSample = true;
            if (g_optUi.popupOpen && !g_optUi.popupInPause && g_optUi.popupItem == OPT_TIMER) {
                *outMode = static_cast<uint8_t>(std::clamp(g_optUi.popupHighlight, 0, 3));
            } else if (g_optUi.popupOpen && !g_optUi.popupInPause && g_optUi.popupItem == OPT_TIMER_POS) {
                *outPos = static_cast<uint8_t>(std::clamp(g_optUi.popupHighlight, 0, 3));
                *outMode = (g_timerMode == 0) ? 3u : g_timerMode;
            } else if (activeItem == OPT_TIMER_SIZE || activeItem == OPT_TIMER_POS) {
                *outMode = (g_timerMode == 0) ? 3u : g_timerMode;
            } else {
                *outMode = g_timerMode;
            }
            return (*outMode != 0);
        }
        return false;
    }

    if (state == 0x12u && g_optUi.popupOpen && g_optUi.popupInPause) {
        if (g_optUi.popupItem == OPT_TIMER) {
            *outMode = static_cast<uint8_t>(std::clamp(g_optUi.popupHighlight, 0, 3));
        } else if (g_optUi.popupItem == OPT_TIMER_POS) {
            *outPos = static_cast<uint8_t>(std::clamp(g_optUi.popupHighlight, 0, 3));
        }
    }
    return (*outMode != 0);
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

        const uint8_t exSave[8] = {0x8d, 0x4d, 0xb0, 0xe8, 0x11, 0xa1, 0x08, 0x00};
        RegRelCall(0x4926e7, exSave, 8, 8, &hk_saveTtsav);
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

            static const uint8_t exPgx[8] = {0x8d, 0x14, 0x41, 0x52, 0xc6, 0x45, 0xc3, 0xff};
            RegRelJmp(0x4ae3f2, exPgx, 8, 5, &hk_pausePrismGridX, 0x90, 0x4ae3fa);

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
