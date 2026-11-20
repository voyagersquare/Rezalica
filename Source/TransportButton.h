#pragma once

#include "LookAndFeel.h"

namespace rz
{
/** Transport key with a symbol instead of text (record / play / pause / stop), on the same grey
    rubber button face as the rest of the UI. setActive() lights the symbol. */
class TransportButton : public juce::Button
{
public:
    enum Kind { record, play, pause, stop };

    TransportButton (Kind k, const juce::String& name) : juce::Button (name), kind (k)
    {
        setWantsKeyboardFocus (false);
        setTooltip (name);
    }

    void setActive (bool a) { if (a != active) { active = a; repaint(); } }
    void setBlink (bool b)  { if (b != blink) { blink = b; repaint(); } }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        using namespace colours;
        getLookAndFeel().drawButtonBackground (g, *this, buttonFace, highlighted, down);

        auto r = getLocalBounds().toFloat().reduced (5.0f);
        // the white frame of a transport key
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.drawRoundedRectangle (r, 4.0f, 1.6f);

        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        const auto c = r.getCentre().translated (0.0f, down ? 1.0f : 0.0f);
        const auto symbolOff = ink;

        switch (kind)
        {
            case record:
            {
                const bool lit = active && ! blink;
                auto dot = juce::Rectangle<float> (s * 1.15f, s * 1.15f).withCentre (c);
                if (lit)
                {
                    g.setColour (ledRed.withAlpha (0.35f));
                    g.fillEllipse (dot.expanded (4.0f));
                }
                g.setColour (lit ? ledRed : juce::Colour (0xffc42424));
                g.fillEllipse (dot);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawEllipse (dot, 1.0f);
                break;
            }
            case play:
            {
                juce::Path p;
                p.addTriangle (c.x - s * 0.42f, c.y - s * 0.55f, c.x - s * 0.42f, c.y + s * 0.55f, c.x + s * 0.58f, c.y);
                if (active)
                {
                    g.setColour (juce::Colour (0xff3ee05a).withAlpha (0.35f));
                    g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                g.setColour (active ? juce::Colour (0xff2bd14a) : symbolOff);
                g.fillPath (p);
                g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;
            }
            case pause:
            {
                const float bw = s * 0.32f, bh = s * 1.1f, gap = s * 0.22f;
                g.setColour (active && ! blink ? accent2 : symbolOff);
                g.fillRoundedRectangle (c.x - gap - bw, c.y - bh * 0.5f, bw, bh, 1.5f);
                g.fillRoundedRectangle (c.x + gap, c.y - bh * 0.5f, bw, bh, 1.5f);
                break;
            }
            case stop:
            {
                g.setColour (symbolOff);
                g.fillRoundedRectangle (juce::Rectangle<float> (s * 1.0f, s * 1.0f).withCentre (c), 1.5f);
                break;
            }
        }
    }

private:
    Kind kind;
    bool active = false, blink = false;
};
} // namespace rz
