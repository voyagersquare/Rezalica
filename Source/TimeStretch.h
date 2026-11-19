#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>

/** Offline time stretching (pitch stays the same).

    Beats   - for drums / percussive material: the audio is cut at its transients (or on a 1/8, 1/16
              grid); every piece plays at its original speed at its new, stretched position. When the
              material is slowed down, the gap after each piece is filled by looping its tail
              (forward or back-and-forth) or left silent; a transient envelope can fade each piece.
    Complex - for melodic / mixed material: a phase vocoder with identity phase locking (peaks keep
              their phase relationships), stereo phase coherence and phase resets at strong onsets,
              which keeps tones smooth and attacks reasonably sharp. */
namespace rz::stretch
{
/** Onset positions in samples (always starts with 0). */
inline std::vector<int> findOnsets (const juce::AudioBuffer<float>& in, double sampleRate)
{
    const int len = in.getNumSamples();
    const int hop = 256;
    const int frames = len / hop;
    std::vector<int> result { 0 };
    if (frames < 8)
        return result;

    const float* L = in.getReadPointer (0);
    const float* R = in.getReadPointer (juce::jmin (1, in.getNumChannels() - 1));
    std::vector<float> energy ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        float sum = 0.0f;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
            sum += 0.25f * (L[i] + R[i]) * (L[i] + R[i]);
        energy[(size_t) f] = std::sqrt (sum / (float) hop);
    }

    std::vector<float> flux ((size_t) frames, 0.0f);
    for (int f = 1; f < frames; ++f)
        flux[(size_t) f] = juce::jmax (0.0f, energy[(size_t) f] - energy[(size_t) f - 1]);

    // adaptive threshold: local mean of the flux plus a fraction of the global maximum
    const float maxFlux = *std::max_element (flux.begin(), flux.end());
    if (maxFlux <= 0.0f)
        return result;

    const int minGap = juce::jmax (2, (int) (sampleRate * 0.05 / hop));
    int last = -minGap;
    for (int f = 2; f < frames - 1; ++f)
    {
        float local = 0.0f;
        int n = 0;
        for (int j = juce::jmax (1, f - 8); j <= juce::jmin (frames - 1, f + 8); ++j, ++n)
            local += flux[(size_t) j];
        local /= (float) juce::jmax (1, n);

        const float v = flux[(size_t) f];
        if (v > 1.6f * local + 0.08f * maxFlux && v >= flux[(size_t) f - 1] && v >= flux[(size_t) f + 1] && f - last >= minGap)
        {
            // move back to the quietest point just before the attack
            int pos = f * hop;
            int best = pos;
            float bestLevel = 1.0e9f;
            for (int i = juce::jmax (0, pos - hop); i < juce::jmin (len, pos + hop / 2); ++i)
            {
                const float a = std::abs (L[i] + R[i]);
                if (a < bestLevel) { bestLevel = a; best = i; }
            }
            if (best > result.back() + hop)
                result.push_back (best);
            last = f;
        }
    }
    return result;
}

//==============================================================================
/** Beats mode. preserve: 0 transients, 1 = 1/8 grid, 2 = 1/16 grid.
    loopMode: 0 off, 1 forward, 2 back-and-forth. envelope: 0..100 (100 = no fade). */
