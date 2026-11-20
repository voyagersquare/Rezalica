#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TimeStretch.h"
#include <algorithm>
#include <map>
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
    aliveToken.reset();                        // late background results are dropped
    stretchWorker.removeAllJobs (true, 5000);
    fileWriter.removeAllJobs (false, 10000); // let pending WAV writes finish
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

void RezalicaProcessor::addTaggedEvent (int offset, int type, int pad, float velocity, float transpose, int tag, bool flag)
{
    if (numEvents < (int) events.size())
        events[(size_t) numEvents++] = { offset, type, pad, velocity, transpose, tag, flag };
}

bool RezalicaProcessor::collectRecordedPianoNotes()
{
    auto notes = sequencer.takeRecordedNotes();
    if (notes.empty())
        return false;

    std::array<rz::NoteClip::Ptr, rz::Sequencer::numPatterns> changed;
    for (auto& r : notes)
    {
        auto& c = changed[(size_t) r.pattern];
        if (c == nullptr)
            c = sequencer.getClip (r.pattern)->copy();
        rz::PianoNote n;
        n.id = sequencer.newNoteId();
        n.start = r.start;
        n.length = r.length;
        n.row = r.row;
        n.velocity = r.velocity;
        n.kind = r.kind;
        c->notes.push_back (n);
    }
    for (int i = 0; i < rz::Sequencer::numPatterns; ++i)
        if (changed[(size_t) i] != nullptr)
            sequencer.setClip (i, changed[(size_t) i]);
    return true;
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
        if (p < numSamples) { addEvent ((int) p, evOffSeq, pad, 0.0f); p = -1.0; }
    }

    const bool wasRunning = sequencer.isRunning();
    sequencer.process (position, numSamples,
        [&] (int pad, float vel, int offset)                       // step sequencer hit
        {
            addEvent (offset, evOnSeq, pad, vel);
            pendingSeqOff[(size_t) pad] = offset + sequencer.getSamplesPerStep() * 0.98;
        },
        [&] (int mode, int row, float vel, int offset, int tag, int targetPad)   // piano roll note on
        {
            if (mode == rz::NoteClip::melodic)
                addTaggedEvent (offset, evOnTag, targetPad, vel, (float) (row - 60), tag, true);
            else if (row >= 0 && row < rz::numPads)
                addTaggedEvent (offset, evOnTag, row, vel, 0.0f, tag, false);
        },
        [&] (int mode, int, int offset, int tag, int)              // piano roll note off
        {
            addTaggedEvent (offset, evOffTag, -1, 0.0f, 0.0f, tag, mode == rz::NoteClip::melodic);
        });

    if (wasRunning && ! sequencer.isRunning())
    {
        for (int pad = 0; pad < rz::numPads; ++pad)
            if (pendingSeqOff[(size_t) pad] >= 0.0) { addEvent (0, evOffSeq, pad, 0.0f); pendingSeqOff[(size_t) pad] = -1.0; }
    }

    for (int pad = 0; pad < rz::numPads; ++pad)
    {
        auto& p = pendingSeqOff[(size_t) pad];
        if (p >= 0.0 && p < numSamples) { addEvent ((int) p, evOffSeq, pad, 0.0f); p = -1.0; }
        else if (p >= numSamples) p -= numSamples;
    }

    // ---- UI (mouse / computer keyboard) ----
    {
        auto reader = uiFifo.read (uiFifo.getNumReady());
        reader.forEach ([&] (int i)
        {
            const auto& e = uiEvents[(size_t) i];
            if (e.pad < 0) { addEvent (0, evStopAll, -1, 0.0f); return; }
            if (e.preview)
            {
                // piano roll keyboard / note preview: melodic, tagged so it can be released
                const int tag = (1 << 29) + juce::roundToInt (e.transpose) + 128;
                if (e.on) addTaggedEvent (0, evOnTag, e.pad, e.velocity, e.transpose, tag, true);
                else      addTaggedEvent (0, evOffTag, -1, 0.0f, 0.0f, tag, true);
                return;
            }
            addEvent (0, e.on ? evOn : evOff, e.pad, e.velocity);
            if (e.on)
            {
                sequencer.recordHit (e.pad, e.velocity, 0);
                sequencer.recordPianoNoteOn (e.pad, e.velocity, 0, false);
            }
            else
            {
                sequencer.recordPianoNoteOff (e.pad, 0, false);
            }
        });
    }

    // ---- MIDI ----
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int offset = juce::jlimit (0, numSamples - 1, meta.samplePosition);

        const int chromatic = chromaticPad.load();

        if (m.isNoteOn() && chromatic >= 0 && chromatic < rz::numPads)
        {
            // melodic: every key plays the chosen pad, C3 (60) = original pitch
            const int note = m.getNoteNumber();
            addTaggedEvent (offset, evOnTag, chromatic, m.getFloatVelocity(), (float) (note - 60), rz::Sequencer::midiTagBase + note, true);
            sequencer.recordPianoNoteOn (note, m.getFloatVelocity(), offset, true);
        }
        else if (m.isNoteOff() && chromatic >= 0 && chromatic < rz::numPads)
        {
            addTaggedEvent (offset, evOffTag, -1, 0.0f, 0.0f, rz::Sequencer::midiTagBase + m.getNoteNumber(), true);
            sequencer.recordPianoNoteOff (m.getNoteNumber(), offset, true);
        }
        else if (m.isNoteOn())
        {
            const int pad = m.getNoteNumber() - midiBaseNote;
            if (pad >= 0 && pad < rz::numPads)
            {
                addEvent (offset, evOn, pad, m.getFloatVelocity());
                sequencer.recordHit (pad, m.getFloatVelocity(), offset);
                sequencer.recordPianoNoteOn (pad, m.getFloatVelocity(), offset, false);
            }
        }
        else if (m.isNoteOff())
        {
            const int pad = m.getNoteNumber() - midiBaseNote;
            if (pad >= 0 && pad < rz::numPads)
            {
                addEvent (offset, evOff, pad, 0.0f);
                sequencer.recordPianoNoteOff (pad, offset, false);
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            addEvent (offset, evStopAll, -1, 0.0f);
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
            case evStopAll: engine.allNotesOff(); sequencer.internalPlay = false; break;
            case evOff:     engine.noteOff (e.pad, false); break;
            case evOffSeq:  engine.noteOff (e.pad, true); break;
            case evOffTag:  engine.noteOffTag (e.tag, e.flag); break;
            case evOnTag:
                if (lazyActive.load() && e.tag >= rz::Sequencer::midiTagBase)
                    break;
                engine.noteOn (e.pad, e.velocity, e.tag < rz::Sequencer::midiTagBase, e.transpose, e.tag, e.flag);
                break;
            case evOn:
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
            case evOnSeq:   engine.noteOn (e.pad, e.velocity, true); break;
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

void RezalicaProcessor::previewNoteFromUi (int pad, float velocity, float transpose, bool isNoteOn)
{
    auto writer = uiFifo.write (1);
    writer.forEach ([&] (int i) { uiEvents[(size_t) i] = { pad, velocity, isNoteOn, true, transpose }; });
}

rz::SampleData::Ptr RezalicaProcessor::loadSample (const juce::File& file, bool shareIfLoaded)
{
    // every load gets its own sample object, so pads with the same file stay independent;
    // only old projects (without sample ids) share by file
    if (shareIfLoaded)
        for (auto* s : samplePool)
            if (s->file == file && ! s->isWarped())
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

//==============================================================================
juce::File RezalicaProcessor::getWarpFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rezalica").getChildFile ("Warped");
}

bool RezalicaProcessor::saveProjectToFile (const juce::File& file)
{
    juce::MemoryBlock state;
    getStateInformation (state);
    auto xml = getXmlFromBinary (state.getData(), (int) state.getSize());
    if (xml == nullptr)
        return false;
    file.getParentDirectory().createDirectory();
    if (! xml->writeTo (file))
        return false;
    currentProjectFile = file;
    return true;
}

bool RezalicaProcessor::loadProjectFromFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("REZALICA"))
        return false;
    juce::MemoryBlock state;
    copyXmlToBinary (*xml, state);
    stopAll();
    setStateInformation (state.getData(), (int) state.getSize());
    undoStack.clear();
    redoStack.clear();
    currentProjectFile = file;
    return true;
}

