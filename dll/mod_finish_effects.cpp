#include "mod_smooth_shared.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace edgemod;

struct FinishVert {
    float    x, y, z;
    D3DCOLOR diffuse;
};
constexpr DWORD kFinishFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;

struct WinEffectState {
    bool     active     = false;
    uint32_t startFrame = 0;
    float    centerX    = 0.0f;
    float    bottomY    = 0.0f;
    float    centerZ    = 0.0f;
    float    cubeSize   = 30.0f;
};

WinEffectState g_winFx{};

// Mobile finish platform color tables from Android EDGE-v4.0.1-full.apk (libcgame.so)
// Outer 3x3 border (0x670770) & Inner 1x1 center (0x6707cc), 23-frame cycle at 30 Hz
static const uint32_t kMobileFinishOuter[23] = {
    0xf1eafeu, 0xf6e9ffu, 0xfeedffu, 0xffedfcu, 0xffeff7u, 0xffedf2u, 0xffedeeu, 0xfff1efu,
    0xfff2e1u, 0xfffae6u, 0xffffe5u, 0xf8fddfu, 0xf4ffe1u, 0xefffe7u, 0xecffeeu, 0xeefff2u,
    0xeefff6u, 0xeafff8u, 0xeffffcu, 0xecfdffu, 0xf0f9ffu, 0xeaf2ffu, 0xececffu
};
static const uint32_t kMobileFinishInner[23] = {
    0x69ffe5u, 0x7be7f2u, 0x8ccfffu, 0xa4c5fcu, 0xbbbbfau, 0xd0b6fdu, 0xe4b1ffu, 0xf2a7f7u,
    0xffa0eeu, 0xff9dd0u, 0xff95b0u, 0xffa1a4u, 0xffad9au, 0xffc16bu, 0xffd845u, 0xedec31u,
    0xdcff1du, 0xb5ff3au, 0x8eff52u, 0x69ff77u, 0x69ff94u, 0x69ffb3u, 0x69ffccu
};

// Java J2ME finish platform color tables from galaxia.spr (11-frame cycle, 2 Java ticks = 90 ms/frame = 2.7 PC 30 Hz ticks/frame)
static const uint32_t kJavaFinishOuter[11] = {
    0xd8faffu, 0xd2ffdeu, 0xe4ffd0u, 0xfaffc1u, 0xfff7d6u, 0xffece4u,
    0xfbe3ecu, 0xfce3f8u, 0xf9e3ffu, 0xe7d8ffu, 0xdbe7fau
};
static const uint32_t kJavaFinishInner[11] = {
    0x51ff70u, 0x86ff4cu, 0xcaff06u, 0xffd771u, 0xffbfabu, 0xffafc7u,
    0xffabedu, 0xe9aeffu, 0xcdbeffu, 0xabc4ffu, 0x72dffeu
};

// Mobile / PC 13-color cube rainbow palettes (matches 0x589b50 & 0x589b84)
static const uint32_t kCubePaletteNormal[13] = {
    0xff718au, 0xff5fdeu, 0xe158ffu, 0xb86effu, 0x7d90ffu, 0x51a6ffu, 0x3fbcffu,
    0x0df78au, 0x46ff52u, 0x7dff3fu, 0xccff00u, 0xffbf3eu, 0xff9741u
};
static const uint32_t kCubePaletteDark[13] = {
    0xff3558u, 0xff27d3u, 0xd720ffu, 0xa03effu, 0x3c59ffu, 0x1a8affu, 0x23b2ffu,
    0x00ff9cu, 0x21ff46u, 0x5aff00u, 0xccff00u, 0xffbb19u, 0xff7809u
};

// J2ME Java finish effect palettes from a.java (lines 10761-10762)
static const uint32_t kJavaPaletteLight[13] = {
    0xfea9b6u, 0xfe9febu, 0xee9dfeu, 0xd3a9feu, 0xb5befeu, 0xa1cffeu, 0x96dafeu,
    0x3df89du, 0x80ff85u, 0xadff5au, 0xeef21cu, 0xffd275u, 0xffbd84u
};
static const uint32_t kJavaPaletteDark[13] = {
    0xff718au, 0xff5fdeu, 0xe158ffu, 0xb86effu, 0x7d90ffu, 0x51a6ffu, 0x3fbcffu,
    0x0df78au, 0x46ff52u, 0x7dff3fu, 0xccff00u, 0xffbf3eu, 0xff9741u
};

inline D3DCOLOR MakeArgb(int a, int r, int g, int b) {
    a = std::clamp(a, 0, 255);
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    return D3DCOLOR_ARGB(a, r, g, b);
}

inline D3DCOLOR ShadeRgb(uint32_t rgb, int delta, int alpha) {
    int r = static_cast<int>((rgb >> 16) & 0xffu) + delta;
    int g = static_cast<int>((rgb >> 8) & 0xffu) + delta;
    int b = static_cast<int>(rgb & 0xffu) + delta;
    return MakeArgb(alpha, r, g, b);
}

inline uint32_t LerpRgbU32(uint32_t a, uint32_t b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    int r0 = static_cast<int>((a >> 16) & 0xffu);
    int g0 = static_cast<int>((a >> 8) & 0xffu);
    int b0 = static_cast<int>(a & 0xffu);
    int r1 = static_cast<int>((b >> 16) & 0xffu);
    int g1 = static_cast<int>((b >> 8) & 0xffu);
    int b1 = static_cast<int>(b & 0xffu);
    int r = std::clamp(r0 + static_cast<int>(std::lround(static_cast<float>(r1 - r0) * t)), 0, 255);
    int g = std::clamp(g0 + static_cast<int>(std::lround(static_cast<float>(g1 - g0) * t)), 0, 255);
    int bl = std::clamp(b0 + static_cast<int>(std::lround(static_cast<float>(b1 - b0) * t)), 0, 255);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(bl);
}

struct ScopedD3DState {
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

