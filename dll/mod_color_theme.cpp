#include "mod_state.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace edgemod {

uint8_t g_colorTheme = 0; // 0=pc, 1=mobile, 2=java

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

// Java static level tileset & planet palettes (from tileset_res_01..04.png, shadowall_01..04.spr, shadow.spr)
static const Rgb8 kJavaLevelPalette[4][11] = {
    {
        {0xfc, 0xfc, 0xfc}, {0xf3, 0xf3, 0xfa}, {0xde, 0xde, 0xf0}, {0x6a, 0x7b, 0x8c},
        {0xca, 0xcd, 0xe0}, {0x58, 0x6c, 0x7a}, {0x94, 0x9c, 0xac}, {0x70, 0x78, 0x8c},
        {0x20, 0x28, 0x34}, {0x62, 0x6a, 0x7e}, {0x16, 0x1c, 0x26}
    },
    {
        {0xf5, 0xf3, 0xf9}, {0xe6, 0xe8, 0xef}, {0x94, 0x98, 0xac}, {0x26, 0x2b, 0x33},
        {0x83, 0x88, 0x99}, {0x16, 0x1c, 0x22}, {0x74, 0x7c, 0x84}, {0x54, 0x5d, 0x6a},
        {0x1a, 0x20, 0x28}, {0x48, 0x50, 0x5d}, {0x12, 0x16, 0x1e}
    },
    {
        {0x7b, 0x81, 0x94}, {0x70, 0x76, 0x89}, {0x52, 0x59, 0x6a}, {0x0e, 0x14, 0x12},
        {0x46, 0x4d, 0x5c}, {0x06, 0x0a, 0x06}, {0x46, 0x4d, 0x5c}, {0x32, 0x39, 0x46},
        {0x10, 0x14, 0x19}, {0x2a, 0x30, 0x3c}, {0x0a, 0x0e, 0x12}
    },
    {
        {0x51, 0x5a, 0x6a}, {0x47, 0x50, 0x60}, {0x39, 0x44, 0x50}, {0x0a, 0x10, 0x14},
        {0x30, 0x3b, 0x45}, {0x05, 0x09, 0x0c}, {0x2c, 0x33, 0x3d}, {0x1e, 0x26, 0x30},
        {0x08, 0x0c, 0x10}, {0x18, 0x1f, 0x28}, {0x04, 0x07, 0x0a}
    }
};

// Exact Java moving platform palettes (from plateform.spr / plateform_big.spr pal[0..3]:
// [0]=top, [1]=left top, [2]=left bottom, [3]=right top, [4]=right bottom)
static const Rgb8 kJavaMovingPlatPalette[4][5] = {
    {{0xfc, 0xfc, 0xfc}, {0xc5, 0xc7, 0xd6}, {0xb4, 0xb6, 0xcd}, {0xbd, 0xc2, 0xcd}, {0xac, 0xb2, 0xbd}},
    {{0xec, 0xec, 0xec}, {0x7b, 0x7d, 0x94}, {0x6a, 0x6d, 0x83}, {0x70, 0x74, 0x80}, {0x5a, 0x65, 0x73}},
    {{0x7b, 0x81, 0x94}, {0x41, 0x4c, 0x52}, {0x39, 0x3c, 0x4a}, {0x39, 0x44, 0x4a}, {0x31, 0x3b, 0x41}},
    {{0x52, 0x59, 0x6a}, {0x41, 0x47, 0x53}, {0x31, 0x38, 0x41}, {0x39, 0x3c, 0x41}, {0x29, 0x32, 0x39}}
};

// Exact Java bumper palettes (from bumpers.spr pal[0..3]:
// [0]=roof, [1]=left top, [2]=left bottom, [3]=right top, [4]=right bottom)
static const Rgb8 kJavaBumperPalette[4][5] = {
    {{0xe8, 0xe8, 0xf0}, {0xbc, 0xc0, 0xd4}, {0xa8, 0xac, 0xc0}, {0xac, 0xb4, 0xc4}, {0x96, 0x9e, 0xb0}},
    {{0xe8, 0xe8, 0xf0}, {0x90, 0x94, 0xa4}, {0x7e, 0x82, 0x92}, {0x7c, 0x80, 0x94}, {0x6a, 0x6e, 0x82}},
    {{0x78, 0x80, 0x94}, {0x3c, 0x48, 0x4c}, {0x30, 0x3a, 0x3e}, {0x34, 0x3c, 0x40}, {0x28, 0x30, 0x34}},
    {{0x50, 0x58, 0x68}, {0x34, 0x3c, 0x40}, {0x28, 0x30, 0x34}, {0x2c, 0x38, 0x40}, {0x20, 0x2a, 0x32}}
};

