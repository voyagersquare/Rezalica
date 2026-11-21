#include "PluginEditor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

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
    : AudioProcessorEditor (&p), processor (p), metronome (p.getSequencer()), ledBpm (p.getSequencer()), waveform (p)
{
    // the standalone sampler always starts empty: no samples, no steps, no notes
    // (JUCE's standalone wrapper would bring back the last session; saved work is in the project files,
    //  FILE > Open Recent and the crash recovery)
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        static bool startedFresh = false;
        if (! startedFresh)
        {
            startedFresh = true;
            processor.newProject();
        }
    }

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
    for (auto* b : { &loadChopButton, &lazyButton, &seqButton, &pianoRollButton, &stopButton, &undoButton, &redoButton })
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
        else if (processor.pushUndo ("Lazy chop"), processor.startLazyChop())
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
        bb.setTooltip ("Bank " + bb.getButtonText() + " (pads " + rz::padLabel (b * rz::padsPerBank) + "-" + rz::padLabel (b * rz::padsPerBank + 15) + ")"
                       "   Ctrl+click: bank volume   right-click: mute / copy / paste");
        bb.onClick = [this, b] { if (bankButtons[(size_t) b].getToggleState()) setBank (b); };
        bb.processor = &processor;
        bb.bank = b;
        bb.onCtrlClick = [this, b] { showBankVolume (b); };
        bb.onRightClick = [this, b] { showBankMenu (b); };
        addAndMakeVisible (bb);
    }
    seqButton.onClick = [this] { toggleSequencerWindow(); };

    // standalone app: project files and audio device settings (in a DAW the DAW does this)
    isStandalone = processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    for (auto* b : { &fileButton, &sceneButton, &samplingButton, &browserButton, &masterFxButton })
    {
        b->setWantsKeyboardFocus (false);
        addChildComponent (*b);
        b->setVisible (isStandalone);
    }
    // transport keys (symbols) next to the LED tempo display
    for (auto* k : { &recKey, &playKey, &pauseKey, &stopKey, &clickKey })
    {
        addChildComponent (*k);
        k->setVisible (isStandalone);
    }
    metronome.setShowClickButton (false); // the metronome key sits next to the transport now
    clickKey.onClick = [this]
    {
        auto& s = processor.getSequencer();
        s.metronome = ! s.metronome.load();
        grabKeyboardFocus();
    };
    clickKey.onRightClick = [this]
    {
        auto& s = processor.getSequencer();
        juce::PopupMenu m;
        m.addSectionHeader ("Click volume");
        for (int i = 1; i <= 10; ++i)
            m.addItem (i, juce::String (i * 10) + " %", true, juce::roundToInt (s.metronomeLevel.load() * 10.0f) == i);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&clickKey), [this] (int r)
        {
            if (r > 0) processor.getSequencer().metronomeLevel = (float) r / 10.0f;
        });
    };
    recKey.onClick = [this]
    {
        auto& s = processor.getSequencer();
        s.recording = ! s.recording.load();
        grabKeyboardFocus();
    };
    playKey.onClick = [this]
    {
        auto& s = processor.getSequencer();
        if (s.isPaused()) s.resumePlayback();
        else if (! s.internalPlay.load()) s.playPattern();
        grabKeyboardFocus();
    };
    pauseKey.onClick = [this]
    {
        auto& s = processor.getSequencer();
        if (s.isPaused()) s.resumePlayback(); else s.pausePlayback();
        grabKeyboardFocus();
    };
    stopKey.onClick = [this] { processor.getSequencer().stopPlayback(); grabKeyboardFocus(); };
    addChildComponent (metronome);
    metronome.setVisible (isStandalone);
    addChildComponent (autosaveLabel);
    autosaveLabel.setVisible (isStandalone);
    autosaveLabel.setColour (juce::Label::textColourId, rz::colours::dim);
    autosaveLabel.setFont (juce::FontOptions (11.0f));
    autosaveLabel.setJustificationType (juce::Justification::centredRight);
    styleToggle (samplingButton, juce::Colour (0xffe0413a));
    styleToggle (browserButton, rz::colours::accent2);
    styleToggle (masterFxButton, rz::colours::accent);
    samplingButton.setTooltip ("Record from the audio input or resample Rezalica's output onto the selected pad");
    browserButton.setTooltip ("Sample browser: preview files and load them onto pads");
    masterFxButton.setTooltip ("EQ, compressor and limiter on Rezalica's output");
    samplingButton.onClick = [this] { toggleToolWindow (samplingWindow, samplingButton, 0); };
    browserButton.onClick = [this] { toggleBrowser(); };
    sceneButton.setButtonText ("SONG");
    sceneButton.setTooltip ("Song: chain patterns into the scene, the scene into a song");
    tempoSlider.setSliderStyle (juce::Slider::LinearBar);
    tempoSlider.setRange (40.0, 240.0, 0.1);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setWantsKeyboardFocus (false);
    tempoSlider.setColour (juce::Slider::trackColourId, rz::colours::panelLight.brighter (0.15f));
    tempoSlider.setTooltip ("Project tempo: the sequencer, song, metronome and warped samples follow it (drag or double-click to type)");
    tempoSlider.onValueChange = [this] { processor.getSequencer().bpm = (float) tempoSlider.getValue(); };
    tempoSlider.setValue (processor.getSequencer().bpm.load(), juce::dontSendNotification);
    addChildComponent (tempoSlider);   // kept for its callbacks; the LED display shows the tempo now
    addAndMakeVisible (ledBpm);
    if (isStandalone)
    {
        loadChopButton.setVisible (false); // the BROWSER loads samples in the standalone app
        stopButton.setVisible (false);
    }
    masterFxButton.onClick = [this] { toggleToolWindow (masterFxWindow, masterFxButton, 2); };
    if (isStandalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            holder->getMuteInputValue().setValue (false); // the input is only recorded, never sent to the output
            // MIDI keyboards / controllers work right away: if no MIDI input is switched on yet, switch them all on
            auto& dm = holder->deviceManager;
            const auto inputs = juce::MidiInput::getAvailableDevices();
            bool anyOn = false;
            for (auto& d : inputs) anyOn = anyOn || dm.isMidiInputDeviceEnabled (d.identifier);
            if (! anyOn)
                for (auto& d : inputs) dm.setMidiInputDeviceEnabled (d.identifier, true);
        }
    fileButton.setTooltip ("New / Open / Save / Save As project, Export Song, Audio Settings");
    sceneButton.setTooltip ("Scenes and song: chain patterns into scenes, scenes into a song");
    styleToggle (sceneButton, juce::Colour (0xffffc94a));
    styleToggle (playButton, juce::Colour (0xff6fd36f));
    playButton.setTooltip ("Play the selected pattern: step sequencer + piano roll (space bar). Independent of scenes / song");
    stopPlayButton.setTooltip ("Stop (space bar)");
    playButton.onClick = [this]
    {
        auto& s = processor.getSequencer();
        if (playButton.getToggleState()) s.playPattern(); else s.stopPlayback();
        grabKeyboardFocus();
    };
    stopPlayButton.onClick = [this] { processor.getSequencer().stopPlayback(); grabKeyboardFocus(); };
    fileButton.onClick = [this] { showFileMenu(); };
    sceneButton.onClick = [this] { toggleSceneWindow(); };
    undoButton.onClick = [this] { if (processor.undo()) selectPad (processor.selectedPad.load()); };
    redoButton.onClick = [this] { if (processor.redo()) selectPad (processor.selectedPad.load()); };
    styleToggle (pianoRollButton, rz::colours::accent2);
    pianoRollButton.setTooltip ("Piano roll: draw notes for melodic samples and drums");
    pianoRollButton.onClick = [this] { togglePianoRoll(); };
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
    chopModeBox.onChange = [this] { processor.pushUndo ("Chop mode"); processor.setChopMode (chopModeFromBox()); refreshChopControls(); };
    addAndMakeVisible (chopModeBox);

    sensitivitySlider.setSliderStyle (juce::Slider::LinearBar);
    sensitivitySlider.setRange (0.0, 100.0, 1.0);
    sensitivitySlider.textFromValueFunction = [] (double v) { return "Sens " + juce::String ((int) v) + "%"; };
    sensitivitySlider.setWantsKeyboardFocus (false);
    sensitivitySlider.setColour (juce::Slider::trackColourId, rz::colours::accent2.withAlpha (0.5f));
    sensitivitySlider.setTooltip ("Transient sensitivity: higher finds more slices (up to 64)");
    sensitivitySlider.onDragStart = [this] { processor.pushUndo ("Transient sensitivity"); };
    sensitivitySlider.onValueChange = [this]
    {
        processor.setTransientSensitivity ((float) sensitivitySlider.getValue() / 100.0f);
        refreshChopControls();
    };
    addAndMakeVisible (sensitivitySlider);

    attachSlider (masterKnob, "master");
    masterKnob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    masterKnob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14);
    masterKnob.slider.setColour (juce::Slider::textBoxTextColourId, rz::colours::ink);
    masterKnob.label.setColour (juce::Label::textColourId, rz::colours::ink);
    masterKnob.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    masterKnob.slider.setNumDecimalPlacesToDisplay (1);
    masterKnob.slider.setTextValueSuffix (" dB");
    masterKnob.slider.setColour (juce::Slider::trackColourId, rz::colours::accent.withAlpha (0.5f));

    // ---- waveform ----
    addAndMakeVisible (waveform);
    waveform.onPadSelected = [this] (int pad) { selectPad (pad); };
    waveform.onRegionChanged = [this] { refreshPadControls(); };
    waveform.onFileDropped = [this] (const juce::File& f)
    {
        processor.pushUndo ("Load sample");
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
        k.slider.onDragStart = [this, &k] { processor.pushUndo ("Pad " + k.label.getText().toLowerCase()); };
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
    setupPadKnob (releaseKnob, 0.0, 4000.0, 0.1, 30.0, " ms", [] (rz::PadSettings& s, float v) { s.releaseMs = v; });
    setupPadKnob (startKnob, 0.0, 1.0, 0.0001, 0.0, "", [] (rz::PadSettings& s, float v) { s.start = juce::jmin (v, s.end.load() - 0.0001f); });
    setupPadKnob (endKnob, 0.0, 1.0, 0.0001, 1.0, "", [] (rz::PadSettings& s, float v) { s.end = juce::jmax (v, s.start.load() + 0.0001f); });
    attackKnob.slider.setSkewFactorFromMidPoint (100.0);
    releaseKnob.slider.setSkewFactorFromMidPoint (300.0);
    pitchKnob.slider.setNumDecimalPlacesToDisplay (1);
    panKnob.slider.getProperties().set ("bipolar", true);
    pitchKnob.slider.getProperties().set ("bipolar", true);

    auto setupPadToggle = [this] (juce::TextButton& b, std::function<void (rz::PadSettings&, bool)> apply, juce::Colour on)
    {
        styleToggle (b, on);
        b.onClick = [this, &b, apply]
        {
            processor.pushUndo ("Pad " + b.getButtonText().toLowerCase());
            apply (processor.getEngine().getPad (processor.selectedPad.load()), b.getToggleState());
        };
        addAndMakeVisible (b);
    };
    setupPadToggle (loopButton, [] (rz::PadSettings& s, bool v) { s.loop = v; }, rz::colours::accent);
    setupPadToggle (reverseButton, [] (rz::PadSettings& s, bool v) { s.reverse = v; }, rz::colours::accent);
    setupPadToggle (gateButton, [] (rz::PadSettings& s, bool v) { s.gate = v; }, rz::colours::accent);
    setupPadToggle (muteButton, [] (rz::PadSettings& s, bool v) { s.mute = v; }, juce::Colour (0xffe0413a));
    gateButton.setTooltip ("Play only while the pad / key / MIDI note is held");

    chokeBox.addItem ("No choke", 1);
    for (int g = 1; g <= 8; ++g)
        chokeBox.addItem ("Choke " + juce::String (g), g + 1);
    chokeBox.setWantsKeyboardFocus (false);
    chokeBox.setTooltip ("Choke group: a hit on this pad stops the other pads of the same group (e.g. open / closed hi-hat)");
    chokeBox.onChange = [this]
    {
        processor.pushUndo ("Choke group");
        processor.getEngine().getPad (processor.selectedPad.load()).chokeGroup = chokeBox.getSelectedId() - 1;
    };
    addAndMakeVisible (chokeBox);

    trimButton.setTooltip ("Trim / truncate: keep only the orange part between the markers as a new sample on this pad "
                           "(saved in Documents/Rezalica/Trimmed)");
    trimButton.onClick = [this]
    {
        const int pad = processor.selectedPad.load();
        processor.pushUndo ("Trim");
        if (processor.trimPad (pad))
            selectPad (pad);
    };
    loopButton.setTooltip ("Loop: plays the part between the markers over and over while you hold the pad / key / MIDI note");
    styleToggle (warpButton);
    warpButton.setTooltip ("Time stretch / warp this pad's sample on or off (settings: STRETCH...)");
    warpButton.onClick = [this]
    {
        const int pad = processor.selectedPad.load();
        auto ws = processor.getWarpSettings (pad);
        ws.enabled = warpButton.getToggleState();
        if (processor.getEngine().getPadSample (pad) == nullptr)
            warpButton.setToggleState (false, juce::dontSendNotification);
        else
        {
            processor.pushUndo (ws.enabled ? "Warp on" : "Warp off");
            processor.requestWarp (pad, ws); // stretches in the background
        }
    };
    stretchButton.setTooltip ("Open the time stretch / warp window");
    stretchButton.onClick = [this] { showStretchWindow(); };
    for (auto* b : { &renderButton, &clearPadButton, &trimButton, &warpButton, &stretchButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    renderButton.setTooltip ("Render / consolidate: saves the warped (time-stretched) sample as a new WAV "
                             "(Documents/Rezalica/Rendered) and uses it instead of the original");
    renderButton.onClick = [this]
    {
        const int pad = processor.selectedPad.load();
        processor.pushUndo ("Render");
        if (processor.consolidatePad (pad))
            selectPad (pad);
    };
    clearPadButton.onClick = [this] { processor.pushUndo ("Clear pad"); processor.clearPad (processor.selectedPad.load()); };

    // ---- pads ----
    const int startBank = juce::jlimit (0, rz::numBanks - 1, processor.currentBank.load());
    for (int i = 0; i < rz::padsPerBank; ++i)
    {
        auto pad = std::make_unique<rz::PadButton> (processor, startBank * rz::padsPerBank + i, juce::String::charToString ((juce::juce_wchar) padKeys[i]));
        pad->onSelect = [this] (int idx)
        {
            // a plain click: one pad, and the start of a Shift+click range
            multiSelection.fill (false);
            selectionAnchor = idx;
            refreshMultiSelection();
            selectPad (idx);
        };
        pad->onMultiSelect = [this] (int idx, juce::ModifierKeys mods)
        {
            if (mods.isShiftDown())
            {
                multiSelection.fill (false);
                for (int i = juce::jmin (selectionAnchor, idx); i <= juce::jmax (selectionAnchor, idx); ++i)
                    multiSelection[(size_t) i] = true;
            }
            else
            {
                if (multiSelectedCount() == 0)
                    multiSelection[(size_t) selectionAnchor] = true;
                multiSelection[(size_t) idx] = ! multiSelection[(size_t) idx];
            }
            refreshMultiSelection();
            return true;
        };
        pad->getMultiCount = [this] { return multiSelectedCount(); };
        pad->onClearSelected = [this] { clearSelectedPads(); };
        pad->onDeselect = [this] { multiSelection.fill (false); refreshMultiSelection(); };
        pad->onLoadRequest = [this] (int idx) { choosePadFile (idx); };
        pad->onClear = [this] (int idx) { processor.pushUndo ("Clear pad"); processor.clearPad (idx); };
        pad->onCopy = [this] (int idx) { processor.copyPad (idx); };
        pad->onPaste = [this] (int idx) { processor.pushUndo ("Paste pad"); processor.pastePad (idx); selectPad (idx); };
        pad->onChop = [this] (int idx, int mode) { processor.pushUndo ("Chop"); processor.chopPadSample (idx, mode); selectPad (0); refreshChopControls(); };
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
        k.slider.onDragStart = [this, &k] { processor.pushUndo ("Effect " + k.label.getText().toLowerCase()); };
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
            processor.pushUndo ("Effect " + b.getButtonText().toLowerCase());
            processor.getEngine().getPad (processor.selectedPad.load()).effects.*member = b.getToggleState();
            refreshPadNames(); // FX badge on the pad
        };
        fxToggleBindings.push_back ({ &b, member });
        addAndMakeVisible (b);
    };

    fxToggle (filterOnButton, &rz::PadFx::filterOn);
    fxKnob (cutoffKnob, 20.0, 20000.0, 20000.0, 1000.0, 0, " Hz", &rz::PadFx::cutoff);
    fxKnob (resoKnob, 0.0, 1.0, 0.0, 0.0, 2, "", &rz::PadFx::resonance);
    fxToggle (satOnButton, &rz::PadFx::satOn);
    fxKnob (tubeKnob, 0.0, 1.0, 0.0, 0.0, 2, "", &rz::PadFx::tube);
    fxKnob (tapeKnob, 0.0, 1.0, 0.0, 0.0, 2, "", &rz::PadFx::tape);
    fxKnob (solidKnob, 0.0, 1.0, 0.0, 0.0, 2, "", &rz::PadFx::solid);
    tubeKnob.slider.setTooltip ("Tube: warm, mostly even harmonics");
    tapeKnob.slider.setTooltip ("Tape: soft compression and gently rounded highs");
    solidKnob.slider.setTooltip ("Solid state: firmer transistor-style clipping, odd harmonics");

    filterTypeBox.addItemList (rz::FxChain::filterTypeNames(), 1);
    filterTypeBox.setWantsKeyboardFocus (false);
    filterTypeBox.setTooltip ("Analog ladder filter: low-pass, high-pass or band-pass, 12 or 24 dB/oct");
    filterTypeBox.onChange = [this]
    {
        processor.pushUndo ("Filter type");
        auto& fx = processor.getEngine().getPad (processor.selectedPad.load()).effects;
        const int type = filterTypeBox.getSelectedId() - 1;
        fx.filterType = type;
        // sensible cutoff when switching between low-pass / high-pass / band-pass
        const float c = fx.cutoff.load();
        if (type >= 2 && type <= 3 && c > 15000.0f) fx.cutoff = 20.0f;
        if (type <= 1 && c < 30.0f)                 fx.cutoff = 20000.0f;
        if (type >= 4 && (c < 30.0f || c > 15000.0f)) fx.cutoff = 1000.0f;
        cutoffKnob.slider.setValue (fx.cutoff.load(), juce::dontSendNotification);
    };
    addAndMakeVisible (filterTypeBox);
    filterTypeLabel.setText ("TYPE", juce::dontSendNotification);
    filterTypeLabel.setJustificationType (juce::Justification::centred);
    filterTypeLabel.setColour (juce::Label::textColourId, rz::colours::dim);
    addAndMakeVisible (filterTypeLabel);
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
        processor.pushUndo ("Delay sync");
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
            processor.pushUndo ("Paste effects");
            processor.getEngine().getPad (processor.selectedPad.load()).effects.fromValueTree (fxClipboard);
            refreshFxControls();
            refreshPadNames();
        }
    };
    fxToAllButton.onClick = [this]
    {
        processor.pushUndo ("Effects to bank");
        const auto fx = processor.getEngine().getPad (processor.selectedPad.load()).effects.toValueTree();
        const int first = processor.currentBank.load() * rz::padsPerBank;
        for (int i = first; i < first + rz::padsPerBank; ++i)
            processor.getEngine().getPad (i).effects.fromValueTree (fx);
        refreshPadNames();
    };

    processor.padsChanged.addChangeListener (this);

    lastPlayedCounter = processor.getEngine().playedCounter.load();

    // on the silver faceplate the print is dark
    sliceCountLabel.setColour (juce::Label::textColourId, rz::colours::ink);
    autosaveLabel.setColour (juce::Label::textColourId, rz::colours::ink.withAlpha (0.7f));
    fxTitle.setColour (juce::Label::textColourId, juce::Colours::white);
    if (isStandalone)
    {
        // the window can be resized and maximised (full screen); the layout grows with it
        setResizable (true, false);
        setResizeLimits (1400, 740, 8000, 5000);
    }
    setSize (isStandalone ? 1530 : 1240, isStandalone ? 784 : 772);
    if (isStandalone)
    {
        lastAutosaveTime = juce::Time::getMillisecondCounterHiRes();
        if (rz::AppSettings::get().getBoolValue ("browserOpen", false))
            toggleBrowser(); // the docked browser stays open between sessions
        juce::Component::SafePointer<RezalicaEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->checkCrashRecovery(); });
    }
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
    dockedSequencer = nullptr;
    dockedPianoRoll = nullptr;
    samplingWindow = nullptr;
    browserPanel = nullptr;
    masterFxWindow = nullptr;
    if (isStandalone)
        rz::AppSettings::getRunningFlag().deleteFile(); // a clean exit: nothing to recover next time
    stretchWindow = nullptr;
    sceneWindow = nullptr;
    pianoRollWindow = nullptr;
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
    refreshMultiSelection();
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
    chokeBox.setSelectedId (juce::jlimit (0, 8, s.chokeGroup.load()) + 1, juce::dontSendNotification);
    warpButton.setToggleState (processor.isPadWarped (pad), juce::dontSendNotification);
    renderButton.setEnabled (processor.isPadWarped (pad));
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
    filterTypeBox.setSelectedId (fx.filterType.load() + 1, juce::dontSendNotification);
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
        const bool muted = processor.isBankMuted (b);
        bankButtons[(size_t) b].setButtonText (letter + (used ? " *" : "") + (muted ? " M" : ""));
        if (muted) bankButtons[(size_t) b].setColour (juce::TextButton::buttonColourId, juce::Colour (0xff8a2a26));
        else       bankButtons[(size_t) b].removeColour (juce::TextButton::buttonColourId); // grey rubber
        bankButtons[(size_t) b].setColour (juce::TextButton::buttonOnColourId, muted ? juce::Colour (0xffe0413a) : rz::colours::accent);
        bankButtons[(size_t) b].repaint();
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

    // follow pads the step sequencer / piano roll play: show that chop in the sample view
    if (const int sequenced = processor.getEngine().sequencedCounter.load(); sequenced != lastSequencedCounter)
    {
        lastSequencedCounter = sequenced;
        const int pad = processor.getEngine().lastSequencedPad.load();
        // only the sample view shows that chop: the selected pad and the bank stay where you work
        if (pad >= 0 && pad < rz::numPads && pad != waveform.getShownPad() && ! processor.isLazyChopActive())
            waveform.setPad (pad);
    }
    if (! processor.getSequencer().isRunning() && waveform.getShownPad() != processor.selectedPad.load() && ! processor.isLazyChopActive())
        waveform.setPad (processor.selectedPad.load()); // stopped: back to the selected pad

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

    // the selected pad can also be changed from the sequencer window or the browser
    const int sel = processor.selectedPad.load();
    if (lastSelectedPad >= 0 && sel != lastSelectedPad)
        selectPad (sel, false);
    lastSelectedPad = sel;
    if (processor.currentBank.load() != pads[0]->getIndex() / rz::padsPerBank)
        setBank (processor.currentBank.load()); // bank changed from the sequencer window
    for (auto& p : pads)
        p->setSelected (p->getIndex() == sel);

    if (isStandalone)
    {
        seqButton.setToggleState (dockView == dockSequencer, juce::dontSendNotification);
        pianoRollButton.setToggleState (dockView == dockPianoRoll, juce::dontSendNotification);
    }
    else if (sequencerWindow != nullptr)
        seqButton.setToggleState (sequencerWindow->isVisible(), juce::dontSendNotification);

    warpButton.setButtonText (processor.isWarpBusy() ? "WARP..." : "WARP");

    if (isStandalone)
    {
        {
            auto& sq = processor.getSequencer();
            const bool blinkOn = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
            playKey.setActive (sq.internalPlay.load());
            pauseKey.setActive (sq.isPaused());
            pauseKey.setBlink (sq.isPaused() && ! blinkOn);
            recKey.setActive (sq.recording.load());
            clickKey.setActive (sq.metronome.load() || sq.countingIn.load());
            {
                // the pendulum swings with the beat while playing
                const double ppqPerMs = juce::jmax (20.0f, sq.effectiveBpm.load()) / 60000.0;
                const auto beat = (juce::int64) (juce::Time::getMillisecondCounterHiRes() * ppqPerMs);
                clickKey.setSwing (sq.internalPlay.load() ? (beat % 2) == 0 : true);
            }
            recKey.setBlink (sq.recording.load() && (sq.countingIn.load() || ! sq.internalPlay.load()) && ! blinkOn); // blinks while armed / counting in
        }
        if (! titleButtonsDone)
            if (auto* dw = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()); dw != nullptr && dw != (juce::Component*) this)
            {
                // minimise + maximise (full screen) + close, like every window
                dw->setTitleBarButtonsRequired (juce::DocumentWindow::allButtons, false);
                titleButtonsDone = true;
            }
        if (samplingWindow != nullptr) samplingButton.setToggleState (samplingWindow->isVisible(), juce::dontSendNotification);
        browserButton.setToggleState (browserPanel != nullptr && browserPanel->isVisible(), juce::dontSendNotification);
        if (! tempoSlider.isMouseButtonDown())
        {
            auto& sq = processor.getSequencer();
            tempoSlider.setValue (sq.syncToHost.load() ? sq.effectiveBpm.load() : sq.bpm.load(), juce::dontSendNotification);
        }
        if (masterFxWindow != nullptr) masterFxButton.setToggleState (masterFxWindow->isVisible(), juce::dontSendNotification);
        if (juce::Time::getMillisecondCounterHiRes() - lastAutosaveTime > 60000.0)
            autosave();
        // keep the space bar working: if nothing in the app has the keyboard focus, take it back
        // (only when the main window itself is the active one: never steal the keyboard from the piano roll,
        //  step sequencer or song window, which play / record from the computer keyboard themselves)
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if ((focused == nullptr || focused->getPeer() != getPeer()) && isShowing() && juce::Process::isForegroundProcess()
            && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0)
            if (auto* peer = getPeer(); peer != nullptr && peer->isFocused())
                grabKeyboardFocus();
    }
    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());
    undoButton.setTooltip (processor.canUndo() ? "Undo: " + processor.getUndoName() + "  (Ctrl+Z)" : "Nothing to undo");
    redoButton.setTooltip (processor.canRedo() ? "Redo: " + processor.getRedoName() + "  (Ctrl+Y)" : "Nothing to redo");
    renderButton.setEnabled (processor.isPadWarped (processor.selectedPad.load()) && ! processor.isWarpBusy());
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
        if (f.existsAsFile() && (processor.pushUndo ("Load sample"), processor.loadFileToPad (pad, f)))
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
        if (f.existsAsFile() && (processor.pushUndo ("Load sample"), processor.loadFileForChopping (f)))
        {
            selectPad (processor.selectedPad.load());
            refreshChopControls();
        }
    });
}

