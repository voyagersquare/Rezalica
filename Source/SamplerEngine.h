#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
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

/** Time stretch / warp settings of a sample. */
struct WarpSettings
{
    enum Mode { beats = 0, complex = 1 };
    static const juce::StringArray& lengthNames()
    {
        static const juce::StringArray n { "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" };
        return n;
    }
    static double lengthInBeats (int index) { static const double b[] { 1, 2, 4, 8, 16, 32, 64 }; return b[juce::jlimit (0, 6, index)]; }

    bool enabled = false;
    int mode = beats;
    int lengthIndex = 2;        // 1 bar
    bool syncHost = true;       // use the DAW tempo
    float bpm = 120.0f;         // used when not synced
    bool wholeSample = true;    // false: only the selected pad's region
    int preserve = 0;           // Beats: 0 transients, 1 = 1/8, 2 = 1/16
    int loopMode = 1;           // Beats: 0 off, 1 loop forward, 2 loop back-and-forth
    float envelope = 100.0f;    // Beats: transient envelope, 100 = no fade

    double lengthBeats() const { return lengthInBeats (lengthIndex); }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("WARP");
        t.setProperty ("enabled", enabled, nullptr);
        t.setProperty ("mode", mode, nullptr);
        t.setProperty ("length", lengthIndex, nullptr);
        t.setProperty ("sync", syncHost, nullptr);
        t.setProperty ("bpm", bpm, nullptr);
        t.setProperty ("whole", wholeSample, nullptr);
        t.setProperty ("preserve", preserve, nullptr);
        t.setProperty ("loopMode", loopMode, nullptr);
        t.setProperty ("envelope", envelope, nullptr);
        return t;
    }

    static WarpSettings fromValueTree (const juce::ValueTree& t)
    {
        WarpSettings w;
        w.enabled = (bool) t.getProperty ("enabled", false);
        w.mode = (int) t.getProperty ("mode", 0);
        w.lengthIndex = (int) t.getProperty ("length", 2);
        w.syncHost = (bool) t.getProperty ("sync", true);
        w.bpm = (float) t.getProperty ("bpm", 120.0f);
        w.wholeSample = (bool) t.getProperty ("whole", true);
        w.preserve = (int) t.getProperty ("preserve", 0);
        w.loopMode = (int) t.getProperty ("loopMode", 1);
        w.envelope = (float) t.getProperty ("envelope", 100.0f);
        return w;
    }

    bool sameRenderAs (const WarpSettings& o) const
    {
        return mode == o.mode && lengthIndex == o.lengthIndex && wholeSample == o.wholeSample
            && preserve == o.preserve && loopMode == o.loopMode && std::abs (envelope - o.envelope) < 0.01f;
    }
};

/** One loaded audio file. Always stored as 2 channels at its original sample rate. */
struct SampleData : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    juce::String name;
    juce::File file;

    // set when this sample is a time-stretched (warped) version of another one
    Ptr warpSource;
    float regionStart = 0.0f, regionEnd = 1.0f; // part of warpSource that was stretched
    WarpSettings warp;
    double renderedBpm = 0.0;
    juce::File renderedFile;                    // WAV written in the background

    bool isWarped() const noexcept { return warpSource != nullptr; }
};

/** Per-pad effect settings (each pad has its own effect chain). */
struct PadFx
{
    std::atomic<bool> filterOn { false }, satOn { false }, delayOn { false }, reverbOn { false };
    std::atomic<int> filterType { 0 };        // LP24, LP12, HP24, HP12, BP24, BP12
    std::atomic<float> cutoff { 20000.0f }, resonance { 0.0f };
    std::atomic<float> tube { 0.0f }, tape { 0.0f }, solid { 0.0f }; // saturation amounts, 0 = off
    std::atomic<int> delayDiv { 2 };          // index into the delay sync divisions, 0 = free (ms)
    std::atomic<float> delayTime { 375.0f }, feedback { 0.35f }, delayMix { 0.3f };
    std::atomic<float> reverbSize { 0.6f }, reverbDamp { 0.4f }, reverbWidth { 1.0f }, reverbMix { 0.3f };

