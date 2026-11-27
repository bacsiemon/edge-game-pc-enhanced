#include "mod_state.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace edgemod;

// Active distortion parameters consumed inside SubMesh::render (0x439f02)
uint8_t g_cubeDistortActive = 0;
float   g_distortAmpPx      = 0.0f;
float   g_distortPhaseDeg   = 0.0f;
float   g_distortSign       = 1.0f;

// Timing & trigger tracking across 30 Hz ticks + g_uiAlpha interpolation
const uint8_t* s_lastWs             = nullptr;
const void*    s_lastCube           = nullptr;
uint32_t       s_lastWsFrame        = 0;
double         s_lastSmooth30       = 0.0;
double         s_playerIdleJavaTicks = 0.0;
bool           s_hasPrevCubePos     = false;
int32_t        s_prevPosX           = 0;
int32_t        s_prevPosY           = 0;
int32_t        s_prevPosZ           = 0;
int32_t        s_prevScaleInv       = 0;

uint32_t       s_rngState           = 0x6d2b79f5u;
uint32_t       s_ghostLastJavaTick  = UINT32_MAX;
bool           s_ghostDistortTick   = false;

struct DistortVert {
    float x, y, z;
    float nx, ny, nz;
};
constexpr DWORD kDistortFvf = D3DFVF_XYZ | D3DFVF_NORMAL;
constexpr int   kGridN      = 20;

std::vector<DistortVert> g_distortVerts;
std::vector<uint16_t>    g_distortIndices;

uint32_t NextRand() {
    uint32_t x = s_rngState;
    if (x == 0u) x = 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rngState = x;
    return x;
}

