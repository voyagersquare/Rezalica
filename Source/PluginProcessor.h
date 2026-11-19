#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "SamplerEngine.h"
#include "Sequencer.h"
#include "FxChain.h"

class RezalicaProcessor : public juce::AudioProcessor,
                          private juce::Timer
{
public:
    static constexpr int midiBaseNote = 36; // C1 = A1 ... D#2 = A16, E2 = B1 ... D#7 = D16

    RezalicaProcessor();
    ~RezalicaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API used by the editor
    enum ChopMode { chop2 = 2, chop4 = 4, chop8 = 8, chop16 = 16, chopTransients = 0, chopManual = -1, chopOff = -2 };
    static constexpr int maxSlices = 64;

    bool loadFileToPad (int pad, const juce::File& file);
    bool chopFileToPads (const juce::File& file, int mode);   // file becomes the chop source
    bool loadFileForChopping (const juce::File& file);        // loads to the selected pad, not chopped until a mode is chosen
    void chopPadSample (int pad, int mode);                   // pad's sample becomes the chop source
    void setChopMode (int mode);                              // re-chops the selected pad's sample automatically
    void setTransientSensitivity (float sensitivity01);       // re-chops when in transient mode
    void clearPad (int pad);
    bool trimPad (int pad); // keeps only start..end of the pad's sample (new sample, saved as WAV)
    static juce::File getTrimFolder();

    // Time stretch / warp (applied to the selected pad's sample, or only its region)
    bool setWarp (int pad, rz::WarpSettings settings);   // settings.enabled = false removes the warp
    rz::WarpSettings getWarpSettings (int pad) const;    // current settings of the pad (or the last used ones)
    bool isPadWarped (int pad) const;
    double getWarpTempo (const rz::WarpSettings& s) const;
    void updateWarpTempo();                              // re-stretches synced samples when the DAW tempo changes
    static juce::File getWarpFolder();

    // manual chop markers (switch the chop mode to Manual)
    void addSlicePoint (float normalisedPosition);
    void removeSlicePoint (int index);
    int moveSlicePoint (int index, float normalisedPosition);
    void clearSlicePoints();

    // Lazy Chop: plays the selected pad's whole sample, every pad hit drops a slice at the playhead
    bool startLazyChop();
    void stopLazyChop();
    bool isLazyChopActive() const noexcept { return lazyActive.load() || lazyStarting; }
    void updateLazyChop(); // call regularly from the UI (message thread)

    int getChopMode() const noexcept               { return chopMode; }
    float getTransientSensitivity() const noexcept { return transientSensitivity; }
    int getNumSlices() const noexcept              { return (int) slicePoints.size(); }
    rz::SampleData::Ptr getChopSource() const      { return chopSource; }
    const std::vector<float>& getSlicePoints() const noexcept { return slicePoints; }
    juce::String getPadName (int pad) const;

    void triggerPadFromUi (int pad, float velocity, bool isNoteOn);
    void stopAll() { triggerPadFromUi (-1, 0.0f, false); }

    rz::SamplerEngine& getEngine() noexcept { return engine; }
    rz::Sequencer& getSequencer() noexcept { return sequencer; }
    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }
    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }
    static const juce::StringArray& getDelayDivisionNames();

    std::atomic<int> selectedPad { 0 };
    std::atomic<int> currentBank { 0 }; // bank shown in the UI and played by the computer keyboard
    juce::ChangeBroadcaster padsChanged; // pad samples / settings replaced

private:
    struct UiEvent { int pad; float velocity; bool on; };
    struct Event
    {
        int offset;
        int type; // 0 = off, 1 = off (seq), 2 = on, 3 = on (seq), -1 = stop all
        int pad;
        float velocity;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static std::vector<float> findSlicePoints (const rz::SampleData& s, int mode, float sensitivity);

    void timerCallback() override;
    rz::SampleData::Ptr loadSample (const juce::File& file);
    rz::SampleData::Ptr renderWarp (const rz::SampleData::Ptr& source, float regionStart, float regionEnd,
                                    const rz::WarpSettings& settings, double bpm, bool saveInBackground = true);
    void replaceSampleOnPads (const rz::SampleData::Ptr& from, const rz::SampleData::Ptr& to);
    void saveInBackground (const rz::SampleData::Ptr& s);

    void rechop();
    void applySlices();
    void prepareManualSource();
    float snapToZeroCrossing (float normalisedPosition) const;
    void addEvent (int offset, int type, int pad, float velocity);
    rz::FxChain::Params readFxParams (int pad, bool padSounding) const;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formatManager;

    rz::SamplerEngine engine;
    rz::Sequencer sequencer;
    std::array<rz::FxChain, rz::numPads> padFx;             // every pad has its own effect chain
    std::array<juce::AudioBuffer<float>, rz::numPads> padBuses;
    std::array<double, rz::numPads> padFxIdleSamples {};   // how long a pad's chain has been silent
    std::array<bool, rz::numPads> padFxWasRunning {};

    // chop state (message thread)
    rz::SampleData::Ptr chopSource, previousChopSource;
    void setChopSource (rz::SampleData::Ptr s);
    int chopMode = chopOff;
    float transientSensitivity = 0.5f;
    std::vector<float> slicePoints;

    std::atomic<bool> lazyActive { false };
    bool lazyStarting = false;
    juce::AbstractFifo lazyFifo { 128 };
    std::array<float, 128> lazyHits {};

    juce::ReferenceCountedArray<rz::SampleData> samplePool; // owns all loaded samples (message thread)

    juce::CriticalSection missingPathLock;
    std::array<juce::String, rz::numPads> missingPaths; // remembered for files that could not be found

    juce::AbstractFifo uiFifo { 256 };
    std::array<UiEvent, 256> uiEvents {};

    std::array<Event, 1024> events {};
    int numEvents = 0;
    std::array<double, rz::numPads> pendingSeqOff {};

    juce::AudioBuffer<float> dryBuffer;
    double currentSampleRate = 44100.0;

    std::atomic<float>* pMaster = nullptr;

    rz::WarpSettings lastWarpSettings;

    // writes stretched samples to disk in the background (declared last: stopped first)
    juce::ThreadPool fileWriter { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RezalicaProcessor)
};