void RezalicaEditor::showBankVolume (int bank)
{
    auto popup = std::make_unique<rz::BankVolumePopup> (processor, bank, [this, bank] { bankButtons[(size_t) bank].repaint(); });
    juce::CallOutBox::launchAsynchronously (std::move (popup), bankButtons[(size_t) bank].getBounds(), this);
}

void RezalicaEditor::showBankMenu (int bank)
{
    const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + bank));
    juce::PopupMenu m;
    m.addSectionHeader ("BANK " + letter);
    m.addItem (1, "Mute bank " + letter, ! processor.isBankMuted (bank));
    m.addItem (2, "Unmute bank " + letter, processor.isBankMuted (bank));
    m.addSeparator();
    m.addItem (3, "Copy bank " + letter);
    m.addItem (4, "Paste bank to " + letter, processor.canPasteBank());
    m.addSeparator();
    m.addItem (5, "Bank volume...");
    m.addItem (6, "Reset bank volume (0 dB)", std::abs (processor.getBankGainDb (bank)) > 0.01f);

    juce::Component::SafePointer<RezalicaEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bankButtons[(size_t) bank]), [safe, bank] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& proc = safe->processor;
        switch (r)
        {
            case 1: proc.pushUndo ("Mute bank");   proc.setBankMute (bank, true);  break;
            case 2: proc.pushUndo ("Unmute bank"); proc.setBankMute (bank, false); break;
            case 3: proc.copyBank (bank); break;
            case 4: proc.pushUndo ("Paste bank");  proc.pasteBank (bank); break;
            case 5: safe->showBankVolume (bank); return;
            case 6: proc.pushUndo ("Bank volume"); proc.setBankGainDb (bank, 0.0f); break;
            default: break;
        }
        safe->refreshPadNames();
        safe->selectPad (proc.selectedPad.load());
    });
}