// Exact Java falling platform palette (from pff.spr pal[0..3]: [0]=top, [1]=left, [2]=right)
static const Rgb8 kJavaFallingPalette[4][3] = {
    {{0xfc, 0xfc, 0xfc}, {0xbd, 0xc2, 0xd5}, {0x94, 0x9d, 0xb4}},
    {{0xf5, 0xf3, 0xf9}, {0x7b, 0x81, 0x94}, {0x5a, 0x5d, 0x6a}},
    {{0x7b, 0x81, 0x94}, {0x41, 0x48, 0x52}, {0x29, 0x2c, 0x31}},
    {{0x51, 0x5a, 0x6a}, {0x20, 0x2c, 0x31}, {0x10, 0x18, 0x18}}
};

static const Rgb8 kJavaSwitchColor = {0xae, 0xc3, 0xd0};

// Java pastel rainbow_menu (aO) used for Cube::ComputePalette on themes 0..1
static const uint32_t kJavaRainbowMenu[13] = {
    0xfea9b6u, 0xfe9febu, 0xee9dfeu, 0xd3a9feu, 0xb5befeu, 0xa1cffeu, 0x96dafeu,
    0x3df89du, 0x80ff85u, 0xadff5au, 0xeef21cu, 0xffd275u, 0xffbd84u
};

// Exact Android prism 18-color cycle from libcgame.so @ 0x671000
static const Rgb8 kAndroidPrismColors[18] = {
    {0xff, 0xaf, 0x0f}, {0xfc, 0xff, 0x10}, {0xa9, 0xff, 0x11}, {0x61, 0xff, 0x1e},
    {0x33, 0xff, 0x55}, {0x12, 0xff, 0x6c}, {0x12, 0xff, 0xbb}, {0x12, 0xf2, 0xff},
    {0x04, 0xc8, 0xf7}, {0x4f, 0xa1, 0xff}, {0x60, 0x73, 0xff}, {0x84, 0x5f, 0xff},
    {0xad, 0x44, 0xf9}, {0xdc, 0x50, 0xff}, {0xff, 0x50, 0xce}, {0xff, 0x55, 0x94},
    {0xff, 0x55, 0x5a}, {0xff, 0x76, 0x31}
};

// Exact Java prism 9-color shaded & top-face cycles from prism_big.spr (pal[11..19], pal[21..29])
static const Rgb8 kJavaPrismDark[9] = {
    {0xdc, 0x7a, 0xff}, {0xff, 0x78, 0xe2}, {0xff, 0x9b, 0x7d},
    {0xff, 0xc8, 0x3d}, {0xbc, 0xf0, 0x00}, {0x5b, 0xed, 0x18},
    {0x3c, 0xc9, 0xf0}, {0x78, 0xa0, 0xff}, {0xa7, 0x8c, 0xff}
};

static const Rgb8 kJavaPrismTop[9] = {
    {0xf7, 0xe0, 0xff}, {0xff, 0xde, 0xf8}, {0xff, 0xe9, 0xe3},
    {0xff, 0xe5, 0xa3}, {0xea, 0xff, 0x9e}, {0xcb, 0xff, 0xb2},
    {0xbf, 0xf1, 0xff}, {0xd1, 0xdf, 0xff}, {0xe8, 0xe0, 0xff}
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
static IDirect3DTexture9* g_themeTex[2][kTargetTexCount] = {};
static IDirect3DTexture9* g_javaMovingPlatTex[4] = {};
static IDirect3DTexture9* g_javaBumperTex[4] = {};
static bool               g_inJavaBumperRender = false;
static bool               g_inJavaMovingPlatformRender = false;

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

static void FillLevelWallPixels(
    D3DLOCKED_RECT* lr,
    Rgb8 topOdd, Rgb8 topEven,
    Rgb8 leftTop, Rgb8 leftBot,
    Rgb8 rightTop, Rgb8 rightBot
) {
    uint8_t* base = static_cast<uint8_t*>(lr->pBits);
    uint32_t cOdd  = PackArgb(0xffu, topOdd);
    uint32_t cEven = PackArgb(0xffu, topEven);
    for (int y = 0; y < 256; ++y) {
        float t = static_cast<float>(y) / 255.0f;
        uint32_t cLeft  = PackArgb(0xffu, LerpRgb8(leftTop, leftBot, t));
        uint32_t cRight = PackArgb(0xffu, LerpRgb8(rightTop, rightBot, t));
        uint32_t* row = reinterpret_cast<uint32_t*>(base + y * lr->Pitch);
        for (int x = 0; x < 64; ++x)    row[x] = cLeft;
        for (int x = 64; x < 128; ++x)  row[x] = cRight;
        for (int x = 128; x < 192; ++x) row[x] = cOdd;
        for (int x = 192; x < 256; ++x) row[x] = cEven;
    }
}

static IDirect3DTexture9* CreateJavaSolidWallTexture(IDirect3DDevice9* dev, const Rgb8* cols5) {
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = dev->CreateTexture(256, 256, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr) || tex == nullptr) return nullptr;

    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits != nullptr) {
        FillLevelWallPixels(&lr, cols5[0], cols5[0], cols5[1], cols5[2], cols5[3], cols5[4]);
        tex->UnlockRect(0);
    } else {
        tex->Release();
        return nullptr;
    }
    return tex;
}

