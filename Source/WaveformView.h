#pragma once

#include "PluginProcessor.h"

namespace rz
{
/** Shows the selected pad's sample, its chop markers and the selected slice.
    - drag the orange triangles to trim the selected pad
    - Shift+click or double-click adds a chop marker (switches to Manual), drag moves it, right-click deletes
    - mouse wheel zooms horizontally around the mouse (down to single samples),
      Shift+wheel or the scroll bar scrolls */
class WaveformView : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     private juce::ScrollBar::Listener
{
public:
    explicit WaveformView (RezalicaProcessor& p);

    void setPad (int pad);
    void refresh();              // call when samples / regions changed
    void updatePlayhead();       // call from a timer
    void setVisibleRange (double start, double length) { setView (start, length); }

    std::function<void (int)> onPadSelected;
    std::function<void()> onRegionChanged;
    std::function<void (const juce::File&)> onFileDropped;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
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
    bool isChopSource() const { return sample != nullptr && sample == processor.getChopSource(); }
    bool isVisibleNorm (float n) const { return n >= viewStart && n <= viewStart + viewLength; }
    void setView (double start, double length);
    void zoomAround (float x, double factor);
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    double minViewLength() const;
    double zoomToSlider (double length) const;
    int sliceMarkerNear (float x) const;
    void showMenu (float x);

    RezalicaProcessor& processor;
    int pad = 0;
    SampleData::Ptr sample;
    Drag dragging = DragNone;
    int dragSlice = -1;
    bool dragOver = false;
    float playhead = -1.0f, previewHead = -1.0f;

    juce::Slider zoomSlider;     // horizontal zoom, 0 = whole sample, 1 = maximum
    juce::ScrollBar scrollBar { false };
    double viewStart = 0.0, viewLength = 1.0; // visible part of the sample (normalised)
};
} // namespace rz
