#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
const juce::StringArray delayDivisions { "Free", "1/4", "1/8", "1/8 dot", "1/16", "1/4 trip", "1/8 trip" };
const double delayDivisionBeats[] { 0.0, 1.0, 0.5, 0.75, 0.25, 2.0 / 3.0, 1.0 / 3.0 };
}

//==============================================================================
RezalicaProcessor::RezalicaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();
    pendingSeqOff.fill (-1.0);

    pMaster = apvts.getRawParameterValue ("master");

    startTimer (1000);
}

RezalicaProcessor::~RezalicaProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout RezalicaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto db = AudioParameterFloatAttributes().withLabel ("dB");

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.0f), 0.0f, db));


    return layout;
}

bool RezalicaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void RezalicaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    sequencer.prepare (sampleRate);
    dryBuffer.setSize (2, juce::jmax (1, samplesPerBlock));
    for (int i = 0; i < rz::numPads; ++i)
    {
        padFx[(size_t) i].prepare (sampleRate, samplesPerBlock);
        padBuses[(size_t) i].setSize (2, juce::jmax (1, samplesPerBlock));
        padFxIdleSamples[(size_t) i] = 1.0e12;
        padFxWasRunning[(size_t) i] = false;
    }
    pendingSeqOff.fill (-1.0);
}

//==============================================================================
void RezalicaProcessor::addEvent (int offset, int type, int pad, float velocity)
{
    if (numEvents < (int) events.size())
        events[(size_t) numEvents++] = { offset, type, pad, velocity };
}

const juce::StringArray& RezalicaProcessor::getDelayDivisionNames()
{
    return delayDivisions;
}

rz::FxChain::Params RezalicaProcessor::readFxParams (int pad, bool padSounding) const
{
    const auto& f = engine.getPad (pad).effects;
    rz::FxChain::Params p;
    juce::ignoreUnused (padSounding);
    p.filterOn = f.filterOn.load();
    p.filterType = f.filterType.load();
    p.cutoff = f.cutoff.load();
    p.resonance = f.resonance.load();
    p.satOn = f.satOn.load();
    p.tube = f.tube.load();
    p.tape = f.tape.load();
    p.solid = f.solid.load();
    p.delayOn = f.delayOn.load();
    p.feedback = f.feedback.load();
    p.mix = f.delayMix.load();
    p.reverbOn = f.reverbOn.load();
    p.reverbSize = f.reverbSize.load();
    p.reverbDamping = f.reverbDamp.load();
    p.reverbWidth = f.reverbWidth.load();
    p.reverbMix = f.reverbMix.load();

    const int div = juce::jlimit (0, delayDivisions.size() - 1, f.delayDiv.load());
    if (div == 0)
        p.delaySamples = f.delayTime.load() * 0.001f * (float) currentSampleRate;
    else
        p.delaySamples = (float) (delayDivisionBeats[div] * 60.0 / juce::jmax (20.0f, sequencer.effectiveBpm.load()) * currentSampleRate);
    p.delaySamples = juce::jmin (p.delaySamples, (float) (currentSampleRate * 2.0));

    return p;
}

void RezalicaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (dryBuffer.getNumSamples() < numSamples)
    {
        dryBuffer.setSize (2, numSamples, false, false, true);
        for (auto& b : padBuses)
            b.setSize (2, numSamples, false, false, true);
    }
    dryBuffer.clear();
    for (auto& b : padBuses)
        b.clear (0, numSamples);

    engine.syncSamples();
    numEvents = 0;

    // ---- sequencer (first, so live hits can be recorded relative to this block) ----
    juce::Optional<juce::AudioPlayHead::PositionInfo> position;
    if (auto* ph = getPlayHead())
        position = ph->getPosition();

    // note-offs scheduled by earlier sequencer steps
    for (int pad = 0; pad < rz::numPads; ++pad)
    {
        auto& p = pendingSeqOff[(size_t) pad];
        if (p < 0.0) continue;
        if (p < numSamples) { addEvent ((int) p, 1, pad, 0.0f); p = -1.0; }
    }

    const bool wasRunning = sequencer.isRunning();
    sequencer.process (position, numSamples, [&] (int pad, float vel, int offset)
    {
        addEvent (offset, 3, pad, vel);
        pendingSeqOff[(size_t) pad] = offset + sequencer.getSamplesPerStep() * 0.98;
    });

    if (wasRunning && ! sequencer.isRunning())
    {
        for (int pad = 0; pad < rz::numPads; ++pad)
            if (pendingSeqOff[(size_t) pad] >= 0.0) { addEvent (0, 1, pad, 0.0f); pendingSeqOff[(size_t) pad] = -1.0; }
    }

    for (int pad = 0; pad < rz::numPads; ++pad)
    {
        auto& p = pendingSeqOff[(size_t) pad];
        if (p >= 0.0 && p < numSamples) { addEvent ((int) p, 1, pad, 0.0f); p = -1.0; }
        else if (p >= numSamples) p -= numSamples;
    }

    // ---- UI (mouse / computer keyboard) ----
    {
        auto reader = uiFifo.read (uiFifo.getNumReady());
        reader.forEach ([&] (int i)
        {
            const auto& e = uiEvents[(size_t) i];
            if (e.pad < 0) { addEvent (0, -1, -1, 0.0f); return; }
            addEvent (0, e.on ? 2 : 0, e.pad, e.velocity);
            if (e.on) sequencer.recordHit (e.pad, e.velocity, 0);
        });
    }

    // ---- MIDI ----
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int offset = juce::jlimit (0, numSamples - 1, meta.samplePosition);

        if (m.isNoteOn())
        {
            const int pad = m.getNoteNumber() - midiBaseNote;
            if (pad >= 0 && pad < rz::numPads)
            {
                addEvent (offset, 2, pad, m.getFloatVelocity());
                sequencer.recordHit (pad, m.getFloatVelocity(), offset);
            }
        }
        else if (m.isNoteOff())
        {
            const int pad = m.getNoteNumber() - midiBaseNote;
            if (pad >= 0 && pad < rz::numPads)
                addEvent (offset, 0, pad, 0.0f);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            addEvent (offset, -1, -1, 0.0f);
        }
    }
    midi.clear();

    std::stable_sort (events.begin(), events.begin() + numEvents, [] (const Event& a, const Event& b)
    {
        return a.offset != b.offset ? a.offset < b.offset : a.type < b.type;
    });

    // ---- render, splitting the block at each event ----
    int cursor = 0;
    for (int i = 0; i < numEvents; ++i)
    {
        const auto& e = events[(size_t) i];
        engine.render (dryBuffer, padBuses, cursor, e.offset - cursor);
        cursor = e.offset;

        switch (e.type)
        {
            case -1: engine.allNotesOff(); sequencer.internalPlay = false; break;
            case 0:  engine.noteOff (e.pad, false); break;
            case 1:  engine.noteOff (e.pad, true); break;
            case 2:
                if (lazyActive.load())
                {
                    const float pos = engine.getPreviewPositionNow();
                    if (pos >= 0.0f)
                    {
                        auto w = lazyFifo.write (1);
                        w.forEach ([&] (int idx) { lazyHits[(size_t) idx] = pos; });
                    }
                }
                else
                {
                    engine.noteOn (e.pad, e.velocity, false);
                }
                break;
            case 3:  engine.noteOn (e.pad, e.velocity, true); break;
            default: break;
        }
    }
    engine.render (dryBuffer, padBuses, cursor, numSamples - cursor);
    engine.updatePlayPositions();

    if (lazyActive.load() && ! engine.isPreviewBusy())
        lazyActive = false; // preview reached the end of the sample

    const float master = juce::Decibels::decibelsToGain (pMaster->load(), -59.9f);
    const int outChannels = juce::jmin (2, buffer.getNumChannels());
    for (int ch = 0; ch < outChannels; ++ch)
        buffer.addFrom (ch, 0, dryBuffer, ch, 0, numSamples, master);

    // per-pad effect chains: only pads that have effects switched on, and only while
    // they play or their delay / reverb tail is still ringing
    for (int i = 0; i < rz::numPads; ++i)
    {
        const auto& f = engine.getPad (i).effects;
        auto& chain = padFx[(size_t) i];

        if (! f.anyOn())
        {
            if (padFxWasRunning[(size_t) i]) { chain.reset(); padFxWasRunning[(size_t) i] = false; }
            padFxIdleSamples[(size_t) i] = 1.0e12;
            continue;
        }

        const bool sounding = engine.isPadSounding (i);
        auto& idle = padFxIdleSamples[(size_t) i];
        idle = sounding ? 0.0 : idle + numSamples;
        const double tail = f.hasTail() ? currentSampleRate * 2.5 : currentSampleRate * 0.05;
        if (idle > tail)
        {
            if (padFxWasRunning[(size_t) i]) { chain.reset(); padFxWasRunning[(size_t) i] = false; }
            continue;
        }

        auto& bus = padBuses[(size_t) i];
        chain.process (bus, numSamples, readFxParams (i, sounding));
        padFxWasRunning[(size_t) i] = true;

        if (bus.getMagnitude (0, numSamples) > 1.0e-4f)
            idle = juce::jmin (idle, 0.0); // long feedback delays keep the chain alive

        for (int ch = 0; ch < outChannels; ++ch)
            buffer.addFrom (ch, 0, bus, ch, 0, numSamples, master);
    }
}

