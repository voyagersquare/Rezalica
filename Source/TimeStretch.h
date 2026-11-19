#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <functional>

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
/** Maps an input sample position to an output sample position (monotonic). */
using TimeMap = std::function<double (double)>;

/** Onset positions in samples (always starts with 0), refined to the start of each attack. */
inline std::vector<int> findOnsets (const juce::AudioBuffer<float>& in, double sampleRate)
{
    const int len = in.getNumSamples();
    const int hop = 128;
    const int frames = len / hop;
    std::vector<int> result { 0 };
    if (frames < 8)
        return result;

    const float* L = in.getReadPointer (0);
    const float* R = in.getReadPointer (juce::jmin (1, in.getNumChannels() - 1));

    // energy per frame, with a gentle high-frequency emphasis so hats / snares register too
    std::vector<float> energy ((size_t) frames);
    float prevMono = 0.0f;
    for (int f = 0; f < frames; ++f)
    {
        float sum = 0.0f;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float hf = m - 0.7f * prevMono;
            prevMono = m;
            sum += m * m + 0.5f * hf * hf;
        }
        energy[(size_t) f] = std::sqrt (sum / (float) hop);
    }

    // log-energy flux is level independent: quiet hits count as much as loud ones
    std::vector<float> flux ((size_t) frames, 0.0f);
    for (int f = 1; f < frames; ++f)
        flux[(size_t) f] = juce::jmax (0.0f, std::log10 (energy[(size_t) f] + 1.0e-4f) - std::log10 (energy[(size_t) f - 1] + 1.0e-4f))
                         * juce::jlimit (0.0f, 1.0f, energy[(size_t) f] * 50.0f); // ignore near-silence
    const float maxFlux = *std::max_element (flux.begin(), flux.end());
    if (maxFlux <= 0.0f)
        return result;

    const int minGap = juce::jmax (2, (int) (sampleRate * 0.045 / hop));
    int last = -minGap;
    for (int f = 2; f < frames - 1; ++f)
    {
        float local = 0.0f;
        int n = 0;
        for (int j = juce::jmax (1, f - 12); j <= juce::jmin (frames - 1, f + 12); ++j, ++n)
            local += flux[(size_t) j];
        local /= (float) juce::jmax (1, n);

        const float v = flux[(size_t) f];
        if (v > 1.8f * local + 0.1f * maxFlux && v >= flux[(size_t) f - 1] && v >= flux[(size_t) f + 1] && f - last >= minGap)
        {
            // sample-accurate start: walk back from the frame to where the signal leaves its noise floor
            const int frameStart = juce::jmax (0, (f - 1) * hop);
            float floorLevel = 0.0f;
            for (int i = juce::jmax (0, frameStart - hop); i < frameStart; ++i)
                floorLevel = juce::jmax (floorLevel, std::abs (L[i] + R[i]) * 0.5f);
            float peak = 0.0f;
            for (int i = frameStart; i < juce::jmin (len, (f + 1) * hop); ++i)
                peak = juce::jmax (peak, std::abs (L[i] + R[i]) * 0.5f);
            const float threshold = floorLevel + 0.1f * (peak - floorLevel);

            int pos = frameStart;
            for (int i = frameStart; i < juce::jmin (len, (f + 1) * hop); ++i)
                if (std::abs (L[i] + R[i]) * 0.5f > threshold) { pos = i; break; }

            // a few samples earlier, at a zero crossing if there is one close by
            pos = juce::jmax (0, pos - 32);
            for (int i = pos; i > juce::jmax (1, pos - 64); --i)
                if ((L[i] + R[i] >= 0.0f) != (L[i - 1] + R[i - 1] >= 0.0f)) { pos = i; break; }

            if (pos > result.back() + hop)
                result.push_back (pos);
            last = f;
        }
    }
    return result;
}

//==============================================================================
/** Beats mode. preserve: 0 transients, 1 = 1/8 grid, 2 = 1/16 grid.
    loopMode: 0 off, 1 forward, 2 back-and-forth. envelope: 0..100 (100 = no fade). */