static void FillThemeTexturePixels(const TargetTexSpec& spec, D3DLOCKED_RECT* lr, bool isJava) {
    const Rgb8* pal = isJava ? kJavaLevelPalette[spec.themeIdx] : kAndroidLevelPalette[spec.themeIdx];
    uint8_t* base = static_cast<uint8_t*>(lr->pBits);

    if (spec.kind == TEX_LEVELWALL) {
        FillLevelWallPixels(lr, pal[0], pal[1], pal[2], pal[3], pal[4], pal[5]);
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
        Rgb8 cTopRgb   = isJava ? kJavaFallingPalette[spec.themeIdx][0] : ScaleRgb(pal[0], 0.9f);
        Rgb8 cLeftRgb  = isJava ? kJavaFallingPalette[spec.themeIdx][1] : LerpRgb8(pal[2], pal[3], 0.25f);
        Rgb8 cRightRgb = isJava ? kJavaFallingPalette[spec.themeIdx][2] : LerpRgb8(pal[4], pal[5], 0.25f);
        Rgb8 cLbevRgb  = isJava ? LerpRgb8(cTopRgb, cLeftRgb, 0.55f)     : ScaleRgb(pal[1], 0.9f);
        Rgb8 cRbevRgb  = isJava ? LerpRgb8(cTopRgb, cRightRgb, 0.65f)    : ScaleRgb(pal[1], 0.9f);
        uint32_t cTop   = PackArgb(0xffu, cTopRgb);
        uint32_t cLbev  = PackArgb(0xffu, cLbevRgb);
        uint32_t cRbev  = PackArgb(0xffu, cRbevRgb);
        uint32_t cLeft  = PackArgb(0xffu, cLeftRgb);
        uint32_t cRight = PackArgb(0xffu, cRightRgb);
        for (int y = 0; y < 64; ++y) {
            uint32_t* row = reinterpret_cast<uint32_t*>(base + y * lr->Pitch);
            for (int x = 0; x < 64; ++x) {
                uint32_t col;
                if (x >= 52) {
                    col = cRight;
                } else if (y >= 52) {
                    col = cLeft;
                } else if (x <= 37 && y <= 37) {
                    col = cTop;
                } else if (y > x) {
                    col = cLbev;
                } else {
                    col = cRbev;
                }
                row[x] = col;
            }
        }
    } else {
        uint8_t alpha = (spec.kind == TEX_SWITCH_30OPACITY) ? 0x4du : 0xffu;
        Rgb8 sw = isJava ? kJavaSwitchColor : Rgb8{0xc3, 0xb8, 0xaf};
        uint32_t col = PackArgb(alpha, sw);
        for (int y = 0; y < spec.height; ++y) {
            uint32_t* row = reinterpret_cast<uint32_t*>(base + y * lr->Pitch);
            for (int x = 0; x < spec.width; ++x) {
                row[x] = col;
            }
        }
    }
}