inline juce::AudioBuffer<float> beats (const juce::AudioBuffer<float>& in, double sampleRate, int outLen,
                                       int preserve, int loopMode, float envelope, double lengthBeats)
{
    const int inLen = in.getNumSamples();
    juce::AudioBuffer<float> out (2, juce::jmax (1, outLen));
    out.clear();
    if (inLen < 2 || outLen < 2)
        return out;

    std::vector<int> bounds;
    if (preserve == 0)
    {
        bounds = findOnsets (in, sampleRate);
    }
    else
    {
        const int steps = juce::jmax (1, (int) std::lround (lengthBeats * (preserve == 1 ? 2.0 : 4.0)));
        for (int i = 0; i < steps; ++i)
            bounds.push_back ((int) ((juce::int64) i * inLen / steps));
    }
    bounds.push_back (inLen);

    const double ratio = (double) outLen / (double) inLen;
    const int fade = juce::jmax (16, (int) (sampleRate * 0.004));
    const float env = juce::jlimit (0.0f, 1.0f, envelope / 100.0f);

    for (size_t i = 0; i + 1 < bounds.size(); ++i)
    {
        const int b0 = bounds[i], b1 = bounds[i + 1];
        const int segLen = b1 - b0;
        if (segLen <= 0)
            continue;

        const int o0 = (int) std::llround (b0 * ratio);
        const int o1 = (i + 2 == bounds.size()) ? outLen : (int) std::llround (b1 * ratio);
        const int outSeg = juce::jmin (o1, outLen) - o0;
        if (outSeg <= 0)
            continue;

        const int loopStart = b0 + segLen / 2;
        const int loopLen = juce::jmax (1, b1 - loopStart);
        const bool looping = loopMode != 0 && outSeg > segLen;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = in.getReadPointer (juce::jmin (ch, in.getNumChannels() - 1));
            float* dst = out.getWritePointer (ch);

            for (int n = 0; n < outSeg; ++n)
            {
                int s;
                float g = 1.0f;

                if (n < segLen)
                {
                    s = b0 + n;
                    // forward loops jump back: fade out the end of the first pass
                    if (looping && loopMode == 1 && n > segLen - fade)
                        g *= (float) (segLen - n) / (float) fade;
                }
                else if (! looping)
                {
                    break; // silence after the piece
                }
                else if (loopMode == 1)
                {
                    const int k = (n - segLen) % loopLen;
                    s = loopStart + k;
                    g *= juce::jmin (1.0f, (float) k / (float) fade, (float) (loopLen - k) / (float) fade);
                }
                else
                {
                    const int period = 2 * loopLen;
                    const int k = (n - segLen) % period;
                    s = k < loopLen ? b1 - 1 - k : loopStart + (k - loopLen);
                }

                g *= 1.0f - (1.0f - env) * (float) n / (float) outSeg;      // transient envelope
                if (n > outSeg - fade)
                    g *= (float) (outSeg - n) / (float) fade;                // de-click the cut

                dst[o0 + n] += src[juce::jlimit (0, inLen - 1, s)] * g;
            }
        }
    }
    return out;
}

