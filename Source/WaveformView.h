#pragma once

#include "PluginProcessor.h"

namespace rz
{
/** Shows the selected pad's sample, its chop markers and the selected slice.
    - drag the orange triangles to trim the selected pad
    - drag a chop marker to move it, double-click to add one, right-click to delete
    - mouse wheel / slider on the right: vertical zoom */
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
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = DragNone; dragSlice = -1; }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    enum Drag { DragNone, DragStart, DragEnd, DragSlice };

    juce::Rectangle<float> waveArea() const;
    float xToNorm (float x) const;
    float normToX (float n) const;
    void rebuildPeaks();
    bool isChopSource() const { return sample != nullptr && sample == processor.getChopSource(); }
    int sliceMarkerNear (float x) const;
    void showMenu (float x);

    RezalicaProcessor& processor;
    int pad = 0;
    SampleData::Ptr sample;
    std::vector<std::pair<float, float>> peaks;
    Drag dragging = DragNone;
    int dragSlice = -1;
    bool dragOver = false;
    float playhead = -1.0f, previewHead = -1.0f;

    juce::Slider zoomSlider;
    float verticalZoom = 1.0f;
};
} // namespace rz
