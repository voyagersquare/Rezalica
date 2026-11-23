#include "MidiControl.h"
#include "AppSettings.h"
#include "PadButton.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace rz
{
namespace
{
    const juce::String learnPrefix ("rz.learn.");

    juce::String describe (const juce::String& key)
    {
        auto parts = juce::StringArray::fromTokens (key, ":", {});
        if (parts[0] == "cc" && parts.size() == 3) return "CC " + parts[2] + " ch " + parts[1];
        if (parts[0] == "note" && parts.size() == 2) return "Note " + juce::MidiMessage::getMidiNoteName (parts[1].getIntValue(), true, true, 3);
        return {};
    }

    juce::String niceName (const juce::String& id)
    {
        static const std::map<juce::String, juce::String> names {
            { "deck.jog", "DECK jog wheel (scratch)" }, { "deck.cut", "DECK cut (fader)" }, { "tempo", "Tempo (BPM)" } };
        if (auto it = names.find (id); it != names.end()) return it->second;
        return id.replaceCharacter ('.', ' ').toUpperCase();
    }
}

//==============================================================================
/** MIDI Learn: covers the editor, shows every learnable control; click one, then move a control on the controller. */
class MidiLearnOverlay : public juce::Component
{
public:
    explicit MidiLearnOverlay (MidiControl& o) : owner (o) { setInterceptsMouseClicks (true, false); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.18f));
        juce::Array<juce::Component*> controls;
        owner.collectControls (controls);
        for (auto* c : controls)
        {
            const auto id = c->getComponentID().fromFirstOccurrenceOf (learnPrefix, false, false);
            auto r = getLocalArea (c, c->getLocalBounds()).toFloat().expanded (2.0f);
            bool selected = id == owner.selectedId;
            auto key = owner.keyFor (id);
            if (auto* pad = dynamic_cast<PadButton*> (c))
            {
                // a pad: the note that plays it
                const int note = owner.processor.noteForPad (pad->getIndex());
                key = note >= 0 ? "Note " + juce::MidiMessage::getMidiNoteName (note, true, true, 3) : juce::String();
                selected = owner.processor.learnPadNote.load() == pad->getIndex();
                if (selected) key = {};
            }
            const auto col = selected ? juce::Colour (0xffffd23a) : key.isNotEmpty() ? juce::Colour (0xff53d769) : juce::Colour (0xff4fa3ff);
            g.setColour (col.withAlpha (selected ? 0.35f : 0.22f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (col);
            g.drawRoundedRectangle (r, 4.0f, selected ? 2.5f : 1.5f);
            if (key.isNotEmpty() || selected)
            {
                const auto text = selected && key.isEmpty() ? juce::String (dynamic_cast<PadButton*> (c) != nullptr ? "hit a pad ..." : "move a control ...") : key;
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 8.0f;
                auto tag = juce::Rectangle<float> (r.getX(), r.getY() - 13.0f, tw, 13.0f);
                g.setColour (juce::Colours::black.withAlpha (0.8f));
                g.fillRoundedRectangle (tag, 3.0f);
                g.setColour (col);
                g.drawText (text, tag, juce::Justification::centred);
            }
        }
        // the hint along the bottom
        auto hint = getLocalBounds().removeFromBottom (26).toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillRect (hint);
        g.setColour (juce::Colour (0xffffd23a));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (owner.getWizard() != MidiControl::wizardNone
                        ? "SET-UP:  " + owner.wizardText() + "   Click anywhere else to skip it, ESC to stop."
                        : juce::String ("MIDI LEARN:  click a knob, fader, button or pad (blue), then move / press / hit it on your MIDI controller.   "
                                        "Right-click: remove.   Click MIDI LEARN (or press ESC) when you are done."),
                    hint.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // the MIDI key under the overlay ends MIDI Learn
        if (auto* exit = owner.exitButton; exit != nullptr && getLocalArea (exit, exit->getLocalBounds()).contains (e.getPosition()))
        {
            owner.setLearnMode (false);
            return;
        }
        // the smallest learnable control under the mouse
        juce::Array<juce::Component*> controls;
        owner.collectControls (controls);
        juce::Component* hit = nullptr;
        for (auto* c : controls)
            if (getLocalArea (c, c->getLocalBounds()).expanded (2).contains (e.getPosition()))
                if (hit == nullptr || c->getWidth() * c->getHeight() < hit->getWidth() * hit->getHeight())
                    hit = c;
        if (owner.getWizard() != MidiControl::wizardNone)
        {
            owner.skipWizardStep(); // during the set-up a click skips the current control
            repaint();
            return;
        }
        if (hit == nullptr) { owner.selectedId = {}; owner.processor.learnPadNote = -1; repaint(); return; }
        const auto id = hit->getComponentID().fromFirstOccurrenceOf (learnPrefix, false, false);
        if (auto* pad = dynamic_cast<PadButton*> (hit))
        {
            // a pad: learn (or with right-click forget) the note that plays it
            owner.selectedId = {};
            if (e.mods.isPopupMenu())
            {
                auto map = owner.processor.getPadNoteMap();
                for (int n = 0; n < 128; ++n)
                    if (owner.processor.padForNote (n) == pad->getIndex()) map[(size_t) n] = -1;
                owner.processor.setPadNoteMap (map, RezalicaProcessor::layoutCustom);
                ++owner.processor.padNoteMapChanged;
            }
            else
                owner.learnPadNote (pad->getIndex());
            repaint();
            return;
        }

        if (id == "deck.jog" && ! e.mods.isPopupMenu())
        {
            // the record can be the jog wheel or the cut fader
            juce::PopupMenu m;
            m.addItem (1, "Jog wheel (scratch)  " + owner.keyFor ("deck.jog"));
            m.addItem (2, "Cut (fader)  " + owner.keyFor ("deck.cut"));
            juce::Component::SafePointer<MidiLearnOverlay> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
            {
                if (safe == nullptr || r == 0) return;
                safe->owner.selectedId = r == 1 ? "deck.jog" : "deck.cut";
                safe->repaint();
            });
            return;
        }
        if (e.mods.isPopupMenu())
        {
            owner.forget (id);
            if (id == "deck.jog") owner.forget ("deck.cut");
            owner.selectedId = {};
        }
        else
            owner.selectedId = id;
        repaint();
    }

    MidiControl& owner;
};

//==============================================================================
MidiControl::MidiControl (RezalicaProcessor& p, juce::Component& r, bool isStandalone)
    : processor (p), root (r), standalone (isStandalone)
{
    loadCurrent();
    if (standalone)
    {
        const auto profiles = juce::ValueTree::fromXml (AppSettings::get().getValue ("midiPresets"));
        bool loaded = false;
        for (auto& d : juce::MidiInput::getAvailableDevices())
        {
            knownInputs.add (d.identifier);
            // a controller with its own preset: that set-up is loaded
            if (! loaded)
                if (auto pr = profiles.getChildWithProperty ("device", d.name); pr.isValid())
                    loaded = loadPreset (pr.getProperty ("name").toString());
        }
    }
    startTimerHz (60); // the jog wheel wants quick answers
}

MidiControl::~MidiControl()
{
    stopTimer();
    processor.midiLearnListening = false;
    if (overlay != nullptr && overlay->getParentComponent() != nullptr)
        overlay->getParentComponent()->removeChildComponent (overlay.get());
}

juce::Component* MidiControl::findControl (const juce::String& id) const
{
    // (also hidden ones: the mixer's faders keep working while STEP SEQ / DECK ... cover them;
    //  a visible one wins when there are two)
    const auto want = learnPrefix + id;
    juce::Component* hidden = nullptr;
    std::function<juce::Component* (juce::Component&)> find = [&] (juce::Component& c) -> juce::Component*
    {
        for (auto* ch : c.getChildren())
        {
            if (ch->getComponentID() == want)
            {
                if (ch->isShowing() || ch->isVisible()) return ch;
                if (hidden == nullptr) hidden = ch;
            }
            if (auto* f = find (*ch)) return f;
        }
        return nullptr;
    };
    if (auto* f = find (root)) return f;
    return hidden;
}

void MidiControl::collectControls (juce::Array<juce::Component*>& out) const
{
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* ch : c.getChildren())
        {
            if (ch == overlay.get() || ! ch->isVisible()) continue;
            if (ch->getComponentID().startsWith (learnPrefix)) out.add (ch);
            walk (*ch);
        }
    };
    walk (root);
}

