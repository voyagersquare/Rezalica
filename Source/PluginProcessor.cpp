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
    pFilterOn = apvts.getRawParameterValue ("filterOn");
    pCutoff = apvts.getRawParameterValue ("cutoff");
    pReso = apvts.getRawParameterValue ("resonance");
    pDrive = apvts.getRawParameterValue ("drive");
    pLofiOn = apvts.getRawParameterValue ("lofiOn");
    pBits = apvts.getRawParameterValue ("bits");
    pRate = apvts.getRawParameterValue ("rate");
    pNoise = apvts.getRawParameterValue ("noise");
    pDelayOn = apvts.getRawParameterValue ("delayOn");
    pDelayDiv = apvts.getRawParameterValue ("delayDiv");
    pDelayTime = apvts.getRawParameterValue ("delayTime");
    pFeedback = apvts.getRawParameterValue ("feedback");
    pDelayMix = apvts.getRawParameterValue ("delayMix");

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

    auto hz = AudioParameterFloatAttributes().withLabel ("Hz");
    auto ms = AudioParameterFloatAttributes().withLabel ("ms");
    auto db = AudioParameterFloatAttributes().withLabel ("dB");

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.0f), 0.0f, db));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "filterOn", 1 }, "Filter On", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "cutoff", 1 }, "Cutoff", NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), 20000.0f, hz));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "resonance", 1 }, "Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.1f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive", NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "lofiOn", 1 }, "Lo-Fi On", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bits", 1 }, "Bits", NormalisableRange<float> (2.0f, 16.0f, 0.1f), 10.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "rate", 1 }, "Rate Divide", NormalisableRange<float> (1.0f, 32.0f, 0.1f, 0.5f), 3.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "noise", 1 }, "Vinyl Noise", NormalisableRange<float> (0.0f, 1.0f), 0.2f));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "delayOn", 1 }, "Delay On", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "delayDiv", 1 }, "Delay Sync", delayDivisions, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "delayTime", 1 }, "Delay Time", NormalisableRange<float> (10.0f, 2000.0f, 1.0f, 0.5f), 375.0f, ms));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "feedback", 1 }, "Feedback", NormalisableRange<float> (0.0f, 0.95f), 0.35f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "delayMix", 1 }, "Delay Mix", NormalisableRange<float> (0.0f, 1.0f), 0.3f));

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
    fx.prepare (sampleRate, samplesPerBlock);
    dryBuffer.setSize (2, juce::jmax (1, samplesPerBlock));
    fxBuffer.setSize (2, juce::jmax (1, samplesPerBlock));
    pendingSeqOff.fill (-1.0);
}

//==============================================================================
void RezalicaProcessor::addEvent (int offset, int type, int pad, float velocity)
{
    if (numEvents < (int) events.size())
        events[(size_t) numEvents++] = { offset, type, pad, velocity };
}

rz::FxChain::Params RezalicaProcessor::readFxParams() const
{
    rz::FxChain::Params p;
    p.filterOn = pFilterOn->load() > 0.5f;
    p.cutoff = pCutoff->load();
    p.resonance = pReso->load();
    p.drive = pDrive->load();
    p.lofiOn = pLofiOn->load() > 0.5f;
    p.bits = pBits->load();
    p.rateDivide = pRate->load();
    p.noise = pNoise->load();
    p.delayOn = pDelayOn->load() > 0.5f;
    p.feedback = pFeedback->load();
    p.mix = pDelayMix->load();

    const int div = juce::jlimit (0, delayDivisions.size() - 1, (int) pDelayDiv->load());
    if (div == 0)
        p.delaySamples = pDelayTime->load() * 0.001f * (float) currentSampleRate;
    else
        p.delaySamples = (float) (delayDivisionBeats[div] * 60.0 / juce::jmax (20.0f, sequencer.effectiveBpm.load()) * currentSampleRate);

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
        fxBuffer.setSize (2, numSamples, false, false, true);
    }
    dryBuffer.clear();
    fxBuffer.clear();

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
        engine.render (dryBuffer, fxBuffer, cursor, e.offset - cursor);
        cursor = e.offset;

        switch (e.type)
        {
            case -1: engine.allNotesOff(); sequencer.internalPlay = false; break;
            case 0:  engine.noteOff (e.pad, false); break;
            case 1:  engine.noteOff (e.pad, true); break;
            case 2:  engine.noteOn (e.pad, e.velocity, false); break;
            case 3:  engine.noteOn (e.pad, e.velocity, true); break;
            default: break;
        }
    }
    engine.render (dryBuffer, fxBuffer, cursor, numSamples - cursor);
    engine.updatePlayPositions();

    fx.process (fxBuffer, numSamples, readFxParams());

    const float master = juce::Decibels::decibelsToGain (pMaster->load(), -59.9f);
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        buffer.addFrom (ch, 0, dryBuffer, ch, 0, numSamples, master);
        buffer.addFrom (ch, 0, fxBuffer, ch, 0, numSamples, master);
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

