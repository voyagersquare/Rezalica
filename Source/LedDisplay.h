#pragma once

#include "Sequencer.h"
#include "LookAndFeel.h"

namespace rz
{
/** Red 7-segment LED tempo display. Drag up / down (Shift = fine) or use the mouse wheel to change
    the tempo, double-click to type it. */
class LedBpmDisplay : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::Timer
{
public:
    explicit LedBpmDisplay (Sequencer& s) : seq (s)
    {
        setTooltip ("Project tempo (BPM): drag up / down (Shift = fine), mouse wheel, double-click to type");
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        timerCallback();
        startTimerHz (15);
    }

    static void drawDigit (juce::Graphics& g, juce::Rectangle<float> r, int digit, bool dot, juce::Colour on)
    {
        // segments a b c d e f g
        static const int masks[] { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
        const int mask = digit >= 0 && digit <= 9 ? masks[digit] : 0;
        const float w = r.getWidth(), h = r.getHeight(), t = juce::jmax (2.0f, w * 0.16f);
        const float x = r.getX(), y = r.getY(), sk = w * 0.12f; // slight italic slant
        auto seg = [&] (int i, float x0, float y0, float x1, float y1)
        {
            const bool lit = (mask >> i) & 1;
            juce::Path p;
            p.startNewSubPath (x0, y0);
            p.lineTo (x1, y1);
            g.setColour (lit ? on : on.withAlpha (0.09f));
            g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            if (lit)
            {
                g.setColour (on.withAlpha (0.18f));
                g.strokePath (p, juce::PathStrokeType (t * 2.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            }
        };
        auto px = [&] (float fx, float fy) { return juce::Point<float> (x + fx * w + (1.0f - fy) * sk, y + fy * h); };
        const float m = 0.12f;
        auto line = [&] (int i, juce::Point<float> a, juce::Point<float> b) { seg (i, a.x, a.y, b.x, b.y); };
        line (0, px (m + 0.08f, 0.0f),  px (1.0f - m - 0.08f, 0.0f));     // a
        line (1, px (1.0f - m, 0.06f),  px (1.0f - m, 0.44f));            // b
        line (2, px (1.0f - m, 0.56f),  px (1.0f - m, 0.94f));            // c
        line (3, px (m + 0.08f, 1.0f),  px (1.0f - m - 0.08f, 1.0f));     // d
        line (4, px (m, 0.56f),         px (m, 0.94f));                   // e
        line (5, px (m, 0.06f),         px (m, 0.44f));                   // f
        line (6, px (m + 0.08f, 0.5f),  px (1.0f - m - 0.08f, 0.5f));     // g
        g.setColour (dot ? on : on.withAlpha (0.09f));
        g.fillEllipse (x + w - t * 0.2f, y + h - t * 0.6f, t, t);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace colours;
        auto b = getLocalBounds().toFloat();
        auto caption = b.removeFromTop (13.0f);
        g.setColour (captionColour);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("BPM", caption, juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xff070707));
        g.fillRoundedRectangle (b, 3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x22ff2020), b.getX(), b.getY(), juce::Colours::transparentBlack, b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

        const int tenths = juce::roundToInt (shownBpm * 10.0f);
        const int whole = tenths / 10, frac = tenths % 10;
        auto inner = b.reduced (6.0f, 5.0f);
        const float dw = inner.getWidth() / 3.6f;
        const int digits[] { whole >= 100 ? whole / 100 : -1, whole >= 10 ? (whole / 10) % 10 : -1, whole % 10 };
        for (int i = 0; i < 3; ++i)
            drawDigit (g, inner.removeFromLeft (dw).reduced (dw * 0.1f, 0.0f), digits[i], i == 2 && frac != 0, ledRed);
        if (frac != 0)
            drawDigit (g, inner.removeFromLeft (dw * 0.6f).reduced (dw * 0.06f, inner.getHeight() * 0.22f).withY (inner.getY() + inner.getHeight() * 0.44f)
                             .withHeight (inner.getHeight() * 0.56f), frac, false, ledRed);
    }

    void mouseDown (const juce::MouseEvent&) override { dragStart = seq.bpm.load(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float step = e.mods.isShiftDown() ? 0.02f : 0.25f;
        setBpm (dragStart - (float) e.getDistanceFromDragStartY() * step);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        setBpm (seq.bpm.load() + (w.deltaY > 0 ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.1f : 1.0f));
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        auto* w = new juce::AlertWindow ("Tempo", "BPM (40 - 240)", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("bpm", juce::String (seq.bpm.load(), 1));
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<LedBpmDisplay> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
        {
            if (safe != nullptr && r == 1)
                safe->setBpm (w->getTextEditorContents ("bpm").getFloatValue());
        }), true);
        if (auto* ed = w->getTextEditor ("bpm")) ed->selectAll();
    }

    juce::Colour captionColour { colours::ink };

private:
    void setBpm (float v)
    {
        seq.bpm = juce::jlimit (40.0f, 240.0f, std::round (v * 10.0f) / 10.0f);
        timerCallback();
    }

    void timerCallback() override
    {
        const float v = seq.syncToHost.load() ? seq.effectiveBpm.load() : seq.bpm.load();
        if (std::abs (v - shownBpm) > 0.01f) { shownBpm = v; repaint(); }
    }

    Sequencer& seq;
    float shownBpm = -1.0f, dragStart = 120.0f;
};
} // namespace rz