juce::String MidiControl::keyFor (const juce::String& id) const
{
    if (auto it = mappings.find (id); it != mappings.end()) return describe (it->second);
    return {};
}

void MidiControl::forget (const juce::String& id)
{
    mappings.erase (id);
    updateNoteControls();
    saveCurrent();
}

void MidiControl::clearAll()
{
    mappings.clear();
    updateNoteControls();
    saveCurrent();
    if (overlay != nullptr) overlay->repaint();
}

void MidiControl::setLearnMode (bool on)
{
    learnMode = on;
    selectedId = {};
    if (! on) { wizard = wizardNone; processor.learnPadNote = -1; }
    processor.midiLearnListening = false;
    if (on)
    {
        if (overlay == nullptr) overlay = std::make_unique<MidiLearnOverlay> (*this);
        root.addAndMakeVisible (*overlay);
        overlay->setBounds (root.getLocalBounds());
        overlay->toFront (false);
    }
    else if (overlay != nullptr)
        root.removeChildComponent (overlay.get());
    if (onLearnModeChanged) onLearnModeChanged (on);
}

void MidiControl::updateNoteControls()
{
    // notes learned for buttons do not play pads
    for (auto& n : processor.noteIsControl) n = false;
    for (auto& [id, key] : mappings)
        if (key.startsWith ("note:"))
            processor.noteIsControl[(size_t) juce::jlimit (0, 127, key.fromFirstOccurrenceOf ("note:", false, false).getIntValue())] = true;
}

