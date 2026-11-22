#include "DeckPanel.h"

namespace rz
{
//==============================================================================
// the platter

void PlatterView::paint (juce::Graphics& g)
{
    auto& deck = processor.getDeck();
    auto rec = deck.getRecord();
    const auto c = centre();
    const float R = radius();
    const double turn = deck.samplesPerTurn();
    const double pos = deck.position.load();
    const float angle = (float) (pos / juce::jmax (1.0, turn) * juce::MathConstants<double>::twoPi);

    // shadow of the platter on the plinth
    for (int i = 6; i > 0; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f * (float) i));
        g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c.translated (4.0f, 8.0f)).expanded ((float) (6 - i) * 1.5f));
    }

    // aluminium platter rim with strobe dots (they turn with the platter)
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe4e5e7), c.x - R, c.y - R, juce::Colour (0xff86888c), c.x + R, c.y + R, false));
    g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
    for (int row = 0; row < 2; ++row)
    {
        const int n = row == 0 ? 90 : 72;
        const float rr = R - 3.5f - (float) row * 5.0f;
        g.setColour (juce::Colour (0xff2a2b2e).withAlpha (0.8f));
        for (int k = 0; k < n; ++k)
        {
            const float a = angle + juce::MathConstants<float>::twoPi * (float) k / (float) n;
            const auto p = c.getPointOnCircumference (rr, a);
            g.fillEllipse (p.x - 1.3f, p.y - 1.3f, 2.6f, 2.6f);
        }
    }

    // the record: black vinyl with grooves and a fixed reflection of the room light
    const float V = R - 15.0f;
    auto vinyl = juce::Rectangle<float> (V * 2.0f, V * 2.0f).withCentre (c);
    g.setColour (juce::Colour (0xff0b0b0c));
    g.fillEllipse (vinyl);
    for (float r = V - 3.0f; r > V * 0.36f; r -= 2.2f)
    {
        g.setColour (juce::Colours::white.withAlpha (((int) (r * 3.1f) % 5 == 0) ? 0.05f : 0.022f));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 0.8f);
    }
    {
        // the light catches the grooves in two opposite wedges (it does not turn)
        for (float base : { -2.35f, 0.79f })
        {
            juce::Path wedge;
            wedge.addPieSegment (vinyl, base - 0.32f, base + 0.32f, 0.37f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), c.x, c.y,
                                                     juce::Colours::white.withAlpha (0.0f), c.x + V * std::sin (base), c.y - V * std::cos (base), true));
            g.fillPath (wedge);
        }
    }
    g.setColour (juce::Colours::black);
    g.drawEllipse (vinyl, 1.5f);

    // the label: turns, with the sample name and a white marker (so you see the record move)
    const float L = V * 0.34f;
    auto label = juce::Rectangle<float> (L * 2.0f, L * 2.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (colours::accent.brighter (0.2f), c.x, c.y - L, colours::accent.darker (0.45f), c.x, c.y + L, false));
    g.fillEllipse (label);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (angle, c.x, c.y));
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRoundedRectangle (c.x - 2.0f, c.y - L + 3.0f, 4.0f, L * 0.42f, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.setFont (juce::FontOptions (juce::jlimit (9.0f, 14.0f, L * 0.16f), juce::Font::bold));
        const auto name = rec != nullptr ? rec->name : juce::String ("drop a sample here");
        g.drawFittedText (name, juce::Rectangle<int> ((int) (c.x - L * 0.8f), (int) (c.y + L * 0.12f), (int) (L * 1.6f), (int) (L * 0.42f)),
                          juce::Justification::centred, 2, 0.8f);
        g.setFont (juce::FontOptions (juce::jlimit (8.0f, 12.0f, L * 0.13f)));
        g.drawText ("REZALICA  33 1/3", juce::Rectangle<float> (c.x - L, c.y - L * 0.42f, L * 2.0f, L * 0.2f), juce::Justification::centred);
    }
    g.setColour (juce::Colour (0xffd9dadc));
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c)); // spindle

    // tonearm: the needle moves from the outside to the label as the sample plays
    {
        const int len = rec != nullptr ? rec->buffer.getNumSamples() : 0;
        const double wrapped = len > 0 ? pos - std::floor (pos / len) * len : 0.0;
        const float progress = len > 0 ? (float) (wrapped / len) : 0.0f;
        const float needleR = V * (0.93f - 0.52f * progress);
        const juce::Point<float> pivot (c.x + R * 0.98f, c.y - R * 0.86f);
        const auto needle = c.getPointOnCircumference (needleR, 0.95f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawLine (juce::Line<float> (pivot.translated (5.0f, 8.0f), needle.translated (5.0f, 8.0f)), 7.0f);
        g.setColour (juce::Colour (0xffd0d2d5));
        g.drawLine (juce::Line<float> (pivot, needle), 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawLine (juce::Line<float> (pivot.translated (-1.0f, -1.0f), needle.translated (-1.0f, -1.0f)), 1.2f);
        // headshell
        juce::Path head;
        head.addRoundedRectangle (-7.0f, -4.0f, 22.0f, 12.0f, 2.0f);
        const float headAngle = std::atan2 (needle.y - pivot.y, needle.x - pivot.x);
        g.setColour (juce::Colour (0xff2b2c2f));
        g.fillPath (head, juce::AffineTransform::rotation (headAngle).translated (needle));
        // pivot / counterweight
        g.setColour (juce::Colour (0xff3a3b3e));
        g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (pivot));
        g.setColour (juce::Colour (0xffbfc1c4));
        g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (pivot));
    }

    if (dropHover)
    {
        g.setColour (colours::accent2.withAlpha (0.25f));
        g.fillEllipse (vinyl);
        g.setColour (colours::accent2);
        g.drawEllipse (vinyl, 3.0f);
    }
    if (grabbed)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillEllipse (vinyl);
    }
}

