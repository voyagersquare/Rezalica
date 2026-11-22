#include "SequencerWindow.h"

namespace rz
{
namespace
{
/** Click cycle of a step: off -> full -> softer -> softer -> softest -> off. */
juce::uint8 nextVelocity (juce::uint8 v)
{
    if (v == 0)  return 127;
    if (v > 96)  return 96;
    if (v > 64)  return 64;
    if (v > 32)  return 32;
    return 0;
}

/** Pop-up for one step (Ctrl+click): timing (early / late) and velocity. */
class StepEditPopup : public juce::Component
{
public:
    StepEditPopup (RezalicaProcessor& p, int patternIndex, int padIndex, int stepIndex, std::function<void()> changed)
        : processor (p), pattern (patternIndex), pad (padIndex), step (stepIndex), onChange (std::move (changed))
    {
        auto& seq = processor.getSequencer();
        title.setText (padLabel (pad) + "  step " + juce::String (step + 1), juce::dontSendNotification);
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        title.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (title);

        for (auto* l : { &timingLabel, &velocityLabel })
        {
            l->setFont (juce::FontOptions (11.0f));
            l->setColour (juce::Label::textColourId, colours::dim);
            l->setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (*l);
        }
        hint.setText ("< earlier        on the grid        later >", juce::dontSendNotification);
        hint.setFont (juce::FontOptions (10.0f));
        hint.setColour (juce::Label::textColourId, colours::dim);
        hint.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (hint);

        timing.setSliderStyle (juce::Slider::LinearHorizontal);
        timing.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        timing.setRange (-50.0, 50.0, 1.0);
        timing.setDoubleClickReturnValue (true, 0.0);
        timing.setValue (seq.getNudge (pattern, pad, step), juce::dontSendNotification);
        timing.setColour (juce::Slider::trackColourId, juce::Colour (0xffffc94a).withAlpha (0.6f));
        timing.onValueChange = [this]
        {
            processor.getSequencer().setNudge (pattern, pad, step, (int) timing.getValue());
            updateLabels();
            if (onChange) onChange();
        };
        addAndMakeVisible (timing);

        velocity.setSliderStyle (juce::Slider::LinearHorizontal);
        velocity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        velocity.setRange (1.0, 127.0, 1.0);
        velocity.setValue (juce::jmax (1, (int) seq.getStep (pattern, pad, step)), juce::dontSendNotification);
        velocity.onValueChange = [this]
        {
            processor.getSequencer().setStep (pattern, pad, step, (juce::uint8) velocity.getValue());
            updateLabels();
            if (onChange) onChange();
        };
        addAndMakeVisible (velocity);

        updateLabels();
        setSize (300, 150);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (10, 6);
        title.setBounds (b.removeFromTop (20));
        timingLabel.setBounds (b.removeFromTop (18));
        timing.setBounds (b.removeFromTop (22));
        hint.setBounds (b.removeFromTop (14));
        b.removeFromTop (6);
        velocityLabel.setBounds (b.removeFromTop (18));
        velocity.setBounds (b.removeFromTop (22));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        // centre mark of the timing slider = exactly on the grid
        auto t = timing.getBounds().toFloat();
        g.setColour (colours::text.withAlpha (0.5f));
        g.fillRect (t.getCentreX() - 0.5f, t.getY() - 2.0f, 1.0f, t.getHeight() + 4.0f);
    }

private:
    void updateLabels()
    {
        const int n = (int) timing.getValue();
        const double ms = std::abs (n) * 0.01 * 15000.0 / juce::jmax (20.0f, processor.getSequencer().effectiveBpm.load());
        timingLabel.setText ("TIMING: " + (n == 0 ? juce::String ("on the grid")
                                                 : juce::String (std::abs (n)) + " % " + (n < 0 ? "earlier" : "later")
                                                   + "  (" + juce::String (ms, 1) + " ms)"),
                             juce::dontSendNotification);
        velocityLabel.setText ("VELOCITY: " + juce::String ((int) velocity.getValue()), juce::dontSendNotification);
        repaint();
    }

