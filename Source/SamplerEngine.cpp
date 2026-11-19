#include "SamplerEngine.h"
#include <cmath>

namespace rz
{
//==============================================================================
void Voice::start (int padIndex, SampleData::Ptr s, const PadSettings& ps, float velocity, double hostRate, juce::uint32 order)
{
    sample = std::move (s);
    settings = &ps;
    pad = padIndex;
    age = order;
    sr = hostRate;

    const int len = sample->buffer.getNumSamples();
    float st = ps.start.load(), en = ps.end.load();
    if (en < st) std::swap (st, en);

    s0 = juce::jlimit (0, juce::jmax (0, len - 1), (int) (st * (float) len));
    s1 = juce::jlimit (s0 + 1, juce::jmax (s0 + 1, len), (int) (en * (float) len));

    reverse = ps.reverse.load();
    loop = ps.loop.load();
    toFx = ps.fx.load();
    pos = reverse ? (double) (s1 - 1) : (double) s0;
    baseRatio = sample->sampleRate / hostRate;

    vel = velocity;
    env = 0.0f;
    stage = Stage::attack;
    attackInc = 1.0f / juce::jmax (1.0f, ps.attackMs.load() * 0.001f * (float) sr);
    active = len > 0;
}

void Voice::release()
{
    if (active && stage != Stage::release)
    {
        stage = Stage::release;
        releaseDec = 1.0f / juce::jmax (1.0f, settings->releaseMs.load() * 0.001f * (float) sr);
    }
}

void Voice::fastStop()
{
    if (! active)
        return;

    const float fast = 1.0f / juce::jmax (1.0f, 0.004f * (float) sr);
    releaseDec = (stage == Stage::release) ? juce::jmax (releaseDec, fast) : fast;
    stage = Stage::release;
}

void Voice::stop()
{
    active = false;
    sample = nullptr; // the processor's sample pool keeps the data alive, so this never frees memory here
}

float Voice::getNormalisedPosition() const noexcept
{
    if (! active || sample == nullptr)
        return -1.0f;
    return (float) (pos / (double) juce::jmax (1, sample->buffer.getNumSamples()));
}

void Voice::render (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& fx, int startSample, int numSamples)
{
    if (! active)
        return;

    auto& out = toFx ? fx : dry;
    const float* srcL = sample->buffer.getReadPointer (0);
    const float* srcR = sample->buffer.getReadPointer (1);
    const int len = sample->buffer.getNumSamples();

    const double inc = baseRatio * std::pow (2.0, (double) settings->pitch.load() / 12.0);
    const float g = juce::Decibels::decibelsToGain (settings->gainDb.load(), -80.0f) * vel;
    const float angle = (settings->pan.load() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float gl = g * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    const float gr = g * std::sin (angle) * juce::MathConstants<float>::sqrt2;
    const double regionLen = (double) (s1 - s0);

    float* oL = out.getWritePointer (0, startSample);
    float* oR = out.getWritePointer (1, startSample);

    for (int i = 0; i < numSamples; ++i)
    {
        if (! reverse && pos >= (double) s1)
        {
            if (! loop) { stop(); return; }
            pos -= regionLen;
            if (pos >= (double) s1) pos = (double) s0;
        }
        else if (reverse && pos < (double) s0)
        {
            if (! loop) { stop(); return; }
            pos += regionLen;
            if (pos < (double) s0) pos = (double) (s1 - 1);
        }

        const int i0 = juce::jlimit (0, len - 1, (int) pos);
        const int i1 = juce::jmin (i0 + 1, len - 1);
        const float frac = (float) (pos - (double) i0);
        const float l = srcL[i0] + frac * (srcL[i1] - srcL[i0]);
        const float r = srcR[i0] + frac * (srcR[i1] - srcR[i0]);

        switch (stage)
        {
            case Stage::attack:
                env += attackInc;
                if (env >= 1.0f) { env = 1.0f; stage = Stage::sustain; }
                break;
            case Stage::release:
                env -= releaseDec;
                if (env <= 0.0f) { stop(); return; }
                break;
            case Stage::sustain:
                break;
        }

        // short fade at the end of a one-shot region to avoid clicks
        float edge = 1.0f;
        if (! loop)
        {
            const double remain = (reverse ? (pos - (double) s0) : ((double) s1 - pos)) / inc;
            if (remain < 48.0)
                edge = (float) juce::jmax (0.0, remain / 48.0);
        }

        const float e = env * edge;
        oL[i] += l * gl * e;
        oR[i] += r * gr * e;

        pos += reverse ? -inc : inc;
    }
}

//==============================================================================
SamplerEngine::SamplerEngine()
{
    for (auto& h : hitCounter) h = 0;
    for (auto& p : playPosition) p = -1.0f;
}

void SamplerEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& v : voices)
        if (v.isActive())
            v.fastStop();
}

