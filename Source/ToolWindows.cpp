#include "ToolWindows.h"
#include "AppSettings.h"

namespace rz
{
namespace
{
void styleToggleButton (juce::TextButton& b, juce::Colour on)
{
    b.setClickingTogglesState (true);
    b.setWantsKeyboardFocus (false);
    b.setColour (juce::TextButton::buttonOnColourId, on);
}

void sectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);
    juce::ignoreUnused (text);
}
} // namespace

//==============================================================================
RecorderPanel::RecorderPanel (RezalicaProcessor& p) : processor (p)
{
    auto& st = AppSettings::get();
    title.setText ("SAMPLING", juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (title);

    // source
    for (auto* b : { &inputButton, &computerButton, &resampleButton })
    {
        styleToggleButton (*b, colours::accent2);
        b->setRadioGroupId (8101);
        b->onClick = [this] { updateSourceUi(); };
        addAndMakeVisible (*b);
    }
    computerButton.setVisible (LoopbackCapture::isAvailable());
    inputButton.setTooltip ("Record the sound card input (microphone, line in, turntable ...)");
    computerButton.setTooltip ("Record what the computer plays (YouTube, Spotify, other programs ...)");
    resampleButton.setTooltip ("Record what Rezalica plays (pads, effects, sequencer) onto a pad");
    const int src = st.getIntValue ("recSource", 0);
    (src == 2 ? resampleButton : src == 1 && LoopbackCapture::isAvailable() ? computerButton : inputButton).setToggleState (true, juce::dontSendNotification);
    settingsButton.setWantsKeyboardFocus (false);
    settingsButton.onClick = [this] { if (onAudioSettings) onAudioSettings(); };
    addAndMakeVisible (settingsButton);

    auto label = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, colours::text);
        l.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    };
    label (channelsL, "Channels");
    label (startL, "Start");
    label (lengthL, "Length");
    label (throughL, "Effects");

    channelsBox.addItem ("Stereo", 1);
    channelsBox.addItem ("Mono (left input)", 2);
    channelsBox.addItem ("Mono (right input)", 3);
    channelsBox.setSelectedId (st.getIntValue ("recChannels", 1), juce::dontSendNotification);
    startBox.addItem ("At once (REC)", 1);
    startBox.addItem ("On sound above the threshold", 2);
    startBox.setSelectedId (st.getIntValue ("recStart", 2), juce::dontSendNotification);
    lengthBox.addItem ("Until STOP", 1);
    int id = 2;
    for (auto t : { "1 second", "2 seconds", "4 seconds", "8 seconds", "1 bar", "2 bars", "4 bars" })
        lengthBox.addItem (t, id++);
    lengthBox.setSelectedId (st.getIntValue ("recLength", 1), juce::dontSendNotification);
    throughBox.addItem ("Straight (no effects)", 1);
    for (int b = 1; b < numBuses; ++b) throughBox.addItem ("Through " + busName (b) + " effects", b + 1);
    throughBox.addItem ("Through MASTER effects", numBuses + 1);
    throughBox.setSelectedId (1, juce::dontSendNotification);
    throughBox.setTooltip ("Record the input through the plug-in effects of a mixer bus (like sampling through an effect on an SP)");
    throughBox.onChange = [this]
    {
        const int sel = throughBox.getSelectedId();
        processor.inputThroughBus = sel <= 1 ? -1 : (sel == numBuses + 1 ? 0 : sel - 1);
    };
    channelsBox.onChange = [this] { processor.getRecorder().inputMode = channelsBox.getSelectedId() - 1; };
    processor.getRecorder().inputMode = channelsBox.getSelectedId() - 1;
    startBox.onChange = [this] { updateSourceUi(); };
    for (auto* c : { &channelsBox, &startBox, &lengthBox, &throughBox })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }

    threshold.setSliderStyle (juce::Slider::LinearBar);
    threshold.setRange (-60.0, -6.0, 1.0);
    threshold.textFromValueFunction = [] (double v) { return "Threshold " + juce::String ((int) v) + " dB"; };
    threshold.setValue (st.getDoubleValue ("recThreshold", -34.0), juce::dontSendNotification);
    threshold.updateText();
    threshold.setTooltip ("How loud the sound must be to start the take (the line on the meter)");
    threshold.setColour (juce::Slider::trackColourId, colours::accent.withAlpha (0.45f));
    threshold.onValueChange = [this] { processor.getRecorder().setThresholdDb ((float) threshold.getValue()); repaint(); };
    threshold.setWantsKeyboardFocus (false);
    addAndMakeVisible (threshold);

    for (auto* t : { &monitorButton, &patternButton, &pickPadButton, &trimButton })
    {
        t->setWantsKeyboardFocus (false);
        t->setColour (juce::ToggleButton::textColourId, colours::text);
        t->setColour (juce::ToggleButton::tickColourId, colours::accent);
        addAndMakeVisible (*t);
    }
    monitorButton.setTooltip ("Hear the input while sampling (use headphones with a microphone)");
    monitorButton.onClick = [this] { processor.monitorInput = monitorButton.getToggleState(); };
    patternButton.setToggleState (true, juce::dontSendNotification);
    pickPadButton.setToggleState (st.getBoolValue ("recPickPad", false), juce::dontSendNotification);
    pickPadButton.setTooltip ("Like an SP: press REC, the empty pads blink, hit the pad that should get the sample");
    trimButton.setToggleState (st.getBoolValue ("recTrim", true), juce::dontSendNotification);

    recButton.setWantsKeyboardFocus (false);
    recButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffa32a26));
    recButton.onClick = [this]
    {
        auto& r = processor.getRecorder();
        if (r.isBusy())
        {
            r.stop();
            if (resamplingPattern) processor.getSequencer().stopPlayback();
            return;
        }
        // like an MPC: REC arms at once and waits for the input above the threshold;
        // meanwhile the pads blink and the pad that is hit gets the take (SP style)
        if (pickPadButton.getToggleState() && currentSource() != AudioRecorder::output)
        {
            processor.armedRecordOptions = options();
            processor.padChoiceArmed = true;
        }
        startTake();
    };
    addAndMakeVisible (recButton);
    cancelButton.setWantsKeyboardFocus (false);
    cancelButton.onClick = [this]
    {
        processor.padChoiceArmed = false;
        processor.getRecorder().cancel();
        if (resamplingPattern) processor.getSequencer().stopPlayback();
        statusLabel.setText ("Cancelled.", juce::dontSendNotification);
    };
    addAndMakeVisible (cancelButton);

    // KEEP / RETRY after a take
    takeTitle.setText ("TAKE", juce::dontSendNotification);
    takeTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    takeTitle.setColour (juce::Label::textColourId, colours::accent2);
    addChildComponent (takeTitle);
    nameL.setText ("Name", juce::dontSendNotification);
    nameL.setColour (juce::Label::textColourId, colours::text);
    addChildComponent (nameL);
    addChildComponent (nameEditor);
    chopL.setText ("Then chop", juce::dontSendNotification);
    chopL.setColour (juce::Label::textColourId, colours::text);
    addChildComponent (chopL);
    chopBox.addItem ("No", 1);
    chopBox.addItem ("2 slices", 2);
    chopBox.addItem ("4 slices", 3);
    chopBox.addItem ("8 slices", 4);
    chopBox.addItem ("16 slices", 5);
    chopBox.addItem ("By transients", 6);
    chopBox.setSelectedId (st.getIntValue ("recChop", 1), juce::dontSendNotification);
    addChildComponent (chopBox);
    addChildComponent (trimButton);
    playTakeButton.onClick = [this] { processor.previewTake(); };
    keepButton.setColour (juce::TextButton::buttonColourId, colours::accent);
    keepButton.onClick = [this]
    {
        const int chopIds[] { RezalicaProcessor::chopOff, RezalicaProcessor::chop2, RezalicaProcessor::chop4, RezalicaProcessor::chop8,
                              RezalicaProcessor::chop16, RezalicaProcessor::chopTransients };
        const int pad = processor.selectedPad.load();
        processor.padChoiceArmed = false;
        const auto f = processor.keepTake (pad, nameEditor.getText(), trimButton.getToggleState(), chopIds[juce::jlimit (0, 5, chopBox.getSelectedId() - 1)]);
        auto& set = AppSettings::get();
        set.setValue ("recTrim", trimButton.getToggleState());
        set.setValue ("recChop", chopBox.getSelectedId());
        set.saveIfNeeded();
        statusLabel.setText (f.existsAsFile() ? "Kept \"" + f.getFileName() + "\" on " + padLabel (pad) : juce::String ("Could not save the take."), juce::dontSendNotification);
        if (f.existsAsFile() && onRecorded) onRecorded();
    };
    retryButton.onClick = [this] { processor.discardTake(); startTake(); };
    discardButton.onClick = [this] { processor.padChoiceArmed = false; processor.discardTake(); statusLabel.setText ("Take thrown away.", juce::dontSendNotification); };
    for (auto* b : { &playTakeButton, &keepButton, &retryButton, &discardButton })
    {
        b->setWantsKeyboardFocus (false);
        addChildComponent (*b);
    }

    for (auto* l : { &targetLabel, &statusLabel, &timeLabel, &clipLabel })
        addAndMakeVisible (*l);
    targetLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    targetLabel.setColour (juce::Label::textColourId, colours::text);
    timeLabel.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    timeLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setColour (juce::Label::textColourId, colours::accent2);
    clipLabel.setText ("CLIP", juce::dontSendNotification);
    clipLabel.setJustificationType (juce::Justification::centred);
    clipLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    clipLabel.setTooltip ("Lights when the input was too loud (click to reset)");
    clipLabel.addMouseListener (this, false);
    addAndMakeVisible (meter);

    updateSourceUi();
    setSize (560, 600);
    startTimerHz (20);
}

