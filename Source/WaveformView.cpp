#include "WaveformView.h"
#include "LookAndFeel.h"

namespace rz
{
WaveformView::WaveformView (RezalicaProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (false);

    zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.setRange (0.0, 1.0, 0.0);
    zoomSlider.setDoubleClickReturnValue (true, 0.0);
    zoomSlider.setWantsKeyboardFocus (false);
    zoomSlider.setTooltip ("Horizontal zoom (or mouse wheel over the waveform). Double-click to show the whole sample.");
    zoomSlider.setColour (juce::Slider::trackColourId, colours::accent2.withAlpha (0.6f));
    zoomSlider.onValueChange = [this]
    {
        const double len = std::pow (minViewLength(), zoomSlider.getValue());
        const double centre = viewStart + viewLength * 0.5;
        setView (centre - len * 0.5, len);
    };
    addAndMakeVisible (zoomSlider);

    scrollBar.setRangeLimits (0.0, 1.0, juce::dontSendNotification);
    scrollBar.setAutoHide (true);
    scrollBar.setWantsKeyboardFocus (false);
    scrollBar.setColour (juce::ScrollBar::thumbColourId, colours::accent2.withAlpha (0.7f));
    scrollBar.addListener (this);
    addAndMakeVisible (scrollBar);
}

void WaveformView::resized()
{
    auto b = getLocalBounds();
    zoomSlider.setBounds (b.getRight() - 150, 2, 140, 16);
    auto a = waveArea().toNearestInt();
    scrollBar.setBounds (a.getX(), getHeight() - 12, a.getWidth(), 10);
}

juce::Rectangle<float> WaveformView::waveArea() const
{
    return getLocalBounds().toFloat().reduced (6.0f, 0.0f).withTrimmedTop (20.0f).withTrimmedBottom (32.0f);
}

double WaveformView::minViewLength() const
{
    if (sample == nullptr || sample->buffer.getNumSamples() <= 16)
        return 1.0;
    return 16.0 / (double) sample->buffer.getNumSamples(); // 16 samples across the whole view
}

double WaveformView::zoomToSlider (double length) const
{
    const double minLen = minViewLength();
    if (minLen >= 1.0)
        return 0.0;
    return juce::jlimit (0.0, 1.0, std::log (length) / std::log (minLen));
}

void WaveformView::setView (double start, double length)
{
    viewLength = juce::jlimit (minViewLength(), 1.0, length);
    viewStart = juce::jlimit (0.0, 1.0 - viewLength, start);
    scrollBar.setCurrentRange (viewStart, viewLength, juce::dontSendNotification);
    zoomSlider.setValue (zoomToSlider (viewLength), juce::dontSendNotification);
    repaint();
}

void WaveformView::zoomAround (float x, double factor)
{
    const double anchor = (double) xToNorm (x);
    const double newLen = juce::jlimit (minViewLength(), 1.0, viewLength * factor);
    const double rel = (anchor - viewStart) / viewLength;
    setView (anchor - rel * newLen, newLen);
}

void WaveformView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    setView (newRangeStart, viewLength);
}

void WaveformView::setPad (int newPad)
{
    pad = newPad;
    refresh();

    // when zoomed in, bring the selected pad's start into view
    if (sample != nullptr && viewLength < 1.0 && processor.getEngine().getPadSample (pad) == sample)
    {
        const float st = processor.getEngine().getPad (pad).start.load();
        if (! isVisibleNorm (st))
            setView (st - viewLength * 0.1, viewLength);
    }
}

