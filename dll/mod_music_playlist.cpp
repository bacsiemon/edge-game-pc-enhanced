#include "mod_state.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace edgemod;

constexpr int kActiveLevelMusicId = 0x1000;

struct PlaylistPair {
    std::string pc;
    std::string java;
};

struct PlaylistLevelEntry {
    std::string name;
    PlaylistPair tracks;
};

struct CanonicalLevel {
    const char* name;
    uint8_t pcId;
    uint8_t javaId;
};

static const char* const kVanillaMusic[25] = {
    "00_Title",
    "01_Eternity",
    "02_Quiet",
    "03_Pad",
    "04_Jingle",
    "05_Tec",
    "06_Kakkoi",
    "07_Dark",
    "08_Squadron",
    "09_8bits",
    "10_Pixel",
    "11_Jupiter",
    "12_Shame",
    "13_Debrief",
    "14_Space",
    "15_Voyage_geometrique",
    "16_Mzone",
    "17_R2",
    "18_Mystery_cube",
    "19_Duty",
    "20_PerfectCell",
    "21_fun",
    "22_lol",
    "23_lostway",
    "24_wall_street",
};

static const char* const kJavaMusic[12] = {
    "100_menus",
    "101_braintonik",
    "102_cube_dance",
    "103_essai_2",
    "104_essai_01",
    "105_test",
    "106_mysterycube",
    "107_03_EDGE",
    "108_jungle",
    "109_RetardTonic",
    "110_oldschool_simon",
    "111_planant",
};

static const uint8_t kGameplayPcIds[22] = {
    1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12,
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
};

static const CanonicalLevel kCanonicalLevels[] = {
    // Standard (48 + level309_winter)
    {"level309",        6,  105},
    {"level309_winter", 6,  105},
    {"level300",        2,  111},
    {"level310",        12, 111},
    {"level36",         3,  104},
    {"level201",        19, 105},
    {"level37",         5,  101},
    {"level311",        15, 111},
    {"level38",         1,  110},
    {"level59",         6,  111},
    {"level301",        18, 111},
    {"level40",         7,  105},
    {"level43",         8,  108},
    {"level206",        17, 105},
    {"level312",        11, 111},
    {"level44",         9,  109},
    {"level42",         10, 110},
    {"level315",        18, 111},
    {"level45",         2,  103},
    {"level46",         3,  102},
    {"level60",         6,  111},
    {"level307",        19, 111},
    {"level48",         3,  102},
    {"level202",        17, 109},
    {"level305",        15, 111},
    {"level41",         8,  108},
    {"level303",        16, 111},
    {"level49",         2,  101},
    {"level302",        18, 111},
    {"level204",        6,  109},
    {"level304",        5,  111},
    {"level52",         11, 105},
    {"level308",        19, 111},
    {"level50",         10, 103},
    {"level306",        6,  111},
    {"level51",         9,  102},
    {"level313",        18, 105},
    {"level47",         3,  108},
    {"level314",        15, 111},
    {"level53",         1,  111},
    {"level54",         9,  109},
    {"level317",        18, 111},
    {"level319",        6,  111},
    {"level318",        12, 111},
    {"level222",        6,  106},
    {"level221",        11, 108},
    {"level220",        6,  109},
    {"level400",        8,  111},
    {"level401",        20, 111},
    // Hidden Bonus (4)
    {"islands_809",     2,  102},
    {"scientist_803",   18, 101},
    {"switchboard_807", 19, 103},
    {"tide_814",        11, 109},
    // Java Extended (44 + level541_winter)
    {"level537",        5,  110},
    {"level536",        22, 111},
    {"level538",        17, 102},
    {"level535_3D",     24, 101},
    {"level539",        23, 103},
    {"level541",        7,  104},
    {"level541_winter", 7,  104},
    {"level500",        1,  105},
    {"level548",        21, 106},
    {"level540",        18, 107},
    {"level511",        3,  108},
    {"level512",        15, 109},
    {"level519",        12, 110},
    {"level510",        6,  111},
    {"level526",        9,  101},
    {"level508",        10, 102},
    {"level546",        7,  103},
    {"level507",        11, 104},
    {"level524",        1,  105},
    {"level516",        16, 106},
    {"level553",        21, 107},
    {"level509",        22, 108},
    {"level523",        6,  109},
    {"level545",        7,  110},
    {"level522",        1,  111},
    {"level518",        17, 101},
    {"level533",        24, 102},
    {"level528",        16, 103},
    {"level542",        7,  104},
    {"level521",        5,  105},
    {"level532",        23, 106},
    {"level514",        19, 107},
    {"level531",        8,  108},
    {"level520",        18, 109},
    {"level600",        7,  110},
    {"level525",        19, 111},
    {"level517",        22, 101},
    {"level544",        7,  102},
    {"level530",        10, 103},
    {"level515",        11, 104},
    {"level527",        8,  105},
    {"level547",        7,  106},
    {"level534",        3,  107},
    {"level551",        8,  108},
    {"level550",        8,  109},
};

static constexpr const char* kDefaultFallbackSoundFont =
    "D:\\Games\\Mods\\EDGE mod\\java\\ost\\Bada_Mobile_0300_base.sf2";

static const char* const kMusicExts[] = {
    ".ogg", ".flac", ".wav", ".mp3", ".m4a", ".aac", ".wma", ".aif", ".aiff", ".opus", ".mid", ".midi"
};