static IDirect3DTexture9* CreateThemeTexture(IDirect3DDevice9* dev, const TargetTexSpec& spec, bool isJava) {
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = dev->CreateTexture(
        spec.width, spec.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr
    );
    if (FAILED(hr) || tex == nullptr) return nullptr;

    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits != nullptr) {
        FillThemeTexturePixels(spec, &lr, isJava);
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
        for (int t = 0; t < 2; ++t) {
            for (int i = 0; i < kTargetTexCount; ++i) {
                if (g_themeTex[t][i] != nullptr) {
                    g_themeTex[t][i]->Release();
                    g_themeTex[t][i] = nullptr;
                }
            }
        }
        for (int i = 0; i < 4; ++i) {
            if (g_javaMovingPlatTex[i] != nullptr) {
                g_javaMovingPlatTex[i]->Release();
                g_javaMovingPlatTex[i] = nullptr;
            }
            if (g_javaBumperTex[i] != nullptr) {
                g_javaBumperTex[i]->Release();
                g_javaBumperTex[i] = nullptr;
            }
        }
        g_cachedDev = d3dDev;
    }

    bool isJava = (g_colorTheme == 2u);
    if (isJava && kTargetTexSpecs[slot].kind == TEX_LEVELWALL) {
        uint8_t themeIdx = kTargetTexSpecs[slot].themeIdx;
        if (g_inJavaMovingPlatformRender) {
            if (g_javaMovingPlatTex[themeIdx] == nullptr) {
                g_javaMovingPlatTex[themeIdx] = CreateJavaSolidWallTexture(d3dDev, kJavaMovingPlatPalette[themeIdx]);
            }
            if (g_javaMovingPlatTex[themeIdx] != nullptr) {
                return static_cast<void*>(g_javaMovingPlatTex[themeIdx]);
            }
        } else if (g_inJavaBumperRender) {
            if (g_javaBumperTex[themeIdx] == nullptr) {
                g_javaBumperTex[themeIdx] = CreateJavaSolidWallTexture(d3dDev, kJavaBumperPalette[themeIdx]);
            }
            if (g_javaBumperTex[themeIdx] != nullptr) {
                return static_cast<void*>(g_javaBumperTex[themeIdx]);
            }
        }
    }

    int themeSlot = isJava ? 1 : 0;
    if (g_themeTex[themeSlot][slot] == nullptr) {
        g_themeTex[themeSlot][slot] = CreateThemeTexture(d3dDev, kTargetTexSpecs[slot], isJava);
    }
    return g_themeTex[themeSlot][slot] != nullptr ? static_cast<void*>(g_themeTex[themeSlot][slot]) : defaultTex;
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

