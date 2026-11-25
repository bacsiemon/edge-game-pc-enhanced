#include "mod_smooth_shared.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace edgemod;

struct MmVert {
    float    x, y, z;
    D3DCOLOR diffuse;
    float    u, v;
};
constexpr DWORD kMmFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1; // 0x142

// GPU-cached static terrain texture
static IDirect3DTexture9*   g_mmTerrainTex    = nullptr;
static IDirect3DDevice9*    g_mmCachedDev     = nullptr;
static uint8_t*             g_mmCachedLevel   = nullptr;
static int32_t              g_mmTexW          = 0;
static int32_t              g_mmTexH          = 0;
static int32_t              g_mmSizeX         = 0;
static int32_t              g_mmSizeZ         = 0;
static int32_t              g_mmHeightTiles   = 0;
static std::vector<uint8_t> g_mmFpFallenState;
static std::vector<MmVert>  g_mmVerts;

static int32_t NextPow2(int32_t v) {
    int32_t p = 1;
    while (p < v && p < 4096) p <<= 1;
    return p;
}

static void ReleaseTerrainTexture() {
    if (g_mmTerrainTex != nullptr) {
        g_mmTerrainTex->Release();
        g_mmTerrainTex = nullptr;
    }
    g_mmCachedDev = nullptr;
    g_mmCachedLevel = nullptr;
    g_mmTexW = 0;
    g_mmTexH = 0;
    g_mmSizeX = 0;
    g_mmSizeZ = 0;
    g_mmHeightTiles = 0;
    g_mmFpFallenState.clear();
}

static void EnsureTerrainTexture(IDirect3DDevice9* dev, uint8_t* level) {
    if (dev == nullptr || level == nullptr) return;
    int32_t sizeX = *reinterpret_cast<const int16_t*>(level + 0x72);
    int32_t sizeZ = *reinterpret_cast<const int16_t*>(level + 0x74);
    int32_t heightTiles = static_cast<int32_t>(level[0x70]);
    uint8_t* const* grid = *reinterpret_cast<uint8_t* const* const*>(level + 0x58);
    if (sizeX <= 0 || sizeZ <= 0 || heightTiles <= 0 || grid == nullptr) return;

    int32_t fpCount = *reinterpret_cast<const int16_t*>(level + 0x14);
    uint8_t* const* fps = *reinterpret_cast<uint8_t* const* const*>(level + 0x18);
    if (fpCount < 0 || fps == nullptr) fpCount = 0;

    bool levelChanged = (dev != g_mmCachedDev || level != g_mmCachedLevel ||
                         sizeX != g_mmSizeX || sizeZ != g_mmSizeZ ||
                         heightTiles != g_mmHeightTiles || g_mmTerrainTex == nullptr);

    bool fpDirty = levelChanged || (static_cast<int32_t>(g_mmFpFallenState.size()) != fpCount);
    if (!fpDirty && fpCount > 0) {
        for (int32_t i = 0; i < fpCount; ++i) {
            const uint8_t* fp = fps[i];
            uint8_t fallen = (fp != nullptr && *reinterpret_cast<const int32_t*>(fp + 0x0c) <= -600) ? 1u : 0u;
            if (g_mmFpFallenState[i] != fallen) {
                fpDirty = true;
                break;
            }
        }
    }
    if (!fpDirty) return;

    int32_t reqTexW = NextPow2(sizeX);
    int32_t reqTexH = NextPow2(sizeZ);
    if (dev != g_mmCachedDev || reqTexW != g_mmTexW || reqTexH != g_mmTexH || g_mmTerrainTex == nullptr) {
        if (g_mmTerrainTex != nullptr) {
            g_mmTerrainTex->Release();
            g_mmTerrainTex = nullptr;
        }
        HRESULT hr = dev->CreateTexture(
            static_cast<UINT>(reqTexW), static_cast<UINT>(reqTexH), 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_mmTerrainTex, nullptr
        );
        if (FAILED(hr) || g_mmTerrainTex == nullptr) {
            g_mmCachedDev = nullptr;
            g_mmCachedLevel = nullptr;
            return;
        }
        g_mmTexW = reqTexW;
        g_mmTexH = reqTexH;
    }

    D3DLOCKED_RECT locked{};
    if (FAILED(g_mmTerrainTex->LockRect(0, &locked, nullptr, 0)) || locked.pBits == nullptr) {
        return;
    }

    uint8_t* dstBase = static_cast<uint8_t*>(locked.pBits);
    for (int32_t gz = 0; gz < g_mmTexH; ++gz) {
        uint32_t* row = reinterpret_cast<uint32_t*>(dstBase + gz * locked.Pitch);
        if (gz >= sizeZ) {
            std::memset(row, 0, static_cast<size_t>(g_mmTexW) * 4u);
            continue;
        }
        int32_t rowOffset = gz * sizeX;
        for (int32_t gx = 0; gx < sizeX; ++gx) {
            uint32_t pixel = 0;
            for (int32_t y = heightTiles; y >= 1; --y) {
                const uint8_t* layer = grid[y];
                if (layer != nullptr && layer[rowOffset + gx] > 0) {
                    uint8_t shade = (y == 1) ? 0x8Bu
                                  : (y == 2) ? 0xABu
                                  : (y == 3) ? 0xCDu
                                             : 0xE5u;
                    pixel = D3DCOLOR_ARGB(255, shade, shade, shade);
                    break;
                }
            }
            row[gx] = pixel;
        }
        for (int32_t gx = sizeX; gx < g_mmTexW; ++gx) {
            row[gx] = 0;
        }
    }
    g_mmTerrainTex->UnlockRect(0);

    g_mmCachedDev = dev;
    g_mmCachedLevel = level;
    g_mmSizeX = sizeX;
    g_mmSizeZ = sizeZ;
    g_mmHeightTiles = heightTiles;
    g_mmFpFallenState.resize(static_cast<size_t>(fpCount));
    for (int32_t i = 0; i < fpCount; ++i) {
        const uint8_t* fp = fps[i];
        g_mmFpFallenState[i] = (fp != nullptr && *reinterpret_cast<const int32_t*>(fp + 0x0c) <= -600) ? 1u : 0u;
    }
}

