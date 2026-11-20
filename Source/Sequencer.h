#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SamplerEngine.h"
#include <cmath>
#include <vector>

namespace rz
{
//==============================================================================
/** One note of the piano roll. Times are in steps (1 step = 1/16 note). */
struct PianoNote
{
    juce::uint32 id = 0;      // stable identity (used to match note-on / note-off)
    float start = 0.0f;       // steps
    float length = 1.0f;      // steps
    int row = 0;              // drum note: pad 0..63, melodic note: MIDI note 0..127
    juce::uint8 velocity = 100;
    juce::uint8 kind = 0;     // NoteClip::drum or NoteClip::melodic
};

/** The piano roll content of one pattern. Immutable once handed to the sequencer:
    the editor creates a new clip for every change (cheap, and safe for the audio thread). */
struct NoteClip : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<NoteClip>;
    enum Mode { drum = 0, melodic = 1 };

    int mode = melodic;       // which part the piano roll shows / edits (both parts always play)
    int targetPad = 0;        // melodic notes play this pad's sample chromatically (C3 = original pitch)
    std::vector<PianoNote> notes;

    Ptr copy() const
    {
        Ptr c = new NoteClip();
        c->mode = mode;
        c->targetPad = targetPad;
        c->notes = notes;
        return c;
    }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("CLIP");
        t.setProperty ("mode", mode, nullptr);
        t.setProperty ("target", targetPad, nullptr);
        juce::MemoryOutputStream mo;
        for (auto& n : notes)
        {
            mo.writeFloat (n.start);
            mo.writeFloat (n.length);
            mo.writeShort ((short) n.row);
            mo.writeByte ((char) n.velocity);
            mo.writeByte ((char) n.kind);
        }
        t.setProperty ("notes", mo.getMemoryBlock().toBase64Encoding(), nullptr);
        return t;
    }

    static Ptr fromValueTree (const juce::ValueTree& t, juce::uint32& nextId)
    {
        Ptr c = new NoteClip();
        c->mode = (int) t.getProperty ("mode", 0);
        c->targetPad = (int) t.getProperty ("target", 0);
        juce::MemoryBlock mb;
        mb.fromBase64Encoding (t.getProperty ("notes").toString());
        juce::MemoryInputStream mi (mb, false);
        while (mi.getNumBytesRemaining() >= 12)
        {
            PianoNote n;
            n.start = mi.readFloat();
            n.length = mi.readFloat();
            n.row = mi.readShort();
            n.velocity = (juce::uint8) mi.readByte();
            n.kind = (juce::uint8) mi.readByte();
            n.id = ++nextId;
            c->notes.push_back (n);
        }
        return c;
    }
};

//==============================================================================
/** Step sequencer (64 pads x up to 128 steps of 1/16) plus a piano roll per pattern.
    Both play together, following the host transport or their own clock. */
class Sequencer
{
public:
    static constexpr int numPatterns = 8;
    static constexpr int maxSteps = 128;
    static constexpr double stepLengthPpq = 0.25; // 1/16
    static constexpr int midiTagBase = 1 << 30;   // tags used for chromatic MIDI notes

    Sequencer()
    {
        for (auto& pat : steps)
            for (auto& row : pat)
                for (auto& s : row)
                    s = 0;
        for (auto& pat : nudges)
            for (auto& row : pat)
                for (auto& n : row)
                    n = 0;
        for (auto& l : length)
            l = 16;
        resetSong();
        for (int i = 0; i < numPatterns; ++i)
        {
            clips[(size_t) i] = new NoteClip();
            clipPool.add (clips[(size_t) i]);
        }
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        internalPpq = 0.0;
        lastPpqEnd = -1.0e9;
        numActive = 0;
    }

    // ---- step data (any thread) ----
    juce::uint8 getStep (int pattern, int pad, int step) const
    {
        return steps[(size_t) pattern][(size_t) pad][(size_t) step].load();
    }

    void setStep (int pattern, int pad, int step, juce::uint8 velocity)
    {
        steps[(size_t) pattern][(size_t) pad][(size_t) step] = velocity;
    }