static uint8_t GetJavaLight0Byte() {
    return 194u;
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
        bool isJava = (g_colorTheme == 2u);
        uint8_t intensity = isJava ? GetJavaLight0Byte() : 182u;
        light[0] = intensity;
        light[1] = intensity;
        light[2] = intensity;
        light[3] = 255;
        float* dir = reinterpret_cast<float*>(light + 0x08);
        if (isJava) {
            // Balanced isometric cube light matching 0.spr am[8]:am[11]:am[14] (Top=1.00, Left=0.87, Right=0.74)
            dir[0] = -0.3600000f;
            dir[1] = -0.7500000f;
            dir[2] = -0.5548874f;
        } else {
            dir[0] = -0.4327311f;
            dir[1] = -0.7212185f;
            dir[2] = -0.5409138f;
        }
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

static bool         g_prismMatOverride       = false;
static bool         g_anyNormalPrismRendered = false;
static D3DMATERIAL9 g_prismMat{};

static void PreparePrismMaterial(int32_t animTick) {
    uint8_t* sm = GetScreenMgr();
    float alpha = (sm != nullptr && *reinterpret_cast<const uint32_t*>(sm + 0x50) == 0x11u)
        ? std::clamp(g_uiAlpha, 0.0f, 1.0f)
        : 0.0f;

    std::memset(&g_prismMat, 0, sizeof(g_prismMat));
    g_prismMat.Diffuse.a = 1.0f;
    g_prismMat.Ambient.a = 1.0f;

    if (g_colorTheme == 1u) {
        int i0 = ((animTick % 18) + 18) % 18;
        int i1 = (i0 + 1) % 18;
        Rgb8 base = LerpRgb8(kAndroidPrismColors[i0], kAndroidPrismColors[i1], alpha);
        float diffScale = 80.0f / 182.0f;
        g_prismMat.Diffuse.r  = diffScale;
        g_prismMat.Diffuse.g  = diffScale;
        g_prismMat.Diffuse.b  = diffScale;
        g_prismMat.Emissive.r = static_cast<float>(base.r) / 255.0f;
        g_prismMat.Emissive.g = static_cast<float>(base.g) / 255.0f;
        g_prismMat.Emissive.b = static_cast<float>(base.b) / 255.0f;
    } else {
        float phase = std::fmod((static_cast<float>(animTick) + alpha) / 1.8f, 9.0f);
        if (phase < 0.0f) phase += 9.0f;
        int i0 = static_cast<int>(phase) % 9;
        int i1 = (i0 + 1) % 9;
        float frac = phase - std::floor(phase);

        float dr = (static_cast<float>(kJavaPrismDark[i0].r) + static_cast<float>(kJavaPrismDark[i1].r - kJavaPrismDark[i0].r) * frac) / 255.0f;
        float dg = (static_cast<float>(kJavaPrismDark[i0].g) + static_cast<float>(kJavaPrismDark[i1].g - kJavaPrismDark[i0].g) * frac) / 255.0f;
        float db = (static_cast<float>(kJavaPrismDark[i0].b) + static_cast<float>(kJavaPrismDark[i1].b - kJavaPrismDark[i0].b) * frac) / 255.0f;

        float tr = (static_cast<float>(kJavaPrismTop[i0].r) + static_cast<float>(kJavaPrismTop[i1].r - kJavaPrismTop[i0].r) * frac) / 255.0f;
        float tg = (static_cast<float>(kJavaPrismTop[i0].g) + static_cast<float>(kJavaPrismTop[i1].g - kJavaPrismTop[i0].g) * frac) / 255.0f;
        float tb = (static_cast<float>(kJavaPrismTop[i0].b) + static_cast<float>(kJavaPrismTop[i1].b - kJavaPrismTop[i0].b) * frac) / 255.0f;

        float topLight = 0.8700000f;
        g_prismMat.Diffuse.r  = (std::max)(0.0f, (tr - dr) / topLight);
        g_prismMat.Diffuse.g  = (std::max)(0.0f, (tg - dg) / topLight);
        g_prismMat.Diffuse.b  = (std::max)(0.0f, (tb - db) / topLight);
        g_prismMat.Emissive.r = dr;
        g_prismMat.Emissive.g = dg;
        g_prismMat.Emissive.b = db;
    }
}

struct ScopedJavaPrismLight {
    IDirect3DDevice9* dev = nullptr;
    D3DLIGHT9 saved{};
    bool active = false;

    explicit ScopedJavaPrismLight(bool enable) {
        if (!enable) return;
        dev = reinterpret_cast<IDirect3DDevice9*(__cdecl*)()>(0x40e6c0)();
        if (dev != nullptr && SUCCEEDED(dev->GetLight(0, &saved))) {
            D3DLIGHT9 prismLight = saved;
            prismLight.Diffuse   = {1.0f, 1.0f, 1.0f, 1.0f};
            prismLight.Direction = {0.2500000f, -0.8700000f, -0.4249706f};
            dev->SetLight(0, &prismLight);
            active = true;
        }
    }

    ~ScopedJavaPrismLight() {
        if (active && dev != nullptr) {
            dev->SetLight(0, &saved);
        }
    }
};

static void __fastcall Hook_PrismRender(uint8_t* prism) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x487800);
    if (g_colorTheme == 0 || prism == nullptr || *reinterpret_cast<void* const*>(prism + 0x24) != nullptr) {
        orig(prism);
        return;
    }

    int32_t animTick = *reinterpret_cast<const int32_t*>(prism + 0x14);
    PreparePrismMaterial(animTick);
    g_prismMatOverride = true;
    g_anyNormalPrismRendered = true;
    {
        ScopedJavaPrismLight keyLight(g_colorTheme == 2u);
        orig(prism);
    }
    g_prismMatOverride = false;
}

static void __fastcall Hook_PrismRenderTransparents(void* renderer) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x407410);
    uint8_t* sm = GetScreenMgr();
    bool inDebrief = (sm != nullptr && *reinterpret_cast<const uint32_t*>(sm + 0x50) == 0x13u);
    bool overrideMat = (g_colorTheme != 0 && g_anyNormalPrismRendered && !inDebrief);
    if (overrideMat) {
        g_prismMatOverride = true;
    }
    {
        ScopedJavaPrismLight keyLight(overrideMat && g_colorTheme == 2u);
        orig(renderer);
    }
    g_prismMatOverride = false;
    g_anyNormalPrismRendered = false;
}

static const D3DMATERIAL9* __cdecl SelectPrismD3DMaterial(const uint8_t* matObj) {
    return g_prismMatOverride ? &g_prismMat
                              : reinterpret_cast<const D3DMATERIAL9*>(matObj + 0x60);
}

static void __declspec(naked) Hook_MaterialLoadD3DMat() {
    // Replaces `call 0x40e6c0; mov ecx, [eax]; lea edx, [esi+0x60]` at 0x437289..0x437292 (10B).
    // Returns eax = IDirect3DDevice9*, ecx = [eax], edx = D3DMATERIAL9* (preserves ebx, esi, edi, ebp).
    __asm {
        mov eax, 0x40e6c0
        call eax
        push eax
        push esi
        call SelectPrismD3DMaterial
        add esp, 4
        mov edx, eax
        pop eax
        mov ecx, dword ptr [eax]
        ret
    }
}

