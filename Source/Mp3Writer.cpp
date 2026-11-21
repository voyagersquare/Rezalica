#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
 #ifdef _MSC_VER
  #pragma comment (lib, "mfplat.lib")
  #pragma comment (lib, "mfreadwrite.lib")
  #pragma comment (lib, "mfuuid.lib")
  #pragma comment (lib, "ole32.lib")
 #endif
#endif

#include "Mp3Writer.h"
#include <vector>

namespace rz
{
namespace
{
/** ID3v1 tag (128 bytes at the end of the file): title, artist. */
void appendId3v1 (const juce::File& file, const juce::String& title, const juce::String& artist)
{
    if (title.isEmpty() && artist.isEmpty()) return;
    char tag[128] {};
    tag[0] = 'T'; tag[1] = 'A'; tag[2] = 'G';
    auto put = [&tag] (int at, int len, const juce::String& s)
    {
        const auto latin = s.toStdString();
        for (int i = 0; i < len && i < (int) latin.size(); ++i) tag[at + i] = latin[(size_t) i];
    };
    put (3, 30, title);
    put (33, 30, artist);
    put (63, 30, "Rezalica");
    tag[127] = (char) 255; // no genre
    juce::FileOutputStream out (file);
    if (out.openedOk())
    {
        out.setPosition (file.getSize());
        out.write (tag, 128);
    }
}
} // namespace

#if defined (_WIN32)
namespace
{
template <typename T> void safeRelease (T*& p) { if (p != nullptr) { p->Release(); p = nullptr; } }
}

bool Mp3Writer::isAvailable() { return true; }

bool Mp3Writer::write (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, int kbps,
                       const juce::String& title, const juce::String& artist, juce::String& error)
{
    const UINT32 sr = (UINT32) juce::roundToInt (sampleRate);
    const UINT32 chans = (UINT32) juce::jlimit (1, 2, audio.getNumChannels());
    if (sr != 44100 && sr != 48000) { error = "MP3 needs 44.1 or 48 kHz"; return false; }

    const HRESULT coInit = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED (MFStartup (MF_VERSION, MFSTARTUP_FULL)))
    {
        error = "Windows Media Foundation is not available";
        if (SUCCEEDED (coInit)) CoUninitialize();
        return false;
    }

    file.deleteFile();
    IMFSinkWriter* writer = nullptr;
    IMFMediaType* outType = nullptr;
    IMFMediaType* inType = nullptr;
    DWORD stream = 0;
    bool ok = false;

    auto fail = [&error] (const char* what, HRESULT hr) { error = juce::String (what) + " (0x" + juce::String::toHexString ((int) hr) + ")"; };

    HRESULT hr = MFCreateSinkWriterFromURL (file.getFullPathName().toWideCharPointer(), nullptr, nullptr, &writer);
    if (FAILED (hr)) fail ("Cannot create the MP3 file", hr);
    else
    {
        MFCreateMediaType (&outType);
        outType->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        outType->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_MP3);
        outType->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, sr);
        outType->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, chans);
        outType->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) (kbps * 1000 / 8));
        hr = writer->AddStream (outType, &stream);
        if (FAILED (hr)) fail ("This bitrate / sample rate is not supported by the MP3 encoder", hr);
        else
        {
            MFCreateMediaType (&inType);
            inType->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            inType->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
            inType->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            inType->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, sr);
            inType->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, chans);
            inType->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, chans * 2);
            inType->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sr * chans * 2);
            hr = writer->SetInputMediaType (stream, inType, nullptr);
            if (FAILED (hr)) fail ("The MP3 encoder does not accept the audio", hr);
            else if (FAILED (hr = writer->BeginWriting())) fail ("Cannot start the MP3 encoder", hr);
            else
            {
                const int total = audio.getNumSamples();
                const int chunk = 4096;
                std::vector<short> pcm ((size_t) chunk * chans);
                ok = true;
                for (int pos = 0; pos < total && ok; pos += chunk)
                {
                    const int n = juce::jmin (chunk, total - pos);
                    for (int i = 0; i < n; ++i)
                        for (UINT32 ch = 0; ch < chans; ++ch)
                        {
                            const float v = juce::jlimit (-1.0f, 1.0f, audio.getSample ((int) ch, pos + i));
                            pcm[(size_t) i * chans + ch] = (short) juce::roundToInt (v * 32767.0f);
                        }
                    const DWORD bytes = (DWORD) (n * (int) chans * 2);
                    IMFMediaBuffer* buffer = nullptr;
                    IMFSample* sample = nullptr;
                    BYTE* dst = nullptr;
                    if (FAILED (MFCreateMemoryBuffer (bytes, &buffer)) || FAILED (buffer->Lock (&dst, nullptr, nullptr)))
                    { ok = false; error = "Out of memory"; safeRelease (buffer); break; }
                    std::memcpy (dst, pcm.data(), bytes);
                    buffer->Unlock();
                    buffer->SetCurrentLength (bytes);
                    MFCreateSample (&sample);
                    sample->AddBuffer (buffer);
                    sample->SetSampleTime ((LONGLONG) ((double) pos * 10000000.0 / sr));
                    sample->SetSampleDuration ((LONGLONG) ((double) n * 10000000.0 / sr));
                    hr = writer->WriteSample (stream, sample);
                    if (FAILED (hr)) { ok = false; fail ("Encoding failed", hr); }
                    safeRelease (sample);
                    safeRelease (buffer);
                }
                if (ok && FAILED (hr = writer->Finalize())) { ok = false; fail ("Cannot finish the MP3 file", hr); }
            }
        }
    }
    safeRelease (inType);
    safeRelease (outType);
    safeRelease (writer);
    MFShutdown();
    if (SUCCEEDED (coInit)) CoUninitialize();
    if (ok) appendId3v1 (file, title, artist);
    return ok;
}
#else
bool Mp3Writer::isAvailable() { return false; }
bool Mp3Writer::write (const juce::File&, const juce::AudioBuffer<float>&, double, int, const juce::String&, const juce::String&, juce::String& error)
{
    juce::ignoreUnused (appendId3v1);
    error = "MP3 export is available in the Windows version";
    return false;
}
#endif
} // namespace rz
