#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

namespace rz
{
constexpr int padsPerBank = 16;
constexpr int numBanks = 4;                       // banks A-D
constexpr int numPads = padsPerBank * numBanks;   // 64 pads in total
constexpr int maxVoices = 32;

/** "A1" ... "D16" */
inline juce::String padLabel (int pad)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + pad / padsPerBank)) + juce::String (pad % padsPerBank + 1);
}

/** One loaded audio file. Always stored as 2 channels at its original sample rate. */
struct SampleData : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    juce::String name;
    juce::File file;
};

/** Per-pad settings. Written by the UI, read by the audio thread. */
struct PadSettings
{
    std::atomic<float> start { 0.0f };     // normalised 0..1 position in the sample
    std::atomic<float> end { 1.0f };
    std::atomic<float> gainDb { 0.0f };
    std::atomic<float> pan { 0.0f };       // -1..1
    std::atomic<float> pitch { 0.0f };     // semitones
    std::atomic<float> attackMs { 1.0f };
    std::atomic<float> releaseMs { 30.0f };
    std::atomic<bool> loop { false };
    std::atomic<bool> reverse { false };
    std::atomic<bool> gate { false };      // plays only while the pad is held
    std::atomic<bool> fx { true };         // routed through the effect chain
    std::atomic<bool> mute { false };

    void resetPlayback()
    {
        gainDb = 0.0f; pan = 0.0f; pitch = 0.0f; attackMs = 1.0f; releaseMs = 30.0f;
        loop = false; reverse = false; gate = false; fx = true; mute = false;
    }
};

class Voice
{
public:
    void start (int padIndex, SampleData::Ptr s, const PadSettings& ps, float velocity, double hostRate, juce::uint32 order);
    void release();
    void fastStop();
    void render (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& fx, int startSample, int numSamples);

    bool isActive() const noexcept     { return active; }
    bool isReleasing() const noexcept  { return stage == Stage::release; }
    int getPad() const noexcept        { return pad; }
    juce::uint32 getAge() const noexcept { return age; }
    float getNormalisedPosition() const noexcept;

private:
    enum class Stage { attack, sustain, release };

    void stop();

    SampleData::Ptr sample;
    const PadSettings* settings = nullptr;
    int pad = -1;
    juce::uint32 age = 0;
    bool active = false, reverse = false, loop = false, toFx = true;
    int s0 = 0, s1 = 0;
    double pos = 0.0, baseRatio = 1.0, sr = 44100.0;
    float vel = 1.0f, env = 0.0f, attackInc = 1.0f, releaseDec = 0.001f;
    Stage stage = Stage::sustain;
};

class SamplerEngine
{
public:
    SamplerEngine();

    void prepare (double sampleRate);

    // ---- message thread ----
    void setPadSample (int pad, SampleData::Ptr s);
    SampleData::Ptr getPadSample (int pad) const;
    PadSettings& getPad (int pad) noexcept { return settings[(size_t) pad]; }

    // ---- audio thread ----
    void syncSamples();
    void noteOn (int pad, float velocity, bool fromSequencer);
    void noteOff (int pad, bool fromSequencer);
    void allNotesOff();
    void render (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& fx, int startSample, int numSamples);
    void updatePlayPositions();

    // ---- preview voice (plays a whole sample, used by Lazy Chop) ----
    void setPreviewSample (SampleData::Ptr s);       // message thread
    void startPreview()  { previewCommand = 1; }     // any thread
    void stopPreview()   { previewCommand = 2; }     // any thread
    bool isPreviewBusy() const { return previewCommand.load() == 1 || previewVoice.isActive(); }
    float getPreviewPositionNow() const { return previewVoice.getNormalisedPosition(); } // audio thread
    std::atomic<float> previewPosition { -1.0f };   // for the UI

    // ---- for the UI ----
    std::array<std::atomic<int>, numPads> hitCounter;
    std::array<std::atomic<float>, numPads> playPosition; // -1 when not playing
    std::atomic<int> lastPlayedPad { -1 };   // last pad hit by MIDI / keyboard / mouse (not the sequencer)
    std::atomic<int> playedCounter { 0 };

private:
    bool isPadPlaying (int pad) const;

    std::array<PadSettings, numPads> settings;
    std::array<Voice, maxVoices> voices;

    mutable juce::SpinLock lock;
    std::array<SampleData::Ptr, numPads> pads;       // guarded by lock
    std::array<SampleData::Ptr, numPads> audioPads;  // audio thread copy
    SampleData::Ptr previewSample;                   // guarded by lock
    Voice previewVoice;
    PadSettings previewSettings;
    std::atomic<int> previewCommand { 0 };
    std::atomic<int> version { 0 };
    int lastVersion = -1;

    double sampleRate = 44100.0;
    juce::uint32 voiceCounter = 0;
};
} // namespace rz