void RezalicaProcessor::newProject()
{
    auto fresh = std::make_unique<RezalicaProcessor>();
    juce::MemoryBlock state;
    fresh->getStateInformation (state);
    stopAll();
    setStateInformation (state.getData(), (int) state.getSize());
    undoStack.clear();
    redoStack.clear();
    currentProjectFile = juce::File();
}

juce::File RezalicaProcessor::getExportFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rezalica").getChildFile ("Export");
}

juce::File RezalicaProcessor::exportPatternMidi (int pattern, bool stepPart, bool pianoPart)
{
    const double bpm = juce::jmax (20.0f, sequencer.effectiveBpm.load());
    const int len = juce::jlimit (1, rz::Sequencer::maxSteps, sequencer.length[(size_t) pattern].load());
    const double ticksPerStep = 240.0; // 960 per quarter note
    const double swingSteps = juce::jlimit (0.0f, 1.0f, sequencer.swing.load()) * 0.5;

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    auto addNote = [&] (int note, double startSteps, double lengthSteps, int velocity)
    {
        if (note < 0 || note > 127) return;
        const double on = juce::jmax (0.0, startSteps) * ticksPerStep;
        const double off = juce::jmin ((double) len, juce::jmax (0.0, startSteps) + lengthSteps) * ticksPerStep;
        track.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, velocity)), on);
        track.addEvent (juce::MidiMessage::noteOff (1, note), juce::jmax (on + 1.0, off));
    };

    if (stepPart)
        for (int pad = 0; pad < rz::numPads; ++pad)
            for (int st = 0; st < len; ++st)
                if (const int v = sequencer.getStep (pattern, pad, st); v > 0)
                    addNote (midiBaseNote + pad, st + ((st & 1) != 0 ? swingSteps : 0.0) + sequencer.getNudge (pattern, pad, st) * 0.01, 0.9, v);

    if (pianoPart)
        for (auto& n : sequencer.getClip (pattern)->notes)
            if (n.start < (float) len)
                addNote (n.kind == rz::NoteClip::melodic ? n.row : midiBaseNote + n.row, n.start, n.length, n.velocity);

    track.addEvent (juce::MidiMessage::endOfTrack(), len * ticksPerStep);
    track.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (960);
    mf.addTrack (track);

    auto folder = getExportFolder();
    folder.createDirectory();
    const juce::String part = stepPart && pianoPart ? "" : stepPart ? " steps" : " piano roll";
    auto file = folder.getNonexistentChildFile ("Rezalica Pattern " + juce::String (pattern + 1) + part, ".mid", false);
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! mf.writeTo (out))
        return {};
    out.flush();
    return file;
}

