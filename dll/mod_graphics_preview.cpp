#include "mod_state.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace edgemod {
uint8_t g_previewLevelTheme = 0; // 0=light 1, 1=light 2, 2=dark 1, 3=dark 2
} // namespace edgemod

namespace {

using namespace edgemod;

constexpr int32_t kPreviewSpawnX = 7;
constexpr int32_t kPreviewSpawnZ = 10;
constexpr int32_t kPreviewSpawnY = 1;

constexpr int32_t kPreviewDarkX = 5;
constexpr int32_t kPreviewDarkZ = 11;
constexpr int32_t kPreviewDarkY = 1;

constexpr int32_t kPreviewFinishX = 11;
constexpr int32_t kPreviewFinishZ = 12;
constexpr int32_t kPreviewFinishY = 1; // resting on finish tile (11, 12, 1)

constexpr int32_t kPreviewFinishDurationTicks = 95;

struct PreviewState {
    bool             inMainGraphics     = false;
    bool             previewLoaded      = false;
    bool             previewLoading     = false;
    bool             inWorldUpdate      = false;
    uint8_t          loadedTheme        = 0;
    uint8_t          targetTheme        = 0;

    PreviewHoverKind hoveredKind        = PREVIEW_HOVER_NONE;
    bool             popupOpen          = false;
    PreviewHoverKind popupKind          = PREVIEW_HOVER_NONE;
    bool             popupSlotVisible   = false;
    int              popupHighlight     = -1;

    bool             fileValidated      = false;
    bool             fileValid          = false;
    FILETIME         lastWriteTime{};
    DWORD            lastFileSize       = 0;

    bool             edgeTimerActive    = false;
    int32_t          edgeTimerTicks     = 1;

    bool             finishActive       = false;
    int32_t          finishTicks        = 0;
    bool             pendingFinishTrig  = false;

    bool             hadThemeOverride   = false;
    CustomThemeSettings lastEffTheme{};
    PreviewHoverKind lastPopupFinishKind = PREVIEW_HOVER_NONE;
    int              lastPopupFinishHi   = -1;

    bool             savedMenuState     = false;
    bool             hasSavedMenuPose   = false;
    void*            savedMenuSkybox[2] = {nullptr, nullptr};
    float            savedMenuFovDeg    = 1.0f;
    float            savedMenuNear      = 39000.0f;
    float            savedMenuFar       = 41500.0f;
    float            savedMenuPos[3]    = {0.0f, 0.0f, 0.0f};
    float            savedMenuLookAt[3] = {0.0f, 0.0f, 0.0f};
    uint8_t          savedSmFovSeg[24]  = {};
};

PreviewState g_pv{};

struct BinCursor {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    size_t         pos  = 0;