static void PushQuad(
    std::vector<MmVert>& out,
    float x0, float y0, float x1, float y1,
    D3DCOLOR col,
    float u0 = 0.0f, float v0 = 0.0f, float u1 = 0.0f, float v1 = 0.0f
) {
    out.push_back({x0, y0, 0.0f, col, u0, v0});
    out.push_back({x1, y0, 0.0f, col, u1, v0});
    out.push_back({x1, y1, 0.0f, col, u1, v1});
    out.push_back({x0, y0, 0.0f, col, u0, v0});
    out.push_back({x1, y1, 0.0f, col, u1, v1});
    out.push_back({x0, y1, 0.0f, col, u0, v1});
}

static void GetMinimapCubeCenterTile(const uint8_t* cube, float* outTileX, float* outTileZ) {
    if (cube == nullptr) {
        *outTileX = 0.5f;
        *outTileZ = 0.5f;
        return;
    }
    uint8_t* model = *reinterpret_cast<uint8_t* const*>(cube + 0x1fc);
    if (model == nullptr) {
        model = *reinterpret_cast<uint8_t* const*>(cube + 0x1f4);
    }
    if (model != nullptr) {
        float pos[3]{};
        reinterpret_cast<float*(__thiscall*)(uint8_t*, float*)>(0x421400)(model, pos);
        if (std::isfinite(pos[0]) && std::isfinite(pos[2]) &&
            std::fabs(pos[0]) < 1.0e6f && std::fabs(pos[2]) < 1.0e6f) {
            *outTileX = pos[0] / 30.0f;
            *outTileZ = pos[2] / 30.0f;
            return;
        }
    }
    float scaleDiv = 1.0f;
    if (*reinterpret_cast<const int32_t*>(cube + 0x138) != 1) {
        scaleDiv = static_cast<float>(*reinterpret_cast<const int32_t*>(cube + 0x134)) * 0.000244140625f;
        if (scaleDiv <= 0.1f) scaleDiv = 1.0f;
    }
    float halfSz = 15.0f / scaleDiv;
    *outTileX = (static_cast<float>(*reinterpret_cast<const int32_t*>(cube + 0x4c)) + halfSz) / 30.0f;
    *outTileZ = (static_cast<float>(*reinterpret_cast<const int32_t*>(cube + 0x50)) + halfSz) / 30.0f;
}

static double GetPreviewClockSec() {
    static LARGE_INTEGER s_freq{};
    static LARGE_INTEGER s_t0{};
    if (s_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&s_freq);
        QueryPerformanceCounter(&s_t0);
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return (s_freq.QuadPart > 0)
        ? (static_cast<double>(now.QuadPart - s_t0.QuadPart) / static_cast<double>(s_freq.QuadPart))
        : 0.0;
}

struct SavedMmD3DState {
    DWORD fvf = 0;
    DWORD tssColorOp = 0, tssColorArg1 = 0, tssColorArg2 = 0;
    DWORD tssAlphaOp = 0, tssAlphaArg1 = 0, tssAlphaArg2 = 0;
    DWORD tssTexXform = 0;
    DWORD sampMin = 0, sampMag = 0, sampMip = 0, sampAddrU = 0, sampAddrV = 0;
    DWORD rsLighting = 0, rsZEnable = 0, rsCull = 0;
};

static void SaveMmD3DState(IDirect3DDevice9* dev, SavedMmD3DState* s) {
    dev->GetFVF(&s->fvf);
    dev->GetTextureStageState(0, D3DTSS_COLOROP, &s->tssColorOp);
    dev->GetTextureStageState(0, D3DTSS_COLORARG1, &s->tssColorArg1);
    dev->GetTextureStageState(0, D3DTSS_COLORARG2, &s->tssColorArg2);
    dev->GetTextureStageState(0, D3DTSS_ALPHAOP, &s->tssAlphaOp);
    dev->GetTextureStageState(0, D3DTSS_ALPHAARG1, &s->tssAlphaArg1);
    dev->GetTextureStageState(0, D3DTSS_ALPHAARG2, &s->tssAlphaArg2);
    dev->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &s->tssTexXform);
    dev->GetSamplerState(0, D3DSAMP_MINFILTER, &s->sampMin);
    dev->GetSamplerState(0, D3DSAMP_MAGFILTER, &s->sampMag);
    dev->GetSamplerState(0, D3DSAMP_MIPFILTER, &s->sampMip);
    dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &s->sampAddrU);
    dev->GetSamplerState(0, D3DSAMP_ADDRESSV, &s->sampAddrV);
    dev->GetRenderState(D3DRS_LIGHTING, &s->rsLighting);
    dev->GetRenderState(D3DRS_ZENABLE, &s->rsZEnable);
    dev->GetRenderState(D3DRS_CULLMODE, &s->rsCull);
}

