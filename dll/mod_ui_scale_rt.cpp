#include "mod_state.h"
#include <cmath>

namespace edgemod {

uint32_t g_autoScale = 1;
float    g_uiScale = 1.0f;
float    g_iniUiScale = 0.0f;
float    g_lastIniScale = 0.0f;
int8_t   g_iniAutoScale = -1;
int8_t   g_lastIniAuto = -1;

const float g_uiScalePresets[9] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f};

bool __cdecl ClampUiScale(float* inOutVal) {
    float v = *inOutVal;
    if (std::isnan(v)) return false;
    if (v < 0.1f) v = 0.1f;
    if (v > 2.5f) v = 2.5f;
    *inOutVal = v;
    return true;
}

float __cdecl GetEffectiveUiScale() {
    float s = g_uiScale;
    if (s == 0.0f) s = g_iniUiScale;
    if (s == 0.0f) s = 1.0f;
    if (!ClampUiScale(&s)) return 1.0f;
    return s;
}

EdgeWString* __cdecl FmtFloatWstr(double val, EdgeWString* out) {
    out->clear_sso();
    int scaled = static_cast<int>(std::lrint(val * 100.0));
    if (scaled < 0) scaled = 0;
    if (scaled > 9000) scaled = 9000;
    int whole = scaled / 100;
    int frac = scaled % 100;
    uint32_t n = 0;
    if (whole >= 10) {
        out->buf[n++] = static_cast<wchar_t>(L'0' + (whole / 10));
    }
    out->buf[n++] = static_cast<wchar_t>(L'0' + (whole % 10));
    if (frac != 0) {
        out->buf[n++] = L'.';
        out->buf[n++] = static_cast<wchar_t>(L'0' + (frac / 10));
        int ones = frac % 10;
        if (ones != 0) {
            out->buf[n++] = static_cast<wchar_t>(L'0' + ones);
        }
    }
    out->buf[n] = L'\0';
    out->size = n;
    return out;
}

static int32_t __cdecl ComputeVirtHeight_Impl(const uint8_t* renderer) {
    int32_t physH = *reinterpret_cast<const int32_t*>(renderer + 0xbc);
    if (physH <= 0) return physH;
    float scale = GetEffectiveUiScale();
    float num = g_autoScale ? 720.0f : static_cast<float>(physH);
    int32_t vh = static_cast<int32_t>(std::lrint(num / scale));
    if (vh < 0x200) vh = 0x200;
    return vh;
}

static int32_t __cdecl ComputeVirtWidth_Impl(const uint8_t* renderer) {
    uint32_t physH = *reinterpret_cast<const uint32_t*>(renderer + 0xbc);
    uint32_t physW = *reinterpret_cast<const uint32_t*>(renderer + 0xb8);
    if (static_cast<int32_t>(physH) <= 0) return static_cast<int32_t>(physW);
    uint32_t vh = static_cast<uint32_t>(ComputeVirtHeight_Impl(renderer));
    return static_cast<int32_t>((static_cast<uint64_t>(vh) * physW) / physH);
}

void __cdecl ApplyUiScale(void* screenMgrPtr) {
    uint8_t* renderer = GetRenderer();
    if (renderer == nullptr) return;
    reinterpret_cast<void(__thiscall*)(void*)>(0x407010)(renderer);
    reinterpret_cast<void(__thiscall*)(void*)>(0x406190)(renderer);
    uint8_t* sm = static_cast<uint8_t*>(screenMgrPtr);
    if (sm != nullptr) {
        int32_t vw = ComputeVirtWidth_Impl(renderer);
        int32_t vh = ComputeVirtHeight_Impl(renderer);
        *reinterpret_cast<int32_t*>(sm + 0x24) = vw;
        *reinterpret_cast<int32_t*>(sm + 0x2c) = vw;
        *reinterpret_cast<int32_t*>(sm + 0x28) = vh;
        *reinterpret_cast<int32_t*>(sm + 0x30) = vh;
    }
    uint8_t* ws = GetWorldScreen();
    if (ws != nullptr) {
        auto** vt = *reinterpret_cast<void***>(ws);
        reinterpret_cast<void(__thiscall*)(void*)>(vt[0x24 / 4])(ws);
    }
}

} // namespace edgemod