//==============================================================================
void RezalicaEditor::showFileMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "New Project            Ctrl+N");
    m.addItem (2, "Open Project...        Ctrl+O");
    const auto recent = rz::AppSettings::getRecentProjects();
    {
        juce::PopupMenu rm;
        for (int i = 0; i < recent.size(); ++i)
            rm.addItem (100 + i, juce::File (recent[i]).getFileNameWithoutExtension() + "    (" + juce::File (recent[i]).getParentDirectory().getFullPathName() + ")",
                        juce::File (recent[i]).existsAsFile());
        if (recent.isEmpty())
            rm.addItem (99, "(no recent projects)", false);
        else
        {
            rm.addSeparator();
            rm.addItem (99, "Clear the list");
        }
        m.addSubMenu ("Open Recent", rm);
    }
    m.addSeparator();
    m.addItem (3, "Save Project           Ctrl+S");
    m.addItem (4, "Save Project As...     Ctrl+Shift+S");
    m.addSeparator();
    m.addItem (7, "Export Song as WAV...");
    m.addItem (8, "Export Song as MIDI...");
    m.addSeparator();
    m.addItem (5, "Audio Settings...");
    if (processor.currentProjectFile.existsAsFile())
    {
        m.addSeparator();
        m.addItem (6, "Show project in folder");
    }
    juce::Component::SafePointer<RezalicaEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton), [safe, recent] (int r)
    {
        if (safe == nullptr) return;
        if (r == 99) { rz::AppSettings::clearRecentProjects(); return; }
        if (r >= 100 && r < 100 + recent.size()) { safe->openProjectFile (juce::File (recent[r - 100])); return; }
        switch (r)
        {
            case 1: safe->newProject(); break;
            case 2: safe->openProject(); break;
            case 3: safe->saveProject (false); break;
            case 4: safe->saveProject (true); break;
            case 5: safe->showAudioSettings(); break;
            case 6: safe->processor.currentProjectFile.revealToUser(); break;
            case 7: safe->exportSong (false); break;
            case 8: safe->exportSong (true); break;
            default: break;
        }
    });
}