void WaveformView::refresh()
{
    auto s = processor.getEngine().getPadSample (pad);
    if (s == nullptr && processor.isLazyChopActive())
        s = processor.getChopSource(); // show the sample being lazy-chopped
    if (s != sample)
    {
        sample = s;
        setView (0.0, 1.0);
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
    return (float) juce::jlimit (0.0, 1.0, viewStart + (double) ((x - a.getX()) / a.getWidth()) * viewLength);
}

float WaveformView::normToX (float n) const
{
    auto a = waveArea();
    return a.getX() + (float) (((double) n - viewStart) / viewLength) * a.getWidth();
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
        g.drawFittedText ("Drop an audio file here (or use LOAD SAMPLE) to load it on the selected pad,\nthen choose a chop mode to slice it. Right-click a pad for more options.",
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

    // everything inside the wave area is clipped to it
    juce::Graphics::ScopedSaveState clipState (g);
    g.reduceClipRegion (area.toNearestInt().expanded (0, 1));

    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.5f;
    g.setColour (outline);
    g.drawHorizontalLine ((int) mid, area.getX(), area.getRight());

    {
        const int len = sample->buffer.getNumSamples();
        const float* L = sample->buffer.getReadPointer (0);
        const float* R = sample->buffer.getReadPointer (1);
        const int width = juce::jmax (1, (int) area.getWidth());
        const double samplesPerPixel = viewLength * len / width;
        auto colourFor = [&] (double n) { return padUsesSample && n >= st && n <= en ? accent2 : dim.withAlpha (0.6f); };

        if (samplesPerPixel >= 1.0)
        {
            for (int x = 0; x < width; ++x)
            {
                const double n0 = viewStart + viewLength * x / width;
                const int s0 = (int) (n0 * len);
                const int s1 = juce::jmin (len, juce::jmax (s0 + 1, (int) ((viewStart + viewLength * (x + 1) / width) * len)));
                const int stride = juce::jmax (1, (s1 - s0) / 512);
                float lo = 0.0f, hi = 0.0f;
                for (int i = s0; i < s1; i += stride)
                {
                    const float m = 0.5f * (L[i] + R[i]);
                    lo = juce::jmin (lo, m);
                    hi = juce::jmax (hi, m);
                }
                g.setColour (colourFor (n0));
                g.drawVerticalLine ((int) area.getX() + x, mid - hi * half, mid - lo * half + 1.0f);
            }
        }
        else
        {
            // zoomed in further than one sample per pixel: draw the actual sample points
            const int first = juce::jmax (0, (int) std::floor (viewStart * len) - 1);
            const int last = juce::jmin (len - 1, (int) std::ceil ((viewStart + viewLength) * len) + 1);
            juce::Path line;
            for (int i = first; i <= last; ++i)
            {
                const float px = normToX ((float) ((double) i / len));
                const float py = mid - 0.5f * (L[i] + R[i]) * half;
                if (i == first) line.startNewSubPath (px, py); else line.lineTo (px, py);
            }
            g.setColour (accent2);
            g.strokePath (line, juce::PathStrokeType (1.5f));

            if (samplesPerPixel < 0.15)
                for (int i = first; i <= last; ++i)
                {
                    const float px = normToX ((float) ((double) i / len));
                    const float py = mid - 0.5f * (L[i] + R[i]) * half;
                    g.setColour (colourFor ((double) i / len));
                    g.fillEllipse (px - 2.5f, py - 2.5f, 5.0f, 5.0f);
                }
        }
    }

    // chop markers (there can be more of them than there are pads in a bank)
    if (isChopSource())
    {
        const auto& pts = processor.getSlicePoints();
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        for (int i = 0; i < (int) pts.size(); ++i)
        {
            if (! isVisibleNorm (pts[(size_t) i]))
                continue;
            const float x = normToX (pts[(size_t) i]);
            g.setColour ((i == dragSlice ? text : accent2).withAlpha (0.75f));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            juce::Path handle;
            handle.addTriangle (x - 5.0f, area.getBottom(), x + 5.0f, area.getBottom(), x, area.getBottom() - 7.0f);
            g.fillPath (handle);
            g.setColour (text.withAlpha (0.55f));
            g.drawText (padLabel (i), juce::Rectangle<float> (x + 2.0f, area.getY() + 2.0f, 28.0f, 14.0f), juce::Justification::left);
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
            g.drawText (padLabel (i), juce::Rectangle<float> (x + 2.0f, area.getY() + 2.0f, 28.0f, 14.0f), juce::Justification::left);
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
}

void WaveformView::paintOverChildren (juce::Graphics& g)
{
    using namespace colours;
    if (sample == nullptr)
        return;

    auto bounds = getLocalBounds().toFloat();
    auto area = waveArea();
    auto& ps = processor.getEngine().getPad (pad);
    const bool padUsesSample = processor.getEngine().getPadSample (pad) == sample;
    const float st = ps.start.load(), en = ps.end.load();

    // header: zoom info
    const double secs = sample->buffer.getNumSamples() / sample->sampleRate;
    g.setColour (dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    auto header = juce::Rectangle<float> (area.getX(), bounds.getY() + 3.0f, (float) zoomSlider.getX() - area.getX() - 8.0f, 14.0f);
    if (viewLength < 0.999)
    {
        const double visibleSamples = viewLength * sample->buffer.getNumSamples();
        g.drawText ("ZOOM  " + juce::String (viewStart * secs, 4) + " - " + juce::String ((viewStart + viewLength) * secs, 4) + " s   ("
                        + juce::String (juce::roundToInt (visibleSamples)) + " samples)",
                    header, juce::Justification::right);
    }
    else
    {
        g.drawText ("ZOOM", header, juce::Justification::right);
    }

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
        g.drawText ("shift+click: add marker   double-click marker: delete   drag: move   wheel: zoom", footer, juce::Justification::centred);
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
    m.addItem (4, "Show whole sample (zoom out)", viewLength < 0.999);

    juce::Component::SafePointer<WaveformView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, marker, n] (int r)
    {
        if (safe == nullptr) return;
        auto& proc = safe->processor;
        if (r == 1) proc.addSlicePoint (n);
        if (r == 2) proc.removeSlicePoint (marker);
        if (r == 3) proc.clearSlicePoints();
        if (r == 4) safe->setView (0.0, 1.0);
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

    // Shift+click: add a manual chop marker (leaves any other chop mode)
    if (e.mods.isShiftDown())
    {
        processor.addSlicePoint (xToNorm (x));
        if (onRegionChanged) onRegionChanged();
        return;
    }

    // Alt+click: move the closest start/end handle to the click
    if (e.mods.isAltDown())
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
    if (sample == nullptr || e.mods.isPopupMenu())
        return;

    // double-click on a marker deletes it (also transient markers), elsewhere adds one
    if (const int m = sliceMarkerNear ((float) e.x); m >= 0)
    {
        dragging = DragNone;
        dragSlice = -1;
        processor.removeSlicePoint (m);
    }
    else
    {
        processor.addSlicePoint (xToNorm ((float) e.x));
    }

    if (onRegionChanged) onRegionChanged();
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (sample == nullptr)
        return;

    const bool scroll = e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY);
    const float delta = scroll ? (std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY) : wheel.deltaY;
    if (std::abs (delta) < 1.0e-4f)
        return;

    if (scroll)
        setView (viewStart - (double) delta * viewLength * 0.5, viewLength);
    else
        zoomAround ((float) e.x, delta > 0.0f ? 0.8 : 1.25); // wheel up = zoom in at the mouse
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
