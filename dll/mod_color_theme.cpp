#include "mod_state.h"
#include "mod_smooth_shared.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace edgemod {

uint8_t g_colorTheme = 0; // 0=pc, 1=mobile, 2=java, 3=custom
CustomThemeSettings g_customTheme{};
uint8_t g_customThemeInited = 0;

CustomThemeSettings MakePresetThemeSettings(uint8_t preset) {
    CustomThemeSettings s{};
    if (preset == 1u) {
        s.terrain          = 1;
        s.wallGradient     = 1;
        s.planets          = 1;
        s.stars            = 0;
        s.playerCube       = 1;
        s.darkCube         = 1;
        s.cubeDistortion   = 0;
        s.prisms           = 1;
        s.movingPlatforms  = 1;
        s.platformBorders  = 1;
        s.fallingPlatforms = 1;
        s.bumpers          = 1;
        s.switches         = 1;
        s.hintTile         = 1;
        s.hintGhost        = 1;
        s.cubeShatter      = 1;
        s.shrinkers        = 1;
        s.finishPlatform   = 1;
        s.finishEffect     = 1;
    } else if (preset == 2u) {
        s.terrain          = 2;
        s.wallGradient     = 0;
        s.planets          = 2;
        s.stars            = 1;
        s.playerCube       = 2;
        s.darkCube         = 2;
        s.cubeDistortion   = 1;
        s.prisms           = 2;
        s.movingPlatforms  = 2;
        s.platformBorders  = 2;
        s.fallingPlatforms = 2;
        s.bumpers          = 2;
        s.switches         = 2;
        s.hintTile         = 2;
        s.hintGhost        = 2;
        s.cubeShatter      = 2;
        s.shrinkers        = 2;
        s.finishPlatform   = 2;
        s.finishEffect     = 2;
    }
    return s;
}

CustomThemeSettings ClampCustomThemeSettings(CustomThemeSettings s) {
    auto clamp3 = [](uint8_t v) -> uint8_t { return (v <= 2u) ? v : 0u; };
    auto clamp2 = [](uint8_t v) -> uint8_t { return (v != 0u) ? 1u : 0u; };
    s.terrain          = clamp3(s.terrain);
    s.wallGradient     = clamp2(s.wallGradient);
    s.planets          = clamp3(s.planets);
    s.stars            = clamp2(s.stars);
    s.playerCube       = clamp3(s.playerCube);
    s.darkCube         = clamp3(s.darkCube);
    s.cubeDistortion   = clamp2(s.cubeDistortion);
    s.prisms           = clamp3(s.prisms);
    s.movingPlatforms  = clamp3(s.movingPlatforms);
    s.platformBorders  = clamp3(s.platformBorders);
    s.fallingPlatforms = clamp3(s.fallingPlatforms);
    s.bumpers          = clamp3(s.bumpers);
    s.switches         = clamp3(s.switches);
    s.hintTile         = clamp3(s.hintTile);
    s.hintGhost        = clamp3(s.hintGhost);
    s.cubeShatter      = clamp3(s.cubeShatter);
    s.shrinkers        = clamp3(s.shrinkers);
    s.finishPlatform   = clamp3(s.finishPlatform);
    s.finishEffect     = clamp3(s.finishEffect);
    return s;
}

CustomThemeSettings GetEffectiveTheme() {
    CustomThemeSettings s{};
    if (g_colorTheme <= 2u) {
        s = MakePresetThemeSettings(g_colorTheme);
    } else {
        s = ClampCustomThemeSettings(g_customTheme);
    }
    ApplyGraphicsPreviewThemeOverrides(&s);
    return s;
}

void SyncEffectiveThemeGlobals() {
    CustomThemeSettings eff = GetEffectiveTheme();
    g_finishEffect   = eff.finishEffect;
    g_finishPlatform = eff.finishPlatform;
    g_cubeDistortion = eff.cubeDistortion;
    OnColorThemeChanged();
}

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
        {0xfc, 0xfc, 0xfc}, {0xf8, 0xf8, 0xfc}, {0xde, 0xde, 0xee}, {0x62, 0x75, 0x83},
        {0xca, 0xcd, 0xe0}, {0x58, 0x6c, 0x7a}, {0x94, 0x9c, 0xac}, {0x70, 0x78, 0x8c},
        {0x20, 0x28, 0x34}, {0x62, 0x6a, 0x7e}, {0x16, 0x1c, 0x26}
    },
    {
        {0xf5, 0xf3, 0xf9}, {0xea, 0xec, 0xf1}, {0x9c, 0x9d, 0xac}, {0x18, 0x20, 0x29},
        {0x8b, 0x8d, 0x9c}, {0x14, 0x1a, 0x20}, {0x74, 0x7c, 0x84}, {0x54, 0x5d, 0x6a},
        {0x1a, 0x20, 0x28}, {0x48, 0x50, 0x5d}, {0x12, 0x16, 0x1e}
    },
    {
        {0x7b, 0x81, 0x94}, {0x7b, 0x81, 0x94}, {0x5a, 0x61, 0x73}, {0x04, 0x0c, 0x08},
        {0x4a, 0x55, 0x62}, {0x00, 0x08, 0x04}, {0x46, 0x4d, 0x5c}, {0x32, 0x39, 0x46},
        {0x10, 0x14, 0x19}, {0x2a, 0x30, 0x3c}, {0x0a, 0x0e, 0x12}
    },
    {
        {0x51, 0x5a, 0x6a}, {0x54, 0x5b, 0x69}, {0x39, 0x48, 0x52}, {0x04, 0x0c, 0x10},
        {0x31, 0x40, 0x4a}, {0x00, 0x08, 0x0c}, {0x2c, 0x33, 0x3d}, {0x1e, 0x26, 0x30},
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

// Exact Android moving platform rainbow border palette (libcgame.so gPlatformPalette @ 0x7a5014):
// [theme 0..3][step 0..8][0=top, 1=left (+Z), 2=right (+X)]
static const Rgb8 kAndroidPlatEdgePalette[4][9][3] = {
    {
        {{0xf7, 0xe0, 0xff}, {0xe9, 0xae, 0xff}, {0xdc, 0x7a, 0xff}},
        {{0xff, 0xde, 0xf8}, {0xff, 0xab, 0xed}, {0xff, 0x78, 0xe2}},
        {{0xff, 0xe9, 0xe3}, {0xff, 0xc1, 0xaf}, {0xff, 0xc8, 0x3d}},
        {{0xff, 0xe5, 0xa3}, {0xff, 0xd7, 0x71}, {0xff, 0xc8, 0x3d}},
        {{0xea, 0xff, 0x9e}, {0xca, 0xff, 0x06}, {0xbc, 0xf0, 0x00}},
        {{0xcb, 0xff, 0xb2}, {0x86, 0xff, 0x4c}, {0x5b, 0xed, 0x18}},
        {{0xbf, 0xf1, 0xff}, {0x72, 0xdf, 0xfe}, {0x3c, 0xc9, 0xf0}},
        {{0xd1, 0xdf, 0xff}, {0xab, 0xc4, 0xff}, {0x78, 0xa0, 0xff}},
        {{0xe8, 0xe0, 0xff}, {0xcd, 0xbe, 0xff}, {0xa7, 0x8c, 0xff}}
    },
    {
        {{0xf2, 0xd0, 0xff}, {0xe4, 0x9c, 0xff}, {0xd6, 0x66, 0xff}},
        {{0xff, 0xce, 0xf4}, {0xff, 0x99, 0xe9}, {0xff, 0x64, 0xdc}},
        {{0xfe, 0xdd, 0xd3}, {0xff, 0xb3, 0x9d}, {0xff, 0xc1, 0x27}},
        {{0xff, 0xe0, 0x91}, {0xff, 0xd1, 0x5d}, {0xff, 0xc1, 0x27}},
        {{0xe5, 0xff, 0x8c}, {0xbf, 0xf8, 0x00}, {0xb2, 0xe8, 0x00}},
        {{0xbe, 0xff, 0xa0}, {0x77, 0xff, 0x36}, {0x4a, 0xec, 0x00}},
        {{0xae, 0xed, 0xff}, {0x5d, 0xda, 0xff}, {0x16, 0xcb, 0xff}},
        {{0xc1, 0xd3, 0xff}, {0x99, 0xb7, 0xff}, {0x64, 0x92, 0xff}},
        {{0xdd, 0xd0, 0xff}, {0xc0, 0xad, 0xff}, {0x98, 0x79, 0xff}}
    },
    {
        {{0xf8, 0xd4, 0xff}, {0xe2, 0x88, 0xff}, {0xcd, 0x38, 0xff}},
        {{0xff, 0xd1, 0xf9}, {0xff, 0x83, 0xe8}, {0xff, 0x35, 0xd7}},
        {{0xff, 0xe2, 0xd9}, {0xff, 0xa5, 0x89}, {0xff, 0xae, 0x00}},
        {{0xff, 0xdb, 0x77}, {0xff, 0xc6, 0x2a}, {0xff, 0xae, 0x00}},
        {{0xe3, 0xff, 0x6f}, {0xb2, 0xff, 0x00}, {0x9c, 0xed, 0x00}},
        {{0xb4, 0xff, 0x8e}, {0x4a, 0xff, 0x00}, {0x08, 0xe8, 0x00}},
        {{0xa2, 0xee, 0xff}, {0x2b, 0xd2, 0xff}, {0x00, 0xb0, 0xed}},
        {{0xbd, 0xd3, 0xff}, {0x83, 0xa9, 0xff}, {0x35, 0x72, 0xff}},
        {{0xe1, 0xd4, 0xff}, {0xb7, 0xa0, 0xff}, {0x7d, 0x53, 0xff}}
    },
    {
        {{0xe7, 0xab, 0xff}, {0xce, 0x50, 0xff}, {0xb3, 0x00, 0xf7}},
        {{0xff, 0xa7, 0xec}, {0xff, 0x4b, 0xd6}, {0xf7, 0x00, 0xbe}},
        {{0xff, 0xc0, 0xaf}, {0xff, 0x79, 0x52}, {0xf1, 0x8b, 0x00}},
        {{0xff, 0xc7, 0x3c}, {0xf7, 0xab, 0x00}, {0xf1, 0x8b, 0x00}},
        {{0xd1, 0xff, 0x34}, {0x8e, 0xf1, 0x00}, {0x77, 0xd6, 0x00}},
        {{0x8e, 0xff, 0x57}, {0x15, 0xf1, 0x00}, {0x00, 0xd0, 0x00}},
        {{0x6f, 0xdf, 0xff}, {0x00, 0xb9, 0xf7}, {0x00, 0x8d, 0xd6}},
        {{0x90, 0xb1, 0xff}, {0x4b, 0x80, 0xff}, {0x00, 0x43, 0xf7}},
        {{0xc1, 0xab, 0xff}, {0x8f, 0x6d, 0xff}, {0x49, 0x13, 0xff}}
    }
};

// Exact Java moving platform rainbow border palette (plateform.spr / plateform_big.spr frame[1..9]):
// [0] for themes 0..2 (pal[0..2] share identical rainbow colors), [1] for theme 3 (pal[3])
static const Rgb8 kJavaPlatEdgePalette[2][9][3] = {
    {
        {{0xbf, 0xf1, 0xff}, {0x72, 0xdf, 0xfe}, {0x3c, 0xc9, 0xf0}},
        {{0xd1, 0xdf, 0xff}, {0xab, 0xc4, 0xff}, {0x78, 0xa0, 0xff}},
        {{0xe8, 0xe0, 0xff}, {0xcd, 0xbe, 0xff}, {0xa7, 0x8c, 0xff}},
        {{0xf7, 0xe0, 0xff}, {0xe9, 0xae, 0xff}, {0xdc, 0x7a, 0xff}},
        {{0xff, 0xde, 0xf8}, {0xff, 0xab, 0xed}, {0xff, 0x78, 0xe2}},
        {{0xff, 0xe9, 0xe3}, {0xff, 0xc1, 0xaf}, {0xff, 0xc8, 0x3d}},
        {{0xff, 0xe5, 0xa3}, {0xff, 0xd7, 0x71}, {0xff, 0xc8, 0x3d}},
        {{0xea, 0xff, 0x9e}, {0xd4, 0xfc, 0x48}, {0xbc, 0xf0, 0x00}},
        {{0xcb, 0xff, 0xb2}, {0x86, 0xff, 0x4c}, {0x5b, 0xed, 0x18}}
    },
    {
        {{0xa6, 0xf1, 0xff}, {0x33, 0xd6, 0xff}, {0x00, 0xb5, 0xef}},
        {{0xc1, 0xd6, 0xff}, {0x88, 0xae, 0xff}, {0x3c, 0x78, 0xff}},
        {{0xe3, 0xd7, 0xff}, {0xbb, 0xa5, 0xff}, {0x82, 0x5a, 0xff}},
        {{0xfa, 0xd7, 0xff}, {0xe5, 0x8d, 0xff}, {0xd1, 0x3f, 0xff}},
        {{0xff, 0xd4, 0xfb}, {0xff, 0x88, 0xeb}, {0xff, 0x3c, 0xda}},
        {{0xff, 0xe5, 0xdc}, {0xff, 0xa9, 0x8e}, {0xff, 0xb4, 0x00}},
        {{0xff, 0xdf, 0x7c}, {0xff, 0xca, 0x32}, {0xff, 0xb4, 0x00}},
        {{0xe6, 0xff, 0x75}, {0xc6, 0xff, 0x00}, {0xa2, 0xef, 0x00}},
        {{0xb8, 0xff, 0x93}, {0x51, 0xff, 0x00}, {0x11, 0xeb, 0x00}}
    }
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

// Exact Android switch colors from libcgame.so CGeomCube::RenderAsBg (mode==5) & switch_off.spr:
// Top = #B0B8C4, Left (+Z) = #94A1B4, Right (+X) = #838D9C.
// PC switch.rmdl modulates switch.img (#B0B8C4) by per-vertex colors (Left=0xFF8B95AA, Right=0xFF858FA2).
static const Rgb8 kAndroidSwitchTopColor = {0xb0, 0xb8, 0xc4};
static constexpr uint32_t kPcSwitchLeftVCol       = 0xff8b95aau;
static constexpr uint32_t kPcSwitchRightVCol      = 0xff858fa2u;
static constexpr uint32_t kAndroidSwitchLeftVCol  = 0xffd6dfeau; // #B0B8C4 * #D6DFEA = #94A1B4
static constexpr uint32_t kAndroidSwitchRightVCol = 0xffbec3cbu; // #B0B8C4 * #BEC3CB = #838D9C
// Exact Java switch_off.spr colors: Top = #AEC3D0, Left (+Z) = #90ACBE, Right (+X) = #5A788D.
static constexpr uint32_t kJavaSwitchLeftVCol     = 0xffd3e1e9u; // #AEC3D0 * #D3E1E9 = #90ACBE
static constexpr uint32_t kJavaSwitchRightVCol    = 0xff849dadu; // #AEC3D0 * #849DAD = #5A788D

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
constexpr uint32_t kRainbowCubeHash    = 0x7D4C829Bu;
constexpr uint32_t kFlickeringStarHash = 0xDBC9C60Au;
constexpr int kTargetTexCount = 14;

// Exact PC rainbowcube.img bottom pastel band (rows 43..63) 13-color cycle
static const Rgb8 kPcHoloSwitchColors[13] = {
    {0xfe, 0xa9, 0xb6}, {0xfe, 0x9f, 0xeb}, {0xee, 0x9d, 0xfe}, {0xd3, 0xa9, 0xfe},
    {0xb5, 0xbe, 0xfe}, {0xa1, 0xcf, 0xfe}, {0x96, 0xda, 0xfe}, {0x3d, 0xf8, 0x9d},
    {0x80, 0xff, 0x85}, {0xad, 0xff, 0x5a}, {0xee, 0xf2, 0x1c}, {0xff, 0xd2, 0x75},
    {0xff, 0xbd, 0x84}
};

// Exact Android holo_switch.spr 8-color cycle (frames 0..7)
static const Rgb8 kAndroidHoloSwitchColors[8] = {
    {0x78, 0xa8, 0xe8}, {0x5c, 0xd2, 0xba}, {0x40, 0xfc, 0x8c}, {0xa8, 0xf5, 0x78},
    {0xfc, 0xc0, 0x60}, {0xfc, 0xa4, 0x98}, {0xfc, 0x88, 0xd0}, {0xba, 0x98, 0xdc}
};

// Exact Java holo_switch.spr 4-color cycle (frames 0..3)
static const Rgb8 kJavaHoloSwitchColors[4] = {
    {0x78, 0xa8, 0xe8}, {0x40, 0xfc, 0x8c}, {0xfc, 0xc0, 0x60}, {0xfc, 0x88, 0xd0}
};

// Exact Java star.spr dim slate tint ([0]=themes 0..2, [1]=theme 3)
static const Rgb8 kJavaStarColors[2] = {
    {0x55, 0x58, 0x60}, {0x3f, 0x42, 0x4a}
};

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
static IDirect3DTexture9* g_platEdgeTex[2][4] = {};
static IDirect3DTexture9* g_holoSwitchTex[3] = {};
static IDirect3DTexture9* g_javaStarTex[2] = {};
static IDirect3DTexture9* g_mobileFallingTex[4][16] = {};
static void*              g_pcLevelWallSp[4][2] = {};
static void*              g_pcPlanetSp[4][2] = {};
enum WallDrawTarget : uint8_t {
    WALL_TARGET_TERRAIN     = 0,
    WALL_TARGET_MOVING_PLAT = 1,
    WALL_TARGET_BUMPER      = 2
};

static int            g_mobileFallingBucket    = -1;
static WallDrawTarget g_wallDrawTarget         = WALL_TARGET_TERRAIN;
static bool           g_inMovingPlatEdgeRender = false;
static bool           g_inHoloSwitchRender     = false;

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

static IDirect3DTexture9* CreatePlatEdgeTexture(IDirect3DDevice9* dev, const Rgb8 pal[9][3]) {
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = dev->CreateTexture(256, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr) || tex == nullptr) return nullptr;

    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits != nullptr) {
        uint8_t* base = static_cast<uint8_t*>(lr.pBits);
        uint32_t rowTop[256];
        uint32_t rowLeft[256];
        uint32_t rowRight[256];
        for (int x = 0; x < 256; ++x) {
            float f = (static_cast<float>(x) / 256.0f) * 9.0f;
            int i0 = static_cast<int>(f) % 9;
            int i1 = (i0 + 1) % 9;
            float frac = f - std::floor(f);
            rowTop[x]   = PackArgb(0xffu, LerpRgb8(pal[i0][0], pal[i1][0], frac));
            rowLeft[x]  = PackArgb(0xffu, LerpRgb8(pal[i0][1], pal[i1][1], frac));
            rowRight[x] = PackArgb(0xffu, LerpRgb8(pal[i0][2], pal[i1][2], frac));
        }
        for (int y = 0; y < 64; ++y) {
            const uint32_t* src = (y < 21) ? rowTop : ((y < 43) ? rowLeft : rowRight);
            std::memcpy(base + y * lr.Pitch, src, sizeof(rowTop));
        }
        tex->UnlockRect(0);
    } else {
        tex->Release();
        return nullptr;
    }
    return tex;
}