    explicit ScopedD3DState(IDirect3DDevice9* d) : dev(d) {
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
        dev->SetFVF(kFinishFvf);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
        dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
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

    ~ScopedD3DState() {
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

static IDirect3DDevice9* GetD3DDevice() {
    uint8_t* renderer = GetRenderer();
    if (renderer == nullptr) return nullptr;
    return *reinterpret_cast<IDirect3DDevice9**>(renderer + 0xcc);
}

static bool IsGameplayPaused() {
    uint8_t* sm = GetScreenMgr();
    return (sm != nullptr && *reinterpret_cast<uint32_t*>(sm + 0x50) == 0x12u);
}

static void EmitQuadXZ(
    std::vector<FinishVert>& out, float x0, float x1, float z0, float z1, float y, D3DCOLOR col
) {
    out.push_back({x0, y, z0, col});
    out.push_back({x1, y, z0, col});
    out.push_back({x1, y, z1, col});
    out.push_back({x0, y, z0, col});
    out.push_back({x1, y, z1, col});
    out.push_back({x0, y, z1, col});
}

static void EmitQuad3D(
    std::vector<FinishVert>& out,
    const float p0[3], const float p1[3], const float p2[3], const float p3[3],
    D3DCOLOR col
) {
    out.push_back({p0[0], p0[1], p0[2], col});
    out.push_back({p1[0], p1[1], p1[2], col});
    out.push_back({p2[0], p2[1], p2[2], col});
    out.push_back({p0[0], p0[1], p0[2], col});
    out.push_back({p2[0], p2[1], p2[2], col});
    out.push_back({p3[0], p3[1], p3[2], col});
}

static void EmitFeatheredQuadXZ(
    std::vector<FinishVert>& out,
    float ix0, float ix1, float iz0, float iz1,
    float padX, float padZ,
    float y, uint32_t rgb, int peakAlpha
) {
    auto makeRingCol = [&](float r) -> D3DCOLOR {
        float w = 1.0f - (3.0f * r * r - 2.0f * r * r * r);
        int a = std::clamp(static_cast<int>(std::lround(static_cast<float>(peakAlpha) * w)), 0, 255);
        return (static_cast<D3DCOLOR>(a) << 24) | (rgb & 0x00ffffffu);
    };

    auto emitQuadVerts = [&](float x0, float z0, D3DCOLOR c0,
                             float x1, float z1, D3DCOLOR c1,
                             float x2, float z2, D3DCOLOR c2,
                             float x3, float z3, D3DCOLOR c3) {
        out.push_back({x0, y, z0, c0});
        out.push_back({x1, y, z1, c1});
        out.push_back({x2, y, z2, c2});
        out.push_back({x0, y, z0, c0});
        out.push_back({x2, y, z2, c2});
        out.push_back({x3, y, z3, c3});
    };

    // Solid inner core
    EmitQuadXZ(out, ix0, ix1, iz0, iz1, y, makeRingCol(0.0f));

    // 3 smoothstep feather rings so transitions into the outer tile have no Mach-band creases
    constexpr float kRadii[4] = {0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f};
    for (int k = 0; k < 3; ++k) {
        float rA = kRadii[k];
        float rB = kRadii[k + 1];
        D3DCOLOR colA = makeRingCol(rA);
        D3DCOLOR colB = makeRingCol(rB);

        float ax0 = ix0 - padX * rA, ax1 = ix1 + padX * rA;
        float az0 = iz0 - padZ * rA, az1 = iz1 + padZ * rA;
        float bx0 = ix0 - padX * rB, bx1 = ix1 + padX * rB;
        float bz0 = iz0 - padZ * rB, bz1 = iz1 + padZ * rB;

        // North / South / West / East edge bands
        emitQuadVerts(ax0, bz0, colB, ax1, bz0, colB, ax1, az0, colA, ax0, az0, colA);
        emitQuadVerts(ax0, az1, colA, ax1, az1, colA, ax1, bz1, colB, ax0, bz1, colB);
        emitQuadVerts(bx0, az0, colB, ax0, az0, colA, ax0, az1, colA, bx0, az1, colB);
        emitQuadVerts(ax1, az0, colA, bx1, az0, colB, bx1, az1, colB, ax1, az1, colA);

        // 4 corners split across the chamfer diagonal (inner vertex colA, outer 3 vertices colB)
        auto emitCorner = [&](float xIn, float zIn, float xA, float zA, float xOut, float zOut, float xB, float zB) {
            out.push_back({xIn,  y, zIn,  colA});
            out.push_back({xA,   y, zA,   colB});
            out.push_back({xB,   y, zB,   colB});
            out.push_back({xA,   y, zA,   colB});
            out.push_back({xOut, y, zOut, colB});
            out.push_back({xB,   y, zB,   colB});
        };
        emitCorner(ax0, az0, ax0, bz0, bx0, bz0, bx0, az0);
        emitCorner(ax1, az0, bx1, az0, bx1, bz0, ax1, bz0);
        emitCorner(ax1, az1, ax1, bz1, bx1, bz1, bx1, az1);
        emitCorner(ax0, az1, bx0, az1, bx0, bz1, ax0, bz1);
    }
}

// ------------------------------------------------------------------
// 1. Mobile Finish Platform Renderer (0x47b283)
// ------------------------------------------------------------------
static float Hash01(uint32_t frame, int idx, uint32_t ch) {
    uint32_t h = frame * 747796405u
               + static_cast<uint32_t>(idx + 16) * 2891336453u
               + ch * 277803737u + 1u;
    h = ((h >> 16) ^ h) * 0x45d9f3bu;
    h = ((h >> 16) ^ h) * 0x45d9f3bu;
    h = (h >> 16) ^ h;
    return static_cast<float>(h % 100u) / 100.0f;
}

static float SmoothHash01(uint32_t step0, float frac, int idx, uint32_t ch) {
    float p0 = Hash01(step0 - 1u, idx, ch);
    float p1 = Hash01(step0,      idx, ch);
    float p2 = Hash01(step0 + 1u, idx, ch);
    float p3 = Hash01(step0 + 2u, idx, ch);
    float t  = std::clamp(frac, 0.0f, 1.0f);
    float t2 = t * t;
    float t3 = t2 * t;
    float v  = 0.5f * (
        (2.0f * p1)
        + (-p0 + p2) * t
        + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );
    return std::clamp(v, 0.0f, 1.0f);
}

static void __cdecl RenderMobileFinishPlatform_Impl(const uint8_t* finish) {
    if (finish == nullptr) return;
    IDirect3DDevice9* dev = GetD3DDevice();
    uint8_t* game = GetWorldScreen();
    if (dev == nullptr || game == nullptr) return;

    float t30 = GetSmoothWsFrame(game);
    if (t30 < 0.0f) t30 = 0.0f;
    float tFloor = std::floor(t30);
    uint32_t frame = static_cast<uint32_t>(tFloor);
    float sub = t30 - tFloor;

    uint32_t o0 = frame % 23u;
    uint32_t o1 = (o0 + 1u) % 23u;
    uint32_t i0 = (frame + 2u) % 23u;
    uint32_t i1 = (i0 + 1u) % 23u;

    D3DCOLOR outerCol = 0xff000000u | LerpRgbU32(kMobileFinishOuter[o0], kMobileFinishOuter[o1], sub);
    D3DCOLOR innerCol = 0xff000000u | LerpRgbU32(kMobileFinishInner[i0], kMobileFinishInner[i1], sub);
    uint32_t glowRgb  = LerpRgbU32(kMobileFinishInner[o0], kMobileFinishInner[o1], sub);

    int32_t fx = *reinterpret_cast<const int32_t*>(finish + 0x0);
    int32_t fy = *reinterpret_cast<const int32_t*>(finish + 0x4);
    int32_t fz = *reinterpret_cast<const int32_t*>(finish + 0x8);

    float x0 = static_cast<float>(fx) * 30.0f;
    float x1 = x0 + 30.0f;
    float xm = x0 - 30.0f;
    float xp = x0 + 60.0f;

    float z0 = static_cast<float>(fy) * 30.0f;
    float z1 = z0 + 30.0f;
    float zm = z0 - 30.0f;
    float zp = z0 + 60.0f;

    float y = static_cast<float>(fz) * 30.0f + 0.06f;

    std::vector<FinishVert> verts;
    verts.reserve(30);
    // 4 non-overlapping outer border quads forming the 3x3 ring + 1 center inner quad
    EmitQuadXZ(verts, xm, xp, zm, z0, y, outerCol);
    EmitQuadXZ(verts, xm, xp, z1, zp, y, outerCol);
    EmitQuadXZ(verts, xm, x0, z0, z1, y, outerCol);
    EmitQuadXZ(verts, x1, xp, z0, z1, y, outerCol);
    EmitQuadXZ(verts, x0, x1, z0, z1, y, innerCol);

    ScopedD3DState guard(dev);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 10, verts.data(), sizeof(FinishVert));

    // Mobile center shimmering/orbiting semi-transparent glow squares with C1-continuous orbit/burst & Catmull-Rom jitter
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kDeg2Rad = kPi / 180.0f;
    float phase = std::fmod(t30 * 10.0f, 500.0f);
    float easedOrbitDeg = 360.0f;
    float jitterAmp = 1.0f;
    if (phase < 360.0f) {
        float uOrbit = phase / 360.0f;
        easedOrbitDeg = 360.0f * (uOrbit - std::sin(2.0f * kPi * uOrbit) / (2.0f * kPi));
    } else {
        float uBurst = (phase - 360.0f) / 140.0f;
        float env = 0.5f * (1.0f - std::cos(2.0f * kPi * uBurst));
        jitterAmp = 1.0f + 5.0f * env;
    }

    std::vector<FinishVert> glowVerts;
    glowVerts.reserve(60);

    for (int idx = 0; idx < 10; ++idx) {
        float u = static_cast<float>(idx) / 10.0f;
        float tri = (u < 0.5f) ? u : (1.0f - u);
        int alpha = static_cast<int>(tri * 2.0f * 100.0f);
        if (alpha <= 0) continue;

        float angDeg = static_cast<float>(idx * 7) + easedOrbitDeg;
        float angRad = angDeg * kDeg2Rad;
        float ca = std::cos(angRad);
        float sa = std::sin(angRad);
        float offsetScale = 1.25f - 0.25f * ca;

        float pivotX = (x0 + 15.0f) + offsetScale * static_cast<float>(idx - 5)
                     + (SmoothHash01(frame, sub, idx, 0u) - 0.5f) * offsetScale;
        float pivotZ = (z0 + 15.0f) + offsetScale * static_cast<float>(idx - 5)
                     + (SmoothHash01(frame, sub, idx, 1u) - 0.5f) * offsetScale;

        float qx0 = x0 + (SmoothHash01(frame, sub, idx, 2u) - 0.5f) * jitterAmp;
        float qz1 = z1 + (SmoothHash01(frame, sub, idx, 3u) - 0.5f) * jitterAmp;
        float qx1 = x1 + (SmoothHash01(frame, sub, idx, 4u) - 0.5f) * jitterAmp;
        float qz0 = z0 + (SmoothHash01(frame, sub, idx, 5u) - 0.5f) * jitterAmp;

        float avgX = 0.5f * (qx0 + qx1);
        float avgZ = 0.5f * (qz0 + qz1);
        float dx = avgX - pivotX;
        float dz = avgZ - pivotZ;
        float shiftX = (ca * dx - sa * dz + pivotX) - avgX;
        float shiftZ = (sa * dx + ca * dz + pivotZ) - avgZ;

        float yGlow = y + 0.02f + static_cast<float>(idx) * 0.005f;
        D3DCOLOR col = (static_cast<D3DCOLOR>(alpha) << 24) | glowRgb;
        EmitQuadXZ(glowVerts, qx0 + shiftX, qx1 + shiftX, qz0 + shiftZ, qz1 + shiftZ, yGlow, col);
    }

    if (!glowVerts.empty()) {
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->DrawPrimitiveUP(
            D3DPT_TRIANGLELIST,
            static_cast<UINT>(glowVerts.size() / 3u),
            glowVerts.data(),
            sizeof(FinishVert)
        );
    }
}

// ------------------------------------------------------------------
// 1b. Java Finish Platform Renderer (galaxia.spr, 0x47b283)
// ------------------------------------------------------------------
static void __cdecl RenderJavaFinishPlatform_Impl(const uint8_t* finish) {
    if (finish == nullptr) return;
    IDirect3DDevice9* dev = GetD3DDevice();
    uint8_t* game = GetWorldScreen();
    if (dev == nullptr || game == nullptr) return;

    float t30 = GetSmoothWsFrame(game);
    if (t30 < 0.0f) t30 = 0.0f;

    // Java galaxia.spr cycles 11 frames at 2 ticks * 45 ms = 90 ms/frame (2.7 PC 30 Hz ticks)
    float javaF = t30 / 2.7f;
    float javaFloor = std::floor(javaF);
    uint32_t step0 = static_cast<uint32_t>(javaFloor);
    float frac = javaF - javaFloor;

    uint32_t j0 = step0 % 11u;
    uint32_t j1 = (j0 + 1u) % 11u;

    uint32_t outerRgb = LerpRgbU32(kJavaFinishOuter[j0], kJavaFinishOuter[j1], frac);
    uint32_t innerRgb = LerpRgbU32(kJavaFinishInner[j0], kJavaFinishInner[j1], frac);
    D3DCOLOR outerCol = 0xff000000u | outerRgb;

    int32_t fx = *reinterpret_cast<const int32_t*>(finish + 0x0);
    int32_t fy = *reinterpret_cast<const int32_t*>(finish + 0x4);
    int32_t fz = *reinterpret_cast<const int32_t*>(finish + 0x8);

    float x0 = static_cast<float>(fx) * 30.0f;
    float x1 = x0 + 30.0f;
    float xm = x0 - 30.0f;
    float xp = x0 + 60.0f;

    float z0 = static_cast<float>(fy) * 30.0f;
    float z1 = z0 + 30.0f;
    float zm = z0 - 30.0f;
    float zp = z0 + 60.0f;

    float y = static_cast<float>(fz) * 30.0f + 0.06f;

    std::vector<FinishVert> outerVerts;
    outerVerts.reserve(6);
    EmitQuadXZ(outerVerts, xm, xp, zm, zp, y, outerCol);

    ScopedD3DState guard(dev);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, outerVerts.data(), sizeof(FinishVert));

    // 3-ring smoothstep feathered 1x1 center halo + 4 Catmull-Rom in-place shimmering layers
    std::vector<FinishVert> blendVerts;
    blendVerts.reserve(150 * 5);
    EmitFeatheredQuadXZ(
        blendVerts,
        x0 + 2.0f, x1 - 2.0f, z0 + 2.0f, z1 - 2.0f,
        5.5f, 5.5f,
        y + 0.015f, innerRgb, 255
    );

    for (int idx = 0; idx < 4; ++idx) {
        float dx0 = (SmoothHash01(step0, frac, idx, 0u) - 0.5f) * 4.4f;
        float dx1 = (SmoothHash01(step0, frac, idx, 1u) - 0.5f) * 4.4f;
        float dz0 = (SmoothHash01(step0, frac, idx, 2u) - 0.5f) * 4.4f;
        float dz1 = (SmoothHash01(step0, frac, idx, 3u) - 0.5f) * 4.4f;
        EmitFeatheredQuadXZ(
            blendVerts,
            x0 + dx0, x1 + dx1, z0 + dz0, z1 + dz1,
            2.5f, 2.5f,
            y + 0.022f + static_cast<float>(idx) * 0.004f,
            innerRgb, 46
        );
    }

    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->DrawPrimitiveUP(
        D3DPT_TRIANGLELIST,
        static_cast<UINT>(blendVerts.size() / 3u),
        blendVerts.data(),
        sizeof(FinishVert)
    );
}

static void __cdecl RenderCustomFinishPlatform_Impl(const uint8_t* finish) {
    if (g_finishPlatform == 2u) {
        RenderJavaFinishPlatform_Impl(finish);
    } else {
        RenderMobileFinishPlatform_Impl(finish);
    }
}

static void __declspec(naked) hk_finishRender() {
    __asm {
        cmp BYTE PTR g_finishPlatform, 0
        jne fr_custom
        mov eax, 0x4836e0
        jmp eax
    fr_custom:
        push ecx
        call RenderCustomFinishPlatform_Impl
        add esp, 4
        ret
    }
}

// ------------------------------------------------------------------
// 2. Win State Snapshot / Cleanup / Vanilla Suppression
// ------------------------------------------------------------------
static void SnapshotWinStart(uint8_t* game, uint8_t* cube) {
    if (game == nullptr) return;
    if (cube != nullptr && *reinterpret_cast<int32_t*>(cube) != 0) {
        return; // Ignore non-player cubes
    }
    g_winFx.active     = true;
    g_winFx.startFrame = *reinterpret_cast<uint32_t*>(game + 0x4);

    float size = 30.0f;
    if (cube != nullptr) {
        int32_t scaleInv = *reinterpret_cast<int32_t*>(cube + 0x134);
        if (scaleInv >= 0x800 && scaleInv <= 0x8000) {
            size = 122880.0f / static_cast<float>(scaleInv);
        }
    }
    g_winFx.cubeSize = size;

    uint8_t* lvl = GetLevel();
    uint8_t* fin = (lvl != nullptr) ? *reinterpret_cast<uint8_t**>(lvl) : nullptr;
    float finX = 0.0f, finY = 0.0f, finZ = 0.0f;
    bool hasFin = (fin != nullptr);
    if (hasFin) {
        finX = static_cast<float>(*reinterpret_cast<int32_t*>(fin + 0x0)) * 30.0f;
        finY = static_cast<float>(*reinterpret_cast<int32_t*>(fin + 0x4)) * 30.0f;
        finZ = static_cast<float>(*reinterpret_cast<int32_t*>(fin + 0x8)) * 30.0f;
    }

    if (cube != nullptr) {
        float px = static_cast<float>(*reinterpret_cast<int32_t*>(cube + 0x4c));
        float py = static_cast<float>(*reinterpret_cast<int32_t*>(cube + 0x50));
        float pz = static_cast<float>(*reinterpret_cast<int32_t*>(cube + 0x54));
        if (!hasFin || (px >= finX - 45.0f && px <= finX + 75.0f &&
                        py >= finY - 45.0f && py <= finY + 75.0f &&
                        pz >= finZ - 45.0f && pz <= finZ + 75.0f)) {
            g_winFx.centerX = px + size * 0.5f;
            g_winFx.bottomY = pz;
            g_winFx.centerZ = py + size * 0.5f;
            return;
        }
    }
    g_winFx.centerX = finX + 15.0f;
    g_winFx.bottomY = finZ;
    g_winFx.centerZ = finY + 15.0f;
}

static void __declspec(naked) hk_cubeOnWinGame() {
    // At 0x48e096: ecx = Game*, esi = Cube*
    __asm {
        push ecx
        push esi
        push ecx
        call SnapshotWinStart
        add esp, 8
        pop ecx
        mov eax, 0x47c200
        jmp eax
    }
}

static void __declspec(naked) hk_unloadLevelClear() {
    // At 0x47e3fc: 7B mov BYTE PTR [esi+0x2120], 0
    __asm {
        mov BYTE PTR g_winFx.active, 0
        push eax
        push ecx
        push edx
        call ResetCubeDistortionState
        call ResetMinimapCache
        call ResetSpeedometerState
        call ResetColorThemeLevelCaches
        pop edx
        pop ecx
        pop eax
        mov BYTE PTR [esi + 0x2120], 0
        mov eax, 0x47e403
        jmp eax
    }
}

static void* __cdecl SkipParticleSpawn_Impl(void** outSharedPtr) {
    if (outSharedPtr != nullptr) {
        outSharedPtr[0] = nullptr;
        outSharedPtr[1] = nullptr;
    }
    return outSharedPtr;
}

static void __declspec(naked) hk_spawnFinishParticle() {
    // At 0x47c2f1: replaces `call 0x4eb750` (__thiscall with 4 args -> ret 0x10)
    __asm {
        cmp BYTE PTR g_finishEffect, 0
        jne sfp_skip
        mov eax, 0x4eb750
        jmp eax
    sfp_skip:
        push DWORD PTR [esp + 4]
        call SkipParticleSpawn_Impl
        add esp, 4
        ret 0x10
    }
}

static void __declspec(naked) hk_playerCubeRender() {
    // At 0x47b37b: ecx = player Cube*, edi = Game*
    __asm {
        cmp BYTE PTR g_finishEffect, 0
        je pcr_orig
        test edi, edi
        je pcr_orig
        cmp BYTE PTR [edi + 0xb5], 0
        je pcr_orig
        mov eax, DWORD PTR [ecx + 0x16c]
        mov DWORD PTR [ecx + 0x16c], 0
        ret
    pcr_orig:
        jmp RenderPlayerCubeWithColorTheme
    }
}

// ------------------------------------------------------------------
// 3. Mobile (1) & Java (2) 3D Finish Effect Renderers (0x47b418)
// ------------------------------------------------------------------
static void GetCameraEyeFromDevice(IDirect3DDevice9* dev, float* outEye) {
    D3DMATRIX v{};
    if (SUCCEEDED(dev->GetTransform(D3DTS_VIEW, &v))) {
        outEye[0] = -(v._41 * v._11 + v._42 * v._12 + v._43 * v._13);
        outEye[1] = -(v._41 * v._21 + v._42 * v._22 + v._43 * v._23);
        outEye[2] = -(v._41 * v._31 + v._42 * v._32 + v._43 * v._33);
    } else {
        outEye[0] = 1000.0f;
        outEye[1] = 1000.0f;
        outEye[2] = 1000.0f;
    }
}

static void EmitBoxSlabsMobile(
    std::vector<FinishVert>& out, const float eye[3],
    float cx, float cyBottom, float cz, float w, float h, float rotY,
    uint32_t baseRgb, int alpha
) {
    float hw = w * 0.5f;
    float c = std::cos(rotY);
    float s = std::sin(rotY);

    // Local corners for [-hw, +hw] x [0, h] x [-hw, +hw] rotated around Y by rotY:
    // worldX = cx + lx*c + lz*s
    // worldY = cyBottom + ly
    // worldZ = cz - lx*s + lz*c
    auto xform = [&](float lx, float ly, float lz, float p[3]) {
        p[0] = cx + lx * c + lz * s;
        p[1] = cyBottom + ly;
        p[2] = cz - lx * s + lz * c;
    };

    struct FaceDef {
        float n0[3];
        float c0[4][3];
    };
    const FaceDef faces[5] = {
        { { 0.0f,  1.0f,  0.0f}, { {-hw, h, -hw}, {+hw, h, -hw}, {+hw, h, +hw}, {-hw, h, +hw} } }, // +Y top
        { { 0.0f,  0.0f,  1.0f}, { {-hw, 0, +hw}, {+hw, 0, +hw}, {+hw, h, +hw}, {-hw, h, +hw} } }, // +Z
        { { 0.0f,  0.0f, -1.0f}, { {+hw, 0, -hw}, {-hw, 0, -hw}, {-hw, h, -hw}, {+hw, h, -hw} } }, // -Z
        { { 1.0f,  0.0f,  0.0f}, { {+hw, 0, +hw}, {+hw, 0, -hw}, {+hw, h, -hw}, {+hw, h, +hw} } }, // +X
        { {-1.0f,  0.0f,  0.0f}, { {-hw, 0, -hw}, {-hw, 0, +hw}, {-hw, h, +hw}, {-hw, h, -hw} } }, // -X
    };

    float cyMid = cyBottom + h * 0.5f;
    for (int f = 0; f < 5; ++f) {
        float nx = faces[f].n0[0] * c + faces[f].n0[2] * s;
        float ny = faces[f].n0[1];
        float nz = -faces[f].n0[0] * s + faces[f].n0[2] * c;

        float fcx = cx + nx * hw * (1.0f - std::fabs(ny));
        float fcy = (ny > 0.5f) ? (cyBottom + h) : cyMid;
        float fcz = cz + nz * hw * (1.0f - std::fabs(ny));

        float vx = eye[0] - fcx;
        float vy = eye[1] - fcy;
        float vz = eye[2] - fcz;
        if (nx * vx + ny * vy + nz * vz <= 0.0f) continue;

        // Mobile RenderAsCube (mode 7) directional light L = (0.4327311, 0.7212185, 0.5409138) * 160
        float ldot = (std::max)(0.0f, nx * 0.4327311f + ny * 0.7212185f + nz * 0.5409138f);
        int shade = static_cast<int>(ldot * 160.0f);
        D3DCOLOR col = ShadeRgb(baseRgb, shade, alpha);

        float p0[3], p1[3], p2[3], p3[3];
        xform(faces[f].c0[0][0], faces[f].c0[0][1], faces[f].c0[0][2], p0);
        xform(faces[f].c0[1][0], faces[f].c0[1][1], faces[f].c0[1][2], p1);
        xform(faces[f].c0[2][0], faces[f].c0[2][1], faces[f].c0[2][2], p2);
        xform(faces[f].c0[3][0], faces[f].c0[3][1], faces[f].c0[3][2], p3);
        EmitQuad3D(out, p0, p1, p2, p3, col);
    }
}

static void RenderMobileFinishEffect(IDirect3DDevice9* dev, uint32_t curFrame, uint32_t theme, float t) {
    if (t >= 50.0f) return;
    float s13 = t + (t * t - t) / 10.0f;
    float hHi = s13 * 0.18f + 1.0f;
    float hMul = (s13 <= 100.0f)
        ? ((1.0f - s13 / 100.0f) * (s13 * 0.05f + 1.0f) + (s13 / 100.0f) * hHi)
        : hHi;

    float widthBase = 1.0f - 0.02f * t;
    if (widthBase <= 0.0f) return;

    float alphaF = (t <= 10.0f)
        ? 1.0f
        : std::clamp((255.0f - (t - 10.0f) * (255.0f / 55.0f)) / 255.0f, 0.0f, 1.0f);
    int alpha = static_cast<int>(std::lround(alphaF * 255.0f));
    if (alpha <= 1) return;

    float nf = (std::min)(100.0f, (hMul * 0.75f + 1.0f) * 20.0f);
    int nSlabs = static_cast<int>(std::ceil(nf));
    if (nSlabs <= 0) return;

    float totalH = hMul * g_winFx.cubeSize;
    float slabH = totalH / nf;

    uint32_t cubeRgb = (*reinterpret_cast<const uint32_t*>(0x5f914c)) & 0x00ffffffu;
    if (cubeRgb == 0u) {
        const uint32_t* pal = (theme == 3u) ? kCubePaletteDark : kCubePaletteNormal;
        int off = ((curFrame & 1u) == 0u) ? -6 : -56;
        cubeRgb = static_cast<uint32_t>(ShadeRgb(pal[curFrame % 13u], off, 0)) & 0x00ffffffu;
    }

    float eye[3]{};
    GetCameraEyeFromDevice(dev, eye);

    std::vector<FinishVert> verts;
    verts.reserve(static_cast<size_t>(nSlabs) * 18u);

    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
    float mid = 0.5f * nf;
    float shrinkLate = (t <= 10.0f) ? 0.0f : (std::min)(1.0f, (t - 10.0f) / 20.0f);
    float pinchT = (t <= 3.0f) ? 0.0f : (std::min)(1.0f, (t - 3.0f) / 20.0f);

    for (int i = 0; i < nSlabs; ++i) {
        float fi = static_cast<float>(i);
        if (fi >= nf) break;

        float frac = fi / nf;
        float s8 = frac * 100.0f;
        float angRad = ((1.0f - frac) * 500.0f + t * 15.0f) * kDeg2Rad;
        float wobble = (s8 * 0.1f) * std::cos(angRad) * (hMul / 5.0f);
        if (s13 < 40.0f) wobble *= (s13 / 40.0f);

        float midFactor = (fi <= mid)
            ? (0.5f * fi / mid)
            : (0.5f * (1.0f - (fi - mid) / mid));
        float bulge = 1.0f + (s13 * midFactor) / 100.0f;
        float wFactor = (widthBase + bulge - shrinkLate) * 0.5f;

        float yLocal = slabH * fi;
        if (yLocal < 60.0f) {
            wFactor *= (std::min)(1.0f, (yLocal / 60.0f) + (1.0f - pinchT));
        }
        if (t > 30.0f) {
            wFactor = (std::max)(0.0f, wFactor - (t - 30.0f) / 50.0f);
        }

        float w = wFactor * g_winFx.cubeSize;
        if (w <= 0.05f) continue;

        float cx = g_winFx.centerX + wobble;
        float cy = g_winFx.bottomY + yLocal;
        float cz = g_winFx.centerZ - wobble;

        float rotHi = s13 * s8 * (-0.002f);
        float rotRad = (s13 > 100.0f)
            ? rotHi
            : ((1.0f - s13 / 100.0f) * (s13 * s8 * (-0.001f)) + (s13 / 100.0f) * rotHi);

        EmitBoxSlabsMobile(verts, eye, cx, cy, cz, w, slabH * 1.03f, rotRad, cubeRgb, alpha);
    }

    if (verts.empty()) return;

    ScopedD3DState guard(dev);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, (alpha >= 253) ? TRUE : FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(verts.size() / 3u), verts.data(), sizeof(FinishVert));
}

