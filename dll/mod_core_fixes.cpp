#include "mod_state.h"

#include <cstdio>
#include <cstring>

namespace {

using namespace edgemod;

// ------------------------------------------------------------------
// 1. D3D9 texture-transform color fix (d3d9_color_fix.py)
// ------------------------------------------------------------------
static void __stdcall MaterialApplyTexTransform_Impl(
    void* devFromEax, uint32_t stage, const void* matFromEcx, void* devFromEbp
) {
    using SetTransformFn = HRESULT(__stdcall*)(void*, uint32_t, const void*);
    using SetTssFn = HRESULT(__stdcall*)(void*, uint32_t, uint32_t, uint32_t);
    auto** vtEax = *reinterpret_cast<void***>(devFromEax);
    reinterpret_cast<SetTransformFn>(vtEax[0xb0 / 4])(devFromEax, stage + 16, matFromEcx);
    if (devFromEbp != nullptr) {
        auto** vtEbp = *reinterpret_cast<void***>(devFromEbp);
        reinterpret_cast<SetTssFn>(vtEbp[0x10c / 4])(devFromEbp, stage, 0x18 /* D3DTSS_TEXTURETRANSFORMFLAGS */, 2 /* COUNT2 */);
    }
}

static void __declspec(naked) hk_MaterialApplyTexTransform() {
    __asm {
        push DWORD PTR [ebp - 0x4]
        push ecx
        push ebx
        push eax
        call MaterialApplyTexTransform_Impl
        ret
    }
}

static void __stdcall RendererBeginSceneTexFlags_Impl(void* dev) {
    if (dev == nullptr) return;
    using SetTssFn = HRESULT(__stdcall*)(void*, uint32_t, uint32_t, uint32_t);
    auto** vt = *reinterpret_cast<void***>(dev);
    auto setTss = reinterpret_cast<SetTssFn>(vt[0x10c / 4]);
    for (uint32_t stage = 0; stage < 8; ++stage) {
        setTss(dev, stage, 0x18 /* D3DTSS_TEXTURETRANSFORMFLAGS */, 2 /* D3DTTFF_COUNT2 */);
    }
}

static void __declspec(naked) hk_RendererBeginSceneTexFlags() {
    __asm {
        push DWORD PTR [esi + 0xcc]
        call RendererBeginSceneTexFlags_Impl
        mov ecx, DWORD PTR [esi + 0xc0]
        ret
    }
}

// ------------------------------------------------------------------
// 2. Audio Volume 0..100 (step 10) + XACT "Default" category (audio_volume.py)
// ------------------------------------------------------------------
static int ClampVol(int v) {
    if (v < 0) return 0;
    if (v > 100) return 100;
    return v;
}

static int StepVol10(int cur) {
    cur = ClampVol(cur);
    uint8_t* sm = GetScreenMgr();
    bool left = (sm != nullptr && sm[0xb3] != 0);
    if (!left) {
        int next = (cur / 10) * 10 + 10;
        return next <= 100 ? next : 0;
    }
    if (cur <= 0) return 100;
    return ((cur - 1) / 10) * 10;
}

static void ApplySfxVolume(int sfxVol) {
    sfxVol = ClampVol(sfxVol);
    uint8_t* ap = GetAudioPlayer();
    if (ap != nullptr) {
        float fVol = static_cast<float>(static_cast<double>(sfxVol) / 100.0);
        using SetVolFn = void(__thiscall*)(void*, int, float);
        reinterpret_cast<SetVolFn>(0x474480)(ap, 0, fVol);
    }
}

static void ApplyMusicVolume(int newVol, int oldVol, uint8_t* sm) {
    if (newVol > 0) {
        Game_PlaySfx(sm, 0xc);
        if (oldVol <= 0) {
            reinterpret_cast<void(__thiscall*)(void*, int)>(0x481780)(sm, 0);
        } else {
            uint8_t* ap = GetAudioPlayer();
            if (ap != nullptr) {
                float trackVol = *reinterpret_cast<float*>(sm + 0x94);
                reinterpret_cast<void(__thiscall*)(void*, int, float)>(0x474480)(ap, 1, trackVol);
            }
        }
    } else {
        Game_PlaySfx(sm, 0xb);
        reinterpret_cast<void(__thiscall*)(void*, int, float)>(0x481560)(sm, 0, 0.0f);
    }
}

static void __cdecl SfxVolAction_Impl() {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
    int* pSfx = reinterpret_cast<int*>(sm + 0x2b0);
    int newVol = StepVol10(*pSfx);
    *pSfx = newVol;
    ApplySfxVolume(newVol);
    if (newVol > 0) {
        Game_PlaySfx(sm, 0xc);
    }
    Game_SaveUserSettings(sm + 0x298);
}

static void __cdecl MusicVolAction_Impl() {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
    int* pMus = reinterpret_cast<int*>(sm + 0x2ac);
    int oldVol = *pMus;
    int newVol = StepVol10(oldVol);
    *pMus = newVol;
    ApplyMusicVolume(newVol, oldVol, sm);
    Game_SaveUserSettings(sm + 0x298);
}

static float __cdecl CalcScaledMusicVol(float rawVol) {
    uint8_t* sm = GetScreenMgr();
    int musVol = (sm != nullptr) ? ClampVol(*reinterpret_cast<int*>(sm + 0x2ac)) : 100;
    double scaleConst = *reinterpret_cast<const double*>(0x585490);
    return static_cast<float>(static_cast<double>(rawVol) * scaleConst * (static_cast<double>(musVol) / 100.0));
}

static void __declspec(naked) hk_scaleMusicVol() {
    __asm {
        push ecx
        push DWORD PTR [ebp + 0xc]
        call CalcScaledMusicVol
        add esp, 4
        fstp DWORD PTR [esi + 0x10]
        movss xmm0, DWORD PTR [esi + 0x10]
        pop ecx
        mov ecx, DWORD PTR [esi + 0x8]
        xor edx, edx
        ret
    }
}

static void __cdecl ApplyVolOnLoad_Impl(uint8_t* sm) {
    int* pMus = reinterpret_cast<int*>(sm + 0x2ac);
    int* pSfx = reinterpret_cast<int*>(sm + 0x2b0);
    *pMus = ClampVol(*pMus);
    *pSfx = ClampVol(*pSfx);
    ApplySfxVolume(*pSfx);
    uint8_t* ap = GetAudioPlayer();
    if (ap != nullptr) {
        reinterpret_cast<void(__thiscall*)(void*, int, float)>(0x474480)(ap, 1, 0.0f);
    }
}

static void __declspec(naked) hk_applyVolOnLoad() {
    __asm {
        mov eax, 0x492ed0
        call eax
        push esi
        call ApplyVolOnLoad_Impl
        add esp, 4
        ret
    }
}

} // namespace

