#include "PluginHost.h"
#include "AppSettings.h"

namespace rz
{
PluginHost::PluginHost()
{
    formats.addDefaultFormats(); // VST3 (and AU on macOS)
    if (auto xml = juce::XmlDocument::parse (getListFile()))
        known.recreateFromXml (*xml);
    // the first scanner threw away plug-ins that only crash when they are unloaded: give those one more try
    if (AppSettings::get().getIntValue ("pluginScanVersion", 1) < 2)
    {
        known.clearBlacklistedFiles();
        AppSettings::get().setValue ("pluginScanVersion", 2);
        AppSettings::get().saveIfNeeded();
        save();
    }
    status = juce::String (getEffects().size()) + " effects";
}

PluginHost::~PluginHost()
{
    stopTimer();
    for (auto& j : running) if (j.process != nullptr) j.process->kill();
    running.clear();
    scanner = nullptr;
}

juce::File PluginHost::getListFile()          { return AppSettings::getFolder().getChildFile ("RezalicaPlugins.xml"); }
juce::File PluginHost::getDeadMansPedalFile() { return AppSettings::getFolder().getChildFile ("RezalicaPluginScan.crash"); }

void PluginHost::save()
{
    if (auto xml = known.createXml())
    {
        getListFile().getParentDirectory().createDirectory();
        xml->writeTo (getListFile());
    }
}

juce::Array<juce::PluginDescription> PluginHost::getEffects() const
{
    juce::Array<juce::PluginDescription> out;
    for (auto& d : known.getTypes())
        if (! d.isInstrument)
            out.add (d);
    std::sort (out.begin(), out.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    return out;
}

const juce::PluginDescription* PluginHost::findByIdentifier (const juce::String& identifier) const
{
    static thread_local juce::PluginDescription found;
    for (auto& d : known.getTypes())
        if (d.createIdentifierString() == identifier)
        {
            found = d;
            return &found;
        }
    return nullptr;
}

juce::FileSearchPath PluginHost::getSearchPath() const
{
    const auto saved = AppSettings::get().getValue ("pluginFolders");
    if (saved.isNotEmpty())
        return juce::FileSearchPath (saved);
    juce::FileSearchPath path;
    for (auto* f : formats.getFormats())
        path.addPath (f->getDefaultLocationsToSearch());
    return path;
}

void PluginHost::setSearchPath (const juce::FileSearchPath& p)
{
    AppSettings::get().setValue ("pluginFolders", p.toString());
    AppSettings::get().saveIfNeeded();
}

void PluginHost::startScan()
{
    if (isScanning() || outOfProcess || formats.getNumFormats() == 0)
    {
        if (formats.getNumFormats() == 0) { status = "Plug-in hosting is not available"; sendChangeMessage(); }
        return;
    }

    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        // list the plug-in files first (fast, nothing is loaded), then scan each one in its own process
        pendingFiles.clear();
        pendingFormats.clear();
        for (int fi = 0; fi < formats.getNumFormats(); ++fi)
        {
            auto* f = formats.getFormat (fi);
            for (auto& file : f->searchPathsForPlugins (getSearchPath(), true, false))
                if (! known.isListingUpToDate (file, *f) && ! known.getBlacklistedFiles().contains (file))
                {
                    pendingFiles.add (file);
                    pendingFormats.add (fi);
                }
        }
        total = pendingFiles.size();
        done = failed = 0;
        outOfProcess = true;
        status = total == 0 ? "Nothing new to scan" : "Scanning 0 / " + juce::String (total);
        progress = 0.0f;
        sendChangeMessage();
        startTimer (50);
        return;
    }

    formatIndex = 0;
    scanner = std::make_unique<juce::PluginDirectoryScanner> (known, *formats.getFormat (0), getSearchPath(), true, getDeadMansPedalFile(), false);
    status = "Scanning...";
    progress = 0.0f;
    sendChangeMessage();
    startTimer (20);
}

void PluginHost::startNextJobs()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    while ((int) running.size() < 3 && ! pendingFiles.isEmpty())
    {
        Job j;
        j.file = pendingFiles[0];
        j.format = pendingFormats[0];
        pendingFiles.remove (0);
        pendingFormats.remove (0);
        j.result = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("rezalica_scan", ".xml", false);
        j.process = std::make_unique<juce::ChildProcess>();
        j.started = juce::Time::getMillisecondCounterHiRes();
        status = "Scanning " + juce::String (done + 1) + " / " + juce::String (total) + ":  " + juce::File (j.file).getFileNameWithoutExtension();
        if (! j.process->start (juce::StringArray { exe.getFullPathName(), "--rz-scan-plugin", j.file, j.result.getFullPathName() }, 0))
        {
            finishJob (j, true);
            continue;
        }
        running.push_back (std::move (j));
    }
}

