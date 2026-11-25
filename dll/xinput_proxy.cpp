#include "patch_utils.h"

namespace {

HMODULE g_realXInput = nullptr;

using FnXInputGetState = DWORD(WINAPI*)(DWORD, void*);
using FnXInputSetState = DWORD(WINAPI*)(DWORD, void*);
using FnXInputGetCaps = DWORD(WINAPI*)(DWORD, DWORD, void*);
using FnXInputEnable = void(WINAPI*)(BOOL);
using FnXInputGetAudio = DWORD(WINAPI*)(DWORD, GUID*, GUID*);
using FnXInputGetBattery = DWORD(WINAPI*)(DWORD, BYTE, void*);
using FnXInputGetKeystroke = DWORD(WINAPI*)(DWORD, DWORD, void*);
using FnXInputOrd100 = DWORD(WINAPI*)(DWORD, void*);
using FnXInputOrd101 = DWORD(WINAPI*)(DWORD, DWORD, void*);
using FnXInputOrd102 = DWORD(WINAPI*)(DWORD);
using FnXInputOrd103 = DWORD(WINAPI*)(DWORD);

FnXInputGetState     pfnGetState = nullptr;
FnXInputSetState     pfnSetState = nullptr;
FnXInputGetCaps      pfnGetCaps = nullptr;
FnXInputEnable       pfnEnable = nullptr;
FnXInputGetAudio     pfnGetAudio = nullptr;
FnXInputGetBattery   pfnGetBattery = nullptr;
FnXInputGetKeystroke pfnGetKeystroke = nullptr;
FnXInputOrd100       pfnOrd100 = nullptr;
FnXInputOrd101       pfnOrd101 = nullptr;
FnXInputOrd102       pfnOrd102 = nullptr;
FnXInputOrd103       pfnOrd103 = nullptr;

void EnsureRealXInputLoaded() {
    if (g_realXInput != nullptr) return;
    wchar_t sysDir[MAX_PATH]{};
    UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (n > 0 && n + 16 < MAX_PATH) {
        std::memcpy(sysDir + n, L"\\xinput1_3.dll", 14 * sizeof(wchar_t));
        g_realXInput = LoadLibraryW(sysDir);
    }
    if (g_realXInput == nullptr) {
        g_realXInput = LoadLibraryW(L"C:\\Windows\\System32\\xinput1_3.dll");
    }
    if (g_realXInput != nullptr) {
        pfnGetState     = reinterpret_cast<FnXInputGetState>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(2)));
        pfnSetState     = reinterpret_cast<FnXInputSetState>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(3)));
        pfnGetCaps      = reinterpret_cast<FnXInputGetCaps>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(4)));
        pfnEnable       = reinterpret_cast<FnXInputEnable>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(5)));
        pfnGetAudio     = reinterpret_cast<FnXInputGetAudio>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(6)));
        pfnGetBattery   = reinterpret_cast<FnXInputGetBattery>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(7)));
        pfnGetKeystroke = reinterpret_cast<FnXInputGetKeystroke>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(8)));
        pfnOrd100       = reinterpret_cast<FnXInputOrd100>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(100)));
        pfnOrd101       = reinterpret_cast<FnXInputOrd101>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(101)));
        pfnOrd102       = reinterpret_cast<FnXInputOrd102>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(102)));
        pfnOrd103       = reinterpret_cast<FnXInputOrd103>(GetProcAddress(g_realXInput, MAKEINTRESOURCEA(103)));
    }
}

void RegisterAllPatchesOnce() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    RegisterCoreFixes();
    RegisterMusicPlaylist();
    RegisterUiScaleAndRt();
    RegisterMenuCameraOptions();
    RegisterFinishEffects();
    RegisterColorTheme();
    RegisterUiSmoothPart1();
    RegisterUiSmoothPart2();
    RegisterEdgeTimer();
    RegisterMenuHover();
    RegisterCubeDistortion();
}

} // namespace

extern "C" {

DWORD WINAPI XInputGetState(DWORD dwUserIndex, void* pState) {
    EnsureRealXInputLoaded();
    return pfnGetState ? pfnGetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputSetState(DWORD dwUserIndex, void* pVibration) {
    EnsureRealXInputLoaded();
    return pfnSetState ? pfnSetState(dwUserIndex, pVibration) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, void* pCapabilities) {
    EnsureRealXInputLoaded();
    return pfnGetCaps ? pfnGetCaps(dwUserIndex, dwFlags, pCapabilities) : ERROR_DEVICE_NOT_CONNECTED;
}

void WINAPI XInputEnable(BOOL enable) {
    EnsureRealXInputLoaded();
    if (pfnEnable) pfnEnable(enable);
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD dwUserIndex, GUID* pDSoundRenderGuid, GUID* pDSoundCaptureGuid) {
    EnsureRealXInputLoaded();
    return pfnGetAudio ? pfnGetAudio(dwUserIndex, pDSoundRenderGuid, pDSoundCaptureGuid) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, void* pBatteryInfo) {
    EnsureRealXInputLoaded();
    return pfnGetBattery ? pfnGetBattery(dwUserIndex, devType, pBatteryInfo) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, void* pKeystroke) {
    EnsureRealXInputLoaded();
    return pfnGetKeystroke ? pfnGetKeystroke(dwUserIndex, dwReserved, pKeystroke) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputOrd100(DWORD dwUserIndex, void* pState) {
    EnsureRealXInputLoaded();
    return pfnOrd100 ? pfnOrd100(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputOrd101(DWORD dwUserIndex, DWORD dwFlag, void* pVoid) {
    EnsureRealXInputLoaded();
    return pfnOrd101 ? pfnOrd101(dwUserIndex, dwFlag, pVoid) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputOrd102(DWORD dwUserIndex) {
    EnsureRealXInputLoaded();
    return pfnOrd102 ? pfnOrd102(dwUserIndex) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputOrd103(DWORD dwUserIndex) {
    EnsureRealXInputLoaded();
    return pfnOrd103 ? pfnOrd103(dwUserIndex) : ERROR_DEVICE_NOT_CONNECTED;
}

int __cdecl EdgeMod_VerifyBuffer(const uint8_t* exeFileBuf, size_t bufLen) {
    RegisterAllPatchesOnce();
    return edgemod::VerifyAllPatchesAgainstBuffer(exeFileBuf, bufLen);
}

int __cdecl EdgeMod_GetPatchCount() {
    RegisterAllPatchesOnce();
    return static_cast<int>(edgemod::GetPatchRegistry().size());
}

void __cdecl EdgeMod_GetPatchRange(int idx, uint32_t* outStartVa, uint32_t* outEndVa) {
    RegisterAllPatchesOnce();
    const auto& r = edgemod::GetPatchRegistry()[static_cast<size_t>(idx)];
    *outStartVa = r.va;
    *outEndVa = r.cover_end_va;
}

} // extern "C"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        if (GetProcAddress(GetModuleHandleW(nullptr), "EdgeMod_VerifierHost") == nullptr) {
            RegisterAllPatchesOnce();
            if (!edgemod::ApplyAllRegisteredPatches()) {
                MessageBoxW(
                    nullptr,
                    L"XINPUT1_3.dll patch verification failed.\nEnsure edge.exe is the unmodified Steam build.",
                    L"EDGE Mod Error",
                    MB_ICONERROR | MB_OK
                );
                return FALSE;
            }
        }
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (g_realXInput != nullptr) {
            FreeLibrary(g_realXInput);
            g_realXInput = nullptr;
        }
    }
    return TRUE;
}
