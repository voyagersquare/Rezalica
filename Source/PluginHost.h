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

    /** Scans the plug-in folders for new / changed plug-ins (the ones already known are not loaded again).
        quiet: the automatic scan when Rezalica starts (only says something when it finds new effects). */
    void startScan (bool quiet = false);
    bool isScanning() const noexcept { return scanner != nullptr || outOfProcess; }
    /** Plug-ins that crashed while being scanned are skipped; this lets them be tried again. */
    void clearFailed() { known.clearBlacklistedFiles(); save(); sendChangeMessage(); }
    /** The plug-in files that crashed / hung while being scanned (they are skipped). */
    juce::StringArray getFailedFiles() const { return known.getBlacklistedFiles(); }
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
    std::unique_ptr<juce::PluginDirectoryScanner> scanner; // in-process scan (only inside a DAW)
    int formatIndex = 0;

    // standalone app: every plug-in file is scanned by a separate copy of Rezalica, so a crash
    // only loses that one plug-in (it is marked as failed) and the scan goes on
    struct Job { juce::String file; int format = 0; bool retry = false; std::unique_ptr<juce::ChildProcess> process; juce::File result; double started = 0.0; };
    bool outOfProcess = false;
    juce::StringArray pendingFiles, retryFiles;   // retryFiles: failed once, tried again alone at the end
    juce::Array<int> pendingFormats, retryFormats;
    bool quietScan = false;
    int effectsBefore = 0;
    std::vector<Job> running;
    int total = 0, done = 0, failed = 0;
    void startNextJobs();
    void finishJob (Job&, bool crashed);
    juce::String status;
    float progress = 0.0f;
};
} // namespace rz
