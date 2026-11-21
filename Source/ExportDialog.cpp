#include "ExportDialog.h"
#include "AppSettings.h"
#include "Mp3Writer.h"

namespace rz
{
ExportDialog::ExportDialog (RezalicaProcessor& p, std::function<void (const RezalicaProcessor::ExportSettings&)> onExport)
    : processor (p), exportCallback (std::move (onExport))
{
    auto& st = AppSettings::get();
    auto label = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, colours::text);
        l.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    };
    label (formatL, "Format");
    label (rateL, "Sample rate");
    label (depthL, "Bit depth");
    label (bitrateL, "Bitrate");
    label (channelsL, "Channels");
    label (tailL, "Tail at the end");
    label (titleL, "Title");
    label (artistL, "Artist");

    format.addItem ("WAV  (uncompressed, best quality)", 1);
    format.addItem ("MP3  (small file, for sharing)", 2);
    format.setItemEnabled (2, Mp3Writer::isAvailable());
    for (int r : { 44100, 48000, 88200, 96000 })
        rate.addItem (juce::String (r / 1000.0, r % 1000 == 0 ? 0 : 1) + " kHz", r);
    depth.addItem ("16-bit  (CD)", 16);
    depth.addItem ("24-bit  (studio)", 24);
    depth.addItem ("32-bit float", 32);
    for (int k : { 128, 160, 192, 224, 256, 320 })
        bitrate.addItem (juce::String (k) + " kbps" + (k == 320 ? "  (best)" : k == 128 ? "  (smallest)" : ""), k);
    channels.addItem ("Stereo", 2);
    channels.addItem ("Mono", 1);
    tail.addItem ("None", 1);
    tail.addItem ("1 second", 2);
    tail.addItem ("3 seconds", 4);
    tail.addItem ("5 seconds", 6);

    // the last used options
    format.setSelectedId (st.getIntValue ("exportFormat", 1) == 2 && Mp3Writer::isAvailable() ? 2 : 1, juce::dontSendNotification);
    rate.setSelectedId (st.getIntValue ("exportRate", 44100), juce::dontSendNotification);
    depth.setSelectedId (st.getIntValue ("exportDepth", 24), juce::dontSendNotification);
    bitrate.setSelectedId (st.getIntValue ("exportKbps", 320), juce::dontSendNotification);
    channels.setSelectedId (st.getIntValue ("exportChannels", 2), juce::dontSendNotification);
    tail.setSelectedId (st.getIntValue ("exportTail", 4), juce::dontSendNotification);
    dither.setToggleState (st.getBoolValue ("exportDither", true), juce::dontSendNotification);
    normalize.setToggleState (st.getBoolValue ("exportNormalize", false), juce::dontSendNotification);
    title.setText (processor.currentProjectFile != juce::File() ? processor.currentProjectFile.getFileNameWithoutExtension() : juce::String ("Rezalica Song"));
    artist.setText (st.getValue ("exportArtist"));

    for (auto* c : { &format, &rate, &depth, &bitrate, &channels, &tail })
    {
        c->onChange = [this] { update(); };
        addAndMakeVisible (*c);
    }
    for (auto* t : { &dither, &normalize })
    {
        t->setColour (juce::ToggleButton::textColourId, colours::text);
        t->onClick = [this] { update(); };
        addAndMakeVisible (*t);
    }
    for (auto* e : { &title, &artist })
        addAndMakeVisible (*e);
    info.setColour (juce::Label::textColourId, colours::accent2);
    info.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (info);

    exportButton.setColour (juce::TextButton::buttonColourId, colours::accent);
    exportButton.onClick = [this]
    {
        const auto s = current();
        auto& set = AppSettings::get();
        set.setValue ("exportFormat", format.getSelectedId());
        set.setValue ("exportRate", rate.getSelectedId());
        set.setValue ("exportDepth", depth.getSelectedId());
        set.setValue ("exportKbps", bitrate.getSelectedId());
        set.setValue ("exportChannels", channels.getSelectedId());
        set.setValue ("exportTail", tail.getSelectedId());
        set.setValue ("exportDither", dither.getToggleState());
        set.setValue ("exportNormalize", normalize.getToggleState());
        set.setValue ("exportArtist", artist.getText());
        set.saveIfNeeded();
        auto cb = exportCallback;
        close();
        if (cb) cb (s);
    };
    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (exportButton);
    addAndMakeVisible (cancelButton);

    update();
    setSize (520, 430);
}

RezalicaProcessor::ExportSettings ExportDialog::current() const
{
    RezalicaProcessor::ExportSettings s;
    s.format = format.getSelectedId() == 2 ? RezalicaProcessor::ExportSettings::mp3 : RezalicaProcessor::ExportSettings::wav;
    s.sampleRate = rate.getSelectedId();
    s.bitDepth = depth.getSelectedId();
    s.dither = dither.getToggleState();
    s.mp3Kbps = bitrate.getSelectedId();
    s.mono = channels.getSelectedId() == 1;
    s.normalize = normalize.getToggleState();
    s.tailSeconds = tail.getSelectedId() - 1;
    s.title = title.getText().trim();
    s.artist = artist.getText().trim();
    return s;
}

void ExportDialog::update()
{
    const bool mp3 = format.getSelectedId() == 2;
    // MP3: 44.1 / 48 kHz only; no bit depth / dither
    rate.setItemEnabled (88200, ! mp3);
    rate.setItemEnabled (96000, ! mp3);
    if (mp3 && rate.getSelectedId() > 48000) rate.setSelectedId (48000, juce::dontSendNotification);
    depth.setEnabled (! mp3);
    dither.setEnabled (! mp3 && depth.getSelectedId() == 16);
    bitrate.setEnabled (mp3);

    // length and an estimate of the file size
    auto& seq = processor.getSequencer();
    const double bpm = juce::jmax (20.0f, seq.effectiveBpm.load());
    const double seconds = seq.songTotalPpq() * 60.0 / bpm + (tail.getSelectedId() - 1);
    const int ch = channels.getSelectedId() == 1 ? 1 : 2;
    const double bytes = mp3 ? seconds * bitrate.getSelectedId() * 1000.0 / 8.0
                             : seconds * rate.getSelectedId() * ch * depth.getSelectedId() / 8.0;
    const int secs = juce::roundToInt (seconds);
    info.setText ("Length " + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2)
                      + "   about " + juce::String (bytes / (1024.0 * 1024.0), 1) + " MB",
                  juce::dontSendNotification);
}

void ExportDialog::close()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);
}

void ExportDialog::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void ExportDialog::resized()
{
    auto b = getLocalBounds().reduced (16);
    auto row = [&b] (juce::Label& l, juce::Component& c)
    {
        auto r = b.removeFromTop (28);
        l.setBounds (r.removeFromLeft (120));
        r.removeFromLeft (10);
        c.setBounds (r);
        b.removeFromTop (6);
    };
    row (formatL, format);
    row (rateL, rate);
    row (depthL, depth);
    {
        auto r = b.removeFromTop (22);
        r.removeFromLeft (130);
        dither.setBounds (r);
        b.removeFromTop (6);
    }
    row (bitrateL, bitrate);
    row (channelsL, channels);
    row (tailL, tail);
    {
        auto r = b.removeFromTop (22);
        r.removeFromLeft (130);
        normalize.setBounds (r);
        b.removeFromTop (6);
    }
    row (titleL, title);
    row (artistL, artist);
    auto buttons = b.removeFromBottom (32);
    exportButton.setBounds (buttons.removeFromRight (130));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (100));
    info.setBounds (b);
}
} // namespace rz
