#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"

namespace rz
{
/** A button you drag out of the plugin: on drag it creates a file (MIDI or WAV)
    and hands it to the DAW / Explorer. A plain click creates the file and shows it in its folder. */
class DragExportButton : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    DragExportButton (const juce::String& label, std::function<juce::File()> makeFile)
        : text (label), createFile (std::move (makeFile))
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setWantsKeyboardFocus (false);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (busy ? colours::accent2 : isMouseOver() ? colours::panelLight.brighter (0.15f) : colours::panelLight);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (colours::accent2.withAlpha (0.8f));
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        // little "drag" grip
        g.setColour (busy ? juce::Colours::black : colours::accent2);
        for (int i = 0; i < 3; ++i)
            g.fillRect (b.getX() + 8.0f, b.getCentreY() - 5.0f + (float) i * 4.0f, 8.0f, 1.5f);

        g.setColour (busy ? juce::Colours::black : colours::text);
        g.setFont (juce::FontOptions (juce::jmin (13.0f, b.getHeight() * 0.5f), juce::Font::bold));
        g.drawText (busy ? juce::String ("RENDERING...") : text, b.withTrimmedLeft (18.0f), juce::Justification::centred);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    void mouseDown (const juce::MouseEvent&) override { dragged = false; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragged || e.getDistanceFromDragStart() < 5)
            return;
        dragged = true;
        auto f = make();
        if (f.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragged)
            if (auto f = make(); f.existsAsFile())
                f.revealToUser();
        dragged = false;
    }

private:
    juce::File make()
    {
        busy = true;
        repaint();
        auto f = createFile ? createFile() : juce::File();
        busy = false;
        repaint();
        return f;
    }

    juce::String text;
    std::function<juce::File()> createFile;
    bool dragged = false, busy = false;
};
} // namespace rz
