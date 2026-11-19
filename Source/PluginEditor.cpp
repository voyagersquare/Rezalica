#include "PluginEditor.h"

namespace
{
const char* const padKeys = "1234qwerasdfzxcv"; // computer keyboard -> pads 1..16

void styleToggle (juce::TextButton& b, juce::Colour on = rz::colours::accent)
{
    b.setClickingTogglesState (true);
    b.setWantsKeyboardFocus (false);
    b.setColour (juce::TextButton::buttonOnColourId, on);
}
} // namespace

RezalicaEditor::RezalicaEditor (RezalicaProcessor& p)
    : AudioProcessorEditor (&p), processor (p), waveform (p)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    auto& apvts = processor.getApvts();
    auto attachSlider = [&] (rz::Knob& k, const juce::String& id)
    {
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (apvts, id, k.slider));
        k.slider.textFromValueFunction = nullptr;
        k.slider.setNumDecimalPlacesToDisplay (2);
        addAndMakeVisible (k);
    };
    auto attachButton = [&] (juce::TextButton& b, const juce::String& id)
    {
        styleToggle (b);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (apvts, id, b));
        addAndMakeVisible (b);
    };

    // ---- header ----
    for (auto* b : { &loadChopButton, &rechopButton, &seqButton, &stopButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    styleToggle (seqButton, rz::colours::accent2);
    loadChopButton.onClick = [this] { chooseChopFile(); };
    rechopButton.onClick = [this] { processor.chopPadSample (processor.selectedPad.load(), chopModeFromBox()); };
    seqButton.onClick = [this] { toggleSequencerWindow(); };
    stopButton.onClick = [this] { processor.stopAll(); };
    loadChopButton.setTooltip ("Load an audio file and slice it across the pads");

    chopModeBox.addItem ("4 slices", 1);
    chopModeBox.addItem ("8 slices", 2);
    chopModeBox.addItem ("16 slices", 3);
    chopModeBox.addItem ("Transients", 4);
    chopModeBox.setSelectedId (3, juce::dontSendNotification);
    chopModeBox.setWantsKeyboardFocus (false);
    addAndMakeVisible (chopModeBox);

    attachSlider (masterKnob, "master");
    masterKnob.slider.setSliderStyle (juce::Slider::LinearBar);
    masterKnob.slider.setNumDecimalPlacesToDisplay (1);
    masterKnob.slider.setTextValueSuffix (" dB");
    masterKnob.slider.setColour (juce::Slider::trackColourId, rz::colours::accent.withAlpha (0.5f));

    // ---- waveform ----
    addAndMakeVisible (waveform);
    waveform.onPadSelected = [this] (int pad) { selectPad (pad); };
    waveform.onRegionChanged = [this] { refreshPadControls(); };
    waveform.onFileDropped = [this] (const juce::File& f) { processor.chopFileToPads (f, chopModeFromBox()); selectPad (0); };

    // ---- selected pad controls ----
    padTitle.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    padTitle.setColour (juce::Label::textColourId, rz::colours::accent);
    addAndMakeVisible (padTitle);

    auto setupPadKnob = [this] (rz::Knob& k, double lo, double hi, double step, double def, const juce::String& suffix,
                                std::function<void (rz::PadSettings&, float)> apply)
    {
        k.slider.setRange (lo, hi, step);
        k.slider.setDoubleClickReturnValue (true, def);
        k.slider.setTextValueSuffix (suffix);
        k.slider.onValueChange = [this, &k, apply]
        {
            apply (processor.getEngine().getPad (processor.selectedPad.load()), (float) k.slider.getValue());
            waveform.repaint();
        };
        addAndMakeVisible (k);
    };

    setupPadKnob (gainKnob, -36.0, 12.0, 0.1, 0.0, " dB", [] (rz::PadSettings& s, float v) { s.gainDb = v; });
    setupPadKnob (panKnob, -1.0, 1.0, 0.01, 0.0, "", [] (rz::PadSettings& s, float v) { s.pan = v; });
    setupPadKnob (pitchKnob, -24.0, 24.0, 0.01, 0.0, " st", [] (rz::PadSettings& s, float v) { s.pitch = v; });
    setupPadKnob (attackKnob, 0.0, 2000.0, 0.1, 1.0, " ms", [] (rz::PadSettings& s, float v) { s.attackMs = v; });
    setupPadKnob (releaseKnob, 1.0, 4000.0, 0.1, 30.0, " ms", [] (rz::PadSettings& s, float v) { s.releaseMs = v; });
    setupPadKnob (startKnob, 0.0, 1.0, 0.0001, 0.0, "", [] (rz::PadSettings& s, float v) { s.start = juce::jmin (v, s.end.load() - 0.0001f); });
    setupPadKnob (endKnob, 0.0, 1.0, 0.0001, 1.0, "", [] (rz::PadSettings& s, float v) { s.end = juce::jmax (v, s.start.load() + 0.0001f); });
    attackKnob.slider.setSkewFactorFromMidPoint (100.0);
    releaseKnob.slider.setSkewFactorFromMidPoint (300.0);
    pitchKnob.slider.setNumDecimalPlacesToDisplay (1);

    auto setupPadToggle = [this] (juce::TextButton& b, std::function<void (rz::PadSettings&, bool)> apply, juce::Colour on)
    {
        styleToggle (b, on);
        b.onClick = [this, &b, apply] { apply (processor.getEngine().getPad (processor.selectedPad.load()), b.getToggleState()); };
        addAndMakeVisible (b);
    };
    setupPadToggle (loopButton, [] (rz::PadSettings& s, bool v) { s.loop = v; }, rz::colours::accent);
    setupPadToggle (reverseButton, [] (rz::PadSettings& s, bool v) { s.reverse = v; }, rz::colours::accent);
    setupPadToggle (gateButton, [] (rz::PadSettings& s, bool v) { s.gate = v; }, rz::colours::accent);
    setupPadToggle (fxButton, [] (rz::PadSettings& s, bool v) { s.fx = v; }, rz::colours::accent2);
    setupPadToggle (muteButton, [] (rz::PadSettings& s, bool v) { s.mute = v; }, juce::Colour (0xffe0413a));
    loopButton.setTooltip ("Loop the region. Without GATE, hit the pad again to stop it.");
    gateButton.setTooltip ("Play only while the pad / key / MIDI note is held");
    fxButton.setTooltip ("Send this pad through the effects");

    for (auto* b : { &loadPadButton, &clearPadButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    loadPadButton.onClick = [this] { choosePadFile (processor.selectedPad.load()); };
    clearPadButton.onClick = [this] { processor.clearPad (processor.selectedPad.load()); };

    // ---- pads ----
    for (int i = 0; i < rz::numPads; ++i)
    {
        auto pad = std::make_unique<rz::PadButton> (processor, i, juce::String::charToString ((juce::juce_wchar) padKeys[i]));
        pad->onSelect = [this] (int idx) { selectPad (idx); };
        pad->onLoadRequest = [this] (int idx) { choosePadFile (idx); };
        pad->onClear = [this] (int idx) { processor.clearPad (idx); };
        pad->onChop = [this] (int idx, int mode) { processor.chopPadSample (idx, mode); selectPad (0); };
        addAndMakeVisible (*pad);
        pads.push_back (std::move (pad));
        lastHits[(size_t) i] = processor.getEngine().hitCounter[(size_t) i].load();
    }

    // ---- effects ----
    attachButton (filterOnButton, "filterOn");
    attachSlider (cutoffKnob, "cutoff");
    attachSlider (resoKnob, "resonance");
    attachSlider (driveKnob, "drive");
    attachButton (lofiOnButton, "lofiOn");
    attachSlider (bitsKnob, "bits");
    attachSlider (rateKnob, "rate");
    attachSlider (noiseKnob, "noise");
    attachButton (delayOnButton, "delayOn");
    attachSlider (timeKnob, "delayTime");
    timeKnob.slider.setNumDecimalPlacesToDisplay (0);
    timeKnob.slider.setTextValueSuffix (" ms");
    cutoffKnob.slider.setNumDecimalPlacesToDisplay (0);
    cutoffKnob.slider.setTextValueSuffix (" Hz");
    attachSlider (feedbackKnob, "feedback");
    attachSlider (mixKnob, "delayMix");

    delayDivBox.addItemList (processor.getApvts().getParameter ("delayDiv")->getAllValueStrings(), 1);
    delayDivBox.setWantsKeyboardFocus (false);
    delayDivAttachment = std::make_unique<ComboAttachment> (apvts, "delayDiv", delayDivBox);
    addAndMakeVisible (delayDivBox);
    delayDivLabel.setText ("SYNC", juce::dontSendNotification);
    delayDivLabel.setJustificationType (juce::Justification::centred);
    delayDivLabel.setColour (juce::Label::textColourId, rz::colours::dim);
    addAndMakeVisible (delayDivLabel);

    processor.padsChanged.addChangeListener (this);

    setSize (1080, 720);
    selectPad (juce::jlimit (0, rz::numPads - 1, processor.selectedPad.load()));
    refreshPadNames();
    startTimerHz (30);
}

RezalicaEditor::~RezalicaEditor()
{
    stopTimer();
    processor.padsChanged.removeChangeListener (this);
    sequencerWindow = nullptr;
    setLookAndFeel (nullptr);
}

//==============================================================================
void RezalicaEditor::selectPad (int pad)
{
    processor.selectedPad = pad;
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setSelected (i == pad);
    waveform.setPad (pad);
    refreshPadControls();
    grabKeyboardFocus();
}

void RezalicaEditor::refreshPadControls()
{
    const int pad = processor.selectedPad.load();
    auto& s = processor.getEngine().getPad (pad);

    auto name = processor.getPadName (pad);
    padTitle.setText ("PAD " + juce::String (pad + 1) + (name.isNotEmpty() ? "  -  " + name : juce::String ("  -  empty")),
                      juce::dontSendNotification);

    gainKnob.slider.setValue (s.gainDb.load(), juce::dontSendNotification);
    panKnob.slider.setValue (s.pan.load(), juce::dontSendNotification);
    pitchKnob.slider.setValue (s.pitch.load(), juce::dontSendNotification);
    attackKnob.slider.setValue (s.attackMs.load(), juce::dontSendNotification);
    releaseKnob.slider.setValue (s.releaseMs.load(), juce::dontSendNotification);
    startKnob.slider.setValue (s.start.load(), juce::dontSendNotification);
    endKnob.slider.setValue (s.end.load(), juce::dontSendNotification);

    loopButton.setToggleState (s.loop.load(), juce::dontSendNotification);
    reverseButton.setToggleState (s.reverse.load(), juce::dontSendNotification);
    gateButton.setToggleState (s.gate.load(), juce::dontSendNotification);
    fxButton.setToggleState (s.fx.load(), juce::dontSendNotification);
    muteButton.setToggleState (s.mute.load(), juce::dontSendNotification);
}

void RezalicaEditor::refreshPadNames()
{
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setSampleName (processor.getPadName (i));
}

void RezalicaEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshPadNames();
    waveform.refresh();
    refreshPadControls();
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setSelected (i == processor.selectedPad.load());
}

void RezalicaEditor::timerCallback()
{
    for (int i = 0; i < rz::numPads; ++i)
    {
        const int h = processor.getEngine().hitCounter[(size_t) i].load();
        if (h != lastHits[(size_t) i])
        {
            lastHits[(size_t) i] = h;
            pads[(size_t) i]->flash();
        }
        else
        {
            pads[(size_t) i]->decay();
        }
    }

    waveform.updatePlayhead();

    // the selected pad can also be changed from the sequencer window
    const int sel = processor.selectedPad.load();
    if (! pads[(size_t) sel]->isMouseButtonDown())
        for (int i = 0; i < rz::numPads; ++i)
            pads[(size_t) i]->setSelected (i == sel);

    if (sequencerWindow != nullptr)
        seqButton.setToggleState (sequencerWindow->isVisible(), juce::dontSendNotification);
}

int RezalicaEditor::chopModeFromBox() const
{
    switch (chopModeBox.getSelectedId())
    {
        case 1: return RezalicaProcessor::chop4;
        case 2: return RezalicaProcessor::chop8;
        case 4: return RezalicaProcessor::chopTransients;
        default: return RezalicaProcessor::chop16;
    }
}

void RezalicaEditor::choosePadFile (int pad)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load sample to pad " + juce::String (pad + 1), juce::File(),
                                                       processor.getFormatManager().getWildcardForAllFormats());
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, pad] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile() && processor.loadFileToPad (pad, f))
            selectPad (pad);
    });
}

