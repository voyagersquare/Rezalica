#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace rz
{
/** Effect chain for the FX bus: Filter+Drive -> Lo-Fi -> Delay -> Reverb. */
class FxChain
{
public:
    struct Params
    {
        bool filterOn = false;
        float cutoff = 20000.0f, resonance = 0.0f, drive = 0.0f;
        bool lofiOn = false;
        float bits = 12.0f, rateDivide = 4.0f, noise = 0.2f;
        bool delayOn = false;
        float delaySamples = 10000.0f, feedback = 0.35f, mix = 0.3f;
        bool reverbOn = false;
        float reverbSize = 0.5f, reverbDamping = 0.4f, reverbWidth = 1.0f, reverbMix = 0.3f;
    };

    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        filter.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, maxBlockSize), 2 });
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

        delayBuffer.setSize (2, (int) (sampleRate * 2.5) + 4);
        delayTime.reset (sampleRate, 0.08);
        reverb.setSampleRate (sampleRate);
        reset();
    }

    void reset()
    {
        filter.reset();
        reverb.reset();
        delayBuffer.clear();
        writePos = 0;
        holdL = holdR = 0.0f;
        holdCounter = 1.0e9f;
    }

    void process (juce::AudioBuffer<float>& buffer, int numSamples, const Params& p)
    {
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);

        if (p.filterOn)
        {
            filter.setCutoffFrequency (juce::jlimit (20.0f, (float) (sampleRate * 0.45), p.cutoff));
            filter.setResonance (0.707f + p.resonance * 9.0f);
            const float g = 1.0f + p.drive * 24.0f;
            const float norm = 1.0f / std::tanh (g);

            for (int i = 0; i < numSamples; ++i)
            {
                float l = filter.processSample (0, L[i]);
                float r = filter.processSample (1, R[i]);
                if (p.drive > 0.001f)
                {
                    l = std::tanh (l * g) * norm * 0.8f;
                    r = std::tanh (r * g) * norm * 0.8f;
                }
                L[i] = l;
                R[i] = r;
            }
        }

        if (p.lofiOn)
        {
            const float levels = std::pow (2.0f, juce::jlimit (2.0f, 16.0f, p.bits) - 1.0f);
            const float divide = juce::jmax (1.0f, p.rateDivide);

            for (int i = 0; i < numSamples; ++i)
            {
                holdCounter += 1.0f;
                if (holdCounter >= divide)
                {
                    holdCounter -= divide;
                    if (holdCounter >= divide) holdCounter = 0.0f;
                    holdL = std::round (L[i] * levels) / levels;
                    holdR = std::round (R[i] * levels) / levels;
                }

                float n = (rng.nextFloat() * 2.0f - 1.0f) * p.noise * 0.015f;
                if (rng.nextFloat() < p.noise * 0.0004f)
                    n += (rng.nextFloat() * 2.0f - 1.0f) * 0.35f * p.noise; // vinyl crackle

                L[i] = holdL + n;
                R[i] = holdR + n;
            }
        }

        if (p.delayOn)
        {
            const int size = delayBuffer.getNumSamples();
            float* dL = delayBuffer.getWritePointer (0);
            float* dR = delayBuffer.getWritePointer (1);
            delayTime.setTargetValue (juce::jlimit (1.0f, (float) (size - 2), p.delaySamples));
            const float fb = juce::jlimit (0.0f, 0.95f, p.feedback);

            for (int i = 0; i < numSamples; ++i)
            {
                const float d = delayTime.getNextValue();
                float readPos = (float) writePos - d;
                if (readPos < 0.0f) readPos += (float) size;
                const int r0 = (int) readPos;
                const int r1 = (r0 + 1) % size;
                const float frac = readPos - (float) r0;
                const float wl = dL[r0] + frac * (dL[r1] - dL[r0]);
                const float wr = dR[r0] + frac * (dR[r1] - dR[r0]);

                dL[writePos] = std::tanh (L[i] + wl * fb);
                dR[writePos] = std::tanh (R[i] + wr * fb);
                writePos = (writePos + 1) % size;

                L[i] += wl * p.mix;
                R[i] += wr * p.mix;
            }
        }
        else
        {
            delayTime.setCurrentAndTargetValue (juce::jlimit (1.0f, (float) (delayBuffer.getNumSamples() - 2), p.delaySamples));
        }

        if (p.reverbOn)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = juce::jlimit (0.0f, 1.0f, p.reverbSize);
            rp.damping = juce::jlimit (0.0f, 1.0f, p.reverbDamping);
            rp.width = juce::jlimit (0.0f, 1.0f, p.reverbWidth);
            rp.wetLevel = juce::jlimit (0.0f, 1.0f, p.reverbMix) * 0.6f;
            rp.dryLevel = 1.0f - 0.5f * juce::jlimit (0.0f, 1.0f, p.reverbMix);
            reverb.setParameters (rp);
            reverb.processStereo (L, R, numSamples);
        }
        else if (reverbWasOn)
        {
            reverb.reset();
        }
        reverbWasOn = p.reverbOn;
    }

private:
    double sampleRate = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> filter;

    float holdL = 0.0f, holdR = 0.0f, holdCounter = 0.0f;
    juce::Random rng;

    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
    juce::SmoothedValue<float> delayTime { 10000.0f };

    juce::Reverb reverb;
    bool reverbWasOn = false;
};
} // namespace rz