void PlatterView::mouseDown (const juce::MouseEvent& e)
{
    if (auto* p = getParentComponent()) p->grabKeyboardFocus();
    auto& deck = processor.getDeck();
    deck.cut = e.mods.isShiftDown();
    if (e.position.getDistanceFrom (centre()) > radius() || deck.getRecord() == nullptr)
        return;
    grabbed = true;
    turned = 0.0;
    lastAngle = std::atan2 (e.position.y - centre().y, e.position.x - centre().x);
    deck.handOffset = 0.0;
    deck.touched = true;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void PlatterView::mouseDrag (const juce::MouseEvent& e)
{
    auto& deck = processor.getDeck();
    deck.cut = e.mods.isShiftDown();
    if (! grabbed) return;
    // the hand turns the record: the angle moved around the spindle = that much of the groove
    const double a = std::atan2 (e.position.y - centre().y, e.position.x - centre().x);
    double d = a - lastAngle;
    while (d > juce::MathConstants<double>::pi) d -= juce::MathConstants<double>::twoPi;
    while (d < -juce::MathConstants<double>::pi) d += juce::MathConstants<double>::twoPi;
    lastAngle = a;
    turned += d;
    deck.handOffset = turned / juce::MathConstants<double>::twoPi * deck.samplesPerTurn();
}

void PlatterView::mouseUp (const juce::MouseEvent& e)
{
    auto& deck = processor.getDeck();
    deck.cut = e.mods.isShiftDown();
    if (! grabbed) return;
    grabbed = false;
    deck.touched = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

//==============================================================================
// the waveform under the needle

void DeckWaveView::updatePeaks()
{
    auto rec = processor.getDeck().getRecord();
    if (rec.get() == peaksFor) return;
    peaksFor = rec.get();
    fine.clear();
    overview.clear();
    if (rec == nullptr) return;
    const auto& b = rec->buffer;
    const int n = b.getNumSamples();
    fine.resize ((size_t) (n / fineBin + 1), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        auto& f = fine[(size_t) (i / fineBin)];
        f = juce::jmax (f, std::abs (b.getSample (0, i)), std::abs (b.getSample (juce::jmin (1, b.getNumChannels() - 1), i)));
    }
    overview.assign (512, 0.0f);
    for (size_t k = 0; k < fine.size(); ++k)
    {
        auto& o = overview[k * overview.size() / fine.size()];
        o = juce::jmax (o, fine[k]);
    }
}

void DeckWaveView::paint (juce::Graphics& g)
{
    updatePeaks();
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0d0d0e));
    g.fillRoundedRectangle (b, 5.0f);
    auto& deck = processor.getDeck();
    auto rec = deck.getRecord();
    if (rec == nullptr || fine.empty())
    {
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Drag a sample from the BROWSER onto the record", b, juce::Justification::centred);
        return;
    }
    const int len = rec->buffer.getNumSamples();
    const double pos = deck.position.load();
    const double wrapped = pos - std::floor (pos / len) * len;
    const bool looping = deck.loop.load();

    // the whole sample on top, with the needle and the cue points
    auto top = b.removeFromTop ((float) overviewHeight).reduced (4.0f, 2.0f);
    {
        const float mid = top.getCentreY();
        g.setColour (colours::dim.withAlpha (0.7f));
        for (int x = 0; x < (int) top.getWidth(); ++x)
        {
            const float v = overview[(size_t) x * overview.size() / (size_t) juce::jmax (1, (int) top.getWidth())] * top.getHeight() * 0.5f;
            g.drawVerticalLine ((int) top.getX() + x, mid - v, mid + v);
        }
        for (int i = 0; i < Turntable::numCues; ++i)
            if (const double cp = deck.cues[(size_t) i].load(); cp >= 0.0)
            {
                const float x = top.getX() + (float) (cp / len) * top.getWidth();
                g.setColour (colours::accent2);
                g.fillRect (x - 1.0f, top.getY(), 2.0f, top.getHeight());
            }
        g.setColour (juce::Colours::white);
        g.fillRect (top.getX() + (float) (wrapped / len) * top.getWidth() - 1.0f, top.getY() - 2.0f, 2.0f, top.getHeight() + 4.0f);
    }

    // zoomed: 4 seconds around the needle, the record runs to the left like Serato / Traktor
    auto z = b.reduced (4.0f, 4.0f);
    const double span = 4.0 * rec->sampleRate;
    const float mid = z.getCentreY();
    g.setColour (colours::accent2.withAlpha (0.9f));
    for (int x = 0; x < (int) z.getWidth(); ++x)
    {
        const double s = wrapped + ((double) x / z.getWidth() - 0.5) * span;
        double sp = s;
        if (looping) sp = s - std::floor (s / len) * len;
        else if (s < 0.0 || s >= len) continue;
        const float v = fine[(size_t) juce::jlimit (0, (int) fine.size() - 1, (int) (sp / fineBin))] * z.getHeight() * 0.48f;
        g.drawVerticalLine ((int) z.getX() + x, mid - v, mid + v);
    }
    for (int i = 0; i < Turntable::numCues; ++i)
        if (const double cp = deck.cues[(size_t) i].load(); cp >= 0.0)
        {
            double d = cp - wrapped;
            if (looping) { if (d > len * 0.5) d -= len; if (d < -len * 0.5) d += len; }
            const float x = z.getX() + (float) (d / span + 0.5) * z.getWidth();
            if (x < z.getX() || x > z.getRight()) continue;
            g.setColour (colours::accent2);
            g.drawVerticalLine ((int) x, z.getY(), z.getBottom());
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (x + 2.0f, z.getY(), 14.0f, 12.0f), juce::Justification::centredLeft);
        }
    g.setColour (colours::accent);
    g.fillRect (z.getCentreX() - 1.0f, z.getY(), 2.0f, z.getHeight()); // the needle
}

