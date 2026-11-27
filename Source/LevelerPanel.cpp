#include "LevelerPanel.h"
#include "BinaryData.h"

namespace rz
{
namespace
{
const juce::Colour panelGrey (0xff6d7175), panelLight (0xff7c8085), panelDark (0xff55595d), printWhite (0xffeeeeea);

/** Positions on the faceplate are given as fractions of the unit's width / height (0..1). */
struct Map
{
    juce::Rectangle<float> f;
    juce::Point<float> p (float x, float y) const { return { f.getX() + x * f.getWidth(), f.getY() + y * f.getHeight() }; }
    float u (float px) const { return px * f.getWidth() / 900.0f; }   // a size given for a 900 px wide unit
};

void screw (juce::Graphics& g, juce::Point<float> c, float r)
{
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 2.0f, r * 2.0f + 2.0f).withCentre (c.translated (0.8f, 1.2f)));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9d9d4), c.x - r, c.y - r, juce::Colour (0xff6e6e6a), c.x + r, c.y + r, false));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (juce::Colour (0xff3a3a38));
    g.drawLine (c.x - r * 0.7f, c.y - r * 0.15f, c.x + r * 0.7f, c.y + r * 0.15f, juce::jmax (1.0f, r * 0.28f));
}

juce::Font boldFont (float h)   { return juce::Font (juce::FontOptions (h, juce::Font::bold)); }
juce::Font plainFont (float h)  { return juce::Font (juce::FontOptions (h)); }

juce::Font scriptFont (float h)
{
    static const juce::Typeface::Ptr script = juce::Typeface::createSystemTypefaceFor (BinaryData::GreatVibesRegular_ttf,
                                                                                      BinaryData::GreatVibesRegular_ttfSize);
    return juce::Font (juce::FontOptions (script).withHeight (h));
}

void centredText (juce::Graphics& g, const juce::String& s, juce::Point<float> c, float w, float h)
{
    g.drawText (s, juce::Rectangle<float> (w, h).withCentre (c), juce::Justification::centred, false);
}
} // namespace

//==============================================================================
float LevelerPanel::PanelKnob::angleFor (float v) const
{
    const float t = maxV > minV ? (v - minV) / (maxV - minV) : 0.0f;
    return startAngle + t * (endAngle - startAngle);
}

void LevelerPanel::PanelKnob::setValue (float v, bool notify)
{
    v = juce::jlimit (minV, maxV, v);
    if (steps > 1)
    {
        const float st = (maxV - minV) / (float) (steps - 1);
        v = minV + std::round ((v - minV) / st) * st;
    }
    if (std::abs (v - value) < 1.0e-6f) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

void LevelerPanel::PanelKnob::mouseDown (const juce::MouseEvent&) { dragStartValue = value; }

void LevelerPanel::PanelKnob::mouseDrag (const juce::MouseEvent& e)
{
    const float range = maxV - minV;
    const float delta = (float) (-e.getDistanceFromDragStartY() + e.getDistanceFromDragStartX() * 0.5f) / (steps > 1 ? 40.0f : 180.0f);
    setValue (dragStartValue + delta * range);
}

void LevelerPanel::PanelKnob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float a = angleFor (value);
    if (style == big)
    {
        // a big black knob with a fluted skirt and a polished chrome cap, a white line as pointer
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (r * 0.06f, r * 0.12f)));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), c.x, c.y - r, juce::Colour (0xff050505), c.x, c.y + r, false));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        // flutes around the skirt
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        for (int i = 0; i < 36; ++i)
        {
            const float t = (float) i / 36.0f * juce::MathConstants<float>::twoPi;
            g.drawLine (c.x + std::sin (t) * r * 0.78f, c.y - std::cos (t) * r * 0.78f, c.x + std::sin (t) * r * 0.97f, c.y - std::cos (t) * r * 0.97f, 1.2f);
        }
        // chrome cap
        const float cr = r * 0.52f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8e8e6), c.x - cr * 0.5f, c.y - cr * 0.7f, juce::Colour (0xff2a2a2a), c.x + cr * 0.6f, c.y + cr, true));
        g.fillEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.6f), c.x - cr * 0.35f, c.y - cr * 0.45f,
                                                 juce::Colours::transparentWhite, c.x - cr * 0.35f + cr * 0.9f, c.y - cr * 0.45f + cr * 0.9f, true));
        g.fillEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c), 1.0f);
        // pointer line on the skirt
        g.setColour (printWhite);
        g.drawLine (c.x + std::sin (a) * r * 0.70f, c.y - std::cos (a) * r * 0.70f, c.x + std::sin (a) * r * 0.97f, c.y - std::cos (a) * r * 0.97f, juce::jmax (2.0f, r * 0.07f));
    }
    else
    {
        // a small black pointer knob (a "chicken head" style bar)
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (r * 1.6f, r * 1.6f).withCentre (c.translated (1.0f, 2.0f)));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff444444), c.x, c.y - r, juce::Colour (0xff080808), c.x, c.y + r, false));
        g.fillEllipse (juce::Rectangle<float> (r * 1.6f, r * 1.6f).withCentre (c));
        juce::Path bar;
        bar.addRoundedRectangle (-r * 0.22f, -r, r * 0.44f, r * 2.0f, r * 0.2f);
        bar.applyTransform (juce::AffineTransform::rotation (a).translated (c));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4a), c.x - r, c.y - r, juce::Colour (0xff0a0a0a), c.x + r, c.y + r, false));
        g.fillPath (bar);
        g.setColour (printWhite);
        g.drawLine (c.x + std::sin (a) * r * 0.35f, c.y - std::cos (a) * r * 0.35f, c.x + std::sin (a) * r * 0.92f, c.y - std::cos (a) * r * 0.92f, juce::jmax (1.5f, r * 0.12f));
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawEllipse (juce::Rectangle<float> (r * 1.6f, r * 1.6f).withCentre (c).reduced (1.0f), 1.0f);
    }
}