//==============================================================================
void MidiControl::handle (const RezalicaProcessor::MidiControlEvent& e)
{
    const auto key = e.kind == 0 ? "cc:" + juce::String (e.channel) + ":" + juce::String (e.number)
                                 : "note:" + juce::String (e.number);

    // MIDI Learn: the selected control takes this CC / note
    if (learnMode && selectedId.isNotEmpty() && e.kind != 2)
    {
        for (auto it = mappings.begin(); it != mappings.end();)
            it = (it->second == key) ? mappings.erase (it) : std::next (it);
        mappings[selectedId] = key;
        if (onMessage) onMessage ("MIDI Learn: " + niceName (selectedId) + " = " + describe (key));
        selectedId = {};
        processor.midiLearnListening = false;
        updateNoteControls();
        saveCurrent();
        if (wizard != wizardNone) nextWizardStep();
        if (overlay != nullptr) overlay->repaint();
        return;
    }

    for (auto& [id, k] : mappings)
    {
        if (k != key) continue;
        auto& deck = processor.getDeck();
        const int v = e.value;
        if (id == "deck.jog")
        {
            if (e.kind != 0) continue;
            // relative jog wheels: either centred on 64 or two's complement (1..63 forward, 65..127 back)
            const int delta = (v > 48 && v < 80) ? v - 64 : (v < 64 ? v : v - 128);
            if (delta == 0) continue;
            if (! deck.touched.load()) { deck.handOffset = 0.0; deck.touched = true; }
            deck.handOffset = deck.handOffset.load() + delta * deck.samplesPerTurn() / 256.0;
            lastJogMs = juce::Time::getMillisecondCounterHiRes();
        }
        else if (id == "deck.cut")
            deck.cut = e.kind == 0 ? v < 64 : e.kind == 1;
        else if (id == "tempo")
        {
            if (e.kind == 0) processor.getSequencer().bpm = (float) std::round (60.0 + v / 127.0 * 120.0);
        }
        else if (auto* c = findControl (id))
        {
            if (auto* s = dynamic_cast<juce::Slider*> (c))
            {
                if (e.kind == 0 && s->isEnabled())
                    s->setValue (s->proportionOfLengthToValue (v / 127.0), juce::sendNotificationSync);
            }
            else if (auto* b = dynamic_cast<juce::Button*> (c))
            {
                // a button: a note-on, or a CC going over the middle
                const int before = lastValue.count (id) > 0 ? lastValue[id] : 0;
                lastValue[id] = v;
                const bool press = e.kind == 1 || (e.kind == 0 && v >= 64 && before < 64);
                if (press && b->isEnabled()) b->triggerClick();
            }
        }
    }
}