void PluginHost::finishJob (Job& j, bool crashed)
{
    bool ok = false;
    // the result counts even when the scan process crashed afterwards (many plug-ins crash on unload,
    // after the result was written); only a hang (timeout) or no result at all means the plug-in failed
    juce::ignoreUnused (crashed);
    if (j.result.existsAsFile())
        if (auto xml = juce::XmlDocument::parse (j.result))
        {
            for (auto* e : xml->getChildIterator())
            {
                juce::PluginDescription d;
                if (d.loadFromXml (*e)) { known.addType (d); ok = true; }
            }
            ok = ok || xml->getNumChildElements() == 0; // a file without effects is fine too
        }
    if (! ok)
    {
        known.addToBlacklist (j.file); // crashed or hung while being scanned: skipped from now on
        ++failed;
    }
    j.result.deleteFile();
    ++done;
    save(); // keep what is found even if Rezalica itself is closed during the scan
}

void PluginHost::timerCallback()
{
    if (outOfProcess)
    {
        for (auto it = running.begin(); it != running.end();)
        {
            const bool timedOut = juce::Time::getMillisecondCounterHiRes() - it->started > 60000.0;
            if (it->process->isRunning() && ! timedOut) { ++it; continue; }
            if (timedOut) { it->process->kill(); it->result.deleteFile(); }
            const bool crashed = timedOut || it->process->getExitCode() != 0;
            finishJob (*it, crashed);
            it = running.erase (it);
        }
        startNextJobs();
        progress = total > 0 ? (float) done / (float) total : 1.0f;
        if (running.empty() && pendingFiles.isEmpty())
        {
            outOfProcess = false;
            stopTimer();
            save();
            status = juce::String (getEffects().size()) + " effects" + (failed > 0 ? "  (" + juce::String (failed) + " plug-ins could not be loaded and are skipped)" : juce::String());
            progress = 1.0f;
        }
        sendChangeMessage();
        return;
    }

    if (scanner == nullptr) { stopTimer(); return; }
    juce::String name;
    const bool more = scanner->scanNextFile (true, name);
    progress = scanner->getProgress();
    status = "Scanning: " + name;
    if (! more)
    {
        ++formatIndex;
        if (formatIndex < formats.getNumFormats())
            scanner = std::make_unique<juce::PluginDirectoryScanner> (known, *formats.getFormat (formatIndex), getSearchPath(), true, getDeadMansPedalFile(), false);
        else
        {
            const auto failed = scanner->getFailedFiles();
            scanner = nullptr;
            stopTimer();
            save();
            status = juce::String (getEffects().size()) + " effects" + (failed.isEmpty() ? juce::String() : "  (" + juce::String (failed.size()) + " could not be loaded)");
            progress = 1.0f;
        }
    }
    sendChangeMessage();
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance (const juce::PluginDescription& d, double sampleRate, int blockSize, juce::String& error)
{
    return formats.createPluginInstance (d, sampleRate, blockSize, error);
}
} // namespace rz