    bool skip(size_t n) {
        if (n > size - pos) return false;
        pos += n;
        return true;
    }
    bool u8(uint8_t* out) {
        if (pos + 1 > size) return false;
        *out = data[pos++];
        return true;
    }
    bool u16(uint16_t* out) {
        if (pos + 2 > size) return false;
        *out = static_cast<uint16_t>(data[pos]) | (static_cast<uint16_t>(data[pos + 1]) << 8);
        pos += 2;
        return true;
    }
    bool i16(int16_t* out) {
        uint16_t u = 0;
        if (!u16(&u)) return false;
        *out = static_cast<int16_t>(u);
        return true;
    }
    bool u32(uint32_t* out) {
        if (pos + 4 > size) return false;
        *out = static_cast<uint32_t>(data[pos])
             | (static_cast<uint32_t>(data[pos + 1]) << 8)
             | (static_cast<uint32_t>(data[pos + 2]) << 16)
             | (static_cast<uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return true;
    }
};

static bool BuildPreviewFilePathW(wchar_t* out, size_t cap) {
    wchar_t exePath[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    wchar_t* slash = nullptr;
    for (DWORD i = 0; i < n; ++i) {
        if (exePath[i] == L'\\' || exePath[i] == L'/') slash = &exePath[i];
    }
    if (slash == nullptr) return false;
    size_t dirLen = static_cast<size_t>(slash - exePath + 1);
    static constexpr wchar_t kSuffix[] = L"levels\\graphics_preview.bin";
    constexpr size_t kSufLen = sizeof(kSuffix) / sizeof(wchar_t);
    if (dirLen + kSufLen > cap) return false;
    std::memcpy(out, exePath, dirLen * sizeof(wchar_t));
    std::memcpy(out + dirLen, kSuffix, kSufLen * sizeof(wchar_t));
    return true;
}

static bool ParseAndValidatePreviewBin(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 64u || size > 1048576u) return false;
    BinCursor r{data, size, 0};

    uint32_t levelId = 0, nameLen = 0;
    if (!r.u32(&levelId) || !r.u32(&nameLen) || nameLen > 256u || !r.skip(nameLen)) return false;
    if (!r.skip(10 + 2)) return false; // 5 rank u16 + totalPrisms i16

    uint8_t heightTiles = 0;
    uint16_t sizeX = 0, sizeZ = 0;
    if (!r.u8(&heightTiles) || !r.u16(&sizeX) || !r.u16(&sizeZ)) return false;
    if (heightTiles == 0 || heightTiles > 64u || sizeX == 0 || sizeX > 128u || sizeZ == 0 || sizeZ > 128u) {
        return false;
    }
    if (!r.skip(4)) return false; // 2 header shorts

    uint16_t mmW = 0, mmH = 0;
    if (!r.u16(&mmW) || !r.u16(&mmH) || mmW > 512u || mmH > 512u) return false;
    if (!r.skip(5)) return false;
    size_t mmBytes = (static_cast<size_t>(mmW) * static_cast<size_t>(mmH) + 7u) / 8u;
    if (!r.skip(mmBytes)) return false;

    size_t layerBytes = (static_cast<size_t>(sizeX) * static_cast<size_t>(sizeZ) + 7u) / 8u;
    if (!r.skip(layerBytes * static_cast<size_t>(heightTiles))) return false;

    int16_t spX = 0, spZ = 0, spY = 0, camZoom = 0;
    if (!r.i16(&spX) || !r.i16(&spZ) || !r.i16(&spY) || !r.i16(&camZoom)) return false;
    if (spX < 0 || spX >= static_cast<int16_t>(sizeX) ||
        spZ < 0 || spZ >= static_cast<int16_t>(sizeZ) ||
        spY < 1 || spY > static_cast<int16_t>(heightTiles) + 1) {
        return false;
    }
    if (camZoom < 0 && !r.skip(3)) return false;

    int16_t finX = 0, finZ = 0, finY = 0;
    if (!r.i16(&finX) || !r.i16(&finZ) || !r.i16(&finY)) return false;
    if (finX < 0 || finX >= static_cast<int16_t>(sizeX) ||
        finZ < 0 || finZ >= static_cast<int16_t>(sizeZ) ||
        finY < 1 || finY > static_cast<int16_t>(heightTiles)) {
        return false;
    }

    int16_t numMoving = 0;
    if (!r.i16(&numMoving) || numMoving < 0 || numMoving > 512) return false;
    for (int i = 0; i < numMoving; ++i) {
        uint8_t nWp = 0;
        if (!r.skip(5) || !r.u8(&nWp)) return false;
        if (!r.skip(static_cast<size_t>(nWp) * 10u)) return false;
    }

    int16_t numBumpers = 0;
    if (!r.i16(&numBumpers) || numBumpers < 0 || numBumpers > 512) return false;
    if (!r.skip(static_cast<size_t>(numBumpers) * 23u)) return false;

    int16_t numFalling = 0;
    if (!r.i16(&numFalling) || numFalling < 0 || numFalling > 1024) return false;
    if (!r.skip(static_cast<size_t>(numFalling) * 8u)) return false;

    int16_t numCk = 0;
    if (!r.i16(&numCk) || numCk < 0 || numCk > 512) return false;
    if (!r.skip(static_cast<size_t>(numCk) * 10u)) return false;

    int16_t numResizers = 0;
    if (!r.i16(&numResizers) || numResizers < 0 || numResizers > 512) return false;
    for (int i = 0; i < numResizers; ++i) {
        uint16_t trig = 0;
        if (!r.skip(6) || !r.u16(&trig) || !r.skip(2)) return false;
        if (trig == 0xffffu && !r.skip(9)) return false;
    }

    int16_t numPrisms = 0;
    if (!r.i16(&numPrisms) || numPrisms < 0 || numPrisms > 512) return false;
    if (!r.skip(static_cast<size_t>(numPrisms) * 7u)) return false;

    int16_t skipped = 0, numTriggers = 0;
    if (!r.i16(&skipped) || !r.i16(&numTriggers) || numTriggers < 0 || numTriggers > 1024) return false;
    if (!r.skip(static_cast<size_t>(numTriggers) * 5u)) return false;

    int16_t numSwitches = 0;
    if (!r.i16(&numSwitches) || numSwitches < 0 || numSwitches > 512) return false;
    for (int i = 0; i < numSwitches; ++i) {
        uint8_t mode = 0;
        if (!r.skip(7) || !r.u8(&mode)) return false;
        if (mode == 0u) {
            if (!r.skip(6)) return false;
        } else if (mode == 1u) {
            if (!r.skip(2)) return false;
        }
        int16_t tCnt = 0;
        if (!r.i16(&tCnt) || tCnt < 0 || tCnt > 512) return false;
        if (!r.skip(static_cast<size_t>(tCnt) * 2u)) return false;
    }

    int16_t numHolo = 0;
    if (!r.i16(&numHolo) || numHolo < 0 || numHolo > 256) return false;
    for (int i = 0; i < numHolo; ++i) {
        uint16_t h0 = 0;
        if (!r.skip(6) || !r.u16(&h0)) return false;
        if (h0 == 0xfffeu && !r.skip(4)) return false;
        int16_t h1 = 0;
        if (!r.i16(&h1) || h1 < 0 || h1 > 2048) return false;
        if (!r.skip(6)) return false;
        if (!r.skip(static_cast<size_t>(h1) * 4u)) return false;
    }

    int16_t numShrinkers = 0;
    if (!r.i16(&numShrinkers) || numShrinkers < 0 || numShrinkers > 512) return false;
    if (!r.skip(static_cast<size_t>(numShrinkers) * 8u)) return false;

    if (!r.skip(5)) return false; // ignored short + theme + unk + music
    return r.pos == r.size;
}

static bool ValidatePreviewLevelFile() {
    wchar_t path[MAX_PATH]{};
    if (!BuildPreviewFilePathW(path, MAX_PATH)) {
        g_pv.fileValidated = true;
        g_pv.fileValid = false;
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad) ||
        (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        fad.nFileSizeHigh != 0 ||
        fad.nFileSizeLow < 64u ||
        fad.nFileSizeLow > 1048576u) {
        g_pv.fileValidated = true;
        g_pv.fileValid = false;
        return false;
    }
    if (g_pv.fileValidated &&
        g_pv.lastFileSize == fad.nFileSizeLow &&
        g_pv.lastWriteTime.dwLowDateTime == fad.ftLastWriteTime.dwLowDateTime &&
        g_pv.lastWriteTime.dwHighDateTime == fad.ftLastWriteTime.dwHighDateTime) {
        return g_pv.fileValid;
    }

    g_pv.lastFileSize = fad.nFileSizeLow;
    g_pv.lastWriteTime = fad.ftLastWriteTime;
    g_pv.fileValidated = true;
    g_pv.fileValid = false;

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::vector<uint8_t> buf(fad.nFileSizeLow);
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(h, buf.data(), fad.nFileSizeLow, &bytesRead, nullptr);
    CloseHandle(h);
    if (!ok || bytesRead != fad.nFileSizeLow) return false;

    g_pv.fileValid = ParseAndValidatePreviewBin(buf.data(), buf.size());
    return g_pv.fileValid;
}

static void ClearSparkEmitter(uint8_t* ws) {
    if (ws == nullptr) return;
    uint8_t* sm = GetScreenMgr();
    uint8_t* spark = *reinterpret_cast<uint8_t**>(ws + 0xf8);
    if (spark == nullptr) return;
    if (sm != nullptr) {
        void* sparkMgr = *reinterpret_cast<void**>(sm + 0x3c);
        if (sparkMgr != nullptr) {
            reinterpret_cast<void(__thiscall*)(void*, void*)>(0x4ea9a0)(sparkMgr, ws + 0xf8);
        }
    }
    uint8_t* ctrl = *reinterpret_cast<uint8_t**>(ws + 0xfc);
    *reinterpret_cast<void**>(ws + 0xfc) = nullptr;
    *reinterpret_cast<void**>(ws + 0xf8) = nullptr;
    if (ctrl != nullptr) {
        if (InterlockedDecrement(reinterpret_cast<LONG*>(ctrl + 4)) == 0) {
            auto** vt = *reinterpret_cast<void***>(ctrl);
            reinterpret_cast<void(__thiscall*)(void*)>(vt[0])(ctrl);
            if (InterlockedDecrement(reinterpret_cast<LONG*>(ctrl + 8)) == 0) {
                reinterpret_cast<void(__thiscall*)(void*)>(vt[1])(ctrl);
            }
        }
    }
}

static void ResetCubeToTile(uint8_t* cube, int32_t gx, int32_t gz, int32_t gy) {
    if (cube == nullptr) return;
    int32_t pos[3] = {gx, gz, gy};
    reinterpret_cast<void(__thiscall*)(void*, const int32_t*, uint8_t)>(0x48dc30)(cube, pos, 0);
    *reinterpret_cast<uint32_t*>(cube + 0x16c) = 0u;
}

static void ClearCubeInterpFinishState(uint8_t* cube) {
    if (cube == nullptr) return;
    *reinterpret_cast<int32_t*>(cube + 0x1a0) = 0;
    cube[0x171] = 0;
    uint8_t* lvl = GetLevel();
    if (lvl != nullptr) {
        int32_t id = *reinterpret_cast<const int32_t*>(cube);
        int32_t interpIdx = *reinterpret_cast<const int32_t*>(lvl + 0xa4);
        uint8_t* interpBase = *reinterpret_cast<uint8_t**>(lvl + interpIdx * 4 + 0xa8);
        if (interpBase != nullptr) {
            interpBase[id * 68 + 0x0d] = 1;
            interpBase[id * 68 + 0x40] = 0;
        }
    }
}

static void EnsurePreviewDarkCubeVisible(uint8_t* ws) {
    if (ws == nullptr || ws[0x2120] == 0) return;
    ws[0x48] = 1; // always solid
    ws[0x50] = 1; // visible
    *reinterpret_cast<int32_t*>(ws + 0x44) = -1;
    *reinterpret_cast<int32_t*>(ws + 0x64) = 0;
    *reinterpret_cast<int32_t*>(ws + 0x68) = 0;
    uint8_t* darkCube = *reinterpret_cast<uint8_t**>(ws + 0x38);
    uint8_t* lvl = GetLevel();
    if (darkCube != nullptr && lvl != nullptr) {
        int32_t id = *reinterpret_cast<const int32_t*>(darkCube);
        int32_t interpIdx = *reinterpret_cast<const int32_t*>(lvl + 0xa4);
        uint8_t* interpBase = *reinterpret_cast<uint8_t**>(lvl + interpIdx * 4 + 0xa8);
        if (interpBase != nullptr) {
            interpBase[id * 68 + 0x0d] = 1;
            interpBase[id * 68 + 0x40] = 0;
        }
    }
}

static void ApplyPreviewPlayerPrismBuff(uint8_t* ws) {
    if (ws == nullptr || ws[0x2120] == 0) return;
    uint8_t* playerCube = *reinterpret_cast<uint8_t**>(ws + 0x34);
    if (playerCube == nullptr) return;
    *reinterpret_cast<int32_t*>(playerCube + 0x160) = 0; // phase 2 prism buff (0/22 rolls used -> fast rainbow)
    *reinterpret_cast<int32_t*>(playerCube + 0x168) = 0; // phase 1 white strobe off
}

static void ResetPreviewFinishState(uint8_t* ws) {
    g_pv.finishActive = false;
    g_pv.finishTicks = 0;
    ResetCustomFinishEffectState();
    if (ws == nullptr || ws[0x2120] == 0) return;
    ws[0xb5] = 0;
    *reinterpret_cast<int32_t*>(ws + 0xb8) = 0;
    *reinterpret_cast<int32_t*>(ws + 0xbc) = 0;
    ClearSparkEmitter(ws);
    uint8_t* playerCube = *reinterpret_cast<uint8_t**>(ws + 0x34);
    if (playerCube != nullptr) {
        ClearCubeInterpFinishState(playerCube);
        ResetCubeToTile(playerCube, kPreviewSpawnX, kPreviewSpawnZ, kPreviewSpawnY);
    }
    EnsurePreviewDarkCubeVisible(ws);
    ApplyPreviewPlayerPrismBuff(ws);
    auto** vt = *reinterpret_cast<void***>(ws);
    reinterpret_cast<void(__thiscall*)(void*, float)>(vt[4])(ws, 1.0f);
}

static void ResetPreviewEdgeTimerState(uint8_t* ws) {
    g_pv.edgeTimerActive = false;
    g_pv.edgeTimerTicks = 1;
    if (ws == nullptr || ws[0x2120] == 0) return;
    ws[0x6c] = 0;
    ws[0x6d] = 0;
    *reinterpret_cast<int32_t*>(ws + 0x70) = 0;
    *reinterpret_cast<int32_t*>(ws + 0x74) = 0;
    *reinterpret_cast<int32_t*>(ws + 0x78) = 0;
    *reinterpret_cast<int32_t*>(ws + 0x7c) = 0;
    *reinterpret_cast<int32_t*>(ws + 0x80) = 0;
    *reinterpret_cast<int32_t*>(ws + 0x84) = 0;
}

static void ApplyPreviewEdgeTimerState(uint8_t* ws) {
    if (ws == nullptr || ws[0x2120] == 0) return;
    uint8_t* playerCube = *reinterpret_cast<uint8_t**>(ws + 0x34);
    uint8_t* darkCube   = *reinterpret_cast<uint8_t**>(ws + 0x38);
    if (playerCube == nullptr) return;

    ws[0x6c] = 1;
    *reinterpret_cast<int32_t*>(ws + 0x70) = 1; // right-side 2D offset
    *reinterpret_cast<int32_t*>(ws + 0x78) = g_pv.edgeTimerTicks;
    *reinterpret_cast<int32_t*>(ws + 0x80) = 30; // full opacity / rise
    *reinterpret_cast<int32_t*>(playerCube + 0x18c) = 2; // axis Z
    *reinterpret_cast<int32_t*>(playerCube + 0x190) = 1; // dir +1 -> dirCode 2 (facing camera)
    reinterpret_cast<void(__thiscall*)(void*, int32_t)>(0x47b690)(ws, 0);

    if (darkCube != nullptr) {
        ws[0x6d] = 1;
        *reinterpret_cast<int32_t*>(ws + 0x74) = 0; // left-side 2D offset
        *reinterpret_cast<int32_t*>(ws + 0x7c) = g_pv.edgeTimerTicks;
        *reinterpret_cast<int32_t*>(ws + 0x84) = 30; // full opacity / rise
        *reinterpret_cast<int32_t*>(darkCube + 0x18c) = 2; // axis Z
        *reinterpret_cast<int32_t*>(darkCube + 0x190) = 1; // dir +1 -> dirCode 2 (facing camera)
        reinterpret_cast<void(__thiscall*)(void*, int32_t)>(0x47b690)(ws, 1);
    }
}

static void SpawnPreviewDarkCube(uint8_t* ws) {
    if (ws == nullptr || ws[0x2120] == 0) return;
    uint8_t* darkCube = *reinterpret_cast<uint8_t**>(ws + 0x38);
    if (darkCube == nullptr) return;
    ResetCubeToTile(darkCube, kPreviewDarkX, kPreviewDarkZ, kPreviewDarkY);
    EnsurePreviewDarkCubeVisible(ws);
}

static void ExecuteTriggerFinishEffect(uint8_t* ws) {
    if (ws == nullptr || ws[0x2120] == 0) return;
    uint8_t* playerCube = *reinterpret_cast<uint8_t**>(ws + 0x34);
    if (playerCube == nullptr) return;

    ClearSparkEmitter(ws);
    ResetCustomFinishEffectState();
    ws[0xb5] = 0;
    *reinterpret_cast<int32_t*>(ws + 0xb8) = 0;
    *reinterpret_cast<int32_t*>(ws + 0xbc) = 0;

    ClearCubeInterpFinishState(playerCube);
    ResetCubeToTile(playerCube, kPreviewFinishX, kPreviewFinishZ, kPreviewFinishY);
    EnsurePreviewDarkCubeVisible(ws);
    ApplyPreviewPlayerPrismBuff(ws);
    *reinterpret_cast<int32_t*>(playerCube + 0x04) = 0; // idle so Cube::onWin animates cleanly
    auto** vt = *reinterpret_cast<void***>(ws);
    reinterpret_cast<void(__thiscall*)(void*, float)>(vt[4])(ws, 1.0f);

    uint8_t* sm = GetScreenMgr();
    float savedMusicDuck = (sm != nullptr) ? *reinterpret_cast<const float*>(sm + 0x98) : 1.0f;

    bool prevInUpdate = g_pv.inWorldUpdate;
    g_pv.inWorldUpdate = true;
    reinterpret_cast<void(__thiscall*)(void*)>(0x48e060)(playerCube); // Cube::onWin
    g_pv.inWorldUpdate = prevInUpdate;

    if (sm != nullptr) {
        *reinterpret_cast<float*>(sm + 0x98) = savedMusicDuck;
    }

    // Never drift camera away from the overview shot and suppress light-adaptation flash
    EnsurePreviewDarkCubeVisible(ws);
    *reinterpret_cast<int32_t*>(ws + 0xbc) = 0;
    *reinterpret_cast<uint32_t*>(playerCube + 0x16c) = 0u;
    reinterpret_cast<void(__thiscall*)(void*, float)>(vt[4])(ws, 0.0f);

    g_pv.finishActive = true;
    g_pv.finishTicks = 0;
}

static uint8_t GetDesiredPreviewTheme() {
    if (g_pv.popupOpen && g_pv.popupKind == PREVIEW_HOVER_LEVEL &&
        g_pv.popupSlotVisible && g_pv.popupHighlight >= 0 && g_pv.popupHighlight <= 3) {
        return static_cast<uint8_t>(g_pv.popupHighlight);
    }
    return (g_previewLevelTheme <= 3u) ? g_previewLevelTheme : 0u;
}

static bool IsMenuBackgroundState(uint32_t state) {
    return state >= 2u && state <= 15u;
}

static void SetLightVectorEnabled(uint8_t* vecBase, bool enabled) {
    if (vecBase == nullptr) return;
    uint8_t* it  = *reinterpret_cast<uint8_t**>(vecBase);
    uint8_t* end = *reinterpret_cast<uint8_t**>(vecBase + 4);
    if (it == nullptr || end == nullptr || it >= end) return;
    for (; it < end; it += 8u) {
        void* light = *reinterpret_cast<void**>(it);
        if (light != nullptr) {
            reinterpret_cast<void(__thiscall*)(void*, uint8_t)>(0x4e98a0)(light, enabled ? 1u : 0u);
            if (enabled) {
                reinterpret_cast<void(__thiscall*)(void*)>(0x4e9810)(light);
            }
        }
    }
}

static float ReadConfigFloatByName(const char* keyName, float fallback) {
    void** cfgHolder = reinterpret_cast<void**(__cdecl*)()>(0x4764c0)();
    if (cfgHolder == nullptr || *cfgHolder == nullptr || keyName == nullptr) return fallback;
    EdgeString keyStr{};
    keyStr.ctor_cstr(keyName);
    uint32_t handle = 0;
    reinterpret_cast<uint32_t*(__thiscall*)(void*, uint32_t*, const EdgeString*)>(0x440d60)(
        *cfgHolder, &handle, &keyStr
    );
    float val = reinterpret_cast<float(__thiscall*)(void*, uint32_t)>(0x4408c0)(*cfgHolder, handle);
    keyStr.dtor();
    return std::isfinite(val) ? val : fallback;
}

static void SaveMenuBackgroundStateIfNeeded() {
    if (g_pv.savedMenuState) return;
    uint8_t* renderer = *reinterpret_cast<uint8_t**>(0x5f8770);
    if (renderer != nullptr) {
        reinterpret_cast<void*(__thiscall*)(void*, const void*)>(0x4ecfd0)(g_pv.savedMenuSkybox, renderer + 0x58);
    }

    float cfgNear = ReadConfigFloatByName("edge.ingame.camera.menu_near_plane", -1.0f);
    float cfgFar  = ReadConfigFloatByName("edge.ingame.camera.menu_far_plane", -1.0f);
    float cfgFov  = ReadConfigFloatByName("edge.ingame.camera.menu_fov", -1.0f);
    if (cfgFov >= 0.1f && cfgFov <= 120.0f) g_pv.savedMenuFovDeg = cfgFov;
    if (cfgNear > 0.0f && cfgFar > cfgNear) {
        g_pv.savedMenuNear = cfgNear;
        g_pv.savedMenuFar  = cfgFar;
    }

    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        std::memcpy(g_pv.savedSmFovSeg, sm + 0x78, sizeof(g_pv.savedSmFovSeg));
        uint32_t slot = *reinterpret_cast<const uint32_t*>(sm + 0x330);
        if (slot > 1u) slot = 0u;
        uint8_t* cam = *reinterpret_cast<uint8_t**>(sm + 0x30c + slot * 12u);
        if (cam != nullptr) {
            if (cfgFov < 0.1f || cfgFov > 120.0f) {
                float fovDeg = *reinterpret_cast<const float*>(cam + 0x1c0) * 57.29577951308232f;
                if (fovDeg >= 0.1f && fovDeg <= 120.0f) g_pv.savedMenuFovDeg = fovDeg;
            }
            if (cfgNear <= 0.0f || cfgFar <= cfgNear) {
                float nearP = *reinterpret_cast<const float*>(cam + 0x1c4);
                float farP  = *reinterpret_cast<const float*>(cam + 0x1c8);
                if (nearP > 0.0f && farP > nearP) {
                    g_pv.savedMenuNear = nearP;
                    g_pv.savedMenuFar  = farP;
                }
            }
            std::memcpy(g_pv.savedMenuPos, cam + 0x108, 12);
            std::memcpy(g_pv.savedMenuLookAt, cam + 0x208, 12);
            g_pv.hasSavedMenuPose = true;
        }
    }
    g_pv.savedMenuState = true;
}

static void RestoreMenuBackgroundState(bool restoreToScene) {
    if (!g_pv.savedMenuState) return;
    if (restoreToScene) {
        uint8_t* renderer = *reinterpret_cast<uint8_t**>(0x5f8770);
        if (renderer != nullptr) {
            reinterpret_cast<void*(__thiscall*)(void*, const void*)>(0x4ecfd0)(renderer + 0x58, g_pv.savedMenuSkybox);
            const uint8_t blackClear[4] = {0u, 0u, 0u, 255u};
            reinterpret_cast<void(__thiscall*)(void*, const uint8_t*)>(0x405f90)(renderer, blackClear);
        }
        uint8_t* ms = *reinterpret_cast<uint8_t**>(0x5f91b4);
        if (ms != nullptr) {
            SetLightVectorEnabled(ms + 0x170, true);
        }
        uint8_t* sm = GetScreenMgr();
        if (sm != nullptr) {
            std::memcpy(sm + 0x78, g_pv.savedSmFovSeg, sizeof(g_pv.savedSmFovSeg));
            static constexpr size_t kCamOffsets[3] = {0x30c, 0x318, 0x324};
            for (size_t off : kCamOffsets) {
                uint8_t* cam = *reinterpret_cast<uint8_t**>(sm + off);
                if (cam != nullptr) {
                    if (g_pv.hasSavedMenuPose) {
                        std::memcpy(cam + 0x108, g_pv.savedMenuPos, 12);
                        *reinterpret_cast<uint32_t*>(cam + 0x198) |= 1u;
                        std::memcpy(cam + 0x208, g_pv.savedMenuLookAt, 12);
                        cam[0x320] = 1u;
                    }
                    reinterpret_cast<void(__thiscall*)(void*, float, float)>(0x423840)(
                        cam, g_pv.savedMenuNear, g_pv.savedMenuFar
                    );
                    reinterpret_cast<void(__thiscall*)(void*, float)>(0x423780)(cam, g_pv.savedMenuFovDeg);
                    void* dummySp[2] = {nullptr, nullptr};
                    auto** vt = *reinterpret_cast<void***>(cam);
                    reinterpret_cast<void(__thiscall*)(void*, void*, int)>(vt[3])(cam, dummySp, 0);
                    reinterpret_cast<void(__thiscall*)(void*)>(0x421ff0)(dummySp);
                }
            }
            sm[0x315] = 0;
            sm[0x321] = 0;
            reinterpret_cast<void(__thiscall*)(void*)>(0x4a0320)(sm + 0x324);
        }
    }
    void* nullSp[2] = {nullptr, nullptr};
    reinterpret_cast<void*(__thiscall*)(void*, const void*)>(0x4ecfd0)(g_pv.savedMenuSkybox, nullSp);
    g_pv.savedMenuState = false;
    g_pv.hasSavedMenuPose = false;
}

static bool LoadPreviewLevelInternal(uint8_t* ws, uint8_t theme) {
    if (ws == nullptr) return false;
    SaveMenuBackgroundStateIfNeeded();

    uint8_t* ms = *reinterpret_cast<uint8_t**>(0x5f91b4);
    if (ms != nullptr) {
        SetLightVectorEnabled(ms + 0x170, false);
    }

    if (ws[0x2120] != 0) {
        bool prev = g_pv.inWorldUpdate;
        g_pv.inWorldUpdate = true;
        SetLightVectorEnabled(ws + 0x212c, false);
        reinterpret_cast<void(__thiscall*)(void*, uint8_t)>(0x47e2d0)(ws, 0);
        void* nullModel[2] = {nullptr, nullptr};
        reinterpret_cast<void*(__thiscall*)(void*, const void*)>(0x43e9c0)(ws + 0x213c, nullModel);
        g_pv.inWorldUpdate = prev;
    }

    EdgeString fileStr{};
    EdgeString dispStr{};
    fileStr.ctor_cstr("graphics_preview");
    dispStr.ctor_cstr("");

    uint8_t levelInfo[0x40]{};
    reinterpret_cast<void(__thiscall*)(void*, const EdgeString*, int32_t, const EdgeString*, int32_t)>(0x4833e0)(
        levelInfo, &fileStr, -1, &dispStr, 4
    );

    g_pv.targetTheme = theme;
    g_pv.previewLoading = true;
    bool prevUpdate = g_pv.inWorldUpdate;
    g_pv.inWorldUpdate = true;

    reinterpret_cast<void(__thiscall*)(void*, void*, uint8_t, uint8_t)>(0x47d3b0)(ws, levelInfo, 0, 1);

    bool ok = (ws[0x2120] != 0 && GetLevel() != nullptr);
    if (ok) {
        reinterpret_cast<void(__thiscall*)(void*)>(0x47f940)(ws); // WorldScreen::start
        *reinterpret_cast<int32_t*>(ws + 0x211c) = 0;           // no flash on load
        uint8_t* playerCube = *reinterpret_cast<uint8_t**>(ws + 0x34);
        if (playerCube != nullptr) {
            *reinterpret_cast<uint32_t*>(playerCube + 0x16c) = 0u;
        }
        ResetPreviewCubeFxWhiteFlash();
        SpawnPreviewDarkCube(ws);
        ApplyPreviewPlayerPrismBuff(ws);
    } else if (ws[0x2120] != 0) {
        SetLightVectorEnabled(ws + 0x212c, false);
        reinterpret_cast<void(__thiscall*)(void*, uint8_t)>(0x47e2d0)(ws, 0);
        void* nullModel[2] = {nullptr, nullptr};
        reinterpret_cast<void*(__thiscall*)(void*, const void*)>(0x43e9c0)(ws + 0x213c, nullModel);
    }

    g_pv.inWorldUpdate = prevUpdate;
    g_pv.previewLoading = false;

    reinterpret_cast<void(__thiscall*)(void*)>(0x482f80)(levelInfo);
    dispStr.dtor();
    fileStr.dtor();

    if (!ok) {
        g_pv.fileValid = false;
        g_pv.previewLoaded = false;
        RestoreMenuBackgroundState(true);
        return false;
    }

    g_pv.previewLoaded = true;
    g_pv.loadedTheme = theme;
    g_pv.finishActive = false;
    g_pv.finishTicks = 0;
    g_pv.edgeTimerActive = false;
    g_pv.edgeTimerTicks = 1;
    SyncEffectiveThemeGlobals();

    // Prime camera and entity interpolation immediately so frame 0 has no menu-camera flash
    reinterpret_cast<void(__thiscall*)(void*)>(0x47b8d0)(ws);
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        uint32_t curSlot = *reinterpret_cast<const uint32_t*>(sm + 0x330);
        if (curSlot <= 1u) {
            uint32_t otherSlot = curSlot ^ 1u;
            reinterpret_cast<void(__thiscall*)(void*, void*)>(0x4a01f0)(
                sm + 0x30c + otherSlot * 12u, sm + 0x30c + curSlot * 12u
            );
        }
        sm[0x315] = 0;
        sm[0x321] = 0;
    }
    auto** vt = *reinterpret_cast<void***>(ws);
    reinterpret_cast<void(__thiscall*)(void*, float)>(vt[4])(ws, 1.0f);
    return true;
}

