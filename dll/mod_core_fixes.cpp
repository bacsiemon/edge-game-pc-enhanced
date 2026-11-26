#include "mod_state.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

static bool g_windowFocused = true;
static bool g_focusMuteApplied = false;

static HWND GetGameHwnd() {
    HWND hwnd = *reinterpret_cast<HWND*>(0x5f8bc8);
    if (hwnd == nullptr) {
        hwnd = reinterpret_cast<HWND(__cdecl*)()>(0x414820)();
    }
    return hwnd;
}

static bool QueryWindowFocused(HWND hwnd) {
    if (hwnd == nullptr) {
        hwnd = GetGameHwnd();
    }
    if (hwnd == nullptr) return true;
    if (IsIconic(hwnd)) return false;
    HWND fg = GetForegroundWindow();
    if (fg == nullptr) return false;
    if (fg == hwnd) return true;
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    return fgPid == GetCurrentProcessId();
}

static bool IsAudioMutedByFocus() {
    return (g_muteUnfocused != 0) && !g_windowFocused;
}

static void __stdcall RendererBeginSceneTexFlags_Impl(void* dev) {
    if (g_muteUnfocused != 0 || g_focusMuteApplied) {
        bool focused = QueryWindowFocused(nullptr);
        if (focused != g_windowFocused) {
            g_windowFocused = focused;
        }
        SyncFocusAudioMute();
    }
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
        float fVol = IsAudioMutedByFocus() ? 0.0f : static_cast<float>(static_cast<double>(sfxVol) / 100.0);
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
    if (IsAudioMutedByFocus()) return 0.0f;
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
    g_windowFocused = QueryWindowFocused(nullptr);
    g_focusMuteApplied = IsAudioMutedByFocus();
    ApplySfxVolume(*pSfx);
    uint8_t* ap = GetAudioPlayer();
    if (ap != nullptr) {
        reinterpret_cast<void(__thiscall*)(void*, int, float)>(0x474480)(ap, 1, 0.0f);
    }
}

// ------------------------------------------------------------------
// 4. Hint Ghost SFX volume reduction & infinite loop fix
// ------------------------------------------------------------------
static uint16_t ReadLe16(const uint8_t* p, size_t off) {
    return static_cast<uint16_t>(p[off]) | (static_cast<uint16_t>(p[off + 1]) << 8);
}

static uint32_t ReadLe32(const uint8_t* p, size_t off) {
    return static_cast<uint32_t>(p[off]) |
           (static_cast<uint32_t>(p[off + 1]) << 8) |
           (static_cast<uint32_t>(p[off + 2]) << 16) |
           (static_cast<uint32_t>(p[off + 3]) << 24);
}

static void WriteLe16(std::vector<uint8_t>& buf, size_t off, uint16_t v) {
    buf[off + 0] = static_cast<uint8_t>(v & 0xffu);
    buf[off + 1] = static_cast<uint8_t>((v >> 8) & 0xffu);
}

static void WriteLe32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    buf[off + 0] = static_cast<uint8_t>(v & 0xffu);
    buf[off + 1] = static_cast<uint8_t>((v >> 8) & 0xffu);
    buf[off + 2] = static_cast<uint8_t>((v >> 16) & 0xffu);
    buf[off + 3] = static_cast<uint8_t>((v >> 24) & 0xffu);
}

static uint16_t ComputeXsbFcs16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xffffu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408u) : static_cast<uint16_t>(crc >> 1);
        }
    }
    return static_cast<uint16_t>(crc ^ 0xffffu);
}

static uint8_t ScaleXactVolByte(uint8_t oldVol, int num, int den) {
    long scaled = std::lround(static_cast<double>(oldVol) * static_cast<double>(num) / static_cast<double>(den));
    if (scaled < 1) scaled = 1;
    if (scaled > 255) scaled = 255;
    return static_cast<uint8_t>(scaled);
}