//==============================================================================
void RezalicaProcessor::triggerPadFromUi (int pad, float velocity, bool isNoteOn)
{
    auto writer = uiFifo.write (1);
    writer.forEach ([&] (int i) { uiEvents[(size_t) i] = { pad, velocity, isNoteOn }; });
}

rz::SampleData::Ptr RezalicaProcessor::loadSample (const juce::File& file)
{
    for (auto* s : samplePool)
        if (s->file == file)
            return s;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;

    const auto maxLength = (juce::int64) (reader->sampleRate * 60.0 * 10.0); // 10 minutes max
    const int length = (int) juce::jmin (reader->lengthInSamples, maxLength);

    rz::SampleData::Ptr s = new rz::SampleData();
    s->buffer.setSize (2, length);
    s->sampleRate = reader->sampleRate;
    s->file = file;
    s->name = file.getFileNameWithoutExtension();
    reader->read (&s->buffer, 0, length, 0, true, true);
    if (reader->numChannels == 1)
        s->buffer.copyFrom (1, 0, s->buffer, 0, 0, length);

    samplePool.add (s);
    return s;
}

std::vector<float> RezalicaProcessor::findSlicePoints (const rz::SampleData& s, int mode, float sensitivity)
{
    std::vector<float> points;
    const int len = s.buffer.getNumSamples();

    if (mode == chopManual)
        return { 0.0f };
    if (mode == chopOff)
        return {};

    if (mode != chopTransients)
    {
        for (int i = 0; i < mode; ++i)
            points.push_back ((float) i / (float) mode);
        return points;
    }

    // Energy-flux onset detection. Sensitivity 0..1 lowers the threshold and the
    // minimum distance between slices, giving anywhere from a few up to 64 slices.
    const int hop = 512;
    const int frames = len / hop;
    if (frames < 4)
        return { 0.0f };

    std::vector<float> energy ((size_t) frames, 0.0f);
    const float* L = s.buffer.getReadPointer (0);
    const float* R = s.buffer.getReadPointer (1);
    for (int f = 0; f < frames; ++f)
    {
        float sum = 0.0f;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            sum += m * m;
        }
        energy[(size_t) f] = std::sqrt (sum / (float) hop);
    }

    std::vector<float> flux ((size_t) frames, 0.0f);
    float maxFlux = 0.0f;
    for (int f = 1; f < frames; ++f)
    {
        flux[(size_t) f] = juce::jmax (0.0f, energy[(size_t) f] - energy[(size_t) f - 1]);
        maxFlux = juce::jmax (maxFlux, flux[(size_t) f]);
    }
    if (maxFlux <= 0.0f)
        return { 0.0f };

    const float sens = juce::jlimit (0.0f, 1.0f, sensitivity);
    const float threshold = maxFlux * (0.01f + 0.6f * (1.0f - sens) * (1.0f - sens));

    std::vector<std::pair<float, int>> candidates;
    for (int f = 1; f < frames - 1; ++f)
        if (flux[(size_t) f] > threshold && flux[(size_t) f] >= flux[(size_t) f - 1] && flux[(size_t) f] >= flux[(size_t) f + 1])
            candidates.push_back ({ flux[(size_t) f], f });

    std::sort (candidates.begin(), candidates.end(), [] (auto& a, auto& b) { return a.first > b.first; });

    const double minGapSeconds = juce::jmap ((double) sens, 0.12, 0.025);
    const int minGap = juce::jmax (2, (int) (s.sampleRate * minGapSeconds / hop));
    std::vector<int> chosen { 0 };
    for (auto& c : candidates)
    {
        if ((int) chosen.size() >= maxSlices) break;
        bool ok = true;
        for (auto f : chosen)
            if (std::abs (f - c.second) < minGap) { ok = false; break; }
        if (ok) chosen.push_back (c.second);
    }
    std::sort (chosen.begin(), chosen.end());

    for (auto f : chosen)
        points.push_back (juce::jmax (0.0f, (float) (f * hop - hop / 4) / (float) len));

    return points;
}