static void RestoreMmD3DState(IDirect3DDevice9* dev, const SavedMmD3DState& s) {
    dev->SetTexture(0, nullptr);
    uint8_t boundCount = *reinterpret_cast<const uint8_t*>(0x5f87c9);
    void** boundArr = *reinterpret_cast<void***>(0x5f87cc);
    if (boundCount > 0 && boundArr != nullptr) {
        boundArr[0] = nullptr;
    }
    dev->SetTextureStageState(0, D3DTSS_COLOROP, s.tssColorOp);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, s.tssColorArg1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, s.tssColorArg2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, s.tssAlphaOp);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, s.tssAlphaArg1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, s.tssAlphaArg2);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, s.tssTexXform);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, s.sampMin);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, s.sampMag);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, s.sampMip);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, s.sampAddrU);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, s.sampAddrV);
    dev->SetRenderState(D3DRS_LIGHTING, s.rsLighting);
    dev->SetRenderState(D3DRS_ZENABLE, s.rsZEnable);
    dev->SetRenderState(D3DRS_CULLMODE, s.rsCull);
    dev->SetFVF(s.fvf);
}

static void BeginMinimapTransformAndScissor(
    uint8_t* renderer, uint8_t* mstack,
    int32_t mmX, int32_t mmY, int32_t boxW, int32_t boxH,
    int32_t rot
) {
    Game_SetBlend(renderer, 0);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f750)(mstack); // MatrixStack::Push

    int32_t offX = (rot == 1 || rot == 2) ? boxW : 0;
    int32_t offY = (rot == 2 || rot == 3) ? boxH : 0;
    float transMat[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        static_cast<float>(mmX + offX), -static_cast<float>(mmY + offY), 0.0f, 1.0f
    };
    reinterpret_cast<void(__thiscall*)(void*, const float*)>(0x41f520)(mstack, transMat);

    float rotMat[16]{};
    float angleRad = static_cast<float>(-static_cast<double>(rot) * (*reinterpret_cast<const double*>(0x586ad8)));
    reinterpret_cast<void(__stdcall*)(float*, float)>(0x4bb2c4)(rotMat, angleRad);
    reinterpret_cast<void(__thiscall*)(void*, const float*)>(0x41f520)(mstack, rotMat);

    int32_t scissorRect[4] = {mmX, mmY, boxW, boxH};
    reinterpret_cast<void(__thiscall*)(void*, const int32_t*)>(0x4060d0)(renderer, scissorRect);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f660)(mstack); // SetTransform(D3DTS_WORLD)
}

static void EndMinimapTransformAndScissor(uint8_t* renderer, uint8_t* mstack) {
    reinterpret_cast<void(__thiscall*)(void*)>(0x405b90)(renderer); // clearScissorRect
    Game_SetBlend(renderer, 3);
    reinterpret_cast<void(__thiscall*)(void*)>(0x41f610)(mstack); // MatrixStack::Pop
}

static void ComputePlayerDotStyle(float smoothFrame, float* outPulse, uint32_t* outRgb) {
    float f = std::fmod(smoothFrame, 16.0f);
    if (f < 0.0f) f += 16.0f;
    float pulse = 0.0f;
    if (f < 1.0f) pulse = 2.0f + 2.0f * f;
    else if (f < 2.0f) pulse = 4.0f * (2.0f - f);
    else if (f >= 15.0f) pulse = 2.0f * (f - 15.0f);
    *outPulse = pulse;

    const uint32_t* pal = reinterpret_cast<const uint32_t*>(0x5f9110);
    uint32_t idx = static_cast<uint32_t>(static_cast<int32_t>(std::floor(f))) & 15u;
    uint32_t nextIdx = (idx + 1u) & 15u;
    int32_t pct = std::clamp(static_cast<int32_t>(std::lrint((f - std::floor(f)) * 100.0f)), 0, 100);
    *outRgb = Game_ColorLerp(pal[idx], pal[nextIdx], pct);
}