static bool PatchHologramSoundBank(const uint8_t* src, uint32_t srcSize, std::vector<uint8_t>& out) {
    if (src == nullptr || srcSize < 0x4au) return false;
    if (ReadLe32(src, 0x00) != 0x4b424453u) return false; // "SDBK"
    if (ReadLe16(src, 0x04) < 43u || ReadLe16(src, 0x06) != 43u) return false;

    const uint16_t cueSimpleCount  = ReadLe16(src, 0x13);
    const uint16_t cueComplexCount = ReadLe16(src, 0x15);
    const uint16_t soundCount      = ReadLe16(src, 0x1c);
    const uint32_t cueSimpleOff    = ReadLe32(src, 0x22);
    const uint32_t cueNameOff      = ReadLe32(src, 0x2a);
    const uint32_t varOff          = ReadLe32(src, 0x32);
    const uint32_t transOff        = ReadLe32(src, 0x36);
    const uint32_t cueNameIdxOff   = ReadLe32(src, 0x42);
    const uint32_t soundOff        = ReadLe32(src, 0x46);

    if (cueSimpleCount == 0 || cueSimpleOff == 0xffffffffu || cueNameOff == 0xffffffffu || soundOff == 0xffffffffu) {
        return false;
    }
    if (cueSimpleOff + static_cast<uint32_t>(cueSimpleCount) * 5u > srcSize || cueNameOff >= srcSize) {
        return false;
    }

    uint32_t loopSndOff  = 0xffffffffu;
    uint32_t onOffSndOff = 0xffffffffu;
    uint32_t namePos = cueNameOff;
    for (uint16_t i = 0; i < cueSimpleCount && namePos < srcSize; ++i) {
        const char* cueName = reinterpret_cast<const char*>(src + namePos);
        size_t maxLen = srcSize - namePos;
        size_t len = 0;
        while (len < maxLen && cueName[len] != '\0') ++len;
        if (len >= maxLen) break;

        if (_stricmp(cueName, "hologram_loop") == 0) {
            loopSndOff = ReadLe32(src, cueSimpleOff + static_cast<size_t>(i) * 5u + 1u);
        } else if (_stricmp(cueName, "hologram_on-off") == 0) {
            onOffSndOff = ReadLe32(src, cueSimpleOff + static_cast<size_t>(i) * 5u + 1u);
        }
        namePos += static_cast<uint32_t>(len + 1u);
    }

    if (loopSndOff == 0xffffffffu && onOffSndOff == 0xffffffffu) {
        return false;
    }

    const bool allSimpleContiguous =
        soundCount > 0 &&
        soundOff < cueSimpleOff &&
        soundOff + static_cast<uint32_t>(soundCount) * 12u == cueSimpleOff &&
        cueComplexCount == 0 &&
        varOff == 0xffffffffu &&
        transOff == 0xffffffffu;

    if (loopSndOff != 0xffffffffu &&
        loopSndOff + 12u <= srcSize &&
        (src[loopSndOff] & 0x01u) == 0u &&
        allSimpleContiguous) {
        constexpr uint32_t kDelta = 24u;
        uint8_t cs[36] = {};
        cs[0x00] = 0x01; // SOUND_FLAG_COMPLEX
        cs[0x01] = src[loopSndOff + 1u];
        cs[0x02] = src[loopSndOff + 2u];
        cs[0x03] = ScaleXactVolByte(src[loopSndOff + 3u], 48, 90);
        cs[0x04] = src[loopSndOff + 4u];
        cs[0x05] = src[loopSndOff + 5u];
        cs[0x06] = src[loopSndOff + 6u];
        cs[0x07] = 36u;
        cs[0x08] = 0u;
        cs[0x09] = 1u;   // trackCount
        cs[0x0a] = 0xb4; // 180 = 0.0 dB track volume
        const uint32_t evtOff = loopSndOff + 19u;
        cs[0x0b] = static_cast<uint8_t>(evtOff & 0xffu);
        cs[0x0c] = static_cast<uint8_t>((evtOff >> 8) & 0xffu);
        cs[0x0d] = static_cast<uint8_t>((evtOff >> 16) & 0xffu);
        cs[0x0e] = static_cast<uint8_t>((evtOff >> 24) & 0xffu);
        cs[0x13] = 1u;   // eventCount
        // evtInfo: FACTEVENT_PLAYWAVE (1) | timestamp (0) | first-event bit 29 (0x20000000)
        cs[0x14] = 0x01u;
        cs[0x15] = 0x00u;
        cs[0x16] = 0x00u;
        cs[0x17] = 0x20u;
        cs[0x1a] = 0xffu; // separator
        cs[0x1c] = src[loopSndOff + 9u];  // waveIndex lo
        cs[0x1d] = src[loopSndOff + 10u]; // waveIndex hi
        cs[0x1e] = src[loopSndOff + 11u]; // waveBank
        cs[0x1f] = 0xffu; // loopCount = XACTLOOPCOUNT_INFINITE

        out.clear();
        out.reserve(srcSize + kDelta);
        out.insert(out.end(), src, src + loopSndOff);
        out.insert(out.end(), cs, cs + sizeof(cs));
        out.insert(out.end(), src + loopSndOff + 12u, src + srcSize);

        if (onOffSndOff != 0xffffffffu) {
            const uint32_t newOnOff = onOffSndOff + (onOffSndOff > loopSndOff ? kDelta : 0u);
            if (newOnOff + 9u <= out.size()) {
                out[newOnOff + 3u] = ScaleXactVolByte(out[newOnOff + 3u], 56, 90);
            }
        }

        static constexpr size_t kHdrOffs[10] = {0x22, 0x26, 0x2a, 0x2e, 0x32, 0x36, 0x3a, 0x3e, 0x42, 0x46};
        for (size_t hOff : kHdrOffs) {
            const uint32_t v = ReadLe32(out.data(), hOff);
            if (v != 0xffffffffu && v > loopSndOff) {
                WriteLe32(out, hOff, v + kDelta);
            }
        }

        const uint32_t newSimpleOff = ReadLe32(out.data(), 0x22);
        if (newSimpleOff != 0xffffffffu && newSimpleOff + static_cast<uint32_t>(cueSimpleCount) * 5u <= out.size()) {
            for (uint16_t i = 0; i < cueSimpleCount; ++i) {
                const size_t cOff = newSimpleOff + static_cast<size_t>(i) * 5u + 1u;
                const uint32_t sc = ReadLe32(out.data(), cOff);
                if (sc > loopSndOff) {
                    WriteLe32(out, cOff, sc + kDelta);
                }
            }
        }

        const uint32_t newNameIdxOff = (cueNameIdxOff != 0xffffffffu) ? ReadLe32(out.data(), 0x42) : 0xffffffffu;
        if (newNameIdxOff != 0xffffffffu && newNameIdxOff + static_cast<uint32_t>(cueSimpleCount) * 6u <= out.size()) {
            for (uint16_t i = 0; i < cueSimpleCount; ++i) {
                const size_t niOff = newNameIdxOff + static_cast<size_t>(i) * 6u;
                const uint32_t no = ReadLe32(out.data(), niOff);
                if (no > loopSndOff) {
                    WriteLe32(out, niOff, no + kDelta);
                }
            }
        }
    } else {
        out.assign(src, src + srcSize);
        if (onOffSndOff != 0xffffffffu && onOffSndOff + 9u <= out.size()) {
            out[onOffSndOff + 3u] = ScaleXactVolByte(out[onOffSndOff + 3u], 56, 90);
        }
        if (loopSndOff != 0xffffffffu && loopSndOff + 12u <= out.size()) {
            out[loopSndOff + 3u] = ScaleXactVolByte(out[loopSndOff + 3u], 48, 90);
            if ((src[loopSndOff] & 0x1fu) == 0x01u && loopSndOff + 19u <= out.size() && out[loopSndOff + 9u] >= 1u) {
                const uint32_t trackCode = ReadLe32(out.data(), loopSndOff + 11u);
                if (trackCode + 17u <= out.size() &&
                    out[trackCode] >= 1u &&
                    (ReadLe32(out.data(), trackCode + 1u) & 0x1fu) == 1u &&
                    out[trackCode + 7u] == 0xffu) {
                    out[trackCode + 12u] = 0xffu;
                }
            }
        }
    }

    WriteLe16(out, 0x08, ComputeXsbFcs16(out.data() + 0x12, out.size() - 0x12));
    return true;
}