juce::File RezalicaProcessor::exportPatternWav (int pattern, bool stepPart, bool pianoPart)
{
    // render offline in a second, private instance with the same project
    const double bpm = juce::jmax (20.0f, sequencer.effectiveBpm.load());
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const int len = juce::jlimit (1, rz::Sequencer::maxSteps, sequencer.length[(size_t) pattern].load());
    const int loopSamples = (int) std::llround (len * 0.25 * 60.0 / bpm * sr);

    juce::MemoryBlock state;
    getStateInformation (state);
    auto offlinePtr = std::make_unique<RezalicaProcessor>(); // on the heap: the processor is large
    auto& offline = *offlinePtr;
    offline.setStateInformation (state.getData(), (int) state.getSize());

    auto& seq = offline.getSequencer();
    if (! stepPart)
        seq.clearPattern (pattern);
    if (! pianoPart)
    {
        auto empty = seq.getClip (pattern)->copy();
        empty->notes.clear();
        seq.setClip (pattern, empty);
    }
    seq.currentPattern = pattern;
    seq.syncToHost = false;
    seq.bpm = (float) bpm;

    const int block = 512;
    offline.setRateAndBufferSizeDetails (sr, block);
    offline.prepareToPlay (sr, block);
    seq.internalPlay = true;

    // two passes: the second one includes the tails (reverb, delay ...) that ring over from the first,
    // so the file loops seamlessly
    juce::AudioBuffer<float> out (2, loopSamples);
    out.clear();
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer noMidi;
    for (int pos = 0; pos < 2 * loopSamples; pos += block)
    {
        const int n = juce::jmin (block, 2 * loopSamples - pos);
        juce::AudioBuffer<float> sub (buf.getArrayOfWritePointers(), 2, n);
        offline.processBlock (sub, noMidi);
        noMidi.clear();
        const int dst = pos - loopSamples;
        for (int i = 0; i < n; ++i)
            if (dst + i >= 0 && dst + i < loopSamples)
                for (int ch = 0; ch < 2; ++ch)
                    out.setSample (ch, dst + i, sub.getSample (ch, i));
    }

    auto folder = getExportFolder();
    folder.createDirectory();
    const juce::String part = stepPart && pianoPart ? "" : stepPart ? " steps" : " piano roll";
    auto file = folder.getNonexistentChildFile ("Rezalica Pattern " + juce::String (pattern + 1) + part
                                                + " " + juce::String (juce::roundToInt (bpm)) + " BPM", ".wav", false);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os == nullptr)
        return {};
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w == nullptr)
        return {};
    os.release();
    w->writeFromAudioSampleBuffer (out, 0, loopSamples);
    w.reset();
    return file;
}

bool RezalicaProcessor::exportSongMidi (const juce::File& file)
{
    std::vector<rz::Sequencer::SongSegment> segs;
    double total = 0.0;
    const int n = sequencer.buildSong (segs, total);
    if (n == 0)
        return false;

    const double bpm = juce::jmax (20.0f, sequencer.effectiveBpm.load());
    const double ticksPerStep = 240.0;
    const double swingSteps = juce::jlimit (0.0f, 1.0f, sequencer.swing.load()) * 0.5;

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    for (int si = 0; si < n; ++si)
    {
        const auto& sg = segs[(size_t) si];
        if (sg.pattern < 0) continue;
        const double segSteps = sg.start / rz::Sequencer::stepLengthPpq;
        const int len = juce::jlimit (1, rz::Sequencer::maxSteps, sequencer.length[(size_t) sg.pattern].load());
        auto addNote = [&] (int note, double startSteps, double lengthSteps, int velocity)
        {
            if (note < 0 || note > 127) return;
            const double st = juce::jlimit (0.0, (double) len, startSteps);
            const double on = (segSteps + st) * ticksPerStep;
            const double off = (segSteps + juce::jmin ((double) len, st + lengthSteps)) * ticksPerStep;
            track.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, velocity)), on);
            track.addEvent (juce::MidiMessage::noteOff (1, note), juce::jmax (on + 1.0, off));
        };
        for (int pad = 0; pad < rz::numPads; ++pad)
            for (int stp = 0; stp < len; ++stp)
                if (const int v = sequencer.getStep (sg.pattern, pad, stp); v > 0)
                    addNote (midiBaseNote + pad, stp + ((stp & 1) != 0 ? swingSteps : 0.0) + sequencer.getNudge (sg.pattern, pad, stp) * 0.01, 0.9, v);
        for (auto& note : sequencer.getClip (sg.pattern)->notes)
            if (note.start < (float) len)
                addNote (note.kind == rz::NoteClip::melodic ? note.row : midiBaseNote + note.row, note.start, note.length, note.velocity);
    }

    track.addEvent (juce::MidiMessage::endOfTrack(), total / rz::Sequencer::stepLengthPpq * ticksPerStep);
    track.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (960);
    mf.addTrack (track);
    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && mf.writeTo (out);
}

bool RezalicaProcessor::exportSongWav (const juce::File& file)
{
    std::vector<rz::Sequencer::SongSegment> segs;
    double total = 0.0;
    if (sequencer.buildSong (segs, total) == 0)
        return false;

    const double bpm = juce::jmax (20.0f, sequencer.effectiveBpm.load());
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const int songSamples = (int) std::llround (total * 60.0 / bpm * sr);
    const int tailSamples = (int) (sr * 3.0); // let reverb / delay ring out at the end

    juce::MemoryBlock state;
    getStateInformation (state);
    auto offlinePtr = std::make_unique<RezalicaProcessor>(); // on the heap: the processor is large
    auto& offline = *offlinePtr;
    offline.setStateInformation (state.getData(), (int) state.getSize());
    auto& seq = offline.getSequencer();
    seq.songMode = true;
    seq.songLoop = false;
    seq.syncToHost = false;
    seq.bpm = (float) bpm;

    const int block = 512;
    offline.setRateAndBufferSizeDetails (sr, block);
    offline.prepareToPlay (sr, block);
    seq.internalPlay = true;

    const int totalSamples = songSamples + tailSamples;
    juce::AudioBuffer<float> out (2, totalSamples);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer noMidi;
    for (int pos = 0; pos < totalSamples; pos += block)
    {
        const int n = juce::jmin (block, totalSamples - pos);
        juce::AudioBuffer<float> sub (buf.getArrayOfWritePointers(), 2, n);
        offline.processBlock (sub, noMidi);
        noMidi.clear();
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, sub, ch, 0, n);
    }

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os == nullptr)
        return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w == nullptr)
        return false;
    os.release();
    return w->writeFromAudioSampleBuffer (out, 0, totalSamples);
}

juce::File RezalicaProcessor::getRenderFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rezalica").getChildFile ("Rendered");
}

