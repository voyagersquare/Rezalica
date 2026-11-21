#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace rz
{
/** MP3 export with the encoder that comes with Windows (Media Foundation): no extra programs needed.
    bitrate in kbps (128, 160, 192, 224, 256, 320); sample rate 44100 or 48000; 1 or 2 channels.
    Title / artist are written as an ID3 tag. */
struct Mp3Writer
{
    static bool isAvailable();
    static bool write (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, int kbps,
                       const juce::String& title, const juce::String& artist, juce::String& error);
};
} // namespace rz