static void SyncGraphicsPreviewLifecycle(uint8_t* sm) {
    if (sm == nullptr) return;
    uint32_t state = *reinterpret_cast<const uint32_t*>(sm + 0x50);
    uint8_t* ws = GetWorldScreen();
    bool want = (state == 9u && g_pv.inMainGraphics && ValidatePreviewLevelFile());
    if (want) {
        uint8_t desiredTheme = GetDesiredPreviewTheme();
        if (!g_pv.previewLoaded || g_pv.loadedTheme != desiredTheme || ws == nullptr || ws[0x2120] == 0) {
            LoadPreviewLevelInternal(ws, desiredTheme);
        }
    } else if (g_pv.previewLoaded) {
        UnloadGraphicsPreviewIfActive();
    }
}

static void PreUpdatePreviewEntities(uint8_t* ws) {
    if (ws == nullptr || ws[0x2120] == 0) return;

    // 1. Trigger deferred finish effect safely inside WorldScreen update phase
    if (g_pv.pendingFinishTrig) {
        g_pv.pendingFinishTrig = false;
        ExecuteTriggerFinishEffect(ws);
    }
    if (g_pv.finishActive) {
        ++g_pv.finishTicks;
        *reinterpret_cast<int32_t*>(ws + 0xbc) = 0; // keep camera locked on overview
        if (g_pv.finishTicks >= kPreviewFinishDurationTicks) {
            ResetPreviewFinishState(ws);
        }
    }

    // 2. Edge timer preview while hovering edge timer row or its popup
    bool wantEdgeTimer = IsGraphicsPreviewHoveringEdgeTimer();
    uint8_t* playerCube = *reinterpret_cast<uint8_t**>(ws + 0x34);
    if (wantEdgeTimer && playerCube != nullptr) {
        if (!g_pv.edgeTimerActive) {
            g_pv.edgeTimerActive = true;
            g_pv.edgeTimerTicks = 1;
        }
        ApplyPreviewEdgeTimerState(ws);
    } else if (g_pv.edgeTimerActive) {
        ResetPreviewEdgeTimerState(ws);
    }
}

