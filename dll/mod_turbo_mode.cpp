#include "mod_state.h"
#include "mod_smooth_shared.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace edgemod {

namespace {

// Matches vanilla LevelSaveInfo layout (16 bytes) at UserSettings +0x4 std::map value.
struct TurboLevelSaveInfo {
    uint8_t  completed;
    uint8_t  pad[3];
    uint32_t bestTimeTicks;
    int32_t  bestPrisms;
    int32_t  bestRank;
};

static_assert(sizeof(TurboLevelSaveInfo) == 16, "TurboLevelSaveInfo must be 16 bytes");

// Node-based container guarantees pointer stability across insertions (LevelSelect caches +0x40).
static std::unordered_map<int32_t, TurboLevelSaveInfo> g_turboScores;
static int32_t g_turboLastPlayedType = 0;
static int32_t g_turboLastPlayedIdx[3] = {0, 0, 0};
static bool    g_turboScoresLoaded = false;

static bool GetTurboScoresPathW(wchar_t* outPath, size_t cap, bool createDirs) {
    if (outPath == nullptr || cap == 0) return false;
    outPath[0] = L'\0';

    wchar_t base[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        while (n > 0 && (base[n - 1] == L'\\' || base[n - 1] == L'/')) {
            base[--n] = L'\0';
        }
        wchar_t dir1[MAX_PATH]{};
        wchar_t dir2[MAX_PATH]{};
        int r1 = std::swprintf(dir1, MAX_PATH, L"%ls\\Two Tribes", base);
        int r2 = std::swprintf(dir2, MAX_PATH, L"%ls\\Two Tribes\\EDGE", base);
        int r3 = std::swprintf(outPath, cap, L"%ls\\Two Tribes\\EDGE\\edgemod_turbo_scores.ini", base);
        if (r1 > 0 && r2 > 0 && r3 > 0 && static_cast<size_t>(r3) < cap) {
            if (createDirs) {
                CreateDirectoryW(dir1, nullptr);
                CreateDirectoryW(dir2, nullptr);
            }
            return true;
        }
    }

    DWORD exeLen = GetModuleFileNameW(nullptr, base, MAX_PATH);
    if (exeLen > 0 && exeLen < MAX_PATH) {
        wchar_t* slash = std::wcsrchr(base, L'\\');
        wchar_t* fslash = std::wcsrchr(base, L'/');
        if (fslash != nullptr && (slash == nullptr || fslash > slash)) slash = fslash;
        if (slash != nullptr) {
            *slash = L'\0';
            int r = std::swprintf(outPath, cap, L"%ls\\edgemod_turbo_scores.ini", base);
            if (r > 0 && static_cast<size_t>(r) < cap) return true;
        }
    }
    outPath[0] = L'\0';
    return false;
}

static bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void TrimSpan(const char*& p, const char*& end) {
    while (p < end && IsSpace(*p)) ++p;
    while (end > p && IsSpace(end[-1])) --end;
}

static bool EqIc(const char* p, const char* end, const char* lit) {
    size_t i = 0;
    while (p < end) {
        char c = *p++;
        if (c >= 'A' && c <= 'Z') c |= 0x20;
        if (lit[i] == '\0' || c != lit[i]) return false;
        ++i;
    }
    return lit[i] == '\0';
}

static bool ParseInt32Span(const char* p, const char* end, int32_t* out) {
    TrimSpan(p, end);
    if (p >= end) return false;
    bool neg = false;
    if (*p == '-') {
        neg = true;
        ++p;
    } else if (*p == '+') {
        ++p;
    }
    if (p >= end || *p < '0' || *p > '9') return false;
    int64_t val = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        if (val > 0x7fffffffLL) return false;
        ++p;
    }
    if (p != end) return false;
    *out = static_cast<int32_t>(neg ? -val : val);
    return true;
}

