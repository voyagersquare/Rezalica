#include "SceneWindow.h"

namespace rz
{
namespace
{
juce::String timeString (double seconds)
{
    const int s = (int) std::round (seconds);
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

juce::Colour trackColour (int track)
{
    static const juce::uint32 c[] { 0xffff7a2f, 0xff3fc6c0, 0xffb07cff, 0xff6fd36f };
    return juce::Colour (c[juce::jlimit (0, 3, track)]);
}

void drawGutterLabel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour c)
{
    g.setColour (c.withAlpha (0.18f));
    g.fillRoundedRectangle (r.reduced (2.0f), 5.0f);
    g.setColour (c);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (text, r, juce::Justification::centred);
}
} // namespace

const std::vector<std::pair<juce::String, juce::uint32>>& patternColourChoices()
{
    static const std::vector<std::pair<juce::String, juce::uint32>> c {
        { "Orange", 0xffff7a2f }, { "Teal", 0xff3fc6c0 }, { "Yellow", 0xffffc94a }, { "Purple", 0xffb07cff },
        { "Green", 0xff6fd36f }, { "Pink", 0xffff5f8a }, { "Blue", 0xff4fa3ff }, { "Sand", 0xffd9a066 },
        { "Red", 0xffe5484d }, { "Lime", 0xffb5e550 }, { "Sky", 0xff7fd8ff }, { "Grey", 0xffa0a4ad }
    };
    return c;
}

juce::Colour patternColour (const Sequencer& seq, int pattern)
{
    if (pattern < 0 || pattern >= Sequencer::numPatterns) return colours::dim;
    if (const auto chosen = seq.patternColours[(size_t) pattern]; chosen != 0)
        return juce::Colour (chosen);
    return juce::Colour (patternColourChoices()[(size_t) (pattern % 8)].second);
}

//==============================================================================
juce::Rectangle<float> SceneGrid::cellBounds (Cell c) const
{
    const int tracks = processor.getSequencer().getNumTracks();
    const float w = (float) (getWidth() - gutter) / (float) slotsPerPage;
    const float h = (float) getHeight() / (float) tracks;
    return { (float) gutter + (float) (c.slot - firstSlot) * w, (float) c.track * h, w, h };
}

SceneGrid::Cell SceneGrid::cellAt (juce::Point<float> p) const
{
    if (! getLocalBounds().toFloat().contains (p) || p.x < (float) gutter) return {};
    const int tracks = processor.getSequencer().getNumTracks();
    const float w = (float) (getWidth() - gutter) / (float) slotsPerPage;
    Cell c;
    c.track = juce::jlimit (0, tracks - 1, (int) (p.y / ((float) getHeight() / (float) tracks)));
    c.slot = juce::jlimit (0, Sequencer::slotsPerScene - 1, firstSlot + (int) ((p.x - (float) gutter) / w));
    return c;
}

void SceneGrid::paint (juce::Graphics& g)
{
    using namespace colours;
    auto& seq = processor.getSequencer();
    const bool playing = seq.isPlayingSong();
    const int tracks = seq.getNumTracks();

    for (int t = 0; t < tracks; ++t)
    {
        const int playSlot = playing ? seq.songPosSlots[(size_t) t].load() : -1;
        auto rowArea = cellBounds ({ t, firstSlot }).withX (0.0f).withWidth ((float) gutter);
        drawGutterLabel (g, rowArea, "T" + juce::String (t + 1), trackColour (t));

        for (int sl = firstSlot; sl < firstSlot + slotsPerPage; ++sl)
        {
            auto cell = cellBounds ({ t, sl }).reduced (2.0f);
            const int p = seq.getSceneSlot (t, sl);
            const bool now = sl == playSlot;
            const bool isSource = dragging && pressed.track == t && pressed.slot == sl;

            g.setColour (p >= 0 ? patternColour (seq, p).withMultipliedBrightness (now ? 1.0f : 0.62f).withMultipliedAlpha (isSource && ! copyDrag ? 0.45f : 1.0f)
                                : ((sl / 4) % 2 == 0 ? juce::Colour (0xff2b2e35) : juce::Colour (0xff24262c)));
            g.fillRoundedRectangle (cell, 5.0f);
            if (now)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (cell, 5.0f, 2.0f);
            }

            g.setColour (p >= 0 ? juce::Colours::black.withAlpha (0.6f) : dim.withAlpha (0.6f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (sl + 1), cell.reduced (4.0f, 3.0f), juce::Justification::topLeft);

            if (p >= 0)
            {
                // pattern name, written vertically when the cell is narrow
                const auto label = seq.getPatternLabel (p);
                g.setColour (juce::Colours::black);
                g.setFont (juce::FontOptions (juce::jmin (15.0f, cell.getWidth() * 0.45f, cell.getHeight() * 0.3f), juce::Font::bold));
                if (cell.getWidth() >= 60.0f || label.length() <= 3 || cell.getHeight() < 70.0f)
                {
                    g.drawFittedText (label, cell.reduced (3.0f).toNearestInt(), juce::Justification::centred, 3);
                }
                else
                {
                    juce::Graphics::ScopedSaveState ss (g);
                    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, cell.getCentreX(), cell.getCentreY()));
                    auto r = juce::Rectangle<float> (cell.getHeight() - 20.0f, cell.getWidth()).withCentre (cell.getCentre());
                    g.drawText (label, r, juce::Justification::centred, true);
                }
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.setFont (juce::FontOptions (9.0f));
                g.drawText (juce::String (seq.length[(size_t) p].load()), cell.reduced (4.0f, 3.0f), juce::Justification::bottomRight);
            }
            else
            {
                g.setColour (dim.withAlpha (0.45f));
                g.setFont (juce::FontOptions (16.0f));
                g.drawText ("+", cell, juce::Justification::centred);
            }
        }
    }

    // drag & drop target
    if (dragging && dropTarget.valid() && dropTarget.slot >= firstSlot && dropTarget.slot < firstSlot + slotsPerPage)
    {
        auto cell = cellBounds (dropTarget).reduced (1.0f);
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (cell, 5.0f, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (copyDrag ? "COPY" : "MOVE", cell.removeFromBottom (18.0f), juce::Justification::centred);
    }
}

void SceneGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto c = cellAt (e.position);
    dragging = false;
    pressed = {};
    if (! c.valid()) return;
    auto& seq = processor.getSequencer();

    if (e.mods.isPopupMenu())
    {
        seq.setSceneSlot (c.track, c.slot, -1);
        repaint();
        return;
    }
    pressed = c;
}

void SceneGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressed.valid() || processor.getSequencer().getSceneSlot (pressed.track, pressed.slot) < 0)
        return;
    if (! dragging && e.getDistanceFromDragStart() < 6)
        return;
    dragging = true;
    copyDrag = e.mods.isCtrlDown() || e.mods.isCommandDown();
    dropTarget = cellAt (e.position);
    repaint();
}

void SceneGrid::mouseUp (const juce::MouseEvent& e)
{
    auto& seq = processor.getSequencer();
    if (dragging)
    {
        dragging = false;
        const auto to = cellAt (e.position);
        if (to.valid() && (to.track != pressed.track || to.slot != pressed.slot))
        {
            const int p = seq.getSceneSlot (pressed.track, pressed.slot);
            const int old = seq.getSceneSlot (to.track, to.slot);
            seq.setSceneSlot (to.track, to.slot, p);
            if (! (e.mods.isCtrlDown() || e.mods.isCommandDown()))
                seq.setSceneSlot (pressed.track, pressed.slot, old); // move = swap with what was there
        }
        pressed = {};
        dropTarget = {};
        repaint();
        return;
    }

    if (! pressed.valid() || e.mods.isPopupMenu())
        return;
    const auto c = pressed;
    pressed = {};

    // empty slot: choose a pattern right away; filled slot: wait a moment, a double-click renames
    if (seq.getSceneSlot (c.track, c.slot) < 0)
    {
        showPatternMenu (c);
        return;
    }
    const int token = ++clickToken;
    juce::Component::SafePointer<SceneGrid> safe (this);
    juce::Timer::callAfterDelay (juce::MouseEvent::getDoubleClickTimeout() + 20, [safe, c, token]
    {
        if (safe != nullptr && safe->clickToken == token)
            safe->showPatternMenu (c);
    });
}

void SceneGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    ++clickToken; // cancels the pending single-click menu
    pressed = {};
    const auto c = cellAt (e.position);
    if (! c.valid()) return;
    const int p = processor.getSequencer().getSceneSlot (c.track, c.slot);
    juce::Component::SafePointer<SceneGrid> safe (this);
    if (p >= 0)
        renamePattern (this, processor, p, [safe] { if (safe != nullptr) safe->repaint(); });
}

void SceneGrid::showPatternMenu (Cell c)
{
    auto& seq = processor.getSequencer();
    const int current = seq.getSceneSlot (c.track, c.slot);
    juce::PopupMenu m;
    m.addSectionHeader ("Track " + juce::String (c.track + 1) + ", slot " + juce::String (c.slot + 1));
    for (int p = 0; p < Sequencer::numPatterns; ++p)
        m.addColouredItem (p + 1, "Pattern " + juce::String (p + 1) + ":  " + seq.getPatternLabel (p) + "   (" + juce::String (seq.length[(size_t) p].load()) + " steps)",
                           patternColour (seq, p), true, current == p);
    m.addSeparator();
    if (current >= 0)
    {
        m.addItem (200, "Rename / colour...   (double-click)");
        juce::PopupMenu colourMenu;
        const auto& choices = patternColourChoices();
        for (size_t i = 0; i < choices.size(); ++i)
            colourMenu.addColouredItem (300 + (int) i, choices[i].first, juce::Colour (choices[i].second), true, seq.patternColours[(size_t) current] == choices[i].second);
        colourMenu.addItem (299, "Default colour");
        m.addSubMenu ("Colour of " + seq.getPatternLabel (current), colourMenu);
    }
    m.addItem (100, "Empty");
    juce::Component::SafePointer<SceneGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cellBounds (c).toNearestInt())),
                     [safe, c, current] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& s = safe->processor.getSequencer();
        if (r == 200) { renamePattern (safe.getComponent(), safe->processor, current, [safe] { if (safe != nullptr) safe->repaint(); }); return; }
        if (r == 299 && current >= 0) s.patternColours[(size_t) current] = 0;
        else if (r >= 300 && current >= 0) s.patternColours[(size_t) current] = patternColourChoices()[(size_t) (r - 300)].second;
        else s.setSceneSlot (c.track, c.slot, r == 100 ? -1 : r - 1);
        safe->repaint();
    });
}

void SceneGrid::renamePattern (juce::Component* owner, RezalicaProcessor& processor, int pattern, std::function<void()> done)
{
    if (pattern < 0) return;
    auto& seq = processor.getSequencer();
    auto* w = new juce::AlertWindow ("Rename pattern", "Name and colour of pattern " + juce::String (pattern + 1) + " (empty name = P" + juce::String (pattern + 1) + ")",
                                     juce::MessageBoxIconType::NoIcon, owner);
    w->addTextEditor ("name", seq.patternNames[(size_t) pattern]);
    juce::StringArray names { "Default" };
    int selected = 0;
    const auto& choices = patternColourChoices();
    for (size_t i = 0; i < choices.size(); ++i)
    {
        names.add (choices[i].first);
        if (seq.patternColours[(size_t) pattern] == choices[i].second) selected = (int) i + 1;
    }
    w->addComboBox ("colour", names, "Colour");
    if (auto* cb = w->getComboBoxComponent ("colour"))
        cb->setSelectedItemIndex (selected, juce::dontSendNotification);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::Component> safeOwner (owner);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([&processor, safeOwner, w, pattern, done] (int r)
    {
        if (safeOwner == nullptr || r != 1) return;
        auto& s = processor.getSequencer();
        s.patternNames[(size_t) pattern] = w->getTextEditorContents ("name").trim().substring (0, 24);
        if (auto* cb = w->getComboBoxComponent ("colour"))
        {
            const int idx = cb->getSelectedItemIndex();
            s.patternColours[(size_t) pattern] = idx <= 0 ? 0u : patternColourChoices()[(size_t) (idx - 1)].second;
        }
        if (done) done();
    }), true);
    if (auto* ed = w->getTextEditor ("name"))
        ed->selectAll();
}