static void __cdecl OnScreenMgrUpdate_Impl(uint8_t* sm, float dt) {
    SyncGraphicsPreviewLifecycle(sm);
    if (!IsGraphicsPreviewActive()) return;
    uint8_t* ws = GetWorldScreen();
    if (ws == nullptr || ws[0x2120] == 0) return;

    g_pv.inWorldUpdate = true;
    PreUpdatePreviewEntities(ws);
    auto** vt = *reinterpret_cast<void***>(ws);
    reinterpret_cast<void(__thiscall*)(void*, float)>(vt[2])(ws, dt);
    g_pv.inWorldUpdate = false;
}

static void __cdecl OnScreenMgrRender_Impl(uint8_t* sm) {
    (void)sm;
    if (!IsGraphicsPreviewActive()) return;
    uint8_t* ws = GetWorldScreen();
    if (ws == nullptr || ws[0x2120] == 0) return;

    if (g_pv.edgeTimerActive && IsGraphicsPreviewHoveringEdgeTimer()) {
        ApplyPreviewEdgeTimerState(ws);
    }
    auto** vt = *reinterpret_cast<void***>(ws);
    reinterpret_cast<void(__thiscall*)(void*)>(vt[0])(ws);
}

static void __cdecl OnScreenMgrStepFixed_Impl(uint8_t* sm) {
    (void)sm;
    if (!IsGraphicsPreviewActive()) return;
    uint8_t* ws = GetWorldScreen();
    if (ws == nullptr || ws[0x2120] == 0) return;

    g_pv.inWorldUpdate = true;
    // Clear edge timer flags before Cube::update so 0x479950 never fires mid-tick;
    // OnSavePrev_Ext() has already snapshotted the previous tick's active timer state.
    ws[0x6c] = 0;
    ws[0x6d] = 0;
    auto** vt = *reinterpret_cast<void***>(ws);
    reinterpret_cast<void(__thiscall*)(void*)>(vt[3])(ws);
    EnsurePreviewDarkCubeVisible(ws);
    ApplyPreviewPlayerPrismBuff(ws);
    if (IsGraphicsPreviewHoveringEdgeTimer()) {
        if (!g_pv.edgeTimerActive) {
            g_pv.edgeTimerActive = true;
            g_pv.edgeTimerTicks = 1;
        } else {
            ++g_pv.edgeTimerTicks;
            if (g_pv.edgeTimerTicks > 132) {
                g_pv.edgeTimerTicks = 1;
            }
        }
        ApplyPreviewEdgeTimerState(ws);
    } else if (g_pv.edgeTimerActive) {
        ResetPreviewEdgeTimerState(ws);
    }
    g_pv.inWorldUpdate = false;
}