static bool                                      g_playlistLoaded = false;
static FILETIME                                  g_xmlLastWrite{};
static std::string                               g_xmlSoundFont;
static float                                     g_xmlSoundFontGainDb = 0.0f;
static PlaylistPair                              g_titleTracks   = {"00_Title", "100_menus"};
static PlaylistPair                              g_debriefTracks = {"13_Debrief", "13_Debrief"};
static std::vector<PlaylistLevelEntry>           g_levelEntries;
static std::unordered_map<std::string, size_t>   g_levelIndex;
static std::string                               g_currentLevelName;
static int                                       g_currentLevelFileMusicId = -1;
static std::string                               g_lastPlayedPath;
static std::string                               g_lastPlayedSoundFont;
static uint64_t                                  g_lastPlayedMidiCfgRev = 0;

static std::string ToLowerAscii(const std::string& s) {
    std::string out = s;
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

static std::string TrimAscii(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) {
        --e;
    }
    return s.substr(b, e - b);
}

static bool EndsWithNoCase(const std::string& s, const char* suffix) {
    size_t slen = std::strlen(suffix);
    if (s.size() < slen) return false;
    return _stricmp(s.c_str() + (s.size() - slen), suffix) == 0;
}

static bool StartsWithNoCase(const std::string& s, const char* prefix) {
    size_t plen = std::strlen(prefix);
    if (s.size() < plen) return false;
    return _strnicmp(s.c_str(), prefix, plen) == 0;
}

static const std::string& GetExeDir() {
    static std::string s_dir;
    static bool s_inited = false;
    if (!s_inited) {
        s_inited = true;
        char buf[MAX_PATH] = {};
        DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            char* lastSlash = nullptr;
            for (char* p = buf; *p != '\0'; ++p) {
                if (*p == '\\' || *p == '/') {
                    lastSlash = p;
                }
            }
            if (lastSlash != nullptr) {
                *lastSlash = '\0';
                s_dir = buf;
            }
        }
    }
    return s_dir;
}

static std::string JoinExePath(const char* rel) {
    const std::string& dir = GetExeDir();
    if (dir.empty()) return rel;
    return dir + "\\" + rel;
}

static std::string GetPlaylistXmlPath() {
    return JoinExePath("music\\playlist.xml");
}

static bool FileExistsOnDisk(const std::string& path) {
    if (path.empty()) return false;
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static bool IsAbsoluteTrackPath(const std::string& p) {
    if (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':') return true;
    if (p.size() >= 2 && ((p[0] == '\\' && p[1] == '\\') || (p[0] == '/' && p[1] == '/'))) return true;
    if (!p.empty() && (p[0] == '\\' || p[0] == '/')) return true;
    return false;
}

static bool HasSupportedMusicExt(const std::string& spec, std::string* outExt = nullptr) {
    size_t slash = spec.find_last_of("/\\");
    size_t dot = spec.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return false;
    }
    std::string ext = spec.substr(dot);
    if (!IsSupportedMusicExtension(ext.c_str())) {
        return false;
    }
    if (outExt != nullptr) *outExt = ext;
    return true;
}

static std::string ResolveSoundFontCandidate(const std::string& spec) {
    std::string s = TrimAscii(spec);
    if (s.empty()) return {};
    if (IsAbsoluteTrackPath(s)) {
        return FileExistsOnDisk(s) ? s : std::string{};
    }
    std::string norm = s;
    for (char& ch : norm) {
        if (ch == '/') ch = '\\';
    }
    if (!StartsWithNoCase(norm, "music\\")) {
        std::string inMusic = JoinExePath(("music\\" + norm).c_str());
        if (FileExistsOnDisk(inMusic)) return inMusic;
    }
    std::string inExe = JoinExePath(norm.c_str());
    if (FileExistsOnDisk(inExe)) return inExe;
    return {};
}

static std::string ResolveTrackFilePath(const std::string& spec) {
    std::string s = TrimAscii(spec);
    if (s.empty()) return {};

    if (IsAbsoluteTrackPath(s)) {
        return FileExistsOnDisk(s) ? s : std::string{};
    }

    std::string relSlash = s;
    for (char& ch : relSlash) {
        if (ch == '\\') ch = '/';
    }
    if (StartsWithNoCase(relSlash, "music/")) {
        relSlash = relSlash.substr(6);
    }
    if (relSlash.empty()) return {};

    std::string relBack = relSlash;
    for (char& ch : relBack) {
        if (ch == '/') ch = '\\';
    }

    if (HasSupportedMusicExt(relSlash)) {
        std::string full = JoinExePath(("music\\" + relBack).c_str());
        if (FileExistsOnDisk(full)) {
            return "music/" + relSlash;
        }
        return {};
    }

    for (const char* ext : kMusicExts) {
        std::string candBack = relBack + ext;
        if (FileExistsOnDisk(JoinExePath(("music\\" + candBack).c_str()))) {
            return "music/" + relSlash + ext;
        }
    }
    return {};
}

