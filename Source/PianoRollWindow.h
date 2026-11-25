#pragma once

#include "PluginProcessor.h"
#include "Scales.h"
#include "LookAndFeel.h"
#include "MetronomeControls.h"
#include "TransportButton.h"
#include "ComputerKeyboard.h"
#include "LedDisplay.h"
#include "DragExport.h"
#include <set>

namespace rz
{
/** The note editor: piano keyboard / pad names on the left, bar ruler on top,
    notes in the middle, velocity lane at the bottom. */
class PianoRollEditor : public juce::Component,
                        private juce::ScrollBar::Listener
{
public:
    /** draw = the pencil is on; select = no tool (click / Shift+drag selects, notes can be moved). */
    enum class Tool { draw, select };

    explicit PianoRollEditor (RezalicaProcessor& p);

    // settings from the toolbar
    Tool tool = Tool::select;       // the piano roll opens with no tool: the pencil is switched on by its button
    double snap = 1.0;              // in steps (1 = 1/16), 0 = off
    int newVelocity = 100;
    std::function<void()> onChanged; // after every edit (toolbar refresh)

    // editing commands (toolbar / keyboard)
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void quantize();                // selected notes (or all) to the snap grid
    /** HUMANIZE: the selected notes (or all) move a little off the grid and get slightly different
        velocities, like a drummer playing by hand. amount 0 .. 1 (1 = up to +-1/4 step, +-24 velocity). */
    void humanize (double amount);
    void deleteSelected();
    void selectAll();
    void copySelection();
    void paste();
    void duplicate();
    void transposeSelection (int semitones);
    void nudgeSelection (double steps);
    void clearAll();
    void setViewMode (int mode);    // NoteClip::drum / melodic
    /** TO SCALE: melodic notes (the selected ones, or all) to the nearest note of the scale. */
    void quantizeToScale();
    bool scaleOn() const;           // melodic view with a scale chosen
    int toScale (int note) const;   // the nearest scale note (unchanged without a scale)
    int getViewMode() const;
    void setTargetPad (int pad);
    int getTargetPad() const;

    /** Pulls the clip from the sequencer if it changed (pattern switch, recording). */
    void refreshFromSequencer();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;
    int getKeyboardOctave() const { return computerKeys.octave; }

private:
    ComputerKeyboard computerKeys; // play (and record) from the computer keyboard
    enum class Drag { none, create, move, resizeLeft, resizeRight, rubberBand, erase, velocity, keyboard, ruler };
    static constexpr int keyboardWidth = 96, rulerHeight = 22, velocityHeight = 72, scrollSize = 12;

    // geometry
    juce::Rectangle<int> noteArea() const;
    juce::Rectangle<int> velocityArea() const;
    juce::Rectangle<int> keyboardArea() const;
    juce::Rectangle<int> rulerArea() const;
    int numRows() const;                 // 64 pads or 128 notes
    int rowForDisplay (int displayIndex) const;
    int displayForRow (int row) const;
    // the playhead line moves at the screen's refresh rate (only its thin strip is repainted)
    int lastPlayheadX = -1;
    juce::VBlankAttachment playheadVBlank { this, [this]
    {
        auto& sq = processor.getSequencer();
        double ph = sq.getPlayPositionSteps();
        if (ph < 0.0 && sq.cueSteps.load() > 0.0)
            ph = sq.cueSteps.load(); // stopped: the playhead waits where PLAY will start
        const int x = ph >= 0.0 ? (int) xForStep (ph) : -1;
        if (x == lastPlayheadX) return;
        if (lastPlayheadX >= 0) repaint (lastPlayheadX - 2, 0, 5, getHeight());
        if (x >= 0) repaint (x - 2, 0, 5, getHeight());
        lastPlayheadX = x;
    } };
    float xForStep (double step) const;
    double stepForX (float x) const;
    float yForRow (int row) const;
    int rowForY (float y) const;
    juce::Rectangle<float> noteBounds (const PianoNote& n) const;
    double patternLength() const;

    // notes
    int noteIndexAt (juce::Point<float> p) const;
    double snapFloor (double s) const;
    double snapRound (double s) const;
    bool isSelected (const PianoNote& n) const { return selection.count (n.id) > 0; }
    std::vector<PianoNote> currentKindNotes() const;
    void pushUndo();
    void commit();                       // working copy -> new clip in the sequencer
    void preview (int row, bool on, int velocity = 100);
    void updateScrollBars();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void paintRuler (juce::Graphics&);
    void paintKeyboard (juce::Graphics&);
    void paintNotes (juce::Graphics&);
    void paintVelocity (juce::Graphics&);
    juce::String rowName (int row) const;

    RezalicaProcessor& processor;
    NoteClip::Ptr clip;                  // the clip currently shown (to notice outside changes)
    int shownPattern = -1;
    std::vector<PianoNote> notes;        // working copy (all kinds)
    std::set<juce::uint32> selection;
    // clicking / dragging on the keyboard selects whole rows
    int keyboardAnchorRow = 0;
    bool keyboardAddToSelection = false;
    std::set<juce::uint32> keyboardBaseSelection;
    void selectRowsBetween (int rowA, int rowB);
    std::vector<std::vector<PianoNote>> undoStack, redoStack;
    std::vector<PianoNote> clipboard;

    double viewStart = 0.0;              // first visible step
    double pixelsPerStep = 22.0;
    int topDisplayRow = 0;
    int rowHeight = 16;

    Drag drag = Drag::none;
    std::vector<juce::uint32> chordIds;   // the other notes of a chord being drawn (they follow its length)
    // ruler: click = move the playhead there (PLAY starts there), hold + drag = scrub (plays while held)
    bool scrubbing = false;
    double lastRulerStep = -1.0;
    double rulerStep (float x) const;
    juce::Point<float> dragStart;
    std::vector<PianoNote> dragOriginal;  // selected notes when the drag started
    juce::Rectangle<float> rubber;
    bool changedDuringDrag = false;
    int previewRow = -1;
    double lastLength = 1.0;
    float lastVelocityX = 0.0f;
    float lastEraseX = 0.0f, lastEraseY = 0.0f;

    juce::ScrollBar hScroll { false }, vScroll { true };
};

/** Toolbar + editor. */
class PianoRollPanel : public juce::Component,
                       private juce::Timer
{
public:
    explicit PianoRollPanel (RezalicaProcessor& p);
    ~PianoRollPanel() override;
    /** The piano roll opens in DRUMS mode (MELODIC is one click away). */
    void focusEditor() { editor.grabKeyboardFocus(); }
    bool forwardKeyPressed (const juce::KeyPress& k) { return editor.keyPressed (k); }
    bool forwardKeyState (bool down) { return editor.keyStateChanged (down); }
    void showDrums() { editor.refreshFromSequencer(); editor.setViewMode (NoteClip::drum); noTool(); }
    /** Entering the piano roll: no tool is chosen (the pencil is off). */
    void noTool() { editor.tool = PianoRollEditor::Tool::select; syncControls(); }
    void visibilityChanged() override { if (isShowing()) noTool(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void syncControls();
    void fillPadBox();

    RezalicaProcessor& processor;
    PianoRollEditor editor;

    juce::TextButton playButton { "PLAY" }, recButton { "REC" }, syncButton { "SYNC HOST" };
    juce::Slider bpmSlider;
    juce::ComboBox patternBox, lengthBox;

    juce::TextButton drumButton { "DRUMS" }, melodicButton { "MELODIC" };
    juce::ComboBox padBox;
    juce::TextButton midiKeysButton { "MIDI KEYS" };
    // MELODIC: key + scale, the chord a drawn note makes, notes to the scale
    juce::ComboBox keyBox, scaleBox, chordBox;
    juce::TextButton toScaleButton { "TO SCALE" };
    /** The pencil key: a toggle button that shows a pencil icon. */
    struct PencilButton : public juce::TextButton
    {
        PencilButton() : juce::TextButton ("") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };
    PencilButton pencilButton;
    juce::TextButton humanizeButton { "HUMANIZE" };
    juce::Slider humanizeSlider;
    juce::ComboBox snapBox;
    juce::TextButton quantizeButton { "QUANTIZE" }, undoButton { "UNDO" }, redoButton { "REDO" }, clearButton { "CLEAR" }, copyNextButton { "COPY TO NEXT PATTERN" };
    juce::Slider velocitySlider;
    juce::Label infoLabel;
    DragExportButton dragMidiButton, dragWavButton;
    MetronomeControls metronome;
    TransportBar transport;
    LedBpmDisplay ledBpm;
};

class PianoRollWindow : public juce::DocumentWindow
{
    std::unique_ptr<rz::StayOnTopOfRezalica> onTopHelper;
public:
    PianoRollWindow (RezalicaProcessor& p, juce::LookAndFeel& lnf)
        : juce::DocumentWindow ("Rezalica - Piano Roll", colours::background, juce::DocumentWindow::closeButton)
    {
        setLookAndFeel (&lnf);
        setUsingNativeTitleBar (true);
        setContentOwned (new PianoRollPanel (p), true);
        setResizable (true, false);
        setResizeLimits (900, 480, 2600, 1600);
        onTopHelper = std::make_unique<rz::StayOnTopOfRezalica> (*this); // on top only while Rezalica is in front
        centreWithSize (getWidth(), getHeight());
    }
    ~PianoRollWindow() override { setLookAndFeel (nullptr); }
    void closeButtonPressed() override { setVisible (false); if (onClose) onClose(); }
    // keys that land on the window frame itself go to the piano roll
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (auto* panel = dynamic_cast<PianoRollPanel*> (getContentComponent())) return panel->forwardKeyPressed (k);
        return false;
    }
    bool keyStateChanged (bool down) override
    {
        if (auto* panel = dynamic_cast<PianoRollPanel*> (getContentComponent())) return panel->forwardKeyState (down);
        return false;
    }

    // the computer keyboard plays the piano roll as soon as its window is active
    void activeWindowStatusChanged() override
    {
        if (isActiveWindow())
            if (auto* panel = dynamic_cast<PianoRollPanel*> (getContentComponent())) panel->focusEditor();
    }
    std::function<void()> onClose;
};
} // namespace rz