void MidiControl::timerCallback()
{
    // incoming CC / notes from the audio thread
    const auto r = processor.controlFifo.read (processor.controlFifo.getNumReady());
    r.forEach ([this] (int i) { handle (processor.controlEvents[(size_t) i]); });

    // the jog wheel stopped moving: the hand lets go of the record
    auto& deck = processor.getDeck();
    if (lastJogMs > 0.0 && juce::Time::getMillisecondCounterHiRes() - lastJogMs > 90.0)
    {
        deck.touched = false;
        lastJogMs = 0.0;
    }

    // MIDI Learn waits for a control while one is selected
    processor.midiLearnListening = learnMode && selectedId.isNotEmpty();
    if (overlay != nullptr && overlay->isShowing() && overlay->getBounds() != root.getLocalBounds())
        overlay->setBounds (root.getLocalBounds());

    // a pad learned its note / the layout changed
    if (const int c = processor.padNoteMapChanged.load(); c != lastPadMapChanged)
    {
        lastPadMapChanged = c;
        if (wizard == wizardPads) nextWizardStep();
        if (overlay != nullptr) overlay->repaint();
        saveCurrent();
        if (onPadNotesChanged) onPadNotesChanged();
        if (onMessage) onMessage ("MIDI: pad notes changed (custom layout)");
    }
    if (const int lp = processor.learnPadNote.load(); lp != lastLearnPad)
    {
        lastLearnPad = lp;
        if (onPadNotesChanged) onPadNotesChanged();
    }

    if (++tick % 60 == 0 && standalone)
        checkDevices();
}

void MidiControl::checkDevices()
{
    // a controller plugged in while Rezalica runs: switch it on, and load its preset if there is one
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr) return;
    auto& dm = holder->deviceManager;
    juce::StringArray now;
    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        now.add (d.identifier);
        if (! knownInputs.contains (d.identifier))
        {
            dm.setMidiInputDeviceEnabled (d.identifier, true);
            if (onMessage) onMessage ("MIDI: " + d.name + " connected");
            for (auto& name : getPresetNames())
            {
                auto profiles = juce::ValueTree::fromXml (AppSettings::get().getValue ("midiPresets"));
                auto pr = profiles.getChildWithProperty ("name", name);
                if (pr.isValid() && pr.getProperty ("device").toString() == d.name)
                {
                    loadPreset (name);
                    if (onMessage) onMessage ("MIDI: " + d.name + " connected, preset \"" + name + "\" loaded");
                    break;
                }
            }
        }
    }
    knownInputs = now;
}

//==============================================================================
// quick set-up

void MidiControl::learnPadNote (int pad)
{
    processor.learnPadNote = pad;
    if (overlay != nullptr) overlay->repaint();
}

void MidiControl::startWizard (int kind)
{
    wizardTargets.clear();
    if (kind == wizardKnobs)
        wizardTargets = { "pad.gain", "pad.pan", "pad.pitch", "pad.attack", "pad.release", "pad.start", "pad.end" };
    else if (kind == wizardFaders)
        wizardTargets = { "busa.fader", "busb.fader", "busc.fader", "busd.fader", "master.fader" };
    else if (kind == wizardTransport)
        wizardTargets = { "transport.play", "transport.stop", "transport.rec", "transport.pause", "transport.metronome" };
    else if (kind == wizardPads)
        for (int i = 0; i < rz::padsPerBank; ++i) wizardTargets.add ("pad " + juce::String (i));
    setLearnMode (true);
    wizard = kind;
    wizardStep = -1;
    nextWizardStep();
}

void MidiControl::skipWizardStep()
{
    processor.learnPadNote = -1;
    nextWizardStep();
}

void MidiControl::nextWizardStep()
{
    if (wizard == wizardNone) return;
    ++wizardStep;
    selectedId = {};
    processor.learnPadNote = -1;
    if (wizardStep >= wizardTargets.size())
    {
        const auto done = wizard;
        wizard = wizardNone;
        if (onMessage) onMessage (done == wizardPads ? "MIDI set-up: pads learned" : "MIDI set-up: done");
        setLearnMode (false);
        return;
    }
    if (wizard == wizardPads)
        processor.learnPadNote = processor.currentBank.load() * rz::padsPerBank + wizardStep;
    else
        selectedId = wizardTargets[wizardStep];
    if (overlay != nullptr) overlay->repaint();
}