static void __cdecl OnScreenMgrInterp_Impl(uint8_t* sm, float alpha) {
    (void)sm;
    if (!IsGraphicsPreviewActive()) return;
    uint8_t* ws = GetWorldScreen();
    if (ws == nullptr || ws[0x2120] == 0) return;

    auto** vt = *reinterpret_cast<void***>(ws);
    reinterpret_cast<void(__thiscall*)(void*, float)>(vt[4])(ws, alpha);
}

static void UnloadGraphicsPreviewInternal(bool restoreMenuScene) {
    g_pv.pendingFinishTrig = false;
    g_pv.finishActive = false;
    g_pv.finishTicks = 0;
    ResetCustomFinishEffectState();
    g_pv.edgeTimerActive = false;
    g_pv.edgeTimerTicks = 1;
    g_pv.hadThemeOverride = false;
    g_pv.lastPopupFinishKind = PREVIEW_HOVER_NONE;
    g_pv.lastPopupFinishHi = -1;

    if (g_pv.previewLoaded) {
        g_pv.previewLoaded = false;
        uint8_t* ws = GetWorldScreen();
        if (ws != nullptr && ws[0x2120] != 0) {
            bool prev = g_pv.inWorldUpdate;
            g_pv.inWorldUpdate = true;
            SetLightVectorEnabled(ws + 0x212c, false);
            reinterpret_cast<void(__thiscall*)(void*, uint8_t)>(0x47e2d0)(ws, 0);
            void* nullModel[2] = {nullptr, nullptr};
            reinterpret_cast<void*(__thiscall*)(void*, const void*)>(0x43e9c0)(ws + 0x213c, nullModel);
            g_pv.inWorldUpdate = prev;
        }
    }
    RestoreMenuBackgroundState(restoreMenuScene);
    SyncEffectiveThemeGlobals();
}

