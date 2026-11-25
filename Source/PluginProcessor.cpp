#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TimeStretch.h"
#include "Mp3Writer.h"
#include <algorithm>
#include <map>
#include <cmath>

namespace
{
const juce::StringArray delayDivisions { "Free", "1/4", "1/8", "1/8 dot", "1/16", "1/4 trip", "1/8 trip" };
const double delayDivisionBeats[] { 0.0, 1.0, 0.5, 0.75, 0.25, 2.0 / 3.0, 1.0 / 3.0 };
}

//==============================================================================
juce::AudioProcessor::BusesProperties RezalicaProcessor::makeBuses (bool withInput)
{
    auto b = BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    if (withInput)
        b = b.withInput ("Input", juce::AudioChannelSet::stereo(), true);
    return b;
}

RezalicaProcessor::RezalicaProcessor (bool withInput)
    : AudioProcessor (makeBuses (withInput)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();
    pendingSeqOff.fill (-1.0);

    pMaster = apvts.getRawParameterValue ("master");
    for (auto& n : noteIsControl) n = false;
    notePlaying.fill (-1);
    melodicPlaying.fill (-1);
    clockBuffer.ensureSize (2048);
    setPadNoteLayout (layoutRezalica);

    startTimer (1000);
}

//==============================================================================
// MIDI: pad notes

void RezalicaProcessor::setPadNoteLayout (int layout)
{
    if (layout == layoutCustom) { padNoteLayout = layoutCustom; return; } // keeps the learned map
    std::array<int, 128> m;
    m.fill (-1);
    if (layout == layoutMpc || layout == layoutGm)
    {
        // 16 notes that play the 16 pads of the current bank, laid out like the hardware:
        // the MPC's pad 1 is bottom left (our A13), pad 13 top left (our A1)
        static const int mpc[16] = { 37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53 };
        // General MIDI drums: kick / snare / hats in the bottom row, toms and cymbals above
        static const int gm[16]  = { 36, 38, 42, 46, 37, 39, 44, 49, 41, 43, 45, 47, 48, 50, 51, 53 };
        for (int k = 0; k < 16; ++k)
        {
            const int row = 3 - k / 4, col = k % 4;             // hardware pad k+1 -> our position
            const int note = (layout == layoutMpc ? mpc : gm)[k];
            m[(size_t) note] = bankRelative + row * 4 + col;
        }
    }
    else
    {
        for (int n = midiBaseNote; n < midiBaseNote + rz::numPads && n < 128; ++n)
            m[(size_t) n] = n - midiBaseNote;                // C1 = A1 ... D#7 = D16
        layout = layoutRezalica;
    }
    for (int n = 0; n < 128; ++n) noteMap[(size_t) n] = m[(size_t) n];
    padNoteLayout = layout;
}

std::array<int, 128> RezalicaProcessor::getPadNoteMap() const
{
    std::array<int, 128> m;
    for (int n = 0; n < 128; ++n) m[(size_t) n] = noteMap[(size_t) n].load();
    return m;
}

void RezalicaProcessor::setPadNoteMap (const std::array<int, 128>& map, int layout)
{
    for (int n = 0; n < 128; ++n) noteMap[(size_t) n] = map[(size_t) n];
    padNoteLayout = layout;
}

int RezalicaProcessor::padForNote (int note) const noexcept
{
    if (note < 0 || note > 127) return -1;
    const int v = noteMap[(size_t) note].load();
    if (v >= bankRelative) return juce::jlimit (0, rz::numPads - 1, currentBank.load() * rz::padsPerBank + (v - bankRelative));
    return v >= 0 && v < rz::numPads ? v : -1;
}

int RezalicaProcessor::noteForPad (int pad) const noexcept
{
    for (int n = 0; n < 128; ++n)
        if (padForNote (n) == pad) return n;
    return -1;
}

void RezalicaProcessor::pushControlEvent (juce::uint8 kind, int channel, int number, int value)
{
    const auto w = controlFifo.write (1);
    w.forEach ([&] (int i)
    {
        controlEvents[(size_t) i] = { kind, (juce::uint8) juce::jlimit (1, 16, channel), (juce::uint8) juce::jlimit (0, 127, number),
                                      (juce::uint8) juce::jlimit (0, 127, value) };
    });
}

//==============================================================================
// MIDI clock out

void RezalicaProcessor::setClockOutput (const juce::String& id)
{
    std::unique_ptr<juce::MidiOutput> fresh;
    if (id.isNotEmpty())
    {
        fresh = juce::MidiOutput::openDevice (id);
        if (fresh != nullptr) fresh->startBackgroundThread();
    }
    std::unique_ptr<juce::MidiOutput> old;
    {
        const juce::SpinLock::ScopedLockType sl (clockOutLock);
        old = std::move (clockOut);
        clockOut = std::move (fresh);
        clockOutId = clockOut != nullptr ? id : juce::String();
        clockWasRunning = false;
    }
    if (old != nullptr) { old->stopBackgroundThread(); }
}

void RezalicaProcessor::makeClockMessages (juce::MidiBuffer& out, int numSamples)
{
    // 24 clock ticks per beat: in time with the sequencer while it plays, free running at the tempo when it stops
    const bool running = sequencer.isRunning() && sequencer.internalPlay.load();
    if (sequencer.justRestarted)
        out.addEvent (juce::MidiMessage::midiStart(), 0);
    if (running)
    {
        const double ppq0 = sequencer.getBlockPpq(), pps = sequencer.getPpqPerSampleNow();
        // (the same rounding at both ends of the block: a tick on the boundary is sent once)
        if (pps > 0.0)
            for (double k = std::ceil (ppq0 * 24.0 - 1.0e-6), end = std::ceil ((ppq0 + numSamples * pps) * 24.0 - 1.0e-6); k < end; k += 1.0)
                out.addEvent (juce::MidiMessage::midiClock(), juce::jlimit (0, numSamples - 1, (int) ((k / 24.0 - ppq0) / pps)));
    }
    else
    {
        if (clockWasRunning) out.addEvent (juce::MidiMessage::midiStop(), 0);
        const double perSample = juce::jmax (20.0f, sequencer.bpm.load()) / 60.0 / juce::jmax (1.0, currentSampleRate) * 24.0;
        const double end = freeClockTicks + numSamples * perSample;
        for (double k = std::ceil (freeClockTicks - 1.0e-6), last = std::ceil (end - 1.0e-6); k < last; k += 1.0)
            out.addEvent (juce::MidiMessage::midiClock(), juce::jlimit (0, numSamples - 1, (int) ((k - freeClockTicks) / perSample)));
        freeClockTicks = end;
    }
    clockWasRunning = running;
}

RezalicaProcessor::~RezalicaProcessor()
{
    stopTimer();
    setClockOutput ({});
    pluginsAboutToChange.clear();
    for (auto& bus : buses)
    {
        { const juce::SpinLock::ScopedLockType sl (bus.lock); for (auto& s : bus.slots) s.live = nullptr; }
        for (auto& s : bus.slots) s.owned = nullptr;
    }
    loopback.stop();
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
    const auto in = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void RezalicaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    sequencer.prepare (sampleRate);
    preparedBlockSize = juce::jmax (1, samplesPerBlock);
    inputCopy.setSize (2, juce::jmax (8192, samplesPerBlock));
    for (auto& b : busBuffers)
        b.setSize (2, juce::jmax (1, samplesPerBlock));
    for (auto& bus : buses)
    {
        const juce::SpinLock::ScopedLockType sl (bus.lock);
        for (auto& slot : bus.slots)
            if (slot.owned != nullptr)
                prepareSlot (slot);
    }
    masterFx.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    clickLength = juce::jmax (1, (int) (sampleRate * 0.035));
    clickSamplesLeft = 0;
    clickBuffer.setSize (1, juce::jmax (1, samplesPerBlock));
    deck.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    deckBuffer.setSize (2, juce::jmax (1, samplesPerBlock));
    clickDelay.prepare (maxCompensation);
    // delay lines: the buses and the DECK always; a pad's chain only once it has an effect (they are big)
    for (int i = 0; i < rz::numEffectChains; ++i)
    {
        auto& bus = buses[(size_t) i];
        bool used = i < rz::padChainBase;
        for (auto& slot : bus.slots) used = used || slot.owned != nullptr;
        if (used) bus.compensation.prepare (maxCompensation);
    }
    for (auto& d : busPreDelay) d.prepare (maxCompensation);
    for (auto& b : padBuffers) b.setSize (2, juce::jmax (1, samplesPerBlock));
    pendingSeqOff.fill (-1.0);
}

//==============================================================================
void RezalicaProcessor::addEvent (int offset, int type, int pad, float velocity)
{
    if (numEvents < (int) events.size())
        events[(size_t) numEvents++] = { offset, type, pad, velocity };
}