static IDirect3DTexture9* CreateHorizontalCycleTexture(
    IDirect3DDevice9* dev, const Rgb8* cols, int count
) {
    if (dev == nullptr || cols == nullptr || count <= 0) return nullptr;
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = dev->CreateTexture(256, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr) || tex == nullptr) return nullptr;

    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits != nullptr) {
        uint8_t* base = static_cast<uint8_t*>(lr.pBits);
        uint32_t row[256];
        for (int x = 0; x < 256; ++x) {
            float phase = (static_cast<float>(x) / 256.0f) * static_cast<float>(count);
            int i0 = static_cast<int>(phase) % count;
            int i1 = (i0 + 1) % count;
            float frac = phase - std::floor(phase);
            row[x] = PackArgb(0xffu, LerpRgb8(cols[i0], cols[i1], frac));
        }
        for (int y = 0; y < 64; ++y) {
            std::memcpy(base + y * lr.Pitch, row, sizeof(row));
        }
        tex->UnlockRect(0);
    } else {
        tex->Release();
        return nullptr;
    }
    return tex;
}

static IDirect3DTexture9* CreateJavaTintedStarTexture(
    IDirect3DDevice9* dev, IDirect3DTexture9* srcTex, Rgb8 tint
) {
    if (dev == nullptr || srcTex == nullptr) return nullptr;
    D3DSURFACE_DESC desc{};
    if (FAILED(srcTex->GetLevelDesc(0, &desc))) return nullptr;
    if (desc.Format != D3DFMT_A8R8G8B8 || desc.Pool == D3DPOOL_DEFAULT || desc.Width == 0 || desc.Height == 0) {
        return nullptr;
    }

    IDirect3DTexture9* dstTex = nullptr;
    HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &dstTex, nullptr);
    if (FAILED(hr) || dstTex == nullptr) return nullptr;

    D3DLOCKED_RECT srcLr{};
    D3DLOCKED_RECT dstLr{};
    if (SUCCEEDED(srcTex->LockRect(0, &srcLr, nullptr, D3DLOCK_READONLY)) && srcLr.pBits != nullptr) {
        if (SUCCEEDED(dstTex->LockRect(0, &dstLr, nullptr, 0)) && dstLr.pBits != nullptr) {
            uint32_t rgb = (static_cast<uint32_t>(tint.r) << 16)
                         | (static_cast<uint32_t>(tint.g) << 8)
                         | static_cast<uint32_t>(tint.b);
            const uint8_t* srcBase = static_cast<const uint8_t*>(srcLr.pBits);
            uint8_t* dstBase = static_cast<uint8_t*>(dstLr.pBits);
            for (UINT y = 0; y < desc.Height; ++y) {
                const uint32_t* srcRow = reinterpret_cast<const uint32_t*>(srcBase + y * srcLr.Pitch);
                uint32_t* dstRow = reinterpret_cast<uint32_t*>(dstBase + y * dstLr.Pitch);
                for (UINT x = 0; x < desc.Width; ++x) {
                    dstRow[x] = (srcRow[x] & 0xff000000u) | rgb;
                }
            }
            dstTex->UnlockRect(0);
            srcTex->UnlockRect(0);
            return dstTex;
        }
        srcTex->UnlockRect(0);
    }
    dstTex->Release();
    return nullptr;
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
        Rgb8 cLbevRgb  = isJava ? cTopRgb                                : ScaleRgb(pal[1], 0.9f);
        Rgb8 cRbevRgb  = isJava ? cTopRgb                                : ScaleRgb(pal[1], 0.9f);
        uint32_t cTop   = PackArgb(0xffu, cTopRgb);
        uint32_t cLbev  = PackArgb(0xffu, cLbevRgb);
        uint32_t cRbev  = PackArgb(0xffu, cRbevRgb);
        uint32_t cLeft  = PackArgb(0xffu, cLeftRgb);
        uint32_t cRight = PackArgb(0xffu, cRightRgb);
        for (int y = 0; y < 64; ++y) {
            uint32_t* row = reinterpret_cast<uint32_t*>(base + y * lr->Pitch);
            for (int x = 0; x < 64; ++x) {
                uint32_t col;
                if (x >= 53) {
                    col = cRight;
                } else if (y >= 53) {
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
        Rgb8 sw = isJava ? kJavaSwitchColor : kAndroidSwitchTopColor;
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

static IDirect3DTexture9* CreateMobileFallingTexture(IDirect3DDevice9* dev, uint8_t themeIdx, int bucket) {
    if (dev == nullptr || themeIdx >= 4u) return nullptr;
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = dev->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr) || tex == nullptr) return nullptr;

    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)) && lr.pBits != nullptr) {
        const Rgb8* pal = kAndroidLevelPalette[themeIdx];
        float t = static_cast<float>(std::clamp(bucket, 0, 15)) / 15.0f;
        uint32_t cTop   = PackArgb(0xffu, ScaleRgb(pal[0], 0.9f));
        uint32_t cLbev  = PackArgb(0xffu, ScaleRgb(pal[1], 0.9f));
        uint32_t cRbev  = cLbev;
        uint32_t cLeft  = PackArgb(0xffu, LerpRgb8(pal[2], pal[3], t));
        uint32_t cRight = PackArgb(0xffu, LerpRgb8(pal[4], pal[5], t));
        auto* base = static_cast<uint8_t*>(lr.pBits);
        for (int y = 0; y < 64; ++y) {
            uint32_t* row = reinterpret_cast<uint32_t*>(base + y * lr.Pitch);
            for (int x = 0; x < 64; ++x) {
                uint32_t col;
                if (x >= 53) {
                    col = cRight;
                } else if (y >= 53) {
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
        tex->UnlockRect(0);
    } else {
        tex->Release();
        return nullptr;
    }
    return tex;
}

static void* GetVanillaThemeD3DTex(void* (&spCache)[4][2], int specBaseIdx, uint8_t themeIdx) {
    if (themeIdx > 3u) return nullptr;
    if (spCache[themeIdx][0] == nullptr) {
        uint32_t resId[2] = {kTargetTexSpecs[specBaseIdx + themeIdx].hash0, kTexturesFolderHash};
        void* tmpSp[2] = {nullptr, nullptr};
        reinterpret_cast<void*(__cdecl*)(void*, const uint32_t*, int, int)>(0x404070)(tmpSp, resId, 0, 0);
        if (tmpSp[0] != nullptr) {
            reinterpret_cast<void*(__thiscall*)(void*, const void*)>(0x4ecfd0)(spCache[themeIdx], tmpSp);
        }
        reinterpret_cast<void(__thiscall*)(void*)>(0x421ff0)(tmpSp);
    }
    uint8_t* texObj = static_cast<uint8_t*>(spCache[themeIdx][0]);
    if (texObj == nullptr) return nullptr;
    return *reinterpret_cast<void**>(texObj + 0x2c);
}

static void* GetVanillaLevelWallD3DTex(uint8_t themeIdx) {
    return GetVanillaThemeD3DTex(g_pcLevelWallSp, 0, themeIdx);
}

static void* GetVanillaPlanetD3DTex(uint8_t themeIdx) {
    return GetVanillaThemeD3DTex(g_pcPlanetSp, 4, themeIdx);
}

static void ReleaseCachedTexturesOnDeviceChange(IDirect3DDevice9* d3dDev) {
    if (d3dDev == nullptr || d3dDev == g_cachedDev) return;
    for (int t = 0; t < 2; ++t) {
        for (int i = 0; i < kTargetTexCount; ++i) {
            if (g_themeTex[t][i] != nullptr) {
                g_themeTex[t][i]->Release();
                g_themeTex[t][i] = nullptr;
            }
        }
        for (int i = 0; i < 4; ++i) {
            if (g_platEdgeTex[t][i] != nullptr) {
                g_platEdgeTex[t][i]->Release();
                g_platEdgeTex[t][i] = nullptr;
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
        for (int b = 0; b < 16; ++b) {
            if (g_mobileFallingTex[i][b] != nullptr) {
                g_mobileFallingTex[i][b]->Release();
                g_mobileFallingTex[i][b] = nullptr;
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (g_holoSwitchTex[i] != nullptr) {
            g_holoSwitchTex[i]->Release();
            g_holoSwitchTex[i] = nullptr;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (g_javaStarTex[i] != nullptr) {
            g_javaStarTex[i]->Release();
            g_javaStarTex[i] = nullptr;
        }
    }
    g_cachedDev = d3dDev;
}

static void* __cdecl ResolveD3DTextureForApply(const uint8_t* texObj, IDirect3DDevice9* d3dDev) {
    if (texObj == nullptr) return nullptr;
    void* defaultTex = *reinterpret_cast<void* const*>(texObj + 0x2c);
    if (d3dDev == nullptr) return defaultTex;
    if (*reinterpret_cast<const uint32_t*>(texObj + 0x08) != kTexturesFolderHash) return defaultTex;

    const CustomThemeSettings eff = GetEffectiveTheme();
    uint32_t hash0 = *reinterpret_cast<const uint32_t*>(texObj + 0x04);
    uint8_t* lvl = GetLevel();
    bool isHolo = (g_inHoloSwitchRender && hash0 == kRainbowCubeHash);
    bool isPlatEdge = (g_inMovingPlatEdgeRender && hash0 == kRainbowCubeHash);
    bool isJavaStar = (eff.stars != 0u && hash0 == kFlickeringStarHash && lvl != nullptr);
    bool isCustom = isPlatEdge || isHolo || isJavaStar;

    int slot = -1;
    if (!isCustom) {
        for (int i = 0; i < kTargetTexCount; ++i) {
            if (kTargetTexSpecs[i].hash0 == hash0) {
                slot = i;
                break;
            }
        }
        if (slot < 0) return defaultTex;
    }

    ReleaseCachedTexturesOnDeviceChange(d3dDev);

    if (isPlatEdge) {
        if (eff.platformBorders == 0u) return defaultTex;
        bool isJavaEdge = (eff.platformBorders == 2u);
        uint8_t themeIdx = (lvl != nullptr) ? (lvl[0x6c] & 3u) : 0u;
        int modeSlot = isJavaEdge ? 1 : 0;
        if (g_platEdgeTex[modeSlot][themeIdx] == nullptr) {
            const Rgb8 (*pal)[3] = isJavaEdge
                ? kJavaPlatEdgePalette[(themeIdx == 3u) ? 1 : 0]
                : kAndroidPlatEdgePalette[themeIdx];
            g_platEdgeTex[modeSlot][themeIdx] = CreatePlatEdgeTexture(d3dDev, pal);
        }
        return g_platEdgeTex[modeSlot][themeIdx] != nullptr
            ? static_cast<void*>(g_platEdgeTex[modeSlot][themeIdx])
            : defaultTex;
    } else if (isHolo) {
        uint8_t mode = (eff.hintTile <= 2u) ? eff.hintTile : 0u;
        if (g_holoSwitchTex[mode] == nullptr) {
            if (mode == 0u) {
                g_holoSwitchTex[mode] = CreateHorizontalCycleTexture(d3dDev, kPcHoloSwitchColors, 13);
            } else if (mode == 1u) {
                g_holoSwitchTex[mode] = CreateHorizontalCycleTexture(d3dDev, kAndroidHoloSwitchColors, 8);
            } else {
                g_holoSwitchTex[mode] = CreateHorizontalCycleTexture(d3dDev, kJavaHoloSwitchColors, 4);
            }
        }
        return g_holoSwitchTex[mode] != nullptr ? static_cast<void*>(g_holoSwitchTex[mode]) : defaultTex;
    } else if (isJavaStar) {
        uint8_t themeIdx = lvl[0x6c] & 3u;
        int idx = (themeIdx == 3u) ? 1 : 0;
        if (g_javaStarTex[idx] == nullptr) {
            g_javaStarTex[idx] = CreateJavaTintedStarTexture(
                d3dDev, reinterpret_cast<IDirect3DTexture9*>(defaultTex), kJavaStarColors[idx]
            );
        }
        return g_javaStarTex[idx] != nullptr ? static_cast<void*>(g_javaStarTex[idx]) : defaultTex;
    }

    bool remapPreviewTerrainWall = IsGraphicsPreviewActive()
        && kTargetTexSpecs[slot].kind == TEX_LEVELWALL
        && g_wallDrawTarget == WALL_TARGET_TERRAIN
        && lvl != nullptr;
    bool remapPreviewPlanet = IsGraphicsPreviewActive()
        && kTargetTexSpecs[slot].kind == TEX_PLANET
        && lvl != nullptr;
    if (remapPreviewTerrainWall) {
        slot = static_cast<int>(lvl[0x6c] & 3u);
    } else if (remapPreviewPlanet) {
        slot = 4 + static_cast<int>(lvl[0x6c] & 3u);
    }

    const TargetTexSpec& spec = kTargetTexSpecs[slot];
    uint8_t style = 0;
    if (spec.kind == TEX_LEVELWALL) {
        if (g_wallDrawTarget == WALL_TARGET_MOVING_PLAT) {
            style = eff.movingPlatforms;
        } else if (g_wallDrawTarget == WALL_TARGET_BUMPER) {
            style = eff.bumpers;
        } else {
            style = eff.terrain;
        }
    } else if (spec.kind == TEX_PLANET) {
        style = eff.planets;
    } else if (spec.kind == TEX_FALLING_PLATFORM) {
        style = eff.fallingPlatforms;
    } else {
        style = eff.switches;
    }

    if (style == 0u) {
        if (remapPreviewTerrainWall && spec.themeIdx != 0u) {
            void* pcTex = GetVanillaLevelWallD3DTex(spec.themeIdx);
            if (pcTex != nullptr) return pcTex;
        } else if (remapPreviewPlanet && spec.themeIdx != 0u) {
            void* pcTex = GetVanillaPlanetD3DTex(spec.themeIdx);
            if (pcTex != nullptr) return pcTex;
        }
        return defaultTex;
    }
    bool isJava = (style == 2u);

    if (!isJava && g_mobileFallingBucket >= 0 && spec.kind == TEX_FALLING_PLATFORM) {
        uint8_t themeIdx = spec.themeIdx;
        int b = std::clamp(g_mobileFallingBucket, 0, 15);
        if (g_mobileFallingTex[themeIdx][b] == nullptr) {
            g_mobileFallingTex[themeIdx][b] = CreateMobileFallingTexture(d3dDev, themeIdx, b);
        }
        if (g_mobileFallingTex[themeIdx][b] != nullptr) {
            return static_cast<void*>(g_mobileFallingTex[themeIdx][b]);
        }
    }
    if (isJava && spec.kind == TEX_LEVELWALL) {
        uint8_t themeIdx = spec.themeIdx;
        if (g_wallDrawTarget == WALL_TARGET_MOVING_PLAT) {
            if (g_javaMovingPlatTex[themeIdx] == nullptr) {
                g_javaMovingPlatTex[themeIdx] = CreateJavaSolidWallTexture(d3dDev, kJavaMovingPlatPalette[themeIdx]);
            }
            if (g_javaMovingPlatTex[themeIdx] != nullptr) {
                return static_cast<void*>(g_javaMovingPlatTex[themeIdx]);
            }
        } else if (g_wallDrawTarget == WALL_TARGET_BUMPER) {
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
        g_themeTex[themeSlot][slot] = CreateThemeTexture(d3dDev, spec, isJava);
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

static D3DLIGHT9 g_pcLight0{};
static D3DLIGHT9 g_pcLight1{};
static BOOL      g_pcLight1Enabled = FALSE;
static bool      g_hasPcLight0     = false;
static bool      g_hasPcLight1     = false;

static D3DLIGHT9 MakeThemeLight0Template(const D3DLIGHT9& base, D3DCOLORVALUE diffuse, D3DVECTOR dir) {
    D3DLIGHT9 out = base;
    out.Diffuse   = diffuse;
    out.Direction = dir;
    return out;
}

static void FillWorldStyleLight0(uint8_t style, D3DLIGHT9* light0) {
    if (light0 == nullptr) return;
    if (style == 2u) {
        float c = 194.0f / 255.0f;
        light0->Diffuse   = {c, c, c, 1.0f};
        light0->Direction = {-0.3600000f, -0.7500000f, -0.5548874f};
    } else {
        float c = 205.0f / 255.0f;
        light0->Diffuse   = {c, c, c, 1.0f};
        light0->Direction = {-0.4327311f, -0.7212185f, -0.5409138f};
    }
}

struct ScopedThemeLight {
    IDirect3DDevice9* dev = nullptr;
    D3DLIGHT9 saved0{};
    D3DLIGHT9 saved1{};
    BOOL saved1En = FALSE;
    bool hasSaved0 = false;
    bool hasSaved1 = false;

    void BeginRestorePc() {
        dev = reinterpret_cast<IDirect3DDevice9*(__cdecl*)()>(0x40e6c0)();
        if (dev == nullptr) return;
        if (g_hasPcLight0 && SUCCEEDED(dev->GetLight(0, &saved0))) {
            hasSaved0 = true;
            dev->SetLight(0, &g_pcLight0);
        }
        if (g_hasPcLight1 && SUCCEEDED(dev->GetLight(1, &saved1)) && SUCCEEDED(dev->GetLightEnable(1, &saved1En))) {
            hasSaved1 = true;
            dev->SetLight(1, &g_pcLight1);
            dev->LightEnable(1, g_pcLight1Enabled);
        }
    }

    void BeginSingle(D3DCOLORVALUE diffuse, D3DVECTOR dir) {
        dev = reinterpret_cast<IDirect3DDevice9*(__cdecl*)()>(0x40e6c0)();
        if (dev == nullptr) return;
        if (SUCCEEDED(dev->GetLight(0, &saved0))) {
            hasSaved0 = true;
            D3DLIGHT9 l0 = MakeThemeLight0Template(saved0, diffuse, dir);
            dev->SetLight(0, &l0);
        }
        if (SUCCEEDED(dev->GetLightEnable(1, &saved1En))) {
            hasSaved1 = true;
            dev->LightEnable(1, FALSE);
        }
    }

    void BeginWorldStyle(uint8_t style) {
        if (style == 0u) {
            if (GetEffectiveTheme().playerCube != 0u) {
                BeginRestorePc();
            }
            return;
        }
        dev = reinterpret_cast<IDirect3DDevice9*(__cdecl*)()>(0x40e6c0)();
        if (dev == nullptr) return;
        if (SUCCEEDED(dev->GetLight(0, &saved0))) {
            hasSaved0 = true;
            D3DLIGHT9 l0 = saved0;
            FillWorldStyleLight0(style, &l0);
            dev->SetLight(0, &l0);
        }
        if (SUCCEEDED(dev->GetLightEnable(1, &saved1En))) {
            hasSaved1 = true;
            dev->LightEnable(1, FALSE);
        }
    }

    ~ScopedThemeLight() {
        if (dev == nullptr) return;
        if (hasSaved0) dev->SetLight(0, &saved0);
        if (hasSaved1) {
            if (hasSaved0 && g_hasPcLight1 && saved1En) {
                dev->SetLight(1, &saved1);
            }
            dev->LightEnable(1, saved1En);
        }
    }
};

static void __fastcall Hook_LightApply(uint8_t* light) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x4e9810);
    if (light == nullptr) {
        orig(light);
        return;
    }

    orig(light);

    IDirect3DDevice9* dev = reinterpret_cast<IDirect3DDevice9*(__cdecl*)()>(0x40e6c0)();
    if (dev == nullptr) return;

    // LightD3D starts at light + 0x3c; D3D light slot index is at LightD3D + 0x68 (= light + 0xa4)
    int32_t d3dIdx = *reinterpret_cast<const int32_t*>(light + 0xa4);
    if (d3dIdx == 0) {
        if (SUCCEEDED(dev->GetLight(0, &g_pcLight0))) {
            g_hasPcLight0 = true;
        }
    } else if (d3dIdx == 1) {
        if (SUCCEEDED(dev->GetLight(1, &g_pcLight1)) && SUCCEEDED(dev->GetLightEnable(1, &g_pcLight1Enabled))) {
            g_hasPcLight1 = true;
        }
    }

    uint8_t playerCubeStyle = GetEffectiveTheme().playerCube;
    if (playerCubeStyle == 0u) return;

    if (d3dIdx == 0) {
        D3DLIGHT9 l0{};
        if (SUCCEEDED(dev->GetLight(0, &l0))) {
            FillWorldStyleLight0(playerCubeStyle, &l0);
            dev->SetLight(0, &l0);
        }
    } else {
        dev->LightEnable(static_cast<DWORD>(d3dIdx), FALSE);
    }
}

static bool         g_prismMatOverride       = false;
static bool         g_anyNormalPrismRendered = false;
static D3DMATERIAL9 g_prismMat{};

static void PreparePrismMaterial(int32_t animTick, uint8_t prismStyle) {
    float visTick = GetVisualEntityTick(GetWorldScreen(), animTick);

    std::memset(&g_prismMat, 0, sizeof(g_prismMat));
    g_prismMat.Diffuse.a = 1.0f;
    g_prismMat.Ambient.a = 1.0f;

    if (prismStyle == 1u) {
        float mPhase = std::fmod(visTick, 18.0f);
        if (mPhase < 0.0f) mPhase += 18.0f;
        int i0 = static_cast<int>(mPhase) % 18;
        int i1 = (i0 + 1) % 18;
        float frac = mPhase - std::floor(mPhase);
        Rgb8 base = LerpRgb8(kAndroidPrismColors[i0], kAndroidPrismColors[i1], frac);
        float diffScale = 80.0f / 205.0f;
        g_prismMat.Diffuse.r  = diffScale;
        g_prismMat.Diffuse.g  = diffScale;
        g_prismMat.Diffuse.b  = diffScale;
        g_prismMat.Emissive.r = static_cast<float>(base.r) / 255.0f;
        g_prismMat.Emissive.g = static_cast<float>(base.g) / 255.0f;
        g_prismMat.Emissive.b = static_cast<float>(base.b) / 255.0f;
    } else {
        float phase = std::fmod(visTick / 1.35f, 9.0f);
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

static void __fastcall Hook_PrismRender(uint8_t* prism) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x487800);
    const CustomThemeSettings eff = GetEffectiveTheme();
    if (eff.prisms == 0u || prism == nullptr || *reinterpret_cast<void* const*>(prism + 0x24) != nullptr) {
        ScopedThemeLight lightGuard;
        if (eff.prisms == 0u && eff.playerCube != 0u) {
            lightGuard.BeginRestorePc();
        }
        if (prism != nullptr && *reinterpret_cast<void* const*>(prism + 0x24) == nullptr) {
            int32_t origTick = *reinterpret_cast<int32_t*>(prism + 0x14);
            *reinterpret_cast<int32_t*>(prism + 0x14) = origTick + static_cast<int32_t>(std::floor(GetPauseExtraTicks()));
            orig(prism);
            *reinterpret_cast<int32_t*>(prism + 0x14) = origTick;
        } else {
            orig(prism);
        }
        return;
    }

    int32_t animTick = *reinterpret_cast<const int32_t*>(prism + 0x14);
    PreparePrismMaterial(animTick, eff.prisms);
    g_prismMatOverride = true;
    g_anyNormalPrismRendered = true;
    {
        ScopedThemeLight keyLight;
        if (eff.prisms == 2u) {
            keyLight.BeginSingle({1.0f, 1.0f, 1.0f, 1.0f}, {0.2500000f, -0.8700000f, -0.4249706f});
        } else {
            keyLight.BeginWorldStyle(1u);
        }
        orig(prism);
    }
    g_prismMatOverride = false;
}

static void __fastcall Hook_PrismRenderTransparents(void* renderer) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x407410);
    const CustomThemeSettings eff = GetEffectiveTheme();
    uint8_t* sm = GetScreenMgr();
    bool inDebrief = (sm != nullptr && *reinterpret_cast<const uint32_t*>(sm + 0x50) == 0x13u);
    bool overrideMat = (eff.prisms != 0u && g_anyNormalPrismRendered && !inDebrief);
    if (overrideMat) {
        g_prismMatOverride = true;
    }
    {
        ScopedThemeLight keyLight;
        if (overrideMat) {
            if (eff.prisms == 2u) {
                keyLight.BeginSingle({1.0f, 1.0f, 1.0f, 1.0f}, {0.2500000f, -0.8700000f, -0.4249706f});
            } else {
                keyLight.BeginWorldStyle(1u);
            }
        } else if (eff.prisms == 0u && eff.playerCube != 0u) {
            keyLight.BeginRestorePc();
        }
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
    if (GetEffectiveTheme().playerCube == 2u) {
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
static uint32_t g_rawCubePal15 = 0xffffffu;

static void __cdecl Hook_ComputeCubePalette(int theme) {
    reinterpret_cast<void(__cdecl*)(int)>(0x48b980)(theme);
    uint32_t* pal = reinterpret_cast<uint32_t*>(0x5f9110);
    g_rawCubePal15 = pal[15];
    const CustomThemeSettings eff = GetEffectiveTheme();
    if (eff.playerCube == 2u || eff.darkCube == 2u || eff.hintGhost == 2u) {
        uint8_t* ws = *reinterpret_cast<uint8_t**>(0x5f9064);
        bool isRainbow = (ws != nullptr && ws[0xb5] != 0);
        int t = (theme >= 0 && theme <= 3) ? theme : 2;
        // Exact Java a.e() / e.f() offsets for 0.spr Frame 0 (Top=am[8], Left=am[11], Right=am[14])
        static const int kJavaMid[4]        = { 9,  8, 11, 10};
        static const int kJavaDeltaTop[4]   = { 5,  0, 27, 18};
        static const int kJavaDeltaRight[4] = {-50, -60, -36, -40};

        uint32_t midCol = pal[isRainbow ? 11 : kJavaMid[t]];
        int mr = static_cast<int>((midCol >> 16) & 0xffu);
        int mg = static_cast<int>((midCol >> 8) & 0xffu);
        int mb = static_cast<int>(midCol & 0xffu);

        int dTop   = isRainbow ? 27  : kJavaDeltaTop[t];
        int dRight = isRainbow ? -42 : kJavaDeltaRight[t];

        int rr = std::clamp(mr + dRight, 0, 255);
        int rg = std::clamp(mg + dRight, 0, 255);
        int rb = std::clamp(mb + dRight, 0, 255);

        if (eff.playerCube == 2u) {
            pal[15] = (static_cast<uint32_t>(rr) << 16)
                    | (static_cast<uint32_t>(rg) << 8)
                    | static_cast<uint32_t>(rb);
        }

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
static uint8_t  s_appliedWallGrad   = 0;
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

struct PreviewPlanetVert {
    float x, y, z, u, v;
};

static IDirect3DTexture9* GetPreviewPlanetD3DTex(IDirect3DDevice9* dev, uint8_t themeIdx, uint8_t planetStyle) {
    themeIdx &= 3u;
    ReleaseCachedTexturesOnDeviceChange(dev);
    if (planetStyle == 0u) {
        return reinterpret_cast<IDirect3DTexture9*>(GetVanillaPlanetD3DTex(themeIdx));
    }
    bool isJava = (planetStyle == 2u);
    int tSlot = isJava ? 1 : 0;
    int texSlot = 4 + static_cast<int>(themeIdx);
    if (g_themeTex[tSlot][texSlot] == nullptr) {
        g_themeTex[tSlot][texSlot] = CreateThemeTexture(dev, kTargetTexSpecs[texSlot], isJava);
    }
    return g_themeTex[tSlot][texSlot];
}

static void AddPreviewPlanetBox(
    std::vector<PreviewPlanetVert>& out, float cx, float topY, float cz, float halfW, float height
) {
    float x0 = cx - halfW, x1 = cx + halfW;
    float z0 = cz - halfW, z1 = cz + halfW;
    float y0 = topY - height, y1 = topY;

    // Top face (+Y): solid top band across PC (x=56..63) and Mobile/Java (x=48..63)
    out.push_back({x0, y1, z0, 0.91f, 0.05f});
    out.push_back({x1, y1, z0, 0.98f, 0.05f});
    out.push_back({x1, y1, z1, 0.98f, 0.95f});
    out.push_back({x0, y1, z0, 0.91f, 0.05f});
    out.push_back({x1, y1, z1, 0.98f, 0.95f});
    out.push_back({x0, y1, z1, 0.91f, 0.95f});

    // Left face (+Z): left band u in [0.05, 0.38], v = 0.02 top -> 0.98 bottom
    out.push_back({x0, y1, z1, 0.05f, 0.02f});
    out.push_back({x1, y1, z1, 0.38f, 0.02f});
    out.push_back({x1, y0, z1, 0.38f, 0.98f});
    out.push_back({x0, y1, z1, 0.05f, 0.02f});
    out.push_back({x1, y0, z1, 0.38f, 0.98f});
    out.push_back({x0, y0, z1, 0.05f, 0.98f});

    // Right face (+X): mid band u in [0.55, 0.70], v = 0.02 top -> 0.98 bottom
    out.push_back({x1, y1, z1, 0.55f, 0.02f});
    out.push_back({x1, y1, z0, 0.70f, 0.02f});
    out.push_back({x1, y0, z0, 0.70f, 0.98f});
    out.push_back({x1, y1, z1, 0.55f, 0.02f});
    out.push_back({x1, y0, z0, 0.70f, 0.98f});
    out.push_back({x1, y0, z1, 0.55f, 0.98f});
}

struct ScopedPreviewPlanetD3DState {
    IDirect3DDevice9* dev = nullptr;
    D3DMATRIX         world{};
    DWORD             fvf = 0;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9*  ps = nullptr;
    IDirect3DBaseTexture9*  tex0 = nullptr;
    DWORD             rsLighting = FALSE, rsAlphaBlend = FALSE, rsAlphaTest = FALSE, rsFog = FALSE;
    DWORD             rsSpecular = FALSE, rsStencil = FALSE, rsZEnable = TRUE, rsZWrite = TRUE;
    DWORD             rsCull = D3DCULL_CCW, rsDepthBias = 0, rsSlopeBias = 0;
    DWORD             tssColorOp = 0, tssColorArg1 = 0, tssAlphaOp = 0, tssTexXform = 0, tssTexCoordIdx = 0;
    DWORD             tss1ColorOp = 0, tss1AlphaOp = 0;
    DWORD             smpAddrU = 0, smpAddrV = 0, smpMag = 0, smpMin = 0, smpMip = 0;

    explicit ScopedPreviewPlanetD3DState(IDirect3DDevice9* d, IDirect3DTexture9* tex) : dev(d) {
        if (dev == nullptr) return;
        dev->GetTransform(D3DTS_WORLD, &world);
        dev->GetFVF(&fvf);
        dev->GetVertexShader(&vs);
        dev->GetPixelShader(&ps);
        dev->GetTexture(0, &tex0);
        dev->GetRenderState(D3DRS_LIGHTING, &rsLighting);
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &rsAlphaBlend);
        dev->GetRenderState(D3DRS_ALPHATESTENABLE, &rsAlphaTest);
        dev->GetRenderState(D3DRS_FOGENABLE, &rsFog);
        dev->GetRenderState(D3DRS_SPECULARENABLE, &rsSpecular);
        dev->GetRenderState(D3DRS_STENCILENABLE, &rsStencil);
        dev->GetRenderState(D3DRS_ZENABLE, &rsZEnable);
        dev->GetRenderState(D3DRS_ZWRITEENABLE, &rsZWrite);
        dev->GetRenderState(D3DRS_CULLMODE, &rsCull);
        dev->GetRenderState(D3DRS_DEPTHBIAS, &rsDepthBias);
        dev->GetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, &rsSlopeBias);
        dev->GetTextureStageState(0, D3DTSS_COLOROP, &tssColorOp);
        dev->GetTextureStageState(0, D3DTSS_COLORARG1, &tssColorArg1);
        dev->GetTextureStageState(0, D3DTSS_ALPHAOP, &tssAlphaOp);
        dev->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &tssTexXform);
        dev->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &tssTexCoordIdx);
        dev->GetTextureStageState(1, D3DTSS_COLOROP, &tss1ColorOp);
        dev->GetTextureStageState(1, D3DTSS_ALPHAOP, &tss1AlphaOp);
        dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &smpAddrU);
        dev->GetSamplerState(0, D3DSAMP_ADDRESSV, &smpAddrV);
        dev->GetSamplerState(0, D3DSAMP_MAGFILTER, &smpMag);
        dev->GetSamplerState(0, D3DSAMP_MINFILTER, &smpMin);
        dev->GetSamplerState(0, D3DSAMP_MIPFILTER, &smpMip);

        static constexpr D3DMATRIX kIdentity = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        dev->SetTransform(D3DTS_WORLD, &kIdentity);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
        dev->SetVertexShader(nullptr);
        dev->SetPixelShader(nullptr);
        dev->SetTexture(0, tex);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
        dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZENABLE, TRUE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_DEPTHBIAS, 0);
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    }

    ~ScopedPreviewPlanetD3DState() {
        if (dev == nullptr) return;
        dev->SetTransform(D3DTS_WORLD, &world);
        dev->SetFVF(fvf);
        dev->SetVertexShader(vs);
        dev->SetPixelShader(ps);
        dev->SetTexture(0, tex0);
        if (vs != nullptr) vs->Release();
        if (ps != nullptr) ps->Release();
        if (tex0 != nullptr) tex0->Release();
        dev->SetRenderState(D3DRS_LIGHTING, rsLighting);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, rsAlphaBlend);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, rsAlphaTest);
        dev->SetRenderState(D3DRS_FOGENABLE, rsFog);
        dev->SetRenderState(D3DRS_SPECULARENABLE, rsSpecular);
        dev->SetRenderState(D3DRS_STENCILENABLE, rsStencil);
        dev->SetRenderState(D3DRS_ZENABLE, rsZEnable);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, rsZWrite);
        dev->SetRenderState(D3DRS_CULLMODE, rsCull);
        dev->SetRenderState(D3DRS_DEPTHBIAS, rsDepthBias);
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, rsSlopeBias);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, tssColorOp);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, tssColorArg1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, tssAlphaOp);
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, tssTexXform);
        dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, tssTexCoordIdx);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, tss1ColorOp);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, tss1AlphaOp);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, smpAddrU);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, smpAddrV);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, smpMag);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, smpMin);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, smpMip);
        OnColorThemeChanged();
    }
};

