// The standalone app (instead of JUCE's default StandaloneFilterApp).
// It behaves the same, plus a hidden mode: "--rz-scan-plugin <plugin file> <result xml>" scans one
// plug-in in its own process, so a plug-in that crashes while it is scanned never takes Rezalica with it.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
#include <cstdlib>

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "BinaryData.h"

extern std::function<void (std::function<void()>)> rezalicaConfirmQuit; // set by the editor (PluginEditor.cpp)

namespace
{
/** The main window: its close button asks the app to quit (so unsaved changes are asked about). */
struct RezalicaWindow : public juce::StandaloneFilterWindow
{
    using juce::StandaloneFilterWindow::StandaloneFilterWindow;
    void closeButtonPressed() override
    {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    }
};

class RezalicaApp final : public juce::JUCEApplication
{
public:
    RezalicaApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override           { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override        { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override                 { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.contains ("--rz-scan-plugin"))
        {
            scanOnePlugin();
            return;
        }

        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            pluginHolder = createPluginHolder();
            return;
        }
        showSplash();
        mainWindow = std::make_unique<RezalicaWindow> (getApplicationName(),
                                                                     juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                                                                     createPluginHolder());
        // the main window opens when the splash screen has been shown (about 2 s)
        juce::Component::SafePointer<juce::Component> w (mainWindow.get());
        juce::Timer::callAfterDelay (splash != nullptr ? 2000 : 0, [w] { if (w != nullptr) { w->setVisible (true); w->toFront (true); } });
    }

    void shutdown() override
    {
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (rezalicaConfirmQuit && ! confirmed)
        {
            // "Save changes?" first; quits only after Save / Don't Save
            rezalicaConfirmQuit ([this] { confirmed = true; systemRequestedQuit(); });
            return;
        }
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    /** The saw-and-waveform picture in the middle of the screen while Rezalica starts (medium size, ~2.5 s;
        a click closes it). It deletes itself. */
    void showSplash()
    {
        auto image = juce::ImageCache::getFromMemory (BinaryData::RezalicaAppIcon_png, BinaryData::RezalicaAppIcon_pngSize);
        if (! image.isValid()) return;
        const int size = 320;
        auto* s = new juce::SplashScreen ("Rezalica", image.rescaled (size, size, juce::Graphics::highResamplingQuality), true);
        s->setAlwaysOnTop (true);
        s->deleteAfterDelay (juce::RelativeTime::seconds (2.5), true);
        splash = s;
    }

    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
        return std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false, juce::String{}, nullptr, channelConfig, false);
    }

    /** Scan mode: describe the plug-ins in one file, write them as XML, quit (exit code 0 = done). */
    void scanOnePlugin()
    {
        const auto args = getCommandLineParameterArray();
        const int i = args.indexOf ("--rz-scan-plugin");
        if (i < 0 || i + 2 >= args.size()) { setApplicationReturnValue (2); quit(); return; }
        const juce::String pluginFile = args[i + 1];
        const juce::File out (args[i + 2]);

        juce::AudioPluginFormatManager formats;
        formats.addDefaultFormats();
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* f : formats.getFormats())
            if (f->fileMightContainThisPluginType (pluginFile))
                f->findAllTypesForFile (found, pluginFile);

        juce::XmlElement xml ("PLUGINS");
        for (auto* d : found)
            if (auto e = d->createXml())
                xml.addChildElement (e.release());
        xml.writeTo (out);
        found.clear();
        setApplicationReturnValue (0);
        // done: leave right away, so a plug-in that crashes while being unloaded can't spoil the result
        std::fflush (nullptr);
        std::_Exit (0);
    }

    bool confirmed = false;
    juce::Component::SafePointer<juce::Component> splash;
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new RezalicaApp(); }
#endif