juce::String MidiControl::wizardText() const
{
    if (wizard == wizardNone || wizardStep < 0 || wizardStep >= wizardTargets.size()) return {};
    const auto step = "(" + juce::String (wizardStep + 1) + " / " + juce::String (wizardTargets.size()) + ")";
    if (wizard == wizardPads)
        return "Hit the pad on your controller that should play " + padLabel (processor.currentBank.load() * rz::padsPerBank + wizardStep) + "  " + step;
    const auto what = wizard == wizardKnobs ? "Turn the knob for " : wizard == wizardFaders ? "Move the fader for " : "Press the button for ";
    return what + niceName (wizardTargets[wizardStep]) + "  " + step;
}

void MidiControl::exportPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Export MIDI preset", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                       .getChildFile ((currentPreset.isNotEmpty() ? currentPreset : juce::String ("Rezalica MIDI")) + ".rzmidi"),
                                                   "*.rzmidi");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        juce::ValueTree p ("PRESET");
        p.setProperty ("name", f.getFileNameWithoutExtension(), nullptr);
        p.setProperty ("setup", stateToString(), nullptr);
        if (f.withFileExtension (".rzmidi").replaceWithText (p.toXmlString()) && onMessage)
            onMessage ("MIDI preset exported");
    });
}

void MidiControl::importPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Import MIDI preset", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.rzmidi");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        auto p = juce::ValueTree::fromXml (fc.getResult().loadFileAsString());
        if (! p.isValid() || ! p.hasType ("PRESET")) return;
        stateFromString (p.getProperty ("setup").toString());
        savePreset (p.getProperty ("name").toString(), {});   // also kept in the preset list
        if (onPadNotesChanged) onPadNotesChanged();
        if (onMessage) onMessage ("MIDI preset \"" + p.getProperty ("name").toString() + "\" imported");
    });
}

//==============================================================================
// saving: the current set-up, and named presets (one per controller, loaded when it is connected)

juce::String MidiControl::stateToString() const
{
    juce::ValueTree t ("MIDISETUP");
    t.setProperty ("layout", processor.getPadNoteLayout(), nullptr);
    juce::StringArray notes;
    for (auto v : processor.getPadNoteMap()) notes.add (juce::String (v));
    t.setProperty ("notes", notes.joinIntoString (","), nullptr);
    juce::StringArray learned;
    for (auto& [id, key] : mappings) learned.add (id + "=" + key);
    t.setProperty ("learn", learned.joinIntoString (";"), nullptr);
    return t.toXmlString();
}

void MidiControl::stateFromString (const juce::String& s)
{
    auto t = juce::ValueTree::fromXml (s);
    if (! t.isValid()) return;
    const int layout = (int) t.getProperty ("layout", 0);
    auto notes = juce::StringArray::fromTokens (t.getProperty ("notes").toString(), ",", {});
    if (layout == RezalicaProcessor::layoutCustom && notes.size() == 128)
    {
        std::array<int, 128> m;
        for (int i = 0; i < 128; ++i) m[(size_t) i] = notes[i].getIntValue();
        processor.setPadNoteMap (m, layout);
    }
    else
        processor.setPadNoteLayout (layout);
    mappings.clear();
    for (auto& pair : juce::StringArray::fromTokens (t.getProperty ("learn").toString(), ";", {}))
        if (pair.contains ("="))
            mappings[pair.upToFirstOccurrenceOf ("=", false, false)] = pair.fromFirstOccurrenceOf ("=", false, false);
    updateNoteControls();
}

void MidiControl::saveCurrent()
{
    AppSettings::get().setValue ("midiSetup", stateToString());
    AppSettings::get().setValue ("midiCurrentPreset", currentPreset);
    AppSettings::get().saveIfNeeded();
}

void MidiControl::loadCurrent()
{
    stateFromString (AppSettings::get().getValue ("midiSetup"));
    currentPreset = AppSettings::get().getValue ("midiCurrentPreset");
    processor.clockIn = AppSettings::get().getBoolValue ("midiClockIn", false);
    if (standalone)
        if (const auto out = AppSettings::get().getValue ("midiClockOut"); out.isNotEmpty())
            processor.setClockOutput (out);
}