namespace edgemod {

void __cdecl FmtInt0to100Wstr(int val, EdgeWString* out) {
    if (val < 0) val = 0;
    if (val > 100) val = 100;
    out->clear_sso();
    uint32_t n = 0;
    if (val >= 100) {
        out->buf[0] = L'1';
        out->buf[1] = L'0';
        out->buf[2] = L'0';
        n = 3;
    } else {
        if (val >= 10) {
            out->buf[n++] = static_cast<wchar_t>(L'0' + (val / 10));
        }
        out->buf[n++] = static_cast<wchar_t>(L'0' + (val % 10));
    }
    out->buf[n] = L'\0';
    out->size = n;
}

void SetSfxVolume(int newVol, int sfxId, bool save) {
    newVol = ClampVol(newVol);
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
    int* pSfx = reinterpret_cast<int*>(sm + 0x2b0);
    bool changed = (*pSfx != newVol);
    *pSfx = newVol;
    ApplySfxVolume(newVol);
    if (sfxId > 0 && changed && newVol > 0) {
        Game_PlaySfx(sm, sfxId);
    }
    if (save) {
        Game_SaveUserSettings(sm + 0x298);
    }
}

void SetMusicVolume(int newVol, int sfxId, bool save) {
    newVol = ClampVol(newVol);
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return;
    int* pMus = reinterpret_cast<int*>(sm + 0x2ac);
    int oldVol = *pMus;
    *pMus = newVol;
    if (oldVol != newVol) {
        if (newVol > 0) {
            if (sfxId > 0) {
                Game_PlaySfx(sm, sfxId);
            }
            if (oldVol <= 0) {
                reinterpret_cast<void(__thiscall*)(void*, int)>(0x481780)(sm, 0);
            } else {
                uint8_t* ap = GetAudioPlayer();
                if (ap != nullptr) {
                    float trackVol = *reinterpret_cast<float*>(sm + 0x94);
                    reinterpret_cast<void(__thiscall*)(void*, int, float)>(0x474480)(ap, 1, trackVol);
                }
            }
        } else {
            if (sfxId > 0) {
                int offSfx = (sfxId == 0xc) ? 0xb : sfxId;
                Game_PlaySfx(sm, offSfx);
            }
            reinterpret_cast<void(__thiscall*)(void*, int, float)>(0x481560)(sm, 0, 0.0f);
        }
    }
    if (save) {
        Game_SaveUserSettings(sm + 0x298);
    }
}

} // namespace edgemod