bool RezalicaProcessor::consolidatePad (int pad)
{
    auto warped = engine.getPadSample (pad);
    if (warped == nullptr || ! warped->isWarped())
        return false;

    // a plain sample with the stretched audio: from now on it is just a normal file
    rz::SampleData::Ptr s = new rz::SampleData();
    s->buffer = warped->buffer;
    s->sampleRate = warped->sampleRate;
    s->name = warped->name.replace ("[", "").replace ("]", "") + " render";

    auto folder = getRenderFolder();
    folder.createDirectory();
    auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (s->name), ".wav", false);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
        if (out == nullptr)
            return false;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), s->sampleRate, 2, 24, {}, 0));
        if (w == nullptr)
            return false;
        out.release();
        w->writeFromAudioSampleBuffer (s->buffer, 0, s->buffer.getNumSamples());
    }
    if (! file.existsAsFile())
        return false;
    s->file = file;
    samplePool.add (s);

    ++warpGeneration;                // a pending re-stretch must not bring the old version back
    replaceSampleOnPads (warped, s); // same audio, so every slice / marker stays where it is
    for (int i = 0; i < rz::numPads; ++i)
        if (engine.getPadSample (i) == s)
        {
            const juce::ScopedLock sl (missingPathLock);
            missingPaths[(size_t) i] = {};
        }
    padsChanged.sendChangeMessage();
    return true;
}

double RezalicaProcessor::getWarpTempo (const rz::WarpSettings& s) const
{
    return juce::jlimit (20.0, 400.0, (double) (s.syncHost ? sequencer.effectiveBpm.load() : s.bpm));
}

bool RezalicaProcessor::isPadWarped (int pad) const
{
    auto s = engine.getPadSample (pad);
    return s != nullptr && s->isWarped();
}

rz::WarpSettings RezalicaProcessor::getWarpSettings (int pad) const
{
    if (auto s = engine.getPadSample (pad); s != nullptr && s->isWarped())
        return s->warp;
    auto w = lastWarpSettings;
    w.enabled = false;
    return w;
}

void RezalicaProcessor::saveInBackground (const rz::SampleData::Ptr& s)
{
    auto folder = getWarpFolder();
    folder.createDirectory();
    auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (s->name), ".wav", false);
    file.create(); // reserve the name
    s->renderedFile = file;

    rz::SampleData::Ptr keep = s;
    fileWriter.addJob ([keep, file]
    {
        auto tmp = file.withFileExtension (".tmp");
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out (tmp.createOutputStream());
            if (out == nullptr)
                return;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), keep->sampleRate, 2, 24, {}, 0));
            if (w == nullptr)
                return;
            out.release();
            w->writeFromAudioSampleBuffer (keep->buffer, 0, keep->buffer.getNumSamples());
        }
        tmp.moveFileTo (file); // only complete files get the final name
    });
}

juce::AudioBuffer<float> RezalicaProcessor::computeStretch (const rz::SampleData& source, float regionStart, float regionEnd,
                                                           const rz::WarpSettings& settings, double bpm)
{
    const int len = source.buffer.getNumSamples();
    const int a = juce::jlimit (0, len - 1, (int) (regionStart * (float) len));
    const int b = juce::jlimit (a + 2, len, (int) (regionEnd * (float) len));

    juce::AudioBuffer<float> region (2, b - a);
    for (int ch = 0; ch < 2; ++ch)
        region.copyFrom (ch, 0, source.buffer, ch, a, b - a);

    const double seconds = settings.lengthBeats() * 60.0 / bpm;
    const int outLen = juce::jlimit (16, (int) (source.sampleRate * 60.0 * 10.0), (int) std::llround (seconds * source.sampleRate));

    // warp markers: piecewise time map between the region (input) and the stretched result (output)
    const double inLen = region.getNumSamples();
    const double outLenD = outLen;
    const auto ws = settings;
    rz::stretch::TimeMap outForIn, inForOut;
    if (! ws.markers.empty())
    {
        outForIn = [ws, inLen, outLenD] (double i) { return (double) ws.srcToDst ((float) (i / inLen)) * outLenD; };
        inForOut = [ws, inLen, outLenD] (double o) { return (double) ws.dstToSrc ((float) (o / outLenD)) * inLen; };
    }

    return settings.mode == rz::WarpSettings::complex
         ? rz::stretch::complex (region, source.sampleRate, outLen, inForOut)
         : rz::stretch::beats (region, source.sampleRate, outLen, settings.preserve, settings.loopMode,
                               settings.envelope, settings.lengthBeats(), outForIn);
}

rz::SampleData::Ptr RezalicaProcessor::renderWarp (const rz::SampleData::Ptr& source, float regionStart, float regionEnd,
                                                   const rz::WarpSettings& settings, double bpm, bool background,
                                                   const juce::AudioBuffer<float>* precomputed)
{
    rz::SampleData::Ptr s = new rz::SampleData();
    s->buffer = precomputed != nullptr ? *precomputed : computeStretch (*source, regionStart, regionEnd, settings, bpm);
    s->sampleRate = source->sampleRate;
    s->name = source->name + " [" + rz::WarpSettings::lengthNames()[settings.lengthIndex] + "]";
    s->file = source->file;
    s->warpSource = source;
    s->regionStart = regionStart;
    s->regionEnd = regionEnd;
    s->warp = settings;
    s->warp.enabled = true;
    s->renderedBpm = bpm;

    samplePool.add (s);
    if (background)
        saveInBackground (s);
    return s;
}

void RezalicaProcessor::replaceSampleOnPads (const rz::SampleData::Ptr& from, const rz::SampleData::Ptr& to)
{
    for (int i = 0; i < rz::numPads; ++i)
        if (engine.getPadSample (i) == from)
            engine.setPadSample (i, to);
    if (chopSource == from)
        chopSource = to;
}

