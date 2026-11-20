#include "SceneWindow.h"

namespace rz
{
namespace
{
juce::Colour patternColour (int pattern)
{
    static const juce::uint32 c[] { 0xffff7a2f, 0xff3fc6c0, 0xffffc94a, 0xffb07cff, 0xff6fd36f, 0xffff5f8a, 0xff4fa3ff, 0xffd9a066 };
    return juce::Colour (c[(pattern % 8 + 8) % 8]);
}

juce::String timeString (double seconds)
{
    const int s = (int) std::round (seconds);
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

const juce::Colour sceneColour { 0xffff7a2f };
} // namespace

//==============================================================================
juce::Rectangle<float> SceneRow::slotBounds (int slot) const
{
    const float w = (float) getWidth() / (float) slotsPerPage;
    return { (float) (slot - firstSlot) * w, 0.0f, w, (float) getHeight() };
}

int SceneRow::slotAt (juce::Point<float> p) const
{
    if (! getLocalBounds().toFloat().contains (p)) return -1;
    return juce::jlimit (0, Sequencer::slotsPerScene - 1, firstSlot + (int) (p.x / ((float) getWidth() / (float) slotsPerPage)));
}

void SceneRow::paint (juce::Graphics& g)
{
    using namespace colours;
    auto& seq = processor.getSequencer();
    const bool playing = seq.isPlayingSong();
    const int playSlot = playing ? seq.songPosSlot.load() : -1;

    for (int sl = firstSlot; sl < firstSlot + slotsPerPage; ++sl)
    {
        auto cell = slotBounds (sl).reduced (2.0f);
        const int p = seq.getSceneSlot (0, sl);
        const bool now = sl == playSlot;

        g.setColour (p >= 0 ? patternColour (p).withMultipliedBrightness (now ? 1.0f : 0.62f)
                            : ((sl / 4) % 2 == 0 ? juce::Colour (0xff2b2e35) : juce::Colour (0xff24262c)));
        g.fillRoundedRectangle (cell, 5.0f);
        if (now)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (cell, 5.0f, 2.0f);
        }

        // slot number
        g.setColour (p >= 0 ? juce::Colours::black.withAlpha (0.6f) : dim.withAlpha (0.6f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (sl + 1), cell.reduced (4.0f, 3.0f), juce::Justification::topLeft);

        // pattern name, written vertically when the cell is narrow
        if (p >= 0)
        {
            const auto label = seq.getPatternLabel (p);
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (juce::jmin (15.0f, cell.getWidth() * 0.45f), juce::Font::bold));
            if (cell.getWidth() >= 60.0f || label.length() <= 3)
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

void SceneRow::mouseDown (const juce::MouseEvent& e)
{
    const int sl = slotAt (e.position);
    if (sl < 0) return;
    auto& seq = processor.getSequencer();

    if (e.mods.isPopupMenu())
    {
        seq.setSceneSlot (0, sl, -1);
        repaint();
        return;
    }
    pendingMenuSlot = sl;
}

void SceneRow::mouseUp (const juce::MouseEvent& e)
{
    if (pendingMenuSlot < 0 || e.mods.isPopupMenu())
        return;
    const int sl = pendingMenuSlot;
    pendingMenuSlot = -1;

    // empty slot: choose a pattern right away; filled slot: wait a moment, a double-click renames
    if (processor.getSequencer().getSceneSlot (0, sl) < 0)
    {
        showPatternMenu (sl);
        return;
    }
    const int token = ++clickToken;
    juce::Component::SafePointer<SceneRow> safe (this);
    juce::Timer::callAfterDelay (juce::MouseEvent::getDoubleClickTimeout() + 20, [safe, sl, token]
    {
        if (safe != nullptr && safe->clickToken == token)
            safe->showPatternMenu (sl);
    });
}

void SceneRow::mouseDoubleClick (const juce::MouseEvent& e)
{
    ++clickToken; // cancels the pending single-click menu
    pendingMenuSlot = -1;
    const int sl = slotAt (e.position);
    if (sl < 0) return;
    const int p = processor.getSequencer().getSceneSlot (0, sl);
    if (p >= 0)
        renamePattern (p);
}

void SceneRow::showPatternMenu (int sl)
{
    auto& seq = processor.getSequencer();
    juce::PopupMenu m;
    m.addSectionHeader ("SCENE slot " + juce::String (sl + 1));
    for (int p = 0; p < Sequencer::numPatterns; ++p)
        m.addItem (p + 1, "Pattern " + juce::String (p + 1) + ":  " + seq.getPatternLabel (p) + "   (" + juce::String (seq.length[(size_t) p].load()) + " steps)",
                   true, seq.getSceneSlot (0, sl) == p);
    m.addSeparator();
    if (seq.getSceneSlot (0, sl) >= 0)
        m.addItem (200, "Rename this pattern...   (double-click)");
    m.addItem (100, "Empty");
    juce::Component::SafePointer<SceneRow> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (slotBounds (sl).toNearestInt())),
                     [safe, sl] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& s = safe->processor.getSequencer();
        if (r == 200) { safe->renamePattern (s.getSceneSlot (0, sl)); return; }
        s.setSceneSlot (0, sl, r == 100 ? -1 : r - 1);
        safe->repaint();
    });
}

void SceneRow::renamePattern (int pattern)
{
    if (pattern < 0) return;
    auto& seq = processor.getSequencer();
    auto* w = new juce::AlertWindow ("Rename pattern", "Name for pattern " + juce::String (pattern + 1) + " (empty = P" + juce::String (pattern + 1) + ")",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", seq.patternNames[(size_t) pattern]);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<SceneRow> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, pattern] (int r)
    {
        if (safe != nullptr && r == 1)
        {
            safe->processor.getSequencer().patternNames[(size_t) pattern] = w->getTextEditorContents ("name").trim().substring (0, 24);
            safe->repaint();
        }
    }), true);
    if (auto* ed = w->getTextEditor ("name"))
        ed->selectAll();
}