    bool anyOn() const noexcept { return filterOn.load() || satOn.load() || delayOn.load() || reverbOn.load(); }
    bool hasTail() const noexcept { return delayOn.load() || reverbOn.load(); }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("FX");
        t.setProperty ("filterOn", filterOn.load(), nullptr);
        t.setProperty ("filterType", filterType.load(), nullptr);
        t.setProperty ("cutoff", cutoff.load(), nullptr);
        t.setProperty ("resonance", resonance.load(), nullptr);
        t.setProperty ("satOn", satOn.load(), nullptr);
        t.setProperty ("tube", tube.load(), nullptr);
        t.setProperty ("tape", tape.load(), nullptr);
        t.setProperty ("solid", solid.load(), nullptr);
        t.setProperty ("delayOn", delayOn.load(), nullptr);
        t.setProperty ("delayDiv", delayDiv.load(), nullptr);
        t.setProperty ("delayTime", delayTime.load(), nullptr);
        t.setProperty ("feedback", feedback.load(), nullptr);
        t.setProperty ("delayMix", delayMix.load(), nullptr);
        t.setProperty ("reverbOn", reverbOn.load(), nullptr);
        t.setProperty ("reverbSize", reverbSize.load(), nullptr);
        t.setProperty ("reverbDamp", reverbDamp.load(), nullptr);
        t.setProperty ("reverbWidth", reverbWidth.load(), nullptr);
        t.setProperty ("reverbMix", reverbMix.load(), nullptr);
        return t;
    }

    void fromValueTree (const juce::ValueTree& t)
    {
        filterOn = (bool) t.getProperty ("filterOn", false);
        filterType = (int) t.getProperty ("filterType", 0);
        cutoff = (float) t.getProperty ("cutoff", 20000.0f);
        resonance = (float) t.getProperty ("resonance", 0.0f);
        satOn = (bool) t.getProperty ("satOn", false);
        tube = (float) t.getProperty ("tube", 0.0f);
        tape = (float) t.getProperty ("tape", 0.0f);
        solid = (float) t.getProperty ("solid", 0.0f);
        delayOn = (bool) t.getProperty ("delayOn", false);
        delayDiv = (int) t.getProperty ("delayDiv", 2);
        delayTime = (float) t.getProperty ("delayTime", 375.0f);
        feedback = (float) t.getProperty ("feedback", 0.35f);
        delayMix = (float) t.getProperty ("delayMix", 0.3f);
        reverbOn = (bool) t.getProperty ("reverbOn", false);
        reverbSize = (float) t.getProperty ("reverbSize", 0.6f);
        reverbDamp = (float) t.getProperty ("reverbDamp", 0.4f);
        reverbWidth = (float) t.getProperty ("reverbWidth", 1.0f);
        reverbMix = (float) t.getProperty ("reverbMix", 0.3f);
    }

    void reset() { fromValueTree (juce::ValueTree ("FX")); }
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
    std::atomic<bool> loop { false };      // loops start..end while the pad is held
    std::atomic<bool> reverse { false };
    std::atomic<bool> gate { false };      // plays only while the pad is held
    std::atomic<bool> mute { false };
    PadFx effects;                         // this pad's own effect chain settings

    void resetPlayback()
    {
        gainDb = 0.0f; pan = 0.0f; pitch = 0.0f; attackMs = 1.0f; releaseMs = 30.0f;
        loop = false; reverse = false; gate = false; mute = false;
        effects.reset();
    }
};

class Voice
{
public:
    void start (int padIndex, SampleData::Ptr s, const PadSettings& ps, float velocity, double hostRate, juce::uint32 order);
    void release();
    void fastStop();
    /** Writes into padBus when the pad has effects switched on, else into dry. */
    void render (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& padBus, int startSample, int numSamples);

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
    bool active = false, reverse = false, loop = false;
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
    const PadSettings& getPad (int pad) const noexcept { return settings[(size_t) pad]; }

    // ---- audio thread ----
    void syncSamples();
    void noteOn (int pad, float velocity, bool fromSequencer);
    void noteOff (int pad, bool fromSequencer);
    void allNotesOff();
    void render (juce::AudioBuffer<float>& dry, std::array<juce::AudioBuffer<float>, numPads>& padBuses, int startSample, int numSamples);
    void updatePlayPositions();
    bool isPadSounding (int pad) const; // any voice (incl. releasing) on this pad

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
