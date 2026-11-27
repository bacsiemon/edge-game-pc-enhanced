#include "mod_state.h"
#include "motion_blur_shaders.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace edgemod {

uint8_t g_motionBlur = 0;              // 0=off, 1=low, 2=medium, 3=high
uint8_t g_blurMode   = 2;              // 0=camera, 1=object, 2=both
uint8_t g_motionBlurCaptureActive = 0; // fast gate checked inside SubMesh::render

} // namespace edgemod

namespace {

using namespace edgemod;

struct ObjKey {
    uintptr_t entity;
    uintptr_t subMesh;
    uint32_t  occ;

    bool operator==(const ObjKey& o) const noexcept {
        return entity == o.entity && subMesh == o.subMesh && occ == o.occ;
    }
};

struct ObjKeyHash {
    size_t operator()(const ObjKey& k) const noexcept {
        size_t h = static_cast<size_t>(k.entity);
        h ^= static_cast<size_t>(k.subMesh) * 0x9e3779b1u + (h << 6) + (h >> 2);
        h ^= static_cast<size_t>(k.occ) * 0x85ebca77u + (h << 6) + (h >> 2);
        return h;
    }
};

struct EntitySubPair {
    uintptr_t entity;
    uintptr_t subMesh;

    bool operator==(const EntitySubPair& o) const noexcept {
        return entity == o.entity && subMesh == o.subMesh;
    }
};

struct EntitySubPairHash {
    size_t operator()(const EntitySubPair& k) const noexcept {
        size_t h = static_cast<size_t>(k.entity);
        h ^= static_cast<size_t>(k.subMesh) * 0x9e3779b1u + (h << 6) + (h >> 2);
        return h;
    }
};

struct RecordedDraw {
    IDirect3DVertexBuffer9* vb;
    IDirect3DIndexBuffer9*  ib;
    UINT      stride;
    UINT      numVerts;
    UINT      primCount;
    DWORD     cullMode;
    DWORD     depthBias;
    D3DMATRIX currWorld;
    ObjKey    key;
    float     localCenter[3];
    bool      isDynamic;
    bool      isCube;
};

struct EntityScopeEntry {
    const void* entity;
    bool        isDynamic;
    bool        isCube;
};

struct QuadVertRhw {
    float x, y, z, rhw;
    float u, v;
};

constexpr DWORD kQuadFvf = D3DFVF_XYZRHW | D3DFVF_TEX1;

// Capture & history state
std::vector<EntityScopeEntry> s_entityStack;
std::vector<RecordedDraw>     s_frameDraws;
std::unordered_map<EntitySubPair, uint32_t, EntitySubPairHash> s_frameOccurMap;
std::unordered_map<ObjKey, D3DMATRIX, ObjKeyHash>              s_prevWorldMap;
std::unordered_map<ObjKey, D3DMATRIX, ObjKeyHash>              s_currWorldMap;

bool       s_hasCurrViewProj = false;
D3DMATRIX  s_currView{};
D3DMATRIX  s_currProj{};
D3DMATRIX  s_currVP{};

bool       s_hasPrevFrame = false;
D3DMATRIX  s_prevView{};
D3DMATRIX  s_prevProj{};
D3DMATRIX  s_prevVP{};

const void*   s_lastWs       = nullptr;
uint32_t      s_lastWsFrame  = 0;
uint32_t      s_lastState    = 0;
uint32_t      s_frameSeedIdx = 0;
bool          s_hasLastQpc   = false;
LARGE_INTEGER s_lastQpc{};
LARGE_INTEGER s_qpcFreq{};

template <typename T>
void SafeRelease(T*& ptr) {
    if (ptr != nullptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

struct MbResources {
    IDirect3DDevice9*            dev            = nullptr;
    UINT                         width          = 0;
    UINT                         height         = 0;
    D3DFORMAT                    colorFmt       = D3DFMT_UNKNOWN;
    UINT                         tileSize       = 24;
    UINT                         tileW          = 0;
    UINT                         tileH          = 0;
    bool                         initFailed     = false;
    IDirect3DTexture9*           sceneCopyTex   = nullptr;
    IDirect3DSurface9*           sceneCopySurf  = nullptr;
    IDirect3DTexture9*           velDepthTex    = nullptr;
    IDirect3DSurface9*           velDepthSurf   = nullptr;
    IDirect3DSurface9*           velDepthDs     = nullptr;
    IDirect3DTexture9*           tileMaxHTex    = nullptr;
    IDirect3DSurface9*           tileMaxHSurf   = nullptr;
    IDirect3DTexture9*           tileMaxTex     = nullptr;
    IDirect3DSurface9*           tileMaxSurf    = nullptr;
    IDirect3DTexture9*           neighborMaxTex = nullptr;
    IDirect3DSurface9*           neighborMaxSurf= nullptr;
    IDirect3DVertexDeclaration9* velDecl        = nullptr;
    IDirect3DVertexShader9*      vsVelocity     = nullptr;
    IDirect3DPixelShader9*       psVelocity     = nullptr;
    IDirect3DPixelShader9*       psTileMax1D    = nullptr;
    IDirect3DPixelShader9*       psNeighborMax  = nullptr;
    IDirect3DPixelShader9*       psGather       = nullptr;
    IDirect3DStateBlock9*        stateBlock     = nullptr;

    void ReleaseAll() {
        SafeRelease(stateBlock);
        SafeRelease(psGather);
        SafeRelease(psNeighborMax);
        SafeRelease(psTileMax1D);
        SafeRelease(psVelocity);
        SafeRelease(vsVelocity);
        SafeRelease(velDecl);
        SafeRelease(neighborMaxSurf);
        SafeRelease(neighborMaxTex);
        SafeRelease(tileMaxSurf);
        SafeRelease(tileMaxTex);
        SafeRelease(tileMaxHSurf);
        SafeRelease(tileMaxHTex);
        SafeRelease(velDepthDs);
        SafeRelease(velDepthSurf);
        SafeRelease(velDepthTex);
        SafeRelease(sceneCopySurf);
        SafeRelease(sceneCopyTex);
        dev = nullptr;
        width = 0;
        height = 0;
        colorFmt = D3DFMT_UNKNOWN;
        tileSize = 24;
        tileW = 0;
        tileH = 0;
        initFailed = false;
    }
};

MbResources s_res;

D3DMATRIX MulMat4(const D3DMATRIX& a, const D3DMATRIX& b) {
    D3DMATRIX r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] =
                a.m[i][0] * b.m[0][j] +
                a.m[i][1] * b.m[1][j] +
                a.m[i][2] * b.m[2][j] +
                a.m[i][3] * b.m[3][j];
        }
    }
    return r;
}