//==============================================================================
/** Complex mode: phase vocoder with identity phase locking. */
inline juce::AudioBuffer<float> complex (const juce::AudioBuffer<float>& in, double sampleRate, int outLen)
{
    const int inLen = in.getNumSamples();
    juce::AudioBuffer<float> out (2, juce::jmax (1, outLen));
    out.clear();
    if (inLen < 2 || outLen < 2)
        return out;

    const int order = sampleRate > 60000.0 ? 13 : 12;
    const int N = 1 << order;
    const int half = N / 2;
    const int hs = N / 4;                       // synthesis hop
    const double ratio = (double) outLen / (double) inLen;
    const double ha = hs / ratio;               // analysis hop
    const int pad = N;

    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) N);
    for (int n = 0; n < N; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N);

    // padded input: L, R, mid
    const int padLen = inLen + 2 * pad;
    std::vector<std::vector<float>> x (3, std::vector<float> ((size_t) padLen, 0.0f));
    for (int i = 0; i < inLen; ++i)
    {
        const float l = in.getSample (0, i);
        const float r = in.getSample (juce::jmin (1, in.getNumChannels() - 1), i);
        x[0][(size_t) (pad + i)] = l;
        x[1][(size_t) (pad + i)] = r;
        x[2][(size_t) (pad + i)] = 0.5f * (l + r);
    }

    const int outPadLen = (int) std::ceil (padLen * ratio) + N + hs;
    std::vector<std::vector<float>> y (2, std::vector<float> ((size_t) outPadLen, 0.0f));

    // calibrate the FFT round trip scale and the window overlap gain
    float scale;
    {
        std::vector<float> t ((size_t) (2 * N), 0.0f);
        t[1] = 1.0f;
        fft.performRealOnlyForwardTransform (t.data());
        fft.performRealOnlyInverseTransform (t.data());
        scale = std::abs (t[1]) > 1.0e-9f ? 1.0f / t[1] : 1.0f;
        scale /= 1.5f; // sum of hann^2 at 75 % overlap
    }

    std::vector<float> buf ((size_t) (2 * N));
    std::vector<float> mag[3], ph[3];
    for (int c = 0; c < 3; ++c) { mag[c].resize ((size_t) half + 1); ph[c].resize ((size_t) half + 1); }
    std::vector<float> prevPhase ((size_t) half + 1, 0.0f), synPhase ((size_t) half + 1, 0.0f);
    std::vector<int> peakOf ((size_t) half + 1, 0);
    std::vector<int> peaks;

    auto wrap = [] (float a)
    {
        return a - juce::MathConstants<float>::twoPi * std::floor ((a + juce::MathConstants<float>::pi) / juce::MathConstants<float>::twoPi);
    };

    int prevStart = 0;
    float prevEnergy = 0.0f;
    const int numFrames = (int) std::floor ((padLen - N) / ha) + 1;

    for (int m = 0; m < numFrames; ++m)
    {
        const int start = (int) std::llround (m * ha);
        if (start + N > padLen)
            break;
        const int ts = m * hs;
        if (ts + N > outPadLen)
            break;

        // analysis of L, R and mid
        float energy = 0.0f;
        for (int c = 0; c < 3; ++c)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int n = 0; n < N; ++n)
                buf[(size_t) n] = x[(size_t) c][(size_t) (start + n)] * window[(size_t) n];
            fft.performRealOnlyForwardTransform (buf.data());
            for (int k = 0; k <= half; ++k)
            {
                const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
                mag[c][(size_t) k] = std::sqrt (re * re + im * im);
                ph[c][(size_t) k] = std::atan2 (im, re);
                if (c == 2) energy += mag[c][(size_t) k] * mag[c][(size_t) k];
            }
        }

        // peaks of the mid spectrum
        peaks.clear();
        for (int k = 2; k < half - 1; ++k)
        {
            const float v = mag[2][(size_t) k];
            if (v > mag[2][(size_t) k - 1] && v >= mag[2][(size_t) k + 1] && v > mag[2][(size_t) k - 2] && v >= mag[2][(size_t) k + 2] && v > 1.0e-6f)
                peaks.push_back (k);
        }

        const bool onset = m > 0 && energy > 4.0f * prevEnergy && energy > 1.0e-3f;
        const int hopA = start - prevStart;

        if (m == 0 || onset || peaks.empty() || hopA <= 0)
        {
            for (int k = 0; k <= half; ++k)
                synPhase[(size_t) k] = (m == 0 || onset) ? ph[2][(size_t) k]
                                                         : synPhase[(size_t) k] + juce::MathConstants<float>::twoPi * (float) k * (float) hs / (float) N;
        }
        else
        {
            // advance the phase of each peak by its measured frequency
            for (int p : peaks)
            {
                const float omega = juce::MathConstants<float>::twoPi * (float) p / (float) N;
                const float dphi = wrap (ph[2][(size_t) p] - prevPhase[(size_t) p] - omega * (float) hopA);
                const float freq = omega + dphi / (float) hopA;
                synPhase[(size_t) p] = wrap (synPhase[(size_t) p] + freq * (float) hs);
            }
            // identity phase locking: bins follow the peak of their region
            size_t pi = 0;
            for (int k = 0; k <= half; ++k)
            {
                while (pi + 1 < peaks.size() && std::abs (peaks[pi + 1] - k) < std::abs (peaks[pi] - k))
                    ++pi;
                peakOf[(size_t) k] = peaks[pi];
            }
            for (int k = 0; k <= half; ++k)
            {
                const int p = peakOf[(size_t) k];
                if (p != k)
                    synPhase[(size_t) k] = synPhase[(size_t) p] + (ph[2][(size_t) k] - ph[2][(size_t) p]);
            }
        }

        prevPhase = ph[2];
        prevStart = start;
        prevEnergy = energy;

        // synthesis for L and R, keeping each channel's phase offset to the mid
        for (int c = 0; c < 2; ++c)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int k = 0; k <= half; ++k)
            {
                const float phase = synPhase[(size_t) k] + (ph[c][(size_t) k] - ph[2][(size_t) k]);
                const float re = mag[c][(size_t) k] * std::cos (phase);
                const float im = mag[c][(size_t) k] * std::sin (phase);
                buf[(size_t) (2 * k)] = re;
                buf[(size_t) (2 * k + 1)] = (k == 0 || k == half) ? 0.0f : im;
            }
            for (int k = half + 1; k < N; ++k)
            {
                buf[(size_t) (2 * k)] = buf[(size_t) (2 * (N - k))];
                buf[(size_t) (2 * k + 1)] = -buf[(size_t) (2 * (N - k) + 1)];
            }
            fft.performRealOnlyInverseTransform (buf.data());
            auto& dst = y[(size_t) c];
            for (int n = 0; n < N; ++n)
                dst[(size_t) (ts + n)] += buf[(size_t) n] * window[(size_t) n] * scale;
        }
    }

    const int offset = (int) std::llround (pad * ratio);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < outLen; ++i)
        {
            const int j = offset + i;
            out.setSample (c, i, j < outPadLen ? y[(size_t) c][(size_t) j] : 0.0f);
        }
    return out;
}
} // namespace rz::stretch