void RezalicaProcessor::warpRegionFor (int pad, const rz::WarpSettings& settings, rz::SampleData::Ptr& base, float& ra, float& rb) const
{
    auto current = engine.getPadSample (pad);
    base = current;
    const auto& ps = engine.getPad (pad);
    float st = ps.start.load(), en = ps.end.load();
    if (current != nullptr && current->isWarped())
    {
        base = current->warpSource;
        const float span = current->regionEnd - current->regionStart;
        st = current->regionStart + current->warp.dstToSrc (st) * span;
        en = current->regionStart + current->warp.dstToSrc (en) * span;
    }
    ra = settings.wholeSample ? 0.0f : st;
    rb = settings.wholeSample ? 1.0f : en;
}

void RezalicaProcessor::requestWarp (int pad, rz::WarpSettings settings)
{
    if (! settings.enabled)
    {
        ++warpGeneration; // cancels pending results
        setWarp (pad, settings);
        return;
    }

    rz::SampleData::Ptr base;
    float ra, rb;
    warpRegionFor (pad, settings, base, ra, rb);
    if (base == nullptr)
        return;

    const int generation = ++warpGeneration;
    const double bpm = getWarpTempo (settings);
    std::weak_ptr<int> alive = aliveToken;
    ++warpJobs;

    stretchWorker.addJob ([this, alive, base, ra, rb, settings, bpm, pad, generation]
    {
        auto result = std::make_shared<juce::AudioBuffer<float>> (computeStretch (*base, ra, rb, settings, bpm));
        juce::MessageManager::callAsync ([this, alive, base, ra, rb, settings, pad, generation, result]
        {
            if (alive.expired())
                return;
            --warpJobs;
            if (generation != warpGeneration)
                return; // newer settings are on their way

            rz::SampleData::Ptr nowBase;
            float nra, nrb;
            warpRegionFor (pad, settings, nowBase, nra, nrb);
            if (nowBase == base && std::abs (nra - ra) < 1.0e-6f && std::abs (nrb - rb) < 1.0e-6f)
                setWarp (pad, settings, result.get());
            padsChanged.sendChangeMessage();
        });
    });
}

bool RezalicaProcessor::setWarp (int pad, rz::WarpSettings settings, const juce::AudioBuffer<float>* precomputed)
{
    auto current = engine.getPadSample (pad);
    if (current == nullptr)
        return false;

    // the pads that play this very sample object: slices of one chop share it, copies do not
    std::array<bool, rz::numPads> group {};
    for (int i = 0; i < rz::numPads; ++i)
        group[(size_t) i] = engine.getPadSample (i) == current;

    // 1. go back to the unwarped sample (pad positions mapped back into the original)
    rz::SampleData::Ptr base = current;
    if (current->isWarped())
    {
        base = current->warpSource; // (copies made with Paste / Load have their own originals)
        const float ra = current->regionStart, rb = current->regionEnd;
        const auto oldWarp = current->warp;
        auto mapBack = [ra, rb, oldWarp] (float p) { return ra + oldWarp.dstToSrc (p) * (rb - ra); };
        for (int i = 0; i < rz::numPads; ++i)
            if (engine.getPadSample (i) == current)
            {
                auto& ps = engine.getPad (i);
                ps.start = mapBack (ps.start.load());
                ps.end = mapBack (ps.end.load());
                engine.setPadSample (i, base);
            }
        if (chopSource == current)
        {
            chopSource = base;
            for (auto& p : slicePoints) p = mapBack (p);
        }
    }

    if (settings.enabled)
    {
        // 2. stretch the whole sample (all its pads / slices follow) or only this pad's region
        auto& ps = engine.getPad (pad);
        const float ra = settings.wholeSample ? 0.0f : ps.start.load();
        const float rb = settings.wholeSample ? 1.0f : ps.end.load();
        auto warped = renderWarp (base, ra, rb, settings, getWarpTempo (settings), true, precomputed);

        if (settings.wholeSample)
        {
            // slices follow the audio through the warp markers
            for (int i = 0; i < rz::numPads; ++i)
                if (group[(size_t) i] && engine.getPadSample (i) == base)
                {
                    auto& pp = engine.getPad (i);
                    pp.start = settings.srcToDst (pp.start.load());
                    pp.end = settings.srcToDst (pp.end.load());
                }
            if (chopSource == base)
                for (auto& sp : slicePoints) sp = settings.srcToDst (sp);
            for (int i = 0; i < rz::numPads; ++i)
                if (group[(size_t) i] && engine.getPadSample (i) == base)
                    engine.setPadSample (i, warped);
            if (chopSource == base)
                chopSource = warped;
        }
        else
        {
            engine.setPadSample (pad, warped);
            ps.start = 0.0f;
            ps.end = 1.0f;
        }
        lastWarpSettings = settings;
    }

    padsChanged.sendChangeMessage();
    return true;
}

rz::WarpSettings RezalicaProcessor::suggestWarpSettings (int pad) const
{
    auto w = lastWarpSettings;
    w.enabled = true;
    w.wholeSample = true;
    w.markers.clear();
    if (auto s = engine.getPadSample (pad))
    {
        auto base = s->isWarped() ? s->warpSource : s;
        const double seconds = base->buffer.getNumSamples() / base->sampleRate;
        const double beats = seconds * getWarpTempo (w) / 60.0;
        int best = 0;
        double bestDist = 1.0e9;
        for (int i = 0; i < rz::WarpSettings::lengthNames().size(); ++i)
        {
            const double d = std::abs (std::log (rz::WarpSettings::lengthInBeats (i) / juce::jmax (1.0e-3, beats)));
            if (d < bestDist) { bestDist = d; best = i; }
        }
        w.lengthIndex = best;
    }
    return w;
}

