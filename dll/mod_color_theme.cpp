#include "mod_state.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace edgemod {

uint8_t g_colorTheme = 0; // 0=pc, 1=mobile

void OnColorThemeChanged() {
    uint8_t count = *reinterpret_cast<uint8_t*>(0x5f87c9);
    void** bound = *reinterpret_cast<void***>(0x5f87cc);
    if (bound != nullptr && count > 0) {
        std::memset(bound, 0, static_cast<size_t>(count) * sizeof(void*));
    }
}

} // namespace edgemod

namespace {

using namespace edgemod;

struct Rgb8 {
    uint8_t r, g, b;
};

// Exact Android gLevelPalette (4 themes x 11 RGBs) from libcgame.so @ 0x7a51c8:
// [0]=top odd, [1]=top even, [2]=left top, [3]=left bottom, [4]=right top, [5]=right bottom,
// [6]=planet top, [7]=planet left top, [8]=planet left bottom, [9]=planet right top, [10]=planet right bottom
static const Rgb8 kAndroidLevelPalette[4][11] = {
    {
        {0xfe, 0xfe, 0xff}, {0xfa, 0xfa, 0xfa}, {0xeb, 0xed, 0xee}, {0x85, 0x8f, 0x9b},
        {0xd2, 0xd5, 0xd7}, {0x6b, 0x77, 0x83}, {0x99, 0x9f, 0xa7}, {0x70, 0x78, 0x89},
        {0x25, 0x2a, 0x34}, {0x6a, 0x70, 0x82}, {0x1c, 0x21, 0x2a}
    },
    {
        {0xd0, 0xd4, 0xd8}, {0xd4, 0xd8, 0xdc}, {0x9c, 0x9d, 0xac}, {0x31, 0x33, 0x3a},
        {0x95, 0x96, 0xa4}, {0x1f, 0x25, 0x2c}, {0x6f, 0x74, 0x78}, {0x56, 0x5b, 0x65},
        {0x24, 0x28, 0x2d}, {0x51, 0x55, 0x60}, {0x1e, 0x22, 0x25}
    },
    {
        {0x78, 0x7e, 0x91}, {0x7b, 0x81, 0x94}, {0x59, 0x5f, 0x71}, {0x13, 0x17, 0x16},
        {0x55, 0x5c, 0x6c}, {0x08, 0x0c, 0x08}, {0x36, 0x3a, 0x3b}, {0x2c, 0x31, 0x34},
        {0x19, 0x1c, 0x1d}, {0x2a, 0x2e, 0x31}, {0x17, 0x1a, 0x1b}
    },
    {
        {0x51, 0x5a, 0x6a}, {0x54, 0x5b, 0x69}, {0x3e, 0x48, 0x55}, {0x0f, 0x14, 0x19},
        {0x3c, 0x45, 0x52}, {0x08, 0x0d, 0x11}, {0x20, 0x26, 0x29}, {0x1a, 0x20, 0x24},
        {0x0f, 0x13, 0x16}, {0x19, 0x1f, 0x23}, {0x0e, 0x12, 0x14}
    }
};

constexpr uint32_t kTexturesFolderHash = 0x039573F7u;
constexpr int kTargetTexCount = 14;

enum TargetTexKind : uint8_t {
    TEX_LEVELWALL = 0,
    TEX_PLANET,
    TEX_FALLING_PLATFORM,
    TEX_SWITCH_OPAQUE,
    TEX_SWITCH_30OPACITY
};

struct TargetTexSpec {
    uint32_t      hash0;
    uint16_t      width;
    uint16_t      height;
    TargetTexKind kind;
    uint8_t       themeIdx;
};

static const TargetTexSpec kTargetTexSpecs[kTargetTexCount] = {
    {0xFBC6CF7Bu, 256, 256, TEX_LEVELWALL,        0}, // levelwall_1.img
    {0x6224A97Au, 256, 256, TEX_LEVELWALL,        1}, // levelwall_2.img
    {0xA3AA76BAu, 256, 256, TEX_LEVELWALL,        2}, // levelwall_3.img
    {0x8A916339u, 256, 256, TEX_LEVELWALL,        3}, // levelwall_4.img
    {0x563A09E7u,  64,  64, TEX_PLANET,           0}, // planet_1.img
    {0xD8B50E04u,  64,  64, TEX_PLANET,           1}, // planet_2.img
    {0x141F0E9Au,  64,  64, TEX_PLANET,           2}, // planet_3.img
    {0x1EDA0783u,  64,  64, TEX_PLANET,           3}, // planet_4.img
    {0xC5240908u,  64,  64, TEX_FALLING_PLATFORM, 0}, // fallingplatform_1.img
    {0x96BE528Cu,  64,  64, TEX_FALLING_PLATFORM, 1}, // fallingplatform_2.img
    {0x111899CFu,  64,  64, TEX_FALLING_PLATFORM, 2}, // fallingplatform_3.img
    {0x318AE584u,  64,  64, TEX_FALLING_PLATFORM, 3}, // fallingplatform_4.img
    {0x301C8879u,  64,  64, TEX_SWITCH_OPAQUE,    0}, // switch.img
    {0xB2446CF3u,  64,  64, TEX_SWITCH_30OPACITY, 0}, // switch_30opacity.img
};

static IDirect3DDevice9*  g_cachedDev = nullptr;
static IDirect3DTexture9* g_mobileTex[kTargetTexCount] = {};

static inline uint32_t PackArgb(uint8_t a, Rgb8 c) {
    return (static_cast<uint32_t>(a) << 24)
         | (static_cast<uint32_t>(c.r) << 16)
         | (static_cast<uint32_t>(c.g) << 8)
         | static_cast<uint32_t>(c.b);
}

static inline uint8_t ScaleByte(uint8_t v, float s) {
    int iv = static_cast<int>(std::lround(static_cast<float>(v) * s));
    return static_cast<uint8_t>(std::clamp(iv, 0, 255));
}

static inline Rgb8 ScaleRgb(Rgb8 c, float s) {
    return {ScaleByte(c.r, s), ScaleByte(c.g, s), ScaleByte(c.b, s)};
}

static inline Rgb8 LerpRgb8(Rgb8 a, Rgb8 b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    auto ch = [t](uint8_t x, uint8_t y) -> uint8_t {
        int v = static_cast<int>(std::lround(static_cast<float>(x) + static_cast<float>(y - x) * t));
        return static_cast<uint8_t>(std::clamp(v, 0, 255));
    };
    return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b)};
}

