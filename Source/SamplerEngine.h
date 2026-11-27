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
constexpr int numBuses = 5;                       // mixer: 0 = MASTER, 1..4 = BUS A..D
constexpr int deckChain = numBuses;               // the DECK's own 4 effect slots (after the buses in the processor)
constexpr int padChainBase = numBuses + 1;        // then every pad's own 6 effect slots (pad i = padChainBase + i)
constexpr int numEffectChains = numBuses + 1 + numPads;

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

    /** Warp markers: (source position, target position), both 0..1 of the stretched region.
        Between markers the audio is stretched / squeezed so each marker lands on its target. */
    std::vector<std::pair<float, float>> markers;

    double lengthBeats() const { return lengthInBeats (lengthIndex); }

    /** Piecewise-linear time map through (0,0), the markers and (1,1). */
    float srcToDst (float u) const { return mapThrough (u, false); }
    float dstToSrc (float v) const { return mapThrough (v, true); }

    float mapThrough (float x, bool inverse) const
    {
        float x0 = 0.0f, y0 = 0.0f;
        for (size_t i = 0; i <= markers.size(); ++i)
        {
            const float sx = i < markers.size() ? markers[i].first : 1.0f;
            const float sy = i < markers.size() ? markers[i].second : 1.0f;
            const float ax = inverse ? sy : sx, ay = inverse ? sx : sy;
            if (x <= ax || i == markers.size())
            {
                const float span = ax - x0;
                return span > 1.0e-9f ? y0 + (x - x0) / span * (ay - y0) : ay;
            }
            x0 = ax;
            y0 = ay;
        }
        return x;
    }

    juce::String markersToString() const
    {
        juce::StringArray a;
        for (auto& m : markers) a.add (juce::String (m.first, 7) + ":" + juce::String (m.second, 7));
        return a.joinIntoString (",");
    }

    void markersFromString (const juce::String& str)
    {
        markers.clear();
        for (auto& t : juce::StringArray::fromTokens (str, ",", {}))
            if (t.containsChar (':'))
                markers.push_back ({ t.upToFirstOccurrenceOf (":", false, false).getFloatValue(),
                                     t.fromFirstOccurrenceOf (":", false, false).getFloatValue() });
    }

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
        t.setProperty ("markers", markersToString(), nullptr);
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
        w.markersFromString (t.getProperty ("markers").toString());
        return w;
    }

    bool sameRenderAs (const WarpSettings& o) const
    {
        return mode == o.mode && lengthIndex == o.lengthIndex && wholeSample == o.wholeSample
            && preserve == o.preserve && loopMode == o.loopMode && std::abs (envelope - o.envelope) < 0.01f
            && markersToString() == o.markersToString();
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

    /** An independent copy (own object, same audio): edits / warps of one never touch the other. */
    Ptr clone() const
    {
        Ptr c = new SampleData();
        c->buffer = buffer;
        c->sampleRate = sampleRate;
        c->name = name;
        c->file = file;
        c->warpSource = warpSource != nullptr ? warpSource->clone() : nullptr;
        c->regionStart = regionStart;
        c->regionEnd = regionEnd;
        c->warp = warp;
        c->renderedBpm = renderedBpm;
        c->renderedFile = renderedFile;
        return c;
    }
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

/** Per-pad LEVELER: a leveling amplifier (compressor) in the style of the classic program-dependent
    tube levelers. INPUT drives it into compression, OUTPUT sets the level after it. */
struct PadLeveler
{
    std::atomic<bool> on { false };
    std::atomic<float> input { 6.0f };      // 0 .. 20 (6 = unity gain into the compressor)
    std::atomic<float> output { 10.0f };    // 0 .. 20 (10 = 0 dB)
    std::atomic<int> mode { 0 };            // 0 = SINGLE, 1 = DOUBLE, 2 = TRIPLE (release stages)
    std::atomic<float> recovery { 0.5f };   // 0 = SLOW .. 1 = FAST
    // extras the original does not have
    std::atomic<int> attack { 0 };          // 0 = AUTO (program), 1..6 = 0.3, 1, 3, 10, 30, 100 ms
    std::atomic<int> ratio { 0 };           // 0 = AUTO (grows with level, 2:1 .. 8:1), 1..4 = 2:1, 4:1, 8:1, 20:1
    std::atomic<float> mix { 1.0f };        // 0 = dry .. 1 = all compressed (parallel compression)
    std::atomic<int> character { 0 };       // 0 = TUBE (vari-mu style), 1 = DIODE (the diode-shunt compressor)

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("LEVELER");
        t.setProperty ("on", on.load(), nullptr);
        t.setProperty ("input", input.load(), nullptr);
        t.setProperty ("output", output.load(), nullptr);
        t.setProperty ("mode", mode.load(), nullptr);
        t.setProperty ("recovery", recovery.load(), nullptr);
        t.setProperty ("attack", attack.load(), nullptr);
        t.setProperty ("ratio", ratio.load(), nullptr);
        t.setProperty ("mix", mix.load(), nullptr);
        t.setProperty ("character", character.load(), nullptr);
        return t;
    }
    void fromValueTree (const juce::ValueTree& t)
    {
        on = (bool) t.getProperty ("on", false);
        input = juce::jlimit (0.0f, 20.0f, (float) t.getProperty ("input", 6.0f));
        output = juce::jlimit (0.0f, 20.0f, (float) t.getProperty ("output", 10.0f));
        mode = juce::jlimit (0, 2, (int) t.getProperty ("mode", 0));
        recovery = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("recovery", 0.5f));
        attack = juce::jlimit (0, 6, (int) t.getProperty ("attack", 0));
        ratio = juce::jlimit (0, 4, (int) t.getProperty ("ratio", 0));
        mix = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("mix", 1.0f));
        character = juce::jlimit (0, 1, (int) t.getProperty ("character", 0));
    }
    void reset() { fromValueTree (juce::ValueTree ("LEVELER")); }
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
    std::atomic<int> chokeGroup { 0 };     // 1..8: a hit stops the other pads of the same group (0 = none)
    std::atomic<int> outputBus { -1 };     // mixer bus: -1 = like its bank, 0 = MASTER, 1..4 = BUS A..D
    PadFx effects;                         // this pad's own effect chain settings
    PadLeveler leveler;                    // this pad's LEVELER (compressor)

    void resetPlayback()
    {
        gainDb = 0.0f; pan = 0.0f; pitch = 0.0f; attackMs = 1.0f; releaseMs = 30.0f;
        loop = false; reverse = false; gate = false; mute = false; chokeGroup = 0; outputBus = -1;
        effects.reset();
        leveler.reset();
    }
};