void RegisterCoreFixes() {
    // 1. Bonus levels unlock (0x49450e, 6B)
    {
        static const uint8_t exp[6] = {0x8a, 0x81, 0xd4, 0x02, 0x00, 0x00};
        static const uint8_t rep[6] = {0xb0, 0x01, 0x90, 0x90, 0x90, 0x90};
        RegBytes(0x49450e, exp, rep, 6);
    }

    // 2. D3D9 texture-transform color fix (0x437388 8B, 0x405f03 6B)
    {
        static const uint8_t expMat[8] = {0x51, 0x83, 0xc3, 0x10, 0x53, 0x50, 0xff, 0xd2};
        RegRelCall(0x437388, expMat, 8, 8, &hk_MaterialApplyTexTransform, 0x90);

        static const uint8_t expBeg[6] = {0x8b, 0x8e, 0xc0, 0x00, 0x00, 0x00};
        RegRelCall(0x405f03, expBeg, 6, 6, &hk_RendererBeginSceneTexFlags, 0x90);
    }

    // 3. Audio volume 0..100 & "Effects" -> "Default"
    {
        static const uint8_t expEff[8] = {'E', 'f', 'f', 'e', 'c', 't', 's', '\0'};
        static const uint8_t repDef[8] = {'D', 'e', 'f', 'a', 'u', 'l', 't', '\0'};
        RegBytes(0x585474, expEff, repDef, 8);

        static const uint8_t expTogSfx[13] = {0x56, 0x8b, 0x35, 0x80, 0x90, 0x5f, 0x00, 0x8b, 0x86, 0xb0, 0x02, 0x00, 0x00};
        RegRelJmp(0x494610, expTogSfx, 13, 5, &SfxVolAction_Impl, 0x90, 0x494650);

        static const uint8_t expTogMus[13] = {0x56, 0x8b, 0x35, 0x80, 0x90, 0x5f, 0x00, 0x8b, 0x86, 0xac, 0x02, 0x00, 0x00};
        RegRelJmp(0x494650, expTogMus, 13, 5, &MusicVolAction_Impl, 0x90, 0x4946c0);

        static const uint8_t expSetVol[28] = {
            0xf3, 0x0f, 0x10, 0x45, 0x0c, 0x8b, 0x4e, 0x08, 0x0f, 0x5a, 0xc0, 0xf2, 0x0f, 0x59,
            0x05, 0x90, 0x54, 0x58, 0x00, 0x66, 0x0f, 0x5a, 0xc0, 0xf3, 0x0f, 0x11, 0x46, 0x10
        };
        // call hk_scaleMusicVol (5B) + jmp 0x4744bf (5B -> offset +5 = 0x4744af, target 0x4744bf = +0x15? No! Wait: 0x4744a3 + 28 = 0x4744bf!)
        // Wait: if we pad 0x4744a3..0x4744bf with NOPs after the 5-byte call, execution simply falls through 23 NOPs directly into 0x4744bf!
        RegRelCall(0x4744a3, expSetVol, 28, 28, &hk_scaleMusicVol, 0x90, 0x4744bf);

        static const uint8_t expLoad[5] = {0xe8, 0x4c, 0x03, 0x01, 0x00};
        RegRelCall(0x482b7f, expLoad, 5, 5, &hk_applyVolOnLoad, 0x90);
    }
}