void RezalicaProcessor::setChopSource (rz::SampleData::Ptr s)
{
    if (s != chopSource)
    {
        previousChopSource = chopSource;
        chopSource = std::move (s);
    }
}

void RezalicaProcessor::rechop()
{
    if (chopSource == nullptr)
    {
        slicePoints.clear();
        return;
    }

    if (chopMode != chopManual)
        slicePoints = findSlicePoints (*chopSource, chopMode, transientSensitivity);
    if (slicePoints.empty())
        slicePoints.push_back (0.0f);
    applySlices();
}

void RezalicaProcessor::applySlices()
{
    // slice k goes to pad k: slices 1-16 -> bank A, 17-32 -> bank B, ...
    const int n = (int) slicePoints.size();

    for (int pad = 0; pad < rz::numPads; ++pad)
    {
        const int k = pad;
        if (k < n)
        {
            auto& ps = engine.getPad (pad);
            ps.start = slicePoints[(size_t) k];
            ps.end = (k + 1 < n) ? slicePoints[(size_t) k + 1] : 1.0f;
            engine.setPadSample (pad, chopSource);
            const juce::ScopedLock sl (missingPathLock);
            missingPaths[(size_t) pad] = {};
        }
        else if (const auto old = engine.getPadSample (pad); old != nullptr && (old == chopSource || old == previousChopSource))
        {
            engine.setPadSample (pad, nullptr); // leftover slice from a previous chop
        }
    }
    previousChopSource = nullptr;
    padsChanged.sendChangeMessage();
}

bool RezalicaProcessor::loadFileToPad (int pad, const juce::File& file)
{
    auto s = loadSample (file);
    if (s == nullptr)
        return false;

    auto& ps = engine.getPad (pad);
    ps.start = 0.0f;
    ps.end = 1.0f;
    engine.setPadSample (pad, s);
    {
        const juce::ScopedLock sl (missingPathLock);
        missingPaths[(size_t) pad] = {};
    }
    padsChanged.sendChangeMessage();
    return true;
}

bool RezalicaProcessor::loadFileForChopping (const juce::File& file)
{
    const int pad = selectedPad.load();
    if (! loadFileToPad (pad, file))
        return false;

    setChopSource (engine.getPadSample (pad));
    chopMode = chopOff;
    slicePoints.clear();
    padsChanged.sendChangeMessage();
    return true;
}

