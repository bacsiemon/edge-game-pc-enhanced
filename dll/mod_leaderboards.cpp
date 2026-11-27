#include "mod_state.h"
#include <cstring>
#include <string>

namespace edgemod {

namespace {

struct OfficialLeaderboardEntry {
    int32_t     levelId;
    int32_t     leaderboardId;
    const char* filename;
    const char* steamName;
};

// Fixed Steam AppID 38740 leaderboard mapping:
// 48 Standard + first 16 Bonus + 44 Vanilla Extended = 108 entries.
// Excludes EDGE Epic levels, hidden bonus levels, and custom mod levels.
static const OfficialLeaderboardEntry kOfficialLeaderboards[] = {
    // Standard (48)
    {309, 44875, "level309", "level 01 - 1st contact"},
    {300, 44881, "level300", "level 02 - training"},
    {310, 44878, "level310", "level 03 - playground"},
    {36,  44906, "level36",  "level 04 - pushing stars"},
    {201, 44907, "level201", "level 05 - bump"},
    {37,  44908, "level37",  "level 06 - city rythm"},
    {311, 44909, "level311", "level 07 - speedrun"},
    {38,  44877, "level38",  "level 08 - milky way"},
    {59,  44910, "level59",  "level 09 - 8-bit"},
    {301, 44911, "level301", "level 10 - metro"},
    {40,  44912, "level40",  "level 11 - mini me"},
    {43,  44913, "level43",  "level 12 - vertex"},
    {206, 44914, "level206", "level 13 - equalizer"},
    {312, 44879, "level312", "level 14 - peripherique"},
    {44,  44880, "level44",  "level 15 - time machine"},
    {42,  44915, "level42",  "level 16 - mind the gap"},
    {315, 44916, "level315", "level 17 - edge code"},
    {45,  44917, "level45",  "level 18 - edge time"},
    {46,  44918, "level46",  "level 19 - chase"},
    {60,  44919, "level60",  "level 20 - landing"},
    {307, 44920, "level307", "level 21 - chess"},
    {48,  44921, "level48",  "level 22 - switch keep"},
    {202, 44922, "level202", "level 23 - mecanic"},
    {305, 44923, "level305", "level 24 - higher"},
    {41,  44924, "level41",  "level 25 - squadron"},
    {303, 44925, "level303", "level 26 - metronome"},
    {49,  44926, "level49",  "level 27 - orion"},
    {302, 44927, "level302", "level 28 - try again"},
    {204, 44928, "level204", "level 29 - hypnozone"},
    {304, 44929, "level304", "level 30 - beat"},
    {52,  44930, "level52",  "level 31 - star castle"},
    {308, 44931, "level308", "level 32 - sticker"},
    {50,  44932, "level50",  "level 33 - sync the wall"},
    {306, 44933, "level306", "level 34 - snap"},
    {51,  44934, "level51",  "level 35 - braintonic"},
    {313, 44935, "level313", "level 36 - 2nd contact"},
    {47,  44936, "level47",  "level 37 - jungle fever"},
    {314, 44937, "level314", "level 38 - speedrun 2"},
    {53,  44938, "level53",  "level 39 - edge master"},
    {54,  44939, "level54",  "level 40 - cube invaders"},
    {317, 44940, "level317", "level 41 - starfield"},
    {319, 44941, "level319", "level 42 - bonus"},
    {318, 44942, "level318", "level 43 - extra cube"},
    {222, 44943, "level222", "level 44 - sliced"},
    {221, 44944, "level221", "level 45 - earthquake"},
    {220, 44945, "level220", "level 46 - vertigo"},
    {400, 44946, "level400", "level 47 - push me"},
    {401, 44947, "level401", "level 48 - perfect cell"},

    // Bonus (first 16)
    {815, 46329, "hangout_815",    "bonus 01 - hangout"},
    {810, 44957, "hammer_810",     "bonus 02 - hammer"},
    {805, 44952, "compost_805",    "bonus 03 - compost"},
    {817, 46331, "babylonian_817", "bonus 04 - babylonian"},
    {801, 44948, "swirl_801",      "bonus 05 - swirl"},
    {806, 44953, "density_806",    "bonus 06 - density"},
    {811, 46332, "magic_811",      "bonus 07 - magic"},
    {808, 44955, "cubism_808",     "bonus 08 - cubism"},
    {813, 45472, "mystic_813",     "bonus 09 - mystic"},
    {804, 44951, "indiana_804",    "bonus 10 - indiana"},
    {816, 47383, "chunk_816",      "bonus 11 - chunk"},
    {812, 47385, "goliath_812",    "bonus 12 - goliath"},
    {818, 47387, "fourohfour_818", "bonus 13 - 404"},
    {802, 47389, "gears_802",      "bonus 14 - gears"},
    {800, 47391, "fireworks_800",  "bonus 15 - fireworks"},
    {820, 53400, "winners_820",    "bonus 16 - winners"},

    // Extended (44 vanilla)
    {537, 57829, "level537",    "extended 01 - first step"},
    {536, 57831, "level536",    "extended 02 - climbing"},
    {538, 57833, "level538",    "extended 03 - click"},
    {535, 57835, "level535_3D", "extended 04 - moving walls"},
    {539, 57837, "level539",    "extended 05 - black robot"},
    {541, 57839, "level541",    "extended 06 - darkcube"},
    {500, 57841, "level500",    "extended 07 - roots"},
    {548, 57843, "level548",    "extended 08 - flick"},
    {540, 57845, "level540",    "extended 09 - lying bridge"},
    {511, 57847, "level511",    "extended 10 - crateria"},
    {512, 57849, "level512",    "extended 11 - stratosphere"},
    {519, 57851, "level519",    "extended 12 - tiny road"},
    {510, 57853, "level510",    "extended 13 - cloudy way"},
    {526, 57855, "level526",    "extended 14 - walled up race"},
    {508, 57857, "level508",    "extended 15 - sugar ground"},
    {546, 57859, "level546",    "extended 16 - dark edge time"},
    {507, 57861, "level507",    "extended 17 - trench"},
    {524, 57863, "level524",    "extended 18 - paper wall"},
    {516, 57865, "level516",    "extended 19 - claustrophobia"},
    {553, 57867, "level553",    "extended 20 - electric way"},
    {509, 57869, "level509",    "extended 21 - impulsed maze"},
    {523, 57871, "level523",    "extended 22 - far"},
    {545, 57873, "level545",    "extended 23 - deadly race"},
    {522, 57875, "level522",    "extended 24 - don't click!"},
    {518, 57877, "level518",    "extended 25 - white spring"},
    {533, 57879, "level533",    "extended 26 - hurry"},
    {528, 57881, "level528",    "extended 27 - blind"},
    {542, 57883, "level542",    "extended 28 - wire"},
    {521, 57885, "level521",    "extended 29 - hyperphase"},
    {532, 57887, "level532",    "extended 30 - star factory"},
    {514, 57889, "level514",    "extended 31 - star dust"},
    {531, 57891, "level531",    "extended 32 - destructuring"},
    {520, 57893, "level520",    "extended 33 - black hole"},
    {600, 57895, "level600",    "extended 34 - robot sport"},
    {525, 57897, "level525",    "extended 35 - cubefall"},
    {517, 57899, "level517",    "extended 36 - galaxy rail"},
    {544, 57901, "level544",    "extended 37 - voodoo dance"},
    {530, 57903, "level530",    "extended 38 - highest flag"},
    {515, 57905, "level515",    "extended 39 - geometric snake"},
    {527, 57907, "level527",    "extended 40 - rodeo"},
    {547, 57909, "level547",    "extended 41 - final clash"},
    {534, 57911, "level534",    "extended 42 - pushy"},
    {551, 57913, "level551",    "extended 43 - arksecktor"},
    {550, 57915, "level550",    "extended 44 - timorg"},
};

const OfficialLeaderboardEntry* FindOfficialLeaderboardForCurrentLevel() {
    uint8_t* sm = GetScreenMgr();
    if (sm == nullptr) return nullptr;

    int32_t levelIdx = *reinterpret_cast<const int32_t*>(sm + 0x1c0);
    int32_t mode = *reinterpret_cast<const int32_t*>(sm + 0x1e0);
    if (levelIdx < 0 || mode < 0 || mode > 2) return nullptr;

    using GetLevelEntryFn = const uint8_t*(__thiscall*)(void*, int32_t, int32_t);
    const uint8_t* entry = reinterpret_cast<GetLevelEntryFn>(0x4a4620)(sm + 0x24c, mode, levelIdx);
    if (entry == nullptr) return nullptr;

    int32_t levelId = *reinterpret_cast<const int32_t*>(entry + 0x3c);
    const EdgeString* fnStr = reinterpret_cast<const EdgeString*>(entry + 0x00);
    const char* fn = fnStr->c_str();
    if (fn == nullptr || fn[0] == '\0') return nullptr;

    for (const auto& e : kOfficialLeaderboards) {
        if (e.levelId == levelId && _stricmp(e.filename, fn) == 0) {
            return &e;
        }
    }
    return nullptr;
}

bool IsSteamApiReady() {
    using SteamIfnFn = void*(__cdecl*)(void);
    auto getSteamFriends = *reinterpret_cast<SteamIfnFn const*>(0x577760);
    auto getSteamUser = *reinterpret_cast<SteamIfnFn const*>(0x577764);
    if (getSteamFriends == nullptr || getSteamUser == nullptr) return false;
    return getSteamFriends() != nullptr && getSteamUser() != nullptr;
}

void __cdecl UploadOfficialLeaderboard_Impl(const uint8_t* ws) {
    if (ws == nullptr || g_turboMode != 0) return;
    void* lbMgr = *reinterpret_cast<void* const*>(0x5f9f80);
    uint8_t* sm = GetScreenMgr();
    if (lbMgr == nullptr || sm == nullptr || sm[0x334] == 0) return;

    const OfficialLeaderboardEntry* entry = FindOfficialLeaderboardForCurrentLevel();
    if (entry == nullptr) return;

    int32_t ticks = *reinterpret_cast<const int32_t*>(ws + 0x110);
    int32_t timeMs = static_cast<int32_t>((static_cast<int64_t>(ticks) * 1000LL) / 22LL);
    int32_t rank = *reinterpret_cast<const int32_t*>(ws + 0x114);
    int32_t clampedTime = (timeMs > 0x5f5e0ff) ? 0x5f5e0ff : timeMs;
    int32_t rankScore = (6 - rank) * 0x5f5e100 + clampedTime;

    using AddScoreFn = void(__thiscall*)(void*, const EdgeString*, int32_t);
    auto addScore = reinterpret_cast<AddScoreFn>(0x4ffad0);

    EdgeString lbName{};
    lbName.ctor_cstr(entry->steamName);
    addScore(lbMgr, &lbName, timeMs);
    lbName.dtor();

    std::string rankStr(entry->steamName);
    rankStr += " + rank";
    EdgeString lbRankName{};
    lbRankName.ctor_cstr(rankStr.c_str());
    addScore(lbMgr, &lbRankName, rankScore);
    lbRankName.dtor();
}

void __declspec(naked) hk_uploadOfficialLeaderboard() {
    __asm {
        push esi
        call UploadOfficialLeaderboard_Impl
        add esp, 4
        mov eax, 0x47cb34
        jmp eax
    }
}

int32_t __cdecl DebriefGetButtonStringId_Impl(int32_t slot) {
    switch (slot) {
        case 0: return 0x16;
        case 1: return 0x17;
        case 2: return DebriefHasLeaderboard() ? 0x3c : 0x1b;
        default: return 0x1b;
    }
}

void __declspec(naked) hk_debriefGetButtonStringId() {
    __asm {
        push esi
        call DebriefGetButtonStringId_Impl
        add esp, 4
        ret
    }
}

void __declspec(naked) hk_debriefRenderLoopNext() {
    __asm {
        inc esi
        add edi, 0x21
        call GetDebriefButtonCount
        cmp esi, eax
        jl loop_cont
        mov eax, 0x4abe35
        jmp eax
    loop_cont:
        mov eax, 0x4abcd1
        jmp eax
    }
}

void __declspec(naked) hk_debriefHitTestLoopNext() {
    __asm {
        inc esi
        add eax, 0x21
        push eax
        push edx
        call GetDebriefButtonCount
        mov ecx, eax
        pop edx
        pop eax
        cmp esi, ecx
        jl loop_cont
        mov ecx, 0x4a95d8
        jmp ecx
    loop_cont:
        mov ecx, 0x4a95b0
        jmp ecx
    }
}

void __cdecl DebriefNavUp_Impl(uint8_t* deb) {
    int32_t count = GetDebriefButtonCount();
    int32_t sel = *reinterpret_cast<int32_t*>(deb + 0x10) - 1;
    if (sel < 0 || sel >= count) sel = count - 1;
    *reinterpret_cast<int32_t*>(deb + 0x10) = sel;
}

void __declspec(naked) hk_debriefNavUp() {
    __asm {
        push esi
        call DebriefNavUp_Impl
        add esp, 4
        mov eax, 0x4ac14c
        jmp eax
    }
}

void __cdecl DebriefNavDown_Impl(uint8_t* deb) {
    int32_t count = GetDebriefButtonCount();
    int32_t sel = *reinterpret_cast<int32_t*>(deb + 0x10) + 1;
    if (sel < 0 || sel >= count) sel = 0;
    *reinterpret_cast<int32_t*>(deb + 0x10) = sel;
}

void __declspec(naked) hk_debriefNavDown() {
    __asm {
        push esi
        call DebriefNavDown_Impl
        add esp, 4
        mov eax, 0x4ac14c
        jmp eax
    }
}

int32_t __cdecl DebriefMapActivateAction_Impl(const uint8_t* deb) {
    int32_t sel = *reinterpret_cast<const int32_t*>(deb + 0x10);
    if (!DebriefHasLeaderboard()) {
        if (sel == 2) return 3;
        if (sel >= 3) return 4;
    }
    return sel;
}

void __declspec(naked) hk_debriefActivateMapSlot() {
    __asm {
        push esi
        call DebriefMapActivateAction_Impl
        add esp, 4
        mov ecx, esi
        cmp eax, 3
        ja skip_act
        jmp DWORD PTR [eax * 4 + 0x4ac084]
    skip_act:
        mov eax, 0x4ac05e
        jmp eax
    }
}

int32_t __cdecl GetOfficialLeaderboardIdOrFallback_Impl(const uint8_t* levelMappingEntry) {
    const OfficialLeaderboardEntry* off = FindOfficialLeaderboardForCurrentLevel();
    if (off != nullptr) return off->leaderboardId;
    return *reinterpret_cast<const int32_t*>(levelMappingEntry + 0x38);
}

void __declspec(naked) hk_debriefLoadOfficialLbId() {
    __asm {
        push esi
        call GetOfficialLeaderboardIdOrFallback_Impl
        add esp, 4
        push eax
        lea edx, [ebp - 0x90]
        mov eax, 0x4abfe8
        jmp eax
    }
}

} // namespace

bool __cdecl DebriefHasLeaderboard() {
    if (g_turboMode != 0) return false;
    return FindOfficialLeaderboardForCurrentLevel() != nullptr && IsSteamApiReady();
}

int32_t __cdecl GetDebriefButtonCount() {
    return DebriefHasLeaderboard() ? 4 : 3;
}

} // namespace edgemod

