#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SamplerEngine.h"
#include <cmath>

namespace rz
{
/** Simple 16-pad step sequencer (16th notes), 8 patterns of up to 32 steps.
    Follows the host transport, or runs on its own clock. */
class Sequencer
{
public:
    static constexpr int numPatterns = 8;
    static constexpr int maxSteps = 32;
    static constexpr double stepLengthPpq = 0.25; // 1/16

    Sequencer()
    {
        for (auto& pat : steps)
            for (auto& row : pat)
                for (auto& s : row)
                    s = 0;
        for (auto& l : length)
            l = 16;
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        internalPpq = 0.0;
        lastPpqEnd = -1.0e9;
    }

    // ---- pattern data (any thread) ----
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

    void setPatternData (int pattern, const juce::MemoryBlock& mb)
    {
        clearPattern (pattern);
        if (mb.getSize() < (size_t) (numPads * maxSteps))
            return;
        auto* d = static_cast<const juce::uint8*> (mb.getData());
        for (int p = 0; p < numPads; ++p)
            for (int s = 0; s < maxSteps; ++s)
                setStep (pattern, p, s, d[p * maxSteps + s]);
    }

    // ---- audio thread ----
    template <typename Callback> // Callback (int pad, float velocity, int sampleOffset)
    void process (const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos, int numSamples, Callback&& trigger)
    {
        const bool sync = syncToHost.load();
        double tempo = bpm.load();
        if (sync && pos.hasValue() && pos->getBpm().hasValue() && *pos->getBpm() > 0.0)
            tempo = *pos->getBpm();
        effectiveBpm = (float) tempo;

        const bool hostPlaying = sync && pos.hasValue() && pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        const bool internal = internalPlay.load();

        if (internal && ! wasInternal)
            internalPpq = 0.0;
        wasInternal = internal;

        ppqPerSample = tempo / 60.0 / sampleRate;

        double ppqStart;
        if (hostPlaying)
            ppqStart = *pos->getPpqPosition();
        else if (internal)
            ppqStart = internalPpq;
        else
        {
            running = false;
            currentStep = -1;
            return;
        }

        running = true;

        // ignore tiny overlaps caused by host rounding so steps don't fire twice
        if (ppqStart < lastPpqEnd && ppqStart > lastPpqEnd - 1.0e-3)
            ppqStart = lastPpqEnd;

        const double ppqEnd = ppqStart + numSamples * ppqPerSample;
        blockPpq = ppqStart;
        lastPpqEnd = ppqEnd;
        if (! hostPlaying)
            internalPpq = ppqEnd;

        const int pattern = currentPattern.load();
        const int len = juce::jlimit (1, maxSteps, length[(size_t) pattern].load());
        const double swingOffset = juce::jlimit (0.0f, 1.0f, swing.load()) * stepLengthPpq * 0.5;

        for (long long k = (long long) std::floor (ppqStart / stepLengthPpq) - 1;; ++k)
        {
            const double base = (double) k * stepLengthPpq;
            if (base >= ppqEnd)
                break;

            const double t = base + ((k & 1) != 0 ? swingOffset : 0.0);
            if (t < ppqStart || t >= ppqEnd)
                continue;

            const int offset = juce::jlimit (0, numSamples - 1, (int) ((t - ppqStart) / ppqPerSample));
            const int step = (int) (((k % len) + len) % len);
            currentStep = step;

            for (int pad = 0; pad < numPads; ++pad)
                if (auto v = getStep (pattern, pad, step))
                    trigger (pad, (float) v / 127.0f, offset);
        }
    }

    /** Writes a live pad hit into the current pattern (when recording). */
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

    double getSamplesPerStep() const noexcept { return ppqPerSample > 0.0 ? stepLengthPpq / ppqPerSample : sampleRate * 0.125; }
    bool isRunning() const noexcept { return running; }

    std::atomic<int> currentPattern { 0 };
    std::atomic<int> currentStep { -1 };
    std::atomic<bool> internalPlay { false };
    std::atomic<bool> syncToHost { true };
    std::atomic<bool> recording { false };
    std::atomic<float> bpm { 90.0f };
    std::atomic<float> effectiveBpm { 90.0f };
    std::atomic<float> swing { 0.0f };
    std::array<std::atomic<int>, numPatterns> length;

private:
    std::array<std::array<std::array<std::atomic<juce::uint8>, maxSteps>, numPads>, numPatterns> steps;

    double sampleRate = 44100.0;
    double internalPpq = 0.0, blockPpq = 0.0, ppqPerSample = 0.0, lastPpqEnd = -1.0e9;
    bool running = false, wasInternal = false;
};
} // namespace rz
