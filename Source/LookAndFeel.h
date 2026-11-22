#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rz::colours
{
// hardware-sampler palette: black panels, red print, silver faceplate, cream pads
inline const juce::Colour background { 0xff121213 };
inline const juce::Colour panel { 0xff1a1a1c };
inline const juce::Colour panelLight { 0xff2a2a2e };
inline const juce::Colour outline { 0xff3a3a3f };
inline const juce::Colour accent { 0xffe8423f };   // red print / LEDs
inline const juce::Colour accent2 { 0xffffb54a };  // amber
inline const juce::Colour text { 0xfff1ede3 };     // cream
inline const juce::Colour dim { 0xff9b9a96 };
inline const juce::Colour silver { 0xffc9cacb };   // faceplate
inline const juce::Colour silverDark { 0xffa9abae };
inline const juce::Colour ink { 0xff1b1b1d };      // print on the faceplate
inline const juce::Colour padCream { 0xffe6e2d6 }; // rubber pads
inline const juce::Colour ledRed { 0xffff2d2d };
inline const juce::Colour buttonFace { 0xffcfcfcb }; // grey rubber buttons
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
        setColour (juce::TextButton::buttonColourId, buttonFace);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, ink);
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0d0d0e));
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
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0d0e));
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, outline);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ScrollBar::thumbColourId, dim);
    }

    // ---- grey rubber buttons with an LED that lights when the button is on ----
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        using namespace colours;
        auto r = b.getLocalBounds().toFloat().reduced (1.0f, 1.0f);
        const bool on = b.getToggleState();
        const bool custom = b.isColourSpecified (juce::TextButton::buttonColourId); // e.g. a red REC button
        juce::Colour face = custom ? backgroundColour : buttonFace;
        if (! b.isEnabled()) face = face.withMultipliedSaturation (0.3f).darker (0.25f);
        if (down) face = face.darker (0.18f);
        else if (highlighted) face = face.brighter (0.06f);

        // raised moulded rubber key: shadow under it, bright top, dark bottom, highlight on the top edge
        const float lift = down ? 0.5f : 2.5f;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0.0f, lift), 4.0f);
        if (down) r = r.translated (0.0f, 1.0f);
        g.setGradientFill (juce::ColourGradient (face.brighter (down ? 0.05f : 0.25f), r.getX(), r.getY(),
                                                 face.darker (down ? 0.05f : 0.25f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (down ? 0.15f : 0.55f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 4.0f, r.getRight() - 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawHorizontalLine ((int) r.getBottom() - 2, r.getX() + 4.0f, r.getRight() - 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        if (on && b.getClickingTogglesState())
        {
            // LED strip on the left: the button's own "on" colour
            const auto led = b.findColour (juce::TextButton::buttonOnColourId);
            auto lr = juce::Rectangle<float> (r.getX() + 5.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
            g.setColour (led.withAlpha (0.35f));
            g.fillEllipse (lr.expanded (3.0f));
            g.setColour (led.brighter (0.3f));
            g.fillEllipse (lr);
            g.setColour (led.withAlpha (0.12f));
            g.fillRoundedRectangle (r, 4.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        using namespace colours;
        const bool custom = b.isColourSpecified (juce::TextButton::buttonColourId);
        auto face = custom ? b.findColour (juce::TextButton::buttonColourId) : buttonFace;
        auto txt = face.getPerceivedBrightness() > 0.55f ? ink : text;
        if (! b.isEnabled()) txt = txt.withAlpha (0.4f);
        g.setColour (txt);
        g.setFont (getTextButtonFont (b, b.getHeight()));
        auto r = b.getLocalBounds().reduced (4, 2);
        if (b.getToggleState() && b.getClickingTogglesState())
            r.removeFromLeft (8); // room for the LED
        g.drawFittedText (b.getButtonText(), r, juce::Justification::centred, 2);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace colours;
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (1, 3.0f, (float) w - 3.0f);
        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);
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
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // every knob starts at the left (minimum); only pan / pitch, whose zero is the centre, draw from the middle
        const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, from, angle, true);
        g.setColour (slider.isEnabled() ? accent : dim);
        g.strokePath (value, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // black knob with a cream pointer line, red dot scale around it
        const float knobR = radius * 0.66f;
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            const float dr = radius - 1.5f;
            const auto p = c.getPointOnCircumference (dr, a);
            g.setColour (accent.withAlpha (slider.isEnabled() ? 0.9f : 0.35f));
            g.fillEllipse (p.x - 1.4f, p.y - 1.4f, 2.8f, 2.8f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (c.x - knobR, c.y - knobR + 2.0f, knobR * 2.0f, knobR * 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3d), c.x - knobR * 0.4f, c.y - knobR,
                                                 juce::Colour (0xff0b0b0c), c.x + knobR * 0.3f, c.y + knobR, false));
        g.fillEllipse (c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f);
        // knurled skirt
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        for (int i = 0; i < 24; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (knobR * 0.82f, a), c.getPointOnCircumference (knobR * 0.98f, a)), 1.0f);
        }
        g.setColour (juce::Colours::black);
        g.drawEllipse (c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);
        // light from the top left on the knob cap
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), c.x - knobR * 0.5f, c.y - knobR * 0.7f,
                                                 juce::Colours::transparentWhite, c.x, c.y, true));
        g.fillEllipse (c.x - knobR * 0.8f, c.y - knobR * 0.85f, knobR * 1.2f, knobR * 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-2.0f, -knobR + 2.0f, 4.0f, knobR * 0.7f, 2.0f);
        g.setColour (juce::Colour (0xfff2ead2));
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
        label.setColour (juce::Label::textColourId, colours::text.withAlpha (0.85f));
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

/** Keeps a Rezalica window above the others only while Rezalica is the active program:
    click another program (a browser ...) and the window goes behind it like any other. */
class StayOnTopOfRezalica : private juce::Timer
{
public:
    explicit StayOnTopOfRezalica (juce::TopLevelWindow& w) : window (w) { timerCallback(); startTimerHz (4); }
    ~StayOnTopOfRezalica() override { stopTimer(); }

private:
    void timerCallback() override
    {
        const bool front = juce::Process::isForegroundProcess();
        if (window.isAlwaysOnTop() != front)
            window.setAlwaysOnTop (front);
    }
    juce::TopLevelWindow& window;
};
} // namespace rz