RecorderPanel::~RecorderPanel()
{
    stopTimer();
    processor.padChoiceArmed = false;
    processor.monitorInput = false;    // no hidden input routing once the window is gone
    processor.inputThroughBus = -1;
    if (! processor.getRecorder().isBusy()) processor.stopComputerAudio();
}

RezalicaProcessor::RecordOptions RecorderPanel::options() const
{
    RezalicaProcessor::RecordOptions o;
    o.source = currentSource();
    o.waitForSignal = startBox.getSelectedId() == 2;
    o.thresholdDb = (float) threshold.getValue();
    o.inputMode = channelsBox.getSelectedId() - 1;
    o.patternLength = o.source == AudioRecorder::output && patternButton.getToggleState();
    const int len = lengthBox.getSelectedId();
    const double bpm = juce::jmax (20.0f, processor.getSequencer().effectiveBpm.load());
    const double secs[] { 0.0, 0.0, 1.0, 2.0, 4.0, 8.0 };
    if (len >= 2 && len <= 5) o.lengthSeconds = secs[len];
    else if (len >= 6) o.lengthSeconds = (len == 6 ? 1 : len == 7 ? 2 : 4) * 4.0 * 60.0 / bpm; // bars at the sampler tempo
    return o;
}

void RecorderPanel::startTake()
{
    auto& set = AppSettings::get();
    set.setValue ("recSource", currentSource() == AudioRecorder::output ? 2 : currentSource() == AudioRecorder::computer ? 1 : 0);
    set.setValue ("recChannels", channelsBox.getSelectedId());
    set.setValue ("recStart", startBox.getSelectedId());
    set.setValue ("recLength", lengthBox.getSelectedId());
    set.setValue ("recThreshold", threshold.getValue());
    set.setValue ("recPickPad", pickPadButton.getToggleState());
    set.saveIfNeeded();
    const auto o = options();
    if (o.source == AudioRecorder::input && ! processor.hasAudioInput())
    {
        statusLabel.setText ("No audio input. Choose an input device in Audio Settings.", juce::dontSendNotification);
        return;
    }
    resamplingPattern = o.patternLength;
    if (! processor.startRecording (o))
        statusLabel.setText (o.source == AudioRecorder::computer ? "Computer sound: " + processor.getComputerAudioError()
                                                                  : juce::String ("Could not start recording."), juce::dontSendNotification);
}