static bool ParseUint32Span(const char* p, const char* end, uint32_t* out) {
    TrimSpan(p, end);
    if (p >= end || *p < '0' || *p > '9') return false;
    uint64_t val = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        val = val * 10ull + static_cast<uint64_t>(*p - '0');
        if (val > 0xffffffffULL) return false;
        ++p;
    }
    if (p != end) return false;
    *out = static_cast<uint32_t>(val);
    return true;
}

static bool ParseCsvScoreLine(const char* p, const char* end, TurboLevelSaveInfo* out) {
    const char* c1 = static_cast<const char*>(std::memchr(p, ',', static_cast<size_t>(end - p)));
    if (c1 == nullptr) return false;
    const char* c2 = static_cast<const char*>(std::memchr(c1 + 1, ',', static_cast<size_t>(end - (c1 + 1))));
    if (c2 == nullptr) return false;
    const char* c3 = static_cast<const char*>(std::memchr(c2 + 1, ',', static_cast<size_t>(end - (c2 + 1))));
    if (c3 == nullptr) return false;

    int32_t comp = 0;
    uint32_t ticks = 0;
    int32_t prisms = 0;
    int32_t rank = 0;
    if (!ParseInt32Span(p, c1, &comp)) return false;
    if (!ParseUint32Span(c1 + 1, c2, &ticks)) return false;
    if (!ParseInt32Span(c2 + 1, c3, &prisms)) return false;
    if (!ParseInt32Span(c3 + 1, end, &rank)) return false;

    TurboLevelSaveInfo info{};
    info.completed     = (comp != 0) ? 1u : 0u;
    info.bestTimeTicks = ticks;
    info.bestPrisms    = (prisms >= 0) ? prisms : 0;
    info.bestRank      = std::clamp(rank, 0, 5);
    *out = info;
    return true;
}

// ------------------------------------------------------------------
// Android Turbo Mode Cube Physics (EDGE-v4.0.1-full.apk libcgame.so)
// ------------------------------------------------------------------

static bool IsPlayerTurboActive(const uint8_t* cube) {
    return (g_turboMode != 0) &&
           (cube != nullptr) &&
           (*reinterpret_cast<const int32_t*>(cube + 0x0) == 0) &&
           !IsGraphicsPreviewActive();
}

static void __cdecl CubeApplySpeedUpdateSpin_Impl(uint8_t* cube) {
    if (cube == nullptr) return;

    int32_t curDir = *reinterpret_cast<const int32_t*>(cube + 0x18c);
    int32_t prevDir = *reinterpret_cast<const int32_t*>(cube + 0x184);
    int32_t& vel = *reinterpret_cast<int32_t*>(cube + 0x144);
    if (curDir != prevDir) {
        if (*reinterpret_cast<const int32_t*>(cube + 0x150) == 0) {
            vel = (vel * 80) / 100;
        }
        return;
    }

    int32_t cubeIdx = *reinterpret_cast<const int32_t*>(cube + 0x0);
    bool isTurbo = IsPlayerTurboActive(cube);

    int32_t maxHi  = isTurbo ? 0xc000 : *reinterpret_cast<const int32_t*>(0x5f3ca0);
    int32_t maxLo  = isTurbo ? 0xc000 : *reinterpret_cast<const int32_t*>(0x5f3ca4);
    int32_t stepHi = isTurbo ? 0x0800 : *reinterpret_cast<const int32_t*>(0x5f3ca8);
    int32_t stepLo = isTurbo ? 0x0400 : *reinterpret_cast<const int32_t*>(0x5f3cac);

    int32_t& accel = *reinterpret_cast<int32_t*>(cube + 0x148);
    if (cubeIdx == 1) {
        accel += stepLo;
    } else if (*reinterpret_cast<const int32_t*>(cube + 0x138) == 1) {
        int32_t rem = 22 - *reinterpret_cast<const int32_t*>(cube + 0x160);
        if (rem < 0) rem = 0;
        int32_t maxAccel = maxLo + ((maxHi - maxLo) * rem) / 22;
        if (*reinterpret_cast<const int32_t*>(cube + 0x4) == 1) {
            int32_t step = stepLo + ((stepHi - stepLo) * rem) / 22;
            uint8_t* sm = GetScreenMgr();
            float mag = (sm != nullptr) ? *reinterpret_cast<const float*>(sm + 0xd4) : 1.0f;
            accel += static_cast<int32_t>(static_cast<double>(step) * static_cast<double>(mag));
        }
        if (accel > maxAccel) accel = maxAccel;
    } else {
        accel = maxHi * 2;
    }

    int32_t cap = isTurbo ? 0x1e000 : 0xf000;
    if (*reinterpret_cast<const int32_t*>(cube + 0x190) == *reinterpret_cast<const int32_t*>(cube + 0x188)) {
        if (vel + accel < cap) vel += accel;
    } else {
        if (vel - accel > -cap) vel -= accel;
    }
}