void RegisterLeaderboards() {
    using namespace edgemod;

    // 1. WorldScreen::updatePostWin: map official levels to fixed Steam leaderboard names, skip others
    static const uint8_t exPostWinLb[6] = {0x8b, 0x1d, 0x80, 0x9f, 0x5f, 0x00};
    RegRelJmp(0x47c9a8, exPostWinLb, sizeof(exPostWinLb), 6, &hk_uploadOfficialLeaderboard, 0x90, 0x47cb34);

    // 2. DebriefMenu::render: dynamic button string ID (hide "leaderboard" on slot 2 when unavailable)
    static const uint8_t exDebRenderStr[12] = {0x83, 0xfe, 0x03, 0x77, 0x1c, 0xff, 0x24, 0xb5, 0xf0, 0xbe, 0x4a, 0x00};
    RegRelCall(0x4abda3, exDebRenderStr, sizeof(exDebRenderStr), 0x4abdc9 - 0x4abda3, &hk_debriefGetButtonStringId, 0x90);

    // 3. DebriefMenu::render: dynamic button loop limit (3 vs 4)
    static const uint8_t exDebRenderLoop[13] = {0x46, 0x83, 0xc7, 0x21, 0x83, 0xfe, 0x04, 0x0f, 0x8c, 0x9c, 0xfe, 0xff, 0xff};
    RegRelJmp(0x4abe28, exDebRenderLoop, sizeof(exDebRenderLoop), sizeof(exDebRenderLoop), &hk_debriefRenderLoopNext, 0x90);

    // 4. DebriefMenu::hitTest: dynamic button loop limit (3 vs 4)
    static const uint8_t exDebHitLoop[9] = {0x46, 0x83, 0xc0, 0x21, 0x83, 0xfe, 0x04, 0x7c, 0xd8};
    RegRelJmp(0x4a95cf, exDebHitLoop, sizeof(exDebHitLoop), sizeof(exDebHitLoop), &hk_debriefHitTestLoopNext, 0x90);

    // 5. DebriefMenu::update: dynamic Up navigation wrap
    static const uint8_t exDebUpWrap[14] = {0xff, 0x4e, 0x10, 0x79, 0x50, 0xc7, 0x46, 0x10, 0x03, 0x00, 0x00, 0x00, 0xeb, 0x47};
    RegRelJmp(0x4ac0f7, exDebUpWrap, sizeof(exDebUpWrap), sizeof(exDebUpWrap), &hk_debriefNavUp, 0x90);

    // 6. DebriefMenu::update: dynamic Down navigation wrap
    static const uint8_t exDebDownWrap[18] = {0xff, 0x46, 0x10, 0x83, 0x7e, 0x10, 0x04, 0x7c, 0x2b, 0xc7, 0x46, 0x10, 0x00, 0x00, 0x00, 0x00, 0xeb, 0x22};
    RegRelJmp(0x4ac118, exDebDownWrap, sizeof(exDebDownWrap), sizeof(exDebDownWrap), &hk_debriefNavDown, 0x90);

    // 7. DebriefMenu::activate: map dense slot index (slot 2 -> menu when leaderboard hidden)
    static const uint8_t exDebAct[12] = {0x8b, 0x46, 0x10, 0x83, 0xf8, 0x03, 0x0f, 0x87, 0x3c, 0x01, 0x00, 0x00};
    RegRelJmp(0x4abf16, exDebAct, sizeof(exDebAct), 0x4abf29 - 0x4abf16, &hk_debriefActivateMapSlot, 0x90, 0x4abf29);

    // 8. DebriefMenu::activate: use verified official Steam leaderboard_id for Steam Community URL
    static const uint8_t exDebLbId[10] = {0x8b, 0x4e, 0x38, 0x51, 0x8d, 0x95, 0x70, 0xff, 0xff, 0xff};
    RegRelJmp(0x4abfde, exDebLbId, sizeof(exDebLbId), sizeof(exDebLbId), &hk_debriefLoadOfficialLbId, 0x90);
}
