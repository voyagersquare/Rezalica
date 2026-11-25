#include "PianoRollWindow.h"

namespace rz
{
namespace
{
bool isBlackKey (int note)
{
    const int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}
constexpr float minNoteLength = 0.05f;
} // namespace

//==============================================================================
PianoRollEditor::PianoRollEditor (RezalicaProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    computerKeys.noteFilter = [this] (int n) { return toScale (n); }; // with a scale the keys play only its notes
    for (auto* s : { &hScroll, &vScroll })
    {
        s->addListener (this);
        s->setAutoHide (false);
        s->setColour (juce::ScrollBar::thumbColourId, colours::accent2.withAlpha (0.6f));
        addAndMakeVisible (*s);
    }
    refreshFromSequencer();
}

//==============================================================================
juce::Rectangle<int> PianoRollEditor::rulerArea() const
{
    return { keyboardWidth, 0, getWidth() - keyboardWidth - scrollSize, rulerHeight };
}

juce::Rectangle<int> PianoRollEditor::noteArea() const
{
    return { keyboardWidth, rulerHeight, getWidth() - keyboardWidth - scrollSize,
             juce::jmax (10, getHeight() - rulerHeight - velocityHeight - scrollSize) };
}

juce::Rectangle<int> PianoRollEditor::velocityArea() const
{
    auto n = noteArea();
    return { n.getX(), n.getBottom(), n.getWidth(), velocityHeight };
}

juce::Rectangle<int> PianoRollEditor::keyboardArea() const
{
    auto n = noteArea();
    return { 0, n.getY(), keyboardWidth, n.getHeight() };
}

int PianoRollEditor::getViewMode() const { return clip != nullptr ? clip->mode : NoteClip::drum; }
int PianoRollEditor::getTargetPad() const { return clip != nullptr ? clip->targetPad : 0; }
int PianoRollEditor::numRows() const { return getViewMode() == NoteClip::melodic ? 128 : numPads; }
int PianoRollEditor::rowForDisplay (int d) const { return getViewMode() == NoteClip::melodic ? 127 - d : d; }
int PianoRollEditor::displayForRow (int r) const { return getViewMode() == NoteClip::melodic ? 127 - r : r; }

double PianoRollEditor::patternLength() const
{
    auto& seq = processor.getSequencer();
    return (double) juce::jlimit (1, Sequencer::maxSteps, seq.length[(size_t) seq.currentPattern.load()].load());
}

float PianoRollEditor::xForStep (double s) const
{
    return (float) noteArea().getX() + (float) ((s - viewStart) * pixelsPerStep);
}

double PianoRollEditor::stepForX (float x) const
{
    return viewStart + (x - (float) noteArea().getX()) / pixelsPerStep;
}

float PianoRollEditor::yForRow (int row) const
{
    return (float) noteArea().getY() + (float) ((displayForRow (row) - topDisplayRow) * rowHeight);
}

int PianoRollEditor::rowForY (float y) const
{
    const int d = topDisplayRow + (int) std::floor ((y - (float) noteArea().getY()) / (float) rowHeight);
    return rowForDisplay (juce::jlimit (0, numRows() - 1, d));
}

juce::Rectangle<float> PianoRollEditor::noteBounds (const PianoNote& n) const
{
    const float x0 = xForStep (n.start), x1 = xForStep (n.start + n.length);
    return { x0, yForRow (n.row) + 1.0f, juce::jmax (3.0f, x1 - x0), (float) rowHeight - 2.0f };
}

double PianoRollEditor::snapFloor (double s) const
{
    return snap > 0.0 ? std::floor (s / snap + 1.0e-6) * snap : s;
}

double PianoRollEditor::snapRound (double s) const
{
    return snap > 0.0 ? std::round (s / snap) * snap : s;
}

int PianoRollEditor::noteIndexAt (juce::Point<float> p) const
{
    if (! noteArea().toFloat().contains (p))
        return -1;
    const int kind = getViewMode();
    for (int i = (int) notes.size(); --i >= 0;)
        if ((int) notes[(size_t) i].kind == kind && noteBounds (notes[(size_t) i]).expanded (0.0f, 0.5f).contains (p))
            return i;
    return -1;
}

std::vector<PianoNote> PianoRollEditor::currentKindNotes() const
{
    std::vector<PianoNote> r;
    for (auto& n : notes)
        if ((int) n.kind == getViewMode())
            r.push_back (n);
    return r;
}

juce::String PianoRollEditor::rowName (int row) const
{
    if (getViewMode() == NoteClip::melodic)
        return juce::MidiMessage::getMidiNoteName (row, true, true, 3);
    return padLabel (row);
}

//==============================================================================
void PianoRollEditor::refreshFromSequencer()
{
    auto& seq = processor.getSequencer();
    const int pattern = seq.currentPattern.load();
    auto c = seq.getClip (pattern);

    if (pattern != shownPattern)
    {
        shownPattern = pattern;
        clip = c;
        notes = c->notes;
        selection.clear();
        undoStack.clear();
        redoStack.clear();
        if (getViewMode() == NoteClip::melodic)
            topDisplayRow = juce::jlimit (0, 127, 127 - 72); // around C4 at the top
        repaint();
        if (onChanged) onChanged();
    }
    else if (c != clip && drag == Drag::none)
    {
        clip = c;
        notes = c->notes; // e.g. notes recorded live
        std::set<juce::uint32> keep;
        for (auto& n : notes)
            if (selection.count (n.id)) keep.insert (n.id);
        selection = keep;
        repaint();
        if (onChanged) onChanged();
    }
    updateScrollBars();
}

void PianoRollEditor::pushUndo()
{
    undoStack.push_back (notes);
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void PianoRollEditor::commit()
{
    auto c = clip != nullptr ? clip->copy() : NoteClip::Ptr (new NoteClip());
    c->notes = notes;
    processor.getSequencer().setClip (shownPattern, c);
    clip = c;
    repaint();
    if (onChanged) onChanged();
}

void PianoRollEditor::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (notes);
    notes = undoStack.back();
    undoStack.pop_back();
    selection.clear();
    commit();
}

void PianoRollEditor::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (notes);
    notes = redoStack.back();
    redoStack.pop_back();
    selection.clear();
    commit();
}

void PianoRollEditor::setViewMode (int mode)
{
    if (clip == nullptr || clip->mode == mode) return;
    auto c = clip->copy();
    c->mode = mode;
    c->notes = notes;
    processor.getSequencer().setClip (shownPattern, c);
    clip = c;
    selection.clear();
    topDisplayRow = mode == NoteClip::melodic ? 127 - 72 : 0;
    updateScrollBars();
    repaint();
    if (onChanged) onChanged();
}

void PianoRollEditor::setTargetPad (int pad)
{
    if (clip == nullptr || clip->targetPad == pad) return;
    auto c = clip->copy();
    c->targetPad = pad;
    c->notes = notes;
    processor.getSequencer().setClip (shownPattern, c);
    clip = c;
    repaint();
}

void PianoRollEditor::preview (int row, bool on, int velocity)
{
    if (getViewMode() == NoteClip::melodic)
        processor.previewNoteFromUi (getTargetPad(), (float) velocity / 127.0f, (float) (row - 60), on);
    else if (row >= 0 && row < numPads)
        processor.triggerPadFromUi (row, (float) velocity / 127.0f, on);
}

//==============================================================================
void PianoRollEditor::deleteSelected()
{
    if (selection.empty()) return;
    pushUndo();
    notes.erase (std::remove_if (notes.begin(), notes.end(), [this] (const PianoNote& n) { return isSelected (n); }), notes.end());
    selection.clear();
    commit();
}

void PianoRollEditor::selectAll()
{
    selection.clear();
    for (auto& n : notes)
        if ((int) n.kind == getViewMode())
            selection.insert (n.id);
    repaint();
}

void PianoRollEditor::copySelection()
{
    clipboard.clear();
    for (auto& n : notes)
        if (isSelected (n))
            clipboard.push_back (n);
}

void PianoRollEditor::paste()
{
    if (clipboard.empty()) return;
    double minStart = 1.0e9, maxEnd = 0.0;
    for (auto& n : clipboard) { minStart = juce::jmin (minStart, (double) n.start); maxEnd = juce::jmax (maxEnd, (double) (n.start + n.length)); }
    const double span = snap > 0.0 ? std::ceil ((maxEnd - minStart) / snap - 1.0e-6) * snap : (maxEnd - minStart);

    pushUndo();
    selection.clear();
    for (auto& n : clipboard)
    {
        auto c = n;
        c.id = processor.getSequencer().newNoteId();
        c.start = (float) (n.start + span);
        c.kind = (juce::uint8) getViewMode();
        if (c.start < patternLength())
        {
            notes.push_back (c);
            selection.insert (c.id);
        }
    }
    for (auto& n : clipboard) n.start = (float) (n.start + span); // the next paste goes after this one
    commit();
}

