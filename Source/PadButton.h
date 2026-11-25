#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "Mixer.h"

namespace rz
{
/** One of the 16 performance pads. Click to play, right-click for a menu, drop a file to load it. */
class PadButton : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  public juce::DragAndDropTarget
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
    std::function<void (int)> onCopy, onPaste;
    // multi-select: Shift+click (range) / Ctrl+click (one more pad); returns true when the click only selected
    std::function<bool (int, juce::ModifierKeys)> onMultiSelect;
    std::function<int()> getMultiCount;
    std::function<void()> onClearSelected, onDeselect;
    /** Choke group for this pad, or for all selected pads when several are selected (0 = none). */
    std::function<void (int pad, int group)> onChokeGroup;
    /** VST instruments: load one onto the pad (plug-in identifier), open its window, remove it. */
    std::function<void (int pad, const juce::String& identifier)> onInstrument;
    std::function<void (int pad)> onOpenInstrument, onRemoveInstrument;
    void setMultiSelected (bool m)           { if (m != multiSelected) { multiSelected = m; repaint(); } }

    void setIndex (int i)                    { if (i != index) { index = i; flashLevel = 0.0f; held = false; repaint(); } }
    int getIndex() const noexcept            { return index; }
    void setSelected (bool s)                { if (s != selected) { selected = s; repaint(); } }
    void setSampleName (const juce::String& n) { if (n != name) { name = n; repaint(); } }
    void setHasFx (bool f)                   { if (f != hasFx) { hasFx = f; repaint(); } }
    void setBusBadge (const juce::String& b) { if (b != busBadge) { busBadge = b; repaint(); } }
    /** Sampling: the pad blinks while REC waits for the pad that should get the take. */
    void setArmed (bool a)                   { if (a != armed) { armed = a; repaint(); } }
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

