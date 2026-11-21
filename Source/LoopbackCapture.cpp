#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #ifdef _MSC_VER
  #pragma comment (lib, "ole32.lib")
 #endif
#endif

#include "LoopbackCapture.h"
#include <atomic>
#include <algorithm>
#include <vector>

namespace rz
{
#if defined (_WIN32)
namespace
{
template <typename T> void safeRelease (T*& p) { if (p != nullptr) { p->Release(); p = nullptr; } }
}

struct LoopbackCapture::Impl : public juce::Thread
{
    explicit Impl (LoopbackCapture& o) : juce::Thread ("Rezalica loopback"), owner (o) {}
    ~Impl() override { stopThread (3000); }

    void run() override
    {
        const HRESULT coInit = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice* device = nullptr;
        IAudioClient* client = nullptr;
        IAudioCaptureClient* capture = nullptr;
        WAVEFORMATEX* format = nullptr;

        auto fail = [&] (const char* what)
        {
            const juce::ScopedLock sl (lock);
            error = what;
        };

        if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &enumerator)))
            fail ("No audio device enumerator");
        else if (FAILED (enumerator->GetDefaultAudioEndpoint (eRender, eConsole, &device)))
            fail ("No default output device");
        else if (FAILED (device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &client)))
            fail ("The output device cannot be opened");
        else if (FAILED (client->GetMixFormat (&format)) || format == nullptr)
            fail ("Unknown output format");
        else if (FAILED (client->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, format, nullptr)))
            fail ("Loopback capture is not supported by this device");
        else if (FAILED (client->GetService (__uuidof (IAudioCaptureClient), (void**) &capture)))
            fail ("No capture service");
        else if (FAILED (client->Start()))
            fail ("Loopback capture could not start");
        else
        {
            rate = (double) format->nSamplesPerSec;
            const int chans = juce::jmax (1, (int) format->nChannels);
            const int bits = (int) format->wBitsPerSample;
            // the shared-mode mix format is (nearly always) 32-bit float; 16-bit PCM is handled too
            bool isFloat = bits == 32;
            if (format->wFormatTag == WAVE_FORMAT_PCM) isFloat = false;
            running = true;

            std::vector<float> left (48000), right (48000);
            // while nothing plays, Windows sends no packets at all: fill those gaps with silence by the clock,
            // so pauses keep their length in a take
            const double startMs = juce::Time::getMillisecondCounterHiRes();
            double delivered = 0.0;
            while (! threadShouldExit())
            {
                Sleep (5);
                UINT32 packet = 0;
                if (FAILED (capture->GetNextPacketSize (&packet))) break;
                if (packet == 0)
                {
                    const double expected = (juce::Time::getMillisecondCounterHiRes() - startMs) * 0.001 * rate.load();
                    if (expected - delivered > rate.load() * 0.05)
                    {
                        const int gap = (int) juce::jmin ((double) left.size(), expected - delivered - rate.load() * 0.02);
                        std::fill (left.begin(), left.begin() + gap, 0.0f);
                        std::fill (right.begin(), right.begin() + gap, 0.0f);
                        if (owner.onAudio) owner.onAudio (left.data(), right.data(), gap);
                        delivered += gap;
                    }
                    continue;
                }
                while (packet > 0 && ! threadShouldExit())
                {
                    BYTE* data = nullptr;
                    UINT32 frames = 0;
                    DWORD flags = 0;
                    if (FAILED (capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr))) break;
                    if ((int) frames > (int) left.size()) { left.resize (frames); right.resize (frames); }
                    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr;
                    for (UINT32 i = 0; i < frames; ++i)
                    {
                        float l = 0.0f, r = 0.0f;
                        if (! silent)
                        {
                            if (isFloat)
                            {
                                const float* f = reinterpret_cast<const float*> (data) + (size_t) i * (size_t) chans;
                                l = f[0]; r = chans > 1 ? f[1] : f[0];
                            }
                            else if (bits == 16)
                            {
                                const short* s = reinterpret_cast<const short*> (data) + (size_t) i * (size_t) chans;
                                l = (float) s[0] / 32768.0f; r = (float) (chans > 1 ? s[1] : s[0]) / 32768.0f;
                            }
                        }
                        left[i] = l; right[i] = r;
                    }
                    capture->ReleaseBuffer (frames);
                    if (owner.onAudio) owner.onAudio (left.data(), right.data(), (int) frames);
                    delivered += frames;
                    if (FAILED (capture->GetNextPacketSize (&packet))) break;
                }
            }
            client->Stop();
        }

        running = false;
        if (format != nullptr) CoTaskMemFree (format);
        safeRelease (capture);
        safeRelease (client);
        safeRelease (device);
        safeRelease (enumerator);
        if (SUCCEEDED (coInit)) CoUninitialize();
    }

    LoopbackCapture& owner;
    std::atomic<bool> running { false };
    std::atomic<double> rate { 48000.0 };
    juce::CriticalSection lock;
    juce::String error;
};

LoopbackCapture::LoopbackCapture() : impl (std::make_unique<Impl> (*this)) {}
LoopbackCapture::~LoopbackCapture() { impl = nullptr; }
bool LoopbackCapture::isAvailable() { return true; }

bool LoopbackCapture::start()
{
    if (impl->isThreadRunning()) return true;
    { const juce::ScopedLock sl (impl->lock); impl->error = {}; }
    impl->startThread();
    for (int i = 0; i < 100 && ! impl->running.load() && impl->isThreadRunning(); ++i)
        juce::Thread::sleep (10); // wait until the device is open (or failed)
    return impl->running.load();
}

void LoopbackCapture::stop()              { impl->stopThread (3000); }
bool LoopbackCapture::isRunning() const   { return impl->running.load(); }
double LoopbackCapture::getSampleRate() const { return impl->rate.load(); }
juce::String LoopbackCapture::getLastError() const { const juce::ScopedLock sl (impl->lock); return impl->error; }

#else
struct LoopbackCapture::Impl {};
LoopbackCapture::LoopbackCapture() = default;
LoopbackCapture::~LoopbackCapture() = default;
bool LoopbackCapture::isAvailable() { return false; }
bool LoopbackCapture::start() { return false; }
void LoopbackCapture::stop() {}
bool LoopbackCapture::isRunning() const { return false; }
double LoopbackCapture::getSampleRate() const { return 48000.0; }
juce::String LoopbackCapture::getLastError() const { return "Only on Windows"; }
#endif
} // namespace rz