void ExtractCameraEye(const D3DMATRIX& view, float outEye[3]) {
    outEye[0] = -(view._41 * view._11 + view._42 * view._12 + view._43 * view._13);
    outEye[1] = -(view._41 * view._21 + view._42 * view._22 + view._43 * view._23);
    outEye[2] = -(view._41 * view._31 + view._42 * view._32 + view._43 * view._33);
}

void ExtractCameraForward(const D3DMATRIX& view, float outFwd[3]) {
    float fx = view._13;
    float fy = view._23;
    float fz = view._33;
    float len = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (len > 1e-5f) {
        fx /= len;
        fy /= len;
        fz /= len;
    }
    outFwd[0] = fx;
    outFwd[1] = fy;
    outFwd[2] = fz;
}

struct CubeSymTable {
    float m[24][3][3]{};
    CubeSymTable() {
        static constexpr float kAxes[6][3] = {
            { 1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f},
            { 0.f, 1.f, 0.f}, { 0.f,-1.f, 0.f},
            { 0.f, 0.f, 1.f}, { 0.f, 0.f,-1.f},
        };
        int idx = 0;
        for (int a = 0; a < 6; ++a) {
            for (int b = 0; b < 6; ++b) {
                if ((a / 2) == (b / 2)) continue;
                const float* r0 = kAxes[a];
                const float* r1 = kAxes[b];
                float r2[3] = {
                    r0[1] * r1[2] - r0[2] * r1[1],
                    r0[2] * r1[0] - r0[0] * r1[2],
                    r0[0] * r1[1] - r0[1] * r1[0],
                };
                for (int c = 0; c < 3; ++c) {
                    m[idx][0][c] = r0[c];
                    m[idx][1][c] = r1[c];
                    m[idx][2][c] = r2[c];
                }
                ++idx;
            }
        }
    }
};

const CubeSymTable& GetCubeSymTable() {
    static const CubeSymTable tbl;
    return tbl;
}

bool ExtractNormalizedRot3(const D3DMATRIX& mat, float outR[3][3], float* outDetSign) {
    for (int i = 0; i < 3; ++i) {
        float x = mat.m[i][0];
        float y = mat.m[i][1];
        float z = mat.m[i][2];
        float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-4f) return false;
        outR[i][0] = x / len;
        outR[i][1] = y / len;
        outR[i][2] = z / len;
    }
    float det =
        outR[0][0] * (outR[1][1] * outR[2][2] - outR[1][2] * outR[2][1]) -
        outR[0][1] * (outR[1][0] * outR[2][2] - outR[1][2] * outR[2][0]) +
        outR[0][2] * (outR[1][0] * outR[2][1] - outR[1][1] * outR[2][0]);
    if (std::fabs(det) < 0.5f) return false;
    *outDetSign = (det > 0.0f) ? 1.0f : -1.0f;
    return true;
}

// Align prevWorld's local 90-degree orientation to currWorld via the 24 proper cube symmetries SO(3)_cube,
// preventing single-frame streaks when EDGE wraps a cube's 0..90 deg roll animation back to 0 deg.
bool AlignCubePrevWorld(
    const D3DMATRIX& prevWorld,
    const D3DMATRIX& currWorld,
    const float cLocal[3],
    D3DMATRIX* outAligned
) {
    // Reject large world-space teleports (respawn / level jump)
    float prevCx = cLocal[0] * prevWorld._11 + cLocal[1] * prevWorld._21 + cLocal[2] * prevWorld._31 + prevWorld._41;
    float prevCy = cLocal[0] * prevWorld._12 + cLocal[1] * prevWorld._22 + cLocal[2] * prevWorld._32 + prevWorld._42;
    float prevCz = cLocal[0] * prevWorld._13 + cLocal[1] * prevWorld._23 + cLocal[2] * prevWorld._33 + prevWorld._43;
    float currCx = cLocal[0] * currWorld._11 + cLocal[1] * currWorld._21 + cLocal[2] * currWorld._31 + currWorld._41;
    float currCy = cLocal[0] * currWorld._12 + cLocal[1] * currWorld._22 + cLocal[2] * currWorld._32 + currWorld._42;
    float currCz = cLocal[0] * currWorld._13 + cLocal[1] * currWorld._23 + cLocal[2] * currWorld._33 + currWorld._43;
    float dx = currCx - prevCx;
    float dy = currCy - prevCy;
    float dz = currCz - prevCz;
    if (dx * dx + dy * dy + dz * dz > 45.0f * 45.0f) {
        return false;
    }

    float rPrev[3][3]{}, rCurr[3][3]{};
    float detPrev = 0.0f, detCurr = 0.0f;
    if (!ExtractNormalizedRot3(prevWorld, rPrev, &detPrev) ||
        !ExtractNormalizedRot3(currWorld, rCurr, &detCurr) ||
        detPrev * detCurr <= 0.0f) {
        return false;
    }

    const CubeSymTable& tbl = GetCubeSymTable();
    int bestIdx = 0;
    float bestTrace = -1e9f;
    for (int k = 0; k < 24; ++k) {
        const auto& S = tbl.m[k];
        float tr = 0.0f;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                float sr = S[i][0] * rPrev[0][j] + S[i][1] * rPrev[1][j] + S[i][2] * rPrev[2][j];
                tr += rCurr[i][j] * sr;
            }
        }
        if (tr > bestTrace) {
            bestTrace = tr;
            bestIdx = k;
        }
    }

    if (bestTrace < 1.9f) {
        return false;
    }

    // M_sym = T(-cLocal) * S * T(+cLocal) in row-vector convention
    const auto& bestS = tbl.m[bestIdx];
    D3DMATRIX mSym{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            mSym.m[i][j] = bestS[i][j];
        }
    }
    mSym._41 = cLocal[0] - (cLocal[0] * bestS[0][0] + cLocal[1] * bestS[1][0] + cLocal[2] * bestS[2][0]);
    mSym._42 = cLocal[1] - (cLocal[0] * bestS[0][1] + cLocal[1] * bestS[1][1] + cLocal[2] * bestS[2][1]);
    mSym._43 = cLocal[2] - (cLocal[0] * bestS[0][2] + cLocal[1] * bestS[1][2] + cLocal[2] * bestS[2][2]);
    mSym._44 = 1.0f;

    *outAligned = MulMat4(mSym, prevWorld);
    return true;
}