static void RenderPreviewTopPlanets(IDirect3DDevice9* dev) {
    if (dev == nullptr) return;
    uint8_t* lvl = GetLevel();
    uint8_t themeIdx = (lvl != nullptr) ? (lvl[0x6c] & 3u) : 0u;
    const CustomThemeSettings eff = GetEffectiveTheme();
    IDirect3DTexture9* tex = GetPreviewPlanetD3DTex(dev, themeIdx, eff.planets);
    if (tex == nullptr) return;

    struct TopPlanetSpec { float cx, topY, cz, halfW, height; };
    static constexpr TopPlanetSpec kTopPlanets[7] = {
        {-147.4f,  -7.7f,  142.9f, 22.0f, 220.0f},
        {-125.3f,   7.0f,   54.1f, 27.0f, 250.0f},
        { -93.4f,  19.4f,  -24.8f, 24.0f, 250.0f},
        { -41.6f,  21.6f,  -96.2f, 26.0f, 260.0f},
        {  43.3f,  27.0f, -134.5f, 25.0f, 250.0f},
        { 118.1f,  10.5f, -158.2f, 23.0f, 230.0f},
        { 180.6f,  -6.0f, -169.6f, 20.0f, 210.0f},
    };

    std::vector<PreviewPlanetVert> verts;
    verts.reserve(7 * 18);
    for (const auto& p : kTopPlanets) {
        AddPreviewPlanetBox(verts, p.cx, p.topY, p.cz, p.halfW, p.height);
    }

    ScopedPreviewPlanetD3DState guard(dev, tex);
    dev->DrawPrimitiveUP(
        D3DPT_TRIANGLELIST, static_cast<UINT>(verts.size() / 3u), verts.data(), sizeof(PreviewPlanetVert)
    );
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
        s_appliedWallGrad = 0;
    }

    uint8_t wantWallGrad = GetEffectiveTheme().wallGradient;
    if (s_appliedWallGrad != wantWallGrad && !s_wallSubMeshes.empty()) {
        float span = (static_cast<float>(heightTiles) - 0.5f) * 10.0f;
        if (span < 10.0f) span = 10.0f;
        for (auto& entry : s_wallSubMeshes) {
            if (entry.origV.size() != entry.numVerts) continue;
            void* ptr = nullptr;
            if (SUCCEEDED(entry.vb->Lock(0, 0, &ptr, 0)) && ptr != nullptr) {
                uint8_t* basePtr = static_cast<uint8_t*>(ptr);
                for (uint32_t i = 0; i < entry.numVerts; ++i) {
                    uint8_t* vptr = basePtr + i * entry.stride;
                    if (wantWallGrad != 0u) {
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
        s_appliedWallGrad = wantWallGrad;
    }

    orig(model, arg);

    if (IsGraphicsPreviewActive()) {
        uint8_t* renderer = *reinterpret_cast<uint8_t**>(0x5f8770);
        if (renderer != nullptr) {
            auto* dev = *reinterpret_cast<IDirect3DDevice9**>(renderer + 0xcc);
            RenderPreviewTopPlanets(dev);
        }
    }
}

static void __cdecl Hook_SetHeightUV(void* model, int y) {
    float vScale = 1.0f;
    float vOff = 0.0f;
    if (GetEffectiveTheme().wallGradient == 0u) {
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

struct BumperFlashVert {
    float x, y, z;
    D3DCOLOR color;
};
constexpr DWORD kBumperFlashFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;

struct ScopedBumperFlashD3DState {
    IDirect3DDevice9* dev = nullptr;
    D3DMATRIX savedWorld{};
    DWORD rsLighting = 0, rsCull = 0, rsZEnable = 0, rsZWrite = 0;
    DWORD rsAlphaBlend = 0, rsSrcBlend = 0, rsDstBlend = 0, rsAlphaTest = 0;
    DWORD rsFog = 0, rsSpecular = 0, rsColorVert = 0, rsDiffuseSrc = 0, rsShadeMode = 0;
    DWORD tss0ColorOp = 0, tss0ColorArg1 = 0, tss0AlphaOp = 0, tss0AlphaArg1 = 0;
    DWORD tss1ColorOp = 0, tss1AlphaOp = 0;
    DWORD savedFvf = 0;
    IDirect3DBaseTexture9* savedTex0 = nullptr;
    IDirect3DVertexShader9* savedVs = nullptr;
    IDirect3DPixelShader9* savedPs = nullptr;
    bool valid = false;

    explicit ScopedBumperFlashD3DState(IDirect3DDevice9* d) : dev(d) {
        if (dev == nullptr) return;
        dev->GetTransform(D3DTS_WORLD, &savedWorld);
        dev->GetRenderState(D3DRS_LIGHTING, &rsLighting);
        dev->GetRenderState(D3DRS_CULLMODE, &rsCull);
        dev->GetRenderState(D3DRS_ZENABLE, &rsZEnable);
        dev->GetRenderState(D3DRS_ZWRITEENABLE, &rsZWrite);
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &rsAlphaBlend);
        dev->GetRenderState(D3DRS_SRCBLEND, &rsSrcBlend);
        dev->GetRenderState(D3DRS_DESTBLEND, &rsDstBlend);
        dev->GetRenderState(D3DRS_ALPHATESTENABLE, &rsAlphaTest);
        dev->GetRenderState(D3DRS_FOGENABLE, &rsFog);
        dev->GetRenderState(D3DRS_SPECULARENABLE, &rsSpecular);
        dev->GetRenderState(D3DRS_COLORVERTEX, &rsColorVert);
        dev->GetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, &rsDiffuseSrc);
        dev->GetRenderState(D3DRS_SHADEMODE, &rsShadeMode);
        dev->GetTextureStageState(0, D3DTSS_COLOROP, &tss0ColorOp);
        dev->GetTextureStageState(0, D3DTSS_COLORARG1, &tss0ColorArg1);
        dev->GetTextureStageState(0, D3DTSS_ALPHAOP, &tss0AlphaOp);
        dev->GetTextureStageState(0, D3DTSS_ALPHAARG1, &tss0AlphaArg1);
        dev->GetTextureStageState(1, D3DTSS_COLOROP, &tss1ColorOp);
        dev->GetTextureStageState(1, D3DTSS_ALPHAOP, &tss1AlphaOp);
        dev->GetFVF(&savedFvf);
        dev->GetTexture(0, &savedTex0);
        dev->GetVertexShader(&savedVs);
        dev->GetPixelShader(&savedPs);
        valid = true;

        D3DMATRIX ident{};
        ident._11 = ident._22 = ident._33 = ident._44 = 1.0f;
        dev->SetTransform(D3DTS_WORLD, &ident);
        dev->SetVertexShader(nullptr);
        dev->SetPixelShader(nullptr);
        dev->SetTexture(0, nullptr);
        dev->SetFVF(kBumperFlashFvf);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_ZENABLE, TRUE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
        dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
        dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
        dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    }

    ~ScopedBumperFlashD3DState() {
        if (!valid || dev == nullptr) return;
        dev->SetTransform(D3DTS_WORLD, &savedWorld);
        dev->SetRenderState(D3DRS_LIGHTING, rsLighting);
        dev->SetRenderState(D3DRS_CULLMODE, rsCull);
        dev->SetRenderState(D3DRS_ZENABLE, rsZEnable);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, rsZWrite);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, rsAlphaBlend);
        dev->SetRenderState(D3DRS_SRCBLEND, rsSrcBlend);
        dev->SetRenderState(D3DRS_DESTBLEND, rsDstBlend);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, rsAlphaTest);
        dev->SetRenderState(D3DRS_FOGENABLE, rsFog);
        dev->SetRenderState(D3DRS_SPECULARENABLE, rsSpecular);
        dev->SetRenderState(D3DRS_COLORVERTEX, rsColorVert);
        dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, rsDiffuseSrc);
        dev->SetRenderState(D3DRS_SHADEMODE, rsShadeMode);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, tss0ColorOp);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, tss0ColorArg1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, tss0AlphaOp);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, tss0AlphaArg1);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, tss1ColorOp);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, tss1AlphaOp);
        dev->SetFVF(savedFvf);
        dev->SetTexture(0, savedTex0);
        if (savedTex0 != nullptr) savedTex0->Release();
        dev->SetVertexShader(savedVs);
        if (savedVs != nullptr) savedVs->Release();
        dev->SetPixelShader(savedPs);
        if (savedPs != nullptr) savedPs->Release();

        uint32_t* tcache = *reinterpret_cast<uint32_t**>(0x5f87cc);
        if (tcache != nullptr) *tcache = 0;
    }
};