void PianoRollEditor::duplicate()
{
    copySelection();
    paste();
}

bool PianoRollEditor::scaleOn() const
{
    return getViewMode() == NoteClip::melodic && ! scales::isChromatic (processor.scaleType.load());
}

int PianoRollEditor::toScale (int note) const
{
    return scaleOn() ? scales::snap (processor.scaleType.load(), processor.scaleRoot.load(), note) : note;
}

void PianoRollEditor::quantizeToScale()
{
    // TO SCALE: the melodic notes (the selected ones, or all) move to the nearest note of the scale
    if (getViewMode() != NoteClip::melodic || scales::isChromatic (processor.scaleType.load())) return;
    pushUndo();
    for (auto& n : notes)
        if ((int) n.kind == NoteClip::melodic && (selection.empty() || isSelected (n)))
            n.row = scales::snap (processor.scaleType.load(), processor.scaleRoot.load(), n.row);
    commit();
}

void PianoRollEditor::quantize()
{
    const double grid = snap > 0.0 ? snap : 1.0;
    pushUndo();
    for (auto& n : notes)
        if ((int) n.kind == getViewMode() && (selection.empty() || isSelected (n)))
            n.start = (float) juce::jlimit (0.0, patternLength() - grid, std::round (n.start / grid) * grid);
    commit();
}

void PianoRollEditor::humanize (double amount)
{
    amount = juce::jlimit (0.0, 1.0, amount);
    if (amount <= 0.0) return;
    auto& rnd = juce::Random::getSystemRandom();
    const double maxShift = 0.25 * amount;      // in steps (1/16): at 100 % up to a quarter of a step early / late
    const int maxVel = (int) std::round (24.0 * amount);
    const double len = patternLength();
    pushUndo();
    for (auto& n : notes)
        if ((int) n.kind == getViewMode() && (selection.empty() || isSelected (n)))
        {
            const double shift = (rnd.nextDouble() * 2.0 - 1.0) * maxShift;
            n.start = (float) juce::jlimit (0.0, juce::jmax (0.0, len - (double) n.length), (double) n.start + shift);
            const int dv = maxVel > 0 ? rnd.nextInt (2 * maxVel + 1) - maxVel : 0;
            n.velocity = (juce::uint8) juce::jlimit (1, 127, (int) n.velocity + dv);
        }
    commit();
}

void PianoRollEditor::transposeSelection (int semitones)
{
    if (selection.empty()) return;
    pushUndo();
    for (auto& n : notes)
        if (isSelected (n))
        {
            if (scaleOn() && std::abs (semitones) < 12)   // with a scale: up / down by one note of the scale
                n.row = scales::stepBy (processor.scaleType.load(), processor.scaleRoot.load(), n.row, semitones > 0 ? 1 : -1);
            else
                n.row = rowForDisplay (juce::jlimit (0, numRows() - 1, displayForRow (n.row) - semitones));
        }
    commit();
    for (auto& n : notes)
        if (isSelected (n)) { preview (n.row, true, n.velocity); preview (n.row, false); break; }
}

void PianoRollEditor::nudgeSelection (double steps)
{
    if (selection.empty()) return;
    pushUndo();
    for (auto& n : notes)
        if (isSelected (n))
            n.start = (float) juce::jlimit (0.0, patternLength() - minNoteLength, (double) n.start + steps);
    commit();
}

void PianoRollEditor::clearAll()
{
    pushUndo();
    const int kind = getViewMode();
    notes.erase (std::remove_if (notes.begin(), notes.end(), [kind] (const PianoNote& n) { return (int) n.kind == kind; }), notes.end());
    selection.clear();
    commit();
}

//==============================================================================
void PianoRollEditor::resized()
{
    auto n = noteArea();
    vScroll.setBounds (n.getRight(), n.getY(), scrollSize, n.getHeight());
    hScroll.setBounds (n.getX(), velocityArea().getBottom(), n.getWidth(), scrollSize);
    updateScrollBars();
}

void PianoRollEditor::updateScrollBars()
{
    const double visibleSteps = noteArea().getWidth() / pixelsPerStep;
    const double total = juce::jmax (patternLength() + 4.0, viewStart + visibleSteps);
    hScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
    hScroll.setCurrentRange (viewStart, visibleSteps, juce::dontSendNotification);

    const int visibleRows = noteArea().getHeight() / juce::jmax (1, rowHeight);
    topDisplayRow = juce::jlimit (0, juce::jmax (0, numRows() - visibleRows), topDisplayRow);
    vScroll.setRangeLimits (0.0, (double) numRows(), juce::dontSendNotification);
    vScroll.setCurrentRange ((double) topDisplayRow, (double) visibleRows, juce::dontSendNotification);
}

void PianoRollEditor::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hScroll) viewStart = juce::jmax (0.0, start);
    else                 topDisplayRow = juce::roundToInt (start);
    updateScrollBars();
    repaint();
}

void PianoRollEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : w.deltaX;
    if (std::abs (d) < 1.0e-4f) return;

    if (e.mods.isCommandDown())          // horizontal zoom around the mouse
    {
        const double anchor = stepForX ((float) e.x);
        pixelsPerStep = juce::jlimit (3.0, 200.0, pixelsPerStep * (d > 0 ? 1.2 : 1.0 / 1.2));
        viewStart = juce::jmax (0.0, anchor - (e.x - noteArea().getX()) / pixelsPerStep);
    }
    else if (e.mods.isAltDown())         // row height
    {
        rowHeight = juce::jlimit (8, 40, rowHeight + (d > 0 ? 2 : -2));
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        viewStart = juce::jmax (0.0, viewStart - d * 8.0 * 22.0 / pixelsPerStep);
    }
    else
    {
        topDisplayRow -= (int) std::round (d * 8.0f);
    }
    updateScrollBars();
    repaint();
}

//==============================================================================
namespace
{
/** DRAW tool cursor: a small pencil, its tip is the hot spot. */
const juce::MouseCursor& pencilCursor()
{
    static const juce::MouseCursor cursor = []
    {
        const int sz = 24;
        juce::Image img (juce::Image::ARGB, sz, sz, true);
        juce::Graphics g (img);
        // drawn pointing down-left: tip at (2, 22)
        juce::Path body;
        body.startNewSubPath (2.0f, 22.0f);   // tip
        body.lineTo (5.0f, 14.0f);
        body.lineTo (16.0f, 3.0f);
        body.lineTo (21.0f, 8.0f);
        body.lineTo (10.0f, 19.0f);
        body.closeSubPath();
        g.setColour (juce::Colours::black);
        g.strokePath (body, juce::PathStrokeType (2.2f, juce::PathStrokeType::mitered));
        g.setColour (juce::Colour (0xffffc94a));
        g.fillPath (body);
        juce::Path wood;                    // the sharpened wood and the lead
        wood.addTriangle (2.0f, 22.0f, 5.0f, 14.0f, 10.0f, 19.0f);
        g.setColour (juce::Colour (0xffe8cfa0));
        g.fillPath (wood);
        g.setColour (juce::Colours::black);
        g.fillEllipse (1.0f, 19.5f, 3.5f, 3.5f);
        juce::Path eraser;                  // pink eraser at the end
        eraser.startNewSubPath (16.0f, 3.0f);
        eraser.lineTo (18.5f, 0.5f);
        eraser.lineTo (23.5f, 5.5f);
        eraser.lineTo (21.0f, 8.0f);
        eraser.closeSubPath();
        g.setColour (juce::Colour (0xffff8fa3));
        g.fillPath (eraser);
        g.setColour (juce::Colours::black);
        g.strokePath (eraser, juce::PathStrokeType (1.2f));
        return juce::MouseCursor (img, 2, 22);
    }();
    return cursor;
}
} // namespace

void PianoRollEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (noteArea().toFloat().contains (p))
    {
        const int i = noteIndexAt (p);
        if (i >= 0)
        {
            auto b = noteBounds (notes[(size_t) i]);
            const bool edge = p.x >= b.getRight() - 6.0f || (p.x <= b.getX() + 5.0f && b.getWidth() > 14.0f);
            setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
        }
        else
        {
            if (tool == Tool::draw && ! e.mods.isShiftDown()) setMouseCursor (pencilCursor());
            else                    setMouseCursor (juce::MouseCursor::NormalCursor);
        }
    }
    else
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }
}

double PianoRollEditor::rulerStep (float x) const
{
    const double raw = juce::jlimit (0.0, juce::jmax (0.0, patternLength() - 1.0e-3), stepForX (x));
    return snap > 0.0 ? snapFloor (raw) : raw;
}

void PianoRollEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position;
    dragStart = p;
    changedDuringDrag = false;

    if (rulerArea().toFloat().contains (p) && e.mods.isLeftButtonDown())
    {
        // the playhead jumps here; when stopped, holding the mouse plays from here (scrub)
        drag = Drag::ruler;
        auto& sq = processor.getSequencer();
        lastRulerStep = rulerStep (p.x);
        sq.locateTo (lastRulerStep);
        scrubbing = ! sq.internalPlay.load();
        if (scrubbing)
        {
            sq.stopPlayback(); // (also leaves PAUSE)
            sq.playPattern();
        }
        repaint();
        return;
    }

    if (keyboardArea().toFloat().contains (p))
    {
        drag = Drag::keyboard;
        previewRow = rowForY (p.y);
        preview (previewRow, true, newVelocity);
        // click a key: all notes of that row are selected; drag over the keys: all rows in between
        keyboardAnchorRow = previewRow;
        keyboardAddToSelection = e.mods.isShiftDown() || e.mods.isCommandDown();
        keyboardBaseSelection = keyboardAddToSelection ? selection : std::set<juce::uint32>();
        selectRowsBetween (keyboardAnchorRow, previewRow);
        return;
    }

    if (velocityArea().toFloat().contains (p))
    {
        pushUndo();
        drag = Drag::velocity;
        lastVelocityX = p.x;
        mouseDrag (e);
        return;
    }

    if (! noteArea().toFloat().contains (p))
        return;

    const int idx = noteIndexAt (p);

    if (e.mods.isPopupMenu())            // right click: delete, right drag: delete everything touched
    {
        pushUndo();
        drag = Drag::erase;
        lastEraseX = p.x;
        lastEraseY = p.y;
        if (idx >= 0)
        {
            selection.erase (notes[(size_t) idx].id);
            notes.erase (notes.begin() + idx);
            changedDuringDrag = true;
            repaint();
        }
        return;
    }

    if (idx >= 0)
    {
        auto& n = notes[(size_t) idx];
        if (e.mods.isShiftDown())
        {
            if (isSelected (n)) { selection.erase (n.id); repaint(); drag = Drag::none; return; }
            selection.insert (n.id);
        }
        else if (! isSelected (n))
        {
            selection = { n.id };
        }

        auto b = noteBounds (n);
        if (p.x >= b.getRight() - 6.0f)                          drag = Drag::resizeRight;
        else if (p.x <= b.getX() + 5.0f && b.getWidth() > 14.0f) drag = Drag::resizeLeft;
        else                                                     drag = Drag::move;

        pushUndo();
        const int clickedRow = n.row;
        const int clickedVel = n.velocity;

        // Ctrl / Cmd + drag: move copies, the originals stay
        if (drag == Drag::move && e.mods.isCommandDown())
        {
            std::vector<PianoNote> copies;
            for (auto& o : notes)
                if (isSelected (o)) { auto c = o; c.id = processor.getSequencer().newNoteId(); copies.push_back (c); }
            selection.clear();
            for (auto& c : copies) { notes.push_back (c); selection.insert (c.id); }
            changedDuringDrag = true;
        }

        dragOriginal.clear();
        for (auto& o : notes)
            if (isSelected (o)) dragOriginal.push_back (o);

        if (drag == Drag::move)
        {
            previewRow = clickedRow;
            preview (previewRow, true, clickedVel);
        }
        repaint();
        return;
    }

    // empty space: the pencil draws; with no tool, or with Shift held, a frame selects notes
    if (tool == Tool::draw && ! e.mods.isShiftDown())
    {
        pushUndo();
        PianoNote n;
        n.id = processor.getSequencer().newNoteId();
        n.kind = (juce::uint8) getViewMode();
        n.row = toScale (rowForY (p.y));   // with a scale: only its notes
        n.start = (float) juce::jlimit (0.0, juce::jmax (0.0, patternLength() - minNoteLength), snapFloor (stepForX (p.x)));
        n.length = (float) juce::jmin (juce::jmax ((double) minNoteLength, lastLength), patternLength() - n.start);
        n.velocity = (juce::uint8) newVelocity;
        notes.push_back (n);
        selection = { n.id };
        chordIds.clear();
        if (getViewMode() == NoteClip::melodic && processor.chordType.load() != scales::chordOff)
        {
            // a chord: the other notes of it on the same step
            const auto chordNotes = scales::chord (processor.chordType.load(), processor.scaleType.load(), processor.scaleRoot.load(), n.row);
            for (size_t k = 1; k < chordNotes.size(); ++k)
            {
                auto c = n;
                c.id = processor.getSequencer().newNoteId();
                c.row = chordNotes[k];
                notes.push_back (c);
                selection.insert (c.id);
                chordIds.push_back (c.id);
            }
        }
        dragOriginal = { n };
        drag = Drag::create;
        changedDuringDrag = true;
        previewRow = n.row;
        preview (previewRow, true, n.velocity);
        repaint();
    }
    else
    {
        if (! e.mods.isShiftDown())
            selection.clear();
        dragOriginal.clear();
        for (auto& o : notes)
            if (isSelected (o)) dragOriginal.push_back (o); // kept while rubber-banding with shift
        drag = Drag::rubberBand;
        rubber = { p, p };
        repaint();
    }
}

void PianoRollEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (drag == Drag::ruler)
    {
        // scrub: every time the mouse reaches another grid step, playback jumps there
        const double st = rulerStep (p.x);
        if (std::abs (st - lastRulerStep) > 1.0e-6)
        {
            lastRulerStep = st;
            processor.getSequencer().locateTo (st);
            repaint();
        }
        return;
    }
    const double deltaSteps = stepForX (p.x) - stepForX (dragStart.x);
    const double len = patternLength();
    const double minLen = snap > 0.0 ? juce::jmin (snap, 1.0) : (double) minNoteLength;

    switch (drag)
    {
        case Drag::keyboard:
        {
            const int r = rowForY (p.y);
            if (r != previewRow) { preview (previewRow, false); previewRow = r; preview (r, true, newVelocity); }
            selectRowsBetween (keyboardAnchorRow, r);
            break;
        }

        case Drag::velocity:
        {
            auto va = velocityArea().toFloat();
            const int vel = juce::jlimit (1, 127, (int) std::round ((va.getBottom() - p.y) / (va.getHeight() - 6.0f) * 127.0f));
            const float x0 = juce::jmin (lastVelocityX, p.x) - 3.0f, x1 = juce::jmax (lastVelocityX, p.x) + 3.0f;
            // with notes selected, the velocity bar changes all of them at once
            bool anySelected = false;
            for (auto& n : notes)
                anySelected = anySelected || ((int) n.kind == getViewMode() && isSelected (n));
            for (auto& n : notes)
            {
                if ((int) n.kind != getViewMode())
                    continue;
                if (anySelected)
                {
                    if (isSelected (n)) { n.velocity = (juce::uint8) vel; changedDuringDrag = true; }
                    continue;
                }
                const float nx = xForStep (n.start);
                if (nx >= x0 && nx <= x1) { n.velocity = (juce::uint8) vel; changedDuringDrag = true; }
            }
            lastVelocityX = p.x;
            repaint();
            break;
        }

        case Drag::erase:
        {
            // walk along the mouse path so fast movements don't skip notes
            const float dist = std::hypot (p.x - lastEraseX, p.y - lastEraseY);
            const int stepsN = juce::jmax (1, (int) (dist / 3.0f));
            for (int i = 1; i <= stepsN; ++i)
            {
                const float t = (float) i / (float) stepsN;
                const juce::Point<float> q (lastEraseX + (p.x - lastEraseX) * t, lastEraseY + (p.y - lastEraseY) * t);
                const int idx = noteIndexAt (q);
                if (idx >= 0)
                {
                    selection.erase (notes[(size_t) idx].id);
                    notes.erase (notes.begin() + idx);
                    changedDuringDrag = true;
                }
            }
            lastEraseX = p.x;
            lastEraseY = p.y;
            repaint();
            break;
        }

        case Drag::create:
        {
            for (auto& n : notes)
                if (! dragOriginal.empty() && (n.id == dragOriginal[0].id || std::find (chordIds.begin(), chordIds.end(), n.id) != chordIds.end()))
                {
                    const double end = juce::jmax ((double) n.start + minLen, snap > 0.0 ? std::ceil (stepForX (p.x) / snap - 1.0e-6) * snap : stepForX (p.x));
                    n.length = (float) juce::jlimit (minLen, len - n.start, end - n.start);
                }
            repaint();
            break;
        }

        case Drag::resizeRight:
        case Drag::resizeLeft:
        {
            for (auto& o : dragOriginal)
                for (auto& n : notes)
                    if (n.id == o.id)
                    {
                        const double oEnd = o.start + o.length;
                        if (drag == Drag::resizeRight)
                        {
                            const double end = juce::jlimit ((double) o.start + minLen, len, snapRound (oEnd + deltaSteps));
                            n.length = (float) (end - o.start);
                        }
                        else
                        {
                            const double start = juce::jlimit (0.0, oEnd - minLen, snapRound (o.start + deltaSteps));
                            n.start = (float) start;
                            n.length = (float) (oEnd - start);
                        }
                    }
            changedDuringDrag = true;
            repaint();
            break;
        }

        case Drag::move:
        {
            const double d = snap > 0.0 ? std::round (deltaSteps / snap) * snap : deltaSteps;
            const int rowDelta = displayForRow (rowForY (p.y)) - displayForRow (rowForY (dragStart.y));
            for (auto& o : dragOriginal)
                for (auto& n : notes)
                    if (n.id == o.id)
                    {
                        n.start = (float) juce::jlimit (0.0, len - minNoteLength, o.start + d);
                        n.row = rowForDisplay (juce::jlimit (0, numRows() - 1, displayForRow (o.row) + rowDelta));
                        if ((int) n.kind == NoteClip::melodic) n.row = toScale (n.row); // with a scale: only its notes
                    }
            if (! dragOriginal.empty())
            {
                const int r = rowForDisplay (juce::jlimit (0, numRows() - 1, displayForRow (dragOriginal[0].row) + rowDelta));
                if (r != previewRow) { preview (previewRow, false); previewRow = r; preview (r, true, dragOriginal[0].velocity); }
            }
            changedDuringDrag = changedDuringDrag || std::abs (d) > 1.0e-6 || rowDelta != 0;
            repaint();
            break;
        }

        case Drag::rubberBand:
        {
            rubber = juce::Rectangle<float> (dragStart, p);
            selection.clear();
            for (auto& o : dragOriginal) selection.insert (o.id);
            for (auto& n : notes)
                if ((int) n.kind == getViewMode() && noteBounds (n).intersects (rubber))
                    selection.insert (n.id);
            repaint();
            break;
        }

        case Drag::none:
            break;
    }
}