bool CreateRtTexAndSurf(
    IDirect3DDevice9* dev, UINT w, UINT h, D3DFORMAT fmt,
    IDirect3DTexture9** outTex, IDirect3DSurface9** outSurf
) {
    if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, outTex, nullptr))) {
        return false;
    }
    if (*outTex == nullptr || FAILED((*outTex)->GetSurfaceLevel(0, outSurf)) || *outSurf == nullptr) {
        return false;
    }
    return true;
}

bool EnsureMotionBlurResources(IDirect3DDevice9* dev, UINT w, UINT h, D3DFORMAT colorFmt) {
    if (dev == nullptr || w == 0 || h == 0) return false;
    if (s_res.dev == dev && s_res.width == w && s_res.height == h && s_res.colorFmt == colorFmt) {
        return !s_res.initFailed;
    }

    s_res.ReleaseAll();
    s_res.dev = dev;
    s_res.width = w;
    s_res.height = h;
    s_res.colorFmt = colorFmt;

    IDirect3D9* d3d9 = nullptr;
    if (FAILED(dev->GetDirect3D(&d3d9)) || d3d9 == nullptr) {
        s_res.initFailed = true;
        return false;
    }

    D3DCAPS9 caps{};
    D3DDEVICE_CREATION_PARAMETERS cp{};
    HRESULT hrCaps = d3d9->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps);
    HRESULT hrCp   = dev->GetCreationParameters(&cp);
    UINT adapter   = SUCCEEDED(hrCp) ? cp.AdapterOrdinal : 0u;
    D3DDEVTYPE devType = SUCCEEDED(hrCp) ? cp.DeviceType : D3DDEVTYPE_HAL;
    if (SUCCEEDED(hrCp)) {
        hrCaps = d3d9->GetDeviceCaps(adapter, devType, &caps);
    }

    bool sm3Ok = SUCCEEDED(hrCaps) &&
                 caps.VertexShaderVersion >= D3DVS_VERSION(3, 0) &&
                 caps.PixelShaderVersion >= D3DPS_VERSION(3, 0);

    D3DDISPLAYMODE dispMode{};
    bool fp16Ok = false;
    if (SUCCEEDED(d3d9->GetAdapterDisplayMode(adapter, &dispMode))) {
        fp16Ok = SUCCEEDED(d3d9->CheckDeviceFormat(
            adapter, devType, dispMode.Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F
        ));
    }
    d3d9->Release();

    if (!sm3Ok || !fp16Ok) {
        s_res.initFailed = true;
        return false;
    }

    UINT tileSize = std::clamp<UINT>((h * 32u + 540u) / 1080u, 16u, 64u);
    UINT tileW = (w + tileSize - 1u) / tileSize;
    UINT tileH = (h + tileSize - 1u) / tileSize;
    s_res.tileSize = tileSize;
    s_res.tileW = tileW;
    s_res.tileH = tileH;

    auto failInit = [&]() {
        s_res.ReleaseAll();
        s_res.dev = dev;
        s_res.width = w;
        s_res.height = h;
        s_res.colorFmt = colorFmt;
        s_res.initFailed = true;
        return false;
    };

    if (!CreateRtTexAndSurf(dev, w, h, colorFmt, &s_res.sceneCopyTex, &s_res.sceneCopySurf) ||
        !CreateRtTexAndSurf(dev, w, h, D3DFMT_A16B16G16R16F, &s_res.velDepthTex, &s_res.velDepthSurf) ||
        !CreateRtTexAndSurf(dev, tileW, h, D3DFMT_A16B16G16R16F, &s_res.tileMaxHTex, &s_res.tileMaxHSurf) ||
        !CreateRtTexAndSurf(dev, tileW, tileH, D3DFMT_A16B16G16R16F, &s_res.tileMaxTex, &s_res.tileMaxSurf) ||
        !CreateRtTexAndSurf(dev, tileW, tileH, D3DFMT_A16B16G16R16F, &s_res.neighborMaxTex, &s_res.neighborMaxSurf)) {
        return failInit();
    }

    if (FAILED(dev->CreateDepthStencilSurface(
            w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &s_res.velDepthDs, nullptr)) ||
        s_res.velDepthDs == nullptr) {
        return failInit();
    }

    static const D3DVERTEXELEMENT9 kVelElements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        D3DDECL_END()
    };
    if (FAILED(dev->CreateVertexDeclaration(kVelElements, &s_res.velDecl)) ||
        FAILED(dev->CreateVertexShader(reinterpret_cast<const DWORD*>(shaders::kVsVelocity), &s_res.vsVelocity)) ||
        FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(shaders::kPsVelocity), &s_res.psVelocity)) ||
        FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(shaders::kPsTileMax1D), &s_res.psTileMax1D)) ||
        FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(shaders::kPsNeighborMax), &s_res.psNeighborMax)) ||
        FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(shaders::kPsGather), &s_res.psGather)) ||
        FAILED(dev->CreateStateBlock(D3DSBT_ALL, &s_res.stateBlock))) {
        return failInit();
    }

    return true;
}

