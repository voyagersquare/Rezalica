#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace rz
{
/** One of the 16 performance pads. Click to play, right-click for a menu, drop a file to load it. */
class PadButton : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    PadButton (RezalicaProcessor& p, int padIndex, const juce::String& keyHint)
        : processor (p), index (padIndex), key (keyHint)
    {
        setWantsKeyboardFocus (false);
    }

    std::function<void (int)> onSelect;
    std::function<void (int)> onLoadRequest;
    std::function<void (int)> onClear;
    std::function<void (int, int)> onChop; // pad, mode

    void setSelected (bool s)                { if (s != selected) { selected = s; repaint(); } }
    void setSampleName (const juce::String& n) { if (n != name) { name = n; repaint(); } }
    void flash()                             { flashLevel = 1.0f; repaint(); }

    void decay()
    {
        if (flashLevel > 0.0f)
        {
            flashLevel = juce::jmax (0.0f, flashLevel - 0.12f);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace colours;
        auto b = getLocalBounds().toFloat().reduced (3.0f);

        auto base = name.isEmpty() ? juce::Colour (0xff2a2c32) : juce::Colour (0xff3a3d45);
        if (held) base = base.brighter (0.15f);
        g.setColour (base.interpolatedWith (accent, flashLevel * 0.85f));
        g.fillRoundedRectangle (b, 7.0f);

        g.setColour (selected ? accent : outline);
        g.drawRoundedRectangle (b, 7.0f, selected ? 2.5f : 1.0f);

        auto inner = b.reduced (8.0f, 6.0f);
        g.setColour (flashLevel > 0.5f ? juce::Colours::black : text);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText (juce::String (index + 1), inner.removeFromTop (26.0f), juce::Justification::topLeft);

        g.setFont (juce::FontOptions (11.0f));
        g.setColour (flashLevel > 0.5f ? juce::Colours::black : dim);
        g.drawText (key.toUpperCase(), b.reduced (8.0f, 6.0f), juce::Justification::topRight);

        g.setColour (flashLevel > 0.5f ? juce::Colours::black : (name.isEmpty() ? dim : text));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (name.isEmpty() ? juce::String ("empty") : name, inner.toNearestInt(), juce::Justification::bottomLeft, 2);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onSelect) onSelect (index);

        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }

        held = true;
        // clicking higher on the pad plays louder
        const float vel = juce::jlimit (0.2f, 1.0f, 1.1f - (float) e.y / (float) juce::jmax (1, getHeight()) * 0.6f);
        processor.triggerPadFromUi (index, vel, true);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (held)
        {
            held = false;
            processor.triggerPadFromUi (index, 0.0f, false);
            repaint();
        }
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() > 0 && processor.getFormatManager().findFormatForFileExtension (juce::File (files[0]).getFileExtension()) != nullptr;
    }

    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; flashLevel = 0.4f; repaint(); }
    void fileDragExit (const juce::StringArray&) override            { dragOver = false; flashLevel = 0.0f; repaint(); }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false;
        flashLevel = 0.0f;
        processor.loadFileToPad (index, juce::File (files[0]));
        if (onSelect) onSelect (index);
    }

private:
    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Load sample...");
        m.addItem (2, "Clear pad", name.isNotEmpty());
        m.addSeparator();
        juce::PopupMenu chop;
        chop.addItem (10, "4 equal slices");
        chop.addItem (11, "8 equal slices");
        chop.addItem (12, "16 equal slices");
        chop.addItem (13, "By transients");
        m.addSubMenu ("Chop this sample to pads", chop, name.isNotEmpty());

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r == 1 && onLoadRequest) onLoadRequest (index);
            if (r == 2 && onClear) onClear (index);
            if (r >= 10 && r <= 13 && onChop)
                onChop (index, r == 10 ? 4 : r == 11 ? 8 : r == 12 ? 16 : 0);
        });
    }

    RezalicaProcessor& processor;
    const int index;
    const juce::String key;
    juce::String name;
    bool selected = false, held = false, dragOver = false;
    float flashLevel = 0.0f;
};
} // namespace rz