void RezalicaProcessor::addTaggedEvent (int offset, int type, int pad, float velocity, float transpose, int tag, bool flag, double skip)
{
    if (numEvents < (int) events.size())
        events[(size_t) numEvents++] = { offset, type, pad, velocity, transpose, tag, flag, skip };
}

void RezalicaProcessor::clearEffectTails()
{
    for (auto& bus : buses)
    {
        const juce::SpinLock::ScopedLockType sl (bus.lock); // the audio thread skips the slots meanwhile
        for (auto& slot : bus.slots)
            if (slot.owned != nullptr)
                slot.owned->reset();
    }
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
    // sampling: the sound card input arrives in the first channels
    inputCopyChannels = 0;
    const int throughBus = inputThroughBus.load();
    if (const int inCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels()); inCh > 0)
    {
        if (throughBus < 0)
            recorder.capture (rz::AudioRecorder::input, buffer.getArrayOfReadPointers(), inCh, numSamples);
        if ((monitorInput.load() || throughBus >= 0) && inputCopy.getNumSamples() >= numSamples)
        {
            inputCopyChannels = juce::jmin (2, inCh);
            for (int ch = 0; ch < 2; ++ch)
                inputCopy.copyFrom (ch, 0, buffer, juce::jmin (ch, inCh - 1), 0, numSamples);
            if (const int mode = recorder.inputMode.load(); mode > 0)   // mono L / R for monitor and bus too
                inputCopy.copyFrom (mode == 1 ? 1 : 0, 0, inputCopy, mode == 1 ? 0 : 1, 0, numSamples);
        }
    }
    buffer.clear();

    if (busBuffers[0].getNumSamples() < numSamples)
        for (auto& b : busBuffers)
            b.setSize (2, numSamples, false, false, true);
    for (auto& b : busBuffers)
        b.clear (0, numSamples);
    // sampling through a bus: the input goes into that bus (its effects), recorded after them
    if (throughBus >= 0 && inputCopyChannels > 0)
        for (int ch = 0; ch < 2; ++ch)
            busBuffers[(size_t) juce::jlimit (0, rz::numBuses - 1, throughBus)].addFrom (ch, 0, inputCopy, ch, 0, numSamples);

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
        [&] (int mode, int row, float vel, int offset, int tag, int targetPad, double skip)   // piano roll note on
        {
            if (mode == rz::NoteClip::melodic)
                addTaggedEvent (offset, evOnTag, targetPad, vel, (float) (row - 60), tag, true, skip);
            else if (row >= 0 && row < rz::numPads)
                addTaggedEvent (offset, evOnTag, row, vel, 0.0f, tag, false, skip);
        },
        [&] (int mode, int, int offset, int tag, int)              // piano roll note off
        {
            addTaggedEvent (offset, evOffTag, -1, 0.0f, 0.0f, tag, mode == rz::NoteClip::melodic);
        });

    // STOP once: what sounds fades out with each pad's RELEASE; twice: cut at once (+ the metronome click)
    if (const int stopKind = sequencer.stopSound.exchange (0); stopKind == 2)
    {
        engine.allNotesOff();
        clickSamplesLeft = 0;
    }
    else if (stopKind == 1)
    {
        engine.releaseAll();
    }

    // MIDI clock out
    {
        const juce::SpinLock::ScopedTryLockType tl (clockOutLock);
        if (tl.isLocked() && clockOut != nullptr)
        {
            clockBuffer.clear();
            makeClockMessages (clockBuffer, numSamples);
            if (! clockBuffer.isEmpty())
            {
                // stamped so the ticks leave together with the audio of this block
                const double delayMs = (pluginLatencySamples.load() + deviceOutputLatencySamples.load()) * 1000.0 / juce::jmax (1.0, currentSampleRate);
                clockOut->sendBlockOfMessages (clockBuffer, juce::Time::getMillisecondCounterHiRes() + delayMs, currentSampleRate);
            }
        }
    }

    if (sequencer.justRestarted)
    {
        recorder.sequencerStarted(); // resampling a pattern starts exactly with it
        // PLAY with an empty pattern (no steps, no piano roll notes): the selected pad's sample plays
        // (with LOOP it loops until STOP, else it plays once)
        // (not while REC is armed: then PLAY starts the recording, not the sample)
        if (! sequencer.isPlayingSong() && ! sequencer.recording.load() && ! sequencer.pianoRecording.load()
            && sequencer.isPatternEmpty (sequencer.currentPattern.load()))
        {
            addTaggedEvent (0, evOffTag, -1, 0.0f, 0.0f, playSampleTag, true);
            addTaggedEvent (0, evOnTag, selectedPad.load(), 1.0f, 0.0f, playSampleTag, false);
            playingSampleOnPlay = true;
        }
    }

    if (playingSampleOnPlay && ! sequencer.isRunning())
    {
        addTaggedEvent (0, evOffTag, -1, 0.0f, 0.0f, playSampleTag, true); // STOP ends it
        playingSampleOnPlay = false;
    }

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
                if (e.record) // played on the computer keyboard in the piano roll: recorded as a melodic note
                {
                    const int note = juce::jlimit (0, 127, 60 + juce::roundToInt (e.transpose));
                    if (e.on) sequencer.recordPianoNoteOn (note, e.velocity, 0, true);
                    else      sequencer.recordPianoNoteOff (note, 0, true);
                }
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
    if (! midi.isEmpty()) ++midiActivity;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int offset = juce::jlimit (0, numSamples - 1, meta.samplePosition);

        // MIDI clock in: follow the external tempo, start / continue / stop
        if (clockIn.load())
        {
            if (m.isMidiClock())
            {
                const juce::int64 t = sampleClock + offset;
                if (lastClockTick >= 0)
                {
                    const double iv = (double) (t - lastClockTick);
                    if (iv > 0.0 && iv < currentSampleRate * 0.5)
                    {
                        clockInterval = clockInterval <= 0.0 ? iv : clockInterval * 0.9 + iv * 0.1;
                        if (++clockTicks % 24 == 0)
                        {
                            const double bpm = juce::jlimit (20.0, 400.0, 60.0 * currentSampleRate / (24.0 * clockInterval));
                            sequencer.bpm = (float) (std::round (bpm * 10.0) / 10.0);
                        }
                    }
                }
                lastClockTick = t;
                continue;
            }
            if (m.isMidiStart())    { sequencer.playPattern(); continue; }
            if (m.isMidiContinue()) { if (sequencer.isPaused()) sequencer.resumePlayback(); else sequencer.playPattern(); continue; }
            if (m.isMidiStop())     { sequencer.stopPlayback(); sequencer.stopSound = 1; continue; }
        }

        // MIDI Machine Control: the transport keys of most controllers (Akai MPD / MPK, M-Audio ...) send it
        // by default, so PLAY / STOP / REC / PAUSE work without any set-up
        if (m.isSysEx())
        {
            const auto* d = m.getSysExData();
            if (m.getSysExDataSize() >= 4 && d[0] == 0x7f && d[2] == 0x06)
            {
                switch (d[3])
                {
                    case 0x01: sequencer.stopPlayback(); sequencer.stopSound = 1; break;                     // STOP
                    case 0x02: case 0x03:                                                                   // PLAY
                        if (sequencer.isPaused()) sequencer.resumePlayback();
                        else if (! sequencer.internalPlay.load()) sequencer.playPattern();
                        break;
                    case 0x06: sequencer.recording = true;                                                  // RECORD
                               if (! sequencer.internalPlay.load()) sequencer.playPattern();
                               break;
                    case 0x07: sequencer.recording = false; break;                                          // RECORD EXIT
                    case 0x09: sequencer.pausePlayback(); break;                                            // PAUSE
                    default: break;
                }
                ++mmcCount;
            }
            continue;
        }

        // knobs / faders / buttons of the controller: to the editor (MIDI Learn)
        if (m.isController())
        {
            pushControlEvent (0, m.getChannel(), m.getControllerNumber(), m.getControllerValue());
            continue;
        }
        if ((m.isNoteOn() || m.isNoteOff()) && (midiLearnListening.load() || noteIsControl[(size_t) m.getNoteNumber()].load()))
        {
            // a note learned for a button (or MIDI Learn is waiting): it does not play a pad
            pushControlEvent (m.isNoteOn() ? 1 : 2, m.getChannel(), m.getNoteNumber(), m.isNoteOn() ? m.getVelocity() : 0);
            continue;
        }
        if (m.isNoteOn())
            if (const int learnFor = learnPadNote.exchange (-1); learnFor >= 0)
            {
                // "Learn MIDI note" on a pad: this note plays that pad from now on (the custom layout)
                const int note = m.getNoteNumber();
                for (auto& v : noteMap)
                    if (v.load() == learnFor || (v.load() >= bankRelative && currentBank.load() * rz::padsPerBank + v.load() - bankRelative == learnFor))
                        v = -1;
                noteMap[(size_t) note] = learnFor;
                padNoteLayout = layoutCustom;
                ++padNoteMapChanged;
            }

        const int chromatic = chromaticPad.load();

        if (m.isNoteOn() && chromatic >= 0 && chromatic < rz::numPads)
        {
            // melodic: every key plays the chosen pad, C3 (60) = original pitch (with a scale: its nearest note)
            const int note = rz::scales::snap (scaleType.load(), scaleRoot.load(), m.getNoteNumber());
            melodicPlaying[(size_t) m.getNoteNumber()] = note;
            addTaggedEvent (offset, evOnTag, chromatic, m.getFloatVelocity(), (float) (note - 60), rz::Sequencer::midiTagBase + note, true);
            sequencer.recordPianoNoteOn (note, m.getFloatVelocity(), offset, true);
        }
        else if (m.isNoteOff() && chromatic >= 0 && chromatic < rz::numPads)
        {
            const int note = melodicPlaying[(size_t) m.getNoteNumber()] >= 0 ? melodicPlaying[(size_t) m.getNoteNumber()] : m.getNoteNumber();
            melodicPlaying[(size_t) m.getNoteNumber()] = -1;
            addTaggedEvent (offset, evOffTag, -1, 0.0f, 0.0f, rz::Sequencer::midiTagBase + note, true);
            sequencer.recordPianoNoteOff (note, offset, true);
        }
        else if (m.isNoteOn())
        {
            const int pad = padForNote (m.getNoteNumber());
            notePlaying[(size_t) m.getNoteNumber()] = pad; // (its note-off goes to the same pad, even if the bank changes)
            if (pad >= 0 && pad < rz::numPads)
            {
                addEvent (offset, evOn, pad, m.getFloatVelocity());
                sequencer.recordHit (pad, m.getFloatVelocity(), offset);
                sequencer.recordPianoNoteOn (pad, m.getFloatVelocity(), offset, false);
            }
        }
        else if (m.isNoteOff())
        {
            int pad = notePlaying[(size_t) m.getNoteNumber()];
            if (pad < 0) pad = padForNote (m.getNoteNumber());
            notePlaying[(size_t) m.getNoteNumber()] = -1;
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

    // pads with their own effects play into their own buffers (the chains run after the voices)
    if (padBuffers[0].getNumSamples() < numSamples)
        for (auto& pb : padBuffers) pb.setSize (2, numSamples, false, false, true);
    for (int pd = 0; pd < rz::numPads; ++pd)
    {
        auto& chain = buses[(size_t) (rz::padChainBase + pd)];
        const juce::SpinLock::ScopedTryLockType tl (chain.lock);
        if (tl.isLocked())
        {
            bool any = false;
            for (auto& slot : chain.slots) any = any || (slot.live != nullptr && ! slot.bypass.load());
            padHasFx[(size_t) pd] = any && chain.compensation.line.getNumSamples() > 4;
        }
        engine.padTarget[(size_t) pd] = padHasFx[(size_t) pd] ? &padBuffers[(size_t) pd] : nullptr;
        if (padHasFx[(size_t) pd]) padBuffers[(size_t) pd].clear (0, numSamples);
    }

    std::stable_sort (events.begin(), events.begin() + numEvents, [] (const Event& a, const Event& b)
    {
        return a.offset != b.offset ? a.offset < b.offset : a.type < b.type;
    });

    // ---- render, splitting the block at each event ----
    int cursor = 0;
    for (int i = 0; i < numEvents; ++i)
    {
        const auto& e = events[(size_t) i];
        engine.render (busBuffers, cursor, e.offset - cursor);
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
                engine.noteOn (e.pad, e.velocity, e.tag < rz::Sequencer::midiTagBase, e.transpose, e.tag, e.flag, e.skip);
                break;
            case evOn:
                if (padChoiceArmed.load())
                {
                    // sampling: this hit only chooses the pad (silent, so it can't start / spoil the take)
                    engine.lastPlayedPad = e.pad;
                    ++engine.playedCounter;
                    break;
                }
                if (lazyActive.load() && lazyWaiting.exchange (false))
                {
                    // the first hit: the sample starts here, and its start is the first marker
                    engine.startPreview();
                    auto w = lazyFifo.write (1);
                    w.forEach ([&] (int idx) { lazyHits[(size_t) idx] = 0.0f; });
                }
                else if (lazyActive.load())
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
    engine.render (busBuffers, cursor, numSamples - cursor);
    engine.updatePlayPositions();

    // DECK: its own buffer, then its effects, then MASTER (below, with the plug-in delay compensation)
    if (deckBuffer.getNumSamples() < numSamples)
        deckBuffer.setSize (2, numSamples, false, false, true);
    deckBuffer.clear (0, numSamples);
    deck.render (deckBuffer, numSamples, sequencer.effectiveBpm.load(), sequencer.nextBeatOffset (numSamples));

    if (lazyActive.load() && ! lazyWaiting.load() && ! engine.isPreviewBusy())
        lazyActive = false; // preview reached the end of the sample

    // ---- mixer: BUS A-D (their effects, fader, mute / solo) into MASTER, then the MASTER effects and fader ----
    const int outChannels = juce::jmin (2, buffer.getNumChannels());
    bool anySolo = false;
    for (int bi = 1; bi < rz::numBuses; ++bi) anySolo = anySolo || buses[(size_t) bi].solo.load();
    auto& masterBus = busBuffers[0];

    // plug-in delay compensation: every bus (and the pads that go straight to MASTER) is delayed
    // to the slowest bus, and the metronome by that plus the MASTER plug-ins, so everything stays in time
    for (int bi = 0; bi < rz::numEffectChains; ++bi)
    {
        auto& bus = buses[(size_t) bi];
        const juce::SpinLock::ScopedTryLockType tl (bus.lock);
        if (! tl.isLocked()) continue; // keep the last value while the slots change
        int lat = 0;
        for (auto& slot : bus.slots)
            if (slot.live != nullptr && ! slot.bypass.load())
                lat += juce::jmax (0, slot.live->getLatencySamples());
        bus.latency = lat;
    }
    // the pads' own effects come first: every pad is lined up to the slowest pad chain
    int padLatency = 0;
    for (int pd = 0; pd < rz::numPads; ++pd)
        if (padHasFx[(size_t) pd])
            padLatency = juce::jmax (padLatency, buses[(size_t) (rz::padChainBase + pd)].latency);
    padLatency = juce::jmin (padLatency, maxCompensation / 2);
    int busLatency = 0;
    for (int bi = 1; bi < rz::padChainBase; ++bi) // (the DECK's effects count like a bus)
        busLatency = juce::jmax (busLatency, buses[(size_t) bi].latency);
    busLatency = juce::jmin (busLatency, maxCompensation / 2 - 1);
    // the pads without effects wait for the pads with effects (on every bus), then the pads' chains join them
    for (int bi = 0; bi < rz::numBuses; ++bi)
        busPreDelay[(size_t) bi].process (busBuffers[(size_t) bi].getArrayOfWritePointers(), 2, numSamples, padLatency);
    for (int pd = 0; pd < rz::numPads; ++pd)
    {
        if (! padHasFx[(size_t) pd]) continue;
        auto& chain = buses[(size_t) (rz::padChainBase + pd)];
        auto& pb = padBuffers[(size_t) pd];
        processEffectChain (chain, pb, numSamples);
        chain.compensation.process (pb.getArrayOfWritePointers(), 2, numSamples, padLatency - chain.latency);
        chain.peak = juce::jmax (chain.peak.load() * 0.85f, pb.getMagnitude (0, 0, numSamples));
        auto& target = busBuffers[(size_t) engine.busForPad (pd)];
        for (int ch = 0; ch < 2; ++ch)
            target.addFrom (ch, 0, pb, ch, 0, numSamples);
    }
    const int totalLatency = juce::jmin (maxCompensation - 1, padLatency + busLatency + buses[0].latency);
    pluginLatencySamples = totalLatency;
    sequencer.displayLatencySamples = totalLatency + deviceOutputLatencySamples.load();
    buses[0].compensation.process (masterBus.getArrayOfWritePointers(), 2, numSamples, busLatency);

    for (int bi = 1; bi < rz::numBuses; ++bi)
    {
        auto& bus = buses[(size_t) bi];
        auto& data = busBuffers[(size_t) bi];
        processBusChain (bi, numSamples);
        bus.compensation.process (data.getArrayOfWritePointers(), 2, numSamples, busLatency - bus.latency);
        if (bi == throughBus && inputCopyChannels > 0)
            recorder.capture (rz::AudioRecorder::input, data.getArrayOfReadPointers(), 2, numSamples); // with the bus effects
        const bool silent = bus.mute.load() || (anySolo && ! bus.solo.load());
        const float g = silent ? 0.0f : juce::Decibels::decibelsToGain (bus.gainDb.load(), -59.9f);
        for (int ch = 0; ch < 2; ++ch)
            masterBus.addFromWithRamp (ch, 0, data.getReadPointer (ch), numSamples, bus.lastGain, g);
        bus.lastGain = g;
        bus.peak = juce::jmax (bus.peak.load() * 0.85f, juce::jmax (data.getMagnitude (0, 0, numSamples), data.getMagnitude (1, 0, numSamples)) * g);
    }
    {
        // the DECK through its own effects, lined up like a bus, into MASTER
        auto& db = buses[(size_t) rz::deckChain];
        processEffectChain (db, deckBuffer, numSamples);
        deck.recordBlock (deckBuffer, numSamples); // REC takes the scratch with its effects
        db.compensation.process (deckBuffer.getArrayOfWritePointers(), 2, numSamples, padLatency + busLatency - db.latency);
        for (int ch = 0; ch < 2; ++ch)
            masterBus.addFrom (ch, 0, deckBuffer, ch, 0, numSamples);
    }
    processBusChain (0, numSamples);
    if (throughBus == 0 && inputCopyChannels > 0)
        recorder.capture (rz::AudioRecorder::input, masterBus.getArrayOfReadPointers(), 2, numSamples);
    {
        auto& mb = buses[0];
        const float master = mb.mute.load() ? 0.0f : juce::Decibels::decibelsToGain (pMaster->load(), -59.9f);
        for (int ch = 0; ch < outChannels; ++ch)
            buffer.addFromWithRamp (ch, 0, masterBus.getReadPointer (ch), numSamples, mb.lastGain, master);
        mb.lastGain = master;
        mb.peak = juce::jmax (mb.peak.load() * 0.85f, buffer.getMagnitude (0, numSamples));
    }

    // ---- resampling, metronome ----
    masterFx.reportPeak (buffer, outChannels, numSamples); // (the built-in master effects are gone: the mixer has plug-ins now)
    recorder.capture (rz::AudioRecorder::output, buffer.getArrayOfReadPointers(), outChannels, numSamples);
    // monitor: hear the input (not when it already plays through a bus)
    if (monitorInput.load() && throughBus < 0 && inputCopyChannels > 0)
        for (int ch = 0; ch < outChannels; ++ch)
            buffer.addFrom (ch, 0, inputCopy, ch, 0, numSamples);

    // metronome: rendered on its own, then delayed like the music (plug-in delay compensation)
    if (clickBuffer.getNumSamples() < numSamples)
        clickBuffer.setSize (1, numSamples, false, false, true); // only if the host sends a bigger block than promised
    clickBuffer.clear (0, 0, numSamples);
    if (sequencer.numClicks > 0 || clickSamplesLeft > 0)
    {
        const float level = juce::jlimit (0.0f, 1.0f, sequencer.metronomeLevel.load()) * 0.5f;
        auto* cb = clickBuffer.getWritePointer (0);
        int ci = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            while (ci < sequencer.numClicks && sequencer.clicks[(size_t) ci].offset <= i)
            {
                clickSamplesLeft = clickLength;
                clickPhase = 0.0;
                clickFreq = sequencer.clicks[(size_t) ci].downbeat ? 1760.0 : 1175.0; // A6 on the 1, D6 on the other beats
                ++ci;
            }
            if (clickSamplesLeft <= 0) continue;
            const float env = (float) clickSamplesLeft / (float) clickLength;
            cb[i] = (float) std::sin (clickPhase) * env * env * level;
            clickPhase += juce::MathConstants<double>::twoPi * clickFreq / currentSampleRate;
            --clickSamplesLeft;
        }
    }
    {
        float* ch[2] = { clickBuffer.getWritePointer (0), clickBuffer.getWritePointer (0) };
        clickDelay.process (ch, 1, numSamples, totalLatency);
        for (int c = 0; c < outChannels; ++c)
            buffer.addFrom (c, 0, clickBuffer, 0, 0, numSamples);
    }
    sampleClock += numSamples; // (MIDI clock in measures its ticks against it)
}