void DrawFullscreenQuad(IDirect3DDevice9* dev, UINT w, UINT h, float maxU = 1.0f, float maxV = 1.0f) {
    float x1 = static_cast<float>(w) - 0.5f;
    float y1 = static_cast<float>(h) - 0.5f;
    QuadVertRhw verts[4] = {
        {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
        {   x1, -0.5f, 0.0f, 1.0f, maxU, 0.0f},
        {-0.5f,    y1, 0.0f, 1.0f, 0.0f, maxV},
        {   x1,    y1, 0.0f, 1.0f, maxU, maxV},
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(QuadVertRhw));
}

void SetClampSampler(IDirect3DDevice9* dev, DWORD stage, bool linear = false) {
    DWORD filter = linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    dev->SetSamplerState(stage, D3DSAMP_MINFILTER, filter);
    dev->SetSamplerState(stage, D3DSAMP_MAGFILTER, filter);
    dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, 0);
}

void SetViewportFull(IDirect3DDevice9* dev, UINT w, UINT h) {
    D3DVIEWPORT9 vp{0, 0, w, h, 0.0f, 1.0f};
    dev->SetViewport(&vp);
}

void InvalidateEngineTextureCache() {
    uint8_t stageCount = *reinterpret_cast<const uint8_t*>(0x5f87c9);
    void**  texCache   = *reinterpret_cast<void***>(0x5f87cc);
    if (texCache != nullptr) {
        for (uint8_t i = 0; i < stageCount; ++i) {
            texCache[i] = nullptr;
        }
    }
}

void AdvanceMotionBlurHistory() {
    s_prevView = s_currView;
    s_prevProj = s_currProj;
    s_prevVP   = s_currVP;
    s_prevWorldMap.swap(s_currWorldMap);
    s_hasPrevFrame = true;
}

void __cdecl MotionBlur_BeginWorldFrame(const uint8_t* ws, const uint8_t* renderer) {
    g_motionBlurCaptureActive = 0;
    s_entityStack.clear();
    s_frameDraws.clear();
    s_frameOccurMap.clear();
    s_currWorldMap.clear();
    s_hasCurrViewProj = false;

    if (g_motionBlur == 0u || ws == nullptr || renderer == nullptr) {
        ResetMotionBlurHistory();
        return;
    }

    const uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) {
        ResetMotionBlurHistory();
        return;
    }

    uint32_t state = *reinterpret_cast<const uint32_t*>(sm + 0x50);
    bool previewActive = IsGraphicsPreviewActive();
    bool allowedState = (state == 0x11u || state == 0x13u || (state >= 0x14u && state <= 0x16u) || previewActive);
    if (!allowedState || ws[0x2120] == 0) {
        ResetMotionBlurHistory();
        s_lastState = state;
        return;
    }

    uint32_t wsFrame = *reinterpret_cast<const uint32_t*>(ws + 0x04);
    bool allowTransition = (s_lastState == 0x11u && state == 0x13u);
    if (ws != s_lastWs || wsFrame < s_lastWsFrame || (state != s_lastState && !allowTransition)) {
        ResetMotionBlurHistory();
    }
    s_lastWs      = ws;
    s_lastWsFrame = wsFrame;
    s_lastState   = state;

    s_frameDraws.reserve(512);
    g_motionBlurCaptureActive = 1;
}

