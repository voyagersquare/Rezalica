#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace rz
{
/** LEVELER: the selected pad's leveling amplifier, drawn as a grey rack unit (rack ears, big INPUT / OUTPUT
    knobs, MODE and RECOVERY TIME, a "dB COMPRESSION" meter, the POWER lamp and an ON / OFF toggle). */
class LevelerPanel : public juce::Component,
                     private juce::Timer
{
public:
    explicit LevelerPanel (RezalicaProcessor& p);
    ~LevelerPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    /** A knob of the panel: drag up / down (or turn the wheel), double-click = default. */
    struct PanelKnob : public juce::Component, public juce::SettableTooltipClient
    {
        enum Style { big, pointer };
        PanelKnob (Style s, float lo, float hi, float def, int detents = 0) : style (s), minV (lo), maxV (hi), defV (def), steps (detents) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override { setValue (defV); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            const float step = steps > 0 ? (w.deltaY > 0 ? 1.0f : -1.0f) * (maxV - minV) / (float) (steps - 1) : w.deltaY * (maxV - minV) * 0.3f;
            setValue (value + step);
        }
        void setValue (float v, bool notify = true);
        float getValue() const noexcept { return value; }
        float angleFor (float v) const;   // radians, 0 = up
        std::function<void (float)> onChange;
        Style style;
        float minV, maxV, defV, value = 0.0f, dragStartValue = 0.0f;
        int steps;
        float startAngle = -2.4f, endAngle = 2.4f;
    };

    /** The ON / OFF bat toggle switch. */
    struct ToggleSwitch : public juce::Component, public juce::SettableTooltipClient
    {
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { on = ! on; repaint(); if (onChange) onChange (on); }
        bool on = false;
        std::function<void (bool)> onChange;
    };

    /** The "dB COMPRESSION" meter. */
    struct Meter : public juce::Component
    {
        void paint (juce::Graphics&) override;
        float db = 0.0f;   // shown gain reduction (0 .. 40)
    };

private:
    void timerCallback() override;
    void syncFromPad();
    PadLeveler& lev();
    juce::Rectangle<float> faceplate() const;
    void renderBackground (juce::Graphics&);
    juce::Image background;

    RezalicaProcessor& processor;
    PanelKnob inputKnob { PanelKnob::big, 0.0f, 20.0f, 6.0f }, outputKnob { PanelKnob::big, 0.0f, 20.0f, 10.0f };
    PanelKnob modeKnob { PanelKnob::pointer, 0.0f, 2.0f, 0.0f, 3 }, recoveryKnob { PanelKnob::pointer, 0.0f, 1.0f, 0.5f };
    // the extras (lower row): ATTACK, RATIO, CHARACTER, MIX
    PanelKnob attackKnob { PanelKnob::pointer, 0.0f, 6.0f, 0.0f, 7 }, ratioKnob { PanelKnob::pointer, 0.0f, 4.0f, 0.0f, 5 };
    PanelKnob characterKnob { PanelKnob::pointer, 0.0f, 1.0f, 0.0f, 2 }, mixKnob { PanelKnob::pointer, 0.0f, 1.0f, 1.0f };
    ToggleSwitch power;
    Meter meter;
    int shownPad = -1;
    float needle = 0.0f;
};
} // namespace rz
