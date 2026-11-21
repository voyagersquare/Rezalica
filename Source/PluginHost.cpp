#include "PluginHost.h"
#include "AppSettings.h"

namespace rz
{
PluginHost::PluginHost()
{
    formats.addDefaultFormats(); // VST3 (and AU on macOS)
    if (auto xml = juce::XmlDocument::parse (getListFile()))
        known.recreateFromXml (*xml);
    status = juce::String (getEffects().size()) + " effects";
}

PluginHost::~PluginHost()
{
    stopTimer();
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
    if (isScanning() || formats.getNumFormats() == 0)
    {
        if (formats.getNumFormats() == 0) { status = "Plug-in hosting is not available"; sendChangeMessage(); }
        return;
    }
    formatIndex = 0;
    scanner = std::make_unique<juce::PluginDirectoryScanner> (known, *formats.getFormat (0), getSearchPath(), true, getDeadMansPedalFile(), false);
    status = "Scanning...";
    progress = 0.0f;
    sendChangeMessage();
    startTimer (20);
}

void PluginHost::timerCallback()
{
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