void RezalicaEditor::toggleSceneWindow()
{
    if (sceneWindow == nullptr)
    {
        sceneWindow = std::make_unique<rz::SceneWindow> (processor, lnf);
        sceneWindow->onClose = [this] { sceneButton.setToggleState (false, juce::dontSendNotification); };
    }
    const bool show = ! sceneWindow->isVisible();
    sceneWindow->setVisible (show);
    if (show)
        sceneWindow->toFront (true);
    sceneButton.setToggleState (show, juce::dontSendNotification);
}

void RezalicaEditor::exportSong (bool asMidi)
{
    if (processor.getSequencer().songLengthSteps() == 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Export song",
                                                "The song is empty. Open SCENE and put scenes on the song steps first.");
        return;
    }
    auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rezalica").getChildFile ("Export");
    folder.createDirectory();
    const auto name = processor.currentProjectFile != juce::File() ? processor.currentProjectFile.getFileNameWithoutExtension() : juce::String ("Rezalica Song");
    const juce::String ext = asMidi ? ".mid" : ".wav";
    fileChooser = std::make_unique<juce::FileChooser> (asMidi ? "Export the song as MIDI" : "Export the song as WAV",
                                                       folder.getChildFile (name + ext), asMidi ? "*.mid" : "*.wav");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, asMidi, ext] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! f.hasFileExtension (ext.substring (1)))
            f = f.withFileExtension (ext);
        const bool ok = asMidi ? processor.exportSongMidi (f) : processor.exportSongWav (f);
        if (ok)
            f.revealToUser();
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export song", "The song could not be exported.");
    });
}

