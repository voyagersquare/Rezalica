#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace rz
{
/** Colour of a pattern: the one chosen in the SCENE window, or the default palette. */
juce::Colour patternColour (const Sequencer& seq, int pattern);
/** Colours that can be chosen for a pattern. */
const std::vector<std::pair<juce::String, juce::uint32>>& patternColourChoices();

/** SCENE: up to 4 tracks of 128 pattern slots. The patterns of a track play one after another,
    the tracks play at the same time. Click: choose pattern, double-click: rename / colour,
    drag: move (swap), Ctrl+drag: copy, right-click: empty. */
class SceneGrid : public juce::Component
{
public:
    explicit SceneGrid (RezalicaProcessor& p) : processor (p) {}
    static constexpr int slotsPerPage = 32, gutter = 36;
    void setFirstSlot (int s) { firstSlot = s; repaint(); }
    int getFirstSlot() const { return firstSlot; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Opens the rename / colour dialog of a pattern. */
    static void renamePattern (juce::Component* owner, RezalicaProcessor& processor, int pattern, std::function<void()> done);

private:
    struct Cell { int track = -1, slot = -1; bool valid() const { return track >= 0 && slot >= 0; } };
    Cell cellAt (juce::Point<float>) const;
    juce::Rectangle<float> cellBounds (Cell) const;
    void showPatternMenu (Cell);

    RezalicaProcessor& processor;
    int firstSlot = 0;
    Cell pressed, dropTarget;
    bool dragging = false, copyDrag = false;
    int clickToken = 0;
};

/** SONG: one row per scene track, 64 steps. An active step plays that track of the scene;
    a step where every track is off is a bar of silence. The ruler sets the start marker. */
class SongGrid : public juce::Component
{
public:
    explicit SongGrid (RezalicaProcessor& p) : processor (p) {}
    static constexpr int stepsPerPage = 32, gutter = 36, rulerHeight = 22;
    void setFirstStep (int s) { firstStep = s; repaint(); }
    int getFirstStep() const { return firstStep; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    int stepAtX (float x) const;
    int trackAtY (float y) const;
    juce::Rectangle<float> cellBounds (int track, int step) const;
    RezalicaProcessor& processor;
    int firstStep = 0;
    int paintTrack = -1, paintValue = -2;
};

class ScenePanel : public juce::Component,
                   private juce::Timer
{
public:
    explicit ScenePanel (RezalicaProcessor& p);
    ~ScenePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    RezalicaProcessor& processor;
    juce::TextButton playButton { "PLAY SONG" }, stopButton { "STOP" }, loopButton { "LOOP" };
    std::array<juce::TextButton, 4> slotPages;
    std::array<juce::TextButton, 2> songPages;
    juce::ComboBox tracksBox;
    juce::Slider bpmSlider;
    juce::Label positionLabel, lengthLabel, sceneTitle, songTitle, hintLabel;
    SceneGrid scene;
    SongGrid song;
    int lastTracks = -1;

    /** Big song clock: elapsed time and bar.beat.16th. */
    struct SongTimer : public juce::Component, public juce::SettableTooltipClient
    {
        juce::String time { "00:00.0" }, bars { "1.1.1" }, total;
        bool running = false;
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff0e0f12));
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (running ? juce::Colour (0xff6fd36f) : colours::outline);
            g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
            auto left = b.reduced (10.0f, 2.0f);
            auto right = left.removeFromRight (left.getWidth() * 0.42f);
            g.setColour (running ? juce::Colour (0xff6fd36f) : colours::text);
            g.setFont (juce::FontOptions (juce::jmin (26.0f, b.getHeight() * 0.62f), juce::Font::bold));
            g.drawText (time, left, juce::Justification::centredLeft);
            g.setColour (colours::accent2);
            g.setFont (juce::FontOptions (juce::jmin (15.0f, b.getHeight() * 0.36f), juce::Font::bold));
            g.drawText ("BAR " + bars, right.removeFromTop (right.getHeight() * 0.55f), juce::Justification::centredRight);
            g.setColour (colours::dim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("of " + total, right, juce::Justification::centredRight);
        }
    } songTimer;
};

class SceneWindow : public juce::DocumentWindow
{
public:
    SceneWindow (RezalicaProcessor& p, juce::LookAndFeel& lnf)
        : juce::DocumentWindow ("Rezalica - Song", colours::background, juce::DocumentWindow::closeButton)
    {
        setLookAndFeel (&lnf);
        setUsingNativeTitleBar (true);
        setContentOwned (new ScenePanel (p), true);
        setResizable (true, false);
        setResizeLimits (900, 520, 2800, 1600);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
    }
    ~SceneWindow() override { setLookAndFeel (nullptr); }
    void closeButtonPressed() override { setVisible (false); if (onClose) onClose(); }
    std::function<void()> onClose;
};
} // namespace rz
