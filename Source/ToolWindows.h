#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace rz
{
/** A level meter bar (0..1 peak, drawn in dB). */
struct LevelMeter : public juce::Component
{
    float level = 0.0f;
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0e0f12));
        g.fillRoundedRectangle (b, 3.0f);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto bar = b.reduced (2.0f).withWidth ((b.getWidth() - 4.0f) * frac);
        g.setColour (db > -1.0f ? juce::Colour (0xffe0413a) : db > -9.0f ? juce::Colour (0xffffc94a) : juce::Colour (0xff6fd36f));
        g.fillRoundedRectangle (bar, 2.0f);
    }
};

//==============================================================================
/** SAMPLING: records the sound card input or Rezalica's own output (resample) onto the selected pad. */
class RecorderPanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit RecorderPanel (RezalicaProcessor& p);
    ~RecorderPanel() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onAudioSettings, onRecorded;

private:
    void timerCallback() override;
    void updateSourceUi();
    RezalicaProcessor& processor;
    juce::TextButton inputButton { "INPUT" }, resampleButton { "RESAMPLE" }, recButton { "REC" }, cancelButton { "CANCEL" },
                     settingsButton { "Audio Settings..." };
    juce::ToggleButton waitButton { "Start on sound" }, patternButton { "One loop of the pattern (starts it)" };
    juce::Label title, targetLabel, statusLabel, timeLabel, helpLabel;
    LevelMeter meter;
};

//==============================================================================
/** BROWSER: browse folders, preview samples and load them onto pads. */
class BrowserPanel : public juce::Component,
                     private juce::FileBrowserListener,
                     private juce::Timer
{
public:
    explicit BrowserPanel (RezalicaProcessor& p);
    ~BrowserPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragStarted = false; }

private:
    bool dragStarted = false;
    void selectionChanged() override;
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File&) override;
    void browserRootChanged (const juce::File&) override {}
    void timerCallback() override;

    void setFolder (const juce::File&);
    void fillPlaces();
    void loadToPad (const juce::File&, bool nextPad);

    RezalicaProcessor& processor;
    juce::TimeSliceThread thread { "Rezalica browser" };
    juce::WildcardFileFilter filter { "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg", "*", "Audio files" };
    juce::DirectoryContentsList contents { &filter, thread };
    juce::FileListComponent list { contents };
    juce::ComboBox places;
    juce::TextButton upButton { "UP" }, folderButton { "FOLDER..." }, loadButton { "LOAD TO PAD" }, loadNextButton { "LOAD + NEXT PAD" },
                     stopButton { "STOP" };
    juce::ToggleButton autoPreview { "Preview on click" };
    juce::Label pathLabel, targetLabel, helpLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Array<juce::File> placeFiles;
};

//==============================================================================
/** MASTER FX: EQ, compressor and limiter on Rezalica's output. */
class MasterFxPanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit MasterFxPanel (RezalicaProcessor& p);
    ~MasterFxPanel() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    struct Binding { Knob* knob; std::atomic<float>* value; };
    RezalicaProcessor& processor;
    juce::TextButton eqButton { "EQ" }, compButton { "COMPRESSOR" }, limButton { "LIMITER" }, resetButton { "RESET" };
    Knob lowKnob { "LOW" }, midKnob { "MID" }, freqKnob { "MID FREQ" }, highKnob { "HIGH" },
         thrKnob { "THRESHOLD" }, ratioKnob { "RATIO" }, attKnob { "ATTACK" }, relKnob { "RELEASE" }, makeupKnob { "MAKEUP" },
         ceilKnob { "CEILING" };
    std::vector<Binding> bindings;
    LevelMeter outMeter;
    juce::Label grLabel, outLabel;
    juce::Rectangle<int> eqArea, compArea, limArea;
};

//==============================================================================
class ToolWindow : public juce::DocumentWindow
{
public:
    ToolWindow (const juce::String& name, juce::Component* content, juce::LookAndFeel& lnf, bool canResize)
        : juce::DocumentWindow (name, colours::background, juce::DocumentWindow::closeButton)
    {
        setLookAndFeel (&lnf);
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (canResize, false);
        if (canResize)
            setResizeLimits (420, 360, 2000, 1600);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
    }
    ~ToolWindow() override { setLookAndFeel (nullptr); }
    void closeButtonPressed() override { setVisible (false); if (onClose) onClose(); }
    std::function<void()> onClose;
};
} // namespace rz
