#pragma once

#include "SamplerEngine.h"
#include <atomic>
#include <cmath>

namespace rz
{
/** DECK: a turntable emulation. The record (a sample) turns with a motor that speeds up / brakes like a
    real direct-drive deck; holding the platter makes the sound follow the hand (scratching), letting go
    gives it back to the motor. Positions are in samples of the record; the hand moves the record by
    an offset from where it was grabbed (one turn = 1.8 s of the record, 33 1/3 rpm).

    The message thread sets the controls (atomics); render() runs on the audio thread. */
class Turntable
{
public:
    static constexpr double secondsPerTurn = 60.0 / (100.0 / 3.0); // 1.8 s: the groove of a 33 1/3 record
    static constexpr int numCues = 4;

    // ---- message thread ----
    void setRecord (SampleData::Ptr s)
    {
        {
            const juce::SpinLock::ScopedLockType sl (recordLock);
            pendingRecord = s;
            recordChanged = true;
        }
        record = s;
        for (auto& c : cues) c = -1.0;
    }
    SampleData::Ptr getRecord() const { return record; }

    /** One turn of the platter, in record samples. */
    double samplesPerTurn() const
    {
        auto r = record;
        return secondsPerTurn * (r != nullptr ? r->sampleRate : 44100.0);
    }

    // ---- controls (any thread) ----
    std::atomic<bool> motorOn { false };      // START / STOP
    std::atomic<bool> rpm45 { false };        // 45 rpm: 1.35 x faster (a 33 record on 45)
    std::atomic<bool> sync { false };         // speed = sequencer tempo / record tempo
    std::atomic<bool> loop { true };          // the end of the sample joins its start (an endless groove)
    std::atomic<bool> cut { false };          // the fader is closed (SHIFT held / CUT key)
    std::atomic<bool> reverseFader { false }; // "hamster": closed and open swap
    std::atomic<float> pitch { 0.0f };        // -1 .. 1 of the pitch range
    std::atomic<float> pitchRange { 0.08f };  // 0.08 or 0.16
    std::atomic<float> volume { 1.0f };
    std::atomic<float> recordBpm { 0.0f };    // tempo of the sample (0 = unknown: SYNC does nothing)
    std::atomic<bool> startOnBeat { false };  // START with SYNC while the sequencer plays: on the next beat

    // the hand on the platter
    std::atomic<bool> touched { false };
    std::atomic<double> handOffset { 0.0 };   // record samples moved since the platter was grabbed

    std::atomic<double> jumpRequest { -1.0 }; // CUE: jump there (record samples, 0 .. length)
    std::array<std::atomic<double>, numCues> cues { -1.0, -1.0, -1.0, -1.0 };

    // ---- read by the UI ----
    std::atomic<double> position { 0.0 };     // unwrapped: keeps counting turns (for the drawing)
    std::atomic<float> speed { 0.0f };        // 1 = normal speed of the record
    std::atomic<float> level { 0.0f };

    // ---- recording what the deck plays (after the fader) ----
    enum RecState { recIdle = 0, recRunning = 1, recStopping = 2, recDone = 3 };
    std::atomic<int> recState { recIdle };
    void startRecording() { if (recState.load() == recIdle) { recWrite = 0; recState = recRunning; } }
    void stopRecording()  { if (recState.load() == recRunning) recState = recStopping; }
    /** Once the audio thread finished the take: hands it over. */
    bool takeRecording (juce::AudioBuffer<float>& out, double& sr)
    {
        if (recState.load() != recDone) return false;
        const int n = recWrite.load();
        if (n > 0)
        {
            out.setSize (2, n);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, recBuffer, ch, 0, n);
        }
        sr = hostRate;
        recState = recIdle;
        return n > 0;
    }
    double getRecordedSeconds() const { return recWrite.load() / juce::jmax (1.0, hostRate); }

    // ---- audio thread ----
    void prepare (double sampleRate, int maxBlock)
    {
        hostRate = sampleRate;
        recBuffer.setSize (2, (int) (sampleRate * 120.0)); // up to 2 minutes of scratching
        recWrite = 0;
        temp.setSize (2, juce::jmax (1, maxBlock));
        smoothHand = 1.0 - std::exp (-1.0 / (0.007 * sampleRate));   // 2 x 7 ms: smooth, still direct
        motionSmooth = (float) (1.0 - std::exp (-1.0 / (0.004 * sampleRate)));
        gainStep = 1.0f / (float) juce::jmax (1.0, 0.0007 * sampleRate); // the fader moves in 0.7 ms
    }