static const uint32_t* __cdecl SelectCubePaletteTable_Impl(int theme) {
    if (g_colorTheme == 2u) {
        return (theme >= 0 && theme <= 1)
            ? kJavaRainbowMenu
            : reinterpret_cast<const uint32_t*>(0x589b50);
    }
    return (theme == 3)
        ? reinterpret_cast<const uint32_t*>(0x589b84)
        : reinterpret_cast<const uint32_t*>(0x589b50);
}

static void __declspec(naked) Hook_SelectCubePaletteTable() {
    // Replaces table selection at 0x48b98c..0x48b99e (19B) inside Cube::ComputePalette.
    // Preserves ebx (theme), esi, edi, ebp; writes selected table ptr into [ebp-0x10].
    __asm {
        push ebx
        call SelectCubePaletteTable_Impl
        add esp, 4
        mov dword ptr [ebp - 0x10], eax
        ret
    }
}

static int g_javaCubeDeltaTopRight[3] = {55, 55, 55};

static void __cdecl Hook_ComputeCubePalette(int theme) {
    reinterpret_cast<void(__cdecl*)(int)>(0x48b980)(theme);
    if (g_colorTheme == 2u) {
        uint8_t* ws = *reinterpret_cast<uint8_t**>(0x5f9064);
        bool isRainbow = (ws != nullptr && ws[0xb5] != 0);
        int t = (theme >= 0 && theme <= 3) ? theme : 2;
        // Exact Java a.e() / e.f() offsets for 0.spr Frame 0 (Top=am[8], Left=am[11], Right=am[14])
        static const int kJavaMid[4]        = { 9,  8, 11, 10};
        static const int kJavaDeltaTop[4]   = { 5,  0, 27, 18};
        static const int kJavaDeltaRight[4] = {-50, -60, -36, -40};

        uint32_t* pal = reinterpret_cast<uint32_t*>(0x5f9110);
        uint32_t midCol = pal[isRainbow ? 11 : kJavaMid[t]];
        int mr = static_cast<int>((midCol >> 16) & 0xffu);
        int mg = static_cast<int>((midCol >> 8) & 0xffu);
        int mb = static_cast<int>(midCol & 0xffu);

        int dTop   = isRainbow ? 27  : kJavaDeltaTop[t];
        int dRight = isRainbow ? -42 : kJavaDeltaRight[t];

        int rr = std::clamp(mr + dRight, 0, 255);
        int rg = std::clamp(mg + dRight, 0, 255);
        int rb = std::clamp(mb + dRight, 0, 255);

        pal[15] = (static_cast<uint32_t>(rr) << 16)
                | (static_cast<uint32_t>(rg) << 8)
                | static_cast<uint32_t>(rb);

        // Preserve full Top-to-Right contrast using unclamped target Top delta vs clamped Right base
        g_javaCubeDeltaTopRight[0] = (std::max)(0, mr + dTop - rr);
        g_javaCubeDeltaTopRight[1] = (std::max)(0, mg + dTop - rg);
        g_javaCubeDeltaTopRight[2] = (std::max)(0, mb + dTop - rb);
    }
}

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
                    if (g_colorTheme == 1u) {
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
    if (g_colorTheme != 1u) {
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
        cmp byte ptr [g_prismMatOverride], 0
        jne prism_op
        mov edx, dword ptr [esi + 0x1c]
        mov edx, dword ptr [edx + edi*8 + 8]
        ret
    prism_op:
        mov edx, 3 // D3DTOP_SELECTARG2
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
    bool isJava = (g_colorTheme == 2u);
    if (isJava) {
        OnColorThemeChanged();
        g_inJavaBumperRender = true;
    }

    int param = *reinterpret_cast<const int*>(bumper + 0x10);
    for (int i = 0; i < 4; ++i) {
        uint8_t* sideMesh = *reinterpret_cast<uint8_t**>(bumper + 0x54 + i * 8);
        if (sideMesh != nullptr) {
            bool triggered = (*reinterpret_cast<const int*>(sideMesh + 0x1c) != 5)
                          && (*reinterpret_cast<const int*>(sideMesh + 0x08) != 0);
            if (renderer != nullptr) {
                reinterpret_cast<SetLightingFn>(0x405e40)(renderer, triggered ? 1 : 0);
            }
            Hook_SetHeightUV(sideMesh, param);
            reinterpret_cast<DrawMeshFn>(0x421460)(sideMesh, 0);
        }
    }

    void* roofMesh = *reinterpret_cast<void**>(bumper + 0x74);
    if (roofMesh != nullptr) {
        if (renderer != nullptr) {
            reinterpret_cast<SetLightingFn>(0x405e40)(renderer, 0);
        }
        Hook_SetHeightUV(roofMesh, param);
        reinterpret_cast<DrawMeshFn>(0x421460)(roofMesh, 0);
    }

    g_inJavaBumperRender = false;
    if (isJava) {
        OnColorThemeChanged();
    }

    if (renderer != nullptr) {
        reinterpret_cast<SetLightingFn>(0x405e40)(renderer, 1);
    }
}

static void __fastcall Hook_MovingPlatformRender(uint8_t* plat) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x483fd0);
    if (g_colorTheme != 2u || plat == nullptr) {
        orig(plat);
        return;
    }

    OnColorThemeChanged();
    g_inJavaMovingPlatformRender = true;
    orig(plat);
    g_inJavaMovingPlatformRender = false;
    OnColorThemeChanged();
}