void PianoRollEditor::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::ruler)
    {
        drag = Drag::none;
        if (scrubbing)
        {
            // end of the scrub: stop and fade out (the playhead stays where the mouse let go)
            auto& sq = processor.getSequencer();
            sq.stopPlayback();
            sq.stopSound = 1;
            scrubbing = false;
        }
        repaint();
        return;
    }
    if (previewRow >= 0 && (drag == Drag::keyboard || drag == Drag::move || drag == Drag::create))
        preview (previewRow, false);
    previewRow = -1;

    if (drag == Drag::create || drag == Drag::resizeRight)
        for (auto& n : notes)
            if (! dragOriginal.empty() && n.id == dragOriginal[0].id)
                lastLength = n.length;

    const bool editing = drag == Drag::create || drag == Drag::move || drag == Drag::resizeLeft || drag == Drag::resizeRight
                      || drag == Drag::erase || drag == Drag::velocity;
    if (editing)
    {
        if (changedDuringDrag) commit();
        else if (! undoStack.empty()) undoStack.pop_back(); // nothing changed: no undo step
    }

    drag = Drag::none;
    rubber = {};
    dragOriginal.clear();
    repaint();
}

void PianoRollEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! noteArea().toFloat().contains (e.position) || e.mods.isPopupMenu())
        return;
    const int idx = noteIndexAt (e.position);
    if (idx >= 0 && tool == Tool::select)
    {
        pushUndo();
        selection.erase (notes[(size_t) idx].id);
        notes.erase (notes.begin() + idx);
        commit();
    }
    else if (idx < 0 && tool == Tool::select)
    {
        // select tool: double-click on empty space adds a note
        tool = Tool::draw;
        mouseDown (e);
        mouseUp (e);
        tool = Tool::select;
    }
}

bool PianoRollEditor::keyPressed (const juce::KeyPress& k)
{
    const bool cmd = k.getModifiers().isCommandDown();
    const bool shift = k.getModifiers().isShiftDown();
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    const int code = k.getKeyCode();

    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (cmd && (code == 'A' || c == 'a')) { selectAll(); return true; }
    if (cmd && (code == 'C' || c == 'c')) { copySelection(); return true; }
    if (cmd && (code == 'V' || c == 'v')) { paste(); return true; }
    if (cmd && (code == 'D' || c == 'd')) { duplicate(); return true; }
    if (cmd && (code == 'Z' || c == 'z')) { shift ? redo() : undo(); return true; }
    if (cmd && (code == 'Y' || c == 'y')) { redo(); return true; }
    if (code == juce::KeyPress::upKey)    { transposeSelection (shift ? 12 : 1); return true; }
    if (code == juce::KeyPress::downKey)  { transposeSelection (shift ? -12 : -1); return true; }
    if (code == juce::KeyPress::leftKey)  { nudgeSelection (-(shift ? 4.0 : (snap > 0.0 ? snap : 1.0))); return true; }
    if (code == juce::KeyPress::rightKey) { nudgeSelection (shift ? 4.0 : (snap > 0.0 ? snap : 1.0)); return true; }
    // the computer keyboard plays: notes in the melodic view, pads in the drum view
    if (! cmd && getViewMode() == NoteClip::melodic)
    {
        if (computerKeys.octaveKey (c))
        {
            // keep the keys' range in view
            const int visibleRows = noteArea().getHeight() / juce::jmax (1, rowHeight);
            const int topNote = juce::jmin (127, computerKeys.baseNote() + 15);
            const int d = displayForRow (topNote);
            if (d < topDisplayRow || displayForRow (computerKeys.baseNote()) >= topDisplayRow + visibleRows)
                topDisplayRow = juce::jmax (0, d - 3);
            updateScrollBars();
            repaint();
            if (onChanged) onChanged();
            return true;
        }
        if (computerKeys.isNoteKey (c)) return true; // played in keyStateChanged
    }
    if (! cmd && getViewMode() == NoteClip::drum && computerKeys.isPadKey (c))
        return true;
    if (! cmd && c == 'q') { quantize(); return true; }
    if (! cmd && c == 'b') { tool = tool == Tool::draw ? Tool::select : Tool::draw; if (onChanged) onChanged(); return true; }
    if (k == juce::KeyPress::escapeKey) { selection.clear(); repaint(); return true; }
    if (k == juce::KeyPress::spaceKey)
    {
        auto& seq = processor.getSequencer();
        seq.togglePatternPlay();
        return true;
    }
    return false;
}

void PianoRollEditor::selectRowsBetween (int rowA, int rowB)
{
    const int dA = displayForRow (rowA), dB = displayForRow (rowB);
    const int lo = juce::jmin (dA, dB), hi = juce::jmax (dA, dB);
    selection = keyboardBaseSelection;
    for (auto& n : notes)
        if ((int) n.kind == getViewMode())
            if (const int d = displayForRow (n.row); d >= lo && d <= hi)
                selection.insert (n.id);
    repaint();
    if (onChanged) onChanged();
}

bool PianoRollEditor::keyStateChanged (bool)
{
    if (getViewMode() == NoteClip::melodic)
    {
        const bool used = computerKeys.pollNotes (processor, getTargetPad(), (float) newVelocity / 127.0f);
        if (used) repaint (keyboardArea());
        return used;
    }
    return computerKeys.pollPads (processor);
}

void PianoRollEditor::focusLost (FocusChangeType)
{
    computerKeys.releaseAll (processor);
}

//==============================================================================
void PianoRollEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    paintNotes (g);
    paintKeyboard (g);
    paintRuler (g);
    paintVelocity (g);

    g.setColour (colours::panel);
    g.fillRect (0, 0, keyboardWidth, rulerHeight);
    g.fillRect (0, velocityArea().getY(), keyboardWidth, getHeight() - velocityArea().getY());
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("VELOCITY", juce::Rectangle<int> (0, velocityArea().getY(), keyboardWidth, 20), juce::Justification::centred);
}