//==============================================================================
void LevelerPanel::ToggleSwitch::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float w = b.getWidth();
    // the round chrome nut and the bat lever (up = ON)
    const float nut = w * 0.36f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeeeeea), c.x - nut, c.y - nut, juce::Colour (0xff5a5a58), c.x + nut, c.y + nut, false));
    g.fillEllipse (juce::Rectangle<float> (nut * 2.0f, nut * 2.0f).withCentre (c));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (juce::Rectangle<float> (nut * 2.0f, nut * 2.0f).withCentre (c), 1.0f);
    const float len = b.getHeight() * 0.42f, bw = w * 0.16f;
    const float dir = on ? -1.0f : 1.0f;
    juce::Path bat;
    bat.startNewSubPath (c.x - bw * 0.5f, c.y);
    bat.lineTo (c.x - bw * 0.75f, c.y + dir * len);
    bat.lineTo (c.x + bw * 0.75f, c.y + dir * len);
    bat.lineTo (c.x + bw * 0.5f, c.y);
    bat.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffafaf8), c.x - bw, c.y, juce::Colour (0xff7a7a78), c.x + bw, c.y, false));
    g.fillPath (bat);
    g.fillEllipse (juce::Rectangle<float> (bw * 1.7f, bw * 1.7f).withCentre ({ c.x, c.y + dir * len }));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.strokePath (bat, juce::PathStrokeType (0.8f));
}