void SamplerEngine::setPadSample (int pad, SampleData::Ptr s)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    pads[(size_t) pad] = std::move (s);
    ++version;
}

SampleData::Ptr SamplerEngine::getPadSample (int pad) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return pads[(size_t) pad];
}

void SamplerEngine::syncSamples()
{
    const int v = version.load();
    if (v == lastVersion)
        return;

    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked())
        return;

    for (size_t i = 0; i < pads.size(); ++i)
        audioPads[i] = pads[i];

    lastVersion = v;
}

bool SamplerEngine::isPadPlaying (int pad) const
{
    for (auto& v : voices)
        if (v.isActive() && v.getPad() == pad && ! v.isReleasing())
            return true;
    return false;
}

void SamplerEngine::noteOn (int pad, float velocity, bool fromSequencer)
{
    if (pad < 0 || pad >= numPads)
        return;

    auto& ps = settings[(size_t) pad];
    auto s = audioPads[(size_t) pad];
    if (! fromSequencer)
    {
        lastPlayedPad = pad;
        ++playedCounter;
    }

    if (s == nullptr || ps.mute.load())
        return;

    // Loop pads without gate behave like a toggle when played by hand.
    if (! fromSequencer && ps.loop.load() && ! ps.gate.load() && isPadPlaying (pad))
    {
        for (auto& v : voices)
            if (v.isActive() && v.getPad() == pad)
                v.release();
        return;
    }

    // Each pad is monophonic: retriggering cuts the previous voice.
    for (auto& v : voices)
        if (v.isActive() && v.getPad() == pad)
            v.fastStop();

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.isActive()) { target = &v; break; }

    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.getAge() < target->getAge())
                target = &v;
    }

    target->start (pad, s, ps, velocity, sampleRate, ++voiceCounter);
    hitCounter[(size_t) pad]++;
}

void SamplerEngine::noteOff (int pad, bool fromSequencer)
{
    if (pad < 0 || pad >= numPads)
        return;

    auto& ps = settings[(size_t) pad];
    const bool shouldRelease = ps.gate.load() || (fromSequencer && ps.loop.load());
    if (! shouldRelease)
        return;

    for (auto& v : voices)
        if (v.isActive() && v.getPad() == pad)
            v.release();
}

void SamplerEngine::allNotesOff()
{
    for (auto& v : voices)
        v.fastStop();
}

void SamplerEngine::render (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& fx, int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;

    for (auto& v : voices)
        v.render (dry, fx, startSample, numSamples);
}

void SamplerEngine::updatePlayPositions()
{
    std::array<float, numPads> p;
    p.fill (-1.0f);

    for (auto& v : voices)
        if (v.isActive() && ! v.isReleasing())
            p[(size_t) v.getPad()] = v.getNormalisedPosition();

    for (size_t i = 0; i < p.size(); ++i)
        playPosition[i] = p[i];
}
} // namespace rz
