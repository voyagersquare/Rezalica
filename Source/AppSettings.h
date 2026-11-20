#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace rz
{
/** Settings of the standalone app that don't belong to a project:
    recent projects, the sample browser folder, autosave. */
struct AppSettings
{
    static juce::File getFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Rezalica");
    }

    static juce::PropertiesFile& get()
    {
        static std::unique_ptr<juce::PropertiesFile> file = []
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Rezalica";
            o.filenameSuffix = ".settings";
            o.folderName = "Rezalica";
            o.osxLibrarySubFolder = "Application Support";
            return std::make_unique<juce::PropertiesFile> (getFolder().getChildFile ("Rezalica.settings"), o);
        }();
        return *file;
    }

    // ---- recent projects ----
    static juce::StringArray getRecentProjects()
    {
        juce::StringArray list;
        list.addTokens (get().getValue ("recentProjects"), "|", {});
        list.removeEmptyStrings();
        return list;
    }

    static void addRecentProject (const juce::File& f)
    {
        auto list = getRecentProjects();
        list.removeString (f.getFullPathName());
        list.insert (0, f.getFullPathName());
        while (list.size() > 10)
            list.remove (list.size() - 1);
        get().setValue ("recentProjects", list.joinIntoString ("|"));
        get().saveIfNeeded();
    }

    static void clearRecentProjects()
    {
        get().setValue ("recentProjects", juce::String());
        get().saveIfNeeded();
    }

    // ---- autosave / crash recovery ----
    static juce::File getAutosaveFile()  { return getFolder().getChildFile ("Autosave").getChildFile ("autosave.rezalica"); }
    static juce::File getRunningFlag()   { return getFolder().getChildFile ("Autosave").getChildFile ("running.flag"); }
};
} // namespace rz
