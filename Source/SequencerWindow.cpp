#include "SequencerWindow.h"

namespace rz
{
//==============================================================================
int SequencerGrid::numSteps() const
{
    auto& seq = processor.getSequencer();
    return juce::jlimit (1, Sequencer::maxSteps, seq.length[(size_t) seq.currentPattern.load()].load());
}

juce::Rectangle<float> SequencerGrid::cellBounds (int pad, int step) const
{
    const float w = (float) (getWidth() - labelWidth) / (float) numSteps();
    const float h = (float) getHeight() / (float) padsPerBank;
    return { (float) labelWidth + (float) step * w, (float) pad * h, w, h };
}

bool SequencerGrid::cellAt (juce::Point<int> pos, int& pad, int& step) const
{
    pad = processor.currentBank.load() * padsPerBank
        + juce::jlimit (0, padsPerBank - 1, pos.y * padsPerBank / juce::jmax (1, getHeight()));
    if (pos.x < labelWidth)
    {
        step = -1;
        return false;
    }
    step = juce::jlimit (0, numSteps() - 1, (pos.x - labelWidth) * numSteps() / juce::jmax (1, getWidth() - labelWidth));
    return true;
}

void SequencerGrid::paint (juce::Graphics& g)
{
    using namespace colours;
    auto& seq = processor.getSequencer();
    const int pattern = seq.currentPattern.load();
    const int steps = numSteps();
    const int current = seq.currentStep.load();
    const float rowH = (float) getHeight() / (float) padsPerBank;
    const int bankBase = processor.currentBank.load() * padsPerBank;

    for (int row = 0; row < padsPerBank; ++row)
    {
        const int pad = bankBase + row;
        auto label = juce::Rectangle<float> (0.0f, (float) row * rowH, (float) labelWidth - 4.0f, rowH).reduced (1.0f);
        g.setColour (pad == processor.selectedPad.load() ? panelLight.brighter (0.1f) : panel);
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (text);
        g.setFont (juce::FontOptions (juce::jmin (13.0f, rowH * 0.6f), juce::Font::bold));
        g.drawText (padLabel (pad), label.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (juce::jmin (11.0f, rowH * 0.55f)));
        g.drawText (processor.getPadName (pad), label.reduced (6.0f, 0.0f).withTrimmedLeft (30.0f), juce::Justification::centredLeft, true);

        for (int s = 0; s < steps; ++s)
        {
            auto cell = cellBounds (row, s).reduced (1.5f);
            const bool beatStart = (s / 4) % 2 == 0;
            juce::Colour c = beatStart ? juce::Colour (0xff2b2e35) : juce::Colour (0xff24262c);
            if (s == current) c = c.brighter (0.25f);

            const auto v = seq.getStep (pattern, pad, s);
            if (v > 0)
                c = accent.withMultipliedBrightness (0.45f + 0.55f * (float) v / 127.0f);

            g.setColour (c);
            g.fillRoundedRectangle (cell, 3.0f);
        }
    }

    if (current >= 0 && current < steps)
    {
        auto col = cellBounds (0, current).withHeight ((float) getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawRect (col, 1.0f);
    }
}

void SequencerGrid::mouseDown (const juce::MouseEvent& e)
{
    int pad, step;
    if (! cellAt (e.getPosition(), pad, step))
    {
        // clicking a row label auditions that pad
        auditionPad = pad;
        processor.selectedPad = pad;
        processor.triggerPadFromUi (pad, 1.0f, true);
        repaint();
        return;
    }

    auto& seq = processor.getSequencer();
    const int pattern = seq.currentPattern.load();
    const auto current = seq.getStep (pattern, pad, step);

    if (e.mods.isPopupMenu())          paintValue = 0;
    else if (e.mods.isShiftDown())     paintValue = 127;      // accent
    else                               paintValue = current > 0 ? 0 : 100;

    seq.setStep (pattern, pad, step, paintValue);
    repaint();
}

void SequencerGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (auditionPad >= 0)
        return;

    int pad, step;
    if (cellAt (e.getPosition(), pad, step))
    {
        auto& seq = processor.getSequencer();
        seq.setStep (seq.currentPattern.load(), pad, step, paintValue);
        repaint();
    }
}

void SequencerGrid::mouseUp (const juce::MouseEvent&)
{
    if (auditionPad >= 0)
    {
        processor.triggerPadFromUi (auditionPad, 0.0f, false);
        auditionPad = -1;
    }
}

//==============================================================================
SequencerPanel::SequencerPanel (RezalicaProcessor& p) : processor (p), grid (p)
{
    auto& seq = processor.getSequencer();

    for (auto* b : { &playButton, &recButton, &syncButton })
    {
        b->setClickingTogglesState (true);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0413a));
    syncButton.setColour (juce::TextButton::buttonOnColourId, colours::accent2);

    playButton.onClick = [this] { processor.getSequencer().internalPlay = playButton.getToggleState(); };
    recButton.onClick  = [this] { processor.getSequencer().recording = recButton.getToggleState(); };
    syncButton.onClick = [this] { processor.getSequencer().syncToHost = syncButton.getToggleState(); };