void DeckWaveView::mouseDown (const juce::MouseEvent& e)
{
    // a click in the whole-sample strip moves the needle there
    auto rec = processor.getDeck().getRecord();
    if (rec == nullptr || e.y > overviewHeight + 2) return;
    const float t = juce::jlimit (0.0f, 1.0f, (e.position.x - 4.0f) / juce::jmax (1.0f, (float) getWidth() - 8.0f));
    processor.getDeck().jumpRequest = (double) t * (rec->buffer.getNumSamples() - 1);
}

//==============================================================================
// the panel

DeckPanel::DeckPanel (RezalicaProcessor& p) : processor (p), platter (p), wave (p)
{
    setWantsKeyboardFocus (true);
    auto& deck = processor.getDeck();
    addAndMakeVisible (platter);
    addAndMakeVisible (wave);
    platter.onLoaded = [this] { syncControls(); };

    title.setText ("DECK", juce::dontSendNotification);
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (title);

    startButton.onClick = [this] { startStop(); };
    startButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff6f7276));
    startButton.setColour (juce::TextButton::textColourOffId, colours::text);
    for (auto* b : { &rpm33, &rpm45 })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4501);
    }
    rpm33.onClick = [this] { processor.getDeck().rpm45 = false; };
    rpm45.onClick = [this] { processor.getDeck().rpm45 = true; };

    syncButton.setClickingTogglesState (true);
    syncButton.onClick = [this] { processor.getDeck().sync = syncButton.getToggleState(); };
    syncButton.setTooltip ("The record turns at the sequencer tempo (needs the sample's BPM). START then starts on the next beat while the sequencer plays.");
    loopButton.setClickingTogglesState (true);
    loopButton.onClick = [this] { processor.getDeck().loop = loopButton.getToggleState(); };
    loopButton.setTooltip ("The end of the sample joins its start (an endless groove)");
    hamsterButton.setClickingTogglesState (true);
    hamsterButton.onClick = [this] { processor.getDeck().reverseFader = hamsterButton.getToggleState(); };
    hamsterButton.setTooltip ("Hamster: the fader works the other way round (SHIFT / CUT opens the sound)");

    cutButton.setTooltip ("Closes the fader while pressed. With the mouse on the record, hold SHIFT instead.");
    cutButton.onStateChange = [this] { processor.getDeck().cut = cutButton.isDown(); };

    loadPadButton.setTooltip ("Puts the selected pad's whole sample on the record");
    loadPadButton.onClick = [this]
    {
        if (processor.loadPadToDeck (processor.selectedPad.load())) syncControls();
    };

    recButton.setClickingTogglesState (true);
    recButton.setColour (juce::TextButton::buttonOnColourId, colours::ledRed);
    recButton.setTooltip ("Records what the deck plays (after the fader); stop it to put the scratch on the selected pad");
    recButton.onClick = [this]
    {
        auto& d = processor.getDeck();
        if (recButton.getToggleState()) d.startRecording(); else d.stopRecording();
    };

    for (int i = 0; i < Turntable::numCues; ++i)
    {
        auto& b = cueButtons[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        b.setColour (juce::TextButton::buttonOnColourId, colours::accent2);
        b.setTooltip ("Empty: sets a cue point where the needle is. Set: jumps there. Right-click: delete.");
        b.onClick = [this, i] { cueClicked (i, nullptr); };
        cueMice.push_back (std::make_unique<CueMouse> (*this, i));
        b.addMouseListener (cueMice.back().get(), false);
        addAndMakeVisible (b);
    }
    cueCaption.setText ("CUE", juce::dontSendNotification);

    pitchSlider.setRange (-1.0, 1.0, 0.0);
    pitchSlider.setDoubleClickReturnValue (true, 0.0);
    pitchSlider.setSliderSnapsToMousePosition (false);
    pitchSlider.setValue (-deck.pitch.load(), juce::dontSendNotification);
    pitchSlider.onValueChange = [this] { processor.getDeck().pitch = (float) -pitchSlider.getValue(); syncControls(); }; // + at the bottom, like a Technics
    pitchSlider.setTooltip ("PITCH (double-click: 0). Down = faster, like on a Technics.");
    for (auto* b : { &range8, &range16 })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4502);
    }
    range8.onClick = [this] { processor.getDeck().pitchRange = 0.08f; syncControls(); };
    range16.onClick = [this] { processor.getDeck().pitchRange = 0.16f; syncControls(); };

    volumeKnob.slider.setRange (0.0, 1.0, 0.0);
    volumeKnob.slider.setValue (deck.volume.load(), juce::dontSendNotification);
    volumeKnob.slider.onValueChange = [this] { processor.getDeck().volume = (float) volumeKnob.slider.getValue(); };
    volumeKnob.slider.setTextValueSuffix ("");
    volumeKnob.slider.setNumDecimalPlacesToDisplay (2);

    bpmCaption.setText ("SAMPLE BPM", juce::dontSendNotification);
    bpmLabel.setEditable (true);
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0d0d0e));
    bpmLabel.setColour (juce::Label::outlineColourId, colours::outline);
    bpmLabel.setTooltip ("The tempo of the sample (for SYNC). Click to type it.");
    bpmLabel.onTextChange = [this]
    {
        processor.getDeck().recordBpm = (float) juce::jlimit (0.0, 400.0, bpmLabel.getText().getDoubleValue());
        syncControls();
    };

    hint.setText ("Hold the record with the mouse and move it = scratch.  Hold SHIFT = cut (fader closed).  "
                  "Drag a sample from the BROWSER onto the record.", juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, colours::dim);
    hint.setFont (juce::FontOptions (11.0f));

    for (auto* c : std::initializer_list<juce::Component*> { &startButton, &rpm33, &rpm45, &syncButton, &loopButton, &hamsterButton, &cutButton,
                                                             &loadPadButton, &recButton, &range8, &range16, &pitchSlider, &volumeKnob,
                                                             &bpmCaption, &bpmLabel, &pitchLabel, &recLabel, &hint, &cueCaption })
        addAndMakeVisible (*c);
    for (auto* l : { &bpmCaption, &pitchLabel, &recLabel, &cueCaption })
    {
        l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, colours::text);
    }
    for (auto* b : std::initializer_list<juce::Component*> { &startButton, &rpm33, &rpm45, &syncButton, &loopButton, &hamsterButton, &cutButton,
                                                             &loadPadButton, &recButton, &range8, &range16 })
        b->setWantsKeyboardFocus (false);

    syncControls();
    startTimerHz (20);
}

