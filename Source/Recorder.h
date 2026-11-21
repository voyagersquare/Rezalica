#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace rz
{
/** Records audio on the audio thread into a buffer the message thread allocated:
    the sound card input (sampling) or Rezalica's own output (resampling). */
class AudioRecorder
{
public:
    enum Source { input = 0, output = 1, computer = 2 }; // computer: what the PC plays (loopback)
    enum State { idle = 0, waiting = 1, recording = 2, finished = 3 };

    // ---- message thread ----
    /** maxSamples: buffer size (and the automatic stop); waitForSignal: starts at the first sound;
        withSequencer: starts on the block where the sequencer starts playing from the top. */
    void start (Source src, int maxSamples, bool waitForSignal, bool withSequencer, double sampleRate)
    {
        if (state.load() != idle && state.load() != finished) return;
        buffer.setSize (2, juce::jmax (1, maxSamples), false, true, false);
        buffer.clear();
        source = src;
        rate = sampleRate;
        writePos = 0;
        stopRequested = false;
        waitSignal = waitForSignal;
        syncStart = withSequencer;
        level = 0.0f;
        state = (waitForSignal || withSequencer) ? waiting : recording;
    }

    void stop() { stopRequested = true; }
    /** Throws away whatever is being recorded. */
    void cancel()
    {
        stopRequested = true;
        discard = true;
    }

    /** When a take is finished: hands it over (trimmed to its length) and goes back to idle. */
    bool takeRecording (juce::AudioBuffer<float>& out, double& sampleRate)
    {
        if (state.load() != finished) return false;
        const int n = writePos.load();
        const bool keep = ! discard.exchange (false);
        if (keep && n > 0)
        {
            out.setSize (2, n);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, 0, buffer, ch, 0, n);
        }
        sampleRate = rate;
        state = idle;
        return keep && n > 0;
    }

    State getState() const { return (State) state.load(); }
    bool isBusy() const { const int s = state.load(); return s == waiting || s == recording; }
    double getSecondsRecorded() const { return writePos.load() / juce::jmax (1.0, rate); }
    double getMaxSeconds() const { return buffer.getNumSamples() / juce::jmax (1.0, rate); }
    Source getSource() const { return source; }

    std::atomic<float> level { 0.0f };    // peak of what comes in (meter)
    std::atomic<float> inputLevel { 0.0f }; // input peak even when not recording (monitor meter)
    float signalThreshold = 0.02f;       // about -34 dB

    // ---- audio thread ----
    /** The sequencer restarted this block: a take that waits for it starts now. */
    void sequencerStarted()
    {
        if (state.load() == waiting && syncStart)
            state = recording;
    }

    /** Feeds one block from the given source; data has numChannels channels. */
    void capture (Source from, const float* const* data, int numChannels, int numSamples)
    {
        if (numChannels <= 0 || numSamples <= 0) return;
        float pk = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                pk = juce::jmax (pk, std::abs (data[ch][i]));
        if (from != output)
            inputLevel = juce::jmax (pk, inputLevel.load() * 0.8f);

        const int st = state.load();
        if (from != source || (st != waiting && st != recording))
            return;
        if (stopRequested.load())
        {
            state = finished;
            return;
        }
        level = juce::jmax (pk, level.load() * 0.8f);

        int first = 0;
        if (st == waiting)
        {
            if (syncStart || ! waitSignal) return;
            // starts at the first sample above the threshold
            first = -1;
            for (int i = 0; i < numSamples && first < 0; ++i)
                for (int ch = 0; ch < numChannels; ++ch)
                    if (std::abs (data[ch][i]) > signalThreshold) { first = i; break; }
            if (first < 0) return;
            state = recording;
        }

        const int pos = writePos.load();
        const int n = juce::jmin (numSamples - first, buffer.getNumSamples() - pos);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, pos, data[juce::jmin (ch, numChannels - 1)] + first, n);
        writePos = pos + n;
        if (pos + n >= buffer.getNumSamples())
            state = finished; // full (or the planned length is done)
    }

private:
    juce::AudioBuffer<float> buffer;
    std::atomic<int> state { idle };
    std::atomic<int> writePos { 0 };
    std::atomic<bool> stopRequested { false }, discard { false };
    Source source = input;
    double rate = 44100.0;
    bool waitSignal = false, syncStart = false;
};
} // namespace rz