static std::string TryFindCustomMusicSpec(const char* pattern) {
    WIN32_FIND_DATAA fd{};
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::string bestFile;
    int bestRank = 999;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        std::string fname(fd.cFileName);
        std::string ext;
        if (!HasSupportedMusicExt(fname, &ext)) {
            continue;
        }
        int rank = 999;
        for (int i = 0; i < static_cast<int>(sizeof(kMusicExts) / sizeof(kMusicExts[0])); ++i) {
            if (_stricmp(ext.c_str(), kMusicExts[i]) == 0) {
                rank = i;
                break;
            }
        }
        if (rank < bestRank) {
            bestRank = rank;
            bestFile = fname;
            if (rank == 0) break;
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    if (bestFile.empty()) return {};
    if (EndsWithNoCase(bestFile, ".ogg")) {
        bestFile.resize(bestFile.size() - 4);
    }
    return bestFile;
}

static std::string ResolveIdToStem(int musicId) {
    if (musicId >= 0 && musicId < 25) {
        return kVanillaMusic[musicId];
    }
    if (musicId >= 27 && musicId <= 255) {
        char patPrefix[MAX_PATH + 32] = {};
        char patExact[MAX_PATH + 32] = {};
        const std::string& exeDir = GetExeDir();
        if (!exeDir.empty()) {
            std::snprintf(patPrefix, sizeof(patPrefix), "%s\\music\\%d_*.*", exeDir.c_str(), musicId);
            std::snprintf(patExact, sizeof(patExact), "%s\\music\\%d.*", exeDir.c_str(), musicId);
        } else {
            std::snprintf(patPrefix, sizeof(patPrefix), "music\\%d_*.*", musicId);
            std::snprintf(patExact, sizeof(patExact), "music\\%d.*", musicId);
        }
        std::string found = TryFindCustomMusicSpec(patPrefix);
        if (found.empty()) {
            found = TryFindCustomMusicSpec(patExact);
        }
        if (!found.empty()) {
            return found;
        }
        if (musicId >= 100 && musicId <= 111) {
            return kJavaMusic[musicId - 100];
        }
    }
    return {};
}

static std::string NormalizeTrackSpec(const std::string& raw) {
    std::string s = TrimAscii(raw);
    if (s.empty()) return {};

    bool allDigits = true;
    for (char ch : s) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        int id = std::atoi(s.c_str());
        std::string resolved = ResolveIdToStem(id);
        if (!resolved.empty()) return resolved;
        return {};
    }

    if (IsAbsoluteTrackPath(s)) {
        return s;
    }

    if (StartsWithNoCase(s, "music/") || StartsWithNoCase(s, "music\\")) {
        s = s.substr(6);
    }
    return TrimAscii(s);
}

static int ReadLevelBinMusicId(const std::string& levelName) {
    std::string rel = "levels\\" + levelName + ".bin";
    std::string full = JoinExePath(rel.c_str());
    FILE* f = nullptr;
    if (fopen_s(&f, full.c_str(), "rb") != 0 || f == nullptr) return -1;
    if (std::fseek(f, -1, SEEK_END) != 0) {
        std::fclose(f);
        return -1;
    }
    int ch = std::fgetc(f);
    std::fclose(f);
    return (ch != EOF) ? static_cast<uint8_t>(ch) : -1;
}

static const CanonicalLevel* FindCanonicalLevel(const std::string& levelName) {
    for (const auto& entry : kCanonicalLevels) {
        if (_stricmp(entry.name, levelName.c_str()) == 0) {
            return &entry;
        }
    }
    if (EndsWithNoCase(levelName, "_winter") && levelName.size() > 7) {
        std::string base = levelName.substr(0, levelName.size() - 7);
        for (const auto& entry : kCanonicalLevels) {
            if (_stricmp(entry.name, base.c_str()) == 0) {
                return &entry;
            }
        }
    }
    return nullptr;
}

static uint32_t Fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char ch : s) {
        h ^= static_cast<uint32_t>(std::tolower(ch));
        h *= 16777619u;
    }
    return h;
}

static PlaylistPair AutoGenerateLevelEntry(const std::string& levelName, int knownFileMusicId) {
    int fileId = (knownFileMusicId >= 0) ? knownFileMusicId : ReadLevelBinMusicId(levelName);
    const CanonicalLevel* canon = FindCanonicalLevel(levelName);
    std::mt19937 rng(GetTickCount() ^ Fnv1a(levelName));

    std::string pcStem;
    if (fileId >= 0 && fileId < 25) {
        pcStem = kVanillaMusic[fileId];
    } else if (canon != nullptr) {
        pcStem = kVanillaMusic[canon->pcId];
    } else if (fileId >= 27 && fileId <= 255) {
        pcStem = ResolveIdToStem(fileId);
    }
    if (pcStem.empty()) {
        uint8_t picked = kGameplayPcIds[rng() % (sizeof(kGameplayPcIds) / sizeof(kGameplayPcIds[0]))];
        pcStem = kVanillaMusic[picked];
    }

    std::string javaStem;
    if (canon != nullptr) {
        javaStem = kJavaMusic[canon->javaId - 100];
    } else if (StartsWithNoCase(levelName, "ending_")) {
        javaStem = pcStem;
    } else if (fileId >= 0 && fileId != 25 && fileId != 26 && !pcStem.empty()) {
        javaStem = pcStem;
    } else {
        uint32_t idx = 1u + (rng() % 11u); // 101..111
        javaStem = kJavaMusic[idx];
    }

    return {pcStem, javaStem};
}

static std::string EscapeXmlAttr(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
        case '&':  out += "&amp;"; break;
        case '"':  out += "&quot;"; break;
        case '<':  out += "&lt;"; break;
        case '>':  out += "&gt;"; break;
        default:   out.push_back(ch); break;
        }
    }
    return out;
}

