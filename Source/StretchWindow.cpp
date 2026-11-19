#include "StretchWindow.h"

namespace rz
{
namespace
{
void sectionLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, colours::dim);
}

void hintLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (11.0f));
    l.setColour (juce::Label::textColourId, colours::dim);
    l.setJustificationType (juce::Justification::topLeft);
}

void radio (juce::TextButton& b, int group)
{
    b.setClickingTogglesState (true);
    b.setRadioGroupId (group);
    b.setWantsKeyboardFocus (false);
}
} // namespace

StretchPanel::StretchPanel (RezalicaProcessor& p) : processor (p)
{
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (title);

    info.setFont (juce::FontOptions (12.0f));
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);

    sectionLabel (modeLabel, "WARP MODE");
    sectionLabel (lengthLabel, "LENGTH (TIME STRETCH TO)");
    sectionLabel (tempoLabel, "TEMPO");
    sectionLabel (scopeLabel, "APPLY TO");
    sectionLabel (beatsLabel, "BEATS OPTIONS");
    for (auto* l : { &modeLabel, &lengthLabel, &tempoLabel, &scopeLabel, &beatsLabel })
        addAndMakeVisible (*l);

    // mode
    radio (beatsButton, 9101);
    radio (complexButton, 9101);
    beatsButton.onClick = [this] { if (beatsButton.getToggleState()) { ui.mode = WarpSettings::beats; settingsChanged (true); } };
    complexButton.onClick = [this] { if (complexButton.getToggleState()) { ui.mode = WarpSettings::complex; settingsChanged (true); } };
    hintLabel (beatsHint, "For drums and percussive loops: keeps every hit (transient) sharp, only the spaces between them change.");
    hintLabel (complexHint, "For melodic, harmonic and mixed material: smoother, pitch and tone stay natural.");
    for (auto* c : { (juce::Component*) &beatsButton, (juce::Component*) &complexButton, (juce::Component*) &beatsHint, (juce::Component*) &complexHint })
        addAndMakeVisible (*c);

    // length
    for (int i = 0; i < (int) lengthButtons.size(); ++i)
    {
        auto& b = lengthButtons[(size_t) i];
        b.setButtonText (WarpSettings::lengthNames()[i]);
        radio (b, 9102);
        b.onClick = [this, i] { if (lengthButtons[(size_t) i].getToggleState()) { ui.lengthIndex = i; settingsChanged (true); } };
        addAndMakeVisible (b);
    }

    // tempo
    syncButton.setClickingTogglesState (true);
    syncButton.setWantsKeyboardFocus (false);
    syncButton.setColour (juce::TextButton::buttonOnColourId, colours::accent2);
    syncButton.onClick = [this] { ui.syncHost = syncButton.getToggleState(); settingsChanged (true); };
    addAndMakeVisible (syncButton);

    bpmSlider.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider.setRange (40.0, 240.0, 0.1);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.setWantsKeyboardFocus (false);
    bpmSlider.setColour (juce::Slider::trackColourId, colours::panelLight.brighter (0.15f));
    bpmSlider.onValueChange = [this] { ui.bpm = (float) bpmSlider.getValue(); settingsChanged (false); };
    bpmSlider.onDragEnd = [this] { settingsChanged (true); };
    addAndMakeVisible (bpmSlider);

    // scope
    radio (wholeButton, 9103);
    radio (padOnlyButton, 9103);
    wholeButton.setTooltip ("Stretch the whole sample; all of its slices / pads follow");
    padOnlyButton.setTooltip ("Stretch only the selected pad's part (between its markers)");
    wholeButton.onClick = [this] { if (wholeButton.getToggleState()) { ui.wholeSample = true; settingsChanged (true); } };
    padOnlyButton.onClick = [this] { if (padOnlyButton.getToggleState()) { ui.wholeSample = false; settingsChanged (true); } };
    addAndMakeVisible (wholeButton);
    addAndMakeVisible (padOnlyButton);

    // beats options
    preserveBox.addItemList ({ "Transients", "1/8", "1/16" }, 1);
    preserveBox.onChange = [this] { ui.preserve = preserveBox.getSelectedId() - 1; settingsChanged (true); };
    loopBox.addItemList ({ "Off", "Loop Forward", "Loop Back-and-Forth" }, 1);
    loopBox.onChange = [this] { ui.loopMode = loopBox.getSelectedId() - 1; settingsChanged (true); };
    envelopeSlider.setSliderStyle (juce::Slider::LinearBar);
    envelopeSlider.setRange (0.0, 100.0, 1.0);
    envelopeSlider.setColour (juce::Slider::trackColourId, colours::accent.withAlpha (0.5f));
    envelopeSlider.onValueChange = [this] { ui.envelope = (float) envelopeSlider.getValue(); settingsChanged (false); };
    envelopeSlider.onDragEnd = [this] { settingsChanged (true); };
    hintLabel (preserveLabel, "Preserve");
    hintLabel (loopLabel, "Transient loop");
    hintLabel (envelopeLabel, "Transient envelope");
    for (auto* c : { (juce::Component*) &preserveBox, (juce::Component*) &loopBox, (juce::Component*) &envelopeSlider,
                     (juce::Component*) &preserveLabel, (juce::Component*) &loopLabel, (juce::Component*) &envelopeLabel })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }

    // on / off
    warpOnButton.setColour (juce::TextButton::buttonColourId, colours::accent);
    warpOnButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    warpOnButton.setTooltip ("Time stretch the sample with these settings (re-applies when you change them)");
    warpOffButton.setTooltip ("Go back to the original, unstretched sample");
    warpOnButton.onClick = [this]
    {
        ui.enabled = true;
        processor.requestWarp (processor.selectedPad.load(), ui);
        updateInfo();
    };
    warpOffButton.onClick = [this]
    {
        ui.enabled = false;
        processor.requestWarp (processor.selectedPad.load(), ui);
        updateInfo();
    };
    for (auto* b : { &warpOnButton, &warpOffButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    setSize (640, 480);
    loadFromPad();
    startTimerHz (8);
}

StretchPanel::~StretchPanel()
{
    stopTimer();
}

void StretchPanel::loadFromPad()
{
    shownPad = processor.selectedPad.load();
    ui = processor.getWarpSettings (shownPad);

    beatsButton.setToggleState (ui.mode == WarpSettings::beats, juce::dontSendNotification);
    complexButton.setToggleState (ui.mode == WarpSettings::complex, juce::dontSendNotification);
    for (int i = 0; i < (int) lengthButtons.size(); ++i)
        lengthButtons[(size_t) i].setToggleState (i == ui.lengthIndex, juce::dontSendNotification);
    syncButton.setToggleState (ui.syncHost, juce::dontSendNotification);
    bpmSlider.setValue (ui.bpm, juce::dontSendNotification);
    wholeButton.setToggleState (ui.wholeSample, juce::dontSendNotification);
    padOnlyButton.setToggleState (! ui.wholeSample, juce::dontSendNotification);
    preserveBox.setSelectedId (ui.preserve + 1, juce::dontSendNotification);
    loopBox.setSelectedId (ui.loopMode + 1, juce::dontSendNotification);
    envelopeSlider.setValue (ui.envelope, juce::dontSendNotification);
    updateInfo();
}

void StretchPanel::settingsChanged (bool reapply)
{
    const bool beats = ui.mode == WarpSettings::beats;
    for (auto* c : { (juce::Component*) &preserveBox, (juce::Component*) &loopBox, (juce::Component*) &envelopeSlider })
        c->setEnabled (beats);
    bpmSlider.setEnabled (! ui.syncHost);

    // while the warp is on, changes are applied straight away
    if (reapply && processor.isPadWarped (processor.selectedPad.load()))
    {
        ui.enabled = true;
        processor.requestWarp (processor.selectedPad.load(), ui);
    }
    updateInfo();
}

void StretchPanel::updateInfo()
{
    const int pad = processor.selectedPad.load();
    auto s = processor.getEngine().getPadSample (pad);
    const bool warped = s != nullptr && s->isWarped();

    title.setText ("TIME STRETCH / WARP  -  PAD " + padLabel (pad)
                       + (processor.isWarpBusy() ? "   (stretching...)" : warped ? "   (WARP ON)" : "   (WARP OFF)"),
                   juce::dontSendNotification);

    const bool beats = ui.mode == WarpSettings::beats;
    for (auto* c : { (juce::Component*) &preserveBox, (juce::Component*) &loopBox, (juce::Component*) &envelopeSlider })
        c->setEnabled (beats);
    bpmSlider.setEnabled (! ui.syncHost);
    if (ui.syncHost && ! bpmSlider.isMouseButtonDown())
        bpmSlider.setValue (processor.getWarpTempo (ui), juce::dontSendNotification);

    if (s == nullptr)
    {
        info.setText ("This pad is empty. Load a sample first.", juce::dontSendNotification);
        return;
    }

    // original (unstretched) part that would be stretched
    auto base = warped ? s->warpSource : s;
    auto& ps = processor.getEngine().getPad (pad);
    float ra, rb;
    if (warped)                 { ra = s->regionStart; rb = s->regionEnd; }
    else if (ui.wholeSample)    { ra = 0.0f; rb = 1.0f; }
    else                        { ra = ps.start.load(); rb = ps.end.load(); }
    const double origSeconds = (rb - ra) * base->buffer.getNumSamples() / base->sampleRate;
    const double bpm = processor.getWarpTempo (ui);
    const double targetSeconds = ui.lengthBeats() * 60.0 / bpm;
    const double origBpm = origSeconds > 0.0 ? ui.lengthBeats() * 60.0 / origSeconds : 0.0;

    juce::String t;
    t << "Sample: " << base->name << "\n"
      << "Original: " << juce::String (origSeconds, 3) << " s  (as " << WarpSettings::lengthNames()[ui.lengthIndex]
      << " that is about " << juce::String (origBpm, 1) << " BPM)\n"
      << "Target: " << WarpSettings::lengthNames()[ui.lengthIndex] << " at " << juce::String (bpm, 1) << " BPM = "
      << juce::String (targetSeconds, 3) << " s   (stretch x" << juce::String (targetSeconds / juce::jmax (1.0e-6, origSeconds), 3) << ")\n"
      << (warped ? "The stretched sample is saved in Documents/Rezalica/Warped (in the background)."
                 : "Press WARP ON to stretch. The original stays untouched.");
    info.setText (t, juce::dontSendNotification);
}

void StretchPanel::timerCallback()
{
    if (processor.selectedPad.load() != shownPad)
        loadFromPad();
    else
        updateInfo();
}

void StretchPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void StretchPanel::resized()
{
    auto b = getLocalBounds().reduced (16);
    title.setBounds (b.removeFromTop (24));
    b.removeFromTop (4);
    info.setBounds (b.removeFromTop (70));
    b.removeFromTop (8);

    modeLabel.setBounds (b.removeFromTop (18));
    {
        auto row = b.removeFromTop (30);
        const int w = (row.getWidth() - 10) / 2;
        beatsButton.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (10);
        complexButton.setBounds (row);
        auto hints = b.removeFromTop (32);
        beatsHint.setBounds (hints.removeFromLeft (w));
        hints.removeFromLeft (10);
        complexHint.setBounds (hints);
    }
    b.removeFromTop (8);

    lengthLabel.setBounds (b.removeFromTop (18));
    {
        auto row = b.removeFromTop (30);
        const int w = (row.getWidth() - 6 * 6) / 7;
        for (auto& lb : lengthButtons)
        {
            lb.setBounds (row.removeFromLeft (w));
            row.removeFromLeft (6);
        }
    }
    b.removeFromTop (12);

    {
        auto row = b.removeFromTop (18);
        const int w = (row.getWidth() - 10) / 2;
        tempoLabel.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (10);
        scopeLabel.setBounds (row);
        auto r2 = b.removeFromTop (30);
        auto left = r2.removeFromLeft (w);
        r2.removeFromLeft (10);
        syncButton.setBounds (left.removeFromLeft (left.getWidth() / 2 + 20));
        left.removeFromLeft (6);
        bpmSlider.setBounds (left);
        const int hw = (r2.getWidth() - 6) / 2;
        wholeButton.setBounds (r2.removeFromLeft (hw));
        r2.removeFromLeft (6);
        padOnlyButton.setBounds (r2);
    }
    b.removeFromTop (12);

    beatsLabel.setBounds (b.removeFromTop (18));
    {
        auto labels = b.removeFromTop (16);
        auto row = b.removeFromTop (28);
        const int w = (row.getWidth() - 20) / 3;
        preserveLabel.setBounds (labels.removeFromLeft (w)); labels.removeFromLeft (10);
        loopLabel.setBounds (labels.removeFromLeft (w));     labels.removeFromLeft (10);
        envelopeLabel.setBounds (labels);
        preserveBox.setBounds (row.removeFromLeft (w));      row.removeFromLeft (10);
        loopBox.setBounds (row.removeFromLeft (w));          row.removeFromLeft (10);
        envelopeSlider.setBounds (row);
    }

    auto bottom = b.removeFromBottom (36);
    const int bw = (bottom.getWidth() - 10) / 2;
    warpOnButton.setBounds (bottom.removeFromLeft (bw));
    bottom.removeFromLeft (10);
    warpOffButton.setBounds (bottom);
}
} // namespace rz
