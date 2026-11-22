// Headless smoke test: chops a generated sample, plays it via MIDI and the sequencer, checks output.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/SceneWindow.h"
#include "../Source/AppSettings.h"
#include "../Source/Mp3Writer.h"
#include "../Source/ExportDialog.h"
#include "../Source/TimeStretch.h"
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

    // Lazy Chop: hit pads while the sample plays (2 s sample, hits at ~0.5 s and ~1.0 s)
    check (p.startLazyChop(), "lazy chop starts");
    check (p.getNumSlices() == 0 && p.getEngine().getPadSample (0) == nullptr, "lazy chop starts with empty pads");
    {
        juce::AudioBuffer<float> buf (2, 512);
        for (int blk = 0; blk < 230; ++blk) // ~2.45 s at 48 kHz, longer than the sample
        {
            juce::MidiBuffer m;
            if (blk == 47 || blk == 94)
                m.addEvent (juce::MidiMessage::noteOn (1, RezalicaProcessor::midiBaseNote + 9, (juce::uint8) 100), 0);
            p.processBlock (buf, m);
            p.updateLazyChop();
        }
    }
    std::cout << "       lazy slices: " << p.getNumSlices() << "  first at " << (p.getNumSlices() > 0 ? p.getSlicePoints()[0] : -1.0f) << "\n";
    check (p.getNumSlices() == 2, "two pad hits made two slices");
    check (p.getNumSlices() == 2 && std::abs (p.getSlicePoints()[0] - 0.25f) < 0.02f && std::abs (p.getSlicePoints()[1] - 0.5f) < 0.02f,
           "lazy slices land at the playhead positions");
    check (p.getEngine().getPadSample (0) != nullptr && p.getEngine().getPadSample (1) != nullptr, "lazy slices fill A1, A2");
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
        check (rz::NoteClip().mode == rz::NoteClip::melodic, "piano roll opens in melodic mode");
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
        m.getEngine().bankBus[0] = 1;                       // bank A -> BUS A
        m.getBus (1).mute = true;
        check (m.getEngine().busForPad (0) == 1 && level() < 1.0e-6f, "a bank sent to BUS A follows its mute");
        m.getBus (1).mute = false;
        m.setSlotProcessor (1, 0, std::make_unique<HalfGain>(), nullptr);
        const float half = level();
        std::cout << "       mixer: dry " << dry << ", through an effect on BUS A " << half << "\n";
        check (std::abs (half - dry * 0.5f) < dry * 0.05f, "the effect in a BUS A slot processes the pads of that bus");
        m.getBus (1).slots[0].bypass = true;
        check (std::abs (level() - dry) < dry * 0.05f, "bypass skips the effect");
        m.getBus (1).slots[0].bypass = false;
        m.getEngine().getPad (0).outputBus = 2;             // the pad itself -> BUS B (overrides the bank)
        m.getBus (2).solo = true;
        check (m.getEngine().busForPad (0) == 2 && std::abs (level() - dry) < dry * 0.05f, "a pad can go to its own bus; solo plays it");
        m.getEngine().getPad (0).outputBus = -1;            // back to the bank (BUS A), which is not soloed
        check (level() < 1.0e-6f, "solo silences the other buses");
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
    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
