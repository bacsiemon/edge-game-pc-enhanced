#include "mod_state.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push, 0)
#define TSF_IMPLEMENTATION
#include "third_party/tsf.h"

#define TML_IMPLEMENTATION
#include "third_party/tml.h"

#define DR_MP3_IMPLEMENTATION
#include "third_party/dr_mp3.h"

#define DR_WAV_IMPLEMENTATION
#include "third_party/dr_wav.h"

#define DR_FLAC_IMPLEMENTATION
#include "third_party/dr_flac.h"
#pragma warning(pop)

namespace {

using namespace edgemod;

static const GUID kMFMediaType_Audio = {
    0x73647561, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
static const GUID kMFAudioFormat_PCM = {
    0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
static const GUID kMF_MT_MAJOR_TYPE = {
    0x48eba18e, 0xf8c9, 0x4687, {0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f}
};
static const GUID kMF_MT_SUBTYPE = {
    0xf7e34c9a, 0x42e8, 0x4714, {0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5}
};
static const GUID kMF_MT_AUDIO_NUM_CHANNELS = {
    0x37e48bf5, 0x645e, 0x4c5b, {0x89, 0xde, 0xad, 0xa9, 0xe2, 0x9b, 0x69, 0x6a}
};
static const GUID kMF_MT_AUDIO_SAMPLES_PER_SECOND = {
    0x5faeeae7, 0x0290, 0x4c31, {0x9e, 0x8a, 0xc5, 0x34, 0xf6, 0x8d, 0x9d, 0xba}
};
static const GUID kMF_MT_AUDIO_BITS_PER_SAMPLE = {
    0xf2deb57f, 0x40fa, 0x4764, {0xaa, 0x33, 0xed, 0x4f, 0x2d, 0x1f, 0xf6, 0x69}
};

static bool EndsWithNoCase(const std::string& s, const char* suffix) {
    size_t slen = std::strlen(suffix);
    if (s.size() < slen) return false;
    return _stricmp(s.c_str() + (s.size() - slen), suffix) == 0;
}

static bool IsAbsolutePath(const std::string& p) {
    if (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':') return true;
    if (p.size() >= 2 && ((p[0] == '\\' && p[1] == '\\') || (p[0] == '/' && p[1] == '/'))) return true;
    if (!p.empty() && (p[0] == '\\' || p[0] == '/')) return true;
    return false;
}

static const std::string& GetExeDirForDecoders() {
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

static bool FileExistsA(const char* path) {
    if (path == nullptr || path[0] == '\0') return false;
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static std::string ResolveFullDiskPath(const std::string& path) {
    if (IsAbsolutePath(path)) return path;
    const std::string& exeDir = GetExeDirForDecoders();
    if (exeDir.empty()) return path;
    std::string norm = path;
    for (char& c : norm) {
        if (c == '/') c = '\\';
    }
    return exeDir + "\\" + norm;
}

static std::wstring AcpToWide(const char* s) {
    if (s == nullptr || s[0] == '\0') return {};
    int needed = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, &out[0], needed);
    return out;
}

static inline float ClampSample(float x) {
    if (x < -1.0f) return -1.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static void WriteTypedSample(uint32_t sampleType, void* basePtr, uint32_t idx, float val) {
    const float clamped = ClampSample(val);
    switch (sampleType) {
    case 0:
        reinterpret_cast<int8_t*>(basePtr)[idx] = static_cast<int8_t>(std::lrint(clamped * 127.0f));
        break;
    case 1:
        reinterpret_cast<uint8_t*>(basePtr)[idx] = static_cast<uint8_t>(std::lrint((clamped + 1.0f) * 127.5f));
        break;
    case 2:
        reinterpret_cast<int16_t*>(basePtr)[idx] = static_cast<int16_t>(std::lrint(clamped * 32767.0f));
        break;
    case 3:
        reinterpret_cast<float*>(basePtr)[idx] = clamped;
        break;
    default:
        break;
    }
}

static float PickMappedChannelSample(const float* frameSamples, uint32_t srcCh, uint32_t dstCh, uint32_t outChIdx) {
    if (srcCh == 0) return 0.0f;
    if (dstCh == 1) {
        if (srcCh == 1) return frameSamples[0];
        return 0.5f * (frameSamples[0] + frameSamples[1]);
    }
    if (srcCh == 1) {
        return (outChIdx < 2) ? frameSamples[0] : 0.0f;
    }
    return (outChIdx < srcCh) ? frameSamples[outChIdx] : 0.0f;
}

// Virtual base layout matching edge.exe OggDecoder vtable (0x580c5c)
struct EdgeAudioDecoder {
    virtual ~EdgeAudioDecoder() = default;                                                                                        // +0x00
    virtual uint32_t getChannelCount() = 0;                                                                                       // +0x04
    virtual uint32_t getSampleRate() = 0;                                                                                         // +0x08
    virtual uint32_t getSampleSizeBits() { return 32u; }                                                                          // +0x0c
    virtual uint32_t getFrameCount() { return 0u; }                                                                               // +0x10
    virtual uint32_t getExtra() { return 0u; }                                                                                    // +0x14
    virtual uint32_t decodeDeinterleaved(uint32_t sampleType, void** ppChannels, uint32_t channels, uint32_t frames, uint32_t offset); // +0x18
    virtual uint32_t decodeInterleaved(uint32_t sampleType, void* pBuffer, uint32_t channels, uint32_t frames, uint32_t offset);       // +0x1c
    virtual bool     rewind();                                                                                                    // +0x20

protected:
    virtual uint32_t decodeFloatFrames(float* dstInterleaved, uint32_t frames) = 0;
    virtual uint32_t sourceChannels() const = 0;
    virtual bool     rewindStream() = 0;

    bool m_decodedAnySinceRewind = false;
    std::vector<float> m_scratch;
};

uint32_t EdgeAudioDecoder::decodeDeinterleaved(
    uint32_t sampleType, void** ppChannels, uint32_t channels, uint32_t frames, uint32_t offset
) {
    if (frames == 0 || ppChannels == nullptr || channels == 0) return 0;
    const uint32_t srcCh = sourceChannels();
    if (srcCh == 0) return 0;

    m_scratch.resize(static_cast<size_t>(frames) * srcCh);
    const uint32_t got = decodeFloatFrames(m_scratch.data(), frames);
    if (got > 0) m_decodedAnySinceRewind = true;

    for (uint32_t f = 0; f < got; ++f) {
        const float* frm = m_scratch.data() + static_cast<size_t>(f) * srcCh;
        for (uint32_t c = 0; c < channels; ++c) {
            if (ppChannels[c] != nullptr) {
                const float val = PickMappedChannelSample(frm, srcCh, channels, c);
                WriteTypedSample(sampleType, ppChannels[c], offset + f, val);
            }
        }
    }
    return got;
}

uint32_t EdgeAudioDecoder::decodeInterleaved(
    uint32_t sampleType, void* pBuffer, uint32_t channels, uint32_t frames, uint32_t offset
) {
    if (frames == 0 || pBuffer == nullptr || channels == 0) return 0;
    const uint32_t srcCh = sourceChannels();
    if (srcCh == 0) return 0;

    m_scratch.resize(static_cast<size_t>(frames) * srcCh);
    const uint32_t got = decodeFloatFrames(m_scratch.data(), frames);
    if (got > 0) m_decodedAnySinceRewind = true;

    for (uint32_t f = 0; f < got; ++f) {
        const float* frm = m_scratch.data() + static_cast<size_t>(f) * srcCh;
        const uint32_t baseIdx = (offset + f) * channels;
        for (uint32_t c = 0; c < channels; ++c) {
            const float val = PickMappedChannelSample(frm, srcCh, channels, c);
            WriteTypedSample(sampleType, pBuffer, baseIdx + c, val);
        }
    }
    return got;
}

bool EdgeAudioDecoder::rewind() {
    if (!m_decodedAnySinceRewind) {
        return false;
    }
    const bool ok = rewindStream();
    m_decodedAnySinceRewind = false;
    return ok;
}

// ------------------------------------------------------------------
// Shared SoundFont Cache + MIDI Decoder (TinySoundFont + TinyMidiLoader)
// ------------------------------------------------------------------
static std::mutex g_sfMutex;
static tsf*       g_cachedSf = nullptr;
static std::string g_cachedSfPath;
static FILETIME   g_cachedSfTime{};

static tsf* AcquireSoundFontCopy(const char* sfPath) {
    if (sfPath == nullptr || sfPath[0] == '\0') return nullptr;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(sfPath, GetFileExInfoStandard, &fad) ||
        (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_sfMutex);
    const bool samePath = (_stricmp(g_cachedSfPath.c_str(), sfPath) == 0);
    const bool sameTime =
        fad.ftLastWriteTime.dwLowDateTime == g_cachedSfTime.dwLowDateTime &&
        fad.ftLastWriteTime.dwHighDateTime == g_cachedSfTime.dwHighDateTime;

    if (g_cachedSf == nullptr || !samePath || !sameTime) {
        tsf* loaded = tsf_load_filename(sfPath);
        if (loaded == nullptr) {
            return nullptr;
        }
        if (g_cachedSf != nullptr) {
            tsf_close(g_cachedSf);
        }
        g_cachedSf = loaded;
        g_cachedSfPath = sfPath;
        g_cachedSfTime = fad.ftLastWriteTime;
    }
    return tsf_copy(g_cachedSf);
}

static void CloseSoundFontCopy(tsf* synth) {
    if (synth == nullptr) return;
    std::lock_guard<std::mutex> lock(g_sfMutex);
    tsf_close(synth);
}

static void DispatchMidiMessage(tsf* synth, const tml_message* msg) {
    const int ch = static_cast<unsigned char>(msg->channel);
    switch (msg->type) {
    case TML_PROGRAM_CHANGE:
        tsf_channel_set_presetnumber(synth, ch, static_cast<unsigned char>(msg->program), (ch == 9) ? 1 : 0);
        break;
    case TML_NOTE_ON: {
        const int key = static_cast<unsigned char>(msg->key);
        const int vel = static_cast<unsigned char>(msg->velocity);
        if (vel == 0) {
            tsf_channel_note_off(synth, ch, key);
        } else {
            tsf_channel_note_on(synth, ch, key, static_cast<float>(vel) / 127.0f);
        }
        break;
    }
    case TML_NOTE_OFF:
        tsf_channel_note_off(synth, ch, static_cast<unsigned char>(msg->key));
        break;
    case TML_PITCH_BEND:
        tsf_channel_set_pitchwheel(synth, ch, static_cast<int>(msg->pitch_bend));
        break;
    case TML_CONTROL_CHANGE:
        tsf_channel_midi_control(
            synth, ch,
            static_cast<unsigned char>(msg->control),
            static_cast<unsigned char>(msg->control_value)
        );
        break;
    default:
        break;
    }
}

static std::string BuildFileTimestampTag(const char* path) {
    if (path == nullptr || path[0] == '\0') return {};
    std::string norm(path);
    for (char& ch : norm) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        char buf[32]{};
        std::snprintf(
            buf, sizeof(buf), "|%08lx:%08lx",
            static_cast<unsigned long>(fad.ftLastWriteTime.dwLowDateTime),
            static_cast<unsigned long>(fad.ftLastWriteTime.dwHighDateTime)
        );
        norm += buf;
    }
    return norm;
}

// ------------------------------------------------------------------
// music/midi.ini Configuration Loader / Auto-Generator
// ------------------------------------------------------------------
static std::mutex       g_midiCfgMutex;
static MidiTuningConfig g_midiCfg{};
static FILETIME         g_midiIniTime{};
static bool             g_midiCfgLoaded = false;
static uint64_t         g_midiCfgRevision = 0;

static std::string TrimIniAscii(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

static std::string ToLowerIniAscii(const std::string& s) {
    std::string out = s;
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

static bool ParseIniBool(const std::string& val, bool fallback) {
    std::string v = ToLowerIniAscii(TrimIniAscii(val));
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return fallback;
}

static float ParseIniFloat(const std::string& val, float minV, float maxV, float fallback) {
    std::string s = TrimIniAscii(val);
    if (s.empty()) return fallback;
    char* endPtr = nullptr;
    float f = std::strtof(s.c_str(), &endPtr);
    if (endPtr == s.c_str() || !std::isfinite(f)) return fallback;
    return (std::max)(minV, (std::min)(maxV, f));
}

static int ParseIniInt(const std::string& val, int minV, int maxV, int fallback) {
    std::string s = TrimIniAscii(val);
    if (s.empty()) return fallback;
    char* endPtr = nullptr;
    long v = std::strtol(s.c_str(), &endPtr, 10);
    if (endPtr == s.c_str()) return fallback;
    if (v < static_cast<long>(minV)) return minV;
    if (v > static_cast<long>(maxV)) return maxV;
    return static_cast<int>(v);
}

static std::string GetMidiIniPath() {
    return ResolveFullDiskPath("music\\midi.ini");
}

static const char* kDefaultMidiIniText =
    "; EDGE MIDI / SoundFont Playback Configuration\r\n"
    "; Auto-generated if missing. Hot-reloads automatically on edit (next track or playlist switch).\r\n"
    "; Numeric values are clamped to safe ranges; invalid entries fall back to defaults.\r\n"
    "\r\n"
    "[soundfont]\r\n"
    "; Optional default SoundFont (.sf2). Leave empty to follow <soundfont> in music/playlist.xml\r\n"
    "; (or Bada_Mobile_0300_base.sf2). Relative paths check music/ then the game folder.\r\n"
    "path=\r\n"
    "\r\n"
    "[quality]\r\n"
    "; Sample interpolation: linear or cubic (4-point Catmull-Rom, cleaner highs when pitch-shifting)\r\n"
    "interpolation=cubic\r\n"
    "; Simultaneous polyphony voice cap (16..512)\r\n"
    "max_voices=128\r\n"
    "; Output sample rate: 22050, 44100, or 48000\r\n"
    "sample_rate=44100\r\n"
    "\r\n"
    "[loudness]\r\n"
    "; Per-track RMS loudness normalization targeting target_db (-30..-10 dBFS)\r\n"
    "auto_level=1\r\n"
    "target_db=-17.0\r\n"
    "; Manual output gain offset in dB (-36..24), added after auto-leveling & playlist.xml gain\r\n"
    "gain_db=0.0\r\n"
    "; Soft-knee tanh peak limiter (prevents clipping on dense chords)\r\n"
    "limiter=1\r\n"
    "; 20 Hz DC-blocking high-pass filter\r\n"
    "dc_blocker=1\r\n"
    "\r\n"
    "[modulation]\r\n"
    "; MIDI CC 1 (Modulation Wheel) -> vibrato pitch LFO depth\r\n"
    "mod_wheel=1\r\n"
    "; Maximum added vibrato depth in cents at CC 1 = 127 (0..200)\r\n"
    "mod_wheel_cents=50.0\r\n"
    "\r\n"
    "[reverb]\r\n"
    "; Stereo Freeverb spatial ambience (0.0 disables, 0.0..1.0)\r\n"
    "level=0.18\r\n"
    "room=0.55\r\n"
    "damp=0.55\r\n"
    "width=0.85\r\n"
    "\r\n"
    "[chorus]\r\n"
    "; Stereo chorus ensemble effect (0.0 disables, 0.0..1.0)\r\n"
    "level=0.12\r\n"
    "; LFO rate in Hz (0.05..5.0)\r\n"
    "speed=0.35\r\n"
    "; Modulation sweep depth in ms (0.1..10.0)\r\n"
    "depth_ms=2.0\r\n"
    "\r\n"
    "[playback]\r\n"
    "; Keep decaying notes & reverb/chorus tails ringing smoothly across loop wraps\r\n"
    "seamless_loop=1\r\n"
    "; Support RPG Maker CC 111 loop-start marker on repeat\r\n"
    "cc111_loop=1\r\n";

static bool WriteDefaultMidiIniIfMissing(const std::string& iniPath) {
    if (iniPath.empty() || FileExistsA(iniPath.c_str())) return false;
    std::string musicDir = ResolveFullDiskPath("music");
    if (!musicDir.empty()) {
        CreateDirectoryA(musicDir.c_str(), nullptr);
    }
    FILE* f = nullptr;
    if (fopen_s(&f, iniPath.c_str(), "wb") != 0 || f == nullptr) return false;
    const size_t len = std::strlen(kDefaultMidiIniText);
    const size_t wrote = std::fwrite(kDefaultMidiIniText, 1, len, f);
    std::fclose(f);
    return wrote == len;
}

static MidiTuningConfig ParseMidiIniText(const std::string& text) {
    MidiTuningConfig cfg{};
    std::string section;
    size_t pos = 0;
    const size_t n = text.size();
    while (pos < n) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = n;
        std::string line = TrimIniAscii(text.substr(pos, eol - pos));
        pos = (eol < n) ? (eol + 1) : n;

        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close != std::string::npos) {
                section = ToLowerIniAscii(TrimIniAscii(line.substr(1, close - 1)));
            }
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = ToLowerIniAscii(TrimIniAscii(line.substr(0, eq)));
        std::string val = TrimIniAscii(line.substr(eq + 1));

        if (section == "soundfont") {
            if (key == "path" || key == "file" || key == "soundfont") {
                cfg.soundFontPath = val;
            }
        } else if (section == "quality") {
            if (key == "interpolation" || key == "interp") {
                std::string lv = ToLowerIniAscii(val);
                if (lv == "linear" || lv == "0") cfg.cubicInterp = false;
                else if (lv == "cubic" || lv == "1") cfg.cubicInterp = true;
            } else if (key == "max_voices") {
                cfg.maxVoices = ParseIniInt(val, 16, 512, cfg.maxVoices);
            } else if (key == "sample_rate") {
                int sr = ParseIniInt(val, 22050, 48000, static_cast<int>(cfg.sampleRate));
                if (sr == 22050 || sr == 44100 || sr == 48000) {
                    cfg.sampleRate = static_cast<uint32_t>(sr);
                } else {
                    cfg.sampleRate = 44100u;
                }
            }
        } else if (section == "loudness") {
            if (key == "auto_level") {
                cfg.autoLevel = ParseIniBool(val, cfg.autoLevel);
            } else if (key == "target_db") {
                cfg.targetDb = ParseIniFloat(val, -30.0f, -10.0f, cfg.targetDb);
            } else if (key == "gain_db" || key == "gain") {
                cfg.gainDb = ParseIniFloat(val, -36.0f, 24.0f, cfg.gainDb);
            } else if (key == "limiter") {
                cfg.limiter = ParseIniBool(val, cfg.limiter);
            } else if (key == "dc_blocker") {
                cfg.dcBlocker = ParseIniBool(val, cfg.dcBlocker);
            }
        } else if (section == "modulation") {
            if (key == "mod_wheel") {
                cfg.modWheel = ParseIniBool(val, cfg.modWheel);
            } else if (key == "mod_wheel_cents") {
                cfg.modWheelCents = ParseIniFloat(val, 0.0f, 200.0f, cfg.modWheelCents);
            }
        } else if (section == "reverb") {
            if (key == "level") cfg.reverbLevel = ParseIniFloat(val, 0.0f, 1.0f, cfg.reverbLevel);
            else if (key == "room") cfg.reverbRoom = ParseIniFloat(val, 0.0f, 1.0f, cfg.reverbRoom);
            else if (key == "damp") cfg.reverbDamp = ParseIniFloat(val, 0.0f, 1.0f, cfg.reverbDamp);
            else if (key == "width") cfg.reverbWidth = ParseIniFloat(val, 0.0f, 1.0f, cfg.reverbWidth);
        } else if (section == "chorus") {
            if (key == "level") cfg.chorusLevel = ParseIniFloat(val, 0.0f, 1.0f, cfg.chorusLevel);
            else if (key == "speed") cfg.chorusSpeed = ParseIniFloat(val, 0.05f, 5.0f, cfg.chorusSpeed);
            else if (key == "depth_ms") cfg.chorusDepthMs = ParseIniFloat(val, 0.1f, 10.0f, cfg.chorusDepthMs);
        } else if (section == "playback") {
            if (key == "seamless_loop") cfg.seamlessLoop = ParseIniBool(val, cfg.seamlessLoop);
            else if (key == "cc111_loop") cfg.cc111Loop = ParseIniBool(val, cfg.cc111Loop);
        }
    }
    return cfg;
}

static void EnsureMidiConfigLoadedLocked() {
    std::string iniPath = GetMidiIniPath();
    if (iniPath.empty()) {
        if (!g_midiCfgLoaded) {
            g_midiCfg = MidiTuningConfig{};
            g_midiCfgLoaded = true;
            ++g_midiCfgRevision;
        }
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(iniPath.c_str(), GetFileExInfoStandard, &fad) ||
        (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        WriteDefaultMidiIniIfMissing(iniPath);
        g_midiCfg = MidiTuningConfig{};
        g_midiCfgLoaded = true;
        if (GetFileAttributesExA(iniPath.c_str(), GetFileExInfoStandard, &fad)) {
            g_midiIniTime = fad.ftLastWriteTime;
        }
        ++g_midiCfgRevision;
        return;
    }

    const bool sameTime = g_midiCfgLoaded &&
        fad.ftLastWriteTime.dwLowDateTime == g_midiIniTime.dwLowDateTime &&
        fad.ftLastWriteTime.dwHighDateTime == g_midiIniTime.dwHighDateTime;
    if (sameTime) return;

    FILE* f = nullptr;
    std::string text;
    if (fopen_s(&f, iniPath.c_str(), "rb") == 0 && f != nullptr) {
        if (std::fseek(f, 0, SEEK_END) == 0) {
            long sz = std::ftell(f);
            if (sz > 0 && sz <= 1024 * 1024) {
                std::rewind(f);
                text.resize(static_cast<size_t>(sz));
                size_t got = std::fread(&text[0], 1, static_cast<size_t>(sz), f);
                text.resize(got);
            }
        }
        std::fclose(f);
    }

    g_midiCfg = ParseMidiIniText(text);
    g_midiIniTime = fad.ftLastWriteTime;
    g_midiCfgLoaded = true;
    ++g_midiCfgRevision;
}

static std::mutex g_autoGainMutex;
static std::unordered_map<std::string, double> g_midiRawRmsCache;

static double MeasureMidiRawRmsDb(const char* midiPath, const char* sfPath, const tml_message* head) {
    if (head == nullptr || sfPath == nullptr || sfPath[0] == '\0') return -999.0;

    std::string cacheKey = BuildFileTimestampTag(midiPath) + "||" + BuildFileTimestampTag(sfPath);
    if (!cacheKey.empty()) {
        std::lock_guard<std::mutex> lock(g_autoGainMutex);
        auto it = g_midiRawRmsCache.find(cacheKey);
        if (it != g_midiRawRmsCache.end()) {
            return it->second;
        }
    }

    tsf* probe = AcquireSoundFontCopy(sfPath);
    if (probe == nullptr) return -999.0;

    constexpr uint32_t kProbeRate = 11025u;
    constexpr uint32_t kMaxProbeFrames = 30u * kProbeRate;

    tsf_set_output(probe, TSF_STEREO_INTERLEAVED, kProbeRate, 0.0f);
    tsf_set_max_voices(probe, 128);
    tsf_set_cubic_interpolation(probe, 0);
    tsf_set_modwheel_depth(probe, 0.0f);
    tsf_channel_set_presetnumber(probe, 9, 0, 1);

    const tml_message* cur = head;
    double curTimeMs = 0.0;
    double totalSumSq = 0.0;
    double activeSumSq = 0.0;
    uint64_t totalSamples = 0;
    uint64_t activeSamples = 0;
    float chunkBuf[64u * 2u]{};

    uint32_t doneFrames = 0;
    while (doneFrames < kMaxProbeFrames && cur != nullptr) {
        const uint32_t chunk = (std::min)(kMaxProbeFrames - doneFrames, 64u);
        curTimeMs += (static_cast<double>(chunk) * 1000.0) / static_cast<double>(kProbeRate);
        while (cur != nullptr && static_cast<double>(cur->time) <= curTimeMs) {
            DispatchMidiMessage(probe, cur);
            cur = cur->next;
        }
        tsf_render_float(probe, chunkBuf, static_cast<int>(chunk), 0);
        const uint32_t nSamples = chunk * 2u;
        double chunkSumSq = 0.0;
        for (uint32_t i = 0; i < nSamples; ++i) {
            const double s = static_cast<double>(chunkBuf[i]);
            chunkSumSq += s * s;
        }
        totalSumSq += chunkSumSq;
        totalSamples += nSamples;
        if (chunkSumSq / static_cast<double>(nSamples) > 1e-5) {
            activeSumSq += chunkSumSq;
            activeSamples += nSamples;
        }
        doneFrames += chunk;
    }

    CloseSoundFontCopy(probe);

    const double meanSq = (activeSamples > 0)
        ? (activeSumSq / static_cast<double>(activeSamples))
        : ((totalSamples > 0) ? (totalSumSq / static_cast<double>(totalSamples)) : 0.0);

    const double rmsDb = (meanSq > 1e-9) ? (10.0 * std::log10(meanSq)) : -999.0;
    if (!cacheKey.empty()) {
        std::lock_guard<std::mutex> lock(g_autoGainMutex);
        g_midiRawRmsCache[cacheKey] = rmsDb;
    }
    return rmsDb;
}

// ------------------------------------------------------------------
// MIDI DSP Effects: DC Blocker, Stereo Chorus, Freeverb, Soft Limiter
// ------------------------------------------------------------------
class StereoDcBlocker {
public:
    void init(uint32_t sampleRate) {
        const double sr = (sampleRate > 0u) ? static_cast<double>(sampleRate) : 44100.0;
        m_r = static_cast<float>(std::exp(-6.283185307179586 * 20.0 / sr));
        reset();
    }
    void reset() {
        m_xPrev[0] = m_xPrev[1] = 0.0f;
        m_yPrev[0] = m_yPrev[1] = 0.0f;
    }
    void process(float* interleaved, uint32_t frames) {
        for (uint32_t f = 0; f < frames; ++f) {
            for (int c = 0; c < 2; ++c) {
                const float x = interleaved[f * 2u + c];
                float y = x - m_xPrev[c] + m_r * m_yPrev[c];
                if (std::fabs(y) < 1e-18f) y = 0.0f;
                m_xPrev[c] = x;
                m_yPrev[c] = y;
                interleaved[f * 2u + c] = y;
            }
        }
    }
private:
    float m_r = 0.997f;
    float m_xPrev[2]{};
    float m_yPrev[2]{};
};

class SimpleStereoChorus {
public:
    void init(uint32_t sampleRate, float level, float speedHz, float depthMs) {
        m_level = level;
        if (m_level <= 0.0001f) {
            m_buf[0].clear();
            m_buf[1].clear();
            return;
        }
        const float sr = (sampleRate > 0u) ? static_cast<float>(sampleRate) : 44100.0f;
        m_baseDelay = 0.012f * sr;
        m_depthSamp = (depthMs * 0.001f) * sr;
        m_phaseInc = (6.283185307179586f * speedHz) / sr;
        const size_t cap = static_cast<size_t>(std::ceil(m_baseDelay + m_depthSamp)) + 64u;
        m_buf[0].assign(cap, 0.0f);
        m_buf[1].assign(cap, 0.0f);
        m_writePos = 0;
        m_phase = 0.0f;
    }
    void reset() {
        std::fill(m_buf[0].begin(), m_buf[0].end(), 0.0f);
        std::fill(m_buf[1].begin(), m_buf[1].end(), 0.0f);
        m_writePos = 0;
        m_phase = 0.0f;
    }
    void process(float* interleaved, uint32_t frames) {
        if (m_level <= 0.0001f || m_buf[0].empty()) return;
        const size_t cap = m_buf[0].size();
        const float wet = 0.5f * m_level;
        for (uint32_t f = 0; f < frames; ++f) {
            const float lfoL = std::sin(m_phase);
            const float lfoR = std::cos(m_phase); // 90 deg quadrature for stereo width
            m_phase += m_phaseInc;
            if (m_phase >= 6.283185307179586f) m_phase -= 6.283185307179586f;

            const float delays[2] = {
                m_baseDelay + 0.5f * (lfoL + 1.0f) * m_depthSamp,
                m_baseDelay + 0.5f * (lfoR + 1.0f) * m_depthSamp
            };

            for (int c = 0; c < 2; ++c) {
                const float in = interleaved[f * 2u + c];
                m_buf[c][m_writePos] = in;

                float rpos = static_cast<float>(m_writePos) - delays[c];
                while (rpos < 0.0f) rpos += static_cast<float>(cap);
                const size_t i0 = static_cast<size_t>(rpos) % cap;
                const size_t i1 = (i0 + 1u) % cap;
                const float frac = rpos - std::floor(rpos);
                const float delayed = m_buf[c][i0] * (1.0f - frac) + m_buf[c][i1] * frac;
                interleaved[f * 2u + c] = in + wet * delayed;
            }
            m_writePos = (m_writePos + 1u) % cap;
        }
    }
private:
    float m_level = 0.0f;
    float m_baseDelay = 529.0f;
    float m_depthSamp = 88.0f;
    float m_phaseInc = 0.0f;
    float m_phase = 0.0f;
    size_t m_writePos = 0;
    std::vector<float> m_buf[2];
};

class SimpleFreeverb {
public:
    void init(uint32_t sampleRate, float level, float room, float damp, float width) {
        m_level = level;
        if (m_level <= 0.0001f) {
            for (int c = 0; c < 2; ++c) {
                for (int i = 0; i < kNumCombs; ++i) m_comb[c][i].buf.clear();
                for (int i = 0; i < kNumAllpass; ++i) m_ap[c][i].buf.clear();
            }
            return;
        }
        static constexpr int kCombTunings[kNumCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static constexpr int kApTunings[kNumAllpass] = {556, 441, 341, 225};
        static constexpr int kSpread = 23;
        const double scale = ((sampleRate > 0u) ? static_cast<double>(sampleRate) : 44100.0) / 44100.0;

        for (int c = 0; c < 2; ++c) {
            const int sp = (c == 1) ? kSpread : 0;
            for (int i = 0; i < kNumCombs; ++i) {
                const size_t len = static_cast<size_t>((std::max)(1L, std::lround((kCombTunings[i] + sp) * scale)));
                m_comb[c][i].buf.assign(len, 0.0f);
                m_comb[c][i].idx = 0;
                m_comb[c][i].store = 0.0f;
            }
            for (int i = 0; i < kNumAllpass; ++i) {
                const size_t len = static_cast<size_t>((std::max)(1L, std::lround((kApTunings[i] + sp) * scale)));
                m_ap[c][i].buf.assign(len, 0.0f);
                m_ap[c][i].idx = 0;
            }
        }
        m_feedback = 0.70f + 0.28f * room;
        m_damp1 = 0.40f * damp;
        m_damp2 = 1.0f - m_damp1;
        m_wet1 = level * (0.5f + 0.5f * width);
        m_wet2 = level * (0.5f - 0.5f * width);
    }

    void reset() {
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < kNumCombs; ++i) {
                std::fill(m_comb[c][i].buf.begin(), m_comb[c][i].buf.end(), 0.0f);
                m_comb[c][i].idx = 0;
                m_comb[c][i].store = 0.0f;
            }
            for (int i = 0; i < kNumAllpass; ++i) {
                std::fill(m_ap[c][i].buf.begin(), m_ap[c][i].buf.end(), 0.0f);
                m_ap[c][i].idx = 0;
            }
        }
    }

    void process(float* interleaved, uint32_t frames) {
        if (m_level <= 0.0001f || m_comb[0][0].buf.empty()) return;
        constexpr float kInputGain = 0.015f;
        for (uint32_t f = 0; f < frames; ++f) {
            const float dryL = interleaved[f * 2u + 0u];
            const float dryR = interleaved[f * 2u + 1u];
            const float inMono = (dryL + dryR) * kInputGain;

            float acc[2] = {0.0f, 0.0f};
            for (int c = 0; c < 2; ++c) {
                float out = 0.0f;
                for (int i = 0; i < kNumCombs; ++i) {
                    Comb& cb = m_comb[c][i];
                    float bufOut = cb.buf[cb.idx];
                    if (std::fabs(bufOut) < 1e-18f) bufOut = 0.0f;
                    out += bufOut;
                    cb.store = bufOut * m_damp2 + cb.store * m_damp1;
                    if (std::fabs(cb.store) < 1e-18f) cb.store = 0.0f;
                    cb.buf[cb.idx] = inMono + cb.store * m_feedback;
                    if (++cb.idx >= cb.buf.size()) cb.idx = 0;
                }
                for (int i = 0; i < kNumAllpass; ++i) {
                    Allpass& ap = m_ap[c][i];
                    float bufOut = ap.buf[ap.idx];
                    if (std::fabs(bufOut) < 1e-18f) bufOut = 0.0f;
                    const float feedOut = -out + bufOut;
                    ap.buf[ap.idx] = out + bufOut * 0.5f;
                    if (++ap.idx >= ap.buf.size()) ap.idx = 0;
                    out = feedOut;
                }
                acc[c] = out;
            }
            interleaved[f * 2u + 0u] = dryL + m_wet1 * acc[0] + m_wet2 * acc[1];
            interleaved[f * 2u + 1u] = dryR + m_wet1 * acc[1] + m_wet2 * acc[0];
        }
    }

private:
    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllpass = 4;
    struct Comb {
        std::vector<float> buf;
        size_t idx = 0;
        float store = 0.0f;
    };
    struct Allpass {
        std::vector<float> buf;
        size_t idx = 0;
    };
    Comb m_comb[2][kNumCombs]{};
    Allpass m_ap[2][kNumAllpass]{};
    float m_level = 0.0f;
    float m_feedback = 0.84f;
    float m_damp1 = 0.22f;
    float m_damp2 = 0.78f;
    float m_wet1 = 0.0f;
    float m_wet2 = 0.0f;
};

static inline float SoftLimitMidiSample(float x) {
    constexpr float kKnee = 0.85f;
    constexpr float kHead = 1.0f - kKnee;
    const float ax = std::fabs(x);
    if (ax <= kKnee) return x;
    const float limited = kKnee + kHead * std::tanh((ax - kKnee) / kHead);
    return (x < 0.0f) ? -limited : limited;
}

class MidiDecoder final : public EdgeAudioDecoder {
public:
    MidiDecoder(tsf* synth, tml_message* head, const MidiTuningConfig& cfg, float totalGainDb)
        : m_synth(synth), m_head(head), m_cur(head), m_cfg(cfg), m_totalGainDb(totalGainDb) {
        tsf_set_output(m_synth, TSF_STEREO_INTERLEAVED, static_cast<int>(m_cfg.sampleRate), m_totalGainDb);
        tsf_set_max_voices(m_synth, m_cfg.maxVoices);
        tsf_set_cubic_interpolation(m_synth, m_cfg.cubicInterp ? 1 : 0);
        tsf_set_modwheel_depth(m_synth, m_cfg.modWheel ? m_cfg.modWheelCents : 0.0f);
        tsf_channel_set_presetnumber(m_synth, 9, 0, 1);

        if (m_cfg.dcBlocker) m_dcBlocker.init(m_cfg.sampleRate);
        m_chorus.init(m_cfg.sampleRate, m_cfg.chorusLevel, m_cfg.chorusSpeed, m_cfg.chorusDepthMs);
        m_reverb.init(m_cfg.sampleRate, m_cfg.reverbLevel, m_cfg.reverbRoom, m_cfg.reverbDamp, m_cfg.reverbWidth);

        if (m_cfg.cc111Loop) {
            for (const tml_message* m = m_head; m != nullptr; m = m->next) {
                if (m->type == TML_CONTROL_CHANGE && static_cast<unsigned char>(m->control) == 111u) {
                    if (m->next != nullptr) {
                        m_loopStartNode = m->next;
                        m_loopStartTimeMs = static_cast<double>(m->time);
                    }
                    break;
                }
            }
        }
    }

    ~MidiDecoder() override {
        if (m_head != nullptr) tml_free(m_head);
        CloseSoundFontCopy(m_synth);
    }

    uint32_t getChannelCount() override { return 2u; }
    uint32_t getSampleRate() override { return m_cfg.sampleRate; }

protected:
    uint32_t sourceChannels() const override { return 2u; }

    uint32_t decodeFloatFrames(float* dstInterleaved, uint32_t frames) override {
        if (m_synth == nullptr || m_head == nullptr) return 0;
        const double sr = static_cast<double>(m_cfg.sampleRate);
        uint32_t rendered = 0;
        while (rendered < frames) {
            if (m_cur == nullptr) {
                break;
            }
            const uint32_t chunk = (std::min)(frames - rendered, 64u);
            m_curTimeMs += (static_cast<double>(chunk) * 1000.0) / sr;
            while (m_cur != nullptr && static_cast<double>(m_cur->time) <= m_curTimeMs) {
                DispatchMidiMessage(m_synth, m_cur);
                m_cur = m_cur->next;
            }
            float* outChunk = dstInterleaved + static_cast<size_t>(rendered) * 2u;
            tsf_render_float(m_synth, outChunk, static_cast<int>(chunk), 0);
            if (m_cfg.dcBlocker) {
                m_dcBlocker.process(outChunk, chunk);
            }
            if (m_cfg.chorusLevel > 0.0001f) {
                m_chorus.process(outChunk, chunk);
            }
            if (m_cfg.reverbLevel > 0.0001f) {
                m_reverb.process(outChunk, chunk);
            }
            if (m_cfg.limiter) {
                for (uint32_t i = 0; i < chunk * 2u; ++i) {
                    outChunk[i] = SoftLimitMidiSample(outChunk[i]);
                }
            }
            rendered += chunk;
        }
        return rendered;
    }

    bool rewindStream() override {
        if (m_synth == nullptr || m_head == nullptr) return false;
        if (m_cfg.seamlessLoop) {
            for (int ch = 0; ch < 16; ++ch) {
                tsf_channel_note_off_all(m_synth, ch);
                tsf_channel_set_sustain(m_synth, ch, 0);
                tsf_channel_midi_control(m_synth, ch, 1, 0);
                tsf_channel_set_pitchwheel(m_synth, ch, 8192);
            }
            if (m_loopStartNode != nullptr) {
                m_cur = m_loopStartNode;
                m_curTimeMs = m_loopStartTimeMs;
            } else {
                m_cur = m_head;
                m_curTimeMs = 0.0;
            }
        } else {
            tsf_reset(m_synth);
            tsf_channel_set_presetnumber(m_synth, 9, 0, 1);
            m_dcBlocker.reset();
            m_chorus.reset();
            m_reverb.reset();
            m_cur = m_head;
            m_curTimeMs = 0.0;
        }
        return true;
    }

private:
    tsf* m_synth = nullptr;
    tml_message* m_head = nullptr;
    const tml_message* m_cur = nullptr;
    const tml_message* m_loopStartNode = nullptr;
    double m_loopStartTimeMs = 0.0;
    double m_curTimeMs = 0.0;
    MidiTuningConfig m_cfg;
    float m_totalGainDb = 0.0f;
    StereoDcBlocker m_dcBlocker;
    SimpleStereoChorus m_chorus;
    SimpleFreeverb m_reverb;
};

// ------------------------------------------------------------------
// Multi-channel to Stereo downmix helper for dr_mp3 / dr_wav / dr_flac
// ------------------------------------------------------------------
static void DownmixToStereo(const float* src, float* dst, uint32_t frames, uint32_t srcCh) {
    for (uint32_t f = 0; f < frames; ++f) {
        const float* s = src + static_cast<size_t>(f) * srcCh;
        float l = s[0];
        float r = s[1];
        if (srcCh >= 6) {
            // 5.1+ fold-down: L + 0.707*C + 0.5*Ls, R + 0.707*C + 0.5*Rs
            l += 0.70710678f * s[2] + 0.5f * s[4];
            r += 0.70710678f * s[2] + 0.5f * s[5];
        }
        dst[f * 2u + 0u] = l;
        dst[f * 2u + 1u] = r;
    }
}

// ------------------------------------------------------------------
// MP3 Decoder (dr_mp3)
// ------------------------------------------------------------------
class Mp3Decoder final : public EdgeAudioDecoder {
public:
    explicit Mp3Decoder(const drmp3& dec) : m_mp3(dec) {
        m_outCh = (m_mp3.channels == 1) ? 1u : 2u;
    }
    ~Mp3Decoder() override { drmp3_uninit(&m_mp3); }

    uint32_t getChannelCount() override { return m_outCh; }
    uint32_t getSampleRate() override { return m_mp3.sampleRate; }

protected:
    uint32_t sourceChannels() const override { return m_outCh; }

    uint32_t decodeFloatFrames(float* dstInterleaved, uint32_t frames) override {
        if (m_mp3.channels <= 2) {
            return static_cast<uint32_t>(drmp3_read_pcm_frames_f32(&m_mp3, frames, dstInterleaved));
        }
        m_rawTmp.resize(static_cast<size_t>(frames) * m_mp3.channels);
        const uint32_t got = static_cast<uint32_t>(drmp3_read_pcm_frames_f32(&m_mp3, frames, m_rawTmp.data()));
        DownmixToStereo(m_rawTmp.data(), dstInterleaved, got, m_mp3.channels);
        return got;
    }

    bool rewindStream() override {
        return drmp3_seek_to_pcm_frame(&m_mp3, 0) != 0;
    }

private:
    drmp3 m_mp3{};
    uint32_t m_outCh = 2u;
    std::vector<float> m_rawTmp;
};

// ------------------------------------------------------------------
// WAV Decoder (dr_wav)
// ------------------------------------------------------------------
class WavDecoder final : public EdgeAudioDecoder {
public:
    explicit WavDecoder(const drwav& dec) : m_wav(dec) {
        m_outCh = (m_wav.channels == 1) ? 1u : 2u;
    }
    ~WavDecoder() override { drwav_uninit(&m_wav); }

    uint32_t getChannelCount() override { return m_outCh; }
    uint32_t getSampleRate() override { return m_wav.sampleRate; }

protected:
    uint32_t sourceChannels() const override { return m_outCh; }

    uint32_t decodeFloatFrames(float* dstInterleaved, uint32_t frames) override {
        if (m_wav.channels <= 2) {
            return static_cast<uint32_t>(drwav_read_pcm_frames_f32(&m_wav, frames, dstInterleaved));
        }
        m_rawTmp.resize(static_cast<size_t>(frames) * m_wav.channels);
        const uint32_t got = static_cast<uint32_t>(drwav_read_pcm_frames_f32(&m_wav, frames, m_rawTmp.data()));
        DownmixToStereo(m_rawTmp.data(), dstInterleaved, got, m_wav.channels);
        return got;
    }

    bool rewindStream() override {
        return drwav_seek_to_pcm_frame(&m_wav, 0) != 0;
    }

private:
    drwav m_wav{};
    uint32_t m_outCh = 2u;
    std::vector<float> m_rawTmp;
};

// ------------------------------------------------------------------
// FLAC Decoder (dr_flac)
// ------------------------------------------------------------------
class FlacDecoder final : public EdgeAudioDecoder {
public:
    explicit FlacDecoder(drflac* dec) : m_flac(dec) {
        m_outCh = (m_flac->channels == 1) ? 1u : 2u;
    }
    ~FlacDecoder() override {
        if (m_flac != nullptr) drflac_close(m_flac);
    }

    uint32_t getChannelCount() override { return m_outCh; }
    uint32_t getSampleRate() override { return m_flac->sampleRate; }

protected:
    uint32_t sourceChannels() const override { return m_outCh; }

    uint32_t decodeFloatFrames(float* dstInterleaved, uint32_t frames) override {
        if (m_flac->channels <= 2) {
            return static_cast<uint32_t>(drflac_read_pcm_frames_f32(m_flac, frames, dstInterleaved));
        }
        m_rawTmp.resize(static_cast<size_t>(frames) * m_flac->channels);
        const uint32_t got = static_cast<uint32_t>(drflac_read_pcm_frames_f32(m_flac, frames, m_rawTmp.data()));
        DownmixToStereo(m_rawTmp.data(), dstInterleaved, got, m_flac->channels);
        return got;
    }

    bool rewindStream() override {
        return drflac_seek_to_pcm_frame(m_flac, 0) != 0;
    }

private:
    drflac* m_flac = nullptr;
    uint32_t m_outCh = 2u;
    std::vector<float> m_rawTmp;
};

// ------------------------------------------------------------------
// Windows Media Foundation Fallback (.m4a/.aac/.wma/.aif/.aiff/.opus/abs .ogg)
// ------------------------------------------------------------------
struct MfApi {
    using FnMFStartup = HRESULT(STDAPICALLTYPE*)(ULONG, DWORD);
    using FnMFCreateMediaType = HRESULT(STDAPICALLTYPE*)(IMFMediaType**);
    using FnMFCreateSourceReaderFromURL = HRESULT(STDAPICALLTYPE*)(LPCWSTR, IMFAttributes*, IMFSourceReader**);

    bool ready = false;
    FnMFStartup pMFStartup = nullptr;
    FnMFCreateMediaType pMFCreateMediaType = nullptr;
    FnMFCreateSourceReaderFromURL pMFCreateSourceReaderFromURL = nullptr;
};

static MfApi& GetMfApi() {
    static MfApi api;
    static std::once_flag once;
    std::call_once(once, []() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HMODULE hPlat = LoadLibraryA("mfplat.dll");
        HMODULE hRead = LoadLibraryA("mfreadwrite.dll");
        if (hPlat == nullptr || hRead == nullptr) return;
        api.pMFStartup = reinterpret_cast<MfApi::FnMFStartup>(GetProcAddress(hPlat, "MFStartup"));
        api.pMFCreateMediaType = reinterpret_cast<MfApi::FnMFCreateMediaType>(GetProcAddress(hPlat, "MFCreateMediaType"));
        api.pMFCreateSourceReaderFromURL =
            reinterpret_cast<MfApi::FnMFCreateSourceReaderFromURL>(GetProcAddress(hRead, "MFCreateSourceReaderFromURL"));
        if (api.pMFStartup && api.pMFCreateMediaType && api.pMFCreateSourceReaderFromURL) {
            if (SUCCEEDED(api.pMFStartup(MF_VERSION, MFSTARTUP_LITE))) {
                api.ready = true;
            }
        }
    });
    return api;
}

class MfDecoder final : public EdgeAudioDecoder {
public:
    static MfDecoder* Open(const char* fullPath) {
        MfApi& mf = GetMfApi();
        if (!mf.ready) return nullptr;
        std::wstring wpath = AcpToWide(fullPath);
        if (wpath.empty()) return nullptr;

        IMFSourceReader* reader = nullptr;
        if (FAILED(mf.pMFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader)) || reader == nullptr) {
            return nullptr;
        }

        IMFMediaType* outType = nullptr;
        if (FAILED(mf.pMFCreateMediaType(&outType)) || outType == nullptr) {
            reader->Release();
            return nullptr;
        }
        outType->SetGUID(kMF_MT_MAJOR_TYPE, kMFMediaType_Audio);
        outType->SetGUID(kMF_MT_SUBTYPE, kMFAudioFormat_PCM);
        outType->SetUINT32(kMF_MT_AUDIO_NUM_CHANNELS, 2u);
        outType->SetUINT32(kMF_MT_AUDIO_SAMPLES_PER_SECOND, kSampleRate);
        outType->SetUINT32(kMF_MT_AUDIO_BITS_PER_SAMPLE, 16u);

        HRESULT hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, outType);
        outType->Release();
        if (FAILED(hr)) {
            reader->Release();
            return nullptr;
        }

        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);
        return new MfDecoder(reader);
    }