//==============================================================================
juce::File RezalicaProcessor::getRecordingFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rezalica").getChildFile ("Recordings");
}

bool RezalicaProcessor::startComputerAudio()
{
    if (loopback.isRunning()) return true;
    loopback.onAudio = [this] (const float* l, const float* r, int n)
    {
        const float* chans[] { l, r };
        recorder.capture (rz::AudioRecorder::computer, chans, 2, n); // runs on the capture thread
    };
    return loopback.start();
}

void RezalicaProcessor::stopComputerAudio()
{
    loopback.stop();
}

bool RezalicaProcessor::startRecording (rz::AudioRecorder::Source source, bool patternLength, bool waitForSignal)
{
    if (recorder.isBusy()) return false;
    if (source == rz::AudioRecorder::computer)
    {
        if (! startComputerAudio()) return false;
        const double lr = loopback.getSampleRate();
        recorder.start (source, (int) (lr * 600.0), waitForSignal, false, lr); // the take keeps the PC's sample rate
        return true;
    }
    if (source == rz::AudioRecorder::input && ! hasAudioInput()) return false;
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    if (source == rz::AudioRecorder::output && patternLength)
    {
        // one loop of the selected pattern, from its first step
        const double bpm = juce::jmax (20.0f, sequencer.syncToHost.load() ? sequencer.effectiveBpm.load() : sequencer.bpm.load());
        const int len = juce::jlimit (1, rz::Sequencer::maxSteps, sequencer.length[(size_t) sequencer.currentPattern.load()].load());
        const int samples = (int) std::llround (len * rz::Sequencer::stepLengthPpq * 60.0 / bpm * sr);
        recorder.start (source, samples, false, true, sr);
        sequencer.syncToHost = false;
        sequencer.playPattern();
        return true;
    }
    recorder.start (source, (int) (sr * 600.0), waitForSignal, false, sr); // up to 10 minutes
    return true;
}