    /** Adds the deck into 'out'. seqBpm: the sequencer tempo; nextBeat: sample offset of the next beat
        in this block (-1: none / not playing). */
    void render (juce::AudioBuffer<float>& out, int numSamples, double seqBpm, int nextBeat)
    {
        {
            const juce::SpinLock::ScopedTryLockType tl (recordLock);
            if (tl.isLocked() && recordChanged)
            {
                live = pendingRecord; // (the processor's sample pool keeps every sample alive)
                recordChanged = false;
                pos = 0.0;
                rate = 0.0;
            }
        }
        if (temp.getNumSamples() < numSamples)
            temp.setSize (2, numSamples, false, false, true);
        temp.clear (0, 0, numSamples);

        const auto* rec = live.get();
        const int len = rec != nullptr ? rec->buffer.getNumSamples() : 0;
        if (len < 4)
        {
            position = 0.0; speed = 0.0f; level = level.load() * 0.8f;
            return;
        }

        const double base = rec->sampleRate / hostRate;               // record samples per host sample at speed 1
        const double bpmFactor = sync.load() && recordBpm.load() > 1.0f && seqBpm > 1.0 ? seqBpm / recordBpm.load() : -1.0;
        const double motorSpeed = bpmFactor > 0.0 ? bpmFactor
                                                  : (1.0 + (double) pitch.load() * pitchRange.load()) * (rpm45.load() ? 1.35 : 1.0);
        const bool motor = motorOn.load();
        // direct drive: quick start, a brake that stops in about half a second, the hand gives it back fast
        const double tau = motor ? (justReleased ? 0.035 : 0.14) : (justReleased ? 0.06 : 0.22);
        const double follow = 1.0 - std::exp (-1.0 / (tau * hostRate));

        if (const double j = jumpRequest.exchange (-1.0); j >= 0.0)
        {
            const double cycle = loop.load() ? std::floor (pos / len) * len : 0.0;
            pos = cycle + juce::jlimit (0.0, (double) len - 1.0, j);
            if (touched.load()) { touchOrigin = pos - handOffset.load(); hand1 = hand2 = pos; }
        }

        const bool hand = touched.load();
        if (hand && ! wasTouched)
        {
            touchOrigin = pos - handOffset.load(); // the UI sends the movement since it grabbed the platter
            hand1 = hand2 = pos;
        }
        if (! hand && wasTouched)
            justReleased = true;
        wasTouched = hand;

        // where the hand has put the record; the mouse reports it in small jumps a few ms apart,
        // so it is followed through two smoothing stages: the record moves evenly, also very slowly
        const double handEnd = touchOrigin + handOffset.load();

        const float* srcL = rec->buffer.getReadPointer (0);
        const float* srcR = rec->buffer.getReadPointer (juce::jmin (1, rec->buffer.getNumChannels() - 1));
        const bool looping = loop.load();
        const bool closed = cut.load() != reverseFader.load();
        const float targetGain = closed ? 0.0f : volume.load();
        float pk = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            if (startOnBeat.load() && i == nextBeat)
            {
                motorOn = true;
                startOnBeat = false;
                rate = motorSpeed * base; // on the beat, at full speed at once
            }

            if (hand)
            {
                hand1 += (handEnd - hand1) * smoothHand;
                hand2 += (hand1 - hand2) * smoothHand;
                const double prev = pos;
                pos = hand2;
                rate = pos - prev;
            }
            else
            {
                const double w = motorOn.load() ? motorSpeed * base : 0.0;
                rate += (w - rate) * follow;
                if (justReleased && std::abs (rate - w) < 0.002 * base) justReleased = false;
                pos += rate;
            }

            if (! looping)
            {
                if (pos < 0.0) { pos = 0.0; if (! hand) rate = juce::jmax (0.0, rate); }
                if (pos > (double) (len - 1)) { pos = (double) (len - 1); }
            }

            // 4-point interpolation at the needle
            float l = 0.0f, r = 0.0f;
            {
                const double p = looping ? pos - std::floor (pos / len) * len : pos;
                const int i1 = (int) p;
                const float t = (float) (p - i1);
                auto idx = [&] (int k) { return looping ? ((k % len) + len) % len : juce::jlimit (0, len - 1, k); };
                const int i0 = idx (i1 - 1), j1 = idx (i1), i2 = idx (i1 + 1), i3 = idx (i1 + 2);
                auto herm = [t] (float y0, float y1, float y2, float y3)
                {
                    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                    return ((c3 * t + c2) * t + c1) * t + y1;
                };
                l = herm (srcL[i0], srcL[j1], srcL[i2], srcL[i3]);
                r = herm (srcR[i0], srcR[j1], srcR[i2], srcR[i3]);
                if (! looping && (pos <= 0.0 || pos >= (double) (len - 1))) l = r = 0.0f; // off the groove
            }

            // a record that hardly moves is silent (no DC from a held needle); smoothed, so a slow,
            // uneven hand does not switch the sound on and off (that crackled)
            const float motionTarget = (float) juce::jlimit (0.0, 1.0, std::abs (rate) / (0.004 * base));
            motion += (motionTarget - motion) * motionSmooth;
            gain += juce::jlimit (-gainStep, gainStep, targetGain - gain);
            const float g = gain * motion;
            temp.setSample (0, i, l * g);
            temp.setSample (1, i, r * g);
            pk = juce::jmax (pk, std::abs (l * g), std::abs (r * g));
        }

