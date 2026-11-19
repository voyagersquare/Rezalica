#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rz::colours
{
inline const juce::Colour background { 0xff16171b };
inline const juce::Colour panel { 0xff222429 };
inline const juce::Colour panelLight { 0xff2f323a };
inline const juce::Colour outline { 0xff3a3e47 };
inline const juce::Colour accent { 0xffff7a2f };   // orange
inline const juce::Colour accent2 { 0xff3fc6c0 };  // teal
inline const juce::Colour text { 0xffe8e6e3 };
inline const juce::Colour dim { 0xff8a8f99 };
} // namespace rz::colours

namespace rz
{
class RezalicaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RezalicaLookAndFeel()
    {
        using namespace colours;
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::DocumentWindow::backgroundColourId, background);
        setColour (juce::TextButton::buttonColourId, panelLight);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::ComboBox::backgroundColourId, panelLight);
        setColour (juce::ComboBox::outlineColourId, outline);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, accent);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId, accent);
        setColour (juce::Slider::backgroundColourId, panelLight);
        setColour (juce::Slider::thumbColourId, text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace colours;
        auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float arcR = radius - 2.0f;

        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (panelLight);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // bipolar sliders draw from the centre
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, from, angle, true);
        g.setColour (slider.isEnabled() ? accent : dim);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float knobR = radius * 0.68f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4e58), c.x, c.y - knobR,
                                                 juce::Colour (0xff1e2025), c.x, c.y + knobR, false));
        g.fillEllipse (c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f);
        g.setColour (outline);
        g.drawEllipse (c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -knobR + 2.0f, 3.0f, knobR * 0.55f, 1.5f);
        g.setColour (text);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.55f), juce::Font::bold));
    }

    juce::Font getLabelFont (juce::Label& l) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) l.getHeight() * 0.85f)));
    }
};

/** A rotary knob with a caption above it. */
class Knob : public juce::Component
{
public:
    explicit Knob (const juce::String& caption)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 15);
        slider.setWantsKeyboardFocus (false);
        label.setText (caption, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, colours::dim);
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds (b.removeFromTop (15));
        slider.setBounds (b);
    }

    juce::Slider slider;
    juce::Label label;
};
} // namespace rz