juce::StringArray MidiControl::getPresetNames() const
{
    juce::StringArray names;
    auto profiles = juce::ValueTree::fromXml (AppSettings::get().getValue ("midiPresets"));
    for (auto p : profiles) names.add (p.getProperty ("name").toString());
    return names;
}

void MidiControl::savePreset (const juce::String& name, const juce::String& device)
{
    auto profiles = juce::ValueTree::fromXml (AppSettings::get().getValue ("midiPresets"));
    if (! profiles.isValid()) profiles = juce::ValueTree ("MIDIPRESETS");
    if (auto old = profiles.getChildWithProperty ("name", name); old.isValid()) profiles.removeChild (old, nullptr);
    juce::ValueTree p ("PRESET");
    p.setProperty ("name", name, nullptr);
    p.setProperty ("device", device, nullptr);
    p.setProperty ("setup", stateToString(), nullptr);
    profiles.appendChild (p, nullptr);
    AppSettings::get().setValue ("midiPresets", profiles.toXmlString());
    currentPreset = name;
    saveCurrent();
    if (onMessage) onMessage ("MIDI preset \"" + name + "\" saved");
}

bool MidiControl::loadPreset (const juce::String& name)
{
    auto profiles = juce::ValueTree::fromXml (AppSettings::get().getValue ("midiPresets"));
    auto p = profiles.getChildWithProperty ("name", name);
    if (! p.isValid()) return false;
    stateFromString (p.getProperty ("setup").toString());
    currentPreset = name;
    saveCurrent();
    if (onPadNotesChanged) onPadNotesChanged();
    if (overlay != nullptr) overlay->repaint();
    return true;
}

void MidiControl::deletePreset (const juce::String& name)
{
    auto profiles = juce::ValueTree::fromXml (AppSettings::get().getValue ("midiPresets"));
    if (auto p = profiles.getChildWithProperty ("name", name); p.isValid()) profiles.removeChild (p, nullptr);
    AppSettings::get().setValue ("midiPresets", profiles.toXmlString());
    if (currentPreset == name) currentPreset = {};
    saveCurrent();
}

