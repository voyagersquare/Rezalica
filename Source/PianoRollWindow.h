#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "MetronomeControls.h"
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
    enum class Tool { draw, select };

    explicit PianoRollEditor (RezalicaProcessor& p);

    // settings from the toolbar
    Tool tool = Tool::draw;
    double snap = 1.0;              // in steps (1 = 1/16), 0 = off
    int newVelocity = 100;
    std::function<void()> onChanged; // after every edit (toolbar refresh)

    // editing commands (toolbar / keyboard)
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void quantize();                // selected notes (or all) to the snap grid
    void deleteSelected();
    void selectAll();
    void copySelection();
    void paste();
    void duplicate();
    void transposeSelection (int semitones);
    void nudgeSelection (double steps);
    void clearAll();
    void setViewMode (int mode);    // NoteClip::drum / melodic
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

private:
    enum class Drag { none, create, move, resizeLeft, resizeRight, rubberBand, erase, velocity, keyboard };
    static constexpr int keyboardWidth = 96, rulerHeight = 22, velocityHeight = 72, scrollSize = 12;

    // geometry
    juce::Rectangle<int> noteArea() const;
    juce::Rectangle<int> velocityArea() const;
    juce::Rectangle<int> keyboardArea() const;
    juce::Rectangle<int> rulerArea() const;
    int numRows() const;                 // 64 pads or 128 notes
    int rowForDisplay (int displayIndex) const;
    int displayForRow (int row) const;
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
    std::vector<std::vector<PianoNote>> undoStack, redoStack;
    std::vector<PianoNote> clipboard;

    double viewStart = 0.0;              // first visible step
    double pixelsPerStep = 22.0;
    int topDisplayRow = 0;
    int rowHeight = 16;

    Drag drag = Drag::none;
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
    /** The piano roll opens in melodic mode. */
    void showMelodic() { editor.refreshFromSequencer(); editor.setViewMode (NoteClip::melodic); syncControls(); }
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
    juce::TextButton midiKeysButton { "PLAY WITH MIDI KEYS" };
    juce::TextButton drawButton { "DRAW" }, selectButton { "SELECT" };
    juce::ComboBox snapBox;
    juce::TextButton quantizeButton { "QUANTIZE" }, undoButton { "UNDO" }, redoButton { "REDO" }, clearButton { "CLEAR" };
    juce::Slider velocitySlider;
    juce::Label infoLabel;
    DragExportButton dragMidiButton, dragWavButton;
    MetronomeControls metronome;
};

class PianoRollWindow : public juce::DocumentWindow
{
public:
    PianoRollWindow (RezalicaProcessor& p, juce::LookAndFeel& lnf)
        : juce::DocumentWindow ("Rezalica - Piano Roll", colours::background, juce::DocumentWindow::closeButton)
    {
        setLookAndFeel (&lnf);
        setUsingNativeTitleBar (true);
        setContentOwned (new PianoRollPanel (p), true);
        setResizable (true, false);
        setResizeLimits (900, 480, 2600, 1600);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
    }
    ~PianoRollWindow() override { setLookAndFeel (nullptr); }
    void closeButtonPressed() override { setVisible (false); if (onClose) onClose(); }
    std::function<void()> onClose;
};
} // namespace rz