//==============================================================================
juce::Rectangle<float> SongRow::stepBounds (int step) const
{
    const float w = (float) getWidth() / (float) stepsPerPage;
    return { (float) (step - firstStep) * w, 0.0f, w, (float) getHeight() };
}

int SongRow::stepAt (juce::Point<float> p) const
{
    if (! getLocalBounds().toFloat().contains (p)) return -1;
    return juce::jlimit (0, Sequencer::maxSongSteps - 1, firstStep + (int) (p.x / ((float) getWidth() / (float) stepsPerPage)));
}

void SongRow::paint (juce::Graphics& g)
{
    using namespace colours;
    auto& seq = processor.getSequencer();
    const int playStep = seq.isPlayingSong() ? seq.songPosStep.load() : -1;
    const int songLen = seq.songLengthSteps();

    for (int st = firstStep; st < firstStep + stepsPerPage; ++st)
    {
        auto cell = stepBounds (st).reduced (2.0f);
        const bool on = seq.getSongStep (st) >= 0;
        juce::Colour c = (st / 4) % 2 == 0 ? juce::Colour (0xff2b2e35) : juce::Colour (0xff24262c);
        if (on) c = sceneColour.withMultipliedBrightness (st == playStep ? 1.15f : 0.8f);
        else if (st < songLen) c = c.darker (0.25f); // a bar of silence inside the song
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
            g.setFont (juce::FontOptions (juce::jmin (14.0f, cell.getWidth() * 0.4f), juce::Font::bold));
            g.drawFittedText ("SCENE", cell.reduced (2.0f).toNearestInt(), juce::Justification::centred, 1);
        }
        else if (st < songLen)
        {
            g.setColour (dim.withAlpha (0.5f));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("rest", cell, juce::Justification::centred);
        }
    }
}

void SongRow::mouseDown (const juce::MouseEvent& e)
{
    const int st = stepAt (e.position);
    if (st < 0) return;
    auto& seq = processor.getSequencer();
    // click: the step plays the scene / click again (or right-click): empty
    paintValue = (e.mods.isPopupMenu() || seq.getSongStep (st) >= 0) ? -1 : 0;
    seq.setSongStep (st, paintValue);
    repaint();
}

void SongRow::mouseDrag (const juce::MouseEvent& e)
{
    const int st = stepAt (e.position);
    if (st < 0 || paintValue < -1) return;
    processor.getSequencer().setSongStep (st, paintValue);
    repaint();
}

