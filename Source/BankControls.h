#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace rz
{
/** Bank volume curve: slider position 0..1 <-> dB (-60 = off ... +6), 0 dB sits at about 70 %. */
inline float bankPosToDb (float p)  { p = juce::jlimit (0.0f, 1.0f, p); return 6.0f - 66.0f * (1.0f - p) * (1.0f - p); }
inline float bankDbToPos (float db) { return 1.0f - std::sqrt (juce::jlimit (0.0f, 66.0f, 6.0f - db) / 66.0f); }

/** A bank button (A-D): click = select, Ctrl+click = volume slider, right-click = bank menu.
    A thin yellow line at the bottom shows the bank volume. */
class BankButton : public juce::TextButton
{
public:
    std::function<void()> onCtrlClick, onRightClick;
    RezalicaProcessor* processor = nullptr;
    int bank = 0;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        if (e.mods.isCommandDown()) { if (onCtrlClick) onCtrlClick(); return; }
        juce::TextButton::mouseDown (e);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        juce::TextButton::paintButton (g, highlighted, down);
        if (processor == nullptr) return;
        const float pos = bankDbToPos (processor->getBankGainDb (bank));
        auto r = getLocalBounds().toFloat().reduced (5.0f, 0.0f);
        g.setColour (juce::Colour (0xffffc94a));
        g.fillRect (r.getX(), r.getBottom() - 3.0f, r.getWidth() * pos, 2.0f);
    }
};

/** The pop-up volume slider of one bank (opened with Ctrl+click on the bank button). */
class BankVolumePopup : public juce::Component
{
public:
    BankVolumePopup (RezalicaProcessor& p, int bankIndex, std::function<void()> changed)
        : processor (p), bank (bankIndex), onChange (std::move (changed))
    {
        setSize (96, 270);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace colours;
        g.fillAll (panel);
        g.setColour (text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("BANK " + juce::String::charToString ((juce::juce_wchar) ('A' + bank)), getLocalBounds().removeFromTop (24), juce::Justification::centred);

        auto t = track();
        g.setColour (juce::Colour (0xff141518));
        g.fillRoundedRectangle (t, 5.0f);

        const float db = processor.getBankGainDb (bank);
        const float y = yForPos (bankDbToPos (db));
        g.setColour (accent2.withAlpha (0.35f));
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (t.getX(), y, t.getRight(), t.getBottom()), 5.0f);

        // scale
        g.setFont (juce::FontOptions (9.0f));
        for (float mark : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            const float my = yForPos (bankDbToPos (mark));
            g.setColour (std::abs (mark) < 0.01f ? text : dim);
            g.drawHorizontalLine ((int) my, t.getRight() + 2.0f, t.getRight() + 8.0f);
            g.drawText (mark > 0.0f ? "+" + juce::String ((int) mark) : juce::String ((int) mark),
                        juce::Rectangle<float> (t.getRight() + 10.0f, my - 6.0f, 30.0f, 12.0f), juce::Justification::left);
        }

        // the current volume: yellow line
        g.setColour (juce::Colour (0xffffc94a));
        g.fillRoundedRectangle (t.getX() - 6.0f, y - 2.0f, t.getWidth() + 12.0f, 4.0f, 2.0f);

        g.setColour (text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (db <= -59.9f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB",
                    getLocalBounds().removeFromBottom (24), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        processor.pushUndo ("Bank volume");
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto t = track();
        const float pos = 1.0f - ((float) e.y - t.getY()) / t.getHeight();
        set (bankPosToDb (pos));
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        processor.pushUndo ("Bank volume");
        set (0.0f);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        set (processor.getBankGainDb (bank) + (w.deltaY > 0.0f ? 0.5f : -0.5f));
    }

private:
    juce::Rectangle<float> track() const { return { 22.0f, 32.0f, 16.0f, (float) getHeight() - 32.0f - 30.0f }; }
    float yForPos (float pos) const { auto t = track(); return t.getBottom() - pos * t.getHeight(); }

    void set (float db)
    {
        processor.setBankGainDb (bank, db);
        repaint();
        if (onChange) onChange();
    }

    RezalicaProcessor& processor;
    const int bank;
    std::function<void()> onChange;
};
} // namespace rz