void __cdecl MotionBlur_EndWorldFrameAndApply(const uint8_t* renderer) {
    bool wasCapturing = (g_motionBlurCaptureActive != 0u);
    g_motionBlurCaptureActive = 0;
    s_entityStack.clear();

    if (!wasCapturing || g_motionBlur == 0u || renderer == nullptr || !s_hasCurrViewProj) {
        return;
    }

    IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9* const*>(renderer + 0xcc);
    if (dev == nullptr) {
        ResetMotionBlurHistory();
        return;
    }

    if (s_qpcFreq.QuadPart == 0) {
        QueryPerformanceFrequency(&s_qpcFreq);
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float dt = 0.0f;
    if (s_hasLastQpc && s_qpcFreq.QuadPart > 0) {
        dt = static_cast<float>(
            static_cast<double>(now.QuadPart - s_lastQpc.QuadPart) /
            static_cast<double>(s_qpcFreq.QuadPart)
        );
    }
    s_lastQpc = now;
    s_hasLastQpc = true;

    bool discontinuity = false;
    if (!s_hasPrevFrame || dt <= 0.0005f || dt > 0.25f) {
        discontinuity = true;
    } else if (std::fabs(s_currProj._44 - s_prevProj._44) > 0.1f) {
        // Instant ortho <-> perspective switch
        discontinuity = true;
    } else {
        float curEye[3]{}, prevEye[3]{};
        float curFwd[3]{}, prevFwd[3]{};
        ExtractCameraEye(s_currView, curEye);
        ExtractCameraEye(s_prevView, prevEye);
        ExtractCameraForward(s_currView, curFwd);
        ExtractCameraForward(s_prevView, prevFwd);
        float fwdDot = curFwd[0] * prevFwd[0] + curFwd[1] * prevFwd[1] + curFwd[2] * prevFwd[2];
        if (fwdDot < 0.85f) {
            discontinuity = true;
        } else {
            float dx = curEye[0] - prevEye[0];
            float dy = curEye[1] - prevEye[1];
            float dz = curEye[2] - prevEye[2];
            // Only flag large lateral camera teleports; allow smooth dolly zooms along the view ray
            float along = dx * curFwd[0] + dy * curFwd[1] + dz * curFwd[2];
            float lx = dx - curFwd[0] * along;
            float ly = dy - curFwd[1] * along;
            float lz = dz - curFwd[2] * along;
            if (lx * lx + ly * ly + lz * lz > 250.0f * 250.0f) {
                discontinuity = true;
            }
        }
    }

    if (discontinuity || s_frameDraws.empty()) {
        AdvanceMotionBlurHistory();
        return;
    }

    IDirect3DSurface9* savedRt = nullptr;
    IDirect3DSurface9* savedDs = nullptr;
    if (FAILED(dev->GetRenderTarget(0, &savedRt)) || savedRt == nullptr) {
        SafeRelease(savedRt);
        ResetMotionBlurHistory();
        return;
    }
    dev->GetDepthStencilSurface(&savedDs);

    D3DSURFACE_DESC rtDesc{};
    if (FAILED(savedRt->GetDesc(&rtDesc)) || rtDesc.Width == 0 || rtDesc.Height == 0) {
        SafeRelease(savedDs);
        SafeRelease(savedRt);
        ResetMotionBlurHistory();
        return;
    }

    if (!EnsureMotionBlurResources(dev, rtDesc.Width, rtDesc.Height, rtDesc.Format)) {
        SafeRelease(savedDs);
        SafeRelease(savedRt);
        AdvanceMotionBlurHistory();
        return;
    }

    const UINT physW = s_res.width;
    const UINT physH = s_res.height;

    if (FAILED(s_res.stateBlock->Capture())) {
        SafeRelease(savedDs);
        SafeRelease(savedRt);
        ResetMotionBlurHistory();
        return;
    }

    // Resolve (or copy) current 3D scene color before overwriting savedRt
    if (FAILED(dev->StretchRect(savedRt, nullptr, s_res.sceneCopySurf, nullptr, D3DTEXF_NONE))) {
        s_res.stateBlock->Apply();
        SafeRelease(savedDs);
        SafeRelease(savedRt);
        ResetMotionBlurHistory();
        return;
    }

    // ------------------------------------------------------------------
    // Pass 1: Velocity + Linear Depth
    // ------------------------------------------------------------------
    dev->SetRenderTarget(0, s_res.velDepthSurf);
    dev->SetDepthStencilSurface(s_res.velDepthDs);
    SetViewportFull(dev, physW, physH);

    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x00000000u, 1.0f, 0);

    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0Fu);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);
    dev->SetRenderState(D3DRS_DEPTHBIAS, 0);
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);

    dev->SetVertexDeclaration(s_res.velDecl);
    dev->SetVertexShader(s_res.vsVelocity);
    dev->SetPixelShader(s_res.psVelocity);

    // Half-exposure shutter presets (stored vel = half blur span) + tile-bounded max radius
    float shutterSec = 0.014f;
    float maxRadiusBase = 22.0f;
    if (g_motionBlur == 1u) {
        shutterSec = 0.008f;
        maxRadiusBase = 14.0f;
    } else if (g_motionBlur >= 3u) {
        shutterSec = 0.022f;
        maxRadiusBase = 32.0f;
    }
    float velScale = (0.5f * shutterSec) / dt;
    float maxRadiusPx = std::clamp(
        maxRadiusBase * (static_cast<float>(physH) / 1080.0f),
        6.0f,
        static_cast<float>(s_res.tileSize)
    );

    const float velParams[4] = {velScale, maxRadiusPx, 0.0f, 0.0f};
    const float texelSize[4] = {
        1.0f / static_cast<float>(physW),
        1.0f / static_cast<float>(physH),
        static_cast<float>(physW),
        static_cast<float>(physH),
    };
    dev->SetPixelShaderConstantF(0, velParams, 1);
    dev->SetPixelShaderConstantF(1, texelSize, 1);

    const uint8_t mode = (g_blurMode <= 2u) ? g_blurMode : 2u;
    for (const RecordedDraw& d : s_frameDraws) {
        D3DMATRIX currWVP = MulMat4(d.currWorld, s_currVP);
        D3DMATRIX currWV  = MulMat4(d.currWorld, s_currView);
        D3DMATRIX prevWVP{};

        const D3DMATRIX* pPrevWorld = nullptr;
        D3DMATRIX alignedPrev{};
        if (d.isDynamic && mode != 0u) {
            auto it = s_prevWorldMap.find(d.key);
            if (it != s_prevWorldMap.end()) {
                pPrevWorld = &it->second;
                if (d.isCube) {
                    if (AlignCubePrevWorld(*pPrevWorld, d.currWorld, d.localCenter, &alignedPrev)) {
                        pPrevWorld = &alignedPrev;
                    } else {
                        pPrevWorld = nullptr;
                    }
                }
            }
        }

        if (mode == 0u) {
            // Camera only
            prevWVP = MulMat4(d.currWorld, s_prevVP);
        } else if (mode == 1u) {
            // Object only
            if (pPrevWorld != nullptr) {
                prevWVP = MulMat4(*pPrevWorld, s_currVP);
            } else {
                prevWVP = currWVP;
            }
        } else {
            // Both camera + object
            if (pPrevWorld != nullptr) {
                prevWVP = MulMat4(*pPrevWorld, s_prevVP);
            } else {
                prevWVP = MulMat4(d.currWorld, s_prevVP);
            }
        }

        const float currWvZ[4] = {currWV._13, currWV._23, currWV._33, currWV._43};
        dev->SetVertexShaderConstantF(0, &currWVP._11, 4);
        dev->SetVertexShaderConstantF(4, &prevWVP._11, 4);
        dev->SetVertexShaderConstantF(8, currWvZ, 1);

        dev->SetRenderState(D3DRS_CULLMODE, d.cullMode);
        dev->SetRenderState(D3DRS_DEPTHBIAS, d.depthBias);
        dev->SetStreamSource(0, d.vb, 0, d.stride);
        dev->SetIndices(d.ib);
        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d.numVerts, 0, d.primCount);
    }

    // ------------------------------------------------------------------
    // Pass 2..4: Separable TileMaxH -> TileMaxV -> NeighborMax
    // ------------------------------------------------------------------
    dev->SetDepthStencilSurface(nullptr);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetVertexShader(nullptr);
    dev->SetFVF(kQuadFvf);
    dev->SetIndices(nullptr);

    const UINT tileSize = s_res.tileSize;
    const UINT tileW = s_res.tileW;
    const UINT tileH = s_res.tileH;
    const float tileMaxU = static_cast<float>(tileW * tileSize) / static_cast<float>(physW);
    const float tileMaxV = static_cast<float>(tileH * tileSize) / static_cast<float>(physH);

    // Pass 2: TileMaxH (exact 1-pixel step across tileSize)
    const float tileInfo[4] = {static_cast<float>(tileSize), 0.0f, 0.0f, 0.0f};
    dev->SetRenderTarget(0, s_res.tileMaxHSurf);
    SetViewportFull(dev, tileW, physH);
    dev->SetTexture(0, s_res.velDepthTex);
    SetClampSampler(dev, 0, false);
    dev->SetPixelShader(s_res.psTileMax1D);
    {
        float stepH[4] = {
            1.0f / static_cast<float>(physW),
            0.0f,
            -0.5f * static_cast<float>(tileSize - 1u) / static_cast<float>(physW),
            0.0f
        };
        dev->SetPixelShaderConstantF(0, stepH, 1);
        dev->SetPixelShaderConstantF(1, tileInfo, 1);
    }
    DrawFullscreenQuad(dev, tileW, physH, tileMaxU, 1.0f);

    // Pass 3: TileMaxV (exact 1-pixel step across tileSize)
    dev->SetRenderTarget(0, s_res.tileMaxSurf);
    SetViewportFull(dev, tileW, tileH);
    dev->SetTexture(0, s_res.tileMaxHTex);
    SetClampSampler(dev, 0, false);
    {
        float stepV[4] = {
            0.0f,
            1.0f / static_cast<float>(physH),
            0.0f,
            -0.5f * static_cast<float>(tileSize - 1u) / static_cast<float>(physH)
        };
        dev->SetPixelShaderConstantF(0, stepV, 1);
        dev->SetPixelShaderConstantF(1, tileInfo, 1);
    }
    DrawFullscreenQuad(dev, tileW, tileH, 1.0f, tileMaxV);

    // Pass 4: NeighborMax
    dev->SetRenderTarget(0, s_res.neighborMaxSurf);
    SetViewportFull(dev, tileW, tileH);
    dev->SetTexture(0, s_res.tileMaxTex);
    SetClampSampler(dev, 0, false);
    dev->SetPixelShader(s_res.psNeighborMax);
    {
        float nmStep[4] = {
            1.0f / static_cast<float>(tileW),
            1.0f / static_cast<float>(tileH),
            0.0f,
            0.0f
        };
        dev->SetPixelShaderConstantF(0, nmStep, 1);
    }
    DrawFullscreenQuad(dev, tileW, tileH, 1.0f, 1.0f);

    // ------------------------------------------------------------------
    // Pass 5: Depth-Aware Reconstruction Gather -> savedRt
    // ------------------------------------------------------------------
    dev->SetRenderTarget(0, savedRt);
    SetViewportFull(dev, physW, physH);
    dev->SetTexture(0, s_res.sceneCopyTex);
    dev->SetTexture(1, s_res.velDepthTex);
    dev->SetTexture(2, s_res.neighborMaxTex);
    SetClampSampler(dev, 0, true);
    SetClampSampler(dev, 1, false);
    SetClampSampler(dev, 2, false);
    dev->SetPixelShader(s_res.psGather);
    {
        const float gatherCfg[4] = {
            0.015f, // 15 world units soft Z transition
            0.35f,  // skip pixels below 0.35 px velocity
            static_cast<float>(s_frameSeedIdx & 63u),
            0.0f
        };
        const float nmUvScale[4] = {
            static_cast<float>(physW) / static_cast<float>(tileW * tileSize),
            static_cast<float>(physH) / static_cast<float>(tileH * tileSize),
            0.0f,
            0.0f
        };
        dev->SetPixelShaderConstantF(0, texelSize, 1);
        dev->SetPixelShaderConstantF(1, gatherCfg, 1);
        dev->SetPixelShaderConstantF(2, nmUvScale, 1);
    }
    DrawFullscreenQuad(dev, physW, physH, 1.0f, 1.0f);

    // Unbind textures & restore original render target + state
    dev->SetTexture(0, nullptr);
    dev->SetTexture(1, nullptr);
    dev->SetTexture(2, nullptr);
    dev->SetRenderTarget(0, savedRt);
    dev->SetDepthStencilSurface(savedDs);
    s_res.stateBlock->Apply();
    InvalidateEngineTextureCache();

    SafeRelease(savedDs);
    SafeRelease(savedRt);

    // Advance history
    AdvanceMotionBlurHistory();
    ++s_frameSeedIdx;
}