namespace {

using namespace edgemod;

// ------------------------------------------------------------------
// Offscreen uiRT sharpening state & helpers (ui_rt_sharpening.py)
// ------------------------------------------------------------------
void* g_uiRtPtr  = nullptr;
void* g_uiRtCtrl = nullptr;
void* g_savedRt  = nullptr;
void* g_savedDs  = nullptr;

struct StateEntry { uint8_t op; uint8_t key; uint16_t val; };
const StateEntry kUiRtStates[] = {
    {1, 0x1b, 1},    // ALPHABLENDENABLE = TRUE
    {1, 0xab, 1},    // BLENDOP = ADD
    {1, 0x0f, 0},    // ALPHATESTENABLE = FALSE
    {1, 0x16, 1},    // CULLMODE = NONE
    {1, 0xa8, 0x0f}, // COLORWRITEENABLE = RGBA
    {2, 11, 0},      // stage 0 TEXCOORDINDEX = 0
    {2, 1, 2},       // stage 0 COLOROP = SELECTARG1
    {2, 2, 2},       // stage 0 COLORARG1 = TEXTURE
    {2, 4, 2},       // stage 0 ALPHAOP = SELECTARG1
    {2, 5, 2},       // stage 0 ALPHAARG1 = TEXTURE
    {2, 24, 0},      // stage 0 TEXTURETRANSFORMFLAGS = DISABLE
    {3, 1, 1},       // stage 1 COLOROP = DISABLE
    {3, 4, 1},       // stage 1 ALPHAOP = DISABLE
};

void ComReleaseZero(void** ppUnk) {
    if (*ppUnk != nullptr) {
        void* unk = *ppUnk;
        *ppUnk = nullptr;
        auto** vt = *reinterpret_cast<void***>(unk);
        reinterpret_cast<uint32_t(__stdcall*)(void*)>(vt[0x8 / 4])(unk);
    }
}

void DevSetRS(void* dev, uint32_t rs, uint32_t val) {
    auto** vt = *reinterpret_cast<void***>(dev);
    reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, uint32_t)>(vt[0xe4 / 4])(dev, rs, val);
}

void SyncSepAlpha(const uint8_t* renderer) {
    void* dev = *reinterpret_cast<void* const*>(renderer + 0xcc);
    if (dev == nullptr) return;
    if (*reinterpret_cast<const int32_t*>(renderer + 0x1c) == 1) {
        DevSetRS(dev, 0xcf /* SRCBLENDALPHA */, 1 /* ONE */);
        DevSetRS(dev, 0xd0 /* DESTBLENDALPHA */, 2 /* ZERO -> wait: orig pushed 2 then 1 -> eax=0xcf edx=1, eax=0xd0 edx=2! */);
    } else {
        DevSetRS(dev, 0xcf /* SRCBLENDALPHA */, 2 /* SRCALPHA */);
        DevSetRS(dev, 0xd0 /* DESTBLENDALPHA */, 6 /* INVSRCALPHA */);
    }
}

void ReleaseUiRT() {
    if (g_uiRtPtr != nullptr || g_uiRtCtrl != nullptr) {
        void* zeroPair[2] = {nullptr, nullptr};
        reinterpret_cast<void(__thiscall*)(void*, void*)>(0x43e9c0)(&g_uiRtPtr, zeroPair);
    }
}

void __cdecl CleanupUiRT_Impl() {
    reinterpret_cast<void(__cdecl*)()>(0x4099f0)();
    ComReleaseZero(&g_savedRt);
    ComReleaseZero(&g_savedDs);
    ReleaseUiRT();
}

void GetUiDrawSize(const uint8_t* renderer, int32_t* outW, int32_t* outH) {
    if (g_savedRt != nullptr && g_uiRtPtr != nullptr) {
        const auto* rt = static_cast<const uint8_t*>(g_uiRtPtr);
        *outW = *reinterpret_cast<const int32_t*>(rt + 0x8);
        *outH = *reinterpret_cast<const int32_t*>(rt + 0xc);
    } else {
        *outW = *reinterpret_cast<const int32_t*>(renderer + 0xb8);
        *outH = *reinterpret_cast<const int32_t*>(renderer + 0xbc);
    }
}