void RezalicaEditor::showAudioSettings()
{
    // JUCE's audio settings: device type (ASIO incl. ASIO4ALL and interface drivers, Windows Audio,
    // Windows Audio exclusive / low latency, DirectSound), output / input device, sample rate,
    // buffer size, active channels, test tone and MIDI inputs
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        holder->showAudioSettingsDialog();
}

void RezalicaEditor::newProject()
{
    juce::Component::SafePointer<RezalicaEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "New project",
                                        "Start a new, empty project? Unsaved changes are lost.", "New project", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([safe] (int ok)
    {
        if (safe == nullptr || ok == 0) return;
        safe->processor.newProject();
        safe->updateWindowTitle();
        safe->selectPad (0);
    }));
}

void RezalicaEditor::openProject()
{
    auto start = processor.currentProjectFile.existsAsFile() ? processor.currentProjectFile.getParentDirectory()
                                                             : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rezalica");
    fileChooser = std::make_unique<juce::FileChooser> ("Open a Rezalica project", start, "*.rezalica");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile())
            openProjectFile (f);
    });
}

void RezalicaEditor::openProjectFile (const juce::File& f)
{
    if (processor.loadProjectFromFile (f))
    {
        rz::AppSettings::addRecentProject (f);
        updateWindowTitle();
        selectPad (processor.selectedPad.load());
        refreshChopControls();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open project",
                                                "This file could not be opened as a Rezalica project.");
    }
}

void RezalicaEditor::toggleToolWindow (std::unique_ptr<rz::ToolWindow>& w, juce::TextButton& button, int which)
{
    if (w == nullptr)
    {
        juce::Component* content = nullptr;
        juce::String name;
        if (which == 0)
        {
            auto* panel = new rz::RecorderPanel (processor);
            panel->onAudioSettings = [this] { showAudioSettings(); };
            panel->onRecorded = [this] { selectPad (processor.selectedPad.load(), false); refreshChopControls(); };
            content = panel;
            name = "Rezalica - Sampling";
        }
        else if (which == 1)
        {
            content = new rz::BrowserPanel (processor);
            name = "Rezalica - Sample Browser";
        }
        else
        {
            content = new rz::MasterFxPanel (processor);
            name = "Rezalica - Master FX";
        }
        w = std::make_unique<rz::ToolWindow> (name, content, lnf, which == 1);
        juce::Component::SafePointer<juce::TextButton> safeButton (&button);
        w->onClose = [safeButton] { if (safeButton != nullptr) safeButton->setToggleState (false, juce::dontSendNotification); };
    }
    const bool show = ! w->isVisible();
    w->setVisible (show);
    if (show)
        w->toFront (true);
    button.setToggleState (show, juce::dontSendNotification);
}

int RezalicaEditor::multiSelectedCount() const
{
    int n = 0;
    for (auto m : multiSelection) if (m) ++n;
    return n;
}

void RezalicaEditor::refreshMultiSelection()
{
    for (auto& p : pads)
        p->setMultiSelected (multiSelection[(size_t) p->getIndex()]);
}

void RezalicaEditor::clearSelectedPads()
{
    const int n = multiSelectedCount();
    if (n == 0) return;
    processor.pushUndo ("Clear " + juce::String (n) + " pads"); // one UNDO brings them all back
    for (int i = 0; i < rz::numPads; ++i)
        if (multiSelection[(size_t) i])
            processor.clearPad (i);
    multiSelection.fill (false);
    refreshMultiSelection();
    selectPad (processor.selectedPad.load(), false);
    refreshPadNames();
}

void RezalicaEditor::toggleBrowser()
{
    if (browserPanel == nullptr)
    {
        browserPanel = std::make_unique<rz::BrowserPanel> (processor);
        addChildComponent (*browserPanel);
    }
    browserPanel->setVisible (! browserPanel->isVisible());
    browserButton.setToggleState (browserPanel->isVisible(), juce::dontSendNotification);
    rz::AppSettings::get().setValue ("browserOpen", browserPanel->isVisible());
    rz::AppSettings::get().saveIfNeeded();
    resized();
    repaint();
}

bool RezalicaEditor::shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails& d, juce::StringArray& files, bool& canMove)
{
    // browser files can also be dragged out of Rezalica (into a DAW or a folder)
    const auto desc = d.description.toString();
    if (! desc.startsWith ("rzfile:")) return false;
    files.add (desc.fromFirstOccurrenceOf ("rzfile:", false, false));
    canMove = false;
    return true;
}