    ~MfDecoder() override {
        if (m_reader != nullptr) {
            m_reader->Release();
            m_reader = nullptr;
        }
    }

    uint32_t getChannelCount() override { return 2u; }
    uint32_t getSampleRate() override { return kSampleRate; }

protected:
    uint32_t sourceChannels() const override { return 2u; }

    uint32_t decodeFloatFrames(float* dstInterleaved, uint32_t frames) override {
        if (m_reader == nullptr || frames == 0) return 0;
        const size_t wantFloats = static_cast<size_t>(frames) * 2u;

        while (m_fifo.size() - m_fifoHead < wantFloats && !m_eof) {
            DWORD flags = 0;
            IMFSample* sample = nullptr;
            HRESULT hr = m_reader->ReadSample(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample
            );
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
                if (sample != nullptr) sample->Release();
                m_eof = true;
                break;
            }
            if (sample != nullptr) {
                IMFMediaBuffer* buf = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf != nullptr) {
                    BYTE* pData = nullptr;
                    DWORD cbLen = 0;
                    if (SUCCEEDED(buf->Lock(&pData, nullptr, &cbLen)) && pData != nullptr && cbLen >= 4) {
                        const size_t nSamples = cbLen / sizeof(int16_t);
                        const int16_t* pcm = reinterpret_cast<const int16_t*>(pData);
                        for (size_t i = 0; i < nSamples; ++i) {
                            m_fifo.push_back(static_cast<float>(pcm[i]) / 32768.0f);
                        }
                        buf->Unlock();
                    }
                    buf->Release();
                }
                sample->Release();
            }
        }

        const size_t availFloats = m_fifo.size() - m_fifoHead;
        const uint32_t gotFrames = static_cast<uint32_t>((std::min)(wantFloats, availFloats) / 2u);
        if (gotFrames > 0) {
            const size_t gotFloats = static_cast<size_t>(gotFrames) * 2u;
            std::memcpy(dstInterleaved, m_fifo.data() + m_fifoHead, gotFloats * sizeof(float));
            m_fifoHead += gotFloats;
            if (m_fifoHead >= m_fifo.size()) {
                m_fifo.clear();
                m_fifoHead = 0;
            } else if (m_fifoHead > 16384u) {
                m_fifo.erase(m_fifo.begin(), m_fifo.begin() + static_cast<ptrdiff_t>(m_fifoHead));
                m_fifoHead = 0;
            }
        }
        return gotFrames;
    }

    bool rewindStream() override {
        if (m_reader == nullptr) return false;
        PROPVARIANT var{};
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = 0;
        const HRESULT hr = m_reader->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
        m_fifo.clear();
        m_fifoHead = 0;
        m_eof = false;
        return SUCCEEDED(hr);
    }