static void* __cdecl LoadPreviewTerrainModel_Impl(
    void* outSp, const EdgeString* name, const EdgeString* folder, int a4, int a5
) {
    using Fn = void*(__cdecl*)(void*, const EdgeString*, const EdgeString*, int, int);
    auto orig = reinterpret_cast<Fn>(0x421ac0);
    if (!IsGraphicsPreviewLoading()) {
        return orig(outSp, name, folder, a4, a5);
    }
    EdgeString baseName{};
    baseName.ctor_cstr("level309");
    void* res = orig(outSp, &baseName, folder, a4, a5);
    baseName.dtor();
    return res;
}

static void __cdecl OnScreenMgrSetState_Impl(uint32_t newState) {
    if (newState != 9u) {
        g_pv.inMainGraphics = false;
        g_pv.hoveredKind = PREVIEW_HOVER_NONE;
        g_pv.popupOpen = false;
        if (g_pv.previewLoaded || g_pv.savedMenuState) {
            // MenuScreen::setState skips skybox/camera/lights reload between menu states (2..15)
            UnloadGraphicsPreviewInternal(IsMenuBackgroundState(newState));
        }
    }
}

static void __cdecl ApplyPreviewCamera_Impl(uint8_t* ws, uint8_t* cam, float* pFovy, float* pActiveMul) {
    if (!IsGraphicsPreviewActive() || ws == nullptr || cam == nullptr) return;

    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
    int32_t virtW = *reinterpret_cast<const int32_t*>(sm + 0x24);
    int32_t virtH = *reinterpret_cast<const int32_t*>(sm + 0x28);
    if (virtW <= 0 || virtH <= 0) return;

    int basePt[2] = {0, 0};
    reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
    int32_t menuW = *reinterpret_cast<const int32_t*>(0x589ddc);
    float menuRightPx = static_cast<float>(basePt[0] + menuW + 8);
    float openFrac = std::clamp((static_cast<float>(virtW) - menuRightPx) / static_cast<float>(virtW), 0.25f, 0.90f);
    float ndcCenterX = std::clamp(menuRightPx / static_cast<float>(virtW), 0.0f, 0.70f);

    float aspect = *reinterpret_cast<const float*>(cam + 0x1dc);
    if (aspect < 0.5f) {
        aspect = static_cast<float>(virtW) / static_cast<float>(virtH);
    }

    float fovyDeg = (pFovy != nullptr && *pFovy > 5.0f) ? *pFovy : 22.0f;
    if (pFovy != nullptr && *pFovy <= 5.0f) {
        *pFovy = fovyDeg;
    }
    float fovyRad = fovyDeg * 0.0174532925f;
    float tanHalf = std::tan(fovyRad * 0.5f);
    if (tanHalf < 0.001f) tanHalf = 0.001f;

    // Showcase area in level309 zoomed out slightly to frame top planets, moving platforms, and SW switches cleanly.
    float halfWorldH = (std::max)(245.0f, 260.0f / (openFrac * aspect));
    float halfWorldW = halfWorldH * aspect;

    // Right vector for camera eye=lookAt+(1,1,1)*d, up=(0,1,0) is (+1, 0, -1)/sqrt(2).
    // Shifting lookAt along -right pushes the showcase scene into the right-hand open space.
    float shiftRight = -ndcCenterX * halfWorldW;
    constexpr float kInvSqrt2 = 0.70710678f;
    constexpr float kInvSqrt3 = 0.57735027f;

    float cx = 235.0f + shiftRight * kInvSqrt2;
    float cy = 65.0f;
    float cz = 242.0f - shiftRight * kInvSqrt2;

    *reinterpret_cast<float*>(ws + 0x1c) = cx;
    *reinterpret_cast<float*>(ws + 0x20) = cy;
    *reinterpret_cast<float*>(ws + 0x24) = cz;
    *reinterpret_cast<float*>(ws + 0x28) = cx;
    *reinterpret_cast<float*>(ws + 0x2c) = cy;
    *reinterpret_cast<float*>(ws + 0x30) = cz;

    void** cfgHolder = reinterpret_cast<void**(__cdecl*)()>(0x4764c0)();
    if (cfgHolder != nullptr && *cfgHolder != nullptr && pActiveMul != nullptr) {
        uint32_t distKey = *reinterpret_cast<const uint32_t*>(ws + 0xc);
        float baseDist = reinterpret_cast<float(__thiscall*)(void*, uint32_t)>(0x4408c0)(*cfgHolder, distKey);
        if (baseDist > 1.0f) {
            // WorldScreen::updateCamera offsets eye by (1,1,1)*(baseDist*activeMul), so Euclidean distance = sqrt(3)*baseDist*activeMul
            float desiredDistParam = (halfWorldH / tanHalf) * kInvSqrt3;
            *pActiveMul = std::clamp(desiredDistParam / baseDist, 0.35f, 6.0f);
        }
    }
}

static bool __cdecl IsGraphicsPreviewActive_Bridge() {
    return IsGraphicsPreviewActive();
}

static bool __cdecl IsGraphicsPreviewLoading_Bridge() {
    return IsGraphicsPreviewLoading();
}

static bool __cdecl IsGraphicsPreviewWorldMuted_Bridge() {
    return g_pv.inWorldUpdate;
}

static uint8_t __cdecl GetTargetPreviewTheme_Bridge() {
    return g_pv.targetTheme;
}

// --- Naked hook trampolines ---

static void __declspec(naked) hk_screenMgrUpdate() {
    __asm {
        push dword ptr [ebp+0x8]
        push esi
        call OnScreenMgrUpdate_Impl
        add esp, 8
        mov eax, dword ptr [esi+0x50]
        cmp eax, 0x12
        jne skip_pause
        mov ecx, 0x48123e
        jmp ecx
    skip_pause:
        mov ecx, 0x481254
        jmp ecx
    }
}

static void __declspec(naked) hk_screenMgrRender() {
    __asm {
        push esi
        call OnScreenMgrRender_Impl
        add esp, 4
        mov eax, dword ptr [esi+0x50]
        cmp eax, 0x12
        mov ecx, 0x4812c8
        jmp ecx
    }
}

static void __declspec(naked) hk_screenMgrStepFixed() {
    __asm {
        push esi
        call OnScreenMgrStepFixed_Impl
        add esp, 4
        mov eax, dword ptr [esi+0x50]
        cmp eax, 0x12
        mov ecx, 0x481336
        jmp ecx
    }
}

static void __declspec(naked) hk_screenMgrInterp() {
    __asm {
        push dword ptr [ebp+0x8]
        push esi
        call OnScreenMgrInterp_Impl
        add esp, 8
        mov eax, dword ptr [esi+0x50]
        cmp eax, 0x12
        mov ecx, 0x4813c1
        jmp ecx
    }
}

static void __declspec(naked) hk_menuUpdateSkipLogo() {
    __asm {
        push eax
        push ecx
        call IsGraphicsPreviewActive_Bridge
        test al, al
        pop ecx
        pop eax
        jnz skip_logo_upd
        cmp eax, 0x12
        je skip_logo_upd
        push 0x49c9e1
        ret
    skip_logo_upd:
        push 0x49d382
        ret
    }
}

static void __declspec(naked) hk_screenMgrSetState() {
    __asm {
        push edi
        call OnScreenMgrSetState_Impl
        add esp, 4
        mov eax, dword ptr [esi+0x50]
        push edi
        push eax
        mov dword ptr [esi+0x54], eax
        mov edx, 0x481edf
        mov ecx, esi
        jmp edx
    }
}

static void __declspec(naked) hk_menuRenderSkipSkybox() {
    __asm {
        push eax
        call IsGraphicsPreviewActive_Bridge
        test al, al
        pop eax
        jnz skip_skybox
        cmp eax, 0x12
        je skip_skybox
        mov ecx, 0x49c6ed
        jmp ecx
    skip_skybox:
        mov ecx, 0x49c741
        jmp ecx
    }
}

static void __declspec(naked) hk_menuRenderSkipLogoModel() {
    __asm {
        push eax
        call IsGraphicsPreviewActive_Bridge
        test al, al
        pop eax
        jnz skip_logo
        cmp eax, 0x12
        je skip_logo
        mov ecx, 0x49c76d
        jmp ecx
    skip_logo:
        mov ecx, 0x49c836
        jmp ecx
    }
}