//==============================================================================
void LevelerPanel::Meter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float w = b.getWidth(), h = b.getHeight();
    // black square bezel with the stepped (art deco) lower part
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (b.translated (2.0f, 3.0f), w * 0.06f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a2a), 0, 0, juce::Colour (0xff050505), 0, h, false));
    g.fillRoundedRectangle (b, w * 0.06f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (b.reduced (1.0f), w * 0.06f, 1.0f);
    auto face = juce::Rectangle<float> (b.getX() + w * 0.09f, b.getY() + h * 0.08f, w * 0.82f, h * 0.6f);
    // the stepped deco below the face
    for (int i = 0; i < 3; ++i)
    {
        auto step = juce::Rectangle<float> (face.getX() + w * 0.06f * (float) i, face.getBottom() + h * 0.02f + h * 0.07f * (float) i,
                                            face.getWidth() - w * 0.12f * (float) i, h * 0.06f);
        g.setColour (juce::Colour (0xff1a1a1a).brighter (0.15f * (float) (3 - i)));
        g.fillRect (step);
    }
    // adjust screw at the bottom
    {
        const auto c = juce::Point<float> (b.getCentreX(), b.getBottom() - h * 0.12f);
        const float r = w * 0.06f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), c.x, c.y - r, juce::Colours::black, c.x, c.y + r, false));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.0f);
    }

    // cream face
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7f3e6), face.getCentreX(), face.getY(), juce::Colour (0xffd8d2bf), face.getCentreX(), face.getBottom(), false));
    g.fillRect (face);
    const float radius = face.getWidth() * 0.74f;
    const auto pivot = juce::Point<float> (face.getCentreX(), face.getY() + face.getHeight() * 0.33f + radius);
    const float aLeft = -0.5f, aRight = 0.5f;
    auto angleFor = [&] (float db) { return aRight + (aLeft - aRight) * juce::jlimit (0.0f, 40.0f, db) / 40.0f; }; // 0 dB right, 40 left
    g.saveState();
    g.reduceClipRegion (face.toNearestInt());
    // scale arc and ticks
    g.setColour (juce::Colour (0xff1c1c1c));
    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, aLeft, aRight, true);
    g.strokePath (arc, juce::PathStrokeType (1.2f));
    for (int db = 0; db <= 40; db += 2)
    {
        const float a = angleFor ((float) db);
        const bool major = db % 10 == 0;
        const float r0 = radius * 0.86f, r1 = radius * (major ? 0.96f : 0.92f);
        g.drawLine (pivot.x + std::sin (a) * r0, pivot.y - std::cos (a) * r0, pivot.x + std::sin (a) * r1, pivot.y - std::cos (a) * r1, major ? 1.6f : 1.0f);
        if (major)
        {
            g.setFont (boldFont (face.getHeight() * 0.12f));
            centredText (g, juce::String (db), { pivot.x + std::sin (a) * radius * 1.03f, pivot.y - std::cos (a) * radius * 1.03f }, 30.0f, 14.0f);
        }
    }
    g.setFont (boldFont (face.getHeight() * 0.12f));
    centredText (g, "REZ-LEVEL", { face.getCentreX(), face.getY() + face.getHeight() * 0.585f }, face.getWidth(), face.getHeight() * 0.14f);
    g.setFont (plainFont (face.getHeight() * 0.1f));
    centredText (g, "DB COMPRESSION", { face.getCentreX(), face.getY() + face.getHeight() * 0.705f }, face.getWidth(), face.getHeight() * 0.12f);
    {
        auto tag = juce::Rectangle<float> (face.getWidth() * 0.36f, face.getHeight() * 0.12f).withCentre ({ face.getCentreX(), face.getY() + face.getHeight() * 0.86f });
        g.drawRect (tag, 1.0f);
        g.setFont (boldFont (tag.getHeight() * 0.75f));
        g.drawText ("REZALICA", tag, juce::Justification::centred, false);
    }
    // the needle
    const float a = angleFor (db);
    g.setColour (juce::Colour (0xff111111));
    g.drawLine (pivot.x + std::sin (a) * radius * 0.55f, pivot.y - std::cos (a) * radius * 0.55f,
                pivot.x + std::sin (a) * radius * 0.98f, pivot.y - std::cos (a) * radius * 0.98f, 1.6f);
    g.restoreState();
    // glass
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), face.getX(), face.getY(), juce::Colours::transparentWhite, face.getCentreX(), face.getCentreY(), false));
    g.fillRect (face.withHeight (face.getHeight() * 0.5f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRect (face, 1.0f);
}

//==============================================================================
LevelerPanel::LevelerPanel (RezalicaProcessor& p) : processor (p)
{
    for (auto* k : { &inputKnob, &outputKnob, &modeKnob, &recoveryKnob })
        addAndMakeVisible (*k);
    addAndMakeVisible (power);
    addAndMakeVisible (meter);
    modeKnob.startAngle = -1.15f; modeKnob.endAngle = 1.15f;            // SINGLE / DOUBLE / TRIPLE
    recoveryKnob.startAngle = -1.9f; recoveryKnob.endAngle = 1.9f;      // SLOW .. FAST
    inputKnob.setTooltip ("INPUT: how hard the pad drives the LEVELER (more = more compression). Double-click: default");
    outputKnob.setTooltip ("OUTPUT: the level after the LEVELER (10 = 0 dB). Double-click: default");
    modeKnob.setTooltip ("MODE: SINGLE = one release time; DOUBLE / TRIPLE add slower stages: short peaks recover fast, long loud parts slowly");
    recoveryKnob.setTooltip ("RECOVERY TIME: how fast the compression lets go (SLOW .. FAST)");
    power.setTooltip ("ON / OFF: the LEVELER of the selected pad");

    inputKnob.onChange    = [this] (float v) { lev().input = v; };
    outputKnob.onChange   = [this] (float v) { lev().output = v; };
    modeKnob.onChange     = [this] (float v) { lev().mode = juce::roundToInt (v); };
    recoveryKnob.onChange = [this] (float v) { lev().recovery = v; };
    power.onChange = [this] (bool on) { processor.setLevelerOn (processor.selectedPad.load(), on); repaint(); };
    syncFromPad();
    startTimerHz (30);
}

LevelerPanel::~LevelerPanel() { stopTimer(); }

PadLeveler& LevelerPanel::lev() { return processor.getEngine().getPad (juce::jlimit (0, numPads - 1, processor.selectedPad.load())).leveler; }

void LevelerPanel::syncFromPad()
{
    shownPad = processor.selectedPad.load();
    auto& l = lev();
    inputKnob.setValue (l.input.load(), false);
    outputKnob.setValue (l.output.load(), false);
    modeKnob.setValue ((float) l.mode.load(), false);
    recoveryKnob.setValue (l.recovery.load(), false);
    power.on = l.on.load();
    power.repaint();
    repaint();
}

void LevelerPanel::timerCallback()
{
    if (processor.selectedPad.load() != shownPad || power.on != lev().on.load())
        syncFromPad();
    // meter ballistics: quick to rise, slower to fall back
    const float target = lev().on.load() ? processor.getLevelerReduction (shownPad) : 0.0f;
    needle += (target - needle) * (target > needle ? 0.55f : 0.18f);
    if (std::abs (needle - meter.db) > 0.02f) { meter.db = needle; meter.repaint(); }
}

juce::Rectangle<float> LevelerPanel::faceplate() const
{
    // the unit keeps its shape (about 3.6 : 1) and sits in the middle of the panel
    auto area = getLocalBounds().toFloat().reduced (10.0f);
    const float aspect = 900.0f / 250.0f;
    float w = area.getWidth(), h = w / aspect;
    if (h > area.getHeight()) { h = area.getHeight(); w = h * aspect; }
    return juce::Rectangle<float> (w, h).withCentre (area.getCentre());
}

void LevelerPanel::resized()
{
    const Map m { faceplate() };
    auto at = [&] (juce::Component& c, float x, float y, float sizePx)
    {
        const float s = m.u (sizePx);
        c.setBounds (juce::Rectangle<float> (s, s).withCentre (m.p (x, y)).toNearestInt());
    };
    at (inputKnob, 0.225f, 0.47f, 82.0f);
    at (outputKnob, 0.765f, 0.47f, 82.0f);
    at (modeKnob, 0.355f, 0.34f, 32.0f);
    at (recoveryKnob, 0.355f, 0.63f, 32.0f);
    power.setBounds (juce::Rectangle<float> (m.u (30.0f), m.u (46.0f)).withCentre (m.p (0.637f, 0.58f)).toNearestInt());
    meter.setBounds (juce::Rectangle<float> (m.u (138.0f), m.u (150.0f)).withCentre (m.p (0.5f, 0.42f)).toNearestInt());
}

void LevelerPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0f10));
    const auto f = faceplate();
    const Map m { f };
    const bool on = lev().on.load();

    // rack ears (behind) with their mounting slots
    const float earW = f.getWidth() * 0.09f;
    for (int side = 0; side < 2; ++side)
    {
        auto ear = side == 0 ? f.withWidth (earW) : f.withLeft (f.getRight() - earW);
        g.setGradientFill (juce::ColourGradient (panelLight, ear.getX(), ear.getY(), panelDark, ear.getRight(), ear.getBottom(), false));
        g.fillRect (ear);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRect (ear, 1.0f);
        for (float y : { 0.12f, 0.88f })
        {
            auto slot = juce::Rectangle<float> (earW * 0.42f, f.getHeight() * 0.07f)
                            .withCentre ({ side == 0 ? ear.getX() + earW * 0.22f : ear.getRight() - earW * 0.22f, f.getY() + f.getHeight() * y });
            g.setColour (juce::Colour (0xff0b0f10));
            g.fillRoundedRectangle (slot, slot.getHeight() * 0.5f);
        }
    }
    // the front panel (a little in front of the ears)
    auto front = f.reduced (earW * 0.55f, f.getHeight() * 0.0f).withTrimmedTop (f.getHeight() * 0.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (front.translated (0.0f, m.u (4.0f)));
    g.setGradientFill (juce::ColourGradient (panelLight, front.getX(), front.getY(), panelGrey, front.getX(), front.getBottom(), false));
    g.fillRect (front);
    // fine hammertone grain
    {
        juce::Random rnd (1234);
        for (int i = 0; i < 900; ++i)
        {
            const float x = front.getX() + rnd.nextFloat() * front.getWidth(), y = front.getY() + rnd.nextFloat() * front.getHeight();
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.035f));
            g.fillRect (x, y, 1.5f, 1.5f);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawLine (front.getX(), front.getY() + 1.0f, front.getRight(), front.getY() + 1.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawRect (front, 1.0f);
    // the bent lower lip with its little vents
    {
        auto lip = front.removeFromBottom (f.getHeight() * 0.07f);
        g.setColour (panelDark);
        g.fillRect (lip);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int i = 0; i < 26; ++i)
            g.fillRect (juce::Rectangle<float> (m.u (14.0f), m.u (3.0f)).withCentre ({ lip.getX() + lip.getWidth() * ((float) i + 0.5f) / 26.0f, lip.getCentreY() }));
    }
    // screws: two on each side, one lower
    for (float x : { 0.135f, 0.865f })
    {
        screw (g, m.p (x, 0.17f), m.u (6.5f));
        screw (g, m.p (x, 0.29f), m.u (6.5f));
        screw (g, m.p (x, 0.72f), m.u (6.5f));
    }

    // scales of the big knobs: dots and 0 4 8 12 16 20
    g.setColour (printWhite);
    for (auto* k : { &inputKnob, &outputKnob })
    {
        const auto c = k->getBounds().toFloat().getCentre();
        const float r = k->getWidth() * 0.5f;
        for (int i = 0; i <= 10; ++i)
        {
            const float v = (float) i * 2.0f;
            const float a = k->angleFor (v);
            const bool labelled = i % 2 == 0;
            const float dr = r * 1.2f;
            g.fillEllipse (juce::Rectangle<float> (m.u (3.6f), m.u (3.6f)).withCentre ({ c.x + std::sin (a) * dr, c.y - std::cos (a) * dr }));
            if (labelled)
            {
                g.setFont (plainFont (m.u (14.0f)));
                centredText (g, juce::String ((int) v), { c.x + std::sin (a) * r * 1.5f, c.y - std::cos (a) * r * 1.5f }, m.u (30.0f), m.u (16.0f));
            }
        }
        g.setFont (boldFont (m.u (15.0f)));
        centredText (g, k == &inputKnob ? "INPUT" : "OUTPUT", { c.x, c.y + r * 1.75f }, m.u (100.0f), m.u (18.0f));
    }

    // MODE + RECOVERY TIME
    {
        const auto mc = modeKnob.getBounds().toFloat().getCentre();
        const float r = modeKnob.getWidth() * 0.5f;
        g.setFont (boldFont (m.u (15.0f)));
        centredText (g, "MODE", { mc.x, mc.y - r * 2.3f }, m.u (80.0f), m.u (18.0f));
        g.setFont (plainFont (m.u (10.5f)));
        centredText (g, "DOUBLE", { mc.x, mc.y - r * 1.35f }, m.u (60.0f), m.u (12.0f));
        g.drawText ("SINGLE", juce::Rectangle<float> (m.u (60.0f), m.u (12.0f)).withCentre ({ mc.x - r * 1.25f - m.u (28.0f), mc.y - r * 0.55f }), juce::Justification::centredRight);
        g.drawText ("TRIPLE", juce::Rectangle<float> (m.u (60.0f), m.u (12.0f)).withCentre ({ mc.x + r * 1.25f + m.u (28.0f), mc.y - r * 0.55f }), juce::Justification::centredLeft);
        for (int i = 0; i < 3; ++i)
        {
            const float a = modeKnob.angleFor ((float) i);
            g.fillEllipse (juce::Rectangle<float> (m.u (3.0f), m.u (3.0f)).withCentre ({ mc.x + std::sin (a) * r * 1.2f, mc.y - std::cos (a) * r * 1.2f }));
        }
        const auto rc = recoveryKnob.getBounds().toFloat().getCentre();
        g.setFont (boldFont (m.u (14.0f)));
        centredText (g, "RECOVERY", { rc.x, rc.y - r * 2.55f }, m.u (110.0f), m.u (16.0f));
        centredText (g, "TIME", { rc.x, rc.y - r * 1.62f }, m.u (110.0f), m.u (16.0f));
        g.setFont (plainFont (m.u (10.5f)));
        g.drawText ("SLOW", juce::Rectangle<float> (m.u (50.0f), m.u (12.0f)).withCentre ({ rc.x - r - m.u (28.0f), rc.y + r * 0.3f }), juce::Justification::centredRight);
        g.drawText ("FAST", juce::Rectangle<float> (m.u (50.0f), m.u (12.0f)).withCentre ({ rc.x + r + m.u (28.0f), rc.y + r * 0.3f }), juce::Justification::centredLeft);
        // the name, with its script line
        g.setFont (boldFont (m.u (24.0f)).withExtraKerningFactor (0.03f));
        centredText (g, "REZ-LEVEL", m.p (0.355f, 0.80f), m.u (200.0f), m.u (26.0f));
        g.setFont (scriptFont (m.u (22.0f)));
        centredText (g, "pad edition", m.p (0.355f, 0.885f), m.u (200.0f), m.u (24.0f));
    }

    // POWER lamp (lit while the pad's LEVELER is on) and the ON / OFF labels
    {
        g.setFont (boldFont (m.u (15.0f)));
        centredText (g, "POWER", m.p (0.637f, 0.20f), m.u (90.0f), m.u (18.0f));
        const auto c = m.p (0.637f, 0.31f);
        const float r = m.u (13.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.4f, r * 2.4f).withCentre (c));
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0x66ff2a1a), c.x, c.y, juce::Colour (0x00ff2a1a), c.x + r * 3.0f, c.y, true));
            g.fillEllipse (juce::Rectangle<float> (r * 6.0f, r * 6.0f).withCentre (c));
        }
        g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xffff6a50) : juce::Colour (0xff6a1a14), c.x - r * 0.3f, c.y - r * 0.4f,
                                                 on ? juce::Colour (0xffc0100a) : juce::Colour (0xff2a0806), c.x + r, c.y + r, true));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        // the faceted jewel
        g.setColour (juce::Colours::white.withAlpha (on ? 0.55f : 0.18f));
        g.fillRoundedRectangle (juce::Rectangle<float> (r * 0.9f, r * 0.35f).withCentre (c.translated (-r * 0.05f, -r * 0.05f)), r * 0.1f);
        g.setColour (printWhite);
        g.setFont (boldFont (m.u (12.0f)));
        centredText (g, "ON", m.p (0.637f, 0.44f), m.u (50.0f), m.u (14.0f));
        centredText (g, "OFF", m.p (0.637f, 0.725f), m.u (50.0f), m.u (14.0f));
    }

    // red name plate: which pad this is
    {
        auto plate = juce::Rectangle<float> (m.u (96.0f), m.u (26.0f)).withCentre (m.p (0.5f, 0.86f));
        g.setColour (juce::Colour (0xff9a9a96));
        g.fillRect (plate.expanded (m.u (2.0f)));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe5432c), plate.getX(), plate.getY(), juce::Colour (0xffb82a18), plate.getX(), plate.getBottom(), false));
        g.fillRect (plate);
        g.setColour (juce::Colour (0xff1a0a06));
        g.setFont (boldFont (m.u (17.0f)).withExtraKerningFactor (0.08f));
        g.drawText ("PAD " + padLabel (juce::jlimit (0, numPads - 1, processor.selectedPad.load())), plate, juce::Justification::centred, false);
    }
}
} // namespace rz
