#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SamplerEngine.h"

namespace rz
{
/** One effect slot of a mixer bus: a hosted plug-in (or any AudioProcessor). */
struct MixerSlot
{
    std::unique_ptr<juce::AudioProcessor> owned;   // message thread
    juce::AudioProcessor* live = nullptr;          // what the audio thread runs (guarded by the bus lock)
    juce::PluginDescription desc;                  // to save / reload it
    bool hasDesc = false;
    std::atomic<bool> bypass { false };
    juce::AudioBuffer<float> scratch;              // room for the plug-in's channels
    juce::MidiBuffer midi;
};

/** A fixed delay for plug-in delay compensation: the audio comes out 'delay' samples later.
    It always records, so a change of the delay does not lose what came before. */
struct LatencyDelay
{
    juce::AudioBuffer<float> line;
    int pos = 0;

    void prepare (int maxDelay)
    {
        line.setSize (2, juce::jmax (2, maxDelay + 1));
        line.clear();
        pos = 0;
    }

    void process (float* const* data, int numChannels, int numSamples, int delay)
    {
        const int size = line.getNumSamples();
        delay = juce::jlimit (0, size - 1, delay);
        const int chans = juce::jmin (2, numChannels);
        for (int i = 0; i < numSamples; ++i)
        {
            const int rd = (pos - delay + size) % size;
            for (int ch = 0; ch < chans; ++ch)
            {
                line.setSample (ch, pos, data[ch][i]);
                data[ch][i] = line.getSample (ch, rd);
            }
            pos = (pos + 1) % size;
        }
    }
};

/** BUS A-D and MASTER: 4 effect slots, a fader, mute / solo and a level meter.
    (The same holds the DECK's effects and every pad's own 6 effects.) */
struct MixerBus
{
    static constexpr int numSlots = 4;   // the mixer's buses and the DECK
    static constexpr int maxSlots = 6;   // a pad's own effects
    std::atomic<float> gainDb { 0.0f };   // the MASTER fader is the "master" parameter instead
    std::atomic<bool> mute { false }, solo { false };
    std::atomic<float> peak { 0.0f };
    float lastGain = 1.0f;
    std::array<MixerSlot, maxSlots> slots;
    juce::SpinLock lock;
    int latency = 0;          // audio thread: the delay of its active plug-ins (samples)
    LatencyDelay compensation; // audio thread: lines this bus up with the slowest one
};

inline juce::String busName (int bus)
{
    if (bus == 0) return "MASTER";
    if (bus == deckChain) return "DECK";
    if (bus >= padChainBase) return "PAD " + padLabel (bus - padChainBase);
    return "BUS " + juce::String::charToString ((juce::juce_wchar) ('A' + bus - 1));
}
} // namespace rz