static void __declspec(naked) hk_menuRenderSkipPostProc() {
    __asm {
        call IsGraphicsPreviewActive_Bridge
        test al, al
        jnz skip_pp
        mov edx, dword ptr ds:[0x5f9080]
        mov ecx, 0x49c83c
        jmp ecx
    skip_pp:
        mov ecx, 0x49c848
        jmp ecx
    }
}

static void __declspec(naked) hk_wsUpdateTail() {
    __asm {
        mov eax, dword ptr ds:[0x5f9080]
        mov eax, dword ptr [eax+0x50]
        cmp eax, 0x11
        je run_full
        cmp eax, 0x14
        je run_full
        cmp eax, 0x15
        je run_full
        cmp eax, 0x16
        je run_full
        cmp eax, 9
        jne done
        call IsGraphicsPreviewActive_Bridge
        test al, al
        jz done
        mov ecx, 0x47f915 // run WorldScreen::updateVisuals (skip minimap)
        jmp ecx
    run_full:
        mov ecx, 0x47f90c
        jmp ecx
    done:
        mov ecx, 0x47f91c
        jmp ecx
    }
}

static void __declspec(naked) hk_guardProgressLoad() {
    __asm {
        push ecx
        call IsGraphicsPreviewLoading_Bridge
        test al, al
        pop ecx
        jnz skip_prog
        mov eax, 0x494780
        jmp eax
    skip_prog:
        ret 8
    }
}

static void __declspec(naked) hk_levelLoadThemeByte() {
    __asm {
        push dword ptr [esp+4]
        mov eax, 0x486530
        call eax
        add esp, 4
        push eax
        call IsGraphicsPreviewLoading_Bridge
        test al, al
        pop eax
        jz keep_byte
        call GetTargetPreviewTheme_Bridge
    keep_byte:
        ret
    }
}

static void __declspec(naked) hk_guardWinToDebrief() {
    __asm {
        push eax
        call IsGraphicsPreviewActive_Bridge
        test al, al
        pop eax
        jnz skip_debrief
        cmp dword ptr [esi+0xb8], eax
        jle skip_debrief
        mov ecx, 0x47c860
        jmp ecx
    skip_debrief:
        mov ecx, 0x47cb36
        jmp ecx
    }
}

static void __declspec(naked) hk_guardPreviewSfx() {
    __asm {
        push ecx
        call IsGraphicsPreviewWorldMuted_Bridge
        test al, al
        pop ecx
        jnz mute_sfx
        push ebp
        mov ebp, esp
        cmp dword ptr [ecx+0x2b0], 0
        mov eax, 0x4815fa
        jmp eax
    mute_sfx:
        ret 4
    }
}

static void __declspec(naked) hk_applyPreviewCamera() {
    __asm {
        lea eax, [ebp-0x8]
        push eax
        lea eax, [ebp-0x4]
        push eax
        push esi
        push edi
        call ApplyPreviewCamera_Impl
        add esp, 16
        fld dword ptr [edi+0x1c]
        fstp dword ptr [esi+0x208]
        ret
    }
}

} // namespace

namespace edgemod {

bool __cdecl IsGraphicsPreviewActive() {
    uint8_t* sm = GetScreenMgr();
    uint8_t* ws = GetWorldScreen();
    return g_pv.previewLoaded
        && g_pv.inMainGraphics
        && sm != nullptr
        && *reinterpret_cast<const uint32_t*>(sm + 0x50) == 9u
        && ws != nullptr
        && ws[0x2120] != 0;
}

bool __cdecl IsGraphicsPreviewLoading() {
    return g_pv.previewLoading;
}

bool __cdecl IsGraphicsPreviewFileValid() {
    return ValidatePreviewLevelFile();
}

bool __cdecl IsGraphicsPreviewHoveringCubeDistortion() {
    if (!IsGraphicsPreviewActive()) return false;
    if (g_pv.popupOpen) return g_pv.popupKind == PREVIEW_HOVER_CUBE_DISTORTION;
    return g_pv.hoveredKind == PREVIEW_HOVER_CUBE_DISTORTION;
}

bool __cdecl IsGraphicsPreviewHoveringEdgeTimer() {
    if (!IsGraphicsPreviewActive()) return false;
    if (g_pv.popupOpen) return g_pv.popupKind == PREVIEW_HOVER_EDGE_TIMER;
    return g_pv.hoveredKind == PREVIEW_HOVER_EDGE_TIMER;
}

uint8_t __cdecl GetEffectiveEdgeTimerMode() {
    if (IsGraphicsPreviewActive() && g_pv.popupOpen && g_pv.popupKind == PREVIEW_HOVER_EDGE_TIMER &&
        g_pv.popupSlotVisible && g_pv.popupHighlight >= 0 && g_pv.popupHighlight <= 2) {
        return static_cast<uint8_t>(g_pv.popupHighlight);
    }
    return (g_edgeTimerMode <= 2u) ? g_edgeTimerMode : 0u;
}

static bool ThemeSettingsEqual(const CustomThemeSettings& a, const CustomThemeSettings& b) {
    return std::memcmp(&a, &b, sizeof(CustomThemeSettings)) == 0;
}

static CustomThemeSettings ComputeEffectiveThemeNoHover() {
    if (g_colorTheme <= 2u) {
        return MakePresetThemeSettings(g_colorTheme);
    }
    return ClampCustomThemeSettings(g_customTheme);
}

void __cdecl ApplyGraphicsPreviewThemeOverrides(CustomThemeSettings* eff) {
    if (eff == nullptr || !IsGraphicsPreviewActive()) return;
    if (!g_pv.popupOpen || !g_pv.popupSlotVisible || g_pv.popupHighlight < 0) return;

    int hi = g_pv.popupHighlight;
    switch (g_pv.popupKind) {
    case PREVIEW_HOVER_THEME:
        if (hi >= 0 && hi <= 2) {
            *eff = MakePresetThemeSettings(static_cast<uint8_t>(hi));
        } else if (hi == 3) {
            *eff = ClampCustomThemeSettings(g_customTheme);
        }
        break;
    case PREVIEW_HOVER_TERRAIN:           if (hi <= 2) eff->terrain          = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_WALL_GRADIENT:     if (hi <= 1) eff->wallGradient     = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_PLANETS:           if (hi <= 2) eff->planets          = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_STARS:             if (hi <= 1) eff->stars            = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_PLAYER_CUBE:       if (hi <= 2) eff->playerCube       = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_DARK_CUBE:         if (hi <= 2) eff->darkCube         = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_CUBE_DISTORTION:   if (hi <= 1) eff->cubeDistortion   = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_PRISMS:            if (hi <= 2) eff->prisms           = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_MOVING_PLATFORMS:  if (hi <= 2) eff->movingPlatforms  = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_PLATFORM_BORDERS:  if (hi <= 2) eff->platformBorders  = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_FALLING_PLATFORMS: if (hi <= 2) eff->fallingPlatforms = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_BUMPERS:           if (hi <= 2) eff->bumpers          = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_SWITCHES:          if (hi <= 2) eff->switches         = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_SHRINKERS:         if (hi <= 2) eff->shrinkers        = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_FINISH_PLATFORM:   if (hi <= 2) eff->finishPlatform   = static_cast<uint8_t>(hi); break;
    case PREVIEW_HOVER_FINISH_EFFECT:     if (hi <= 2) eff->finishEffect     = static_cast<uint8_t>(hi); break;
    default: break;
    }
}

void __cdecl SetGraphicsPreviewHoverState(
    bool inMainGraphics, PreviewHoverKind hoveredKind, bool popupOpen,
    PreviewHoverKind popupKind, bool popupSlotVisible, int popupHighlight
) {
    bool prevInGraphics = g_pv.inMainGraphics;
    g_pv.inMainGraphics   = inMainGraphics;
    g_pv.hoveredKind      = hoveredKind;
    g_pv.popupOpen        = popupOpen;
    g_pv.popupKind        = popupKind;
    g_pv.popupSlotVisible = popupSlotVisible;
    g_pv.popupHighlight   = popupHighlight;

    if (!inMainGraphics && prevInGraphics && g_pv.previewLoaded) {
        UnloadGraphicsPreviewIfActive();
        return;
    }

    if (IsGraphicsPreviewActive()) {
        CustomThemeSettings curEff = ComputeEffectiveThemeNoHover();
        ApplyGraphicsPreviewThemeOverrides(&curEff);
        if (!g_pv.hadThemeOverride || !ThemeSettingsEqual(curEff, g_pv.lastEffTheme)) {
            g_pv.hadThemeOverride = true;
            g_pv.lastEffTheme = curEff;
            SyncEffectiveThemeGlobals();
        }

        if (popupOpen && popupKind == PREVIEW_HOVER_FINISH_EFFECT && popupSlotVisible &&
            popupHighlight >= 0 && popupHighlight <= 2) {
            if (g_pv.lastPopupFinishKind != PREVIEW_HOVER_FINISH_EFFECT ||
                g_pv.lastPopupFinishHi != popupHighlight) {
                g_pv.lastPopupFinishKind = PREVIEW_HOVER_FINISH_EFFECT;
                g_pv.lastPopupFinishHi = popupHighlight;
                TriggerGraphicsPreviewFinishEffect();
            }
        } else {
            if (!popupOpen && g_pv.lastPopupFinishKind == PREVIEW_HOVER_FINISH_EFFECT &&
                !g_pv.pendingFinishTrig && g_pv.finishActive) {
                ResetPreviewFinishState(GetWorldScreen());
            }
            g_pv.lastPopupFinishKind = PREVIEW_HOVER_NONE;
            g_pv.lastPopupFinishHi = -1;
        }
    } else {
        g_pv.hadThemeOverride = false;
        g_pv.lastPopupFinishKind = PREVIEW_HOVER_NONE;
        g_pv.lastPopupFinishHi = -1;
    }
}

void __cdecl OnGraphicsPreviewThemeChanged() {
    if (!IsGraphicsPreviewActive()) return;
    uint8_t* ws = GetWorldScreen();
    uint8_t desired = GetDesiredPreviewTheme();
    if (ws != nullptr && desired != g_pv.loadedTheme) {
        LoadPreviewLevelInternal(ws, desired);
    }
}

void __cdecl TriggerGraphicsPreviewFinishEffect() {
    if (!IsGraphicsPreviewActive()) return;
    g_pv.pendingFinishTrig = true;
}

void __cdecl UnloadGraphicsPreviewIfActive() {
    uint8_t* sm = GetScreenMgr();
    bool restoreMenuScene = (sm != nullptr && IsMenuBackgroundState(*reinterpret_cast<const uint32_t*>(sm + 0x50)));
    UnloadGraphicsPreviewInternal(restoreMenuScene);
}

} // namespace edgemod

