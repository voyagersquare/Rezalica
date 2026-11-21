#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "DragExport.h"
#include "MetronomeControls.h"
#include "TransportButton.h"
#include "ComputerKeyboard.h"

namespace rz
{
/** The step grid: 16 rows (pads) x pattern length (16th-note steps). */
class SequencerGrid : public juce::Component
{
public:
    explicit SequencerGrid (RezalicaProcessor& p) : processor (p) { setWantsKeyboardFocus (false); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int stepsPerPage = 32;
    void setFirstStep (int s) { if (s != firstStep) { firstStep = s; repaint(); } }
    int getFirstStep() const noexcept { return firstStep; }
    int numSteps() const;

private:
    static constexpr int labelWidth = 120;
    int visibleSteps() const { return juce::jmax (1, juce::jmin (stepsPerPage, numSteps() - firstStep)); }
    int firstStep = 0;
    bool cellAt (juce::Point<int> pos, int& pad, int& step) const;
    juce::Rectangle<float> cellBounds (int pad, int step) const;

    RezalicaProcessor& processor;
    juce::uint8 paintValue = 0;
    int auditionPad = -1;
};

/** Transport + pattern controls above the grid. */
class SequencerPanel : public juce::Component,
                       private juce::Timer
{
public:
    explicit SequencerPanel (RezalicaProcessor& p);
    ~SequencerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override { return computerKeys.pollPads (processor); }
    void focusLost (FocusChangeType) override { computerKeys.releaseAll (processor); }
    void mouseDown (const juce::MouseEvent&) override;

private:
    ComputerKeyboard computerKeys;
    void timerCallback() override;
    void syncControls();

    RezalicaProcessor& processor;
    juce::TextButton playButton { "PLAY" }, recButton { "REC" }, syncButton { "SYNC HOST" }, clearButton { "CLEAR" }, copyButton { "COPY TO NEXT" };
    juce::ComboBox patternBox, lengthBox, swingGridBox;
    juce::Slider bpmSlider, swingSlider;
    juce::Label bpmLabel, swingLabel, patternLabel, lengthLabel, infoLabel;
    std::array<juce::TextButton, numBanks> bankButtons;
    std::array<juce::TextButton, Sequencer::maxSteps / SequencerGrid::stepsPerPage> pageButtons;
    juce::TextButton followButton { "FOLLOW" };
    DragExportButton dragMidiButton, dragWavButton;
    SequencerGrid grid;
    MetronomeControls metronome;
    TransportBar transport;
};

class SequencerWindow : public juce::DocumentWindow
{
public:
    SequencerWindow (RezalicaProcessor& p, juce::LookAndFeel& lnf)
        : juce::DocumentWindow ("Rezalica - Pattern Sequencer", colours::background, juce::DocumentWindow::closeButton)
    {
        setLookAndFeel (&lnf);
        setUsingNativeTitleBar (true);
        setContentOwned (new SequencerPanel (p), true);
        setResizable (true, false);
        setResizeLimits (640, 420, 2000, 1400);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
    }

    ~SequencerWindow() override { setLookAndFeel (nullptr); }

    void activeWindowStatusChanged() override
    {
        if (isActiveWindow())
            if (auto* c = getContentComponent()) c->grabKeyboardFocus(); // the computer keyboard plays the pads
    }

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClose) onClose();
    }

    std::function<void()> onClose;
};
} // namespace rz