static void FillMobileTexturePixels(const TargetTexSpec& spec, D3DLOCKED_RECT* lr) {
    const Rgb8* pal = kAndroidLevelPalette[spec.themeIdx];
    uint8_t* base = static_cast<uint8_t*>(lr->pBits);

    if (spec.kind == TEX_LEVELWALL) {
        for (int y = 0; y < 256; ++y) {
            float t = static_cast<float>(y) / 255.0f;
            uint32_t cLeft  = PackArgb(0xffu, LerpRgb8(pal[2], pal[3], t));
            uint32_t cRight = PackArgb(0xffu, LerpRgb8(pal[4], pal[5], t));
            uint32_t cOdd   = PackArgb(0xffu, pal[0]);
            uint32_t cEven  = PackArgb(0xffu, pal[1]);
            uint32_t* row = reinterpret_cast<uint32_t*>(base + y * lr->Pitch);
            for (int x = 0; x < 64; ++x)    row[x] = cLeft;
            for (int x = 64; x < 128; ++x)  row[x] = cRight;
            for (int x = 128; x < 192; ++x) row[x] = cOdd;
            for (int x = 192; x < 256; ++x) row[x] = cEven;
        }
    } else if (spec.kind == TEX_PLANET) {
        for (int y = 0; y < 64; ++y) {
            float t = static_cast<float>(y) / 63.0f;
            uint32_t cLeft  = PackArgb(0xffu, LerpRgb8(pal[7], pal[8], t));
            uint32_t cRight = PackArgb(0xffu, LerpRgb8(pal[9], pal[10], t));
            uint32_t cTop   = PackArgb(0xffu, pal[6]);
            uint32_t* row = reinterpret_cast<uint32_t*>(base + y * lr->Pitch);
            for (int x = 0; x < 32; ++x)  row[x] = cLeft;
            for (int x = 32; x < 48; ++x) row[x] = cRight;
            for (int x = 48; x < 64; ++x) row[x] = cTop;
        }
    } else if (spec.kind == TEX_FALLING_PLATFORM) {
        uint32_t cBevel = PackArgb(0xffu, ScaleRgb(pal[1], 0.9f));
        uint32_t cTop   = PackArgb(0xffu, ScaleRgb(pal[0], 0.9f));
        uint32_t cLeft  = PackArgb(0xffu, LerpRgb8(pal[2], pal[3], 0.25f));
        uint32_t cRight = PackArgb(0xffu, LerpRgb8(pal[4], pal[5], 0.25f));
        for (int y = 0; y < 64; ++y) {
            uint32_t* row = reinterpret_cast<uint32_t*>(base + y * lr->Pitch);
            for (int x = 0; x < 64; ++x) {
                uint32_t col = cBevel;
                if (x >= 52) {
                    col = cRight;
                } else if (y >= 52 && x <= 53) {
                    col = cLeft;
                } else if (x <= 37 && y <= 37) {
                    col = cTop;
                }
                row[x] = col;
            }
        }
    } else {
        uint8_t alpha = (spec.kind == TEX_SWITCH_30OPACITY) ? 0x4du : 0xffu;
        uint32_t col = PackArgb(alpha, {0xc3, 0xb8, 0xaf});
        for (int y = 0; y < spec.height; ++y) {
            uint32_t* row = reinterpret_cast<uint32_t*>(base + y * lr->Pitch);
            for (int x = 0; x < spec.width; ++x) {
                row[x] = col;
            }
        }
    }
}

