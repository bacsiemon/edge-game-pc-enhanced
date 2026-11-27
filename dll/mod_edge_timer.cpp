#include "mod_state.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace edgemod;

struct TimerVert3D {
    float    x, y, z;
    D3DCOLOR diffuse;
};
constexpr DWORD kTimer3DFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;

struct TimerVert2D {
    float    x, y, z, rhw;
    D3DCOLOR diffuse;
};
constexpr DWORD kTimer2DFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

struct GlyphRect {
    uint8_t x, y, w, h;
};

// Matches PC 0x586400/0x5f3ac0 and Android 0x670669/0x670674 pixel-for-pixel
static const uint8_t kGlyphRectCounts[11] = {
    4, 2, 5, 4, 3, 5, 5, 2, 5, 5, 3
};

static const GlyphRect kGlyphRects[11][5] = {
    {{0, 0, 3, 1}, {0, 1, 1, 3}, {2, 1, 1, 3}, {0, 4, 3, 1}, {0, 0, 0, 0}}, // 0
    {{1, 0, 1, 1}, {2, 0, 1, 5}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // 1
    {{0, 0, 2, 1}, {2, 0, 1, 3}, {0, 2, 2, 1}, {0, 3, 1, 2}, {1, 4, 2, 1}}, // 2
    {{0, 0, 2, 1}, {2, 0, 1, 5}, {1, 2, 1, 1}, {0, 4, 2, 1}, {0, 0, 0, 0}}, // 3
    {{0, 0, 1, 3}, {1, 2, 1, 1}, {2, 0, 1, 5}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // 4
    {{0, 0, 3, 1}, {0, 1, 1, 1}, {0, 2, 3, 1}, {2, 3, 1, 1}, {0, 4, 3, 1}}, // 5
    {{0, 0, 1, 5}, {1, 0, 2, 1}, {1, 2, 2, 1}, {2, 3, 1, 1}, {1, 4, 2, 1}}, // 6
    {{0, 0, 3, 1}, {2, 1, 1, 4}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // 7
    {{0, 0, 1, 5}, {2, 0, 1, 5}, {1, 0, 1, 1}, {1, 2, 1, 1}, {1, 4, 1, 1}}, // 8
    {{0, 0, 1, 3}, {2, 0, 1, 5}, {1, 0, 1, 1}, {1, 2, 1, 1}, {0, 4, 2, 1}}, // 9
    {{0, 0, 1, 6}, {1, 0, 1, 5}, {2, 0, 1, 4}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // 10 (prime ')
};

struct Edge3DSlotState {
    bool    active  = false;
    uint8_t dirCode = 2;
    float   smoothX = 0.0f;
    float   smoothY = 0.0f;
    float   smoothZ = 0.0f;
};

Edge3DSlotState g_slotState[2]{};
double g_last3DRenderSec = 0.0;

static double GetQpcSec() {
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

static void ResetEdge3DState() {
    g_slotState[0].active = false;
    g_slotState[1].active = false;
    g_last3DRenderSec = 0.0;
}

struct ScopedEdgeTimer3DD3DState {
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

    explicit ScopedEdgeTimer3DD3DState(IDirect3DDevice9* d) : dev(d) {
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
        dev->SetFVF(kTimer3DFvf);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
        dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
        dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
        dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    }

    ~ScopedEdgeTimer3DD3DState() {
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

static uint8_t ResolveEdgeDirCode(const uint8_t* ws, const uint8_t* cube, int32_t slot, uint8_t prevDir) {
    int32_t axis = *reinterpret_cast<const int32_t*>(cube + 0x18c);
    int32_t dir  = *reinterpret_cast<const int32_t*>(cube + 0x190);
    if (axis != 1 && axis != 2) {
        axis = *reinterpret_cast<const int32_t*>(cube + 0x184);
        dir  = *reinterpret_cast<const int32_t*>(cube + 0x188);
    }
    if (axis == 2) {
        return (dir < 0) ? 0u : 1u;
    }
    if (axis == 1) {
        return (dir > 0) ? 2u : 3u;
    }
    if (prevDir <= 3u) {
        return prevDir;
    }
    int32_t dirFlag = *reinterpret_cast<const int32_t*>(ws + slot * 4 + 0x70);
    return (dirFlag == 0) ? 2u : 0u;
}

static void EmitLocalRect3D(
    std::vector<TimerVert3D>& out,
    const Edge3DSlotState& st,
    float lx, float ly, float lw, float lh,
    D3DCOLOR col
) {
    auto emitVert = [&](float px, float py) {
        constexpr float kScale3D = 0.7f;
        float wx, wy, wz;
        wy = st.smoothY - kScale3D * py;
        if (st.dirCode == 2u || st.dirCode == 3u) {
            wx = st.smoothX + kScale3D * px;
            wz = st.smoothZ;
        } else {
            wx = st.smoothX;
            wz = st.smoothZ - kScale3D * px;
        }
        out.push_back({wx, wy, wz, col});
    };

    emitVert(lx,      ly);
    emitVert(lx + lw, ly);
    emitVert(lx + lw, ly + lh);
    emitVert(lx,      ly);
    emitVert(lx + lw, ly + lh);
    emitVert(lx,      ly + lh);
}

static void EmitGlyph3D(
    std::vector<TimerVert3D>& shadowOut,
    std::vector<TimerVert3D>& fgOut,
    const Edge3DSlotState& st,
    int glyphIdx, float gx, float gy, float scale,
    D3DCOLOR shadowCol, D3DCOLOR fgCol
) {
    if (glyphIdx < 0 || glyphIdx > 10) return;
    int cnt = kGlyphRectCounts[glyphIdx];
    for (int i = 0; i < cnt; ++i) {
        const GlyphRect& r = kGlyphRects[glyphIdx][i];
        float rx = gx + static_cast<float>(r.x) * scale;
        float ry = gy + static_cast<float>(r.y) * scale;
        float rw = static_cast<float>(r.w) * scale;
        float rh = static_cast<float>(r.h) * scale;
        if (rw <= 0.0f || rh <= 0.0f) continue;
        EmitLocalRect3D(shadowOut, st, rx + 2.0f, ry + 2.0f, rw, rh, shadowCol);
        EmitLocalRect3D(fgOut,     st, rx,        ry,        rw, rh, fgCol);
    }
}

static const uint8_t* GetRenderCam(const uint8_t* sm) {
    if (sm == nullptr) return nullptr;
    const uint8_t* renderCam = *reinterpret_cast<const uint8_t* const*>(sm + 0x324);
    if (renderCam != nullptr) return renderCam;
    uint32_t camIdx = *reinterpret_cast<const uint32_t*>(sm + 0x330);
    if (camIdx > 2u) camIdx = 2u;
    return *reinterpret_cast<const uint8_t* const*>(sm + 0x30c + camIdx * 12);
}

static bool GetInterpolatedCubePos(const uint8_t* cube, float outPos[3]) {
    if (cube == nullptr) return false;
    uint8_t* model = *reinterpret_cast<uint8_t* const*>(cube + 0x1fc);
    if (model == nullptr) {
        model = *reinterpret_cast<uint8_t* const*>(cube + 0x1f4);
    }
    if (model == nullptr) return false;
    reinterpret_cast<float*(__thiscall*)(uint8_t*, float*)>(0x421400)(model, outPos);
    return true;
}

static void EmitRect2D(
    std::vector<TimerVert2D>& out,
    float x, float y, float w, float h,
    D3DCOLOR col
) {
    float x0 = x - 0.5f;
    float y0 = y - 0.5f;
    float x1 = x + w - 0.5f;
    float y1 = y + h - 0.5f;
    out.push_back({x0, y0, 0.0f, 1.0f, col});
    out.push_back({x1, y0, 0.0f, 1.0f, col});
    out.push_back({x1, y1, 0.0f, 1.0f, col});
    out.push_back({x0, y0, 0.0f, 1.0f, col});
    out.push_back({x1, y1, 0.0f, 1.0f, col});
    out.push_back({x0, y1, 0.0f, 1.0f, col});
}

static void EmitGlyph2D(
    std::vector<TimerVert2D>& shadowOut,
    std::vector<TimerVert2D>& fgOut,
    int glyphIdx, float gx, float gy, float scale, float shadowOff,
    D3DCOLOR shadowCol, D3DCOLOR fgCol
) {
    if (glyphIdx < 0 || glyphIdx > 10) return;
    int cnt = kGlyphRectCounts[glyphIdx];
    for (int i = 0; i < cnt; ++i) {
        const GlyphRect& r = kGlyphRects[glyphIdx][i];
        float rx = gx + static_cast<float>(r.x) * scale;
        float ry = gy + static_cast<float>(r.y) * scale;
        float rw = static_cast<float>(r.w) * scale;
        float rh = static_cast<float>(r.h) * scale;
        if (rw <= 0.0f || rh <= 0.0f) continue;
        EmitRect2D(shadowOut, rx + shadowOff, ry + shadowOff, rw, rh, shadowCol);
        EmitRect2D(fgOut,     rx,             ry,             rw, rh, fgCol);
    }
}

static bool __cdecl RenderEdgeTimer2D_Impl(uint8_t* ws, int32_t slot) {
    uint8_t mode = GetEffectiveEdgeTimerMode();
    if (mode == 2u) return true;
    if (ws == nullptr || slot < 0 || slot > 1) return false;

    uint8_t* renderer = GetRenderer();
    IDirect3DDevice9* dev = (renderer != nullptr)
        ? *reinterpret_cast<IDirect3DDevice9**>(renderer + 0xcc)
        : nullptr;
    uint8_t* sm = GetScreenMgr();
    const uint8_t* cam = GetRenderCam(sm);
    if (renderer == nullptr || dev == nullptr || sm == nullptr || cam == nullptr) return false;

    const uint8_t* cube = *reinterpret_cast<const uint8_t* const*>(ws + 0x34 + slot * 4);
    float cubePos[3]{};
    if (!GetInterpolatedCubePos(cube, cubePos)) return false;

    D3DVIEWPORT9 vpRect{};
    if (FAILED(dev->GetViewport(&vpRect)) || vpRect.Width == 0 || vpRect.Height == 0) return false;
    float vpX   = static_cast<float>(vpRect.X);
    float vpY   = static_cast<float>(vpRect.Y);
    float drawW = static_cast<float>(vpRect.Width);
    float drawH = static_cast<float>(vpRect.Height);

    const float* vp = reinterpret_cast<const float*>(cam + 0x22c);
    float cx = cubePos[0] * vp[0] + cubePos[1] * vp[4] + cubePos[2] * vp[8]  + vp[12];
    float cy = cubePos[0] * vp[1] + cubePos[1] * vp[5] + cubePos[2] * vp[9]  + vp[13];
    float cw = cubePos[0] * vp[3] + cubePos[1] * vp[7] + cubePos[2] * vp[11] + vp[15];
    if (std::fabs(cw) < 1e-5f) return false;

    float sx = vpX + drawW * ((cx + cw) / (2.0f * cw));
    float sy = vpY + drawH * (1.0f - (cy + cw) / (2.0f * cw));

    const float* proj = *reinterpret_cast<const float* const*>(cam + 0x304);
    if (proj == nullptr) return false;

    float m11 = std::fabs(proj[5]);
    float m33 = std::fabs(proj[15]);
    if (m11 < 1e-6f) return false;

    float halfWorldH = 0.0f;
    bool isOrtho = (m33 > 0.5f) || (*reinterpret_cast<const float*>(cam + 0x1c0) <= kOrthoSentinelRadThreshold);
    if (isOrtho) {
        halfWorldH = 1.0f / m11;
    } else {
        const float* eye    = reinterpret_cast<const float*>(cam + 0x108);
        const float* lookAt = reinterpret_cast<const float*>(cam + 0x208);
        float fx = lookAt[0] - eye[0];
        float fy = lookAt[1] - eye[1];
        float fz = lookAt[2] - eye[2];
        float lookDist = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (lookDist < 1.0f) return false;
        halfWorldH = lookDist * std::tan(*reinterpret_cast<const float*>(cam + 0x1c0) * 0.5f);
    }

    float worldUnitsPerDrawPx = halfWorldH / (drawH * 0.5f);
    if (worldUnitsPerDrawPx < 1e-5f) return false;

    bool isNormal = (*reinterpret_cast<const int32_t*>(cube + 0x138) == 1);
    int32_t dirFlag = *reinterpret_cast<const int32_t*>(ws + slot * 4 + 0x70);
    int32_t offXKey = isNormal
        ? ((dirFlag == 0) ? *reinterpret_cast<const int32_t*>(ws + 0x88) : *reinterpret_cast<const int32_t*>(ws + 0x8c))
        : ((dirFlag == 0) ? *reinterpret_cast<const int32_t*>(ws + 0x90) : *reinterpret_cast<const int32_t*>(ws + 0x94));
    int32_t offYKey = isNormal
        ? *reinterpret_cast<const int32_t*>(ws + 0x98)
        : *reinterpret_cast<const int32_t*>(ws + 0x9c);

    void** cfgHolder = reinterpret_cast<void**(__cdecl*)()>(0x4764c0)();
    if (cfgHolder == nullptr || *cfgHolder == nullptr) return false;
    auto getCfgFloat = reinterpret_cast<float(__thiscall*)(void*, int32_t)>(0x4408c0);
    float offsetX = getCfgFloat(*cfgHolder, offXKey);
    float offsetY = getCfgFloat(*cfgHolder, offYKey);

    float zScale = (mode == 1u) ? ComputeEdgeTimerZoomScale(ws, slot) : 1.0f;
    float u = (drawH / 450.0f) * zScale;

    float effTicks = 0.0f;
    float effRise = 0.0f;
    uint32_t rgb = 0xffffffu;
    GetEdgeTimerSmoothState(ws, slot, &effTicks, &effRise, &rgb);

    sx -= offsetX / worldUnitsPerDrawPx;
    sy -= offsetY / worldUnitsPerDrawPx;
    sy -= effRise * 0.625f * u;
    sx += ((dirFlag == 0) ? -36.0f : 17.0f) * u;

    int32_t curTicks = *reinterpret_cast<const int32_t*>(ws + slot * 4 + 0x78);
    float t = effTicks + 0.0001f;
    int32_t sec = static_cast<int32_t>(t / 22.0f);
    float rem = t - static_cast<float>(sec) * 22.0f;
    if (rem < 0.0f) rem = 0.0f;
    int32_t cs = std::clamp(static_cast<int32_t>((rem * 100.0f) / 22.0f), 0, 99);

    float bounceF = 5.0f;
    if (curTicks > 10) {
        if (rem < 1.0f)      bounceF = 8.0f;
        else if (rem < 2.0f) bounceF = 8.0f - (rem - 1.0f);
        else if (rem < 3.0f) bounceF = 7.0f - (rem - 2.0f) * 2.0f;
    }

    D3DCOLOR shadowCol = D3DCOLOR_ARGB(0xaa, 0, 0, 0);
    D3DCOLOR fgCol = D3DCOLOR_ARGB(
        0xff,
        static_cast<uint8_t>((rgb >> 16) & 0xffu),
        static_cast<uint8_t>((rgb >> 8) & 0xffu),
        static_cast<uint8_t>(rgb & 0xffu)
    );

    std::vector<TimerVert2D> shadowVerts;
    std::vector<TimerVert2D> fgVerts;
    shadowVerts.reserve(160);
    fgVerts.reserve(160);

    float shadowOff = 1.25f * u;
    float intScale  = bounceF * u;
    float digitStep = 3.0f * intScale;
    float digitY    = sy + (20.0f - 5.0f * bounceF) * u;

    EmitGlyph2D(shadowVerts, fgVerts, sec % 10, sx - 2.0f * u - digitStep, digitY, intScale, shadowOff, shadowCol, fgCol);
    if (sec >= 10) {
        EmitGlyph2D(shadowVerts, fgVerts, (sec / 10) % 10, sx - 4.0f * u - 2.0f * digitStep, digitY, intScale, shadowOff, shadowCol, fgCol);
    }
    if (sec >= 100) {
        EmitGlyph2D(shadowVerts, fgVerts, (sec / 100) % 10, sx - 6.0f * u - 3.0f * digitStep, digitY, intScale, shadowOff, shadowCol, fgCol);
    }

    if (g_timeFmt != 0) {
        float dotX = sx + 1.0f * u;
        float dotY = sy + 17.0f * u;
        float dotS = 4.0f * u;
        EmitRect2D(shadowVerts, dotX + shadowOff, dotY + shadowOff, dotS, dotS, shadowCol);
        EmitRect2D(fgVerts,     dotX,             dotY,             dotS, dotS, fgCol);
    } else {
        EmitGlyph2D(shadowVerts, fgVerts, 10, sx, sy, u, shadowOff, shadowCol, fgCol);
    }

    float csScale = 4.0f * u;
    float csY     = sy + 1.0f * u;
    EmitGlyph2D(shadowVerts, fgVerts, cs / 10, sx + 6.0f * u,  csY, csScale, shadowOff, shadowCol, fgCol);
    EmitGlyph2D(shadowVerts, fgVerts, cs % 10, sx + 21.0f * u, csY, csScale, shadowOff, shadowCol, fgCol);

    if (shadowVerts.empty() && fgVerts.empty()) return true;

    reinterpret_cast<void(__thiscall*)(void*)>(0x407410)(renderer);
    ScopedEdgeTimer3DD3DState guard(dev);
    dev->SetFVF(kTimer2DFvf);
    if (!shadowVerts.empty()) {
        dev->DrawPrimitiveUP(
            D3DPT_TRIANGLELIST,
            static_cast<UINT>(shadowVerts.size() / 3u),
            shadowVerts.data(),
            sizeof(TimerVert2D)
        );
    }
    if (!fgVerts.empty()) {
        dev->DrawPrimitiveUP(
            D3DPT_TRIANGLELIST,
            static_cast<UINT>(fgVerts.size() / 3u),
            fgVerts.data(),
            sizeof(TimerVert2D)
        );
    }
    return true;
}

static void __declspec(naked) hk_renderHudTimerCall() {
    // At 0x47b648 & 0x47b671: replaces `call 0x47ac20` (__thiscall with 1 stack arg -> ret 4)
    __asm {
        push ecx
        push DWORD PTR [esp + 8]
        push ecx
        call RenderEdgeTimer2D_Impl
        add esp, 8
        pop ecx
        test al, al
        jne rht_done
        mov eax, 0x47ac20
        jmp eax
    rht_done:
        ret 4
    }
}

} // namespace

namespace edgemod {

float __cdecl ComputeEdgeTimerZoomScale(const uint8_t* ws, int32_t slot) {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr || ws == nullptr || slot < 0 || slot > 1) return 1.0f;

    const uint8_t* cam = GetRenderCam(sm);
    if (cam == nullptr) return 1.0f;

    void** cfgHolder = reinterpret_cast<void**(__cdecl*)()>(0x4764c0)();
    float distToCube = (cfgHolder != nullptr && *cfgHolder != nullptr)
        ? reinterpret_cast<float(__thiscall*)(void*, int32_t)>(0x4408c0)(
              *cfgHolder, *reinterpret_cast<const int32_t*>(ws + 0xc)
          )
        : 0.0f;
    if (distToCube <= 1.0f) return 1.0f;

    constexpr float kSqrt3   = 1.7320508f;
    constexpr float kTan11   = 0.194380309f;
    constexpr float kRefZoom = 1.875f; // config.bcfg zoom.level_1 (standard gameplay zoom)
    float baselineHalfWorldH = kSqrt3 * distToCube * kTan11 * kRefZoom;

    const float* proj = *reinterpret_cast<const float* const*>(cam + 0x304);
    if (proj == nullptr) return 1.0f;

    float m11 = std::fabs(proj[5]);
    float m33 = std::fabs(proj[15]);
    if (m11 < 1e-6f) return 1.0f;

    float curHalfWorldH = 0.0f;
    bool isOrtho = (m33 > 0.5f) || (*reinterpret_cast<const float*>(cam + 0x1c0) <= kOrthoSentinelRadThreshold);
    if (isOrtho) {
        curHalfWorldH = 1.0f / m11;
    } else {
        const float* eye    = reinterpret_cast<const float*>(cam + 0x108);
        const float* lookAt = reinterpret_cast<const float*>(cam + 0x208);
        float fx = lookAt[0] - eye[0];
        float fy = lookAt[1] - eye[1];
        float fz = lookAt[2] - eye[2];
        float lookDist = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (lookDist < 1.0f) return 1.0f;

        float depth = lookDist;
        uint8_t* cube = *reinterpret_cast<uint8_t* const*>(ws + 0x34 + slot * 4);
        float cubePos[3]{};
        if (GetInterpolatedCubePos(cube, cubePos)) {
            float d = ((cubePos[0] - eye[0]) * fx +
                       (cubePos[1] - eye[1]) * fy +
                       (cubePos[2] - eye[2]) * fz) / lookDist;
            if (d >= 1.0f) depth = d;
        }
        curHalfWorldH = depth / m11;
    }

    if (curHalfWorldH < 1e-3f) return 1.0f;
    return std::clamp(baselineHalfWorldH / curHalfWorldH, 0.25f, 4.0f);
}

void __cdecl RenderEdgeTimer3D() {
    if (GetEffectiveEdgeTimerMode() != 2u) {
        ResetEdge3DState();
        return;
    }

    uint8_t* ws = GetWorldScreen();
    uint8_t* renderer = GetRenderer();
    IDirect3DDevice9* dev = (renderer != nullptr)
        ? *reinterpret_cast<IDirect3DDevice9**>(renderer + 0xcc)
        : nullptr;
    if (ws == nullptr || dev == nullptr || ws[0x2120] == 0u) {
        ResetEdge3DState();
        return;
    }

    uint8_t* sm = GetScreenMgr();
    bool isPaused = (sm != nullptr && *reinterpret_cast<uint32_t*>(sm + 0x50) == 0x12u);

    double nowSec = GetQpcSec();
    float dt = 0.0f;
    if (!isPaused && g_last3DRenderSec > 0.0 && nowSec > g_last3DRenderSec) {
        dt = static_cast<float>(nowSec - g_last3DRenderSec);
        if (dt > 0.1f) dt = 0.1f;
    }
    g_last3DRenderSec = nowSec;

    std::vector<TimerVert3D> shadowVerts;
    std::vector<TimerVert3D> fgVerts;
    shadowVerts.reserve(256);
    fgVerts.reserve(256);

    for (int32_t slot = 0; slot < 2; ++slot) {
        Edge3DSlotState& st = g_slotState[slot];
        bool slotActive = (slot == 0)
            ? (ws[0x6c] != 0u)
            : (ws[0x6d] != 0u && ws[0x48] != 0u);
        if (!slotActive) {
            st.active = false;
            continue;
        }

        uint8_t* cube = *reinterpret_cast<uint8_t* const*>(ws + 0x34 + slot * 4);
        float cubePos[3]{};
        if (!GetInterpolatedCubePos(cube, cubePos)) {
            st.active = false;
            continue;
        }

        int32_t curTicks = *reinterpret_cast<const int32_t*>(ws + slot * 4 + 0x78);
        if (!st.active || curTicks <= 1) {
            st.dirCode = ResolveEdgeDirCode(ws, cube, slot, 255u);
        } else {
            st.dirCode = ResolveEdgeDirCode(ws, cube, slot, st.dirCode);
        }

        float effTicks = 0.0f;
        float effRise = 0.0f;
        uint32_t rgb = 0xffffffu;
        GetEdgeTimerSmoothState(ws, slot, &effTicks, &effRise, &rgb);

        float tx = cubePos[0];
        float ty = cubePos[1] + effRise - 15.0f;
        float tz = cubePos[2];

        float dx = tx - st.smoothX;
        float dy = ty - st.smoothY;
        float dz = tz - st.smoothZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (!st.active || distSq > 120.0f * 120.0f) {
            st.smoothX = tx;
            st.smoothY = ty;
            st.smoothZ = tz;
            st.active = true;
        } else if (!isPaused && dt > 0.0f) {
            float k = 1.0f - std::pow(0.5f, dt * 30.0f);
            st.smoothX += dx * k;
            st.smoothY += dy * k;
            st.smoothZ += dz * k;
        }

        bool isNormal = (*reinterpret_cast<const int32_t*>(cube + 0x138) == 1);
        float originX = (st.dirCode == 1u || st.dirCode == 2u)
            ? (isNormal ? -90.0f : -60.0f)
            : (isNormal ?  60.0f :  30.0f);
        float originY = (st.dirCode == 1u)
            ? -60.0f
            : ((st.dirCode == 3u) ? -45.0f : 0.0f);

        float t = effTicks + 0.0001f;
        int32_t sec = static_cast<int32_t>(t / 22.0f);
        float rem = t - static_cast<float>(sec) * 22.0f;
        if (rem < 0.0f) rem = 0.0f;
        int32_t cs = std::clamp(static_cast<int32_t>((rem * 100.0f) / 22.0f), 0, 99);

        float bounceF = 5.0f;
        if (curTicks > 10) {
            if (rem < 1.0f)      bounceF = 8.0f;
            else if (rem < 2.0f) bounceF = 8.0f - (rem - 1.0f);
            else if (rem < 3.0f) bounceF = 7.0f - (rem - 2.0f) * 2.0f;
        }

        D3DCOLOR shadowCol = D3DCOLOR_ARGB(0xaa, 0, 0, 0);
        D3DCOLOR fgCol = D3DCOLOR_ARGB(
            0xff,
            static_cast<uint8_t>((rgb >> 16) & 0xffu),
            static_cast<uint8_t>((rgb >> 8) & 0xffu),
            static_cast<uint8_t>(rgb & 0xffu)
        );

        float digitStep = 3.0f * bounceF;
        float digitY = originY - 5.0f * bounceF + 20.0f;

        EmitGlyph3D(shadowVerts, fgVerts, st, sec % 10, originX - 2.0f - digitStep, digitY, bounceF, shadowCol, fgCol);
        if (sec >= 10) {
            EmitGlyph3D(shadowVerts, fgVerts, st, (sec / 10) % 10, originX - 4.0f - 2.0f * digitStep, digitY, bounceF, shadowCol, fgCol);
        }
        if (sec >= 100) {
            EmitGlyph3D(shadowVerts, fgVerts, st, (sec / 100) % 10, originX - 6.0f - 3.0f * digitStep, digitY, bounceF, shadowCol, fgCol);
        }

        if (g_timeFmt != 0) {
            float dotX = originX + 1.0f;
            float dotY = originY + 17.0f; // baseline (0,4,1,1) * 4 at originY + 1 -> y = originY + 17, size 4x4
            EmitLocalRect3D(shadowVerts, st, dotX + 2.0f, dotY + 2.0f, 4.0f, 4.0f, shadowCol);
            EmitLocalRect3D(fgVerts,     st, dotX,        dotY,        4.0f, 4.0f, fgCol);
        } else {
            EmitGlyph3D(shadowVerts, fgVerts, st, 10, originX, originY, 1.0f, shadowCol, fgCol);
        }

        EmitGlyph3D(shadowVerts, fgVerts, st, cs / 10, originX + 6.0f,  originY + 1.0f, 4.0f, shadowCol, fgCol);
        EmitGlyph3D(shadowVerts, fgVerts, st, cs % 10, originX + 21.0f, originY + 1.0f, 4.0f, shadowCol, fgCol);
    }

    if (shadowVerts.empty() && fgVerts.empty()) return;

    ScopedEdgeTimer3DD3DState guard(dev);
    if (!shadowVerts.empty()) {
        dev->DrawPrimitiveUP(
            D3DPT_TRIANGLELIST,
            static_cast<UINT>(shadowVerts.size() / 3u),
            shadowVerts.data(),
            sizeof(TimerVert3D)
        );
    }
    if (!fgVerts.empty()) {
        dev->DrawPrimitiveUP(
            D3DPT_TRIANGLELIST,
            static_cast<UINT>(fgVerts.size() / 3u),
            fgVerts.data(),
            sizeof(TimerVert3D)
        );
    }
}

} // namespace edgemod

void RegisterEdgeTimer() {
    // Skip 2D HUD timer draw calls in WorldScreen::render when g_edgeTimerMode == 2 (3D)
    static const uint8_t exHud0[5] = {0xe8, 0xd3, 0xf5, 0xff, 0xff};
    RegRelCall(0x47b648, exHud0, 5, 5, &hk_renderHudTimerCall);

    static const uint8_t exHud1[5] = {0xe8, 0xaa, 0xf5, 0xff, 0xff};
    RegRelCall(0x47b671, exHud1, 5, 5, &hk_renderHudTimerCall);
}