static void RenderLiveMinimapBox(
    uint8_t* renderer, uint8_t* sm, uint8_t* ws, IDirect3DDevice9* dev, uint8_t* level,
    const EffectiveMinimapMetrics& eff, float cellW, float cellH, int rot,
    int32_t mmX, int32_t mmY, int32_t boxW, int32_t boxH, int32_t drawAlpha
) {
    uint8_t* mstack = GetMatrixStack();
    if (mstack == nullptr || drawAlpha <= 0 || eff.width <= 0.0f || eff.height <= 0.0f) return;

    uint8_t* cube = *reinterpret_cast<uint8_t* const*>(ws + 0x34);
    float cubeTileX = 0.5f;
    float cubeTileZ = 0.5f;
    GetMinimapCubeCenterTile(cube, &cubeTileX, &cubeTileZ);

    EnsureTerrainTexture(dev, level);

    BeginMinimapTransformAndScissor(renderer, mstack, mmX, mmY, boxW, boxH, rot);

    SavedMmD3DState saved{};
    SaveMmD3DState(dev, &saved);
    dev->SetFVF(kMmFvf);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    const float effW = eff.width;
    const float effH = eff.height;
    const float halfW = effW * 0.5f;
    const float halfH = effH * 0.5f;
    auto mapX = [&](float tx) { return halfW + (tx - cubeTileX) * cellW; };
    auto mapY = [&](float tz) { return -(halfH + (tz - cubeTileZ) * cellH); };

    // Pass 1: Background quad
    if (g_minimapBgOpacity > 0) {
        int32_t bgAlpha = std::clamp(
            static_cast<int32_t>(std::lround(static_cast<double>(drawAlpha) * (static_cast<double>(g_minimapBgOpacity) / 100.0))),
            0, 255
        );
        if (bgAlpha > 0) {
            D3DCOLOR bgCol = D3DCOLOR_ARGB(bgAlpha, 0x21, 0x21, 0x21);
            MmVert bgVerts[6] = {
                {0.0f,  0.0f,  0.0f, bgCol, 0.0f, 0.0f},
                {effW,  0.0f,  0.0f, bgCol, 0.0f, 0.0f},
                {effW, -effH,  0.0f, bgCol, 0.0f, 0.0f},
                {0.0f,  0.0f,  0.0f, bgCol, 0.0f, 0.0f},
                {effW, -effH,  0.0f, bgCol, 0.0f, 0.0f},
                {0.0f, -effH,  0.0f, bgCol, 0.0f, 0.0f},
            };
            dev->SetTexture(0, nullptr);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
            dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, bgVerts, sizeof(MmVert));
        }
    }

    // Pass 2: GPU-cached static terrain (1 single quad)
    if (g_mmTerrainTex != nullptr && g_mmSizeX > 0 && g_mmSizeZ > 0 && g_mmTexW > 0 && g_mmTexH > 0) {
        int32_t tileAlpha = std::clamp(
            static_cast<int32_t>(std::lround((static_cast<double>(drawAlpha) / 255.0) * 108.0)),
            0, 255
        );
        if (tileAlpha > 0) {
            float x0 = mapX(0.0f);
            float y0 = mapY(0.0f);
            float x1 = mapX(static_cast<float>(g_mmSizeX));
            float y1 = mapY(static_cast<float>(g_mmSizeZ));
            float u1 = static_cast<float>(g_mmSizeX) / static_cast<float>(g_mmTexW);
            float v1 = static_cast<float>(g_mmSizeZ) / static_cast<float>(g_mmTexH);
            D3DCOLOR tileCol = D3DCOLOR_ARGB(tileAlpha, 255, 255, 255);
            MmVert texVerts[6] = {
                {x0, y0, 0.0f, tileCol, 0.0f, 0.0f},
                {x1, y0, 0.0f, tileCol, u1,   0.0f},
                {x1, y1, 0.0f, tileCol, u1,   v1},
                {x0, y0, 0.0f, tileCol, 0.0f, 0.0f},
                {x1, y1, 0.0f, tileCol, u1,   v1},
                {x0, y1, 0.0f, tileCol, 0.0f, v1},
            };
            dev->SetTexture(0, g_mmTerrainTex);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, texVerts, sizeof(MmVert));
        }
    }

    // Pass 3: Batched dynamic entities + player cube
    g_mmVerts.clear();
    if (g_mmVerts.capacity() < 1024u) {
        g_mmVerts.reserve(1024u);
    }

    const float halfTilesX = (cellW > 0.001f) ? (halfW / cellW) : 20.0f;
    const float halfTilesZ = (cellH > 0.001f) ? (halfH / cellH) : 20.0f;
    const float minTX = cubeTileX - halfTilesX - 2.0f;
    const float maxTX = cubeTileX + halfTilesX + 2.0f;
    const float minTZ = cubeTileZ - halfTilesZ - 2.0f;
    const float maxTZ = cubeTileZ + halfTilesZ + 2.0f;

    int32_t entAlpha = std::clamp(
        static_cast<int32_t>(std::lround((static_cast<double>(drawAlpha) / 255.0) * 122.0)),
        0, 255
    );
    D3DCOLOR entCol = D3DCOLOR_ARGB(entAlpha, 255, 255, 255);

    if (entAlpha > 0) {
        // Moving platforms (60+ FPS interpolated positions when available)
        int32_t mpCount = *reinterpret_cast<const int16_t*>(level + 0x04);
        uint8_t* const* mps = *reinterpret_cast<uint8_t* const* const*>(level + 0x08);
        const uint8_t* interpState = *reinterpret_cast<const uint8_t* const*>(level + 0xb0);
        bool hasInterpMp = (interpState != nullptr && level[0xb4] != 0);
        int32_t interpMpCount = hasInterpMp ? *reinterpret_cast<const int32_t*>(interpState + 0x9c) : 0;
        const float* mpArr = (hasInterpMp && interpMpCount > 0)
            ? *reinterpret_cast<const float* const*>(interpState + 0xa0)
            : nullptr;

        if (mps != nullptr && mpCount > 0) {
            for (int32_t i = 0; i < mpCount; ++i) {
                const uint8_t* mp = mps[i];
                if (mp == nullptr) continue;
                float wx = 0.0f;
                float wz = 0.0f;
                if (mpArr != nullptr && i < interpMpCount) {
                    wx = mpArr[i * 4 + 0] - 15.0f;
                    wz = mpArr[i * 4 + 2] - 15.0f;
                } else {
                    wx = static_cast<float>(*reinterpret_cast<const int16_t*>(mp + 0x14));
                    wz = static_cast<float>(*reinterpret_cast<const int16_t*>(mp + 0x16));
                }
                float tx = wx / 30.0f;
                float tz = wz / 30.0f;
                if (tx < minTX || tx > maxTX || tz < minTZ || tz > maxTZ) continue;
                PushQuad(g_mmVerts, mapX(tx), mapY(tz), mapX(tx + 1.0f), mapY(tz + 1.0f), entCol);
            }
        }

        // Bumpers
        int32_t bmpCount = *reinterpret_cast<const int16_t*>(level + 0x0c);
        uint8_t* const* bmps = *reinterpret_cast<uint8_t* const* const*>(level + 0x10);
        if (bmps != nullptr && bmpCount > 0) {
            for (int32_t i = 0; i < bmpCount; ++i) {
                const uint8_t* b = bmps[i];
                if (b == nullptr) continue;
                float tx = static_cast<float>(*reinterpret_cast<const int32_t*>(b + 0x08)) / 30.0f;
                float tz = static_cast<float>(*reinterpret_cast<const int32_t*>(b + 0x0c)) / 30.0f;
                if (tx < minTX || tx > maxTX || tz < minTZ || tz > maxTZ) continue;
                PushQuad(g_mmVerts, mapX(tx), mapY(tz), mapX(tx + 1.0f), mapY(tz + 1.0f), entCol);
            }
        }

        // Falling platforms
        int32_t fpCount = *reinterpret_cast<const int16_t*>(level + 0x14);
        uint8_t* const* fps = *reinterpret_cast<uint8_t* const* const*>(level + 0x18);
        if (fps != nullptr && fpCount > 0) {
            for (int32_t i = 0; i < fpCount; ++i) {
                const uint8_t* fp = fps[i];
                if (fp == nullptr || *reinterpret_cast<const int32_t*>(fp + 0x0c) <= -600) continue;
                float tx = static_cast<float>(*reinterpret_cast<const int32_t*>(fp + 0x04)) / 30.0f;
                float tz = static_cast<float>(*reinterpret_cast<const int32_t*>(fp + 0x08)) / 30.0f;
                if (tx < minTX || tx > maxTX || tz < minTZ || tz > maxTZ) continue;
                PushQuad(g_mmVerts, mapX(tx), mapY(tz), mapX(tx + 1.0f), mapY(tz + 1.0f), entCol);
            }
        }
    }

    float smoothFrame = GetSmoothWsFrame(ws);

    // Prisms (smooth rainbow)
    int32_t prismAlpha = std::clamp(
        static_cast<int32_t>(std::lround((static_cast<double>(drawAlpha) / 255.0) * 217.0)),
        0, 255
    );
    if (prismAlpha > 0) {
        int32_t pCount = *reinterpret_cast<const int32_t*>(level + 0x30);
        uint8_t* const* prisms = *reinterpret_cast<uint8_t* const* const*>(level + 0x34);
        if (prisms != nullptr && pCount > 0) {
            uint32_t rb = CalcSmoothRainbow(smoothFrame, 1.0f);
            D3DCOLOR prismCol = (rb & 0x00ffffffu) | (static_cast<uint32_t>(prismAlpha) << 24);
            for (int32_t i = 0; i < pCount; ++i) {
                const uint8_t* pr = prisms[i];
                if (pr == nullptr || pr[0x18] == 0) continue;
                float tx = static_cast<float>(*reinterpret_cast<const int32_t*>(pr + 0x04)) / 30.0f;
                float tz = static_cast<float>(*reinterpret_cast<const int32_t*>(pr + 0x08)) / 30.0f;
                if (tx < minTX || tx > maxTX || tz < minTZ || tz > maxTZ) continue;
                PushQuad(
                    g_mmVerts,
                    mapX(tx + 0.375f), mapY(tz + 0.375f),
                    mapX(tx + 0.625f), mapY(tz + 0.625f),
                    prismCol
                );
            }
        }
    }

    // Player cube dot (centered smoothly on real position)
    if (cube != nullptr) {
        float scaleDiv = 1.0f;
        if (*reinterpret_cast<const int32_t*>(cube + 0x138) != 1) {
            scaleDiv = static_cast<float>(*reinterpret_cast<const int32_t*>(cube + 0x134)) * 0.000244140625f;
            if (scaleDiv <= 0.1f) scaleDiv = 1.0f;
        }
        float pulse = 0.0f;
        uint32_t dotRgb = 0xffffffu;
        ComputePlayerDotStyle(smoothFrame, &pulse, &dotRgb);
        float dotW = (cellW / scaleDiv) + pulse;
        float dotH = (cellH / scaleDiv) + pulse;
        D3DCOLOR dotCol = (dotRgb & 0x00ffffffu) | (static_cast<uint32_t>(drawAlpha) << 24);
        PushQuad(
            g_mmVerts,
            halfW - dotW * 0.5f, -(halfH - dotH * 0.5f),
            halfW + dotW * 0.5f, -(halfH + dotH * 0.5f),
            dotCol
        );
    }

    if (!g_mmVerts.empty()) {
        dev->SetTexture(0, nullptr);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->DrawPrimitiveUP(
            D3DPT_TRIANGLELIST,
            static_cast<UINT>(g_mmVerts.size() / 3u),
            g_mmVerts.data(),
            sizeof(MmVert)
        );
    }

    RestoreMmD3DState(dev, saved);
    EndMinimapTransformAndScissor(renderer, mstack);
}