static HRESULT __cdecl CreatePatchedSoundBank_Impl(
    void* xactEngine, const void* pvBuf, uint32_t dwSize, void** ppSoundBank
) {
    using CreateSoundBankFn = HRESULT(__stdcall*)(void*, const void*, uint32_t, uint32_t, uint32_t, void**);
    auto** vt = *reinterpret_cast<void***>(xactEngine);
    auto createSb = reinterpret_cast<CreateSoundBankFn>(vt[0x24 / 4]);

    static std::vector<uint8_t> g_patchedXsbBuf;
    std::vector<uint8_t> patched;
    if (PatchHologramSoundBank(static_cast<const uint8_t*>(pvBuf), dwSize, patched)) {
        g_patchedXsbBuf = std::move(patched);
        HRESULT hr = createSb(xactEngine, g_patchedXsbBuf.data(), static_cast<uint32_t>(g_patchedXsbBuf.size()), 0, 0, ppSoundBank);
        if (SUCCEEDED(hr)) {
            return hr;
        }
        g_patchedXsbBuf.clear();
    }
    return createSb(xactEngine, pvBuf, dwSize, 0, 0, ppSoundBank);
}

static void __declspec(naked) hk_xactCreateSoundBank() {
    // Replaces 21 bytes at 0x43de15..0x43de2a inside XactSoundSystem::loadSoundBank.
    // Inputs: edi = XactSoundSystem* ([edi+4] = IXACT3Engine*), edx = pvBuffer, ecx = dwSize, esi = path EdgeString*.
    // Output: eax = HRESULT, ebx = &ppSoundBank (ebp - 0x44), esi/ebp preserved.
    __asm {
        lea ebx, [ebp - 0x44]
        push ebx
        push ecx
        push edx
        push DWORD PTR [edi + 0x4]
        call CreatePatchedSoundBank_Impl
        add esp, 16
        ret
    }
}