AudioRecorder::Source RecorderPanel::currentSource() const
{
    if (resampleButton.getToggleState()) return AudioRecorder::output;
    if (computerButton.getToggleState()) return AudioRecorder::computer;
    return AudioRecorder::input;
}

void RecorderPanel::updateSourceUi()
{
    const auto src = currentSource();
    const bool live = src != AudioRecorder::output;
    for (auto* c : std::initializer_list<juce::Component*> { &channelsL, &channelsBox, &startL, &startBox, &pickPadButton })
        c->setVisible (live);
    threshold.setVisible (live && startBox.getSelectedId() == 2);
    patternButton.setVisible (! live);
    settingsButton.setVisible (src == AudioRecorder::input);
    monitorButton.setVisible (src == AudioRecorder::input);
    throughL.setVisible (src == AudioRecorder::input);
    throughBox.setVisible (src == AudioRecorder::input);
    if (src != AudioRecorder::input) { processor.monitorInput = false; processor.inputThroughBus = -1; monitorButton.setToggleState (false, juce::dontSendNotification); throughBox.setSelectedId (1, juce::dontSendNotification); }
    // the computer's sound is listened to while COMPUTER is chosen (for the meter)
    if (src == AudioRecorder::computer)
    {
        if (! processor.startComputerAudio())
            statusLabel.setText ("Computer sound: " + processor.getComputerAudioError(), juce::dontSendNotification);
        else
            statusLabel.setText ("Listening to the computer's sound (" + juce::String ((int) processor.getComputerAudio().getSampleRate()) + " Hz)",
                                 juce::dontSendNotification);
    }
    else if (! processor.getRecorder().isBusy())
        processor.stopComputerAudio();
    resized();
}

void RecorderPanel::timerCallback()
{
    auto& r = processor.getRecorder();
    const int pad = processor.selectedPad.load();
    const auto padName = processor.getPadName (pad);
    targetLabel.setText ("Records onto pad " + padLabel (pad) + (padName.isNotEmpty() ? "  (" + padName + ")" : juce::String ("  (empty)")), juce::dontSendNotification);

    const bool live = currentSource() != AudioRecorder::output;
    meter.level = (r.isBusy() && r.getState() == AudioRecorder::recording) ? r.level.load()
                : live ? r.inputLevel.load() : processor.getMasterFx().outputPeak.load();
    meter.threshold = (live && startBox.getSelectedId() == 2) ? (float) threshold.getValue() : -100.0f;
    r.inputLevel = r.inputLevel.load() * 0.85f;
    meter.repaint();
    clipLabel.setColour (juce::Label::textColourId, r.clipped.load() ? juce::Colours::white : colours::dim);
    clipLabel.setColour (juce::Label::backgroundColourId, r.clipped.load() ? juce::Colour (0xffe0413a) : juce::Colours::transparentBlack);

    const bool busy = r.isBusy();
    recButton.setToggleState (busy, juce::dontSendNotification);
    recButton.setButtonText (r.getState() == AudioRecorder::waiting ? "WAITING..." : r.isBusy() ? "STOP" : "REC");
    for (auto* b : { &inputButton, &computerButton, &resampleButton })
        b->setEnabled (! busy);
    cancelButton.setEnabled (busy);

    const double secs = r.getSecondsRecorded();
    timeLabel.setText (juce::String (secs, 1) + " s", juce::dontSendNotification);
    timeLabel.setColour (juce::Label::textColourId, r.getState() == AudioRecorder::recording ? juce::Colour (0xffe0413a) : colours::text);

    if (r.getState() == AudioRecorder::waiting)
        statusLabel.setText (r.getSource() == AudioRecorder::output ? juce::String ("Starting with the pattern...")
                             : processor.padChoiceArmed.load() ? juce::String ("Waiting for the input above the threshold... hit the pad that should get the sample")
                                                               : juce::String ("Waiting for the input above the threshold..."), juce::dontSendNotification);
    else if (r.getState() == AudioRecorder::recording)
        statusLabel.setText (processor.padChoiceArmed.load() ? "Recording...  (hit the pad that should get it)" : "Recording...", juce::dontSendNotification);
    else if (r.getState() == AudioRecorder::finished)
    {
        if (resamplingPattern) { processor.getSequencer().stopPlayback(); resamplingPattern = false; }
        if (processor.fetchTake())
        {
            statusLabel.setText ("Take: " + juce::String (processor.getTakeSeconds(), 1) + " s.  PLAY to listen, KEEP to put it on " + padLabel (pad) + ", RETRY to record again.",
                                 juce::dontSendNotification);
            nameEditor.setText (juce::String());
        }
        else
            statusLabel.setText ("Nothing recorded.", juce::dontSendNotification);
    }

    const bool take = processor.hasTake();
    for (auto* c : std::initializer_list<juce::Component*> { &takeTitle, &nameL, &nameEditor, &chopL, &chopBox, &trimButton,
                                                              &playTakeButton, &keepButton, &retryButton, &discardButton })
        if (c->isVisible() != take) c->setVisible (take);
    if (take != ! takeArea.isEmpty()) { resized(); repaint(); }

    if (currentSource() == AudioRecorder::input && ! processor.hasAudioInput())
        statusLabel.setText ("No audio input. Choose an input device in Audio Settings.", juce::dontSendNotification);
}

void RecorderPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    if (! takeArea.isEmpty())
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (takeArea.toFloat(), 6.0f);
        g.setColour (colours::accent2.withAlpha (0.6f));
        g.drawRoundedRectangle (takeArea.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }
}

void RecorderPanel::resized()
{
    auto b = getLocalBounds().reduced (14);
    title.setBounds (b.removeFromTop (26));
    b.removeFromTop (4);
    auto row = b.removeFromTop (30);
    inputButton.setBounds (row.removeFromLeft (84)); row.removeFromLeft (4);
    if (computerButton.isVisible()) { computerButton.setBounds (row.removeFromLeft (100)); row.removeFromLeft (4); }
    resampleButton.setBounds (row.removeFromLeft (100));
    settingsButton.setBounds (row.removeFromRight (130));
    b.removeFromTop (8);

    auto line = [&b] (juce::Label& l, juce::Component& c)
    {
        auto r = b.removeFromTop (26);
        l.setBounds (r.removeFromLeft (80));
        r.removeFromLeft (8);
        c.setBounds (r);
        b.removeFromTop (5);
    };
    if (channelsBox.isVisible()) line (channelsL, channelsBox);
    if (startBox.isVisible())
    {
        auto r = b.removeFromTop (26);
        startL.setBounds (r.removeFromLeft (80));
        r.removeFromLeft (8);
        startBox.setBounds (r.removeFromLeft (r.getWidth() / 2 + 20));
        r.removeFromLeft (6);
        threshold.setBounds (r);
        b.removeFromTop (5);
    }
    line (lengthL, lengthBox);
    if (throughBox.isVisible()) line (throughL, throughBox);
    {
        auto r = b.removeFromTop (22);
        r.removeFromLeft (88);
        if (monitorButton.isVisible()) monitorButton.setBounds (r);
        else if (patternButton.isVisible()) patternButton.setBounds (r);
        b.removeFromTop (2);
    }
    if (pickPadButton.isVisible())
    {
        auto r = b.removeFromTop (22);
        r.removeFromLeft (88);
        pickPadButton.setBounds (r);
    }
    b.removeFromTop (6);
    targetLabel.setBounds (b.removeFromTop (22));
    b.removeFromTop (6);
    auto recRow = b.removeFromTop (50);
    recButton.setBounds (recRow.removeFromLeft (150)); recRow.removeFromLeft (8);
    cancelButton.setBounds (recRow.removeFromLeft (90).withSizeKeepingCentre (90, 32)); recRow.removeFromLeft (8);
    timeLabel.setBounds (recRow);
    b.removeFromTop (8);
    auto meterRow = b.removeFromTop (16);
    clipLabel.setBounds (meterRow.removeFromRight (44));
    meterRow.removeFromRight (6);
    meter.setBounds (meterRow);
    b.removeFromTop (6);
    statusLabel.setBounds (b.removeFromTop (36));
    b.removeFromTop (6);

    takeArea = {};
    if (processor.hasTake())
    {
        takeArea = b.removeFromTop (juce::jmin (b.getHeight(), 128));
        auto t = takeArea.reduced (10, 6);
        takeTitle.setBounds (t.removeFromTop (18));
        auto r1 = t.removeFromTop (26);
        nameL.setBounds (r1.removeFromLeft (50));
        nameEditor.setBounds (r1);
        t.removeFromTop (4);
        auto r2 = t.removeFromTop (24);
        trimButton.setBounds (r2.removeFromLeft (r2.getWidth() / 2));
        chopL.setBounds (r2.removeFromLeft (76));
        chopBox.setBounds (r2);
        t.removeFromTop (6);
        auto r3 = t.removeFromTop (30);
        const int w = (r3.getWidth() - 18) / 4;
        for (auto* bt : { &playTakeButton, &keepButton, &retryButton, &discardButton })
        {
            bt->setBounds (r3.removeFromLeft (w));
            r3.removeFromLeft (6);
        }
    }
}