    RezalicaProcessor& processor;
    const int pattern, pad, step;
    std::function<void()> onChange;
    juce::Label title, timingLabel, velocityLabel, hint;
    juce::Slider timing, velocity;
};
} // namespace
//==============================================================================
int SequencerGrid::numSteps() const
{
    auto& seq = processor.getSequencer();
    return juce::jlimit (1, Sequencer::maxSteps, seq.length[(size_t) seq.currentPattern.load()].load());
}

juce::Rectangle<float> SequencerGrid::cellBounds (int pad, int step) const
{
    const float w = (float) (getWidth() - labelWidth) / (float) visibleSteps();
    const float h = (float) getHeight() / (float) padsPerBank;
    return { (float) labelWidth + (float) (step - firstStep) * w, (float) pad * h, w, h };
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
    step = juce::jlimit (0, numSteps() - 1, firstStep + (pos.x - labelWidth) * visibleSteps() / juce::jmax (1, getWidth() - labelWidth));
    return true;
}

void SequencerGrid::paint (juce::Graphics& g)
{
    using namespace colours;
    auto& seq = processor.getSequencer();
    const int pattern = seq.currentPattern.load();
    const int steps = numSteps();
    const double heard = seq.getPlayPositionSteps(); // the step you hear now (not the one being computed)
    const int current = heard >= 0.0 ? juce::jmin (steps - 1, (int) std::floor (heard)) : -1;
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

        for (int s = firstStep; s < juce::jmin (steps, firstStep + visibleSteps()); ++s)
        {
            auto cell = cellBounds (row, s).reduced (1.5f);
            const bool beatStart = (s / 4) % 2 == 0;
            juce::Colour c = beatStart ? juce::Colour (0xff2b2e35) : juce::Colour (0xff24262c);
            if (s == current) c = c.brighter (0.25f);

            const auto v = seq.getStep (pattern, pad, s);
            if (v > 0)
                c = accent.withMultipliedBrightness (0.35f + 0.65f * (float) v / 127.0f);

            g.setColour (c);
            g.fillRoundedRectangle (cell, 3.0f);

            if (v > 0)
            {
                // velocity: a bar along the left side, as high as the step is loud
                const float h = (cell.getHeight() - 4.0f) * (float) v / 127.0f;
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.fillRect (cell.getX() + 3.0f, cell.getBottom() - 2.0f - h, 3.0f, h);

                // timing offset: a yellow mark left (early) or right (late) of the centre
                if (const int n = seq.getNudge (pattern, pad, s); n != 0)
                {
                    const float x = cell.getCentreX() + (float) n / 50.0f * (cell.getWidth() * 0.5f - 3.0f);
                    g.setColour (juce::Colour (0xffffc94a));
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x, cell.getCentreX()), cell.getBottom() - 4.0f,
                                                                            juce::jmax (x, cell.getCentreX()), cell.getBottom() - 2.0f));
                    g.fillRect (x - 1.0f, cell.getY() + 2.0f, 2.0f, cell.getHeight() - 4.0f);
                }
            }
        }
    }

    if (current >= firstStep && current < juce::jmin (steps, firstStep + visibleSteps()))
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

    // Ctrl + click: timing (early / late) and velocity of this step
    if (e.mods.isCommandDown() && ! e.mods.isPopupMenu())
    {
        if (current == 0)
            seq.setStep (pattern, pad, step, 127);
        paintValue = 0;
        auditionPad = -2; // no drag painting
        juce::Component::SafePointer<SequencerGrid> safe (this);
        auto popup = std::make_unique<StepEditPopup> (processor, pattern, pad, step, [safe] { if (safe != nullptr) safe->repaint(); });
        juce::CallOutBox::launchAsynchronously (std::move (popup), cellBounds (pad % padsPerBank, step).toNearestInt(), this);
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())          paintValue = 0;
    else if (e.mods.isShiftDown())     paintValue = 127;      // accent
    else                               paintValue = nextVelocity (current); // full, softer, softer, softest, off

    seq.setStep (pattern, pad, step, paintValue);
    if (paintValue == 0)
        seq.setNudge (pattern, pad, step, 0);
    repaint();
}

void SequencerGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (auditionPad >= 0 || auditionPad == -2)
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
        processor.triggerPadFromUi (auditionPad, 0.0f, false);
    auditionPad = -1;
}