static std::string UnescapeXmlAttr(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0) { out.push_back('&'); i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { out.push_back('"'); i += 5; continue; }
            if (s.compare(i, 6, "&apos;") == 0) { out.push_back('\''); i += 5; continue; }
            if (s.compare(i, 4, "&lt;") == 0) { out.push_back('<'); i += 3; continue; }
            if (s.compare(i, 4, "&gt;") == 0) { out.push_back('>'); i += 3; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

static bool ReadWholeFile(const std::string& path, std::string* outText) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    long sz = std::ftell(f);
    if (sz < 0 || sz > 8 * 1024 * 1024) {
        std::fclose(f);
        return false;
    }
    std::rewind(f);
    outText->resize(static_cast<size_t>(sz));
    if (sz > 0) {
        size_t got = std::fread(&(*outText)[0], 1, static_cast<size_t>(sz), f);
        outText->resize(got);
    }
    std::fclose(f);
    return true;
}

static bool WriteWholeFile(const std::string& path, const std::string& text) {
    std::string musicDir = JoinExePath("music");
    CreateDirectoryA(musicDir.c_str(), nullptr);
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || f == nullptr) return false;
    size_t written = 0;
    if (!text.empty()) {
        written = std::fwrite(text.data(), 1, text.size(), f);
    }
    std::fclose(f);
    return written == text.size();
}

static void RefreshXmlTimestamp() {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExA(GetPlaylistXmlPath().c_str(), GetFileExInfoStandard, &fad)) {
        g_xmlLastWrite = fad.ftLastWriteTime;
    }
}

static void ParseAttributes(
    const std::string& tagBody,
    std::vector<std::pair<std::string, std::string>>* outAttrs
) {
    size_t i = 0;
    const size_t n = tagBody.size();
    while (i < n) {
        while (i < n && (std::isspace(static_cast<unsigned char>(tagBody[i])) || tagBody[i] == '/')) {
            ++i;
        }
        if (i >= n) break;
        size_t keyStart = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(tagBody[i])) && tagBody[i] != '=' && tagBody[i] != '/') {
            ++i;
        }
        std::string key = ToLowerAscii(tagBody.substr(keyStart, i - keyStart));
        while (i < n && std::isspace(static_cast<unsigned char>(tagBody[i]))) ++i;
        if (i >= n || tagBody[i] != '=') continue;
        ++i;
        while (i < n && std::isspace(static_cast<unsigned char>(tagBody[i]))) ++i;
        if (i >= n) break;
        char quote = tagBody[i];
        if (quote != '"' && quote != '\'') continue;
        ++i;
        size_t valStart = i;
        while (i < n && tagBody[i] != quote) ++i;
        std::string val = UnescapeXmlAttr(tagBody.substr(valStart, i - valStart));
        if (i < n) ++i;
        outAttrs->emplace_back(std::move(key), std::move(val));
    }
}

