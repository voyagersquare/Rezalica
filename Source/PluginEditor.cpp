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

    // ---- header ----
    for (auto* b : { &loadChopButton, &lazyButton, &seqButton, &stopButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    styleToggle (seqButton, rz::colours::accent2);
    loadChopButton.onClick = [this] { chooseChopFile(); };
    styleToggle (lazyButton, juce::Colour (0xffe0413a));
    lazyButton.setTooltip ("Lazy Chop: plays the selected pad's whole sample - hit any pad, key or MIDI note "
                           "to drop a slice at the playhead. Slices fill A1, A2, A3... Click again to stop.");
    lazyButton.onClick = [this]
    {
        if (processor.isLazyChopActive())
            processor.stopLazyChop();
        else if (processor.startLazyChop())
            selectPad (0);
        lazyButton.setToggleState (processor.isLazyChopActive(), juce::dontSendNotification);
        refreshChopControls();
        waveform.refresh();
    };
    sliceCountLabel.setJustificationType (juce::Justification::centredLeft);
    sliceCountLabel.setColour (juce::Label::textColourId, rz::colours::dim);
    addAndMakeVisible (sliceCountLabel);

    for (int b = 0; b < rz::numBanks; ++b)
    {
        auto& bb = bankButtons[(size_t) b];
        bb.setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + b)));
        bb.setClickingTogglesState (true);
        bb.setRadioGroupId (4711);
        bb.setWantsKeyboardFocus (false);
        bb.setTooltip ("Bank " + bb.getButtonText() + " (pads " + rz::padLabel (b * rz::padsPerBank) + "-" + rz::padLabel (b * rz::padsPerBank + 15) + ")");
        bb.onClick = [this, b] { if (bankButtons[(size_t) b].getToggleState()) setBank (b); };
        addAndMakeVisible (bb);
    }
    seqButton.onClick = [this] { toggleSequencerWindow(); };
    stopButton.onClick = [this] { processor.stopAll(); };
    loadChopButton.setTooltip ("Load an audio file onto the selected pad. Choose a chop mode to slice it.");

    chopModeBox.addItem ("Off (no chop)", 7);
    chopModeBox.addSeparator();
    chopModeBox.addItem ("2 slices", 1);
    chopModeBox.addItem ("4 slices", 2);
    chopModeBox.addItem ("8 slices", 3);
    chopModeBox.addItem ("16 slices", 4);
    chopModeBox.addItem ("Transients", 5);
    chopModeBox.addItem ("Manual", 6);
    chopModeBox.setWantsKeyboardFocus (false);
    chopModeBox.setTooltip ("Changing this re-chops the selected pad's sample automatically");
    chopModeBox.onChange = [this] { processor.setChopMode (chopModeFromBox()); refreshChopControls(); };
    addAndMakeVisible (chopModeBox);

    sensitivitySlider.setSliderStyle (juce::Slider::LinearBar);
    sensitivitySlider.setRange (0.0, 100.0, 1.0);
    sensitivitySlider.textFromValueFunction = [] (double v) { return "Sens " + juce::String ((int) v) + "%"; };
    sensitivitySlider.setWantsKeyboardFocus (false);
    sensitivitySlider.setColour (juce::Slider::trackColourId, rz::colours::accent2.withAlpha (0.5f));
    sensitivitySlider.setTooltip ("Transient sensitivity: higher finds more slices (up to 64)");
    sensitivitySlider.onValueChange = [this]
    {
        processor.setTransientSensitivity ((float) sensitivitySlider.getValue() / 100.0f);
        refreshChopControls();
    };
    addAndMakeVisible (sensitivitySlider);

    attachSlider (masterKnob, "master");
    masterKnob.slider.setSliderStyle (juce::Slider::LinearBar);
    masterKnob.slider.setNumDecimalPlacesToDisplay (1);
    masterKnob.slider.setTextValueSuffix (" dB");
    masterKnob.slider.setColour (juce::Slider::trackColourId, rz::colours::accent.withAlpha (0.5f));

    // ---- waveform ----
    addAndMakeVisible (waveform);
    waveform.onPadSelected = [this] (int pad) { selectPad (pad); };
    waveform.onRegionChanged = [this] { refreshPadControls(); };
    waveform.onFileDropped = [this] (const juce::File& f)
    {
        if (processor.loadFileForChopping (f))
            selectPad (processor.selectedPad.load());
        refreshChopControls();
    };

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
    setupPadToggle (muteButton, [] (rz::PadSettings& s, bool v) { s.mute = v; }, juce::Colour (0xffe0413a));
    loopButton.setTooltip ("Loop the region. Without GATE, hit the pad again to stop it.");
    gateButton.setTooltip ("Play only while the pad / key / MIDI note is held");

    for (auto* b : { &loadPadButton, &clearPadButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    loadPadButton.onClick = [this] { choosePadFile (processor.selectedPad.load()); };
    clearPadButton.onClick = [this] { processor.clearPad (processor.selectedPad.load()); };

    // ---- pads ----
    const int startBank = juce::jlimit (0, rz::numBanks - 1, processor.currentBank.load());
    for (int i = 0; i < rz::padsPerBank; ++i)
    {
        auto pad = std::make_unique<rz::PadButton> (processor, startBank * rz::padsPerBank + i, juce::String::charToString ((juce::juce_wchar) padKeys[i]));
        pad->onSelect = [this] (int idx) { selectPad (idx); };
        pad->onLoadRequest = [this] (int idx) { choosePadFile (idx); };
        pad->onClear = [this] (int idx) { processor.clearPad (idx); };
        pad->onChop = [this] (int idx, int mode) { processor.chopPadSample (idx, mode); selectPad (0); refreshChopControls(); };
        addAndMakeVisible (*pad);
        pads.push_back (std::move (pad));
    }
    for (int i = 0; i < rz::numPads; ++i)
        lastHits[(size_t) i] = processor.getEngine().hitCounter[(size_t) i].load();

    // ---- effects (per pad: they always edit the selected pad) ----
    auto fxKnob = [this] (rz::Knob& k, double lo, double hi, double def, double skewMid, int decimals,
                          const juce::String& suffix, std::atomic<float> rz::PadFx::* member)
    {
        k.slider.setRange (lo, hi, 0.0);
        if (skewMid > 0.0) k.slider.setSkewFactorFromMidPoint (skewMid);
        k.slider.setDoubleClickReturnValue (true, def);
        k.slider.setNumDecimalPlacesToDisplay (decimals);
        k.slider.setTextValueSuffix (suffix);
        k.slider.onValueChange = [this, &k, member]
        {
            processor.getEngine().getPad (processor.selectedPad.load()).effects.*member = (float) k.slider.getValue();
        };
        fxKnobBindings.push_back ({ &k, member });
        addAndMakeVisible (k);
    };
    auto fxToggle = [this] (juce::TextButton& b, std::atomic<bool> rz::PadFx::* member)
    {
        styleToggle (b);
        b.onClick = [this, &b, member]
        {
            processor.getEngine().getPad (processor.selectedPad.load()).effects.*member = b.getToggleState();
            refreshPadNames(); // FX badge on the pad
        };
        fxToggleBindings.push_back ({ &b, member });
        addAndMakeVisible (b);
    };

    fxToggle (filterOnButton, &rz::PadFx::filterOn);
    fxKnob (cutoffKnob, 20.0, 20000.0, 20000.0, 1000.0, 0, " Hz", &rz::PadFx::cutoff);
    fxKnob (resoKnob, 0.0, 1.0, 0.1, 0.0, 2, "", &rz::PadFx::resonance);
    fxKnob (driveKnob, 0.0, 1.0, 0.0, 0.0, 2, "", &rz::PadFx::drive);
    fxToggle (lofiOnButton, &rz::PadFx::lofiOn);
    fxKnob (bitsKnob, 2.0, 16.0, 10.0, 0.0, 1, "", &rz::PadFx::bits);
    fxKnob (rateKnob, 1.0, 32.0, 3.0, 6.0, 1, "", &rz::PadFx::rate);
    fxKnob (noiseKnob, 0.0, 1.0, 0.2, 0.0, 2, "", &rz::PadFx::noise);
    fxToggle (delayOnButton, &rz::PadFx::delayOn);
    fxKnob (timeKnob, 10.0, 2000.0, 375.0, 400.0, 0, " ms", &rz::PadFx::delayTime);
    fxKnob (feedbackKnob, 0.0, 0.95, 0.35, 0.0, 2, "", &rz::PadFx::feedback);
    fxKnob (mixKnob, 0.0, 1.0, 0.3, 0.0, 2, "", &rz::PadFx::delayMix);
    fxToggle (reverbOnButton, &rz::PadFx::reverbOn);
    fxKnob (reverbSizeKnob, 0.0, 1.0, 0.6, 0.0, 2, "", &rz::PadFx::reverbSize);
    fxKnob (reverbDampKnob, 0.0, 1.0, 0.4, 0.0, 2, "", &rz::PadFx::reverbDamp);
    fxKnob (reverbWidthKnob, 0.0, 1.0, 1.0, 0.0, 2, "", &rz::PadFx::reverbWidth);
    fxKnob (reverbMixKnob, 0.0, 1.0, 0.3, 0.0, 2, "", &rz::PadFx::reverbMix);

    delayDivBox.addItemList (RezalicaProcessor::getDelayDivisionNames(), 1);
    delayDivBox.setWantsKeyboardFocus (false);
    delayDivBox.onChange = [this]
    {
        processor.getEngine().getPad (processor.selectedPad.load()).effects.delayDiv = delayDivBox.getSelectedId() - 1;
        timeKnob.setEnabled (delayDivBox.getSelectedId() == 1);
    };
    addAndMakeVisible (delayDivBox);
    delayDivLabel.setText ("SYNC", juce::dontSendNotification);
    delayDivLabel.setJustificationType (juce::Justification::centred);
    delayDivLabel.setColour (juce::Label::textColourId, rz::colours::dim);
    addAndMakeVisible (delayDivLabel);

    fxTitle.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    fxTitle.setColour (juce::Label::textColourId, rz::colours::accent);
    addAndMakeVisible (fxTitle);

    for (auto* b : { &copyFxButton, &pasteFxButton, &fxToAllButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    copyFxButton.setTooltip ("Copy the selected pad's effects");
    pasteFxButton.setTooltip ("Paste copied effects onto the selected pad");
    fxToAllButton.setTooltip ("Copy the selected pad's effects to every pad of the current bank");
    copyFxButton.onClick = [this]
    {
        fxClipboard = processor.getEngine().getPad (processor.selectedPad.load()).effects.toValueTree();
        pasteFxButton.setEnabled (true);
    };
    pasteFxButton.setEnabled (false);
    pasteFxButton.onClick = [this]
    {
        if (fxClipboard.isValid())
        {
            processor.getEngine().getPad (processor.selectedPad.load()).effects.fromValueTree (fxClipboard);
            refreshFxControls();
            refreshPadNames();
        }
    };
    fxToAllButton.onClick = [this]
    {
        const auto fx = processor.getEngine().getPad (processor.selectedPad.load()).effects.toValueTree();
        const int first = processor.currentBank.load() * rz::padsPerBank;
        for (int i = first; i < first + rz::padsPerBank; ++i)
            processor.getEngine().getPad (i).effects.fromValueTree (fx);
        refreshPadNames();
    };

    processor.padsChanged.addChangeListener (this);

    lastPlayedCounter = processor.getEngine().playedCounter.load();

    setSize (1180, 748);
    setBank (startBank);
    selectPad (juce::jlimit (0, rz::numPads - 1, processor.selectedPad.load()));
    refreshPadNames();
    refreshChopControls();
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
void RezalicaEditor::setBank (int bank)
{
    bank = juce::jlimit (0, rz::numBanks - 1, bank);
    processor.currentBank = bank;
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setIndex (bank * rz::padsPerBank + i);
    for (int b = 0; b < rz::numBanks; ++b)
        bankButtons[(size_t) b].setToggleState (b == bank, juce::dontSendNotification);
    refreshPadNames();
    for (auto& p : pads)
        p->setSelected (p->getIndex() == processor.selectedPad.load());
    repaint (padArea);
}

void RezalicaEditor::selectPad (int pad, bool grabFocus)
{
    processor.selectedPad = pad;
    if (pad / rz::padsPerBank != processor.currentBank.load())
        setBank (pad / rz::padsPerBank);
    for (auto& p : pads)
        p->setSelected (p->getIndex() == pad);
    waveform.setPad (pad);
    refreshPadControls();
    refreshFxControls();
    if (grabFocus)
        grabKeyboardFocus();
}

void RezalicaEditor::refreshChopControls()
{
    const int mode = processor.getChopMode();
    const int id = mode == RezalicaProcessor::chop2 ? 1 : mode == RezalicaProcessor::chop4 ? 2
                 : mode == RezalicaProcessor::chop8 ? 3 : mode == RezalicaProcessor::chopTransients ? 5
                 : mode == RezalicaProcessor::chopManual ? 6 : mode == RezalicaProcessor::chopOff ? 7 : 4;
    chopModeBox.setSelectedId (id, juce::dontSendNotification);

    const bool transient = mode == RezalicaProcessor::chopTransients;
    if (sensitivitySlider.isVisible() != transient)
    {
        sensitivitySlider.setVisible (transient);
        resized();
    }
    if (! sensitivitySlider.isMouseButtonDown())
        sensitivitySlider.setValue (processor.getTransientSensitivity() * 100.0, juce::dontSendNotification);

    const int n = processor.getNumSlices();
    juce::String text;
    if (n > rz::padsPerBank)
        text = juce::String (n) + " slices  A-" + juce::String::charToString ((juce::juce_wchar) ('A' + (n - 1) / rz::padsPerBank));
    else if (n > 0)
        text = juce::String (n) + " slices";
    if (processor.isLazyChopActive())
        text = "LAZY: " + juce::String (n) + " slices";
    sliceCountLabel.setText (text, juce::dontSendNotification);
}

void RezalicaEditor::refreshPadControls()
{
    const int pad = processor.selectedPad.load();
    auto& s = processor.getEngine().getPad (pad);

    auto name = processor.getPadName (pad);
    padTitle.setText ("PAD " + rz::padLabel (pad) + (name.isNotEmpty() ? "  -  " + name : juce::String ("  -  empty")),
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
    muteButton.setToggleState (s.mute.load(), juce::dontSendNotification);
}

void RezalicaEditor::refreshFxControls()
{
    const int pad = processor.selectedPad.load();
    const auto& fx = processor.getEngine().getPad (pad).effects;
    for (auto& b : fxKnobBindings)
        b.knob->slider.setValue ((fx.*(b.member)).load(), juce::dontSendNotification);
    for (auto& b : fxToggleBindings)
        b.button->setToggleState ((fx.*(b.member)).load(), juce::dontSendNotification);
    delayDivBox.setSelectedId (fx.delayDiv.load() + 1, juce::dontSendNotification);
    timeKnob.setEnabled (fx.delayDiv.load() == 0);
    fxTitle.setText ("EFFECTS FOR PAD " + rz::padLabel (pad) + "  (only this pad)", juce::dontSendNotification);
}

void RezalicaEditor::refreshPadNames()
{
    for (auto& p : pads)
    {
        p->setSampleName (processor.getPadName (p->getIndex()));
        p->setHasFx (processor.getEngine().getPad (p->getIndex()).effects.anyOn());
    }

    for (int b = 0; b < rz::numBanks; ++b)
    {
        bool used = false;
        for (int i = 0; i < rz::padsPerBank && ! used; ++i)
            used = processor.getEngine().getPadSample (b * rz::padsPerBank + i) != nullptr;
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + b));
        bankButtons[(size_t) b].setButtonText (used ? letter + " *" : letter);
    }
}

void RezalicaEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshPadNames();
    refreshChopControls();
    waveform.refresh();
    refreshPadControls();
    refreshFxControls();
    if (processor.currentBank.load() != pads[0]->getIndex() / rz::padsPerBank)
        setBank (processor.currentBank.load());
    for (auto& p : pads)
        p->setSelected (p->getIndex() == processor.selectedPad.load());
}

void RezalicaEditor::timerCallback()
{
    for (int i = 0; i < rz::numPads; ++i)
    {
        const int h = processor.getEngine().hitCounter[(size_t) i].load();
        const bool hit = h != lastHits[(size_t) i];
        lastHits[(size_t) i] = h;
        for (auto& p : pads)
            if (p->getIndex() == i)
                hit ? p->flash() : p->decay();
    }

    // Lazy Chop: collect the slice markers the audio thread recorded
    if (processor.isLazyChopActive() || lastLazyState)
    {
        processor.updateLazyChop();
        const bool lazy = processor.isLazyChopActive();
        lazyButton.setToggleState (lazy, juce::dontSendNotification);
        if (lazy != lastLazyState)
            refreshChopControls();
        lastLazyState = lazy;
        lastPlayedCounter = processor.getEngine().playedCounter.load(); // pads hits are markers now, don't follow them
    }

    // follow pads played from MIDI / computer keyboard: show them in the waveform
    const int played = processor.getEngine().playedCounter.load();
    if (played != lastPlayedCounter)
    {
        lastPlayedCounter = played;
        const int pad = processor.getEngine().lastPlayedPad.load();
        if (pad >= 0 && pad < rz::numPads && pad != processor.selectedPad.load())
            selectPad (pad, false);
    }

    waveform.updatePlayhead();

    // the selected pad can also be changed from the sequencer window
    const int sel = processor.selectedPad.load();
    if (processor.currentBank.load() != pads[0]->getIndex() / rz::padsPerBank)
        setBank (processor.currentBank.load()); // bank changed from the sequencer window
    for (auto& p : pads)
        p->setSelected (p->getIndex() == sel);

    if (sequencerWindow != nullptr)
        seqButton.setToggleState (sequencerWindow->isVisible(), juce::dontSendNotification);
}

int RezalicaEditor::chopModeFromBox() const
{
    switch (chopModeBox.getSelectedId())
    {
        case 1: return RezalicaProcessor::chop2;
        case 2: return RezalicaProcessor::chop4;
        case 3: return RezalicaProcessor::chop8;
        case 5: return RezalicaProcessor::chopTransients;
        case 6: return RezalicaProcessor::chopManual;
        case 7: return RezalicaProcessor::chopOff;
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
    fileChooser = std::make_unique<juce::FileChooser> ("Load a sample (choose a chop mode afterwards)", juce::File(),
                                                       processor.getFormatManager().getWildcardForAllFormats());
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile() && processor.loadFileForChopping (f))
        {
            selectPad (processor.selectedPad.load());
            refreshChopControls();
        }
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
    for (int i = 0; i < rz::padsPerBank; ++i)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) padKeys[i]);
        if (down != keyDown[(size_t) i])
        {
            keyDown[(size_t) i] = down;
            if (down)
                keyPad[(size_t) i] = processor.currentBank.load() * rz::padsPerBank + i;
            processor.triggerPadFromUi (keyPad[(size_t) i], down ? 1.0f : 0.0f, down);
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
    g.drawText ("REZALICA", headerArea.withWidth (160).withTrimmedBottom (10), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("SLICE SAMPLER  4 x 16 PADS", headerArea.withWidth (160).withTrimmedTop (28), juce::Justification::centredLeft);

    drawPanel (sampleArea, {});
    {
        const int bank = processor.currentBank.load();
        const int lo = RezalicaProcessor::midiBaseNote + bank * rz::padsPerBank;
        drawPanel (padArea, "BANK " + juce::String::charToString ((juce::juce_wchar) ('A' + bank))
                              + "   MIDI " + juce::MidiMessage::getMidiNoteName (lo, true, true, 3)
                              + "-" + juce::MidiMessage::getMidiNoteName (lo + 15, true, true, 3)
                              + "   keys 1-4 Q-R A-F Z-V");
    }
    drawPanel (filterArea, {});
    drawPanel (lofiArea, {});
    drawPanel (delayArea, {});
    drawPanel (reverbArea, {});
}

void RezalicaEditor::resized()
{
    auto b = getLocalBounds().reduced (12);

    headerArea = b.removeFromTop (40);
    {
        auto h = headerArea;
        h.removeFromLeft (175);
        masterKnob.setBounds (h.removeFromRight (130).withSizeKeepingCentre (130, 44));
        h.removeFromRight (10);
        auto row = h.withSizeKeepingCentre (h.getWidth(), 30);
        loadChopButton.setBounds (row.removeFromLeft (120)); row.removeFromLeft (6);
        chopModeBox.setBounds (row.removeFromLeft (120)); row.removeFromLeft (6);
        if (sensitivitySlider.isVisible())
        {
            sensitivitySlider.setBounds (row.removeFromLeft (120));
            row.removeFromLeft (6);
        }
        lazyButton.setBounds (row.removeFromLeft (100)); row.removeFromLeft (6);
        sliceCountLabel.setBounds (row.removeFromLeft (130));
        stopButton.setBounds (row.removeFromRight (90)); row.removeFromRight (6);
        seqButton.setBounds (row.removeFromRight (110));
    }
    b.removeFromTop (10);

    fxArea = b.removeFromBottom (176);
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
        const int tw = toggles.getWidth() / 4;
        for (auto* t : { &loopButton, &reverseButton, &gateButton, &muteButton })
            t->setBounds (toggles.removeFromLeft (tw).reduced (3, 0));
    }

    // ---- pads ----
    {
        auto p = padArea.reduced (10);
        auto bankRow = p.removeFromTop (24);
        for (int bi = rz::numBanks; --bi >= 0;)
        {
            bankButtons[(size_t) bi].setBounds (bankRow.removeFromRight (40));
            bankRow.removeFromRight (4);
        }
        p.removeFromTop (6);
        const int cw = p.getWidth() / 4, ch = p.getHeight() / 4;
        for (int i = 0; i < rz::padsPerBank; ++i)
            pads[(size_t) i]->setBounds (p.getX() + (i % 4) * cw, p.getY() + (i / 4) * ch, cw, ch);
    }

    // ---- effects ----
    {
        auto f = fxArea;
        auto titleRow = f.removeFromTop (22);
        fxToAllButton.setBounds (titleRow.removeFromRight (130));
        titleRow.removeFromRight (6);
        pasteFxButton.setBounds (titleRow.removeFromRight (90));
        titleRow.removeFromRight (6);
        copyFxButton.setBounds (titleRow.removeFromRight (90));
        fxTitle.setBounds (titleRow);
        f.removeFromTop (4);
        const int w = (f.getWidth() - 30) / 4;
        filterArea = f.removeFromLeft (w); f.removeFromLeft (10);
        lofiArea = f.removeFromLeft (w);   f.removeFromLeft (10);
        delayArea = f.removeFromLeft (w);  f.removeFromLeft (10);
        reverbArea = f;

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
        layoutGroup (reverbArea, reverbOnButton, { &reverbSizeKnob, &reverbDampKnob, &reverbWidthKnob, &reverbMixKnob });

        auto d = delayArea.reduced (10);
        auto top = d.removeFromTop (26);
        delayOnButton.setBounds (top.removeFromLeft (90));
        top.removeFromLeft (4);
        delayDivLabel.setBounds (top.removeFromLeft (40));
        delayDivBox.setBounds (top.removeFromLeft (juce::jmin (110, top.getWidth())));
        d.removeFromTop (6);
        const int iw = d.getWidth() / 3;
        for (auto* c : { &timeKnob, &feedbackKnob, &mixKnob })
            c->setBounds (d.removeFromLeft (iw).reduced (2, 0));
    }
}
