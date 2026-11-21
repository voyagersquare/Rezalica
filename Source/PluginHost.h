#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace rz
{
/** Third-party plug-ins (VST3): the known list, scanning and creating instances.
    One shared instance for the whole app (message thread only). */
class PluginHost : public juce::ChangeBroadcaster,
                   private juce::Timer
{
public:
    PluginHost();
    ~PluginHost() override;

    juce::AudioPluginFormatManager& getFormats() noexcept { return formats; }
    juce::KnownPluginList& getKnownList() noexcept { return known; }

    /** Effect plug-ins (no instruments), sorted by name. */
    juce::Array<juce::PluginDescription> getEffects() const;
    const juce::PluginDescription* findByIdentifier (const juce::String& identifier) const;

    /** Folders that are scanned (default: the system's VST3 folders). */
    juce::FileSearchPath getSearchPath() const;
    void setSearchPath (const juce::FileSearchPath&);

    /** Scans the folders one file per timer tick (plug-ins that crash the scan are skipped next time). */
    void startScan();
    bool isScanning() const noexcept { return scanner != nullptr; }
    juce::String getScanStatus() const { return status; }
    float getScanProgress() const noexcept { return progress; }

    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);

private:
    void timerCallback() override;
    void save();
    static juce::File getListFile();
    static juce::File getDeadMansPedalFile();

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    int formatIndex = 0;
    juce::String status;
    float progress = 0.0f;
};
} // namespace rz
