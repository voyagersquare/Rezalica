#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "LoopbackCapture.h"

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
    ~RecorderPanel() override
    {
        stopTimer();
        if (! processor.getRecorder().isBusy()) processor.stopComputerAudio();
    }
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onAudioSettings, onRecorded;

private:
    void timerCallback() override;
    void updateSourceUi();
    RezalicaProcessor& processor;
    AudioRecorder::Source currentSource() const;
    juce::TextButton computerButton { "COMPUTER" };
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
                     private juce::Timer,
                     private juce::ChangeListener
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
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showPlugins (bool);

    /** PLUGINS: the scanned effects; drag one onto a mixer slot. */
    struct PluginRows : public juce::ListBoxModel
    {
        juce::Array<juce::PluginDescription> effects;
        int getNumRows() override { return effects.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (row < 0 || row >= effects.size()) return;
            if (selected) g.fillAll (colours::accent.withAlpha (0.55f));
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (effects[row].name, 10, 0, w - 20, h, juce::Justification::centredLeft, true);
            g.setColour (colours::dim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (effects[row].manufacturerName + "  " + effects[row].pluginFormatName, 10, 0, w - 20, h, juce::Justification::centredRight, true);
        }
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
        {
            if (rows.isEmpty() || rows[0] >= effects.size()) return {};
            return "rzplugin:" + effects[rows[0]].createIdentifierString();
        }
    } pluginRows;
    juce::ListBox pluginList { "plugins", &pluginRows };
    juce::TextButton filesTab { "FILES" }, pluginsTab { "PLUGINS" }, scanButton { "SCAN" }, vstFoldersButton { "VST3 FOLDERS..." };
    juce::Label scanStatus;
    void selectionChanged() override;
    void fileClicked (const juce::File&, const juce::MouseEvent&) override;
    void showFavoritesMenu();
    void showItemMenu (const juce::File&);
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
    juce::TextButton favoritesButton { "FAVORITES" };
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
