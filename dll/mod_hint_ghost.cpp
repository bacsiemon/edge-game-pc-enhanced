#include "mod_state.h"
#include "mod_smooth_shared.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace edgemod;

struct GhostVert {
    float    x, y, z;
    D3DCOLOR diffuse;
};
constexpr DWORD kGhostFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;

// Active hook override when WorldScreen::render draws the Hint Ghost ([ws+0x48] == 0)
uint8_t        g_hintGhostOverrideStyle = 0; // 0=inactive, 1=mobile, 2=java
const uint8_t* g_hintGhostOverrideWs    = nullptr;

// Smooth timing & deterministic per-tick RNG state
const uint8_t* s_lastWs             = nullptr;
uint32_t       s_lastWsFrame        = 0;
uint32_t       s_prevPreviewFrame   = 0;
double         s_lastSmooth30       = 0.0;
uint32_t       s_rngState           = 0x9e3779b9u;

int32_t        s_androidLastTick     = -1;
bool           s_androidGlitch       = false;
bool           s_androidPrevGlitch   = false;
float          s_androidSx           = 1.0f;
float          s_androidSy           = 1.0f;
float          s_androidSz           = 1.0f;
float          s_androidPrevSx       = 1.0f;
float          s_androidPrevSy       = 1.0f;
float          s_androidPrevSz       = 1.0f;
float          s_androidHalo1Scale   = 1.15f;
float          s_androidHalo2Scale   = 1.07f;
float          s_androidPrevHalo1    = 1.15f;
float          s_androidPrevHalo2    = 1.07f;
uint32_t       s_androidSliceSeed    = 1u;
uint32_t       s_androidPrevSeed     = 1u;

uint32_t       s_javaLastTick        = UINT32_MAX;
bool           s_javaGlitch          = false;
float          s_javaPrevSignedGlitch = 0.0f;
float          s_javaSignedGlitch    = 0.0f;

int32_t        s_pcLastTick          = -1;
int32_t        s_pcFlickerAdd        = 0;
int32_t        s_pcPrevFlickerAdd    = 0;
uint32_t       s_pcGlitchLastTick    = UINT32_MAX;
float          s_pcPrevSignedGlitch  = 0.0f;
float          s_pcSignedGlitch      = 0.0f;

std::vector<GhostVert> g_ghostVerts;

inline uint32_t NextRand() {
    uint32_t x = s_rngState;
    if (x == 0u) x = 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rngState = x;
    return x;
}