static void __declspec(naked) hk_cubeApplySpeedUpdateSpin() {
    __asm {
        push ecx
        call CubeApplySpeedUpdateSpin_Impl
        add esp, 4
        ret
    }
}

static int32_t __cdecl CubeShouldSkipCheckFall_Impl(const uint8_t* cube) {
    if (!IsPlayerTurboActive(cube)) return 0;
    return (*reinterpret_cast<const int32_t*>(cube + 0x150) > 0x2710) ? 1 : 0;
}

static void __declspec(naked) hk_cubeCheckFall() {
    // At 0x48c4e7: esi = cube, before `cmp WORD PTR [esi+0x14], 0; push ebx; push edi`
    __asm {
        push eax
        push ecx
        push edx
        push esi
        call CubeShouldSkipCheckFall_Impl
        add esp, 4
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jne skip_fall
        cmp WORD PTR [esi + 0x14], 0
        push ebx
        push edi
        push 0x48c4ee
        ret
    skip_fall:
        push 0x48c623
        ret
    }
}

static int32_t __cdecl CubeGetForwardThreshold_Impl(const uint8_t* cube) {
    return IsPlayerTurboActive(cube) ? 0x1e000 : 0xf000;
}

static void __declspec(naked) hk_cubeApplySpeedFwd() {
    // At 0x48fe9d: esi = cube, replaces 14B up to 0x48feab / 0x48fecd
    __asm {
        push ecx
        push edx
        push esi
        call CubeGetForwardThreshold_Impl
        add esp, 4
        pop edx
        pop ecx
        mov ebx, DWORD PTR [esi + 0x144]
        cmp ebx, eax
        jle fwd_le
        push 0x48feab
        ret
    fwd_le:
        push 0x48fecd
        ret
    }
}

// ------------------------------------------------------------------
// Turbo Save / History Routing
// ------------------------------------------------------------------

static TurboLevelSaveInfo* __cdecl GetTurboLevelSaveInfo_Impl(int32_t levelId) {
    EnsureTurboScoresLoaded();
    return &g_turboScores[levelId];
}

static int32_t __cdecl GetTurboLastPlayedIdx_Impl(uint32_t mode) {
    EnsureTurboScoresLoaded();
    return (mode <= 2u) ? g_turboLastPlayedIdx[mode] : 0;
}

static void __cdecl SetTurboLastPlayedIdx_Impl(uint32_t mode, int32_t idx) {
    EnsureTurboScoresLoaded();
    if (mode <= 2u) {
        g_turboLastPlayedIdx[mode] = (idx >= 0) ? idx : 0;
    }
}

static int32_t __cdecl GetTurboLastPlayedType_Impl() {
    EnsureTurboScoresLoaded();
    return std::clamp(g_turboLastPlayedType, 0, 2);
}