void RezalicaEditor::chooseChopFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load a sample to chop across the pads", juce::File(),
                                                       processor.getFormatManager().getWildcardForAllFormats());
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile() && processor.chopFileToPads (f, chopModeFromBox()))
            selectPad (0);
    });
}

void RezalicaEditor::toggleSequencerWindow()
{
    if (sequencerWindow == nullptr)
    {
        sequencerWindow = std::make_unique<rz::SequencerWindow> (processor, lnf);
        sequencerWindow->onClose = [this] { seqButton.setToggleState (false, juce::dontSendNotification); };
    }

    const bool show = ! sequencerWindow->isVisible();
    sequencerWindow->setVisible (show);
    if (show)
        sequencerWindow->toFront (true);
    seqButton.setToggleState (show, juce::dontSendNotification);
}

//==============================================================================
bool RezalicaEditor::keyPressed (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    for (int i = 0; padKeys[i] != 0; ++i)
        if (c == (juce::juce_wchar) padKeys[i])
            return true; // handled in keyStateChanged

    if (key == juce::KeyPress::spaceKey)
    {
        auto& seq = processor.getSequencer();
        seq.internalPlay = ! seq.internalPlay.load();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        processor.stopAll();
        return true;
    }
    return false;
}

bool RezalicaEditor::keyStateChanged (bool)
{
    bool used = false;
    for (int i = 0; i < rz::numPads; ++i)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) padKeys[i]);
        if (down != keyDown[(size_t) i])
        {
            keyDown[(size_t) i] = down;
            processor.triggerPadFromUi (i, down ? 1.0f : 0.0f, down);
            used = true;
        }
    }
    return used;
}

