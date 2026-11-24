#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace edgemod {

// MSVC 2010 std::wstring release layout (24 bytes)
struct EdgeWString {
    union {
        wchar_t buf[8];
        wchar_t* ptr;
    };
    uint32_t size;
    uint32_t cap;

    void clear_sso() {
        std::memset(this, 0, sizeof(*this));
        cap = 7;
    }

    void set_literal(const wchar_t* s, uint32_t len) {
        std::memset(this, 0, sizeof(*this));
        if (len < 8) {
            std::memcpy(buf, s, (len + 1) * sizeof(wchar_t));
            size = len;
            cap = 7;
        } else {
            ptr = const_cast<wchar_t*>(s);
            size = len;
            cap = len;
        }
    }

    void free_if_heap() {
        if (cap >= 8 && ptr != nullptr) {
            using FreeFn = void(__cdecl*)(void*);
            reinterpret_cast<FreeFn>(0x4ba62a)(ptr);
            ptr = nullptr;
        }
        size = 0;
        cap = 7;
    }
};
static_assert(sizeof(EdgeWString) == 24, "EdgeWString must be 24 bytes");

#define EDGE_STATIC_WSTR(name, lit) \
    static EdgeWString name = []() { \
        EdgeWString w{}; \
        w.set_literal(L##lit, static_cast<uint32_t>(sizeof(L##lit) / sizeof(wchar_t) - 1)); \
        return w; \
    }()

// MSVC 2010 std::string release layout (28 bytes)
struct EdgeString {
    union {
        char buf[16];
        char* ptr;
    };
    uint32_t size;
    uint32_t cap;
    uint32_t pad;

    const char* c_str() const { return cap >= 16 ? ptr : buf; }

    void ctor_cstr(const char* s) {
        using CtorFn = EdgeString*(__thiscall*)(EdgeString*, const char*);
        reinterpret_cast<CtorFn>(0x4014e0)(this, s);
    }

    void dtor() {
        using DtorFn = void(__thiscall*)(EdgeString*);
        reinterpret_cast<DtorFn>(0x51e620)(this);
    }
};
static_assert(sizeof(EdgeString) == 28, "EdgeString must be 28 bytes");

// Raw game singletons
inline uint8_t* GetScreenMgr() { return *reinterpret_cast<uint8_t**>(0x5f9080); }
inline uint8_t* GetWorldScreen() { return *reinterpret_cast<uint8_t**>(0x5f9064); }
inline uint8_t* GetMenuScreen() { return *reinterpret_cast<uint8_t**>(0x5f91b4); }
inline uint8_t* GetRenderer() { return *reinterpret_cast<uint8_t**>(0x5f8770); }
inline uint8_t* GetAudioPlayer() { return *reinterpret_cast<uint8_t**>(0x5f903c); }
inline uint8_t* GetLevel() { return *reinterpret_cast<uint8_t**>(0x5f9040); }
inline uint8_t* GetMatrixStack() { return *reinterpret_cast<uint8_t**>(0x5f87d0); }

// PE VA <-> file offset mapper for offline verification against unmodified edge.exe
inline bool VaToFileOffset(uint32_t va, uint32_t* outOff) {
    if (va >= 0x401000 && va < 0x576600) {
        *outOff = (va - 0x401000) + 0x400;
        return true;
    }
    if (va >= 0x577000 && va < 0x5e2a00) {
        *outOff = (va - 0x577000) + 0x175a00;
        return true;
    }
    if (va >= 0x5e3000 && va < 0x5f8800) {
        *outOff = (va - 0x5e3000) + 0x1e1400;
        return true;
    }
    return false;
}

struct PatchRecord {
    uint32_t va;
    std::vector<uint8_t> expected;
    std::vector<uint8_t> replacement;
    uint32_t cover_end_va; // full replaced instruction span for static diff coverage check
};

inline std::vector<PatchRecord>& GetPatchRegistry() {
    static std::vector<PatchRecord> registry;
    return registry;
}

inline void RegBytes(uint32_t va, const void* expectedBytes, const void* newBytes, size_t len, uint32_t coverEndVa = 0) {
    PatchRecord rec;
    rec.va = va;
    rec.expected.assign(static_cast<const uint8_t*>(expectedBytes), static_cast<const uint8_t*>(expectedBytes) + len);
    rec.replacement.assign(static_cast<const uint8_t*>(newBytes), static_cast<const uint8_t*>(newBytes) + len);
    rec.cover_end_va = coverEndVa ? coverEndVa : (va + static_cast<uint32_t>(len));
    GetPatchRegistry().push_back(std::move(rec));
}

inline void RegU32(uint32_t va, uint32_t expectedVal, uint32_t newVal) {
    RegBytes(va, &expectedVal, &newVal, 4);
}

inline void RegRel(uint32_t va, uint8_t opcode, const void* expectedBytes, size_t expectedLen,
                   size_t totalSpan, uint32_t targetVa, uint8_t padByte = 0x90, uint32_t coverEndVa = 0) {
    std::vector<uint8_t> repl(totalSpan, padByte);
    repl[0] = opcode;
    int32_t rel = static_cast<int32_t>(targetVa) - static_cast<int32_t>(va + 5);
    std::memcpy(&repl[1], &rel, 4);
    PatchRecord rec;
    rec.va = va;
    rec.expected.assign(static_cast<const uint8_t*>(expectedBytes), static_cast<const uint8_t*>(expectedBytes) + expectedLen);
    rec.replacement = std::move(repl);
    rec.cover_end_va = coverEndVa ? coverEndVa : (va + static_cast<uint32_t>(totalSpan));
    GetPatchRegistry().push_back(std::move(rec));
}

inline void RegRelCall(uint32_t va, const void* expectedBytes, size_t expectedLen,
                       size_t totalSpan, void* targetFn, uint8_t padByte = 0x90, uint32_t coverEndVa = 0) {
    RegRel(va, 0xE8, expectedBytes, expectedLen, totalSpan, reinterpret_cast<uint32_t>(targetFn), padByte, coverEndVa);
}

inline void RegRelJmp(uint32_t va, const void* expectedBytes, size_t expectedLen,
                      size_t totalSpan, void* targetFn, uint8_t padByte = 0xCC, uint32_t coverEndVa = 0) {
    RegRel(va, 0xE9, expectedBytes, expectedLen, totalSpan, reinterpret_cast<uint32_t>(targetFn), padByte, coverEndVa);
}

inline void RegRelCallVa(uint32_t va, const void* expectedBytes, size_t expectedLen,
                         size_t totalSpan, uint32_t targetVa, uint8_t padByte = 0x90, uint32_t coverEndVa = 0) {
    RegRel(va, 0xE8, expectedBytes, expectedLen, totalSpan, targetVa, padByte, coverEndVa);
}

inline void RegRelJmpVa(uint32_t va, const void* expectedBytes, size_t expectedLen,
                        size_t totalSpan, uint32_t targetVa, uint8_t padByte = 0xCC, uint32_t coverEndVa = 0) {
    RegRel(va, 0xE9, expectedBytes, expectedLen, totalSpan, targetVa, padByte, coverEndVa);
}

inline int VerifyAllPatchesAgainstBuffer(const uint8_t* buf, size_t bufLen) {
    int mismatches = 0;
    for (const auto& rec : GetPatchRegistry()) {
        uint32_t off = 0;
        if (!VaToFileOffset(rec.va, &off) || off + rec.expected.size() > bufLen) {
            ++mismatches;
            continue;
        }
        if (std::memcmp(buf + off, rec.expected.data(), rec.expected.size()) != 0) {
            ++mismatches;
        }
    }
    return mismatches;
}

inline bool ApplyAllRegisteredPatches() {
    DWORD oldText = 0, oldRdata = 0, oldData = 0;
    VirtualProtect(reinterpret_cast<void*>(0x401000), 0x576600 - 0x401000, PAGE_EXECUTE_READWRITE, &oldText);
    VirtualProtect(reinterpret_cast<void*>(0x577000), 0x5e2a00 - 0x577000, PAGE_READWRITE, &oldRdata);
    VirtualProtect(reinterpret_cast<void*>(0x5e3000), 0x5f8800 - 0x5e3000, PAGE_READWRITE, &oldData);

    bool ok = true;
    for (const auto& rec : GetPatchRegistry()) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(rec.va);
        if (std::memcmp(ptr, rec.expected.data(), rec.expected.size()) != 0) {
            ok = false;
            break;
        }
    }
    if (ok) {
        for (const auto& rec : GetPatchRegistry()) {
            uint8_t* ptr = reinterpret_cast<uint8_t*>(rec.va);
            std::memcpy(ptr, rec.replacement.data(), rec.replacement.size());
        }
    }

    DWORD dummy = 0;
    VirtualProtect(reinterpret_cast<void*>(0x401000), 0x576600 - 0x401000, oldText, &dummy);
    VirtualProtect(reinterpret_cast<void*>(0x577000), 0x5e2a00 - 0x577000, oldRdata, &dummy);
    VirtualProtect(reinterpret_cast<void*>(0x5e3000), 0x5f8800 - 0x5e3000, oldData, &dummy);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    return ok;
}

} // namespace edgemod

void RegisterCoreFixes();
void RegisterUiScaleAndRt();
void RegisterMenuCameraOptions();
void RegisterFinishEffects();
void RegisterColorTheme();
void RegisterUiSmoothPart1();
void RegisterUiSmoothPart2();