void UpdateSmoothTime(const uint8_t* ws, uint32_t state, double* outJavaTime, double* outDtJava) {
    uint32_t wsFrame = *reinterpret_cast<const uint32_t*>(ws + 0x04);
    if (ws != s_lastWs || wsFrame < s_lastWsFrame) {
        ResetCubeDistortionState();
        s_lastWs = ws;
        s_lastWsFrame = wsFrame;
        s_lastSmooth30 = static_cast<double>(wsFrame);
    } else {
        s_lastWsFrame = wsFrame;
    }

    double smooth30 = s_lastSmooth30;
    double dt30 = 0.0;
    if (state == 0x11u || IsGraphicsPreviewActive()) {
        double alpha = static_cast<double>(std::clamp(g_uiAlpha, 0.0f, 1.0f));
        smooth30 = static_cast<double>(wsFrame) + alpha;
        if (smooth30 < s_lastSmooth30 && (s_lastSmooth30 - smooth30) < 2.0) {
            smooth30 = s_lastSmooth30;
        }
        dt30 = std::clamp(smooth30 - s_lastSmooth30, 0.0, 4.0);
        s_lastSmooth30 = smooth30;
    }

    // Java runs at ~22.222 Hz (45 ms/tick) vs PC's 30 Hz gameplay loop.
    constexpr double kTicks30PerJavaTick = 1.35;
    *outJavaTime = smooth30 / kTicks30PerJavaTick;
    *outDtJava   = dt30 / kTicks30PerJavaTick;
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

static bool __cdecl DrawDistortedCubeSubMesh_Impl(
    IDirect3DDevice9* dev,
    const uint8_t* subMesh,
    const uint8_t* renderParams
) {
    if (dev == nullptr || subMesh == nullptr) return false;
    // Skip non-material passes (e.g. shadow/depth pass where [renderParams+0xc] == 3).
    if (renderParams != nullptr && *reinterpret_cast<const uint32_t*>(renderParams + 0x0c) == 3u) {
        return false;
    }

    const float* posBegin = *reinterpret_cast<const float* const*>(subMesh + 0x10);
    const float* posEnd   = *reinterpret_cast<const float* const*>(subMesh + 0x14);
    if (posBegin == nullptr || posEnd <= posBegin) return false;

    size_t numFloats = static_cast<size_t>(posEnd - posBegin);
    size_t numVerts  = numFloats / 3u;
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

    D3DMATRIX world{}, view{}, proj{};
    if (FAILED(dev->GetTransform(D3DTS_WORLD, &world))) return false;
    if (FAILED(dev->GetTransform(D3DTS_VIEW, &view))) return false;
    if (FAILED(dev->GetTransform(D3DTS_PROJECTION, &proj))) return false;

    float R[3] = {view._11, view._21, view._31};
    float U[3] = {view._12, view._22, view._32};
    float F[3] = {view._13, view._23, view._33};
    float eye[3] = {
        -(view._41 * view._11 + view._42 * view._12 + view._43 * view._13),
        -(view._41 * view._21 + view._42 * view._22 + view._43 * view._23),
        -(view._41 * view._31 + view._42 * view._32 + view._43 * view._33),
    };
    bool isOrtho = (std::fabs(proj._44 - 1.0f) < 0.01f);

    float uMin =  1e30f, uMax = -1e30f;
    float rMin =  1e30f, rMax = -1e30f;
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
    if (cubeScreenH < 1e-4f || cubeScreenW < 1e-4f) return false;

    float det3 = world._11 * (world._22 * world._33 - world._23 * world._32)
               - world._12 * (world._21 * world._33 - world._23 * world._31)
               + world._13 * (world._21 * world._32 - world._22 * world._31);
    float normalSign = (det3 < 0.0f) ? -1.0f : 1.0f;

    struct BoxFace {
        float org[3];
        float uVec[3];
        float vVec[3];
        float nrm[3];
    };
    float dx = maxX - minX;
    float dy = maxY - minY;
    float dz = maxZ - minZ;
    const BoxFace faces[6] = {
        {{minX, minY, maxZ}, { dx, 0.f, 0.f}, {0.f,  dy, 0.f}, { 0.f,  0.f,  1.f}}, // +Z
        {{maxX, minY, minZ}, {-dx, 0.f, 0.f}, {0.f,  dy, 0.f}, { 0.f,  0.f, -1.f}}, // -Z
        {{maxX, minY, maxZ}, {0.f, 0.f, -dz}, {0.f,  dy, 0.f}, { 1.f,  0.f,  0.f}}, // +X
        {{minX, minY, minZ}, {0.f, 0.f,  dz}, {0.f,  dy, 0.f}, {-1.f,  0.f,  0.f}}, // -X
        {{minX, maxY, maxZ}, { dx, 0.f, 0.f}, {0.f, 0.f, -dz}, { 0.f,  1.f,  0.f}}, // +Y
        {{minX, minY, minZ}, { dx, 0.f, 0.f}, {0.f, 0.f,  dz}, { 0.f, -1.f,  0.f}}, // -Y
    };

    g_distortVerts.clear();
    g_distortIndices.clear();
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;

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

        float facing = 0.0f;
        if (isOrtho) {
            facing = Dot3(nw, F);
        } else {
            float toEye[3] = {eye[0] - fcW[0], eye[1] - fcW[1], eye[2] - fcW[2]};
            facing = Dot3(nw, toEye);
        }
        if (facing <= 1e-4f) continue;

        uint16_t baseVert = static_cast<uint16_t>(g_distortVerts.size());
        for (int iv = 0; iv <= kGridN; ++iv) {
            float fv = static_cast<float>(iv) / static_cast<float>(kGridN);
            for (int iu = 0; iu <= kGridN; ++iu) {
                float fu = static_cast<float>(iu) / static_cast<float>(kGridN);
                float lx = bf.org[0] + bf.uVec[0] * fu + bf.vVec[0] * fv;
                float ly = bf.org[1] + bf.uVec[1] * fu + bf.vVec[1] * fv;
                float lz = bf.org[2] + bf.uVec[2] * fu + bf.vVec[2] * fv;

                float pw[3]{};
                TransformPoint(world, lx, ly, lz, pw);

                float u = std::clamp((Dot3(pw, U) - uMin) / cubeScreenH, 0.0f, 1.0f);
                float ySpr = (1.0f - u) * 59.0f;
                float angRad = (ySpr * 10.0f + g_distortPhaseDeg) * kDeg2Rad;
                float dxPx = g_distortSign * std::cos(angRad) * (u * g_distortAmpPx);
                float dxWorld = (dxPx / 50.0f) * cubeScreenW;

                g_distortVerts.push_back({
                    pw[0] + R[0] * dxWorld,
                    pw[1] + R[1] * dxWorld,
                    pw[2] + R[2] * dxWorld,
                    nw[0], nw[1], nw[2]
                });
            }
        }

        const uint16_t stride = static_cast<uint16_t>(kGridN + 1);
        for (int iv = 0; iv < kGridN; ++iv) {
            for (int iu = 0; iu < kGridN; ++iu) {
                uint16_t i00 = static_cast<uint16_t>(baseVert + iv * stride + iu);
                uint16_t i10 = static_cast<uint16_t>(i00 + 1u);
                uint16_t i01 = static_cast<uint16_t>(i00 + stride);
                uint16_t i11 = static_cast<uint16_t>(i01 + 1u);
                g_distortIndices.push_back(i00);
                g_distortIndices.push_back(i10);
                g_distortIndices.push_back(i11);
                g_distortIndices.push_back(i00);
                g_distortIndices.push_back(i11);
                g_distortIndices.push_back(i01);
            }
        }
    }

    if (g_distortVerts.empty() || g_distortIndices.empty()) return false;

    static const D3DMATRIX kIdentity = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
    };

    DWORD savedFvf = 0;
    DWORD savedCull = D3DCULL_CCW;
    DWORD savedDiffSrc = D3DMCS_MATERIAL;
    dev->GetFVF(&savedFvf);
    dev->GetRenderState(D3DRS_CULLMODE, &savedCull);
    dev->GetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, &savedDiffSrc);

    dev->SetTransform(D3DTS_WORLD, &kIdentity);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
    dev->SetFVF(kDistortFvf);

    UINT totalVerts = static_cast<UINT>(g_distortVerts.size());
    UINT totalPrims = static_cast<UINT>(g_distortIndices.size() / 3u);
    dev->DrawIndexedPrimitiveUP(
        D3DPT_TRIANGLELIST,
        0,
        totalVerts,
        totalPrims,
        g_distortIndices.data(),
        D3DFMT_INDEX16,
        g_distortVerts.data(),
        sizeof(DistortVert)
    );

    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetRenderState(D3DRS_CULLMODE, savedCull);
    dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, savedDiffSrc);
    dev->SetFVF(savedFvf);
    return true;
}