//==============================================================================
void RezalicaEditor::paint (juce::Graphics& g)
{
    using namespace rz::colours;
    g.fillAll (background);

    auto drawPanel = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (outline);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        if (title.isNotEmpty())
        {
            g.setColour (dim);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (title, r.reduced (10, 6).removeFromTop (14), juce::Justification::topLeft);
        }
    };

    // title
    g.setColour (accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("REZALICA", headerArea.withWidth (160), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("16-PAD SLICE SAMPLER", headerArea.withX (headerArea.getX() + 150).withWidth (140), juce::Justification::centredLeft);

    drawPanel (sampleArea, {});
    drawPanel (padArea, "PADS   (MIDI C1-D#2  /  keys 1-4 Q-R A-F Z-V)");
    drawPanel (filterArea, {});
    drawPanel (lofiArea, {});
    drawPanel (delayArea, {});
}

void RezalicaEditor::resized()
{
    auto b = getLocalBounds().reduced (12);

    headerArea = b.removeFromTop (40);
    {
        auto h = headerArea;
        h.removeFromLeft (300);
        masterKnob.setBounds (h.removeFromRight (130).withSizeKeepingCentre (130, 44));
        h.removeFromRight (10);
        auto row = h.withSizeKeepingCentre (h.getWidth(), 30);
        loadChopButton.setBounds (row.removeFromLeft (120)); row.removeFromLeft (6);
        chopModeBox.setBounds (row.removeFromLeft (110)); row.removeFromLeft (6);
        rechopButton.setBounds (row.removeFromLeft (90)); row.removeFromLeft (20);
        seqButton.setBounds (row.removeFromLeft (110)); row.removeFromLeft (6);
        stopButton.setBounds (row.removeFromLeft (90));
    }
    b.removeFromTop (10);

    fxArea = b.removeFromBottom (150);
    b.removeFromBottom (10);

    padArea = b.removeFromRight (420);
    b.removeFromRight (10);
    sampleArea = b;

    // ---- sample + pad strip ----
    {
        auto s = sampleArea.reduced (10);
        waveform.setBounds (s.removeFromTop (s.getHeight() - 190));
        s.removeFromTop (8);
        auto titleRow = s.removeFromTop (24);
        clearPadButton.setBounds (titleRow.removeFromRight (70));
        titleRow.removeFromRight (6);
        loadPadButton.setBounds (titleRow.removeFromRight (70));
        padTitle.setBounds (titleRow);

        s.removeFromTop (6);
        auto knobs = s.removeFromTop (100);
        const int kw = knobs.getWidth() / 7;
        for (auto* k : { &gainKnob, &panKnob, &pitchKnob, &attackKnob, &releaseKnob, &startKnob, &endKnob })
            k->setBounds (knobs.removeFromLeft (kw).reduced (2, 0));

        s.removeFromTop (8);
        auto toggles = s.removeFromTop (30);
        const int tw = toggles.getWidth() / 5;
        for (auto* t : { &loopButton, &reverseButton, &gateButton, &fxButton, &muteButton })
            t->setBounds (toggles.removeFromLeft (tw).reduced (3, 0));
    }

    // ---- pads ----
    {
        auto p = padArea.reduced (10);
        p.removeFromTop (18);
        const int cw = p.getWidth() / 4, ch = p.getHeight() / 4;
        for (int i = 0; i < rz::numPads; ++i)
            pads[(size_t) i]->setBounds (p.getX() + (i % 4) * cw, p.getY() + (i / 4) * ch, cw, ch);
    }

    // ---- effects ----
    {
        auto f = fxArea;
        const int w = (f.getWidth() - 20) / 3;
        filterArea = f.removeFromLeft (w); f.removeFromLeft (10);
        lofiArea = f.removeFromLeft (w);   f.removeFromLeft (10);
        delayArea = f;

        auto layoutGroup = [] (juce::Rectangle<int> area, juce::TextButton& onButton, std::initializer_list<juce::Component*> items)
        {
            auto a = area.reduced (10);
            onButton.setBounds (a.removeFromTop (26).withWidth (juce::jmin (150, a.getWidth())));
            a.removeFromTop (6);
            const int iw = a.getWidth() / (int) items.size();
            for (auto* c : items)
                c->setBounds (a.removeFromLeft (iw).reduced (2, 0));
        };

        layoutGroup (filterArea, filterOnButton, { &cutoffKnob, &resoKnob, &driveKnob });
        layoutGroup (lofiArea, lofiOnButton, { &bitsKnob, &rateKnob, &noiseKnob });

        auto d = delayArea.reduced (10);
        auto top = d.removeFromTop (26);
        delayOnButton.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
        delayDivLabel.setBounds (top.removeFromLeft (40));
        delayDivBox.setBounds (top.removeFromLeft (110));
        d.removeFromTop (6);
        const int iw = d.getWidth() / 3;
        for (auto* c : { &timeKnob, &feedbackKnob, &mixKnob })
            c->setBounds (d.removeFromLeft (iw).reduced (2, 0));
    }
}
