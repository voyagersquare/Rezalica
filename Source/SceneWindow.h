#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace rz
{
/** SCENES: each scene is a chain of up to 8 patterns. */
class SceneList : public juce::Component
{
public:
    explicit SceneList (RezalicaProcessor& p) : processor (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    static constexpr int labelWidth = 90, infoWidth = 110;
    juce::Rectangle<float> slotBounds (int scene, int slot) const;
    RezalicaProcessor& processor;
};

/** SONG: a sequence of scenes, laid out like the step sequencer (rows = scenes, columns = song steps). */
class SongGrid : public juce::Component
{
public:
    explicit SongGrid (RezalicaProcessor& p) : processor (p) {}
    static constexpr int stepsPerPage = 32;
    void setFirstStep (int s) { firstStep = s; repaint(); }
    int getFirstStep() const { return firstStep; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    static constexpr int labelWidth = 90, headerHeight = 20;
    bool cellAt (juce::Point<int>, int& scene, int& step) const;
    juce::Rectangle<float> cellBounds (int scene, int step) const;
    RezalicaProcessor& processor;
    int firstStep = 0;
    int paintScene = -2; // what a drag paints (-1 = erase)
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
    juce::TextButton page1 { "SONG 1-32" }, page2 { "SONG 33-64" };
    juce::Slider bpmSlider;
    juce::Label positionLabel, lengthLabel, scenesTitle, songTitle, hintLabel;

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
    SceneList scenes;
    SongGrid song;
};

class SceneWindow : public juce::DocumentWindow
{
public:
    SceneWindow (RezalicaProcessor& p, juce::LookAndFeel& lnf)
        : juce::DocumentWindow ("Rezalica - Scenes / Song", colours::background, juce::DocumentWindow::closeButton)
    {
        setLookAndFeel (&lnf);
        setUsingNativeTitleBar (true);
        setContentOwned (new ScenePanel (p), true);
        setResizable (true, false);
        setResizeLimits (900, 560, 2600, 1600);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
    }
    ~SceneWindow() override { setLookAndFeel (nullptr); }
    void closeButtonPressed() override { setVisible (false); if (onClose) onClose(); }
    std::function<void()> onClose;
};
} // namespace rz
