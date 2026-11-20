#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "PadButton.h"
#include "WaveformView.h"
#include "SequencerWindow.h"
#include "StretchWindow.h"
#include "PianoRollWindow.h"
#include "BankControls.h"
#include "SceneWindow.h"
#include "ToolWindows.h"
#include "AppSettings.h"
#include "LedDisplay.h"
#include "TransportButton.h"
#include "MetronomeControls.h"

class RezalicaEditor : public juce::AudioProcessorEditor,
                       public juce::DragAndDropContainer,   // files dragged from the docked sample browser
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit RezalicaEditor (RezalicaProcessor&);
    ~RezalicaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void selectPad (int pad, bool grabFocus = true);
    void setBank (int bank);
    void refreshChopControls();
    void refreshPadControls();
    void refreshFxControls();
    void refreshPadNames();
    void choosePadFile (int pad);
    void chooseChopFile();
    int chopModeFromBox() const;
    void toggleSequencerWindow();

    RezalicaProcessor& processor;
    rz::RezalicaLookAndFeel lnf;

    // header
    juce::TextButton loadChopButton { "LOAD SAMPLE" }, lazyButton { "LAZY CHOP" }, seqButton { "STEP SEQ" }, pianoRollButton { "PIANO ROLL" }, stopButton { "STOP ALL" },
                     undoButton { "UNDO" }, redoButton { "REDO" },
                     fileButton { "FILE" }, sceneButton { "SCENE" },  // FILE / SCENE / PLAY / STOP: standalone app only
                     playButton { "PLAY" }, stopPlayButton { "STOP" },
                     samplingButton { "SAMPLING" }, browserButton { "BROWSER" }, masterFxButton { "MASTER FX" }; // standalone tool row
    rz::MetronomeControls metronome;
    juce::Label autosaveLabel;
    juce::Rectangle<int> toolRowArea;
    std::unique_ptr<rz::ToolWindow> samplingWindow, masterFxWindow;
    std::unique_ptr<rz::BrowserPanel> browserPanel;   // docked on the left of the main window
    juce::Rectangle<int> browserArea;
    void toggleBrowser();
    juce::Slider tempoSlider;                          // (replaced by the LED display)
    rz::LedBpmDisplay ledBpm;
    rz::TransportButton recKey { rz::TransportButton::record, "REC: record what you play into the selected pattern (uses the count-in)" },
                        playKey { rz::TransportButton::play, "PLAY the selected pattern (space bar). After PAUSE it carries on" },
                        pauseKey { rz::TransportButton::pause, "PAUSE: stop here, PLAY carries on from the same place" },
                        stopKey { rz::TransportButton::stop, "STOP (back to the start)" },
                        clickKey { rz::TransportButton::metronome, "Metronome: a click on every beat at the project tempo (right-click: click volume)" };
    bool titleButtonsDone = false;                          // project tempo: red LED display, drag / wheel / double-click
    juce::Rectangle<int> logoArea;
    juce::Image faceplate;                             // brushed silver texture, rendered on resize
    void renderFaceplate();
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&, juce::StringArray&, bool&) override;
    void toggleToolWindow (std::unique_ptr<rz::ToolWindow>& w, juce::TextButton& button, int which);
    // autosave / crash recovery / recent projects (standalone)
    void autosave();
    void checkCrashRecovery();
    void openProjectFile (const juce::File& f);
    double lastAutosaveTime = 0.0;
    juce::MemoryBlock lastAutosavedState;
    int lastSelectedPad = -1;
    // multi-select of pads (Shift+click = range, Ctrl+click = add / remove one pad)
    std::array<bool, rz::numPads> multiSelection {};
    int selectionAnchor = 0;
    int multiSelectedCount() const;
    void refreshMultiSelection();
    void clearSelectedPads();
    bool isStandalone = false;
    void showFileMenu();
    void showAudioSettings();
    void newProject();
    void openProject();
    void saveProject (bool saveAs);
    void updateWindowTitle();
    void toggleSceneWindow();
    void exportSong (bool asMidi);
    std::unique_ptr<rz::SceneWindow> sceneWindow;
    bool lastLazyState = false;
    juce::ComboBox chopModeBox;
    juce::Slider sensitivitySlider;
    juce::Label sliceCountLabel;
    std::array<rz::BankButton, rz::numBanks> bankButtons;
    void showBankVolume (int bank);
    void showBankMenu (int bank);
    int lastPlayedCounter = 0;
    rz::Knob masterKnob { "MASTER" };

    // sample view + selected pad
    rz::WaveformView waveform;
    juce::Label padTitle;
    rz::Knob gainKnob { "GAIN" }, panKnob { "PAN" }, pitchKnob { "PITCH" }, attackKnob { "ATTACK" },
             releaseKnob { "RELEASE" }, startKnob { "START" }, endKnob { "END" };
    juce::TextButton loopButton { "LOOP" }, reverseButton { "REVERSE" }, gateButton { "GATE" },
                     muteButton { "MUTE" }, renderButton { "RENDER" }, clearPadButton { "CLEAR" }, trimButton { "TRIM" },
                     warpButton { "WARP" }, stretchButton { "STRETCH..." };
    juce::ComboBox chokeBox;

    // pads
    std::vector<std::unique_ptr<rz::PadButton>> pads;
    std::array<int, rz::numPads> lastHits {};
    std::array<bool, rz::padsPerBank> keyDown {};
    std::array<int, rz::padsPerBank> keyPad {};   // which pad each held key started

    // effects
    juce::TextButton filterOnButton { "FILTER" }, satOnButton { "SATURATION" }, delayOnButton { "DELAY" }, reverbOnButton { "REVERB" };
    juce::ComboBox filterTypeBox;
    juce::Label filterTypeLabel;
    rz::Knob reverbSizeKnob { "SIZE" }, reverbDampKnob { "DAMP" }, reverbWidthKnob { "WIDTH" }, reverbMixKnob { "MIX" };
    rz::Knob cutoffKnob { "CUTOFF" }, resoKnob { "RESONANCE" },
             tubeKnob { "TUBE" }, tapeKnob { "TAPE" }, solidKnob { "SOLID STATE" },
             timeKnob { "TIME" }, feedbackKnob { "FEEDBACK" }, mixKnob { "MIX" };
    juce::ComboBox delayDivBox;
    juce::Label delayDivLabel;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    // effects of the selected pad
    struct FxKnobBinding   { rz::Knob* knob; std::atomic<float> rz::PadFx::* member; };
    struct FxToggleBinding { juce::TextButton* button; std::atomic<bool> rz::PadFx::* member; };
    std::vector<FxKnobBinding> fxKnobBindings;
    std::vector<FxToggleBinding> fxToggleBindings;
    juce::Label fxTitle;
    juce::TextButton copyFxButton { "COPY FX" }, pasteFxButton { "PASTE FX" }, fxToAllButton { "FX TO BANK" };
    juce::ValueTree fxClipboard;

    std::unique_ptr<rz::SequencerWindow> sequencerWindow;
    std::unique_ptr<rz::StretchWindow> stretchWindow;
    std::unique_ptr<rz::PianoRollWindow> pianoRollWindow;
    void togglePianoRoll();
    void showStretchWindow();
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> headerArea, sampleArea, padArea, fxArea;
    juce::Rectangle<int> filterArea, satArea, delayArea, reverbArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RezalicaEditor)
};