bool RezalicaProcessor::startRecording (const RecordOptions& o)
{
    if (recorder.isBusy()) return false;
    discardTake();
    recorder.inputMode = o.inputMode;
    recorder.setThresholdDb (o.thresholdDb);
    recorder.clipped = false;
    if (o.source == rz::AudioRecorder::computer)
    {
        if (! startComputerAudio()) return false;
        const double lr = loopback.getSampleRate();
        const int n = (int) (lr * (o.lengthSeconds > 0.0 ? o.lengthSeconds : 600.0));
        recorder.start (o.source, n, o.waitForSignal, false, lr); // the take keeps the PC's sample rate
        return true;
    }
    if (o.source == rz::AudioRecorder::input && ! hasAudioInput()) return false;
    if (o.source == rz::AudioRecorder::output && o.patternLength)
        return startRecording (o.source, true, false);
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const int n = (int) (sr * (o.lengthSeconds > 0.0 ? o.lengthSeconds : 600.0));
    recorder.start (o.source, n, o.source != rz::AudioRecorder::output && o.waitForSignal, false, sr);
    return true;
}

bool RezalicaProcessor::fetchTake()
{
    juce::AudioBuffer<float> take;
    double sr = 44100.0;
    pendingTakeSource = recorder.getSource();
    if (! recorder.takeRecording (take, sr))
        return false;
    pendingTake = std::move (take);
    pendingTakeRate = sr;
    return true;
}

void RezalicaProcessor::previewTake()
{
    engine.stopPreview();
    if (! hasTake()) return;
    previewData = new rz::SampleData();
    previewData->buffer = pendingTake;
    previewData->sampleRate = pendingTakeRate;
    previewData->name = "take";
    samplePool.add (previewData);
    engine.setPreviewSample (previewData);
    engine.startPreview();
}

void RezalicaProcessor::discardTake()
{
    engine.stopPreview();
    pendingTake.setSize (2, 0);
}

juce::File RezalicaProcessor::keepTake (int pad, const juce::String& name, bool trimSilence, int chopModeAfter)
{
    if (! hasTake()) return {};
    engine.stopPreview();
    auto take = pendingTake;
    if (trimSilence)
    {
        // cut what is quieter than -50 dB at both ends (keep 5 ms before the first sound)
        const float floor = juce::Decibels::decibelsToGain (-50.0f);
        const int n = take.getNumSamples();
        int first = 0, last = n - 1;
        auto loud = [&] (int i) { return std::abs (take.getSample (0, i)) > floor || std::abs (take.getSample (1, i)) > floor; };
        while (first < n && ! loud (first)) ++first;
        while (last > first && ! loud (last)) --last;
        if (first < n)
        {
            first = juce::jmax (0, first - (int) (pendingTakeRate * 0.005));
            last = juce::jmin (n - 1, last + (int) (pendingTakeRate * 0.02));
            juce::AudioBuffer<float> t (2, last - first + 1);
            for (int ch = 0; ch < 2; ++ch) t.copyFrom (ch, 0, take, ch, first, t.getNumSamples());
            take = std::move (t);
        }
    }
    auto folder = getRecordingFolder();
    folder.createDirectory();
    const auto base = juce::File::createLegalFileName (name.trim()).isNotEmpty() ? juce::File::createLegalFileName (name.trim())
        : (pendingTakeSource == rz::AudioRecorder::input ? juce::String ("Sample ") : pendingTakeSource == rz::AudioRecorder::computer ? juce::String ("Computer ") : juce::String ("Resample "))
              + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    auto file = folder.getNonexistentChildFile (base, ".wav", false);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
        if (os == nullptr) return {};
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), pendingTakeRate, 2, 24, {}, 0));
        if (w == nullptr) return {};
        os.release();
        if (! w->writeFromAudioSampleBuffer (take, 0, take.getNumSamples())) return {};
    }
    noteSessionFile (file);
    pad = juce::jlimit (0, rz::numPads - 1, pad);
    pushUndo ("Record to " + rz::padLabel (pad));
    selectedPad = pad;
    if (! loadFileForChopping (file)) return {};
    if (chopModeAfter != chopOff)
        chopPadSample (pad, chopModeAfter);
    pendingTake.setSize (2, 0);
    return file;
}

