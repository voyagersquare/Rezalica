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
            // NOT "Rezalica.settings": on Windows JUCE's standalone wrapper keeps its own settings in that file
            // and rewrites it when the app closes, which threw away the favourites and recent projects
            return std::make_unique<juce::PropertiesFile> (getFolder().getChildFile ("RezalicaSampler.settings"), o);
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

    // ---- favourite folders of the sample browser ----
    static juce::StringArray getFavorites()
    {
        juce::StringArray list;
        list.addTokens (get().getValue ("favoriteFolders"), "|", {});
        list.removeEmptyStrings();
        return list;
    }

    static bool isFavorite (const juce::File& f) { return getFavorites().contains (f.getFullPathName()); }

    static void setFavorite (const juce::File& f, bool add)
    {
        auto list = getFavorites();
        list.removeString (f.getFullPathName());
        if (add)
            list.add (f.getFullPathName());
        get().setValue ("favoriteFolders", list.joinIntoString ("|"));
        get().saveIfNeeded();
    }

    // ---- autosave / crash recovery ----
    static juce::File getAutosaveFile()  { return getFolder().getChildFile ("Autosave").getChildFile ("autosave.rezalica"); }
    static juce::File getRunningFlag()   { return getFolder().getChildFile ("Autosave").getChildFile ("running.flag"); }
};
} // namespace rz