        for (int ch = 0; ch < juce::jmin (2, out.getNumChannels()); ++ch)
            out.addFrom (ch, 0, temp, ch, 0, numSamples);

        // REC: what the deck plays, after the fader
        const int st = recState.load();
        if (st == recRunning)
        {
            const int w0 = recWrite.load();
            const int n = juce::jmin (numSamples, recBuffer.getNumSamples() - w0);
            for (int ch = 0; ch < 2; ++ch)
                recBuffer.copyFrom (ch, w0, temp, ch, 0, n);
            recWrite = w0 + n;
            if (recWrite >= recBuffer.getNumSamples()) recState = recDone;
        }
        else if (st == recStopping)
            recState = recDone;

        position = pos;
        speed = (float) (rate / base);
        level = juce::jmax (pk, level.load() * 0.85f);
    }

    /** A guess of the sample's tempo: a whole number of bars (1, 2, 4, 8, 16) between 70 and 140 BPM. */
    static float guessBpm (const SampleData& s)
    {
        if (s.isWarped() && s.renderedBpm > 0.0) return (float) s.renderedBpm;
        if (const float named = bpmFromName (s.name); named > 0.0f) return named;
        const double secs = s.buffer.getNumSamples() / juce::jmax (1.0, s.sampleRate);
        if (secs < 0.5) return 0.0f;
        for (int bars : { 1, 2, 4, 8, 16 })
        {
            const double bpm = bars * 4.0 * 60.0 / secs;
            if (bpm >= 70.0 && bpm < 140.0) return (float) bpm;
        }
        return 0.0f;
    }

    /** "boom-bap-85-bpm", "Break 95bpm", "BPM_120 loop" -> the number next to "bpm" (40 .. 250), else 0. */
    static float bpmFromName (const juce::String& name)
    {
        const auto n = name.toLowerCase();
        for (int at = n.indexOf ("bpm"); at >= 0; at = n.indexOf (at + 3, "bpm"))
        {
            auto digitsBefore = [&]
            {
                int e = at;
                while (e > 0 && (n[e - 1] == ' ' || n[e - 1] == '-' || n[e - 1] == '_' || n[e - 1] == '.')) --e;
                int b = e;
                while (b > 0 && juce::CharacterFunctions::isDigit (n[b - 1])) --b;
                return n.substring (b, e);
            };
            auto digitsAfter = [&]
            {
                int b = at + 3;
                while (b < n.length() && (n[b] == ' ' || n[b] == '-' || n[b] == '_' || n[b] == ':')) ++b;
                int e = b;
                while (e < n.length() && juce::CharacterFunctions::isDigit (n[e])) ++e;
                return n.substring (b, e);
            };
            for (auto d : { digitsBefore(), digitsAfter() })
                if (d.length() >= 2 && d.length() <= 3)
                    if (const int v = d.getIntValue(); v >= 40 && v <= 250)
                        return (float) v;
        }
        return 0.0f;
    }

private:
    SampleData::Ptr record;          // message thread
    SampleData::Ptr pendingRecord;   // handed over under the lock
    SampleData::Ptr live;            // audio thread
    juce::SpinLock recordLock;
    bool recordChanged = false;

    double hostRate = 44100.0;
    double pos = 0.0, rate = 0.0, touchOrigin = 0.0, hand1 = 0.0, hand2 = 0.0, smoothHand = 0.1;
    float motion = 0.0f, motionSmooth = 0.01f;
    bool wasTouched = false, justReleased = false;
    float gain = 1.0f, gainStep = 0.01f;
    juce::AudioBuffer<float> temp, recBuffer;
    std::atomic<int> recWrite { 0 };
};
} // namespace rz
