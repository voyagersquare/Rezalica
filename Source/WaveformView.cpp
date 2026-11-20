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
        const bool sameSource = sample != nullptr && s != nullptr
                             && (s->warpSource == sample || s->warpSource == sample->warpSource || sample->warpSource == s);
        sample = s;
        warpPreview = false; // the stretched result arrived
        warpAwaiting = false;
        if (! sameSource)
            setView (0.0, 1.0); // keep the zoom while warping the same sample
        else
            setView (viewStart, viewLength);
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
        g.drawFittedText ("Drop an audio file here (from the BROWSER or your file manager) to load it on the selected pad,\nthen choose a chop mode to slice it. Right-click a pad for more options.",
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

        if (warpPreview && sample->warpSource != nullptr)
        {
            // live preview while dragging a warp marker: read the original through the new time map
            const auto& base = *sample->warpSource;
            const int blen = base.buffer.getNumSamples();
            const float* bl = base.buffer.getReadPointer (0);
            const float* br = base.buffer.getReadPointer (1);
            const double ra = sample->regionStart, rb = sample->regionEnd;
            auto basePos = [&] (double v) { return (int) ((ra + warpWorking.dstToSrc ((float) juce::jlimit (0.0, 1.0, v)) * (rb - ra)) * blen); };
            for (int x = 0; x < width; ++x)
            {
                const double n0 = viewStart + viewLength * x / width;
                const double n1 = viewStart + viewLength * (x + 1) / width;
                const int s0 = juce::jlimit (0, blen - 1, basePos (n0));
                const int s1 = juce::jlimit (s0 + 1, blen, basePos (n1));
                const int stride = juce::jmax (1, (s1 - s0) / 512);
                float lo = 0.0f, hi = 0.0f;
                for (int i = s0; i < s1; i += stride)
                {
                    const float m = 0.5f * (bl[i] + br[i]);
                    lo = juce::jmin (lo, m);
                    hi = juce::jmax (hi, m);
                }
                g.setColour (accent2.withAlpha (0.8f));
                g.drawVerticalLine ((int) area.getX() + x, mid - hi * half, mid - lo * half + 1.0f);
            }
        }
        else if (samplesPerPixel >= 1.0)
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

    // warp strip along the top: the yellow diamonds live here, dragging in it stretches the audio
    {
        auto strip = warpStrip();
        g.setColour (juce::Colour (0xff1d1e23));
        g.fillRect (strip);
        g.setColour (juce::Colour (0x55ffc94a));
        g.drawHorizontalLine ((int) strip.getBottom(), strip.getX(), strip.getRight());
        if (! isWarpView())
        {
            g.setColour (dim.withAlpha (0.6f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("WARP: drag here to stretch", strip.reduced (6.0f, 0.0f), juce::Justification::centredRight);
        }
    }

    if (isWarpView())
        drawWarpGrid (g, area);

    // chop markers (there can be more of them than there are pads in a bank)
    if (isChopSource())
    {
        const auto& pts = processor.getSlicePoints();
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        for (int i = 0; i < (int) pts.size(); ++i)
        {
            const float pv = displayPos (pts[(size_t) i]);
            if (! isVisibleNorm (pv))
                continue;
            const float x = normToX (pv);
            g.setColour ((i == dragSlice ? text : accent2).withAlpha (0.75f));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            juce::Path handle;
            handle.addTriangle (x - 5.0f, area.getBottom(), x + 5.0f, area.getBottom(), x, area.getBottom() - 7.0f);
            g.fillPath (handle);
            g.setColour (text.withAlpha (0.55f));
            g.drawText (padLabel (i), juce::Rectangle<float> (x + 2.0f, warpStrip().getBottom() + 2.0f, 28.0f, 14.0f), juce::Justification::left);
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
            g.drawText (padLabel (i), juce::Rectangle<float> (x + 2.0f, warpStrip().getBottom() + 2.0f, 28.0f, 14.0f), juce::Justification::left);
        }
    }

    // start / end handles of the selected pad
    if (padUsesSample)
    {
        for (int m = 0; m < 2; ++m)
        {
            const float x = normToX (displayPos (m == 0 ? st : en));
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

    // hints go in the top row (left of the zoom info) so they never cover the sample name
    {
        auto hint = header.withWidth (juce::jmax (0.0f, header.getWidth() - 290.0f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        if (processor.isLazyChopActive())
        {
            g.setColour (juce::Colour (0xffe0413a));
            g.drawText ("LAZY CHOP: hit pads / keys / MIDI to drop slices", hint, juce::Justification::left, true);
        }
        else if (isWarpView())
        {
            g.setColour (juce::Colour (0xffffc94a));
            g.drawText (warpPreview ? "warping..." : "WARP: drag in the top strip (or Ctrl+drag) onto the grid   dbl-click strip: add / remove",
                        hint, juce::Justification::left, true);
        }
        else if (isChopSource())
        {
            g.setColour (dim);
            g.drawText ("Shift+click: add   dbl-click: delete   Ctrl+drag: warp",
                        hint, juce::Justification::left, true);
        }
    }

    // footer: sample on the left (shortened with ... when long), pad region on the right
    const auto footer = juce::Rectangle<float> (area.getX(), area.getBottom() + 1.0f, area.getWidth(), 16.0f);
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.0f));
    const juce::String info = "   " + juce::String (secs, 2) + " s   " + juce::String ((int) sample->sampleRate) + " Hz";
    auto left = footer.withWidth (footer.getWidth() - 200.0f);
    g.drawText (sample->name + info, left, juce::Justification::left, true);
    if (padUsesSample)
        g.drawText ("Pad " + padLabel (pad) + "  " + juce::String (st * secs, 3) + " - " + juce::String (en * secs, 3) + " s",
                    footer, juce::Justification::right);
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    if (sample == nullptr) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    if (warpStrip().contains (e.position))
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); // warp strip: drag = stretch the audio
        return;
    }
    auto& ps = processor.getEngine().getPad (pad);
    const float x = (float) e.x;
    const bool nearMarker = warpMarkerNear (x) >= 0 || sliceMarkerNear (x) >= 0
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
    const int wm = warpMarkerNear (x);
    if (isWarpView())
    {
        m.addSeparator();
        m.addItem (5, "Remove this warp marker", wm >= 0);
        m.addItem (6, "Remove all warp markers", ! shownWarp().markers.empty());
    }

    juce::Component::SafePointer<WaveformView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, marker, n, wm] (int r)
    {
        if (safe == nullptr) return;
        auto& proc = safe->processor;
        if ((r == 5 || r == 6) && safe->isWarpView())
        {
            proc.pushUndo ("Warp markers");
            if (! safe->warpPreview) safe->warpWorking = safe->sample->warp;
            if (r == 5 && wm >= 0 && wm < (int) safe->warpWorking.markers.size())
                safe->warpWorking.markers.erase (safe->warpWorking.markers.begin() + wm);
            if (r == 6)
                safe->warpWorking.markers.clear();
            safe->warpPreview = true;
            safe->applyWarp();
            return;
        }
        if (r >= 1 && r <= 3) proc.pushUndo ("Chop markers");
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

    // the warp strip at the top (with the yellow diamonds), or Ctrl + drag anywhere: warp the audio
    if (e.mods.isCommandDown() || warpStrip().contains (e.position))
    {
        startWarpDrag (x);
        return;
    }

    // chop markers win over the pad's own start/end handles
    if (const int m = sliceMarkerNear (x); m >= 0)
    {
        processor.pushUndo ("Move marker");
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
        processor.pushUndo ("Pad start / end");
        dragging = ds <= de ? DragStart : DragEnd;
        return;
    }

    // Shift+click: add a manual chop marker (leaves any other chop mode)
    if (e.mods.isShiftDown())
    {
        processor.pushUndo ("Add marker");
        processor.addSlicePoint (xToNorm (x));
        if (onRegionChanged) onRegionChanged();
        return;
    }

    // Alt+click: move the closest start/end handle to the click
    if (e.mods.isAltDown())
    {
        processor.pushUndo ("Pad start / end");
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

    if (dragging == DragWarp)
    {
        if (warpPendingCreate)
        {
            if (e.getDistanceFromDragStart() < 3)
                return;
            if (! beginPendingWarpMarker())
            {
                dragging = DragNone;
                return;
            }
        }
        warpMoved = true;
        auto& m = warpWorking.markers;
        if (warpDragIndex < 0 || warpDragIndex >= (int) m.size())
            return;
        float v = e.mods.isAltDown() ? n : snapToGrid (n);
        const float eps = 1.0e-4f;
        const float lo = (warpDragIndex > 0 ? m[(size_t) warpDragIndex - 1].second : 0.0f) + eps;
        const float hi = (warpDragIndex + 1 < (int) m.size() ? m[(size_t) warpDragIndex + 1].second : 1.0f) - eps;
        m[(size_t) warpDragIndex].second = juce::jlimit (lo, juce::jmax (lo, hi), v);
        repaint();
        return;
    }

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

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (dragging == DragWarp)
    {
        if (warpMoved)
            applyWarp();
        else if (! warpAwaiting)
            warpPreview = false; // just a click on a marker: nothing changed
        warpPendingCreate = false;
        repaint();
    }
    dragging = DragNone;
    dragSlice = -1;
    warpDragIndex = -1;
}

//==============================================================================
double WaveformView::gridStepNorm() const
{
    if (sample == nullptr || ! sample->isWarped())
        return 0.0;
    const double beats = sample->warp.lengthBeats();
    const double pxPerBeat = waveArea().getWidth() / (viewLength * beats);
    // finest grid that still leaves ~10 px between lines: 4 beats ... 1/64 beat (1/256 note)
    double sub = 1.0;
    while (sub > 1.0 / 64.0 && sub * 0.5 * pxPerBeat >= 10.0)
        sub *= 0.5;
    while (sub < 4.0 && sub * pxPerBeat < 10.0)
        sub *= 2.0;
    return sub / beats;
}

float WaveformView::snapToGrid (float v) const
{
    const double g = gridStepNorm();
    return g > 0.0 ? (float) (std::round (v / g) * g) : v;
}

float WaveformView::displayPos (float v) const
{
    if (! warpPreview || sample == nullptr || ! sample->isWarped())
        return v;
    return warpWorking.srcToDst (sample->warp.dstToSrc (v));
}

int WaveformView::warpMarkerNear (float x) const
{
    if (! isWarpView())
        return -1;
    const auto& m = shownWarp().markers;
    int best = -1;
    float bestDist = 7.0f;
    for (int i = 0; i < (int) m.size(); ++i)
    {
        const float d = std::abs (x - normToX (m[(size_t) i].second));
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

bool WaveformView::ensureWarped()
{
    // not warped yet: switch WARP on (whole sample, length that fits the current tempo)
    if (isWarpView())
        return true;
    if (processor.getEngine().getPadSample (pad) == nullptr)
        return false;
    processor.setWarp (pad, processor.suggestWarpSettings (pad));
    refresh();
    return isWarpView();
}

void WaveformView::startWarpDrag (float x)
{
    dragging = DragWarp;
    warpMoved = false;
    warpDragIndex = -1;
    warpPendingCreate = false;

    // grab an existing warp marker (the yellow diamond)
    if (isWarpView())
    {
        if (! warpPreview)
            warpWorking = sample->warp;
        if (const int wm = warpMarkerNear (x); wm >= 0)
        {
            processor.pushUndo ("Warp marker");
            warpDragIndex = wm;
            warpPreview = true;
            repaint();
            return;
        }
    }

    // otherwise a new warp marker is made as soon as the mouse moves
    // (at the transient / chop marker under the mouse, or right there)
    warpPendingCreate = true;
    warpPendingX = x;
}

bool WaveformView::beginPendingWarpMarker()
{
    warpPendingCreate = false;
    processor.pushUndo ("Warp marker");
    if (! ensureWarped())
        return false;
    if (! warpPreview)
        warpWorking = sample->warp;

    float v = xToNorm (warpPendingX);
    if (const int cm = sliceMarkerNear (warpPendingX); cm >= 0)
        v = displayPos (processor.getSlicePoints()[(size_t) cm]);
    if (v <= 0.0005f || v >= 0.9995f)
        return false; // the start and the end stay where they are

    const float u = warpWorking.dstToSrc (v);
    auto& m = warpWorking.markers;
    auto it = std::lower_bound (m.begin(), m.end(), u, [] (const std::pair<float, float>& a, float val) { return a.first < val; });
    warpDragIndex = (int) std::distance (m.begin(), m.insert (it, { u, v }));
    warpPreview = true;
    repaint();
    return true;
}

void WaveformView::applyWarp()
{
    if (! warpPreview || sample == nullptr)
        return;
    auto s = warpWorking;
    s.enabled = true;
    warpAwaiting = true;
    processor.requestWarp (pad, s); // stretched in the background; refresh() ends the preview
    repaint();
}

void WaveformView::drawWarpGrid (juce::Graphics& g, juce::Rectangle<float> area)
{
    const double beats = sample->warp.lengthBeats();
    const double grid = gridStepNorm();
    if (grid <= 0.0)
        return;

    const double first = std::floor (viewStart / grid) * grid;
    for (double v = first; v <= viewStart + viewLength + grid * 0.5; v += grid)
    {
        const double beat = v * beats;
        const bool bar = std::abs (beat / 4.0 - std::round (beat / 4.0)) < 1.0e-6;
        const bool onBeat = std::abs (beat - std::round (beat)) < 1.0e-6;
        g.setColour (bar ? juce::Colour (0x66ffffff) : onBeat ? juce::Colour (0x33ffffff) : juce::Colour (0x18ffffff));
        g.drawVerticalLine ((int) normToX ((float) v), area.getY(), area.getBottom());
        if (bar)
        {
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.setColour (juce::Colour (0x99ffffff));
            g.drawText (juce::String ((int) std::round (beat / 4.0) + 1), juce::Rectangle<float> (normToX ((float) v) + 2.0f, area.getBottom() - 14.0f, 30.0f, 12.0f),
                        juce::Justification::left);
        }
    }

    // warp markers: yellow handles at the top
    const auto& m = shownWarp().markers;
    for (int i = 0; i < (int) m.size(); ++i)
    {
        const float v = m[(size_t) i].second;
        if (! isVisibleNorm (v)) continue;
        const float x = normToX (v);
        g.setColour (juce::Colour (0xffffc94a).withAlpha (i == warpDragIndex ? 1.0f : 0.85f));
        g.fillRect (x - 0.75f, area.getY(), 1.5f, area.getHeight());
        juce::Path d;
        d.addRectangle (-4.5f, -4.5f, 9.0f, 9.0f);
        g.fillPath (d, juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f).translated (x, warpStrip().getCentreY()));
    }
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (sample == nullptr || e.mods.isPopupMenu())
        return;

    // warp strip: double-click adds a warp marker, double-click on a diamond removes it
    if (warpStrip().contains (e.position) && warpMarkerNear ((float) e.x) < 0)
    {
        dragging = DragNone;
        warpPendingX = (float) e.x;
        if (beginPendingWarpMarker())
            applyWarp();
        return;
    }

    // Ctrl + double-click (or double-click on a diamond): remove a warp marker
    if (e.mods.isCommandDown() || warpStrip().contains (e.position))
    {
        if (! isWarpView()) return;
        const int wm = warpMarkerNear ((float) e.x);
        if (wm >= 0)
        {
            processor.pushUndo ("Remove warp marker");
            if (! warpPreview) warpWorking = sample->warp;
            warpWorking.markers.erase (warpWorking.markers.begin() + wm);
            warpPreview = true;
            applyWarp();
        }
        dragging = DragNone;
        return;
    }

    // double-click on a marker deletes it (also transient markers), elsewhere adds one
    if (const int m = sliceMarkerNear ((float) e.x); m >= 0)
    {
        dragging = DragNone;
        dragSlice = -1;
        processor.pushUndo ("Delete marker");
        processor.removeSlicePoint (m);
    }
    else
    {
        processor.pushUndo ("Add marker");
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
