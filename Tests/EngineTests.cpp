// Headless smoke test: chops a generated sample, plays it via MIDI and the sequencer, checks output.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
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
        save (*ed, "editor.png");
        rz::SequencerPanel panel (q);
        rz::RezalicaLookAndFeel lnf;
        panel.setLookAndFeel (&lnf);
        save (panel, "sequencer.png");
        panel.setLookAndFeel (nullptr);

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
