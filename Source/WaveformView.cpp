#include "WaveformView.h"
#include "LookAndFeel.h"

namespace rz
{
WaveformView::WaveformView (RezalicaProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (false);

    zoomSlider.setSliderStyle (juce::Slider::LinearVertical);
    zoomSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.setRange (1.0, 32.0, 0.01);
    zoomSlider.setSkewFactorFromMidPoint (4.0);
    zoomSlider.setDoubleClickReturnValue (true, 1.0);
    zoomSlider.setWantsKeyboardFocus (false);
    zoomSlider.setTooltip ("Vertical zoom (mouse wheel over the waveform, double-click to reset)");
    zoomSlider.setColour (juce::Slider::trackColourId, colours::accent2.withAlpha (0.6f));
    zoomSlider.onValueChange = [this] { verticalZoom = (float) zoomSlider.getValue(); repaint(); };
    addAndMakeVisible (zoomSlider);
}

void WaveformView::resized()
{
    zoomSlider.setBounds (getLocalBounds().removeFromRight (24).reduced (2, 22));
    rebuildPeaks();
}

juce::Rectangle<float> WaveformView::waveArea() const
{
    return getLocalBounds().toFloat().withTrimmedRight (24.0f).reduced (6.0f, 18.0f);
}

void WaveformView::setPad (int newPad)
{
    pad = newPad;
    refresh();
}

void WaveformView::refresh()
{
    auto s = processor.getEngine().getPadSample (pad);
    if (s == nullptr && processor.isLazyChopActive())
        s = processor.getChopSource(); // show the sample being lazy-chopped
    if (s != sample)
    {
        sample = s;
        rebuildPeaks();
    }
    repaint();
}

void WaveformView::updatePlayhead()
{
    if (sample == nullptr && processor.isLazyChopActive())
        refresh();

    const float p = processor.getEngine().playPosition[(size_t) pad].load();
    const float pv = isChopSource() ? processor.getEngine().previewPosition.load() : -1.0f;
    if (! juce::exactlyEqual (p, playhead) || ! juce::exactlyEqual (pv, previewHead))
    {
        playhead = p;
        previewHead = pv;
        repaint();
    }
}

float WaveformView::xToNorm (float x) const
{
    auto a = waveArea();
    return juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth());
}

float WaveformView::normToX (float n) const
{
    auto a = waveArea();
    return a.getX() + n * a.getWidth();
}

void WaveformView::rebuildPeaks()
{
    peaks.clear();
    if (sample == nullptr)
        return;

    const int width = juce::jmax (1, (int) waveArea().getWidth());
    const int len = sample->buffer.getNumSamples();
    const float* L = sample->buffer.getReadPointer (0);
    const float* R = sample->buffer.getReadPointer (1);
    peaks.resize ((size_t) width);

    for (int x = 0; x < width; ++x)
    {
        const int a = (int) ((juce::int64) x * len / width);
        const int b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * len / width));
        float lo = 0.0f, hi = 0.0f;
        const int stride = juce::jmax (1, (b - a) / 256);
        for (int i = a; i < juce::jmin (b, len); i += stride)
        {
            const float m = 0.5f * (L[i] + R[i]);
            lo = juce::jmin (lo, m);
            hi = juce::jmax (hi, m);
        }
        peaks[(size_t) x] = { lo, hi };
    }
}