bool RezalicaProcessor::chopFileToPads (const juce::File& file, int mode)
{
    auto s = loadSample (file);
    if (s == nullptr)
        return false;

    setChopSource (s);
    chopMode = mode;
    rechop();
    return true;
}

void RezalicaProcessor::chopPadSample (int pad, int mode)
{
    if (auto s = engine.getPadSample (pad))
    {
        setChopSource (s);
        chopMode = mode;
        rechop();
    }
}

void RezalicaProcessor::setChopMode (int mode)
{
    if (mode == chopManual)
    {
        prepareManualSource(); // removes the old markers: ready for manual chopping
        applySlices();
        return;
    }

    if (mode == chopOff)
    {
        // back to one whole sample (on A1)
        if (auto s = engine.getPadSample (selectedPad.load()))
            setChopSource (s);
        chopMode = chopOff;
        if (chopSource != nullptr)
        {
            slicePoints = { 0.0f };
            applySlices();
        }
        slicePoints.clear();
        return;
    }

    chopMode = mode;
    if (auto s = engine.getPadSample (selectedPad.load()))
        setChopSource (s);
    rechop();
}

void RezalicaProcessor::prepareManualSource()
{
    auto s = engine.getPadSample (selectedPad.load());
    if (s != nullptr && s != chopSource)
    {
        setChopSource (s);
        slicePoints = { 0.0f };
    }

    // coming from another mode (transients, 16 slices ...): its markers are removed
    if (chopMode != chopManual || slicePoints.empty())
        slicePoints = { 0.0f };

    chopMode = chopManual;
}

float RezalicaProcessor::snapToZeroCrossing (float n) const
{
    if (chopSource == nullptr)
        return n;

    const auto& b = chopSource->buffer;
    const int len = b.getNumSamples();
    const int centre = juce::jlimit (0, len - 1, (int) (n * (float) len));
    const float* L = b.getReadPointer (0);
    const float* R = b.getReadPointer (1);
    auto mono = [&] (int i) { return L[i] + R[i]; };

    for (int d = 0; d < 256; ++d)
        for (int i : { centre - d, centre + d })
            if (i > 0 && i < len && (mono (i - 1) <= 0.0f) != (mono (i) <= 0.0f))
                return (float) i / (float) len;
    return n;
}

void RezalicaProcessor::addSlicePoint (float n)
{
    prepareManualSource();
    if (chopSource == nullptr || (int) slicePoints.size() >= maxSlices)
        return;

    n = snapToZeroCrossing (juce::jlimit (0.0f, 0.999f, n));
    for (auto p : slicePoints)
        if (std::abs (p - n) < 0.002f)
            return; // already a marker here

    slicePoints.insert (std::upper_bound (slicePoints.begin(), slicePoints.end(), n), n);
    applySlices();
}

void RezalicaProcessor::removeSlicePoint (int index)
{
    // works on any markers (transient, equal or manual): the others stay, mode becomes Manual
    if (chopSource == nullptr || index < 0 || index >= (int) slicePoints.size() || slicePoints.size() <= 1)
        return;
    chopMode = chopManual;
    slicePoints.erase (slicePoints.begin() + index);
    applySlices();
}

int RezalicaProcessor::moveSlicePoint (int index, float n)
{
    if (chopSource == nullptr || index < 0 || index >= (int) slicePoints.size())
        return -1;

    chopMode = chopManual;
    const float gap = 64.0f / (float) juce::jmax (64, chopSource->buffer.getNumSamples());
    const float lo = index > 0 ? slicePoints[(size_t) index - 1] + gap : 0.0f;
    const float hi = index + 1 < (int) slicePoints.size() ? slicePoints[(size_t) index + 1] - gap : 1.0f - gap;
    slicePoints[(size_t) index] = juce::jlimit (lo, juce::jmax (lo, hi), n);
    applySlices();
    return index;
}

void RezalicaProcessor::clearSlicePoints()
{
    prepareManualSource();
    slicePoints = { 0.0f };
    applySlices();
}