static void __declspec(naked) hk_subMeshDrawDistorted() {
    // At 0x439f02 (6B): 8B 96 A4 00 00 00  mov edx, DWORD PTR [esi+0xa4]
    // edi = IDirect3DDevice9*, esi = SubMesh*, [ebp+8] = RenderParams*
    __asm {
        cmp BYTE PTR g_cubeDistortActive, 0
        je smd_orig
        push DWORD PTR [ebp + 8]
        push esi
        push edi
        call DrawDistortedCubeSubMesh_Impl
        add esp, 12
        test al, al
        jz smd_orig
        mov eax, 0x439f62
        jmp eax
    smd_orig:
        mov edx, DWORD PTR [esi + 0xa4]
        mov eax, 0x439f08
        jmp eax
    }
}

} // namespace

namespace edgemod {

void __cdecl BeginPlayerCubeDistortion(void* cubePtr) {
    g_cubeDistortActive = 0;
    if (g_cubeDistortion == 0 || cubePtr == nullptr) return;

    const uint8_t* sm = GetScreenMgr();
    const uint8_t* ws = GetWorldScreen();
    if (sm == nullptr || ws == nullptr || ws[0x2120] == 0) return;

    uint32_t state = *reinterpret_cast<const uint32_t*>(sm + 0x50);
    bool previewActive = IsGraphicsPreviewActive();
    if (state != 0x11u && state != 0x12u && !previewActive) return;
    if (ws[0xb5] != 0) return;

    double javaTime = 0.0;
    double dtJava   = 0.0;
    UpdateSmoothTime(ws, state, &javaTime, &dtJava);

    const uint8_t* cube = static_cast<const uint8_t*>(cubePtr);
    int32_t cubeState = *reinterpret_cast<const int32_t*>(cube + 0x04);
    int32_t cubeAnim  = *reinterpret_cast<const int32_t*>(cube + 0x0c);
    int32_t posX      = *reinterpret_cast<const int32_t*>(cube + 0x4c);
    int32_t posY      = *reinterpret_cast<const int32_t*>(cube + 0x50);
    int32_t posZ      = *reinterpret_cast<const int32_t*>(cube + 0x54);
    int32_t velX      = *reinterpret_cast<const int32_t*>(cube + 0x58);
    int32_t velY      = *reinterpret_cast<const int32_t*>(cube + 0x5c);
    int32_t velZ      = *reinterpret_cast<const int32_t*>(cube + 0x60);
    int32_t scaleInv  = *reinterpret_cast<const int32_t*>(cube + 0x134);

    if (cubePtr != s_lastCube) {
        s_lastCube = cubePtr;
        s_hasPrevCubePos = false;
        s_playerIdleJavaTicks = 0.0;
    }

    bool isIdle = (
        cubeState == 0 && cubeAnim == 0 &&
        velX == 0 && velY == 0 && velZ == 0 &&
        s_hasPrevCubePos &&
        posX == s_prevPosX && posY == s_prevPosY && posZ == s_prevPosZ &&
        scaleInv == s_prevScaleInv
    );

    s_prevPosX = posX;
    s_prevPosY = posY;
    s_prevPosZ = posZ;
    s_prevScaleInv = scaleInv;
    s_hasPrevCubePos = true;

    if (previewActive) {
        if (isIdle && IsGraphicsPreviewHoveringCubeDistortion()) {
            if (s_playerIdleJavaTicks < 100.0) {
                s_playerIdleJavaTicks = 100.1;
            }
            s_playerIdleJavaTicks += dtJava;
        } else {
            s_playerIdleJavaTicks = 0.0;
        }
    } else if (state == 0x11u) {
        if (isIdle) {
            s_playerIdleJavaTicks += dtJava;
        } else {
            s_playerIdleJavaTicks = 0.0;
        }
    }

    if (isIdle && s_playerIdleJavaTicks > 100.0) {
        double idleElapsed = s_playerIdleJavaTicks - 100.0;
        double phase = std::fmod(idleElapsed, 36.0);
        if (phase < 8.0) {
            float ramp = static_cast<float>(std::clamp(phase / 0.35, 0.0, 1.0));
            ramp = ramp * ramp * (3.0f - 2.0f * ramp);
            g_distortAmpPx    = static_cast<float>(8.0 - phase) * ramp;
            g_distortPhaseDeg = static_cast<float>(std::fmod(javaTime * 20.0, 360.0));
            g_distortSign     = 1.0f;
            g_cubeDistortActive = 1;
        }
    }
}

void __cdecl BeginGhostCubeDistortion(void* wsPtr) {
    g_cubeDistortActive = 0;
    if (g_cubeDistortion == 0 || wsPtr == nullptr) return;

    const uint8_t* sm = GetScreenMgr();
    const uint8_t* ws = static_cast<const uint8_t*>(wsPtr);
    if (sm == nullptr || ws[0x2120] == 0) return;

    uint32_t state = *reinterpret_cast<const uint32_t*>(sm + 0x50);
    bool previewActive = IsGraphicsPreviewActive();
    if (state != 0x11u && state != 0x12u && !previewActive) return;
    if (ws[0x48] != 0 || ws[0x50] == 0) return;

    int32_t ay = *reinterpret_cast<const int32_t*>(ws + 0x64);
    int32_t az = *reinterpret_cast<const int32_t*>(ws + 0x68);
    if (ay > 0 || az > 0) return;

    double javaTime = 0.0;
    double dtJava   = 0.0;
    UpdateSmoothTime(ws, state, &javaTime, &dtJava);

    uint32_t javaTickInt = static_cast<uint32_t>(std::floor(javaTime));
    if ((state == 0x11u || previewActive) && javaTickInt != s_ghostLastJavaTick) {
        s_ghostLastJavaTick = javaTickInt;
        bool rngHit = ((NextRand() % 10u) == 0u);
        bool periodicHit = (
            (javaTickInt % 50u == 0u) ||
            (javaTickInt % 51u == 0u) ||
            (javaTickInt % 52u == 0u) ||
            (javaTickInt % 53u == 0u)
        );
        s_ghostDistortTick = (rngHit || periodicHit);
    }

    if (s_ghostDistortTick) {
        g_distortAmpPx    = 8.0f;
        g_distortPhaseDeg = static_cast<float>(std::fmod(javaTime * 15.0, 360.0));
        g_distortSign     = ((javaTickInt % 3u) == 0u) ? -1.0f : 1.0f;
        g_cubeDistortActive = 1;
    }
}

void __cdecl EndCubeDistortion() {
    g_cubeDistortActive = 0;
}

void __cdecl ResetCubeDistortionState() {
    g_cubeDistortActive   = 0;
    s_lastWs              = nullptr;
    s_lastCube            = nullptr;
    s_lastWsFrame         = 0;
    s_lastSmooth30        = 0.0;
    s_playerIdleJavaTicks = 0.0;
    s_hasPrevCubePos      = false;
    s_ghostLastJavaTick   = UINT32_MAX;
    s_ghostDistortTick    = false;
}

} // namespace edgemod

void RegisterCubeDistortion() {
    // SubMesh::render draw call (0x439f02..0x439f07, 6B)
    static const uint8_t exSubMeshDraw[6] = {0x8b, 0x96, 0xa4, 0x00, 0x00, 0x00};
    RegRelJmp(0x439f02, exSubMeshDraw, 6, 6, &hk_subMeshDrawDistorted, 0x90);
}
