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

private:
    void timerCallback() override;
    RezalicaProcessor& processor;
    juce::TextButton playButton { "PLAY SONG" }, stopButton { "STOP" }, loopButton { "LOOP" }, songModeButton { "SONG MODE" };
    juce::TextButton page1 { "SONG 1-32" }, page2 { "SONG 33-64" };
    juce::Slider bpmSlider;
    juce::Label positionLabel, lengthLabel, scenesTitle, songTitle, hintLabel;
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
