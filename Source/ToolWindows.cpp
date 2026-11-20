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
    title.setText ("SAMPLING", juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (title);

    styleToggleButton (inputButton, colours::accent2);
    styleToggleButton (resampleButton, colours::accent2);
    inputButton.setRadioGroupId (8101);
    resampleButton.setRadioGroupId (8101);
    inputButton.setToggleState (true, juce::dontSendNotification);
    inputButton.setTooltip ("Record the sound card input (microphone, line in, turntable ...)");
    resampleButton.setTooltip ("Record what Rezalica plays (pads, effects, sequencer) onto a pad");
    inputButton.onClick = [this] { updateSourceUi(); };
    resampleButton.onClick = [this] { updateSourceUi(); };
    addAndMakeVisible (inputButton);
    addAndMakeVisible (resampleButton);

    for (auto* t : { &waitButton, &patternButton })
    {
        t->setWantsKeyboardFocus (false);
        t->setColour (juce::ToggleButton::textColourId, colours::text);
        t->setColour (juce::ToggleButton::tickColourId, colours::accent);
        addAndMakeVisible (*t);
    }
    waitButton.setToggleState (true, juce::dontSendNotification);
    patternButton.setToggleState (true, juce::dontSendNotification);
    waitButton.setTooltip ("REC waits and starts at the first sound, so the sample starts right on the hit");
    patternButton.setTooltip ("Starts the selected pattern from its first step and records exactly one loop of it");

    recButton.setWantsKeyboardFocus (false);
    recButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffa32a26));
    recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0413a));
    recButton.onClick = [this]
    {
        auto& r = processor.getRecorder();
        if (r.isBusy())
        {
            r.stop();
            if (resampleButton.getToggleState() && patternButton.getToggleState())
                processor.getSequencer().stopPlayback();
            return;
        }
        const auto src = resampleButton.getToggleState() ? AudioRecorder::output : AudioRecorder::input;
        if (src == AudioRecorder::input && ! processor.hasAudioInput())
        {
            statusLabel.setText ("No audio input. Choose an input device in Audio Settings.", juce::dontSendNotification);
            return;
        }
        if (! processor.startRecording (src, src == AudioRecorder::output && patternButton.getToggleState(),
                                        src == AudioRecorder::input && waitButton.getToggleState()))
            statusLabel.setText ("Could not start recording.", juce::dontSendNotification);
    };
    addAndMakeVisible (recButton);

    cancelButton.setWantsKeyboardFocus (false);
    cancelButton.onClick = [this]
    {
        processor.getRecorder().cancel();
        statusLabel.setText ("Cancelled.", juce::dontSendNotification);
    };
    addAndMakeVisible (cancelButton);

    settingsButton.setWantsKeyboardFocus (false);
    settingsButton.onClick = [this] { if (onAudioSettings) onAudioSettings(); };
    addAndMakeVisible (settingsButton);

    for (auto* l : { &targetLabel, &statusLabel, &timeLabel, &helpLabel })
        addAndMakeVisible (*l);
    targetLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    targetLabel.setColour (juce::Label::textColourId, colours::text);
    timeLabel.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    timeLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setColour (juce::Label::textColourId, colours::accent2);
    helpLabel.setColour (juce::Label::textColourId, colours::dim);
    helpLabel.setFont (juce::FontOptions (11.0f));
    helpLabel.setJustificationType (juce::Justification::topLeft);
    helpLabel.setText ("The take is saved as a WAV in Documents/Rezalica/Recordings and loaded onto the selected pad "
                       "(ready to chop: pick a chop mode or use LAZY CHOP). UNDO brings the old sample back.",
                       juce::dontSendNotification);
    addAndMakeVisible (meter);

    updateSourceUi();
    setSize (520, 380);
    startTimerHz (20);
}

void RecorderPanel::updateSourceUi()
{
    const bool input = inputButton.getToggleState();
    waitButton.setVisible (input);
    patternButton.setVisible (! input);
    settingsButton.setVisible (input);
}