bool RezalicaProcessor::loadFileToDeck (const juce::File& file)
{
    auto s = loadSample (file, true);
    if (s == nullptr) return false;
    deck.setRecord (s);
    deck.recordBpm = rz::Turntable::guessBpm (*s);
    deck.motorOn = true; // dropped on the deck: it starts turning
    return true;
}

bool RezalicaProcessor::loadPadToDeck (int pad)
{
    auto s = engine.getPadSample (juce::jlimit (0, rz::numPads - 1, pad));
    if (s == nullptr) return false;
    deck.setRecord (s);
    deck.recordBpm = rz::Turntable::guessBpm (*s);
    deck.motorOn = true;
    return true;
}

juce::File RezalicaProcessor::collectDeckRecording (int pad)
{
    juce::AudioBuffer<float> take;
    double sr = 44100.0;
    if (! deck.takeRecording (take, sr))
        return {};
    pendingTake = std::move (take);
    pendingTakeRate = sr;
    pendingTakeSource = rz::AudioRecorder::output;
    return keepTake (pad, "Deck scratch " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), true, chopOff);
}

juce::File RezalicaProcessor::collectRecording (int pad)
{
    juce::AudioBuffer<float> take;
    double sr = 44100.0;
    if (! recorder.takeRecording (take, sr))
        return {};

    auto folder = getRecordingFolder();
    folder.createDirectory();
    auto file = folder.getNonexistentChildFile ((recorder.getSource() == rz::AudioRecorder::input ? "Sample "
                                                 : recorder.getSource() == rz::AudioRecorder::computer ? "Computer " : "Resample ")
                                                + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav", false);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
        if (os == nullptr) return {};
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
        if (w == nullptr) return {};
        os.release();
        if (! w->writeFromAudioSampleBuffer (take, 0, take.getNumSamples()))
            return {};
    }
    noteSessionFile (file);

    pad = juce::jlimit (0, rz::numPads - 1, pad);
    pushUndo ("Record to " + rz::padLabel (pad));
    const bool ok = pad == selectedPad.load() ? loadFileForChopping (file) : loadFileToPad (pad, file);
    return ok ? file : juce::File();
}

void RezalicaProcessor::previewFile (const juce::File& file)
{
    engine.stopPreview();
    if (! file.existsAsFile() || lazyActive.load() || lazyStarting)
        return;
    previewData = loadSample (file);
    if (previewData == nullptr)
        return;
    engine.setPreviewSample (previewData);
    engine.startPreview();
}

//==============================================================================
void RezalicaProcessor::triggerPadFromUi (int pad, float velocity, bool isNoteOn)
{
    auto writer = uiFifo.write (1);
    writer.forEach ([&] (int i) { uiEvents[(size_t) i] = { pad, velocity, isNoteOn }; });
}

void RezalicaProcessor::previewNoteFromUi (int pad, float velocity, float transpose, bool isNoteOn, bool recordIt)
{
    auto writer = uiFifo.write (1);
    writer.forEach ([&] (int i) { uiEvents[(size_t) i] = { pad, velocity, isNoteOn, true, transpose, recordIt }; });
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

    // the slices of one chop cut each other (like an MPC / Koala chop): they share a choke group.
    // Keep the group the first slice already has, else take the lowest group no other pad uses.
    int chopGroup = 0;
    if (n > 1)
    {
        chopGroup = engine.getPad (0).chokeGroup.load();
        if (chopGroup <= 0)
        {
            std::array<bool, 9> used {};
            for (int pad = n; pad < rz::numPads; ++pad)
                used[(size_t) juce::jlimit (0, 8, engine.getPad (pad).chokeGroup.load())] = true;
            chopGroup = 1;
            while (chopGroup < 8 && used[(size_t) chopGroup]) ++chopGroup;
        }
    }

    for (int pad = 0; pad < rz::numPads; ++pad)
    {
        const int k = pad;
        if (k < n)
        {
            auto& ps = engine.getPad (pad);
            ps.start = slicePoints[(size_t) k];
            ps.end = (k + 1 < n) ? slicePoints[(size_t) k + 1] : 1.0f;
            ps.chokeGroup = chopGroup;
            engine.setPadSample (pad, chopSource);
            const juce::ScopedLock sl (missingPathLock);
            missingPaths[(size_t) pad] = {};
        }
        else if (const auto old = engine.getPadSample (pad); old != nullptr && (old == chopSource || old == previousChopSource))
        {
            engine.setPadSample (pad, nullptr); // leftover slice from a previous chop
            if (chopGroup > 0 && engine.getPad (pad).chokeGroup.load() == chopGroup)
                engine.getPad (pad).chokeGroup = 0;
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
    // like on hardware samplers: silence until the first hit; that hit starts the sample and sets the first marker
    lazyWaiting = true;
    lazyStarting = false;
    lazyActive = true;
    return true;
}

void RezalicaProcessor::stopLazyChop()
{
    lazyWaiting = false;
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
    {
        s->file = file;
        noteSessionFile (file);
    }

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

double RezalicaProcessor::normalizePad (int pad, float targetDb)
{
    auto src = engine.getPadSample (pad);
    if (src == nullptr)
        return std::numeric_limits<double>::quiet_NaN();

    // the part that plays (between the markers; for a warped pad the stretched audio)
    auto& ps = engine.getPad (pad);
    const int len = src->buffer.getNumSamples();
    float st = ps.start.load(), en = ps.end.load();
    if (en < st) std::swap (st, en);
    const int a = juce::jlimit (0, len - 1, (int) (st * (float) len));
    const int b = juce::jlimit (a + 1, len, (int) (en * (float) len));

    // peak normalization: the loudest single peak, one gain for everything (the dynamics stay the same)
    float peak = 0.0f;
    for (int ch = 0; ch < src->buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, src->buffer.getMagnitude (ch, a, b - a));
    if (peak < 1.0e-6f)
        return std::numeric_limits<double>::quiet_NaN(); // silence: nothing to normalize
    const float gain = juce::Decibels::decibelsToGain (targetDb) / peak;

    // then BOUNCE: the result becomes a new WAV of just that part ...
    juce::AudioBuffer<float> out (2, b - a);
    for (int ch = 0; ch < 2; ++ch)
        out.copyFrom (ch, 0, src->buffer, juce::jmin (ch, src->buffer.getNumChannels() - 1), a, b - a);
    out.applyGain (gain);
    auto base = src->name.replace ("[", "").replace ("]", "").trim();
    if (! base.endsWith (" norm")) base += " norm";
    auto folder = getRenderFolder();
    folder.createDirectory();
    auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (base), ".wav", false);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
        if (os == nullptr) return std::numeric_limits<double>::quiet_NaN();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), src->sampleRate, 2, 24, {}, 0));
        if (w == nullptr) return std::numeric_limits<double>::quiet_NaN();
        os.release();
        if (! w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples())) return std::numeric_limits<double>::quiet_NaN();
    }
    noteSessionFile (file);

    // ... loaded back onto the pad and into the sample view, ready for more work (chop, warp, trim ...), like Ableton
    selectedPad = juce::jlimit (0, rz::numPads - 1, pad);
    if (! loadFileForChopping (file))
        return std::numeric_limits<double>::quiet_NaN();
    return juce::Decibels::gainToDecibels (gain);
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
    // the record on the deck now belongs to the saved project (it stays there after a restart)
    if (auto r = deck.getRecord(); r != nullptr && r->file.existsAsFile())
        savedDeckFile = r->file.getFullPathName();
    juce::MemoryBlock state;
    getStateInformation (state);
    auto xml = getXmlFromBinary (state.getData(), (int) state.getSize());
    if (xml == nullptr)
        return false;
    file.getParentDirectory().createDirectory();
    if (! xml->writeTo (file))
        return false;
    currentProjectFile = file;
    markProjectSaved();
    forgetSessionFilesUsedBy (*xml); // a saved project uses them: never offered for deleting
    return true;
}