    for (auto* b : { &clearButton, &copyButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    clearButton.onClick = [this]
    {
        auto& s = processor.getSequencer();
        s.clearPattern (s.currentPattern.load());
        grid.repaint();
    };
    copyButton.onClick = [this]
    {
        auto& s = processor.getSequencer();
        const int from = s.currentPattern.load();
        const int to = (from + 1) % Sequencer::numPatterns;
        s.setPatternData (to, s.getPatternData (from));
        s.length[(size_t) to] = s.length[(size_t) from].load();
        s.currentPattern = to;
        syncControls();
        grid.repaint();
    };

    for (int i = 0; i < Sequencer::numPatterns; ++i)
        patternBox.addItem ("Pattern " + juce::String (i + 1), i + 1);
    patternBox.onChange = [this] { processor.getSequencer().currentPattern = patternBox.getSelectedId() - 1; grid.repaint(); };

    for (int len : { 4, 8, 12, 16, 24, 32 })
        lengthBox.addItem (juce::String (len) + " steps", len);
    lengthBox.onChange = [this]
    {
        auto& s = processor.getSequencer();
        s.length[(size_t) s.currentPattern.load()] = lengthBox.getSelectedId();
        grid.repaint();
    };

    for (auto* c : { &patternBox, &lengthBox })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }

    bpmSlider.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider.setRange (40.0, 240.0, 0.1);
    bpmSlider.setValue (seq.bpm.load(), juce::dontSendNotification);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.onValueChange = [this] { processor.getSequencer().bpm = (float) bpmSlider.getValue(); };

    swingSlider.setSliderStyle (juce::Slider::LinearBar);
    swingSlider.setRange (0.0, 100.0, 1.0);
    swingSlider.setTextValueSuffix (" % swing");
    swingSlider.onValueChange = [this] { processor.getSequencer().swing = (float) swingSlider.getValue() / 100.0f; };

    for (auto* s : { &bpmSlider, &swingSlider })
    {
        s->setWantsKeyboardFocus (false);
        s->setColour (juce::Slider::trackColourId, colours::panelLight.brighter (0.15f));
        addAndMakeVisible (*s);
    }

    for (int b = 0; b < numBanks; ++b)
    {
        auto& bb = bankButtons[(size_t) b];
        bb.setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + b)));
        bb.setClickingTogglesState (true);
        bb.setRadioGroupId (4712);
        bb.setWantsKeyboardFocus (false);
        bb.onClick = [this, b] { if (bankButtons[(size_t) b].getToggleState()) { processor.currentBank = b; grid.repaint(); } };
        addAndMakeVisible (bb);
    }

    infoLabel.setText ("Click = step on/off   Shift+click = accent   Right-click/drag = erase   Click a pad name = audition. "
                       "With SYNC HOST on, the pattern follows your DAW transport.",
                       juce::dontSendNotification);
    infoLabel.setColour (juce::Label::textColourId, colours::dim);
    addAndMakeVisible (infoLabel);
    addAndMakeVisible (grid);

    syncControls();
    setSize (1000, 640);
    startTimerHz (30);
}

SequencerPanel::~SequencerPanel()
{
    stopTimer();
}

void SequencerPanel::syncControls()
{
    auto& s = processor.getSequencer();
    playButton.setToggleState (s.internalPlay.load(), juce::dontSendNotification);
    recButton.setToggleState (s.recording.load(), juce::dontSendNotification);
    syncButton.setToggleState (s.syncToHost.load(), juce::dontSendNotification);
    patternBox.setSelectedId (s.currentPattern.load() + 1, juce::dontSendNotification);
    for (int b = 0; b < numBanks; ++b)
        bankButtons[(size_t) b].setToggleState (b == processor.currentBank.load(), juce::dontSendNotification);
    lengthBox.setSelectedId (s.length[(size_t) s.currentPattern.load()].load(), juce::dontSendNotification);
    if (! bpmSlider.isMouseButtonDown())
        bpmSlider.setValue (s.syncToHost.load() ? s.effectiveBpm.load() : s.bpm.load(), juce::dontSendNotification);
    if (! swingSlider.isMouseButtonDown())
        swingSlider.setValue (s.swing.load() * 100.0, juce::dontSendNotification);
}

void SequencerPanel::timerCallback()
{
    syncControls();
    grid.repaint();
}

void SequencerPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void SequencerPanel::resized()
{
    auto b = getLocalBounds().reduced (10);
    auto top = b.removeFromTop (32);

    playButton.setBounds (top.removeFromLeft (70)); top.removeFromLeft (6);
    recButton.setBounds (top.removeFromLeft (60)); top.removeFromLeft (6);
    syncButton.setBounds (top.removeFromLeft (100)); top.removeFromLeft (12);
    bpmSlider.setBounds (top.removeFromLeft (120)); top.removeFromLeft (12);
    patternBox.setBounds (top.removeFromLeft (110)); top.removeFromLeft (6);
    lengthBox.setBounds (top.removeFromLeft (100)); top.removeFromLeft (12);
    swingSlider.setBounds (top.removeFromLeft (110)); top.removeFromLeft (12);
    copyButton.setBounds (top.removeFromRight (110)); top.removeFromRight (6);
    clearButton.setBounds (top.removeFromRight (70));

    b.removeFromTop (6);
    auto bankRow = b.removeFromTop (26);
    for (auto& bb : bankButtons)
    {
        bb.setBounds (bankRow.removeFromLeft (40));
        bankRow.removeFromLeft (4);
    }

    b.removeFromTop (6);
    infoLabel.setBounds (b.removeFromBottom (22));
    b.removeFromBottom (4);
    grid.setBounds (b);
}
} // namespace rz
