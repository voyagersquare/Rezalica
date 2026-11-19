#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "PadButton.h"
#include "WaveformView.h"
#include "SequencerWindow.h"

class RezalicaEditor : public juce::AudioProcessorEditor,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit RezalicaEditor (RezalicaProcessor&);
    ~RezalicaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void selectPad (int pad, bool grabFocus = true);
    void setBank (int bank);
    void refreshChopControls();
    void refreshPadControls();
    void refreshPadNames();
    void choosePadFile (int pad);
    void chooseChopFile();
    int chopModeFromBox() const;
    void toggleSequencerWindow();

    RezalicaProcessor& processor;
    rz::RezalicaLookAndFeel lnf;

    // header
    juce::TextButton loadChopButton { "LOAD SAMPLE" }, lazyButton { "LAZY CHOP" }, seqButton { "SEQUENCER" }, stopButton { "STOP ALL" };
    bool lastLazyState = false;
    juce::ComboBox chopModeBox;
    juce::Slider sensitivitySlider;
    juce::Label sliceCountLabel;
    std::array<juce::TextButton, rz::numBanks> bankButtons;
    int lastPlayedCounter = 0;
    rz::Knob masterKnob { "MASTER" };

    // sample view + selected pad
    rz::WaveformView waveform;
    juce::Label padTitle;
    rz::Knob gainKnob { "GAIN" }, panKnob { "PAN" }, pitchKnob { "PITCH" }, attackKnob { "ATTACK" },
             releaseKnob { "RELEASE" }, startKnob { "START" }, endKnob { "END" };
    juce::TextButton loopButton { "LOOP" }, reverseButton { "REVERSE" }, gateButton { "GATE" },
                     fxButton { "FX" }, muteButton { "MUTE" }, loadPadButton { "LOAD" }, clearPadButton { "CLEAR" };

    // pads
    std::vector<std::unique_ptr<rz::PadButton>> pads;
    std::array<int, rz::numPads> lastHits {};
    std::array<bool, rz::padsPerBank> keyDown {};
    std::array<int, rz::padsPerBank> keyPad {};   // which pad each held key started

    // effects
    juce::TextButton filterOnButton { "FILTER + DRIVE" }, lofiOnButton { "LO-FI / VINYL" }, delayOnButton { "DELAY" }, reverbOnButton { "REVERB" };
    rz::Knob reverbSizeKnob { "SIZE" }, reverbDampKnob { "DAMP" }, reverbWidthKnob { "WIDTH" }, reverbMixKnob { "MIX" };
    rz::Knob cutoffKnob { "CUTOFF" }, resoKnob { "RESONANCE" }, driveKnob { "DRIVE" },
             bitsKnob { "BITS" }, rateKnob { "RATE" }, noiseKnob { "NOISE" },
             timeKnob { "TIME" }, feedbackKnob { "FEEDBACK" }, mixKnob { "MIX" };
    juce::ComboBox delayDivBox;
    juce::Label delayDivLabel;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::unique_ptr<ComboAttachment> delayDivAttachment;

    std::unique_ptr<rz::SequencerWindow> sequencerWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> headerArea, sampleArea, padArea, fxArea;
    juce::Rectangle<int> filterArea, lofiArea, delayArea, reverbArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RezalicaEditor)
};