//==============================================================================
void RezalicaProcessor::noteSessionFile (const juce::File& f)
{
    const juce::ScopedLock sl (sessionFilesLock);
    sessionFiles.addIfNotAlreadyThere (f);
}

void RezalicaProcessor::forgetSessionFilesUsedBy (const juce::XmlElement& project)
{
    juce::StringArray used;
    std::function<void (const juce::XmlElement&)> walk = [&] (const juce::XmlElement& e)
    {
        for (int i = 0; i < e.getNumAttributes(); ++i)
            used.add (e.getAttributeValue (i));
        for (auto* c : e.getChildIterator())
            walk (*c);
    };
    walk (project);
    const juce::ScopedLock sl (sessionFilesLock);
    for (int i = sessionFiles.size(); --i >= 0;)
        if (used.contains (sessionFiles.getReference (i).getFullPathName()))
            sessionFiles.remove (i);
}

juce::Array<juce::File> RezalicaProcessor::getUnsavedSessionFiles() const
{
    const juce::ScopedLock sl (sessionFilesLock);
    juce::Array<juce::File> r;
    for (auto& f : sessionFiles)
        if (f.existsAsFile())
            r.add (f);
    return r;
}

int RezalicaProcessor::trashSessionFiles()
{
    juce::Array<juce::File> files;
    {
        const juce::ScopedLock sl (sessionFilesLock);
        files = sessionFiles;
        sessionFiles.clear();
    }
    int n = 0;
    for (auto& f : files)
        if (f.existsAsFile() && (f.moveToTrash() || f.deleteFile()))
            ++n;
    return n;
}

void RezalicaProcessor::forgetSessionFiles()
{
    const juce::ScopedLock sl (sessionFilesLock);
    sessionFiles.clear();
}

juce::int64 RezalicaProcessor::projectFingerprint()
{
    juce::MemoryBlock state;
    getStateInformation (state);
    auto xml = getXmlFromBinary (state.getData(), (int) state.getSize());
    if (xml == nullptr) return 0;
    // what you look at is not a change of the project
    for (auto* a : { "selectedPad", "bank", "chromaticPad" }) xml->removeAttribute (a);
    if (auto* seq = xml->getChildByName ("SEQ")) seq->removeAttribute ("pattern");
    return xml->toString().hashCode64();
}

bool RezalicaProcessor::hasUnsavedChanges() { return projectFingerprint() != savedFingerprint; }
void RezalicaProcessor::markProjectSaved()   { savedFingerprint = projectFingerprint(); }

bool RezalicaProcessor::loadProjectFromFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("REZALICA"))
        return false;
    juce::MemoryBlock state;
    copyXmlToBinary (*xml, state);
    stopAll();
    restoringProject = true;
    setStateInformation (state.getData(), (int) state.getSize());
    restoringProject = false;
    undoStack.clear();
    redoStack.clear();
    currentProjectFile = file;
    markProjectSaved();
    return true;
}

void RezalicaProcessor::newProject()
{
    auto fresh = std::make_unique<RezalicaProcessor> (false);
    juce::MemoryBlock state;
    fresh->getStateInformation (state);
    stopAll();
    setStateInformation (state.getData(), (int) state.getSize());
    undoStack.clear();
    redoStack.clear();
    currentProjectFile = juce::File();
    markProjectSaved();
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
                    addNote (midiBaseNote + pad, st + sequencer.swingOffsetSteps (pattern, st) + sequencer.getNudge (pattern, pad, st) * 0.01, 0.9, v);

    if (pianoPart)
        for (auto& n : sequencer.getClip (pattern)->notes)
            if (n.start < (float) len)
                addNote (n.kind == rz::NoteClip::melodic ? n.row : midiBaseNote + n.row,
                         n.start + (n.start == std::floor (n.start) ? sequencer.swingOffsetSteps (pattern, (long long) n.start) : 0.0), n.length, n.velocity);

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
    auto offlinePtr = std::make_unique<RezalicaProcessor> (false); // on the heap: the processor is large
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
    seq.metronome = false;
    seq.recording = false;
    seq.pianoRecording = false;
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
                    addNote (midiBaseNote + pad, stp + sequencer.swingOffsetSteps (sg.pattern, stp) + sequencer.getNudge (sg.pattern, pad, stp) * 0.01, 0.9, v);
        for (auto& note : sequencer.getClip (sg.pattern)->notes)
            if (note.start < (float) len)
                addNote (note.kind == rz::NoteClip::melodic ? note.row : midiBaseNote + note.row,
                         note.start + (note.start == std::floor (note.start) ? sequencer.swingOffsetSteps (sg.pattern, (long long) note.start) : 0.0),
                         note.length, note.velocity);
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

juce::AudioBuffer<float> RezalicaProcessor::renderSong (double sr, double tailSeconds)
{
    std::vector<rz::Sequencer::SongSegment> segs;
    double total = 0.0;
    if (sequencer.buildSong (segs, total) == 0)
        return {};

    const double bpm = juce::jmax (20.0f, sequencer.effectiveBpm.load());
    if (sr <= 0.0) sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    const int songSamples = (int) std::llround (total * 60.0 / bpm * sr);
    const int tailSamples = (int) (sr * juce::jmax (0.0, tailSeconds)); // let reverb / delay ring out at the end

    juce::MemoryBlock state;
    getStateInformation (state);
    auto offlinePtr = std::make_unique<RezalicaProcessor> (false); // on the heap: the processor is large
    auto& offline = *offlinePtr;
    offline.setStateInformation (state.getData(), (int) state.getSize());
    auto& seq = offline.getSequencer();
    seq.songMode = true;
    seq.songLoop = false;
    seq.songStartStep = 0;     // the whole song
    seq.metronome = false;
    seq.recording = false;
    seq.pianoRecording = false;
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
    // the mixer's plug-ins delay everything by the same (compensated) amount: drop that from the start
    int skip = -1, written = 0;
    for (long long pos = 0; written < totalSamples; pos += block)
    {
        juce::AudioBuffer<float> sub (buf.getArrayOfWritePointers(), 2, block);
        offline.processBlock (sub, noMidi);
        noMidi.clear();
        if (skip < 0)
            skip = juce::jmax (0, offline.pluginLatencySamples.load());
        const int from = (int) juce::jlimit ((long long) 0, (long long) block, (long long) skip - pos); // first sample of this block to keep
        const int n = juce::jmin (block - from, totalSamples - written);
        for (int ch = 0; ch < 2 && n > 0; ++ch)
            out.copyFrom (ch, written, sub, ch, from, n);
        written += juce::jmax (0, n);
    }
    return out;
}

bool RezalicaProcessor::exportSongWav (const juce::File& file)
{
    ExportSettings s; // 24-bit WAV at the current sample rate, 3 s tail
    juce::String err;
    return exportSong (file, s, err);
}

bool RezalicaProcessor::exportSong (const juce::File& file, const ExportSettings& es, juce::String& error)
{
    const double sr = es.sampleRate > 0.0 ? es.sampleRate : (currentSampleRate > 0.0 ? currentSampleRate : 44100.0);
    auto audio = renderSong (sr, es.tailSeconds);
    if (audio.getNumSamples() == 0) { error = "The song is empty"; return false; }

    // mono: both channels summed
    int chans = 2;
    if (es.mono)
    {
        audio.addFrom (0, 0, audio, 1, 0, audio.getNumSamples());
        audio.applyGain (0, 0, audio.getNumSamples(), 0.5f);
        chans = 1;
    }
    // normalize: the loudest peak at -0.3 dBFS
    if (es.normalize)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < chans; ++ch) peak = juce::jmax (peak, audio.getMagnitude (ch, 0, audio.getNumSamples()));
        if (peak > 1.0e-6f) audio.applyGain (juce::Decibels::decibelsToGain (-0.3f) / peak);
    }
    juce::AudioBuffer<float> outAudio (audio.getArrayOfWritePointers(), chans, audio.getNumSamples());

    if (es.format == ExportSettings::mp3)
        return rz::Mp3Writer::write (file, outAudio, sr, es.mp3Kbps, es.title, es.artist, error);

    // WAV: 16-bit gets TPDF dither (when asked), 32 = float
    if (es.bitDepth == 16 && es.dither)
    {
        juce::Random rnd (1234);
        const float lsb = 1.0f / 32768.0f;
        for (int ch = 0; ch < chans; ++ch)
        {
            float* d = outAudio.getWritePointer (ch);
            for (int i = 0; i < outAudio.getNumSamples(); ++i)
                d[i] += (rnd.nextFloat() - rnd.nextFloat()) * lsb;
        }
    }
    file.deleteFile();
    juce::StringPairArray meta;
    if (es.title.isNotEmpty())  meta.set (juce::WavAudioFormat::riffInfoTitle, es.title);
    if (es.artist.isNotEmpty()) meta.set (juce::WavAudioFormat::riffInfoArtist, es.artist);
    meta.set (juce::WavAudioFormat::riffInfoSoftware, "Rezalica");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os == nullptr) { error = "The file cannot be written"; return false; }
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned int) chans, juce::jlimit (16, 32, es.bitDepth), meta, 0));
    if (w == nullptr) { error = "This WAV format is not supported"; return false; }
    os.release();
    if (! w->writeFromAudioSampleBuffer (outAudio, 0, outAudio.getNumSamples())) { error = "Writing failed"; return false; }
    return true;
}

