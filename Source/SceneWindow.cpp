#include "SceneWindow.h"

namespace rz
{
namespace
{
juce::Colour sceneColour (int scene)
{
    static const juce::uint32 c[] { 0xffff7a2f, 0xff3fc6c0, 0xffffc94a, 0xffb07cff, 0xff6fd36f, 0xffff5f8a, 0xff4fa3ff, 0xffd9a066 };
    return juce::Colour (c[scene % 8]);
}

juce::String timeString (double seconds)
{
    const int s = (int) std::round (seconds);
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}
} // namespace

//==============================================================================
juce::Rectangle<float> SceneList::slotBounds (int scene, int slot) const
{
    const float rowH = (float) getHeight() / (float) Sequencer::numScenes;
    const float w = (float) (getWidth() - labelWidth - infoWidth) / (float) Sequencer::slotsPerScene;
    return { (float) labelWidth + (float) slot * w, (float) scene * rowH, w, rowH };
}

void SceneList::paint (juce::Graphics& g)
{
    using namespace colours;
    auto& seq = processor.getSequencer();
    const float rowH = (float) getHeight() / (float) Sequencer::numScenes;
    const bool playing = seq.songMode.load() && seq.isRunning();
    const int playStep = seq.songPosStep.load();
    const int playScene = playStep >= 0 ? seq.getSongStep (playStep) : -1;
    const int playSlot = seq.songPosSlot.load();

    for (int sc = 0; sc < Sequencer::numScenes; ++sc)
    {
        auto label = juce::Rectangle<float> (0.0f, (float) sc * rowH, (float) labelWidth - 6.0f, rowH).reduced (1.0f);
        g.setColour (playing && sc == playScene ? sceneColour (sc).withAlpha (0.35f) : panel);
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (sceneColour (sc));
        g.fillRect (label.removeFromLeft (4.0f));
        g.setColour (text);
        g.setFont (juce::FontOptions (juce::jmin (13.0f, rowH * 0.5f), juce::Font::bold));
        g.drawText ("SCENE " + juce::String (sc + 1), label.reduced (6.0f, 0.0f), juce::Justification::centredLeft);

        for (int sl = 0; sl < Sequencer::slotsPerScene; ++sl)
        {
            auto cell = slotBounds (sc, sl).reduced (2.0f);
            const int p = seq.getSceneSlot (sc, sl);
            const bool now = playing && sc == playScene && sl == playSlot;
            g.setColour (p >= 0 ? sceneColour (sc).withMultipliedBrightness (now ? 1.0f : 0.6f) : juce::Colour (0xff24262c));
            g.fillRoundedRectangle (cell, 3.0f);
            if (now)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (cell, 3.0f, 1.5f);
            }
            g.setColour (p >= 0 ? juce::Colours::black : dim.withAlpha (0.5f));
            g.setFont (juce::FontOptions (juce::jmin (12.0f, rowH * 0.45f), juce::Font::bold));
            g.drawText (p >= 0 ? "P" + juce::String (p + 1) : juce::String ("+"), cell, juce::Justification::centred);
        }

        // length of the scene
        const int steps = seq.sceneLengthSteps (sc);
        auto info = juce::Rectangle<float> ((float) getWidth() - (float) infoWidth + 6.0f, (float) sc * rowH, (float) infoWidth - 6.0f, rowH);
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (steps / 16.0, steps % 16 == 0 ? 0 : 2) + (steps == 16 ? " bar" : " bars"), info, juce::Justification::centredLeft);
    }
}

void SceneList::mouseDown (const juce::MouseEvent& e)
{
    auto& seq = processor.getSequencer();
    for (int sc = 0; sc < Sequencer::numScenes; ++sc)
        for (int sl = 0; sl < Sequencer::slotsPerScene; ++sl)
            if (slotBounds (sc, sl).contains (e.position))
            {
                if (e.mods.isPopupMenu())
                {
                    seq.setSceneSlot (sc, sl, -1);
                    repaint();
                    return;
                }
                juce::PopupMenu m;
                m.addSectionHeader ("SCENE " + juce::String (sc + 1) + ", slot " + juce::String (sl + 1));
                for (int p = 0; p < Sequencer::numPatterns; ++p)
                    m.addItem (p + 1, "Pattern " + juce::String (p + 1) + "   (" + juce::String (seq.length[(size_t) p].load()) + " steps)",
                               true, seq.getSceneSlot (sc, sl) == p);
                m.addSeparator();
                m.addItem (100, "Empty");
                juce::Component::SafePointer<SceneList> safe (this);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (slotBounds (sc, sl).toNearestInt())),
                                 [safe, sc, sl] (int r)
                {
                    if (safe == nullptr || r == 0) return;
                    safe->processor.getSequencer().setSceneSlot (sc, sl, r == 100 ? -1 : r - 1);
                    safe->repaint();
                });
                return;
            }
}

