#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SamplerEngine.h"
#include <cmath>

namespace rz
{
/** The LEVELER's sound: a program-dependent leveling amplifier.
    - INPUT drives the signal into a soft-knee compressor whose ratio grows the harder it is pushed (like a
      variable-mu tube stage: gentle at first, firmer on loud peaks).
    - MODE = how many release stages work together: SINGLE (one time constant), DOUBLE and TRIPLE add slower
      stages that only charge on long loud passages - so short peaks recover fast, sustained loudness slowly.
    - RECOVERY TIME scales all release times (SLOW .. FAST).
    - A touch of tube-like colour on the way out, then OUTPUT. Stereo linked. No latency. */
class LevelerDsp
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
    }

    void reset()
    {
        for (auto& e : env) e = 0.0f;
        grSmoothed = 0.0f;
        inGain.reset (sampleRate, 0.05);
        outGain.reset (sampleRate, 0.05);
        first = true;
    }

    /** Processes the pad's audio in place; returns the gain reduction now (dB, positive). */
    float process (juce::AudioBuffer<float>& b, int numSamples, const PadLeveler& p)
    {
        const float inDb = (p.input.load() - 6.0f) * 1.5f;      // 0 -> -9 dB, 6 -> 0 dB, 20 -> +21 dB
        const float outDb = (p.output.load() - 10.0f) * 1.5f;   // 0 -> -15 dB, 10 -> 0 dB, 20 -> +15 dB
        if (first) { inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inDb)); outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outDb)); first = false; }
        inGain.setTargetValue (juce::Decibels::decibelsToGain (inDb));
        outGain.setTargetValue (juce::Decibels::decibelsToGain (outDb));

        const int stages = juce::jlimit (0, 2, p.mode.load()) + 1;
        const float rec = juce::jlimit (0.0f, 1.0f, p.recovery.load());
        const float baseRelease = 2.5f * std::pow (0.06f, rec);   // SLOW 2.5 s .. FAST 0.15 s
        // stage 1: quick (10 ms attack); stage 2: slower to charge and to let go; stage 3: slower still
        static constexpr float attackMs[3]  { 10.0f, 80.0f, 400.0f };
        static constexpr float releaseMul[3] { 1.0f, 3.5f, 10.0f };
        float aAtt[3], aRel[3];
        for (int s = 0; s < 3; ++s)
        {
            aAtt[s] = std::exp (-1.0f / (float) (sampleRate * attackMs[s] * 0.001));
            const float rel = baseRelease * (stages == 1 ? 1.0f : releaseMul[s] * (s == 0 ? 0.35f : 1.0f));
            aRel[s] = std::exp (-1.0f / (float) (sampleRate * rel));
        }

        float* L = b.getWritePointer (0);
        float* R = b.getWritePointer (1);
        float maxGr = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = inGain.getNextValue();
            float l = L[i] * g, r = R[i] * g;
            const float level = juce::jmax (std::abs (l), std::abs (r));

            float det = 0.0f;
            for (int s = 0; s < stages; ++s)
            {
                auto& e = env[(size_t) s];
                e = level > e ? aAtt[s] * e + (1.0f - aAtt[s]) * level
                              : aRel[s] * e + (1.0f - aRel[s]) * level;
                // the slower stages hold a little less (they only add the long-term leveling)
                det = juce::jmax (det, e * (s == 0 ? 1.0f : 0.9f));
            }

            const float gr = gainReductionDb (juce::Decibels::gainToDecibels (det, -120.0f));
            const float gain = juce::Decibels::decibelsToGain (-gr);
            l *= gain; r *= gain;

            // a little tube colour: soft, slightly asymmetric curve (mostly 2nd harmonic), only noticeable when hot
            l = colour (l); r = colour (r);

            const float o = outGain.getNextValue();
            L[i] = l * o;
            R[i] = r * o;
            maxGr = juce::jmax (maxGr, gr);
        }
        grSmoothed = maxGr;
        return grSmoothed;
    }

    static float gainReductionDb (float levelDb)
    {
        // soft knee around -18 dBFS; the ratio grows from 2:1 to about 8:1 the further it goes over
        constexpr float threshold = -18.0f, knee = 10.0f;
        const float over = levelDb - threshold;
        if (over <= -knee * 0.5f) return 0.0f;
        float x = over;
        if (over < knee * 0.5f)
        {
            const float t = (over + knee * 0.5f);
            x = t * t / (2.0f * knee);           // quadratic knee
        }
        const float ratio = juce::jlimit (2.0f, 8.0f, 2.0f + x * 0.25f);
        return juce::jlimit (0.0f, 40.0f, x * (1.0f - 1.0f / ratio));
    }

private:
    static float colour (float x)
    {
        constexpr float bias = 0.12f;
        const float y = std::tanh (x + bias) - std::tanh (bias);
        return x + 0.35f * (y - x);  // blend: clean when quiet, warm when driven
    }

    double sampleRate = 44100.0;
    std::array<float, 3> env {};
    float grSmoothed = 0.0f;
    juce::SmoothedValue<float> inGain { 1.0f }, outGain { 1.0f };
    bool first = true;
};
} // namespace rz