juce::File RezalicaProcessor::getRenderFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rezalica").getChildFile ("Bounced");
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
    s->name = warped->name.replace ("[", "").replace ("]", "") + " bounce";

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
    noteSessionFile (file);
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
    noteSessionFile (file);

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
    t.setProperty ("choke", ps.chokeGroup.load(), nullptr);
    t.setProperty ("bus", ps.outputBus.load(), nullptr);
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
    ps.chokeGroup = (int) t.getProperty ("choke", 0);
    ps.outputBus = juce::jlimit (-1, rz::numBuses - 1, (int) t.getProperty ("bus", -1));
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
//==============================================================================
void RezalicaProcessor::prepareSlot (rz::MixerSlot& slot)
{
    auto* p = slot.owned.get();
    if (p == nullptr) return;
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    if (auto* inst = dynamic_cast<juce::AudioPluginInstance*> (p))
    {
        // stereo in / out when the plug-in can do it
        juce::AudioProcessor::BusesLayout stereo;
        stereo.inputBuses.add (juce::AudioChannelSet::stereo());
        stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        if (inst->checkBusesLayoutSupported (stereo))
            inst->setBusesLayout (stereo);
    }
    p->setRateAndBufferSizeDetails (sr, preparedBlockSize);
    p->prepareToPlay (sr, preparedBlockSize);
    const int chans = juce::jmax (2, p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
    slot.scratch.setSize (chans, juce::jmax (preparedBlockSize, 512));
    slot.midi.ensureSize (256);
}

void RezalicaProcessor::processBusChain (int busIndex, int numSamples)
{
    processEffectChain (buses[(size_t) busIndex], busBuffers[(size_t) busIndex], numSamples);
}

void RezalicaProcessor::processEffectChain (rz::MixerBus& bus, juce::AudioBuffer<float>& data, int numSamples)
{
    const juce::SpinLock::ScopedTryLockType tl (bus.lock);
    if (! tl.isLocked()) return; // the slots are being changed right now
    for (auto& slot : bus.slots)
    {
        auto* p = slot.live;
        if (p == nullptr || slot.bypass.load()) continue;
        auto& sc = slot.scratch;
        const int cap = sc.getNumSamples();
        for (int pos = 0; pos < numSamples; pos += cap)
        {
            const int n = juce::jmin (cap, numSamples - pos);
            juce::AudioBuffer<float> sub (sc.getArrayOfWritePointers(), sc.getNumChannels(), n);
            sub.clear();
            for (int ch = 0; ch < 2; ++ch)
                sub.copyFrom (ch, 0, data, ch, pos, n);
            slot.midi.clear();
            p->processBlock (sub, slot.midi);
            for (int ch = 0; ch < 2; ++ch)
                data.copyFrom (ch, pos, sub, juce::jmin (ch, sub.getNumChannels() - 1), 0, n);
        }
    }
}

void RezalicaProcessor::setSlotProcessor (int busIndex, int slotIndex, std::unique_ptr<juce::AudioProcessor> p, const juce::PluginDescription* desc)
{
    auto& bus = getBus (busIndex);
    auto& slot = bus.slots[(size_t) juce::jlimit (0, rz::MixerBus::maxSlots - 1, slotIndex)];
    for (auto& [owner, closeEditors] : pluginsAboutToChange) if (closeEditors) closeEditors();
    std::unique_ptr<juce::AudioProcessor> old;
    {
        // prepare the new one first, then swap it in while the audio thread is not using the slots
        auto fresh = std::move (p);
        if (fresh != nullptr && bus.compensation.line.getNumSamples() < 4)
        {
            // a pad's first effect: its delay line (for the plug-in delay compensation)
            const juce::SpinLock::ScopedLockType sl (bus.lock);
            bus.compensation.prepare (maxCompensation);
        }
        if (fresh != nullptr)
        {
            slot.scratch.setSize (2, 1); // replaced in prepareSlot
            std::swap (slot.owned, fresh);
            prepareSlot (slot);
            std::swap (slot.owned, fresh);
        }
        const juce::SpinLock::ScopedLockType sl (bus.lock);
        old = std::move (slot.owned);
        slot.owned = std::move (fresh);
        slot.live = slot.owned.get();
        slot.hasDesc = desc != nullptr;
        if (desc != nullptr) slot.desc = *desc;
        slot.bypass = false;
    }
    if (old != nullptr) old->releaseResources();
    old = nullptr;
    mixerChanged.sendChangeMessage();
}

bool RezalicaProcessor::loadPluginIntoSlot (int busIndex, int slotIndex, const juce::PluginDescription& desc, juce::String& error)
{
    auto inst = pluginHost->createInstance (desc, currentSampleRate > 0.0 ? currentSampleRate : 44100.0, preparedBlockSize, error);
    if (inst == nullptr)
    {
        if (error.isEmpty()) error = "The plug-in could not be loaded";
        return false;
    }
    setSlotProcessor (busIndex, slotIndex, std::move (inst), &desc);
    return true;
}

void RezalicaProcessor::clearSlot (int busIndex, int slotIndex)
{
    setSlotProcessor (busIndex, slotIndex, nullptr, nullptr);
}

juce::AudioProcessor* RezalicaProcessor::getSlotProcessor (int busIndex, int slotIndex) const
{
    return buses[(size_t) juce::jlimit (0, rz::numEffectChains - 1, busIndex)].slots[(size_t) juce::jlimit (0, rz::MixerBus::maxSlots - 1, slotIndex)].owned.get();
}

juce::String RezalicaProcessor::getSlotName (int busIndex, int slotIndex) const
{
    if (auto* p = getSlotProcessor (busIndex, slotIndex)) return p->getName();
    return {};
}

juce::ValueTree RezalicaProcessor::mixerToValueTree() const
{
    juce::ValueTree m ("MIXER");
    for (int b = 0; b < rz::numBanks; ++b)
        m.setProperty ("bankBus" + juce::String (b), engine.bankBus[(size_t) b].load(), nullptr);
    for (int bi = 0; bi < rz::numEffectChains; ++bi)
    {
        auto& bus = buses[(size_t) bi];
        juce::ValueTree t ("BUS");
        t.setProperty ("index", bi, nullptr);
        t.setProperty ("gain", bus.gainDb.load(), nullptr);
        t.setProperty ("mute", bus.mute.load(), nullptr);
        t.setProperty ("solo", bus.solo.load(), nullptr);
        for (int si = 0; si < rz::MixerBus::maxSlots; ++si)
        {
            auto& slot = bus.slots[(size_t) si];
            if (slot.owned == nullptr || ! slot.hasDesc) continue;
            juce::ValueTree st ("SLOT");
            st.setProperty ("index", si, nullptr);
            st.setProperty ("bypass", slot.bypass.load(), nullptr);
            if (auto xml = slot.desc.createXml())
                st.appendChild (juce::ValueTree::fromXml (*xml), nullptr);
            juce::MemoryBlock mb;
            slot.owned->getStateInformation (mb);
            st.setProperty ("state", mb.toBase64Encoding(), nullptr);
            t.appendChild (st, nullptr);
        }
        m.appendChild (t, nullptr);
    }
    return m;
}

void RezalicaProcessor::mixerFromValueTree (const juce::ValueTree& m)
{
    for (int bi = 0; bi < rz::numEffectChains; ++bi)
        for (int si = 0; si < rz::MixerBus::maxSlots; ++si)
            if (buses[(size_t) bi].slots[(size_t) si].owned != nullptr)
                clearSlot (bi, si);
    for (int b = 0; b < rz::numBanks; ++b)
        engine.bankBus[(size_t) b] = juce::jlimit (0, rz::numBuses - 1, (int) m.getProperty ("bankBus" + juce::String (b), 0));
    for (auto& bus : buses) { bus.gainDb = 0.0f; bus.mute = false; bus.solo = false; }
    if (! m.isValid()) return;
    for (auto t : m)
    {
        const int bi = t.getProperty ("index", -1);
        if (bi < 0 || bi >= rz::numEffectChains) continue;
        auto& bus = buses[(size_t) bi];
        bus.gainDb = (float) t.getProperty ("gain", 0.0f);
        bus.mute = (bool) t.getProperty ("mute", false);
        bus.solo = (bool) t.getProperty ("solo", false);
        for (auto st : t)
        {
            const int si = st.getProperty ("index", -1);
            if (si < 0 || si >= rz::MixerBus::maxSlots || st.getNumChildren() == 0) continue;
            juce::PluginDescription d;
            if (auto xml = st.getChild (0).createXml(); xml == nullptr || ! d.loadFromXml (*xml)) continue;
            juce::String err;
            if (! loadPluginIntoSlot (bi, si, d, err)) continue; // plug-in missing on this computer
            juce::MemoryBlock mb;
            mb.fromBase64Encoding (st.getProperty ("state").toString());
            if (auto* p = getSlotProcessor (bi, si); p != nullptr && mb.getSize() > 0)
                p->setStateInformation (mb.getData(), (int) mb.getSize());
            bus.slots[(size_t) si].bypass = (bool) st.getProperty ("bypass", false);
        }
    }
}

void RezalicaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("REZALICA");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("selectedPad", selectedPad.load(), nullptr);
    root.setProperty ("chromaticPad", chromaticPad.load(), nullptr);
    root.setProperty ("scaleRoot", scaleRoot.load(), nullptr);
    root.setProperty ("scaleType", scaleType.load(), nullptr);
    root.setProperty ("chordType", chordType.load(), nullptr);
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
        p.setProperty ("choke", ps.chokeGroup.load(), nullptr);
        p.setProperty ("bus", ps.outputBus.load(), nullptr);
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
    seq.setProperty ("metronome", sequencer.metronome.load(), nullptr);
    seq.setProperty ("countIn", sequencer.countInBars.load(), nullptr);
    seq.setProperty ("clickLevel", sequencer.metronomeLevel.load(), nullptr);
    for (int i = 0; i < rz::Sequencer::numPatterns; ++i)
    {
        juce::ValueTree pat ("PATTERN");
        pat.setProperty ("index", i, nullptr);
        pat.setProperty ("length", sequencer.length[(size_t) i].load(), nullptr);
        pat.setProperty ("steps", sequencer.getPatternData (i).toBase64Encoding(), nullptr);
        pat.setProperty ("stride", rz::Sequencer::maxSteps, nullptr);
        pat.setProperty ("name", sequencer.patternNames[(size_t) i], nullptr);
        pat.setProperty ("colour", (juce::int64) sequencer.patternColours[(size_t) i], nullptr);
        pat.setProperty ("swing", sequencer.patternSwing[(size_t) i].load(), nullptr);
        pat.setProperty ("swingGrid", sequencer.swingGrid[(size_t) i].load(), nullptr);
        pat.setProperty ("nudge", sequencer.getNudgeData (i).toBase64Encoding(), nullptr);
        pat.appendChild (sequencer.getClip (i)->toValueTree(), nullptr);
        seq.appendChild (pat, nullptr);
    }
    root.appendChild (seq, nullptr);
    root.appendChild (sequencer.songToValueTree(), nullptr);
    root.appendChild (mixerToValueTree(), nullptr);
    {
        juce::ValueTree d ("DECK");
        if (auto r = deck.getRecord(); r != nullptr && r->file.existsAsFile())
        {
            d.setProperty ("file", r->file.getFullPathName(), nullptr);
            // the record only stays on the deck after a restart when it is the one saved with the project
            d.setProperty ("savedWithProject", r->file.getFullPathName() == savedDeckFile, nullptr);
        }
        d.setProperty ("bpm", deck.recordBpm.load(), nullptr);
        d.setProperty ("sync", deck.sync.load(), nullptr);
        d.setProperty ("pitch", deck.pitch.load(), nullptr);
        d.setProperty ("range", deck.pitchRange.load(), nullptr);
        d.setProperty ("rpm45", deck.rpm45.load(), nullptr);
        d.setProperty ("loop", deck.loop.load(), nullptr);
        d.setProperty ("reverse", deck.reverse.load(), nullptr);
        d.setProperty ("volume", deck.volume.load(), nullptr);
        d.setProperty ("hamster", deck.reverseFader.load(), nullptr);
        root.appendChild (d, nullptr);
    }

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
    scaleRoot = juce::jlimit (0, 11, (int) root.getProperty ("scaleRoot", 0));
    scaleType = juce::jlimit (0, (int) rz::scales::all().size() - 1, (int) root.getProperty ("scaleType", 0));
    chordType = juce::jlimit (0, rz::scales::numChords - 1, (int) root.getProperty ("chordType", 0));
    currentBank = juce::jlimit (0, rz::numBanks - 1, (int) root.getProperty ("bank", 0));
    for (int b = 0; b < rz::numBanks; ++b)
    {
        setBankGainDb (b, (float) root.getProperty ("bankDb" + juce::String (b), 0.0f));
        engine.bankMute[(size_t) b] = (bool) root.getProperty ("bankMute" + juce::String (b), false);
    }

    auto pads = root.getChildWithName ("PADS");
    std::map<int, rz::SampleData::Ptr> restoredById;
    bool olderProject = false; // saved before choke groups existed
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
        ps.chokeGroup = (int) p.getProperty ("choke", 0);
        ps.outputBus = juce::jlimit (-1, rz::numBuses - 1, (int) p.getProperty ("bus", -1));
        olderProject = olderProject || ! p.hasProperty ("choke");
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

    // older projects: the slices of one chop cut each other, as chops do now
    if (olderProject)
    {
        std::array<bool, rz::numPads> done {};
        for (int a = 0; a < rz::numPads; ++a)
        {
            auto s = engine.getPadSample (a);
            if (s == nullptr || done[(size_t) a]) continue;
            std::vector<int> group { a };
            for (int b = a + 1; b < rz::numPads; ++b)
                if (engine.getPadSample (b) == s) group.push_back (b);
            for (int b : group) done[(size_t) b] = true;
            if (group.size() < 2) continue;
            std::array<bool, 9> used {};
            for (int i = 0; i < rz::numPads; ++i) used[(size_t) juce::jlimit (0, 8, engine.getPad (i).chokeGroup.load())] = true;
            int g = 1;
            while (g < 8 && used[(size_t) g]) ++g;
            for (int b : group) engine.getPad (b).chokeGroup = g;
        }
    }

    auto seq = root.getChildWithName ("SEQ");
    if (seq.isValid())
    {
        sequencer.currentPattern = juce::jlimit (0, rz::Sequencer::numPatterns - 1, (int) seq.getProperty ("pattern", 0));
        sequencer.syncToHost = (bool) seq.getProperty ("sync", true);
        sequencer.bpm = (float) seq.getProperty ("bpm", 90.0f);
        const float oldSwing = (float) seq.getProperty ("swing", 0.0f); // older projects: one swing for all patterns
        sequencer.metronome = (bool) seq.getProperty ("metronome", false);
        sequencer.countInBars = (int) seq.getProperty ("countIn", 0);
        sequencer.metronomeLevel = (float) seq.getProperty ("clickLevel", 0.5f);

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
            sequencer.patternColours[(size_t) i] = (juce::uint32) (juce::int64) pat.getProperty ("colour", 0);
            sequencer.patternSwing[(size_t) i] = (float) pat.getProperty ("swing", oldSwing);
            sequencer.swingGrid[(size_t) i] = (int) pat.getProperty ("swingGrid", 0);
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
    mixerFromValueTree (root.getChildWithName ("MIXER"));
    if (auto d = root.getChildWithName ("DECK"); d.isValid())
    {
        // a project that is opened brings its record; the session Rezalica restores at start only does
        // when that record was saved with the project (otherwise the deck starts empty)
        const juce::File f (d.getProperty ("file", juce::String()).toString());
        const bool keep = restoringProject || (bool) d.getProperty ("savedWithProject", false);
        rz::SampleData::Ptr rec;
        if (keep && f.existsAsFile())
            rec = loadSample (f, true);
        deck.setRecord (rec);
        deck.recordBpm = rec != nullptr ? (float) d.getProperty ("bpm", 0.0f) : 0.0f;
        savedDeckFile = rec != nullptr && keep ? f.getFullPathName() : juce::String();
        deck.motorOn = false; // a project opens with the deck standing still
        deck.sync = (bool) d.getProperty ("sync", false);
        deck.pitch = (float) d.getProperty ("pitch", 0.0f);
        deck.pitchRange = (float) d.getProperty ("range", 0.08f);
        deck.rpm45 = (bool) d.getProperty ("rpm45", false);
        deck.loop = (bool) d.getProperty ("loop", true);
        deck.reverse = (bool) d.getProperty ("reverse", false);
        deck.volume = (float) d.getProperty ("volume", 1.0f);
        deck.reverseFader = (bool) d.getProperty ("hamster", false);
    }
    else
    {
        deck.setRecord (nullptr); // a new project: an empty deck
        deck.motorOn = false;
        savedDeckFile = {};
    }

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