private:
    static constexpr uint32_t kSampleRate = 44100u;
    static constexpr float    kDefaultMidiGainDb = -1.5f;

    explicit MfDecoder(IMFSourceReader* reader) : m_reader(reader) {}

    IMFSourceReader* m_reader = nullptr;
    std::vector<float> m_fifo;
    size_t m_fifoHead = 0;
    bool m_eof = false;
};

static uint8_t __stdcall OpenMusicDecoder_Impl(uint8_t* mp, const EdgeString* pathStr) {
    if (mp == nullptr || pathStr == nullptr || pathStr->size == 0) {
        return 0;
    }
    const char* rawPtr = pathStr->c_str();
    if (rawPtr == nullptr || rawPtr[0] == '\0') {
        return 0;
    }
    std::string path(rawPtr, pathStr->size);

    // Native OggDecoder for relative .ogg paths (preserves vanilla streaming behavior 100%).
    if (!IsAbsolutePath(path) && EndsWithNoCase(path, ".ogg")) {
        std::string diskPath = ResolveFullDiskPath(path);
        if (!FileExistsA(diskPath.c_str())) {
            return 0;
        }
        return reinterpret_cast<uint8_t(__thiscall*)(void*, const EdgeString*)>(0x43ccc0)(mp, pathStr);
    }

    // Destroy any previous decoder attached to this MusicPlayer before loading our custom decoder.
    void* oldDec = *reinterpret_cast<void**>(mp + 0x40);
    if (oldDec != nullptr) {
        auto** vt = *reinterpret_cast<void***>(oldDec);
        reinterpret_cast<void(__thiscall*)(void*, int)>(vt[0])(oldDec, 1);
    }
    *reinterpret_cast<uint32_t*>(mp + 0x40) = 0;
    *reinterpret_cast<uint32_t*>(mp + 0x44) = 0;
    *reinterpret_cast<uint32_t*>(mp + 0x48) = 0;

    std::string fullPath = ResolveFullDiskPath(path);
    if (!FileExistsA(fullPath.c_str())) {
        return 0;
    }

    std::string sfPath = GetActiveSoundFontPath();
    float sfGainDb = GetActiveSoundFontGainDb();
    auto* dec = static_cast<EdgeAudioDecoder*>(CreateCustomMusicDecoder(fullPath.c_str(), sfPath.c_str(), sfGainDb));
    if (dec == nullptr) {
        return 0;
    }

    *reinterpret_cast<EdgeAudioDecoder**>(mp + 0x40) = dec;
    *reinterpret_cast<uint32_t*>(mp + 0x44) = dec->getChannelCount();
    *reinterpret_cast<uint32_t*>(mp + 0x48) = dec->getSampleRate();
    return 1;
}