//==============================================================================
BrowserPanel::BrowserPanel (RezalicaProcessor& p) : processor (p)
{
    thread.startThread (juce::Thread::Priority::low);
    refreshButton.setTooltip ("Read this folder again (new samples appear by themselves after a moment too)");
    refreshButton.setWantsKeyboardFocus (false);
    refreshButton.onClick = [this] { contents.refresh(); folderStamp = contents.getDirectory().getLastModificationTime(); };
    addAndMakeVisible (refreshButton);
    list.addListener (this);
    list.setColour (juce::ListBox::backgroundColourId, colours::panel);
    list.setColour (juce::DirectoryContentsDisplayComponent::textColourId, colours::text);
    list.setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, colours::accent.withAlpha (0.55f));
    list.setColour (juce::DirectoryContentsDisplayComponent::highlightedTextColourId, juce::Colours::white);
    list.setRowHeight (22);
    list.addMouseListener (this, true); // drag files out of the list onto pads / the sample view
    addAndMakeVisible (list);

    places.setWantsKeyboardFocus (false);
    places.onChange = [this]
    {
        const int i = places.getSelectedId() - 1;
        if (i >= 0 && i < placeFiles.size()) setFolder (placeFiles[i]);
    };
    addAndMakeVisible (places);

    favoritesButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffe9c46a));
    favoritesButton.setTooltip ("Your favourite sample folders (right-click a folder in the list to add it)");
    favoritesButton.onClick = [this] { showFavoritesMenu(); };
    for (auto* b : { &favoritesButton, &upButton, &folderButton, &loadButton, &loadNextButton, &stopButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    upButton.onClick = [this] { setFolder (contents.getDirectory().getParentDirectory()); };
    folderButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a sample folder", contents.getDirectory());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().isDirectory()) setFolder (fc.getResult());
        });
    };
    loadButton.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.8f));
    loadButton.onClick = [this] { loadToPad (list.getSelectedFile(), false); };
    loadNextButton.setTooltip ("Load onto the selected pad, then select the next pad (fill a bank quickly)");
    loadNextButton.onClick = [this] { loadToPad (list.getSelectedFile(), true); };
    stopButton.onClick = [this] { processor.previewFile ({}); };

    autoPreview.setToggleState (true, juce::dontSendNotification);
    autoPreview.setWantsKeyboardFocus (false);
    autoPreview.setColour (juce::ToggleButton::textColourId, colours::text);
    autoPreview.setColour (juce::ToggleButton::tickColourId, colours::accent);
    addAndMakeVisible (autoPreview);

    pathLabel.setColour (juce::Label::textColourId, colours::dim);
    pathLabel.setFont (juce::FontOptions (11.0f));
    targetLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    helpLabel.setText ("Click: preview   Double-click: load   Drag: onto a pad or the sample view", juce::dontSendNotification);
    helpLabel.setColour (juce::Label::textColourId, colours::dim);
    helpLabel.setFont (juce::FontOptions (11.0f));
    for (auto* l : { &pathLabel, &targetLabel, &helpLabel })
        addAndMakeVisible (*l);

    // FILES / PLUGINS
    for (auto* t : { &filesTab, &pluginsTab })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (8301);
        t->setWantsKeyboardFocus (false);
        t->setColour (juce::TextButton::buttonOnColourId, colours::accent2);
        addAndMakeVisible (*t);
    }
    filesTab.setToggleState (true, juce::dontSendNotification);
    filesTab.onClick = [this] { showPlugins (false); };
    pluginsTab.onClick = [this] { showPlugins (true); };
    pluginsTab.setTooltip ("Third-party VST3 effects: drag one into a slot of the mixer (BUS A-D, MASTER)");
    pluginList.setRowHeight (24);
    pluginList.setColour (juce::ListBox::backgroundColourId, colours::panel);
    addChildComponent (pluginList);
    scanButton.setWantsKeyboardFocus (false);
    scanButton.setTooltip ("Look for VST3 effects in the VST3 folders (plug-ins that crash the scan are skipped next time)");
    scanButton.onClick = [this] { processor.pluginHost->startScan(); };
    addChildComponent (scanButton);
    vstFoldersButton.setWantsKeyboardFocus (false);
    vstFoldersButton.onClick = [this]
    {
        auto path = processor.pluginHost->getSearchPath();
        juce::PopupMenu m;
        for (int i = 0; i < path.getNumPaths(); ++i)
            m.addItem (100 + i, "Remove  " + path.getRawString (i));
        m.addSeparator();
        m.addItem (1, "Add a folder...");
        m.addItem (2, "Back to the default VST3 folders");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&vstFoldersButton), [this, path] (int r)
        {
            auto p = path;
            if (r == 1)
            {
                chooser = std::make_unique<juce::FileChooser> ("Add a VST3 folder", juce::File ("C:\\Program Files\\Common Files\\VST3"));
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, p] (const juce::FileChooser& fc) mutable
                {
                    if (fc.getResult().isDirectory()) { p.add (fc.getResult()); processor.pluginHost->setSearchPath (p); }
                });
            }
            else if (r == 2) { AppSettings::get().setValue ("pluginFolders", juce::String()); AppSettings::get().saveIfNeeded(); }
            else if (r >= 100) { p.remove (r - 100); processor.pluginHost->setSearchPath (p); }
        });
    };
    addChildComponent (vstFoldersButton);
    skippedButton.setWantsKeyboardFocus (false);
    skippedButton.setTooltip ("Plug-ins that crashed or froze while being scanned: Rezalica skips them");
    skippedButton.onClick = [this]
    {
        const auto failedFiles = processor.pluginHost->getFailedFiles();
        juce::PopupMenu m;
        m.addSectionHeader (failedFiles.isEmpty() ? juce::String ("No plug-in crashed the scan")
                                                  : juce::String (failedFiles.size()) + " plug-ins crashed or froze while being scanned:");
        for (int i = 0; i < failedFiles.size(); ++i)
            m.addItem (100 + i, juce::File (failedFiles[i]).getFileNameWithoutExtension() + "      " + failedFiles[i], true, false);
        if (! failedFiles.isEmpty())
        {
            m.addSeparator();
            m.addItem (1, "Try them again (next SCAN)");
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&skippedButton), [this, failedFiles] (int r)
        {
            if (r == 1) processor.pluginHost->clearFailed();
            else if (r >= 100 && r < 100 + failedFiles.size()) juce::File (failedFiles[r - 100]).revealToUser();
        });
    };
    addChildComponent (skippedButton);
    scanStatus.setColour (juce::Label::textColourId, colours::dim);
    scanStatus.setFont (juce::FontOptions (11.0f));
    addChildComponent (scanStatus);
    processor.pluginHost->addChangeListener (this);
    pluginRows.effects = processor.pluginHost->getEffects();
    pluginRows.openMaker = AppSettings::get().getValue ("pluginMaker");
    pluginRows.rebuild();
    pluginRows.onChanged = [this]
    {
        AppSettings::get().setValue ("pluginMaker", pluginRows.openMaker);
        pluginList.deselectAllRows();
        pluginList.updateContent();
        pluginList.scrollToEnsureRowIsOnscreen (0);
        pluginList.repaint();
    };

    fillPlaces();
    juce::File start (AppSettings::get().getValue ("browserFolder"));
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    setFolder (start);

    setSize (560, 640);
    startTimerHz (5);
}