static void RenderJavaFinishEffect(IDirect3DDevice9* dev, uint32_t theme, float t) {
    float bh = t * 0.80f;
    if (bh >= 50.0f) return;
    float scaleX = 1.0f - bh * (90.0f / 4096.0f);
    if (scaleX <= 0.0f) return;

    // Exact J2ME a.d(Graphics, int) linear vertical stretch: ((bh * 2867) % 245760) / 3 + 4096
    float scaleY = 1.0f + std::fmod(bh * 2867.0f, 245760.0f) / (3.0f * 4096.0f);

    D3DMATRIX view{};
    float rx =  0.70710678f, ry = 0.0f,        rz = -0.70710678f;
    float ux = -0.40824829f, uy = 0.81649658f, uz = -0.40824829f;
    float fx =  0.57735027f, fy = 0.57735027f, fz =  0.57735027f;
    if (SUCCEEDED(dev->GetTransform(D3DTS_VIEW, &view))) {
        rx = view._11; ry = view._21; rz = view._31;
        ux = view._12; uy = view._22; uz = view._32;
        fx = view._13; fy = view._23; fz = view._33;
    }

    float s  = g_winFx.cubeSize;
    float cX = g_winFx.centerX;
    float cY = g_winFx.bottomY + 0.5f * s;
    float cZ = g_winFx.centerZ;

    float eye[3]{};
    GetCameraEyeFromDevice(dev, eye);
    float dist = (eye[0] - cX) * fx + (eye[1] - cY) * fy + (eye[2] - cZ) * fz;
    if (dist < 100.0f) dist = 500.0f;

    float shiftF = 0.8660254f * s;
    float perspCorr = std::clamp((dist - shiftF) / dist, 0.5f, 1.0f);

    float pX = cX + fx * shiftF;
    float pY = cY + fy * shiftF;
    float pZ = cZ + fz * shiftF;

    float h0 = (s * 1.63299316f) * perspCorr; // exact 1x cube screen height along U
    float w0 = (s * 1.41421356f) * perspCorr; // exact 1x cube screen width along R

    // Pinned bottom tip (matches cube bottom vertex at t=0, stays anchored through t=57)
    float baseX = pX - ux * (0.5f * h0);
    float baseY = pY - uy * (0.5f * h0);
    float baseZ = pZ - uz * (0.5f * h0);

    float totalH = h0 * scaleY;
    float halfWFull = 0.5f * w0 * scaleX;

    // Exact J2ME palettes & per-frame strobe synced to ~24 Hz Java tick cadence
    uint32_t javaTick = g_winFx.startFrame + static_cast<uint32_t>(std::floor(bh));
    const uint32_t* pal = (theme >= 2u) ? kJavaPaletteDark : kJavaPaletteLight;
    uint32_t baseRgb = pal[javaTick % 13u];
    int strobe = ((javaTick & 1u) == 0u) ? 50 : 0;

    D3DCOLOR colTop   = ShadeRgb(baseRgb, strobe + 90, 255);
    D3DCOLOR colLeft  = ShadeRgb(baseRgb, strobe + 15, 255);
    D3DCOLOR colRight = ShadeRgb(baseRgb, strobe - 35, 255);

    constexpr int kBandSegs = 64;
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;

    std::vector<FinishVert> verts;
    verts.reserve(static_cast<size_t>(kBandSegs) * 48u);

    // Exact J2ME a.d(Graphics, int) horizontal sway for 0.spr (50x59 px idle cube):
    // angleDeg = y_spr * 5 + as * 15 = (1 - u) * 295 + bh * 15
    // wobblePx = (cos256(angleDeg) * (30 * u) * (scaleY_fp / 3)) >> 20 = 10 * u * scaleY * cos(...)
    // Divided by 50 px cube width => 0.20 * scaleY * u * w0, with a ~1.5-frame ease-in at t=0.
    float swayEaseIn = std::clamp(bh / 1.2f, 0.0f, 1.0f);
    swayEaseIn = swayEaseIn * (2.0f - swayEaseIn);

    auto wobbleAt = [&](float u) -> float {
        float angRad = ((1.0f - u) * 295.0f + bh * 15.0f) * kDeg2Rad;
        float amp = (0.20f * scaleY * swayEaseIn) * (u * w0);
        return std::cos(angRad) * amp;
    };
    auto makePt = [&](float u, float xOff, float outP[3]) {
        float h = wobbleAt(u) + xOff;
        float v = totalH * u;
        outP[0] = baseX + rx * h + ux * v;
        outP[1] = baseY + ry * h + uy * v;
        outP[2] = baseZ + rz * h + uz * v;
    };
    auto emitBandQuad = [&](float u0, float u1, float sL0, float sR0, float sL1, float sR1, D3DCOLOR col) {
        float span0 = std::fabs(sR0 - sL0);
        float span1 = std::fabs(sR1 - sL1);
        if (span0 < 1e-4f && span1 < 1e-4f) return;
        float pL0[3], pR0[3], pR1[3], pL1[3];
        makePt(u0, sL0, pL0);
        makePt(u0, sR0, pR0);
        makePt(u1, sR1, pR1);
        makePt(u1, sL1, pL1);
        if (span0 < 1e-4f) {
            verts.push_back({pL0[0], pL0[1], pL0[2], col});
            verts.push_back({pR1[0], pR1[1], pR1[2], col});
            verts.push_back({pL1[0], pL1[1], pL1[2], col});
        } else if (span1 < 1e-4f) {
            verts.push_back({pL0[0], pL0[1], pL0[2], col});
            verts.push_back({pR0[0], pR0[1], pR0[2], col});
            verts.push_back({pR1[0], pR1[1], pR1[2], col});
        } else {
            EmitQuad3D(verts, pL0, pR0, pR1, pL1, col);
        }
    };

    // Exact J2ME 0.spr uniform vertical stretch keeps cap proportions at 25% of total height
    constexpr float capFrac = 0.25f;
    float uA = capFrac;
    float uB = 1.0f - 2.0f * capFrac;
    float uC = 1.0f - capFrac;

    for (int seg = 0; seg < kBandSegs; ++seg) {
        float f0 = static_cast<float>(seg) / static_cast<float>(kBandSegs);
        float f1 = static_cast<float>(seg + 1) / static_cast<float>(kBandSegs);
        float u0 = uA * f0;
        float u1 = uA * f1;
        float hw0 = halfWFull * f0;
        float hw1 = halfWFull * f1;
        emitBandQuad(u0, u1, -hw0, 0.0f, -hw1, 0.0f, colLeft);
        emitBandQuad(u0, u1, 0.0f, hw0, 0.0f, hw1, colRight);
    }
    for (int seg = 0; seg < kBandSegs; ++seg) {
        float f0 = static_cast<float>(seg) / static_cast<float>(kBandSegs);
        float f1 = static_cast<float>(seg + 1) / static_cast<float>(kBandSegs);
        float u0 = uA + (uB - uA) * f0;
        float u1 = uA + (uB - uA) * f1;
        emitBandQuad(u0, u1, -halfWFull, 0.0f, -halfWFull, 0.0f, colLeft);
        emitBandQuad(u0, u1, 0.0f, halfWFull, 0.0f, halfWFull, colRight);
    }
    for (int seg = 0; seg < kBandSegs; ++seg) {
        float f0 = static_cast<float>(seg) / static_cast<float>(kBandSegs);
        float f1 = static_cast<float>(seg + 1) / static_cast<float>(kBandSegs);
        float u0 = uB + (uC - uB) * f0;
        float u1 = uB + (uC - uB) * f1;
        float tw0 = halfWFull * f0;
        float tw1 = halfWFull * f1;
        emitBandQuad(u0, u1, -halfWFull, -tw0, -halfWFull, -tw1, colLeft);
        emitBandQuad(u0, u1, -tw0, tw0, -tw1, tw1, colTop);
        emitBandQuad(u0, u1, tw0, halfWFull, tw1, halfWFull, colRight);
    }
    for (int seg = 0; seg < kBandSegs; ++seg) {
        float f0 = static_cast<float>(seg) / static_cast<float>(kBandSegs);
        float f1 = static_cast<float>(seg + 1) / static_cast<float>(kBandSegs);
        float u0 = uC + (1.0f - uC) * f0;
        float u1 = uC + (1.0f - uC) * f1;
        float tw0 = halfWFull * (1.0f - f0);
        float tw1 = halfWFull * (1.0f - f1);
        emitBandQuad(u0, u1, -tw0, tw0, -tw1, tw1, colTop);
    }

    if (verts.empty()) return;

    ScopedD3DState guard(dev);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(verts.size() / 3u), verts.data(), sizeof(FinishVert));
}

