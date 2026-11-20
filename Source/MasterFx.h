#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>

namespace rz
{
/** Settings of the master chain on Rezalica's output: 3-band EQ -> compressor -> limiter.
    Written by the UI, read by the audio thread. */
struct MasterFxSettings
{
    std::atomic<bool> eqOn { false }, compOn { false }, limiterOn { false };
    std::atomic<float> lowDb { 0.0f }, midDb { 0.0f }, midFreq { 1000.0f }, highDb { 0.0f };   // low shelf 100 Hz, peak, high shelf 8 kHz
    std::atomic<float> threshold { -18.0f }, ratio { 3.0f }, attackMs { 10.0f }, releaseMs { 120.0f }, makeupDb { 0.0f };
    std::atomic<float> ceilingDb { -0.3f };

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("MASTERFX");
        t.setProperty ("eqOn", eqOn.load(), nullptr);
        t.setProperty ("low", lowDb.load(), nullptr);
        t.setProperty ("mid", midDb.load(), nullptr);
        t.setProperty ("midFreq", midFreq.load(), nullptr);
        t.setProperty ("high", highDb.load(), nullptr);
        t.setProperty ("compOn", compOn.load(), nullptr);
        t.setProperty ("threshold", threshold.load(), nullptr);
        t.setProperty ("ratio", ratio.load(), nullptr);
        t.setProperty ("attack", attackMs.load(), nullptr);
        t.setProperty ("release", releaseMs.load(), nullptr);
        t.setProperty ("makeup", makeupDb.load(), nullptr);
        t.setProperty ("limiterOn", limiterOn.load(), nullptr);
        t.setProperty ("ceiling", ceilingDb.load(), nullptr);
        return t;
    }

    void fromValueTree (const juce::ValueTree& t)
    {
        eqOn = (bool) t.getProperty ("eqOn", false);
        lowDb = (float) t.getProperty ("low", 0.0f);
        midDb = (float) t.getProperty ("mid", 0.0f);
        midFreq = (float) t.getProperty ("midFreq", 1000.0f);
        highDb = (float) t.getProperty ("high", 0.0f);
        compOn = (bool) t.getProperty ("compOn", false);
        threshold = (float) t.getProperty ("threshold", -18.0f);
        ratio = (float) t.getProperty ("ratio", 3.0f);
        attackMs = (float) t.getProperty ("attack", 10.0f);
        releaseMs = (float) t.getProperty ("release", 120.0f);
        makeupDb = (float) t.getProperty ("makeup", 0.0f);
        limiterOn = (bool) t.getProperty ("limiterOn", false);
        ceilingDb = (float) t.getProperty ("ceiling", -0.3f);
    }
};