//==============================================================================
ScenePanel::ScenePanel (RezalicaProcessor& p) : processor (p), scene (p), song (p)
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
    addAndMakeVisible (stopButton);
    playButton.setTooltip ("Play the song from the start (space bar in this window). The sampler's PLAY plays just the selected pattern");
    loopButton.setTooltip ("Start the song again when it ends");

    playButton.onClick = [this]
    {
        auto& s = processor.getSequencer();
        if (playButton.getToggleState()) s.playSong(); // from the beginning of the song
        else                             s.stopPlayback();
    };
    stopButton.onClick = [this] { processor.getSequencer().stopPlayback(); processor.stopAll(); };
    loopButton.onClick = [this] { processor.getSequencer().songLoop = loopButton.getToggleState(); };

    for (int i = 0; i < (int) slotPages.size(); ++i)
    {
        auto& b = slotPages[(size_t) i];
        b.setButtonText (juce::String (i * SceneRow::slotsPerPage + 1) + "-" + juce::String ((i + 1) * SceneRow::slotsPerPage));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (7201);
        b.setWantsKeyboardFocus (false);
        b.onClick = [this, i] { if (slotPages[(size_t) i].getToggleState()) scene.setFirstSlot (i * SceneRow::slotsPerPage); };
        addAndMakeVisible (b);
    }
    slotPages[0].setToggleState (true, juce::dontSendNotification);
    for (int i = 0; i < (int) songPages.size(); ++i)
    {
        auto& b = songPages[(size_t) i];
        b.setButtonText (juce::String (i * SongRow::stepsPerPage + 1) + "-" + juce::String ((i + 1) * SongRow::stepsPerPage));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (7202);
        b.setWantsKeyboardFocus (false);
        b.onClick = [this, i] { if (songPages[(size_t) i].getToggleState()) song.setFirstStep (i * SongRow::stepsPerPage); };
        addAndMakeVisible (b);
    }
    songPages[0].setToggleState (true, juce::dontSendNotification);

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
    sceneTitle.setText ("SCENE  -  patterns play one after another   (click: choose pattern, double-click: rename, right-click: empty)",
                        juce::dontSendNotification);
    songTitle.setText ("SONG  -  click a step: it plays the scene (click again: empty = a bar of silence), drag to fill",
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
    setSize (1240, 640);
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
    if (! bpmSlider.isMouseButtonDown())
        bpmSlider.setValue (s.syncToHost.load() ? s.effectiveBpm.load() : s.bpm.load(), juce::dontSendNotification);

    std::vector<Sequencer::SongSegment> segs;
    double total = 0.0;
    s.buildSong (segs, total);
    const double bpm = juce::jmax (20.0f, s.effectiveBpm.load());
    lengthLabel.setText ("Song: " + juce::String (s.songLengthSteps()) + " steps  |  " + juce::String (total / 4.0, 0) + " bars",
                         juce::dontSendNotification);

    // song clock
    {
        const float pos = s.songPosPpq.load();
        const bool on = s.isPlayingSong() && pos >= 0.0f;
        const double q = on ? (double) pos : 0.0;
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
        const int sl = s.songPosSlot.load();
        positionLabel.setText ("Song step " + juce::String (ps + 1)
                                   + (sl >= 0 ? "  >  slot " + juce::String (sl + 1) + ":  " + s.getPatternLabel (s.getSceneSlot (0, sl)) : juce::String ("  >  rest")),
                               juce::dontSendNotification);
        // follow the playing pages
        if (sl >= 0)
        {
            const int page = sl / SceneRow::slotsPerPage;
            if (page * SceneRow::slotsPerPage != scene.getFirstSlot())
            {
                scene.setFirstSlot (page * SceneRow::slotsPerPage);
                slotPages[(size_t) page].setToggleState (true, juce::dontSendNotification);
            }
        }
        const int sp = ps / SongRow::stepsPerPage;
        if (sp * SongRow::stepsPerPage != song.getFirstStep())
        {
            song.setFirstStep (sp * SongRow::stepsPerPage);
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
    playButton.setBounds (top.removeFromLeft (110).withSizeKeepingCentre (110, 32)); top.removeFromLeft (6);
    stopButton.setBounds (top.removeFromLeft (70).withSizeKeepingCentre (70, 32)); top.removeFromLeft (6);
    loopButton.setBounds (top.removeFromLeft (70).withSizeKeepingCentre (70, 32)); top.removeFromLeft (12);
    bpmSlider.setBounds (top.removeFromLeft (120).withSizeKeepingCentre (120, 30)); top.removeFromLeft (12);
    songTimer.setBounds (top.removeFromRight (270).withSizeKeepingCentre (270, 40));
    top.removeFromRight (10);
    lengthLabel.setBounds (top.removeFromRight (220));
    positionLabel.setBounds (top);

    b.removeFromTop (10);
    hintLabel.setBounds (b.removeFromBottom (18));
    b.removeFromBottom (6);

    const int rowH = juce::jmax (110, (b.getHeight() - 2 * 30 - 16) / 2);

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
    song.setBounds (b.removeFromTop (rowH));
}
} // namespace rz