struct BumperRoofFlashState {
    bool active = false;
    float elapsed = 0.0f;
    uint32_t lastTriggerFrame = 0xffffffffu;
};

static const uint8_t*                  g_bumperFlashLevel = nullptr;
static uint32_t                        g_bumperFlashLastWsFrame = 0;
static std::vector<BumperRoofFlashState> g_bumperFlashStates;

constexpr float kBumperFlashAttackSec = 0.033f;
constexpr float kBumperFlashDecaySec  = 0.167f;
constexpr float kBumperFlashTotalSec  = kBumperFlashAttackSec + kBumperFlashDecaySec;
constexpr float kBumperFlashMaxAlpha  = 138.0f; // 255 - 0x75, matching 0xff757575 peak darkening

static uint8_t* GetBumperRoofFlashNode(const uint8_t* roofMesh) {
    if (roofMesh == nullptr) return nullptr;
    const uint8_t* root = *reinterpret_cast<const uint8_t* const*>(roofMesh + 0x00);
    if (root == nullptr) return nullptr;
    const uint8_t* staticRoof = *reinterpret_cast<const uint8_t* const*>(root + 0xec);
    if (staticRoof == nullptr) return nullptr;
    return *reinterpret_cast<uint8_t* const*>(staticRoof + 0xf4);
}

static float EvalBumperFlashIntensity(float elapsed) {
    if (elapsed <= 0.0f || elapsed >= kBumperFlashTotalSec) return 0.0f;
    if (elapsed < kBumperFlashAttackSec) {
        float u = elapsed / kBumperFlashAttackSec;
        return u * u * (3.0f - 2.0f * u);
    }
    float inv = (kBumperFlashTotalSec - elapsed) / kBumperFlashDecaySec;
    return inv * inv * (3.0f - 2.0f * inv);
}

static void RenderSmoothBumperRoofFlash(const uint8_t* bumper) {
    uint8_t* lvl      = GetLevel();
    uint8_t* ws       = GetWorldScreen();
    uint8_t* sm       = GetScreenMgr();
    uint8_t* renderer = GetRenderer();
    if (bumper == nullptr || lvl == nullptr || ws == nullptr || renderer == nullptr) return;

    const uint8_t* roofMesh = *reinterpret_cast<const uint8_t* const*>(bumper + 0x74);
    if (roofMesh == nullptr) return;

    int16_t bumperId = *reinterpret_cast<const int16_t*>(bumper + 0x00);
    const uint8_t* interpState = *reinterpret_cast<const uint8_t* const*>(lvl + 0xb0);
    if (interpState == nullptr) return;

    int32_t count = *reinterpret_cast<const int32_t*>(interpState + 0x04);
    const int32_t* cmds = *reinterpret_cast<const int32_t* const*>(interpState + 0x08);
    if (count <= 0 || cmds == nullptr || bumperId < 0 || bumperId >= count) return;

    uint32_t wsFrame = *reinterpret_cast<const uint32_t*>(ws + 0x04);
    if (lvl != g_bumperFlashLevel || g_bumperFlashStates.size() != static_cast<size_t>(count) ||
        wsFrame < g_bumperFlashLastWsFrame) {
        g_bumperFlashStates.assign(static_cast<size_t>(count), BumperRoofFlashState{});
    }
    g_bumperFlashLevel = lvl;
    g_bumperFlashLastWsFrame = wsFrame;

    int32_t roofCmd = cmds[bumperId * 5 + 4];
    bool paused = (sm != nullptr && *reinterpret_cast<const uint32_t*>(sm + 0x50) == 0x12u);
    float dt = paused ? 0.0f : std::clamp(*reinterpret_cast<const float*>(renderer + 0x08), 0.0f, 0.1f);

    BumperRoofFlashState& st = g_bumperFlashStates[static_cast<size_t>(bumperId)];
    if (roofCmd == 1) {
        st.active = false;
        st.elapsed = 0.0f;
    } else if (roofCmd == 0 && st.lastTriggerFrame != wsFrame) {
        float curI = st.active ? EvalBumperFlashIntensity(st.elapsed) : 0.0f;
        st.active = true;
        st.elapsed = kBumperFlashAttackSec * std::clamp(curI, 0.0f, 1.0f);
        st.lastTriggerFrame = wsFrame;
    }

    if (st.active) {
        st.elapsed += dt;
        if (st.elapsed >= kBumperFlashTotalSec) {
            st.active = false;
            st.elapsed = 0.0f;
        }
    }
    if (!st.active) return;

    float intensity = EvalBumperFlashIntensity(st.elapsed);
    int alpha = std::clamp(static_cast<int>(std::lround(intensity * kBumperFlashMaxAlpha)), 0, 255);
    if (alpha <= 0) return;

    auto* dev = *reinterpret_cast<IDirect3DDevice9* const*>(renderer + 0xcc);
    if (dev == nullptr) return;

    float cx = *reinterpret_cast<const float*>(roofMesh + 0x30);
    float cy = *reinterpret_cast<const float*>(roofMesh + 0x34);
    float cz = *reinterpret_cast<const float*>(roofMesh + 0x38);
    float sx = *reinterpret_cast<const float*>(roofMesh + 0x48);
    float sy = *reinterpret_cast<const float*>(roofMesh + 0x4c);
    float sz = *reinterpret_cast<const float*>(roofMesh + 0x50);

    float halfX = 0.40f * sx;
    float halfZ = 0.40f * sz;
    float topY  = cy + 0.50f * sy + 0.05f;
    float x0 = cx - halfX, x1 = cx + halfX;
    float z0 = cz - halfZ, z1 = cz + halfZ;
    D3DCOLOR col = (static_cast<D3DCOLOR>(alpha) << 24);

    BumperFlashVert verts[6] = {
        {x0, topY, z0, col},
        {x1, topY, z0, col},
        {x1, topY, z1, col},
        {x0, topY, z0, col},
        {x1, topY, z1, col},
        {x0, topY, z1, col},
    };

    ScopedBumperFlashD3DState guard(dev);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, verts, sizeof(BumperFlashVert));
}