static void __cdecl RenderCustomFinishEffect_Impl() {
    if (g_finishEffect == 0u) return;
    uint8_t* game = GetWorldScreen();
    if (game == nullptr || game[0xb5] == 0u) return;

    IDirect3DDevice9* dev = GetD3DDevice();
    if (dev == nullptr) return;

    if (!g_winFx.active) {
        uint8_t* cube = *reinterpret_cast<uint8_t**>(game + 0x34);
        SnapshotWinStart(game, cube);
    }

    uint32_t curFrame = *reinterpret_cast<uint32_t*>(game + 0x4);
    uint32_t elapsed = (curFrame >= g_winFx.startFrame) ? (curFrame - g_winFx.startFrame) : 0u;
    float sub = IsGameplayPaused() ? 0.0f : std::clamp(g_uiAlpha, 0.0f, 1.0f);
    float t = static_cast<float>(elapsed) + sub;

    uint8_t* lvl = GetLevel();
    uint32_t theme = (lvl != nullptr) ? *reinterpret_cast<uint32_t*>(lvl + 0x6c) : 0u;

    if (g_finishEffect == 1u) {
        RenderMobileFinishEffect(dev, curFrame, theme, t);
    } else if (g_finishEffect == 2u) {
        RenderJavaFinishEffect(dev, theme, t);
    }
}