static void __stdcall GhostPlayLoop_Impl(uint8_t* sm, int sfxId) {
    if (sm == nullptr) return;
    reinterpret_cast<void(__thiscall*)(void*, int)>(0x481610)(sm, sfxId);
    Game_PlaySfx(sm, sfxId);
}

static void __declspec(naked) hk_ghostPlayLoop() {
    // Replaces `call 0x4815f0` at 0x47a84e (__thiscall: ecx = ScreenMgr*, [esp+4] = sfxId).
    __asm {
        pop eax
        pop edx
        push edx
        push ecx
        push eax
        jmp GhostPlayLoop_Impl
    }
}

static uint32_t __fastcall ResetGhost_Impl(uint8_t* ws) {
    if (ws != nullptr) {
        ws[0x48] = 0;
        ws[0x50] = 0;
        *reinterpret_cast<uint32_t*>(ws + 0x64) = 0;
        *reinterpret_cast<uint32_t*>(ws + 0x68) = 0;
    }
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        reinterpret_cast<void(__thiscall*)(void*, int)>(0x481610)(sm, 0x15);
    }
    return 0;
}

static LRESULT CALLBACK hk_GameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    bool updateFocus = false;
    bool focused = g_windowFocused;
    switch (msg) {
    case WM_ACTIVATEAPP:
        updateFocus = true;
        focused = (wParam != 0) && !IsIconic(hwnd);
        break;
    case WM_ACTIVATE: {
        updateFocus = true;
        bool active = (LOWORD(wParam) != WA_INACTIVE) && (HIWORD(wParam) == 0);
        focused = active ? !IsIconic(hwnd) : QueryWindowFocused(hwnd);
        break;
    }
    case WM_SETFOCUS:
        updateFocus = true;
        focused = !IsIconic(hwnd);
        break;
    case WM_KILLFOCUS:
        updateFocus = true;
        focused = QueryWindowFocused(hwnd);
        break;
    case WM_SIZE:
        updateFocus = true;
        focused = (wParam == SIZE_MINIMIZED) ? false : QueryWindowFocused(hwnd);
        break;
    case WM_MOUSEWHEEL:
        OnOptionsMouseWheel(static_cast<int32_t>(GET_WHEEL_DELTA_WPARAM(wParam)));
        break;
    default:
        break;
    }
    if (updateFocus && focused != g_windowFocused) {
        g_windowFocused = focused;
        SyncFocusAudioMute();
    }
    return reinterpret_cast<WNDPROC>(0x44d4e0)(hwnd, msg, wParam, lParam);
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

void SyncFocusAudioMute() {
    bool shouldMute = IsAudioMutedByFocus();
    if (shouldMute == g_focusMuteApplied) return;
    uint8_t* ap = GetAudioPlayer();
    if (ap == nullptr) return;
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr && !shouldMute) return;
    g_focusMuteApplied = shouldMute;
    int sfxVol = (sm != nullptr) ? ClampVol(*reinterpret_cast<int*>(sm + 0x2b0)) : 100;
    ApplySfxVolume(sfxVol);
    if (sm != nullptr) {
        float trackVol = *reinterpret_cast<float*>(sm + 0x94);
        reinterpret_cast<void(__thiscall*)(void*, int, float)>(0x474480)(ap, 1, trackVol);
    } else {
        reinterpret_cast<void(__thiscall*)(void*, int, float)>(0x474480)(ap, 1, 0.0f);
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

        RegU32(0x44d72f, 0x0044d4e0u, reinterpret_cast<uint32_t>(&hk_GameWndProc));
    }

    // 4. Hint Ghost SFX volume reduction & native infinite loop
    {
        static const uint8_t expCreateSb[21] = {
            0x8b, 0x47, 0x04, 0x8b, 0x38, 0x8d, 0x5d, 0xbc, 0x53, 0x6a, 0x00,
            0x6a, 0x00, 0x51, 0x52, 0x8b, 0x57, 0x24, 0x50, 0xff, 0xd2
        };
        RegRelCall(0x43de15, expCreateSb, 21, 21, &hk_xactCreateSoundBank, 0x90);

        static const uint8_t expGhostLoop[5] = {0xe8, 0x9d, 0x6d, 0x00, 0x00};
        RegRelCall(0x47a84e, expGhostLoop, 5, 5, &hk_ghostPlayLoop);

        static const uint8_t expResetGhost[15] = {
            0x33, 0xc0, 0x88, 0x41, 0x48, 0x88, 0x41, 0x50, 0x89, 0x41, 0x64, 0x89, 0x41, 0x68, 0xc3
        };
        RegRelJmp(0x479740, expResetGhost, 15, 15, &ResetGhost_Impl, 0x90, 0x47974f);
    }
}