bool CalcUiRTSize(const uint8_t* renderer, int32_t* outW, int32_t* outH) {
    int32_t physW = *reinterpret_cast<const int32_t*>(renderer + 0xb8);
    int32_t physH = *reinterpret_cast<const int32_t*>(renderer + 0xbc);
    if (physW <= 0 || physH <= 0) return false;
    int32_t vw = ComputeVirtWidth_Impl(renderer);
    int32_t vh = ComputeVirtHeight_Impl(renderer);
    if (vw <= 0 || vh <= 0) return false;
    if ((physH % vh) == 0 && (physW % vw) == 0) return false;
    int32_t k = (physH + (vh >> 1)) / vh;
    if (k < 1) k = 1;
    while (vw * k > 4096 || vh * k > 4096) {
        if (k <= 1) return false;
        --k;
    }
    *outW = vw * k;
    *outH = vh * k;
    return true;
}

void __cdecl SetD3DBlendTail_Impl(uint8_t* renderer, uint32_t a, uint32_t b) {
    *reinterpret_cast<uint32_t*>(renderer + 0x18) = a;
    *reinterpret_cast<uint32_t*>(renderer + 0x1c) = b;
    if (g_savedRt != nullptr) {
        SyncSepAlpha(renderer);
    }
}

void __cdecl Begin2D_Impl(uint8_t* renderer, uint32_t retAddr) {
    renderer[1] = 1;
    if (retAddr == 0x44e1fa || g_savedRt != nullptr) return;
    void* dev = *reinterpret_cast<void**>(renderer + 0xcc);
    if (dev == nullptr) return;
    int32_t rtW = 0, rtH = 0;
    if (!CalcUiRTSize(renderer, &rtW, &rtH)) {
        ReleaseUiRT();
        return;
    }
    uint8_t* cur = static_cast<uint8_t*>(g_uiRtPtr);
    bool valid = false;
    if (cur != nullptr &&
        *reinterpret_cast<int32_t*>(cur + 0x8) == rtW &&
        *reinterpret_cast<int32_t*>(cur + 0xc) == rtH &&
        *reinterpret_cast<void**>(cur + 0x18) != nullptr) {
        uint8_t* tex = *reinterpret_cast<uint8_t**>(cur + 0x20);
        if (tex != nullptr && *reinterpret_cast<void**>(tex + 0x2c) != nullptr) {
            valid = true;
        }
    }
    if (!valid) {
        void* pair[2] = {nullptr, nullptr};
        reinterpret_cast<void*(__cdecl*)(void**, int32_t, int32_t, int32_t, int32_t)>(0x40c880)(pair, rtW, rtH, 1, 0);
        reinterpret_cast<void(__thiscall*)(void*, void*)>(0x43e9c0)(&g_uiRtPtr, pair);
        cur = static_cast<uint8_t*>(g_uiRtPtr);
        if (cur == nullptr || *reinterpret_cast<void**>(cur + 0x18) == nullptr) return;
        uint8_t* tex = *reinterpret_cast<uint8_t**>(cur + 0x20);
        if (tex == nullptr || *reinterpret_cast<void**>(tex + 0x2c) == nullptr) return;
    }

    auto** vt = *reinterpret_cast<void***>(dev);
    auto getRt = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, void**)>(vt[0x98 / 4]);
    auto getDs = reinterpret_cast<HRESULT(__stdcall*)(void*, void**)>(vt[0xa0 / 4]);
    auto setRt = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, void*)>(vt[0x94 / 4]);
    auto setDs = reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(vt[0x9c / 4]);
    auto clear = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, const void*, uint32_t, uint32_t, float, uint32_t)>(vt[0xac / 4]);

    if (getRt(dev, 0, &g_savedRt) < 0 || g_savedRt == nullptr) {
        ComReleaseZero(&g_savedRt);
        ComReleaseZero(&g_savedDs);
        return;
    }
    if (getDs(dev, &g_savedDs) < 0) {
        g_savedDs = nullptr;
    }
    if (setRt(dev, 0, *reinterpret_cast<void**>(cur + 0x18)) < 0) {
        ComReleaseZero(&g_savedRt);
        ComReleaseZero(&g_savedDs);
        return;
    }
    setDs(dev, nullptr);
    struct D3DVP { uint32_t X, Y, W, H; float MinZ, MaxZ; } vp{0, 0, static_cast<uint32_t>(rtW), static_cast<uint32_t>(rtH), 0.0f, 1.0f};
    reinterpret_cast<HRESULT(__stdcall*)(void*, const D3DVP*)>(vt[0xbc / 4])(dev, &vp);
    clear(dev, 0, nullptr, 1, 0, 1.0f, 0);
    DevSetRS(dev, 0xce, 1); // SEPARATEALPHABLENDENABLE
    DevSetRS(dev, 0xab, 1); // BLENDOP
    DevSetRS(dev, 0xd1, 1); // BLENDOPALPHA
    SyncSepAlpha(renderer);
}

