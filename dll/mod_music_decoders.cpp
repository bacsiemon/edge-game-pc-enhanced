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

static std::mutex g_autoGainMutex;
static std::unordered_map<std::string, float> g_midiAutoGainCache;

static float ComputeMidiAutoGainDb(const char* midiPath, const char* sfPath, const tml_message* head) {
    if (head == nullptr || sfPath == nullptr || sfPath[0] == '\0') return 0.0f;

    std::string cacheKey = BuildFileTimestampTag(midiPath) + "||" + BuildFileTimestampTag(sfPath);
    if (!cacheKey.empty()) {
        std::lock_guard<std::mutex> lock(g_autoGainMutex);
        auto it = g_midiAutoGainCache.find(cacheKey);
        if (it != g_midiAutoGainCache.end()) {
            return it->second;
        }
    }

    tsf* probe = AcquireSoundFontCopy(sfPath);
    if (probe == nullptr) return 0.0f;

    constexpr uint32_t kProbeRate = 11025u;
    constexpr uint32_t kMaxProbeFrames = 30u * kProbeRate;
    constexpr float kTargetMidiRmsDb = -17.0f;

    tsf_set_output(probe, TSF_STEREO_INTERLEAVED, kProbeRate, 0.0f);
    tsf_set_max_voices(probe, 128);
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

    float autoGainDb = 0.0f;
    if (meanSq > 1e-9) {
        const double rmsDb = 10.0 * std::log10(meanSq);
        autoGainDb = (std::max)(-15.0f, (std::min)(15.0f, kTargetMidiRmsDb - static_cast<float>(rmsDb)));
    }

    if (!cacheKey.empty()) {
        std::lock_guard<std::mutex> lock(g_autoGainMutex);
        g_midiAutoGainCache[cacheKey] = autoGainDb;
    }
    return autoGainDb;
}

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
    MidiDecoder(tsf* synth, tml_message* head, float totalGainDb)
        : m_synth(synth), m_head(head), m_cur(head), m_totalGainDb(totalGainDb) {
        tsf_set_output(m_synth, TSF_STEREO_INTERLEAVED, kSampleRate, m_totalGainDb);
        tsf_set_max_voices(m_synth, 128);
        tsf_channel_set_presetnumber(m_synth, 9, 0, 1);
    }

    ~MidiDecoder() override {
        if (m_head != nullptr) tml_free(m_head);
        CloseSoundFontCopy(m_synth);
    }

    uint32_t getChannelCount() override { return 2u; }
    uint32_t getSampleRate() override { return kSampleRate; }

protected:
    uint32_t sourceChannels() const override { return 2u; }

    uint32_t decodeFloatFrames(float* dstInterleaved, uint32_t frames) override {
        if (m_synth == nullptr || m_head == nullptr) return 0;
        uint32_t rendered = 0;
        while (rendered < frames) {
            if (m_cur == nullptr) {
                break;
            }
            const uint32_t chunk = (std::min)(frames - rendered, 64u);
            m_curTimeMs += (static_cast<double>(chunk) * 1000.0) / static_cast<double>(kSampleRate);
            while (m_cur != nullptr && static_cast<double>(m_cur->time) <= m_curTimeMs) {
                DispatchMidiMessage(m_synth, m_cur);
                m_cur = m_cur->next;
            }
            tsf_render_float(m_synth, dstInterleaved + static_cast<size_t>(rendered) * 2u, static_cast<int>(chunk), 0);
            float* outChunk = dstInterleaved + static_cast<size_t>(rendered) * 2u;
            for (uint32_t i = 0; i < chunk * 2u; ++i) {
                outChunk[i] = SoftLimitMidiSample(outChunk[i]);
            }
            rendered += chunk;
        }
        return rendered;
    }

    bool rewindStream() override {
        if (m_synth == nullptr || m_head == nullptr) return false;
        tsf_reset(m_synth);
        tsf_channel_set_presetnumber(m_synth, 9, 0, 1);
        m_cur = m_head;
        m_curTimeMs = 0.0;
        return true;
    }

private:
    static constexpr uint32_t kSampleRate = 44100u;

    tsf* m_synth = nullptr;
    tml_message* m_head = nullptr;
    tml_message* m_cur = nullptr;
    double m_curTimeMs = 0.0;
    float m_totalGainDb = 0.0f;
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
        tml_message* head = tml_load_filename(fullPath);
        if (head == nullptr) return nullptr;
        const float autoGainDb = ComputeMidiAutoGainDb(fullPath, soundFontPath, head);
        tsf* synth = AcquireSoundFontCopy(soundFontPath);
        if (synth == nullptr) {
            tml_free(head);
            return nullptr;
        }
        return new MidiDecoder(synth, head, autoGainDb + soundFontGainDb);
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