static void __fastcall Hook_BumperRender(uint8_t* bumper) {
    using OrigFn = void(__thiscall*)(void*);
    if (bumper == nullptr) {
        reinterpret_cast<OrigFn>(0x484fa0)(bumper);
        return;
    }

    const CustomThemeSettings eff = GetEffectiveTheme();
    uint8_t bumperStyle = eff.bumpers;
    uint8_t* roofMesh  = *reinterpret_cast<uint8_t**>(bumper + 0x74);
    uint8_t* flashNode = GetBumperRoofFlashNode(roofMesh);

    if (bumperStyle == 0u) {
        ScopedThemeLight lightGuard;
        if (eff.playerCube != 0u) {
            lightGuard.BeginRestorePc();
        }
        // Always flush tcache[0] even if styles match: Java/themed bumpers use distinct D3D9 textures
        // on the same engine Texture* (levelwall_<t+1>.img).
        OnColorThemeChanged();
        g_wallDrawTarget = WALL_TARGET_BUMPER;
        uint8_t savedVis = 0;
        if (flashNode != nullptr) {
            savedVis = flashNode[0x1a1];
            flashNode[0x1a1] = 0;
        }
        reinterpret_cast<OrigFn>(0x484fa0)(bumper);
        if (flashNode != nullptr) {
            flashNode[0x1a1] = savedVis;
        }
        g_wallDrawTarget = WALL_TARGET_TERRAIN;
        OnColorThemeChanged();
        RenderSmoothBumperRoofFlash(bumper);
        return;
    }

    using SetLightingFn = void(__thiscall*)(void*, uint8_t);
    using DrawMeshFn    = void(__thiscall*)(void*, int);

    uint8_t* renderer = GetRenderer();
    OnColorThemeChanged();
    g_wallDrawTarget = WALL_TARGET_BUMPER;

    int param = *reinterpret_cast<const int*>(bumper + 0x10);
    for (int i = 0; i < 4; ++i) {
        uint8_t* sideMesh = *reinterpret_cast<uint8_t**>(bumper + 0x54 + i * 8);
        if (sideMesh != nullptr) {
            bool triggered = (*reinterpret_cast<const int*>(sideMesh + 0x1c) != 5)
                          && (*reinterpret_cast<const int*>(sideMesh + 0x08) != 0);
            ScopedThemeLight sideLight;
            if (triggered) {
                sideLight.BeginWorldStyle(bumperStyle);
            }
            if (renderer != nullptr) {
                reinterpret_cast<SetLightingFn>(0x405e40)(renderer, triggered ? 1 : 0);
            }
            Hook_SetHeightUV(sideMesh, param);
            reinterpret_cast<DrawMeshFn>(0x421460)(sideMesh, 0);
        }
    }

    if (roofMesh != nullptr) {
        if (renderer != nullptr) {
            reinterpret_cast<SetLightingFn>(0x405e40)(renderer, 0);
        }
        Hook_SetHeightUV(roofMesh, param);
        uint8_t savedVis = 0;
        if (flashNode != nullptr) {
            savedVis = flashNode[0x1a1];
            flashNode[0x1a1] = 0;
        }
        reinterpret_cast<DrawMeshFn>(0x421460)(roofMesh, 0);
        if (flashNode != nullptr) {
            flashNode[0x1a1] = savedVis;
        }
    }

    g_wallDrawTarget = WALL_TARGET_TERRAIN;
    OnColorThemeChanged();

    if (renderer != nullptr) {
        reinterpret_cast<SetLightingFn>(0x405e40)(renderer, 1);
    }

    RenderSmoothBumperRoofFlash(bumper);
}

struct CachedPlatEdgeSubMesh {
    IDirect3DVertexBuffer9* vb = nullptr;
    uint32_t numVerts = 0;
    uint32_t stride = 0;
    uint32_t uvOff = 0;
    bool appliedCustom = false;
    std::vector<uint8_t> faceKind; // 0=top, 1=left (+Z), 2=right (+X)
    std::vector<float> origU;
    std::vector<float> origV;
};

static IDirect3DDevice9* s_platEdgeCacheDev = nullptr;
static bool s_anyPlatEdgeVbModified = false;
static std::vector<CachedPlatEdgeSubMesh> s_platEdgeSubMeshes;

static void ReadPlatEdgeSubMeshData(CachedPlatEdgeSubMesh& info, const uint8_t* basePtr) {
    info.faceKind.resize(info.numVerts, 0);
    info.origU.resize(info.numVerts);
    info.origV.resize(info.numVerts);
    for (uint32_t i = 0; i < info.numVerts; ++i) {
        const uint8_t* vptr = basePtr + i * info.stride;
        info.origU[i] = *reinterpret_cast<const float*>(vptr + info.uvOff);
        info.origV[i] = *reinterpret_cast<const float*>(vptr + info.uvOff + 4u);
    }
    for (uint32_t t = 0; t + 2 < info.numVerts; t += 3) {
        bool allTop = true;
        bool allLeft = true;
        bool allRight = true;
        for (uint32_t k = 0; k < 3; ++k) {
            const uint8_t* vptr = basePtr + (t + k) * info.stride;
            float vx = *reinterpret_cast<const float*>(vptr + 0u);
            float vy = *reinterpret_cast<const float*>(vptr + 4u);
            float vz = *reinterpret_cast<const float*>(vptr + 8u);
            if (std::fabs(vy - 0.5f) >= 1e-3f) allTop = false;
            if (std::fabs(vz - 0.5f) >= 1e-3f) allLeft = false;
            if (std::fabs(vx - 0.5f) >= 1e-3f) allRight = false;
        }
        uint8_t kind = allTop ? 0u : (allLeft ? 1u : (allRight ? 2u : 0u));
        info.faceKind[t]     = kind;
        info.faceKind[t + 1] = kind;
        info.faceKind[t + 2] = kind;
    }
}

static void SyncMovingPlatEdgeUVs(uint8_t* nodePtr, bool customTheme) {
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
                        if (h1 == kTexturesFolderHash && h0 == kRainbowCubeHash) {
                            auto* vb = *reinterpret_cast<IDirect3DVertexBuffer9**>(sm + 0xa0);
                            uint32_t flags  = *reinterpret_cast<const uint32_t*>(sm + 0x80);
                            uint32_t stride = *reinterpret_cast<const uint32_t*>(sm + 0x84);
                            const uint8_t* p0 = *reinterpret_cast<const uint8_t* const*>(sm + 0x10);
                            const uint8_t* p1 = *reinterpret_cast<const uint8_t* const*>(sm + 0x14);
                            uint32_t numVerts = (p1 > p0) ? static_cast<uint32_t>((p1 - p0) / 12) : 0;
                            uint32_t uvOff = 12u + ((flags & 1u) ? 12u : 0u) + ((flags & 2u) ? 4u : 0u);
                            if (vb != nullptr && numVerts >= 3 && (numVerts % 3u) == 0 && (flags & 4u) != 0 && uvOff + 8u <= stride) {
                                CachedPlatEdgeSubMesh* entry = nullptr;
                                for (auto& cached : s_platEdgeSubMeshes) {
                                    if (cached.vb == vb && cached.numVerts == numVerts) {
                                        entry = &cached;
                                        break;
                                    }
                                }
                                if (entry == nullptr) {
                                    void* ptr = nullptr;
                                    if (SUCCEEDED(vb->Lock(0, 0, &ptr, D3DLOCK_READONLY)) && ptr != nullptr) {
                                        CachedPlatEdgeSubMesh info;
                                        info.vb = vb;
                                        info.numVerts = numVerts;
                                        info.stride = stride;
                                        info.uvOff = uvOff;
                                        ReadPlatEdgeSubMeshData(info, static_cast<const uint8_t*>(ptr));
                                        if (info.origU[0] < 0.005f) {
                                            info.appliedCustom = true;
                                            s_anyPlatEdgeVbModified = true;
                                        }
                                        vb->Unlock();
                                        s_platEdgeSubMeshes.push_back(std::move(info));
                                        entry = &s_platEdgeSubMeshes.back();
                                    }
                                }
                                if (entry != nullptr && entry->appliedCustom != customTheme) {
                                    void* ptr = nullptr;
                                    if (SUCCEEDED(entry->vb->Lock(0, 0, &ptr, 0)) && ptr != nullptr) {
                                        uint8_t* basePtr = static_cast<uint8_t*>(ptr);
                                        for (uint32_t i = 0; i < entry->numVerts; ++i) {
                                            float outU = entry->origU[i];
                                            float outV = entry->origV[i];
                                            if (customTheme) {
                                                uint8_t kind = entry->faceKind[i];
                                                outU = 0.5f / 256.0f;
                                                outV = (kind == 0u) ? 0.16f : ((kind == 1u) ? 0.50f : 0.84f);
                                            }
                                            *reinterpret_cast<float*>(basePtr + i * entry->stride + entry->uvOff)      = outU;
                                            *reinterpret_cast<float*>(basePtr + i * entry->stride + entry->uvOff + 4u) = outV;
                                        }
                                        entry->vb->Unlock();
                                        entry->appliedCustom = customTheme;
                                        if (customTheme) {
                                            s_anyPlatEdgeVbModified = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                sm += 0xa8;
            }
        }
        SyncMovingPlatEdgeUVs(node + 0xec, customTheme);
        node = *reinterpret_cast<uint8_t**>(node + 0xf4);
    }
}

static IDirect3DDevice9* GetD3DDevice() {
    uint8_t* renderer = GetRenderer();
    return (renderer != nullptr) ? *reinterpret_cast<IDirect3DDevice9**>(renderer + 0xcc) : nullptr;
}

constexpr float   kPlatformDepthBiasStep    = 1.5e-6f;
constexpr float   kFallingPlatformDepthBias = 0.75e-6f;
constexpr uint8_t kMaxOverlapLayer          = 6u;

struct ScopedDepthBias {
    IDirect3DDevice9* dev = nullptr;
    DWORD savedBias = 0;
    bool active = false;

    ScopedDepthBias(IDirect3DDevice9* d, float bias) : dev(d) {
        if (dev != nullptr && bias != 0.0f) {
            dev->GetRenderState(D3DRS_DEPTHBIAS, &savedBias);
            DWORD dwBias = 0;
            std::memcpy(&dwBias, &bias, sizeof(dwBias));
            dev->SetRenderState(D3DRS_DEPTHBIAS, dwBias);
            active = true;
        }
    }

    ~ScopedDepthBias() {
        if (active && dev != nullptr) {
            dev->SetRenderState(D3DRS_DEPTHBIAS, savedBias);
        }
    }
};

struct MovingPlatDepthPlan {
    bool skip = false;
    uint8_t layer = 0;
};

struct MovingPlatEntry {
    uint16_t index = 0;
    uint8_t valid = 0;
    uint8_t state = 0;
    int32_t sizeKind = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    float hx = 0.0f, hy = 0.0f, hz = 0.0f;
};

static std::vector<MovingPlatDepthPlan> s_mpDepthPlan;
static std::vector<MovingPlatEntry>     s_mpEntries;
static std::vector<uint16_t>            s_mpOrder;
static std::vector<uint16_t>            s_mpKept;
static uint8_t*                         s_mpPlanLevel = nullptr;
static int                              s_mpRenderCursor = 0;

static void PrepareMovingPlatformDepthPlan(uint8_t* lvl) {
    int16_t count = *reinterpret_cast<const int16_t*>(lvl + 0x04);
    uint8_t** arr = *reinterpret_cast<uint8_t***>(lvl + 0x08);
    if (count <= 0 || arr == nullptr) {
        s_mpDepthPlan.clear();
        s_mpPlanLevel = lvl;
        s_mpRenderCursor = 0;
        return;
    }

    const size_t n = static_cast<size_t>(count);
    s_mpDepthPlan.assign(n, MovingPlatDepthPlan{});
    s_mpEntries.resize(n);
    s_mpOrder.resize(n);
    s_mpKept.clear();
    if (s_mpKept.capacity() < n) {
        s_mpKept.reserve(n);
    }

    for (int i = 0; i < count; ++i) {
        const size_t idx = static_cast<size_t>(i);
        s_mpOrder[idx] = static_cast<uint16_t>(i);
        MovingPlatEntry& e = s_mpEntries[idx];
        e = MovingPlatEntry{};
        e.index = static_cast<uint16_t>(i);

        uint8_t* plat = arr[i];
        if (plat == nullptr) continue;
        int32_t st = *reinterpret_cast<const int32_t*>(plat + 0x6c);
        if (st != 0 && st != 1) continue;
        const uint8_t* bodyModel = *reinterpret_cast<const uint8_t* const*>(plat + st * 8 + 0x50);
        if (bodyModel == nullptr) continue;

        e.valid = 1;
        e.state = static_cast<uint8_t>(st);
        e.sizeKind = *reinterpret_cast<const int32_t*>(plat + 0x44);
        e.x = *reinterpret_cast<const float*>(bodyModel + 0x30);
        e.y = *reinterpret_cast<const float*>(bodyModel + 0x34);
        e.z = *reinterpret_cast<const float*>(bodyModel + 0x38);
        e.sx = *reinterpret_cast<const float*>(bodyModel + 0x48);
        e.sy = *reinterpret_cast<const float*>(bodyModel + 0x4c);
        e.sz = *reinterpret_cast<const float*>(bodyModel + 0x50);
        e.hx = std::fabs(e.sx) * 0.5f;
        e.hy = std::fabs(e.sy) * 0.5f;
        e.hz = std::fabs(e.sz) * 0.5f;
    }

    // Active platforms (rainbow edges) take visual precedence over inactive ones, then earlier index.
    std::sort(s_mpOrder.begin(), s_mpOrder.end(), [](uint16_t a, uint16_t b) {
        const uint8_t sa = s_mpEntries[a].state;
        const uint8_t sb = s_mpEntries[b].state;
        if (sa != sb) return sa > sb;
        return a < b;
    });

    for (uint16_t idx : s_mpOrder) {
        const MovingPlatEntry& a = s_mpEntries[idx];
        if (!a.valid) continue;

        bool skip = false;
        uint32_t usedMask = 0;
        for (uint16_t prev : s_mpKept) {
            const MovingPlatEntry& b = s_mpEntries[prev];
            float dx = std::fabs(a.x - b.x);
            if (dx >= (a.hx + b.hx) - 0.05f) continue;
            float dy = std::fabs(a.y - b.y);
            if (dy >= (a.hy + b.hy) - 0.05f) continue;
            float dz = std::fabs(a.z - b.z);
            if (dz >= (a.hz + b.hz) - 0.05f) continue;

            if (dx < 0.05f && dy < 0.05f && dz < 0.05f
                && a.sizeKind == b.sizeKind
                && std::fabs(a.sx - b.sx) < 0.05f
                && std::fabs(a.sy - b.sy) < 0.05f
                && std::fabs(a.sz - b.sz) < 0.05f) {
                skip = true;
                break;
            }
            usedMask |= (1u << s_mpDepthPlan[prev].layer);
        }

        if (skip) {
            s_mpDepthPlan[idx].skip = true;
            continue;
        }

        uint8_t layer = 0;
        while (layer < kMaxOverlapLayer && (usedMask & (1u << layer)) != 0u) {
            ++layer;
        }
        s_mpDepthPlan[idx].layer = layer;
        s_mpKept.push_back(idx);
    }

    s_mpPlanLevel = lvl;
    s_mpRenderCursor = 0;
}

static MovingPlatDepthPlan ResolveMovingPlatformDepthPlan(uint8_t* plat) {
    uint8_t* lvl = GetLevel();
    if (lvl == nullptr || plat == nullptr) return {};
    int16_t count = *reinterpret_cast<const int16_t*>(lvl + 0x04);
    uint8_t** arr = *reinterpret_cast<uint8_t***>(lvl + 0x08);
    if (count <= 0 || arr == nullptr) return {};

    if (plat == arr[0] || lvl != s_mpPlanLevel || static_cast<int>(s_mpDepthPlan.size()) != count) {
        PrepareMovingPlatformDepthPlan(lvl);
    }

    int idx = -1;
    if (s_mpRenderCursor >= 0 && s_mpRenderCursor < count && arr[s_mpRenderCursor] == plat) {
        idx = s_mpRenderCursor;
        ++s_mpRenderCursor;
    } else {
        for (int i = 0; i < count; ++i) {
            if (arr[i] == plat) {
                idx = i;
                s_mpRenderCursor = i + 1;
                break;
            }
        }
    }
    if (idx < 0 || static_cast<size_t>(idx) >= s_mpDepthPlan.size()) return {};
    return s_mpDepthPlan[static_cast<size_t>(idx)];
}

static void __fastcall Hook_MovingPlatformRender(uint8_t* plat) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x483fd0);
    if (plat == nullptr) {
        orig(plat);
        return;
    }

    IDirect3DDevice9* curDev = g_cachedDev;
    if (curDev != s_platEdgeCacheDev) {
        s_platEdgeSubMeshes.clear();
        s_platEdgeCacheDev = curDev;
        s_anyPlatEdgeVbModified = false;
    }

    const CustomThemeSettings eff = GetEffectiveTheme();
    uint8_t bodyStyle = eff.movingPlatforms;
    uint8_t borderStyle = eff.platformBorders;

    MovingPlatDepthPlan plan = ResolveMovingPlatformDepthPlan(plat);
    if (plan.skip) {
        if (borderStyle == 0u && s_anyPlatEdgeVbModified) {
            uint8_t* edgeModel = *reinterpret_cast<uint8_t**>(plat + 0x60);
            if (edgeModel != nullptr) {
                SyncMovingPlatEdgeUVs(edgeModel, false);
            }
        }
        return;
    }

    float bias = static_cast<float>(1u + plan.layer) * kPlatformDepthBiasStep;
    ScopedDepthBias depthGuard(GetD3DDevice(), bias);

    if (bodyStyle == 0u && borderStyle == 0u && eff.terrain == 0u && eff.wallGradient == 0u) {
        if (s_anyPlatEdgeVbModified) {
            uint8_t* edgeModel = *reinterpret_cast<uint8_t**>(plat + 0x60);
            if (edgeModel != nullptr) {
                SyncMovingPlatEdgeUVs(edgeModel, false);
            }
        }
        orig(plat);
        return;
    }

    using DrawMeshFn   = void(__thiscall*)(void*, int);
    using SetUvMatFn   = void(__cdecl*)(void*, const float*);

    int32_t state = *reinterpret_cast<const int32_t*>(plat + 0x6c);
    if (state >= 0 && state <= 1) {
        uint8_t* bodyModel = *reinterpret_cast<uint8_t**>(plat + state * 8 + 0x50);
        if (bodyModel != nullptr) {
            double yVal = static_cast<double>(*reinterpret_cast<const float*>(bodyModel + 0x34))
                        - *reinterpret_cast<const double*>(0x585020);
            int yInt = static_cast<int>(yVal);
            OnColorThemeChanged();
            g_wallDrawTarget = WALL_TARGET_MOVING_PLAT;
            Hook_SetHeightUV(bodyModel, yInt);
            reinterpret_cast<DrawMeshFn>(0x421460)(bodyModel, 0);
            g_wallDrawTarget = WALL_TARGET_TERRAIN;
            OnColorThemeChanged();
        }
    }

    uint8_t* edgeModel = *reinterpret_cast<uint8_t**>(plat + 0x60);
    if (state == 1 && edgeModel != nullptr) {
        int32_t animTick = *reinterpret_cast<const int32_t*>(plat + 0x68);
        if (borderStyle == 0u) {
            if (s_anyPlatEdgeVbModified) {
                SyncMovingPlatEdgeUVs(edgeModel, false);
            }
            int32_t visTick = animTick + static_cast<int32_t>(std::floor(GetPauseExtraTicks()));
            reinterpret_cast<void(__cdecl*)(void*, int32_t, int)>(0x495c50)(edgeModel, visTick, 0xd);
            reinterpret_cast<DrawMeshFn>(0x421460)(edgeModel, 0);
        } else {
            SyncMovingPlatEdgeUVs(edgeModel, true);

            float visTick = GetVisualEntityTick(GetWorldScreen(), animTick);
            float ticksPerColor = (borderStyle == 2u) ? 2.7f : 1.0f;
            float phase = std::fmod(visTick / (9.0f * ticksPerColor), 1.0f);
            if (phase < 0.0f) phase += 1.0f;

            float mat[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                phase, 0.0f, 1.0f, 0.0f,
                phase, 0.0f, 0.0f, 1.0f
            };
            reinterpret_cast<SetUvMatFn>(0x495a70)(edgeModel, mat);

            OnColorThemeChanged();
            g_inMovingPlatEdgeRender = true;
            reinterpret_cast<DrawMeshFn>(0x421460)(edgeModel, 0);
            g_inMovingPlatEdgeRender = false;
            OnColorThemeChanged();
        }
    }
}

