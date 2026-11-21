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

/** BUS A-D and MASTER: 4 effect slots, a fader, mute / solo and a level meter. */
struct MixerBus
{
    static constexpr int numSlots = 4;
    std::atomic<float> gainDb { 0.0f };   // the MASTER fader is the "master" parameter instead
    std::atomic<bool> mute { false }, solo { false };
    std::atomic<float> peak { 0.0f };
    float lastGain = 1.0f;
    std::array<MixerSlot, numSlots> slots;
    juce::SpinLock lock;
};

inline juce::String busName (int bus) { return bus == 0 ? juce::String ("MASTER") : "BUS " + juce::String::charToString ((juce::juce_wchar) ('A' + bus - 1)); }
} // namespace rz