DeckPanel::~DeckPanel()
{
    stopTimer();
    processor.getDeck().touched = false;
    processor.getDeck().cut = false;
}

void DeckPanel::startStop()
{
    auto& d = processor.getDeck();
    if (d.startOnBeat.load()) { d.startOnBeat = false; return; }
    if (d.motorOn.load()) { d.motorOn = false; return; }
    // SYNC while the sequencer plays: start exactly on the next beat
    if (d.sync.load() && d.recordBpm.load() > 1.0f && processor.getSequencer().isRunning())
        d.startOnBeat = true;
    else
        d.motorOn = true;
}

void DeckPanel::cueClicked (int i, const juce::MouseEvent* e)
{
    auto& d = processor.getDeck();
    auto rec = d.getRecord();
    if (rec == nullptr) return;
    auto& cue = d.cues[(size_t) i];
    if (e != nullptr && e->mods.isPopupMenu())
        cue = -1.0;                                   // right-click: delete
    else if (cue.load() < 0.0)
    {
        const int len = rec->buffer.getNumSamples();
        const double pos = d.position.load();
        cue = pos - std::floor (pos / len) * len;     // set it where the needle is
    }
    else
        d.jumpRequest = cue.load();                   // jump
    syncControls();
}

void DeckPanel::syncControls()
{
    auto& d = processor.getDeck();
    rpm33.setToggleState (! d.rpm45.load(), juce::dontSendNotification);
    rpm45.setToggleState (d.rpm45.load(), juce::dontSendNotification);
    syncButton.setToggleState (d.sync.load(), juce::dontSendNotification);
    loopButton.setToggleState (d.loop.load(), juce::dontSendNotification);
    hamsterButton.setToggleState (d.reverseFader.load(), juce::dontSendNotification);
    range8.setToggleState (d.pitchRange.load() < 0.12f, juce::dontSendNotification);
    range16.setToggleState (d.pitchRange.load() >= 0.12f, juce::dontSendNotification);
    if (std::abs (pitchSlider.getValue() + d.pitch.load()) > 1.0e-4)
        pitchSlider.setValue (-d.pitch.load(), juce::dontSendNotification);
    volumeKnob.slider.setValue (d.volume.load(), juce::dontSendNotification);
    const float bpm = d.recordBpm.load();
    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (bpm > 0.0f ? juce::String (bpm, 1) : juce::String ("?"), juce::dontSendNotification);
    for (int i = 0; i < Turntable::numCues; ++i)
        cueButtons[(size_t) i].setToggleState (d.cues[(size_t) i].load() >= 0.0, juce::dontSendNotification);
    const bool syncing = d.sync.load() && bpm > 1.0f;
    pitchSlider.setEnabled (! syncing);
    const double seqBpm = processor.getSequencer().effectiveBpm.load();
    pitchLabel.setText (syncing ? "SYNC " + juce::String ((seqBpm / bpm - 1.0) * 100.0, 1) + " %"
                                : "PITCH " + juce::String (d.pitch.load() * d.pitchRange.load() * 100.0f, 1) + " %",
                        juce::dontSendNotification);
}

