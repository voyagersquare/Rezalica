#include "PluginHost.h"
#include "AppSettings.h"

namespace rz
{
PluginHost::PluginHost()
{
    formats.addDefaultFormats(); // VST3 (and AU on macOS)
    if (auto xml = juce::XmlDocument::parse (getListFile()))
        known.recreateFromXml (*xml);
    // (nothing else here: this runs whenever a Rezalica processor is created - also when a host or the
    //  VST3 build tool just loads the plug-in - so it must not touch settings files or start timers)
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
        if (! d.isInstrument && d.name != "Rezalica")   // (not Rezalica's own VST3)
            out.add (d);
    std::sort (out.begin(), out.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    return out;
}

juce::Array<juce::PluginDescription> PluginHost::getInstruments() const
{
    juce::Array<juce::PluginDescription> out;
    for (auto& d : known.getTypes())
        if (d.isInstrument && d.name != "Rezalica")    // (not Rezalica's own VST3)
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

void PluginHost::startScan (bool quiet)
{
    if (isScanning() || outOfProcess || formats.getNumFormats() == 0)
    {
        if (formats.getNumFormats() == 0) { status = "Plug-in hosting is not available"; sendChangeMessage(); }
        return;
    }

    // the first scanner threw away plug-ins that only crash when they are unloaded: give those one more try
    // (version 3: earlier scans could mark good plug-ins as failed - three scans at once shared one result
    //  file - so everything that was skipped gets one more try)
    if (AppSettings::get().getIntValue ("pluginScanVersion", 1) < 3)
    {
        known.clearBlacklistedFiles();
        AppSettings::get().setValue ("pluginScanVersion", 3);
        AppSettings::get().saveIfNeeded();
    }
    quietScan = quiet;
    effectsBefore = getEffects().size();

    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        // list the plug-in files first (fast, nothing is loaded), then scan each one in its own process
        pendingFiles.clear();
        pendingFormats.clear();
        retryFiles.clear();
        retryFormats.clear();
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
        status = total == 0 ? (quiet ? juce::String (getEffects().size()) + " effects" : juce::String ("Nothing new to scan"))
                            : "Scanning 0 / " + juce::String (total);
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
    // first everything, three at a time; then the ones that failed once, alone (a plug-in can fail when
    // it is loaded next to others, or just be slow the first time)
    const bool retryPass = pendingFiles.isEmpty();
    auto& files = retryPass ? retryFiles : pendingFiles;
    auto& fmts = retryPass ? retryFormats : pendingFormats;
    while ((int) running.size() < (retryPass ? 1 : 3) && ! files.isEmpty())
    {
        Job j;
        j.file = files[0];
        j.format = fmts[0];
        j.retry = retryPass;
        files.remove (0);
        fmts.remove (0);
        // every scan gets its own result file (three scans at once used to share one name)
        j.result = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("rezalica_scan_" + juce::Uuid().toDashedString() + ".xml");
        j.process = std::make_unique<juce::ChildProcess>();
        j.started = juce::Time::getMillisecondCounterHiRes();
        status = (retryPass ? "Trying again: " : "Scanning " + juce::String (done + 1) + " / " + juce::String (total) + ":  ")
                 + juce::File (j.file).getFileNameWithoutExtension();
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
    // a plug-in that was found before stays (a failed re-scan is not a reason to throw it away)
    bool knownBefore = false;
    for (auto& d : known.getTypes())
        knownBefore = knownBefore || d.fileOrIdentifier == j.file;
    j.result.deleteFile();
    if (! ok && ! knownBefore)
    {
        if (! j.retry)
        {
            retryFiles.add (j.file);   // once more at the end, on its own, before it counts as failed
            retryFormats.add (j.format);
            return;
        }
        known.addToBlacklist (j.file); // failed twice (crashed / hung / no result): skipped from now on
        ++failed;
    }
    ++done;
    save(); // keep what is found even if Rezalica itself is closed during the scan
}

void PluginHost::timerCallback()
{
    if (outOfProcess)
    {
        for (auto it = running.begin(); it != running.end();)
        {
            const bool timedOut = juce::Time::getMillisecondCounterHiRes() - it->started > (it->retry ? 120000.0 : 60000.0);
            if (it->process->isRunning() && ! timedOut) { ++it; continue; }
            if (timedOut) { it->process->kill(); it->result.deleteFile(); }
            const bool crashed = timedOut || it->process->getExitCode() != 0;
            finishJob (*it, crashed);
            it = running.erase (it);
        }
        startNextJobs();
        progress = total > 0 ? (float) done / (float) total : 1.0f;
        if (running.empty() && pendingFiles.isEmpty() && retryFiles.isEmpty())
        {
            outOfProcess = false;
            stopTimer();
            save();
            const int added = getEffects().size() - effectsBefore;
            status = juce::String (getEffects().size()) + " effects"
                     + (added > 0 ? "  (" + juce::String (added) + " new)" : juce::String())
                     + (failed > 0 ? "  (" + juce::String (failed) + " plug-ins could not be loaded and are skipped)" : juce::String());
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
    AppSettings::PluginActivity activity ("loading the plug-in " + d.name + " (" + d.manufacturerName + ")");
    return formats.createPluginInstance (d, sampleRate, blockSize, error);
}
} // namespace rz
