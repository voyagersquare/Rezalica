#pragma once

#include "Sequencer.h"
#include "LookAndFeel.h"

namespace rz
{
/** CLICK (metronome on / off, right-click: volume) + COUNT-IN (bars of clicks before REC starts). */
class MetronomeControls : public juce::Component,
                          private juce::Timer
{
public:
    explicit MetronomeControls (Sequencer& s) : seq (s)
    {
        clickButton.setClickingTogglesState (true);
        clickButton.setWantsKeyboardFocus (false);
        clickButton.setColour (juce::TextButton::buttonOnColourId, colours::accent2);
        clickButton.setTooltip ("Metronome: a click on every beat while playing (right-click: click volume)");
        clickButton.onClick = [this] { seq.metronome = clickButton.getToggleState(); };
        clickButton.addMouseListener (this, false);
        addAndMakeVisible (clickButton);

        countInBox.addItem ("No count-in", 1);
        countInBox.addItem ("Count-in 1 bar", 2);
        countInBox.addItem ("Count-in 2 bars", 3);
        countInBox.setWantsKeyboardFocus (false);
        countInBox.setTooltip ("With REC on, PLAY first counts in with clicks, then the pattern starts and records");
        countInBox.onChange = [this] { seq.countInBars = countInBox.getSelectedId() - 1; };
        addAndMakeVisible (countInBox);
        timerCallback();
        startTimerHz (8);
    }

    /** false: only the count-in menu (the CLICK key sits elsewhere, e.g. next to the transport). */
    void setShowClickButton (bool show) { clickButton.setVisible (show); resized(); }

    void resized() override
    {
        auto b = getLocalBounds();
        if (clickButton.isVisible())
        {
            clickButton.setBounds (b.removeFromLeft (64));
            b.removeFromLeft (4);
        }
        countInBox.setBounds (b);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != &clickButton || ! e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        m.addSectionHeader ("Click volume");
        for (int i = 1; i <= 10; ++i)
            m.addItem (i, juce::String (i * 10) + " %", true, juce::roundToInt (seq.metronomeLevel.load() * 10.0f) == i);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (clickButton), [this] (int r)
        {
            if (r > 0) seq.metronomeLevel = (float) r / 10.0f;
        });
    }

private:
    void timerCallback() override
    {
        clickButton.setToggleState (seq.metronome.load(), juce::dontSendNotification);
        countInBox.setSelectedId (juce::jlimit (0, 2, seq.countInBars.load()) + 1, juce::dontSendNotification);
        const bool counting = seq.countingIn;
        clickButton.setButtonText (counting ? "COUNT" : "CLICK");
    }

    Sequencer& seq;
    juce::TextButton clickButton { "CLICK" };
    juce::ComboBox countInBox;
};
} // namespace rz