static bool IsSwitchTexHash(uint32_t hash0) {
    return hash0 == 0x301C8879u || hash0 == 0xB2446CF3u;
}

struct CachedSwitchSubMesh {
    IDirect3DVertexBuffer9* vb = nullptr;
    uint32_t numVerts = 0;
    uint32_t stride = 0;
    uint32_t colorOff = 0;
    uint8_t appliedTheme = 0;
    bool hasSideVerts = false;
    std::vector<uint8_t> faceKind; // 0=top/other, 1=left (+Z), 2=right (+X)
    std::vector<uint32_t> origColors;
};

static IDirect3DDevice9* s_switchCacheDev = nullptr;
static bool s_anySwitchVbModified = false;
static std::vector<CachedSwitchSubMesh> s_switchSubMeshes;

static void SyncSwitchModelVertexColors(uint8_t* nodePtr, uint8_t targetTheme) {
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
                        if (h1 == kTexturesFolderHash && IsSwitchTexHash(h0)) {
                            auto* vb = *reinterpret_cast<IDirect3DVertexBuffer9**>(sm + 0xa0);
                            uint32_t flags  = *reinterpret_cast<const uint32_t*>(sm + 0x80);
                            uint32_t stride = *reinterpret_cast<const uint32_t*>(sm + 0x84);
                            const uint8_t* p0 = *reinterpret_cast<const uint8_t* const*>(sm + 0x10);
                            const uint8_t* p1 = *reinterpret_cast<const uint8_t* const*>(sm + 0x14);
                            uint32_t numVerts = (p1 > p0) ? static_cast<uint32_t>((p1 - p0) / 12) : 0;
                            constexpr uint32_t colorOff = 24u;
                            if (vb != nullptr && numVerts > 0 && (flags & 3u) == 3u && stride >= 28u) {
                                CachedSwitchSubMesh* entry = nullptr;
                                for (auto& cached : s_switchSubMeshes) {
                                    if (cached.vb == vb && cached.numVerts == numVerts) {
                                        entry = &cached;
                                        break;
                                    }
                                }
                                if (entry == nullptr) {
                                    void* ptr = nullptr;
                                    if (SUCCEEDED(vb->Lock(0, 0, &ptr, D3DLOCK_READONLY)) && ptr != nullptr) {
                                        CachedSwitchSubMesh info;
                                        info.vb = vb;
                                        info.numVerts = numVerts;
                                        info.stride = stride;
                                        info.colorOff = colorOff;
                                        info.faceKind.resize(numVerts);
                                        info.origColors.resize(numVerts);
                                        uint8_t initialApplied = 0;
                                        const uint8_t* basePtr = static_cast<const uint8_t*>(ptr);
                                        for (uint32_t i = 0; i < numVerts; ++i) {
                                            const uint8_t* vptr = basePtr + i * stride;
                                            float nx = *reinterpret_cast<const float*>(vptr + 12u);
                                            float nz = *reinterpret_cast<const float*>(vptr + 20u);
                                            uint32_t col = *reinterpret_cast<const uint32_t*>(vptr + colorOff);
                                            uint8_t kind = 0;
                                            if (nz > 0.5f) {
                                                kind = 1;
                                            } else if (nx > 0.5f) {
                                                kind = 2;
                                            }
                                            info.faceKind[i] = kind;
                                            if (kind == 1 || kind == 2) {
                                                info.hasSideVerts = true;
                                            }
                                            if (kind == 1 && col == kAndroidSwitchLeftVCol) {
                                                info.origColors[i] = kPcSwitchLeftVCol;
                                                initialApplied = 1u;
                                            } else if (kind == 2 && col == kAndroidSwitchRightVCol) {
                                                info.origColors[i] = kPcSwitchRightVCol;
                                                initialApplied = 1u;
                                            } else if (kind == 1 && col == kJavaSwitchLeftVCol) {
                                                info.origColors[i] = kPcSwitchLeftVCol;
                                                initialApplied = 2u;
                                            } else if (kind == 2 && col == kJavaSwitchRightVCol) {
                                                info.origColors[i] = kPcSwitchRightVCol;
                                                initialApplied = 2u;
                                            } else {
                                                info.origColors[i] = col;
                                            }
                                        }
                                        vb->Unlock();
                                        info.appliedTheme = initialApplied;
                                        if (initialApplied != 0u) {
                                            s_anySwitchVbModified = true;
                                        }
                                        s_switchSubMeshes.push_back(std::move(info));
                                        entry = &s_switchSubMeshes.back();
                                    }
                                }
                                if (entry != nullptr && entry->hasSideVerts && entry->appliedTheme != targetTheme) {
                                    void* ptr = nullptr;
                                    if (SUCCEEDED(entry->vb->Lock(0, 0, &ptr, 0)) && ptr != nullptr) {
                                        uint8_t* basePtr = static_cast<uint8_t*>(ptr);
                                        for (uint32_t i = 0; i < entry->numVerts; ++i) {
                                            uint8_t kind = entry->faceKind[i];
                                            uint32_t outCol = entry->origColors[i];
                                            if (targetTheme == 1u) {
                                                if (kind == 1) outCol = kAndroidSwitchLeftVCol;
                                                else if (kind == 2) outCol = kAndroidSwitchRightVCol;
                                            } else if (targetTheme == 2u) {
                                                if (kind == 1) outCol = kJavaSwitchLeftVCol;
                                                else if (kind == 2) outCol = kJavaSwitchRightVCol;
                                            }
                                            *reinterpret_cast<uint32_t*>(basePtr + i * entry->stride + entry->colorOff) = outCol;
                                        }
                                        entry->vb->Unlock();
                                        entry->appliedTheme = targetTheme;
                                        if (targetTheme != 0u) {
                                            s_anySwitchVbModified = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                sm += 0xa8;
            }
        }
        SyncSwitchModelVertexColors(node + 0xec, targetTheme);
        node = *reinterpret_cast<uint8_t**>(node + 0xf4);
    }
}

static void __fastcall Hook_SwitchRender(uint8_t* sw) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x485c30);
    if (sw == nullptr) {
        orig(sw);
        return;
    }

    IDirect3DDevice9* curDev = g_cachedDev;
    if (curDev != s_switchCacheDev) {
        s_switchSubMeshes.clear();
        s_switchCacheDev = curDev;
        s_anySwitchVbModified = false;
    }

    uint8_t targetTheme = GetEffectiveTheme().switches;
    if (targetTheme == 0u && !s_anySwitchVbModified) {
        orig(sw);
        return;
    }

    uint8_t* model = *reinterpret_cast<uint8_t**>(sw + 0x30);
    if (model != nullptr) {
        SyncSwitchModelVertexColors(model, targetTheme);
    }
    orig(sw);
}

struct SavedHoloTexMatAnim {
    uint8_t* entry;
    void*    savedAnim;
};

static void ApplyHoloSwitchSmoothUV(
    uint8_t* nodePtr, float phase,
    SavedHoloTexMatAnim* saved, int& savedCount, int maxSaved
) {
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
                        if (h1 == kTexturesFolderHash && h0 == kRainbowCubeHash) {
                            uint8_t* tm = *reinterpret_cast<uint8_t**>(mat + 0x2c);
                            uint8_t* tmEnd = *reinterpret_cast<uint8_t**>(mat + 0x30);
                            while (tm != nullptr && tm < tmEnd) {
                                if (savedCount < maxSaved) {
                                    saved[savedCount++] = {
                                        tm,
                                        *reinterpret_cast<void**>(tm + 0x40)
                                    };
                                }
                                *reinterpret_cast<void**>(tm + 0x40) = nullptr;
                                float uvMat[16] = {
                                    1.0f, 0.0f, 0.0f, 0.0f,
                                    0.0f, 1.0f, 0.0f, 0.0f,
                                    phase, 0.0f, 1.0f, 0.0f,
                                    phase, 0.0f, 0.0f, 1.0f
                                };
                                std::memcpy(tm, uvMat, sizeof(uvMat));
                                tm += 0x48;
                            }
                        }
                    }
                }
                sm += 0xa8;
            }
        }
        ApplyHoloSwitchSmoothUV(node + 0xec, phase, saved, savedCount, maxSaved);
        node = *reinterpret_cast<uint8_t**>(node + 0xf4);
    }
}

static void __fastcall Hook_HoloSwitchRender(uint8_t* holo) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x486150);
    if (holo == nullptr) {
        orig(holo);
        return;
    }

    uint8_t holoStyle = GetEffectiveTheme().hintTile;
    uint8_t* ws = *reinterpret_cast<uint8_t**>(0x5f9064);
    float smoothFrame = (ws != nullptr) ? GetVisualWsFrame(ws) : 0.0f;
    float phase = 0.0f;
    if (holoStyle == 0u) {
        // PC: 13 pastel colors over 65 ticks (5 ticks/color, matching holoswitch.ema)
        phase = std::fmod(smoothFrame / 65.0f, 1.0f);
        if (phase < 0.0f) phase += 1.0f;
    } else if (holoStyle == 1u) {
        // Android: max(0, ((tick / 2) % (8 + 2)) - 2) @ 30 Hz (20 ticks total: 4-tick #78a8e8 hold + 16-tick 8-color sweep)
        float step = std::fmod(smoothFrame / 2.0f, 10.0f);
        if (step < 0.0f) step += 10.0f;
        phase = (step > 2.0f) ? ((step - 2.0f) / 8.0f) : 0.0f;
    } else {
        // Java: max(0, ((as / 2) % (4 + 2)) - 2) @ 22 Hz (12 Java ticks = 16.36 PC ticks: 2-step #78a8e8 hold + 4-step 4-color sweep)
        constexpr float kJavaStepTicks = 2.0f * 30.0f / 22.0f;
        float step = std::fmod(smoothFrame / kJavaStepTicks, 6.0f);
        if (step < 0.0f) step += 6.0f;
        phase = (step > 2.0f) ? ((step - 2.0f) / 4.0f) : 0.0f;
    }
    // Compensate for holoswitch.rmdl vertex U midpoint (~0.0342) so phase=0 samples exact color[0]
    constexpr float kHoloSwitchBaseU = 0.0342f;
    phase = std::fmod(phase - kHoloSwitchBaseU, 1.0f);
    if (phase < 0.0f) phase += 1.0f;

    SavedHoloTexMatAnim saved[16]{};
    int savedCount = 0;
    uint8_t* model = *reinterpret_cast<uint8_t**>(holo + 0x28);
    if (model != nullptr) {
        ApplyHoloSwitchSmoothUV(model, phase, saved, savedCount, 16);
    }

    OnColorThemeChanged();
    g_inHoloSwitchRender = true;
    orig(holo);
    g_inHoloSwitchRender = false;
    OnColorThemeChanged();

    for (int i = 0; i < savedCount; ++i) {
        *reinterpret_cast<void**>(saved[i].entry + 0x40) = saved[i].savedAnim;
    }
}

static void EmitTopQuad(
    BumperFlashVert* out, float x0, float x1, float z0, float z1, float y, D3DCOLOR col
) {
    out[0] = {x0, y, z0, col};
    out[1] = {x1, y, z0, col};
    out[2] = {x1, y, z1, col};
    out[3] = {x0, y, z0, col};
    out[4] = {x1, y, z1, col};
    out[5] = {x0, y, z1, col};
}

static int EmitTopRing(
    BumperFlashVert* out, float cx, float cz, float y, float halfOuter, float halfInner, D3DCOLOR col
) {
    if (halfInner <= 0.001f) {
        EmitTopQuad(out, cx - halfOuter, cx + halfOuter, cz - halfOuter, cz + halfOuter, y, col);
        return 6;
    }
    float ox0 = cx - halfOuter, ox1 = cx + halfOuter;
    float oz0 = cz - halfOuter, oz1 = cz + halfOuter;
    float ix0 = cx - halfInner, ix1 = cx + halfInner;
    float iz0 = cz - halfInner, iz1 = cz + halfInner;
    EmitTopQuad(out + 0,  ox0, ox1, oz0, iz0, y, col); // top strip
    EmitTopQuad(out + 6,  ox0, ox1, iz1, oz1, y, col); // bottom strip
    EmitTopQuad(out + 12, ox0, ix0, iz0, iz1, y, col); // left strip
    EmitTopQuad(out + 18, ix1, ox1, iz0, iz1, y, col); // right strip
    return 24;
}

static void RenderMobileShrinker(
    IDirect3DDevice9* dev, const uint8_t* shrinker, const uint8_t* lvl, const uint8_t* ws
) {
    float cx   = static_cast<float>(*reinterpret_cast<const int32_t*>(shrinker + 0x04)) + 15.0f;
    float cz   = static_cast<float>(*reinterpret_cast<const int32_t*>(shrinker + 0x08)) + 15.0f;
    float topY = static_cast<float>(*reinterpret_cast<const int32_t*>(shrinker + 0x0c)) + 0.45f;
    bool isToBig = (*reinterpret_cast<const int32_t*>(shrinker + 0x14) != 0);

    float smoothFrame = (ws != nullptr) ? GetVisualWsFrame(ws) : 0.0f;
    float u = std::fmod(smoothFrame, 50.0f) / 50.0f;
    if (u < 0.0f) u += 1.0f;
    float raw = isToBig ? u : (1.0f - u);
    float s1 = (std::max)(raw * 1.1f, 0.3f);

    float alpha = 1.0f;
    float scale = s1;
    if (s1 > 1.0f) {
        alpha = std::clamp((1.1f - s1) / 0.1f, 0.0f, 1.0f);
        scale = 1.0f;
    } else if (s1 < 0.4f) {
        alpha = std::clamp((s1 - 0.3f) / 0.1f, 0.0f, 1.0f);
    }
    int alphaByte = std::clamp(static_cast<int>(std::lround(alpha * 255.0f)), 0, 255);
    if (alphaByte <= 0) return;

    uint8_t themeIdx = (lvl != nullptr) ? (lvl[0x6c] & 3u) : 0u;
    float cPhase = std::fmod(smoothFrame, 9.0f);
    if (cPhase < 0.0f) cPhase += 9.0f;
    int i0 = static_cast<int>(cPhase) % 9;
    int i1 = (i0 + 1) % 9;
    float frac = cPhase - std::floor(cPhase);
    Rgb8 rgb = LerpRgb8(kAndroidPlatEdgePalette[themeIdx][i0][1], kAndroidPlatEdgePalette[themeIdx][i1][1], frac);
    D3DCOLOR col = PackArgb(static_cast<uint8_t>(alphaByte), rgb);

    float halfOuter = scale * 15.0f;
    float thick = scale * 4.0f + 1.0f;
    float halfInner = (std::max)(0.0f, halfOuter - thick);

    BumperFlashVert verts[24];
    int vertCount = EmitTopRing(verts, cx, cz, topY, halfOuter, halfInner, col);
    ScopedBumperFlashD3DState guard(dev);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(vertCount / 3), verts, sizeof(BumperFlashVert));
}