//==============================================================================
juce::Rectangle<float> SongGrid::cellBounds (int track, int step) const
{
    const int tracks = processor.getSequencer().getNumTracks();
    const float w = (float) (getWidth() - gutter) / (float) stepsPerPage;
    const float h = (float) (getHeight() - rulerHeight) / (float) tracks;
    return { (float) gutter + (float) (step - firstStep) * w, (float) rulerHeight + (float) track * h, w, h };
}

int SongGrid::stepAtX (float x) const
{
    if (x < (float) gutter || x >= (float) getWidth()) return -1;
    const float w = (float) (getWidth() - gutter) / (float) stepsPerPage;
    return juce::jlimit (0, Sequencer::maxSongSteps - 1, firstStep + (int) ((x - (float) gutter) / w));
}

int SongGrid::trackAtY (float y) const
{
    if (y < (float) rulerHeight || y >= (float) getHeight()) return -1;
    const int tracks = processor.getSequencer().getNumTracks();
    return juce::jlimit (0, tracks - 1, (int) ((y - (float) rulerHeight) / ((float) (getHeight() - rulerHeight) / (float) tracks)));
}

void SongGrid::paint (juce::Graphics& g)
{
    using namespace colours;
    auto& seq = processor.getSequencer();
    const int playStep = seq.isPlayingSong() ? seq.songPosStep.load() : -1;
    const int songLen = seq.songLengthSteps();
    const int tracks = seq.getNumTracks();
    const int startStep = seq.songStartStep.load();

    // ruler with the start marker
    {
        auto ruler = getLocalBounds().toFloat().removeFromTop ((float) rulerHeight);
        g.setColour (juce::Colour (0xff1c1e23));
        g.fillRoundedRectangle (ruler.withTrimmedLeft ((float) gutter).reduced (2.0f, 1.0f), 4.0f);
        g.setColour (dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("START", ruler.withWidth ((float) gutter), juce::Justification::centred);
        for (int st = firstStep; st < firstStep + stepsPerPage; ++st)
        {
            auto c = cellBounds (0, st).withY (ruler.getY()).withHeight (ruler.getHeight());
            if (st == startStep)
            {
                juce::Path tri;
                tri.addTriangle (c.getX() + 4.0f, c.getY() + 3.0f, c.getX() + 18.0f, c.getCentreY(), c.getX() + 4.0f, c.getBottom() - 3.0f);
                g.setColour (juce::Colour (0xff6fd36f));
                g.fillPath (tri);
                g.drawText ("start", c.withTrimmedLeft (18.0f), juce::Justification::centredLeft);
            }
            else if (st % 4 == 0)
            {
                g.setColour (dim.withAlpha (0.7f));
                g.drawText (juce::String (st + 1), c.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
            }
        }
    }

    for (int t = 0; t < tracks; ++t)
    {
        auto gut = cellBounds (t, firstStep).withX (0.0f).withWidth ((float) gutter);
        drawGutterLabel (g, gut, "T" + juce::String (t + 1), trackColour (t));

        for (int st = firstStep; st < firstStep + stepsPerPage; ++st)
        {
            auto cell = cellBounds (t, st).reduced (2.0f);
            const bool on = seq.getSongStep (st, t) >= 0;
            const bool silentBar = st < songLen && ! seq.isSongStepActive (st);
            juce::Colour c = (st / 4) % 2 == 0 ? juce::Colour (0xff2b2e35) : juce::Colour (0xff24262c);
            if (on) c = trackColour (t).withMultipliedBrightness (st == playStep ? 1.15f : 0.8f);
            else if (st < songLen) c = c.darker (0.25f);
            g.setColour (c);
            g.fillRoundedRectangle (cell, 5.0f);
            if (st == playStep)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (cell, 5.0f, 2.0f);
            }

            g.setColour (on ? juce::Colours::black.withAlpha (0.6f) : dim.withAlpha (0.6f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (st + 1), cell.reduced (4.0f, 3.0f), juce::Justification::topLeft);
            if (on)
            {
                g.setColour (juce::Colours::black);
                g.setFont (juce::FontOptions (juce::jmin (14.0f, cell.getWidth() * 0.4f, cell.getHeight() * 0.35f), juce::Font::bold));
                g.drawFittedText (tracks > 1 ? "T" + juce::String (t + 1) : juce::String ("SCENE"), cell.reduced (2.0f).toNearestInt(), juce::Justification::centred, 1);
            }
            else if (silentBar && t == 0)
            {
                g.setColour (dim.withAlpha (0.5f));
                g.setFont (juce::FontOptions (9.0f));
                g.drawText ("rest", cell, juce::Justification::centred);
            }
        }
    }
}

void SongGrid::mouseDown (const juce::MouseEvent& e)
{
    auto& seq = processor.getSequencer();
    const int st = stepAtX (e.position.x);
    paintValue = -2;
    if (st < 0) return;

    if (e.position.y < (float) rulerHeight)
    {
        // ruler: start marker (click a step / right-click: back to the beginning)
        seq.songStartStep = e.mods.isPopupMenu() ? 0 : st;
        repaint();
        return;
    }

    paintTrack = trackAtY (e.position.y);
    if (paintTrack < 0) return;
    // click: this track plays on the step / click again (or right-click): off
    paintValue = (e.mods.isPopupMenu() || seq.getSongStep (st, paintTrack) >= 0) ? -1 : 0;
    seq.setSongStep (st, paintValue, paintTrack);
    repaint();
}

void SongGrid::mouseDrag (const juce::MouseEvent& e)
{
    const int st = stepAtX (e.position.x);
    if (st < 0 || paintValue < -1 || paintTrack < 0) return;
    processor.getSequencer().setSongStep (st, paintValue, paintTrack);
    repaint();
}

//==============================================================================
ScenePanel::ScenePanel (RezalicaProcessor& p) : processor (p), transport (p.getSequencer(), TransportBar::song), scene (p), song (p)
{
    auto toggle = [this] (juce::TextButton& b, juce::Colour on)
    {
        b.setClickingTogglesState (true);
        b.setWantsKeyboardFocus (false);
        b.setColour (juce::TextButton::buttonOnColourId, on);
        addAndMakeVisible (b);
    };
    toggle (playButton, colours::accent);
    toggle (loopButton, colours::accent2);
    stopButton.setWantsKeyboardFocus (false);
    playButton.setVisible (false);   // the transport block (PLAY / PAUSE / STOP / metronome) replaces them
    addAndMakeVisible (transport);
    transport.onStop = [this] { processor.stopAll(); };
    playButton.setTooltip ("Play the song from its start marker (space bar in this window). The sampler's PLAY plays just the selected pattern");
    loopButton.setTooltip ("Start the song again when it ends");

    playButton.onClick = [this]
    {
        auto& s = processor.getSequencer();
        if (playButton.getToggleState()) s.playSong(); // from the start marker
        else                             s.stopPlayback();
    };
    stopButton.onClick = [this] { processor.getSequencer().stopPlayback(); processor.stopAll(); };
    loopButton.onClick = [this] { processor.getSequencer().songLoop = loopButton.getToggleState(); };

    for (int i = 0; i < (int) slotPages.size(); ++i)
    {
        auto& b = slotPages[(size_t) i];
        b.setButtonText (juce::String (i * SceneGrid::slotsPerPage + 1) + "-" + juce::String ((i + 1) * SceneGrid::slotsPerPage));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (7201);
        b.setWantsKeyboardFocus (false);
        b.onClick = [this, i] { if (slotPages[(size_t) i].getToggleState()) scene.setFirstSlot (i * SceneGrid::slotsPerPage); };
        addAndMakeVisible (b);
    }
    slotPages[0].setToggleState (true, juce::dontSendNotification);
    for (int i = 0; i < (int) songPages.size(); ++i)
    {
        auto& b = songPages[(size_t) i];
        b.setButtonText (juce::String (i * SongGrid::stepsPerPage + 1) + "-" + juce::String ((i + 1) * SongGrid::stepsPerPage));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (7202);
        b.setWantsKeyboardFocus (false);
        b.onClick = [this, i] { if (songPages[(size_t) i].getToggleState()) song.setFirstStep (i * SongGrid::stepsPerPage); };
        addAndMakeVisible (b);
    }
    songPages[0].setToggleState (true, juce::dontSendNotification);

    for (int t = 1; t <= Sequencer::numTracks; ++t)
        tracksBox.addItem (t == 1 ? "1 track" : juce::String (t) + " tracks", t);
    tracksBox.setWantsKeyboardFocus (false);
    tracksBox.setTooltip ("Scene tracks: each track is its own row of patterns, all tracks play at the same time (e.g. drums + melody)");
    tracksBox.onChange = [this] { processor.getSequencer().usedTracks = tracksBox.getSelectedId(); resized(); };
    addAndMakeVisible (tracksBox);

    bpmSlider.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider.setRange (40.0, 240.0, 0.1);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.setWantsKeyboardFocus (false);
    bpmSlider.setColour (juce::Slider::trackColourId, colours::panelLight.brighter (0.15f));
    bpmSlider.onValueChange = [this] { processor.getSequencer().bpm = (float) bpmSlider.getValue(); };
    addAndMakeVisible (bpmSlider);

    for (auto* l : { &positionLabel, &lengthLabel })
    {
        l->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (*l);
    }
    positionLabel.setColour (juce::Label::textColourId, colours::accent);
    lengthLabel.setColour (juce::Label::textColourId, colours::dim);
    lengthLabel.setJustificationType (juce::Justification::centredRight);

    for (auto* l : { &sceneTitle, &songTitle })
    {
        l->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, colours::text);
        addAndMakeVisible (*l);
    }
    sceneTitle.setText ("SCENE   click: pattern   double-click: rename / colour   drag: move   Ctrl+drag: copy   right-click: empty",
                        juce::dontSendNotification);
    songTitle.setText ("SONG   click a step: the track plays the scene there   drag to fill   ruler: start marker",
                       juce::dontSendNotification);
    hintLabel.setText ("Export the song from the FILE menu (WAV or MIDI). The sampler's PLAY / space bar in the main window plays just the selected pattern.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::FontOptions (11.0f));
    hintLabel.setColour (juce::Label::textColourId, colours::dim);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (scene);
    addAndMakeVisible (song);
    songTimer.setTooltip ("Song time  |  bar . beat . 16th");
    addAndMakeVisible (songTimer);

    setWantsKeyboardFocus (true);
    setSize (1240, 660);
    timerCallback(); // show tempo, length and position right away
    startTimerHz (20);
}

ScenePanel::~ScenePanel()
{
    stopTimer();
}

void ScenePanel::timerCallback()
{
    auto& s = processor.getSequencer();
    playButton.setToggleState (s.isPlayingSong(), juce::dontSendNotification);
    loopButton.setToggleState (s.songLoop.load(), juce::dontSendNotification);
    tracksBox.setSelectedId (s.getNumTracks(), juce::dontSendNotification);
    if (lastTracks != s.getNumTracks()) { lastTracks = s.getNumTracks(); resized(); }
    if (! bpmSlider.isMouseButtonDown())
        bpmSlider.setValue (s.syncToHost.load() ? s.effectiveBpm.load() : s.bpm.load(), juce::dontSendNotification);

    const double total = s.songTotalPpq();
    const double bpm = juce::jmax (20.0f, s.effectiveBpm.load());
    lengthLabel.setText ("Song: " + juce::String (s.songLengthSteps()) + " steps  |  " + juce::String (total / 4.0, 0) + " bars",
                         juce::dontSendNotification);

    // song clock
    {
        const float pos = s.songPosPpq.load();
        const bool on = s.isPlayingSong() && pos >= 0.0f;
        const double q = on ? (double) pos : s.songStartPpq();
        const double secs = q * 60.0 / bpm;
        const int tenths = (int) std::floor (secs * 10.0);
        songTimer.time = juce::String (tenths / 600).paddedLeft ('0', 2) + ":" + juce::String ((tenths / 10) % 60).paddedLeft ('0', 2)
                       + "." + juce::String (tenths % 10);
        const int sixteenths = (int) std::floor (q * 4.0 + 1.0e-6);
        songTimer.bars = juce::String (sixteenths / 16 + 1) + "." + juce::String ((sixteenths / 4) % 4 + 1) + "." + juce::String (sixteenths % 4 + 1);
        songTimer.total = timeString (total * 60.0 / bpm) + "  (" + juce::String (total / 4.0, 0) + " bars)";
        songTimer.running = on;
        songTimer.repaint();
    }

    const int ps = s.songPosStep.load();
    if (s.isPlayingSong() && ps >= 0)
    {
        juce::String text = "Song step " + juce::String (ps + 1);
        int firstSlot = -1;
        for (int t = 0; t < s.getNumTracks(); ++t)
            if (const int sl = s.songPosSlots[(size_t) t].load(); sl >= 0)
            {
                text << "   T" << (t + 1) << ": " << s.getPatternLabel (s.getSceneSlot (t, sl));
                if (firstSlot < 0) firstSlot = sl;
            }
        if (firstSlot < 0) text << "   rest";
        positionLabel.setText (text, juce::dontSendNotification);

        // follow the playing pages
        if (firstSlot >= 0)
        {
            const int page = firstSlot / SceneGrid::slotsPerPage;
            if (page * SceneGrid::slotsPerPage != scene.getFirstSlot())
            {
                scene.setFirstSlot (page * SceneGrid::slotsPerPage);
                slotPages[(size_t) page].setToggleState (true, juce::dontSendNotification);
            }
        }
        const int sp = ps / SongGrid::stepsPerPage;
        if (sp * SongGrid::stepsPerPage != song.getFirstStep())
        {
            song.setFirstStep (sp * SongGrid::stepsPerPage);
            songPages[(size_t) sp].setToggleState (true, juce::dontSendNotification);
        }
    }
    else
    {
        positionLabel.setText (s.isPlayingPattern() ? "Playing the selected pattern (sampler PLAY)"
                               : "Stopped  -  PLAY SONG or space bar plays the song", juce::dontSendNotification);
    }

    scene.repaint();
    song.repaint();
}

bool ScenePanel::keyPressed (const juce::KeyPress& k)
{
    if (k != juce::KeyPress::spaceKey) return false;
    auto& s = processor.getSequencer();
    if (s.isPlayingSong()) s.stopPlayback(); else s.playSong(); // space in the SCENE window: the song
    return true;
}

void ScenePanel::mouseDown (const juce::MouseEvent&) { grabKeyboardFocus(); }

void ScenePanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void ScenePanel::resized()
{
    auto b = getLocalBounds().reduced (10);
    auto top = b.removeFromTop (40);
    transport.setBounds (top.removeFromLeft (transport.getIdealWidth (44)).withSizeKeepingCentre (transport.getIdealWidth (44), 38)); top.removeFromLeft (8);
    loopButton.setBounds (top.removeFromLeft (70).withSizeKeepingCentre (70, 32)); top.removeFromLeft (12);
    bpmSlider.setBounds (top.removeFromLeft (120).withSizeKeepingCentre (120, 30)); top.removeFromLeft (8);
    tracksBox.setBounds (top.removeFromLeft (100).withSizeKeepingCentre (100, 30)); top.removeFromLeft (12);
    songTimer.setBounds (top.removeFromRight (270).withSizeKeepingCentre (270, 40));
    top.removeFromRight (10);
    lengthLabel.setBounds (top.removeFromRight (200));
    positionLabel.setBounds (top);

    b.removeFromTop (10);
    hintLabel.setBounds (b.removeFromBottom (18));
    b.removeFromBottom (6);

    const int avail = b.getHeight() - 2 * 30 - 16 - SongGrid::rulerHeight;
    const int rowH = juce::jmax (110, avail / 2);

    auto sceneHead = b.removeFromTop (26);
    for (int i = (int) slotPages.size(); --i >= 0;)
    {
        slotPages[(size_t) i].setBounds (sceneHead.removeFromRight (80));
        sceneHead.removeFromRight (4);
    }
    sceneTitle.setBounds (sceneHead);
    b.removeFromTop (4);
    scene.setBounds (b.removeFromTop (rowH));

    b.removeFromTop (16);
    auto songHead = b.removeFromTop (26);
    for (int i = (int) songPages.size(); --i >= 0;)
    {
        songPages[(size_t) i].setBounds (songHead.removeFromRight (80));
        songHead.removeFromRight (4);
    }
    songTitle.setBounds (songHead);
    b.removeFromTop (4);
    song.setBounds (b.removeFromTop (rowH + SongGrid::rulerHeight));
}
} // namespace rz