static void __cdecl RenderGameplayMinimap_Impl(uint8_t* ws) {
    if (ws == nullptr || ws[0x2120] == 0) return;

    int32_t cur2108 = *reinterpret_cast<const int32_t*>(ws + 0x2108);
    if (g_wsPtr != ws || cur2108 != g_wsTgt2108) {
        g_wsPtr = ws;
        g_wsTgt2108 = cur2108;
        g_wsPrev2108 = cur2108;
    }
    int32_t drawAlpha = g_wsTgt2108;
    if (drawAlpha != g_wsPrev2108) {
        float aF = static_cast<float>(g_wsPrev2108) + static_cast<float>(drawAlpha - g_wsPrev2108) * g_uiAlpha;
        drawAlpha = std::clamp(static_cast<int32_t>(std::lrint(aF)), 0, 255);
    }
    if (drawAlpha <= 0) return;

    uint8_t* renderer = GetRenderer();
    uint8_t* sm = GetScreenMgr();
    uint8_t* level = GetLevel();
    IDirect3DDevice9* dev = (renderer != nullptr) ? *reinterpret_cast<IDirect3DDevice9* const*>(renderer + 0xcc) : nullptr;
    if (renderer == nullptr || sm == nullptr || level == nullptr || dev == nullptr) return;

    EffectiveMinimapMetrics eff{};
    GetEffectiveMinimapMetrics(&eff);
    if (!eff.enabled || eff.width <= 0.0f || eff.height <= 0.0f) return;

    float zoom = *reinterpret_cast<const float*>(sm + 0x34);
    void** ppBcfg = reinterpret_cast<void**(__cdecl*)()>(0x4764c0)();
    if (ppBcfg != nullptr && *ppBcfg != nullptr) {
        float minZ = reinterpret_cast<float(__thiscall*)(void*, uint32_t)>(0x4408c0)(*ppBcfg, *reinterpret_cast<const uint32_t*>(sm + 0x60));
        float maxZ = reinterpret_cast<float(__thiscall*)(void*, uint32_t)>(0x4408c0)(*ppBcfg, *reinterpret_cast<const uint32_t*>(sm + 0x68));
        if (zoom < minZ) zoom = minZ;
        if (zoom > maxZ) zoom = maxZ;
    }
    if (!std::isfinite(zoom) || zoom <= 0.01f) zoom = 1.0f;

    float cellW = eff.cellW / zoom;
    float cellH = eff.cellH / zoom;

    double camAngle = reinterpret_cast<double(__thiscall*)(void*)>(0x478950)(sm + 0xa0);
    double diffDeg = *reinterpret_cast<const double*>(0x586ae0) - camAngle;
    int rot = static_cast<int>(diffDeg) / 90;
    rot &= 0x80000003;
    if (rot < 0) rot = ((rot - 1) | ~3) + 1;

    int32_t boxW = static_cast<int32_t>(std::lround(eff.width));
    int32_t boxH = static_cast<int32_t>(std::lround(eff.height));
    if (rot == 1 || rot == 3) std::swap(boxW, boxH);

    int32_t virtW = *reinterpret_cast<const int32_t*>(sm + 0x24);
    int32_t virtH = *reinterpret_cast<const int32_t*>(sm + 0x28);
    int32_t alignX = eff.alignX;
    int32_t mmX = 0;
    if (alignX == 1) mmX = (virtW - boxW) / 2;
    else if (alignX == 2) mmX = virtW - boxW;
    mmX += eff.offX;
    mmX = std::clamp(mmX, 0, (std::max)(0, virtW - boxW));

    int32_t alignY = eff.alignY;
    int32_t mmY = 0;
    if (alignY == 1) mmY = (virtH - boxH) / 2;
    else if (alignY == 2) mmY = virtH - boxH;
    mmY += eff.offY;
    mmY = std::clamp(mmY, 0, (std::max)(0, virtH - boxH));

    RenderLiveMinimapBox(renderer, sm, ws, dev, level, eff, cellW, cellH, rot, mmX, mmY, boxW, boxH, drawAlpha);
}

