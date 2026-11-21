#pragma once

#include "PluginProcessor.h"

namespace rz
{
/** Plays Rezalica from the computer keyboard in the pattern windows.
    Pads: 1 2 3 4 / Q W E R / A S D F / Z X C V (16 pads of the current bank).
    Notes (melodic piano roll): A W S E D F T G Y(Z) H U J K O L P = C ... D#, X / C = octave down / up. */
class ComputerKeyboard
{
public:
    static constexpr const char* padKeys = "1234qwerasdfzxcv";
    static constexpr const char* noteKeys = "awsedftgyhujkolp";

    /** Pads of the current bank. Returns true if a key went up or down. */
    bool pollPads (RezalicaProcessor& p)
    {
        bool used = false;
        if (juce::ModifierKeys::getCurrentModifiers().isCommandDown()) return false;
        for (int i = 0; i < 16; ++i)
        {
            const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) padKeys[i]);
            if (down == padDown[(size_t) i]) continue;
            padDown[(size_t) i] = down;
            if (down) padStarted[(size_t) i] = p.currentBank.load() * padsPerBank + i;
            p.triggerPadFromUi (padStarted[(size_t) i], down ? 1.0f : 0.0f, down);
            used = true;
        }
        return used;
    }

    /** Chromatic notes of a pad (C3 = original pitch); recorded when the piano roll REC is on. */
    bool pollNotes (RezalicaProcessor& p, int pad, float velocity)
    {
        bool used = false;
        if (juce::ModifierKeys::getCurrentModifiers().isCommandDown()) return false;
        for (int i = 0; i < 16; ++i)
        {
            bool down = juce::KeyPress::isKeyCurrentlyDown ((int) noteKeys[i]);
            if (noteKeys[i] == 'y') down = down || juce::KeyPress::isKeyCurrentlyDown ((int) 'z'); // QWERTZ keyboards
            if (down == noteDown[(size_t) i]) continue;
            noteDown[(size_t) i] = down;
            if (down) { noteStarted[(size_t) i] = juce::jlimit (0, 127, 60 + octave * 12 + i); notePad[(size_t) i] = pad; }
            p.previewNoteFromUi (notePad[(size_t) i], down ? velocity : 0.0f, (float) (noteStarted[(size_t) i] - 60), down, true);
            used = true;
        }
        return used;
    }

    /** X / C change the octave of the note keys. */
    bool octaveKey (juce::juce_wchar c)
    {
        if (c == 'x') { octave = juce::jmax (-4, octave - 1); return true; }
        if (c == 'c') { octave = juce::jmin (4, octave + 1); return true; }
        return false;
    }

    bool isPadKey (juce::juce_wchar c) const  { for (int i = 0; padKeys[i] != 0; ++i) if (c == (juce::juce_wchar) padKeys[i]) return true; return false; }
    bool isNoteKey (juce::juce_wchar c) const { for (int i = 0; noteKeys[i] != 0; ++i) if (c == (juce::juce_wchar) noteKeys[i]) return true; return c == 'z'; }

    /** Releases everything (e.g. when the window loses focus or the mode changes). */
    void releaseAll (RezalicaProcessor& p)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (padDown[(size_t) i]) p.triggerPadFromUi (padStarted[(size_t) i], 0.0f, false);
            if (noteDown[(size_t) i]) p.previewNoteFromUi (notePad[(size_t) i], 0.0f, (float) (noteStarted[(size_t) i] - 60), false, true);
            padDown[(size_t) i] = noteDown[(size_t) i] = false;
        }
    }

    int octave = 0;

private:
    std::array<bool, 16> padDown {}, noteDown {};
    std::array<int, 16> padStarted {}, noteStarted {}, notePad {};
};
} // namespace rz
