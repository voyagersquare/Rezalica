// Headless smoke test: chops a generated sample, plays it via MIDI and the sequencer, checks output.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/SceneWindow.h"
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
    auto& a2fx = p.getEngine().getPad (1).effects;
    a2fx.filterOn = true; a2fx.cutoff = 200.0f;
    const auto a2Filtered = renderPad (1);
    check (a2Filtered.getMagnitude (0, 0, a2Filtered.getNumSamples()) < a2Dry.getMagnitude (0, 0, a2Dry.getNumSamples()) * 0.9f,
           "A2's own filter changes A2");

    // Saturation: gentle, keeps roughly the same level, adds harmonics
    a2fx.reset();
    for (int type = 0; type < 3; ++type)
    {
        a2fx.reset();
        a2fx.satOn = true;
        (type == 0 ? a2fx.tube : type == 1 ? a2fx.tape : a2fx.solid) = 1.0f;
        const auto sat = renderPad (1);
        const float ratio = sat.getRMSLevel (0, 0, sat.getNumSamples()) / juce::jmax (1.0e-9f, a2Dry.getRMSLevel (0, 0, a2Dry.getNumSamples()));
        float diff = 0.0f;
        for (int i = 0; i < sat.getNumSamples(); ++i)
            diff = juce::jmax (diff, std::abs (sat.getSample (0, i) - a2Dry.getSample (0, i)));
        const char* names[] { "tube", "tape", "solid state" };
        std::cout << "       " << names[type] << " at max: level ratio " << ratio << ", max change " << diff << "\n";
        check (ratio > 0.6f && ratio < 1.4f && diff > 0.005f && std::isfinite (ratio), "saturation changes the sound but keeps the level");
    }
    for (int type = 0; type < 6; ++type)
    {
        a2fx.reset();
        a2fx.filterOn = true; a2fx.filterType = type; a2fx.cutoff = 1000.0f; a2fx.resonance = 1.0f;
        const auto f = renderPad (1);
        check (std::isfinite (f.getMagnitude (0, 0, f.getNumSamples())) && f.getMagnitude (0, 0, f.getNumSamples()) < 4.0f,
               "ladder filter types stay stable at full resonance");
    }
    a2fx.reset();

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
            scp.setLookAndFeel (nullptr);
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
