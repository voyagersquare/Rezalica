#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace rz
{
/** Gives a control a MIDI Learn name (knobs, faders, buttons; "deck.jog", "deck.cut" and "tempo" are special). */
inline void setLearnId (juce::Component& c, const juce::String& id) { c.setComponentID ("rz.learn." + id); }

/** MIDI controllers in the standalone app: switches new controllers on as they are plugged in, MIDI Learn
    (a control of the controller -> a knob / fader / button of Rezalica), the pad note layout and the presets
    per controller. Lives in the editor (message thread); the processor passes the CC / notes on. */
class MidiControl : private juce::Timer
{
public:
    MidiControl (RezalicaProcessor& p, juce::Component& root, bool standalone);
    ~MidiControl() override;

    // ---- MIDI Learn mode ----
    void setLearnMode (bool on);
    bool isLearnMode() const noexcept { return learnMode; }
    std::function<void (bool)> onLearnModeChanged;
    std::function<void (const juce::String&)> onMessage;   // a short status line (e.g. "MIDI: MPD218 connected")
    std::function<void()> onPadNotesChanged;                // repaint the pads / the note label

    juce::String keyFor (const juce::String& id) const;     // "CC 21 ch 1", "Note C3" ... or ""
    void forget (const juce::String& id);
    void clearAll();
    juce::String selectedId;                                // the control waiting for its MIDI control
    juce::Component* exitButton = nullptr;                  // a click on it (the MIDI key) ends MIDI Learn

    /** Quick set-up: learn the controller's pads / knobs / faders / transport keys one after another. */
    enum Wizard { wizardNone = 0, wizardPads, wizardKnobs, wizardFaders, wizardTransport };
    void startWizard (int kind);
    int getWizard() const noexcept { return wizard; }
    juce::String wizardText() const;      // what to do now ("Turn the knob for GAIN (2 / 7)")
    void skipWizardStep();
    /** The pad whose note MIDI Learn waits for (or -1). */
    void learnPadNote (int pad);

    /** The menu of the MIDI key (learn, pad notes, controllers, presets, clock). */
    void showMenu (juce::Component* target);

    // ---- presets per controller ----
    juce::StringArray getPresetNames() const;
    void savePreset (const juce::String& name, const juce::String& device);
    bool loadPreset (const juce::String& name);
    void deletePreset (const juce::String& name);
    juce::String currentPreset;

    /** The learnable control with this id inside the root component (or nullptr). */
    juce::Component* findControl (const juce::String& id) const;
    /** All visible learnable controls (for the overlay). */
    void collectControls (juce::Array<juce::Component*>& out) const;

    /** Applies one incoming control (used by the timer; public for the tests). */
    void handle (const RezalicaProcessor::MidiControlEvent& e);
    void pollNow() { timerCallback(); }

private:
    void timerCallback() override;
    void saveCurrent();
    void loadCurrent();
    void updateNoteControls();
    juce::String stateToString() const;
    void stateFromString (const juce::String&);
    void checkDevices();
    void nextWizardStep();
    void exportPreset();
    void importPreset();
    int wizard = wizardNone, wizardStep = 0;
    juce::StringArray wizardTargets;
    std::unique_ptr<juce::FileChooser> chooser;

    friend class MidiLearnOverlay;
    RezalicaProcessor& processor;
    juce::Component& root;
    const bool standalone;
    bool learnMode = false;
    std::map<juce::String, juce::String> mappings;          // control id -> "cc:<channel>:<number>" / "note:<number>"
    std::map<juce::String, int> lastValue;                  // buttons: rising edge of a CC
    juce::StringArray knownInputs;
    int tick = 0, lastPadMapChanged = 0, lastLearnPad = -1;
    double lastJogMs = 0.0;
    std::unique_ptr<juce::Component> overlay;
};
} // namespace rz