        // cream rubber pad: drop shadow, soft dome highlight, lights up warm when hit
        auto base = name.isEmpty() ? padCream.darker (0.08f) : padCream;
        if (held) base = base.darker (0.12f);
        base = base.interpolatedWith (juce::Colour (0xffff6a3d), flashLevel * 0.75f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (b.translated (held ? 0.5f : 1.5f, held ? 1.0f : 4.0f), 6.0f);
        auto body = held ? b.translated (0.0f, 2.5f) : b;
        g.setGradientFill (juce::ColourGradient (base.brighter (0.2f), body.getX(), body.getY(),
                                                 base.darker (0.22f), body.getX(), body.getBottom(), false));
        g.fillRoundedRectangle (body, 6.0f);
        // domed rubber: a soft highlight on the upper half, darker rim at the bottom
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (held ? 0.1f : 0.35f), body.getCentreX(), body.getY(),
                                                 juce::Colours::transparentWhite, body.getCentreX(), body.getCentreY(), false));
        g.fillRoundedRectangle (body.reduced (3.0f).withHeight (body.getHeight() * 0.5f), 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.drawHorizontalLine ((int) body.getBottom() - 2, body.getX() + 6.0f, body.getRight() - 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawRoundedRectangle (body.reduced (1.5f), 5.0f, 1.0f);
        if (multiSelected)
        {
            g.setColour (juce::Colour (0xffffc94a).withAlpha (0.22f));
            g.fillRoundedRectangle (body, 6.0f);
        }
        if (armed)
        {
            g.setColour (juce::Colour (0xffe8423f).withAlpha (0.45f));
            g.fillRoundedRectangle (body, 6.0f);
            g.setColour (juce::Colour (0xffe8423f));
            g.drawRoundedRectangle (body.reduced (1.0f), 6.0f, 3.0f);
        }
        g.setColour (selected ? accent : multiSelected ? juce::Colour (0xffe0a020) : juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (body, 6.0f, selected || multiSelected ? 2.5f : 1.0f);
        b = body;

        auto inner = b.reduced (8.0f, 6.0f);
        g.setColour (ink);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText (padLabel (index), inner.removeFromTop (26.0f), juce::Justification::topLeft);

        g.setFont (juce::FontOptions (11.0f));
        g.setColour (ink.withAlpha (0.45f));
        g.drawText (key.toUpperCase(), b.reduced (8.0f, 6.0f), juce::Justification::topRight);

        if (const int choke = processor.getEngine().getPad (index).chokeGroup.load(); choke > 0)
        {
            // its choke group
            auto badge = b.reduced (8.0f, 6.0f).removeFromBottom (14.0f).removeFromRight (busBadge.isNotEmpty() ? 72.0f : 30.0f).removeFromLeft (30.0f);
            g.setColour (juce::Colour (0xffe0a020));
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("CH " + juce::String (choke), badge, juce::Justification::centred);
            if (busBadge.isEmpty()) inner.removeFromRight (32.0f);
        }
        if (busBadge.isNotEmpty())
        {
            // the mixer bus this pad plays into (nothing shown for MASTER)
            auto badge = b.reduced (8.0f, 6.0f).removeFromBottom (14.0f).removeFromRight (34.0f);
            g.setColour (juce::Colour (0xff4fa3ff));
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("BUS " + busBadge, badge, juce::Justification::centred);
            inner.removeFromRight (36.0f);
        }
        if (processor.padHasInstrument (index))
        {
            // a VST instrument plays this pad
            auto badge = b.reduced (8.0f, 6.0f).removeFromBottom (14.0f).removeFromRight (hasFx ? 58.0f : 32.0f).removeFromLeft (32.0f);
            g.setColour (juce::Colour (0xff7a4fd6));
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("VSTi", badge, juce::Justification::centred);
            if (! hasFx) inner.removeFromRight (34.0f);
        }
        if (hasFx)
        {
            auto badge = b.reduced (8.0f, 6.0f).removeFromBottom (14.0f).removeFromRight (22.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("FX", badge, juce::Justification::centred);
            inner.removeFromRight (24.0f);
        }

        g.setColour (name.isEmpty() ? ink.withAlpha (0.35f) : ink.withAlpha (0.8f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (name.isEmpty() ? juce::String ("empty") : name, inner.toNearestInt(), juce::Justification::bottomLeft, 2);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        heldIndex = index;

        if (e.mods.isPopupMenu())
        {
            // with several pads selected, the menu works on the selection: keep it
            if (! (getMultiCount && getMultiCount() > 1) && onSelect) onSelect (index);
            showMenu();
            return;
        }

        if ((e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isCommandDown()) && onMultiSelect && onMultiSelect (index, e.mods))
            return; // selecting, not playing

        if (onSelect) onSelect (index);

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
            processor.triggerPadFromUi (heldIndex, 0.0f, false);
            repaint();
        }
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() > 0 && processor.getFormatManager().findFormatForFileExtension (juce::File (files[0]).getFileExtension()) != nullptr;
    }

    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; flashLevel = 0.4f; repaint(); }
    void fileDragExit (const juce::StringArray&) override            { dragOver = false; flashLevel = 0.0f; repaint(); }
    // drag & drop from Rezalica's sample browser (description "rzfile:<path>")
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        const auto s = d.description.toString();
        return s.startsWith ("rzfile:") || s.startsWith ("rzinstrument:");
    }
    void itemDragEnter (const SourceDetails&) override { fileDragEnter ({}, 0, 0); }
    void itemDragExit (const SourceDetails&) override { fileDragExit ({}); }
    void itemDropped (const SourceDetails& d) override
    {
        const auto s = d.description.toString();
        if (s.startsWith ("rzinstrument:"))
        {
            dragOver = false; flashLevel = 0.0f; repaint();
            if (onInstrument) onInstrument (index, s.fromFirstOccurrenceOf ("rzinstrument:", false, false));
            return;
        }
        filesDropped (juce::StringArray (s.fromFirstOccurrenceOf ("rzfile:", false, false)), 0, 0);
    }


    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false;
        flashLevel = 0.0f;
        processor.pushUndo ("Load sample");
        processor.loadFileToPad (index, juce::File (files[0]));
        if (onSelect) onSelect (index);
    }

private:
    void showMenu()
    {
        juce::PopupMenu m;
        const int multi = getMultiCount ? getMultiCount() : 0;
        if (multi > 1)
        {
            m.addSectionHeader (juce::String (multi) + " pads selected");
            m.addItem (30, "Clear all selected pads (" + juce::String (multi) + ")");
            m.addItem (31, "Deselect");
            m.addSeparator();
        }
        {
            // VST instruments
            const bool hasInst = processor.padHasInstrument (index);
            juce::PopupMenu inst;
            instrumentChoices = processor.pluginHost->getInstruments();
            for (int i = 0; i < instrumentChoices.size(); ++i)
                inst.addItem (1000 + i, instrumentChoices[i].name + "   (" + instrumentChoices[i].manufacturerName + ")");
            if (instrumentChoices.isEmpty())
                inst.addItem (999, "No instruments found (BROWSER > PLUGINS > SCAN)", false);
            m.addSubMenu (hasInst ? "Replace VST instrument" : "Load VST instrument", inst);
            if (hasInst)
            {
                m.addItem (40, "Open instrument window (" + processor.getInstrumentName (index) + ")");
                m.addItem (41, "Remove instrument");
            }
            m.addSeparator();
        }
        m.addItem (1, "Load sample...");
        m.addItem (2, "Clear pad", name.isNotEmpty());
        m.addSeparator();
        m.addItem (20, "Copy pad", name.isNotEmpty());
        m.addItem (21, "Paste pad", processor.canPastePad());
        m.addSeparator();
        juce::PopupMenu chop;
        chop.addItem (9, "2 equal slices");
        chop.addItem (10, "4 equal slices");
        chop.addItem (11, "8 equal slices");
        chop.addItem (12, "16 equal slices");
        chop.addItem (13, "By transients");
        m.addSubMenu ("Chop this sample to pads", chop, name.isNotEmpty());
        m.addSeparator();
        {
            // mixer routing: by default a pad follows its bank (which goes to MASTER)
            juce::PopupMenu send;
            const int own = processor.getEngine().getPad (index).outputBus.load();
            const int bankBus = processor.getEngine().bankBus[(size_t) (index / padsPerBank)].load();
            send.addItem (60, "Same as bank (" + busName (bankBus) + ")", true, own < 0);
            for (int bi = 0; bi < numBuses; ++bi)
                send.addItem (61 + bi, busName (bi), true, own == bi);
            m.addSubMenu ("Send to", send);
        }
        {
            // choke group: for this pad, or for all selected pads at once
            juce::PopupMenu choke;
            const int current = processor.getEngine().getPad (index).chokeGroup.load();
            choke.addItem (80, "No choke", true, current == 0);
            for (int g = 1; g <= 8; ++g)
                choke.addItem (80 + g, "Choke " + juce::String (g), true, current == g);
            m.addSubMenu (multi > 1 ? "Choke group (" + juce::String (multi) + " selected pads)" : juce::String ("Choke group"), choke);
        }
        {
            // which note of the MIDI controller plays this pad
            m.addSeparator();
            const int note = processor.noteForPad (index);
            const bool learning = processor.learnPadNote.load() == index;
            m.addSectionHeader ("MIDI note: " + (note >= 0 ? juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " (" + juce::String (note) + ")"
                                                           : juce::String ("none")));
            m.addItem (70, learning ? "Waiting for a note ... (cancel)" : "Learn MIDI note (hit a pad / key on the controller)");
            m.addItem (71, "Forget MIDI note", note >= 0);
        }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r >= 80 && r <= 88 && onChokeGroup)
                onChokeGroup (index, r - 80);
            if (r == 70)
            {
                // the next note that comes in plays this pad (the pad layout becomes "custom")
                processor.learnPadNote = processor.learnPadNote.load() == index ? -1 : index;
                repaint();
            }
            if (r == 71)
            {
                auto map = processor.getPadNoteMap();
                for (int n = 0; n < 128; ++n)
                    if (processor.padForNote (n) == index) map[(size_t) n] = -1;
                processor.setPadNoteMap (map, RezalicaProcessor::layoutCustom);
                ++processor.padNoteMapChanged;
            }
            if (r >= 60 && r <= 60 + numBuses)
            {
                processor.pushUndo ("Send to bus");
                processor.getEngine().getPad (index).outputBus = r == 60 ? -1 : r - 61;
                const int bus = processor.getEngine().busForPad (index);
                setBusBadge (bus > 0 ? juce::String::charToString ((juce::juce_wchar) ('A' + bus - 1)) : juce::String());
            }
            if (r >= 1000 && r < 1000 + instrumentChoices.size() && onInstrument)
                onInstrument (index, instrumentChoices[r - 1000].createIdentifierString());
            if (r == 40 && onOpenInstrument) onOpenInstrument (index);
            if (r == 41 && onRemoveInstrument) onRemoveInstrument (index);
            if (r == 30 && onClearSelected) onClearSelected();
            if (r == 31 && onDeselect) onDeselect();
            if (r == 1 && onLoadRequest) onLoadRequest (index);
            if (r == 2 && onClear) onClear (index);
            if (r == 20 && onCopy) onCopy (index);
            if (r == 21 && onPaste) onPaste (index);
            if (r >= 9 && r <= 13 && onChop)
                onChop (index, r == 9 ? 2 : r == 10 ? 4 : r == 11 ? 8 : r == 12 ? 16 : 0);
        });
    }

    RezalicaProcessor& processor;
    int index;
    const juce::String key;
    juce::String name;
    int heldIndex = 0;
    bool selected = false, held = false, dragOver = false, hasFx = false, multiSelected = false;
    juce::String busBadge;
    juce::Array<juce::PluginDescription> instrumentChoices;
    bool armed = false;
    float flashLevel = 0.0f;
};
} // namespace rz