BrowserPanel::~BrowserPanel()
{
    processor.pluginHost->removeChangeListener (this);
    stopTimer();
    processor.previewFile ({});
    list.removeListener (this);
    thread.stopThread (2000);
}

void BrowserPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    pluginRows.effects = processor.pluginHost->getEffects();
    pluginRows.rebuild();
    pluginList.updateContent();
    pluginList.repaint();
    scanStatus.setText (processor.pluginHost->getScanStatus(), juce::dontSendNotification);
    scanButton.setEnabled (! processor.pluginHost->isScanning());
    const int nFailed = processor.pluginHost->getFailedFiles().size();
    skippedButton.setButtonText ("SKIPPED (" + juce::String (nFailed) + ")");
    skippedButton.setColour (juce::TextButton::buttonColourId, nFailed > 0 ? juce::Colour (0xffe0a020) : colours::buttonFace);
}

void BrowserPanel::showPlugins (bool plugins)
{
    for (auto* c : std::initializer_list<juce::Component*> { &list, &places, &upButton, &folderButton, &favoritesButton, &refreshButton, &pathLabel,
                                                              &loadButton, &loadNextButton, &stopButton, &autoPreview, &targetLabel })
        c->setVisible (! plugins);
    for (auto* c : std::initializer_list<juce::Component*> { &pluginList, &scanButton, &vstFoldersButton, &skippedButton, &scanStatus })
        c->setVisible (plugins);
    helpLabel.setText (plugins ? "Drag an effect onto a slot of BUS A-D or MASTER in the mixer"
                               : "Click: preview   Double-click: load   Drag: onto a pad or the sample view", juce::dontSendNotification);
    changeListenerCallback (nullptr);
    resized();
}

void BrowserPanel::fillPlaces()
{
    placeFiles.clear();
    places.clear (juce::dontSendNotification);
    auto add = [this] (const juce::String& name, const juce::File& f)
    {
        if (! f.isDirectory()) return;
        placeFiles.add (f);
        places.addItem (name, placeFiles.size());
    };
    const auto favorites = AppSettings::getFavorites();
    if (! favorites.isEmpty())
    {
        places.addSectionHeading ("Favorites");
        for (auto& f : favorites)
            add (juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 ")) + juce::File (f).getFileName(), juce::File (f));
        places.addSeparator();
    }
    add ("Rezalica recordings", RezalicaProcessor::getRecordingFolder());
    add ("Music", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    add ("Desktop", juce::File::getSpecialLocation (juce::File::userDesktopDirectory));
    add ("Documents", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    add ("Downloads", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"));
    add ("Home", juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    juce::Array<juce::File> roots;
    juce::File::findFileSystemRoots (roots);
    for (auto& r : roots)
        add ("Drive " + r.getFullPathName(), r);
}

void BrowserPanel::setFolder (const juce::File& f)
{
    if (! f.isDirectory()) return;
    contents.setDirectory (f, true, true);
    folderStamp = f.getLastModificationTime();
    pathLabel.setText (f.getFullPathName(), juce::dontSendNotification);
    AppSettings::get().setValue ("browserFolder", f.getFullPathName());
    AppSettings::get().saveIfNeeded();
    places.setSelectedId (placeFiles.indexOf (f) + 1, juce::dontSendNotification);
    if (places.getSelectedId() == 0)
        places.setText (f.getFileName(), juce::dontSendNotification);
    favoritesButton.setButtonText (AppSettings::isFavorite (f) ? juce::String (juce::CharPointer_UTF8 ("FAVORITES \xe2\x98\x85")) : juce::String ("FAVORITES"));
}

void BrowserPanel::fileClicked (const juce::File& f, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showItemMenu (f);
}

void BrowserPanel::showItemMenu (const juce::File& f)
{
    juce::PopupMenu m;
    const auto folder = f.isDirectory() ? f : f.getParentDirectory();
    const bool fav = AppSettings::isFavorite (folder);
    m.addSectionHeader (f.getFileName());
    if (f.isDirectory())
        m.addItem (1, "Open folder");
    else
    {
        m.addItem (4, "Load onto the selected pad");
        m.addItem (5, "Preview");
    }
    m.addSeparator();
    m.addItem (2, fav ? "Remove \"" + folder.getFileName() + "\" from FAVORITES" : "Add \"" + folder.getFileName() + "\" to FAVORITES");
    m.addItem (3, "Show in Explorer");
    juce::Component::SafePointer<BrowserPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, f, folder, fav] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->setFolder (f);
        else if (r == 2)
        {
            AppSettings::setFavorite (folder, ! fav);
            safe->fillPlaces();
            safe->setFolder (safe->contents.getDirectory());
        }
        else if (r == 3) f.revealToUser();
        else if (r == 4) safe->loadToPad (f, false);
        else if (r == 5) safe->processor.previewFile (f);
    });
}

void BrowserPanel::showFavoritesMenu()
{
    juce::PopupMenu m;
    const auto favorites = AppSettings::getFavorites();
    const auto current = contents.getDirectory();
    m.addSectionHeader ("FAVORITES");
    for (int i = 0; i < favorites.size(); ++i)
    {
        const juce::File f (favorites[i]);
        m.addItem (100 + i, f.getFileName() + "    (" + f.getParentDirectory().getFullPathName() + ")", f.isDirectory(), f == current);
    }
    if (favorites.isEmpty())
        m.addItem (99, "(no favourites yet: right-click a folder in the list)", false);
    m.addSeparator();
    const bool fav = AppSettings::isFavorite (current);
    m.addItem (1, fav ? "Remove this folder from FAVORITES" : "Add this folder to FAVORITES");
    if (! favorites.isEmpty())
    {
        juce::PopupMenu remove;
        for (int i = 0; i < favorites.size(); ++i)
            remove.addItem (300 + i, juce::File (favorites[i]).getFileName());
        m.addSubMenu ("Remove from FAVORITES", remove);
    }
    juce::Component::SafePointer<BrowserPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&favoritesButton), [safe, favorites, current, fav] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 100 && r < 100 + favorites.size()) { safe->setFolder (juce::File (favorites[r - 100])); return; }
        if (r == 1) AppSettings::setFavorite (current, ! fav);
        else if (r >= 300 && r < 300 + favorites.size()) AppSettings::setFavorite (juce::File (favorites[r - 300]), false);
        safe->fillPlaces();
        safe->setFolder (safe->contents.getDirectory());
    });
}