void RezalicaEditor::autosave()
{
    lastAutosaveTime = juce::Time::getMillisecondCounterHiRes();
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    if (state == lastAutosavedState)
        return; // nothing changed since the last autosave
    auto file = rz::AppSettings::getAutosaveFile();
    file.getParentDirectory().createDirectory();
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
    {
        xml->setAttribute ("autosaveOf", processor.currentProjectFile.getFullPathName());
        if (xml->writeTo (file))
        {
            lastAutosavedState = state;
            autosaveLabel.setText ("Autosaved " + juce::Time::getCurrentTime().formatted ("%H:%M"), juce::dontSendNotification);
        }
    }
}

void RezalicaEditor::checkCrashRecovery()
{
    auto flag = rz::AppSettings::getRunningFlag();
    auto autosaved = rz::AppSettings::getAutosaveFile();
    const bool crashed = flag.existsAsFile() && autosaved.existsAsFile();
    flag.getParentDirectory().createDirectory();
    flag.replaceWithText (juce::Time::getCurrentTime().toISO8601 (true)); // removed again on a clean exit
    if (! crashed)
        return;

    juce::Component::SafePointer<RezalicaEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Rezalica was not closed properly",
                                        "Rezalica did not close normally last time (crash or power cut).\n"
                                        "Restore the automatically saved project from "
                                            + autosaved.getLastModificationTime().formatted ("%d.%m.%Y %H:%M") + "?",
                                        "Restore", "No", this,
                                        juce::ModalCallbackFunction::create ([safe, autosaved] (int ok)
    {
        if (safe == nullptr || ok == 0) return;
        auto xml = juce::XmlDocument::parse (autosaved);
        const juce::File original (xml != nullptr ? xml->getStringAttribute ("autosaveOf") : juce::String());
        if (safe->processor.loadProjectFromFile (autosaved))
        {
            safe->processor.currentProjectFile = original; // Save goes back to the real project file
            safe->updateWindowTitle();
            safe->selectPad (safe->processor.selectedPad.load());
            safe->refreshChopControls();
        }
    }));
}

void RezalicaEditor::saveProject (bool saveAs)
{
    if (! saveAs && processor.currentProjectFile != juce::File())
    {
        if (! processor.saveProjectToFile (processor.currentProjectFile))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save project", "The project could not be saved.");
        else
            rz::AppSettings::addRecentProject (processor.currentProjectFile);
        updateWindowTitle();
        return;
    }

    auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rezalica").getChildFile ("Projects");
    folder.createDirectory();
    auto suggestion = processor.currentProjectFile != juce::File() ? processor.currentProjectFile
                                                                   : folder.getNonexistentChildFile ("Rezalica Project", ".rezalica", false);
    fileChooser = std::make_unique<juce::FileChooser> ("Save the Rezalica project", suggestion, "*.rezalica");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! f.hasFileExtension ("rezalica"))
            f = f.withFileExtension ("rezalica");
        if (! processor.saveProjectToFile (f))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save project", "The project could not be saved.");
        else
            rz::AppSettings::addRecentProject (f);
        updateWindowTitle();
    });
}

void RezalicaEditor::updateWindowTitle()
{
    if (! isStandalone) return;
    if (auto* top = getTopLevelComponent(); top != nullptr && top != this)
        top->setName ("Rezalica" + (processor.currentProjectFile != juce::File()
                                        ? " - " + processor.currentProjectFile.getFileNameWithoutExtension() : juce::String()));
}

juce::Component* RezalicaEditor::dockedPanel() const
{
    if (dockView == dockSequencer) return dockedSequencer.get();
    if (dockView == dockPianoRoll) return dockedPianoRoll.get();
    return nullptr;
}

std::vector<juce::Component*> RezalicaEditor::stripAndFxComponents()
{
    // (the pad title row with WARP / STRETCH / TRIM / RENDER / CLEAR stays)
    return { &gainKnob, &panKnob, &pitchKnob, &attackKnob, &releaseKnob, &startKnob, &endKnob,
             &loopButton, &reverseButton, &gateButton, &muteButton, &chokeBox,
             &fxTitle, &copyFxButton, &pasteFxButton, &fxToAllButton,
             &filterOnButton, &filterTypeBox, &filterTypeLabel, &cutoffKnob, &resoKnob,
             &satOnButton, &tubeKnob, &tapeKnob, &solidKnob,
             &delayOnButton, &delayDivLabel, &delayDivBox, &timeKnob, &feedbackKnob, &mixKnob,
             &reverbOnButton, &reverbSizeKnob, &reverbDampKnob, &reverbWidthKnob, &reverbMixKnob };
}

void RezalicaEditor::showDock (int view)
{
    if (view == dockView) view = dockNone;   // the same button again closes it
    if (auto* old = dockedPanel()) old->setVisible (false);

    if (view != dockNone && dockView == dockNone)
    {
        // the pad controls and effects make room for the sequencer; remember what was showing
        hiddenByDock.clear();
        for (auto* c : stripAndFxComponents())
            if (c->isVisible()) { hiddenByDock.push_back (c); c->setVisible (false); }
    }
    else if (view == dockNone)
    {
        for (auto* c : hiddenByDock) c->setVisible (true);
        hiddenByDock.clear();
    }

    dockView = view;
    if (view == dockSequencer)
    {
        if (dockedSequencer == nullptr) { dockedSequencer = std::make_unique<rz::SequencerPanel> (processor); addChildComponent (*dockedSequencer); }
        dockedSequencer->setVisible (true);
    }
    else if (view == dockPianoRoll)
    {
        if (dockedPianoRoll == nullptr) { dockedPianoRoll = std::make_unique<rz::PianoRollPanel> (processor); addChildComponent (*dockedPianoRoll); }
        dockedPianoRoll->showMelodic();
        dockedPianoRoll->setVisible (true);
    }
    seqButton.setToggleState (view == dockSequencer, juce::dontSendNotification);
    pianoRollButton.setToggleState (view == dockPianoRoll, juce::dontSendNotification);
    resized();
    repaint();
    if (view == dockPianoRoll) dockedPianoRoll->focusEditor();
    else if (view == dockSequencer) dockedSequencer->grabKeyboardFocus();
}

void RezalicaEditor::togglePianoRoll()
{
    if (isStandalone) { showDock (dockPianoRoll); return; }
    if (pianoRollWindow == nullptr)
    {
        pianoRollWindow = std::make_unique<rz::PianoRollWindow> (processor, lnf);
        pianoRollWindow->onClose = [this] { pianoRollButton.setToggleState (false, juce::dontSendNotification); };
    }
    const bool show = ! pianoRollWindow->isVisible();
    if (show)
        if (auto* panel = dynamic_cast<rz::PianoRollPanel*> (pianoRollWindow->getContentComponent()))
            panel->showMelodic();
    pianoRollWindow->setVisible (show);
    if (show)
        pianoRollWindow->toFront (true);
    pianoRollButton.setToggleState (show, juce::dontSendNotification);
}

void RezalicaEditor::showStretchWindow()
{
    if (stretchWindow == nullptr)
        stretchWindow = std::make_unique<rz::StretchWindow> (processor, lnf);
    stretchWindow->setVisible (true);
    stretchWindow->toFront (true);
}