void __cdecl End2D_Impl(uint8_t* renderer) {
    if (g_savedRt == nullptr) return;
    void* dev = *reinterpret_cast<void**>(renderer + 0xcc);
    if (dev == nullptr) {
        g_savedRt = nullptr;
        g_savedDs = nullptr;
        return;
    }
    reinterpret_cast<void(__thiscall*)(void*)>(0x407410)(renderer);
    dev = *reinterpret_cast<void**>(renderer + 0xcc);
    if (dev == nullptr) {
        g_savedRt = nullptr;
        g_savedDs = nullptr;
        return;
    }
    DevSetRS(dev, 0xae, 0); // SCISSORTESTENABLE
    DevSetRS(dev, 0xce, 0); // SEPARATEALPHABLENDENABLE

    auto** vt = *reinterpret_cast<void***>(dev);
    auto setRt = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, void*)>(vt[0x94 / 4]);
    auto setDs = reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(vt[0x9c / 4]);
    setRt(dev, 0, g_savedRt);
    ComReleaseZero(&g_savedRt);
    setDs(dev, g_savedDs);
    ComReleaseZero(&g_savedDs);

    int32_t physW = *reinterpret_cast<int32_t*>(renderer + 0xb8);
    int32_t physH = *reinterpret_cast<int32_t*>(renderer + 0xbc);
    struct D3DVP { uint32_t X, Y, W, H; float MinZ, MaxZ; } vp{0, 0, static_cast<uint32_t>(physW), static_cast<uint32_t>(physH), 0.0f, 1.0f};
    reinterpret_cast<HRESULT(__stdcall*)(void*, const D3DVP*)>(vt[0xbc / 4])(dev, &vp);

    uint8_t* rt = static_cast<uint8_t*>(g_uiRtPtr);
    if (rt != nullptr) {
        uint8_t* tex = *reinterpret_cast<uint8_t**>(rt + 0x20);
        if (tex != nullptr && *reinterpret_cast<void**>(tex + 0x2c) != nullptr) {
            *reinterpret_cast<uint32_t*>(tex + 0x4c) = 3; // ClampU
            *reinterpret_cast<uint32_t*>(tex + 0x50) = 3; // ClampV
            *reinterpret_cast<uint32_t*>(tex + 0x54) = 2; // Linear
            *reinterpret_cast<uint32_t*>(tex + 0x58) = 2; // Linear
            *reinterpret_cast<uint32_t*>(tex + 0x5c) = 0;
            reinterpret_cast<void(__thiscall*)(void*, int)>(0x40cf90)(tex, 0);

            auto setTss = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, uint32_t, uint32_t)>(vt[0x10c / 4]);
            for (const auto& st : kUiRtStates) {
                if (st.op == 1) DevSetRS(dev, st.key, st.val);
                else setTss(dev, st.op - 2, st.key, st.val);
            }
            reinterpret_cast<void(__thiscall*)(void*, int, int)>(0x405910)(renderer, 1, 5);
            reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(vt[0x170 / 4])(dev, nullptr);
            reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(vt[0x1ac / 4])(dev, nullptr);
            reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t)>(vt[0x164 / 4])(dev, 0x104 /* D3DFVF_XYZRHW | D3DFVF_TEX1 */);

            struct QuadV { float x, y, z, rhw, u, v; };
            float maxX = static_cast<float>(physW) - 0.5f;
            float maxY = static_cast<float>(physH) - 0.5f;
            QuadV v[4] = {
                {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
                { maxX, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f},
                {-0.5f,  maxY, 0.0f, 1.0f, 0.0f, 1.0f},
                { maxX,  maxY, 0.0f, 1.0f, 1.0f, 1.0f},
            };
            reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, uint32_t, const void*, uint32_t)>(vt[0x14c / 4])(dev, 5 /* TRIANGLESTRIP */, 2, v, 24);
            reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, void*)>(vt[0x104 / 4])(dev, 0, nullptr);
            uint32_t* tcache = *reinterpret_cast<uint32_t**>(0x5f87cc);
            if (tcache != nullptr) *tcache = 0;
            setTss(dev, 0, 24, 2);
            DevSetRS(dev, 0x0f, renderer[0x20]);
            uint32_t cull = 1;
            if (renderer[0x34] != 0) {
                uint32_t a = *reinterpret_cast<uint32_t*>(renderer + 0x2c);
                uint32_t b = *reinterpret_cast<uint32_t*>(renderer + 0x30);
                cull = reinterpret_cast<const uint32_t*>(0x5784c4)[b + a * 2];
            }
            DevSetRS(dev, 0x16, cull);
        }
    }
}