//==============================================================================
SequencerPanel::SequencerPanel (RezalicaProcessor& p)
    : processor (p),
      dragMidiButton ("DRAG MIDI", [this] { return processor.exportPatternMidi (processor.getSequencer().currentPattern.load(), true, false); }),
      dragWavButton ("DRAG WAV", [this] { return processor.exportPatternWav (processor.getSequencer().currentPattern.load(), true, false); }),
      grid (p), metronome (p.getSequencer()), transport (p.getSequencer(), TransportBar::stepPattern), ledBpm (p.getSequencer())
{
    ledBpm.captionColour = colours::dim;
    addAndMakeVisible (ledBpm);
    addAndMakeVisible (transport);
    metronome.setShowClickButton (false); // the metronome key is in the transport block
    dragMidiButton.setTooltip ("Drag the pattern's steps into your DAW as a MIDI clip (click: show the file)");
    dragWavButton.setTooltip ("Drag the pattern's steps into your DAW as audio (rendered loop, click: show the file)");
    addAndMakeVisible (dragMidiButton);
    addAndMakeVisible (dragWavButton);
    auto& seq = processor.getSequencer();

    for (auto* b : { &playButton, &recButton, &syncButton })
    {
        b->setClickingTogglesState (true);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0413a));
    syncButton.setColour (juce::TextButton::buttonOnColourId, colours::accent2);

    playButton.onClick = [this]
    {
        auto& s = processor.getSequencer();
        if (playButton.getToggleState()) s.playPattern(); else s.stopPlayback();
    };
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
        s.setClip (to, s.getClip (from)->copy()); // the piano roll part too
        s.setNudgeData (to, s.getNudgeData (from));
        s.length[(size_t) to] = s.length[(size_t) from].load();
        s.patternSwing[(size_t) to] = s.patternSwing[(size_t) from].load();
        s.swingGrid[(size_t) to] = s.swingGrid[(size_t) from].load();
        s.currentPattern = to;
        syncControls();
        grid.repaint();
    };

    for (int i = 0; i < Sequencer::numPatterns; ++i)
        patternBox.addItem ("Pattern " + juce::String (i + 1), i + 1);
    patternBox.onChange = [this] { processor.getSequencer().currentPattern = patternBox.getSelectedId() - 1; grid.repaint(); };

    for (int len : { 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 })
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
    swingSlider.setTooltip ("Swing of this pattern");
    swingSlider.onValueChange = [this]
    {
        auto& s = processor.getSequencer();
        s.patternSwing[(size_t) s.currentPattern.load()] = (float) swingSlider.getValue() / 100.0f;
    };
    swingGridBox.addItem ("1/16", 1);
    swingGridBox.addItem ("1/8", 2);
    swingGridBox.setTooltip ("Swing grid: shuffle every 2nd 16th or every 2nd 8th note");
    swingGridBox.setWantsKeyboardFocus (false);
    swingGridBox.onChange = [this]
    {
        auto& s = processor.getSequencer();
        s.swingGrid[(size_t) s.currentPattern.load()] = swingGridBox.getSelectedId() - 1;
    };
    addAndMakeVisible (swingGridBox);
    addAndMakeVisible (metronome);

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

    for (int i = 0; i < (int) pageButtons.size(); ++i)
    {
        auto& pb = pageButtons[(size_t) i];
        pb.setButtonText (juce::String (i * SequencerGrid::stepsPerPage + 1) + "-" + juce::String ((i + 1) * SequencerGrid::stepsPerPage));
        pb.setClickingTogglesState (true);
        pb.setRadioGroupId (4713);
        pb.setWantsKeyboardFocus (false);
        pb.onClick = [this, i] { if (pageButtons[(size_t) i].getToggleState()) { followButton.setToggleState (false, juce::dontSendNotification); grid.setFirstStep (i * SequencerGrid::stepsPerPage); } };
        addAndMakeVisible (pb);
    }
    followButton.setClickingTogglesState (true);
    followButton.setWantsKeyboardFocus (false);
    followButton.setToggleState (true, juce::dontSendNotification);
    followButton.setColour (juce::TextButton::buttonOnColourId, colours::accent2);
    followButton.setTooltip ("Show the page that is playing");
    addAndMakeVisible (followButton);

    infoLabel.setText ("Click = full / softer / softer / softest / off   Ctrl+click = timing (early / late) + velocity   "
                       "Right-click/drag = erase   Click a pad name = audition",
                       juce::dontSendNotification);
    infoLabel.setColour (juce::Label::textColourId, colours::dim);
    addAndMakeVisible (infoLabel);
    addAndMakeVisible (grid);

    syncControls();
    setWantsKeyboardFocus (true);
    setSize (1120, 660);
    startTimerHz (30);
}

SequencerPanel::~SequencerPanel()
{
    stopTimer();
}

void SequencerPanel::syncControls()
{
    auto& s = processor.getSequencer();
    playButton.setToggleState (s.isPlayingPattern(), juce::dontSendNotification);
    recButton.setToggleState (s.recording.load(), juce::dontSendNotification);
    syncButton.setToggleState (s.syncToHost.load(), juce::dontSendNotification);
    for (int i = 0; i < Sequencer::numPatterns; ++i)   // names from RENAME in the SCENE window
    {
        const auto label = juce::String (i + 1) + ": " + s.getPatternLabel (i);
        if (patternBox.getItemText (i) != label) patternBox.changeItemText (i + 1, label);
    }
    patternBox.setSelectedId (s.currentPattern.load() + 1, juce::dontSendNotification);
    if (const auto cur = patternBox.getItemText (s.currentPattern.load()); patternBox.getText() != cur)
        patternBox.setText (cur, juce::dontSendNotification);
    for (int b = 0; b < numBanks; ++b)
        bankButtons[(size_t) b].setToggleState (b == processor.currentBank.load(), juce::dontSendNotification);
    lengthBox.setSelectedId (s.length[(size_t) s.currentPattern.load()].load(), juce::dontSendNotification);
    if (! bpmSlider.isMouseButtonDown())
        bpmSlider.setValue (s.syncToHost.load() ? s.effectiveBpm.load() : s.bpm.load(), juce::dontSendNotification);
    if (! swingSlider.isMouseButtonDown())
        swingSlider.setValue (s.patternSwing[(size_t) s.currentPattern.load()].load() * 100.0, juce::dontSendNotification);
    swingGridBox.setSelectedId (s.swingGrid[(size_t) s.currentPattern.load()].load() + 1, juce::dontSendNotification);
}