static void __declspec(naked) hk_openMusicDecoder() {
    // Replaces `call 0x43ccc0` at 0x43cf31 (5B).
    // __thiscall: ecx = MusicPlayer*, [esp+4] = const EdgeString* pathStr. Callee cleans 4 bytes.
    __asm {
        pop eax
        pop edx
        push edx
        push ecx
        push eax
        jmp OpenMusicDecoder_Impl
    }
}

} // namespace

namespace edgemod {

void EnsureMidiConfigReady() {
    std::lock_guard<std::mutex> lock(g_midiCfgMutex);
    EnsureMidiConfigLoadedLocked();
}

MidiTuningConfig GetActiveMidiTuningConfig() {
    std::lock_guard<std::mutex> lock(g_midiCfgMutex);
    EnsureMidiConfigLoadedLocked();
    return g_midiCfg;
}

uint64_t GetMidiConfigRevision() {
    std::lock_guard<std::mutex> lock(g_midiCfgMutex);
    EnsureMidiConfigLoadedLocked();
    return g_midiCfgRevision;
}

bool IsSupportedMusicExtension(const char* extWithDot) {
    if (extWithDot == nullptr || extWithDot[0] != '.') return false;
    static const char* const kExts[] = {
        ".ogg", ".flac", ".wav", ".mp3", ".m4a", ".aac", ".wma", ".aif", ".aiff", ".opus", ".mid", ".midi"
    };
    for (const char* e : kExts) {
        if (_stricmp(extWithDot, e) == 0) return true;
    }
    return false;
}

void* CreateCustomMusicDecoder(const char* fullPath, const char* soundFontPath, float soundFontGainDb) {
    if (fullPath == nullptr || fullPath[0] == '\0') return nullptr;
    std::string p(fullPath);

    if (EndsWithNoCase(p, ".mid") || EndsWithNoCase(p, ".midi")) {
        const MidiTuningConfig cfg = GetActiveMidiTuningConfig();
        tml_message* head = tml_load_filename(fullPath);
        if (head == nullptr) return nullptr;

        float autoGainDb = 0.0f;
        if (cfg.autoLevel) {
            const double rawRmsDb = MeasureMidiRawRmsDb(fullPath, soundFontPath, head);
            if (rawRmsDb > -900.0) {
                autoGainDb = (std::max)(-15.0f, (std::min)(15.0f, cfg.targetDb - static_cast<float>(rawRmsDb)));
            }
        }

        tsf* synth = AcquireSoundFontCopy(soundFontPath);
        if (synth == nullptr) {
            tml_free(head);
            return nullptr;
        }
        return new MidiDecoder(synth, head, cfg, autoGainDb + cfg.gainDb + soundFontGainDb);
    }
    if (EndsWithNoCase(p, ".mp3")) {
        drmp3 mp3{};
        if (drmp3_init_file(&mp3, fullPath, nullptr) && mp3.channels > 0 && mp3.sampleRate > 0) {
            return new Mp3Decoder(mp3);
        }
        return nullptr;
    }
    if (EndsWithNoCase(p, ".wav")) {
        drwav wav{};
        if (drwav_init_file(&wav, fullPath, nullptr) && wav.channels > 0 && wav.sampleRate > 0) {
            return new WavDecoder(wav);
        }
        return nullptr;
    }
    if (EndsWithNoCase(p, ".flac")) {
        drflac* flac = drflac_open_file(fullPath, nullptr);
        if (flac != nullptr) {
            if (flac->channels > 0 && flac->sampleRate > 0) {
                return new FlacDecoder(flac);
            }
            drflac_close(flac);
        }
        return nullptr;
    }
    return MfDecoder::Open(fullPath);
}

} // namespace edgemod

void RegisterMusicDecoders() {
    // MusicPlayer::play -> MusicPlayer::openDecoder (0x43cf31, 5B)
    static const uint8_t expOpenDec[5] = {0xe8, 0x8a, 0xfd, 0xff, 0xff};
    RegRelCall(0x43cf31, expOpenDec, 5, 5, &hk_openMusicDecoder);
}