std::vector<float> RezalicaProcessor::findSlicePoints (const rz::SampleData& s, int mode)
{
    std::vector<float> points;
    const int len = s.buffer.getNumSamples();

    if (mode != chopTransients)
    {
        for (int i = 0; i < mode; ++i)
            points.push_back ((float) i / (float) mode);
        return points;
    }

    // Simple energy-flux onset detection.
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
    float mean = 0.0f;
    for (int f = 1; f < frames; ++f)
    {
        flux[(size_t) f] = juce::jmax (0.0f, energy[(size_t) f] - energy[(size_t) f - 1]);
        mean += flux[(size_t) f];
    }
    mean /= (float) frames;

    float var = 0.0f;
    for (auto v : flux) var += (v - mean) * (v - mean);
    const float threshold = mean + 1.0f * std::sqrt (var / (float) frames);

    std::vector<std::pair<float, int>> candidates;
    for (int f = 1; f < frames - 1; ++f)
        if (flux[(size_t) f] > threshold && flux[(size_t) f] >= flux[(size_t) f - 1] && flux[(size_t) f] >= flux[(size_t) f + 1])
            candidates.push_back ({ flux[(size_t) f], f });

    std::sort (candidates.begin(), candidates.end(), [] (auto& a, auto& b) { return a.first > b.first; });

    const int minGap = juce::jmax (4, (int) (s.sampleRate * 0.06 / hop));
    std::vector<int> chosen { 0 };
    for (auto& c : candidates)
    {
        if ((int) chosen.size() >= rz::numPads) break;
        bool ok = true;
        for (auto f : chosen)
            if (std::abs (f - c.second) < minGap) { ok = false; break; }
        if (ok) chosen.push_back (c.second);
    }
    std::sort (chosen.begin(), chosen.end());

    for (auto f : chosen)
        points.push_back (juce::jmax (0.0f, (float) (f * hop - hop / 4) / (float) len));

    if (points.size() < 2)
        return findSlicePoints (s, chop16);

    return points;
}

void RezalicaProcessor::applySlices (const rz::SampleData::Ptr& s, const std::vector<float>& points, int firstPad)
{
    for (size_t k = 0; k < points.size(); ++k)
    {
        const int pad = firstPad + (int) k;
        if (pad >= rz::numPads) break;
        auto& ps = engine.getPad (pad);
        ps.start = points[k];
        ps.end = (k + 1 < points.size()) ? points[k + 1] : 1.0f;
        engine.setPadSample (pad, s);
        const juce::ScopedLock sl (missingPathLock);
        missingPaths[(size_t) pad] = {};
    }
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

bool RezalicaProcessor::chopFileToPads (const juce::File& file, int mode)
{
    auto s = loadSample (file);
    if (s == nullptr)
        return false;

    applySlices (s, findSlicePoints (*s, mode), 0);
    return true;
}

void RezalicaProcessor::chopPadSample (int pad, int mode)
{
    if (auto s = engine.getPadSample (pad))
        applySlices (s, findSlicePoints (*s, mode), 0);
}

void RezalicaProcessor::clearPad (int pad)
{
    engine.setPadSample (pad, nullptr);
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
        p.setProperty ("fx", ps.fx.load(), nullptr);
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
        ps.fx = (bool) p.getProperty ("fx", true);
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