inline juce::AudioBuffer<float> beats (const juce::AudioBuffer<float>& in, double sampleRate, int outLen,
                                       int preserve, int loopMode, float envelope, double lengthBeats,
                                       TimeMap outForIn = nullptr)
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
    // pieces overlap by a short crossfade: the previous one fades out while the next fades in.
    // At detected transients the fade-in is tiny so attacks stay sharp.
    const int cutFade = juce::jmax (16, (int) (sampleRate * 0.004));
    const int fadeIn = preserve == 0 ? juce::jmax (8, (int) (sampleRate * 0.0005)) : cutFade;
    const float env = juce::jlimit (0.0f, 1.0f, envelope / 100.0f);

    for (size_t i = 0; i + 1 < bounds.size(); ++i)
    {
        const int b0 = bounds[i], b1 = bounds[i + 1];
        const int segLen = b1 - b0;
        if (segLen <= 0)
            continue;

        const int o0 = (int) std::llround (outForIn != nullptr ? outForIn ((double) b0) : b0 * ratio);
        const int o1 = (i + 2 == bounds.size()) ? outLen : (int) std::llround (outForIn != nullptr ? outForIn ((double) b1) : b1 * ratio);
        const int outSeg = juce::jmin (o1, outLen) - o0;
        if (outSeg <= 0)
            continue;

        // loop region: the second half of the piece (its sustain / tail)
        const int loopStart = b0 + segLen / 2;
        const int loopLen = juce::jmax (2, b1 - loopStart);
        const int xfade = juce::jlimit (8, juce::jmax (8, loopLen / 2), (int) (sampleRate * 0.012));
        const bool looping = loopMode != 0 && outSeg > segLen && loopStart - xfade >= b0;
        // the last piece does not need its tail cut: it can ring into the end of the file
        const bool lastPiece = i + 2 == bounds.size();

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = in.getReadPointer (juce::jmin (ch, in.getNumChannels() - 1));
            auto at = [&] (int s) { return src[juce::jlimit (0, inLen - 1, s)]; };
            float* dst = out.getWritePointer (ch);

            const int total = juce::jmin (outSeg + cutFade, outLen - o0); // play on into the next piece's fade-in
            for (int n = 0; n < total; ++n)
            {
                float v;
                if (n < segLen && ! (looping && loopMode == 1 && n >= segLen - xfade))
                {
                    v = at (b0 + n);
                }
                else if (! looping)
                {
                    break; // silence after the piece
                }
                else if (loopMode == 1)
                {
                    // forward loop with an equal-power crossfade: the end of the loop blends
                    // into the audio just before its start, so there is no jump and no dip
                    const int pos = n - (segLen - xfade);            // 0 at the first crossfade
                    const int k = pos % loopLen;                     // position inside one loop pass
                    if (k < xfade)
                    {
                        const float w = (float) k / (float) xfade;
                        const float outgoing = at (b1 - xfade + k);
                        const float incoming = at (loopStart - xfade + k);
                        v = outgoing * std::cos (w * juce::MathConstants<float>::halfPi)
                          + incoming * std::sin (w * juce::MathConstants<float>::halfPi);
                    }
                    else
                    {
                        v = at (loopStart - xfade + k);
                    }
                }
                else
                {
                    // back-and-forth: the waveform turns around, so it stays continuous
                    const int period = 2 * loopLen;
                    const int k = (n - segLen) % period;
                    v = at (k < loopLen ? b1 - 1 - k : loopStart + (k - loopLen));
                }

                float g = 1.0f - (1.0f - env) * juce::jmin (1.0f, (float) n / (float) outSeg);   // transient envelope
                if (i > 0 && n < fadeIn)
                    g *= std::sin ((float) n / (float) fadeIn * juce::MathConstants<float>::halfPi);    // fade in
                if (n >= outSeg)
                {
                    if (lastPiece) break;
                    g *= std::cos ((float) (n - outSeg) / (float) cutFade * juce::MathConstants<float>::halfPi); // fade out
                }
                dst[o0 + n] += v * g;
            }
        }
    }
    return out;
}

