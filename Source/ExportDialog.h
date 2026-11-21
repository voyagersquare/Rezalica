#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace rz
{
/** FILE > Export Song...: format (WAV / MP3) and its options, before choosing the file. */
class ExportDialog : public juce::Component
{
public:
    ExportDialog (RezalicaProcessor& p, std::function<void (const RezalicaProcessor::ExportSettings&)> onExport);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void update();
    RezalicaProcessor::ExportSettings current() const;
    void close();

    RezalicaProcessor& processor;
    std::function<void (const RezalicaProcessor::ExportSettings&)> exportCallback;
    juce::ComboBox format, rate, depth, bitrate, channels, tail;
    juce::ToggleButton dither { "Dither (recommended for 16-bit)" }, normalize { "Normalize (loudest peak at -0.3 dB)" };
    juce::TextEditor title, artist;
    juce::Label formatL, rateL, depthL, bitrateL, channelsL, tailL, titleL, artistL, info;
    juce::TextButton exportButton { "EXPORT..." }, cancelButton { "CANCEL" };
};
} // namespace rz