static void __fastcall Hook_SetMeshLightingState(
    void* renderer, void*, uint32_t enableLighting, uint32_t useVertexColor
) {
    if (g_prismMatOverride) {
        useVertexColor = 0; // D3DMCS_MATERIAL so D3DMATERIAL9::Diffuse drives lighting instead of D3DFVF_DIFFUSE
    }
    reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t)>(0x405c20)(
        renderer, enableLighting, useVertexColor
    );
}

static void __declspec(naked) Hook_LitMeshColorOp() {
    // Replaces `push 5; push 1; push 0; push eax; call edx` at 0x406565..0x40656d (9B -> call + 4x nop).
    // Preserves ebx, esi, edi, ebp.
    __asm {
        cmp byte ptr [g_prismMatOverride], 0
        jne use_select_arg2
        push 5 // D3DTOP_MODULATE2X
        jmp do_call
    use_select_arg2:
        push 3 // D3DTOP_SELECTARG2
    do_call:
        push 1 // D3DTSS_COLOROP
        push 0 // stage 0
        push eax
        call edx
        ret
    }
}

struct ScopedJavaCubeLight {
    IDirect3DDevice9* dev = nullptr;
    D3DLIGHT9 saved{};
    bool active = false;

    explicit ScopedJavaCubeLight(bool enable) {
        if (!enable) return;
        dev = reinterpret_cast<IDirect3DDevice9*(__cdecl*)()>(0x40e6c0)();
        if (dev != nullptr && SUCCEEDED(dev->GetLight(0, &saved))) {
            D3DLIGHT9 cubeLight = saved;
            cubeLight.Diffuse   = {1.0f, 1.0f, 1.0f, 1.0f};
            cubeLight.Direction = {0.0f, -0.8819171f, -0.4714045f};
            dev->SetLight(0, &cubeLight);
            active = true;
        }
    }

    ~ScopedJavaCubeLight() {
        if (active && dev != nullptr) {
            dev->SetLight(0, &saved);
        }
    }
};

static void __fastcall Hook_GhostCubeTransparents(void* renderer) {
    using OrigFn = void(__thiscall*)(void*);
    uint8_t* ws = *reinterpret_cast<uint8_t**>(0x5f9064);
    bool isGhostDarkCube = (ws != nullptr && ws[0x48] == 0);
    ScopedJavaCubeLight cubeLight(g_colorTheme == 2u && isGhostDarkCube);
    reinterpret_cast<OrigFn>(0x407410)(renderer);
}

static void* __cdecl Hook_ComputeCubeDiffuse(
    uint8_t* out, const uint8_t* baseColor,
    int multR, int multG, int multB,
    int addR, int addG, int addB
) {
    if (g_colorTheme == 2u) {
        constexpr float kInvDenom = 1.0f / (2.0f * 0.8819171f);
        for (int c = 0; c < 3; ++c) {
            int v = static_cast<int>(std::lround(static_cast<float>(g_javaCubeDeltaTopRight[c]) * kInvDenom));
            out[c] = static_cast<uint8_t>(std::clamp(v, 0, 255));
        }
        out[3] = baseColor[3];
        return out;
    }
    using OrigFn = void*(__cdecl*)(uint8_t*, const uint8_t*, int, int, int, int, int, int);
    return reinterpret_cast<OrigFn>(0x490e30)(out, baseColor, multR, multG, multB, addR, addG, addB);
}

static void* __cdecl Hook_ComputeCubeMatColor(
    uint8_t* out, const uint8_t* baseColor,
    int multR, int multG, int multB,
    int addR, int addG, int addB
) {
    if (g_colorTheme != 0) {
        int targetScale = (g_colorTheme == 2u) ? 254 : 255;
        auto scaleMult = [targetScale](int m) -> int {
            if (m <= 0) return 0;
            return (std::min)(255, (m * targetScale + 111) / 223);
        };
        multR = scaleMult(multR);
        multG = scaleMult(multG);
        multB = scaleMult(multB);
    }
    using OrigFn = void*(__cdecl*)(uint8_t*, const uint8_t*, int, int, int, int, int, int);
    return reinterpret_cast<OrigFn>(0x490e30)(out, baseColor, multR, multG, multB, addR, addG, addB);
}

} // namespace