void RezalicaProcessor::updateWarpTempo (bool async)
{
    std::vector<rz::SampleData::Ptr> done;
    for (int i = 0; i < rz::numPads; ++i)
    {
        auto s = engine.getPadSample (i);
        if (s == nullptr || ! s->isWarped() || ! s->warp.syncHost || std::find (done.begin(), done.end(), s) != done.end())
            continue;
        done.push_back (s);

        const double bpm = getWarpTempo (s->warp);
        if (std::abs (bpm - s->renderedBpm) < 0.01)
            continue;

        if (! async)
        {
            auto fresh = renderWarp (s->warpSource, s->regionStart, s->regionEnd, s->warp, bpm);
            replaceSampleOnPads (s, fresh);
            padsChanged.sendChangeMessage();
            continue;
        }

        // background: one job per sample at a time
        if (std::find (tempoJobsPending.begin(), tempoJobsPending.end(), s.get()) != tempoJobsPending.end())
            continue;
        tempoJobsPending.push_back (s.get());
        std::weak_ptr<int> alive = aliveToken;
        ++warpJobs;
        stretchWorker.addJob ([this, alive, s, bpm]
        {
            auto result = std::make_shared<juce::AudioBuffer<float>> (computeStretch (*s->warpSource, s->regionStart, s->regionEnd, s->warp, bpm));
            juce::MessageManager::callAsync ([this, alive, s, bpm, result]
            {
                if (alive.expired())
                    return;
                --warpJobs;
                tempoJobsPending.erase (std::remove (tempoJobsPending.begin(), tempoJobsPending.end(), s.get()), tempoJobsPending.end());

                bool stillUsed = false;
                for (int padIndex = 0; padIndex < rz::numPads; ++padIndex)
                    stillUsed = stillUsed || engine.getPadSample (padIndex) == s;
                if (! stillUsed)
                    return;

                auto fresh = renderWarp (s->warpSource, s->regionStart, s->regionEnd, s->warp, bpm, true, result.get());
                replaceSampleOnPads (s, fresh);
                padsChanged.sendChangeMessage();
            });
        });
    }
}

//==============================================================================
juce::ValueTree RezalicaProcessor::padToValueTree (int pad) const
{
    const auto& ps = engine.getPad (pad);
    juce::ValueTree t ("PADCOPY");
    t.setProperty ("start", ps.start.load(), nullptr);
    t.setProperty ("end", ps.end.load(), nullptr);
    t.setProperty ("gain", ps.gainDb.load(), nullptr);
    t.setProperty ("pan", ps.pan.load(), nullptr);
    t.setProperty ("pitch", ps.pitch.load(), nullptr);
    t.setProperty ("attack", ps.attackMs.load(), nullptr);
    t.setProperty ("release", ps.releaseMs.load(), nullptr);
    t.setProperty ("loop", ps.loop.load(), nullptr);
    t.setProperty ("reverse", ps.reverse.load(), nullptr);
    t.setProperty ("gate", ps.gate.load(), nullptr);
    t.setProperty ("mute", ps.mute.load(), nullptr);
    t.appendChild (ps.effects.toValueTree(), nullptr);
    return t;
}

void RezalicaProcessor::padFromValueTree (int pad, const juce::ValueTree& t)
{
    auto& ps = engine.getPad (pad);
    ps.start = (float) t.getProperty ("start", 0.0f);
    ps.end = (float) t.getProperty ("end", 1.0f);
    ps.gainDb = (float) t.getProperty ("gain", 0.0f);
    ps.pan = (float) t.getProperty ("pan", 0.0f);
    ps.pitch = (float) t.getProperty ("pitch", 0.0f);
    ps.attackMs = (float) t.getProperty ("attack", 1.0f);
    ps.releaseMs = (float) t.getProperty ("release", 30.0f);
    ps.loop = (bool) t.getProperty ("loop", false);
    ps.reverse = (bool) t.getProperty ("reverse", false);
    ps.gate = (bool) t.getProperty ("gate", false);
    ps.mute = (bool) t.getProperty ("mute", false);
    ps.effects.fromValueTree (t.getChildWithName ("FX"));
}

RezalicaProcessor::UndoState RezalicaProcessor::captureUndoState (const juce::String& name) const
{
    UndoState u;
    u.name = name;
    for (int i = 0; i < rz::numPads; ++i)
    {
        u.samples[(size_t) i] = engine.getPadSample (i);
        u.settings[(size_t) i] = padToValueTree (i);
    }
    {
        const juce::ScopedLock sl (missingPathLock);
        u.missing = missingPaths;
    }
    u.chopSource = chopSource;
    u.chopMode = chopMode;
    u.sensitivity = transientSensitivity;
    u.slicePoints = slicePoints;
    for (int b = 0; b < rz::numBanks; ++b)
    {
        u.bankDb[(size_t) b] = bankDb[(size_t) b];
        u.bankMuted[(size_t) b] = engine.bankMute[(size_t) b].load();
    }
    return u;
}

void RezalicaProcessor::restoreUndoState (const UndoState& u)
{
    ++warpGeneration; // results of a stretch still running belong to the state we leave
    for (int i = 0; i < rz::numPads; ++i)
    {
        padFromValueTree (i, u.settings[(size_t) i]);
        engine.setPadSample (i, u.samples[(size_t) i]);
    }
    {
        const juce::ScopedLock sl (missingPathLock);
        missingPaths = u.missing;
    }
    chopSource = u.chopSource;
    chopMode = u.chopMode;
    transientSensitivity = u.sensitivity;
    slicePoints = u.slicePoints;
    for (int b = 0; b < rz::numBanks; ++b)
    {
        setBankGainDb (b, u.bankDb[(size_t) b]);
        engine.bankMute[(size_t) b] = u.bankMuted[(size_t) b];
    }
    padsChanged.sendChangeMessage();
}