static const std::string* FindAttr(
    const std::vector<std::pair<std::string, std::string>>& attrs,
    const char* key
) {
    for (const auto& kv : attrs) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

static void ParsePlaylistXmlText(const std::string& xml) {
    g_xmlSoundFont.clear();
    g_xmlSoundFontGainDb = 0.0f;
    g_titleTracks   = {"", ""};
    g_debriefTracks = {"", ""};
    g_levelEntries.clear();
    g_levelIndex.clear();

    size_t pos = 0;
    const size_t n = xml.size();
    while (pos < n) {
        size_t lt = xml.find('<', pos);
        if (lt == std::string::npos) break;
        if (lt + 4 <= n && xml.compare(lt, 4, "<!--") == 0) {
            size_t endC = xml.find("-->", lt + 4);
            pos = (endC == std::string::npos) ? n : (endC + 3);
            continue;
        }
        if (lt + 2 <= n && xml.compare(lt, 2, "<?") == 0) {
            size_t endPi = xml.find("?>", lt + 2);
            pos = (endPi == std::string::npos) ? n : (endPi + 2);
            continue;
        }
        size_t gt = std::string::npos;
        char quote = 0;
        for (size_t j = lt + 1; j < n; ++j) {
            char ch = xml[j];
            if (quote != 0) {
                if (ch == quote) quote = 0;
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '>') {
                gt = j;
                break;
            }
        }
        if (gt == std::string::npos) break;
        pos = gt + 1;

        std::string inner = TrimAscii(xml.substr(lt + 1, gt - (lt + 1)));
        if (inner.empty() || inner[0] == '/') continue;

        size_t sp = 0;
        while (sp < inner.size() && !std::isspace(static_cast<unsigned char>(inner[sp])) && inner[sp] != '/') {
            ++sp;
        }
        std::string tag = ToLowerAscii(inner.substr(0, sp));
        if (tag != "menu" && tag != "level" && tag != "soundfont" && tag != "midi") continue;

        std::vector<std::pair<std::string, std::string>> attrs;
        ParseAttributes(inner.substr(sp), &attrs);

        if (tag == "soundfont" || tag == "midi") {
            const std::string* pPath = FindAttr(attrs, "path");
            if (pPath == nullptr) pPath = FindAttr(attrs, "file");
            if (pPath == nullptr) pPath = FindAttr(attrs, "soundfont");
            if (pPath != nullptr) {
                g_xmlSoundFont = TrimAscii(*pPath);
            }
            const std::string* pGain = FindAttr(attrs, "gain");
            if (pGain == nullptr) pGain = FindAttr(attrs, "gain_db");
            if (pGain == nullptr) pGain = FindAttr(attrs, "volume_db");
            if (pGain != nullptr) {
                std::string gs = TrimAscii(*pGain);
                if (!gs.empty()) {
                    char* endPtr = nullptr;
                    float val = std::strtof(gs.c_str(), &endPtr);
                    if (endPtr != gs.c_str() && std::isfinite(val)) {
                        g_xmlSoundFontGainDb = (std::max)(-36.0f, (std::min)(24.0f, val));
                    }
                }
            }
            continue;
        }

        const std::string* pName = FindAttr(attrs, "name");
        if (pName == nullptr) pName = FindAttr(attrs, "filename");
        const std::string* pPc   = FindAttr(attrs, "pc");
        const std::string* pJava = FindAttr(attrs, "java");
        if (pName == nullptr || pName->empty()) continue;

        std::string name = TrimAscii(*pName);
        std::string pc   = pPc   ? NormalizeTrackSpec(*pPc)   : std::string{};
        std::string java = pJava ? NormalizeTrackSpec(*pJava) : std::string{};

        if (tag == "menu") {
            std::string key = ToLowerAscii(name);
            if (key == "title") {
                g_titleTracks = {pc, java};
            } else if (key == "debrief") {
                g_debriefTracks = {pc, java};
            }
            continue;
        }

        std::string key = ToLowerAscii(name);
        auto it = g_levelIndex.find(key);
        if (it == g_levelIndex.end()) {
            g_levelIndex[key] = g_levelEntries.size();
            g_levelEntries.push_back({name, {pc, java}});
        } else {
            g_levelEntries[it->second].tracks = {pc, java};
        }
    }
}

static void ExtractMappingCategory(
    const std::string& mappingXml,
    const char* openTag,
    const char* closeTag,
    std::vector<std::string>* outNames
) {
    size_t b = mappingXml.find(openTag);
    if (b == std::string::npos) return;
    size_t e = mappingXml.find(closeTag, b);
    if (e == std::string::npos) return;

    size_t pos = b;
    while (pos < e) {
        size_t lt = mappingXml.find("<level", pos);
        if (lt == std::string::npos || lt >= e) break;
        size_t gt = mappingXml.find('>', lt);
        if (gt == std::string::npos || gt > e) break;
        std::vector<std::pair<std::string, std::string>> attrs;
        ParseAttributes(mappingXml.substr(lt + 6, gt - (lt + 6)), &attrs);
        const std::string* fn = FindAttr(attrs, "filename");
        if (fn != nullptr && !fn->empty()) {
            outNames->push_back(TrimAscii(*fn));
        }
        pos = gt + 1;
    }
}

static std::string BuildPlaylistXmlText(
    const std::vector<std::pair<const char*, std::vector<PlaylistLevelEntry>>>* grouped
) {
    std::string out;
    out.reserve(16384);
    out += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
    out += "<playlist>\r\n";
    if (!g_xmlSoundFont.empty()) {
        out += "  <soundfont path=\"" + EscapeXmlAttr(g_xmlSoundFont) + "\" />\r\n";
    }
    out += "  <menu name=\"title\" pc=\"" + EscapeXmlAttr(g_titleTracks.pc) + "\" java=\"" + EscapeXmlAttr(g_titleTracks.java) + "\" />\r\n";
    out += "  <menu name=\"debrief\" pc=\"" + EscapeXmlAttr(g_debriefTracks.pc) + "\" java=\"" + EscapeXmlAttr(g_debriefTracks.java) + "\" />\r\n";
    out += "  <levels>\r\n";
    if (grouped != nullptr) {
        for (const auto& sec : *grouped) {
            if (sec.second.empty()) continue;
            out += "    <!-- ";
            out += sec.first;
            out += " -->\r\n";
            for (const auto& lv : sec.second) {
                out += "    <level name=\"" + EscapeXmlAttr(lv.name) +
                       "\" pc=\"" + EscapeXmlAttr(lv.tracks.pc) +
                       "\" java=\"" + EscapeXmlAttr(lv.tracks.java) + "\" />\r\n";
            }
        }
    } else {
        for (const auto& lv : g_levelEntries) {
            out += "    <level name=\"" + EscapeXmlAttr(lv.name) +
                   "\" pc=\"" + EscapeXmlAttr(lv.tracks.pc) +
                   "\" java=\"" + EscapeXmlAttr(lv.tracks.java) + "\" />\r\n";
        }
    }
    out += "  </levels>\r\n";
    out += "</playlist>\r\n";
    return out;
}

static void GenerateFullPlaylistXml() {
    if (FileExistsOnDisk(JoinExePath("music\\Bada_Mobile_0300_base.sf2"))) {
        g_xmlSoundFont = "music\\Bada_Mobile_0300_base.sf2";
    } else {
        g_xmlSoundFont = kDefaultFallbackSoundFont;
    }
    g_titleTracks   = {"00_Title", "100_menus"};
    g_debriefTracks = {"13_Debrief", "13_Debrief"};
    g_levelEntries.clear();
    g_levelIndex.clear();

    std::string mappingXml;
    ReadWholeFile(JoinExePath("levels\\mapping.xml"), &mappingXml);

    std::vector<std::string> stdNames, bonusNames, extNames;
    if (!mappingXml.empty()) {
        ExtractMappingCategory(mappingXml, "<standard", "</standard>", &stdNames);
        ExtractMappingCategory(mappingXml, "<bonus",    "</bonus>",    &bonusNames);
        ExtractMappingCategory(mappingXml, "<extended", "</extended>", &extNames);
    }

    std::vector<std::pair<const char*, std::vector<PlaylistLevelEntry>>> grouped = {
        {"Standard", {}},
        {"Bonus", {}},
        {"Extended", {}},
        {"Other", {}},
    };

    auto addUnique = [&](const std::string& name, size_t groupIdx) {
        if (name.empty()) return;
        std::string key = ToLowerAscii(name);
        if (g_levelIndex.find(key) != g_levelIndex.end()) return;
        std::string binPath = JoinExePath(("levels\\" + name + ".bin").c_str());
        DWORD attrs = GetFileAttributesA(binPath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return;
        }
        PlaylistPair tracks = AutoGenerateLevelEntry(name, -1);
        g_levelIndex[key] = g_levelEntries.size();
        PlaylistLevelEntry entry{name, tracks};
        g_levelEntries.push_back(entry);
        grouped[groupIdx].second.push_back(std::move(entry));
    };

    for (const auto& n : stdNames)   addUnique(n, 0);
    for (const auto& n : bonusNames) addUnique(n, 1);
    for (const auto& n : extNames)   addUnique(n, 2);

    std::vector<std::string> diskBins;
    WIN32_FIND_DATAA fd{};
    HANDLE hFind = FindFirstFileA(JoinExePath("levels\\*.bin").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            size_t len = std::strlen(fd.cFileName);
            if (len > 4 && _stricmp(fd.cFileName + len - 4, ".bin") == 0) {
                diskBins.emplace_back(fd.cFileName, len - 4);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    std::sort(diskBins.begin(), diskBins.end(), [](const std::string& a, const std::string& b) {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    });
    for (const auto& n : diskBins) {
        addUnique(n, 3);
    }

    std::string xml = BuildPlaylistXmlText(&grouped);
    if (WriteWholeFile(GetPlaylistXmlPath(), xml)) {
        RefreshXmlTimestamp();
    }
    g_playlistLoaded = true;
}

static void EnsurePlaylistLoaded() {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    std::string path = GetPlaylistXmlPath();
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad) ||
        (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        GenerateFullPlaylistXml();
        return;
    }

    if (g_playlistLoaded &&
        fad.ftLastWriteTime.dwLowDateTime == g_xmlLastWrite.dwLowDateTime &&
        fad.ftLastWriteTime.dwHighDateTime == g_xmlLastWrite.dwHighDateTime) {
        return;
    }

    std::string text;
    if (ReadWholeFile(path, &text)) {
        ParsePlaylistXmlText(text);
        g_xmlLastWrite = fad.ftLastWriteTime;
        g_playlistLoaded = true;
    } else if (!g_playlistLoaded) {
        GenerateFullPlaylistXml();
    }
}

static const PlaylistLevelEntry* LookupLevelEntry(const std::string& levelName) {
    auto it = g_levelIndex.find(ToLowerAscii(levelName));
    if (it != g_levelIndex.end()) {
        return &g_levelEntries[it->second];
    }
    if (EndsWithNoCase(levelName, "_winter") && levelName.size() > 7) {
        std::string base = levelName.substr(0, levelName.size() - 7);
        auto itBase = g_levelIndex.find(ToLowerAscii(base));
        if (itBase != g_levelIndex.end()) {
            return &g_levelEntries[itBase->second];
        }
    }
    return nullptr;
}

static void AppendMissingLevelToXml(const PlaylistLevelEntry& entry) {
    std::string path = GetPlaylistXmlPath();
    std::string xml;
    if (!ReadWholeFile(path, &xml)) {
        WriteWholeFile(path, BuildPlaylistXmlText(nullptr));
        RefreshXmlTimestamp();
        return;
    }

    std::string line =
        "    <level name=\"" + EscapeXmlAttr(entry.name) +
        "\" pc=\"" + EscapeXmlAttr(entry.tracks.pc) +
        "\" java=\"" + EscapeXmlAttr(entry.tracks.java) + "\" />\r\n";

    size_t closePos = xml.rfind("</levels>");
    if (closePos != std::string::npos) {
        size_t lineStart = closePos;
        while (lineStart > 0 && (xml[lineStart - 1] == ' ' || xml[lineStart - 1] == '\t')) {
            --lineStart;
        }
        std::string updated = xml.substr(0, lineStart) + line + xml.substr(lineStart);
        if (WriteWholeFile(path, updated)) {
            RefreshXmlTimestamp();
        }
    } else {
        if (WriteWholeFile(path, BuildPlaylistXmlText(nullptr))) {
            RefreshXmlTimestamp();
        }
    }
}

static void EnsureLevelInPlaylist(const std::string& levelName, int rawFileMusicId) {
    EnsurePlaylistLoaded();
    if (levelName.empty()) return;
    if (LookupLevelEntry(levelName) != nullptr) return;

    PlaylistPair tracks = AutoGenerateLevelEntry(levelName, rawFileMusicId);
    std::string key = ToLowerAscii(levelName);
    g_levelIndex[key] = g_levelEntries.size();
    PlaylistLevelEntry entry{levelName, tracks};
    g_levelEntries.push_back(entry);
    AppendMissingLevelToXml(entry);
}

static std::string PickPlaylistPath(const PlaylistPair& pair, const char* fallbackSpec) {
    const std::string& preferred = (g_musicPlaylist != 0) ? pair.java : pair.pc;
    if (preferred.empty()) {
        return {};
    }
    std::string path = ResolveTrackFilePath(preferred);
    if (!path.empty()) {
        return path;
    }
    if (g_musicPlaylist != 0 && !pair.pc.empty()) {
        path = ResolveTrackFilePath(pair.pc);
        if (!path.empty()) {
            return path;
        }
    }
    if (fallbackSpec != nullptr && fallbackSpec[0] != '\0') {
        path = ResolveTrackFilePath(fallbackSpec);
        if (!path.empty()) {
            return path;
        }
    }
    return {};
}

static std::string ResolveTrackPathForMusicId(int musicId) {
    EnsurePlaylistLoaded();

    if (musicId == 0) {
        return PickPlaylistPath(g_titleTracks, nullptr);
    }
    if (musicId == 13) {
        return PickPlaylistPath(g_debriefTracks, nullptr);
    }
    if (musicId == kActiveLevelMusicId) {
        if (!g_currentLevelName.empty()) {
            const PlaylistLevelEntry* entry = LookupLevelEntry(g_currentLevelName);
            if (entry == nullptr) {
                EnsureLevelInPlaylist(g_currentLevelName, g_currentLevelFileMusicId);
                entry = LookupLevelEntry(g_currentLevelName);
            }
            if (entry != nullptr) {
                return PickPlaylistPath(entry->tracks, nullptr);
            }
        }
        if (g_currentLevelFileMusicId >= 0 && g_currentLevelFileMusicId < 25) {
            std::string p = ResolveTrackFilePath(kVanillaMusic[g_currentLevelFileMusicId]);
            if (!p.empty()) return p;
        }
        return ResolveTrackFilePath("06_Kakkoi");
    }
    if (musicId >= 0 && musicId < 25) {
        std::string p = ResolveTrackFilePath(kVanillaMusic[musicId]);
        if (!p.empty()) return p;
    }
    // 25 is unused; 26 (0x1a) is edge.exe's uninitialized musicId sentinel.
    if (musicId >= 27 && musicId <= 255) {
        std::string custom = ResolveIdToStem(musicId);
        if (!custom.empty()) {
            std::string p = ResolveTrackFilePath(custom);
            if (!p.empty()) return p;
        }
    }
    return ResolveTrackFilePath("06_Kakkoi");
}

static bool __cdecl HasDebriefMusic() {
    EnsurePlaylistLoaded();
    return !ResolveTrackPathForMusicId(13).empty();
}

static const char* __cdecl GetMusicFilename_Impl(int musicId) {
    static char s_outPath[MAX_PATH * 2] = {};
    std::string path = ResolveTrackPathForMusicId(musicId);
    if (path.empty()) {
        g_lastPlayedPath.clear();
        g_lastPlayedSoundFont.clear();
        g_lastPlayedMidiCfgRev = GetMidiConfigRevision();
        return nullptr;
    }
    if (path.size() >= sizeof(s_outPath)) {
        path.resize(sizeof(s_outPath) - 1);
    }
    std::memcpy(s_outPath, path.c_str(), path.size() + 1);
    g_lastPlayedPath = path;
    g_lastPlayedSoundFont = GetActiveSoundFontPath();
    g_lastPlayedMidiCfgRev = GetMidiConfigRevision();
    return s_outPath;
}

static void __cdecl AudioPlayMusicResolve_Impl(void* ap, int musicId) {
    const char* path = GetMusicFilename_Impl(musicId);
    if (path == nullptr) {
        if (ap != nullptr) {
            reinterpret_cast<void(__thiscall*)(void*, int)>(0x4743e0)(ap, 1);
        }
        return;
    }
    if (ap == nullptr) return;
    uint8_t* mp = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(ap) + 0x8);
    if (mp == nullptr) return;

    EdgeString es{};
    es.ctor_cstr(path);
    auto** vt = *reinterpret_cast<void***>(mp);
    reinterpret_cast<bool(__thiscall*)(void*, const EdgeString*, uint8_t)>(vt[0x04 / 4])(mp, &es, 1);
    es.dtor();
}

static void __declspec(naked) hk_audioPlayMusicResolve() {
    // At 0x47417e (14B): replaces `call 0x473e60; mov esi, eax; add esp, 4; test esi, esi; je 0x4741f6`.
    // On entry: [esp] = musicId (pushed at 0x47417d), edi = AudioPlayer*.
    __asm {
        mov eax, DWORD PTR [esp]
        push eax
        push edi
        call AudioPlayMusicResolve_Impl
        add esp, 12
        mov eax, 0x4741f6
        jmp eax
    }
}

static void __declspec(naked) hk_conditionalFadeMusic() {
    // Replaces `call 0x481560` at 0x48e08b (Cube::onWin duck) and 0x47c996 (enter debrief stop).
    // __thiscall: ecx = ScreenMgr*, stack = [retAddr, fade, targetVol].
    __asm {
        push ecx
        call HasDebriefMusic
        pop ecx
        test al, al
        jz cfm_skip
        mov eax, 0x481560
        jmp eax
    cfm_skip:
        ret 8
    }
}

static void __cdecl DebriefEnterMusic_Impl() {
    if (!HasDebriefMusic()) {
        return;
    }
    uint8_t* sm = GetScreenMgr();
    if (sm != nullptr) {
        // Start debrief music immediately at 100% volume (fadeIn = 0) instead of fading in.
        reinterpret_cast<void(__thiscall*)(void*, int, int)>(0x481590)(sm, 13, 0);
    }
}

static int __cdecl OnLevelLoadMusic_Impl(const EdgeString* levelName, int rawFileMusicId, uint32_t playMusic) {
    if (playMusic == 0 || levelName == nullptr || levelName->size == 0 || levelName->size >= 256) {
        return rawFileMusicId;
    }
    const char* namePtr = levelName->c_str();
    if (namePtr == nullptr || namePtr[0] == '\0') {
        return rawFileMusicId;
    }
    g_currentLevelName.assign(namePtr, levelName->size);
    g_currentLevelFileMusicId = rawFileMusicId;
    EnsureLevelInPlaylist(g_currentLevelName, rawFileMusicId);
    return kActiveLevelMusicId;
}

static void __declspec(naked) hk_levelLoadMusic() {
    // At 0x476205: replaces `mov DWORD PTR [esi+0x94], eax` (6 bytes).
    // ebp+0x8 = LevelInfo* (starts with EdgeString filename at +0x0), eax = raw fileMusicId, esi+0x98 = playMusic.
    __asm {
        push ecx
        push edx
        movzx edx, BYTE PTR [esi + 0x98]
        push edx
        push eax
        push DWORD PTR [ebp + 0x8]
        call OnLevelLoadMusic_Impl
        add esp, 12
        pop edx
        pop ecx
        mov DWORD PTR [esi + 0x94], eax
        ret
    }
}

} // namespace