//==============================================================================
void MidiControl::showMenu (juce::Component* target)
{
    juce::PopupMenu m;
    m.addItem (1, "MIDI Learn  (knobs, faders, buttons, pads, DECK)", true, learnMode);
    {
        juce::PopupMenu setup;
        setup.addItem (20, "Pads: hit the 16 pads one after another (bank " + juce::String::charToString ((juce::juce_wchar) ('A' + processor.currentBank.load())) + ")");
        setup.addItem (21, "Knobs: turn them one after another (GAIN ... END)");
        setup.addItem (22, "Faders: move them one after another (BUS A-D, MASTER)");
        setup.addItem (23, "Transport keys: PLAY, STOP, REC, PAUSE, metronome");
        setup.addSeparator();
        setup.addItem (24, "(Transport keys that send MMC work without set-up)", false);
        m.addSubMenu ("Quick set-up of a controller", setup);
    }
    m.addItem (2, "Forget all MIDI Learn", ! mappings.empty());
    m.addSeparator();

    juce::PopupMenu pads;
    const int layout = processor.getPadNoteLayout();
    pads.addItem (10, "Rezalica / Akai MPD:  C1 = A1 ... (64 pads)", true, layout == RezalicaProcessor::layoutRezalica);
    pads.addItem (11, "MPC 2000XL:  16 pads of the current bank", true, layout == RezalicaProcessor::layoutMpc);
    pads.addItem (12, "General MIDI drums:  16 pads of the current bank", true, layout == RezalicaProcessor::layoutGm);
    pads.addItem (13, "Custom (learned)", false, layout == RezalicaProcessor::layoutCustom);
    pads.addSeparator();
    pads.addItem (14, "To learn a pad's note: right-click the pad > Learn MIDI note", false);
    m.addSubMenu ("Pad notes", pads);

    juce::Array<juce::MidiDeviceInfo> inputs, outputs;
    juce::AudioDeviceManager* dm = nullptr;
    if (standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
        {
            dm = &holder->deviceManager;
            inputs = juce::MidiInput::getAvailableDevices();
            outputs = juce::MidiOutput::getAvailableDevices();
        }
    if (dm != nullptr)
    {
        juce::PopupMenu ins;
        for (int i = 0; i < inputs.size(); ++i)
            ins.addItem (100 + i, inputs[i].name, true, dm->isMidiInputDeviceEnabled (inputs[i].identifier));
        if (inputs.isEmpty()) ins.addItem (99, "(no MIDI controller connected)", false);
        m.addSubMenu ("Controllers (on / off)", ins);
    }

    juce::PopupMenu presets;
    const auto names = getPresetNames();
    for (int i = 0; i < names.size(); ++i)
        presets.addItem (300 + i, names[i], true, names[i] == currentPreset);
    if (! names.isEmpty()) presets.addSeparator();
    for (int i = 0; i < inputs.size(); ++i)
        if (dm != nullptr && dm->isMidiInputDeviceEnabled (inputs[i].identifier))
            presets.addItem (400 + i, "Save for " + inputs[i].name + "  (loads when it is connected)");
    presets.addItem (500, "Save as ...");
    presets.addSeparator();
    presets.addItem (510, "Export to a file ...");
    presets.addItem (511, "Import from a file ...");
    if (! names.isEmpty())
    {
        juce::PopupMenu del;
        for (int i = 0; i < names.size(); ++i) del.addItem (600 + i, names[i]);
        presets.addSubMenu ("Delete", del);
    }
    m.addSubMenu ("Presets" + (currentPreset.isNotEmpty() ? "  (" + currentPreset + ")" : juce::String()), presets);

    m.addSeparator();
    m.addItem (3, "Clock in: follow the tempo and START / STOP of another device", true, processor.clockIn.load());
    if (standalone)
    {
        juce::PopupMenu clock;
        clock.addItem (200, "Off", true, processor.getClockOutput().isEmpty());
        for (int i = 0; i < outputs.size(); ++i)
            clock.addItem (201 + i, outputs[i].name, true, processor.getClockOutput() == outputs[i].identifier);
        m.addSubMenu ("Clock out", clock);
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                     [this, inputs, outputs, names, dm] (int r)
    {
        if (r == 1) setLearnMode (! learnMode);
        else if (r >= 20 && r <= 23) startWizard (r == 20 ? wizardPads : r == 21 ? wizardKnobs : r == 22 ? wizardFaders : wizardTransport);
        else if (r == 510) exportPreset();
        else if (r == 511) importPreset();
        else if (r == 2) clearAll();
        else if (r >= 10 && r <= 12)
        {
            processor.setPadNoteLayout (r - 10);
            saveCurrent();
            if (onPadNotesChanged) onPadNotesChanged();
        }
        else if (r == 3)
        {
            processor.clockIn = ! processor.clockIn.load();
            AppSettings::get().setValue ("midiClockIn", processor.clockIn.load());
            AppSettings::get().saveIfNeeded();
        }
        else if (r >= 100 && r < 100 + inputs.size() && dm != nullptr)
        {
            const auto& d = inputs[r - 100];
            dm->setMidiInputDeviceEnabled (d.identifier, ! dm->isMidiInputDeviceEnabled (d.identifier));
        }
        else if (r == 200 || (r > 200 && r <= 200 + outputs.size()))
        {
            const auto id = r == 200 ? juce::String() : outputs[r - 201].identifier;
            processor.setClockOutput (id);
            AppSettings::get().setValue ("midiClockOut", id);
            AppSettings::get().saveIfNeeded();
        }
        else if (r >= 300 && r < 300 + names.size()) loadPreset (names[r - 300]);
        else if (r >= 400 && r < 400 + inputs.size()) savePreset (inputs[r - 400].name, inputs[r - 400].name);
        else if (r == 500)
        {
            auto* w = new juce::AlertWindow ("Save MIDI preset", "Name of the preset (e.g. the controller):", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", currentPreset.isNotEmpty() ? currentPreset : juce::String ("My controller"));
            w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int ok)
            {
                const auto name = w->getTextEditorContents ("name").trim();
                if (ok == 1 && name.isNotEmpty()) savePreset (name, {});
            }), true);
        }
        else if (r >= 600 && r < 600 + names.size()) deletePreset (names[r - 600]);
    });
}
} // namespace rz
