#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace rz
{
/** SCENE: one row of up to 128 pattern slots that play one after another.
    Click: choose a pattern, double-click: rename that pattern, right-click: empty. */
class SceneRow : public juce::Component
{
public:
    explicit SceneRow (RezalicaProcessor& p) : processor (p) {}
    static constexpr int slotsPerPage = 32;
    void setFirstSlot (int s) { firstSlot = s; repaint(); }
    int getFirstSlot() const { return firstSlot; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    int slotAt (juce::Point<float>) const;
    juce::Rectangle<float> slotBounds (int slot) const;
    void showPatternMenu (int slot);
    void renamePattern (int pattern);

    RezalicaProcessor& processor;
    int firstSlot = 0;
    int pendingMenuSlot = -1, clickToken = 0;
};

/** SONG: one row of 64 steps; an active step plays the scene, an empty one is a bar of silence. */
class SongRow : public juce::Component
{
public:
    explicit SongRow (RezalicaProcessor& p) : processor (p) {}
    static constexpr int stepsPerPage = 32;
    void setFirstStep (int s) { firstStep = s; repaint(); }
    int getFirstStep() const { return firstStep; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    int stepAt (juce::Point<float>) const;
    juce::Rectangle<float> stepBounds (int step) const;
    RezalicaProcessor& processor;
    int firstStep = 0;
    int paintValue = -2;
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
    juce::Slider bpmSlider;
    juce::Label positionLabel, lengthLabel, sceneTitle, songTitle, hintLabel;
    SceneRow scene;
    SongRow song;

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
        : juce::DocumentWindow ("Rezalica - Scene / Song", colours::background, juce::DocumentWindow::closeButton)
    {
        setLookAndFeel (&lnf);
        setUsingNativeTitleBar (true);
        setContentOwned (new ScenePanel (p), true);
        setResizable (true, false);
        setResizeLimits (900, 500, 2800, 1600);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
    }
    ~SceneWindow() override { setLookAndFeel (nullptr); }
    void closeButtonPressed() override { setVisible (false); if (onClose) onClose(); }
    std::function<void()> onClose;
};
} // namespace rz