namespace edgemod {

std::string GetActiveSoundFontPath() {
    EnsurePlaylistLoaded();
    if (!g_xmlSoundFont.empty()) {
        std::string resolved = ResolveSoundFontCandidate(g_xmlSoundFont);
        if (!resolved.empty()) return resolved;
    }
    MidiTuningConfig midiCfg = GetActiveMidiTuningConfig();
    if (!midiCfg.soundFontPath.empty()) {
        std::string resolved = ResolveSoundFontCandidate(midiCfg.soundFontPath);
        if (!resolved.empty()) return resolved;
    }
    std::string localSf = JoinExePath("music\\Bada_Mobile_0300_base.sf2");
    if (FileExistsOnDisk(localSf)) return localSf;
    if (FileExistsOnDisk(kDefaultFallbackSoundFont)) return kDefaultFallbackSoundFont;
    return {};
}

float GetActiveSoundFontGainDb() {
    EnsurePlaylistLoaded();
    return g_xmlSoundFontGainDb;
}

void EnsureMusicPlaylistReady() {
    EnsurePlaylistLoaded();
    EnsureMidiConfigReady();
}

void OnMusicPlaylistChanged() {
    EnsurePlaylistLoaded();
    uint8_t* sm = GetScreenMgr();
    uint8_t* ap = GetAudioPlayer();
    if (sm == nullptr || ap == nullptr) return;

    int curId = *reinterpret_cast<int*>(sm + 0x90);
    int musVol = *reinterpret_cast<int*>(sm + 0x2ac);
    float curVol = *reinterpret_cast<float*>(sm + 0x94);
    float targetVol = *reinterpret_cast<float*>(sm + 0x98);

    if (musVol <= 0 || targetVol <= 0.0f || curId == 0x1a) {
        return;
    }

    uint8_t* ws = GetWorldScreen();
    bool inLevel = (ws != nullptr && ws[0x2120] != 0);
    if (inLevel && curId != kActiveLevelMusicId) {
        return;
    }
    if (!inLevel && curId != 0 && curId != 13) {
        return;
    }

    std::string newPath = ResolveTrackPathForMusicId(curId);
    std::string newSf   = GetActiveSoundFontPath();
    uint64_t newCfgRev  = GetMidiConfigRevision();
    const bool sameTrack = (_stricmp(newPath.c_str(), g_lastPlayedPath.c_str()) == 0);
    const bool isMidi = EndsWithNoCase(newPath, ".mid") || EndsWithNoCase(newPath, ".midi");
    const bool sameSf = (_stricmp(newSf.c_str(), g_lastPlayedSoundFont.c_str()) == 0);
    const bool sameCfg = (newCfgRev == g_lastPlayedMidiCfgRev);
    if (sameTrack && (!isMidi || (sameSf && sameCfg))) {
        return;
    }

    if (newPath.empty()) {
        reinterpret_cast<void(__thiscall*)(void*, int)>(0x4743e0)(ap, 1);
        g_lastPlayedPath.clear();
        g_lastPlayedSoundFont.clear();
        g_lastPlayedMidiCfgRev = newCfgRev;
        return;
    }

    reinterpret_cast<void(__thiscall*)(void*, int)>(0x474160)(ap, curId);
    reinterpret_cast<void(__thiscall*)(void*, int, float)>(0x474480)(ap, 1, curVol);
}

} // namespace edgemod