static void __declspec(naked) hk_renderCustomMinimap_bridge() {
    __asm {
        push edx
        push edi
        call RenderGameplayMinimap_Impl
        add esp, 4
        pop edx
        ret
    }
}

static void RenderSampleMinimapBox(
    uint8_t* renderer, IDirect3DDevice9* dev,
    const EffectiveMinimapMetrics& eff, int32_t mmX, int32_t mmY, int32_t drawAlpha
) {
    uint8_t* mstack = GetMatrixStack();
    if (mstack == nullptr || drawAlpha <= 0 || eff.width <= 0.0f || eff.height <= 0.0f) return;

    int32_t boxW = static_cast<int32_t>(std::lround(eff.width));
    int32_t boxH = static_cast<int32_t>(std::lround(eff.height));
    BeginMinimapTransformAndScissor(renderer, mstack, mmX, mmY, boxW, boxH, 0);

    SavedMmD3DState saved{};
    SaveMmD3DState(dev, &saved);
    dev->SetFVF(kMmFvf);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

    g_mmVerts.clear();

    const float effW = eff.width;
    const float effH = eff.height;
    const float halfW = effW * 0.5f;
    const float halfH = effH * 0.5f;
    const float cellW = eff.cellW / 1.5f;
    const float cellH = eff.cellH / 1.5f;
    const double alphaScale = static_cast<double>(drawAlpha) / 255.0;

    // Background
    if (g_minimapBgOpacity > 0) {
        int32_t bgAlpha = std::clamp(
            static_cast<int32_t>(std::lround(static_cast<double>(drawAlpha) * (static_cast<double>(g_minimapBgOpacity) / 100.0))),
            0, 255
        );
        if (bgAlpha > 0) {
            PushQuad(g_mmVerts, 0.0f, 0.0f, effW, -effH, D3DCOLOR_ARGB(bgAlpha, 0x21, 0x21, 0x21));
        }
    }

    const double sec = GetPreviewClockSec();
    const float smoothFrame = static_cast<float>(sec * 30.0);
    // Smooth figure-8 / patrol demo cube offset in tiles around center (5.5, 5.5)
    const float cubeTX = 5.5f + 0.6f * static_cast<float>(std::sin(sec * 1.4));
    const float cubeTZ = 5.5f + 0.4f * static_cast<float>(std::cos(sec * 1.4));
    auto mapX = [&](float tx) { return halfW + (tx - cubeTX) * cellW; };
    auto mapY = [&](float tz) { return -(halfH + (tz - cubeTZ) * cellH); };

    // Stylized multi-height static platform grid (11x11 centered around 5.5, 5.5)
    // 0 = empty, 1..4 = height levels (matching 0x8B, 0xAB, 0xCD, 0xE5)
    static const uint8_t kSampleMap[11][11] = {
        {0,0,0,0,0,0,0,0,0,0,0},
        {0,1,1,1,0,0,0,2,2,2,0},
        {0,1,2,1,0,0,0,2,3,2,0},
        {0,1,1,1,1,0,1,2,2,2,0},
        {0,0,0,1,2,2,2,1,0,0,0},
        {0,0,0,1,2,3,2,1,0,0,0},
        {0,0,0,1,2,2,2,1,0,0,0},
        {0,2,2,1,0,0,0,1,3,3,0},
        {0,2,4,2,0,0,0,1,3,4,0},
        {0,2,2,2,0,0,0,1,3,3,0},
        {0,0,0,0,0,0,0,0,0,0,0},
    };
    int32_t tileAlpha = std::clamp(static_cast<int32_t>(std::lround(alphaScale * 108.0)), 0, 255);
    for (int gz = 0; gz < 11; ++gz) {
        for (int gx = 0; gx < 11; ++gx) {
            uint8_t h = kSampleMap[gz][gx];
            if (h == 0) continue;
            uint8_t shade = (h == 1) ? 0x8Bu : (h == 2) ? 0xABu : (h == 3) ? 0xCDu : 0xE5u;
            D3DCOLOR c = D3DCOLOR_ARGB(tileAlpha, shade, shade, shade);
            PushQuad(
                g_mmVerts,
                mapX(static_cast<float>(gx)), mapY(static_cast<float>(gz)),
                mapX(static_cast<float>(gx + 1)), mapY(static_cast<float>(gz + 1)),
                c
            );
        }
    }

    // Oscillating moving platform bridging left/right gap at z = 5.0
    int32_t entAlpha = std::clamp(static_cast<int32_t>(std::lround(alphaScale * 122.0)), 0, 255);
    float mpTX = 1.5f + 1.0f * (0.5f + 0.5f * static_cast<float>(std::sin(sec * 2.2)));
    PushQuad(g_mmVerts, mapX(mpTX), mapY(5.0f), mapX(mpTX + 1.0f), mapY(6.0f), D3DCOLOR_ARGB(entAlpha, 255, 255, 255));
    float mpTZ = 1.5f + 1.0f * (0.5f + 0.5f * static_cast<float>(std::cos(sec * 2.2)));
    PushQuad(g_mmVerts, mapX(5.0f), mapY(mpTZ), mapX(6.0f), mapY(mpTZ + 1.0f), D3DCOLOR_ARGB(entAlpha, 255, 255, 255));

    // 2 rainbow prisms
    int32_t prismAlpha = std::clamp(static_cast<int32_t>(std::lround(alphaScale * 217.0)), 0, 255);
    uint32_t rb = CalcSmoothRainbow(smoothFrame, 1.0f);
    D3DCOLOR prismCol = (rb & 0x00ffffffu) | (static_cast<uint32_t>(prismAlpha) << 24);
    PushQuad(g_mmVerts, mapX(2.375f), mapY(2.375f), mapX(2.625f), mapY(2.625f), prismCol);
    PushQuad(g_mmVerts, mapX(8.375f), mapY(8.375f), mapX(8.625f), mapY(8.625f), prismCol);

    // Pulsing player cube dot at center
    float pulse = 0.0f;
    uint32_t dotRgb = 0xffffffu;
    ComputePlayerDotStyle(smoothFrame, &pulse, &dotRgb);
    float dotW = cellW + pulse;
    float dotH = cellH + pulse;
    D3DCOLOR dotCol = (dotRgb & 0x00ffffffu) | (static_cast<uint32_t>(drawAlpha) << 24);
    PushQuad(
        g_mmVerts,
        halfW - dotW * 0.5f, -(halfH - dotH * 0.5f),
        halfW + dotW * 0.5f, -(halfH + dotH * 0.5f),
        dotCol
    );

    if (!g_mmVerts.empty()) {
        dev->DrawPrimitiveUP(
            D3DPT_TRIANGLELIST,
            static_cast<UINT>(g_mmVerts.size() / 3u),
            g_mmVerts.data(),
            sizeof(MmVert)
        );
    }

    RestoreMmD3DState(dev, saved);
    EndMinimapTransformAndScissor(renderer, mstack);
}