void PianoRollEditor::paintRuler (juce::Graphics& g)
{
    auto r = rulerArea();
    g.setColour (colours::panel);
    g.fillRect (r);
    g.saveState();
    g.reduceClipRegion (r);

    const double first = std::floor (viewStart);
    const double last = stepForX ((float) r.getRight());
    for (double s = first; s <= last; s += 1.0)
    {
        const float x = xForStep (s);
        const int step = (int) s;
        if (step % 16 == 0)
        {
            g.setColour (colours::text);
            g.drawVerticalLine ((int) x, (float) r.getY(), (float) r.getBottom());
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (step / 16 + 1), juce::Rectangle<float> (x + 3.0f, (float) r.getY(), 40.0f, (float) r.getHeight()), juce::Justification::centredLeft);
        }
        else if (step % 4 == 0 && pixelsPerStep * 4.0 > 24.0)
        {
            g.setColour (colours::dim);
            g.drawVerticalLine ((int) x, (float) r.getBottom() - 8.0f, (float) r.getBottom());
            if (pixelsPerStep * 4.0 > 50.0)
            {
                g.setFont (juce::FontOptions (9.0f));
                g.drawText (juce::String (step / 16 + 1) + "." + juce::String ((step % 16) / 4 + 1),
                            juce::Rectangle<float> (x + 2.0f, (float) r.getY(), 40.0f, (float) r.getHeight()), juce::Justification::centredLeft);
            }
        }
    }

    // end of the pattern
    const float endX = xForStep (patternLength());
    g.setColour (colours::accent);
    g.fillRect (endX - 1.0f, (float) r.getY(), 2.0f, (float) r.getHeight());

    // start marker: where PLAY starts (click the ruler to move it, hold + drag to scrub)
    {
        const float cx = xForStep (processor.getSequencer().cueSteps.load());
        juce::Path tri;
        tri.addTriangle (cx - 6.0f, (float) r.getY() + 2.0f, cx + 6.0f, (float) r.getY() + 2.0f, cx, (float) r.getY() + 11.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillPath (tri);
    }
    g.restoreState();
}

