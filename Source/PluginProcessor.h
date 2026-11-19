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
    static constexpr int midiBaseNote = 36; // C1 = pad 1 ... D#2 = pad 16

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
    double getTailLengthSeconds() const override { return 2.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API used by the editor
    enum ChopMode { chop4 = 4, chop8 = 8, chop16 = 16, chopTransients = 0 };

    bool loadFileToPad (int pad, const juce::File& file);
    bool chopFileToPads (const juce::File& file, int mode);
    void chopPadSample (int pad, int mode);
    void clearPad (int pad);
    juce::String getPadName (int pad) const;

    void triggerPadFromUi (int pad, float velocity, bool isNoteOn);
    void stopAll() { triggerPadFromUi (-1, 0.0f, false); }

    rz::SamplerEngine& getEngine() noexcept { return engine; }
    rz::Sequencer& getSequencer() noexcept { return sequencer; }
    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }
    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }

    std::atomic<int> selectedPad { 0 };
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
    static std::vector<float> findSlicePoints (const rz::SampleData& s, int mode);

    void timerCallback() override;
    rz::SampleData::Ptr loadSample (const juce::File& file);
    void applySlices (const rz::SampleData::Ptr& s, const std::vector<float>& points, int firstPad);
    void addEvent (int offset, int type, int pad, float velocity);
    rz::FxChain::Params readFxParams() const;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formatManager;

    rz::SamplerEngine engine;
    rz::Sequencer sequencer;
    rz::FxChain fx;

    juce::ReferenceCountedArray<rz::SampleData> samplePool; // owns all loaded samples (message thread)

    juce::CriticalSection missingPathLock;
    std::array<juce::String, rz::numPads> missingPaths; // remembered for files that could not be found

    juce::AbstractFifo uiFifo { 256 };
    std::array<UiEvent, 256> uiEvents {};

    std::array<Event, 1024> events {};
    int numEvents = 0;
    std::array<double, rz::numPads> pendingSeqOff {};

    juce::AudioBuffer<float> dryBuffer, fxBuffer;
    double currentSampleRate = 44100.0;

    std::atomic<float>* pMaster = nullptr;
    std::atomic<float>* pFilterOn = nullptr; std::atomic<float>* pCutoff = nullptr;
    std::atomic<float>* pReso = nullptr; std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pLofiOn = nullptr; std::atomic<float>* pBits = nullptr;
    std::atomic<float>* pRate = nullptr; std::atomic<float>* pNoise = nullptr;
    std::atomic<float>* pDelayOn = nullptr; std::atomic<float>* pDelayDiv = nullptr;
    std::atomic<float>* pDelayTime = nullptr; std::atomic<float>* pFeedback = nullptr;
    std::atomic<float>* pDelayMix = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RezalicaProcessor)
};