bool RezalicaProcessor::startLazyChop()
{
    auto s = engine.getPadSample (selectedPad.load());
    if (s == nullptr)
        s = chopSource;
    if (s == nullptr)
        return false;

    setChopSource (s);
    chopMode = chopManual;
    slicePoints.clear();
    applySlices();      // pads fill up as you hit them
    slicePoints.clear();

    lazyFifo.reset();
    engine.setPreviewSample (s);
    engine.startPreview();
    lazyStarting = true; // until the audio thread has started the preview
    lazyActive = true;
    return true;
}

void RezalicaProcessor::stopLazyChop()
{
    lazyActive = false;
    lazyStarting = false;
    engine.stopPreview();
    updateLazyChop();
}

void RezalicaProcessor::updateLazyChop()
{
    if (lazyStarting && engine.previewPosition.load() >= 0.0f)
        lazyStarting = false;
    if (lazyStarting && ! lazyActive.load())
        lazyStarting = false;

    bool changed = false;
    auto r = lazyFifo.read (lazyFifo.getNumReady());
    r.forEach ([&] (int i)
    {
        const float n = snapToZeroCrossing (lazyHits[(size_t) i]);
        if ((int) slicePoints.size() >= maxSlices)
            return;
        for (auto p : slicePoints)
            if (std::abs (p - n) < 0.002f)
                return;
        slicePoints.insert (std::upper_bound (slicePoints.begin(), slicePoints.end(), n), n);
        changed = true;
    });

    if (changed)
        applySlices();
}

void RezalicaProcessor::setTransientSensitivity (float sensitivity01)
{
    transientSensitivity = juce::jlimit (0.0f, 1.0f, sensitivity01);
    if (chopMode == chopTransients)
        rechop();
}

juce::File RezalicaProcessor::getTrimFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rezalica").getChildFile ("Trimmed");
}

bool RezalicaProcessor::trimPad (int pad)
{
    auto src = engine.getPadSample (pad);
    if (src == nullptr)
        return false;

    auto& ps = engine.getPad (pad);
    const int len = src->buffer.getNumSamples();
    float st = ps.start.load(), en = ps.end.load();
    if (en < st) std::swap (st, en);
    const int a = juce::jlimit (0, len - 1, (int) (st * (float) len));
    const int b = juce::jlimit (a + 1, len, (int) (en * (float) len));
    if (b - a < 16 || (a == 0 && b == len))
        return false; // nothing to trim

    rz::SampleData::Ptr s = new rz::SampleData();
    s->buffer.setSize (2, b - a);
    for (int ch = 0; ch < 2; ++ch)
        s->buffer.copyFrom (ch, 0, src->buffer, ch, a, b - a);
    s->sampleRate = src->sampleRate;
    s->name = src->name + " trim";

    // keep it on disk so the project can find it again
    auto folder = getTrimFolder();
    folder.createDirectory();
    auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (s->name), ".wav", false);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
        if (out != nullptr)
            if (std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), s->sampleRate, 2, 24, {}, 0)); w != nullptr)
            {
                out.release(); // the writer owns the stream now
                w->writeFromAudioSampleBuffer (s->buffer, 0, s->buffer.getNumSamples());
            }
    }
    if (file.existsAsFile())
        s->file = file;

    samplePool.add (s);
    ps.start = 0.0f;
    ps.end = 1.0f;
    engine.setPadSample (pad, s);
    {
        const juce::ScopedLock sl (missingPathLock);
        missingPaths[(size_t) pad] = {};
    }
    padsChanged.sendChangeMessage();
    return true;
}