static IDirect3DTexture9* CreateMobileTexture(IDirect3DDevice9* dev, const TargetTexSpec& spec) {
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = dev->CreateTexture(
        spec.width, spec.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr
    );
    if (FAILED(hr) || tex == nullptr) return nullptr;

    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits != nullptr) {
        FillMobileTexturePixels(spec, &lr);
        tex->UnlockRect(0);
    } else {
        tex->Release();
        return nullptr;
    }
    return tex;
}

static void* __cdecl ResolveD3DTextureForApply(const uint8_t* texObj, IDirect3DDevice9* d3dDev) {
    if (texObj == nullptr) return nullptr;
    void* defaultTex = *reinterpret_cast<void* const*>(texObj + 0x2c);
    if (g_colorTheme == 0 || d3dDev == nullptr) return defaultTex;
    if (*reinterpret_cast<const uint32_t*>(texObj + 0x08) != kTexturesFolderHash) return defaultTex;

    uint32_t hash0 = *reinterpret_cast<const uint32_t*>(texObj + 0x04);
    int slot = -1;
    for (int i = 0; i < kTargetTexCount; ++i) {
        if (kTargetTexSpecs[i].hash0 == hash0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return defaultTex;

    if (d3dDev != g_cachedDev) {
        for (int i = 0; i < kTargetTexCount; ++i) {
            if (g_mobileTex[i] != nullptr) {
                g_mobileTex[i]->Release();
                g_mobileTex[i] = nullptr;
            }
        }
        g_cachedDev = d3dDev;
    }

    if (g_mobileTex[slot] == nullptr) {
        g_mobileTex[slot] = CreateMobileTexture(d3dDev, kTargetTexSpecs[slot]);
    }
    return g_mobileTex[slot] != nullptr ? static_cast<void*>(g_mobileTex[slot]) : defaultTex;
}

static void __declspec(naked) Hook_TextureApplyLoadTex() {
    // Replaces `call 0x40e6c0; mov ecx, [esi+0x2c]` at 0x40d02a..0x40d031 (8B).
    // Returns eax = IDirect3DDevice9*, ecx = IDirect3DTexture9* (preserves ebx, esi, edi, ebp).
    __asm {
        mov eax, 0x40e6c0
        call eax
        push eax
        push eax
        push esi
        call ResolveD3DTextureForApply
        add esp, 8
        mov ecx, eax
        pop eax
        ret
    }
}

static void __fastcall Hook_LightApply(uint8_t* light) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x4e9810);
    if (g_colorTheme == 0 || light == nullptr) {
        orig(light);
        return;
    }

    uint8_t savedColor[4];
    float savedDir[3];
    std::memcpy(savedColor, light + 0x00, sizeof(savedColor));
    std::memcpy(savedDir, light + 0x08, sizeof(savedDir));

    // LightD3D starts at light + 0x3c; D3D light slot index is at LightD3D + 0x68 (= light + 0xa4)
    int32_t d3dIdx = *reinterpret_cast<const int32_t*>(light + 0xa4);
    if (d3dIdx == 0) {
        light[0] = 182;
        light[1] = 182;
        light[2] = 182;
        light[3] = 255;
        float* dir = reinterpret_cast<float*>(light + 0x08);
        dir[0] = -0.4327311f;
        dir[1] = -0.7212185f;
        dir[2] = -0.5409138f;
    } else {
        light[0] = 0;
        light[1] = 0;
        light[2] = 0;
        light[3] = 255;
    }

    orig(light);

    std::memcpy(light + 0x00, savedColor, sizeof(savedColor));
    std::memcpy(light + 0x08, savedDir, sizeof(savedDir));
}