void RezalicaEditor::toggleSequencerWindow()
{
    if (isStandalone) { showDock (dockSequencer); return; }
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
    if (key.getModifiers().isCommandDown())
    {
        const int code = key.getKeyCode();
        const auto ch = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        if (isStandalone)
        {
            if (code == 'S' || ch == 's' || ch == 19) { saveProject (key.getModifiers().isShiftDown()); return true; }
            if (code == 'O' || ch == 'o' || ch == 15) { openProject(); return true; }
            if (code == 'N' || ch == 'n' || ch == 14) { newProject(); return true; }
        }
        if (code == 'Z' || ch == 'z' || ch == 26)
        {
            if (key.getModifiers().isShiftDown() ? processor.redo() : processor.undo())
                selectPad (processor.selectedPad.load());
            return true;
        }
        if (code == 'Y' || ch == 'y' || ch == 25)
        {
            if (processor.redo())
                selectPad (processor.selectedPad.load());
            return true;
        }
        return false;
    }

    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    for (int i = 0; padKeys[i] != 0; ++i)
        if (c == (juce::juce_wchar) padKeys[i] || c == 'y')
            return true; // handled in keyStateChanged

    if (key == juce::KeyPress::spaceKey)
    {
        // the sampler's PLAY / STOP: the selected pattern, whatever the scenes / song contain
        processor.getSequencer().togglePatternPlay();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        multiSelection.fill (false); // ESC also drops a multi-selection of pads
        refreshMultiSelection();
        processor.stopAll();
        return true;
    }
    return false;
}

bool RezalicaEditor::keyStateChanged (bool)
{
    bool used = false;
    // the docked step sequencer / piano roll play the computer keyboard themselves
    if (auto* panel = dockedPanel(); panel != nullptr && panel->hasKeyboardFocus (true))
        return false;
    // Ctrl / Cmd shortcuts (Ctrl+Z ...) must not play the pads on those keys
    if (juce::ModifierKeys::getCurrentModifiers().isCommandDown())
        return false;
    for (int i = 0; i < rz::padsPerBank; ++i)
    {
        bool down = juce::KeyPress::isKeyCurrentlyDown ((int) padKeys[i]);
        if (padKeys[i] == 'z') down = down || juce::KeyPress::isKeyCurrentlyDown ((int) 'y'); // QWERTZ: bottom row is Y X C V
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
void RezalicaEditor::renderFaceplate()
{
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    if (faceplate.isValid() && faceplate.getWidth() == w && faceplate.getHeight() == h)
        return;
    faceplate = juce::Image (juce::Image::RGB, w, h, false);
    juce::Graphics g (faceplate);
    using namespace rz::colours;
    // brushed aluminium: a soft vertical sheen plus fine horizontal grain
    g.setGradientFill (juce::ColourGradient (silver.brighter (0.12f), 0.0f, 0.0f, silverDark, 0.0f, (float) h, false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), (float) w * 0.3f, 0.0f,
                                             juce::Colours::transparentWhite, (float) w * 0.75f, (float) h, false));
    g.fillAll();
    juce::Random rnd (303);
    for (int y = 0; y < h; ++y)
    {
        const float a = rnd.nextFloat() * 0.06f;
        g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (a));
        const int x0 = rnd.nextInt (juce::jmax (1, w / 3));
        g.drawHorizontalLine (y, (float) x0, (float) (x0 + w / 2 + rnd.nextInt (juce::jmax (1, w / 2))));
    }
}