void DeckPanel::timerCallback()
{
    auto& d = processor.getDeck();
    // SHIFT also when the mouse is not over the deck (the key goes to the focused component)
    if (! cutButton.isDown())
        d.cut = juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown() && isShowing() && hasKeyboardFocus (true);

    const bool running = d.motorOn.load() || d.startOnBeat.load();
    startButton.setButtonText (d.startOnBeat.load() ? "WAITING FOR BEAT" : running ? "STOP" : "START");
    startButton.setColour (juce::TextButton::buttonColourId, running ? juce::Colour (0xff3f8f4f) : juce::Colour (0xff6f7276));

    // REC finished: the take goes onto the selected pad
    if (d.recState.load() == Turntable::recDone)
    {
        const int pad = processor.selectedPad.load();
        auto file = processor.collectDeckRecording (pad);
        recButton.setToggleState (false, juce::dontSendNotification);
        lastInfo = file != juce::File() ? "Scratch saved on pad " + padLabel (pad) : juce::String ("Nothing recorded (silence)");
        infoTicks = 80;
    }
    if (d.recState.load() == Turntable::recRunning)
        recLabel.setText ("REC " + juce::String (d.getRecordedSeconds(), 1) + " s", juce::dontSendNotification);
    else if (infoTicks > 0)
    {
        --infoTicks;
        recLabel.setText (lastInfo, juce::dontSendNotification);
    }
    else
        recLabel.setText ("REC goes to pad " + padLabel (processor.selectedPad.load()), juce::dontSendNotification);
    syncControls();
}