void __cdecl RenderDeferredEdgeTimer3D(const uint8_t* renderer) {
    if (g_motionBlur == 0u || renderer == nullptr) return;
    IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9* const*>(renderer + 0xcc);
    if (dev == nullptr) {
        RenderEdgeTimer3D();
        return;
    }

    D3DMATRIX savedView{}, savedProj{};
    bool restoredMat = false;
    if (s_hasCurrViewProj &&
        SUCCEEDED(dev->GetTransform(D3DTS_VIEW, &savedView)) &&
        SUCCEEDED(dev->GetTransform(D3DTS_PROJECTION, &savedProj))) {
        dev->SetTransform(D3DTS_VIEW, &s_currView);
        dev->SetTransform(D3DTS_PROJECTION, &s_currProj);
        restoredMat = true;
    }

    DWORD savedScissor = FALSE;
    DWORD savedStencil = FALSE;
    DWORD savedBlendOp = D3DBLENDOP_ADD;
    DWORD savedSepAlpha = FALSE;
    DWORD savedColorWrite = 0x0Fu;
    DWORD savedSrgbWrite = FALSE;
    dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &savedScissor);
    dev->GetRenderState(D3DRS_STENCILENABLE, &savedStencil);
    dev->GetRenderState(D3DRS_BLENDOP, &savedBlendOp);
    dev->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &savedSepAlpha);
    dev->GetRenderState(D3DRS_COLORWRITEENABLE, &savedColorWrite);
    dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &savedSrgbWrite);

    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0Fu);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);

    D3DVIEWPORT9 savedVp{};
    bool restoredVp = false;
    UINT vpW = 0, vpH = 0;
    IDirect3DSurface9* curRt = nullptr;
    if (SUCCEEDED(dev->GetRenderTarget(0, &curRt)) && curRt != nullptr) {
        D3DSURFACE_DESC desc{};
        if (SUCCEEDED(curRt->GetDesc(&desc))) {
            vpW = desc.Width;
            vpH = desc.Height;
        }
        curRt->Release();
    }
    if (vpW == 0 || vpH == 0) {
        int32_t physW = *reinterpret_cast<const int32_t*>(renderer + 0xb8);
        int32_t physH = *reinterpret_cast<const int32_t*>(renderer + 0xbc);
        if (physW > 0 && physH > 0) {
            vpW = static_cast<UINT>(physW);
            vpH = static_cast<UINT>(physH);
        }
    }
    if (vpW > 0 && vpH > 0 && SUCCEEDED(dev->GetViewport(&savedVp))) {
        SetViewportFull(dev, vpW, vpH);
        restoredVp = true;
    }

    RenderEdgeTimer3D();

    if (restoredVp) {
        dev->SetViewport(&savedVp);
    }
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, savedSrgbWrite);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, savedColorWrite);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, savedSepAlpha);
    dev->SetRenderState(D3DRS_BLENDOP, savedBlendOp);
    dev->SetRenderState(D3DRS_STENCILENABLE, savedStencil);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, savedScissor);
    if (restoredMat) {
        dev->SetTransform(D3DTS_VIEW, &savedView);
        dev->SetTransform(D3DTS_PROJECTION, &savedProj);
    }
}