void RecorderPanel::timerCallback()
{
    auto& r = processor.getRecorder();
    const int pad = processor.selectedPad.load();
    targetLabel.setText ("Records onto pad " + padLabel (pad) + "  (" + processor.getPadName (pad) + ")", juce::dontSendNotification);

    const bool input = inputButton.getToggleState();
    meter.level = (r.isBusy() && r.getState() == AudioRecorder::recording) ? r.level.load()
                : input ? r.inputLevel.load() : processor.getMasterFx().outputPeak.load();
    r.inputLevel = r.inputLevel.load() * 0.85f;
    meter.repaint();

    const bool busy = r.isBusy();
    recButton.setToggleState (busy, juce::dontSendNotification);
    recButton.setButtonText (busy ? "STOP" : "REC");
    inputButton.setEnabled (! busy);
    resampleButton.setEnabled (! busy);
    cancelButton.setEnabled (busy);

    const double secs = r.getSecondsRecorded();
    timeLabel.setText (juce::String (secs, 1) + " s", juce::dontSendNotification);
    timeLabel.setColour (juce::Label::textColourId, r.getState() == AudioRecorder::recording ? juce::Colour (0xffe0413a) : colours::text);

    if (r.getState() == AudioRecorder::waiting)
        statusLabel.setText (r.getSource() == AudioRecorder::input ? "Waiting for sound..." : "Starting with the pattern...", juce::dontSendNotification);
    else if (r.getState() == AudioRecorder::recording)
        statusLabel.setText ("Recording...", juce::dontSendNotification);
    else if (r.getState() == AudioRecorder::finished)
    {
        const auto file = processor.collectRecording (pad);
        if (r.getSource() == AudioRecorder::output && patternButton.getToggleState())
            processor.getSequencer().stopPlayback();
        statusLabel.setText (file.existsAsFile() ? "Saved \"" + file.getFileName() + "\" onto " + padLabel (pad)
                                                 : juce::String ("Nothing recorded."), juce::dontSendNotification);
        if (file.existsAsFile() && onRecorded) onRecorded();
    }

    if (input && ! processor.hasAudioInput())
        statusLabel.setText ("No audio input. Choose an input device in Audio Settings.", juce::dontSendNotification);
}

void RecorderPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void RecorderPanel::resized()
{
    auto b = getLocalBounds().reduced (14);
    title.setBounds (b.removeFromTop (28));
    b.removeFromTop (6);
    auto row = b.removeFromTop (30);
    inputButton.setBounds (row.removeFromLeft (110)); row.removeFromLeft (6);
    resampleButton.setBounds (row.removeFromLeft (110)); row.removeFromLeft (12);
    settingsButton.setBounds (row.removeFromRight (130));
    b.removeFromTop (8);
    auto opt = b.removeFromTop (26);
    waitButton.setBounds (opt);
    patternButton.setBounds (opt);
    b.removeFromTop (6);
    targetLabel.setBounds (b.removeFromTop (22));
    b.removeFromTop (8);
    auto recRow = b.removeFromTop (54);
    recButton.setBounds (recRow.removeFromLeft (150)); recRow.removeFromLeft (8);
    cancelButton.setBounds (recRow.removeFromLeft (90).withSizeKeepingCentre (90, 32)); recRow.removeFromLeft (8);
    timeLabel.setBounds (recRow);
    b.removeFromTop (8);
    meter.setBounds (b.removeFromTop (14));
    b.removeFromTop (6);
    statusLabel.setBounds (b.removeFromTop (22));
    b.removeFromTop (4);
    helpLabel.setBounds (b);
}

//==============================================================================
BrowserPanel::BrowserPanel (RezalicaProcessor& p) : processor (p)
{
    thread.startThread (juce::Thread::Priority::low);
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
        const int i = places.getSelectedItemIndex();
        if (i >= 0 && i < placeFiles.size()) setFolder (placeFiles[i]);
    };
    addAndMakeVisible (places);

    for (auto* b : { &upButton, &folderButton, &loadButton, &loadNextButton, &stopButton })
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
    stopTimer();
    processor.previewFile ({});
    list.removeListener (this);
    thread.stopThread (2000);
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
    pathLabel.setText (f.getFullPathName(), juce::dontSendNotification);
    AppSettings::get().setValue ("browserFolder", f.getFullPathName());
    AppSettings::get().saveIfNeeded();
    places.setSelectedItemIndex (placeFiles.indexOf (f), juce::dontSendNotification);
    if (places.getSelectedItemIndex() < 0)
        places.setText (f.getFileName(), juce::dontSendNotification);
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
    auto top = b.removeFromTop (28);
    upButton.setBounds (top.removeFromLeft (50)); top.removeFromLeft (6);
    folderButton.setBounds (top.removeFromRight (90)); top.removeFromRight (6);
    places.setBounds (top);
    b.removeFromTop (4);
    pathLabel.setBounds (b.removeFromTop (18));
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