//==============================================================================
juce::Rectangle<float> SongGrid::cellBounds (int scene, int step) const
{
    const float w = (float) (getWidth() - labelWidth) / (float) stepsPerPage;
    const float h = (float) (getHeight() - headerHeight) / (float) Sequencer::numScenes;
    return { (float) labelWidth + (float) (step - firstStep) * w, (float) headerHeight + (float) scene * h, w, h };
}

bool SongGrid::cellAt (juce::Point<int> pos, int& scene, int& step) const
{
    if (pos.x < labelWidth || pos.y < headerHeight)
        return false;
    scene = juce::jlimit (0, Sequencer::numScenes - 1, (pos.y - headerHeight) * Sequencer::numScenes / juce::jmax (1, getHeight() - headerHeight));
    step = juce::jlimit (0, Sequencer::maxSongSteps - 1, firstStep + (pos.x - labelWidth) * stepsPerPage / juce::jmax (1, getWidth() - labelWidth));
    return true;
}

void SongGrid::paint (juce::Graphics& g)
{
    using namespace colours;
    auto& seq = processor.getSequencer();
    const int playStep = seq.songMode.load() && seq.isRunning() ? seq.songPosStep.load() : -1;
    const int songLen = seq.songLengthSteps();

    // step numbers
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (int st = firstStep; st < firstStep + stepsPerPage; ++st)
    {
        auto c = cellBounds (0, st).withY (0.0f).withHeight ((float) headerHeight);
        g.setColour (st == playStep ? text : st < songLen ? dim : dim.withAlpha (0.4f));
        g.drawText (juce::String (st + 1), c, juce::Justification::centred);
    }

    for (int sc = 0; sc < Sequencer::numScenes; ++sc)
    {
        auto lab = juce::Rectangle<float> (0.0f, cellBounds (sc, firstStep).getY(), (float) labelWidth - 6.0f, cellBounds (sc, firstStep).getHeight()).reduced (1.0f);
        g.setColour (panel);
        g.fillRoundedRectangle (lab, 3.0f);
        g.setColour (sceneColour (sc));
        g.fillRect (lab.removeFromLeft (4.0f));
        g.setColour (text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("SCENE " + juce::String (sc + 1), lab.reduced (6.0f, 0.0f), juce::Justification::centredLeft);

        for (int st = firstStep; st < firstStep + stepsPerPage; ++st)
        {
            auto cell = cellBounds (sc, st).reduced (1.5f);
            const bool on = seq.getSongStep (st) == sc;
            juce::Colour c = (st / 4) % 2 == 0 ? juce::Colour (0xff2b2e35) : juce::Colour (0xff24262c);
            if (st == playStep) c = c.brighter (0.25f);
            if (on) c = sceneColour (sc);
            g.setColour (c);
            g.fillRoundedRectangle (cell, 3.0f);
            if (on && cell.getWidth() > 14.0f)
            {
                g.setColour (juce::Colours::black);
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (juce::String (sc + 1), cell, juce::Justification::centred);
            }
        }
    }

    if (playStep >= firstStep && playStep < firstStep + stepsPerPage)
    {
        auto col = cellBounds (0, playStep);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawRect (col.withHeight ((float) getHeight() - (float) headerHeight), 1.5f);
    }
}

void SongGrid::mouseDown (const juce::MouseEvent& e)
{
    int sc, st;
    if (! cellAt (e.getPosition(), sc, st))
        return;
    auto& seq = processor.getSequencer();
    // one scene per song step: click sets it, clicking it again (or right-click) empties the step
    paintScene = (e.mods.isPopupMenu() || seq.getSongStep (st) == sc) ? -1 : sc;
    seq.setSongStep (st, paintScene);
    repaint();
}

void SongGrid::mouseDrag (const juce::MouseEvent& e)
{
    int sc, st;
    if (paintScene < -1 || ! cellAt (e.getPosition(), sc, st))
        return;
    auto& seq = processor.getSequencer();
    if (paintScene == -1) { if (seq.getSongStep (st) == sc) seq.setSongStep (st, -1); }
    else                  seq.setSongStep (st, paintScene);
    repaint();
}

//==============================================================================
ScenePanel::ScenePanel (RezalicaProcessor& p) : processor (p), scenes (p), song (p)
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

    for (auto* b : { &page1, &page2 })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (7101);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    page1.setToggleState (true, juce::dontSendNotification);
    page1.onClick = [this] { song.setFirstStep (0); };
    page2.onClick = [this] { song.setFirstStep (SongGrid::stepsPerPage); };

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

    for (auto* l : { &scenesTitle, &songTitle })
    {
        l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, colours::dim);
        addAndMakeVisible (*l);
    }
    scenesTitle.setText ("SCENES  -  each scene plays its patterns one after another (click a slot to choose a pattern, right-click: empty)",
                         juce::dontSendNotification);
    songTitle.setText ("SONG  -  click a cell to put that scene on that song step (click again: remove), drag to fill",
                       juce::dontSendNotification);
    hintLabel.setText ("Export the song from the FILE menu (WAV or MIDI). Empty song steps / scenes play one bar of silence.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::FontOptions (11.0f));
    hintLabel.setColour (juce::Label::textColourId, colours::dim);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (scenes);
    addAndMakeVisible (song);
    songTimer.setTooltip ("Song time  |  bar . beat . 16th");
    addAndMakeVisible (songTimer);
    setSize (1180, 700);
    setWantsKeyboardFocus (true);
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

    // song length and position
    std::array<Sequencer::SongSegment, Sequencer::maxSegments> segs;
    double total = 0.0;
    s.buildSong (segs, total);
    const double bpm = juce::jmax (20.0f, s.effectiveBpm.load());
    const int steps = s.songLengthSteps();
    lengthLabel.setText ("Song: " + juce::String (steps) + " steps  |  " + juce::String (total / 4.0, 0) + " bars  |  "
                         + timeString (total * 60.0 / bpm), juce::dontSendNotification);

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
    if (s.songMode.load() && s.isRunning() && ps >= 0)
    {
        const int sc = s.getSongStep (ps);
        const int sl = s.songPosSlot.load();
        positionLabel.setText ("Song step " + juce::String (ps + 1) + "  >  Scene " + (sc >= 0 ? juce::String (sc + 1) : juce::String ("-"))
                                   + (sl >= 0 && sc >= 0 ? "  >  Pattern " + juce::String (s.getSceneSlot (sc, sl) + 1) : juce::String()),
                               juce::dontSendNotification);
        // follow the playing page
        const int page = ps / SongGrid::stepsPerPage;
        if (page * SongGrid::stepsPerPage != song.getFirstStep())
        {
            song.setFirstStep (page * SongGrid::stepsPerPage);
            (page == 0 ? page1 : page2).setToggleState (true, juce::dontSendNotification);
        }
    }
    else
    {
        positionLabel.setText (s.isPlayingPattern() ? "Playing the selected pattern (sampler PLAY)"
                               : "Stopped  -  PLAY SONG or space bar plays the song", juce::dontSendNotification);
    }

    scenes.repaint();
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
    playButton.setBounds (top.removeFromLeft (110)); top.removeFromLeft (6);
    stopButton.setBounds (top.removeFromLeft (70)); top.removeFromLeft (6);
    loopButton.setBounds (top.removeFromLeft (70)); top.removeFromLeft (6);
    top.removeFromLeft (6);
    bpmSlider.setBounds (top.removeFromLeft (120)); top.removeFromLeft (12);
    songTimer.setBounds (top.removeFromRight (270).withSizeKeepingCentre (270, 40));
    top.removeFromRight (10);
    lengthLabel.setBounds (top.removeFromRight (300));
    positionLabel.setBounds (top);

    b.removeFromTop (8);
    scenesTitle.setBounds (b.removeFromTop (18));
    scenes.setBounds (b.removeFromTop (juce::jmax (180, b.getHeight() * 2 / 5)));
    b.removeFromTop (10);
    auto songHead = b.removeFromTop (24);
    page2.setBounds (songHead.removeFromRight (100)); songHead.removeFromRight (4);
    page1.setBounds (songHead.removeFromRight (100)); songHead.removeFromRight (8);
    songTitle.setBounds (songHead);
    b.removeFromTop (4);
    hintLabel.setBounds (b.removeFromBottom (18));
    song.setBounds (b);
}
} // namespace rz