static void __declspec(naked) hk_worldSetClearColor() {
    // At 0x47b20f: replaces `call 0x405f90` (__thiscall Renderer::setClearColor(colorPtr))
    // ecx = Renderer*, edi = WorldScreen*, [esp+4] = colorPtr
    __asm {
        push ecx
        push edi
        push DWORD PTR [esp + 12]
        mov eax, 0x405f90
        call eax
        call MotionBlur_BeginWorldFrame
        add esp, 8
        ret 4
    }
}

static void __declspec(naked) hk_worldEnd3D() {
    // At 0x47b43b: replaces `call 0x4076d0` (__thiscall Renderer::end3D())
    // ecx = Renderer*
    __asm {
        push ecx
        call MotionBlur_EndWorldFrameAndApply
        call RenderDeferredEdgeTimer3D
        pop ecx
        mov eax, 0x4076d0
        jmp eax
    }
}

static void __declspec(naked) hk_onLostDevice() {
    // At 0x44db7d: replaces `call 0x406150`
    __asm {
        push ecx
        call ReleaseMotionBlurResources
        pop ecx
        mov eax, 0x406150
        jmp eax
    }
}

static void __declspec(naked) hk_onDestroyDevice() {
    // At 0x44dc16: replaces `call 0x408450`
    __asm {
        push ecx
        call ReleaseMotionBlurResources
        pop ecx
        mov eax, 0x408450
        jmp eax
    }
}

static void __declspec(naked) hk_cubeResetToGrid() {
    // At 0x48dc3c: replaces `call 0x48c680` (inside Cube::resetToGrid; ecx = Cube*)
    __asm {
        push ecx
        call ResetMotionBlurEntity
        pop ecx
        mov eax, 0x48c680
        jmp eax
    }
}

} // namespace

namespace edgemod {

void __cdecl MotionBlur_PushEntity(const void* entityPtr, bool isDynamic, bool isCube) {
    if (s_entityStack.size() < 16u) {
        s_entityStack.push_back({entityPtr, isDynamic, isCube});
    }
}

void __cdecl MotionBlur_PopEntity() {
    if (!s_entityStack.empty()) {
        s_entityStack.pop_back();
    }
}

void __cdecl MotionBlur_RecordSubMeshDraw(
    void* d3dDevPtr, const uint8_t* subMesh, const uint8_t* renderParams
) {
    if (g_motionBlurCaptureActive == 0u || d3dDevPtr == nullptr || subMesh == nullptr) return;
    if (renderParams != nullptr && *reinterpret_cast<const uint32_t*>(renderParams + 0x0c) == 3u) return;
    if (IsCustomHintGhostOverrideActive()) return;

    const uint8_t* renderer = GetRenderer();
    if (renderer != nullptr && renderer[0xa4] != 0u) return; // inside Renderer::renderTransparents

    IDirect3DDevice9* dev = static_cast<IDirect3DDevice9*>(d3dDevPtr);
    DWORD zEnable = D3DZB_FALSE;
    DWORD zWrite  = FALSE;
    if (FAILED(dev->GetRenderState(D3DRS_ZENABLE, &zEnable)) || zEnable == D3DZB_FALSE) {
        return;
    }
    if (FAILED(dev->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite)) || zWrite == FALSE) {
        return;
    }

    if (!s_hasCurrViewProj) {
        if (SUCCEEDED(dev->GetTransform(D3DTS_VIEW, &s_currView)) &&
            SUCCEEDED(dev->GetTransform(D3DTS_PROJECTION, &s_currProj))) {
            s_currVP = MulMat4(s_currView, s_currProj);
            s_hasCurrViewProj = true;
        }
    }

