#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace rz
{
/** Captures what the computer plays ("what you hear") - on Windows through WASAPI loopback of the
    default output device. Runs its own thread; every captured block goes to onAudio
    (stereo float, at getSampleRate()). On other systems it is not available. */
class LoopbackCapture
{
public:
    LoopbackCapture();
    ~LoopbackCapture();

    static bool isAvailable();
    /** Starts capturing (returns false if the device could not be opened). */
    bool start();
    void stop();
    bool isRunning() const;
    double getSampleRate() const;
    juce::String getLastError() const;

    /** Called on the capture thread: left, right, number of samples. */
    std::function<void (const float*, const float*, int)> onAudio;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace rz
