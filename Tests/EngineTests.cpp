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

    check (renderBlocks (p, 4) == 0.0f, "silent with no samples");

    check (p.chopFileToPads (file, RezalicaProcessor::chopTransients), "load & chop by transients");
    int slicesFound = 0;
    for (int i = 0; i < rz::numPads; ++i)
        if (p.getEngine().getPadSample (i) != nullptr) ++slicesFound;
    std::cout << "       transient slices: " << slicesFound << "\n";
    check (slicesFound == 8, "8 transients detected");
    check (std::abs (p.getEngine().getPad (3).start.load() - 3.0f / 8.0f) < 0.01f, "slice 4 starts near 3/8");

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

    // Reverse + effects shouldn't blow up
    p.getEngine().getPad (0).reverse = true;
    *p.getApvts().getRawParameterValue ("filterOn") = 1.0f;
    *p.getApvts().getRawParameterValue ("lofiOn") = 1.0f;
    *p.getApvts().getRawParameterValue ("delayOn") = 1.0f;
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, RezalicaProcessor::midiBaseNote, (juce::uint8) 127), 0);
    peak = renderBlocks (p, 100, midi);
    check (peak > 0.01f && peak < 4.0f && std::isfinite (peak), "reverse + all effects produce sane output");

    // State round trip
    juce::MemoryBlock state;
    p.getStateInformation (state);
    RezalicaProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    check (q.getEngine().getPadSample (5) != nullptr, "state restores samples");
    check (q.getEngine().getPad (0).reverse.load(), "state restores pad settings");
    check (q.getSequencer().getStep (0, 0, 4) == 100, "state restores pattern");

    if (snapshotDir.isNotEmpty())
    {
        for (int s = 2; s < 16; s += 4) q.getSequencer().setStep (0, 4, s, 127);
        for (int s = 0; s < 16; s += 2) q.getSequencer().setStep (0, 7, s, 70);
        q.selectedPad = 2;
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
    }

    file.deleteFile();
    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