void PianoRollEditor::paintKeyboard (juce::Graphics& g)
{
    auto k = keyboardArea();
    g.saveState();
    g.reduceClipRegion (k);
    const bool melodic = getViewMode() == NoteClip::melodic;

    for (int d = topDisplayRow; d < numRows(); ++d)
    {
        const int row = rowForDisplay (d);
        const float y = yForRow (row);
        if (y > (float) k.getBottom()) break;
        auto cell = juce::Rectangle<float> ((float) k.getX(), y, (float) k.getWidth() - 2.0f, (float) rowHeight);

        if (melodic)
        {
            const bool black = isBlackKey (row);
            g.setColour (row == previewRow ? colours::accent : black ? juce::Colour (0xff1a1b1f) : juce::Colour (0xffd8d8d8));
            g.fillRect (black ? cell.withWidth (cell.getWidth() * 0.62f) : cell);
            if (black)
            {
                g.setColour (juce::Colour (0xffd8d8d8));
                g.fillRect (cell.withTrimmedLeft (cell.getWidth() * 0.62f));
            }
            // a scale: a green mark on its notes (the root brighter)
            if (! scales::isChromatic (processor.scaleType.load()) && scales::contains (processor.scaleType.load(), processor.scaleRoot.load(), row))
            {
                const bool rootRow = (((row - processor.scaleRoot.load()) % 12) + 12) % 12 == 0;
                g.setColour (rootRow ? juce::Colour (0xffe8423f) : juce::Colour (0xff53d769));
                g.fillRect (cell.withWidth (5.0f));
            }
            // the notes the computer keys play now (A ... P): amber strip, held keys light up
            const int base = computerKeys.baseNote();
            if (row >= base && row < base + 16)
            {
                g.setColour (computerKeys.isNoteHeld (row) ? colours::accent : colours::accent2.withAlpha (0.85f));
                g.fillRect (cell.withLeft (cell.getRight() - 6.0f));
                if (computerKeys.isNoteHeld (row))
                {
                    g.setColour (colours::accent.withAlpha (0.45f));
                    g.fillRect (cell);
                }
            }
            g.setColour (juce::Colour (0xff888888));
            g.drawHorizontalLine ((int) cell.getBottom(), cell.getX(), cell.getRight());
            if (row % 12 == 0 && rowHeight >= 10)
            {
                g.setColour (juce::Colours::black);
                g.setFont (juce::FontOptions (juce::jmin (11.0f, (float) rowHeight - 2.0f), juce::Font::bold));
                g.drawText (rowName (row), cell.reduced (4.0f, 0.0f), juce::Justification::centredRight);
            }
        }
        else
        {
            g.setColour (row == previewRow ? colours::accent.withAlpha (0.6f)
                         : (row / padsPerBank) % 2 == 0 ? colours::panel : colours::panelLight);
            g.fillRect (cell.reduced (0.0f, 0.5f));
            if (rowHeight >= 10)
            {
                g.setColour (colours::text);
                g.setFont (juce::FontOptions (juce::jmin (11.0f, (float) rowHeight - 3.0f), juce::Font::bold));
                g.drawText (padLabel (row), cell.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
                g.setColour (colours::dim);
                g.setFont (juce::FontOptions (juce::jmin (10.0f, (float) rowHeight - 4.0f)));
                g.drawText (processor.getPadName (row), cell.reduced (4.0f, 0.0f).withTrimmedLeft (28.0f), juce::Justification::centredLeft, true);
            }
        }
    }
    g.restoreState();
}

void PianoRollEditor::paintNotes (juce::Graphics& g)
{
    auto a = noteArea();
    g.saveState();
    g.reduceClipRegion (a);
    const bool melodic = getViewMode() == NoteClip::melodic;

    // rows
    for (int d = topDisplayRow; d < numRows(); ++d)
    {
        const int row = rowForDisplay (d);
        const float y = yForRow (row);
        if (y > (float) a.getBottom()) break;
        juce::Colour c = melodic ? (isBlackKey (row) ? juce::Colour (0xff1c1d22) : juce::Colour (0xff24262c))
                                 : ((row / padsPerBank) % 2 == 0 ? juce::Colour (0xff24262c) : juce::Colour (0xff202227));
        if (melodic && ! scales::isChromatic (processor.scaleType.load()))
        {
            // a scale: its notes light, the others dark, the root a little red
            const bool in = scales::contains (processor.scaleType.load(), processor.scaleRoot.load(), row);
            const bool rootRow = (((row - processor.scaleRoot.load()) % 12) + 12) % 12 == 0;
            c = ! in ? juce::Colour (0xff141518) : rootRow ? juce::Colour (0xff332a2d) : juce::Colour (0xff272a31);
        }
        g.setColour (c);
        g.fillRect ((float) a.getX(), y, (float) a.getWidth(), (float) rowHeight);
        g.setColour (juce::Colour (0xff1a1b1f));
        g.drawHorizontalLine ((int) (y + (float) rowHeight) - 1, (float) a.getX(), (float) a.getRight());
        if ((melodic && row % 12 == 0) || (! melodic && row % padsPerBank == padsPerBank - 1))
        {
            g.setColour (colours::outline.brighter (0.2f));
            g.drawHorizontalLine ((int) (y + (float) rowHeight) - 1, (float) a.getX(), (float) a.getRight());
        }
    }

    // every other group of 4 steps (one beat) a little lighter: 4 darker, 4 lighter ... over the whole pattern
    {
        const double lastVisible = stepForX ((float) a.getRight());
        for (double s = std::floor (viewStart / 4.0) * 4.0; s <= lastVisible; s += 4.0)
            if (((long long) (s / 4.0)) % 2 == 1)
            {
                const float x0 = xForStep (s), x1 = xForStep (s + 4.0);
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRect (x0, (float) a.getY(), x1 - x0, (float) a.getHeight());
            }
    }

    // vertical grid: snap lines, beats, bars
    const double grid = snap > 0.0 ? snap : 1.0;
    const double firstLine = std::floor (viewStart / grid) * grid;
    const double lastStep = stepForX ((float) a.getRight());
    if (grid * pixelsPerStep >= 5.0)
        for (double s = firstLine; s <= lastStep; s += grid)
        {
            g.setColour (juce::Colour (0xff2c2f36));
            g.drawVerticalLine ((int) xForStep (s), (float) a.getY(), (float) a.getBottom());
        }
    for (double s = std::floor (viewStart); s <= lastStep; s += 1.0)
    {
        const int step = (int) s;
        if (step % 4 != 0) continue;
        g.setColour (step % 16 == 0 ? colours::outline.brighter (0.35f) : colours::outline);
        g.drawVerticalLine ((int) xForStep (s), (float) a.getY(), (float) a.getBottom());
    }

    // outside the pattern
    const float endX = xForStep (patternLength());
    if (endX < (float) a.getRight())
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (endX, (float) a.getY(), (float) a.getRight(), (float) a.getBottom()));
    }

    // notes
    for (auto& n : notes)
    {
        if ((int) n.kind != getViewMode()) continue;
        auto b = noteBounds (n);
        if (b.getRight() < (float) a.getX() || b.getX() > (float) a.getRight()) continue;
        const bool sel = isSelected (n);
        const auto base = melodic ? colours::accent2 : colours::accent;
        g.setColour (base.withMultipliedBrightness (0.45f + 0.55f * (float) n.velocity / 127.0f));
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (sel ? juce::Colours::white : juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, sel ? 1.6f : 1.0f);
        // resize grip
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRect (b.getRight() - 3.0f, b.getY() + 2.0f, 2.0f, b.getHeight() - 4.0f);
        if (b.getWidth() > 30.0f && rowHeight >= 12)
        {
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.setFont (juce::FontOptions (juce::jmin (10.0f, (float) rowHeight - 4.0f)));
            g.drawText (rowName (n.row), b.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }

    // playhead
    // (drawn where the v-blank callback last put it, so the strip it repaints always matches)
    if (lastPlayheadX >= 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawVerticalLine (lastPlayheadX, (float) a.getY(), (float) a.getBottom());
    }

    if (! rubber.isEmpty())
    {
        g.setColour (colours::accent2.withAlpha (0.15f));
        g.fillRect (rubber);
        g.setColour (colours::accent2);
        g.drawRect (rubber, 1.0f);
    }
    g.restoreState();
}

void PianoRollEditor::paintVelocity (juce::Graphics& g)
{
    auto v = velocityArea();
    g.setColour (juce::Colour (0xff141518));
    g.fillRect (v);
    g.setColour (colours::outline);
    g.drawHorizontalLine (v.getY(), (float) v.getX(), (float) v.getRight());
    g.saveState();
    g.reduceClipRegion (v);
    const auto base = getViewMode() == NoteClip::melodic ? colours::accent2 : colours::accent;
    for (auto& n : notes)
    {
        if ((int) n.kind != getViewMode()) continue;
        const float x = xForStep (n.start);
        const float h = (float) (v.getHeight() - 6) * (float) n.velocity / 127.0f;
        g.setColour (isSelected (n) ? juce::Colours::white : base.withAlpha (0.85f));
        g.fillRect (x, (float) v.getBottom() - h, 3.0f, h);
        g.fillEllipse (x - 2.0f, (float) v.getBottom() - h - 3.0f, 7.0f, 7.0f);
    }
    g.restoreState();
}

//==============================================================================
PianoRollPanel::PianoRollPanel (RezalicaProcessor& p)
    : processor (p), editor (p),
      dragMidiButton ("DRAG MIDI", [this] { return processor.exportPatternMidi (processor.getSequencer().currentPattern.load(), false, true); }),
      dragWavButton ("DRAG WAV", [this] { return processor.exportPatternWav (processor.getSequencer().currentPattern.load(), false, true); }),
      metronome (p.getSequencer()),
      transport (p.getSequencer(), TransportBar::pianoPattern),
      ledBpm (p.getSequencer())
{
    ledBpm.captionColour = colours::dim;
    addAndMakeVisible (ledBpm);
    addAndMakeVisible (metronome);
    addAndMakeVisible (transport);
    metronome.setShowClickButton (false); // the metronome key is in the transport block
    dragMidiButton.setTooltip ("Drag the piano roll notes into your DAW as a MIDI clip (click: show the file)");
    dragWavButton.setTooltip ("Drag the piano roll part into your DAW as audio (rendered loop, click: show the file)");
    addAndMakeVisible (dragMidiButton);
    addAndMakeVisible (dragWavButton);
    auto toggle = [this] (juce::TextButton& b, juce::Colour on)
    {
        b.setClickingTogglesState (true);
        b.setWantsKeyboardFocus (false);
        b.setColour (juce::TextButton::buttonOnColourId, on);
        addAndMakeVisible (b);
    };
    toggle (playButton, colours::accent);
    toggle (recButton, juce::Colour (0xffe0413a));
    toggle (syncButton, colours::accent2);
    playButton.onClick = [this]
    {
        auto& s = processor.getSequencer();
        if (playButton.getToggleState()) s.playPattern(); else s.stopPlayback();
    };
    recButton.onClick = [this] { processor.getSequencer().pianoRecording = recButton.getToggleState(); };
    syncButton.onClick = [this] { processor.getSequencer().syncToHost = syncButton.getToggleState(); };
    recButton.setTooltip ("Record pads (drums) or MIDI keys (melodic) into the piano roll while it plays");

    bpmSlider.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider.setRange (40.0, 240.0, 0.1);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.setWantsKeyboardFocus (false);
    bpmSlider.setColour (juce::Slider::trackColourId, colours::panelLight.brighter (0.15f));
    bpmSlider.onValueChange = [this] { processor.getSequencer().bpm = (float) bpmSlider.getValue(); };
    addAndMakeVisible (bpmSlider);

    for (int i = 0; i < Sequencer::numPatterns; ++i)
        patternBox.addItem ("Pattern " + juce::String (i + 1), i + 1);
    patternBox.onChange = [this] { processor.getSequencer().currentPattern = patternBox.getSelectedId() - 1; editor.refreshFromSequencer(); };
    for (int len : { 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 })
        lengthBox.addItem (juce::String (len) + " steps", len);
    lengthBox.onChange = [this]
    {
        auto& s = processor.getSequencer();
        s.length[(size_t) s.currentPattern.load()] = lengthBox.getSelectedId();
        editor.repaint();
    };
    for (auto* c : { &patternBox, &lengthBox, &padBox, &snapBox })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }

    // view mode
    for (auto* b : { &drumButton, &melodicButton })
    {
        b->setRadioGroupId (5301);
        toggle (*b, colours::accent);
    }
    melodicButton.setColour (juce::TextButton::buttonOnColourId, colours::accent2);
    drumButton.setTooltip ("Rows are the 64 pads: for drums and percussion");
    melodicButton.setTooltip ("Rows are notes: the chosen pad's sample is played chromatically (C3 = original pitch)");
    drumButton.onClick = [this] { if (drumButton.getToggleState()) editor.setViewMode (NoteClip::drum); syncControls(); };
    melodicButton.onClick = [this] { if (melodicButton.getToggleState()) editor.setViewMode (NoteClip::melodic); syncControls(); };

    fillPadBox();
    padBox.setTooltip ("The pad (sample) the melodic notes play");
    padBox.onChange = [this]
    {
        const int pad = padBox.getSelectedId() - 1;
        if (pad < 0) return;
        editor.setTargetPad (pad);
        if (midiKeysButton.getToggleState())
            processor.chromaticPad = pad;
    };
    toggle (midiKeysButton, colours::accent2);
    midiKeysButton.setTooltip ("Play with MIDI keys: your MIDI keyboard plays the chosen pad chromatically instead of triggering pads "
                               "(with a scale: only its notes)");

    // ---- scales (MELODIC) ----
    for (int k = 0; k < 12; ++k)
        keyBox.addItem (scales::rootName (k), k + 1);
    keyBox.setTooltip ("The key (root note) of the scale");
    keyBox.onChange = [this] { processor.scaleRoot = keyBox.getSelectedId() - 1; editor.repaint(); };
    {
        juce::String group;
        const auto& list = scales::all();
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const juce::String g (list[(size_t) i].group);
            if (g.isNotEmpty() && g != group) { scaleBox.addSectionHeading (g); group = g; }
            scaleBox.addItem (list[(size_t) i].name, i + 1);
        }
    }
    scaleBox.setTooltip ("Scale: its notes are light in the piano roll, the others dark; drawn, moved and played notes "
                         "stick to it (also the computer keys and MIDI KEYS). Up / down arrows move notes by one scale step.");
    scaleBox.onChange = [this] { processor.scaleType = scaleBox.getSelectedId() - 1; editor.repaint(); syncControls(); };
    for (int c = 0; c < scales::numChords; ++c)
        chordBox.addItem (scales::chordName (c), c + 1);
    chordBox.setTooltip ("Chord: a note you draw becomes this chord, built from the scale (no scale: major chords)");
    chordBox.onChange = [this] { processor.chordType = chordBox.getSelectedId() - 1; };
    toScaleButton.setTooltip ("To scale: the melodic notes (the selected ones, or all) move to the nearest note of the scale");
    toScaleButton.onClick = [this] { editor.quantizeToScale(); };
    bounceButton.setWantsKeyboardFocus (false);
    bounceButton.setTooltip ("Bounce: render the piano roll notes of this pattern (with the instrument / sample and its effects) "
                             "to a WAV loop on a pad of any bank, to chop and edit like any sample");
    bounceButton.onClick = [this] { showBounceMenu(); };
    addAndMakeVisible (bounceButton);
    for (auto* c : std::initializer_list<juce::Component*> { &keyBox, &scaleBox, &chordBox, &toScaleButton })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (*c);
    }
    midiKeysButton.onClick = [this] { processor.chromaticPad = midiKeysButton.getToggleState() ? editor.getTargetPad() : -1; };

    // tools
    toggle (pencilButton, colours::accent);
    pencilButton.setTooltip ("Pencil (B): on = click to draw a note, drag to set its length.  "
                             "Shift+drag = select notes (frame).  Off = select and move notes only");
    pencilButton.onClick = [this]
    {
        editor.tool = pencilButton.getToggleState() ? PianoRollEditor::Tool::draw : PianoRollEditor::Tool::select;
        editor.grabKeyboardFocus();
    };

    humanizeButton.setWantsKeyboardFocus (false);
    humanizeButton.setTooltip ("Humanize the selected notes (or all): they move a little off the grid and their velocities vary, "
                               "like played by hand. The slider sets how much");
    humanizeButton.onClick = [this] { editor.humanize (humanizeSlider.getValue() / 100.0); };
    addAndMakeVisible (humanizeButton);
    humanizeSlider.setSliderStyle (juce::Slider::LinearBar);
    humanizeSlider.setRange (0.0, 100.0, 1.0);
    humanizeSlider.textFromValueFunction = [] (double v) { return "Amount " + juce::String ((int) v) + " %"; };
    humanizeSlider.setValue (30.0, juce::dontSendNotification);
    humanizeSlider.updateText();
    humanizeSlider.setWantsKeyboardFocus (false);
    humanizeSlider.setColour (juce::Slider::trackColourId, colours::accent2.withAlpha (0.45f));
    humanizeSlider.setTooltip ("Humanize amount: 100 % = notes up to a quarter step (1/64) early or late, velocity +-24");
    addAndMakeVisible (humanizeSlider);

    snapBox.addItem ("Snap 1/4", 1);
    snapBox.addItem ("Snap 1/8", 2);
    snapBox.addItem ("Snap 1/16", 3);
    snapBox.addItem ("Snap 1/32", 4);
    snapBox.addItem ("Snap 1/8 T", 5);
    snapBox.addItem ("Snap 1/16 T", 6);
    snapBox.addItem ("Snap off", 7);
    snapBox.setSelectedId (3, juce::dontSendNotification);
    snapBox.onChange = [this]
    {
        static const double values[] { 4.0, 2.0, 1.0, 0.5, 4.0 / 3.0, 2.0 / 3.0, 0.0 };
        editor.snap = values[juce::jlimit (0, 6, snapBox.getSelectedId() - 1)];
        editor.repaint();
    };

    for (auto* b : { &quantizeButton, &undoButton, &redoButton, &clearButton, &copyNextButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    quantizeButton.setTooltip ("Quantize the selected notes (or all) to the snap grid (Q)");
    quantizeButton.onClick = [this] { editor.quantize(); };
    undoButton.onClick = [this] { editor.undo(); };
    redoButton.onClick = [this] { editor.redo(); };
    clearButton.onClick = [this] { editor.clearAll(); };
    copyNextButton.setTooltip ("Copy this pattern (notes, steps, length, swing) to the next pattern and switch to it");
    copyNextButton.onClick = [this]
    {
        processor.getSequencer().copyToNextPattern();
        editor.refreshFromSequencer();
        syncControls();
    };

    velocitySlider.setSliderStyle (juce::Slider::LinearBar);
    velocitySlider.setRange (1.0, 127.0, 1.0);
    velocitySlider.textFromValueFunction = [] (double v) { return "Velocity " + juce::String ((int) v); };
    velocitySlider.setValue (100.0, juce::dontSendNotification);
    velocitySlider.updateText();
    velocitySlider.setWantsKeyboardFocus (false);
    velocitySlider.setColour (juce::Slider::trackColourId, colours::accent.withAlpha (0.45f));
    velocitySlider.onValueChange = [this] { editor.newVelocity = (int) velocitySlider.getValue(); };
    addAndMakeVisible (velocitySlider);

    infoLabel.setText ("KEYS: melodic A W S E D F T G Z H U J K = notes, Y / X = octave down / up; drums 1-4 Q-R A-F Z-V = pads   |   "
                       "Ruler: click = jump, hold + drag = scrub  STOP x2 = cut  |  Pencil: click = note  Shift+drag = select  edge = resize  right-click = delete  Ctrl+drag = copy  wheel / Ctrl = zoom  Ctrl+Z/Y C/V/D  Del",
                       juce::dontSendNotification);
    infoLabel.setColour (juce::Label::textColourId, colours::dim);
    infoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (infoLabel);

    editor.onChanged = [this] { syncControls(); };
    addAndMakeVisible (editor);

    setSize (1260, 700);
    syncControls();
    startTimerHz (30);
}

PianoRollPanel::~PianoRollPanel()
{
    stopTimer();
}

void PianoRollPanel::showBounceMenu()
{
    const int pattern = processor.getSequencer().currentPattern.load();
    auto isFree = [this] (int p) { return processor.getEngine().getPadSample (p) == nullptr && ! processor.padHasInstrument (p)
                                         && ! processor.isInstrumentPending (p); };
    int firstFree = -1;
    for (int p = 0; p < numPads && firstFree < 0; ++p)
        if (isFree (p)) firstFree = p;
    const int sel = processor.selectedPad.load();

    juce::PopupMenu m;
    m.addSectionHeader ("Bounce the piano roll of pattern " + juce::String (pattern + 1) + " to a pad");
    if (firstFree >= 0) m.addItem (1000 + firstFree, "First empty pad:  " + padLabel (firstFree));
    if (! processor.padHasInstrument (sel))
        m.addItem (2000 + sel, "Selected pad:  " + padLabel (sel) + (isFree (sel) ? juce::String() : "   (replaces " + processor.getPadName (sel) + ")"));
    m.addSeparator();
    for (int b = 0; b < numBanks; ++b)
    {
        juce::PopupMenu bank;
        for (int i = 0; i < padsPerBank; ++i)
        {
            const int p = b * padsPerBank + i;
            const auto name = processor.getPadName (p);
            bank.addItem (3000 + p, padLabel (p) + (name.isNotEmpty() ? "   " + name : juce::String ("   (empty)")), ! processor.padHasInstrument (p));
        }
        m.addSubMenu ("Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + b)), bank);
    }
    juce::Component::SafePointer<PianoRollPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bounceButton), [safe, pattern] (int r)
    {
        if (safe == nullptr || r < 1000) return;
        const int pad = r % 1000;
        auto& pr = safe->processor;
        pr.pushUndo ("Bounce to pad");
        juce::String err;
        if (pr.bouncePianoRollToPad (pattern, pad, err))
        {
            const auto help = safe->infoLabel.getText();
            safe->infoLabel.setText ("Bounced pattern " + juce::String (pattern + 1) + " to pad " + padLabel (pad)
                                     + "  (Documents/Rezalica/Bounced)", juce::dontSendNotification);
            juce::Timer::callAfterDelay (6000, [safe, help] { if (safe != nullptr) safe->infoLabel.setText (help, juce::dontSendNotification); });
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Bounce to pad", err);
    });
}

void PianoRollPanel::fillPadBox()
{
    const int sel = padBox.getSelectedId();
    padBox.clear (juce::dontSendNotification);
    for (int i = 0; i < numPads; ++i)
    {
        auto name = processor.getPadName (i);
        padBox.addItem (padLabel (i) + (name.isNotEmpty() ? "  " + name : juce::String()), i + 1);
    }
    padBox.setSelectedId (sel > 0 ? sel : editor.getTargetPad() + 1, juce::dontSendNotification);
}

void PianoRollPanel::syncControls()
{
    auto& s = processor.getSequencer();
    playButton.setToggleState (s.isPlayingPattern(), juce::dontSendNotification);
    recButton.setToggleState (s.pianoRecording.load(), juce::dontSendNotification);
    syncButton.setToggleState (s.syncToHost.load(), juce::dontSendNotification);
    for (int i = 0; i < Sequencer::numPatterns; ++i)   // names from RENAME in the SCENE window
    {
        const auto label = juce::String (i + 1) + ": " + s.getPatternLabel (i);
        if (patternBox.getItemText (i) != label) patternBox.changeItemText (i + 1, label);
    }
    patternBox.setSelectedId (s.currentPattern.load() + 1, juce::dontSendNotification);
    if (const auto cur = patternBox.getItemText (s.currentPattern.load()); patternBox.getText() != cur)
        patternBox.setText (cur, juce::dontSendNotification);
    lengthBox.setSelectedId (s.length[(size_t) s.currentPattern.load()].load(), juce::dontSendNotification);
    if (! bpmSlider.isMouseButtonDown())
        bpmSlider.setValue (s.syncToHost.load() ? s.effectiveBpm.load() : s.bpm.load(), juce::dontSendNotification);

    const bool melodic = editor.getViewMode() == NoteClip::melodic;
    drumButton.setToggleState (! melodic, juce::dontSendNotification);
    melodicButton.setToggleState (melodic, juce::dontSendNotification);
    padBox.setEnabled (melodic);
    midiKeysButton.setEnabled (melodic);
    if (! padBox.isPopupActive())
        padBox.setSelectedId (editor.getTargetPad() + 1, juce::dontSendNotification);
    midiKeysButton.setToggleState (processor.chromaticPad.load() >= 0, juce::dontSendNotification);
    // the scale controls belong to MELODIC
    for (auto* c : std::initializer_list<juce::Component*> { &keyBox, &scaleBox, &chordBox, &toScaleButton })
        c->setVisible (melodic);
    if (! keyBox.isPopupActive())   keyBox.setSelectedId (processor.scaleRoot.load() + 1, juce::dontSendNotification);
    if (! scaleBox.isPopupActive()) scaleBox.setSelectedId (processor.scaleType.load() + 1, juce::dontSendNotification);
    if (! chordBox.isPopupActive()) chordBox.setSelectedId (processor.chordType.load() + 1, juce::dontSendNotification);
    toScaleButton.setEnabled (! scales::isChromatic (processor.scaleType.load()));
    keyBox.setEnabled (! scales::isChromatic (processor.scaleType.load()));

    pencilButton.setToggleState (editor.tool == PianoRollEditor::Tool::draw, juce::dontSendNotification);
    undoButton.setEnabled (editor.canUndo());
    redoButton.setEnabled (editor.canRedo());
}

void PianoRollPanel::timerCallback()
{
    // while this window is the active one, the computer keyboard belongs to the piano roll
    // (also when the focus was left in another Rezalica window: then Windows would hand the keys to this
    //  window's frame instead of the piano roll)
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (auto* peer = getPeer(); peer != nullptr && peer->isFocused() && isShowing()
        && ! editor.hasKeyboardFocus (false)
        // nobody has it, or it sits on a window / the main sampler around us (not on e.g. the browser list)
        && (focused == nullptr || focused->getPeer() != peer || focused->isParentOf (this))
        && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0)
        editor.grabKeyboardFocus();
    processor.collectRecordedPianoNotes();
    editor.refreshFromSequencer();
    syncControls();
    static int counter = 0;
    if (++counter % 30 == 0 && ! padBox.isPopupActive())
        fillPadBox(); // pad names change when samples are loaded
    editor.repaint();
}

void PianoRollPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void PianoRollPanel::resized()
{
    auto b = getLocalBounds().reduced (10);
    const bool melodic = editor.getViewMode() == NoteClip::melodic;

    // (a little narrower than before, so the scale controls fit)
    auto row1 = b.removeFromTop (30);
    playButton.setVisible (false);
    recButton.setVisible (false);
    transport.setBounds (row1.removeFromLeft (transport.getIdealWidth (34))); row1.removeFromLeft (8);
    // the sampler's tempo (one tempo for everything): LED display instead of SYNC HOST + BPM bar
    syncButton.setVisible (false);
    bpmSlider.setVisible (false);
    ledBpm.setBounds (row1.removeFromLeft (80).withSizeKeepingCentre (80, 34).translated (0, -2)); row1.removeFromLeft (8);
    patternBox.setBounds (row1.removeFromLeft (86)); row1.removeFromLeft (4);
    lengthBox.setBounds (row1.removeFromLeft (92)); row1.removeFromLeft (10);
    drumButton.setBounds (row1.removeFromLeft (70)); row1.removeFromLeft (4);
    melodicButton.setBounds (row1.removeFromLeft (82)); row1.removeFromLeft (8);
    // the buttons keep their size; in a narrow window the pad and scale menus get smaller
    bounceButton.setBounds (row1.removeFromRight (juce::jmin (112, row1.getWidth()))); row1.removeFromRight (6);
    toScaleButton.setBounds (row1.removeFromRight (juce::jmin (78, row1.getWidth()))); row1.removeFromRight (4);
    const int flexible = juce::jmax (0, row1.getWidth() - 92 - 56 - 18);
    const int padW = juce::jlimit (60, 112, flexible * 2 / 5);
    padBox.setBounds (row1.removeFromLeft (padW)); row1.removeFromLeft (4);
    midiKeysButton.setBounds (row1.removeFromLeft (juce::jmin (92, row1.getWidth()))); row1.removeFromLeft (10);
    // MELODIC: key + scale + TO SCALE
    keyBox.setBounds (row1.removeFromLeft (juce::jmin (56, row1.getWidth()))); row1.removeFromLeft (4);
    scaleBox.setBounds (row1.removeFromLeft (juce::jmin (210, row1.getWidth())));

    b.removeFromTop (6);
    auto row2 = b.removeFromTop (28);
    pencilButton.setBounds (row2.removeFromLeft (40)); row2.removeFromLeft (8);
    snapBox.setBounds (row2.removeFromLeft (100)); row2.removeFromLeft (4);
    quantizeButton.setBounds (row2.removeFromLeft (84)); row2.removeFromLeft (8);
    velocitySlider.setBounds (row2.removeFromLeft (118)); row2.removeFromLeft (8);
    humanizeButton.setBounds (row2.removeFromLeft (88)); row2.removeFromLeft (4);
    humanizeSlider.setBounds (row2.removeFromLeft (112)); row2.removeFromLeft (8);
    undoButton.setBounds (row2.removeFromLeft (52)); row2.removeFromLeft (4);
    redoButton.setBounds (row2.removeFromLeft (52)); row2.removeFromLeft (8);
    clearButton.setBounds (row2.removeFromLeft (62)); row2.removeFromLeft (4);
    copyNextButton.setBounds (row2.removeFromLeft (172)); row2.removeFromLeft (8);
    dragWavButton.setBounds (row2.removeFromRight (98));
    row2.removeFromRight (6);
    dragMidiButton.setBounds (row2.removeFromRight (98));
    row2.removeFromRight (8);
    metronome.setBounds (row2.removeFromLeft (juce::jmin (116, row2.getWidth()))); row2.removeFromLeft (8);
    chordBox.setBounds (row2.removeFromLeft (juce::jmin (110, row2.getWidth()))); // MELODIC: the chord a drawn note makes
    juce::ignoreUnused (melodic);

    b.removeFromTop (6);
    infoLabel.setBounds (b.removeFromBottom (18));
    b.removeFromBottom (4);
    editor.setBounds (b);
}

} // namespace rz