void BrowserPanel::selectionChanged()
{
    const auto f = list.getSelectedFile();
    if (autoPreview.getToggleState() && f.existsAsFile())
        processor.previewFile (f);
}

void BrowserPanel::fileDoubleClicked (const juce::File& f)
{
    if (f.isDirectory())
        setFolder (f);
    else
        loadToPad (f, false);
}

void BrowserPanel::loadToPad (const juce::File& f, bool nextPad)
{
    if (! f.existsAsFile()) return;
    processor.previewFile ({});
    const int pad = processor.selectedPad.load();
    processor.pushUndo ("Load " + f.getFileName());
    if (processor.loadFileForChopping (f) && nextPad)
    {
        const int next = (pad + 1) % numPads;
        processor.selectedPad = next;
        processor.currentBank = next / padsPerBank;
    }
}

void BrowserPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6 || ! list.isParentOf (e.eventComponent))
        return;
    const auto f = list.getSelectedFile();
    if (! f.existsAsFile())
        return;
    dragStarted = true;
    processor.previewFile ({});
    // a small label as the drag image
    juce::Image img (juce::Image::ARGB, 220, 24, true);
    {
        juce::Graphics g (img);
        g.setColour (colours::accent.withAlpha (0.85f));
        g.fillRoundedRectangle (0.0f, 0.0f, 220.0f, 24.0f, 5.0f);
        g.setColour (juce::Colours::black);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (f.getFileName(), 8, 0, 204, 24, juce::Justification::centredLeft, true);
    }
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->startDragging ("rzfile:" + f.getFullPathName(), &list, juce::ScaledImage (img), true);
    else
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void BrowserPanel::timerCallback()
{
    // files added to (or removed from) the shown folder appear without reopening it
    if (++folderCheck % 10 == 0)
    {
        const auto dir = contents.getDirectory();
        const auto stamp = dir.getLastModificationTime();
        if (dir.isDirectory() && stamp != folderStamp)
        {
            if (folderStamp != juce::Time()) contents.refresh();
            folderStamp = stamp;
        }
    }

    const int pad = processor.selectedPad.load();
    const auto name = processor.getPadName (pad);
    targetLabel.setText ("Selected pad: " + padLabel (pad) + "  " + (name.isNotEmpty() ? "(" + name + ")" : juce::String ("(empty)")), juce::dontSendNotification);
}

void BrowserPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void BrowserPanel::resized()
{
    auto b = getLocalBounds().reduced (10);
    {
        auto tabs = b.removeFromTop (26);
        filesTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).reduced (0, 0).withTrimmedRight (3));
        pluginsTab.setBounds (tabs.withTrimmedLeft (3));
        b.removeFromTop (6);
    }
    if (pluginsTab.getToggleState())
    {
        auto row = b.removeFromTop (28);
        scanButton.setBounds (row.removeFromLeft (64)); row.removeFromLeft (4);
        skippedButton.setBounds (row.removeFromRight (106)); row.removeFromRight (4);
        vstFoldersButton.setBounds (row);
        b.removeFromTop (4);
        scanStatus.setBounds (b.removeFromTop (18));
        b.removeFromTop (4);
        helpLabel.setBounds (b.removeFromBottom (18));
        b.removeFromBottom (4);
        pluginList.setBounds (b);
        return;
    }
    auto top = b.removeFromTop (28);
    upButton.setBounds (top.removeFromLeft (50)); top.removeFromLeft (6);
    folderButton.setBounds (top.removeFromRight (90)); top.removeFromRight (6);
    places.setBounds (top);
    b.removeFromTop (4);
    auto favRow = b.removeFromTop (26);
    favoritesButton.setBounds (favRow.removeFromLeft (120)); favRow.removeFromLeft (8);
    refreshButton.setBounds (favRow.removeFromRight (76)); favRow.removeFromRight (6);
    pathLabel.setBounds (favRow);
    b.removeFromTop (4);

    auto bottom = b.removeFromBottom (18);
    helpLabel.setBounds (bottom);
    b.removeFromBottom (6);
    auto options = b.removeFromBottom (26);
    stopButton.setBounds (options.removeFromRight (70)); options.removeFromRight (6);
    autoPreview.setBounds (options);
    b.removeFromBottom (4);
    auto btns = b.removeFromBottom (30);
    loadButton.setBounds (btns.removeFromLeft (btns.getWidth() * 45 / 100)); btns.removeFromLeft (6);
    loadNextButton.setBounds (btns);
    b.removeFromBottom (6);
    targetLabel.setBounds (b.removeFromBottom (22));
    b.removeFromBottom (4);
    list.setBounds (b);
}

