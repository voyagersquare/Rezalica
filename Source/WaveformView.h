#pragma once

#include "PluginProcessor.h"

namespace rz
{
/** Shows the selected pad's sample, all slices that share it, and lets you drag the start/end markers. */
class WaveformView : public juce::Component,
                     public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView (RezalicaProcessor& p);

    void setPad (int pad);
    void refresh();              // call when samples / regions changed
    void updatePlayhead();       // call from a timer

    std::function<void (int)> onPadSelected;
    std::function<void()> onRegionChanged;
    std::function<void (const juce::File&)> onFileDropped;

    void paint (juce::Graphics&) override;
    void resized() override { rebuildPeaks(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = DragNone; }
    void mouseMove (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    enum Drag { DragNone, DragStart, DragEnd };

    juce::Rectangle<float> waveArea() const { return getLocalBounds().toFloat().reduced (6.0f, 18.0f); }
    float xToNorm (float x) const;
    float normToX (float n) const;
    void rebuildPeaks();

    RezalicaProcessor& processor;
    int pad = 0;
    SampleData::Ptr sample;
    std::vector<std::pair<float, float>> peaks;
    Drag dragging = DragNone;
    bool dragOver = false;
    float playhead = -1.0f;
};
} // namespace rz