static bool g_inMobileBumperRender = false;

static int GetLevelHeightTiles() {
    uint8_t* lvl = GetLevel();
    if (lvl == nullptr) return 4;
    uint8_t h = lvl[0x70];
    return (h >= 1) ? static_cast<int>(h) : 4;
}

static bool IsLevelWallHash(uint32_t hash0) {
    for (int i = 0; i < 4; ++i) {
        if (kTargetTexSpecs[i].hash0 == hash0) return true;
    }
    return false;
}

struct CachedWallSubMesh {
    IDirect3DVertexBuffer9* vb = nullptr;
    uint32_t numVerts = 0;
    uint32_t stride   = 0;
    uint32_t uvOff    = 0;
    std::vector<float> origV;
};

static uint8_t* s_cachedLevelModel  = nullptr;
static int      s_cachedHeightTiles = 0;
static uint8_t  s_appliedLevelTheme = 0;
static std::vector<CachedWallSubMesh> s_wallSubMeshes;

static void CollectLevelWallSubMeshes(uint8_t* nodePtr, std::vector<CachedWallSubMesh>& out) {
    if (nodePtr == nullptr) return;
    uint8_t* node = *reinterpret_cast<uint8_t**>(nodePtr);
    while (node != nullptr) {
        using GetMeshFn = uint8_t*(__thiscall*)(void*);
        uint8_t* meshObj = (*reinterpret_cast<GetMeshFn**>(node))[1](node);
        if (meshObj != nullptr) {
            uint8_t* sm = *reinterpret_cast<uint8_t**>(meshObj + 0x1d0);
            uint8_t* smEnd = *reinterpret_cast<uint8_t**>(meshObj + 0x1d4);
            while (sm != nullptr && sm < smEnd) {
                uint8_t* mat = *reinterpret_cast<uint8_t**>(sm + 0x04);
                if (mat != nullptr) {
                    uint8_t** texBegin = *reinterpret_cast<uint8_t***>(mat + 0x0c);
                    uint8_t** texEnd   = *reinterpret_cast<uint8_t***>(mat + 0x10);
                    if (texBegin != nullptr && texBegin < texEnd && texBegin[0] != nullptr) {
                        const uint8_t* tex = texBegin[0];
                        uint32_t h1 = *reinterpret_cast<const uint32_t*>(tex + 0x08);
                        uint32_t h0 = *reinterpret_cast<const uint32_t*>(tex + 0x04);
                        if (h1 == kTexturesFolderHash && IsLevelWallHash(h0)) {
                            auto* vb = *reinterpret_cast<IDirect3DVertexBuffer9**>(sm + 0xa0);
                            uint32_t flags  = *reinterpret_cast<const uint32_t*>(sm + 0x80);
                            uint32_t stride = *reinterpret_cast<const uint32_t*>(sm + 0x84);
                            const uint8_t* p0 = *reinterpret_cast<const uint8_t* const*>(sm + 0x10);
                            const uint8_t* p1 = *reinterpret_cast<const uint8_t* const*>(sm + 0x14);
                            uint32_t numVerts = (p1 > p0) ? static_cast<uint32_t>((p1 - p0) / 12) : 0;
                            uint32_t uvOff = 12u + ((flags & 1u) ? 12u : 0u) + ((flags & 2u) ? 4u : 0u);
                            if (vb != nullptr && numVerts > 0 && (flags & 4u) != 0 && stride >= 20u && uvOff + 8u <= stride) {
                                CachedWallSubMesh info;
                                info.vb = vb;
                                info.numVerts = numVerts;
                                info.stride = stride;
                                info.uvOff = uvOff;
                                out.push_back(std::move(info));
                            }
                        }
                    }
                }
                sm += 0xa8;
            }
        }
        CollectLevelWallSubMeshes(node + 0xec, out);
        node = *reinterpret_cast<uint8_t**>(node + 0xf4);
    }
}