void SequencerPanel::timerCallback()
{
    // while this window is the active one, the computer keyboard plays its pads
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (auto* peer = getPeer(); peer != nullptr && peer->isFocused() && isShowing()
        && ! hasKeyboardFocus (true)
        && (focused == nullptr || focused->getPeer() != peer || focused->isParentOf (this))
        && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0)
        grabKeyboardFocus();
    // pages of 32 steps (only shown when the pattern is longer than 32)
    const int steps = grid.numSteps();
    const int pages = (steps + SequencerGrid::stepsPerPage - 1) / SequencerGrid::stepsPerPage;
    for (int i = 0; i < (int) pageButtons.size(); ++i)
        pageButtons[(size_t) i].setVisible (pages > 1 && i < pages);
    followButton.setVisible (pages > 1);
    int first = grid.getFirstStep();
    if (first >= steps) first = 0;
    const double heard = processor.getSequencer().getPlayPositionSteps();
    const int cur = heard >= 0.0 ? (int) std::floor (heard) : -1;
    if (followButton.getToggleState() && cur >= 0)
        first = (cur / SequencerGrid::stepsPerPage) * SequencerGrid::stepsPerPage;
    grid.setFirstStep (pages > 1 ? first : 0);
    for (int i = 0; i < (int) pageButtons.size(); ++i)
        pageButtons[(size_t) i].setToggleState (i * SequencerGrid::stepsPerPage == grid.getFirstStep(), juce::dontSendNotification);

    syncControls();
    grid.repaint();
}

bool SequencerPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey) { processor.getSequencer().togglePatternPlay(); return true; }
    if (! k.getModifiers().isCommandDown() && computerKeys.isPadKey (juce::CharacterFunctions::toLowerCase (k.getTextCharacter())))
        return true; // played in keyStateChanged (1-4, Q-R, A-F, Z-V)
    return false;
}

void SequencerPanel::mouseDown (const juce::MouseEvent&) { grabKeyboardFocus(); }

void SequencerPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void SequencerPanel::resized()
{
    auto b = getLocalBounds().reduced (10);
    auto top = b.removeFromTop (32);

    playButton.setVisible (false);
    recButton.setVisible (false);
    transport.setBounds (top.removeFromLeft (transport.getIdealWidth (40))); top.removeFromLeft (8);
    syncButton.setVisible (false);   // one tempo for the whole sampler: the LED display
    bpmSlider.setVisible (false);
    ledBpm.setBounds (top.removeFromLeft (84).withSizeKeepingCentre (84, 36).translated (0, -2)); top.removeFromLeft (12);
    patternBox.setBounds (top.removeFromLeft (110)); top.removeFromLeft (6);
    lengthBox.setBounds (top.removeFromLeft (100)); top.removeFromLeft (12);
    swingSlider.setBounds (top.removeFromLeft (110)); top.removeFromLeft (4);
    swingGridBox.setBounds (top.removeFromLeft (64)); top.removeFromLeft (12);
    copyButton.setBounds (top.removeFromRight (110)); top.removeFromRight (6);
    clearButton.setBounds (top.removeFromRight (70));

    b.removeFromTop (6);
    auto bankRow = b.removeFromTop (26);
    for (auto& bb : bankButtons)
    {
        bb.setBounds (bankRow.removeFromLeft (40));
        bankRow.removeFromLeft (4);
    }
    bankRow.removeFromLeft (16);
    dragMidiButton.setBounds (bankRow.removeFromLeft (110));
    bankRow.removeFromLeft (6);
    dragWavButton.setBounds (bankRow.removeFromLeft (110));
    bankRow.removeFromLeft (16);
    metronome.setBounds (bankRow.removeFromLeft (136));
    followButton.setBounds (bankRow.removeFromRight (80));
    bankRow.removeFromRight (8);
    for (int i = (int) pageButtons.size(); --i >= 0;)
    {
        pageButtons[(size_t) i].setBounds (bankRow.removeFromRight (70));
        bankRow.removeFromRight (4);
    }

    b.removeFromTop (6);
    infoLabel.setBounds (b.removeFromBottom (22));
    b.removeFromBottom (4);
    grid.setBounds (b);
}
} // namespace rz