void DeckPanel::paint (juce::Graphics& g)
{
    // dark brushed plinth
    auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2c2f), 0.0f, 0.0f, juce::Colour (0xff141416), 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 6.0f);
    juce::Random rnd (45);
    for (int y = 0; y < getHeight(); y += 2)
    {
        g.setColour (juce::Colours::white.withAlpha (rnd.nextFloat() * 0.025f));
        g.drawHorizontalLine (y, 0.0f, b.getRight());
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
    // the fader: open / closed lamp
    auto& d = processor.getDeck();
    const bool closed = d.cut.load() != d.reverseFader.load();
    auto lamp = cutButton.getBounds().toFloat().translated (0.0f, (float) cutButton.getHeight() + 4.0f).withHeight (6.0f);
    g.setColour (closed ? colours::ledRed : juce::Colour (0xff6fd36f));
    g.fillRoundedRectangle (lamp, 3.0f);
    // level
    auto meter = volumeKnob.getBounds().toFloat().removeFromRight (6.0f).translated (8.0f, 0.0f);
    g.setColour (juce::Colour (0xff0d0d0e));
    g.fillRect (meter);
    const float lv = juce::jlimit (0.0f, 1.0f, d.level.load());
    g.setColour (lv > 0.95f ? colours::ledRed : colours::accent2);
    g.fillRect (meter.withTop (meter.getBottom() - meter.getHeight() * lv));
}