// ------------------------------------------------------------------
// Naked hooks
// ------------------------------------------------------------------
static void __declspec(naked) hk_getPhysWidth() {
    __asm {
        mov eax, DWORD PTR [ecx + 0xb8]
        ret
    }
}

static void __declspec(naked) hk_getPhysHeight() {
    __asm {
        mov eax, DWORD PTR [ecx + 0xbc]
        ret
    }
}

static void __declspec(naked) hk_computeVirtWidth() {
    __asm {
        push ecx
        push edx
        push ecx
        call ComputeVirtWidth_Impl
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

static void __declspec(naked) hk_computeVirtHeight() {
    __asm {
        push ecx
        push edx
        push ecx
        call ComputeVirtHeight_Impl
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

static void __declspec(naked) hk_camVirtSetup1() {
    __asm {
        xor ebx, ebx
        push esi
        call ComputeVirtWidth_Impl
        add esp, 4
        mov DWORD PTR [ebp - 0x10], eax
        fild DWORD PTR [ebp - 0x10]
        fstp DWORD PTR [ebp - 0x14]
        push esi
        call ComputeVirtHeight_Impl
        add esp, 4
        mov DWORD PTR [ebp - 0x10], eax
        mov eax, 0x407099
        jmp eax
    }
}

static void __declspec(naked) hk_camVirtSetup2() {
    __asm {
        push esi
        call ComputeVirtHeight_Impl
        add esp, 4
        mov DWORD PTR [ebp - 0xc], eax
        push esi
        call ComputeVirtWidth_Impl
        add esp, 4
        mov DWORD PTR [ebp - 0x8], eax
        mov eax, 0x40644d
        jmp eax
    }
}

static void __declspec(naked) hk_camVirtSetup3() {
    __asm {
        push esi
        call ComputeVirtHeight_Impl
        add esp, 4
        mov DWORD PTR [ebp + 0x8], eax
        push esi
        call ComputeVirtWidth_Impl
        add esp, 4
        mov DWORD PTR [ebp - 0x14], eax
        mov eax, 0x4083f6
        jmp eax
    }
}

static void __cdecl ScaleScissorRect_Impl(const uint8_t* renderer, const int32_t* rect, int32_t* outLtrb) {
    int32_t drawW = 0, drawH = 0;
    GetUiDrawSize(renderer, &drawW, &drawH);
    int32_t vw = ComputeVirtWidth_Impl(renderer);
    int32_t vh = ComputeVirtHeight_Impl(renderer);
    if (vw <= 0) vw = 1;
    if (vh <= 0) vh = 1;
    outLtrb[0] = static_cast<int32_t>((static_cast<int64_t>(rect[0]) * drawW) / vw);
    outLtrb[2] = static_cast<int32_t>((static_cast<int64_t>(rect[0] + rect[2]) * drawW) / vw);
    outLtrb[1] = static_cast<int32_t>((static_cast<int64_t>(rect[1]) * drawH) / vh);
    outLtrb[3] = static_cast<int32_t>((static_cast<int64_t>(rect[1] + rect[3]) * drawH) / vh);
}

static void __declspec(naked) hk_setScissorRect() {
    __asm {
        push esi
        push edi
        mov esi, ecx
        lea eax, [ebp - 0x14]
        push eax
        push DWORD PTR [ebp + 0x8]
        push esi
        call ScaleScissorRect_Impl
        add esp, 12
        mov eax, DWORD PTR [esi + 0xcc]
        lea edx, [ebp - 0x14]
        mov ecx, 0x40610b
        jmp ecx
    }
}

static void __cdecl ScaleViewport_Impl(const uint8_t* renderer, int32_t* px, int32_t* py, int32_t* pw, int32_t* ph) {
    int32_t vw = ComputeVirtWidth_Impl(renderer);
    int32_t vh = ComputeVirtHeight_Impl(renderer);
    if (vw <= 0) vw = 1;
    if (vh <= 0) vh = 1;
    int32_t physW = *reinterpret_cast<const int32_t*>(renderer + 0xb8);
    int32_t physH = *reinterpret_cast<const int32_t*>(renderer + 0xbc);
    *px = static_cast<int32_t>((static_cast<int64_t>(*px) * physW) / vw);
    *pw = static_cast<int32_t>((static_cast<int64_t>(*pw) * physW) / vw);
    *py = static_cast<int32_t>((static_cast<int64_t>(*py) * physH) / vh);
    *ph = static_cast<int32_t>((static_cast<int64_t>(*ph) * physH) / vh);
}

static void __declspec(naked) hk_setViewport() {
    // Entry registers at 0x405bcd: edi=Renderer*, esi=x, edx=y, [ebp+0x10]=w, eax=h
    __asm {
        push eax
        push DWORD PTR [ebp + 0x10]
        push edx
        push esi
        lea eax, [esp + 12]
        push eax
        lea eax, [esp + 12]
        push eax
        lea eax, [esp + 12]
        push eax
        lea eax, [esp + 12]
        push eax
        push edi
        call ScaleViewport_Impl
        add esp, 20
        pop esi
        pop edx
        pop ecx
        pop eax
        push 0x405be1
        ret
    }
}

static void __declspec(naked) hk_viewportApplyToCamera() {
    __asm {
        mov eax, DWORD PTR ds:[0x5f8770]
        test eax, eax
        je vp_phys
        push eax
        call ComputeVirtWidth_Impl
        add esp, 4
        mov DWORD PTR [ebp - 0x4], eax
        fild DWORD PTR [ebp - 0x4]
        fstp DWORD PTR [ebp - 0x4]
        mov eax, DWORD PTR ds:[0x5f8770]
        push eax
        call ComputeVirtHeight_Impl
        add esp, 4
        mov DWORD PTR [ebp + 0x8], eax
        fild DWORD PTR [ebp + 0x8]
        fstp DWORD PTR [ebp + 0x8]
        mov ecx, DWORD PTR [esi + 0x18]
        mov eax, 0x408a0f
        jmp eax
    vp_phys:
        fild DWORD PTR [esi + 0x8]
        fstp DWORD PTR [ebp - 0x4]
        fild DWORD PTR [esi + 0xc]
        fstp DWORD PTR [ebp + 0x8]
        mov eax, 0x408a0f
        jmp eax
    }
}

static void __cdecl ScaleMousePos_Impl(const uint8_t* renderer, const float* inPos, float* outX, float* outY) {
    int32_t physW = *reinterpret_cast<const int32_t*>(renderer + 0xb8);
    int32_t physH = *reinterpret_cast<const int32_t*>(renderer + 0xbc);
    if (physW > 0 && physH > 0) {
        int32_t vw = ComputeVirtWidth_Impl(renderer);
        int32_t vh = ComputeVirtHeight_Impl(renderer);
        *outX = (static_cast<float>(vw) * inPos[0]) / static_cast<float>(physW);
        *outY = (static_cast<float>(vh) * inPos[1]) / static_cast<float>(physH);
    } else {
        *outX = inPos[0];
        *outY = inPos[1];
    }
}

static void __declspec(naked) hk_mouseControllerScale() {
    __asm {
        lea ecx, [ebp - 0x4]
        push ecx
        lea ecx, [ebp - 0x8]
        push ecx
        push eax
        push esi
        call ScaleMousePos_Impl
        add esp, 16
        mov eax, 0x44c810
        jmp eax
    }
}

static void __declspec(naked) hk_setD3DBlend_tail() {
    __asm {
        push ebx
        push edi
        push esi
        call SetD3DBlendTail_Impl
        add esp, 12
        mov eax, 0x40595c
        jmp eax
    }
}

static void __declspec(naked) hk_begin2D_tail() {
    __asm {
        push DWORD PTR [esp + 0x4]
        push esi
        call Begin2D_Impl
        add esp, 8
        pop esi
        ret
    }
}

static void __declspec(naked) hk_end2D_hook() {
    __asm {
        push esi
        mov esi, ecx
        push esi
        call End2D_Impl
        add esp, 4
        cmp BYTE PTR [ebp + 0x8], 0
        je e2d_skip
        mov eax, 0x40669c
        jmp eax
    e2d_skip:
        mov eax, 0x4066b2
        jmp eax
    }
}

} // namespace

namespace edgemod {

void __cdecl VirtualRectToUiScissorLtrb(
    const uint8_t* renderer, int32_t x, int32_t y, int32_t w, int32_t h,
    int32_t* outLtrb, int32_t* outDrawW, int32_t* outDrawH
) {
    int32_t drawW = 0, drawH = 0;
    GetUiDrawSize(renderer, &drawW, &drawH);
    int32_t vw = ComputeVirtWidth_Impl(renderer);
    int32_t vh = ComputeVirtHeight_Impl(renderer);
    if (vw <= 0) vw = 1;
    if (vh <= 0) vh = 1;
    if (outDrawW != nullptr) *outDrawW = drawW;
    if (outDrawH != nullptr) *outDrawH = drawH;

    auto floorDiv = [](int64_t num, int64_t den) -> int32_t {
        if (num >= 0) return static_cast<int32_t>(num / den);
        return static_cast<int32_t>(-((-num + den - 1) / den));
    };
    auto ceilDiv = [](int64_t num, int64_t den) -> int32_t {
        if (num >= 0) return static_cast<int32_t>((num + den - 1) / den);
        return static_cast<int32_t>(-((-num) / den));
    };
    int32_t l = floorDiv(static_cast<int64_t>(x) * drawW, vw);
    int32_t t = floorDiv(static_cast<int64_t>(y) * drawH, vh);
    int32_t r = ceilDiv(static_cast<int64_t>(x + w) * drawW, vw);
    int32_t b = ceilDiv(static_cast<int64_t>(y + h) * drawH, vh);
    if (l < 0) l = 0; else if (l > drawW) l = drawW;
    if (r < 0) r = 0; else if (r > drawW) r = drawW;
    if (t < 0) t = 0; else if (t > drawH) t = drawH;
    if (b < 0) b = 0; else if (b > drawH) b = drawH;
    outLtrb[0] = l;
    outLtrb[1] = t;
    outLtrb[2] = r;
    outLtrb[3] = b;
}

} // namespace edgemod

void RegisterUiScaleAndRt() {
    // Redirect physical 3D RT size callers
    {
        static const uint8_t exp401aab[5] = {0xe8, 0x80, 0x40, 0x00, 0x00};
        RegRelCall(0x401aab, exp401aab, 5, 5, &hk_getPhysWidth);

        static const uint8_t exp404787[5] = {0xe8, 0xa4, 0x13, 0x00, 0x00};
        RegRelCall(0x404787, exp404787, 5, 5, &hk_getPhysWidth);

        static const uint8_t exp40c620[5] = {0xe8, 0x0b, 0x95, 0xff, 0xff};
        RegRelCall(0x40c620, exp40c620, 5, 5, &hk_getPhysWidth);

        static const uint8_t exp40c830[5] = {0xe8, 0xfb, 0x92, 0xff, 0xff};
        RegRelCall(0x40c830, exp40c830, 5, 5, &hk_getPhysWidth);

        static const uint8_t exp401abd[5] = {0xe8, 0x9e, 0x40, 0x00, 0x00};
        RegRelCall(0x401abd, exp401abd, 5, 5, &hk_getPhysHeight);

        static const uint8_t exp404799[5] = {0xe8, 0xc2, 0x13, 0x00, 0x00};
        RegRelCall(0x404799, exp404799, 5, 5, &hk_getPhysHeight);

        static const uint8_t exp40c62e[5] = {0xe8, 0x2d, 0x95, 0xff, 0xff};
        RegRelCall(0x40c62e, exp40c62e, 5, 5, &hk_getPhysHeight);

        static const uint8_t exp40c828[5] = {0xe8, 0x33, 0x93, 0xff, 0xff};
        RegRelCall(0x40c828, exp40c828, 5, 5, &hk_getPhysHeight);

        static const uint8_t exp4d0b9d[5] = {0xe8, 0xbe, 0x4f, 0xf3, 0xff};
        RegRelCall(0x4d0b9d, exp4d0b9d, 5, 5, &hk_getPhysHeight);
    }

    // Virtual width/height
    {
        static const uint8_t exp5b30[6] = {0x80, 0xb9, 0xd8, 0x00, 0x00, 0x00};
        RegRelJmp(0x405b30, exp5b30, 6, 5, &hk_computeVirtWidth, 0x90);

        static const uint8_t exp5b60[6] = {0x80, 0xb9, 0xd8, 0x00, 0x00, 0x00};
        RegRelJmp(0x405b60, exp5b60, 6, 5, &hk_computeVirtHeight, 0x90);
    }

    // Mode-0 renderer jump & virtual camera/viewport/scissor/mouse setup
    {
        static const uint8_t exp7e81[6] = {0x0f, 0x84, 0xfb, 0x04, 0x00, 0x00};
        RegRelJmpVa(0x407e81, exp7e81, 6, 6, 0x408382, 0x90);

        static const uint8_t exp703a[8] = {0x8a, 0x8e, 0xd8, 0x00, 0x00, 0x00, 0x33, 0xdb};
        RegRelJmp(0x40703a, exp703a, 8, 5, &hk_camVirtSetup1, 0x90, 0x407099);

        static const uint8_t exp6404[8] = {0x8a, 0x8e, 0xd8, 0x00, 0x00, 0x00, 0x84, 0xc9};
        RegRelJmp(0x406404, exp6404, 8, 5, &hk_camVirtSetup2, 0x90, 0x40644d);

        static const uint8_t exp839d[8] = {0x8a, 0x8e, 0xd8, 0x00, 0x00, 0x00, 0x84, 0xc9};
        RegRelJmp(0x40839d, exp839d, 8, 5, &hk_camVirtSetup3, 0x90, 0x4083f6);

        static const uint8_t exp60e6[7] = {0x56, 0x57, 0x8b, 0x78, 0x08, 0x8b, 0xf1};
        RegRelJmp(0x4060e6, exp60e6, 7, 5, &hk_setScissorRect, 0x90, 0x40610b);

        static const uint8_t exp5bcd[10] = {0x80, 0xbf, 0xf5, 0x00, 0x00, 0x00, 0x00, 0x8b, 0x4d, 0x10};
        RegRelJmp(0x405bcd, exp5bcd, 10, 5, &hk_setViewport, 0x90, 0x405be1);

        static const uint8_t exp89db[8] = {0xdb, 0x46, 0x08, 0xa1, 0x70, 0x87, 0x5f, 0x00};
        RegRelJmp(0x4089db, exp89db, 8, 5, &hk_viewportApplyToCamera, 0x90, 0x408a0f);

        static const uint8_t exp44c7e6[9] = {0xd9, 0x00, 0x80, 0xbe, 0xf5, 0x00, 0x00, 0x00, 0x00};
        RegRelJmp(0x44c7e6, exp44c7e6, 9, 5, &hk_mouseControllerScale, 0x90, 0x44c810);
    }

    // Offscreen uiRT sharpening hooks
    {
        static const uint8_t exp5956[6] = {0x89, 0x7e, 0x18, 0x89, 0x5e, 0x1c};
        RegRelJmp(0x405956, exp5956, 6, 6, &hk_setD3DBlend_tail, 0x90);

        static const uint8_t exp6693[9] = {0x80, 0x7d, 0x08, 0x00, 0x56, 0x8b, 0xf1, 0x74, 0x16};
        RegRelJmp(0x406693, exp6693, 9, 9, &hk_end2D_hook, 0x90);

        static const uint8_t exp77ae[6] = {0xc6, 0x46, 0x01, 0x01, 0x5e, 0xc3};
        RegRelJmp(0x4077ae, exp77ae, 6, 6, &hk_begin2D_tail, 0x90);

        static const uint8_t exp78f8[5] = {0xe8, 0xf3, 0x20, 0x00, 0x00};
        RegRelCall(0x4078f8, exp78f8, 5, 5, &CleanupUiRT_Impl);
    }
}