static void __cdecl OnTurboLevelLoad_Impl(uint32_t mode, int32_t levelIdx) {
    EnsureTurboScoresLoaded();
    if (IsGraphicsPreviewActive()) return;
    if (mode <= 2u) {
        g_turboLastPlayedType = static_cast<int32_t>(mode);
        g_turboLastPlayedIdx[mode] = (levelIdx >= 0) ? levelIdx : 0;
        SaveTurboScores();
    }
}

static void __declspec(naked) hk_userSettingsGetLevelSaveMut() {
    __asm {
        cmp BYTE PTR g_turboMode, 0
        jne turbo_mut
        push ebp
        mov ebp, esp
        push ecx
        push esi
        mov esi, ecx
        push 0x4920a7
        ret
    turbo_mut:
        mov eax, DWORD PTR [esp + 4]
        push eax
        call GetTurboLevelSaveInfo_Impl
        add esp, 4
        ret 4
    }
}

static void __declspec(naked) hk_userSettingsGetLevelSaveConst() {
    __asm {
        cmp BYTE PTR g_turboMode, 0
        jne turbo_const
        push ebp
        mov ebp, esp
        push ecx
        push esi
        mov esi, ecx
        push 0x4920e7
        ret
    turbo_const:
        mov eax, DWORD PTR [esp + 4]
        push eax
        call GetTurboLevelSaveInfo_Impl
        add esp, 4
        ret 4
    }
}

static void __declspec(naked) hk_userSettingsGetLastPlayedIdx() {
    __asm {
        cmp BYTE PTR g_turboMode, 0
        jne turbo_get_idx
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 8]
        push 0x492066
        ret
    turbo_get_idx:
        mov eax, DWORD PTR [esp + 4]
        push eax
        call GetTurboLastPlayedIdx_Impl
        add esp, 4
        ret 4
    }
}

static void __declspec(naked) hk_userSettingsSetLastPlayedIdx() {
    __asm {
        cmp BYTE PTR g_turboMode, 0
        jne turbo_set_idx
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 8]
        push 0x492086
        ret
    turbo_set_idx:
        mov edx, DWORD PTR [esp + 8]
        mov eax, DWORD PTR [esp + 4]
        push edx
        push eax
        call SetTurboLastPlayedIdx_Impl
        add esp, 8
        ret 8
    }
}

static void __declspec(naked) hk_modeSelectGetLastPlayedType() {
    // At 0x4b8c37: eax = ScreenMgr*, replaces `mov edi, DWORD PTR [eax + 0x2c4]` (6B)
    __asm {
        cmp BYTE PTR g_turboMode, 0
        jne turbo_ms_type
        mov edi, DWORD PTR [eax + 0x2c4]
        push 0x4b8c3d
        ret
    turbo_ms_type:
        push eax
        push ecx
        push edx
        call GetTurboLastPlayedType_Impl
        mov edi, eax
        pop edx
        pop ecx
        pop eax
        push 0x4b8c3d
        ret
    }
}

static void __declspec(naked) hk_progressOnLevelLoadTail() {
    // At 0x4947b6: stack top has [ebx=levelIdx, edi=mode] already pushed for 0x492080 (ret 8), ecx = esi+0x298
    __asm {
        cmp BYTE PTR g_turboMode, 0
        jne turbo_on_load
        mov DWORD PTR [esi + 0x2c4], edi
        mov eax, 0x492080
        call eax
        lea ecx, [esi + 0x298]
        mov eax, 0x4927d0
        call eax
        push 0x4947cc
        ret
    turbo_on_load:
        add esp, 8
        push ebx
        push edi
        call OnTurboLevelLoad_Impl
        add esp, 8
        push 0x4947cc
        ret
    }
}

static void __declspec(naked) hk_recordCompletionSave() {
    // At 0x49522d: replaces `call 0x4927d0` (__thiscall, 0 stack args)
    __asm {
        cmp BYTE PTR g_turboMode, 0
        jne turbo_rec_save
        mov eax, 0x4927d0
        jmp eax
    turbo_rec_save:
        call SaveTurboScores
        ret
    }
}