static void __cdecl InitMinimapPostSync_Impl(uint8_t* ws) {
    reinterpret_cast<void(__thiscall*)(void*)>(0x479c60)(ws);
    SyncLiveWorldMinimapFields();
}

static void __declspec(naked) hk_initMinimapPostSync() {
    __asm {
        push ecx
        call InitMinimapPostSync_Impl
        add esp, 4
        ret
    }
}

} // namespace

namespace edgemod {

void __cdecl ResetMinimapCache() {
    ReleaseTerrainTexture();
}

void __cdecl RenderMinimapMenuPreview(uint8_t* renderer, uint8_t* ws, uint32_t expectedState) {
    if (renderer == nullptr) return;
    uint8_t previewPos = 0;
    bool forceSample = false;
    bool dockBesideMenu = false;
    if (!GetMinimapMenuPreview(expectedState, &previewPos, &forceSample, &dockBesideMenu)) return;

    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr || *reinterpret_cast<const uint32_t*>(sm + 0x50) != expectedState) return;

    IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9* const*>(renderer + 0xcc);
    if (dev == nullptr) return;

    int32_t virtW = *reinterpret_cast<const int32_t*>(sm + 0x24);
    int32_t virtH = *reinterpret_cast<const int32_t*>(sm + 0x28);
    if (virtW <= 0 || virtH <= 0) return;

    EffectiveMinimapMetrics eff{};
    GetEffectiveMinimapMetrics(&eff);
    if (eff.width <= 0.0f || eff.height <= 0.0f) return;

    int basePt[2] = {0, 0};
    reinterpret_cast<void(__cdecl*)(int*)>(0x496030)(basePt);
    int menuW = *reinterpret_cast<const int*>(0x589ddc);
    float menuRight = static_cast<float>(basePt[0] + menuW + 6);

    auto computeCornerXY = [&](int32_t boxW, int32_t boxH, int32_t* outX, int32_t* outY) {
        bool useCorner = (g_minimapPosOverride != 0) || IsOptionsPopupOpen(expectedState == 0x12u);
        int32_t alignX = useCorner ? ((previewPos == 1u || previewPos == 3u) ? 2 : 0) : eff.alignX;
        int32_t alignY = useCorner ? ((previewPos == 2u || previewPos == 3u) ? 2 : 0) : eff.alignY;
        int32_t offX   = useCorner ? 0 : eff.offX;
        int32_t offY   = useCorner ? 0 : eff.offY;

        int32_t x = 0;
        if (alignX == 1) x = (virtW - boxW) / 2;
        else if (alignX == 2) x = virtW - boxW;
        x = std::clamp(x + offX, 0, (std::max)(0, virtW - boxW));

        int32_t y = 0;
        if (alignY == 1) y = (virtH - boxH) / 2;
        else if (alignY == 2) y = virtH - boxH;
        y = std::clamp(y + offY, 0, (std::max)(0, virtH - boxH));

        *outX = x;
        *outY = y;
    };