namespace edgemod {

uint32_t __fastcall RenderPlayerCubeWithColorTheme(void* cube) {
    using OrigFn = uint32_t(__thiscall*)(void*);
    uint32_t ret = 0;
    {
        ScopedJavaCubeLight cubeLight(g_colorTheme == 2u);
        ret = reinterpret_cast<OrigFn>(0x48c630)(cube);
    }
    return ret;
}

} // namespace edgemod

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

    // 8. WorldScreen::render Prism::render (0x47b3b9, 5B)
    static const uint8_t exPrismRender[5] = {0xe8, 0x42, 0xc4, 0x00, 0x00};
    RegRelCall(0x47b3b9, exPrismRender, 5, 5, &Hook_PrismRender);

    // 9. WorldScreen::render post-prism Renderer::renderTransparents (0x47b3d5, 5B)
    static const uint8_t exPrismTransparents[5] = {0xe8, 0x36, 0xc0, 0xf8, 0xff};
    RegRelCall(0x47b3d5, exPrismTransparents, 5, 5, &Hook_PrismRenderTransparents);

    // 10. Material::apply D3DMATERIAL9 load (0x437289..0x437292, 10B -> call + nop)
    static const uint8_t exMatLoadD3DMat[10] = {0xe8, 0x32, 0x74, 0xfd, 0xff, 0x8b, 0x08, 0x8d, 0x56, 0x60};
    RegRelCall(0x437289, exMatLoadD3DMat, 10, 10, &Hook_MaterialLoadD3DMat, 0x90);

    // 11. Cube::ComputePalette palette table select (0x48b98c..0x48b99e, 19B -> call + nop)
    static const uint8_t exSelectCubePal[19] = {
        0xc7, 0x45, 0xf0, 0x84, 0x9b, 0x58, 0x00,
        0x83, 0xfb, 0x03, 0x74, 0x07,
        0xc7, 0x45, 0xf0, 0x50, 0x9b, 0x58, 0x00
    };
    RegRelCall(0x48b98c, exSelectCubePal, 19, 19, &Hook_SelectCubePaletteTable, 0x90);

    // 12. Cube::update Cube::ComputePalette call (0x490ccf, 5B)
    static const uint8_t exComputeCubePal[5] = {0xe8, 0xac, 0xac, 0xff, 0xff};
    RegRelCall(0x490ccf, exComputeCubePal, 5, 5, &Hook_ComputeCubePalette);

    // 13. WorldScreen::render MovingPlatform::render (0x47b2aa, 5B)
    static const uint8_t exMovingPlatRender[5] = {0xe8, 0x21, 0x8d, 0x00, 0x00};
    RegRelCall(0x47b2aa, exMovingPlatRender, 5, 5, &Hook_MovingPlatformRender);

    // 14. Renderer::setVertexFormat lighting/color-vertex call (0x40654f, 5B)
    static const uint8_t exSetMeshLightState[5] = {0xe8, 0xcc, 0xf6, 0xff, 0xff};
    RegRelCall(0x40654f, exSetMeshLightState, 5, 5, &Hook_SetMeshLightingState);

    // 15. Renderer::setVertexFormat lit D3DTSS_COLOROP (0x406565..0x40656d, 9B -> call + nop)
    static const uint8_t exLitMeshColorOp[9] = {0x6a, 0x05, 0x6a, 0x01, 0x6a, 0x00, 0x50, 0xff, 0xd2};
    RegRelCall(0x406565, exLitMeshColorOp, 9, 9, &Hook_LitMeshColorOp, 0x90);

    // 16. Cube::update Diffuse computeMaterialColor (0x490bd5, 5B)
    static const uint8_t exCubeDiff[5] = {0xe8, 0x56, 0x02, 0x00, 0x00};
    RegRelCall(0x490bd5, exCubeDiff, 5, 5, &Hook_ComputeCubeDiffuse);

    // 17. WorldScreen::render Ghost Cube::render Transparents (0x47b406, 5B)
    static const uint8_t exGhostCubeTransp[5] = {0xe8, 0x05, 0xc0, 0xf8, 0xff};
    RegRelCall(0x47b406, exGhostCubeTransp, 5, 5, &Hook_GhostCubeTransparents);
}
