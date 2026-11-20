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
                     public juce::DragAndDropTarget,
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
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    // drag & drop from Rezalica's sample browser (description "rzfile:<path>")
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("rzfile:"); }
    void itemDragEnter (const SourceDetails&) override { fileDragEnter ({}, 0, 0); }
    void itemDragExit (const SourceDetails&) override { fileDragExit ({}); }
    void itemDropped (const SourceDetails& d) override { filesDropped (juce::StringArray (d.description.toString().fromFirstOccurrenceOf ("rzfile:", false, false)), 0, 0); }

    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    enum Drag { DragNone, DragStart, DragEnd, DragSlice, DragWarp };

    // warp markers (Ctrl + drag): the sample is stretched so the marker lands on the grid
    bool isWarpView() const { return sample != nullptr && sample->isWarped(); }
    double gridStepNorm() const;            // current grid resolution, as a fraction of the sample
    float snapToGrid (float v) const;
    int warpMarkerNear (float x) const;     // index into the shown warp markers, or -1
    const WarpSettings& shownWarp() const { return warpPreview ? warpWorking : sample->warp; }
    float displayPos (float v) const;       // stored position -> position shown during a pending warp
    void startWarpDrag (float x);
    bool beginPendingWarpMarker();          // creates the marker once the mouse really moves
    bool ensureWarped();                    // switches WARP on if needed
    juce::Rectangle<float> warpStrip() const { return waveArea().withHeight (18.0f); }
    void applyWarp();
    void drawWarpGrid (juce::Graphics&, juce::Rectangle<float> area);

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

    WarpSettings warpWorking;
    bool warpPreview = false;  // drawing warpWorking while the stretched sample is being computed
    int warpDragIndex = -1;
    bool warpPendingCreate = false, warpMoved = false, warpAwaiting = false;
    float warpPendingX = 0.0f;

    juce::Slider zoomSlider;     // horizontal zoom, 0 = whole sample, 1 = maximum
    juce::ScrollBar scrollBar { false };
    double viewStart = 0.0, viewLength = 1.0; // visible part of the sample (normalised)
};
} // namespace rz