void RezalicaProcessor::pushUndo (const juce::String& actionName)
{
    auto u = captureUndoState (actionName);

    // nothing changed since the last step (e.g. a click without a drag): just rename it
    if (! undoStack.empty())
    {
        const auto& t = undoStack.back();
        bool same = t.chopSource == u.chopSource && t.chopMode == u.chopMode && t.slicePoints == u.slicePoints
                 && t.bankDb == u.bankDb && t.bankMuted == u.bankMuted
                 && std::abs (t.sensitivity - u.sensitivity) < 1.0e-6f && t.missing == u.missing;
        for (int i = 0; i < rz::numPads && same; ++i)
            same = t.samples[(size_t) i] == u.samples[(size_t) i] && t.settings[(size_t) i].isEquivalentTo (u.settings[(size_t) i]);
        if (same)
        {
            undoStack.back().name = actionName;
            return;
        }
    }

    undoStack.push_back (std::move (u));
    if (undoStack.size() > 40)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool RezalicaProcessor::undo()
{
    if (undoStack.empty())
        return false;
    auto u = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back (captureUndoState (u.name));
    restoreUndoState (u);
    return true;
}

bool RezalicaProcessor::redo()
{
    if (redoStack.empty())
        return false;
    auto r = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back (captureUndoState (r.name));
    restoreUndoState (r);
    return true;
}

void RezalicaProcessor::setBankGainDb (int bank, float db)
{
    bank = juce::jlimit (0, rz::numBanks - 1, bank);
    db = juce::jlimit (-60.0f, 6.0f, db);
    bankDb[(size_t) bank] = db;
    engine.bankGain[(size_t) bank] = db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
}

float RezalicaProcessor::getBankGainDb (int bank) const
{
    return bankDb[(size_t) juce::jlimit (0, rz::numBanks - 1, bank)];
}

void RezalicaProcessor::copyBank (int bank)
{
    for (int i = 0; i < rz::padsPerBank; ++i)
    {
        const int pad = bank * rz::padsPerBank + i;
        bankClipboard.samples[(size_t) i] = engine.getPadSample (pad);
        bankClipboard.settings[(size_t) i] = padToValueTree (pad);
    }
    bankClipboard.db = getBankGainDb (bank);
    bankClipboard.valid = true;
}

void RezalicaProcessor::pasteBank (int bank)
{
    if (! bankClipboard.valid)
        return;

    // independent copies; pads that shared a sample (slices) share its copy
    std::map<rz::SampleData*, rz::SampleData::Ptr> copies;
    for (int i = 0; i < rz::padsPerBank; ++i)
    {
        const int pad = bank * rz::padsPerBank + i;
        rz::SampleData::Ptr c;
        if (auto* src = bankClipboard.samples[(size_t) i].get())
        {
            auto it = copies.find (src);
            if (it == copies.end())
            {
                c = src->clone();
                samplePool.add (c);
                if (c->warpSource != nullptr)
                    samplePool.add (c->warpSource);
                copies[src] = c;
            }
            else
            {
                c = it->second;
            }
        }
        padFromValueTree (pad, bankClipboard.settings[(size_t) i]);
        engine.setPadSample (pad, c);
        const juce::ScopedLock sl (missingPathLock);
        missingPaths[(size_t) pad] = {};
    }
    setBankGainDb (bank, bankClipboard.db);
    padsChanged.sendChangeMessage();
}

void RezalicaProcessor::copyPad (int pad)
{
    padClipboard.sample = engine.getPadSample (pad);
    padClipboard.settings = padToValueTree (pad);
    padClipboard.valid = true;
}

void RezalicaProcessor::pastePad (int pad)
{
    if (! padClipboard.valid)
        return;
    padFromValueTree (pad, padClipboard.settings);
    // an independent copy: warping / rendering the original later leaves this pad untouched
    rz::SampleData::Ptr copy = padClipboard.sample != nullptr ? padClipboard.sample->clone() : nullptr;
    if (copy != nullptr)
    {
        samplePool.add (copy);
        if (copy->warpSource != nullptr)
            samplePool.add (copy->warpSource);
    }
    engine.setPadSample (pad, copy);
    {
        const juce::ScopedLock sl (missingPathLock);
        missingPaths[(size_t) pad] = {};
    }
    padsChanged.sendChangeMessage();
}

void RezalicaProcessor::timerCallback()
{
    collectRecordedPianoNotes();
    updateWarpTempo (true);

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
    root.setProperty ("chromaticPad", chromaticPad.load(), nullptr);
    root.setProperty ("bank", currentBank.load(), nullptr);
    for (int b = 0; b < rz::numBanks; ++b)
    {
        root.setProperty ("bankDb" + juce::String (b), bankDb[(size_t) b], nullptr);
        root.setProperty ("bankMute" + juce::String (b), engine.bankMute[(size_t) b].load(), nullptr);
    }
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree pads ("PADS");
    for (int i = 0; i < rz::numPads; ++i)
    {
        auto& ps = engine.getPad (i);
        juce::ValueTree p ("PAD");
        p.setProperty ("index", i, nullptr);

        juce::String path;
        if (auto s = engine.getPadSample (i))
        {
            path = s->file.getFullPathName();
            // pads that share one sample object (slices of a chop) get the same id
            int id = -1;
            for (int j = 0; j < i && id < 0; ++j)
                if (engine.getPadSample (j) == s) id = j;
            p.setProperty ("sampleId", id >= 0 ? id : i, nullptr);
        }
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
        if (auto ws = engine.getPadSample (i); ws != nullptr && ws->isWarped())
        {
            auto w = ws->warp.toValueTree();
            w.setProperty ("regionStart", ws->regionStart, nullptr);
            w.setProperty ("regionEnd", ws->regionEnd, nullptr);
            w.setProperty ("renderedBpm", ws->renderedBpm, nullptr);
            w.setProperty ("renderedFile", ws->renderedFile.getFullPathName(), nullptr);
            p.appendChild (w, nullptr);
        }
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
        pat.setProperty ("stride", rz::Sequencer::maxSteps, nullptr);
        pat.setProperty ("name", sequencer.patternNames[(size_t) i], nullptr);
        pat.setProperty ("nudge", sequencer.getNudgeData (i).toBase64Encoding(), nullptr);
        pat.appendChild (sequencer.getClip (i)->toValueTree(), nullptr);
        seq.appendChild (pat, nullptr);
    }
    root.appendChild (seq, nullptr);
    root.appendChild (sequencer.songToValueTree(), nullptr);

    juce::ValueTree chop ("CHOP");
    chop.setProperty ("mode", chopMode, nullptr);
    chop.setProperty ("sensitivity", transientSensitivity, nullptr);
    {
        juce::StringArray pts;
        for (auto p : slicePoints) pts.add (juce::String (p, 6));
        chop.setProperty ("points", pts.joinIntoString (","), nullptr);
    }
    chop.setProperty ("source", chopSource != nullptr ? chopSource->file.getFullPathName() : juce::String(), nullptr);
    for (int i = 0; i < rz::numPads; ++i)
        if (chopSource != nullptr && engine.getPadSample (i) == chopSource)
        {
            int id = -1;
            for (int j = 0; j < i && id < 0; ++j)
                if (engine.getPadSample (j) == chopSource) id = j;
            chop.setProperty ("sourceId", id >= 0 ? id : i, nullptr);
            break;
        }
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
    chromaticPad = juce::jlimit (-1, rz::numPads - 1, (int) root.getProperty ("chromaticPad", -1));
    currentBank = juce::jlimit (0, rz::numBanks - 1, (int) root.getProperty ("bank", 0));
    for (int b = 0; b < rz::numBanks; ++b)
    {
        setBankGainDb (b, (float) root.getProperty ("bankDb" + juce::String (b), 0.0f));
        engine.bankMute[(size_t) b] = (bool) root.getProperty ("bankMute" + juce::String (b), false);
    }

    auto pads = root.getChildWithName ("PADS");
    std::map<int, rz::SampleData::Ptr> restoredById;
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
        const int sampleId = (int) p.getProperty ("sampleId", -1);
        if (sampleId >= 0 && restoredById.count (sampleId) > 0)
        {
            // same sample object as an earlier pad (slices of one chop)
            engine.setPadSample (i, restoredById[sampleId]);
            const juce::ScopedLock sl (missingPathLock);
            missingPaths[(size_t) i] = {};
            continue;
        }

        rz::SampleData::Ptr s;
        if (path.isNotEmpty())
        {
            juce::File f (path);
            if (f.existsAsFile())
                s = loadSample (f, sampleId < 0); // projects without ids share by file, as before
        }

        // warped (time-stretched) sample: reuse, load the WAV saved in the background, or re-stretch
        if (auto w = p.getChildWithName ("WARP"); s != nullptr && w.isValid() && (bool) w.getProperty ("enabled", false))
        {
            const auto settings = rz::WarpSettings::fromValueTree (w);
            const float ra = (float) w.getProperty ("regionStart", 0.0f), rb = (float) w.getProperty ("regionEnd", 1.0f);
            const double bpm = (double) w.getProperty ("renderedBpm", 120.0);
            rz::SampleData::Ptr warped;

            for (auto* c : samplePool)
                if (c->warpSource == s && std::abs (c->regionStart - ra) < 1.0e-6f && std::abs (c->regionEnd - rb) < 1.0e-6f
                    && c->warp.sameRenderAs (settings) && std::abs (c->renderedBpm - bpm) < 0.01)
                    { warped = c; break; }

            const juce::File rendered (w.getProperty ("renderedFile").toString());
            if (warped == nullptr && rendered.existsAsFile())
            {
                std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (rendered));
                if (reader != nullptr && reader->lengthInSamples > 0)
                {
                    warped = new rz::SampleData();
                    warped->buffer.setSize (2, (int) reader->lengthInSamples);
                    reader->read (&warped->buffer, 0, (int) reader->lengthInSamples, 0, true, true);
                    if (reader->numChannels == 1)
                        warped->buffer.copyFrom (1, 0, warped->buffer, 0, 0, warped->buffer.getNumSamples());
                    warped->sampleRate = reader->sampleRate;
                    warped->name = s->name + " [" + rz::WarpSettings::lengthNames()[settings.lengthIndex] + "]";
                    warped->file = s->file;
                    warped->warpSource = s;
                    warped->regionStart = ra;
                    warped->regionEnd = rb;
                    warped->warp = settings;
                    warped->renderedBpm = bpm;
                    warped->renderedFile = rendered;
                    samplePool.add (warped);
                }
            }
            if (warped == nullptr)
                warped = renderWarp (s, ra, rb, settings, bpm);
            s = warped;
        }

        engine.setPadSample (i, s);
        if (sampleId >= 0 && s != nullptr)
            restoredById[sampleId] = s;

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
            sequencer.setPatternData (i, mb, (int) pat.getProperty ("stride", 32));
            sequencer.patternNames[(size_t) i] = pat.getProperty ("name").toString();
            juce::MemoryBlock nb;
            nb.fromBase64Encoding (pat.getProperty ("nudge").toString());
            sequencer.setNudgeData (i, nb);
            if (auto clip = pat.getChildWithName ("CLIP"); clip.isValid())
                sequencer.setClip (i, rz::NoteClip::fromValueTree (clip, sequencer.noteIdCounter()));
            else
                sequencer.setClip (i, new rz::NoteClip());
        }
    }

    sequencer.songFromValueTree (root.getChildWithName ("SONG"));

    auto chop = root.getChildWithName ("CHOP");
    chopSource = nullptr;
    slicePoints.clear();
    if (chop.isValid())
    {
        chopMode = (int) chop.getProperty ("mode", (int) chop16);
        transientSensitivity = (float) chop.getProperty ("sensitivity", 0.5f);
        juce::File src (chop.getProperty ("source").toString());
        const int sourceId = (int) chop.getProperty ("sourceId", -1);
        if (sourceId >= 0 && restoredById.count (sourceId) > 0)
        {
            chopSource = restoredById[sourceId];
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
        else if (src.existsAsFile())
        {
            chopSource = loadSample (src, true);
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

    // the chop source may be playing in its warped version
    if (chopSource != nullptr)
        for (int i = 0; i < rz::numPads; ++i)
            if (auto ps = engine.getPadSample (i); ps != nullptr && ps->warpSource == chopSource && ps->warp.wholeSample)
            {
                chopSource = ps;
                break;
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