// ------------------------------------------------------------------
// Loading Screen "turbo" Indicator (Android RenderInitLevel parity)
// ------------------------------------------------------------------

static inline void SetFontColorRgbAlpha(uint32_t rgb, uint8_t alpha) {
    uint8_t rgba[4] = {
        static_cast<uint8_t>((rgb >> 16) & 0xffu),
        static_cast<uint8_t>((rgb >> 8) & 0xffu),
        static_cast<uint8_t>(rgb & 0xffu),
        alpha
    };
    std::memcpy(reinterpret_cast<void*>(0x5f4268), rgba, 4);
}

static int __cdecl LevelLoadDrawNameAndTurbo_Impl(
    const uint8_t* menu, int32_t frame,
    const EdgeWString* nameWs, int32_t x, int32_t y,
    int32_t scaleX, int32_t scaleY, int32_t extraX, int32_t extraY, int32_t alignFlags
) {
    int32_t fadePct = CalcLevelLoadNameFadePct(menu);
    uint32_t nameCol = Game_ColorLerp(0x000000u, 0xffffffu, fadePct);
    SetFontColorRgbAlpha(nameCol, 0xffu);
    int ret = Game_DrawText(nameWs, x, y, scaleX, scaleY, extraX, extraY, alignFlags);

    if (g_turboMode != 0 && fadePct > 0) {
        uint32_t rb = GetSmoothMenuRainbow(frame);
        uint32_t turboCol = Game_ColorLerp(0x000000u, rb, fadePct);
        SetFontColorRgbAlpha(turboCol, 0xffu);
        EDGE_STATIC_WSTR(kTurboWs, "turbo");
        Game_DrawText(&kTurboWs, x, y + 30, scaleX, scaleY, extraX, extraY, alignFlags);
    }
    return ret;
}

static void __declspec(naked) hk_levelLoadDrawNameAndTurbo() {
    // At 0x4ae998: replaces __cdecl `call 0x4a6c80`; caller cleans `add esp, 0x2c` at 0x4ae9a0.
    // Must preserve ebx (slideY), esi (menu this), edi (0), ebp.
    __asm {
        lea eax, [esp + 4]
        push DWORD PTR [eax + 0x1c] // alignFlags
        push DWORD PTR [eax + 0x18] // extraY
        push DWORD PTR [eax + 0x14] // extraX
        push DWORD PTR [eax + 0x10] // scaleY
        push DWORD PTR [eax + 0x0c] // scaleX
        push DWORD PTR [eax + 0x08] // y
        push DWORD PTR [eax + 0x04] // x
        push DWORD PTR [eax + 0x00] // nameWs
        push DWORD PTR [ebp - 0x44] // frame
        push esi                    // LevelLoadMenu* this
        call LevelLoadDrawNameAndTurbo_Impl
        add esp, 40
        ret
    }
}

} // namespace