static void RenderJavaShrinker(IDirect3DDevice9* dev, const uint8_t* shrinker, const uint8_t* ws) {
    float cx   = static_cast<float>(*reinterpret_cast<const int32_t*>(shrinker + 0x04)) + 15.0f;
    float cz   = static_cast<float>(*reinterpret_cast<const int32_t*>(shrinker + 0x08)) + 15.0f;
    float topY = static_cast<float>(*reinterpret_cast<const int32_t*>(shrinker + 0x0c)) + 0.45f;
    bool isToBig = (*reinterpret_cast<const int32_t*>(shrinker + 0x14) != 0);

    constexpr float kJavaFullHalf = 14.95f;
    constexpr float kJavaCoreHalf = kJavaFullHalf * (14.0f / 50.0f);
    static const Rgb8 kJavaShrinkerDark = {0x00, 0x1d, 0x54};
    static const Rgb8 kJavaShrinkerMid  = {0x37, 0x62, 0x8b};
    static const Rgb8 kJavaFlashOuter[4] = {
        {0x88, 0xaf, 0xbf}, {0x6d, 0x98, 0xaf}, {0x52, 0x81, 0x9f}, {0x1c, 0x42, 0x76}
    };
    static const Rgb8 kJavaFlashInner[4] = {
        {0x37, 0x62, 0x8b}, {0x29, 0x52, 0x80}, {0x1c, 0x42, 0x76}, {0x00, 0x22, 0x61}
    };
    static const float kJavaShrinkScales[5] = {
        42.0f / 50.0f, 36.0f / 50.0f, 30.0f / 50.0f, 22.0f / 50.0f, 14.0f / 50.0f
    };

    float smoothFrame = (ws != nullptr) ? GetVisualWsFrame(ws) : 0.0f;
    float t = std::fmod(smoothFrame * (22.0f / 30.0f), 22.0f);
    if (t < 0.0f) t += 22.0f;
    float tEff = isToBig ? (22.0f - t) : t;
    if (tEff >= 22.0f) tEff -= 22.0f;

    BumperFlashVert verts[30];
    int vertCount = 0;
    if (tEff < 4.0f) {
        EmitTopQuad(
            verts, cx - kJavaFullHalf, cx + kJavaFullHalf, cz - kJavaFullHalf, cz + kJavaFullHalf,
            topY, PackArgb(0xffu, kJavaShrinkerDark)
        );
        vertCount = 6;
    } else if (tEff < 14.0f) {
        float pos = ((tEff - 4.0f) / 10.0f) * 4.0f;
        int idx = std::clamp(static_cast<int>(pos), 0, 3);
        float frac = std::clamp(pos - static_cast<float>(idx), 0.0f, 1.0f);
        float sc = kJavaShrinkScales[idx] + (kJavaShrinkScales[idx + 1] - kJavaShrinkScales[idx]) * frac;
        float h = sc * kJavaFullHalf;
        EmitTopQuad(verts, cx - h, cx + h, cz - h, cz + h, topY, PackArgb(0xffu, kJavaShrinkerMid));
        vertCount = 6;
    } else {
        float pos = ((tEff - 14.0f) / 8.0f) * 3.0f;
        int idx = std::clamp(static_cast<int>(pos), 0, 2);
        float frac = std::clamp(pos - static_cast<float>(idx), 0.0f, 1.0f);
        D3DCOLOR cOuter = PackArgb(0xffu, LerpRgb8(kJavaFlashOuter[idx], kJavaFlashOuter[idx + 1], frac));
        D3DCOLOR cInner = PackArgb(0xffu, LerpRgb8(kJavaFlashInner[idx], kJavaFlashInner[idx + 1], frac));
        vertCount = EmitTopRing(verts, cx, cz, topY, kJavaFullHalf, kJavaCoreHalf, cOuter);
        EmitTopQuad(
            verts + vertCount, cx - kJavaCoreHalf, cx + kJavaCoreHalf, cz - kJavaCoreHalf, cz + kJavaCoreHalf,
            topY, cInner
        );
        vertCount += 6;
    }

    ScopedBumperFlashD3DState guard(dev);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(vertCount / 3), verts, sizeof(BumperFlashVert));
}

static void SyncShrinkerNodeTimer(const uint8_t* shrinker) {
    if (shrinker == nullptr) return;
    uint8_t* model = *reinterpret_cast<uint8_t* const*>(shrinker + 0x18);
    if (model == nullptr) return;
    uint8_t* rootNode = *reinterpret_cast<uint8_t* const*>(model + 0x00);
    if (rootNode == nullptr) return;
    rootNode[0x1ad] = 0; // clear Node::updateTimer updated-this-frame guard (0x42539d / 0x425721)
    uint8_t* nodeTimer = *reinterpret_cast<uint8_t* const*>(rootNode + 0xdc);
    if (nodeTimer != nullptr) {
        *reinterpret_cast<float*>(nodeTimer + 0x10) = *reinterpret_cast<const float*>(model + 0x20);
    }
}

static void __fastcall Hook_ShrinkerRender(uint8_t* shrinker) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x486f50);
    if (shrinker != nullptr) {
        SyncShrinkerNodeTimer(shrinker);
    }
    uint8_t shrinkerStyle = GetEffectiveTheme().shrinkers;
    if (shrinkerStyle == 0u || shrinker == nullptr) {
        orig(shrinker);
        return;
    }
    if (shrinker[0x10] == 0) return;

    const uint8_t* model = *reinterpret_cast<const uint8_t* const*>(shrinker + 0x18);
    if (model != nullptr && ((*reinterpret_cast<const uint32_t*>(model + 0x2c) & 0x800u) != 0u)) {
        return;
    }

    IDirect3DDevice9* dev = GetD3DDevice();
    if (dev == nullptr) {
        orig(shrinker);
        return;
    }

    uint8_t* ws = GetWorldScreen();
    if (shrinkerStyle == 1u) {
        RenderMobileShrinker(dev, shrinker, GetLevel(), ws);
    } else {
        RenderJavaShrinker(dev, shrinker, ws);
    }
}

static void __fastcall Hook_FallingPlatformRender(uint8_t* plat) {
    using OrigFn = void(__thiscall*)(void*);
    auto orig = reinterpret_cast<OrigFn>(0x486b20);
    if (plat == nullptr) {
        orig(plat);
        return;
    }

    ScopedDepthBias depthGuard(GetD3DDevice(), kFallingPlatformDepthBias);
    if (GetEffectiveTheme().fallingPlatforms != 1u) {
        orig(plat);
        return;
    }

    int hTiles = GetLevelHeightTiles();
    float H = (static_cast<float>(hTiles) - 0.5f) * 30.0f;
    if (H < 30.0f) H = 30.0f;
    int32_t y = *reinterpret_cast<const int32_t*>(plat + 0x0c);
    float t = std::clamp(1.0f - (static_cast<float>(y) + 13.0f) / H, 0.0f, 1.0f);
    int bucket = std::clamp(static_cast<int>(std::lround(t * 15.0f)), 0, 15);

    OnColorThemeChanged();
    g_mobileFallingBucket = bucket;
    orig(plat);
    g_mobileFallingBucket = -1;
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

static void BeginDarkCubeLight(ScopedThemeLight& guard, const CustomThemeSettings& eff, bool isSolid) {
    uint8_t style = isSolid ? eff.darkCube : eff.hintGhost;
    if (style == 0u) {
        if (eff.playerCube != 0u) {
            guard.BeginRestorePc();
        }
    } else if (style == 1u) {
        if (isSolid) {
            uint8_t themeIdx = 0;
            uint8_t* lvl = GetLevel();
            if (lvl != nullptr) themeIdx = lvl[0x6c] & 3u;
            if (themeIdx < 2u) {
                guard.BeginSingle({0.35895f, 0.35895f, 0.35895f, 1.0f}, {-0.24874f, -0.88303f, -0.39798f});
            } else {
                guard.BeginSingle({0.24639f, 0.24639f, 0.24639f, 1.0f}, {-0.19931f, -0.92407f, -0.32614f});
            }
        } else {
            guard.BeginWorldStyle(1u);
        }
    } else {
        if (isSolid) {
            guard.BeginWorldStyle(2u);
        } else {
            guard.BeginSingle({1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, -0.8819171f, -0.4714045f});
        }
    }
}

static void __fastcall Hook_DarkCubeRender(void* cube) {
    using OrigFn = void(__thiscall*)(void*);
    const CustomThemeSettings eff = GetEffectiveTheme();
    ScopedThemeLight darkLight;
    BeginDarkCubeLight(darkLight, eff, true);
    reinterpret_cast<OrigFn>(0x48c630)(cube);
}

struct ScopedPlayerCubeDistort {
    explicit ScopedPlayerCubeDistort(void* cube) { BeginPlayerCubeDistortion(cube); }
    ~ScopedPlayerCubeDistort() { EndCubeDistortion(); }
};

struct ScopedGhostCubeDistort {
    bool active = false;
    explicit ScopedGhostCubeDistort(void* ws, bool enable) : active(enable) {
        if (active) BeginGhostCubeDistortion(ws);
    }
    ~ScopedGhostCubeDistort() {
        if (active) EndCubeDistortion();
    }
};

struct ScopedCustomHintGhost {
    bool active = false;
    explicit ScopedCustomHintGhost(const uint8_t* ws, uint8_t style) : active(style == 1u || style == 2u) {
        if (active) BeginCustomHintGhostOverride(ws, style);
    }
    ~ScopedCustomHintGhost() {
        if (active) EndCustomHintGhostOverride();
    }
};

static void __fastcall Hook_GhostCubeTransparents(void* renderer) {
    using OrigFn = void(__thiscall*)(void*);
    const CustomThemeSettings eff = GetEffectiveTheme();
    uint8_t* ws = *reinterpret_cast<uint8_t**>(0x5f9064);
    bool isGhostDarkCube = (ws != nullptr && ws[0x48] == 0);
    bool isSolidDarkCube = (ws != nullptr && ws[0x48] != 0);
    ScopedThemeLight darkLight;
    if (isSolidDarkCube) {
        BeginDarkCubeLight(darkLight, eff, true);
    } else if (isGhostDarkCube && eff.hintGhost == 0u) {
        BeginDarkCubeLight(darkLight, eff, false);
    }
    bool usePcDistort = isGhostDarkCube && (eff.hintGhost == 0u);
    ScopedGhostCubeDistort ghostDistort(ws, usePcDistort);
    ScopedCustomHintGhost  customGhost(ws, isGhostDarkCube ? eff.hintGhost : 0u);
    reinterpret_cast<OrigFn>(0x407410)(renderer);
}

static void __fastcall Hook_HintGhostRender(void* cube) {
    using OrigFn = void(__thiscall*)(void*);
    const CustomThemeSettings eff = GetEffectiveTheme();
    uint8_t* ws = *reinterpret_cast<uint8_t**>(0x5f9064);

    static const uint8_t* s_pcGhostWs = nullptr;
    static uint32_t s_pcGhostLastFrame = 0;
    static uint8_t s_pcGhostPrevA = 0;
    static uint8_t s_pcGhostCurA = 0;
    static bool s_pcGhostHasPrev = false;

    if (eff.hintGhost == 0u && cube != nullptr && ws != nullptr) {
        uint8_t* mat = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(cube) + 0x204);
        if (mat != nullptr) {
            uint32_t wsFrame = *reinterpret_cast<const uint32_t*>(ws + 0x04);
            if (ws != s_pcGhostWs || wsFrame < s_pcGhostLastFrame) {
                s_pcGhostHasPrev = false;
            }
            if (wsFrame != s_pcGhostLastFrame || !s_pcGhostHasPrev) {
                uint8_t freshA = mat[0x3f];
                s_pcGhostPrevA = s_pcGhostHasPrev ? s_pcGhostCurA : freshA;
                s_pcGhostCurA = freshA;
                s_pcGhostHasPrev = true;
                s_pcGhostLastFrame = wsFrame;
                s_pcGhostWs = ws;
            }
            const uint8_t* sm = GetScreenMgr();
            uint32_t state = (sm != nullptr) ? *reinterpret_cast<const uint32_t*>(sm + 0x50) : 0u;
            if (state != 0x12u || IsGraphicsPreviewActive()) {
                uint8_t smoothA = s_pcGhostCurA;
                if (state == 0x11u) {
                    float a = std::clamp(g_uiAlpha, 0.0f, 1.0f);
                    smoothA = static_cast<uint8_t>(std::clamp(
                        static_cast<int>(std::lround(
                            static_cast<float>(s_pcGhostPrevA) +
                            static_cast<float>(static_cast<int>(s_pcGhostCurA) - static_cast<int>(s_pcGhostPrevA)) * a
                        )),
                        0, 255
                    ));
                }
                mat[0x3f] = smoothA;
                mat[0x43] = smoothA;
                *reinterpret_cast<uint32_t*>(mat + 0x08) |= 0x40u;
            }
        }
    } else if (ws == nullptr) {
        s_pcGhostWs = nullptr;
        s_pcGhostHasPrev = false;
    }

    ScopedThemeLight darkLight;
    if (eff.hintGhost == 0u) {
        BeginDarkCubeLight(darkLight, eff, false);
    }
    bool usePcDistort = (eff.hintGhost == 0u);
    ScopedGhostCubeDistort ghostDistort(ws, usePcDistort);
    ScopedCustomHintGhost  customGhost(ws, eff.hintGhost);
    reinterpret_cast<OrigFn>(0x48c670)(cube);
}

static int32_t g_curMatCubeType = 0;

static uint8_t GetEffectiveCubeMatStyle() {
    const CustomThemeSettings eff = GetEffectiveTheme();
    if (g_curMatCubeType == 0) return eff.playerCube;
    const uint8_t* ws = *reinterpret_cast<const uint8_t* const*>(0x5f9064);
    if (ws != nullptr && ws[0x48] == 0) {
        return eff.hintGhost;
    }
    return eff.darkCube;
}

static bool IsPcHintGhostWithJavaPlayerCube() {
    if (g_curMatCubeType != 1) return false;
    const uint8_t* ws = *reinterpret_cast<const uint8_t* const*>(0x5f9064);
    if (ws == nullptr || ws[0x48] != 0) return false;
    const CustomThemeSettings eff = GetEffectiveTheme();
    return (eff.hintGhost == 0u && eff.playerCube == 2u);
}

static void* __cdecl Hook_ComputeCubeDiffuse(
    uint8_t* out, const uint8_t* baseColor,
    int multR, int multG, int multB,
    int addR, int addG, int addB
) {
    uint8_t style = GetEffectiveCubeMatStyle();
    if (style == 2u) {
        constexpr float kInvDenom = 1.0f / (2.0f * 0.8819171f);
        for (int c = 0; c < 3; ++c) {
            int v = static_cast<int>(std::lround(static_cast<float>(g_javaCubeDeltaTopRight[c]) * kInvDenom));
            out[c] = static_cast<uint8_t>(std::clamp(v, 0, 255));
        }
        out[3] = baseColor[3];
        return out;
    }
    uint8_t cleanBase[4]{};
    if (baseColor != nullptr && IsPcHintGhostWithJavaPlayerCube()) {
        cleanBase[0] = static_cast<uint8_t>((g_rawCubePal15 >> 16) & 0xffu);
        cleanBase[1] = static_cast<uint8_t>((g_rawCubePal15 >> 8) & 0xffu);
        cleanBase[2] = static_cast<uint8_t>(g_rawCubePal15 & 0xffu);
        cleanBase[3] = baseColor[3];
        baseColor = cleanBase;
    }
    using OrigFn = void*(__cdecl*)(uint8_t*, const uint8_t*, int, int, int, int, int, int);
    return reinterpret_cast<OrigFn>(0x490e30)(out, baseColor, multR, multG, multB, addR, addG, addB);
}

static void __declspec(naked) Hook_ComputeCubeDiffuse_Naked() {
    __asm {
        mov eax, dword ptr [esi]
        mov g_curMatCubeType, eax
        jmp Hook_ComputeCubeDiffuse
    }
}