void RegisterMusicPlaylist() {
    // 1. MusicManager::getMusicFilename (0x473e60, 9B)
    {
        static const uint8_t expGetMusic[9] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x83, 0xf8, 0x18};
        RegRelJmp(0x473e60, expGetMusic, 9, 5, &GetMusicFilename_Impl, 0x90, 0x473f2c);
    }

    // 2. AudioPlayer::playMusic stem lookup + stop on explicit silence (0x47417e, 14B)
    {
        static const uint8_t expPlayStem[14] = {
            0xe8, 0xdd, 0xfc, 0xff, 0xff, 0x8b, 0xf0, 0x83, 0xc4, 0x04, 0x85, 0xf6, 0x74, 0x6a
        };
        RegRelJmp(0x47417e, expPlayStem, 14, 5, &hk_audioPlayMusicResolve, 0x90, 0x47418c);
    }

    // 3. Level::load musicId capture + playlist auto-generation (0x476205, 6B)
    {
        static const uint8_t expLevelMusic[6] = {0x89, 0x86, 0x94, 0x00, 0x00, 0x00};
        RegRelCall(0x476205, expLevelMusic, 6, 6, &hk_levelLoadMusic, 0x90);
    }

    // 4. Debrief music: 100% initial volume (fadeIn=0) or continue level music without ducking
    {
        static const uint8_t expWinDuck[5] = {0xe8, 0xd0, 0x34, 0xff, 0xff};
        RegRelCall(0x48e08b, expWinDuck, 5, 5, &hk_conditionalFadeMusic);

        static const uint8_t expDebriefStop[5] = {0xe8, 0xc5, 0x4b, 0x00, 0x00};
        RegRelCall(0x47c996, expDebriefStop, 5, 5, &hk_conditionalFadeMusic);

        static const uint8_t expDebriefPlay[9] = {0x6a, 0x01, 0x6a, 0x0d, 0xe8, 0x34, 0x7d, 0xfd, 0xff};
        RegRelCall(0x4a9853, expDebriefPlay, 9, 9, &DebriefEnterMusic_Impl, 0x90);
    }
}