void __cdecl EnsureTurboScoresLoaded() {
    if (g_turboScoresLoaded) return;
    g_turboScoresLoaded = true;

    g_turboLastPlayedType = 0;
    g_turboLastPlayedIdx[0] = 0;
    g_turboLastPlayedIdx[1] = 0;
    g_turboLastPlayedIdx[2] = 0;

    wchar_t iniPath[MAX_PATH]{};
    if (!GetTurboScoresPathW(iniPath, MAX_PATH, false)) return;

    HANDLE hFile = CreateFileW(
        iniPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < 0 || fileSize.QuadPart > 1048576) {
        CloseHandle(hFile);
        return;
    }

    std::string data(static_cast<size_t>(fileSize.QuadPart), '\0');
    size_t totalRead = 0;
    while (totalRead < data.size()) {
        DWORD toRead = static_cast<DWORD>((std::min)(data.size() - totalRead, static_cast<size_t>(65536)));
        DWORD got = 0;
        if (!ReadFile(hFile, &data[totalRead], toRead, &got, nullptr) || got == 0) break;
        totalRead += got;
    }
    CloseHandle(hFile);
    data.resize(totalRead);

    size_t pos = 0;
    if (data.size() >= 3 &&
        static_cast<uint8_t>(data[0]) == 0xEF &&
        static_cast<uint8_t>(data[1]) == 0xBB &&
        static_cast<uint8_t>(data[2]) == 0xBF) {
        pos = 3;
    }

    enum Section { SEC_NONE = 0, SEC_META, SEC_LEVELS };
    Section sec = SEC_NONE;

    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        size_t lineEnd = (nl == std::string::npos) ? data.size() : nl;
        const char* p = data.data() + pos;
        const char* end = data.data() + lineEnd;
        pos = (nl == std::string::npos) ? data.size() : (nl + 1);

        TrimSpan(p, end);
        if (p >= end || *p == ';' || *p == '#') continue;

        if (*p == '[') {
            const char* close = static_cast<const char*>(std::memchr(p + 1, ']', static_cast<size_t>(end - (p + 1))));
            if (close != nullptr) {
                const char* secP = p + 1;
                const char* secE = close;
                TrimSpan(secP, secE);
                if (EqIc(secP, secE, "meta")) sec = SEC_META;
                else if (EqIc(secP, secE, "levels")) sec = SEC_LEVELS;
                else sec = SEC_NONE;
            }
            continue;
        }

        const char* eq = static_cast<const char*>(std::memchr(p, '=', static_cast<size_t>(end - p)));
        if (eq == nullptr || sec == SEC_NONE) continue;

        const char* kP = p;
        const char* kE = eq;
        const char* vP = eq + 1;
        const char* vE = end;
        TrimSpan(kP, kE);
        TrimSpan(vP, vE);

        if (sec == SEC_META) {
            int32_t iv = 0;
            if (!ParseInt32Span(vP, vE, &iv)) continue;
            if (EqIc(kP, kE, "last_type")) g_turboLastPlayedType = std::clamp(iv, 0, 2);
            else if (EqIc(kP, kE, "last_idx_0")) g_turboLastPlayedIdx[0] = (iv >= 0) ? iv : 0;
            else if (EqIc(kP, kE, "last_idx_1")) g_turboLastPlayedIdx[1] = (iv >= 0) ? iv : 0;
            else if (EqIc(kP, kE, "last_idx_2")) g_turboLastPlayedIdx[2] = (iv >= 0) ? iv : 0;
        } else if (sec == SEC_LEVELS) {
            int32_t levelId = 0;
            TurboLevelSaveInfo info{};
            if (ParseInt32Span(kP, kE, &levelId) && ParseCsvScoreLine(vP, vE, &info)) {
                g_turboScores[levelId] = info;
            }
        }
    }
}

