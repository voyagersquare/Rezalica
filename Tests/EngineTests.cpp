// Headless smoke test: chops a generated sample, plays it via MIDI and the sequencer, checks output.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/SceneWindow.h"
#include "../Source/AppSettings.h"
#include "../Source/Mp3Writer.h"
#include "../Source/ExportDialog.h"
#include "../Source/TimeStretch.h"
#include "../Source/MidiControl.h"
#include <iostream>

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << "\n";
    if (! ok) ++failures;
}

static float renderBlocks (RezalicaProcessor& p, int blocks, juce::MidiBuffer midiFirstBlock = {})
{
    juce::AudioBuffer<float> buf (2, 512);
    float peak = 0.0f;
    for (int i = 0; i < blocks; ++i)
    {
        juce::MidiBuffer m;
        if (i == 0) m = midiFirstBlock;
        p.processBlock (buf, m);
        peak = juce::jmax (peak, buf.getMagnitude (0, 0, 512), buf.getMagnitude (1, 0, 512));
    }
    return peak;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String snapshotDir = argc > 2 && juce::String (argv[1]) == "--snapshot" ? juce::String (argv[2]) : juce::String();

    // 2 seconds: 8 clicks with silence between -> easy transients
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_test.wav");
    {
        juce::AudioBuffer<float> b (1, 88200);
        b.clear();
        for (int k = 0; k < 8; ++k)
            for (int i = 0; i < 2000; ++i)
                b.setSample (0, k * 11025 + i, 0.8f * std::sin (i * 0.05f) * (1.0f - i / 2000.0f));
        juce::WavAudioFormat wav;
        file.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), 44100, 1, 16, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    RezalicaProcessor p;
    p.prepareToPlay (48000.0, 512);

    check (renderBlocks (p, 4) < 1.0e-9f, "silent with no samples");

    check (p.chopFileToPads (file, RezalicaProcessor::chopTransients), "load & chop by transients");
    int slicesFound = 0;
    for (int i = 0; i < rz::numPads; ++i)
        if (p.getEngine().getPadSample (i) != nullptr) ++slicesFound;
    std::cout << "       transient slices: " << slicesFound << "\n";
    check (slicesFound == 8, "8 transients detected");
    check (std::abs (p.getEngine().getPad (3).start.load() - 3.0f / 8.0f) < 0.01f, "slice 4 starts near 3/8");

    // Equal chop modes re-chop automatically and clear leftovers
    p.selectedPad = 0;
    p.setChopMode (RezalicaProcessor::chop2);
    check (p.getEngine().getPadSample (1) != nullptr && p.getEngine().getPadSample (2) == nullptr, "auto re-chop to 2 slices clears pads 3+");
    p.setChopMode (RezalicaProcessor::chop16);
    check (p.getNumSlices() == 16 && p.getEngine().getPadSample (15) != nullptr, "auto re-chop to 16 slices");

    // Long sample: many transients spread over banks A-D
    auto longFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_long.wav");
    {
        juce::AudioBuffer<float> b (1, 44100 * 20);
        b.clear();
        for (int k = 0; k < 70; ++k)  // 70 hits, ~0.28 s apart
            for (int i = 0; i < 1500; ++i)
                b.setSample (0, k * 12600 + i, (0.3f + 0.5f * (float) (k % 5) / 4.0f) * std::sin (i * 0.07f) * (1.0f - i / 1500.0f));
        juce::WavAudioFormat wav;
        longFile.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (longFile), 44100, 1, 16, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    p.setTransientSensitivity (0.1f);
    p.chopFileToPads (longFile, RezalicaProcessor::chopTransients);
    const int lowSens = p.getNumSlices();
    p.setTransientSensitivity (1.0f);
    const int highSens = p.getNumSlices();
    std::cout << "       transient slices at 10% / 100% sensitivity: " << lowSens << " / " << highSens << "\n";
    check (highSens == RezalicaProcessor::maxSlices, "max sensitivity gives 64 slices");
    check (lowSens < highSens, "lower sensitivity gives fewer slices");
    check (p.getEngine().getPadSample (63) != nullptr, "slice 64 lands on pad D16");

    juce::MidiBuffer bankMidi;
    bankMidi.addEvent (juce::MidiMessage::noteOn (1, RezalicaProcessor::midiBaseNote + 16, (juce::uint8) 100), 0);
    const int bHits = p.getEngine().hitCounter[16].load();
    renderBlocks (p, 4, bankMidi);
    check (p.getEngine().hitCounter[16].load() == bHits + 1 && p.getEngine().lastPlayedPad.load() == 16, "MIDI E2 plays pad B1 and is tracked for the UI");
    renderBlocks (p, 60);

    p.chopFileToPads (file, RezalicaProcessor::chopTransients);
    p.setTransientSensitivity (0.5f);
    check (p.getNumSlices() == 8 && p.getEngine().getPadSample (8) == nullptr, "re-chopping a short sample clears banks B-D");

    // Loading does not chop; choosing a mode does
    p.selectedPad = 5;
    check (p.loadFileForChopping (file), "load sample to selected pad");
    check (p.getChopMode() == RezalicaProcessor::chopOff && p.getNumSlices() == 0, "loaded sample is not chopped");
    check (p.getEngine().getPadSample (5) != nullptr && std::abs (p.getEngine().getPad (5).end.load() - 1.0f) < 1.0e-6f, "whole sample sits on the selected pad");
    p.setChopMode (RezalicaProcessor::chop4);
    check (p.getNumSlices() == 4 && p.getEngine().getPadSample (3) != nullptr, "choosing a mode chops it");
    p.setChopMode (RezalicaProcessor::chopTransients);
    check (p.getNumSlices() == 8, "switching mode re-chops automatically");

    // Deleting a transient marker keeps the others and switches to Manual
    p.removeSlicePoint (3);
    check (p.getChopMode() == RezalicaProcessor::chopManual && p.getNumSlices() == 7, "deleting a transient marker keeps the other 7");
    p.setChopMode (RezalicaProcessor::chopTransients);
    check (p.getNumSlices() == 8, "back to transients restores all 8");

    // Manual chop markers: adding one leaves transient mode and removes its markers
    p.selectedPad = 0;
    p.addSlicePoint (0.06f);
    check (p.getChopMode() == RezalicaProcessor::chopManual && p.getNumSlices() == 2, "shift+click marker switches to manual and clears transient markers");
    check (p.getEngine().getPadSample (2) == nullptr, "old transient slices are removed from the pads");
    check (std::abs (p.getEngine().getPad (1).start.load() - 0.06f) < 0.01f, "new marker becomes start of pad A2");
    for (float pos : { 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 0.125f })
        p.addSlicePoint (pos);
    check (p.getNumSlices() == 9 && p.getEngine().getPadSample (8) != nullptr, "more manual markers add more slices / pads");
    p.removeSlicePoint (1);
    check (p.getNumSlices() == 8 && p.getEngine().getPadSample (8) == nullptr, "deleting a marker removes the slice");
    p.moveSlicePoint (2, 0.30f);
    check (std::abs (p.getEngine().getPad (2).start.load() - 0.30f) < 0.001f
           && std::abs (p.getEngine().getPad (1).end.load() - 0.30f) < 0.001f, "dragging a marker moves both neighbouring slices");
    p.setTransientSensitivity (0.9f);
    check (p.getNumSlices() == 8, "sensitivity does not wipe manual markers");

    // Lazy Chop: waits in silence for the first hit; that hit starts the sample (first marker at its start),
    // the next hits (~0.5 s and ~1.0 s later in a 2 s sample) set markers at the playhead
    check (p.startLazyChop(), "lazy chop starts");
    check (p.getNumSlices() == 0 && p.getEngine().getPadSample (0) == nullptr, "lazy chop starts with empty pads");
    {
        juce::AudioBuffer<float> buf (2, 512);
        float waitingPeak = 0.0f;
        for (int blk = 0; blk < 30; ++blk) // 0.3 s: nothing hit yet
        {
            juce::MidiBuffer m;
            p.processBlock (buf, m);
            p.updateLazyChop();
            waitingPeak = juce::jmax (waitingPeak, buf.getMagnitude (0, 0, 512));
        }
        check (waitingPeak < 1.0e-6f && p.getNumSlices() == 0 && p.isLazyChopActive() && p.isLazyChopWaiting(),
               "lazy chop waits in silence until the first hit");
        for (int blk = 0; blk < 230; ++blk) // ~2.45 s at 48 kHz, longer than the sample
        {
            juce::MidiBuffer m;
            if (blk == 0 || blk == 47 || blk == 94)
                m.addEvent (juce::MidiMessage::noteOn (1, RezalicaProcessor::midiBaseNote + 9, (juce::uint8) 100), 0);
            p.processBlock (buf, m);
            p.updateLazyChop();
        }
    }
    std::cout << "       lazy slices: " << p.getNumSlices() << "  first at " << (p.getNumSlices() > 0 ? p.getSlicePoints()[0] : -1.0f) << "\n";
    check (p.getNumSlices() == 3, "three pad hits made three slices (the first at the start)");
    check (p.getNumSlices() == 3 && p.getSlicePoints()[0] < 0.002f && std::abs (p.getSlicePoints()[1] - 0.25f) < 0.02f
               && std::abs (p.getSlicePoints()[2] - 0.5f) < 0.02f,
           "lazy slices: the first at the sample start, the next ones at the playhead");
    check (p.getEngine().getPadSample (0) != nullptr && p.getEngine().getPadSample (2) != nullptr, "lazy slices fill A1, A2, A3");
    check (! p.isLazyChopActive(), "lazy chop stops when the sample ends");

    p.chopFileToPads (file, RezalicaProcessor::chopTransients);
    p.setTransientSensitivity (0.5f);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, RezalicaProcessor::midiBaseNote + 2, (juce::uint8) 100), 10);
    float peak = renderBlocks (p, 20, midi);
    check (peak > 0.1f, "MIDI note C1+2 plays pad 3");
    check (renderBlocks (p, 40) < 1.0e-4f, "one-shot finishes and goes silent");

    // Sequencer: pad 1 on every beat, internal clock
    auto& seq = p.getSequencer();
    seq.syncToHost = false;
    seq.bpm = 120.0f;
    for (int s = 0; s < 16; s += 4) seq.setStep (0, 0, s, 100);
    seq.internalPlay = true;
    int hitsBefore = p.getEngine().hitCounter[0].load();
    renderBlocks (p, (int) (48000 * 2.0 / 512)); // 2 s = 4 beats at 120 BPM
    int hits = p.getEngine().hitCounter[0].load() - hitsBefore;
    std::cout << "       sequencer hits in 2 s: " << hits << "\n";
    check (hits == 4, "sequencer fires 4 beats in 2 s at 120 BPM");
    seq.internalPlay = false;
    renderBlocks (p, 50);

    // Effects are per pad: switching them on for A1 must not change A2
    auto renderPad = [&] (int padIndex)
    {
        p.getEngine().allNotesOff();
        renderBlocks (p, 60);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, RezalicaProcessor::midiBaseNote + padIndex, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> out (2, 512 * 40);
        juce::AudioBuffer<float> blk (2, 512);
        for (int b = 0; b < 40; ++b)
        {
            juce::MidiBuffer mm;
            if (b == 0) mm = m;
            p.processBlock (blk, mm);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b * 512, blk, ch, 0, 512);
        }
        return out;
    };
    const auto a2Dry = renderPad (1);
    auto& a1fx = p.getEngine().getPad (0).effects;
    a1fx.filterOn = true; a1fx.cutoff = 300.0f; a1fx.satOn = true; a1fx.tube = 0.6f; a1fx.delayOn = true; a1fx.reverbOn = true;
    const auto a2WithA1FxOn = renderPad (1); // A1 has effects but is silent
    renderPad (0);                           // A1 plays through its effects ...
    renderBlocks (p, 500);                   // ... and its delay / reverb tail dies away
    const auto a2WithA1Fx = renderPad (1);
    float diffSilent = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a2Dry.getNumSamples(); ++i)
            diffSilent = juce::jmax (diffSilent, std::abs (a2Dry.getSample (ch, i) - a2WithA1FxOn.getSample (ch, i)));
    check (diffSilent < 1.0e-6f, "A1's effects are not applied to A2");
    float maxDiff = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a2Dry.getNumSamples(); ++i)
            maxDiff = juce::jmax (maxDiff, std::abs (a2Dry.getSample (ch, i) - a2WithA1Fx.getSample (ch, i)));
    std::cout << "       A2 difference with effects on A1: " << maxDiff << "\n";
    check (maxDiff < 1.0e-6f, "effects on A1 do not affect A2 (per-pad effects)");
    p.getEngine().getPad (1).effects.reset();
    a1fx.reset();

    // LOOP plays between the markers while held, stops when released
    {
        auto& ps = p.getEngine().getPad (1);
        ps.loop = true;
        p.getEngine().allNotesOff();
        renderBlocks (p, 60);
        juce::MidiBuffer on;
        on.addEvent (juce::MidiMessage::noteOn (1, RezalicaProcessor::midiBaseNote + 1, (juce::uint8) 100), 0);
        renderBlocks (p, 2, on);
        // slice A2 is 0.25 s long; hold for ~1.5 s and it must still sound
        renderBlocks (p, 140);
        check (p.getEngine().isPadSounding (1), "loop keeps playing while held");
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, RezalicaProcessor::midiBaseNote + 1), 0);
        renderBlocks (p, 10, off);
        check (! p.getEngine().isPadSounding (1), "loop stops when the pad is released");
        ps.loop = false;
    }

    // TRIM keeps only the region between the markers
    {
        auto& ps = p.getEngine().getPad (2);
        const auto before = p.getEngine().getPadSample (2);
        const float st = ps.start.load(), en = ps.end.load();
        const int expected = (int) (en * (float) before->buffer.getNumSamples()) - (int) (st * (float) before->buffer.getNumSamples());
        check (p.trimPad (2), "trim");
        const auto after = p.getEngine().getPadSample (2);
        check (after != before && std::abs (after->buffer.getNumSamples() - expected) <= 1, "trimmed sample is exactly the marked region");
        check (ps.start.load() < 1.0e-6f && ps.end.load() > 0.999999f && after->file.existsAsFile(), "trimmed pad plays all of it and is saved as WAV");
        check (p.getEngine().getPadSample (1) == before, "other pads keep the original sample");
        after->file.deleteFile();
    }

    // Reverse + effects shouldn't blow up
    p.getEngine().getPad (0).reverse = true;
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, RezalicaProcessor::midiBaseNote, (juce::uint8) 127), 0);
    peak = renderBlocks (p, 100, midi);
    check (peak > 0.01f && peak < 4.0f && std::isfinite (peak), "reverse + all effects produce sane output");

    // CPU check: 16 pads with every effect on, all playing
    {
        for (int i = 0; i < 16; ++i)
        {
            auto& f = p.getEngine().getPad (i).effects;
            f.filterOn = true; f.satOn = true; f.tube = 0.5f; f.tape = 0.5f; f.solid = 0.5f; f.delayOn = true; f.reverbOn = true;
        }
        const int blocks = (int) (48000 * 10.0 / 512);
        juce::AudioBuffer<float> buf (2, 512);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer m;
            if (b % 20 == 0)
                for (int i = 0; i < 16; ++i)
                    m.addEvent (juce::MidiMessage::noteOn (1, RezalicaProcessor::midiBaseNote + i, (juce::uint8) 100), 0);
            p.processBlock (buf, m);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "       10 s of audio, 16 pads x 4 effects: " << ms << " ms (" << (ms / 100.0) << " % of one core)\n";
        for (int i = 0; i < 16; ++i)
            p.getEngine().getPad (i).effects.reset();
        p.getEngine().getPad (0).effects.filterOn = true; p.getEngine().getPad (0).effects.cutoff = 300.0f;
        p.getEngine().getPad (0).effects.reverbOn = true;
    }

    // ---- Time stretch algorithms ----
    {
        const double sr = 44100.0;
        juce::AudioBuffer<float> sine (2, 44100);
        for (int i = 0; i < 44100; ++i)
            for (int ch = 0; ch < 2; ++ch)
                sine.setSample (ch, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / (float) sr));
        auto zeroCrossRate = [] (const juce::AudioBuffer<float>& b, int from, int to)
        {
            int z = 0;
            for (int i = from + 1; i < to; ++i)
                if ((b.getSample (0, i - 1) < 0.0f) != (b.getSample (0, i) < 0.0f)) ++z;
            return (double) z / (to - from);
        };
        const double zIn = zeroCrossRate (sine, 0, 44100);
        for (double factor : { 2.0, 0.6 })
        {
            const int outLen = (int) (44100 * factor);
            auto out = rz::stretch::complex (sine, sr, outLen);
            const double zOut = zeroCrossRate (out, outLen / 4, outLen * 3 / 4);
            const float rmsRatio = out.getRMSLevel (0, outLen / 4, outLen / 2) / sine.getRMSLevel (0, 0, 44100);
            std::cout << "       complex x" << factor << ": pitch ratio " << zOut / zIn << ", level ratio " << rmsRatio << "\n";
            check (out.getNumSamples() == outLen, "complex: exact target length");
            check (std::abs (zOut / zIn - 1.0) < 0.02, "complex: pitch stays the same");
            check (rmsRatio > 0.8f && rmsRatio < 1.25f, "complex: level stays the same");
        }

        // Beats: 8 clicks 0.25 s apart, stretched 1.5x -> clicks 0.375 s apart, each still sharp
        juce::AudioBuffer<float> clicks (2, 88200);
        clicks.clear();
        for (int k = 0; k < 8; ++k)
            for (int i = 0; i < 2000; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    clicks.setSample (ch, k * 11025 + i, 0.8f * std::sin (i * 0.05f) * (1.0f - i / 2000.0f));
        auto stretched = rz::stretch::beats (clicks, sr, 132300, 0, 0, 100.0f, 8.0);
        int found = 0;
        for (int k = 0; k < 8; ++k)
        {
            const int expect = (int) (k * 11025 * 1.5);
            if (stretched.getMagnitude (0, expect, 400) > 0.3f && (k == 0 || stretched.getMagnitude (0, expect - 2000, 1500) < 0.01f))
                ++found;
        }
        std::cout << "       beats x1.5: " << found << "/8 hits at their new positions\n";
        check (stretched.getNumSamples() == 132300 && found == 8, "beats: every hit moves to its stretched position");
        // forward loop of a sustained tone: crossfaded, so no clicks / jumps
        {
            juce::AudioBuffer<float> tone (2, 22050);
            for (int i = 0; i < 22050; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    tone.setSample (ch, i, 0.5f * std::sin (i * 0.031f) * std::exp (-i / 30000.0f));
            auto looped = rz::stretch::beats (tone, sr, 66150, 2, 1, 100.0f, 4.0);
            float maxStepIn = 0.0f, maxStepOut = 0.0f;
            for (int i = 1; i < 22050; ++i) maxStepIn = juce::jmax (maxStepIn, std::abs (tone.getSample (0, i) - tone.getSample (0, i - 1)));
            for (int i = 1; i < 66150 - 200; ++i) maxStepOut = juce::jmax (maxStepOut, std::abs (looped.getSample (0, i) - looped.getSample (0, i - 1)));
            std::cout << "       beats loop: largest sample step " << maxStepOut << " (original " << maxStepIn << ")\n";
            check (maxStepOut < maxStepIn * 1.6f, "beats: loop forward is click-free");
        }
        auto squeezed = rz::stretch::beats (clicks, sr, 44100, 0, 1, 100.0f, 8.0);
        check (squeezed.getNumSamples() == 44100 && squeezed.getMagnitude (0, 3 * 5512, 300) > 0.3f, "beats: speeding up keeps the hits");
    }

    // ---- Warp on the processor ----
    {
        p.chopFileToPads (file, RezalicaProcessor::chop8);
        auto& wseq = p.getSequencer();
        wseq.syncToHost = false;
        wseq.bpm = 120.0f;
        renderBlocks (p, 2); // updates the tempo the warp follows

        const auto original = p.getEngine().getPadSample (3);
        const float start3 = p.getEngine().getPad (3).start.load();
        p.selectedPad = 3;
        rz::WarpSettings w;
        w.enabled = true;
        w.mode = rz::WarpSettings::beats;
        w.lengthIndex = 3;         // 2 bars = 8 beats = 4 s at 120 BPM
        w.syncHost = true;
        w.wholeSample = true;
        check (p.setWarp (3, w), "warp on");
        auto warped = p.getEngine().getPadSample (3);
        check (warped != original && warped->isWarped() && std::abs (warped->buffer.getNumSamples() - 4 * 44100) < 2, "warp: 2 bars at 120 BPM = 4 s");
        check (p.getEngine().getPadSample (0) == warped && std::abs (p.getEngine().getPad (3).start.load() - start3) < 1.0e-6f,
               "whole-sample warp keeps all slices on their pads");

        // the stretched sample is written to disk in the background
        for (int i = 0; i < 100 && ! warped->renderedFile.existsAsFile(); ++i)
            juce::Thread::sleep (50);
        juce::Thread::sleep (100);
        check (warped->renderedFile.existsAsFile() && warped->renderedFile.getSize() > 4 * 44100 * 2 * 3, "stretched sample saved in the background");

        // state restores the warp from the saved file
        juce::MemoryBlock st;
        p.getStateInformation (st);
        RezalicaProcessor r;
        r.setStateInformation (st.getData(), (int) st.getSize());
        auto rw = r.getEngine().getPadSample (3);
        check (rw != nullptr && rw->isWarped() && rw->buffer.getNumSamples() == warped->buffer.getNumSamples()
               && rw->renderedFile == warped->renderedFile, "project reopens with the warped sample (loaded from the saved WAV)");

        // following the DAW tempo
        wseq.bpm = 96.0f;
        renderBlocks (p, 2);
        p.updateWarpTempo();
        auto rewarped = p.getEngine().getPadSample (3);
        check (rewarped->isWarped() && std::abs (rewarped->buffer.getNumSamples() - 5 * 44100) < 2, "warp follows a tempo change (96 BPM -> 5 s)");

        // complex mode, only one pad
        w.mode = rz::WarpSettings::complex;
        w.wholeSample = false;
        w.lengthIndex = 1;          // 2 beats at 96 BPM = 1.25 s
        p.setWarp (3, w);
        auto padOnly = p.getEngine().getPadSample (3);
        check (padOnly->isWarped() && p.getEngine().getPadSample (0) == original && std::abs (padOnly->buffer.getNumSamples() - 55125) < 2,
               "pad-only warp stretches just that pad's slice");

        // background version (what the WARP button uses)
        w.mode = rz::WarpSettings::beats;
        w.wholeSample = true;
        w.lengthIndex = 2;
        p.requestWarp (3, w);
        for (int i = 0; i < 200 && p.isWarpBusy(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        auto bg = p.getEngine().getPadSample (3);
        check (! p.isWarpBusy() && bg->isWarped() && std::abs (bg->buffer.getNumSamples() - (int) (4 * 60.0 / 96.0 * 44100)) < 2,
               "background warp finishes and lands on the pads");
        for (int i = 0; i < 100 && ! bg->renderedFile.existsAsFile(); ++i) juce::Thread::sleep (50);
        juce::Thread::sleep (100);

        w.enabled = false;
        p.setWarp (3, w);
        bg->renderedFile.deleteFile();
        check (p.getEngine().getPadSample (3) == original && std::abs (p.getEngine().getPad (3).start.load() - start3) < 1.0e-5f,
               "warp off restores the original sample and markers");

        for (auto f : { warped->renderedFile, rewarped->renderedFile, padOnly->renderedFile })
            f.deleteFile();
        wseq.bpm = 120.0f;
        renderBlocks (p, 2);
    }

    // ---- Step sequencer: 128 steps ----
    {
        auto& sq = p.getSequencer();
        sq.syncToHost = false;
        sq.bpm = 240.0f;              // 1 step = 1/16 at 240 BPM = 62.5 ms
        sq.length[0] = 128;
        sq.setStep (0, 0, 100, 100);  // step 101 of 128
        // only step 101 should play: mute the other steps of pad A1 for this test
        std::array<juce::uint8, 16> saved {};
        for (int st = 0; st < 16; ++st) { saved[(size_t) st] = sq.getStep (0, 0, st); sq.setStep (0, 0, st, 0); }
        const int before = p.getEngine().hitCounter[0].load();
        sq.internalPlay = true;
        renderBlocks (p, (int) (48000 * 6.3 / 512)); // 6.3 s > 100 steps
        sq.internalPlay = false;
        renderBlocks (p, 20);
        for (int st = 0; st < 16; ++st) sq.setStep (0, 0, st, saved[(size_t) st]);
        check (p.getEngine().hitCounter[0].load() == before + 1, "128-step pattern plays step 101");

        // timing offsets: a step nudged early / late fires that much before / after the grid
        {
            sq.length[0] = 16;
            std::array<juce::uint8, 16> keep {};
            for (int st = 0; st < 16; ++st) { keep[(size_t) st] = sq.getStep (0, 0, st); sq.setStep (0, 0, st, 0); }
            sq.setStep (0, 0, 4, 127);        // beat 2 at 240 BPM: 4 steps * 62.5 ms = 250 ms
            auto hitTime = [&] (int nudge)
            {
                sq.setNudge (0, 0, 4, nudge);
                p.getEngine().allNotesOff();
                renderBlocks (p, 40);
                sq.internalPlay = true;
                juce::AudioBuffer<float> blk (2, 64);
                for (int i = 0; i < 48000 / 64; ++i)  // 1 s in small blocks
                {
                    juce::MidiBuffer m;
                    const int h = p.getEngine().hitCounter[0].load();
                    p.processBlock (blk, m);
                    if (p.getEngine().hitCounter[0].load() != h && i > 0)
                    {
                        sq.internalPlay = false;
                        renderBlocks (p, 4);
                        return i * 64.0 / 48.0; // ms
                    }
                }
                sq.internalPlay = false;
                return -1.0;
            };
            const double onGrid = hitTime (0), early = hitTime (-40), late = hitTime (40);
            std::cout << "       step timing: early " << early << " ms, on grid " << onGrid << " ms, late " << late << " ms\n";
            check (std::abs (onGrid - 250.0) < 3.0 && std::abs (early - 225.0) < 3.0 && std::abs (late - 275.0) < 3.0,
                   "step nudge moves the hit 40 % of a step early / late");
            sq.setNudge (0, 0, 4, 0);
            for (int st = 0; st < 16; ++st) sq.setStep (0, 0, st, keep[(size_t) st]);
            sq.length[0] = 128;
        }

        juce::MemoryBlock old (64 * 32);
        old.fillWith (0);
        static_cast<juce::uint8*> (old.getData())[5 * 32 + 7] = 90; // pad A6, step 8 in the old 32-step layout
        sq.setPatternData (1, old, 32);
        check (sq.getStep (1, 5, 7) == 90 && sq.getStep (1, 5, 39) == 0, "old 32-step projects load correctly");
        sq.clearPattern (1);
        sq.setStep (0, 0, 100, 0);
        sq.length[0] = 16;
        sq.bpm = 120.0f;
    }

    // ---- Piano roll ----
    {
        auto& sq = p.getSequencer();
        // a 440 Hz tone on pad A2 for pitch checks
        auto toneFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_tone.wav");
        {
            juce::AudioBuffer<float> b (1, 44100 * 2);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / 44100.0f));
            juce::WavAudioFormat wav;
            toneFile.deleteFile();
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (toneFile), 44100, 1, 16, {}, 0));
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        p.loadFileToPad (1, toneFile);

        auto renderCollect = [&] (double seconds)
        {
            const int blocks = (int) (48000 * seconds / 512);
            juce::AudioBuffer<float> out (2, blocks * 512), blk (2, 512);
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer m;
                p.processBlock (blk, m);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b * 512, blk, ch, 0, 512);
            }
            return out;
        };
        auto zeroRate = [] (const juce::AudioBuffer<float>& b, int from, int to)
        {
            int z = 0;
            for (int i = from + 1; i < to; ++i)
                if ((b.getSample (0, i - 1) < 0.0f) != (b.getSample (0, i) < 0.0f)) ++z;
            return (double) z / (to - from);
        };

        sq.syncToHost = false;
        sq.bpm = 120.0f;                 // 1 step = 125 ms
        sq.length[3] = 16;
        sq.currentPattern = 3;

        // melodic: C3 = original pitch, C4 = one octave up; notes last as long as drawn
        rz::NoteClip::Ptr clip = new rz::NoteClip();
        clip->mode = rz::NoteClip::melodic;
        clip->targetPad = 1;
        rz::PianoNote a; a.id = sq.newNoteId(); a.kind = rz::NoteClip::melodic; a.row = 60; a.start = 0; a.length = 4;  a.velocity = 100;
        rz::PianoNote b = a; b.id = sq.newNoteId(); b.row = 72; b.start = 8;
        clip->notes = { a, b };
        sq.setClip (3, clip);
        sq.internalPlay = true;
        auto out = renderCollect (2.0);  // 16 steps
        sq.internalPlay = false;
        renderCollect (0.2);
        const double zLow = zeroRate (out, 2400, 21000);          // note C3 (0 .. 0.5 s)
        const double zHigh = zeroRate (out, 48000 + 2400, 69000);  // note C4 (1.0 .. 1.5 s)
        const float gap = out.getMagnitude (0, 31000, 12000);       // 0.65 .. 0.9 s: between the notes
        std::cout << "       piano roll: C4/C3 pitch ratio " << zHigh / zLow << ", level between notes " << gap << "\n";
        check (std::abs (zHigh / zLow - 2.0) < 0.03, "melodic notes play the sample chromatically (C4 = octave up)");
        check (gap < 0.01f, "melodic notes stop at their end (note length)");

        // chord: two notes at once on the same pad (polyphonic)
        rz::NoteClip::Ptr chord = clip->copy();
        rz::PianoNote c1 = a; c1.id = sq.newNoteId(); c1.row = 64; c1.start = 0;
        chord->notes = { a, c1 };
        sq.setClip (3, chord);
        sq.internalPlay = true;
        auto chordOut = renderCollect (0.4);
        sq.internalPlay = false;
        renderCollect (0.2);
        check (chordOut.getRMSLevel (0, 4800, 9600) > out.getRMSLevel (0, 4800, 9600) * 1.2f, "melodic notes are polyphonic (chords)");

        // drum notes: rows are pads
        rz::NoteClip::Ptr drums = new rz::NoteClip();
        rz::PianoNote d; d.id = sq.newNoteId(); d.kind = rz::NoteClip::drum; d.row = 0; d.start = 2; d.length = 1; d.velocity = 120;
        drums->notes = { d };
        sq.setClip (3, drums);
        const int padHits = p.getEngine().hitCounter[0].load();
        sq.internalPlay = true;
        renderCollect (0.6);
        sq.internalPlay = false;
        renderCollect (0.2);
        check (p.getEngine().hitCounter[0].load() == padHits + 1, "drum notes in the piano roll trigger their pad");

        // chromatic MIDI keys + recording into the piano roll
        sq.setClip (3, new rz::NoteClip());
        p.chromaticPad = 1;
        sq.pianoRecording = true;
        sq.internalPlay = true;
        {
            juce::AudioBuffer<float> blk (2, 512);
            for (int bi = 0; bi < 120; ++bi)
            {
                juce::MidiBuffer m;
                if (bi == 10) m.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 90), 0);
                if (bi == 40) m.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
                p.processBlock (blk, m);
                if (bi == 20)
                    check (p.getEngine().isPadSounding (1), "MIDI keys play the chosen pad chromatically");
            }
        }
        sq.internalPlay = false;
        sq.pianoRecording = false;
        p.chromaticPad = -1;
        renderCollect (0.2);
        p.collectRecordedPianoNotes();
        auto rec = sq.getClip (3);
        const bool recOk = rec->notes.size() == 1 && rec->notes[0].row == 67 && rec->notes[0].kind == rz::NoteClip::melodic
                        && std::abs (rec->notes[0].length - 30 * 512 / 6000.0f) < 0.3f;
        if (! rec->notes.empty())
            std::cout << "       recorded note: row " << rec->notes[0].row << " start " << rec->notes[0].start << " length " << rec->notes[0].length << " steps\n";
        check (recOk, "REC records MIDI notes into the piano roll with their length");

        // the piano roll is saved with the project
        juce::MemoryBlock st;
        p.getStateInformation (st);
        RezalicaProcessor r2;
        r2.setStateInformation (st.getData(), (int) st.getSize());
        auto rc = r2.getSequencer().getClip (3);
        check (rc->notes.size() == 1 && rc->notes[0].row == 67 && rc->notes[0].kind == rz::NoteClip::melodic, "piano roll notes are saved with the project");

        sq.setClip (3, new rz::NoteClip());
        p.clearPad (1);
        sq.currentPattern = 0;
        toneFile.deleteFile();
    }

    // ---- Warp markers ----
    {
        p.chopFileToPads (file, RezalicaProcessor::chop8);
        auto& sq = p.getSequencer();
        sq.syncToHost = false;
        sq.bpm = 120.0f;
        renderBlocks (p, 2);
        rz::WarpSettings w;
        w.enabled = true;
        w.mode = rz::WarpSettings::beats;
        w.lengthIndex = 3;           // 2 bars = 4 s at 120 BPM
        w.wholeSample = true;
        w.loopMode = 0;
        w.markers = { { 0.25f, 0.4f } }; // the 3rd hit (at 1/4 of the loop) is moved to 40 %
        p.setWarp (2, w);
        auto ws = p.getEngine().getPadSample (2);
        const int len = ws->buffer.getNumSamples();
        const int at = (int) (0.4f * len);
        const bool hitMoved = ws->buffer.getMagnitude (0, at, 600) > 0.3f && ws->buffer.getMagnitude (0, (int) (0.25f * len), 600) < 0.01f;
        check (len == 4 * 44100 && hitMoved, "warp marker moves its hit to the new position");
        check (std::abs (p.getEngine().getPad (2).start.load() - 0.4f) < 0.002f && std::abs (p.getEngine().getPad (1).end.load() - 0.4f) < 0.002f,
               "slices follow the warp marker");
        // between the marker and the end the audio is stretched evenly: 4th hit (3/8) -> 0.4 + 0.125/0.75 * 0.6 = 0.5
        check (ws->buffer.getMagnitude (0, (int) (0.5f * len), 600) > 0.3f, "audio between warp markers is stretched evenly");

        w.mode = rz::WarpSettings::complex;
        p.setWarp (2, w);
        auto wc = p.getEngine().getPadSample (2);
        check (wc->buffer.getNumSamples() == 4 * 44100 && std::isfinite (wc->buffer.getMagnitude (0, 0, wc->buffer.getNumSamples())),
               "complex mode follows warp markers too");

        w.enabled = false;
        p.setWarp (2, w);
        check (std::abs (p.getEngine().getPad (2).start.load() - 0.25f) < 0.002f, "warp off puts the slices back");

        // RENDER: the warped sample becomes a plain WAV that replaces it on every pad
        w.enabled = true;
        w.mode = rz::WarpSettings::beats;
        p.setWarp (2, w);
        auto beforeRender = p.getEngine().getPadSample (2);
        const float start2 = p.getEngine().getPad (2).start.load();
        check (p.consolidatePad (2), "render");
        auto rendered = p.getEngine().getPadSample (2);
        check (rendered != beforeRender && ! rendered->isWarped() && rendered->file.existsAsFile()
               && rendered->buffer.getNumSamples() == beforeRender->buffer.getNumSamples()
               && p.getEngine().getPadSample (0) == rendered
               && std::abs (p.getEngine().getPad (2).start.load() - start2) < 1.0e-6f,
               "rendered WAV replaces the warped sample on all its pads, slices unchanged");
        std::unique_ptr<juce::AudioFormatReader> rr (p.getFormatManager().createReaderFor (rendered->file));
        check (rr != nullptr && (int) rr->lengthInSamples == rendered->buffer.getNumSamples(), "rendered WAV file is complete");
        rr.reset();
        for (int i = 0; i < 40 && ! beforeRender->renderedFile.existsAsFile(); ++i) juce::Thread::sleep (50);
        juce::Thread::sleep (100);
        beforeRender->renderedFile.deleteFile();
        p.chopFileToPads (file, RezalicaProcessor::chop8); // back to the original for the tests below
        rendered->file.deleteFile();
        for (auto f : { ws->renderedFile, wc->renderedFile }) { for (int i = 0; i < 40 && ! f.existsAsFile(); ++i) juce::Thread::sleep (50); juce::Thread::sleep (100); f.deleteFile(); }
    }

    // ---- Copies stay untouched when the original is warped / rendered ----
    {
        auto& sq = p.getSequencer();
        sq.syncToHost = false;
        sq.bpm = 120.0f;
        renderBlocks (p, 2);
        p.loadFileToPad (40, file);                 // C9
        p.copyPad (40);
        p.pastePad (41);                            // C10: pasted copy
        p.loadFileToPad (42, file);                 // C11: same file loaded again
        auto orig41 = p.getEngine().getPadSample (41);
        auto orig42 = p.getEngine().getPadSample (42);
        check (orig41 != p.getEngine().getPadSample (40) && orig42 != p.getEngine().getPadSample (40), "copies get their own sample");

        rz::WarpSettings w; w.enabled = true; w.lengthIndex = 3; w.wholeSample = true;
        p.selectedPad = 40;
        p.setWarp (40, w);
        check (p.getEngine().getPadSample (40)->isWarped() && p.getEngine().getPadSample (41) == orig41 && p.getEngine().getPadSample (42) == orig42
               && ! orig41->isWarped(), "warping one pad leaves its copies untouched");
        auto warped40 = p.getEngine().getPadSample (40);
        p.consolidatePad (40);
        check (p.getEngine().getPadSample (41) == orig41 && p.getEngine().getPadSample (42) == orig42, "rendering one pad leaves its copies untouched");
        auto rendered40 = p.getEngine().getPadSample (40);

        // ---- Undo / redo ----
        p.getEngine().getPad (42).start = 0.25f; p.getEngine().getPad (42).end = 0.5f;
        p.pushUndo ("Trim");
        p.trimPad (42);
        auto trimmed = p.getEngine().getPadSample (42);
        check (p.canUndo() && p.getUndoName() == "Trim", "undo step recorded");
        p.undo();
        check (p.getEngine().getPadSample (42) == orig42 && std::abs (p.getEngine().getPad (42).end.load() - 0.5f) < 1.0e-6f, "undo brings the untrimmed sample back");
        check (p.canRedo(), "redo available after undo");
        p.redo();
        check (p.getEngine().getPadSample (42) == trimmed && p.getEngine().getPad (42).start.load() < 1.0e-6f, "redo trims again");
        p.undo();

        p.pushUndo ("Pad gain");
        p.getEngine().getPad (41).gainDb = -12.0f;
        p.pushUndo ("Pad reverse");                // a second, different edit
        p.getEngine().getPad (41).reverse = true;
        p.undo();
        check (! p.getEngine().getPad (41).reverse.load() && std::abs (p.getEngine().getPad (41).gainDb.load() + 12.0f) < 1.0e-4f, "undo goes back one step");
        p.undo();
        check (std::abs (p.getEngine().getPad (41).gainDb.load()) < 1.0e-4f, "and another step");

        const int stepsBefore = (int) p.canUndo();
        p.pushUndo ("Nothing"); // no change since the last step: not a new step
        p.pushUndo ("Still nothing");
        juce::ignoreUnused (stepsBefore);

        // ---- the project keeps copies separate, slices of a chop together ----
        juce::MemoryBlock st;
        p.getStateInformation (st);
        RezalicaProcessor r3;
        r3.setStateInformation (st.getData(), (int) st.getSize());
        check (r3.getEngine().getPadSample (41) != nullptr && r3.getEngine().getPadSample (41) != r3.getEngine().getPadSample (42),
               "reopened project: copies stay independent");
        check (r3.getEngine().getPadSample (0) != nullptr && r3.getEngine().getPadSample (0) == r3.getEngine().getPadSample (5),
               "reopened project: slices of one chop still share their sample");

        trimmed->file.deleteFile();
        for (int i = 0; i < 40 && ! warped40->renderedFile.existsAsFile(); ++i) juce::Thread::sleep (50);
        juce::Thread::sleep (100);
        warped40->renderedFile.deleteFile();
        for (int pd : { 40, 41, 42 }) p.clearPad (pd);
        rendered40->file.deleteFile();
    }

    // ---- Bank volume, mute, copy / paste ----
    {
        p.chopFileToPads (file, RezalicaProcessor::chop8);
        auto hitPeak = [&] (int pad)
        {
            p.getEngine().allNotesOff();
            renderBlocks (p, 40);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, RezalicaProcessor::midiBaseNote + pad, (juce::uint8) 100), 0);
            return renderBlocks (p, 20, m);
        };
        const float full = hitPeak (2);
        p.setBankGainDb (0, -6.0f);
        const float quieter = hitPeak (2);
        std::cout << "       bank A at -6 dB: level ratio " << quieter / full << "\n";
        check (std::abs (quieter / full - 0.501f) < 0.03f, "bank volume -6 dB halves the level");
        p.setBankGainDb (0, 0.0f);

        p.setBankMute (0, true);
        check (hitPeak (2) < 1.0e-5f, "muted bank stays silent");
        p.setBankMute (0, false);
        check (hitPeak (2) > 0.1f, "unmuted bank plays again");

        // copy bank A to bank C: independent copies, slices still share among themselves
        p.getEngine().getPad (3).gainDb = -3.0f;
        p.setBankGainDb (0, -2.0f);
        p.copyBank (0);
        p.pasteBank (2);
        auto c1 = p.getEngine().getPadSample (32), c2 = p.getEngine().getPadSample (33);
        check (c1 != nullptr && c1 != p.getEngine().getPadSample (0) && c1 == c2
               && std::abs (p.getEngine().getPad (35).gainDb.load() + 3.0f) < 1.0e-4f
               && std::abs (p.getEngine().getPad (34).start.load() - p.getEngine().getPad (2).start.load()) < 1.0e-6f
               && std::abs (p.getBankGainDb (2) + 2.0f) < 1.0e-4f,
               "paste bank: pads, settings, slices and bank volume copied as independent samples");

        // saved with the project, and undo-able
        p.setBankMute (1, true);
        juce::MemoryBlock st;
        p.getStateInformation (st);
        RezalicaProcessor r4;
        r4.setStateInformation (st.getData(), (int) st.getSize());
        check (std::abs (r4.getBankGainDb (0) + 2.0f) < 1.0e-4f && r4.isBankMuted (1) && ! r4.isBankMuted (0), "bank volume / mute saved with the project");
        p.pushUndo ("Mute bank");
        p.setBankMute (3, true);
        p.undo();
        check (! p.isBankMuted (3), "bank mute can be undone");

        p.setBankMute (1, false);
        p.setBankGainDb (0, 0.0f);
        p.setBankGainDb (2, 0.0f);
        p.getEngine().getPad (3).gainDb = 0.0f;
        for (int i = 32; i < 48; ++i) p.clearPad (i);
    }

    // ---- Export a pattern as MIDI / WAV (drag into the DAW) ----
    {
        RezalicaProcessor ex;                 // a fresh instance, independent of the tests above
        ex.prepareToPlay (48000.0, 512);
        auto& sq = ex.getSequencer();
        sq.syncToHost = false;
        sq.bpm = 120.0f;
        renderBlocks (ex, 2);
        ex.chopFileToPads (file, RezalicaProcessor::chop8);
        sq.currentPattern = 5;
        sq.length[5] = 16;
        sq.clearPattern (5);
        sq.setStep (5, 0, 0, 127);
        sq.setStep (5, 2, 8, 64);
        rz::NoteClip::Ptr c = new rz::NoteClip();
        rz::PianoNote n; n.id = sq.newNoteId(); n.kind = rz::NoteClip::melodic; n.row = 64; n.start = 4; n.length = 2; n.velocity = 90;
        c->notes = { n };
        sq.setClip (5, c);

        auto mid = ex.exportPatternMidi (5, true, true);
        juce::MidiFile mf;
        juce::FileInputStream in (mid);
        const bool readOk = in.openedOk() && mf.readFrom (in);
        int notes = 0; bool found36 = false, found38 = false, found64 = false;
        if (readOk && mf.getNumTracks() > 0)
            for (auto* ev : *mf.getTrack (0))
                if (ev->message.isNoteOn())
                {
                    ++notes;
                    const int nn = ev->message.getNoteNumber();
                    const double t = ev->message.getTimeStamp();
                    found36 = found36 || (nn == 36 && t < 1.0 && ev->message.getVelocity() == 127);
                    found38 = found38 || (nn == 38 && std::abs (t - 8 * 240.0) < 1.0);
                    found64 = found64 || (nn == 64 && std::abs (t - 4 * 240.0) < 1.0);
                }
        check (readOk && notes == 3 && found36 && found38 && found64, "MIDI export: steps (pad notes) and piano roll notes at the right times");

        auto wav = ex.exportPatternWav (5, true, false);
        std::unique_ptr<juce::AudioFormatReader> r (ex.getFormatManager().createReaderFor (wav));
        const int expected = (int) std::llround (16 * 0.25 * 60.0 / 120.0 * 48000.0); // 2 s at 48 kHz
        bool hitAtStart = false;
        if (r != nullptr)
        {
            juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
            r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
            hitAtStart = b.getMagnitude (0, 0, 2000) > 0.1f && b.getMagnitude (0, 48000, 2000) > 0.05f   // A1 at the start, A3 at step 9 (1 s)
                      && b.getMagnitude (0, 30000, 15000) < 0.05f;
        }
        if (r != nullptr)
        {
            juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
            r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
            std::cout << "       wav: " << r->lengthInSamples << " samples (expected " << expected << "), sr " << r->sampleRate
                      << ", start peak " << b.getMagnitude (0, 0, 2000) << ", peak at 1 s " << b.getMagnitude (0, 26000, 20000)
                      << ", overall " << b.getMagnitude (0, 0, b.getNumSamples()) << "\n";
            std::cout << "       envelope (0.1 s):";
            for (int w = 0; w < b.getNumSamples(); w += 4800) std::cout << " " << juce::String (b.getMagnitude (0, w, 4800), 2);
            std::cout << "\n";
        }
        check (r != nullptr && (int) r->lengthInSamples == expected && hitAtStart, "WAV export: one loop of the pattern, rendered");
        std::cout << "       exported: " << mid.getFileName() << ", " << wav.getFileName() << "\n";
        r.reset();
        mid.deleteFile();
        wav.deleteFile();
        sq.clearPattern (5);
        sq.setClip (5, new rz::NoteClip());
        sq.currentPattern = 0;
    }
    check (std::isinf (p.getTailLengthSeconds()), "infinite tail: the DAW keeps the plugin running for the sequencer");

    // ---- Warp strip: a plain drag on the diamond (no Ctrl) stretches the audio ----
    {
        RezalicaProcessor wp;
        wp.prepareToPlay (48000.0, 512);
        wp.getSequencer().syncToHost = false;
        wp.getSequencer().bpm = 120.0f;
        renderBlocks (wp, 2);
        wp.chopFileToPads (file, RezalicaProcessor::chop8);
        rz::WarpSettings w; w.enabled = true; w.lengthIndex = 3; w.markers = { { 0.25f, 0.25f } };
        wp.setWarp (0, w);

        rz::WaveformView wv (wp);
        wv.setSize (900, 260);
        wv.setPad (0);
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto ev = [&] (float x, float y, juce::Point<float> downAt, bool dragged, int clicks = 1)
        {
            return juce::MouseEvent (src, { x, y }, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &wv, &wv, juce::Time::getCurrentTime(), downAt, juce::Time::getCurrentTime(), clicks, dragged);
        };
        // the wave area starts at x = 6 and is (900 - 24 - 12) wide; the strip is its top 18 px (y 20..38)
        const float areaW = 900.0f - 24.0f - 12.0f;
        const float x0 = 6.0f + 0.25f * areaW, x1 = 6.0f + 0.4f * areaW, y = 28.0f;
        wv.mouseDown (ev (x0, y, { x0, y }, false));
        wv.mouseDrag (ev (x1, y, { x0, y }, true));
        wv.mouseUp (ev (x1, y, { x0, y }, true));
        for (int i = 0; i < 200 && wp.isWarpBusy(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        auto ws = wp.getEngine().getPadSample (0);
        const float v = ws->isWarped() && ! ws->warp.markers.empty() ? ws->warp.markers[0].second : -1.0f;
        std::cout << "       warp strip drag: marker moved to " << v << "\n";
        check (std::abs (v - 0.4f) < 0.02f, "dragging the diamond in the warp strip moves the warp marker (no Ctrl needed)");

        // below the strip a plain drag does NOT warp (it moves chop markers / pad handles as before)
        const float yBelow = 150.0f;
        const auto before = ws->warp.markers;
        wv.mouseDown (ev (x1, yBelow, { x1, yBelow }, false));
        wv.mouseDrag (ev (x1 + 60.0f, yBelow, { x1, yBelow }, true));
        wv.mouseUp (ev (x1 + 60.0f, yBelow, { x1, yBelow }, true));
        for (int i = 0; i < 50 && wp.isWarpBusy(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check (wp.getEngine().getPadSample (0)->warp.markersToString() == ws->warp.markersToString(), "below the warp strip the mouse does its normal job");
        juce::ignoreUnused (before);
        for (int i = 0; i < 40; ++i) juce::Thread::sleep (25);
    }

    // ---- Standalone projects: Save / Save As / Open / New ----
    {
        auto proj = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_test_project.rezalica");
        proj.deleteFile();
        p.getEngine().getPad (7).gainDb = -7.5f;
        check (p.saveProjectToFile (proj) && proj.existsAsFile() && p.currentProjectFile == proj, "save project");
        RezalicaProcessor other;
        check (other.loadProjectFromFile (proj) && other.getEngine().getPadSample (7) != nullptr
               && std::abs (other.getEngine().getPad (7).gainDb.load() + 7.5f) < 1.0e-4f
               && other.getSequencer().getStep (0, 0, 4) == p.getSequencer().getStep (0, 0, 4),
               "open project restores pads, settings and patterns");
        other.newProject();
        check (other.getEngine().getPadSample (7) == nullptr && other.currentProjectFile == juce::File(), "new project starts empty");
        check (! other.loadProjectFromFile (file), "a non-project file is refused");
        p.getEngine().getPad (7).gainDb = 0.0f;
        proj.deleteFile();
        check (rz::NoteClip().mode == rz::NoteClip::drum, "piano roll opens in DRUMS mode");
    }

    // ---- SCENES / SONG ----
    {
        RezalicaProcessor sp;
        sp.prepareToPlay (48000.0, 512);
        auto& sq = sp.getSequencer();
        sq.syncToHost = false;
        sq.bpm = 120.0f;                     // 1 bar = 2 s
        sp.chopFileToPads (file, RezalicaProcessor::chop8);
        sq.length[0] = 16; sq.setStep (0, 0, 0, 127);   // P1: 1 bar, A1 on beat 1
        sq.length[1] = 8;  sq.setStep (1, 2, 0, 127);   // P2: half a bar, A3 at its start
        sq.setSceneSlot (0, 0, 0); sq.setSceneSlot (0, 1, 1);   // SCENE = P1, P2  (3 s)
        sq.setSongStep (0, 0); sq.setSongStep (2, 0);           // SONG = scene, rest (1 bar), scene  (8 s)

        std::vector<rz::Sequencer::SongSegment> segs;
        double total = 0.0;
        const int nSeg = sq.buildSong (segs, total);
        check (nSeg == 5 && std::abs (total - 16.0) < 1.0e-9, "song timeline: P1 P2 rest P1 P2 = 16 beats (8 s at 120 BPM)");

        // play the song and note when A1 / A3 are hit
        std::vector<double> hitsA1, hitsA3;
        sq.songMode = true;
        sq.restartRequested = true;
        sq.internalPlay = true;
        juce::AudioBuffer<float> blk (2, 128);
        juce::MidiBuffer none;
        int a1 = sp.getEngine().hitCounter[0].load(), a3 = sp.getEngine().hitCounter[2].load();
        bool endedOk = false;
        for (int i = 0; i < 48000 * 10 / 128; ++i)
        {
            sp.processBlock (blk, none);
            const double t = (i + 1) * 128 / 48000.0;
            if (sp.getEngine().hitCounter[0].load() != a1) { hitsA1.push_back (t); a1 = sp.getEngine().hitCounter[0].load(); }
            if (sp.getEngine().hitCounter[2].load() != a3) { hitsA3.push_back (t); a3 = sp.getEngine().hitCounter[2].load(); }
            if (t > 8.1 && ! sq.internalPlay.load()) endedOk = true;
        }
        auto near = [] (const std::vector<double>& v, std::initializer_list<double> want)
        {
            if (v.size() != want.size()) return false;
            size_t k = 0;
            for (double w : want) if (std::abs (v[k++] - w) > 0.01) return false;
            return true;
        };
        std::cout << "       song: A1 hits at";
        for (auto t : hitsA1) std::cout << " " << t;
        std::cout << "  | A3 hits at";
        for (auto t : hitsA3) std::cout << " " << t;
        std::cout << "\n";
        check (near (hitsA1, { 0.0, 5.0 }), "song: P1 plays at 0 s and 5 s (scene, 1 bar rest, scene)");
        check (near (hitsA3, { 2.0, 7.0 }), "song: P2 plays at 2 s and 7 s");
        check (endedOk, "song stops at its end");

        // loop
        sq.songLoop = true;
        sq.restartRequested = true;
        sq.internalPlay = true;
        int loopHits = 0; a1 = sp.getEngine().hitCounter[0].load();
        for (int i = 0; i < 48000 * 9 / 128; ++i)  // 9 s: 0, 5 s and 8 s (= 0 of the second round)
        {
            sp.processBlock (blk, none);
            if (sp.getEngine().hitCounter[0].load() != a1) { ++loopHits; a1 = sp.getEngine().hitCounter[0].load(); }
        }
        sq.internalPlay = false;
        check (loopHits == 3, "LOOP starts the song again");
        sq.songLoop = false;

        // song timer: position in the song while it plays
        sq.playSong();
        for (int i = 0; i < 48000 * 3 / 128; ++i) sp.processBlock (blk, none);   // 3 s
        const float posQ = sq.songPosPpq.load();
        std::cout << "       song timer after 3 s: " << posQ << " quarter notes\n";
        check (std::abs (posQ - 6.0f) < 0.05f && sq.isPlayingSong(), "song timer follows the song (3 s = 6 beats at 120 BPM)");

        // the sampler's PLAY plays the selected pattern, even though a song exists and was playing
        sq.currentPattern = 1;   // P2: A3 every half bar
        sq.playPattern();
        a3 = sp.getEngine().hitCounter[2].load(); a1 = sp.getEngine().hitCounter[0].load();
        {
            int last = a3;
            std::cout << "       sampler PLAY A3 times:";
            for (int i = 0; i < (int) (48000 * 2.9 / 128); ++i)   // 2.9 s: P2 (1 s long) starts 3 times
            {
                sp.processBlock (blk, none);
                if (sp.getEngine().hitCounter[2].load() != last) { last = sp.getEngine().hitCounter[2].load(); std::cout << " " << i * 128 / 48000.0; }
            }
            std::cout << "\n";
        }
        std::cout << "       sampler PLAY: A3 hits " << sp.getEngine().hitCounter[2].load() - a3 << ", A1 hits " << sp.getEngine().hitCounter[0].load() - a1
                  << ", playing pattern " << sq.isPlayingPattern() << "\n";
        check (sp.getEngine().hitCounter[2].load() - a3 == 3 && sp.getEngine().hitCounter[0].load() == a1 && sq.isPlayingPattern(),
               "sampler PLAY loops the selected pattern, independent of the song");
        sq.stopPlayback();
        for (int i = 0; i < 20; ++i) sp.processBlock (blk, none);

        // an empty song never makes the sampler silent
        for (int st = 0; st < rz::Sequencer::maxSongSteps; ++st) sq.setSongStep (st, -1);
        sq.songMode = true;
        sq.restartRequested = true;
        sq.internalPlay = true;
        a3 = sp.getEngine().hitCounter[2].load();
        for (int i = 0; i < 48000 * 2 / 128; ++i) sp.processBlock (blk, none);
        sq.stopPlayback();
        check (sp.getEngine().hitCounter[2].load() - a3 == 2, "with an empty song the selected pattern plays");
        sq.setSongStep (0, 0); sq.setSongStep (2, 0);
        sq.currentPattern = 0;
        for (int i = 0; i < 20; ++i) sp.processBlock (blk, none);

        // export
        auto wavFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_song.wav");
        auto midFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_song.mid");
        check (sp.exportSongWav (wavFile), "export song as WAV");
        std::unique_ptr<juce::AudioFormatReader> r (sp.getFormatManager().createReaderFor (wavFile));
        check (r != nullptr && (int) r->lengthInSamples == 48000 * 11, "song WAV = 8 s song + 3 s tail");
        r.reset();
        check (sp.exportSongMidi (midFile), "export song as MIDI");
        juce::MidiFile mf;
        juce::FileInputStream in (midFile);
        int notes = 0;
        if (in.openedOk() && mf.readFrom (in))
            for (auto* ev : *mf.getTrack (0))
                if (ev->message.isNoteOn()) ++notes;
        check (notes == 4, "song MIDI has every hit of the song (4)");
        wavFile.deleteFile();
        midFile.deleteFile();

        // saved with the project
        juce::MemoryBlock st;
        sp.getStateInformation (st);
        RezalicaProcessor r5;
        r5.setStateInformation (st.getData(), (int) st.getSize());
        check (r5.getSequencer().getSongStep (1) == -1 && r5.getSequencer().getSongStep (2) == 0 && r5.getSequencer().getSceneSlot (0, 1) == 1
                   && r5.getSequencer().songLengthSteps() == 3,
               "scenes and song are saved with the project");
    }

    // ---- SCENE tracks, start marker, swing per pattern, metronome / count-in ----
    {
        auto tp = std::make_unique<RezalicaProcessor> (false);
        auto& sp = *tp;
        sp.prepareToPlay (48000.0, 128);
        auto& sq = sp.getSequencer();
        sq.syncToHost = false;
        sq.bpm = 120.0f;                                   // 1 bar = 2 s
        sp.chopFileToPads (file, RezalicaProcessor::chop8);
        sq.length[0] = 16; sq.setStep (0, 0, 0, 127);      // P1: A1 on beat 1 (1 bar)
        sq.length[1] = 32; sq.setStep (1, 2, 0, 127);      // P2: A3 at its start (2 bars)
        sq.usedTracks = 2;
        sq.setSceneSlot (0, 0, 0);                          // track 1: P1 (1 bar)
        sq.setSceneSlot (1, 0, 1);                          // track 2: P2 (2 bars) -> scene = 2 bars
        sq.setSongStep (0, 0, 0); sq.setSongStep (0, 0, 1); // step 1: both tracks
        sq.setSongStep (1, 0, 1);                           // step 2: only track 2
        check (std::abs (sq.sceneLengthPpq() - 8.0) < 1.0e-9 && std::abs (sq.songTotalPpq() - 16.0) < 1.0e-9,
               "scene = its longest track (2 bars), song = 2 scenes (8 s)");

        juce::AudioBuffer<float> blk (2, 128);
        juce::MidiBuffer none;
        auto run = [&] (double seconds, std::vector<double>& a1, std::vector<double>& a3)
        {
            int c1 = sp.getEngine().hitCounter[0].load(), c3 = sp.getEngine().hitCounter[2].load();
            for (int i = 0; i < (int) (48000 * seconds / 128); ++i)
            {
                sp.processBlock (blk, none);
                const double t = i * 128 / 48000.0;
                if (sp.getEngine().hitCounter[0].load() != c1) { a1.push_back (t); c1 = sp.getEngine().hitCounter[0].load(); }
                if (sp.getEngine().hitCounter[2].load() != c3) { a3.push_back (t); c3 = sp.getEngine().hitCounter[2].load(); }
            }
        };
        auto near = [] (const std::vector<double>& v, std::initializer_list<double> want)
        {
            if (v.size() != want.size()) return false;
            size_t k = 0;
            for (double w : want) if (std::abs (v[k++] - w) > 0.01) return false;
            return true;
        };
        std::vector<double> a1, a3;
        sq.playSong();
        run (9.0, a1, a3);
        std::cout << "       tracks: A1 at"; for (auto t : a1) std::cout << " " << t;
        std::cout << " | A3 at"; for (auto t : a3) std::cout << " " << t; std::cout << "\n";
        check (near (a1, { 0.0 }) && near (a3, { 0.0, 4.0 }), "tracks play together; track 1 is off on song step 2");

        // start marker: PLAY SONG starts at song step 2 (4 s into the song)
        sq.songStartStep = 1;
        a1.clear(); a3.clear();
        sq.playSong();
        run (1.0, a1, a3);
        check (a1.empty() && near (a3, { 0.0 }) && sq.songPosStep.load() == 1, "PLAY SONG starts at the start marker");
        sq.stopPlayback();
        sq.songStartStep = 0;
        for (int i = 0; i < 20; ++i) sp.processBlock (blk, none);

        // swing per pattern, 1/8 grid: step 3 (2nd 8th) moves later by up to 1 step
        sq.setStep (0, 1, 2, 127);
        sq.patternSwing[0] = 0.5f; sq.swingGrid[0] = 1;
        check (std::abs (sq.swingOffsetSteps (0, 2) - 0.5) < 1.0e-9 && sq.swingOffsetSteps (0, 1) == 0.0 && sq.swingOffsetSteps (1, 2) == 0.0,
               "swing is per pattern; 1/8 grid shifts the 2nd 8th");
        {
            sq.currentPattern = 0;
            std::vector<double> a2, dummy;
            int c2 = sp.getEngine().hitCounter[1].load();
            sq.playPattern();
            for (int i = 0; i < 48000 / 128; ++i)
            {
                sp.processBlock (blk, none);
                if (sp.getEngine().hitCounter[1].load() != c2) { a2.push_back (i * 128 / 48000.0); c2 = sp.getEngine().hitCounter[1].load(); }
            }
            sq.stopPlayback();
            // step 3 = 0.25 s, + 0.5 step (62.5 ms)
            check (a2.size() == 1 && std::abs (a2[0] - 0.3125) < 0.005, "swung step lands 62.5 ms late at 120 BPM");
        }
        sq.patternSwing[0] = 0.0f; sq.swingGrid[0] = 0;
        sq.setStep (0, 1, 2, 0);
        for (int i = 0; i < 20; ++i) sp.processBlock (blk, none);

        // metronome: 4 beats in 2 s; count-in: REC + 1 bar count-in delays the pattern by 2 s
        {
            sq.metronome = true;
            sq.playPattern();
            int clicks = 0;
            float peak = 0.0f;
            for (int i = 0; i < (int) (48000 * 1.99 / 128); ++i)
            {
                sp.processBlock (blk, none);
                clicks += sq.numClicks;
                peak = juce::jmax (peak, blk.getMagnitude (0, 0, 128));
            }
            sq.stopPlayback();
            sq.metronome = false;
            std::cout << "       metronome: " << clicks << " clicks, peak " << peak << "\n";
            check (clicks == 4 && peak > 0.05f, "metronome clicks on every beat");
            for (int i = 0; i < 20; ++i) sp.processBlock (blk, none);

            sq.recording = true;
            sq.countInBars = 1;
            std::vector<double> h1, dummy;
            int countClicks = 0;
            int c1 = sp.getEngine().hitCounter[0].load();
            sq.playPattern();
            for (int i = 0; i < (int) (48000 * 2.5 / 128); ++i)
            {
                sp.processBlock (blk, none);
                countClicks += sq.numClicks;
                if (sp.getEngine().hitCounter[0].load() != c1) { h1.push_back (i * 128 / 48000.0); c1 = sp.getEngine().hitCounter[0].load(); }
            }
            sq.stopPlayback();
            sq.recording = false;
            sq.countInBars = 0;
            std::cout << "       count-in: clicks " << countClicks << ", first hit at " << (h1.empty() ? -1.0 : h1[0]) << " s\n";
            check (countClicks == 4 && h1.size() == 1 && std::abs (h1[0] - 2.0) < 0.01, "count-in: 4 clicks, then the pattern starts");
            for (int i = 0; i < 20; ++i) sp.processBlock (blk, none);
        }

        // choke group: pad A2 cuts pad A1
        {
            sp.getEngine().getPad (0).chokeGroup = 1;
            sp.getEngine().getPad (1).chokeGroup = 1;
            sp.getEngine().getPad (0).releaseMs = 2000.0f;
            sp.triggerPadFromUi (0, 1.0f, true);
            for (int i = 0; i < 4; ++i) sp.processBlock (blk, none);
            const bool aBefore = sp.getEngine().isPadSounding (0);
            sp.triggerPadFromUi (1, 1.0f, true);
            for (int i = 0; i < 10; ++i) sp.processBlock (blk, none);   // > 4 ms fade
            check (aBefore && ! sp.getEngine().isPadSounding (0) && sp.getEngine().isPadSounding (1), "choke group: a hit cuts the other pad");
            sp.getEngine().getPad (1).chokeGroup = 2;
            sp.triggerPadFromUi (0, 1.0f, true);
            for (int i = 0; i < 4; ++i) sp.processBlock (blk, none);
            sp.triggerPadFromUi (1, 1.0f, true);
            for (int i = 0; i < 10; ++i) sp.processBlock (blk, none);
            check (sp.getEngine().isPadSounding (0), "different choke groups don't cut each other");
            sp.stopAll();
            for (int i = 0; i < 20; ++i) sp.processBlock (blk, none);
        }

        // project state: tracks, start marker, colours, swing, choke, master FX
        sq.songStartStep = 1;
        sq.patternColours[1] = 0xff4fa3ff;
        sq.patternSwing[1] = 0.3f; sq.swingGrid[1] = 1;
        sq.metronome = true; sq.countInBars = 2;
        juce::MemoryBlock st;
        sp.getStateInformation (st);
        auto r6p = std::make_unique<RezalicaProcessor> (false);
        auto& r6 = *r6p;
        r6.setStateInformation (st.getData(), (int) st.getSize());
        auto& q6 = r6.getSequencer();
        check (q6.getNumTracks() == 2 && q6.getSceneSlot (1, 0) == 1 && q6.getSongStep (1, 1) == 0 && q6.getSongStep (1, 0) == -1
                   && q6.songStartStep.load() == 1 && q6.patternColours[1] == 0xff4fa3ff
                   && std::abs (q6.patternSwing[1].load() - 0.3f) < 1.0e-6f && q6.swingGrid[1].load() == 1
                   && q6.metronome.load() && q6.countInBars.load() == 2,
               "tracks, start marker, colours, swing and metronome are saved with the project");
        check (r6.getEngine().getPad (0).chokeGroup.load() == 1, "choke groups are saved");

        // song export with 2 tracks: 8 s + 3 s tail, MIDI has both tracks' hits (A1 once, A3 twice)
        sq.metronome = true; // must not end up in the export
        auto wavFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_tracks.wav");
        auto midFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_tracks.mid");
        check (sp.exportSongWav (wavFile), "export a 2-track song as WAV");
        std::unique_ptr<juce::AudioFormatReader> rd (sp.getFormatManager().createReaderFor (wavFile));
        check (rd != nullptr && (int) rd->lengthInSamples == 48000 * 11, "2-track song WAV = 8 s + 3 s tail (start marker ignored)");
        rd.reset();
        check (sp.exportSongMidi (midFile), "export a 2-track song as MIDI");
        juce::MidiFile mf;
        juce::FileInputStream in (midFile);
        int notes = 0;
        if (in.openedOk() && mf.readFrom (in))
            for (auto* ev : *mf.getTrack (0))
                if (ev->message.isNoteOn()) ++notes;
        check (notes == 3, "2-track song MIDI has 3 hits");
        wavFile.deleteFile();
        midFile.deleteFile();
    }

    // ---- sampling (input) and resampling (output) ----
    {
        auto rp = std::make_unique<RezalicaProcessor> (true);   // with an input bus, like the standalone app
        auto& r = *rp;
        r.setRateAndBufferSizeDetails (48000.0, 256);
        r.prepareToPlay (48000.0, 256);
        check (r.hasAudioInput(), "the standalone processor has an audio input");
        juce::AudioBuffer<float> blk (2, 256);
        juce::MidiBuffer none;

        // input: waits for sound, then records until STOP
        r.selectedPad = 5;
        check (r.startRecording (rz::AudioRecorder::input, false, true), "REC (input, start on sound)");
        int k = 0;
        for (int i = 0; i < 400; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < 256; ++n)
                    blk.setSample (ch, n, i < 20 ? 0.0f : 0.5f * std::sin ((float) (k + n - 20 * 256) * 0.03f)); // 20 blocks of silence first
            k += 256;
            r.processBlock (blk, none);
            if (i == 399) r.getRecorder().stop();
        }
        blk.clear();
        r.processBlock (blk, none);
        const auto take = r.collectRecording (5);
        auto s5 = r.getEngine().getPadSample (5);
        std::cout << "       input take: " << (s5 != nullptr ? s5->buffer.getNumSamples() : -1) << " samples\n";
        check (take.existsAsFile() && s5 != nullptr && s5->buffer.getNumSamples() > 380 * 256 - 10 && s5->buffer.getNumSamples() <= 380 * 256
                   && std::abs (s5->buffer.getSample (0, 0)) < 0.05f && s5->buffer.getMagnitude (0, 0, 1000) > 0.1f,
               "input take starts at the sound and lands on the pad");
        check (r.getChopSource() == s5, "the take on the selected pad is ready to chop");
        check (r.undo() && r.getEngine().getPadSample (5) == nullptr, "UNDO removes the take");
        take.deleteFile();

        // resample exactly one loop of a pattern
        auto& sq = r.getSequencer();
        sq.syncToHost = false;
        sq.bpm = 120.0f;
        r.chopFileToPads (file, RezalicaProcessor::chop8);
        sq.currentPattern = 2; sq.length[2] = 16; sq.setStep (2, 0, 0, 127); sq.setStep (2, 1, 8, 127);
        r.selectedPad = 20;
        check (r.startRecording (rz::AudioRecorder::output, true, false), "RESAMPLE one loop of the pattern");
        for (int i = 0; i < 48000 * 3 / 256 && r.getRecorder().getState() != rz::AudioRecorder::finished; ++i)
        {
            blk.clear();
            r.processBlock (blk, none);
        }
        const auto re = r.collectRecording (20);
        auto s20 = r.getEngine().getPadSample (20);
        check (re.existsAsFile() && s20 != nullptr && s20->buffer.getNumSamples() == 96000 && s20->buffer.getMagnitude (0, 0, 2000) > 0.05f,
               "resample = exactly 1 bar (2 s), starting on the first hit");
        sq.stopPlayback();
        re.deleteFile();
    }

    // ---- PLAY with an empty pattern plays the selected pad's sample ----
    {
        auto ep = std::make_unique<RezalicaProcessor> (false);
        auto& e = *ep;
        e.prepareToPlay (48000.0, 480);
        auto& sq = e.getSequencer();
        sq.syncToHost = false;
        e.selectedPad = 0;
        e.loadFileForChopping (file);   // 2 s sample
        sq.currentPattern = 4;
        check (sq.isPatternEmpty (4), "pattern 5 is empty");
        juce::AudioBuffer<float> blk (2, 480);
        juce::MidiBuffer none;
        auto runFor = [&] (double secs) { for (int i = 0; i < (int) (48000 * secs / 480); ++i) e.processBlock (blk, none); };

        e.getEngine().getPad (0).loop = true;
        sq.playPattern();
        runFor (0.1);
        const bool started = e.getEngine().isPadSounding (0);
        runFor (5.0);                                // 2.5 loops of the sample
        const bool stillLooping = e.getEngine().isPadSounding (0) && e.getEngine().hitCounter[0].load() == 1;
        sq.stopPlayback();
        runFor (0.2);
        check (started && stillLooping && ! e.getEngine().isPadSounding (0), "PLAY on an empty pattern loops the selected pad (LOOP on) until STOP");

        e.getEngine().getPad (0).loop = false;
        sq.playPattern();
        runFor (2.5);                                // longer than the sample: plays once
        check (! e.getEngine().isPadSounding (0) && e.getEngine().hitCounter[0].load() == 2, "without LOOP the sample plays once");
        sq.stopPlayback();
        runFor (0.1);

        e.loadFileToPad (3, file);
        sq.setStep (4, 3, 4, 127);                   // not empty any more: the pattern plays, not the pad
        const int before = e.getEngine().hitCounter[0].load();
        sq.playPattern();
        runFor (1.0);
        sq.stopPlayback();
        check (! sq.isPatternEmpty (4) && e.getEngine().hitCounter[0].load() == before && e.getEngine().hitCounter[3].load() > 0,
               "a pattern with steps plays its steps, not the selected pad");

        // drag & drop from the browser onto a pad
        rz::PadButton pb (e, 7, "x");
        juce::DragAndDropTarget::SourceDetails d ("rzfile:" + file.getFullPathName(), nullptr, {});
        check (pb.isInterestedInDragSource (d), "pads accept files dragged from the browser");
        pb.itemDropped (d);
        check (e.getEngine().getPadSample (7) != nullptr, "dropping a browser file on a pad loads it");
    }

    // ---- browser favourites ----
    {
        const auto before = rz::AppSettings::getFavorites();
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Rezalica Fav Test");
        dir.createDirectory();
        rz::AppSettings::setFavorite (dir, true);
        const bool added = rz::AppSettings::isFavorite (dir) && rz::AppSettings::getFavorites().size() == before.size() + 1;
        rz::AppSettings::setFavorite (dir, true); // twice: still once
        const bool once = rz::AppSettings::getFavorites().size() == before.size() + 1;
        rz::AppSettings::setFavorite (dir, false);
        check (added && once && ! rz::AppSettings::isFavorite (dir), "folders can be added to / removed from FAVORITES");
        dir.deleteRecursively();
    }

    // ---- multi-select pads: click A1, Shift+click A4, Clear all selected pads ----
    {
        auto mp = std::make_unique<RezalicaProcessor> (false);
        auto& m = *mp;
        m.prepareToPlay (48000.0, 512);
        m.chopFileToPads (file, RezalicaProcessor::chop8);   // A1..A8 filled
        std::unique_ptr<juce::AudioProcessorEditor> ed (m.createEditor());
        std::vector<rz::PadButton*> padsUi;
        for (auto* c : ed->getChildren())
            if (auto* pb = dynamic_cast<rz::PadButton*> (c)) padsUi.push_back (pb);
        check (padsUi.size() == 16, "editor has 16 pad buttons");
        if (padsUi.size() == 16)
        {
            padsUi[0]->onSelect (0);
            padsUi[3]->onMultiSelect (3, juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
            padsUi[6]->onMultiSelect (6, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier));
            check (padsUi[0]->getMultiCount() == 5, "Shift+click selects A1-A4, Ctrl+click adds A7");
            padsUi[1]->onClearSelected();
            int left = 0;
            for (int i = 0; i < 8; ++i) if (m.getEngine().getPadSample (i) != nullptr) ++left;
            check (left == 3 && m.getEngine().getPadSample (4) != nullptr && m.getEngine().getPadSample (6) == nullptr
                       && padsUi[0]->getMultiCount() == 0, "Clear all selected pads empties exactly those pads");
            m.undo();
            int back = 0;
            for (int i = 0; i < 8; ++i) if (m.getEngine().getPadSample (i) != nullptr) ++back;
            check (back == 8, "one UNDO brings all cleared pads back");
        }
    }

    // ---- PAUSE carries on from the same place ----
    {
        auto pp2 = std::make_unique<RezalicaProcessor> (false);
        auto& q = *pp2;
        q.prepareToPlay (48000.0, 480);
        auto& sq = q.getSequencer();
        sq.syncToHost = false;
        sq.bpm = 120.0f;
        sq.length[0] = 64;
        juce::AudioBuffer<float> blk (2, 480);
        juce::MidiBuffer none;
        auto runFor = [&] (double secs) { for (int i = 0; i < (int) (48000 * secs / 480); ++i) q.processBlock (blk, none); };
        sq.playPattern();
        runFor (1.0);                                   // 1 s = 8 steps
        sq.pausePlayback();
        runFor (0.5);
        const bool stopped = ! sq.isRunning() && sq.isPaused();
        sq.resumePlayback();
        runFor (0.5);                                   // 4 more steps
        const double pos = sq.getPlayPositionSteps();
        std::cout << "       after pause + resume: step " << pos << "\n";
        check (stopped && std::abs (pos - 12.0) < 0.3, "PAUSE stops, PLAY carries on from the same step");
        sq.stopPlayback();
        runFor (0.1);
        sq.playPattern();
        runFor (0.25);
        check (std::abs (sq.getPlayPositionSteps() - 2.0) < 0.3, "STOP goes back to the start");
        sq.stopPlayback();
    }

    // ---- piano roll recording: REC + PLAY records, it does not start the sample ----
    {
        auto rp2 = std::make_unique<RezalicaProcessor> (false);
        auto& r = *rp2;
        r.prepareToPlay (48000.0, 480);
        auto& sq = r.getSequencer();
        sq.syncToHost = false;
        sq.bpm = 120.0f;
        r.selectedPad = 0;
        r.loadFileForChopping (file);
        sq.currentPattern = 6;               // empty pattern
        auto clip = sq.getClip (6)->copy();
        clip->mode = rz::NoteClip::melodic;
        clip->targetPad = 0;
        sq.setClip (6, clip);
        juce::AudioBuffer<float> blk (2, 480);
        juce::MidiBuffer none;
        auto runFor = [&] (double secs) { for (int i = 0; i < (int) (48000 * secs / 480); ++i) r.processBlock (blk, none); };

        sq.pianoRecording = true;
        const int hitsBefore = r.getEngine().hitCounter[0].load();
        sq.playPattern();
        runFor (0.3);
        check (r.getEngine().hitCounter[0].load() == hitsBefore, "with REC armed, PLAY does not start the selected pad's sample");

        // computer keyboard note (as the piano roll sends it) and a hit on the target pad (key 1)
        r.previewNoteFromUi (0, 0.8f, 7.0f, true, true);   // G3
        runFor (0.25);
        r.previewNoteFromUi (0, 0.0f, 7.0f, false, true);
        r.triggerPadFromUi (0, 1.0f, true);                // the target pad: C3 in the melodic view
        runFor (0.25);
        r.triggerPadFromUi (0, 0.0f, false);
        runFor (0.1);
        sq.stopPlayback();
        sq.pianoRecording = false;
        runFor (0.05);
        r.collectRecordedPianoNotes();
        int g3 = 0, c3 = 0;
        for (auto& n : sq.getClip (6)->notes)
        {
            if (n.kind == rz::NoteClip::melodic && n.row == 67) ++g3;
            if (n.kind == rz::NoteClip::melodic && n.row == 60) ++c3;
        }
        std::cout << "       recorded notes: " << sq.getClip (6)->notes.size() << " (G3 " << g3 << ", C3 " << c3 << ")\n";
        check (g3 == 1 && c3 == 1, "computer keys and the target pad are recorded as melodic notes");
    }

    // ---- slices of one chop cut each other (A4 stops A1), and the loop starts again with A1 ----
    {
        auto cp = std::make_unique<RezalicaProcessor> (false);
        auto& c = *cp;
        c.prepareToPlay (48000.0, 480);
        c.chopFileToPads (file, RezalicaProcessor::chop4);   // 2 s sample -> 4 slices of 0.5 s
        const int g = c.getEngine().getPad (0).chokeGroup.load();
        check (g > 0 && c.getEngine().getPad (3).chokeGroup.load() == g, "chopped pads share a choke group");
        auto& sq = c.getSequencer();
        sq.syncToHost = false; sq.bpm = 120.0f;
        sq.length[0] = 16;
        sq.setStep (0, 0, 0, 127);
        sq.setStep (0, 3, 2, 127);                         // A4 on step 3 (0.25 s), while A1 still plays
        for (int i = 0; i < 4; ++i) c.getEngine().getPad (i).end = c.getEngine().getPad (i).start.load() + 0.25f; // 0.5 s slices
        juce::AudioBuffer<float> blk (2, 480);
        juce::MidiBuffer none;
        sq.playPattern();
        for (int i = 0; i < 20; ++i) c.processBlock (blk, none);      // 0.2 s: A1
        const bool a1First = c.getEngine().isPadSounding (0);
        for (int i = 0; i < 15; ++i) c.processBlock (blk, none);      // 0.35 s: A4 started at 0.25 s
        const bool a4CutA1 = ! c.getEngine().isPadSounding (0) && c.getEngine().isPadSounding (3);
        for (int i = 0; i < 175; ++i) c.processBlock (blk, none);     // 2.1 s: the loop started again with A1
        const bool againA1 = c.getEngine().isPadSounding (0) && ! c.getEngine().isPadSounding (3);
        sq.stopPlayback();
        check (a1First && a4CutA1 && againA1, "A4 cuts A1, the next loop starts with A1 again");
        check (c.getEngine().lastSequencedPad.load() >= 0 && c.getEngine().sequencedCounter.load() >= 3,
               "the sample view knows which chop the sequencer plays");
    }

    // ---- mixer: pads -> BUS A-D -> MASTER, effects in the slots, mute / solo, saved with the project ----
    {
        struct HalfGain : public juce::AudioProcessor
        {
            HalfGain() : juce::AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo())) {}
            const juce::String getName() const override { return "Half"; }
            void prepareToPlay (double, int) override {}
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override { b.applyGain (0.5f); }
            double getTailLengthSeconds() const override { return 0.0; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}
        };

        auto mp = std::make_unique<RezalicaProcessor> (false);
        auto& m = *mp;
        m.prepareToPlay (48000.0, 512);
        m.loadFileToPad (0, file);
        m.getEngine().getPad (0).loop = true;
        juce::AudioBuffer<float> blk (2, 512);
        juce::MidiBuffer none;
        auto level = [&]
        {
            m.triggerPadFromUi (0, 1.0f, true);
            float pk = 0.0f;
            for (int i = 0; i < 12; ++i) { m.processBlock (blk, none); if (i > 2) pk = juce::jmax (pk, blk.getMagnitude (0, 0, 512)); }
            m.triggerPadFromUi (0, 0.0f, false);
            for (int i = 0; i < 10; ++i) m.processBlock (blk, none);
            return pk;
        };
        const float dry = level();
        check (m.getEngine().busForPad (0) == 0 && dry > 0.05f, "pads play into MASTER by default");
        m.getEngine().bankBus[0] = 1;                       // bank A -> BUS A (a send bus)
        m.getBus (1).mute = true;
        check (m.getEngine().busForPad (0) == 1 && std::abs (level() - dry) < dry * 0.05f,
               "a pad sent to BUS A still plays dry at full level (also with the bus muted / without effects)");
        m.getBus (1).mute = false;
        m.setSlotProcessor (1, 0, std::make_unique<HalfGain>(), nullptr);
        const float wet = level();
        std::cout << "       mixer: dry " << dry << ", dry + the BUS A effect (send 0 dB) " << wet << "\n";
        check (std::abs (wet - dry * 1.5f) < dry * 0.06f, "BUS A is a send: dry + the effect's return");
        m.getBus (1).gainDb = -60.0f;                        // send all the way down: no effect
        check (std::abs (level() - dry) < dry * 0.05f, "the BUS fader is the send amount (down = no effect)");
        m.getBus (1).gainDb = 0.0f;
        m.getBus (1).slots[0].bypass = true;
        check (std::abs (level() - dry) < dry * 0.05f, "bypass: only the dry sound");
        m.getBus (1).slots[0].bypass = false;
        m.getEngine().getPad (0).outputBus = 2;             // the pad itself -> BUS B (overrides the bank)
        m.getBus (2).solo = true;
        check (m.getEngine().busForPad (0) == 2 && std::abs (level() - dry) < dry * 0.05f, "a pad can go to its own bus; solo plays it");
        m.getEngine().getPad (0).outputBus = -1;            // back to the bank (BUS A), which is not soloed
        check (std::abs (level() - dry) < dry * 0.05f, "solo silences the other buses' effects (the pads stay dry)");
        m.getBus (2).solo = false;
        m.getBus (1).gainDb = -6.0f;
        juce::MemoryBlock st;
        m.getEngine().getPad (5).outputBus = 3;
        m.getStateInformation (st);
        auto r7 = std::make_unique<RezalicaProcessor> (false);
        r7->setStateInformation (st.getData(), (int) st.getSize());
        check (r7->getEngine().bankBus[0].load() == 1 && r7->getEngine().getPad (5).outputBus.load() == 3
                   && std::abs (r7->getBus (1).gainDb.load() + 6.0f) < 1.0e-4f, "bus routing and faders are saved with the project");
        m.clearSlot (1, 0);
        check (m.getSlotProcessor (1, 0) == nullptr, "an effect can be removed from its slot");
    }

    // ---- export options and "unsaved changes" ----
    {
        auto ep2 = std::make_unique<RezalicaProcessor> (false);
        auto& e = *ep2;
        e.prepareToPlay (48000.0, 512);
        e.chopFileToPads (file, RezalicaProcessor::chop8);
        auto& sq = e.getSequencer();
        sq.syncToHost = false; sq.bpm = 120.0f;
        sq.setStep (0, 0, 0, 127);
        sq.setSongStep (0, 0);                                   // 2 bars (32 steps) = 4 s
        sq.effectiveBpm = 120.0f;
        RezalicaProcessor::ExportSettings es;
        es.sampleRate = 44100.0; es.bitDepth = 16; es.mono = true; es.normalize = true; es.tailSeconds = 1.0;
        es.title = "Test Song"; es.artist = "Rezalica";
        auto wf = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_export.wav");
        juce::String err;
        const bool ok = e.exportSong (wf, es, err);
        std::unique_ptr<juce::AudioFormatReader> rd (e.getFormatManager().createReaderFor (wf));
        float peak[1] {}, low[1] {};
        if (rd != nullptr) { juce::Range<float> rg[1]; rd->readMaxLevels (0, rd->lengthInSamples, rg, 1); low[0] = rg[0].getStart(); peak[0] = rg[0].getEnd(); }
        std::cout << "       export: " << (rd ? (int) rd->sampleRate : 0) << " Hz, " << (rd ? (int) rd->bitsPerSample : 0) << " bit, "
                  << (rd ? (int) rd->numChannels : 0) << " ch, " << (rd ? (int) rd->lengthInSamples : 0) << " samples, peak " << juce::jmax (peak[0], -low[0]) << "\n";
        check (ok && rd != nullptr && (int) rd->sampleRate == 44100 && rd->bitsPerSample == 16 && rd->numChannels == 1
                   && (int) rd->lengthInSamples == 44100 * 5 && std::abs (juce::jmax (peak[0], -low[0]) - juce::Decibels::decibelsToGain (-0.3f)) < 0.01f,
               "song export: 44.1 kHz / 16-bit / mono / normalized, length = song + tail");
        check (rd != nullptr && rd->metadataValues.getValue (juce::WavAudioFormat::riffInfoTitle, {}) == "Test Song", "the title is written into the WAV");
        rd.reset();
        wf.deleteFile();
        es.format = RezalicaProcessor::ExportSettings::mp3;
        auto mf = wf.withFileExtension ("mp3");
        check (e.exportSong (mf, es, err) == rz::Mp3Writer::isAvailable(), "MP3 export (Windows encoder) reports whether it is available");
        mf.deleteFile();

        e.markProjectSaved();
        e.selectedPad = 5;
        const bool selectionIsNoChange = ! e.hasUnsavedChanges();
        sq.bpm = 97.0f;
        check (selectionIsNoChange && e.hasUnsavedChanges(), "unsaved changes are noticed (selecting a pad is not a change)");
        e.markProjectSaved();
        check (! e.hasUnsavedChanges(), "after saving there is nothing to save");
    }

    // ---- sampling like a hardware sampler: threshold, fixed length, mono L, through a bus, monitor, keep / retry ----
    {
        struct Half : public juce::AudioProcessor
        {
            Half() : juce::AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo())) {}
            const juce::String getName() const override { return "Half"; }
            void prepareToPlay (double, int) override {}
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override { b.applyGain (0.5f); }
            double getTailLengthSeconds() const override { return 0.0; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}
        };
        auto sp2 = std::make_unique<RezalicaProcessor> (true);
        auto& r = *sp2;
        r.setRateAndBufferSizeDetails (48000.0, 256);
        r.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> blk (2, 256);
        juce::MidiBuffer none;
        auto feed = [&] (int blocks, float left, float right, int quietBlocks)
        {
            for (int i = 0; i < blocks; ++i)
            {
                for (int n = 0; n < 256; ++n)
                {
                    const float sw = std::sin ((float) (i * 256 + n) * 0.05f);
                    blk.setSample (0, n, i < quietBlocks ? 0.001f * sw : left * sw);
                    blk.setSample (1, n, i < quietBlocks ? 0.001f * sw : right * sw);
                }
                r.processBlock (blk, none);
            }
        };

        // threshold -20 dB: a -40 dB signal does not start it, a loud one does; fixed 1 s; mono from the RIGHT input
        RezalicaProcessor::RecordOptions o;
        o.source = rz::AudioRecorder::input;
        o.waitForSignal = true; o.thresholdDb = -20.0f; o.lengthSeconds = 1.0; o.inputMode = 2;
        check (r.startRecording (o), "REC with threshold / length / mono R");
        feed (20, 0.01f, 0.01f, 0);                                       // -40 dB: still waiting
        const bool stillWaiting = r.getRecorder().getState() == rz::AudioRecorder::waiting;
        feed (250, 0.1f, 0.8f, 0);                                        // loud: records 1 s, then stops by itself
        r.processBlock (blk, none);
        check (stillWaiting && r.getRecorder().getState() == rz::AudioRecorder::finished && r.fetchTake() && std::abs (r.getTakeSeconds() - 1.0) < 0.01,
               "threshold waits for the sound; a fixed length stops by itself");
        // the take is mono from the right input: both channels the same, at the right input's level
        // (keep it: name, trim, chop 4)
        const auto kept = r.keepTake (3, "My Take", true, RezalicaProcessor::chop4);
        auto s3 = r.getEngine().getPadSample (3);
        check (kept.getFileName().startsWith ("My Take") && s3 != nullptr && s3->buffer.getMagnitude (0, 0, s3->buffer.getNumSamples()) > 0.7f
                   && std::abs (s3->buffer.getSample (0, 1000) - s3->buffer.getSample (1, 1000)) < 1.0e-6f,
               "KEEP: named WAV, mono from the right input on both channels");
        check (r.getEngine().getPadSample (0) != nullptr && r.getNumSlices() == 4, "KEEP can chop the take onto the pads right away");
        kept.deleteFile();

        // through a bus: the input goes through BUS A's effect (half level) and is recorded with it; RETRY / DISCARD
        r.setSlotProcessor (1, 0, std::make_unique<Half>(), nullptr);
        r.inputThroughBus = 1;
        o.waitForSignal = false; o.inputMode = 0; o.lengthSeconds = 0.5;
        r.startRecording (o);
        feed (100, 0.8f, 0.8f, 0);
        r.processBlock (blk, none);
        check (r.fetchTake(), "a take through BUS A");
        r.previewTake();
        const juce::File none2 = r.keepTake (8, "", false, RezalicaProcessor::chopOff);
        auto s8 = r.getEngine().getPadSample (8);
        const float lvl = s8 != nullptr ? s8->buffer.getMagnitude (0, 0, s8->buffer.getNumSamples()) : 0.0f;
        std::cout << "       through BUS A: take level " << lvl << "\n";
        check (std::abs (lvl - 0.4f) < 0.02f, "sampling through a bus records the input with that bus's effects");
        none2.deleteFile();
        r.inputThroughBus = -1;
        r.startRecording (o);
        feed (100, 0.5f, 0.5f, 0);
        r.processBlock (blk, none);
        r.fetchTake();
        r.discardTake();
        check (! r.hasTake(), "DISCARD / RETRY throw the take away");

        // MPC flow: REC waits at once for the input above the threshold; meanwhile a pad hit only chooses the pad (silent)
        {
            r.loadFileToPad (12, file);
            RezalicaProcessor::RecordOptions w;
            w.source = rz::AudioRecorder::input; w.waitForSignal = true; w.thresholdDb = -30.0f; w.lengthSeconds = 0.5;
            r.padChoiceArmed = true;
            r.startRecording (w);
            const int hits = r.getEngine().hitCounter[12].load();
            r.triggerPadFromUi (12, 1.0f, true);
            feed (10, 0.001f, 0.001f, 0);                       // silence: waiting
            const bool waiting = r.getRecorder().getState() == rz::AudioRecorder::waiting;
            const bool chosenSilently = r.getEngine().lastPlayedPad.load() == 12 && r.getEngine().hitCounter[12].load() == hits;
            feed (120, 0.6f, 0.6f, 0);                          // the music starts: the take starts by itself
            r.processBlock (blk, none);
            check (waiting && chosenSilently && r.getRecorder().getState() == rz::AudioRecorder::finished && r.fetchTake(),
                   "REC waits for the input above the threshold; the pad hit only chooses the pad; the take starts by itself");
            r.padChoiceArmed = false;
            r.discardTake();
        }

        // monitor: the input is heard on the output
        r.monitorInput = true;
        feed (4, 0.5f, 0.5f, 0);
        const float heard = blk.getMagnitude (0, 0, 256);
        r.monitorInput = false;
        feed (4, 0.5f, 0.5f, 0);
        check (heard > 0.3f && blk.getMagnitude (0, 0, 256) < 1.0e-6f, "MONITOR: the input is heard only when it is on");
    }

    // Copy / paste pad
    {
        auto& src = p.getEngine().getPad (2);
        src.gainDb = -4.5f; src.reverse = true; src.effects.reverbOn = true; src.effects.reverbMix = 0.77f;
        p.copyPad (2);
        check (p.canPastePad(), "copy pad");
        p.pastePad (20); // B5
        auto& dst = p.getEngine().getPad (20);
        check (p.getEngine().getPadSample (20) != nullptr && p.getEngine().getPadSample (20) != p.getEngine().getPadSample (2)
               && p.getEngine().getPadSample (20)->buffer.getNumSamples() == p.getEngine().getPadSample (2)->buffer.getNumSamples()
               && std::abs (dst.start.load() - src.start.load()) < 1.0e-6f && std::abs (dst.end.load() - src.end.load()) < 1.0e-6f
               && std::abs (dst.gainDb.load() + 4.5f) < 1.0e-4f && dst.reverse.load()
               && dst.effects.reverbOn.load() && std::abs (dst.effects.reverbMix.load() - 0.77f) < 1.0e-4f,
               "paste pad copies sample, markers, settings and effects");
        src.gainDb = 0.0f; src.reverse = false; src.effects.reset();
        dst.reverse = false;
        p.clearPad (20);
    }

    // State round trip
    juce::MemoryBlock state;
    p.getStateInformation (state);
    RezalicaProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    check (q.getEngine().getPadSample (5) != nullptr, "state restores samples");
    check (q.getEngine().getPad (0).reverse.load(), "state restores pad settings");
    check (q.getEngine().getPad (0).effects.reverbOn.load() && std::abs (q.getEngine().getPad (0).effects.cutoff.load() - 300.0f) < 0.1f
           && ! q.getEngine().getPad (1).effects.anyOn(), "state restores per-pad effects");
    check (q.getSequencer().getStep (0, 0, 4) == 100, "state restores pattern");

    if (snapshotDir.isNotEmpty())
    {
        for (int s = 2; s < 16; s += 4) q.getSequencer().setStep (0, 4, s, 127);
        for (int s = 0; s < 16; s += 2) q.getSequencer().setStep (0, 7, s, 70);
        q.selectedPad = 2;
        q.setTransientSensitivity (0.5f);
        q.addSlicePoint (0.07f);
        q.addSlicePoint (0.25f);
        q.addSlicePoint (0.375f);
        q.getEngine().getPad (2).loop = true;
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
        ed->setVisible (true);
        auto save = [&] (juce::Component& c, const juce::String& name)
        {
            auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
            juce::File f (snapshotDir + "/" + name);
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::cout << "       wrote " << f.getFullPathName() << "\n";
        };
        q.setBankGainDb (0, -4.5f);
        q.setBankMute (1, true);
        ed->resized();
        if (auto* e2 = dynamic_cast<RezalicaEditor*> (ed.get())) juce::ignoreUnused (e2);
        save (*ed, "editor.png");
        {
            rz::RezalicaLookAndFeel bl;
            rz::BankVolumePopup bp (q, 0, nullptr);
            bp.setLookAndFeel (&bl);
            save (bp, "bankvolume.png");
            bp.setLookAndFeel (nullptr);
        }
        {
            auto& qs = q.getSequencer();
            const juce::uint8 vels[] { 127, 96, 64, 32 };
            for (int st = 0; st < 16; ++st) qs.setStep (0, 1, st, vels[st % 4]);
            qs.setStep (0, 2, 3, 127); qs.setNudge (0, 2, 3, -35);
            qs.setStep (0, 2, 11, 127); qs.setNudge (0, 2, 11, 30);
        }
        rz::SequencerPanel panel (q);
        rz::RezalicaLookAndFeel lnf;
        panel.setLookAndFeel (&lnf);
        save (panel, "sequencer.png");
        panel.setLookAndFeel (nullptr);

        {
            auto& qs = q.getSequencer();
            rz::NoteClip::Ptr c = new rz::NoteClip();
            c->mode = rz::NoteClip::melodic;
            c->targetPad = 2;
            const int mel[] { 60, 63, 67, 70, 72, 67, 63, 65 };
            for (int k = 0; k < 8; ++k)
            {
                rz::PianoNote n; n.id = qs.newNoteId(); n.kind = rz::NoteClip::melodic; n.row = mel[k];
                n.start = (float) (k * 2); n.length = k % 3 == 0 ? 3.0f : 1.5f; n.velocity = (juce::uint8) (60 + k * 8);
                c->notes.push_back (n);
            }
            for (int k = 0; k < 16; k += 4)
            {
                rz::PianoNote n; n.id = qs.newNoteId(); n.kind = rz::NoteClip::drum; n.row = k % 8 == 0 ? 0 : 4; n.start = (float) k; n.length = 1.0f;
                c->notes.push_back (n);
            }
            qs.setClip (0, c);
            rz::PianoRollPanel pr (q);
            pr.setLookAndFeel (&lnf);
            pr.setSize (1180, 680);
            save (pr, "pianoroll.png");
            pr.setLookAndFeel (nullptr);

            qs.length[0] = 128;
            rz::SequencerPanel sp128 (q);
            sp128.setLookAndFeel (&lnf);
            save (sp128, "sequencer128.png");
            sp128.setLookAndFeel (nullptr);
            qs.length[0] = 16;
        }
        {
            q.selectedPad = 0;
            q.chopFileToPads (file, RezalicaProcessor::chopTransients);
            q.getSequencer().syncToHost = false;
            q.getSequencer().bpm = 120.0f;
            juce::AudioBuffer<float> blk (2, 512); juce::MidiBuffer mm; q.processBlock (blk, mm);
            rz::WarpSettings ww; ww.enabled = true; ww.lengthIndex = 3; ww.markers = { { 0.25f, 0.4f } };
            q.setWarp (0, ww);
            rz::WaveformView wvw (q);
            wvw.setLookAndFeel (&lnf);
            wvw.setSize (900, 260);
            wvw.setPad (0);
            save (wvw, "warpview.png");
            wvw.setLookAndFeel (nullptr);
        }
        {
            auto& qs = q.getSequencer();
            const int sc[] { 0, 1, 0, 2, 3, 3, 4, 1, 1, 0 };
            for (int sl = 0; sl < 10; ++sl) qs.setSceneSlot (0, sl, sc[sl]);
            qs.patternNames[0] = "Intro"; qs.patternNames[1] = "Verse beat"; qs.patternNames[3] = "Chorus";
            const int songSc[] { 0, 0, -1, 0, 0, 0, -1, 0 };
            for (int st = 0; st < 8; ++st) qs.setSongStep (st, songSc[st]);
            rz::ScenePanel scp (q);
            scp.setLookAndFeel (&lnf);
            save (scp, "scenes.png");
            qs.usedTracks = 2;
            for (int sl = 0; sl < 4; ++sl) qs.setSceneSlot (1, sl, 5 + sl % 2);
            qs.patternNames[5] = "Bass"; qs.patternColours[5] = 0xff4fa3ff;
            for (int st = 0; st < 8; ++st) qs.setSongStep (st, st >= 3 ? 0 : -1, 1);
            qs.songStartStep = 3;
            rz::ScenePanel scp2 (q);
            scp2.setLookAndFeel (&lnf);
            save (scp2, "scenes-tracks.png");
            scp2.setLookAndFeel (nullptr);
            scp.setLookAndFeel (nullptr);
            qs.usedTracks = 1;
            qs.songStartStep = 0;
        }
        {
            rz::ExportDialog xd (q, nullptr);
            xd.setLookAndFeel (&lnf);
            save (xd, "export.png");
            xd.setLookAndFeel (nullptr);
            rz::RecorderPanel rp (q);
            rp.setLookAndFeel (&lnf);
            save (rp, "sampling.png");
            rp.setLookAndFeel (nullptr);
            rz::MasterFxPanel mp (q);
            mp.setLookAndFeel (&lnf);
            save (mp, "masterfx.png");
            mp.setLookAndFeel (nullptr);
            rz::BrowserPanel bp (q);
            bp.setLookAndFeel (&lnf);
            juce::Thread::sleep (300);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
            save (bp, "browser.png");
            bp.setLookAndFeel (nullptr);
        }
        {
            rz::StretchPanel sp (q);
            sp.setLookAndFeel (&lnf);
            save (sp, "stretch.png");
            sp.setLookAndFeel (nullptr);
        }

        rz::WaveformView wv (q);
        wv.setLookAndFeel (&lnf);
        wv.setSize (700, 260);
        wv.setPad (2);
        wv.setVisibleRange (0.2495, 0.006);   // ~530 samples
        save (wv, "zoom-mid.png");
        wv.setVisibleRange (0.2502, 0.0007); // ~60 samples: individual sample points
        save (wv, "zoom-max.png");
        wv.setLookAndFeel (nullptr);
    }

    file.deleteFile();
    longFile.deleteFile();
    {
        // timing: a step on every beat must sound exactly with the metronome click (sample-accurate),
        // also with odd block sizes and with a piano roll note
        auto clickFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_timing.wav");
        {
            juce::AudioBuffer<float> b (1, 4800);
            b.clear();
            for (int i = 0; i < 200; ++i) b.setSample (0, i, 0.9f);
            juce::WavAudioFormat wav;
            clickFile.deleteFile();
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (clickFile), 48000, 1, 24, {}, 0));
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        for (int blockSize : { 512, 441, 1024 })
        {
            RezalicaProcessor t;
            t.prepareToPlay (48000.0, blockSize);
            t.loadFileToPad (0, clickFile);
            auto& sq = t.getSequencer();
            sq.bpm = 96.0f;
            const int spb = 30000; // samples per beat at 96 BPM / 48 kHz
            auto run = [&] (bool steps, bool metro, bool note)
            {
                for (int st = 0; st < 32; ++st) sq.setStep (0, 0, st, steps && st % 4 == 0 ? 127 : 0);
                sq.metronome = metro;
                sq.metronomeLevel = 1.0f;
                rz::NoteClip::Ptr clip = new rz::NoteClip();
                clip->mode = rz::NoteClip::drum;
                if (note)
                {
                    rz::PianoNote n; n.id = sq.newNoteId(); n.kind = rz::NoteClip::drum; n.row = 0; n.start = 8.0f; n.length = 1.0f; n.velocity = 127;
                    clip->notes = { n };
                }
                sq.setClip (0, clip);
                std::vector<int> onsets;
                juce::AudioBuffer<float> buf (2, blockSize);
                long long pos = 0;
                bool above = false;
                sq.internalPlay = true;
                while (pos < 4LL * spb)
                {
                    buf.clear();
                    juce::MidiBuffer m;
                    t.processBlock (buf, m);
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const bool a = std::abs (buf.getSample (0, i)) > 0.05f;
                        if (a && ! above) onsets.push_back ((int) (pos + i));
                        above = a;
                        if (! a && std::abs (buf.getSample (0, i)) < 0.001f) above = false;
                    }
                    pos += blockSize;
                }
                sq.internalPlay = false;
                juce::AudioBuffer<float> flush (2, blockSize);
                for (int k = 0; k < 20; ++k) { juce::MidiBuffer m; t.processBlock (flush, m); }
                return onsets;
            };
            auto pads = run (true, false, false);
            auto clicks = run (false, true, false);
            auto notes = run (false, false, true);
            auto first = [] (const std::vector<int>& v, int from) { for (int x : v) if (x >= from) return x; return -1; };
            const int padBeat2 = first (pads, spb / 2), clickBeat2 = first (clicks, spb / 2), noteAt = first (notes, 0);
            std::cout << "  block " << blockSize << ": pad beat 2 at " << padBeat2 << ", click beat 2 at " << clickBeat2
                      << ", piano roll note (step 8) at " << noteAt << " (expected " << spb << " / " << 2 * spb << ")\n";
            check (std::abs (padBeat2 - spb) <= 2 && std::abs (clickBeat2 - spb) <= 2, "step and metronome click on the same sample");
            check (std::abs (noteAt - 2 * spb) <= 2, "piano roll note exactly on its beat");
        }
    }

    {
        // plug-in delay compensation: an effect with 1000 samples of latency on BUS A or MASTER
        // must not pull the pads out of time with the metronome or with the pads on other buses
        struct Late : public juce::AudioProcessor
        {
            Late() : juce::AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo()))
            { setLatencySamples (1000); }
            juce::AudioBuffer<float> line { 2, 1000 };
            int pos = 0;
            const juce::String getName() const override { return "Late"; }
            void prepareToPlay (double, int) override { line.clear(); pos = 0; }
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float in = b.getSample (ch, i);
                        b.setSample (ch, i, line.getSample (ch, pos));
                        line.setSample (ch, pos, in);
                    }
                    pos = (pos + 1) % 1000;
                }
            }
            double getTailLengthSeconds() const override { return 0.0; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}
        };
        auto clickFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_timing.wav");
        for (int where : { 1, 0 })
        {
            auto tp = std::make_unique<RezalicaProcessor> (false);
            auto& t = *tp;
            t.prepareToPlay (48000.0, 512);
            t.loadFileToPad (0, clickFile);
            t.loadFileToPad (1, clickFile);
            t.getEngine().getPad (0).outputBus = 1;   // pad A1 -> BUS A
            t.getEngine().getPad (1).outputBus = 2;   // pad A2 -> BUS B (no effect)
            t.setSlotProcessor (where, 0, std::make_unique<Late>(), nullptr);
            auto& sq = t.getSequencer();
            sq.bpm = 96.0f;
            const int spb = 30000;
            auto firstOnset = [&] (int pad, bool metro)
            {
                for (int p2 = 0; p2 < 2; ++p2)
                    for (int st = 0; st < 32; ++st) sq.setStep (0, p2, st, p2 == pad && st % 4 == 0 ? 127 : 0);
                sq.metronome = metro;
                sq.metronomeLevel = 1.0f;
                juce::AudioBuffer<float> buf (2, 512);
                long long pos = 0;
                int found = -1;
                sq.internalPlay = true;
                while (pos < 2LL * spb && found < 0)
                {
                    juce::MidiBuffer m;
                    buf.clear();
                    t.processBlock (buf, m);
                    for (int i = 0; i < 512 && found < 0; ++i)
                        if (pos + i > spb / 2 && std::abs (buf.getSample (0, i)) > 0.05f) found = (int) (pos + i);
                    pos += 512;
                }
                sq.internalPlay = false;
                juce::AudioBuffer<float> flush (2, 512);
                for (int k = 0; k < 120; ++k) { juce::MidiBuffer m; flush.clear(); t.processBlock (flush, m); }
                return found;
            };
            const int a = firstOnset (0, false), b = firstOnset (1, false), c = firstOnset (-1, true);
            std::cout << "  latency 1000 on " << (where == 0 ? "MASTER" : "BUS A") << ": BUS A pad " << a << ", BUS B pad " << b
                      << ", click " << c << " (reported " << t.pluginLatencySamples.load() << ")\n";
            check (std::abs (a - (spb + 1000)) <= 2 && std::abs (b - a) <= 2 && std::abs (c - a) <= 2,
                   where == 0 ? "MASTER plug-in delay: pads and metronome stay together" : "BUS A plug-in delay: other buses and metronome wait for it");
        }
    }

    {
        // STOP: once = the sample (one-shot, no GATE) fades out with RELEASE; twice = cut at once.
        // Ruler: a jump into the middle of a long piano roll note plays it from there.
        auto longFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_long.wav");
        {
            juce::AudioBuffer<float> b (1, 48000 * 4);
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.5f * std::sin ((float) i * 0.03f));
            juce::WavAudioFormat wav;
            longFile.deleteFile();
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (longFile), 48000, 1, 24, {}, 0));
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        auto tp = std::make_unique<RezalicaProcessor> (false);
        auto& t = *tp;
        t.prepareToPlay (48000.0, 480);
        t.loadFileToPad (0, longFile);
        t.getEngine().getPad (0).releaseMs = 30.0f;
        auto& sq = t.getSequencer();
        sq.bpm = 120.0f;
        sq.setStep (0, 0, 0, 127);
        juce::AudioBuffer<float> buf (2, 480);
        auto blocks = [&] (int n) { float pk = 0.0f; for (int i = 0; i < n; ++i) { juce::MidiBuffer m; buf.clear(); t.processBlock (buf, m); pk = buf.getMagnitude (0, 0, 480); } return pk; };

        sq.playPattern();
        blocks (20);                                   // 0.2 s into a 4 s one-shot sample
        sq.stopPressed();
        const float afterFade = blocks (10);           // 100 ms later: RELEASE 30 ms is over
        check (afterFade < 1.0e-4f, "STOP once: a one-shot sample fades out with its RELEASE");

        t.getEngine().getPad (0).releaseMs = 2000.0f;  // a long release ...
        juce::Time::waitForMillisecondCounter (juce::Time::getMillisecondCounter() + sq.doubleStopMs + 20);
        sq.playPattern();
        blocks (20);
        sq.stopPressed();
        const float stillRinging = blocks (3);
        sq.stopPressed();                              // ... and STOP again right away
        const float cut = blocks (2);
        check (stillRinging > 0.05f && cut < 1.0e-4f, "STOP twice: everything is cut at once");
        sq.setStep (0, 0, 0, 0);
        t.getEngine().getPad (0).releaseMs = 30.0f;

        // piano roll: one 4-bar note; start from the ruler at bar 2 -> it plays from its middle
        rz::NoteClip::Ptr clip = new rz::NoteClip();
        rz::PianoNote n; n.id = sq.newNoteId(); n.kind = rz::NoteClip::drum; n.row = 0; n.start = 0.0f; n.length = 64.0f; n.velocity = 127;
        clip->notes = { n };
        sq.length[0] = 64;
        sq.setClip (0, clip);
        juce::Time::waitForMillisecondCounter (juce::Time::getMillisecondCounter() + sq.doubleStopMs + 20);
        sq.locateTo (16.0);                            // bar 2
        sq.playPattern();
        const float chased = blocks (5);
        check (chased > 0.05f, "a jump into the middle of a piano roll note plays it from there");
        sq.locateTo (40.0);                            // jump while playing
        blocks (3);
        check (std::abs (sq.getPlayPositionSteps() - 40.0) < 1.0, "a click in the ruler while playing jumps the playhead");
        sq.stopPressed();
        blocks (10);
        sq.locateTo (0.0);
    }

    {
        // DECK: the motor spins the record up, the hand stops / reverses it, the fader cuts, the brake stops it,
        // SYNC follows the tempo, REC puts the scratch on a pad
        auto longFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_long.wav");
        auto tp = std::make_unique<RezalicaProcessor> (false);
        auto& t = *tp;
        t.prepareToPlay (48000.0, 480);
        auto& deck = t.getDeck();
        check (t.loadFileToDeck (longFile) && deck.motorOn.load(), "a sample dropped on the DECK starts turning");
        check (rz::Turntable::bpmFromName ("looperman-l-6328942-0408332-boom-bap-drum-85-bpm-hd") == 85.0f
               && rz::Turntable::bpmFromName ("Break 95bpm") == 95.0f && rz::Turntable::bpmFromName ("BPM_120 loop") == 120.0f
               && rz::Turntable::bpmFromName ("Cold Hearted Revenge 3") == 0.0f, "the sample's BPM is read from its name");
        juce::AudioBuffer<float> buf (2, 480);
        auto run = [&] (double seconds) { float pk = 0.0f; for (int i = 0; i < (int) (seconds * 100); ++i) { juce::MidiBuffer m; buf.clear(); t.processBlock (buf, m); pk = buf.getMagnitude (0, 0, 480); } return pk; };
        const float playing = run (0.8);
        check (playing > 0.05f && std::abs (deck.speed.load() - 1.0f) < 0.02f, "the motor brings it to normal speed");

        deck.handOffset = 0.0;
        deck.touched = true;                           // the hand holds the record
        const float held = run (0.1);
        check (held < 1.0e-3f && std::abs (deck.speed.load()) < 0.01f, "holding the record stops the sound");
        double off = 0.0;
        for (int i = 0; i < 20; ++i) { off -= 480.0 * 1.5; deck.handOffset = off; run (0.01); } // pull it back, 1.5 x speed
        check (deck.speed.load() < -1.0f, "pulling the record back plays it backwards");
        deck.touched = false;
        run (0.4);
        check (std::abs (deck.speed.load() - 1.0f) < 0.02f, "let go: the motor takes it back to speed");
        deck.reverse = true;
        const float backwards = run (0.9);
        std::cout << "       reverse: level " << backwards << ", speed " << deck.speed.load() << "\n";
        check (backwards > 0.05f && std::abs (deck.speed.load() + 1.0f) < 0.02f, "REVERSE: the motor turns the record backwards");
        deck.reverse = false;
        run (0.4);

        deck.cut = true;
        check (run (0.05) < 1.0e-4f, "SHIFT / CUT closes the fader");
        deck.cut = false;

        t.getSequencer().bpm = 120.0f;
        deck.recordBpm = 100.0f;
        deck.sync = true;
        run (0.5);
        check (std::abs (deck.speed.load() - 1.2f) < 0.02f, "SYNC: the record turns at the sequencer tempo");
        deck.sync = false;

        deck.startRecording();
        run (0.5);
        deck.stopRecording();
        run (0.02);
        const auto file = t.collectDeckRecording (3);
        check (file.existsAsFile() && t.getEngine().getPadSample (3) != nullptr, "the DECK's REC puts the scratch on a pad");
        file.deleteFile();

        {
            // an effect in a DECK slot processes the deck (not the pads); the volume knob is the deck's own
            struct HalfFx : public juce::AudioProcessor
            {
                HalfFx() : juce::AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo())) {}
                const juce::String getName() const override { return "Half"; }
                void prepareToPlay (double, int) override {}
                void releaseResources() override {}
                void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override { b.applyGain (0.5f); }
                double getTailLengthSeconds() const override { return 0.0; }
                bool acceptsMidi() const override { return false; }
                bool producesMidi() const override { return false; }
                juce::AudioProcessorEditor* createEditor() override { return nullptr; }
                bool hasEditor() const override { return false; }
                int getNumPrograms() override { return 1; }
                int getCurrentProgram() override { return 0; }
                void setCurrentProgram (int) override {}
                const juce::String getProgramName (int) override { return {}; }
                void changeProgramName (int, const juce::String&) override {}
                void getStateInformation (juce::MemoryBlock&) override {}
                void setStateInformation (const void*, int) override {}
            };
            const float dryDeck = run (0.3);
            t.setSlotProcessor (rz::deckChain, 0, std::make_unique<HalfFx>(), nullptr);
            const float wetDeck = run (0.3);
            std::cout << "       deck: dry " << dryDeck << ", through a DECK effect " << wetDeck << "\n";
            check (std::abs (wetDeck - dryDeck * 0.5f) < dryDeck * 0.05f && rz::busName (rz::deckChain) == "DECK",
                   "an effect in a DECK slot processes the deck");
            t.clearSlot (rz::deckChain, 0);
        }

        deck.motorOn = false;
        run (1.5);
        check (std::abs (deck.speed.load()) < 0.01f, "STOP: the brake stops the platter");

        // a very slow scratch with the motor off: the mouse moves in jumps every 16 ms, the sound must stay smooth
        {
            deck.handOffset = 0.0;
            deck.touched = true;
            run (0.05);
            juce::AudioBuffer<float> small (2, 256);
            double hand = 0.0;
            long long done = 0, nextMove = 0;
            float worst = 0.0f, peak = 0.0f, y1 = 0.0f, y2 = 0.0f;
            for (int blk = 0; blk < 400; ++blk)
            {
                if (done >= nextMove) { hand += 0.04 * 768.0; deck.handOffset = hand; nextMove += 768; } // 4 % speed, 60 Hz mouse
                juce::MidiBuffer m;
                small.clear();
                t.processBlock (small, m);
                for (int i = 0; i < 256; ++i)
                {
                    const float y = small.getSample (0, i);
                    if (blk > 20) worst = juce::jmax (worst, std::abs (y - 2.0f * y1 + y2)); // jumps = crackle
                    peak = juce::jmax (peak, std::abs (y));
                    y2 = y1; y1 = y;
                }
                done += 256;
            }
            deck.touched = false;
            std::cout << "       slow scratch: peak " << peak << ", largest jump " << worst << "\n";
            check (peak > 0.05f && worst < 2.0e-3f, "a very slow scratch is smooth (no crackle)");
        }
    }

    {
        // NORMALIZE (peak): the loudest peak between the markers goes to the target, one gain for everything
        auto nf = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_norm.wav");
        {
            juce::AudioBuffer<float> b (1, 48000);
            for (int i = 0; i < 48000; ++i) b.setSample (0, i, (i < 24000 ? 0.25f : 0.1f) * std::sin ((float) i * 0.05f));
            juce::WavAudioFormat wav;
            nf.deleteFile();
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (nf), 48000, 1, 24, {}, 0));
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        auto np = std::make_unique<RezalicaProcessor> (false);
        auto& n = *np;
        n.prepareToPlay (48000.0, 512);
        n.loadFileToPad (0, nf);
        n.getEngine().setPadSample (1, n.getEngine().getPadSample (0)); // a second pad on the same sample
        const auto before = n.getEngine().getPadSample (0);
        const double change = n.normalizePad (0, -1.0f);
        const auto after = n.getEngine().getPadSample (0);
        const float newPeak = after->buffer.getMagnitude (0, 0, after->buffer.getNumSamples());
        const float quietBefore = before->buffer.getMagnitude (0, 30000, 10000), quietAfter = after->buffer.getMagnitude (0, 30000, 10000);
        std::cout << "       normalize: " << change << " dB, peak " << juce::Decibels::gainToDecibels (newPeak) << " dBFS\n";
        check (std::abs (juce::Decibels::gainToDecibels (newPeak) + 1.0f) < 0.05f, "NORMALIZE: the loudest peak lands on -1 dBFS");
        check (std::abs (quietAfter / newPeak - quietBefore / before->buffer.getMagnitude (0, 0, 48000)) < 0.01f,
               "NORMALIZE keeps the dynamics (one gain for the whole sample)");
        check (n.getEngine().getPadSample (1) == before && after->file.existsAsFile()
               && after->file.getParentDirectory().getFileName() == "Bounced" && n.getChopSource() == after,
               "NORMALIZE bounces: a new WAV on this pad only, loaded into the sample view");
        // only the part between the markers counts (and is bounced): the second half (peak 0.1) goes to -3 dBFS
        n.getEngine().getPad (1).start = 0.5f;
        n.normalizePad (1, -3.0f);
        auto half = n.getEngine().getPadSample (1);
        check (std::abs (juce::Decibels::gainToDecibels (half->buffer.getMagnitude (0, 0, half->buffer.getNumSamples())) + 3.0f) < 0.05f
               && std::abs (half->buffer.getNumSamples() - 24000) < 4 && n.getEngine().getPad (1).start.load() == 0.0f,
               "NORMALIZE measures and bounces the part between the markers");
        after->file.deleteFile();
        half->file.deleteFile();
        n.clearPad (2);
        check (std::isnan (n.normalizePad (2, -1.0f)), "an empty pad is not normalized");

        // files made in this session: the ones a saved project uses are never offered for deleting
        {
            n.forgetSessionFiles();
            n.normalizePad (0, -1.0f);
            const auto first = n.getEngine().getPadSample (0)->file;
            check (n.getUnsavedSessionFiles().contains (first), "a bounced file is remembered as made in this session");
            auto proj = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_session_test.rzp");
            n.saveProjectToFile (proj);
            check (n.getUnsavedSessionFiles().isEmpty(), "after saving, the project's files are not offered for deleting");
            n.normalizePad (0, -3.0f);
            const auto second = n.getEngine().getPadSample (0)->file;
            const auto offered = n.getUnsavedSessionFiles();
            check (offered.size() == 1 && offered[0] == second, "a file made after the save (not saved) is offered");
            check (n.trashSessionFiles() == 1 && ! second.existsAsFile() && first.existsAsFile(),
                   "DELETE removes only that file, the saved project's file stays");
            proj.deleteFile();
            first.deleteFile();
        }
    }

    {
        // MIDI: pad note layouts, learning a pad's note, MIDI Learn of a knob / button, clock in and out
        auto mp = std::make_unique<RezalicaProcessor> (false);
        auto& m = *mp;
        m.prepareToPlay (48000.0, 480);
        juce::AudioBuffer<float> buf (2, 480);
        auto block = [&] (juce::MidiBuffer mb) { buf.clear(); m.processBlock (buf, mb); };

        m.setPadNoteLayout (RezalicaProcessor::layoutRezalica);
        check (m.padForNote (36) == 0 && m.padForNote (51) == 15 && m.padForNote (99) == 63 && m.padForNote (35) == -1,
               "pad notes: C1 = A1 ... (64 pads)");
        m.setPadNoteLayout (RezalicaProcessor::layoutMpc);
        m.currentBank = 0;
        const bool mpcA = m.padForNote (37) == 12 && m.padForNote (49) == 0; // MPC pad 1 = bottom left (A13), pad 13 = A1
        m.currentBank = 1;
        check (mpcA && m.padForNote (37) == 28, "pad notes: MPC 2000XL plays the current bank");
        m.currentBank = 0;
        m.setPadNoteLayout (RezalicaProcessor::layoutGm);
        check (m.padForNote (36) == 12 && m.padForNote (38) == 13, "pad notes: General MIDI drums");

        m.learnPadNote = 5;
        { juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); block (mb); }
        check (m.padForNote (60) == 5 && m.getPadNoteLayout() == RezalicaProcessor::layoutCustom && m.learnPadNote.load() == -1,
               "Learn MIDI note: the next note plays that pad");

        // MIDI Learn of a knob and a button (a stand-in editor)
        juce::Component root;
        root.setSize (400, 300);
        juce::Slider knob;
        knob.setRange (0.0, 1.0);
        rz::setLearnId (knob, "test.knob");
        root.addAndMakeVisible (knob);
        juce::TextButton key;
        int clicks = 0;
        key.onClick = [&] { ++clicks; };
        rz::setLearnId (key, "test.key");
        root.addAndMakeVisible (key);
        rz::MidiControl mc (m, root, false);
        mc.clearAll();
        mc.setLearnMode (true);
        mc.selectedId = "test.knob";
        mc.pollNow();
        check (m.midiLearnListening.load(), "MIDI Learn waits for a control");
        { juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::controllerEvent (2, 21, 10), 0); block (mb); }
        mc.pollNow();
        mc.selectedId = "test.key";
        mc.pollNow();
        { juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (1, 70, (juce::uint8) 100), 0); block (mb); }
        mc.pollNow();
        mc.setLearnMode (false);
        check (mc.keyFor ("test.knob") == "CC 21 ch 2" && mc.keyFor ("test.key").startsWith ("Note") && m.noteIsControl[70].load(),
               "MIDI Learn: a CC to a knob, a note to a button");
        { juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::controllerEvent (2, 21, 127), 0);
          mb.addEvent (juce::MidiMessage::noteOn (1, 70, (juce::uint8) 100), 1); block (mb); }
        mc.pollNow();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50); // (a button click is delivered as a message)
        check (std::abs (knob.getValue() - 1.0) < 1.0e-6 && clicks == 1, "the learned controls move the knob and press the button");
        mc.clearAll();

        // quick set-up: knobs one after another
        mc.startWizard (rz::MidiControl::wizardKnobs);
        juce::Slider gain, pan;
        rz::setLearnId (gain, "pad.gain"); rz::setLearnId (pan, "pad.pan");
        root.addAndMakeVisible (gain); root.addAndMakeVisible (pan);
        mc.pollNow();
        { juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::controllerEvent (1, 30, 5), 0); block (mb); }
        mc.pollNow();
        { juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::controllerEvent (1, 31, 5), 0); block (mb); }
        mc.pollNow();
        check (mc.keyFor ("pad.gain") == "CC 30 ch 1" && mc.keyFor ("pad.pan") == "CC 31 ch 1" && mc.selectedId == "pad.pitch",
               "set-up wizard: the knobs are learned one after another");
        mc.setLearnMode (false);
        check (mc.getWizard() == rz::MidiControl::wizardNone, "ESC / MIDI LEARN ends the set-up");
        // quick set-up: pads
        m.currentBank = 0;
        mc.startWizard (rz::MidiControl::wizardPads);
        mc.pollNow();
        { juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (10, 44, (juce::uint8) 90), 0); block (mb); }
        mc.pollNow();
        check (m.padForNote (44) == 0 && m.learnPadNote.load() == 1, "set-up wizard: pad A1 learned, waiting for A2");
        mc.setLearnMode (false);
        mc.clearAll();

        // MMC transport keys: PLAY / STOP without any set-up
        auto mmc = [] (juce::uint8 cmd) { const juce::uint8 d[] = { 0x7f, 0x7f, 0x06, cmd }; return juce::MidiMessage::createSysExMessage (d, 4); };
        { juce::MidiBuffer mb; mb.addEvent (mmc (0x02), 0); block (mb); }
        const bool mmcPlay = m.getSequencer().internalPlay.load();
        { juce::MidiBuffer mb; mb.addEvent (mmc (0x01), 0); block (mb); }
        check (mmcPlay && ! m.getSequencer().internalPlay.load(), "MMC PLAY / STOP from the controller's transport keys");

        // clock in: 24 ticks per beat at 100 BPM, then STOP
        m.clockIn = true;
        {
            const int perTick = 48000 * 60 / (100 * 24); // 1200 samples
            juce::MidiBuffer mb;
            mb.addEvent (juce::MidiMessage::midiStart(), 0);
            block (mb);
            long long pos = 480, next = 0;
            for (int b = 0; b < 400; ++b)
            {
                juce::MidiBuffer t;
                while (next < pos + 480) { t.addEvent (juce::MidiMessage::midiClock(), (int) (next - pos)); next += perTick; }
                block (t);
                pos += 480;
            }
        }
        std::cout << "       clock in: " << m.getSequencer().bpm.load() << " BPM\n";
        check (std::abs (m.getSequencer().bpm.load() - 100.0f) < 0.2f && m.getSequencer().internalPlay.load(),
               "clock in: Rezalica follows the tempo and START");
        { juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::midiStop(), 0); block (mb); }
        check (! m.getSequencer().internalPlay.load(), "clock in: STOP stops it");
        m.clockIn = false;

        // clock out: 24 ticks per beat while playing (120 BPM: 2 beats a second)
        m.getSequencer().bpm = 120.0f;
        m.getSequencer().playPattern();
        int ticks = 0, starts = 0;
        for (int b = 0; b < 100; ++b) // one second
        {
            juce::MidiBuffer none;
            block (none);
            juce::MidiBuffer out;
            m.makeClockMessages (out, 480);
            for (const auto meta : out)
            {
                if (meta.getMessage().isMidiClock()) ++ticks;
                if (meta.getMessage().isMidiStart()) ++starts;
            }
        }
        m.getSequencer().stopPlayback();
        std::cout << "       clock out: " << ticks << " ticks in one second\n";
        check (starts == 1 && std::abs (ticks - 48) <= 1, "clock out: START and 24 ticks per beat");
    }

    {
        // DECK after a restart: empty, unless its record was saved with the project
        auto longFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_long.wav");
        auto a = std::make_unique<RezalicaProcessor> (false);
        a->prepareToPlay (48000.0, 512);
        a->loadFileToDeck (longFile);
        juce::MemoryBlock session;
        a->getStateInformation (session);
        auto b = std::make_unique<RezalicaProcessor> (false);
        b->setStateInformation (session.getData(), (int) session.getSize());
        check (b->getDeck().getRecord() == nullptr, "restart: the deck is empty when the project was not saved");

        auto proj = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_deck_test.rzp");
        a->saveProjectToFile (proj);
        a->getStateInformation (session);
        auto c = std::make_unique<RezalicaProcessor> (false);
        c->setStateInformation (session.getData(), (int) session.getSize());
        check (c->getDeck().getRecord() != nullptr, "restart: the record saved with the project stays on the deck");

        auto d = std::make_unique<RezalicaProcessor> (false);
        check (d->loadProjectFromFile (proj) && d->getDeck().getRecord() != nullptr, "opening the project brings its record");
        d->newProject();
        check (d->getDeck().getRecord() == nullptr, "a new project starts with an empty deck");
        proj.deleteFile();
    }

    {
        // scales: membership, snapping, scale steps, chords; MIDI keys follow the scale in MELODIC
        namespace sc = rz::scales;
        int major = -1, minorPenta = -1, hijaz = -1;
        for (int i = 0; i < (int) sc::all().size(); ++i)
        {
            const juce::String n (sc::all()[(size_t) i].name);
            if (n.startsWith ("Major (Ionian)")) major = i;
            if (n == "Minor Pentatonic") minorPenta = i;
            if (n.startsWith ("Phrygian Dominant")) hijaz = i;
        }
        check (sc::all().size() >= 30 && major > 0 && minorPenta > 0 && hijaz > 0, "scales: the list has all the scales");
        check (sc::contains (major, 0, 64) && ! sc::contains (major, 0, 61) && sc::snap (major, 0, 61) == 60 && sc::snap (major, 0, 66) == 65,
               "scales: C major contains E, not C#; C# -> C, F# -> F");
        check (sc::snap (minorPenta, 9, 62) == 62 && sc::snap (minorPenta, 9, 66) == 67 && ! sc::contains (minorPenta, 9, 71),
               "scales: A minor pentatonic");
        check (sc::contains (hijaz, 2, 63) && sc::contains (hijaz, 2, 66), "scales: D Hijaz has Eb and F#");
        check (sc::stepBy (major, 0, 60, 1) == 62 && sc::stepBy (major, 0, 64, 1) == 65 && sc::stepBy (major, 0, 60, -1) == 59,
               "scales: one scale step up / down");
        const auto dMinor = sc::chord (sc::chordTriad, major, 0, 62);        // D in C major: D F A
        const auto g7 = sc::chord (sc::chordSeventh, major, 0, 67);          // G7: G B D F
        const auto cMajorNoScale = sc::chord (sc::chordTriad, 0, 0, 60);     // no scale: C E G
        check (dMinor == std::vector<int> { 62, 65, 69 } && g7 == std::vector<int> { 67, 71, 74, 77 } && cMajorNoScale == std::vector<int> { 60, 64, 67 },
               "scales: chords built from the scale (Dm, G7) and without a scale (C)");

        auto sp = std::make_unique<RezalicaProcessor> (false);
        auto& sProc = *sp;
        sProc.prepareToPlay (48000.0, 480);
        sProc.scaleType = major;
        sProc.scaleRoot = 0;
        sProc.chromaticPad = 0;            // MIDI KEYS on: the keyboard plays pad A1 melodically
        sProc.getSequencer().pianoRecording = true;
        sProc.getSequencer().playPattern();
        juce::AudioBuffer<float> sb (2, 480);
        { juce::MidiBuffer mb; sb.clear(); sProc.processBlock (sb, mb); }
        { juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100), 0); sb.clear(); sProc.processBlock (sb, mb); }
        for (int k = 0; k < 10; ++k) { juce::MidiBuffer mb; sb.clear(); sProc.processBlock (sb, mb); }
        { juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOff (1, 61), 0); sb.clear(); sProc.processBlock (sb, mb); }
        sProc.getSequencer().stopPlayback();
        sProc.collectRecordedPianoNotes();
        bool recordedC = false;
        if (auto c = sProc.getSequencer().getClip (sProc.getSequencer().currentPattern.load()))
            for (auto& n : c->notes) recordedC = recordedC || (n.kind == rz::NoteClip::melodic && n.row == 60);
        check (recordedC, "MIDI KEYS with a scale: C# is played and recorded as C (C major)");
    }

    {
        // a pad's own effects: they process only that pad, and a plug-in with latency on one pad
        // keeps every pad (and the metronome) in time
        struct Gain : public juce::AudioProcessor
        {
            explicit Gain (float g, int latency = 0)
                : juce::AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo())), gain (g)
            { setLatencySamples (latency); line.setSize (2, juce::jmax (1, latency)); line.clear(); }
            float gain; juce::AudioBuffer<float> line; int pos = 0;
            const juce::String getName() const override { return "Gain"; }
            void prepareToPlay (double, int) override {}
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
            {
                b.applyGain (gain);
                const int L = getLatencySamples();
                if (L <= 0) return;
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    for (int ch = 0; ch < 2; ++ch) { const float in = b.getSample (ch, i); b.setSample (ch, i, line.getSample (ch, pos)); line.setSample (ch, pos, in); }
                    pos = (pos + 1) % L;
                }
            }
            double getTailLengthSeconds() const override { return 0.0; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}
        };
        auto clickFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_timing.wav");
        auto tp = std::make_unique<RezalicaProcessor> (false);
        auto& t = *tp;
        t.prepareToPlay (48000.0, 512);
        t.loadFileToPad (0, clickFile);
        t.loadFileToPad (1, clickFile);
        auto peakOf = [&] (int pad)
        {
            t.triggerPadFromUi (pad, 1.0f, true);
            float pk = 0.0f;
            juce::AudioBuffer<float> buf (2, 512);
            for (int i = 0; i < 6; ++i) { juce::MidiBuffer m; buf.clear(); t.processBlock (buf, m); pk = juce::jmax (pk, buf.getMagnitude (0, 0, 512)); }
            t.triggerPadFromUi (pad, 0.0f, false);
            for (int i = 0; i < 20; ++i) { juce::MidiBuffer m; buf.clear(); t.processBlock (buf, m); }
            return pk;
        };
        const float dry0 = peakOf (0), dry1 = peakOf (1);
        t.setSlotProcessor (rz::padChainBase + 0, 5, std::make_unique<Gain> (0.5f), nullptr); // slot 6 of pad A1
        const float wet0 = peakOf (0), still1 = peakOf (1);
        std::cout << "       pad effects: A1 " << dry0 << " -> " << wet0 << ", A2 " << dry1 << " -> " << still1 << "\n";
        check (std::abs (wet0 - dry0 * 0.5f) < dry0 * 0.05f && std::abs (still1 - dry1) < dry1 * 0.05f && t.padHasEffects (0) && ! t.padHasEffects (1),
               "a pad's own effect processes only that pad");

        // latency 1000 on pad A1's chain: A1 (with it) and A2 (without) still sound together with the metronome
        t.setSlotProcessor (rz::padChainBase + 0, 5, std::make_unique<Gain> (1.0f, 1000), nullptr);
        auto& sq = t.getSequencer();
        sq.bpm = 96.0f;
        auto onset = [&] (int pad, bool metro)
        {
            for (int p2 = 0; p2 < 2; ++p2)
                for (int st = 0; st < 32; ++st) sq.setStep (0, p2, st, p2 == pad && st % 4 == 0 ? 127 : 0);
            sq.metronome = metro;
            sq.metronomeLevel = 1.0f;
            juce::AudioBuffer<float> buf (2, 512);
            long long pos = 0; int found = -1;
            sq.playPattern();
            while (pos < 60000 && found < 0)
            {
                juce::MidiBuffer m; buf.clear(); t.processBlock (buf, m);
                for (int i = 0; i < 512 && found < 0; ++i)
                    if (pos + i > 15000 && std::abs (buf.getSample (0, i)) > 0.05f) found = (int) (pos + i);
                pos += 512;
            }
            sq.stopPlayback(); sq.stopSound = 2;
            juce::AudioBuffer<float> fl (2, 512);
            for (int k = 0; k < 120; ++k) { juce::MidiBuffer m; fl.clear(); t.processBlock (fl, m); }
            return found;
        };
        const int a = onset (0, false), b = onset (1, false), c = onset (-1, true);
        std::cout << "       pad latency 1000: A1 " << a << ", A2 " << b << ", click " << c << "\n";
        check (std::abs (a - b) <= 2 && std::abs (a - c) <= 2 && std::abs (a - 31000) <= 2,
               "a pad effect with latency: all pads and the metronome stay together");
        t.clearSlot (rz::padChainBase + 0, 5);
        check (! t.padHasEffects (0) && rz::busName (rz::padChainBase + 0) == "PAD A1", "removing the effect; chain name");
    }

    {
        std::cout << "VST instrument on a pad\n";
        // a tiny "synth": while a note is held it outputs a DC level = velocity / 127 * (1 + (note - 60) / 100)
        struct Synth : public juce::AudioProcessor
        {
            Synth() : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())) {}
            std::map<int, float> on; std::vector<int> notesSeen;
            const juce::String getName() const override { return "TestSynth"; }
            void prepareToPlay (double, int) override {}
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
            {
                b.clear();
                int pos = 0;
                auto fill = [&] (int to) { float lvl = 0.0f; for (auto& [n, v] : on) lvl += v * (1.0f + (n - 60) / 100.0f);
                                           for (int i = pos; i < to; ++i) for (int ch = 0; ch < b.getNumChannels(); ++ch) b.setSample (ch, i, lvl); pos = to; };
                for (const auto meta : m)
                {
                    fill (meta.samplePosition);
                    auto msg = meta.getMessage();
                    if (msg.isNoteOn()) { on[msg.getNoteNumber()] = msg.getVelocity() / 127.0f; notesSeen.push_back (msg.getNoteNumber()); }
                    else if (msg.isNoteOff()) on.erase (msg.getNoteNumber());
                    else if (msg.isAllNotesOff()) on.clear();
                }
                fill (b.getNumSamples());
            }
            double getTailLengthSeconds() const override { return 0.0; }
            bool acceptsMidi() const override { return true; }
            bool producesMidi() const override { return false; }
            bool isMidiEffect() const override { return false; }
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}
        };
        auto tp = std::make_unique<RezalicaProcessor> (false);
        auto& t = *tp;
        t.prepareToPlay (48000.0, 512);
        auto synthOwned = std::make_unique<Synth>();
        auto* synth = synthOwned.get();
        juce::String err;
        check (t.setInstrumentProcessor (5, std::move (synthOwned), nullptr, err) && t.padHasInstrument (5) && t.getPadName (5) == "TestSynth",
               "an instrument goes on pad A6 and names it");
        auto block = [&] { juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer m; buf.clear(); t.processBlock (buf, m); return buf.getMagnitude (0, 0, 512); };
        const float silent = block();
        t.triggerPadFromUi (5, 1.0f, true);
        block();
        const float playing = block();
        t.triggerPadFromUi (5, 0.0f, false);
        block();
        const float released = block();
        std::cout << "       instrument: " << silent << " -> " << playing << " -> " << released << "\n";
        check (silent < 1.0e-6f && playing > 0.5f && released < 1.0e-6f && ! synth->notesSeen.empty() && synth->notesSeen.back() == 60,
               "hitting the pad plays the instrument (C3), letting go stops it");

        // MIDI keys on that pad: the key's note goes to the instrument
        t.chromaticPad = 5;
        juce::MidiBuffer keys;
        keys.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 127), 0);
        { juce::AudioBuffer<float> buf (2, 512); buf.clear(); t.processBlock (buf, keys); }
        check (! synth->notesSeen.empty() && synth->notesSeen.back() == 67 && block() > 0.5f, "MIDI keys play the instrument's notes");
        juce::MidiBuffer up;
        up.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
        { juce::AudioBuffer<float> buf (2, 512); buf.clear(); t.processBlock (buf, up); }
        check (block() < 1.0e-6f, "the key's note-off stops it");
        t.chromaticPad = -1;

        // the selected pad has the instrument: the MIDI keyboard plays it even without MIDI KEYS
        // (and with MIDI KEYS on another, empty pad)
        for (int keysPad : { -1, 0 })
        {
            t.chromaticPad = keysPad;
            t.selectedPad = 5;
            juce::MidiBuffer k2;
            k2.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
            { juce::AudioBuffer<float> buf (2, 512); buf.clear(); t.processBlock (buf, k2); }
            const bool heard = synth->notesSeen.back() == 64 && block() > 0.3f;
            t.selectedPad = 0; // selecting another pad while the key is held: its note-off still reaches the instrument
            juce::MidiBuffer u2;
            u2.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
            { juce::AudioBuffer<float> buf (2, 512); buf.clear(); t.processBlock (buf, u2); }
            check (heard && block() < 1.0e-6f, keysPad < 0 ? "selected instrument pad: the MIDI keyboard plays it" : "... also when MIDI KEYS points at an empty pad");
        }
        t.chromaticPad = -1;

        // the piano roll's MELODIC pad (empty) follows the instrument pad
        check (t.aimMelodicAt (5) && t.getSequencer().getClip (t.getSequencer().currentPattern.load())->targetPad == 5,
               "the piano roll's empty MELODIC pad becomes the instrument pad");

        // STOP twice: everything stops
        t.triggerPadFromUi (5, 1.0f, true);
        block();
        t.getSequencer().stopSound = 2;
        block();
        check (block() < 1.0e-6f, "STOP x2 silences the instrument");

        t.clearInstrument (5);
        check (! t.padHasInstrument (5) && t.getPadName (5).isEmpty(), "removing the instrument");
    }

    {
        std::cout << "Plug-ins that are not loaded stay in the project\n";
        // a project with an effect on BUS A and an instrument on pad A3 that this computer does not have
        juce::PluginDescription fx, synth;
        fx.name = "NoSuchEffect"; fx.pluginFormatName = "VST3"; fx.fileOrIdentifier = "/nowhere/NoSuchEffect.vst3"; fx.uniqueId = 123;
        synth.name = "NoSuchSynth"; synth.pluginFormatName = "VST3"; synth.fileOrIdentifier = "/nowhere/NoSuchSynth.vst3"; synth.uniqueId = 456; synth.isInstrument = true;
        auto tp = std::make_unique<RezalicaProcessor> (false);
        auto& t = *tp;
        juce::MemoryBlock base;
        t.getStateInformation (base);
        auto root = juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (base.getData(), (int) base.getSize()));
        auto mixer = root.getChildWithName ("MIXER");
        for (auto b : mixer)
            if ((int) b.getProperty ("index") == 1)
            {
                juce::ValueTree st ("SLOT");
                st.setProperty ("index", 2, nullptr);
                st.setProperty ("state", juce::String ("abc"), nullptr);
                st.appendChild (juce::ValueTree::fromXml (*fx.createXml()), nullptr);
                b.appendChild (st, nullptr);
            }
        auto inst = root.getChildWithName ("INSTRUMENTS");
        juce::ValueTree it ("INSTRUMENT");
        it.setProperty ("pad", 2, nullptr);
        it.appendChild (juce::ValueTree::fromXml (*synth.createXml()), nullptr);
        inst.appendChild (it, nullptr);
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_pending.rezalica");
        root.createXml()->writeTo (file);

        for (bool without : { true, false })
        {
            check (t.loadProjectFromFile (file, without), without ? "opened without plug-ins" : "opened normally");
            check (t.isSlotPending (1, 2) && t.getSlotName (1, 2) == "(not loaded) NoSuchEffect"
                   && t.isInstrumentPending (2) && t.getPadName (2) == "(not loaded) NoSuchSynth" && t.countPendingPlugins() == 2,
                   "the effect and the instrument wait in their slot / pad");
            juce::MemoryBlock again;
            t.getStateInformation (again);
            const auto x = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize())->toString();
            check (x.contains ("NoSuchEffect") && x.contains ("NoSuchSynth") && x.contains ("abc"), "saving keeps them (with their settings)");
            juce::String err;
            check (! t.loadPendingSlot (1, 2, err) && err.isNotEmpty() && t.isSlotPending (1, 2), "loading a missing one fails, it stays waiting");
        }
        t.clearInstrument (2);
        t.getBus (1).slots[2].pending = {};
        check (t.countPendingPlugins() == 0, "removing them");

        // Save keeps the previous file as .bak
        auto proj = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rezalica_bak_test.rezalica");
        proj.deleteFile(); proj.getSiblingFile (proj.getFileName() + ".bak").deleteFile();
        t.saveProjectToFile (proj);
        t.saveProjectToFile (proj);
        check (proj.getSiblingFile (proj.getFileName() + ".bak").existsAsFile(), "Save keeps the previous save as .bak");
        file.deleteFile(); proj.deleteFile(); proj.getSiblingFile (proj.getFileName() + ".bak").deleteFile();
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
