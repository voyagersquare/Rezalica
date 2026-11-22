#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace rz
{
/** All time stretch / warp options for the selected pad. */
class StretchPanel : public juce::Component,
                     private juce::Timer
{
public:
    explicit StretchPanel (RezalicaProcessor& p);
    ~StretchPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void loadFromPad();
    void settingsChanged (bool reapply);   // reapply = re-stretch now if warp is on
    void updateInfo();

    RezalicaProcessor& processor;
    WarpSettings ui;
    int shownPad = -1;
    juce::String shownSampleName;

    juce::Label title, info, modeLabel, lengthLabel, tempoLabel, scopeLabel, beatsLabel;
    juce::TextButton beatsButton { "BEATS" }, complexButton { "COMPLEX" };
    juce::Label beatsHint, complexHint;
    std::array<juce::TextButton, 7> lengthButtons;
    juce::TextButton syncButton { "FOLLOW SAMPLER TEMPO" };
    juce::Slider bpmSlider;
    juce::TextButton wholeButton { "WHOLE SAMPLE" }, padOnlyButton { "SELECTED PAD ONLY" };
    juce::ComboBox preserveBox, loopBox;
    juce::Slider envelopeSlider;
    juce::Label preserveLabel, loopLabel, envelopeLabel;
    juce::TextButton warpOnButton { "WARP ON" }, warpOffButton { "WARP OFF" };
};

class StretchWindow : public juce::DocumentWindow
{
    std::unique_ptr<rz::StayOnTopOfRezalica> onTopHelper;
public:
    StretchWindow (RezalicaProcessor& p, juce::LookAndFeel& lnf)
        : juce::DocumentWindow ("Rezalica - Time Stretch / Warp", colours::background, juce::DocumentWindow::closeButton)
    {
        setLookAndFeel (&lnf);
        setUsingNativeTitleBar (true);
        setContentOwned (new StretchPanel (p), true);
        setResizable (false, false);
        onTopHelper = std::make_unique<rz::StayOnTopOfRezalica> (*this); // on top only while Rezalica is in front
        centreWithSize (getWidth(), getHeight());
    }

    ~StretchWindow() override { setLookAndFeel (nullptr); }

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClose) onClose();
    }

    std::function<void()> onClose;
};
} // namespace rz
