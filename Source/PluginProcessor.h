#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <limits>
#include <map>
#include "SamplerEngine.h"
#include "Sequencer.h"
#include "FxChain.h"
#include "MasterFx.h"
#include "Recorder.h"
#include "LoopbackCapture.h"
#include "Mixer.h"
#include "Turntable.h"
#include "Scales.h"
#include "PluginHost.h"

class RezalicaProcessor : public juce::AudioProcessor,
                          private juce::Timer
{
public:
    static constexpr int midiBaseNote = 36; // C1 = A1 ... D#2 = A16, E2 = B1 ... D#7 = D16

    /** withInput: an audio input bus for sampling (the standalone app has one, the plug-in doesn't). */
    explicit RezalicaProcessor (bool withInput = juce::JUCEApplicationBase::isStandaloneApp());
    ~RezalicaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // "infinite" tail: the host must keep calling us even when we are silent and get no MIDI,
    // otherwise the built-in sequencer could not start on the DAW's play / space bar
    double getTailLengthSeconds() const override { return std::numeric_limits<double>::infinity(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API used by the editor
    enum ChopMode { chop2 = 2, chop4 = 4, chop8 = 8, chop16 = 16, chopTransients = 0, chopManual = -1, chopOff = -2 };
    static constexpr int maxSlices = 64;

    bool loadFileToPad (int pad, const juce::File& file);
    bool chopFileToPads (const juce::File& file, int mode);   // file becomes the chop source
    bool loadFileForChopping (const juce::File& file);        // loads to the selected pad, not chopped until a mode is chosen
    void chopPadSample (int pad, int mode);                   // pad's sample becomes the chop source
    void setChopMode (int mode);                              // re-chops the selected pad's sample automatically
    void setTransientSensitivity (float sensitivity01);       // re-chops when in transient mode
    void clearPad (int pad);
    bool trimPad (int pad); // keeps only start..end of the pad's sample (new sample, saved as WAV)
    /** NORMALIZE (peak): finds the loudest peak of the part the pad plays (between its markers) and turns it up
        or down by one amount, so that peak lands on targetDb (dBFS); the dynamics stay the same. Then it BOUNCES:
        that part becomes a new WAV (Documents/Rezalica/Bounced) loaded back onto the pad and into the sample view.
        Returns the change in dB, or NaN when the pad is empty / silent. */
    double normalizePad (int pad, float targetDb);
    static juce::File getTrimFolder();

    // Time stretch / warp (applied to the selected pad's sample, or only its region)
    bool setWarp (int pad, rz::WarpSettings settings,    // settings.enabled = false removes the warp
                  const juce::AudioBuffer<float>* precomputed = nullptr);
    void requestWarp (int pad, rz::WarpSettings settings); // same, but stretches on a background thread
    bool isWarpBusy() const noexcept { return warpJobs.load() > 0; }
    rz::WarpSettings getWarpSettings (int pad) const;    // current settings of the pad (or the last used ones)
    bool isPadWarped (int pad) const;
    rz::WarpSettings suggestWarpSettings (int pad) const; // warp on, length closest to the sample at the current tempo
    double getWarpTempo (const rz::WarpSettings& s) const;
    void updateWarpTempo (bool async = false);           // re-stretches synced samples when the DAW tempo changes

    // ---- undo / redo of pad, chop, warp and effect edits (call pushUndo before an edit) ----
    void pushUndo (const juce::String& actionName);
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    juce::String getUndoName() const { return undoStack.empty() ? juce::String() : undoStack.back().name; }
    juce::String getRedoName() const { return redoStack.empty() ? juce::String() : redoStack.back().name; }

    // ---- banks ----
    void setBankGainDb (int bank, float db);
    float getBankGainDb (int bank) const;
    void setBankMute (int bank, bool mute) { engine.bankMute[(size_t) bank] = mute; }
    bool isBankMuted (int bank) const { return engine.bankMute[(size_t) bank].load(); }
    void copyBank (int bank);
    bool canPasteBank() const noexcept { return bankClipboard.valid; }
    void pasteBank (int bank);

    // copy / paste a whole pad (sample, markers, settings, effects)
    void copyPad (int pad);
    bool canPastePad() const noexcept { return padClipboard.valid; }
    void pastePad (int pad);
    static juce::File getWarpFolder();

    /** RENDER / consolidate: writes the warped (time-stretched) sample of this pad to a new WAV
        and puts that file in place of the warped sample on every pad that uses it. */
    bool consolidatePad (int pad);
    static juce::File getRenderFolder();

    // ---- projects (standalone app: New / Open / Save / Save As) ----
    bool saveProjectToFile (const juce::File& file);
    bool loadProjectFromFile (const juce::File& file);
    void newProject();
    juce::File currentProjectFile;
    /** Unsaved changes since the last New / Open / Save (pad selection and similar don't count). */
    bool hasUnsavedChanges();

    // ---- audio files Rezalica made in this session (bounce, normalize, trim, warp, recordings) ----
    /** Remembers a WAV the program wrote itself (never the user's own samples). Any thread. */
    void noteSessionFile (const juce::File&);
    /** The session's files that no saved project uses (offered for deleting when changes are not saved). */
    juce::Array<juce::File> getUnsavedSessionFiles() const;
    /** Moves those files to the Recycle Bin. Returns how many. */
    int trashSessionFiles();
    /** Keeps them (they stay in Documents/Rezalica) and stops tracking them. */
    void forgetSessionFiles();
    void markProjectSaved();

    // ---- export a pattern for dragging into the DAW ----
    static juce::File getExportFolder();
    /** stepPart / pianoPart: which part of the pattern to export. */
    juce::File exportPatternMidi (int pattern, bool stepPart, bool pianoPart);
    juce::File exportPatternWav (int pattern, bool stepPart, bool pianoPart);

    // ---- export the whole song (SCENE window) ----
    bool exportSongWav (const juce::File& file);

    /** Song export with the options of the export window. */
    struct ExportSettings
    {
        enum Format { wav = 0, mp3 = 1 };
        int format = wav;
        double sampleRate = 0.0;     // 0 = the current one
        int bitDepth = 24;           // WAV: 16, 24 or 32 (float)
        bool dither = true;          // WAV 16-bit: TPDF dither
        int mp3Kbps = 320;
        bool mono = false;
        bool normalize = false;      // peak to -0.3 dBFS
        double tailSeconds = 3.0;    // reverb / delay ringing out after the song
        juce::String title, artist;
    };
    bool exportSong (const juce::File& file, const ExportSettings& settings, juce::String& error);
    /** Renders the whole song offline (stereo). */
    juce::AudioBuffer<float> renderSong (double sampleRate, double tailSeconds);
    bool exportSongMidi (const juce::File& file);

    // manual chop markers (switch the chop mode to Manual)
    void addSlicePoint (float normalisedPosition);
    void removeSlicePoint (int index);
    int moveSlicePoint (int index, float normalisedPosition);
    void clearSlicePoints();

    // Lazy Chop: plays the selected pad's whole sample, every pad hit drops a slice at the playhead
    bool startLazyChop();
    void stopLazyChop();
    bool isLazyChopActive() const noexcept { return lazyActive.load() || lazyStarting; }
    bool isLazyChopWaiting() const noexcept { return lazyWaiting.load(); } // armed, silent until the first hit
    void updateLazyChop(); // call regularly from the UI (message thread)

    int getChopMode() const noexcept               { return chopMode; }
    float getTransientSensitivity() const noexcept { return transientSensitivity; }
    int getNumSlices() const noexcept              { return (int) slicePoints.size(); }
    rz::SampleData::Ptr getChopSource() const      { return chopSource; }
    const std::vector<float>& getSlicePoints() const noexcept { return slicePoints; }
    juce::String getPadName (int pad) const;

    void triggerPadFromUi (int pad, float velocity, bool isNoteOn);
    void stopAll() { triggerPadFromUi (-1, 0.0f, false); }
    /** After a double STOP: effect plug-ins forget their tails (reverb, delay ...). Message thread. */
    void clearEffectTails();
    /** Plays the pad transposed (piano roll keyboard / note preview). */
    void previewNoteFromUi (int pad, float velocity, float transpose, bool isNoteOn, bool recordIt = false);

    rz::SamplerEngine& getEngine() noexcept { return engine; }
    rz::Sequencer& getSequencer() noexcept { return sequencer; }
    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }
    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }
    static const juce::StringArray& getDelayDivisionNames();

    std::atomic<int> selectedPad { 0 };
    std::atomic<int> currentBank { 0 }; // bank shown in the UI and played by the computer keyboard
    std::atomic<int> chromaticPad { -1 }; // >= 0: incoming MIDI plays this pad's sample chromatically (C3 = original)
    /** MELODIC piano roll: key and scale (rz::scales, 0 = chromatic) and the chord a drawn note makes. */
    std::atomic<int> scaleRoot { 0 }, scaleType { 0 }, chordType { 0 };

    // ---- MIDI controllers: which note plays which pad, MIDI Learn, MIDI clock ----
    /** Pad note layouts: Rezalica / Akai MPD (C1 = A1 ... 64 pads), MPC 2000XL and General MIDI drums
        (16 notes that play the current bank), or custom (learned). */
    enum PadNoteLayout { layoutRezalica = 0, layoutMpc = 1, layoutGm = 2, layoutCustom = 3 };
    static constexpr int bankRelative = 1000;  // a note map value of 1000 + i = pad i of the current bank
    void setPadNoteLayout (int layout);        // message thread (fills the note map; custom keeps it)
    int getPadNoteLayout() const noexcept { return padNoteLayout.load(); }
    std::array<int, 128> getPadNoteMap() const;
    void setPadNoteMap (const std::array<int, 128>& map, int layout);
    int padForNote (int note) const noexcept;  // the pad a note plays now (-1: none)
    int noteForPad (int pad) const noexcept;   // the note that plays a pad now (-1: none)
    std::atomic<int> learnPadNote { -1 };      // >= 0: the next note that comes in is learned for this pad
    std::atomic<int> padNoteMapChanged { 0 };  // counts learned notes (the editor saves them)

    /** CC / notes for MIDI Learn: the audio thread passes them to the editor (lock-free). */
    struct MidiControlEvent { juce::uint8 kind = 0, channel = 1, number = 0, value = 0; }; // kind: 0 CC, 1 note on, 2 note off
    juce::AbstractFifo controlFifo { 1024 };
    std::array<MidiControlEvent, 1024> controlEvents;
    std::array<std::atomic<bool>, 128> noteIsControl {}; // notes learned for buttons: they do not play pads
    std::atomic<bool> midiLearnListening { false };      // MIDI Learn waits for a control: notes do not play
    std::atomic<int> midiActivity { 0 };                 // counts blocks with incoming MIDI (the MIDI key blinks)
    std::atomic<int> mmcCount { 0 };                     // MIDI Machine Control commands received (transport keys)

    /** MIDI clock in: Rezalica follows an external tempo and its START / CONTINUE / STOP. */
    std::atomic<bool> clockIn { false };
    /** MIDI clock out to a MIDI output ("" = off): 24 ticks per beat, START / STOP with the sequencer. */
    void setClockOutput (const juce::String& deviceIdentifier);
    juce::String getClockOutput() const { return clockOutId; }
    /** The clock messages for one block (also used by the tests). */
    void makeClockMessages (juce::MidiBuffer& out, int numSamples);

    /** Moves notes recorded live into the piano roll clips (message thread). Returns true if any were added. */
    bool collectRecordedPianoNotes();
    juce::ChangeBroadcaster padsChanged; // pad samples / settings replaced

    // ---- mixer: BUS A-D + MASTER, 4 third-party effect slots each (message thread API) ----
    /** 0 = MASTER, 1..4 = BUS A..D, rz::deckChain = the DECK's effects. */
    rz::MixerBus& getBus (int bus) noexcept { return buses[(size_t) juce::jlimit (0, rz::numEffectChains - 1, bus)]; }
    /** Loads a plug-in (from the scanned list) into a slot; returns false with an error message. */
    bool loadPluginIntoSlot (int bus, int slot, const juce::PluginDescription&, juce::String& error);
    /** Puts any processor into a slot (prepared here); desc may be null. */
    void setSlotProcessor (int bus, int slot, std::unique_ptr<juce::AudioProcessor>, const juce::PluginDescription* desc);
    void clearSlot (int bus, int slot);
    juce::AudioProcessor* getSlotProcessor (int bus, int slot) const;
    juce::String getSlotName (int bus, int slot) const;
    /** Called before plug-ins are replaced or removed (close their editor windows). One per mixer view
        (the mixer and the DECK's effects), keyed by its owner. Message thread. */
    std::map<const void*, std::function<void()>> pluginsAboutToChange;
    juce::ChangeBroadcaster mixerChanged;
    juce::SharedResourcePointer<rz::PluginHost> pluginHost;

    // ---- master chain (EQ / compressor / limiter on the output) ----
    rz::MasterFxSettings masterFxSettings;
    rz::MasterFx& getMasterFx() noexcept { return masterFx; }

    // ---- sampling / resampling ----
    rz::AudioRecorder& getRecorder() noexcept { return recorder; }

    // ---- DECK (turntable) ----
    rz::Turntable& getDeck() noexcept { return deck; }
    /** Puts a sample on the DECK (from the browser); it starts turning. */
    bool loadFileToDeck (const juce::File& file);
    /** The selected pad's whole sample onto the DECK. */
    bool loadPadToDeck (int pad);
    /** After the deck's REC: the scratch goes onto a pad (silence trimmed). Returns the file, or {}. */
    juce::File collectDeckRecording (int pad);
    bool hasAudioInput() const { return getTotalNumInputChannels() > 0; }
    double getSampleRateNow() const noexcept { return currentSampleRate; }
    /** Starts a take. source: input or Rezalica's output; patternLength: resample exactly one loop
        of the selected pattern (starts it from the top); waitForSignal: start at the first sound. */
    bool startRecording (rz::AudioRecorder::Source source, bool patternLength, bool waitForSignal);
    /** Sampling like a hardware sampler (MPC / SP). */
    struct RecordOptions
    {
        rz::AudioRecorder::Source source = rz::AudioRecorder::input;
        bool waitForSignal = true;     // threshold: starts at the first sound above it
        float thresholdDb = -34.0f;
        double lengthSeconds = 0.0;    // 0 = until STOP (max. 10 minutes); else it stops by itself
        bool patternLength = false;    // RESAMPLE: exactly one loop of the selected pattern
        int inputMode = 0;             // 0 stereo, 1 mono L, 2 mono R
    };
    bool startRecording (const RecordOptions&);
    /** Monitor: the input is heard while sampling. Through a bus: the input goes through that bus's
        effects (and is recorded with them). -1 = straight. */
    std::atomic<bool> monitorInput { false };
    /** Delay of the mixer's plug-ins (compensated, set by the audio thread) and of the sound card's
        output (set by the standalone app): the playhead shows what is heard, not what is computed. */
    std::atomic<int> pluginLatencySamples { 0 }, deviceOutputLatencySamples { 0 };
    /** SP style: REC pressed, the pads blink, the next pad hit gets the take (started by the editor). */
    std::atomic<bool> padChoiceArmed { false };
    RecordOptions armedRecordOptions;
    std::atomic<int> inputThroughBus { -1 };

    // ---- KEEP / RETRY: a finished take waits here until it is kept or thrown away ----
    bool fetchTake();                       // moves a finished take out of the recorder (message thread)
    bool hasTake() const noexcept { return pendingTake.getNumSamples() > 0; }
    double getTakeSeconds() const noexcept { return pendingTake.getNumSamples() / juce::jmax (1.0, pendingTakeRate); }
    void previewTake();
    void discardTake();
    /** Saves the take as a WAV (name), optionally without the silence at its ends, loads it onto the pad
        and optionally chops it (chop mode, or chopOff). Returns the file. */
    juce::File keepTake (int pad, const juce::String& name, bool trimSilence, int chopModeAfter);
    /** Message thread: when a take is finished, writes it to a WAV (Documents/Rezalica/Recordings)
        and loads it to the pad (the selected pad becomes the chop source). Returns the file, or {}. */
    juce::File collectRecording (int pad);
    static juce::File getRecordingFolder();
    /** "What you hear": the sound the computer plays, as a sampling source (Windows). */
    bool startComputerAudio();
    void stopComputerAudio();
    bool isComputerAudioRunning() const { return loopback.isRunning(); }
    rz::LoopbackCapture& getComputerAudio() noexcept { return loopback; }
    juce::String getComputerAudioError() const { return loopback.getLastError(); }

    /** Sample browser: plays a file once (empty file = stop). */
    void previewFile (const juce::File& file);

private:
    struct UiEvent { int pad; float velocity; bool on; bool preview = false; float transpose = 0.0f; bool record = false; };
    enum EventType { evStopAll = -1, evOff = 0, evOffSeq = 1, evOffTag = 2, evOn = 10, evOnSeq = 11, evOnTag = 12 };
    struct Event
    {
        int offset;
        int type;          // EventType: note-offs sort before note-ons at the same sample
        int pad;
        float velocity;
        float transpose = 0.0f;
        int tag = -1;
        bool flag = false; // evOnTag: polyphonic, evOffTag: force release
        double skip = 0.0; // evOnTag: samples of the note already played (the playback jumped into it)
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses (bool withInput);
    static std::vector<float> findSlicePoints (const rz::SampleData& s, int mode, float sensitivity);

    void timerCallback() override;
    rz::SampleData::Ptr loadSample (const juce::File& file, bool shareIfLoaded = false);
    rz::SampleData::Ptr renderWarp (const rz::SampleData::Ptr& source, float regionStart, float regionEnd,
                                    const rz::WarpSettings& settings, double bpm, bool saveInBackground = true,
                                    const juce::AudioBuffer<float>* precomputed = nullptr);
    static juce::AudioBuffer<float> computeStretch (const rz::SampleData& source, float regionStart, float regionEnd,
                                                    const rz::WarpSettings& settings, double bpm);
    void warpRegionFor (int pad, const rz::WarpSettings& settings, rz::SampleData::Ptr& base, float& ra, float& rb) const;
    juce::ValueTree padToValueTree (int pad) const;
    void padFromValueTree (int pad, const juce::ValueTree& t);
    void replaceSampleOnPads (const rz::SampleData::Ptr& from, const rz::SampleData::Ptr& to);
    void saveInBackground (const rz::SampleData::Ptr& s);

    void rechop();
    void applySlices();
    void prepareManualSource();
    float snapToZeroCrossing (float normalisedPosition) const;
    void addEvent (int offset, int type, int pad, float velocity);
    void addTaggedEvent (int offset, int type, int pad, float velocity, float transpose, int tag, bool flag, double skip = 0.0);
    rz::FxChain::Params readFxParams (int pad, bool padSounding) const;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formatManager;

    rz::SamplerEngine engine;
    rz::Sequencer sequencer;


    // chop state (message thread)
    rz::SampleData::Ptr chopSource, previousChopSource;
    void setChopSource (rz::SampleData::Ptr s);
    int chopMode = chopOff;
    float transientSensitivity = 0.5f;
    std::vector<float> slicePoints;

    std::atomic<bool> lazyActive { false };
    bool lazyStarting = false;
    std::atomic<bool> lazyWaiting { false };   // LAZY CHOP armed, waiting for the first hit
    juce::AbstractFifo lazyFifo { 128 };
    std::array<float, 128> lazyHits {};

    juce::ReferenceCountedArray<rz::SampleData> samplePool; // owns all loaded samples (message thread)

    juce::CriticalSection missingPathLock;
    std::array<juce::String, rz::numPads> missingPaths; // remembered for files that could not be found

    juce::AbstractFifo uiFifo { 256 };
    std::array<UiEvent, 256> uiEvents {};

    std::array<Event, 1024> events {};
    int numEvents = 0;
    std::array<double, rz::numPads> pendingSeqOff {};

    std::array<juce::AudioBuffer<float>, rz::numBuses> busBuffers;   // what each mixer bus collects this block
    std::array<rz::MixerBus, rz::numEffectChains> buses; // the mixer buses + the DECK's effect slots
    juce::AudioBuffer<float> deckBuffer;                 // the DECK this block, through its effects
    void processEffectChain (rz::MixerBus& bus, juce::AudioBuffer<float>& data, int numSamples);
    int preparedBlockSize = 512;
    void prepareSlot (rz::MixerSlot&);
    void processBusChain (int bus, int numSamples);
    juce::ValueTree mixerToValueTree() const;
    void mixerFromValueTree (const juce::ValueTree&);
    rz::MasterFx masterFx;
    rz::AudioRecorder recorder;
    rz::Turntable deck;
    juce::String savedDeckFile;     // the deck's record as saved with the project (kept after a restart)
    bool restoringProject = false;  // setStateInformation from Open Project (not the session restored at start)
    std::array<std::atomic<int>, 128> noteMap {};   // note -> pad (-1 none, 0..63, bankRelative + i)
    std::atomic<int> padNoteLayout { layoutRezalica };
    std::array<int, 128> notePlaying {};            // audio thread: the pad each held note started
    std::array<int, 128> melodicPlaying {};         // audio thread: the (scale-snapped) note each held key started
    void pushControlEvent (juce::uint8 kind, int channel, int number, int value);
    // MIDI clock
    juce::int64 sampleClock = 0, lastClockTick = -1;
    double clockInterval = 0.0;
    int clockTicks = 0;
    std::unique_ptr<juce::MidiOutput> clockOut;
    juce::SpinLock clockOutLock;
    juce::String clockOutId;
    juce::MidiBuffer clockBuffer;
    double freeClockTicks = 0.0;
    bool clockWasRunning = false;
    juce::Array<juce::File> sessionFiles;
    juce::CriticalSection sessionFilesLock;
    void forgetSessionFilesUsedBy (const juce::XmlElement& project);
    rz::LoopbackCapture loopback;
    int clickSamplesLeft = 0, clickLength = 1;
    juce::AudioBuffer<float> clickBuffer;          // the metronome, delayed like the mixer's plug-ins
    rz::LatencyDelay clickDelay;
    static constexpr int maxCompensation = 1 << 16; // samples of plug-in delay that can be compensated
    double clickPhase = 0.0, clickFreq = 1000.0;
    rz::SampleData::Ptr previewData;
    juce::AudioBuffer<float> pendingTake;
    double pendingTakeRate = 44100.0;
    rz::AudioRecorder::Source pendingTakeSource = rz::AudioRecorder::input;
    juce::AudioBuffer<float> inputCopy;   // the input of this block (monitor / through a bus)
    int inputCopyChannels = 0;
    juce::int64 savedFingerprint = 0;
    juce::int64 projectFingerprint();
    static constexpr int playSampleTag = (1 << 28) + 7; // PLAY of an empty pattern plays the selected pad
    bool playingSampleOnPlay = false;
    double currentSampleRate = 44100.0;

    std::atomic<float>* pMaster = nullptr;

    rz::WarpSettings lastWarpSettings;

    struct PadClipboard { rz::SampleData::Ptr sample; juce::ValueTree settings; bool valid = false; } padClipboard;
    struct BankClipboard
    {
        std::array<rz::SampleData::Ptr, rz::padsPerBank> samples;
        std::array<juce::ValueTree, rz::padsPerBank> settings;
        float db = 0.0f;
        bool valid = false;
    } bankClipboard;
    std::array<float, rz::numBanks> bankDb {}; // bank volume in dB (the engine holds the linear gain)

    struct UndoState
    {
        juce::String name;
        std::array<rz::SampleData::Ptr, rz::numPads> samples;
        std::array<juce::ValueTree, rz::numPads> settings;
        std::array<juce::String, rz::numPads> missing;
        rz::SampleData::Ptr chopSource;
        int chopMode = 0;
        float sensitivity = 0.5f;
        std::vector<float> slicePoints;
        std::array<float, rz::numBanks> bankDb {};
        std::array<bool, rz::numBanks> bankMuted {};
    };
    UndoState captureUndoState (const juce::String& name) const;
    void restoreUndoState (const UndoState& s);
    std::vector<UndoState> undoStack, redoStack;

    std::atomic<int> warpJobs { 0 };
    int warpGeneration = 0;
    std::vector<rz::SampleData*> tempoJobsPending;
    std::shared_ptr<int> aliveToken = std::make_shared<int> (0);
    juce::ThreadPool stretchWorker { 1 };

    // writes stretched samples to disk in the background (declared last: stopped first)
    juce::ThreadPool fileWriter { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RezalicaProcessor)
};