static void __fastcall Hook_StaticLevelRender(uint8_t* model, void*, int arg) {
    using OrigFn = void(__thiscall*)(void*, int);
    auto orig = reinterpret_cast<OrigFn>(0x421460);
    if (model == nullptr) {
        s_cachedLevelModel = nullptr;
        s_wallSubMeshes.clear();
        orig(model, arg);
        return;
    }

    int heightTiles = GetLevelHeightTiles();
    std::vector<CachedWallSubMesh> live;
    CollectLevelWallSubMeshes(model, live);

    bool cacheValid = (model == s_cachedLevelModel)
                   && (heightTiles == s_cachedHeightTiles)
                   && (live.size() == s_wallSubMeshes.size())
                   && (live.empty()
                       || (live[0].vb == s_wallSubMeshes[0].vb
                           && live[0].numVerts == s_wallSubMeshes[0].numVerts));

    if (!cacheValid) {
        s_wallSubMeshes = std::move(live);
        for (auto& entry : s_wallSubMeshes) {
            void* ptr = nullptr;
            if (SUCCEEDED(entry.vb->Lock(0, 0, &ptr, D3DLOCK_READONLY)) && ptr != nullptr) {
                entry.origV.resize(entry.numVerts);
                const uint8_t* vptr = static_cast<const uint8_t*>(ptr);
                for (uint32_t i = 0; i < entry.numVerts; ++i) {
                    entry.origV[i] = *reinterpret_cast<const float*>(vptr + i * entry.stride + entry.uvOff + 4u);
                }
                entry.vb->Unlock();
            }
        }
        s_cachedLevelModel = model;
        s_cachedHeightTiles = heightTiles;
        s_appliedLevelTheme = 0;
    }

    if (s_appliedLevelTheme != g_colorTheme && !s_wallSubMeshes.empty()) {
        float span = (static_cast<float>(heightTiles) - 0.5f) * 10.0f;
        if (span < 10.0f) span = 10.0f;
        for (auto& entry : s_wallSubMeshes) {
            if (entry.origV.size() != entry.numVerts) continue;
            void* ptr = nullptr;
            if (SUCCEEDED(entry.vb->Lock(0, 0, &ptr, 0)) && ptr != nullptr) {
                uint8_t* basePtr = static_cast<uint8_t*>(ptr);
                for (uint32_t i = 0; i < entry.numVerts; ++i) {
                    uint8_t* vptr = basePtr + i * entry.stride;
                    if (g_colorTheme != 0) {
                        float y = *reinterpret_cast<const float*>(vptr + 4u);
                        float u = *reinterpret_cast<const float*>(vptr + entry.uvOff);
                        if (u < 0.5f) {
                            float t = std::clamp((y + 17.5f) / span, 0.0f, 1.0f);
                            float vNew = std::clamp(1.0f - t, 0.002f, 0.998f);
                            *reinterpret_cast<float*>(vptr + entry.uvOff + 4u) = vNew;
                        }
                    } else {
                        *reinterpret_cast<float*>(vptr + entry.uvOff + 4u) = entry.origV[i];
                    }
                }
                entry.vb->Unlock();
            }
        }
        s_appliedLevelTheme = g_colorTheme;
    }

    orig(model, arg);
}

static void __cdecl Hook_SetHeightUV(void* model, int y) {
    float vScale = 1.0f;
    float vOff = 0.0f;
    if (g_colorTheme == 0) {
        vOff = static_cast<float>(-static_cast<double>(y) * (1.0 / 120.0));
    } else {
        int hTiles = GetLevelHeightTiles();
        float H = (static_cast<float>(hTiles) - 0.5f) * 30.0f;
        if (H < 30.0f) H = 30.0f;
        vScale = 120.0f / H;
        vOff   = 1.0f - (static_cast<float>(y) + 105.0f) / H;
    }

    float mat[16] = {
        1.0f, 0.0f,   0.0f, 0.0f,
        0.0f, vScale, 0.0f, 0.0f,
        0.0f, vOff,   1.0f, 0.0f,
        0.0f, vOff,   0.0f, 1.0f
    };
    reinterpret_cast<void(__cdecl*)(void*, const float*)>(0x495a70)(model, mat);
}

static void __declspec(naked) Hook_MaterialColorOp() {
    // Replaces `mov edx, [esi+0x1c]; mov edx, [edx+edi*8+8]` at 0x4372d9..0x4372df (7B).
    // Preserves eax, ebx, esi, edi, ebp; returns desired D3DTSS_COLOROP in edx.
    __asm {
        cmp byte ptr [g_inMobileBumperRender], 0
        je normal_op
        mov edx, 2 // D3DTOP_SELECTARG1
        ret
    normal_op:
        mov edx, dword ptr [esi + 0x1c]
        mov edx, dword ptr [edx + edi*8 + 8]
        ret
    }
}