    void clearPattern (int pattern)
    {
        for (auto& row : steps[(size_t) pattern])
            for (auto& s : row)
                s = 0;
        for (auto& row : nudges[(size_t) pattern])
            for (auto& n : row)
                n = 0;
    }

    /** Timing offset of a step in % of a step: -50 = half a step early ... +50 = half a step late. */
    int getNudge (int pattern, int pad, int step) const { return nudges[(size_t) pattern][(size_t) pad][(size_t) step].load(); }
    void setNudge (int pattern, int pad, int step, int percent)
    {
        nudges[(size_t) pattern][(size_t) pad][(size_t) step] = (juce::int8) juce::jlimit (-50, 50, percent);
    }

    juce::MemoryBlock getNudgeData (int pattern) const
    {
        juce::MemoryBlock mb ((size_t) (numPads * maxSteps));
        auto* d = static_cast<juce::int8*> (mb.getData());
        for (int p = 0; p < numPads; ++p)
            for (int s = 0; s < maxSteps; ++s)
                d[p * maxSteps + s] = (juce::int8) getNudge (pattern, p, s);
        return mb;
    }

    void setNudgeData (int pattern, const juce::MemoryBlock& mb)
    {
        for (auto& row : nudges[(size_t) pattern])
            for (auto& n : row)
                n = 0;
        if (mb.getSize() < (size_t) (numPads * maxSteps))
            return;
        auto* d = static_cast<const juce::int8*> (mb.getData());
        for (int p = 0; p < numPads; ++p)
            for (int s = 0; s < maxSteps; ++s)
                setNudge (pattern, p, s, d[p * maxSteps + s]);
    }

    juce::MemoryBlock getPatternData (int pattern) const
    {
        juce::MemoryBlock mb ((size_t) (numPads * maxSteps));
        auto* d = static_cast<juce::uint8*> (mb.getData());
        for (int p = 0; p < numPads; ++p)
            for (int s = 0; s < maxSteps; ++s)
                d[p * maxSteps + s] = getStep (pattern, p, s);
        return mb;
    }

    /** stride = steps per pad in the data (older versions stored 32). */
    void setPatternData (int pattern, const juce::MemoryBlock& mb, int stride = maxSteps)
    {
        clearPattern (pattern);
        stride = juce::jlimit (1, maxSteps, stride);
        const int padsInData = (int) (mb.getSize() / (size_t) stride);
        auto* d = static_cast<const juce::uint8*> (mb.getData());
        for (int p = 0; p < juce::jmin (numPads, padsInData); ++p)
            for (int s = 0; s < stride; ++s)
                setStep (pattern, p, s, d[p * stride + s]);
    }

    // ---- piano roll clips (message thread) ----
    NoteClip::Ptr getClip (int pattern) const
    {
        const juce::SpinLock::ScopedLockType sl (clipLock);
        return clips[(size_t) pattern];
    }