void RezalicaProcessor::clearPad (int pad)
{
    const bool wasChopSource = chopSource != nullptr && engine.getPadSample (pad) == chopSource;
    engine.setPadSample (pad, nullptr);
    if (wasChopSource)
    {
        bool stillUsed = false;
        for (int i = 0; i < rz::numPads; ++i)
            stillUsed = stillUsed || engine.getPadSample (i) == chopSource;
        if (! stillUsed) { chopSource = nullptr; slicePoints.clear(); }
    }
    engine.getPad (pad).resetPlayback();
    engine.getPad (pad).start = 0.0f;
    engine.getPad (pad).end = 1.0f;
    {
        const juce::ScopedLock sl (missingPathLock);
        missingPaths[(size_t) pad] = {};
    }
    padsChanged.sendChangeMessage();
}

juce::String RezalicaProcessor::getPadName (int pad) const
{
    if (auto s = engine.getPadSample (pad))
        return s->name;

    const juce::ScopedLock sl (missingPathLock);
    if (missingPaths[(size_t) pad].isNotEmpty())
        return "(missing) " + juce::File (missingPaths[(size_t) pad]).getFileNameWithoutExtension();
    return {};
}

void RezalicaProcessor::timerCallback()
{
    // Free samples nobody uses anymore (only the pool holds a reference).
    for (int i = samplePool.size(); --i >= 0;)
        if (samplePool.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            samplePool.remove (i);
}

//==============================================================================
void RezalicaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("REZALICA");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("selectedPad", selectedPad.load(), nullptr);
    root.setProperty ("bank", currentBank.load(), nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree pads ("PADS");
    for (int i = 0; i < rz::numPads; ++i)
    {
        auto& ps = engine.getPad (i);
        juce::ValueTree p ("PAD");
        p.setProperty ("index", i, nullptr);

        juce::String path;
        if (auto s = engine.getPadSample (i))
            path = s->file.getFullPathName();
        else
        {
            const juce::ScopedLock sl (missingPathLock);
            path = missingPaths[(size_t) i];
        }

        p.setProperty ("file", path, nullptr);
        p.setProperty ("start", ps.start.load(), nullptr);
        p.setProperty ("end", ps.end.load(), nullptr);
        p.setProperty ("gain", ps.gainDb.load(), nullptr);
        p.setProperty ("pan", ps.pan.load(), nullptr);
        p.setProperty ("pitch", ps.pitch.load(), nullptr);
        p.setProperty ("attack", ps.attackMs.load(), nullptr);
        p.setProperty ("release", ps.releaseMs.load(), nullptr);
        p.setProperty ("loop", ps.loop.load(), nullptr);
        p.setProperty ("reverse", ps.reverse.load(), nullptr);
        p.setProperty ("gate", ps.gate.load(), nullptr);
        p.appendChild (ps.effects.toValueTree(), nullptr);
        p.setProperty ("mute", ps.mute.load(), nullptr);
        pads.appendChild (p, nullptr);
    }
    root.appendChild (pads, nullptr);

    juce::ValueTree seq ("SEQ");
    seq.setProperty ("pattern", sequencer.currentPattern.load(), nullptr);
    seq.setProperty ("sync", sequencer.syncToHost.load(), nullptr);
    seq.setProperty ("bpm", sequencer.bpm.load(), nullptr);
    seq.setProperty ("swing", sequencer.swing.load(), nullptr);
    for (int i = 0; i < rz::Sequencer::numPatterns; ++i)
    {
        juce::ValueTree pat ("PATTERN");
        pat.setProperty ("index", i, nullptr);
        pat.setProperty ("length", sequencer.length[(size_t) i].load(), nullptr);
        pat.setProperty ("steps", sequencer.getPatternData (i).toBase64Encoding(), nullptr);
        seq.appendChild (pat, nullptr);
    }
    root.appendChild (seq, nullptr);

    juce::ValueTree chop ("CHOP");
    chop.setProperty ("mode", chopMode, nullptr);
    chop.setProperty ("sensitivity", transientSensitivity, nullptr);
    {
        juce::StringArray pts;
        for (auto p : slicePoints) pts.add (juce::String (p, 6));
        chop.setProperty ("points", pts.joinIntoString (","), nullptr);
    }
    chop.setProperty ("source", chopSource != nullptr ? chopSource->file.getFullPathName() : juce::String(), nullptr);
    root.appendChild (chop, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void RezalicaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("REZALICA"))
        return;

    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    selectedPad = juce::jlimit (0, rz::numPads - 1, (int) root.getProperty ("selectedPad", 0));
    currentBank = juce::jlimit (0, rz::numBanks - 1, (int) root.getProperty ("bank", 0));

    auto pads = root.getChildWithName ("PADS");
    for (auto p : pads)
    {
        const int i = p.getProperty ("index", -1);
        if (i < 0 || i >= rz::numPads)
            continue;

        auto& ps = engine.getPad (i);
        ps.start = (float) p.getProperty ("start", 0.0f);
        ps.end = (float) p.getProperty ("end", 1.0f);
        ps.gainDb = (float) p.getProperty ("gain", 0.0f);
        ps.pan = (float) p.getProperty ("pan", 0.0f);
        ps.pitch = (float) p.getProperty ("pitch", 0.0f);
        ps.attackMs = (float) p.getProperty ("attack", 1.0f);
        ps.releaseMs = (float) p.getProperty ("release", 30.0f);
        ps.loop = (bool) p.getProperty ("loop", false);
        ps.reverse = (bool) p.getProperty ("reverse", false);
        ps.gate = (bool) p.getProperty ("gate", false);
        ps.effects.fromValueTree (p.getChildWithName ("FX"));
        ps.mute = (bool) p.getProperty ("mute", false);

        const juce::String path = p.getProperty ("file").toString();
        rz::SampleData::Ptr s;
        if (path.isNotEmpty())
        {
            juce::File f (path);
            if (f.existsAsFile())
                s = loadSample (f);
        }
        engine.setPadSample (i, s);

        const juce::ScopedLock sl (missingPathLock);
        missingPaths[(size_t) i] = (s == nullptr) ? path : juce::String();
    }

    auto seq = root.getChildWithName ("SEQ");
    if (seq.isValid())
    {
        sequencer.currentPattern = juce::jlimit (0, rz::Sequencer::numPatterns - 1, (int) seq.getProperty ("pattern", 0));
        sequencer.syncToHost = (bool) seq.getProperty ("sync", true);
        sequencer.bpm = (float) seq.getProperty ("bpm", 90.0f);
        sequencer.swing = (float) seq.getProperty ("swing", 0.0f);

        for (auto pat : seq)
        {
            const int i = pat.getProperty ("index", -1);
            if (i < 0 || i >= rz::Sequencer::numPatterns)
                continue;
            sequencer.length[(size_t) i] = juce::jlimit (1, rz::Sequencer::maxSteps, (int) pat.getProperty ("length", 16));
            juce::MemoryBlock mb;
            mb.fromBase64Encoding (pat.getProperty ("steps").toString());
            sequencer.setPatternData (i, mb);
        }
    }

    auto chop = root.getChildWithName ("CHOP");
    chopSource = nullptr;
    slicePoints.clear();
    if (chop.isValid())
    {
        chopMode = (int) chop.getProperty ("mode", (int) chop16);
        transientSensitivity = (float) chop.getProperty ("sensitivity", 0.5f);
        juce::File src (chop.getProperty ("source").toString());
        if (src.existsAsFile())
        {
            chopSource = loadSample (src);
            if (chopSource != nullptr) // restore slice list without touching the (possibly edited) pads
            {
                if (chopMode == chopManual)
                {
                    for (auto& t : juce::StringArray::fromTokens (chop.getProperty ("points").toString(), ",", {}))
                        if (t.isNotEmpty()) slicePoints.push_back (t.getFloatValue());
                    std::sort (slicePoints.begin(), slicePoints.end());
                }
                else
                {
                    slicePoints = findSlicePoints (*chopSource, chopMode, transientSensitivity);
                }
            }
        }
    }

    padsChanged.sendChangeMessage();
}

//==============================================================================
juce::AudioProcessorEditor* RezalicaProcessor::createEditor()
{
    return new RezalicaEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RezalicaProcessor();
}