static void __fastcall Hook_BumperRender(uint8_t* bumper) {
    using OrigFn = void(__thiscall*)(void*);
    if (g_colorTheme == 0 || bumper == nullptr) {
        reinterpret_cast<OrigFn>(0x484fa0)(bumper);
        return;
    }

    using SetLightingFn = void(__thiscall*)(void*, uint8_t);
    using DrawMeshFn    = void(__thiscall*)(void*, int);

    uint8_t* renderer = GetRenderer();
    if (renderer != nullptr) {
        reinterpret_cast<SetLightingFn>(0x405e40)(renderer, 0);
    }

    g_inMobileBumperRender = true;
    int param = *reinterpret_cast<const int*>(bumper + 0x10);
    for (int i = 0; i < 4; ++i) {
        void* sideMesh = *reinterpret_cast<void**>(bumper + 0x54 + i * 8);
        if (sideMesh != nullptr) {
            Hook_SetHeightUV(sideMesh, param);
            reinterpret_cast<DrawMeshFn>(0x421460)(sideMesh, 0);
        }
    }

    void* roofMesh = *reinterpret_cast<void**>(bumper + 0x74);
    if (roofMesh != nullptr) {
        Hook_SetHeightUV(roofMesh, param);
        reinterpret_cast<DrawMeshFn>(0x421460)(roofMesh, 0);
    }
    g_inMobileBumperRender = false;

    if (renderer != nullptr) {
        reinterpret_cast<SetLightingFn>(0x405e40)(renderer, 1);
    }
}

static void* __cdecl Hook_ComputeCubeMatColor(
    uint8_t* out, const uint8_t* baseColor,
    int multR, int multG, int multB,
    int addR, int addG, int addB
) {
    if (g_colorTheme != 0) {
        auto scaleMult = [](int m) -> int {
            if (m <= 0) return 0;
            return (std::min)(255, (m * 255 + 111) / 223);
        };
        multR = scaleMult(multR);
        multG = scaleMult(multG);
        multB = scaleMult(multB);
    }
    using OrigFn = void*(__cdecl*)(uint8_t*, const uint8_t*, int, int, int, int, int, int);
    return reinterpret_cast<OrigFn>(0x490e30)(out, baseColor, multR, multG, multB, addR, addG, addB);
}

} // namespace

void RegisterColorTheme() {
    // 1. Texture::apply D3D texture lookup (0x40d02a..0x40d031, 8B)
    static const uint8_t exTexApply[8] = {0xe8, 0x91, 0x16, 0x00, 0x00, 0x8b, 0x4e, 0x2c};
    RegRelCall(0x40d02a, exTexApply, 8, 8, &Hook_TextureApplyLoadTex, 0x90);

    // 2. WorldScreen::render Light::apply (0x47b245, 5B)
    static const uint8_t exLightApply[5] = {0xe8, 0xc6, 0xe5, 0x06, 0x00};
    RegRelCall(0x47b245, exLightApply, 5, 5, &Hook_LightApply);

    // 3. WorldScreen::render Bumper::render (0x47b269, 5B)
    static const uint8_t exBumperRender[5] = {0xe8, 0x32, 0x9d, 0x00, 0x00};
    RegRelCall(0x47b269, exBumperRender, 5, 5, &Hook_BumperRender);

    // 4. Cube::update Cube::computeMaterialColor (0x490c82, 5B)
    static const uint8_t exCubeMat[5] = {0xe8, 0xa9, 0x01, 0x00, 0x00};
    RegRelCall(0x490c82, exCubeMat, 5, 5, &Hook_ComputeCubeMatColor);

    // 5. WorldScreen::render static level Model::render (0x47b220, 5B)
    static const uint8_t exStaticLvl[5] = {0xe8, 0x3b, 0x62, 0xfa, 0xff};
    RegRelCall(0x47b220, exStaticLvl, 5, 5, &Hook_StaticLevelRender);

    // 6. SetHeightUV (0x495bb0..0x495bb5, 6B -> jmp)
    static const uint8_t exSetHeightUv[6] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x40};
    RegRelJmp(0x495bb0, exSetHeightUv, 6, 6, &Hook_SetHeightUV, 0x90);

    // 7. Material::apply D3DTSS_COLOROP (0x4372d9..0x4372df, 7B -> call + nop)
    static const uint8_t exMatColorOp[7] = {0x8b, 0x56, 0x1c, 0x8b, 0x54, 0xfa, 0x08};
    RegRelCall(0x4372d9, exMatColorOp, 7, 7, &Hook_MaterialColorOp, 0x90);
}