void rz::PianoRollPanel::PencilButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto& lf = getLookAndFeel();
    lf.drawButtonBackground (g, *this, findColour (getToggleState() ? buttonOnColourId : buttonColourId), highlighted, down);

    // a pencil, pointing down-left
    auto r = getLocalBounds().toFloat().reduced (getHeight() * 0.2f).withSizeKeepingCentre (getHeight() * 0.62f, getHeight() * 0.62f);
    const float s = r.getWidth() / 24.0f;
    auto pt = [&] (float x, float y) { return juce::Point<float> (r.getX() + x * s, r.getY() + y * s); };
    juce::Path body;
    body.startNewSubPath (pt (5, 14)); body.lineTo (pt (16, 3)); body.lineTo (pt (21, 8)); body.lineTo (pt (10, 19)); body.closeSubPath();
    juce::Path tip;
    tip.startNewSubPath (pt (2, 22)); tip.lineTo (pt (5, 14)); tip.lineTo (pt (10, 19)); tip.closeSubPath();
    juce::Path eraser;
    eraser.startNewSubPath (pt (16, 3)); eraser.lineTo (pt (18.5f, 0.5f)); eraser.lineTo (pt (23.5f, 5.5f)); eraser.lineTo (pt (21, 8)); eraser.closeSubPath();
    const bool on = getToggleState();
    const auto ink = juce::Colours::black.withAlpha (0.85f);
    g.setColour (on ? juce::Colour (0xffffc94a) : juce::Colour (0xffd9c27a));
    g.fillPath (body);
    g.setColour (juce::Colour (0xffe8cfa0));
    g.fillPath (tip);
    g.setColour (juce::Colour (0xffff8fa3));
    g.fillPath (eraser);
    g.setColour (ink);
    const float w = juce::jmax (1.0f, 1.3f * s);
    g.strokePath (body, juce::PathStrokeType (w));
    g.strokePath (tip, juce::PathStrokeType (w));
    g.strokePath (eraser, juce::PathStrokeType (w));
    g.fillEllipse (juce::Rectangle<float> (3.2f * s, 3.2f * s).withCentre (pt (2.8f, 20.6f)));
}