//==============================================================================
MasterFxPanel::MasterFxPanel (RezalicaProcessor& p) : processor (p)
{
    auto& s = processor.masterFxSettings;
    auto setup = [this] (Knob& k, std::atomic<float>& v, double lo, double hi, double step, double mid, const juce::String& suffix)
    {
        k.slider.setRange (lo, hi, step);
        if (mid > lo && mid < hi) k.slider.setSkewFactorFromMidPoint (mid);
        k.slider.setTextValueSuffix (suffix);
        k.slider.setValue (v.load(), juce::dontSendNotification);
        k.slider.onValueChange = [&k, &v] { v = (float) k.slider.getValue(); };
        bindings.push_back ({ &k, &v });
        addAndMakeVisible (k);
    };
    setup (lowKnob, s.lowDb, -12.0, 12.0, 0.1, 0.0, " dB");
    setup (midKnob, s.midDb, -12.0, 12.0, 0.1, 0.0, " dB");
    setup (freqKnob, s.midFreq, 100.0, 10000.0, 1.0, 1000.0, " Hz");
    setup (highKnob, s.highDb, -12.0, 12.0, 0.1, 0.0, " dB");
    setup (thrKnob, s.threshold, -48.0, 0.0, 0.1, 0.0, " dB");
    setup (ratioKnob, s.ratio, 1.0, 20.0, 0.1, 4.0, ":1");
    setup (attKnob, s.attackMs, 0.1, 100.0, 0.1, 10.0, " ms");
    setup (relKnob, s.releaseMs, 10.0, 1000.0, 1.0, 120.0, " ms");
    setup (makeupKnob, s.makeupDb, 0.0, 18.0, 0.1, 0.0, " dB");
    setup (ceilKnob, s.ceilingDb, -12.0, 0.0, 0.1, 0.0, " dB");

    auto toggle = [this] (juce::TextButton& b, std::atomic<bool>& v)
    {
        styleToggleButton (b, colours::accent);
        b.setToggleState (v.load(), juce::dontSendNotification);
        b.onClick = [&b, &v] { v = b.getToggleState(); };
        addAndMakeVisible (b);
    };
    toggle (eqButton, s.eqOn);
    toggle (compButton, s.compOn);
    toggle (limButton, s.limiterOn);
    eqButton.setTooltip ("3-band EQ: low shelf 100 Hz, mid peak, high shelf 8 kHz");
    compButton.setTooltip ("Glue compressor on the whole output");
    limButton.setTooltip ("Brickwall limiter: the output never goes above the ceiling");

    resetButton.setWantsKeyboardFocus (false);
    resetButton.onClick = [this]
    {
        processor.masterFxSettings.fromValueTree (juce::ValueTree ("MASTERFX"));
        timerCallback();
    };
    addAndMakeVisible (resetButton);

    grLabel.setColour (juce::Label::textColourId, colours::accent2);
    grLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    outLabel.setText ("OUTPUT", juce::dontSendNotification);
    outLabel.setColour (juce::Label::textColourId, colours::dim);
    outLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (grLabel);
    addAndMakeVisible (outLabel);
    addAndMakeVisible (outMeter);

    setSize (720, 330);
    startTimerHz (20);
}

void MasterFxPanel::timerCallback()
{
    auto& s = processor.masterFxSettings;
    for (auto& b : bindings)
        if (! b.knob->slider.isMouseButtonDown())
            b.knob->slider.setValue (b.value->load(), juce::dontSendNotification);
    eqButton.setToggleState (s.eqOn.load(), juce::dontSendNotification);
    compButton.setToggleState (s.compOn.load(), juce::dontSendNotification);
    limButton.setToggleState (s.limiterOn.load(), juce::dontSendNotification);
    outMeter.level = processor.getMasterFx().outputPeak.load();
    outMeter.repaint();
    grLabel.setText (s.compOn.load() ? "GR " + juce::String (processor.getMasterFx().gainReductionDb.load(), 1) + " dB" : juce::String(),
                     juce::dontSendNotification);
}

void MasterFxPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    sectionTitle (g, eqArea, "EQ");
    sectionTitle (g, compArea, "COMPRESSOR");
    sectionTitle (g, limArea, "LIMITER");
}

void MasterFxPanel::resized()
{
    auto b = getLocalBounds().reduced (10);
    auto bottom = b.removeFromBottom (24);
    resetButton.setBounds (bottom.removeFromRight (70));
    bottom.removeFromRight (10);
    outLabel.setBounds (bottom.removeFromLeft (52));
    outMeter.setBounds (bottom.withSizeKeepingCentre (bottom.getWidth(), 12));
    b.removeFromBottom (8);

    const int w = b.getWidth();
    eqArea = b.removeFromLeft (w * 4 / 10).reduced (3);
    compArea = b.removeFromLeft (w * 4 / 10).reduced (3);
    limArea = b.reduced (3);

    auto layout = [] (juce::Rectangle<int> area, juce::TextButton& button, std::initializer_list<Knob*> knobs, int perRow)
    {
        auto a = area.reduced (8);
        button.setBounds (a.removeFromTop (28).removeFromLeft (juce::jmin (130, a.getWidth())));
        a.removeFromTop (6);
        const int rows = ((int) knobs.size() + perRow - 1) / perRow;
        const int rh = a.getHeight() / juce::jmax (1, rows), kw = a.getWidth() / perRow;
        int i = 0;
        for (auto* k : knobs)
        {
            k->setBounds (a.getX() + (i % perRow) * kw, a.getY() + (i / perRow) * rh, kw, rh);
            ++i;
        }
    };
    layout (eqArea, eqButton, { &lowKnob, &midKnob, &freqKnob, &highKnob }, 2);
    layout (compArea, compButton, { &thrKnob, &ratioKnob, &makeupKnob, &attKnob, &relKnob }, 3);
    layout (limArea, limButton, { &ceilKnob }, 1);
    grLabel.setBounds (compArea.reduced (8).removeFromTop (28).removeFromRight (100));
}
} // namespace rz