    IDirect3DVertexBuffer9* vb = *reinterpret_cast<IDirect3DVertexBuffer9* const*>(subMesh + 0xa0);
    IDirect3DIndexBuffer9*  ib = *reinterpret_cast<IDirect3DIndexBuffer9* const*>(subMesh + 0xa4);
    UINT stride    = *reinterpret_cast<const UINT*>(subMesh + 0x84);
    UINT primCount = *reinterpret_cast<const UINT*>(subMesh + 0x9c);
    const uint8_t* posBegin = *reinterpret_cast<const uint8_t* const*>(subMesh + 0x10);
    const uint8_t* posEnd   = *reinterpret_cast<const uint8_t* const*>(subMesh + 0x14);
    UINT numVerts  = (posEnd > posBegin) ? static_cast<UINT>((posEnd - posBegin) / 12u) : 0u;
    if (vb == nullptr || ib == nullptr || stride < 12u || numVerts == 0u || primCount == 0u) return;

    D3DMATRIX currWorld{};
    if (FAILED(dev->GetTransform(D3DTS_WORLD, &currWorld))) return;
    DWORD cullMode = D3DCULL_CCW;
    dev->GetRenderState(D3DRS_CULLMODE, &cullMode);
    DWORD depthBias = 0;
    dev->GetRenderState(D3DRS_DEPTHBIAS, &depthBias);

    const void* entityPtr = s_entityStack.empty() ? static_cast<const void*>(subMesh) : s_entityStack.back().entity;
    bool isDynamic = !s_entityStack.empty() && s_entityStack.back().isDynamic;
    bool isCube    = (isDynamic && !s_entityStack.empty()) ? s_entityStack.back().isCube : false;

    float localCenter[3] = {0.0f, 0.0f, 0.0f};
    if (isCube && posBegin != nullptr && numVerts >= 8u && numVerts <= 4096u) {
        const float* fp = reinterpret_cast<const float*>(posBegin);
        float minX = fp[0], maxX = fp[0];
        float minY = fp[1], maxY = fp[1];
        float minZ = fp[2], maxZ = fp[2];
        for (UINT i = 1; i < numVerts; ++i) {
            float vx = fp[i * 3u + 0u];
            float vy = fp[i * 3u + 1u];
            float vz = fp[i * 3u + 2u];
            if (vx < minX) minX = vx; else if (vx > maxX) maxX = vx;
            if (vy < minY) minY = vy; else if (vy > maxY) maxY = vy;
            if (vz < minZ) minZ = vz; else if (vz > maxZ) maxZ = vz;
        }
        localCenter[0] = 0.5f * (minX + maxX);
        localCenter[1] = 0.5f * (minY + maxY);
        localCenter[2] = 0.5f * (minZ + maxZ);
    }

    ObjKey key{reinterpret_cast<uintptr_t>(entityPtr), reinterpret_cast<uintptr_t>(subMesh), 0u};
    if (isDynamic) {
        EntitySubPair pair{key.entity, key.subMesh};
        uint32_t& occ = s_frameOccurMap[pair];
        key.occ = occ++;
        s_currWorldMap[key] = currWorld;
    }

    RecordedDraw rec{};
    rec.vb             = vb;
    rec.ib             = ib;
    rec.stride         = stride;
    rec.numVerts       = numVerts;
    rec.primCount      = primCount;
    rec.cullMode       = cullMode;
    rec.depthBias      = depthBias;
    rec.currWorld      = currWorld;
    rec.key            = key;
    rec.localCenter[0] = localCenter[0];
    rec.localCenter[1] = localCenter[1];
    rec.localCenter[2] = localCenter[2];
    rec.isDynamic      = isDynamic;
    rec.isCube         = isCube;
    s_frameDraws.push_back(rec);
}

void __cdecl ResetMotionBlurHistory() {
    s_hasPrevFrame = false;
    s_hasLastQpc   = false;
    s_prevWorldMap.clear();
    s_currWorldMap.clear();
}

void __cdecl ResetMotionBlurEntity(const void* entityPtr) {
    if (entityPtr == nullptr) return;
    const uintptr_t e = reinterpret_cast<uintptr_t>(entityPtr);
    for (auto it = s_prevWorldMap.begin(); it != s_prevWorldMap.end(); ) {
        if (it->first.entity == e) {
            it = s_prevWorldMap.erase(it);
        } else {
            ++it;
        }
    }
}

void __cdecl ReleaseMotionBlurResources() {
    g_motionBlurCaptureActive = 0;
    s_entityStack.clear();
    s_frameDraws.clear();
    s_res.ReleaseAll();
    ResetMotionBlurHistory();
}

} // namespace edgemod

void RegisterMotionBlur() {
    // 1. WorldScreen::render clear-color call (0x47b20f, 5B) -> begin world capture
    static const uint8_t exClear[5] = {0xe8, 0x7c, 0xad, 0xf8, 0xff};
    RegRelCall(0x47b20f, exClear, 5, 5, &hk_worldSetClearColor);

    // 2. WorldScreen::render Renderer::end3D call (0x47b43b, 5B) -> apply motion blur
    static const uint8_t exEnd3d[5] = {0xe8, 0x90, 0xc2, 0xf8, 0xff};
    RegRelCall(0x47b43b, exEnd3d, 5, 5, &hk_worldEnd3D);

    // 3. OnD3D9LostDevice (0x44db7d, 5B) -> release D3DPOOL_DEFAULT resources before Reset
    static const uint8_t exLost[5] = {0xe8, 0xce, 0x85, 0xfb, 0xff};
    RegRelCall(0x44db7d, exLost, 5, 5, &hk_onLostDevice);

    // 4. OnD3D9DestroyDevice (0x44dc16, 5B) -> release D3DPOOL_DEFAULT resources on shutdown
    static const uint8_t exDestroy[5] = {0xe8, 0x35, 0xa8, 0xfb, 0xff};
    RegRelCall(0x44dc16, exDestroy, 5, 5, &hk_onDestroyDevice);

    // 5. Cube::resetToGrid (0x48dc3c, 5B) -> clear cube history on respawn
    static const uint8_t exCubeReset[5] = {0xe8, 0x3f, 0xea, 0xff, 0xff};
    RegRelCall(0x48dc3c, exCubeReset, 5, 5, &hk_cubeResetToGrid);
}