static void* __cdecl Hook_ComputeCubeMatColor(
    uint8_t* out, const uint8_t* baseColor,
    int multR, int multG, int multB,
    int addR, int addG, int addB
) {
    uint8_t style = GetEffectiveCubeMatStyle();
    if (style != 0u) {
        int targetScale = (style == 2u) ? 254 : 255;
        auto scaleMult = [targetScale](int m) -> int {
            if (m <= 0) return 0;
            return (std::min)(255, (m * targetScale + 111) / 223);
        };
        multR = scaleMult(multR);
        multG = scaleMult(multG);
        multB = scaleMult(multB);
    }
    uint8_t cleanBase[4]{};
    if (baseColor != nullptr && IsPcHintGhostWithJavaPlayerCube()) {
        cleanBase[0] = static_cast<uint8_t>((g_rawCubePal15 >> 16) & 0xffu);
        cleanBase[1] = static_cast<uint8_t>((g_rawCubePal15 >> 8) & 0xffu);
        cleanBase[2] = static_cast<uint8_t>(g_rawCubePal15 & 0xffu);
        cleanBase[3] = baseColor[3];
        baseColor = cleanBase;
    }
    using OrigFn = void*(__cdecl*)(uint8_t*, const uint8_t*, int, int, int, int, int, int);
    return reinterpret_cast<OrigFn>(0x490e30)(out, baseColor, multR, multG, multB, addR, addG, addB);
}

static void __declspec(naked) Hook_ComputeCubeMatColor_Naked() {
    __asm {
        mov eax, dword ptr [esi]
        mov g_curMatCubeType, eax
        jmp Hook_ComputeCubeMatColor
    }
}

struct CubeMatColors {
    uint8_t amb[4]{};
    uint8_t diff[4]{};
    uint8_t emi[4]{};
    bool    valid = false;
};

static bool          s_pauseCubeActive     = false;
static double        s_pauseCubeTickAcc    = 0.0;
static uint8_t       s_pauseCubeLastStyle  = 0;
static uint8_t       s_pauseGhostLastStyle = 0;
static CubeMatColors s_pausePlayerPrev{};
static CubeMatColors s_pausePlayerCur{};
static CubeMatColors s_pauseGhostPrev{};
static CubeMatColors s_pauseGhostCur{};

static void SeedCubeMatColorsFromCube(const uint8_t* cube, CubeMatColors* out) {
    if (cube == nullptr) {
        out->valid = false;
        return;
    }
    const uint8_t* mat = *reinterpret_cast<const uint8_t* const*>(cube + 0x204);
    if (mat == nullptr) {
        out->valid = false;
        return;
    }
    std::memcpy(out->amb,  mat + 0x3c, 4);
    std::memcpy(out->diff, mat + 0x40, 4);
    std::memcpy(out->emi,  mat + 0x48, 4);
    out->valid = true;
}

static void ComputeCubeMaterialColors(
    const uint8_t* ws, const uint8_t* cube, bool isHintGhost, uint32_t visTickForGhost, CubeMatColors* out
) {
    if (ws == nullptr || cube == nullptr || out == nullptr) {
        if (out != nullptr) out->valid = false;
        return;
    }
    uint32_t pal15 = *reinterpret_cast<const uint32_t*>(0x5f914c);
    uint8_t baseColor[4] = {
        static_cast<uint8_t>((pal15 >> 16) & 0xffu),
        static_cast<uint8_t>((pal15 >> 8) & 0xffu),
        static_cast<uint8_t>(pal15 & 0xffu),
        0xffu
    };

    if (!isHintGhost) {
        float flashT = *reinterpret_cast<const float*>(cube + 0x2a8);
        if (flashT > 0.0f) {
            uint8_t flashCol[4] = {
                *reinterpret_cast<const uint8_t*>(0x5e3de4),
                *reinterpret_cast<const uint8_t*>(0x5e3de5),
                *reinterpret_cast<const uint8_t*>(0x5e3de6),
                0xffu
            };
            uint8_t tmp[4]{};
            reinterpret_cast<void*(__cdecl*)(uint8_t*, const uint8_t*, const uint8_t*, float)>(0x490ce0)(
                tmp, baseColor, flashCol, flashT
            );
            std::memcpy(baseColor, tmp, 4);
        }
    } else {
        uint32_t m = visTickForGhost % 10u;
        int32_t b = (m < 5u)
            ? static_cast<int32_t>(5u * (50u + m))
            : static_cast<int32_t>(5u * (60u - m));
        if (ws[0x50] != 0u) {
            int32_t fadeIn = *reinterpret_cast<const int32_t*>(ws + 0x64);
            if (fadeIn > 0) {
                b = (((6 - fadeIn) * 4096 / 6) * 250) >> 12;
            }
        } else {
            int32_t fadeOut = *reinterpret_cast<const int32_t*>(ws + 0x68);
            if (fadeOut > 0) {
                b = ((fadeOut * 4096 / 5) * 250) >> 12;
            }
        }
        baseColor[3] = static_cast<uint8_t>(std::clamp(b / 2, 0, 255));
    }

    void** cfgPtr = reinterpret_cast<void**(__cdecl*)()>(0x4764c0)();
    void* cfg = (cfgPtr != nullptr) ? *cfgPtr : nullptr;
    if (cfg == nullptr) {
        out->valid = false;
        return;
    }
    auto cfgInt = reinterpret_cast<int(__thiscall*)(void*, int)>(0x4408f0);

    g_curMatCubeType = isHintGhost ? 1 : 0;
    Hook_ComputeCubeDiffuse(
        out->diff, baseColor,
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1c4)),
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1c8)),
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1cc)),
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1d0)),
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1d4)),
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1d8))
    );
    Hook_ComputeCubeMatColor(
        out->amb, baseColor,
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1dc)),
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1e0)),
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1e4)),
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1e8)),
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1ec)),
        cfgInt(cfg, *reinterpret_cast<const int*>(cube + 0x1f0))
    );
    out->emi[0] = *reinterpret_cast<const uint8_t*>(0x5f88e0);
    out->emi[1] = *reinterpret_cast<const uint8_t*>(0x5f88e1);
    out->emi[2] = *reinterpret_cast<const uint8_t*>(0x5f88e2);
    out->emi[3] = 0xffu;
    out->valid = true;
}

static void WriteLerpedCubeMat(
    uint8_t* cube, const CubeMatColors& prev, const CubeMatColors& cur, float frac
) {
    if (cube == nullptr || !cur.valid) return;
    uint8_t* mat = *reinterpret_cast<uint8_t**>(cube + 0x204);
    if (mat == nullptr) return;
    const CubeMatColors& p = prev.valid ? prev : cur;
    float f = std::clamp(frac, 0.0f, 1.0f);
    auto lerp4 = [f](const uint8_t a[4], const uint8_t b[4], uint8_t out[4]) {
        for (int i = 0; i < 4; ++i) {
            int v = static_cast<int>(std::lround(
                static_cast<float>(a[i]) + static_cast<float>(static_cast<int>(b[i]) - static_cast<int>(a[i])) * f
            ));
            out[i] = static_cast<uint8_t>(std::clamp(v, 0, 255));
        }
    };
    lerp4(p.amb,  cur.amb,  mat + 0x3c);
    lerp4(p.diff, cur.diff, mat + 0x40);
    lerp4(p.emi,  cur.emi,  mat + 0x48);
    *reinterpret_cast<uint32_t*>(mat + 0x08) |= 0x40u;
}

} // namespace

namespace edgemod {

void __cdecl ResetPausedLevelVisualsState() {
    s_pauseCubeActive = false;
    s_pauseCubeTickAcc = 0.0;
    s_pausePlayerPrev.valid = false;
    s_pausePlayerCur.valid = false;
    s_pauseGhostPrev.valid = false;
    s_pauseGhostCur.valid = false;
}

void __cdecl UpdatePausedLevelVisuals(uint8_t* ws, float dt, bool wsFrozen) {
    if (ws == nullptr) return;
    uint8_t* lvl = GetLevel();

    // 1. Keep PC finish platform, shrinker, and all-theme prism spinning animations running
    if (lvl != nullptr) {
        auto modelUpdate = reinterpret_cast<void(__thiscall*)(void*)>(0x421590);
        uint8_t* fin = *reinterpret_cast<uint8_t**>(lvl + 0x0);
        if (fin != nullptr) {
            void* finModel = *reinterpret_cast<void**>(fin + 0xc);
            if (finModel != nullptr) modelUpdate(finModel);
        }

        int32_t shrinkerCount = *reinterpret_cast<const int32_t*>(lvl + 0x50);
        uint8_t** shrinkers = *reinterpret_cast<uint8_t***>(lvl + 0x54);
        if (shrinkers != nullptr && shrinkerCount > 0) {
            for (int32_t i = 0; i < shrinkerCount; ++i) {
                uint8_t* sh = shrinkers[i];
                if (sh != nullptr && sh[0x10] != 0u) {
                    void* shModel = *reinterpret_cast<void**>(sh + 0x18);
                    if (shModel != nullptr) modelUpdate(shModel);
                }
            }
        }

        int32_t prismCount = *reinterpret_cast<const int32_t*>(lvl + 0x30);
        uint8_t** prisms = *reinterpret_cast<uint8_t***>(lvl + 0x34);
        if (prisms != nullptr && prismCount > 0) {
            for (int32_t i = 0; i < prismCount; ++i) {
                uint8_t* pr = prisms[i];
                if (pr != nullptr && *reinterpret_cast<void* const*>(pr + 0x24) == nullptr && pr[0x18] != 0u) {
                    void* prModel = *reinterpret_cast<void**>(pr + 0x1c);
                    void* prShadow = *reinterpret_cast<void**>(pr + 0x2c);
                    if (prModel != nullptr) modelUpdate(prModel);
                    if (prShadow != nullptr) modelUpdate(prShadow);
                }
            }
        }
    }

    // 2. Advance Player Cube & PC Hint Ghost palette/material colors smoothly at 30 Hz + sub-tick lerp
    if (!wsFrozen || lvl == nullptr) return;

    int theme = *reinterpret_cast<const int*>(lvl + 0x6c);
    uint8_t* cube0 = *reinterpret_cast<uint8_t**>(ws + 0x34);
    uint8_t* cube1 = *reinterpret_cast<uint8_t**>(ws + 0x38);
    bool hasPlayer = (cube0 != nullptr && cube0[0x171] == 0u);
    bool hasHintGhost = (cube1 != nullptr && ws[0x48] == 0u &&
                         (ws[0x50] != 0u || *reinterpret_cast<const int32_t*>(ws + 0x68) > 0));
    const CustomThemeSettings eff = GetEffectiveTheme();

    auto stepOneCubeTick = [&](uint32_t visTick, bool shiftPrev) {
        uint32_t origWsFrame = *reinterpret_cast<uint32_t*>(ws + 0x4);
        *reinterpret_cast<uint32_t*>(ws + 0x4) = visTick;
        Hook_ComputeCubePalette(theme);
        *reinterpret_cast<uint32_t*>(ws + 0x4) = origWsFrame;

        if (shiftPrev) s_pausePlayerPrev = s_pausePlayerCur;
        if (hasPlayer) {
            ComputeCubeMaterialColors(ws, cube0, false, visTick, &s_pausePlayerCur);
        } else {
            s_pausePlayerCur.valid = false;
        }

        if (shiftPrev) s_pauseGhostPrev = s_pauseGhostCur;
        if (hasHintGhost) {
            ComputeCubeMaterialColors(ws, cube1, true, visTick, &s_pauseGhostCur);
        } else {
            s_pauseGhostCur.valid = false;
        }
    };

    uint32_t baseVisTick = *reinterpret_cast<const uint32_t*>(ws + 0x4)
                         + static_cast<uint32_t>(std::floor(GetPauseExtraTicks()));

    if (!s_pauseCubeActive) {
        if (hasPlayer) SeedCubeMatColorsFromCube(cube0, &s_pausePlayerPrev);
        else s_pausePlayerPrev.valid = false;
        if (hasHintGhost) SeedCubeMatColorsFromCube(cube1, &s_pauseGhostPrev);
        else s_pauseGhostPrev.valid = false;
        stepOneCubeTick(baseVisTick, false);
        s_pauseCubeActive = true;
        s_pauseCubeTickAcc = 0.0;
    } else if (eff.playerCube != s_pauseCubeLastStyle || eff.hintGhost != s_pauseGhostLastStyle) {
        uint32_t savedPhase = *reinterpret_cast<uint32_t*>(0x5f9104);
        stepOneCubeTick(baseVisTick, false);
        *reinterpret_cast<uint32_t*>(0x5f9104) = savedPhase;
        s_pausePlayerPrev = s_pausePlayerCur;
        s_pauseGhostPrev = s_pauseGhostCur;
    }
    s_pauseCubeLastStyle = eff.playerCube;
    s_pauseGhostLastStyle = eff.hintGhost;

    s_pauseCubeTickAcc += static_cast<double>(dt) * 30.0;
    int steps = 0;
    while (s_pauseCubeTickAcc >= 1.0 && steps < 4) {
        s_pauseCubeTickAcc -= 1.0;
        stepOneCubeTick(baseVisTick, true);
        ++steps;
    }
    if (s_pauseCubeTickAcc >= 1.0) {
        s_pauseCubeTickAcc = std::fmod(s_pauseCubeTickAcc, 1.0);
    }

    float frac = static_cast<float>(std::clamp(s_pauseCubeTickAcc, 0.0, 1.0));
    if (hasPlayer) {
        WriteLerpedCubeMat(cube0, s_pausePlayerPrev, s_pausePlayerCur, frac);
    }
    if (hasHintGhost && eff.hintGhost == 0u) {
        WriteLerpedCubeMat(cube1, s_pauseGhostPrev, s_pauseGhostCur, frac);
    }
}

void __cdecl ResetColorThemeLevelCaches() {
    s_wallSubMeshes.clear();
    s_cachedLevelModel = nullptr;
    s_cachedHeightTiles = 0;
    s_appliedWallGrad = 0;

    g_bumperFlashStates.clear();
    g_bumperFlashLevel = nullptr;
    g_bumperFlashLastWsFrame = 0;

    s_platEdgeSubMeshes.clear();
    s_anyPlatEdgeVbModified = false;

    s_switchSubMeshes.clear();
    s_anySwitchVbModified = false;

    s_mpDepthPlan.clear();
    s_mpEntries.clear();
    s_mpOrder.clear();
    s_mpKept.clear();
    s_mpPlanLevel = nullptr;
    s_mpRenderCursor = 0;
}

uint32_t __cdecl GetCleanHintGhostBaseRgb() {
    return g_rawCubePal15;
}

uint32_t __fastcall RenderPlayerCubeWithColorTheme(void* cube) {
    using OrigFn = uint32_t(__thiscall*)(void*);
    uint32_t ret = 0;
    {
        ScopedThemeLight cubeLight;
        if (GetEffectiveTheme().playerCube == 2u) {
            cubeLight.BeginSingle({1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, -0.8819171f, -0.4714045f});
        }
        ScopedPlayerCubeDistort cubeDistort(cube);
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
    RegRelCall(0x490c82, exCubeMat, 5, 5, &Hook_ComputeCubeMatColor_Naked);

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
    RegRelCall(0x490bd5, exCubeDiff, 5, 5, &Hook_ComputeCubeDiffuse_Naked);

    // 17. WorldScreen::render Ghost Cube::render Transparents (0x47b406, 5B)
    static const uint8_t exGhostCubeTransp[5] = {0xe8, 0x05, 0xc0, 0xf8, 0xff};
    RegRelCall(0x47b406, exGhostCubeTransp, 5, 5, &Hook_GhostCubeTransparents);

    // 18. WorldScreen::render Switch::render (0x47b309, 5B)
    static const uint8_t exSwitchRender[5] = {0xe8, 0x22, 0xa9, 0x00, 0x00};
    RegRelCall(0x47b309, exSwitchRender, 5, 5, &Hook_SwitchRender);

    // 19. WorldScreen::render HoloSwitch::render (0x47b330, 5B)
    static const uint8_t exHoloSwitchRender[5] = {0xe8, 0x1b, 0xae, 0x00, 0x00};
    RegRelCall(0x47b330, exHoloSwitchRender, 5, 5, &Hook_HoloSwitchRender);

    // 20. WorldScreen::render Shrinker::render (0x47b359, 5B)
    static const uint8_t exShrinkerRender[5] = {0xe8, 0xf2, 0xbb, 0x00, 0x00};
    RegRelCall(0x47b359, exShrinkerRender, 5, 5, &Hook_ShrinkerRender);

    // 21. WorldScreen::render FallingPlatform::render (0x47b2da, 5B)
    static const uint8_t exFallingPlatRender[5] = {0xe8, 0x41, 0xb8, 0x00, 0x00};
    RegRelCall(0x47b2da, exFallingPlatRender, 5, 5, &Hook_FallingPlatformRender);

    // 22. WorldScreen::render DarkCube Cube::render (0x47b3f8, 5B)
    static const uint8_t exDarkCubeRender[5] = {0xe8, 0x33, 0x12, 0x01, 0x00};
    RegRelCall(0x47b3f8, exDarkCubeRender, 5, 5, &Hook_DarkCubeRender);

    // 23. WorldScreen::render HintGhost Cube::renderGhost (0x47b3ff, 5B)
    static const uint8_t exHintGhostRender[5] = {0xe8, 0x6c, 0x12, 0x01, 0x00};
    RegRelCall(0x47b3ff, exHintGhostRender, 5, 5, &Hook_HintGhostRender);
}