class Voice
{
public:
    /** skipHostSamples: start that far into the sound (a note the playback jumped into). */
    void start (int padIndex, SampleData::Ptr s, const PadSettings& ps, float velocity, double hostRate, juce::uint32 order,
                float transposeSemitones = 0.0f, int noteTag = -1, double skipHostSamples = 0.0);
    int getTag() const noexcept { return tag; }
    void release();
    void fastStop();
    /** Adds the voice into 'out' (its mixer bus). */
    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples, float bankGain = 1.0f);
    float peakL = 0.0f, peakR = 0.0f;   // the loudest sample of the last render (for the pad's INPUT meter)

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
    float transpose = 0.0f;
    int tag = -1;
    float lastBankGain = -1.0f; // ramps bank volume changes smoothly
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
    /** transpose: semitones on top of the pad's pitch (piano roll / chromatic MIDI).
        tag >= 0 identifies the note for noteOffTag(); poly = several notes of this pad may sound at once. */
    void noteOn (int pad, float velocity, bool fromSequencer, float transpose = 0.0f, int tag = -1, bool poly = false,
                 double skipHostSamples = 0.0);
    void noteOff (int pad, bool fromSequencer);
    /** Ends the note with this tag. force: always release, else only if the pad is in gate / loop mode. */
    void noteOffTag (int tag, bool force);
    void allNotesOff();
    /** STOP: every voice fades out with its pad's RELEASE (also one-shot samples). */
    void releaseAll();
    /** Renders every voice into the mixer bus of its pad (buses[0] = MASTER). */
    void render (std::array<juce::AudioBuffer<float>, numBuses>& buses, int startSample, int numSamples);
    /** Audio thread: a pad with its own effects plays into this buffer instead of its bus (nullptr = the bus). */
    std::array<juce::AudioBuffer<float>*, numPads> padTarget {};
    /** The mixer bus a pad plays into: its own choice, else its bank's. */
    int busForPad (int pad) const noexcept
    {
        int b = settings[(size_t) pad].outputBus.load();
        if (b < 0) b = bankBus[(size_t) (pad / padsPerBank)].load();
        return juce::jlimit (0, numBuses - 1, b);
    }
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
    // whole-bank volume (linear) and mute
    std::array<std::atomic<float>, numBanks> bankGain;
    std::array<std::atomic<bool>, numBanks> bankMute;
    std::array<std::atomic<int>, numBanks> bankBus;    // mixer bus of a whole bank (0 = MASTER)

    std::atomic<int> lastPlayedPad { -1 };   // last pad hit by MIDI / keyboard / mouse (not the sequencer)
    std::atomic<int> playedCounter { 0 };
    std::atomic<int> lastSequencedPad { -1 };   // last pad hit by the step sequencer / piano roll
    std::atomic<int> sequencedCounter { 0 };
    /** Audio thread: the loudest sample each pad played this block (before its LEVELER / effects). */
    std::array<float, numPads> padPeakL {}, padPeakR {};
    void clearPadPeaks() { padPeakL.fill (0.0f); padPeakR.fill (0.0f); }

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