    void setClip (int pattern, NoteClip::Ptr c)
    {
        clipPool.add (c);
        {
            const juce::SpinLock::ScopedLockType sl (clipLock);
            clips[(size_t) pattern] = c;
            ++clipVersion;
        }
        // free clips nobody uses any more (only the pool holds them)
        for (int i = clipPool.size(); --i >= 0;)
            if (clipPool.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
                clipPool.remove (i);
    }

    juce::uint32 newNoteId() noexcept { return ++nextNoteId; }
    juce::uint32& noteIdCounter() noexcept { return nextNoteId; }

    //==============================================================================
    /** Callbacks: stepHit (pad, velocity, offset), noteOn (clip mode, pad or note, velocity, offset, tag, targetPad),
        noteOff (clip mode, row, offset, tag, targetPad). */
    template <typename StepHit, typename NoteOn, typename NoteOff>
    void process (const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos, int numSamples,
                  StepHit&& stepHit, NoteOn&& noteOn, NoteOff&& noteOff)
    {
        syncClips();

        const bool sync = syncToHost.load();
        double tempo = bpm.load();
        if (sync && pos.hasValue() && pos->getBpm().hasValue() && *pos->getBpm() > 0.0)
            tempo = *pos->getBpm();
        effectiveBpm = (float) tempo;

        const bool hostPlaying = sync && pos.hasValue() && pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        const bool internal = internalPlay.load();

        const bool restart = restartRequested.exchange (false); // always consume the request
        if ((internal && ! wasInternal) || restart)
        {
            internalPpq = 0.0;
            if (running && ! hostPlaying)
                releaseAllNotes (0, noteOff);
            lastPpqEnd = -1.0e9;
        }
        wasInternal = internal;

        ppqPerSample = tempo / 60.0 / sampleRate;

        double ppqStart;
        if (hostPlaying)
            ppqStart = *pos->getPpqPosition();
        else if (internal)
            ppqStart = internalPpq;
        else
        {
            if (running)
                releaseAllNotes (0, noteOff);
            running = false;
            currentStep = -1;
            return;
        }

        // ignore tiny overlaps caused by host rounding so steps don't fire twice
        if (ppqStart < lastPpqEnd && ppqStart > lastPpqEnd - 1.0e-3)
            ppqStart = lastPpqEnd;
        // the host jumped backwards (loop / relocate): end all piano roll notes
        if (running && ppqStart < lastPpqEnd - 1.0e-3)
            releaseAllNotes (0, noteOff);

        running = true;
        const double ppqEnd = ppqStart + numSamples * ppqPerSample;
        blockPpq = ppqStart;
        lastPpqEnd = ppqEnd;
        if (! hostPlaying)
            internalPpq = ppqEnd;

        const double swingOffset = juce::jlimit (0.0f, 1.0f, swing.load()) * stepLengthPpq * 0.5;
        auto toOffset = [&] (double t) { return juce::jlimit (0, numSamples - 1, (int) ((t - ppqStart) / ppqPerSample)); };

        // piano roll note-offs that fall into this block
        for (int i = 0; i < numActive;)
        {
            auto& a = active[(size_t) i];
            if (a.offPpq < ppqEnd)
            {
                noteOff (a.mode, a.row, toOffset (juce::jmax (ppqStart, a.offPpq)), (int) a.id, a.targetPad);
                active[(size_t) i] = active[(size_t) --numActive];
            }
            else
                ++i;
        }

        // plays one pattern (steps + piano roll) for the ppq range [a, b); the pattern starts at 'origin'.
        // wrap = loop the pattern forever (pattern mode), otherwise it plays once (song mode)
        auto renderRange = [&] (int pattern, double origin, bool wrap, double a, double b)
        {
            if (pattern < 0 || pattern >= numPatterns || b <= a)
                return;
            const int len = juce::jlimit (1, maxSteps, length[(size_t) pattern].load());

            // steps (each one may be nudged up to half a step early or late)
            for (long long k = (long long) std::floor ((a - origin) / stepLengthPpq) - 1;; ++k)
            {
                const double base = origin + (double) k * stepLengthPpq;
                if (base - stepLengthPpq * 0.5 >= b)
                    break;
                if (! wrap && k >= len)
                    break;
                if (! wrap && k < 0)
                    continue;

                const int stp = wrap ? (int) (((k % len) + len) % len) : (int) k;
                if (base >= a && base < b)
                    currentStep = stp;

                const double onGrid = base + ((k & 1) != 0 ? swingOffset : 0.0);
                for (int pad = 0; pad < numPads; ++pad)
                    if (auto v = getStep (pattern, pad, stp))
                    {
                        const double t = onGrid + getNudge (pattern, pad, stp) * 0.01 * stepLengthPpq;
                        if (t >= a && t < b)
                            stepHit (pad, (float) v / 127.0f, toOffset (t));
                    }
            }

            // piano roll note-ons
            if (auto* clip = audioClips[(size_t) pattern].get(); clip != nullptr && ! clip->notes.empty())
            {
                const double loopPpq = len * stepLengthPpq;
                const long long firstLoop = wrap ? (long long) std::floor ((a - origin) / loopPpq) : 0;
                const long long lastLoop = wrap ? (long long) std::floor ((b - origin) / loopPpq) : 0;

                for (long long loop = firstLoop; loop <= lastLoop; ++loop)
                    for (auto& n : clip->notes)
                    {
                        if (n.start >= (float) len)
                            continue;
                        double t = origin + loop * loopPpq + n.start * stepLengthPpq;
                        const double frac = n.start - std::floor (n.start);
                        if (frac < 1.0e-4 && (((long long) std::floor (n.start)) & 1) != 0)
                            t += swingOffset; // swing notes that sit on odd 16ths
                        if (t < a || t >= b)
                            continue;

                        const double lenPpq = juce::jmin ((double) n.length, (double) len) * stepLengthPpq;
                        const int offset = toOffset (t);
                        noteOn ((int) n.kind, n.row, (float) n.velocity / 127.0f, offset, (int) n.id, clip->targetPad);

                        if (numActive < (int) active.size())
                            active[(size_t) numActive++] = { n.id, t + lenPpq * 0.995, (int) n.kind, n.row, clip->targetPad };
                        else
                            noteOff ((int) n.kind, n.row, offset, (int) n.id, clip->targetPad);
                    }
            }
        };

        if (songMode.load() && songLengthSteps() > 0)  // an empty song plays the selected pattern
        {
            // SONG: the song steps play their scenes, the scenes play their patterns one after another
            auto& segs = songSegs; // member: no big allocation on the audio thread
            double total = 0.0;
            const int n = buildSong (segs.data(), maxSegments, total);
            const bool loopSong = songLoop.load();

            if (n == 0 || (! loopSong && ppqStart >= total))
            {
                if (n > 0 && ! hostPlaying)
                    internalPlay = false; // the song is over
                currentStep = -1;
                songPosStep = -1;
                songPosSlot = -1;
                patternPosSteps = -1.0f;
                songPosPpq = -1.0f;
            }
            else
            {
                songPosPpq = (float) (loopSong ? std::fmod (ppqStart, total) : ppqStart); // for the SCENE timer
                const long long c0 = loopSong ? (long long) std::floor (ppqStart / total) : 0;
                const long long c1 = loopSong ? (long long) std::floor ((ppqEnd - 1.0e-9) / total) : 0;
                for (long long c = c0; c <= c1; ++c)
                    for (int si = 0; si < n; ++si)
                    {
                        const auto& sg = segs[(size_t) si];
                        const double segAbs = (double) c * total + sg.start;
                        const double a = juce::jmax (ppqStart, segAbs), b = juce::jmin (ppqEnd, segAbs + sg.length);
                        if (ppqStart >= segAbs && ppqStart < segAbs + sg.length)
                        {
                            songPosStep = sg.songStep;
                            songPosSlot = sg.slot;
                            if (sg.pattern >= 0)
                                currentPattern = sg.pattern; // the pattern windows follow the song
                            patternPosSteps = (float) ((ppqStart - segAbs) / stepLengthPpq);
                        }
                        if (a < b)
                            renderRange (sg.pattern, segAbs, false, a, b);
                    }
            }
        }
        else
        {
            const int pattern = currentPattern.load();
            const int len = juce::jlimit (1, maxSteps, length[(size_t) pattern].load());
            const double s = ppqStart / stepLengthPpq;
            patternPosSteps = (float) std::fmod (std::fmod (s, (double) len) + len, (double) len);
            songPosStep = -1;
            songPosPpq = -1.0f;
            renderRange (pattern, 0.0, true, ppqStart, ppqEnd);
        }

        // note-offs of notes that start and end inside this block
        for (int i = 0; i < numActive;)
        {
            auto& a = active[(size_t) i];
            if (a.offPpq < ppqEnd)
            {
                noteOff (a.mode, a.row, toOffset (juce::jmax (ppqStart, a.offPpq)), (int) a.id, a.targetPad);
                active[(size_t) i] = active[(size_t) --numActive];
            }
            else
                ++i;
        }
    }

    /** Writes a live pad hit into the current step pattern (when recording). */
    void recordHit (int pad, float velocity, int sampleOffset)
    {
        if (! recording.load() || ! running)
            return;

        const int pattern = currentPattern.load();
        const int len = juce::jlimit (1, maxSteps, length[(size_t) pattern].load());
        const double ppq = blockPpq + sampleOffset * ppqPerSample;
        const long long k = std::llround (ppq / stepLengthPpq);
        const int step = (int) (((k % len) + len) % len);
        setStep (pattern, pad, step, (juce::uint8) juce::jlimit (1, 127, (int) (velocity * 127.0f)));
    }

    // ---- live recording into the piano roll (audio thread -> fifo -> message thread) ----
    struct RecordedNote { int pattern; int row; float start; float length; juce::uint8 velocity; juce::uint8 kind; };

    /** row: pad (drum) or MIDI note (melodic, from chromatic MIDI). */
    void recordPianoNoteOn (int row, float velocity, int sampleOffset, bool melodic)
    {
        if (! pianoRecording.load() || ! running || row < 0 || row > 127)
            return;
        held[melodic ? 1 : 0][(size_t) row] = { true, blockPpq + sampleOffset * ppqPerSample, (juce::uint8) juce::jlimit (1, 127, (int) (velocity * 127.0f)) };
    }

    void recordPianoNoteOff (int row, int sampleOffset, bool melodic)
    {
        if (row < 0 || row > 127 || ! held[melodic ? 1 : 0][(size_t) row].down)
            return;
        auto& h = held[melodic ? 1 : 0][(size_t) row];
        h.down = false;
        if (! running)
            return;

        const int pattern = currentPattern.load();
        const int len = juce::jlimit (1, maxSteps, length[(size_t) pattern].load());
        const double loopSteps = len;
        const double startSteps = h.startPpq / stepLengthPpq;
        double lengthSteps = (blockPpq + sampleOffset * ppqPerSample - h.startPpq) / stepLengthPpq;
        lengthSteps = juce::jlimit (0.05, loopSteps, lengthSteps);
        const double inLoop = std::fmod (std::fmod (startSteps, loopSteps) + loopSteps, loopSteps);

        auto w = recordFifo.write (1);
        w.forEach ([&] (int i) { recorded[(size_t) i] = { pattern, row, (float) inLoop, (float) lengthSteps, h.velocity, (juce::uint8) (melodic ? 1 : 0) }; });
    }

    /** Message thread: collects notes recorded since the last call. */
    std::vector<RecordedNote> takeRecordedNotes()
    {
        std::vector<RecordedNote> out;
        auto r = recordFifo.read (recordFifo.getNumReady());
        r.forEach ([&] (int i) { out.push_back (recorded[(size_t) i]); });
        return out;
    }

    double getSamplesPerStep() const noexcept { return ppqPerSample > 0.0 ? stepLengthPpq / ppqPerSample : sampleRate * 0.125; }
    bool isRunning() const noexcept { return running; }
    /** Play position inside the current pattern, in steps (for the piano roll playhead), -1 when stopped. */
    double getPlayPositionSteps() const noexcept
    {
        return running ? (double) patternPosSteps.load() : -1.0;
    }

    //==============================================================================
    // SONG: the scene is a container of up to 128 patterns, the song repeats the scene on its active steps
    static constexpr int numScenes = 1, slotsPerScene = 128, maxSongSteps = 64, maxSegments = maxSongSteps * slotsPerScene;
    struct SongSegment { double start = 0.0, length = 0.0; int pattern = -1, songStep = -1, slot = -1; };

    int getSceneSlot (int scene, int slot) const { return sceneSlots[(size_t) scene][(size_t) slot].load(); }
    void setSceneSlot (int scene, int slot, int pattern) { sceneSlots[(size_t) scene][(size_t) slot] = (juce::int8) juce::jlimit (-1, numPatterns - 1, pattern); }
    int getSongStep (int step) const { return songSteps[(size_t) step].load(); }
    void setSongStep (int step, int scene) { songSteps[(size_t) step] = (juce::int8) juce::jlimit (-1, numScenes - 1, scene); }

    /** Length of a scene in steps (its patterns one after another; empty scene = 1 bar). */
    int sceneLengthSteps (int scene) const
    {
        int total = 0;
        for (int sl = 0; sl < slotsPerScene; ++sl)
            if (const int p = getSceneSlot (scene, sl); p >= 0)
                total += juce::jlimit (1, maxSteps, length[(size_t) p].load());
        return total > 0 ? total : 16;
    }

    /** Number of song steps (up to the last one that is set). */
    int songLengthSteps() const
    {
        for (int st = maxSongSteps; --st >= 0;)
            if (getSongStep (st) >= 0) return st + 1;
        return 0;
    }

    /** The song as a timeline: one segment per pattern (empty song step / scene = 1 bar of silence). */
    int buildSong (std::vector<SongSegment>& out, double& total) const
    {
        out.resize ((size_t) maxSegments);
        const int n = buildSong (out.data(), maxSegments, total);
        out.resize ((size_t) n);
        return n;
    }

    int buildSong (SongSegment* out, int capacity, double& total) const
    {
        const int maxSegs = juce::jmin (capacity, maxSegments);
        int n = 0;
        double pos = 0.0;
        const int last = songLengthSteps();
        for (int st = 0; st < last; ++st)
        {
            const int sc = getSongStep (st);
            bool any = false;
            if (sc >= 0)
                for (int sl = 0; sl < slotsPerScene; ++sl)
                    if (const int p = getSceneSlot (sc, sl); p >= 0 && n < maxSegs)
                    {
                        const double len = juce::jlimit (1, maxSteps, length[(size_t) p].load()) * stepLengthPpq;
                        out[(size_t) n++] = { pos, len, p, st, sl };
                        pos += len;
                        any = true;
                    }
            if (! any && n < maxSegs)
            {
                out[(size_t) n++] = { pos, 4.0, -1, st, -1 }; // a bar of silence
                pos += 4.0;
            }
        }
        total = pos;
        return n;
    }

    juce::ValueTree songToValueTree() const
    {
        juce::ValueTree t ("SONG");
        t.setProperty ("mode", songMode.load(), nullptr);
        t.setProperty ("loop", songLoop.load(), nullptr);
        juce::StringArray sc, so;
        for (int s = 0; s < numScenes; ++s)
            for (int sl = 0; sl < slotsPerScene; ++sl)
                sc.add (juce::String (getSceneSlot (s, sl)));
        for (int st = 0; st < maxSongSteps; ++st)
            so.add (juce::String (getSongStep (st)));
        t.setProperty ("scenes", sc.joinIntoString (","), nullptr);
        t.setProperty ("slots", slotsPerScene, nullptr);
        t.setProperty ("song", so.joinIntoString (","), nullptr);
        return t;
    }

    void songFromValueTree (const juce::ValueTree& t)
    {
        resetSong();
        if (! t.isValid()) return;
        songMode = (bool) t.getProperty ("mode", false);
        songLoop = (bool) t.getProperty ("loop", false);
        auto sc = juce::StringArray::fromTokens (t.getProperty ("scenes").toString(), ",", {});
        const int stride = (int) t.getProperty ("slots", 8); // older projects had 8 slots per scene
        for (int i = 0; i < juce::jmin (sc.size(), stride, (int) slotsPerScene); ++i)
            setSceneSlot (0, i, sc[i].getIntValue());
        auto so = juce::StringArray::fromTokens (t.getProperty ("song").toString(), ",", {});
        for (int i = 0; i < juce::jmin (so.size(), (int) maxSongSteps); ++i)
            setSongStep (i, so[i].getIntValue() >= 0 ? 0 : -1);
    }

    void resetSong()
    {
        for (auto& sc : sceneSlots) for (auto& sl : sc) sl = -1;
        for (auto& st : songSteps) st = -1;
        setSceneSlot (0, 0, 0); // scene 1 starts with pattern 1
        songMode = false;
        songLoop = false;
    }

    /** Pattern names (RENAME in the SCENE window); empty = "P1", "P2" ... (message thread). */
    std::array<juce::String, numPatterns> patternNames;
    juce::String getPatternLabel (int pattern) const
    {
        if (pattern < 0 || pattern >= numPatterns) return "-";
        return patternNames[(size_t) pattern].isNotEmpty() ? patternNames[(size_t) pattern] : "P" + juce::String (pattern + 1);
    }

    std::atomic<bool> songMode { false }, songLoop { false };
    std::atomic<bool> restartRequested { false }; // internal clock back to the start

    // ---- transport (message thread) ----
    /** PLAY of the sampler: the selected pattern (step sequencer + piano roll) loops, independent of the song. */
    void playPattern() { songMode = false; restartRequested = true; internalPlay = true; }
    /** PLAY SONG of the SCENE window. */
    void playSong() { songMode = true; restartRequested = true; internalPlay = true; }
    void stopPlayback() { internalPlay = false; }
    /** Space bar / PLAY button toggle for the sampler. */
    void togglePatternPlay() { if (internalPlay.load()) stopPlayback(); else playPattern(); }
    bool isPlayingSong() const { return internalPlay.load() && songMode.load() && songLengthSteps() > 0; }
    bool isPlayingPattern() const { return internalPlay.load() && ! isPlayingSong(); }
    std::atomic<int> songPosStep { -1 }, songPosSlot { -1 };
    std::atomic<float> songPosPpq { -1.0f };   // position in the song (quarter notes), -1 when the song isn't playing

    std::atomic<int> currentPattern { 0 };
    std::atomic<int> currentStep { -1 };
    std::atomic<bool> internalPlay { false };
    std::atomic<bool> syncToHost { true };
    std::atomic<bool> recording { false };       // step sequencer REC
    std::atomic<bool> pianoRecording { false };  // piano roll REC
    std::atomic<float> bpm { 90.0f };
    std::atomic<float> effectiveBpm { 90.0f };
    std::atomic<float> swing { 0.0f };
    std::array<std::atomic<int>, numPatterns> length;

private:
    struct ActiveNote { juce::uint32 id; double offPpq; int mode; int row; int targetPad; };
    struct Held { bool down = false; double startPpq = 0.0; juce::uint8 velocity = 100; };

    void syncClips()
    {
        const int v = clipVersion.load();
        if (v == lastClipVersion)
            return;
        const juce::SpinLock::ScopedTryLockType tl (clipLock);
        if (! tl.isLocked())
            return;
        for (size_t i = 0; i < clips.size(); ++i)
            audioClips[i] = clips[i];
        lastClipVersion = v;
    }

    template <typename NoteOff>
    void releaseAllNotes (int offset, NoteOff&& noteOff)
    {
        for (int i = 0; i < numActive; ++i)
            noteOff (active[(size_t) i].mode, active[(size_t) i].row, offset, (int) active[(size_t) i].id, active[(size_t) i].targetPad);
        numActive = 0;
    }

    std::array<std::array<std::array<std::atomic<juce::uint8>, maxSteps>, numPads>, numPatterns> steps;
    std::array<std::array<std::array<std::atomic<juce::int8>, maxSteps>, numPads>, numPatterns> nudges;
    std::array<std::array<std::atomic<juce::int8>, slotsPerScene>, numScenes> sceneSlots;
    std::array<SongSegment, maxSegments> songSegs; // audio thread's song timeline
    std::array<std::atomic<juce::int8>, maxSongSteps> songSteps;
    std::atomic<float> patternPosSteps { -1.0f };

    // piano roll clips: message thread side (guarded) and the audio thread's copy
    mutable juce::SpinLock clipLock;
    std::array<NoteClip::Ptr, numPatterns> clips;
    std::array<NoteClip::Ptr, numPatterns> audioClips;
    juce::ReferenceCountedArray<NoteClip> clipPool; // message thread: keeps clips alive so the audio thread never frees one
    std::atomic<int> clipVersion { 0 };
    int lastClipVersion = -1;
    juce::uint32 nextNoteId = 0;

    std::array<ActiveNote, 512> active {};
    int numActive = 0;

    std::array<std::array<Held, 128>, 2> held {};
    juce::AbstractFifo recordFifo { 512 };
    std::array<RecordedNote, 512> recorded {};

    double sampleRate = 44100.0;
    double internalPpq = 0.0, blockPpq = 0.0, ppqPerSample = 0.0, lastPpqEnd = -1.0e9;
    bool running = false, wasInternal = false;
};
} // namespace rz
