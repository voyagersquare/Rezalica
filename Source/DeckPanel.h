#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "MixerPanel.h"

namespace rz
{
/** The platter of the DECK: drawn turning, grabbed with the mouse for scratching.
    Drop a sample from the browser (or a file) on it to load it. */
class PlatterView : public juce::Component,
                    public juce::DragAndDropTarget,
                    public juce::FileDragAndDropTarget
{
public:
    explicit PlatterView (RezalicaProcessor& p) : processor (p) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void modifierKeysChanged (const juce::ModifierKeys& m) override { processor.getDeck().cut = m.isShiftDown(); }

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("rzfile:"); }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override { load (juce::File (d.description.toString().fromFirstOccurrenceOf ("rzfile:", false, false))); }
    bool isInterestedInFileDrag (const juce::StringArray& f) override
    {
        return f.size() > 0 && processor.getFormatManager().findFormatForFileExtension (juce::File (f[0]).getFileExtension()) != nullptr;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override { load (juce::File (f[0])); }

    std::function<void()> onLoaded;

private:
    void load (const juce::File& f)
    {
        dropHover = false;
        if (processor.loadFileToDeck (f) && onLoaded) onLoaded();
        repaint();
    }
    juce::Point<float> centre() const { return getLocalBounds().toFloat().getCentre(); }
    float radius() const { return juce::jmin (getWidth(), getHeight()) * 0.5f - 6.0f; }

    RezalicaProcessor& processor;
    bool dropHover = false, grabbed = false;
    double lastAngle = 0.0, turned = 0.0;
};

/** A zoomed waveform that scrolls under the needle (centre line), with the whole sample above it. */
class DeckWaveView : public juce::Component
{
public:
    explicit DeckWaveView (RezalicaProcessor& p) : processor (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override { mouseDown (e); }

private:
    void updatePeaks();
    RezalicaProcessor& processor;
    const SampleData* peaksFor = nullptr;
    std::vector<float> fine, overview;      // peak per bin
    static constexpr int fineBin = 128;
    static constexpr int overviewHeight = 18;
};

class DeckPanel : public juce::Component,
                  private juce::Timer
{
public:
    explicit DeckPanel (RezalicaProcessor& p);
    ~DeckPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void modifierKeysChanged (const juce::ModifierKeys& m) override { processor.getDeck().cut = m.isShiftDown(); }
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    void timerCallback() override;
    void syncControls();
    void startStop();

    RezalicaProcessor& processor;
    PlatterView platter;
    DeckWaveView wave;
    juce::VBlankAttachment vblank { this, [this] { platter.repaint(); wave.repaint(); } };

    juce::TextButton startButton { "START / STOP" }, rpm33 { "33" }, rpm45 { "45" };
    juce::TextButton syncButton { "SYNC" }, loopButton { "LOOP" }, hamsterButton { "HAMSTER" };
    juce::TextButton cutButton { "CUT" }, recButton { "REC" };
    juce::TextButton range8 { "8 %" }, range16 { "16 %" };
    juce::Slider pitchSlider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    Knob volumeKnob { "VOLUME" };
    juce::Label title, bpmCaption, bpmLabel, pitchLabel, recLabel, hint;
    MixerPanel effects; // the DECK's 4 effect slots, like the mixer's (no fader)
    juce::String lastInfo;
    int infoTicks = 0;
};
} // namespace rz
