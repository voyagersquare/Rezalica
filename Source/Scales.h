#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace rz
{
/** Musical scales for the MELODIC piano roll: which notes belong to the key, snapping a note to the
    nearest one, and chords built from the scale. Scale 0 is "Chromatic" (no scale: every note). */
namespace scales
{
struct Scale
{
    const char* name;
    const char* group;              // shown as a heading in the menu
    std::vector<int> steps;         // semitones above the root (0 .. 11)
};

inline const std::vector<Scale>& all()
{
    static const std::vector<Scale> list {
        { "Chromatic (no scale)",          "",                         { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        // major / minor
        { "Major (Ionian)",                "Major / minor",            { 0, 2, 4, 5, 7, 9, 11 } },
        { "Natural Minor (Aeolian)",       "Major / minor",            { 0, 2, 3, 5, 7, 8, 10 } },
        { "Harmonic Minor",                "Major / minor",            { 0, 2, 3, 5, 7, 8, 11 } },
        { "Melodic Minor",                 "Major / minor",            { 0, 2, 3, 5, 7, 9, 11 } },
        // modes
        { "Dorian",                        "Modes",                    { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygian",                      "Modes",                    { 0, 1, 3, 5, 7, 8, 10 } },
        { "Lydian",                        "Modes",                    { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian",                    "Modes",                    { 0, 2, 4, 5, 7, 9, 10 } },
        { "Locrian",                       "Modes",                    { 0, 1, 3, 5, 6, 8, 10 } },
        { "Lydian Dominant",               "Modes",                    { 0, 2, 4, 6, 7, 9, 10 } },
        { "Altered (Super Locrian)",       "Modes",                    { 0, 1, 3, 4, 6, 8, 10 } },
        // pentatonic / blues
        { "Major Pentatonic",              "Pentatonic / blues",       { 0, 2, 4, 7, 9 } },
        { "Minor Pentatonic",              "Pentatonic / blues",       { 0, 3, 5, 7, 10 } },
        { "Blues",                         "Pentatonic / blues",       { 0, 3, 5, 6, 7, 10 } },
        { "Major Blues",                   "Pentatonic / blues",       { 0, 2, 3, 4, 7, 9 } },
        { "Egyptian (Suspended)",          "Pentatonic / blues",       { 0, 2, 5, 7, 10 } },
        // Balkan, oriental, Gypsy
        { "Phrygian Dominant (Hijaz)",     "Balkan / oriental",        { 0, 1, 4, 5, 7, 8, 10 } },
        { "Double Harmonic (Byzantine)",   "Balkan / oriental",        { 0, 1, 4, 5, 7, 8, 11 } },
        { "Hungarian Minor (Gypsy)",       "Balkan / oriental",        { 0, 2, 3, 6, 7, 8, 11 } },
        { "Ukrainian Dorian (Romanian)",   "Balkan / oriental",        { 0, 2, 3, 6, 7, 9, 10 } },
        { "Persian",                       "Balkan / oriental",        { 0, 1, 4, 5, 6, 8, 11 } },
        { "Neapolitan Minor",              "Balkan / oriental",        { 0, 1, 3, 5, 7, 8, 11 } },
        { "Neapolitan Major",              "Balkan / oriental",        { 0, 1, 3, 5, 7, 9, 11 } },
        { "Spanish 8-tone",                "Balkan / oriental",        { 0, 1, 3, 4, 5, 6, 8, 10 } },
        // Japanese
        { "Hirajoshi",                     "Japanese",                 { 0, 2, 3, 7, 8 } },
        { "In-Sen",                        "Japanese",                 { 0, 1, 5, 7, 10 } },
        { "Iwato",                         "Japanese",                 { 0, 1, 5, 6, 10 } },
        { "Kumoi",                         "Japanese",                 { 0, 2, 3, 7, 9 } },
        // jazz / symmetric / others
        { "Bebop Dominant",                "Jazz / symmetric / other", { 0, 2, 4, 5, 7, 9, 10, 11 } },
        { "Bebop Major",                   "Jazz / symmetric / other", { 0, 2, 4, 5, 7, 8, 9, 11 } },
        { "Whole Tone",                    "Jazz / symmetric / other", { 0, 2, 4, 6, 8, 10 } },
        { "Diminished (Half-Whole)",       "Jazz / symmetric / other", { 0, 1, 3, 4, 6, 7, 9, 10 } },
        { "Diminished (Whole-Half)",       "Jazz / symmetric / other", { 0, 2, 3, 5, 6, 8, 9, 11 } },
        { "Prometheus",                    "Jazz / symmetric / other", { 0, 2, 4, 6, 9, 10 } },
        { "Enigmatic",                     "Jazz / symmetric / other", { 0, 1, 4, 6, 8, 10, 11 } },
    };
    return list;
}

inline const char* rootName (int root)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((root % 12) + 12) % 12];
}

inline bool isChromatic (int scale) { return scale <= 0 || scale >= (int) all().size(); }

/** Is this MIDI note in the scale (with the given root, 0 = C)? */
inline bool contains (int scale, int root, int note)
{
    if (isChromatic (scale)) return true;
    const int pc = (((note - root) % 12) + 12) % 12;
    for (int s : all()[(size_t) scale].steps)
        if (s == pc) return true;
    return false;
}

/** The nearest note of the scale (on a tie, the lower one). */
inline int snap (int scale, int root, int note)
{
    if (isChromatic (scale)) return juce::jlimit (0, 127, note);
    for (int d = 0; d < 12; ++d)
    {
        if (note - d >= 0 && contains (scale, root, note - d)) return note - d;
        if (note + d <= 127 && contains (scale, root, note + d)) return note + d;
    }
    return juce::jlimit (0, 127, note);
}

/** The note 'degrees' scale steps above (or below) a note of the scale. */
inline int stepBy (int scale, int root, int note, int degrees)
{
    if (isChromatic (scale)) return juce::jlimit (0, 127, note + degrees);
    int n = snap (scale, root, note);
    const int dir = degrees >= 0 ? 1 : -1;
    for (int k = 0; k < std::abs (degrees); ++k)
    {
        do { n += dir; } while (n > 0 && n < 127 && ! contains (scale, root, n));
    }
    return juce::jlimit (0, 127, n);
}

enum Chord { chordOff = 0, chordTriad, chordSeventh, chordNinth, chordSus2, chordSus4, chordPower, numChords };

inline const char* chordName (int c)
{
    static const char* names[] = { "No chord", "Triad", "7th chord", "9th chord", "Sus2", "Sus4", "Power (5th)" };
    return names[juce::jlimit (0, numChords - 1, c)];
}

/** The notes of a chord on this note: stacked thirds of the scale (with no scale: major chords). */
inline std::vector<int> chord (int c, int scale, int root, int note)
{
    std::vector<int> out { note };
    auto add = [&] (int n) { if (n >= 0 && n <= 127) out.push_back (n); };
    const bool chromatic = isChromatic (scale);
    switch (c)
    {
        case chordTriad:   if (chromatic) { add (note + 4); add (note + 7); }
                           else { add (stepBy (scale, root, note, 2)); add (stepBy (scale, root, note, 4)); } break;
        case chordSeventh: if (chromatic) { add (note + 4); add (note + 7); add (note + 10); }
                           else { add (stepBy (scale, root, note, 2)); add (stepBy (scale, root, note, 4)); add (stepBy (scale, root, note, 6)); } break;
        case chordNinth:   if (chromatic) { add (note + 4); add (note + 7); add (note + 10); add (note + 14); }
                           else { for (int d : { 2, 4, 6, 8 }) add (stepBy (scale, root, note, d)); } break;
        case chordSus2:    if (chromatic) { add (note + 2); add (note + 7); }
                           else { add (stepBy (scale, root, note, 1)); add (stepBy (scale, root, note, 4)); } break;
        case chordSus4:    if (chromatic) { add (note + 5); add (note + 7); }
                           else { add (stepBy (scale, root, note, 3)); add (stepBy (scale, root, note, 4)); } break;
        case chordPower:   add (note + 7); add (note + 12); break;
        default: break;
    }
    return out;
}
} // namespace scales
} // namespace rz