//==============================================================================
/** Complex mode: phase vocoder with identity phase locking.
    The phases are computed once on the mid signal; left and right are rotated by the same
    amount, which keeps the stereo image intact and only needs one phase calculation. */
inline juce::AudioBuffer<float> complex (const juce::AudioBuffer<float>& in, double sampleRate, int outLen,
                                        TimeMap inForOut = nullptr)
{
    const int inLen = in.getNumSamples();
    juce::AudioBuffer<float> out (2, juce::jmax (1, outLen));
    out.clear();
    if (inLen < 2 || outLen < 2)
        return out;

    const int order = sampleRate > 60000.0 ? 13 : 12;
    const int N = 1 << order;
    const int half = N / 2;
    const int hs = N / 8;                       // synthesis hop: 87.5 % overlap, smoother result
    const double ratio = (double) outLen / (double) inLen;
    const double ha = hs / ratio;               // analysis hop
    const int pad = N;

    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) N);
    for (int n = 0; n < N; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N);

    const int padLen = inLen + 2 * pad;
    std::vector<float> xl ((size_t) padLen, 0.0f), xr ((size_t) padLen, 0.0f);
    for (int i = 0; i < inLen; ++i)
    {
        xl[(size_t) (pad + i)] = in.getSample (0, i);
        xr[(size_t) (pad + i)] = in.getSample (juce::jmin (1, in.getNumChannels() - 1), i);
    }

    const int outPadLen = outLen + 2 * pad + N;

    // output position -> input position, both in the padded domains (padding keeps a 1:1 speed)
    auto inputFor = [&] (double t)
    {
        if (t < pad) return t;
        if (t < pad + outLen) return pad + (inForOut != nullptr ? inForOut (t - pad) : (t - pad) / ratio);
        return pad + inLen + (t - pad - outLen);
    };
    std::vector<float> yl ((size_t) outPadLen, 0.0f), yr ((size_t) outPadLen, 0.0f);

    // FFT round trip scale and the window overlap gain (sum of hann^2 at 87.5 % overlap = 3)
    float scale;
    {
        std::vector<float> t ((size_t) (2 * N), 0.0f);
        t[1] = 1.0f;
        fft.performRealOnlyForwardTransform (t.data());
        fft.performRealOnlyInverseTransform (t.data());
        scale = (std::abs (t[1]) > 1.0e-9f ? 1.0f / t[1] : 1.0f) / 3.0f;
    }

    std::vector<float> bl ((size_t) (2 * N)), br ((size_t) (2 * N));
    std::vector<float> mag ((size_t) half + 1), phase ((size_t) half + 1);
    std::vector<float> prevPhase ((size_t) half + 1, 0.0f), synPhase ((size_t) half + 1, 0.0f);
    std::vector<int> peaks;
    peaks.reserve ((size_t) half);

    auto wrap = [] (float a)
    {
        return a - juce::MathConstants<float>::twoPi * std::floor ((a + juce::MathConstants<float>::pi) / juce::MathConstants<float>::twoPi);
    };

    juce::ignoreUnused (ha);
    int prevStart = 0;
    float prevEnergy = 0.0f;

    for (int m = 0;; ++m)
    {
        const int ts = m * hs;
        if (ts + N > outPadLen)
            break;
        // the frame centre follows the time map, so the stretch can change from marker to marker
        const int start = juce::jlimit (0, padLen - N, (int) std::llround (inputFor (ts + N * 0.5) - N * 0.5));

        // analysis: L and R spectra, mid = (L + R) / 2 in the frequency domain
        std::fill (bl.begin(), bl.end(), 0.0f);
        std::fill (br.begin(), br.end(), 0.0f);
        for (int n = 0; n < N; ++n)
        {
            bl[(size_t) n] = xl[(size_t) (start + n)] * window[(size_t) n];
            br[(size_t) n] = xr[(size_t) (start + n)] * window[(size_t) n];
        }
        fft.performRealOnlyForwardTransform (bl.data(), true);
        fft.performRealOnlyForwardTransform (br.data(), true);

        float energy = 0.0f;
        for (int k = 0; k <= half; ++k)
        {
            const float re = 0.5f * (bl[(size_t) (2 * k)] + br[(size_t) (2 * k)]);
            const float im = 0.5f * (bl[(size_t) (2 * k + 1)] + br[(size_t) (2 * k + 1)]);
            const float mg2 = re * re + im * im;
            mag[(size_t) k] = std::sqrt (mg2);
            phase[(size_t) k] = std::atan2 (im, re);
            energy += mg2;
        }

        peaks.clear();
        for (int k = 2; k < half - 1; ++k)
        {
            const float v = mag[(size_t) k];
            if (v > 1.0e-6f && v > mag[(size_t) k - 1] && v >= mag[(size_t) k + 1] && v > mag[(size_t) k - 2] && v >= mag[(size_t) k + 2])
                peaks.push_back (k);
        }

        const bool onset = m > 0 && energy > 3.0f * prevEnergy && energy > 1.0e-3f;
        const int hopA = start - prevStart;

        if (m == 0 || onset)
        {
            synPhase = phase; // phase reset keeps attacks sharp
        }
        else if (peaks.empty() || hopA <= 0)
        {
            for (int k = 0; k <= half; ++k)
                synPhase[(size_t) k] = wrap (synPhase[(size_t) k] + juce::MathConstants<float>::twoPi * (float) k * (float) hs / (float) N);
        }
        else
        {
            // each peak moves on by its measured (true) frequency
            for (int p : peaks)
            {
                const float omega = juce::MathConstants<float>::twoPi * (float) p / (float) N;
                const float dphi = wrap (phase[(size_t) p] - prevPhase[(size_t) p] - omega * (float) hopA);
                synPhase[(size_t) p] = wrap (synPhase[(size_t) p] + (omega + dphi / (float) hopA) * (float) hs);
            }
            // identity phase locking: every bin keeps its phase relation to the peak of its region
            size_t pi = 0;
            for (int k = 0; k <= half; ++k)
            {
                while (pi + 1 < peaks.size() && (peaks[pi] + peaks[pi + 1]) / 2 < k)
                    ++pi;
                const int p = peaks[pi];
                if (p != k)
                    synPhase[(size_t) k] = synPhase[(size_t) p] + (phase[(size_t) k] - phase[(size_t) p]);
            }
        }

        prevPhase = phase;
        prevStart = start;
        prevEnergy = energy;

        // synthesis: rotate L and R by (new phase - analysed mid phase)
        for (int k = 0; k <= half; ++k)
        {
            const float rot = synPhase[(size_t) k] - phase[(size_t) k];
            const float c = std::cos (rot), sn = std::sin (rot);
            for (auto* b : { &bl, &br })
            {
                auto& d = *b;
                const float re = d[(size_t) (2 * k)], im = d[(size_t) (2 * k + 1)];
                d[(size_t) (2 * k)] = re * c - im * sn;
                d[(size_t) (2 * k + 1)] = (k == 0 || k == half) ? 0.0f : re * sn + im * c;
            }
        }
        for (auto* b : { &bl, &br })
        {
            auto& d = *b;
            for (int k = half + 1; k < N; ++k)
            {
                d[(size_t) (2 * k)] = d[(size_t) (2 * (N - k))];
                d[(size_t) (2 * k + 1)] = -d[(size_t) (2 * (N - k) + 1)];
            }
            fft.performRealOnlyInverseTransform (d.data());
        }
        for (int n = 0; n < N; ++n)
        {
            const float w = window[(size_t) n] * scale;
            yl[(size_t) (ts + n)] += bl[(size_t) n] * w;
            yr[(size_t) (ts + n)] += br[(size_t) n] * w;
        }
    }

    const int offset = pad;
    for (int i = 0; i < outLen; ++i)
    {
        const int j = offset + i;
        out.setSample (0, i, j < outPadLen ? yl[(size_t) j] : 0.0f);
        out.setSample (1, i, j < outPadLen ? yr[(size_t) j] : 0.0f);
    }
    return out;
}
} // namespace rz::stretch