/** The master chain itself (audio thread). */
class MasterFx
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec mono { sampleRate, (juce::uint32) juce::jmax (1, blockSize), 1 };
        juce::dsp::ProcessSpec stereo { sampleRate, (juce::uint32) juce::jmax (1, blockSize), 2 };
        for (auto& ch : eq)
            for (auto& f : ch)
            {
                f.coefficients = new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
                f.prepare (mono);
                f.reset();
            }
        comp.prepare (stereo);
        comp.reset();
        limiter.prepare (stereo);
        limiter.reset();
        lastLow = lastMid = lastFreq = lastHigh = -1000.0f;
        gainReductionDb = 0.0f;
    }

    void process (juce::AudioBuffer<float>& buffer, int numSamples, const MasterFxSettings& s)
    {
        const int chans = juce::jmin (2, buffer.getNumChannels());
        if (numSamples <= 0 || chans == 0) return;

        if (s.eqOn.load())
        {
            updateEq (s);
            for (int ch = 0; ch < chans; ++ch)
            {
                float* d = buffer.getWritePointer (ch);
                for (auto& f : eq[(size_t) ch])
                    for (int i = 0; i < numSamples; ++i)
                        d[i] = f.processSample (d[i]);
            }
        }
        else if (eqWasOn)
        {
            for (auto& ch : eq) for (auto& f : ch) f.reset();
        }
        eqWasOn = s.eqOn.load();

        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) chans, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);

        if (s.compOn.load())
        {
            const float before = peak (buffer, chans, numSamples);
            comp.setThreshold (s.threshold.load());
            comp.setRatio (juce::jmax (1.0f, s.ratio.load()));
            comp.setAttack (juce::jmax (0.1f, s.attackMs.load()));
            comp.setRelease (juce::jmax (1.0f, s.releaseMs.load()));
            comp.process (ctx);
            const float after = peak (buffer, chans, numSamples);
            if (before > 1.0e-5f)
                gainReductionDb = gainReductionDb.load() * 0.7f + 0.3f * juce::jmin (0.0f, juce::Decibels::gainToDecibels (after / before));
            buffer.applyGain (0, numSamples, juce::Decibels::decibelsToGain (s.makeupDb.load()));
        }
        else
        {
            if (compWasOn) comp.reset();
            gainReductionDb = 0.0f;
        }
        compWasOn = s.compOn.load();

        if (s.limiterOn.load())
        {
            // JUCE's limiter turns its threshold up to 0 dBFS; turning it back down makes the threshold the ceiling
            const float ceiling = juce::jlimit (-24.0f, 0.0f, s.ceilingDb.load());
            limiter.setThreshold (ceiling);
            limiter.setRelease (60.0f);
            limiter.process (ctx);
            buffer.applyGain (0, numSamples, juce::Decibels::decibelsToGain (ceiling));
        }
        else if (limiterWasOn)
        {
            limiter.reset();
        }
        limiterWasOn = s.limiterOn.load();

        outputPeak = juce::jmax (outputPeak.load() * 0.85f, peak (buffer, chans, numSamples));
    }

    std::atomic<float> gainReductionDb { 0.0f }; // for the UI meter (<= 0)
    std::atomic<float> outputPeak { 0.0f };

private:
    static float peak (const juce::AudioBuffer<float>& b, int chans, int n)
    {
        float m = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            m = juce::jmax (m, b.getMagnitude (ch, 0, n));
        return m;
    }

    void updateEq (const MasterFxSettings& s)
    {
        const float lo = s.lowDb.load(), mid = s.midDb.load(), fq = juce::jlimit (60.0f, 12000.0f, s.midFreq.load()), hi = s.highDb.load();
        if (juce::exactlyEqual (lo, lastLow) && juce::exactlyEqual (mid, lastMid) && juce::exactlyEqual (fq, lastFreq) && juce::exactlyEqual (hi, lastHigh)) return;
        using AC = juce::dsp::IIR::ArrayCoefficients<float>;
        const auto cLow = AC::makeLowShelf (sr, 100.0f, 0.7f, juce::Decibels::decibelsToGain (lo));
        const auto cMid = AC::makePeakFilter (sr, fq, 0.8f, juce::Decibels::decibelsToGain (mid));
        const auto cHigh = AC::makeHighShelf (sr, juce::jmin (8000.0f, (float) sr * 0.4f), 0.7f, juce::Decibels::decibelsToGain (hi));
        for (auto& ch : eq)
        {
            *ch[0].coefficients = cLow;   // assigns in place: no allocation on the audio thread
            *ch[1].coefficients = cMid;
            *ch[2].coefficients = cHigh;
        }
        lastLow = lo; lastMid = mid; lastFreq = fq; lastHigh = hi;
    }

    double sr = 44100.0;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 3>, 2> eq;
    juce::dsp::Compressor<float> comp;
    juce::dsp::Limiter<float> limiter;
    float lastLow = -1000.0f, lastMid = -1000.0f, lastFreq = -1000.0f, lastHigh = -1000.0f;
    bool eqWasOn = false, compWasOn = false, limiterWasOn = false;
};
} // namespace rz