void RegisterGraphicsPreview() {
    using namespace edgemod;

    static const uint8_t exScreenMgrUpd[5]  = {0x83, 0xf8, 0x12, 0x75, 0x16};
    static const uint8_t exScreenMgrRen[6]  = {0x8b, 0x46, 0x50, 0x83, 0xf8, 0x12};
    static const uint8_t exScreenMgrStep[6] = {0x8b, 0x46, 0x50, 0x83, 0xf8, 0x12};
    static const uint8_t exScreenMgrInt[6]  = {0x8b, 0x46, 0x50, 0x83, 0xf8, 0x12};
    static const uint8_t exScreenMgrSet[8]  = {0x8b, 0x46, 0x50, 0x57, 0x50, 0x89, 0x46, 0x54};

    static const uint8_t exMenuUpdLogo[9]   = {0x83, 0xf8, 0x12, 0x0f, 0x84, 0xa1, 0x09, 0x00, 0x00};
    static const uint8_t exMenuSkipSky[5]   = {0x83, 0xf8, 0x12, 0x74, 0x54};
    static const uint8_t exMenuSkipLogo[9]  = {0x83, 0xf8, 0x12, 0x0f, 0x84, 0xc9, 0x00, 0x00, 0x00};
    static const uint8_t exMenuSkipPp[6]    = {0x8b, 0x15, 0x80, 0x90, 0x5f, 0x00};

    static const uint8_t exWsUpdTail[28]    = {
        0xa1, 0x80, 0x90, 0x5f, 0x00, 0x8b, 0x40, 0x50,
        0x83, 0xf8, 0x11, 0x74, 0x0f, 0x83, 0xf8, 0x14,
        0x74, 0x0a, 0x83, 0xf8, 0x15, 0x74, 0x05, 0x83,
        0xf8, 0x16, 0x75, 0x10
    };
    static const uint8_t exProgLoad[5]      = {0xe8, 0x88, 0x73, 0x01, 0x00};
    static const uint8_t exTerrainModel[5]  = {0xe8, 0xff, 0x3a, 0xfa, 0xff};
    static const uint8_t exThemeByte[5]     = {0xe8, 0x7d, 0x03, 0x01, 0x00};
    static const uint8_t exWinDebrief[12]   = {0x39, 0x86, 0xb8, 0x00, 0x00, 0x00, 0x0f, 0x8e, 0xd6, 0x02, 0x00, 0x00};
    static const uint8_t exPlaySfx[10]      = {0x55, 0x8b, 0xec, 0x83, 0xb9, 0xb0, 0x02, 0x00, 0x00, 0x00};
    static const uint8_t exCamLookAt[9]     = {0xd9, 0x47, 0x1c, 0xd9, 0x9e, 0x08, 0x02, 0x00, 0x00};

    RegRelJmp(0x481239, exScreenMgrUpd,  sizeof(exScreenMgrUpd),  5, &hk_screenMgrUpdate,       0x90, 0x48123e);
    RegRelJmp(0x4812c2, exScreenMgrRen,  sizeof(exScreenMgrRen),  5, &hk_screenMgrRender,       0x90, 0x4812c8);
    RegRelJmp(0x481330, exScreenMgrStep, sizeof(exScreenMgrStep), 5, &hk_screenMgrStepFixed,    0x90, 0x481336);
    RegRelJmp(0x4813bb, exScreenMgrInt,  sizeof(exScreenMgrInt),  5, &hk_screenMgrInterp,       0x90, 0x4813c1);
    RegRelJmp(0x481ed7, exScreenMgrSet,  sizeof(exScreenMgrSet),  5, &hk_screenMgrSetState,     0x90, 0x481edf);

    RegRelJmp(0x49c9d8, exMenuUpdLogo,   sizeof(exMenuUpdLogo),   5, &hk_menuUpdateSkipLogo,    0x90, 0x49c9e1);
    RegRelJmp(0x49c6e8, exMenuSkipSky,   sizeof(exMenuSkipSky),   5, &hk_menuRenderSkipSkybox,   0x90, 0x49c6ed);
    RegRelJmp(0x49c764, exMenuSkipLogo,  sizeof(exMenuSkipLogo),  5, &hk_menuRenderSkipLogoModel,0x90, 0x49c76d);
    RegRelJmp(0x49c836, exMenuSkipPp,    sizeof(exMenuSkipPp),    5, &hk_menuRenderSkipPostProc, 0x90, 0x49c83c);

    RegRelJmp(0x47f8f0, exWsUpdTail,     sizeof(exWsUpdTail),     5, &hk_wsUpdateTail,          0x90, 0x47f91c);
    RegRelCall(0x47d3f3, exProgLoad,     sizeof(exProgLoad),      5, &hk_guardProgressLoad);
    RegRelCall(0x47dfbc, exTerrainModel, sizeof(exTerrainModel),  5, &LoadPreviewTerrainModel_Impl);
    RegRelCall(0x4761ae, exThemeByte,    sizeof(exThemeByte),     5, &hk_levelLoadThemeByte);
    RegRelJmp(0x47c854, exWinDebrief,    sizeof(exWinDebrief),    5, &hk_guardWinToDebrief,     0x90, 0x47c860);
    RegRelJmp(0x4815f0, exPlaySfx,       sizeof(exPlaySfx),       5, &hk_guardPreviewSfx,       0x90, 0x4815fa);
    RegRelCall(0x47ba6d, exCamLookAt,    sizeof(exCamLookAt),     9, &hk_applyPreviewCamera,    0x90);
}
