#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <vector>

extern "C" __declspec(dllexport) int __cdecl EdgeMod_VerifierHost() {
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: verify_dll.exe <dll_path> <bak_exe_path> [ranges_out_txt]\n");
        return 2;
    }

    HMODULE hDll = LoadLibraryA(argv[1]);
    if (!hDll) {
        std::fprintf(stderr, "Failed to load DLL %s (err=%lu)\n", argv[1], GetLastError());
        return 3;
    }

    using VerifyFn = int(__cdecl*)(const uint8_t*, size_t);
    using CountFn  = int(__cdecl*)();
    using RangeFn  = void(__cdecl*)(int, uint32_t*, uint32_t*);

    auto pVerify = reinterpret_cast<VerifyFn>(GetProcAddress(hDll, "EdgeMod_VerifyBuffer"));
    auto pCount  = reinterpret_cast<CountFn>(GetProcAddress(hDll, "EdgeMod_GetPatchCount"));
    auto pRange  = reinterpret_cast<RangeFn>(GetProcAddress(hDll, "EdgeMod_GetPatchRange"));
    if (!pVerify || !pCount || !pRange) {
        std::fprintf(stderr, "Missing EdgeMod_* exports in %s\n", argv[1]);
        FreeLibrary(hDll);
        return 4;
    }

    FILE* f = std::fopen(argv[2], "rb");
    if (!f) {
        std::fprintf(stderr, "Failed to open %s\n", argv[2]);
        FreeLibrary(hDll);
        return 5;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);

    int total = pCount();
    int mismatches = pVerify(buf.data(), buf.size());
    std::printf("EdgeMod_VerifyBuffer: %d patches checked, %d mismatches\n", total, mismatches);

    if (argc >= 4) {
        FILE* rf = std::fopen(argv[3], "w");
        if (rf) {
            for (int i = 0; i < total; ++i) {
                uint32_t s = 0, e = 0;
                pRange(i, &s, &e);
                std::fprintf(rf, "0x%06x 0x%06x\n", s, e);
            }
            std::fclose(rf);
        }
    }

    FreeLibrary(hDll);
    return mismatches == 0 ? 0 : 1;
}