void DeckPanel::resized()
{
    auto b = getLocalBounds().reduced (10);
    hint.setBounds (b.removeFromBottom (18));
    b.removeFromBottom (4);
    wave.setBounds (b.removeFromBottom (juce::jlimit (60, 110, b.getHeight() / 4)));
    b.removeFromBottom (8);

    // DECK in the top left corner
    title.setBounds (b.getX(), b.getY(), 60, 22);

    // the platter in the middle, the controls around it: left = motor / tempo, right = pitch fader, cues, fader, REC
    const int colW = 210, pitchW = 70, gap = 18;
    const int side = juce::jmax (120, juce::jmin (b.getHeight(), b.getWidth() - 2 * colW - pitchW - 4 * gap));
    const int groupW = colW + gap + side + gap + pitchW + gap + colW;
    auto group = b.withSizeKeepingCentre (juce::jmin (groupW, b.getWidth()), b.getHeight());

    auto left = group.removeFromLeft (colW);
    group.removeFromLeft (gap);
    platter.setBounds (group.removeFromLeft (side).withSizeKeepingCentre (side, side));
    group.removeFromLeft (gap);
    auto pitchCol = group.removeFromLeft (pitchW);
    group.removeFromLeft (gap);
    auto right = group.removeFromLeft (colW);

    const int rowH = 30, rowGap = 8;
    auto row = [rowH, rowGap] (juce::Rectangle<int>& col) { auto r = col.removeFromTop (rowH); col.removeFromTop (rowGap); return r; };

    // left of the platter: START / STOP, speed, tempo
    left.removeFromTop (28); // (room for the DECK title)
    startButton.setBounds (row (left).withHeight (rowH + 10));
    left.removeFromTop (10);
    {
        auto r = row (left);
        rpm33.setBounds (r.removeFromLeft (r.getWidth() / 2 - 3));
        r.removeFromLeft (6);
        rpm45.setBounds (r);
    }
    {
        auto r = row (left);
        syncButton.setBounds (r.removeFromLeft (r.getWidth() / 2 - 3));
        r.removeFromLeft (6);
        loopButton.setBounds (r);
    }
    {
        auto r = row (left);
        bpmCaption.setBounds (r.removeFromLeft (90));
        bpmLabel.setBounds (r);
    }
    loadPadButton.setBounds (row (left));

    // right of the platter: the pitch fader (like on the deck), then cues, fader and REC
    pitchLabel.setBounds (pitchCol.removeFromTop (18).expanded (12, 0));
    auto ranges = pitchCol.removeFromBottom (54);
    range8.setBounds (ranges.removeFromTop (24));
    ranges.removeFromTop (6);
    range16.setBounds (ranges.removeFromTop (24));
    pitchSlider.setBounds (pitchCol.reduced (18, 4));

    cueCaption.setBounds (right.removeFromTop (16));
    for (int rr = 0; rr < 2; ++rr)
    {
        auto r = row (right);
        for (int k = 0; k < 2; ++k)
        {
            cueButtons[(size_t) (rr * 2 + k)].setBounds (r.removeFromLeft (r.getWidth() / (2 - k) - (k == 0 ? 3 : 0)));
            if (k == 0) r.removeFromLeft (6);
        }
    }
    right.removeFromTop (6);
    {
        auto r = right.removeFromTop (rowH);
        cutButton.setBounds (r.removeFromLeft (r.getWidth() / 2 - 3));
        r.removeFromLeft (6);
        hamsterButton.setBounds (r);
        right.removeFromTop (rowGap + 8); // (the open / closed lamp under CUT)
    }
    {
        // REC and the deck's volume side by side, what REC does under them
        recLabel.setBounds (right.removeFromBottom (18));
        auto r = right.removeFromTop (juce::jmin (right.getHeight(), 84));
        const int w = r.getWidth() / 2 - 3;
        recButton.setBounds (r.removeFromLeft (w).withSizeKeepingCentre (w, rowH));
        r.removeFromLeft (6);
        volumeKnob.setBounds (r.removeFromLeft (80));
    }
}

} // namespace rz
