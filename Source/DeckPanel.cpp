#include "DeckPanel.h"
#include "BinaryData.h"

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

    // the whole sample on top, with the needle
    auto top = b.removeFromTop ((float) overviewHeight).reduced (4.0f, 2.0f);
    {
        const float mid = top.getCentreY();
        g.setColour (colours::dim.withAlpha (0.7f));
        for (int x = 0; x < (int) top.getWidth(); ++x)
        {
            const float v = overview[(size_t) x * overview.size() / (size_t) juce::jmax (1, (int) top.getWidth())] * top.getHeight() * 0.5f;
            g.drawVerticalLine ((int) top.getX() + x, mid - v, mid + v);
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

DeckPanel::DeckPanel (RezalicaProcessor& p) : processor (p), platter (p), wave (p), effects (p, deckChain)
{
    addAndMakeVisible (effects);
    setWantsKeyboardFocus (true);
    auto& deck = processor.getDeck();
    addAndMakeVisible (platter);
    addAndMakeVisible (wave);
    platter.onLoaded = [this] { syncControls(); };

    title.setVisible (false); // the title is painted (calligraphy, bigger than the look-and-feel lets a Label be)
    addChildComponent (title);

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
    syncButton.setTooltip ("The record turns at the sequencer tempo (the sample's BPM comes from its name, e.g. \"85-bpm\", or its length). "
                           "START then starts on the next beat while the sequencer plays.");
    loopButton.setClickingTogglesState (true);
    loopButton.onClick = [this] { processor.getDeck().loop = loopButton.getToggleState(); };
    loopButton.setTooltip ("The end of the sample joins its start (an endless groove)");
    deck.reverseFader = false; // (no hamster switch any more: SHIFT closes the fader)

    recButton.setClickingTogglesState (true);
    recButton.setColour (juce::TextButton::buttonOnColourId, colours::ledRed);
    recButton.setTooltip ("Records what the deck plays (with its effects); stop it to put the scratch on the selected pad");
    recButton.onClick = [this]
    {
        auto& d = processor.getDeck();
        if (recButton.getToggleState()) d.startRecording(); else d.stopRecording();
    };

    // PITCH: a round knob like VOLUME; its value in % of the record's speed (SYNC shows the tempo change)
    pitchKnob.slider.setRange (-1.0, 1.0, 0.0);
    pitchKnob.slider.getProperties().set ("bipolar", true);
    pitchKnob.slider.setDoubleClickReturnValue (true, 0.0);
    pitchKnob.slider.textFromValueFunction = [this] (double v)
    {
        auto& d = processor.getDeck();
        const float bpm = d.recordBpm.load();
        if (d.sync.load() && bpm > 1.0f)
            return "SYNC " + juce::String ((processor.getSequencer().effectiveBpm.load() / bpm - 1.0) * 100.0, 1) + " %";
        return juce::String (v * d.pitchRange.load() * 100.0, 1) + " %";
    };
    pitchKnob.slider.setValue (deck.pitch.load(), juce::dontSendNotification);
    pitchKnob.slider.onValueChange = [this] { processor.getDeck().pitch = (float) pitchKnob.slider.getValue(); };
    pitchKnob.slider.setTooltip ("PITCH: right = faster (double-click: 0). Range 8 % or 16 %.");
    for (auto* b : { &range8, &range16 })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4502);
    }
    range8.onClick = [this] { processor.getDeck().pitchRange = 0.08f; pitchKnob.slider.updateText(); };
    range16.onClick = [this] { processor.getDeck().pitchRange = 0.16f; pitchKnob.slider.updateText(); };

    volumeKnob.slider.setRange (0.0, 1.0, 0.0);
    volumeKnob.slider.setValue (deck.volume.load(), juce::dontSendNotification);
    volumeKnob.slider.onValueChange = [this] { processor.getDeck().volume = (float) volumeKnob.slider.getValue(); };
    volumeKnob.slider.setNumDecimalPlacesToDisplay (2);
    volumeKnob.slider.setDoubleClickReturnValue (true, 1.0);

    hint.setText ("Hold the record with the mouse and move it = scratch.  Hold SHIFT = cut (fader closed).  "
                  "Drag a sample from the BROWSER onto the record.", juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, colours::dim);
    hint.setFont (juce::FontOptions (11.0f));

    for (auto* c : std::initializer_list<juce::Component*> { &startButton, &rpm33, &rpm45, &syncButton, &loopButton,
                                                             &recButton, &range8, &range16, &pitchKnob, &volumeKnob, &recLabel, &hint })
        addAndMakeVisible (*c);
    recLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    recLabel.setColour (juce::Label::textColourId, colours::text);
    for (auto* b : std::initializer_list<juce::Component*> { &startButton, &rpm33, &rpm45, &syncButton, &loopButton, &recButton, &range8, &range16 })
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

void DeckPanel::syncControls()
{
    auto& d = processor.getDeck();
    rpm33.setToggleState (! d.rpm45.load(), juce::dontSendNotification);
    rpm45.setToggleState (d.rpm45.load(), juce::dontSendNotification);
    syncButton.setToggleState (d.sync.load(), juce::dontSendNotification);
    loopButton.setToggleState (d.loop.load(), juce::dontSendNotification);
    range8.setToggleState (d.pitchRange.load() < 0.12f, juce::dontSendNotification);
    range16.setToggleState (d.pitchRange.load() >= 0.12f, juce::dontSendNotification);
    if (! pitchKnob.slider.isMouseButtonDown())
        pitchKnob.slider.setValue (d.pitch.load(), juce::dontSendNotification);
    if (! volumeKnob.slider.isMouseButtonDown())
        volumeKnob.slider.setValue (d.volume.load(), juce::dontSendNotification);
    const bool syncing = d.sync.load() && d.recordBpm.load() > 1.0f;
    pitchKnob.slider.setEnabled (! syncing);
    range8.setEnabled (! syncing);
    range16.setEnabled (! syncing);
    pitchKnob.slider.updateText();
}

void DeckPanel::timerCallback()
{
    auto& d = processor.getDeck();
    // SHIFT also when the mouse is not over the deck (the key goes to the focused component)
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
    {
        // "Deck" in calligraphy (Great Vibes, SIL Open Font License), red print with a soft shadow
        static const juce::Typeface::Ptr script = juce::Typeface::createSystemTypefaceFor (BinaryData::GreatVibesRegular_ttf,
                                                                                         BinaryData::GreatVibesRegular_ttfSize);
        g.setFont (juce::Font (juce::FontOptions (script).withHeight (52.0f)));
        auto t = title.getBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawText ("Deck", t.translated (2.0f, 2.0f), juce::Justification::centredLeft);
        g.setColour (colours::accent);
        g.drawText ("Deck", t, juce::Justification::centredLeft);
    }
    // the deck's level next to its VOLUME knob
    auto& d = processor.getDeck();
    auto meter = volumeKnob.getBounds().toFloat().removeFromRight (6.0f).translated (8.0f, 0.0f).reduced (0.0f, 6.0f);
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

    // "Deck" in the top left corner
    title.setBounds (b.getX() + 4, b.getY() - 8, 170, 60);

    // EFFECTS on the far right, centred on the platter's height
    auto fx = b.removeFromRight (juce::jlimit (220, 300, b.getWidth() / 5));
    const int fxH = juce::jmin (fx.getHeight(), 4 * 34 + 44);
    effects.setBounds (fx.withSizeKeepingCentre (fx.getWidth(), fxH));
    b.removeFromRight (14);

    // the platter in the middle; left: motor and speed, right: the two knobs, the pitch range and REC
    auto area = b.withTrimmedLeft (140); // (the title's corner)
    const int colW = 180, gap = 24;
    const int side = juce::jmax (120, juce::jmin (area.getHeight(), area.getWidth() - 2 * colW - 2 * gap));
    auto group = area.withSizeKeepingCentre (juce::jmin (colW + gap + side + gap + colW, area.getWidth()), area.getHeight());

    auto left = group.removeFromLeft (colW);
    group.removeFromLeft (gap);
    platter.setBounds (group.removeFromLeft (side).withSizeKeepingCentre (side, side));
    group.removeFromLeft (gap);
    auto right = group.removeFromLeft (colW);

    const int rowH = 30, rowGap = 10;
    auto pair = [] (juce::Rectangle<int> r, juce::Component& a, juce::Component& c)
    {
        const int w = r.getWidth() / 2 - 3;
        a.setBounds (r.removeFromLeft (w));
        r.removeFromLeft (6);
        c.setBounds (r);
    };

    // left column, centred vertically: START / STOP, 33 / 45, SYNC / LOOP
    {
        auto l = left.withSizeKeepingCentre (colW, (rowH + 10) + 2 * rowH + 3 * rowGap + 10);
        startButton.setBounds (l.removeFromTop (rowH + 10));
        l.removeFromTop (rowGap + 10);
        pair (l.removeFromTop (rowH), rpm33, rpm45);
        l.removeFromTop (rowGap);
        pair (l.removeFromTop (rowH), syncButton, loopButton);
    }

    // right column, centred vertically: PITCH and VOLUME side by side, 8 % / 16 % under them, then REC
    {
        const int knobH = 96;
        auto r = right.withSizeKeepingCentre (colW, knobH + rowGap + 24 + rowGap + 10 + rowH + 20);
        auto knobs = r.removeFromTop (knobH);
        pitchKnob.setBounds (knobs.removeFromLeft (colW / 2 - 4));
        knobs.removeFromLeft (8);
        volumeKnob.setBounds (knobs.withTrimmedRight (10)); // (the level meter on its right)
        r.removeFromTop (rowGap);
        auto ranges = r.removeFromTop (24).withWidth (colW / 2 - 4);
        range8.setBounds (ranges.removeFromLeft (ranges.getWidth() / 2 - 2));
        ranges.removeFromLeft (4);
        range16.setBounds (ranges);
        r.removeFromTop (rowGap + 10);
        recButton.setBounds (r.removeFromTop (rowH));
        recLabel.setBounds (r.removeFromTop (20));
    }
}

} // namespace rz