void RezalicaEditor::paint (juce::Graphics& g)
{
    using namespace rz::colours;
    // dark bezel around a silver faceplate
    g.fillAll (juce::Colour (0xff0d0d0e));
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path face;
        face.addRoundedRectangle (getLocalBounds().toFloat().reduced (5.0f), 12.0f);
        g.reduceClipRegion (face);
        if (faceplate.isValid())
            g.drawImageAt (faceplate, 0, 0);
        else
            g.fillAll (silver);
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (5.0f), 12.0f, 1.5f);

    // black display panels with red print (like the top section of a hardware sampler)
    auto drawPanel = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        auto f = r.toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.35f));               // bevel highlight under the panel
        g.drawRoundedRectangle (f.translated (0.0f, 1.0f), 6.0f, 1.0f);
        g.setColour (panel);
        g.fillRoundedRectangle (f, 6.0f);
        g.setColour (accent.withAlpha (0.55f));
        g.drawRoundedRectangle (f.reduced (3.5f), 4.0f, 1.0f);
        if (title.isNotEmpty())
        {
            g.setColour (accent);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (title, r.reduced (10, 6).removeFromTop (14), juce::Justification::topLeft);
        }
    };

    // logo
    g.setColour (ink);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold).withStyle ("Bold Italic"));
    g.drawText ("REZALICA", logoArea.withHeight (40).translated (2, 0), juce::Justification::centredLeft);
    g.setColour (accent.darker (0.15f));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("SLICE SAMPLER  4 x 16 PADS", logoArea.withTrimmedTop (36).withHeight (14).translated (3, 0), juce::Justification::centredLeft);

    // red band behind the effects title
    if (fxTitle.isVisible())
    {
        auto t = fxTitle.getBounds().toFloat();
        const float tw = juce::GlyphArrangement::getStringWidth (fxTitle.getFont(), fxTitle.getText()) + 24.0f;
        g.setColour (accent);
        g.fillRoundedRectangle (t.withWidth (juce::jmin (t.getWidth(), tw)).withX (t.getX() - 4.0f), 4.0f);
    }

    drawPanel (sampleArea, {});
    {
        // pads sit on the faceplate, in a slightly recessed area
        auto pf = padArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRoundedRectangle (pf, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (pf.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawRoundedRectangle (pf.translated (0.0f, 1.0f).reduced (0.5f), 8.0f, 1.0f);
        const int bank = processor.currentBank.load();
        const int lo = RezalicaProcessor::midiBaseNote + bank * rz::padsPerBank;
        g.setColour (ink);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("BANK " + juce::String::charToString ((juce::juce_wchar) ('A' + bank))
                        + "   MIDI " + juce::MidiMessage::getMidiNoteName (lo, true, true, 3)
                        + "-" + juce::MidiMessage::getMidiNoteName (lo + 15, true, true, 3)
                        + "   keys 1-4 Q-R A-F Z-V",
                    padArea.reduced (10, 6).removeFromTop (14), juce::Justification::topLeft);
    }
    drawPanel (filterArea, {});
    drawPanel (satArea, {});
    drawPanel (delayArea, {});
    drawPanel (reverbArea, {});
}

void RezalicaEditor::resized()
{
    renderFaceplate();
    auto b = getLocalBounds().reduced (14);

    // top block: buttons on the left, LED tempo display + master volume knob on the right
    auto topBlock = b.removeFromTop (isStandalone ? 78 : 64);
    logoArea = topBlock.withWidth (175);
    {
        auto right = topBlock.removeFromRight (isStandalone ? 484 : 214);
        masterKnob.setBounds (right.removeFromRight (104));
        right.removeFromRight (12);
        ledBpm.setBounds (right.removeFromRight (98).withSizeKeepingCentre (98, 54));
        if (isStandalone)
        {
            // REC  PLAY  PAUSE  STOP  next to the tempo display
            right.removeFromRight (10);
            auto keys = right.removeFromRight (5 * 48 + 4 * 4).withSizeKeepingCentre (5 * 48 + 4 * 4, 44).translated (0, 6);
            for (auto* k : { &clickKey, &stopKey, &pauseKey, &playKey, &recKey })
            {
                k->setBounds (keys.removeFromRight (48));
                keys.removeFromRight (4);
            }
        }
        topBlock.removeFromRight (10);
    }
    headerArea = isStandalone ? topBlock.removeFromTop (40) : topBlock;
    {
        auto h = headerArea;
        h.removeFromLeft (175);
        if (isStandalone)
        {
            auto fr = h.withSizeKeepingCentre (h.getWidth(), 30);
            fileButton.setBounds (fr.removeFromLeft (60));
            h.removeFromLeft (60 + 12);
        }
        auto row = h.withSizeKeepingCentre (h.getWidth(), 30);
        if (loadChopButton.isVisible())
        {
            loadChopButton.setBounds (row.removeFromLeft (112));
            row.removeFromLeft (6);
        }
        chopModeBox.setBounds (row.removeFromLeft (112)); row.removeFromLeft (6);
        if (sensitivitySlider.isVisible())
        {
            sensitivitySlider.setBounds (row.removeFromLeft (100));
            row.removeFromLeft (6);
        }
        lazyButton.setBounds (row.removeFromLeft (96)); row.removeFromLeft (10);
        undoButton.setBounds (row.removeFromLeft (62)); row.removeFromLeft (4);
        redoButton.setBounds (row.removeFromLeft (62)); row.removeFromLeft (8);
        if (stopButton.isVisible())
        {
            stopButton.setBounds (row.removeFromRight (86));
            row.removeFromRight (6);
        }
        if (! isStandalone)
        {
            pianoRollButton.setBounds (row.removeFromRight (100)); row.removeFromRight (6);
            seqButton.setBounds (row.removeFromRight (90)); row.removeFromRight (6);
        }
        sliceCountLabel.setBounds (row);
    }
    if (isStandalone)
    {
        // second row: tools of the standalone app
        topBlock.removeFromTop (10);
        toolRowArea = topBlock.removeFromTop (28);
        auto t = toolRowArea;
        t.removeFromLeft (175);
        browserButton.setBounds (t.removeFromLeft (90)); t.removeFromLeft (4);
        samplingButton.setBounds (t.removeFromLeft (96)); t.removeFromLeft (4);
        masterFxButton.setBounds (t.removeFromLeft (96)); t.removeFromLeft (16);
        metronome.setBounds (t.removeFromLeft (136)); // count-in menu
        // SONG / STEP SEQ / PIANO ROLL at the height of the transport keys
        pianoRollButton.setBounds (t.removeFromRight (100)); t.removeFromRight (6);
        seqButton.setBounds (t.removeFromRight (90)); t.removeFromRight (6);
        sceneButton.setBounds (t.removeFromRight (70)); t.removeFromRight (12);
        autosaveLabel.setBounds (t.removeFromRight (juce::jmin (130, t.getWidth())));
    }
    b.removeFromTop (12);

    if (isStandalone)
    {

        if (browserPanel != nullptr && browserPanel->isVisible())
        {
            browserArea = b.removeFromLeft (340);
            browserPanel->setBounds (browserArea);
            b.removeFromLeft (10);
        }
    }

    if (auto* panel = dockedPanel())
    {
        // sample view + pads on top, the step sequencer / piano roll below them across the whole width
        auto top = b.removeFromTop (juce::jlimit (250, 370, b.getHeight() * 42 / 100));
        b.removeFromTop (10);
        dockArea = b;
        panel->setBounds (dockArea);
        padArea = top.removeFromRight (juce::jmax (420, getWidth() * 27 / 100));
        top.removeFromRight (10);
        sampleArea = top;
        fxArea = filterArea = satArea = delayArea = reverbArea = {};
        {
            auto s = sampleArea.reduced (10);
            auto titleRow = s.removeFromBottom (24);
            s.removeFromBottom (6);
            waveform.setBounds (s);
            clearPadButton.setBounds (titleRow.removeFromRight (70)); titleRow.removeFromRight (6);
            renderButton.setBounds (titleRow.removeFromRight (80));   titleRow.removeFromRight (6);
            trimButton.setBounds (titleRow.removeFromRight (70));     titleRow.removeFromRight (6);
            stretchButton.setBounds (titleRow.removeFromRight (90));  titleRow.removeFromRight (6);
            warpButton.setBounds (titleRow.removeFromRight (70));
            padTitle.setBounds (titleRow);
        }
        layoutPads();
        return;
    }
    dockArea = {};

    fxArea = b.removeFromBottom (176);
    b.removeFromBottom (10);

    padArea = b.removeFromRight (juce::jmax (420, getWidth() * 27 / 100)); // bigger pads when the window is bigger
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
        renderButton.setBounds (titleRow.removeFromRight (80));
        titleRow.removeFromRight (6);
        trimButton.setBounds (titleRow.removeFromRight (70));
        titleRow.removeFromRight (6);
        stretchButton.setBounds (titleRow.removeFromRight (90));
        titleRow.removeFromRight (6);
        warpButton.setBounds (titleRow.removeFromRight (70));
        padTitle.setBounds (titleRow);

        s.removeFromTop (6);
        auto knobs = s.removeFromTop (100);
        const int kw = knobs.getWidth() / 7;
        for (auto* k : { &gainKnob, &panKnob, &pitchKnob, &attackKnob, &releaseKnob, &startKnob, &endKnob })
            k->setBounds (knobs.removeFromLeft (kw).reduced (2, 0));

        s.removeFromTop (8);
        auto toggles = s.removeFromTop (30);
        const int tw = toggles.getWidth() / 5;
        for (auto* t : { &loopButton, &reverseButton, &gateButton, &muteButton })
            t->setBounds (toggles.removeFromLeft (tw).reduced (3, 0));
        chokeBox.setBounds (toggles.reduced (3, 0));
    }

    layoutPads();

    // ---- effects ----
    layoutEffects();
}

void RezalicaEditor::layoutPads()
{
    {
        auto p = padArea.reduced (10);
        auto bankRow = p.removeFromTop (24);
        for (int bi = rz::numBanks; --bi >= 0;)
        {
            bankButtons[(size_t) bi].setBounds (bankRow.removeFromRight (50));
            bankRow.removeFromRight (4);
        }
        p.removeFromTop (6);
        const int cw = p.getWidth() / 4, ch = p.getHeight() / 4;
        for (int i = 0; i < rz::padsPerBank; ++i)
            pads[(size_t) i]->setBounds (p.getX() + (i % 4) * cw, p.getY() + (i / 4) * ch, cw, ch);
    }
}

void RezalicaEditor::layoutEffects()
{
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
        satArea = f.removeFromLeft (w);   f.removeFromLeft (10);
        delayArea = f.removeFromLeft (w);  f.removeFromLeft (10);
        reverbArea = f;

        auto layoutGroup = [] (juce::Rectangle<int> area, juce::TextButton& onButton, std::initializer_list<juce::Component*> items, int buttonWidth = 150)
        {
            auto a = area.reduced (10);
            onButton.setBounds (a.removeFromTop (26).withWidth (juce::jmin (buttonWidth, a.getWidth())));
            a.removeFromTop (6);
            const int iw = a.getWidth() / (int) items.size();
            for (auto* c : items)
                c->setBounds (a.removeFromLeft (iw).reduced (2, 0));
        };

        layoutGroup (filterArea, filterOnButton, { &cutoffKnob, &resoKnob }, 90);
        {
            auto top = filterArea.reduced (10).removeFromTop (26);
            top.removeFromLeft (98);
            filterTypeLabel.setBounds (top.removeFromLeft (40));
            filterTypeBox.setBounds (top.removeFromLeft (juce::jmin (100, top.getWidth())));
        }
        layoutGroup (satArea, satOnButton, { &tubeKnob, &tapeKnob, &solidKnob });
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
