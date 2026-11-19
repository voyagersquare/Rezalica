#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace rz
{
/** One pad's effect chain: Filter -> Saturation (tube, tape, solid state) -> Delay -> Reverb.
    The filter is an analog-modelled ladder filter, the saturation runs 2x oversampled. */
class FxChain
{
public:
    static const juce::StringArray& filterTypeNames()
    {
        static const juce::StringArray names { "LP 24", "LP 12", "HP 24", "HP 12", "BP 24", "BP 12" };
        return names;
    }

    struct Params
    {
        bool filterOn = false;
        int filterType = 0;               // index into filterTypeNames()
        float cutoff = 20000.0f, resonance = 0.0f;
        bool satOn = false;
        float tube = 0.0f, tape = 0.0f, solid = 0.0f; // 0..1, 0 = off
        bool delayOn = false;
        float delaySamples = 10000.0f, feedback = 0.35f, mix = 0.3f;
        bool reverbOn = false;
        float reverbSize = 0.5f, reverbDamping = 0.4f, reverbWidth = 1.0f, reverbMix = 0.3f;
    };

    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        blockSize = juce::jmax (1, maxBlockSize);

        filter.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        filter.setDrive (1.2f);

        oversampling.initProcessing ((size_t) blockSize);

        delayBuffer.setSize (2, (int) (sampleRate * 2.05) + 4); // up to 2 s of delay
        delayTime.reset (sampleRate, 0.08);
        reverb.setSampleRate (sampleRate);
        reset();
    }

    void reset()
    {
        filter.reset();
        oversampling.reset();
        reverb.reset();
        delayBuffer.clear();
        writePos = 0;
        for (auto& s : satState) s = {};
    }

    void process (juce::AudioBuffer<float>& buffer, int numSamples, const Params& p)
    {
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);

        if (p.filterOn)
            processFilter (buffer, numSamples, p);

        if (p.satOn && (p.tube > 0.0005f || p.tape > 0.0005f || p.solid > 0.0005f))
            for (int start = 0; start < numSamples; start += blockSize)
                processSaturation (buffer, start, juce::jmin (blockSize, numSamples - start), p);

        if (p.delayOn)
            processDelay (L, R, numSamples, p);
        else
            delayTime.setCurrentAndTargetValue (juce::jlimit (1.0f, (float) (delayBuffer.getNumSamples() - 2), p.delaySamples));

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
    //==============================================================================
    void processFilter (juce::AudioBuffer<float>& buffer, int numSamples, const Params& p)
    {
        using Mode = juce::dsp::LadderFilterMode;
        static const Mode modes[] { Mode::LPF24, Mode::LPF12, Mode::HPF24, Mode::HPF12, Mode::BPF24, Mode::BPF12 };
        filter.setMode (modes[juce::jlimit (0, 5, p.filterType)]);
        filter.setCutoffFrequencyHz (juce::jlimit (20.0f, (float) (sampleRate * 0.45), p.cutoff));
        filter.setResonance (juce::jlimit (0.0f, 1.0f, p.resonance) * 0.9f); // stays below self-oscillation

        juce::dsp::AudioBlock<float> block (buffer);
        for (int start = 0; start < numSamples; start += blockSize)
        {
            auto sub = block.getSubBlock ((size_t) start, (size_t) juce::jmin (blockSize, numSamples - start));
            juce::dsp::ProcessContextReplacing<float> ctx (sub);
            filter.process (ctx);
        }
    }

    // Gentle saturation stage: drives a curve (peak-normalised), blends it in by the amount
    // and compensates the gain at a typical level (about -10 dBFS) so loudness stays similar.
    template <typename Curve>
    struct Stage
    {
        Stage (float amt, float maxDrive, Curve c) : amount (amt), curve (c)
        {
            g = 1.0f + amount * maxDrive;
            norm = 1.0f / curve (g);
            const float ref = 0.3f;
            const float level = 0.5f * (std::abs (raw (ref)) + std::abs (raw (-ref)));
            comp = level > 1.0e-6f ? ref / level : 1.0f;
        }
        float raw (float x) const { return x + amount * (curve (g * x) * norm - x); }
        float operator() (float x) const { return amount <= 0.0005f ? x : raw (x) * comp; }

        float amount, g = 1.0f, norm = 1.0f, comp = 1.0f;
        Curve curve;
    };
    template <typename Curve>
    static Stage<Curve> makeStage (float amount, float maxDrive, Curve c) { return Stage<Curve> (amount, maxDrive, c); }

    struct SatState { float dc = 0.0f, dcIn = 0.0f, tapeLp = 0.0f; };

    void processSaturation (juce::AudioBuffer<float>& buffer, int start, int num, const Params& p)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto sub = block.getSubBlock ((size_t) start, (size_t) num);
        auto up = oversampling.processSamplesUp (sub);

        const float osRate = (float) (sampleRate * 2.0);
        const float tube = p.tube, tape = p.tape, solid = p.solid;
        // tape: high frequencies soften as you drive it (20 kHz -> ~9 kHz)
        const float tapeCut = 20000.0f - 11000.0f * tape;
        const float tapeCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * tapeCut / osRate);
        const float dcCoef = 1.0f - juce::MathConstants<float>::twoPi * 8.0f / osRate;
        const float tubeBias = 0.22f;
        const float tubeBiasOffset = std::tanh (tubeBias);

        // Tube: asymmetric soft curve -> mostly 2nd harmonic, warm
        const auto tubeStage = makeStage (tube, 3.0f, [=] (float v) { return std::tanh (v + tubeBias) - tubeBiasOffset; });
        // Tape: very soft knee (plus a gentle high-frequency roll-off below)
        const auto tapeStage = makeStage (tape, 2.5f, [] (float v) { return (2.0f / juce::MathConstants<float>::pi) * std::atan (v * juce::MathConstants<float>::halfPi); });
        // Solid state: firmer, symmetric knee -> odd harmonics
        const auto solidStage = makeStage (solid, 4.0f, [] (float v)
        {
            const float c = juce::jlimit (-1.0f, 1.0f, v);
            return c - c * c * c / 3.0f;
        });

        for (size_t ch = 0; ch < juce::jmin ((size_t) 2, up.getNumChannels()); ++ch)
        {
            auto& st = satState[ch];
            float* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                float x = d[i];

                x = tubeStage (x);

                if (tape > 0.0005f)
                {
                    x = tapeStage (x);
                    st.tapeLp += tapeCoef * (x - st.tapeLp);
                    x = st.tapeLp;
                }

                x = solidStage (x);

                // remove the DC offset the asymmetric tube curve creates
                const float out = x - st.dcIn + dcCoef * st.dc;
                st.dcIn = x;
                st.dc = out;
                d[i] = out;
            }
        }

        oversampling.processSamplesDown (sub);
    }

    void processDelay (float* L, float* R, int numSamples, const Params& p)
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

    double sampleRate = 44100.0;
    int blockSize = 512;

    juce::dsp::LadderFilter<float> filter;
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    std::array<SatState, 2> satState {};

    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
    juce::SmoothedValue<float> delayTime { 10000.0f };

    juce::Reverb reverb;
    bool reverbWasOn = false;
};
} // namespace rz