inline uint32_t HashU32(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

inline void TransformPoint(const D3DMATRIX& m, float x, float y, float z, float out[3]) {
    out[0] = x * m._11 + y * m._21 + z * m._31 + m._41;
    out[1] = x * m._12 + y * m._22 + z * m._32 + m._42;
    out[2] = x * m._13 + y * m._23 + z * m._33 + m._43;
}

inline void TransformDir(const D3DMATRIX& m, float x, float y, float z, float out[3]) {
    out[0] = x * m._11 + y * m._21 + z * m._31;
    out[1] = x * m._12 + y * m._22 + z * m._32;
    out[2] = x * m._13 + y * m._23 + z * m._33;
}

inline float Dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline D3DCOLOR PackArgbF(float r, float g, float b, float a) {
    int ai = std::clamp(static_cast<int>(std::lround(a * 255.0f)), 0, 255);
    int ri = std::clamp(static_cast<int>(std::lround(r * 255.0f)), 0, 255);
    int gi = std::clamp(static_cast<int>(std::lround(g * 255.0f)), 0, 255);
    int bi = std::clamp(static_cast<int>(std::lround(b * 255.0f)), 0, 255);
    return D3DCOLOR_ARGB(ai, ri, gi, bi);
}

inline D3DCOLOR PackArgbU8(int a, uint32_t rgb) {
    a = std::clamp(a, 0, 255);
    return (static_cast<D3DCOLOR>(a) << 24) | (rgb & 0x00ffffffu);
}

inline float SmoothStep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline uint32_t LerpRgbU32(uint32_t c0, uint32_t c1, int pct) {
    pct = std::clamp(pct, 0, 100);
    int r0 = static_cast<int>((c0 >> 16) & 0xffu);
    int g0 = static_cast<int>((c0 >> 8) & 0xffu);
    int b0 = static_cast<int>(c0 & 0xffu);
    int r1 = static_cast<int>((c1 >> 16) & 0xffu);
    int g1 = static_cast<int>((c1 >> 8) & 0xffu);
    int b1 = static_cast<int>(c1 & 0xffu);
    int r = r0 + ((r1 - r0) * pct) / 100;
    int g = g0 + ((g1 - g0) * pct) / 100;
    int b = b0 + ((b1 - b0) * pct) / 100;
    return (static_cast<uint32_t>(std::clamp(r, 0, 255)) << 16)
         | (static_cast<uint32_t>(std::clamp(g, 0, 255)) << 8)
         | static_cast<uint32_t>(std::clamp(b, 0, 255));
}

inline uint32_t LerpRgbF(uint32_t c0, uint32_t c1, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float r0 = static_cast<float>((c0 >> 16) & 0xffu);
    float g0 = static_cast<float>((c0 >> 8) & 0xffu);
    float b0 = static_cast<float>(c0 & 0xffu);
    float r1 = static_cast<float>((c1 >> 16) & 0xffu);
    float g1 = static_cast<float>((c1 >> 8) & 0xffu);
    float b1 = static_cast<float>(c1 & 0xffu);
    int r = std::clamp(static_cast<int>(std::lround(r0 + (r1 - r0) * t)), 0, 255);
    int g = std::clamp(static_cast<int>(std::lround(g0 + (g1 - g0) * t)), 0, 255);
    int b = std::clamp(static_cast<int>(std::lround(b0 + (b1 - b0) * t)), 0, 255);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

struct ScopedGhostD3DState {
    IDirect3DDevice9* dev = nullptr;
    D3DMATRIX savedWorld{};
    DWORD rsLighting = 0, rsCull = 0, rsZEnable = 0, rsZWrite = 0;
    DWORD rsAlphaBlend = 0, rsSrcBlend = 0, rsDstBlend = 0, rsAlphaTest = 0;
    DWORD rsFog = 0, rsSpecular = 0, rsColorVert = 0, rsDiffuseSrc = 0, rsShadeMode = 0;
    DWORD tss0ColorOp = 0, tss0ColorArg1 = 0, tss0AlphaOp = 0, tss0AlphaArg1 = 0;
    DWORD tss1ColorOp = 0, tss1AlphaOp = 0;
    DWORD savedFvf = 0;
    IDirect3DBaseTexture9*  savedTex0 = nullptr;
    IDirect3DVertexShader9* savedVs   = nullptr;
    IDirect3DPixelShader9*  savedPs   = nullptr;
    bool valid = false;

    explicit ScopedGhostD3DState(IDirect3DDevice9* d) : dev(d) {
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
        dev->SetFVF(kGhostFvf);
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

    ~ScopedGhostD3DState() {
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

void UpdateSmoothGhostTime(const uint8_t* ws, bool isPreview, double* outSmooth30, float* outAlpha) {
    uint32_t wsFrame = (ws != nullptr) ? *reinterpret_cast<const uint32_t*>(ws + 0x04) : 0u;
    if (ws != s_lastWs || wsFrame < s_lastWsFrame) {
        ResetHintGhostState();
        s_lastWs = ws;
        s_lastWsFrame = wsFrame;
        s_prevPreviewFrame = wsFrame;
        s_lastSmooth30 = static_cast<double>(wsFrame);
    } else {
        if (wsFrame > s_lastWsFrame) {
            s_prevPreviewFrame = s_lastWsFrame;
        }
        s_lastWsFrame = wsFrame;
    }

    const uint8_t* sm = GetScreenMgr();
    uint32_t state = (sm != nullptr) ? *reinterpret_cast<const uint32_t*>(sm + 0x50) : 0u;
    float alpha = 0.0f;
    double smooth30 = s_lastSmooth30;
    if ((state == 0x11u || state == 0x12u) && !isPreview && ws != nullptr) {
        alpha = (state == 0x11u) ? std::clamp(g_uiAlpha, 0.0f, 1.0f) : 0.0f;
        smooth30 = static_cast<double>(GetVisualWsFrame(ws));
        s_lastSmooth30 = smooth30;
    } else if (isPreview) {
        alpha = std::clamp(g_uiAlpha, 0.0f, 1.0f);
        uint32_t baseF = (wsFrame > s_prevPreviewFrame) ? s_prevPreviewFrame : wsFrame;
        smooth30 = static_cast<double>(baseF) + static_cast<double>(alpha);
        if (smooth30 < s_lastSmooth30 && (s_lastSmooth30 - smooth30) < 1.5) {
            smooth30 = s_lastSmooth30;
        }
        s_lastSmooth30 = smooth30;
    }
    *outSmooth30 = smooth30;
    *outAlpha = alpha;
}

void ComputeGhostFadeState(
    const uint8_t* ws, bool isPreview, float alpha,
    int32_t* outFadeIn, int32_t* outFadeOut, float* outScaleFade
) {
    if (isPreview || ws == nullptr) {
        *outFadeIn = 0;
        *outFadeOut = 0;
        *outScaleFade = 1.0f;
        return;
    }
    bool visible = (ws[0x50] != 0);
    int32_t fadeIn = *reinterpret_cast<const int32_t*>(ws + 0x64);
    int32_t fadeOut = *reinterpret_cast<const int32_t*>(ws + 0x68);
    *outFadeIn = fadeIn;
    *outFadeOut = fadeOut;

    if (visible && fadeIn > 0) {
        float eff = (std::max)(0.0f, static_cast<float>(fadeIn) - alpha);
        *outScaleFade = std::clamp((6.0f - eff) / 6.0f, 0.0f, 1.0f);
    } else if (!visible && fadeOut > 0) {
        float eff = (std::max)(0.0f, static_cast<float>(fadeOut) - alpha);
        *outScaleFade = std::clamp(eff / 5.0f, 0.0f, 1.0f);
    } else {
        *outScaleFade = 1.0f;
    }
}

void EmitQuadWorld(
    std::vector<GhostVert>& out,
    const D3DMATRIX& m,
    const float p0[3], const float p1[3], const float p2[3], const float p3[3],
    D3DCOLOR col
) {
    float w0[3], w1[3], w2[3], w3[3];
    TransformPoint(m, p0[0], p0[1], p0[2], w0);
    TransformPoint(m, p1[0], p1[1], p1[2], w1);
    TransformPoint(m, p2[0], p2[1], p2[2], w2);
    TransformPoint(m, p3[0], p3[1], p3[2], w3);

    out.push_back({w0[0], w0[1], w0[2], col});
    out.push_back({w1[0], w1[1], w1[2], col});
    out.push_back({w2[0], w2[1], w2[2], col});
    out.push_back({w0[0], w0[1], w0[2], col});
    out.push_back({w2[0], w2[1], w2[2], col});
    out.push_back({w3[0], w3[1], w3[2], col});
}

// 1. Android Hint Ghost: 20-slice wavy hologram + 2 outer additive glow shells (libcgame.so 0x1c7a00..0x1c8908)
void RenderAndroidHintGhost(
    IDirect3DDevice9* dev,
    const D3DMATRIX& world,
    float minX, float maxX, float minY, float maxY, float minZ, float maxZ,
    const uint8_t* ws, bool isPreview
) {
    double smooth30 = 0.0;
    float alpha = 0.0f;
    UpdateSmoothGhostTime(ws, isPreview, &smooth30, &alpha);

    int32_t fadeIn = 0, fadeOut = 0;
    float scaleFade = 1.0f;
    ComputeGhostFadeState(ws, isPreview, alpha, &fadeIn, &fadeOut, &scaleFade);

    int32_t frame = static_cast<int32_t>(std::floor(smooth30));
    const uint8_t* sm = GetScreenMgr();
    uint32_t state = (sm != nullptr) ? *reinterpret_cast<const uint32_t*>(sm + 0x50) : 0u;
    bool canAdvanceRng = (state == 0x11u || state == 0x12u || isPreview);

    if (canAdvanceRng && frame != s_androidLastTick) {
        bool firstInit = (s_androidLastTick < 0);
        if (!firstInit) {
            s_androidPrevGlitch = s_androidGlitch;
            s_androidPrevSx     = s_androidSx;
            s_androidPrevSy     = s_androidSy;
            s_androidPrevSz     = s_androidSz;
            s_androidPrevHalo1  = s_androidHalo1Scale;
            s_androidPrevHalo2  = s_androidHalo2Scale;
            s_androidPrevSeed   = s_androidSliceSeed;
        }
        s_androidLastTick = frame;
        if (fadeIn > 0 || fadeOut > 0) {
            s_androidGlitch = false;
            s_androidSx = 1.0f;
            s_androidSy = 1.0f;
            s_androidSz = 1.0f;
        } else {
            bool diceGlitch = ((NextRand() % 10u) == 0u);
            bool modGlitch = ((frame % 50) == 0 || (frame % 51) == 0 || (frame % 52) == 0 || (frame % 53) == 0);
            s_androidGlitch = (diceGlitch || modGlitch);
            if (s_androidGlitch) {
                s_androidSx = 1.0f;
                s_androidSy = 1.0f;
                s_androidSz = 1.0f;
            } else {
                s_androidSx = ((NextRand() % 60u) == 0u) ? (0.5f + static_cast<float>(NextRand() % 90u) / 100.0f) : 1.0f;
                s_androidSy = ((NextRand() % 60u) == 0u) ? (0.5f + static_cast<float>(NextRand() % 90u) / 100.0f) : 1.0f;
                s_androidSz = ((NextRand() % 60u) == 0u) ? (0.5f + static_cast<float>(NextRand() % 90u) / 100.0f) : 1.0f;
            }
        }
        s_androidHalo1Scale = 1.14f + static_cast<float>(NextRand() % 3u) / 100.0f;
        s_androidHalo2Scale = 1.06f + static_cast<float>(NextRand() % 3u) / 100.0f;
        s_androidSliceSeed  = NextRand();
        if (firstInit) {
            s_androidPrevGlitch = s_androidGlitch;
            s_androidPrevSx     = s_androidSx;
            s_androidPrevSy     = s_androidSy;
            s_androidPrevSz     = s_androidSz;
            s_androidPrevHalo1  = s_androidHalo1Scale;
            s_androidPrevHalo2  = s_androidHalo2Scale;
            s_androidPrevSeed   = s_androidSliceSeed;
        }
    }

    float frac = static_cast<float>(smooth30 - std::floor(smooth30));
    float sFrac = SmoothStep01(frac);
    float curSx    = s_androidPrevSx + (s_androidSx - s_androidPrevSx) * sFrac;
    float curSy    = s_androidPrevSy + (s_androidSy - s_androidPrevSy) * sFrac;
    float curSz    = s_androidPrevSz + (s_androidSz - s_androidPrevSz) * sFrac;
    float curHalo1 = s_androidPrevHalo1 + (s_androidHalo1Scale - s_androidPrevHalo1) * sFrac;
    float curHalo2 = s_androidPrevHalo2 + (s_androidHalo2Scale - s_androidPrevHalo2) * sFrac;
    float glitchBlend = (s_androidPrevGlitch ? 1.0f : 0.0f) + ((s_androidGlitch ? 1.0f : 0.0f) - (s_androidPrevGlitch ? 1.0f : 0.0f)) * sFrac;

    float f10 = static_cast<float>(std::fmod(smooth30, 10.0));
    if (f10 < 0.0f) f10 += 10.0f;
    float tri = (f10 < 5.0f) ? f10 : (10.0f - f10); // 0..5..0

    float baseAlpha = (fadeIn > 0 || fadeOut > 0)
        ? (250.0f * scaleFade / 255.0f)
        : ((100.0f + 5.0f * tri) / 255.0f);

    uint32_t baseRgb = GetCleanHintGhostBaseRgb();
    float br = static_cast<float>((baseRgb >> 16) & 0xffu);
    float bg = static_cast<float>((baseRgb >> 8) & 0xffu);
    float bb = static_cast<float>(baseRgb & 0xffu);

    float rBase = (br * 2.0f + tri * 3.0f) / 255.0f;
    float gBase = (bg * 2.0f + tri * 3.0f) / 255.0f;
    float bBase = (bb * 2.1f + tri * 3.0f) / 255.0f;

    float f54 = static_cast<float>(std::fmod(smooth30, 54.0));
    if (f54 < 0.0f) f54 += 54.0f;
    float tri54 = (f54 <= 26.0f) ? f54 : (54.0f - f54);

    // Normalized Android CGeomFace::ApplyLight(0.6, 1.0, 0.75) direction
    static constexpr float kLdir[3] = {0.4327311f, 0.7212185f, 0.5409138f};
    float det3 = world._11 * (world._22 * world._33 - world._23 * world._32)
               - world._12 * (world._21 * world._33 - world._23 * world._31)
               + world._13 * (world._21 * world._32 - world._22 * world._31);
    float normalSign = (det3 < 0.0f) ? -1.0f : 1.0f;

    auto computeFaceDot = [&](float nx, float ny, float nz) -> float {
        float nw[3]{};
        TransformDir(world, nx * normalSign, ny * normalSign, nz * normalSign, nw);
        float len = std::sqrt(nw[0] * nw[0] + nw[1] * nw[1] + nw[2] * nw[2]);
        if (len < 1e-5f) return 0.0f;
        nw[0] /= len;
        nw[1] /= len;
        nw[2] /= len;
        return (std::max)(0.0f, Dot3(nw, kLdir));
    };

    float dotTop   = computeFaceDot( 0.0f,  1.0f,  0.0f);
    float dotBot   = computeFaceDot( 0.0f, -1.0f,  0.0f);
    float dotFront = computeFaceDot( 0.0f,  0.0f,  1.0f);
    float dotBack  = computeFaceDot( 0.0f,  0.0f, -1.0f);
    float dotRight = computeFaceDot( 1.0f,  0.0f,  0.0f);
    float dotLeft  = computeFaceDot(-1.0f,  0.0f,  0.0f);

    float cx = 0.5f * (minX + maxX);
    float cy = 0.5f * (minY + maxY);
    float cz = 0.5f * (minZ + maxZ);
    float hx = 0.5f * (maxX - minX);
    float hy = 0.5f * (maxY - minY);
    float hz = 0.5f * (maxZ - minZ);
    float sizeScale = (maxX - minX) / 30.0f;

    auto makeScaledMat = [&](float sx, float sy, float sz) -> D3DMATRIX {
        D3DMATRIX sm = world;
        sm._11 *= sx; sm._12 *= sx; sm._13 *= sx;
        sm._21 *= sy; sm._22 *= sy; sm._23 *= sy;
        sm._31 *= sz; sm._32 *= sz; sm._33 *= sz;
        // Keep cube center anchored at TransformPoint(world, cx, cy, cz)
        float oldC[3], newC[3];
        TransformPoint(world, cx, cy, cz, oldC);
        TransformPoint(sm, cx, cy, cz, newC);
        sm._41 += oldC[0] - newC[0];
        sm._42 += oldC[1] - newC[1];
        sm._43 += oldC[2] - newC[2];
        return sm;
    };

    g_ghostVerts.clear();
    g_ghostVerts.reserve(20 * 6 * 6);

    // Emit outer & inner glow boxes (passes 1 & 2, 10% alpha, uniform box)
    for (int haloPass = 0; haloPass < 2; ++haloPass) {
        float gs = (haloPass == 0) ? curHalo1 : curHalo2;
        D3DMATRIX hm = makeScaledMat(curSx * gs, curSy * scaleFade * gs, curSz * gs);
        float haloAlpha = baseAlpha * 0.10f;
        float x0 = cx - hx, x1 = cx + hx;
        float y0 = cy - hy, y1 = cy + hy;
        float z0 = cz - hz, z1 = cz + hz;

        float p000[3] = {x0, y0, z0}, p100[3] = {x1, y0, z0}, p101[3] = {x1, y0, z1}, p001[3] = {x0, y0, z1};
        float p010[3] = {x0, y1, z0}, p110[3] = {x1, y1, z0}, p111[3] = {x1, y1, z1}, p011[3] = {x0, y1, z1};

        D3DCOLOR cTop   = PackArgbF(rBase * dotTop,   gBase * dotTop,   bBase * dotTop,   haloAlpha);
        D3DCOLOR cBot   = PackArgbF(rBase * dotBot,   gBase * dotBot,   bBase * dotBot,   haloAlpha);
        D3DCOLOR cFront = PackArgbF(rBase * dotFront, gBase * dotFront, bBase * dotFront, haloAlpha);
        D3DCOLOR cBack  = PackArgbF(rBase * dotBack,  gBase * dotBack,  bBase * dotBack,  haloAlpha);
        D3DCOLOR cRight = PackArgbF(rBase * dotRight, gBase * dotRight, bBase * dotRight, haloAlpha);
        D3DCOLOR cLeft  = PackArgbF(rBase * dotLeft,  gBase * dotLeft,  bBase * dotLeft,  haloAlpha);

        EmitQuadWorld(g_ghostVerts, hm, p011, p111, p110, p010, cTop);
        EmitQuadWorld(g_ghostVerts, hm, p000, p100, p101, p001, cBot);
        EmitQuadWorld(g_ghostVerts, hm, p001, p101, p111, p011, cFront);
        EmitQuadWorld(g_ghostVerts, hm, p100, p000, p010, p110, cBack);
        EmitQuadWorld(g_ghostVerts, hm, p101, p100, p110, p111, cRight);
        EmitQuadWorld(g_ghostVerts, hm, p000, p001, p011, p010, cLeft);
    }

    // Pass 3: 20 wavy horizontal slices
    D3DMATRIX mainMat = makeScaledMat(curSx, curSy * scaleFade, curSz);
    constexpr float kPi      = 3.14159265358979323846f;
    constexpr float kDeg2Rad = kPi / 180.0f;
    float m100f = static_cast<float>(std::fmod(smooth30, 100.0));
    if (m100f < 0.0f) m100f += 100.0f;

    float xWeight = 1.0f;
    if (m100f >= 84.0f && m100f < 85.0f) {
        xWeight = 1.0f - SmoothStep01(m100f - 84.0f);
    } else if (m100f >= 85.0f && m100f < 99.0f) {
        xWeight = 0.0f;
    } else if (m100f >= 99.0f) {
        xWeight = SmoothStep01(m100f - 99.0f);
    }

    float zWeight = 1.0f;
    if (m100f >= 69.0f && m100f < 70.0f) {
        zWeight = 1.0f - SmoothStep01(m100f - 69.0f);
    } else if (m100f >= 70.0f && m100f < 84.0f) {
        zWeight = 0.0f;
    } else if (m100f >= 84.0f && m100f < 85.0f) {
        zWeight = SmoothStep01(m100f - 84.0f);
    }

    auto computeSparkle = [](int32_t f, uint32_t seed, int sliceIdx, float* outRgbAdd, float* outAAdd) {
        *outRgbAdd = 0.0f;
        *outAAdd   = 0.0f;
        int f50 = f % 50;
        if (f50 < 0) f50 += 50;
        if (f50 <= 24) {
            uint32_t h0 = HashU32(seed ^ static_cast<uint32_t>(sliceIdx * 97 + 13));
            if ((h0 % 30u) == 0u) {
                uint32_t h1 = HashU32(h0 + 1u);
                *outRgbAdd = 20.0f / 255.0f;
                *outAAdd   = static_cast<float>(10u + (h1 % 20u)) / 255.0f;
            }
        }
    };

    for (int i = 0; i < 20; ++i) {
        float sr = rBase;
        float sg = gBase;
        float sb = bBase;
        float sa = baseAlpha * (0.9f - 0.1f * std::cos(kPi * (static_cast<float>(i) + static_cast<float>(smooth30))));

        float m50f = static_cast<float>(std::fmod(static_cast<double>(i) + smooth30, 50.0));
        if (m50f < 0.0f) m50f += 50.0f;
        if (m50f < 15.0f) {
            float edgeFade = std::clamp(m50f, 0.0f, 1.0f) * std::clamp(15.0f - m50f, 0.0f, 1.0f);
            float kf = (m50f * static_cast<float>(i)) / 15.0f;
            float rgbAdd = ((kf * 3.0f + 10.0f) / 255.0f) * edgeFade;
            float aAdd   = ((kf * 2.5f) / 255.0f) * edgeFade;
            sr += rgbAdd;
            sg += rgbAdd;
            sb += rgbAdd;
            sa += aAdd;
        }

        float prevSpRgb = 0.0f, prevSpA = 0.0f, curSpRgb = 0.0f, curSpA = 0.0f;
        computeSparkle(frame - 1, s_androidPrevSeed, i, &prevSpRgb, &prevSpA);
        computeSparkle(frame,     s_androidSliceSeed, i, &curSpRgb,  &curSpA);
        float spRgb = prevSpRgb + (curSpRgb - prevSpRgb) * frac;
        float spA   = prevSpA   + (curSpA   - prevSpA)   * frac;
        sr += spRgb;
        sg += spRgb;
        sb += spRgb;
        sa += spA;

        sr = (std::min)(sr, 1.0f);
        sg = (std::min)(sg, 1.0f);
        sb = (std::min)(sb, 1.0f);
        sa = (std::min)(sa, 1.0f);

        double angDeg = smooth30 * 25.0 + static_cast<double>(i * 15);
        float cVal = static_cast<float>(std::cos(angDeg * kDeg2Rad) * 256.0);
        float w = static_cast<float>((i * 30) & 0xfffe) / 20.0f;
        float glitchOff = cVal * w * (1365.0f / 1048576.0f);
        float calmOff   = (cVal * w / 2048.0f) * (tri54 / 27.0f);
        float rawOff = calmOff + (glitchOff - calmOff) * glitchBlend;
        float off = rawOff * 0.5f * sizeScale;

        float sxOff = -off * xWeight;
        float szOff = off * zWeight;

        float y0 = minY + (maxY - minY) * (static_cast<float>(i) / 20.0f);
        float y1 = minY + (maxY - minY) * (static_cast<float>(i + 1) / 20.0f);
        float x0 = minX + sxOff, x1 = maxX + sxOff;
        float z0 = minZ + szOff, z1 = maxZ + szOff;

        float p000[3] = {x0, y0, z0}, p100[3] = {x1, y0, z0}, p101[3] = {x1, y0, z1}, p001[3] = {x0, y0, z1};
        float p010[3] = {x0, y1, z0}, p110[3] = {x1, y1, z0}, p111[3] = {x1, y1, z1}, p011[3] = {x0, y1, z1};

        D3DCOLOR cFront = PackArgbF(sr * dotFront, sg * dotFront, sb * dotFront, sa);
        D3DCOLOR cBack  = PackArgbF(sr * dotBack,  sg * dotBack,  sb * dotBack,  sa);
        D3DCOLOR cRight = PackArgbF(sr * dotRight, sg * dotRight, sb * dotRight, sa);
        D3DCOLOR cLeft  = PackArgbF(sr * dotLeft,  sg * dotLeft,  sb * dotLeft,  sa);

        if (i == 0) {
            D3DCOLOR cBot = PackArgbF(sr * dotBot, sg * dotBot, sb * dotBot, sa);
            EmitQuadWorld(g_ghostVerts, mainMat, p000, p100, p101, p001, cBot);
        }
        EmitQuadWorld(g_ghostVerts, mainMat, p001, p101, p111, p011, cFront);
        EmitQuadWorld(g_ghostVerts, mainMat, p100, p000, p010, p110, cBack);
        EmitQuadWorld(g_ghostVerts, mainMat, p101, p100, p110, p111, cRight);
        EmitQuadWorld(g_ghostVerts, mainMat, p000, p001, p011, p010, cLeft);
        if (i == 19) {
            D3DCOLOR cTop = PackArgbF(sr * dotTop, sg * dotTop, sb * dotTop, sa);
            EmitQuadWorld(g_ghostVerts, mainMat, p011, p111, p110, p010, cTop);
        }
    }

    if (g_ghostVerts.empty()) return;

    ScopedGhostD3DState guard(dev);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->DrawPrimitiveUP(
        D3DPT_TRIANGLELIST,
        static_cast<UINT>(g_ghostVerts.size() / 3u),
        g_ghostVerts.data(),
        sizeof(GhostVert)
    );
}

// 2. Java Hint Ghost: 22.22 Hz interlaced scanline bands + random wave glitch + horizontal fade squash
void RenderJavaHintGhost(
    IDirect3DDevice9* dev,
    const D3DMATRIX& world,
    float minX, float maxX, float minY, float maxY, float minZ, float maxZ,
    const uint8_t* ws, bool isPreview
) {
    double smooth30 = 0.0;
    float alpha = 0.0f;
    UpdateSmoothGhostTime(ws, isPreview, &smooth30, &alpha);

    double javaTime = smooth30 / 1.35;
    uint32_t javaTick = static_cast<uint32_t>(std::floor(javaTime));

    int32_t fadeIn = 0, fadeOut = 0;
    float scaleFade = 1.0f;
    ComputeGhostFadeState(ws, isPreview, alpha, &fadeIn, &fadeOut, &scaleFade);

    const uint8_t* sm = GetScreenMgr();
    uint32_t state = (sm != nullptr) ? *reinterpret_cast<const uint32_t*>(sm + 0x50) : 0u;
    if ((state == 0x11u || state == 0x12u || isPreview) && javaTick != s_javaLastTick) {
        bool firstInit = (s_javaLastTick == UINT32_MAX);
        s_javaPrevSignedGlitch = s_javaSignedGlitch;
        s_javaLastTick = javaTick;
        if (fadeIn > 0 || fadeOut > 0) {
            s_javaGlitch = false;
            s_javaSignedGlitch = 0.0f;
        } else {
            bool rngHit = ((NextRand() % 10u) == 0u);
            bool modHit = ((javaTick % 50u) == 0u || (javaTick % 51u) == 0u || (javaTick % 52u) == 0u || (javaTick % 53u) == 0u);
            s_javaGlitch = (rngHit || modHit);
            s_javaSignedGlitch = s_javaGlitch ? (((javaTick % 3u) == 0u) ? -1.0f : 1.0f) : 0.0f;
        }
        if (firstInit) {
            s_javaPrevSignedGlitch = s_javaSignedGlitch;
        }
    }
    float javaFrac = static_cast<float>(javaTime - std::floor(javaTime));
    float signedGlitch = s_javaPrevSignedGlitch + (s_javaSignedGlitch - s_javaPrevSignedGlitch) * SmoothStep01(javaFrac);

    D3DMATRIX view{}, proj{};
    if (FAILED(dev->GetTransform(D3DTS_VIEW, &view))) return;
    if (FAILED(dev->GetTransform(D3DTS_PROJECTION, &proj))) return;

    float R[3] = {view._11, view._21, view._31};
    float U[3] = {view._12, view._22, view._32};
    float F[3] = {view._13, view._23, view._33};
    float eye[3] = {
        -(view._41 * view._11 + view._42 * view._12 + view._43 * view._13),
        -(view._41 * view._21 + view._42 * view._22 + view._43 * view._23),
        -(view._41 * view._31 + view._42 * view._32 + view._43 * view._33),
    };
    bool isOrtho = (std::fabs(proj._44 - 1.0f) < 0.01f);

    float uMin = 1e30f, uMax = -1e30f;
    float rMin = 1e30f, rMax = -1e30f;
    for (int ci = 0; ci < 8; ++ci) {
        float lx = (ci & 1) ? maxX : minX;
        float ly = (ci & 2) ? maxY : minY;
        float lz = (ci & 4) ? maxZ : minZ;
        float pw[3]{};
        TransformPoint(world, lx, ly, lz, pw);
        float uProj = Dot3(pw, U);
        float rProj = Dot3(pw, R);
        if (uProj < uMin) uMin = uProj;
        if (uProj > uMax) uMax = uProj;
        if (rProj < rMin) rMin = rProj;
        if (rProj > rMax) rMax = rProj;
    }
    float cubeScreenH = uMax - uMin;
    float cubeScreenW = rMax - rMin;
    if (cubeScreenH < 1e-4f || cubeScreenW < 1e-4f) return;

    float cWorld[3]{};
    TransformPoint(world, 0.5f * (minX + maxX), 0.5f * (minY + maxY), 0.5f * (minZ + maxZ), cWorld);
    float cR = Dot3(cWorld, R);

    // Exact Java 0.spr Top/Left/Right colors + white strobe (javaTick % 5) * 2%
    const uint8_t* lvl = GetLevel();
    int theme = (lvl != nullptr) ? (lvl[0x6c] & 3) : 0;
    bool isRainbow = (ws != nullptr && ws[0xb5] != 0);
    static const int kJavaMid[4]        = { 9,  8, 11, 10};
    static const int kJavaDeltaTop[4]   = { 5,  0, 27, 18};
    static const int kJavaDeltaRight[4] = {-50, -60, -36, -40};

    const uint32_t* pal = reinterpret_cast<const uint32_t*>(0x5f9110);
    uint32_t midCol = pal[isRainbow ? 11 : kJavaMid[theme]];
    int mr = static_cast<int>((midCol >> 16) & 0xffu);
    int mg = static_cast<int>((midCol >> 8) & 0xffu);
    int mb = static_cast<int>(midCol & 0xffu);
    int dTop   = isRainbow ? 27  : kJavaDeltaTop[theme];
    int dRight = isRainbow ? -42 : kJavaDeltaRight[theme];

    auto makeRgb = [](int r, int g, int b) -> uint32_t {
        return (static_cast<uint32_t>(std::clamp(r, 0, 255)) << 16)
             | (static_cast<uint32_t>(std::clamp(g, 0, 255)) << 8)
             | static_cast<uint32_t>(std::clamp(b, 0, 255));
    };
    float s5 = static_cast<float>(std::fmod(javaTime, 5.0));
    if (s5 < 0.0f) s5 += 5.0f;
    float strobeT = (s5 <= 4.0f) ? (s5 * 0.02f) : ((5.0f - s5) * 0.08f);
    uint32_t rgbTop   = LerpRgbF(makeRgb(mr + dTop, mg + dTop, mb + dTop), 0xffffffu, strobeT);
    uint32_t rgbLeft  = LerpRgbF(midCol, 0xffffffu, strobeT);
    uint32_t rgbRight = LerpRgbF(makeRgb(mr + dRight, mg + dRight, mb + dRight), 0xffffffu, strobeT);

    float scaleX = (fadeIn > 0 || fadeOut > 0) ? scaleFade : 1.0f;
    float glitchPhaseDeg = static_cast<float>(std::fmod(javaTime * 15.0, 360.0));
    float t3 = static_cast<float>(std::fmod(javaTime, 3.0));
    if (t3 < 0.0f) t3 += 3.0f;
    float evenA = 1.0f, oddA = 1.0f;
    if (t3 < 1.0f) {
        float u = SmoothStep01(t3);
        evenA = u;
        oddA  = 1.0f - u;
    } else if (t3 < 2.0f) {
        float u = SmoothStep01(t3 - 1.0f);
        evenA = 1.0f;
        oddA  = u;
    } else {
        float u = SmoothStep01(t3 - 2.0f);
        evenA = 1.0f - u;
        oddA  = 1.0f;
    }

    float det3 = world._11 * (world._22 * world._33 - world._23 * world._32)
               - world._12 * (world._21 * world._33 - world._23 * world._31)
               + world._13 * (world._21 * world._32 - world._22 * world._31);
    float normalSign = (det3 < 0.0f) ? -1.0f : 1.0f;

    struct BoxFace {
        float org[3], uVec[3], vVec[3], nrm[3];
    };
    float dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
    const BoxFace faces[6] = {
        {{minX, minY, maxZ}, { dx, 0.f, 0.f}, {0.f,  dy, 0.f}, { 0.f,  0.f,  1.f}}, // +Z
        {{maxX, minY, minZ}, {-dx, 0.f, 0.f}, {0.f,  dy, 0.f}, { 0.f,  0.f, -1.f}}, // -Z
        {{maxX, minY, maxZ}, {0.f, 0.f, -dz}, {0.f,  dy, 0.f}, { 1.f,  0.f,  0.f}}, // +X
        {{minX, minY, minZ}, {0.f, 0.f,  dz}, {0.f,  dy, 0.f}, {-1.f,  0.f,  0.f}}, // -X
        {{minX, maxY, maxZ}, { dx, 0.f, 0.f}, {0.f, 0.f, -dz}, { 0.f,  1.f,  0.f}}, // +Y
        {{minX, minY, minZ}, { dx, 0.f, 0.f}, {0.f, 0.f,  dz}, { 0.f, -1.f,  0.f}}, // -Y
    };

    constexpr int kBands = 30;
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
    g_ghostVerts.clear();
    g_ghostVerts.reserve(3 * kBands * 6);

    for (int f = 0; f < 6; ++f) {
        const BoxFace& bf = faces[f];
        float fcLoc[3] = {
            bf.org[0] + 0.5f * (bf.uVec[0] + bf.vVec[0]),
            bf.org[1] + 0.5f * (bf.uVec[1] + bf.vVec[1]),
            bf.org[2] + 0.5f * (bf.uVec[2] + bf.vVec[2]),
        };
        float fcW[3]{};
        TransformPoint(world, fcLoc[0], fcLoc[1], fcLoc[2], fcW);

        float nw[3]{};
        TransformDir(world, bf.nrm[0] * normalSign, bf.nrm[1] * normalSign, bf.nrm[2] * normalSign, nw);
        float nLen = std::sqrt(nw[0] * nw[0] + nw[1] * nw[1] + nw[2] * nw[2]);
        if (nLen < 1e-5f) continue;
        nw[0] /= nLen;
        nw[1] /= nLen;
        nw[2] /= nLen;

        float facing = isOrtho
            ? Dot3(nw, F)
            : (nw[0] * (eye[0] - fcW[0]) + nw[1] * (eye[1] - fcW[1]) + nw[2] * (eye[2] - fcW[2]));
        if (facing <= 1e-4f) continue;

        uint32_t faceRgb = rgbLeft;
        if (nw[1] > 0.55f) {
            faceRgb = rgbTop;
        } else if (std::fabs(nw[0]) >= std::fabs(nw[2])) {
            faceRgb = rgbRight;
        }

        // Clip face polygon against horizontal screen-U scanline bands [uBand0, uBand1]
        float corners[4][3]{};
        TransformPoint(world, bf.org[0], bf.org[1], bf.org[2], corners[0]);
        TransformPoint(world, bf.org[0] + bf.uVec[0], bf.org[1] + bf.uVec[1], bf.org[2] + bf.uVec[2], corners[1]);
        TransformPoint(world, bf.org[0] + bf.uVec[0] + bf.vVec[0], bf.org[1] + bf.uVec[1] + bf.vVec[1], bf.org[2] + bf.uVec[2] + bf.vVec[2], corners[2]);
        TransformPoint(world, bf.org[0] + bf.vVec[0], bf.org[1] + bf.vVec[1], bf.org[2] + bf.vVec[2], corners[3]);

        float cornersU[4] = {
            Dot3(corners[0], U), Dot3(corners[1], U), Dot3(corners[2], U), Dot3(corners[3], U)
        };

        auto deformPoint = [&](float pw[3], float uNorm, float out[3]) {
            float dxWorld = 0.0f;
            if (std::fabs(signedGlitch) > 1e-3f) {
                float ySpr = (1.0f - uNorm) * 59.0f;
                float angRad = (ySpr * 10.0f + glitchPhaseDeg) * kDeg2Rad;
                float dxPx = signedGlitch * std::cos(angRad) * (uNorm * 8.0f);
                dxWorld = (dxPx / 50.0f) * cubeScreenW;
            }
            float rOff = Dot3(pw, R) - cR;
            float shiftR = rOff * (scaleX - 1.0f) + dxWorld;
            out[0] = pw[0] + R[0] * shiftR;
            out[1] = pw[1] + R[1] * shiftR;
            out[2] = pw[2] + R[2] * shiftR;
        };

        for (int band = 0; band < kBands; ++band) {
            float bandA = ((band % 2) == 0) ? evenA : oddA;
            if (bandA <= 0.004f) continue;
            D3DCOLOR bandCol = PackArgbU8(static_cast<int>(std::lround(bandA * 255.0f)), faceRgb);

            float uNorm0 = static_cast<float>(band) / static_cast<float>(kBands);
            float uNorm1 = static_cast<float>(band + 1) / static_cast<float>(kBands);
            float uLo = uMin + cubeScreenH * uNorm0;
            float uHi = uMin + cubeScreenH * uNorm1;

            // Sutherland-Hodgman clip of quad against u >= uLo and u <= uHi
            float polyA[8][3]{}, polyB[8][3]{};
            float polyAU[8]{}, polyBU[8]{};
            int cntA = 4;
            for (int k = 0; k < 4; ++k) {
                std::memcpy(polyA[k], corners[k], 12);
                polyAU[k] = cornersU[k];
            }

            auto clipPlane = [](
                const float inP[8][3], const float inU[8], int inCnt,
                float outP[8][3], float outU[8], float bound, bool keepGreater
            ) -> int {
                int outCnt = 0;
                for (int i = 0; i < inCnt; ++i) {
                    int j = (i + 1) % inCnt;
                    float da = keepGreater ? (inU[i] - bound) : (bound - inU[i]);
                    float db = keepGreater ? (inU[j] - bound) : (bound - inU[j]);
                    bool inA = (da >= -1e-5f);
                    bool inB = (db >= -1e-5f);
                    if (inA && inB) {
                        std::memcpy(outP[outCnt], inP[j], 12);
                        outU[outCnt++] = inU[j];
                    } else if (inA && !inB) {
                        float t = da / (da - db);
                        for (int c = 0; c < 3; ++c) outP[outCnt][c] = inP[i][c] + (inP[j][c] - inP[i][c]) * t;
                        outU[outCnt++] = inU[i] + (inU[j] - inU[i]) * t;
                    } else if (!inA && inB) {
                        float t = da / (da - db);
                        for (int c = 0; c < 3; ++c) outP[outCnt][c] = inP[i][c] + (inP[j][c] - inP[i][c]) * t;
                        outU[outCnt++] = inU[i] + (inU[j] - inU[i]) * t;
                        std::memcpy(outP[outCnt], inP[j], 12);
                        outU[outCnt++] = inU[j];
                    }
                }
                return outCnt;
            };

            int cntB = clipPlane(polyA, polyAU, cntA, polyB, polyBU, uLo, true);
            if (cntB < 3) continue;
            int cntOut = clipPlane(polyB, polyBU, cntB, polyA, polyAU, uHi, false);
            if (cntOut < 3) continue;

            float defPts[8][3]{};
            for (int k = 0; k < cntOut; ++k) {
                float uN = std::clamp((polyAU[k] - uMin) / cubeScreenH, 0.0f, 1.0f);
                deformPoint(polyA[k], uN, defPts[k]);
            }
            for (int k = 1; k + 1 < cntOut; ++k) {
                g_ghostVerts.push_back({defPts[0][0], defPts[0][1], defPts[0][2], bandCol});
                g_ghostVerts.push_back({defPts[k][0], defPts[k][1], defPts[k][2], bandCol});
                g_ghostVerts.push_back({defPts[k + 1][0], defPts[k + 1][1], defPts[k + 1][2], bandCol});
            }
        }
    }

    if (g_ghostVerts.empty()) return;

    ScopedGhostD3DState guard(dev);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->DrawPrimitiveUP(
        D3DPT_TRIANGLELIST,
        static_cast<UINT>(g_ghostVerts.size() / 3u),
        g_ghostVerts.data(),
        sizeof(GhostVert)
    );
}

// 3. PC Hint Ghost preview (used in Main Menu Graphics preview at (5, 7, 1) when hintGhost == pc)
void RenderPcPreviewHintGhost(
    IDirect3DDevice9* dev,
    const D3DMATRIX& world,
    float minX, float maxX, float minY, float maxY, float minZ, float maxZ,
    const uint8_t* ws
) {
    double smooth30 = 0.0;
    float alpha = 0.0f;
    UpdateSmoothGhostTime(ws, true, &smooth30, &alpha);

    int32_t frame = static_cast<int32_t>(std::floor(smooth30));
    if (frame != s_pcLastTick) {
        bool firstInit = (s_pcLastTick < 0);
        s_pcPrevFlickerAdd = s_pcFlickerAdd;
        s_pcLastTick = frame;
        if ((frame % 50) < 25 && (NextRand() % 30u) == 0u) {
            s_pcFlickerAdd = 10 + static_cast<int32_t>(NextRand() % 20u);
        } else {
            s_pcFlickerAdd = 0;
        }
        if (firstInit) {
            s_pcPrevFlickerAdd = s_pcFlickerAdd;
        }
    }

    float frac = static_cast<float>(smooth30 - std::floor(smooth30));
    float flickerAdd = static_cast<float>(s_pcPrevFlickerAdd) + static_cast<float>(s_pcFlickerAdd - s_pcPrevFlickerAdd) * frac;
    float f10 = static_cast<float>(std::fmod(smooth30, 10.0));
    if (f10 < 0.0f) f10 += 10.0f;
    float tri = (f10 < 5.0f) ? f10 : (10.0f - f10);
    int pcAlpha = std::clamp(static_cast<int>(std::lround((150.0f - 5.0f * tri + flickerAdd) * 0.5f)), 40, 220);

    uint32_t baseRgb = GetCleanHintGhostBaseRgb();
    float br = static_cast<float>((baseRgb >> 16) & 0xffu) / 255.0f;
    float bg = static_cast<float>((baseRgb >> 8) & 0xffu) / 255.0f;
    float bb = static_cast<float>(baseRgb & 0xffu) / 255.0f;

    // Match PC Light 0 & D3DRS_AMBIENT (0x80808080 -> 2 * (0.5 * 223/255 + base * dot * 205/255))
    static constexpr float kPcLdir[3] = {0.4327311f, 0.7212185f, 0.5409138f};
    auto litFaceCol = [&](float nx, float ny, float nz) -> D3DCOLOR {
        float dot = (std::max)(0.0f, nx * kPcLdir[0] + ny * kPcLdir[1] + nz * kPcLdir[2]);
        float amb = 223.0f / 255.0f;
        float diff = dot * (205.0f / 255.0f) * 2.0f;
        float r = std::clamp(amb * br + br * diff, 0.0f, 1.0f);
        float g = std::clamp(amb * bg + bg * diff, 0.0f, 1.0f);
        float b = std::clamp(amb * bb + bb * diff, 0.0f, 1.0f);
        return PackArgbF(r, g, b, static_cast<float>(pcAlpha) / 255.0f);
    };

    D3DMATRIX view{}, proj{};
    if (FAILED(dev->GetTransform(D3DTS_VIEW, &view))) return;
    if (FAILED(dev->GetTransform(D3DTS_PROJECTION, &proj))) return;
    float R[3] = {view._11, view._21, view._31};
    float U[3] = {view._12, view._22, view._32};

    bool applyDistort = (GetEffectiveTheme().cubeDistortion != 0u);
    double javaTime = smooth30 / 1.35;
    uint32_t javaTick = static_cast<uint32_t>(std::floor(javaTime));
    if (javaTick != s_pcGlitchLastTick) {
        bool firstInit = (s_pcGlitchLastTick == UINT32_MAX);
        s_pcPrevSignedGlitch = s_pcSignedGlitch;
        s_pcGlitchLastTick = javaTick;
        bool rngHit = ((NextRand() % 10u) == 0u);
        bool modHit = ((javaTick % 50u) == 0u || (javaTick % 51u) == 0u || (javaTick % 52u) == 0u || (javaTick % 53u) == 0u);
        s_pcSignedGlitch = (rngHit || modHit) ? (((javaTick % 3u) == 0u) ? -1.0f : 1.0f) : 0.0f;
        if (firstInit) {
            s_pcPrevSignedGlitch = s_pcSignedGlitch;
        }
    }
    float javaFrac = static_cast<float>(javaTime - std::floor(javaTime));
    float signedGlitch = s_pcPrevSignedGlitch + (s_pcSignedGlitch - s_pcPrevSignedGlitch) * SmoothStep01(javaFrac);

    float uMin = 1e30f, uMax = -1e30f;
    float rMin = 1e30f, rMax = -1e30f;
    for (int ci = 0; ci < 8; ++ci) {
        float lx = (ci & 1) ? maxX : minX;
        float ly = (ci & 2) ? maxY : minY;
        float lz = (ci & 4) ? maxZ : minZ;
        float pw[3]{};
        TransformPoint(world, lx, ly, lz, pw);
        float uProj = Dot3(pw, U);
        float rProj = Dot3(pw, R);
        if (uProj < uMin) uMin = uProj;
        if (uProj > uMax) uMax = uProj;
        if (rProj < rMin) rMin = rProj;
        if (rProj > rMax) rMax = rProj;
    }
    float cubeScreenH = (std::max)(1e-4f, uMax - uMin);
    float cubeScreenW = (std::max)(1e-4f, rMax - rMin);

    struct BoxFace {
        float org[3], uVec[3], vVec[3], nrm[3];
    };
    float dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
    const BoxFace faces[3] = {
        {{minX, maxY, maxZ}, { dx, 0.f, 0.f}, {0.f, 0.f, -dz}, { 0.f, 1.f, 0.f}}, // +Y top
        {{minX, minY, maxZ}, { dx, 0.f, 0.f}, {0.f,  dy, 0.f}, { 0.f, 0.f, 1.f}}, // +Z front-left
        {{maxX, minY, maxZ}, {0.f, 0.f, -dz}, {0.f,  dy, 0.f}, { 1.f, 0.f, 0.f}}, // +X front-right
    };

    constexpr int kGrid = 16;
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
    float glitchPhaseDeg = static_cast<float>(std::fmod(javaTime * 15.0, 360.0));

    g_ghostVerts.clear();
    g_ghostVerts.reserve(3 * kGrid * kGrid * 6);

    for (int f = 0; f < 3; ++f) {
        const BoxFace& bf = faces[f];
        D3DCOLOR col = litFaceCol(bf.nrm[0], bf.nrm[1], bf.nrm[2]);
        for (int iv = 0; iv < kGrid; ++iv) {
            float fv0 = static_cast<float>(iv) / static_cast<float>(kGrid);
            float fv1 = static_cast<float>(iv + 1) / static_cast<float>(kGrid);
            for (int iu = 0; iu < kGrid; ++iu) {
                float fu0 = static_cast<float>(iu) / static_cast<float>(kGrid);
                float fu1 = static_cast<float>(iu + 1) / static_cast<float>(kGrid);
                auto evalPt = [&](float fu, float fv, float out[3]) {
                    float lx = bf.org[0] + bf.uVec[0] * fu + bf.vVec[0] * fv;
                    float ly = bf.org[1] + bf.uVec[1] * fu + bf.vVec[1] * fv;
                    float lz = bf.org[2] + bf.uVec[2] * fu + bf.vVec[2] * fv;
                    TransformPoint(world, lx, ly, lz, out);
                    if (applyDistort && std::fabs(signedGlitch) > 1e-3f) {
                        float u = std::clamp((Dot3(out, U) - uMin) / cubeScreenH, 0.0f, 1.0f);
                        float ySpr = (1.0f - u) * 59.0f;
                        float dxPx = signedGlitch * std::cos((ySpr * 10.0f + glitchPhaseDeg) * kDeg2Rad) * (u * 8.0f);
                        float dxW = (dxPx / 50.0f) * cubeScreenW;
                        out[0] += R[0] * dxW;
                        out[1] += R[1] * dxW;
                        out[2] += R[2] * dxW;
                    }
                };
                float p00[3], p10[3], p11[3], p01[3];
                evalPt(fu0, fv0, p00);
                evalPt(fu1, fv0, p10);
                evalPt(fu1, fv1, p11);
                evalPt(fu0, fv1, p01);
                g_ghostVerts.push_back({p00[0], p00[1], p00[2], col});
                g_ghostVerts.push_back({p10[0], p10[1], p10[2], col});
                g_ghostVerts.push_back({p11[0], p11[1], p11[2], col});
                g_ghostVerts.push_back({p00[0], p00[1], p00[2], col});
                g_ghostVerts.push_back({p11[0], p11[1], p11[2], col});
                g_ghostVerts.push_back({p01[0], p01[1], p01[2], col});
            }
        }
    }

    ScopedGhostD3DState guard(dev);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->DrawPrimitiveUP(
        D3DPT_TRIANGLELIST,
        static_cast<UINT>(g_ghostVerts.size() / 3u),
        g_ghostVerts.data(),
        sizeof(GhostVert)
    );
}

void RenderCustomHintGhostAtTransform(
    IDirect3DDevice9* dev,
    const D3DMATRIX& world,
    float minX, float maxX, float minY, float maxY, float minZ, float maxZ,
    uint8_t style, const uint8_t* ws, bool isPreview
) {
    if (dev == nullptr) return;
    if (style == 1u) {
        RenderAndroidHintGhost(dev, world, minX, maxX, minY, maxY, minZ, maxZ, ws, isPreview);
    } else if (style == 2u) {
        RenderJavaHintGhost(dev, world, minX, maxX, minY, maxY, minZ, maxZ, ws, isPreview);
    } else if (isPreview) {
        RenderPcPreviewHintGhost(dev, world, minX, maxX, minY, maxY, minZ, maxZ, ws);
    }
}

} // namespace

namespace edgemod {

void __cdecl BeginCustomHintGhostOverride(const uint8_t* ws, uint8_t style) {
    g_hintGhostOverrideWs    = ws;
    g_hintGhostOverrideStyle = (style == 1u || style == 2u) ? style : 0u;
}

void __cdecl EndCustomHintGhostOverride() {
    g_hintGhostOverrideStyle = 0u;
    g_hintGhostOverrideWs    = nullptr;
}

bool __cdecl IsCustomHintGhostOverrideActive() {
    return g_hintGhostOverrideStyle != 0u;
}

bool __cdecl TryDrawCustomHintGhostSubMesh(void* d3dDev, const uint8_t* subMesh, const uint8_t* renderParams) {
    if (g_hintGhostOverrideStyle == 0u || d3dDev == nullptr || subMesh == nullptr) return false;
    if (renderParams != nullptr && *reinterpret_cast<const uint32_t*>(renderParams + 0x0c) == 3u) {
        return false;
    }

    const float* posBegin = *reinterpret_cast<const float* const*>(subMesh + 0x10);
    const float* posEnd   = *reinterpret_cast<const float* const*>(subMesh + 0x14);
    if (posBegin == nullptr || posEnd <= posBegin) return false;

    size_t numVerts = static_cast<size_t>(posEnd - posBegin) / 3u;
    if (numVerts < 8u || numVerts > 10000u) return false;

    float minX = posBegin[0], maxX = posBegin[0];
    float minY = posBegin[1], maxY = posBegin[1];
    float minZ = posBegin[2], maxZ = posBegin[2];
    for (size_t i = 1; i < numVerts; ++i) {
        float vx = posBegin[i * 3u + 0u];
        float vy = posBegin[i * 3u + 1u];
        float vz = posBegin[i * 3u + 2u];
        if (vx < minX) minX = vx;
        if (vx > maxX) maxX = vx;
        if (vy < minY) minY = vy;
        if (vy > maxY) maxY = vy;
        if (vz < minZ) minZ = vz;
        if (vz > maxZ) maxZ = vz;
    }
    if ((maxX - minX) < 1e-4f || (maxY - minY) < 1e-4f || (maxZ - minZ) < 1e-4f) {
        return false;
    }

    auto* dev = static_cast<IDirect3DDevice9*>(d3dDev);
    D3DMATRIX world{};
    if (FAILED(dev->GetTransform(D3DTS_WORLD, &world))) return false;

    RenderCustomHintGhostAtTransform(
        dev, world, minX, maxX, minY, maxY, minZ, maxZ,
        g_hintGhostOverrideStyle, g_hintGhostOverrideWs, false
    );
    return true;
}

void __cdecl RenderPreviewHintGhostIfActive() {
    if (!IsGraphicsPreviewActive()) return;
    uint8_t* ws = GetWorldScreen();
    uint8_t* renderer = *reinterpret_cast<uint8_t**>(0x5f8770);
    if (ws == nullptr || ws[0x2120] == 0 || renderer == nullptr) return;
    IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9**>(renderer + 0xcc);
    if (dev == nullptr) return;

    // Preview hint tile (HoloSwitch) is at (6, 7, 1). Place Hint Ghost NW (-X) at (5, 7, 1).
    D3DMATRIX world{};
    world._11 = world._22 = world._33 = world._44 = 1.0f;
    world._41 = 5.0f * 30.0f + 15.0f; // 165.0f
    world._42 = 1.0f * 30.0f + 15.0f; //  45.0f
    world._43 = 7.0f * 30.0f + 15.0f; // 225.0f

    uint8_t style = GetEffectiveTheme().hintGhost;
    RenderCustomHintGhostAtTransform(
        dev, world, -15.0f, 15.0f, -15.0f, 15.0f, -15.0f, 15.0f,
        style, ws, true
    );
}

void __cdecl ResetHintGhostState() {
    g_hintGhostOverrideStyle = 0u;
    g_hintGhostOverrideWs    = nullptr;
    s_lastWs                 = nullptr;
    s_lastWsFrame            = 0u;
    s_prevPreviewFrame       = 0u;
    s_lastSmooth30           = 0.0;
    s_androidLastTick        = -1;
    s_androidGlitch          = false;
    s_androidPrevGlitch      = false;
    s_androidSx              = 1.0f;
    s_androidSy              = 1.0f;
    s_androidSz              = 1.0f;
    s_androidPrevSx          = 1.0f;
    s_androidPrevSy          = 1.0f;
    s_androidPrevSz          = 1.0f;
    s_androidHalo1Scale      = 1.15f;
    s_androidHalo2Scale      = 1.07f;
    s_androidPrevHalo1       = 1.15f;
    s_androidPrevHalo2       = 1.07f;
    s_androidSliceSeed       = 1u;
    s_androidPrevSeed        = 1u;
    s_javaLastTick           = UINT32_MAX;
    s_javaGlitch             = false;
    s_javaPrevSignedGlitch   = 0.0f;
    s_javaSignedGlitch       = 0.0f;
    s_pcLastTick             = -1;
    s_pcFlickerAdd           = 0;
    s_pcPrevFlickerAdd       = 0;
    s_pcGlitchLastTick       = UINT32_MAX;
    s_pcPrevSignedGlitch     = 0.0f;
    s_pcSignedGlitch         = 0.0f;
}

} // namespace edgemod