void __cdecl SaveTurboScores() {
    EnsureTurboScoresLoaded();

    wchar_t iniPath[MAX_PATH + 16]{};
    if (!GetTurboScoresPathW(iniPath, MAX_PATH, true)) return;

    std::string out;
    out.reserve(1024);
    out += "; EDGE PC Mod Turbo Mode score history (auto-managed)\r\n";
    out += "[meta]\r\n";
    char buf[128]{};
    std::snprintf(
        buf, sizeof(buf),
        "schema_version = 1\r\nlast_type = %d\r\nlast_idx_0 = %d\r\nlast_idx_1 = %d\r\nlast_idx_2 = %d\r\n\r\n[levels]\r\n",
        std::clamp(g_turboLastPlayedType, 0, 2),
        g_turboLastPlayedIdx[0],
        g_turboLastPlayedIdx[1],
        g_turboLastPlayedIdx[2]
    );
    out += buf;

    std::vector<int32_t> ids;
    ids.reserve(g_turboScores.size());
    for (const auto& kv : g_turboScores) {
        const TurboLevelSaveInfo& v = kv.second;
        if (v.completed != 0 || v.bestTimeTicks != 0 || v.bestPrisms != 0 || v.bestRank != 0) {
            ids.push_back(kv.first);
        }
    }
    std::sort(ids.begin(), ids.end());

    for (int32_t id : ids) {
        const TurboLevelSaveInfo& v = g_turboScores[id];
        std::snprintf(
            buf, sizeof(buf), "%d = %u,%u,%d,%d\r\n",
            id, static_cast<unsigned>(v.completed ? 1u : 0u),
            v.bestTimeTicks, v.bestPrisms, v.bestRank
        );
        out += buf;
    }

    wchar_t tmpPath[MAX_PATH + 16]{};
    if (std::swprintf(tmpPath, MAX_PATH + 16, L"%ls.tmp", iniPath) <= 0) return;

    HANDLE hFile = CreateFileW(
        tmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) return;

    const char* ptr = out.data();
    size_t remaining = out.size();
    bool writeOk = true;
    while (remaining > 0) {
        DWORD chunk = static_cast<DWORD>((std::min)(remaining, static_cast<size_t>(65536)));
        DWORD written = 0;
        if (!WriteFile(hFile, ptr, chunk, &written, nullptr) || written != chunk) {
            writeOk = false;
            break;
        }
        ptr += written;
        remaining -= written;
    }
    if (writeOk) {
        FlushFileBuffers(hFile);
    }
    CloseHandle(hFile);

    if (!writeOk) {
        DeleteFileW(tmpPath);
        return;
    }

    bool moved = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (MoveFileExW(tmpPath, iniPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            moved = true;
            break;
        }
        Sleep(5);
    }
    if (!moved) {
        DeleteFileW(tmpPath);
    }
}

void __cdecl ResetTurboScores() {
    EnsureTurboScoresLoaded();
    // Zero entries in-place instead of .clear() so cached LevelSaveInfo* pointers never dangle.
    for (auto& kv : g_turboScores) {
        kv.second = TurboLevelSaveInfo{};
    }
    g_turboLastPlayedType = 0;
    g_turboLastPlayedIdx[0] = 0;
    g_turboLastPlayedIdx[1] = 0;
    g_turboLastPlayedIdx[2] = 0;
    SaveTurboScores();
}

int32_t __cdecl GetEffectiveLastPlayedType() {
    if (g_turboMode != 0) {
        EnsureTurboScoresLoaded();
        return std::clamp(g_turboLastPlayedType, 0, 2);
    }
    uint8_t* sm = GetScreenMgr();
    return sm ? std::clamp(*reinterpret_cast<const int32_t*>(sm + 0x2c4), 0, 2) : 0;
}

} // namespace edgemod