int WaveformView::sliceMarkerNear (float x) const
{
    if (! isChopSource())
        return -1;

    int best = -1;
    float bestDist = 6.0f;
    const auto& pts = processor.getSlicePoints();
    for (int i = 0; i < (int) pts.size(); ++i)
    {
        const float d = std::abs (x - normToX (pts[(size_t) i]));
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void WaveformView::paint (juce::Graphics& g)
{
    using namespace colours;
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff101114));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (dragOver ? accent : outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, dragOver ? 2.0f : 1.0f);

    auto area = waveArea();

    if (sample == nullptr)
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText ("Drop an audio file here to chop it across the pads,\nor drop it on a single pad. Right-click a pad for more options.",
                          getLocalBounds().reduced (20), juce::Justification::centred, 3);
        return;
    }

    auto& ps = processor.getEngine().getPad (pad);
    const bool padUsesSample = processor.getEngine().getPadSample (pad) == sample;
    const float st = ps.start.load(), en = ps.end.load();

    // selected region
    if (padUsesSample)
    {
        g.setColour (accent.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (normToX (st), area.getY(), normToX (en), area.getBottom()));
    }

    // centre line + waveform (vertically zoomed, clipped to the area)
    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.5f;
    g.setColour (outline);
    g.drawHorizontalLine ((int) mid, area.getX(), area.getRight());

    for (size_t x = 0; x < peaks.size(); ++x)
    {
        const float px = area.getX() + (float) x;
        const float n = (float) x / (float) juce::jmax ((size_t) 1, peaks.size());
        const float top = juce::jmax (area.getY(), mid - peaks[x].second * half * verticalZoom);
        const float bottom = juce::jmin (area.getBottom(), mid - peaks[x].first * half * verticalZoom + 1.0f);
        g.setColour (padUsesSample && n >= st && n <= en ? accent2 : dim.withAlpha (0.6f));
        g.drawVerticalLine ((int) px, top, bottom);
    }

    // chop markers (there can be more of them than there are pads in a bank)
    if (isChopSource())
    {
        const auto& pts = processor.getSlicePoints();
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        for (int i = 0; i < (int) pts.size(); ++i)
        {
            const float x = normToX (pts[(size_t) i]);
            g.setColour ((i == dragSlice ? text : accent2).withAlpha (0.75f));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            juce::Path handle;
            handle.addTriangle (x - 5.0f, area.getBottom(), x + 5.0f, area.getBottom(), x, area.getBottom() - 7.0f);
            g.fillPath (handle);
            g.setColour (text.withAlpha (0.55f));
            g.drawText (padLabel (i), juce::Rectangle<float> (x + 2.0f, bounds.getY() + 2.0f, 28.0f, 14.0f), juce::Justification::left);
        }
    }
    else
    {
        // pads that share this sample without being part of the chop
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        for (int i = 0; i < numPads; ++i)
        {
            if (i == pad || processor.getEngine().getPadSample (i) != sample)
                continue;
            const float x = normToX (processor.getEngine().getPad (i).start.load());
            g.setColour (text.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            g.drawText (padLabel (i), juce::Rectangle<float> (x + 2.0f, bounds.getY() + 2.0f, 28.0f, 14.0f), juce::Justification::left);
        }
    }

    // start / end handles of the selected pad
    if (padUsesSample)
    {
        for (int m = 0; m < 2; ++m)
        {
            const float x = normToX (m == 0 ? st : en);
            g.setColour (accent);
            g.fillRect (x - 1.0f, area.getY(), 2.0f, area.getHeight());
            juce::Path tri;
            if (m == 0) tri.addTriangle (x, area.getY(), x + 9.0f, area.getY(), x, area.getY() + 9.0f);
            else        tri.addTriangle (x, area.getY(), x - 9.0f, area.getY(), x, area.getY() + 9.0f);
            g.fillPath (tri);
        }
    }

    // playheads
    if (playhead >= 0.0f && padUsesSample)
    {
        g.setColour (juce::Colours::white);
        g.drawVerticalLine ((int) normToX (playhead), area.getY(), area.getBottom());
    }
    if (previewHead >= 0.0f)
    {
        g.setColour (juce::Colour (0xffe0413a));
        g.fillRect (normToX (previewHead) - 1.0f, area.getY(), 2.0f, area.getHeight());
    }

    // header / footer text
    g.setColour (dim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("ZOOM", zoomSlider.getBounds().toFloat().withY (bounds.getY() + 3.0f).withHeight (14.0f).expanded (6.0f, 0.0f), juce::Justification::centred);
    g.setFont (juce::FontOptions (11.0f));
    if (verticalZoom > 1.01f)
        g.drawText ("zoom x" + juce::String (verticalZoom, 1), juce::Rectangle<float> (area.getRight() - 80.0f, bounds.getY() + 2.0f, 80.0f, 14.0f), juce::Justification::right);

    const double secs = sample->buffer.getNumSamples() / sample->sampleRate;
    const auto footer = juce::Rectangle<float> (area.getX(), area.getBottom() + 1.0f, area.getWidth(), 16.0f);
    g.drawText (sample->name + "   " + juce::String (secs, 2) + " s   " + juce::String ((int) sample->sampleRate) + " Hz",
                footer, juce::Justification::left);

    if (processor.isLazyChopActive())
    {
        g.setColour (juce::Colour (0xffe0413a));
        g.drawText ("LAZY CHOP: hit pads / keys / MIDI to drop slices", footer, juce::Justification::centred);
    }
    else if (isChopSource())
    {
        g.drawText ("double-click: add marker   right-click: delete   drag: move", footer, juce::Justification::centred);
    }

    g.setColour (dim);
    if (padUsesSample)
        g.drawText ("Pad " + padLabel (pad) + "  " + juce::String (st * secs, 3) + " - " + juce::String (en * secs, 3) + " s",
                    footer, juce::Justification::right);
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    if (sample == nullptr) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    auto& ps = processor.getEngine().getPad (pad);
    const float x = (float) e.x;
    const bool nearMarker = sliceMarkerNear (x) >= 0
                         || std::abs (x - normToX (ps.start.load())) < 6.0f
                         || std::abs (x - normToX (ps.end.load())) < 6.0f;
    setMouseCursor (nearMarker ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveformView::showMenu (float x)
{
    const int marker = sliceMarkerNear (x);
    const float n = xToNorm (x);

    juce::PopupMenu m;
    m.addItem (1, "Add chop marker here");
    m.addItem (2, "Delete this chop marker", marker >= 0 && processor.getNumSlices() > 1);
    m.addItem (3, "Clear all chop markers", isChopSource() && processor.getNumSlices() > 1);
    m.addSeparator();
    m.addItem (4, "Reset vertical zoom", verticalZoom > 1.01f);

    juce::Component::SafePointer<WaveformView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, marker, n] (int r)
    {
        if (safe == nullptr) return;
        auto& proc = safe->processor;
        if (r == 1) proc.addSlicePoint (n);
        if (r == 2) proc.removeSlicePoint (marker);
        if (r == 3) proc.clearSlicePoints();
        if (r == 4) safe->zoomSlider.setValue (1.0);
        if (safe->onRegionChanged) safe->onRegionChanged();
    });
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (sample == nullptr)
        return;

    const float x = (float) e.x;

    if (e.mods.isPopupMenu())
    {
        showMenu (x);
        return;
    }

    // chop markers win over the pad's own start/end handles
    if (const int m = sliceMarkerNear (x); m >= 0)
    {
        dragging = DragSlice;
        dragSlice = m;
        repaint();
        return;
    }

    auto& ps = processor.getEngine().getPad (pad);
    const float ds = std::abs (x - normToX (ps.start.load()));
    const float de = std::abs (x - normToX (ps.end.load()));

    if (ds < 8.0f || de < 8.0f)
    {
        dragging = ds <= de ? DragStart : DragEnd;
        return;
    }

    // Shift/Alt-click: move the closest start/end handle to the click
    if (e.mods.isShiftDown() || e.mods.isAltDown())
    {
        dragging = ds <= de ? DragStart : DragEnd;
        mouseDrag (e);
        return;
    }

    // plain click selects the slice (pad) under the mouse, if any
    const float n = xToNorm (x);
    for (int i = 0; i < numPads; ++i)
    {
        if (processor.getEngine().getPadSample (i) != sample)
            continue;
        auto& o = processor.getEngine().getPad (i);
        if (n >= o.start.load() && n < o.end.load() && i != pad)
        {
            if (onPadSelected) onPadSelected (i);
            return;
        }
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == DragNone || sample == nullptr)
        return;

    const float n = xToNorm ((float) e.x);

    if (dragging == DragSlice)
    {
        dragSlice = processor.moveSlicePoint (dragSlice, n);
        if (onRegionChanged) onRegionChanged();
        repaint();
        return;
    }

    auto& ps = processor.getEngine().getPad (pad);
    const float minGap = 64.0f / (float) juce::jmax (64, sample->buffer.getNumSamples());

    if (dragging == DragStart) ps.start = juce::jmin (n, ps.end.load() - minGap);
    else                       ps.end = juce::jmax (n, ps.start.load() + minGap);

    if (onRegionChanged) onRegionChanged();
    repaint();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (sample == nullptr || e.mods.isPopupMenu() || sliceMarkerNear ((float) e.x) >= 0)
        return;

    processor.addSlicePoint (xToNorm ((float) e.x));
    if (onRegionChanged) onRegionChanged();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const float delta = std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX;
    if (std::abs (delta) < 1.0e-4f)
        return;
    zoomSlider.setValue (juce::jlimit (1.0, 32.0, zoomSlider.getValue() * (delta > 0.0f ? 1.25 : 0.8)));
}

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() > 0 && processor.getFormatManager().findFormatForFileExtension (juce::File (files[0]).getFileExtension()) != nullptr;
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    if (onFileDropped) onFileDropped (juce::File (files[0]));
}
} // namespace rz