static void __declspec(naked) hk_renderBeforeSpark() {
    // At 0x47b418: replaces `call 0x4d0fa0` (before PostProcessor::apply at 0x47b434)
    __asm {
        pushad
        call RenderCustomFinishEffect_Impl
        call RenderEdgeTimer3D
        popad
        mov eax, 0x4d0fa0
        jmp eax
    }
}

} // namespace

void RegisterFinishEffects() {
    // 1. Finish::render call in WorldScreen::render (0x47b283, 5B)
    static const uint8_t exFinish[5] = {0xe8, 0x58, 0x84, 0x00, 0x00};
    RegRelCall(0x47b283, exFinish, 5, 5, &hk_finishRender);

    // 2. Cube::onWin -> Game::onWin call (0x48e096, 5B)
    static const uint8_t exOnWin[5] = {0xe8, 0x65, 0xe1, 0xfe, 0xff};
    RegRelCall(0x48e096, exOnWin, 5, 5, &hk_cubeOnWinGame);

    // 3. Game::unloadLevel levelLoaded=0 clear (0x47e3fc, 7B)
    static const uint8_t exUnload[7] = {0xc6, 0x86, 0x20, 0x21, 0x00, 0x00, 0x00};
    RegRelJmp(0x47e3fc, exUnload, 7, 7, &hk_unloadLevelClear, 0x90);

    // 4. Game::onWin "level_finished" particle spawn call (0x47c2f1, 5B)
    static const uint8_t exPart[5] = {0xe8, 0x5a, 0xf4, 0x06, 0x00};
    RegRelCall(0x47c2f1, exPart, 5, 5, &hk_spawnFinishParticle);

    // 5. WorldScreen::render player Cube::render call (0x47b37b, 5B)
    static const uint8_t exCubeRen[5] = {0xe8, 0xb0, 0x12, 0x01, 0x00};
    RegRelCall(0x47b37b, exCubeRen, 5, 5, &hk_playerCubeRender);

    // 6. WorldScreen::render before SPARK particles & PostProcessor::apply (0x47b418, 5B)
    static const uint8_t exSpark[5] = {0xe8, 0x83, 0x5b, 0x05, 0x00};
    RegRelCall(0x47b418, exSpark, 5, 5, &hk_renderBeforeSpark);
}
