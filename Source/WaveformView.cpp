#include "WaveformView.h"
#include "LookAndFeel.h"

namespace rz
{
WaveformView::WaveformView (RezalicaProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (false);
}

void WaveformView::setPad (int newPad)
{
    pad = newPad;
    refresh();
}

void WaveformView::refresh()
{
    auto s = processor.getEngine().getPadSample (pad);
    if (s != sample)
    {
        sample = s;
        rebuildPeaks();
    }
    repaint();
}

void WaveformView::updatePlayhead()
{
    const float p = processor.getEngine().playPosition[(size_t) pad].load();
    if (! juce::exactlyEqual (p, playhead))
    {
        playhead = p;
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
    const float st = ps.start.load(), en = ps.end.load();

    // selected region
    g.setColour (accent.withAlpha (0.12f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (normToX (st), area.getY(), normToX (en), area.getBottom()));

    // waveform
    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.5f;
    for (size_t x = 0; x < peaks.size(); ++x)
    {
        const float px = area.getX() + (float) x;
        const float n = (float) x / (float) juce::jmax ((size_t) 1, peaks.size());
        g.setColour (n >= st && n <= en ? accent2 : dim.withAlpha (0.6f));
        g.drawVerticalLine ((int) px, mid - peaks[x].second * half, mid - peaks[x].first * half + 1.0f);
    }

    // other pads that use the same sample
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (int i = 0; i < numPads; ++i)
    {
        if (i == pad || processor.getEngine().getPadSample (i) != sample)
            continue;
        const float x = normToX (processor.getEngine().getPad (i).start.load());
        g.setColour (text.withAlpha (0.35f));
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (x + 2.0f, bounds.getY() + 2.0f, 20.0f, 14.0f), juce::Justification::left);
    }

    // start / end markers
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

    // playhead
    if (playhead >= 0.0f)
    {
        g.setColour (juce::Colours::white);
        g.drawVerticalLine ((int) normToX (playhead), area.getY(), area.getBottom());
    }

    // footer
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.0f));
    const double secs = sample->buffer.getNumSamples() / sample->sampleRate;
    g.drawText (sample->name + "   " + juce::String (secs, 2) + " s   " + juce::String ((int) sample->sampleRate) + " Hz",
                juce::Rectangle<float> (area.getX(), area.getBottom() + 1.0f, area.getWidth(), 16.0f), juce::Justification::left);
    g.drawText ("Pad " + juce::String (pad + 1) + "  " + juce::String (st * secs, 3) + " - " + juce::String (en * secs, 3) + " s",
                juce::Rectangle<float> (area.getX(), area.getBottom() + 1.0f, area.getWidth(), 16.0f), juce::Justification::right);
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    if (sample == nullptr) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    auto& ps = processor.getEngine().getPad (pad);
    const float x = (float) e.x;
    const bool nearMarker = std::abs (x - normToX (ps.start.load())) < 6.0f || std::abs (x - normToX (ps.end.load())) < 6.0f;
    setMouseCursor (nearMarker ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (sample == nullptr)
        return;

    auto& ps = processor.getEngine().getPad (pad);
    const float x = (float) e.x;
    const float ds = std::abs (x - normToX (ps.start.load()));
    const float de = std::abs (x - normToX (ps.end.load()));

    if (ds < 8.0f || de < 8.0f)
    {
        dragging = ds <= de ? DragStart : DragEnd;
        return;
    }

    // Alt/Shift-click: move the closest marker to the click
    if (e.mods.isShiftDown() || e.mods.isAltDown())
    {
        dragging = ds <= de ? DragStart : DragEnd;
        mouseDrag (e);
        return;
    }

    // Plain click selects the slice (pad) under the mouse, if any
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

    auto& ps = processor.getEngine().getPad (pad);
    const float n = xToNorm ((float) e.x);
    const float minGap = 64.0f / (float) juce::jmax (64, sample->buffer.getNumSamples());

    if (dragging == DragStart) ps.start = juce::jmin (n, ps.end.load() - minGap);
    else                       ps.end = juce::jmax (n, ps.start.load() + minGap);

    if (onRegionChanged) onRegionChanged();
    repaint();
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