    uint8_t* level = GetLevel();
    if (!forceSample && ws != nullptr && ws[0x2120] != 0 && level != nullptr) {
        float zoom = *reinterpret_cast<const float*>(sm + 0x34);
        void** ppBcfg = reinterpret_cast<void**(__cdecl*)()>(0x4764c0)();
        if (ppBcfg != nullptr && *ppBcfg != nullptr) {
            float minZ = reinterpret_cast<float(__thiscall*)(void*, uint32_t)>(0x4408c0)(*ppBcfg, *reinterpret_cast<const uint32_t*>(sm + 0x60));
            float maxZ = reinterpret_cast<float(__thiscall*)(void*, uint32_t)>(0x4408c0)(*ppBcfg, *reinterpret_cast<const uint32_t*>(sm + 0x68));
            if (zoom < minZ) zoom = minZ;
            if (zoom > maxZ) zoom = maxZ;
        }
        if (!std::isfinite(zoom) || zoom <= 0.01f) zoom = 1.0f;

        double camAngle = reinterpret_cast<double(__thiscall*)(void*)>(0x478950)(sm + 0xa0);
        double diffDeg = *reinterpret_cast<const double*>(0x586ae0) - camAngle;
        int rot = static_cast<int>(diffDeg) / 90;
        rot &= 0x80000003;
        if (rot < 0) rot = ((rot - 1) | ~3) + 1;

        int32_t boxW = static_cast<int32_t>(std::lround(eff.width));
        int32_t boxH = static_cast<int32_t>(std::lround(eff.height));
        if (rot == 1 || rot == 3) std::swap(boxW, boxH);

        int32_t mmX = 0, mmY = 0;
        if (dockBesideMenu) {
            float maxX = (std::max)(0.0f, static_cast<float>(virtW - boxW));
            float maxY = (std::max)(0.0f, static_cast<float>(virtH - boxH));
            mmX = static_cast<int32_t>(std::lround(std::clamp(menuRight + 20.0f, 0.0f, maxX)));
            mmY = static_cast<int32_t>(std::lround(std::clamp(static_cast<float>(4 + 0x75 + GetPauseContentShiftY()), 0.0f, maxY)));
        } else {
            computeCornerXY(boxW, boxH, &mmX, &mmY);
        }

        int32_t maxAlpha = *reinterpret_cast<const int32_t*>(ws + 0x210c);
        if (maxAlpha <= 0 || maxAlpha > 255) maxAlpha = 255;
        int32_t previewAlpha = dockBesideMenu ? maxAlpha : std::clamp((maxAlpha * 140) / 255, 1, 255);

        RenderLiveMinimapBox(
            renderer, sm, ws, dev, level, eff,
            eff.cellW / zoom, eff.cellH / zoom, rot,
            mmX, mmY, boxW, boxH, previewAlpha
        );
    } else {
        int32_t boxW = static_cast<int32_t>(std::lround(eff.width));
        int32_t boxH = static_cast<int32_t>(std::lround(eff.height));
        int32_t mmX = 0, mmY = 0;
        if (dockBesideMenu) {
            float maxX = (std::max)(0.0f, static_cast<float>(virtW - boxW));
            float maxY = (std::max)(0.0f, static_cast<float>(virtH - boxH));
            mmX = static_cast<int32_t>(std::lround(std::clamp(menuRight + 20.0f, 0.0f, maxX)));
            mmY = static_cast<int32_t>(std::lround(std::clamp(90.0f, 0.0f, maxY)));
        } else {
            computeCornerXY(boxW, boxH, &mmX, &mmY);
        }
        int32_t previewAlpha = dockBesideMenu ? 255 : 140;
        RenderSampleMinimapBox(renderer, dev, eff, mmX, mmY, previewAlpha);
    }
}

} // namespace edgemod

void RegisterMinimap() {
    // 1. Skip legacy 30 Hz tile loop & QuadBuffer upload in WorldScreen::updateMinimap (0x47e6a9 -> 0x47f528)
    //    while keeping the [ws+0x2108] fade-in/out stepper at 0x47e620..0x47e6a8 intact.
    const uint8_t exMmSkipBody[13] = {
        0x83, 0xbb, 0x08, 0x21, 0x00, 0x00, 0x00,
        0x0f, 0x84, 0x72, 0x0e, 0x00, 0x00
    };
    edgemod::RegRelJmpVa(0x47e6a9, exMmSkipBody, 13, 13, 0x47f528, 0x90);

    // 2. Replace WorldScreen::render minimap draw call (0x47b511, 7B) with 60+ FPS D3D9 renderer.
    const uint8_t exMmRender[7] = {0x8b, 0xcf, 0xe8, 0x48, 0xf4, 0xff, 0xff};
    edgemod::RegRelCall(0x47b511, exMmRender, 7, 7, &hk_renderCustomMinimap_bridge);

    // 3. Sync overridden minimap enable/position/size/range into WorldScreen immediately after initMinimap (0x47f943, 5B).
    const uint8_t exMmInit[5] = {0xe8, 0x18, 0xa3, 0xff, 0xff};
    edgemod::RegRelCall(0x47f943, exMmInit, 5, 5, &hk_initMinimapPostSync);
}