void RegisterTurboMode() {
    using namespace edgemod;

    // 1. Cube::ApplySpeedUpdateSpin (0x48c350): Android Turbo accel table & ±0x1e000 roll velocity cap
    static const uint8_t exSpin[9] = {0x55, 0x8b, 0xec, 0x8b, 0x81, 0x8c, 0x01, 0x00, 0x00};
    RegRelJmp(0x48c350, exSpin, sizeof(exSpin), sizeof(exSpin), &hk_cubeApplySpeedUpdateSpin, 0x90);

    // 2. Cube::CheckFall (0x48c4e7): Skip fall check when Turbo bumper impulse > 0x2710
    static const uint8_t exCheckFall[7] = {0x66, 0x83, 0x7e, 0x14, 0x00, 0x53, 0x57};
    RegRelJmp(0x48c4e7, exCheckFall, sizeof(exCheckFall), sizeof(exCheckFall), &hk_cubeCheckFall, 0x90);

    // 3. Cube::ApplySpeedForward (0x48fe9d): Turbo over-speed carry threshold 0x1e000
    static const uint8_t exFwd[14] = {0x8b, 0x9e, 0x44, 0x01, 0x00, 0x00, 0x81, 0xfb, 0x00, 0xf0, 0x00, 0x00, 0x7e, 0x22};
    RegRelJmp(0x48fe9d, exFwd, sizeof(exFwd), sizeof(exFwd), &hk_cubeApplySpeedFwd, 0x90);

    // 4. UserSettings::getLevelSaveInfoMutable (0x4920a0): Route to Turbo score map when enabled
    static const uint8_t exGetMut[7] = {0x55, 0x8b, 0xec, 0x51, 0x56, 0x8b, 0xf1};
    RegRelJmp(0x4920a0, exGetMut, sizeof(exGetMut), sizeof(exGetMut), &hk_userSettingsGetLevelSaveMut, 0x90);

    // 5. UserSettings::getLevelSaveInfo (0x4920e0): Route to Turbo score map when enabled
    static const uint8_t exGetConst[7] = {0x55, 0x8b, 0xec, 0x51, 0x56, 0x8b, 0xf1};
    RegRelJmp(0x4920e0, exGetConst, sizeof(exGetConst), sizeof(exGetConst), &hk_userSettingsGetLevelSaveConst, 0x90);

    // 6. UserSettings::getLastPlayedLevelIndex (0x492060)
    static const uint8_t exGetLastIdx[6] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08};
    RegRelJmp(0x492060, exGetLastIdx, sizeof(exGetLastIdx), sizeof(exGetLastIdx), &hk_userSettingsGetLastPlayedIdx, 0x90);

    // 7. UserSettings::setLastPlayedLevelIndex (0x492080)
    static const uint8_t exSetLastIdx[6] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08};
    RegRelJmp(0x492080, exSetLastIdx, sizeof(exSetLastIdx), sizeof(exSetLastIdx), &hk_userSettingsSetLastPlayedIdx, 0x90);

    // 8. ModeSelect::activate lastPlayedLevelType (0x4b8c37)
    static const uint8_t exMsType[6] = {0x8b, 0xb8, 0xc4, 0x02, 0x00, 0x00};
    RegRelJmp(0x4b8c37, exMsType, sizeof(exMsType), sizeof(exMsType), &hk_modeSelectGetLastPlayedType, 0x90);

    // 9. ProgressMgr::onLevelLoad tail (0x4947b6): Keep Normal lastPlayed & ttsav untouched in Turbo Mode
    static const uint8_t exOnLoad[22] = {
        0x89, 0xbe, 0xc4, 0x02, 0x00, 0x00,
        0xe8, 0xbf, 0xd8, 0xff, 0xff,
        0x8d, 0x8e, 0x98, 0x02, 0x00, 0x00,
        0xe8, 0x04, 0xe0, 0xff, 0xff
    };
    RegRelJmp(0x4947b6, exOnLoad, sizeof(exOnLoad), sizeof(exOnLoad), &hk_progressOnLevelLoadTail, 0x90);

    // 10. ProgressMgr::recordLevelCompletion save (0x49522d): Save Turbo scores instead of ttsav
    static const uint8_t exRecSave[5] = {0xe8, 0x9e, 0xd5, 0xff, 0xff};
    RegRelCall(0x49522d, exRecSave, sizeof(exRecSave), sizeof(exRecSave), &hk_recordCompletionSave);

    // 11. LevelLoadMenu::render level name draw (0x4ae998): Smooth fade + Android rainbow "turbo" below level name
    static const uint8_t exLevelLoadName[5] = {0xe8, 0xe3, 0x82, 0xff, 0xff};
    RegRelCall(0x4ae998, exLevelLoadName, sizeof(exLevelLoadName), sizeof(exLevelLoadName), &hk_levelLoadDrawNameAndTurbo);
}
