#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "ToolWindows.h"

namespace rz
{
class MixerPanel;

/** One effect slot: drop a plug-in from BROWSER > PLUGINS here, click to open it, right-click for more. */
class MixerSlotView : public juce::Component,
                      public juce::DragAndDropTarget,
                      public juce::SettableTooltipClient
{
public:
    MixerSlotView (MixerPanel& o, int busIndex, int slotIndex) : owner (o), bus (busIndex), slot (slotIndex) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("rzplugin:"); }
    void itemDragEnter (const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dragOver = false; repaint(); }
    void itemDropped (const SourceDetails&) override;
    void setBus (int b) { bus = b; repaint(); }

private:
    MixerPanel& owner;
    int bus, slot;
    bool dragOver = false;
};

/** BUS A-D / MASTER: 4 slots, fader, meter, mute / solo. */
class MixerStrip : public juce::Component
{
public:
    MixerStrip (MixerPanel& o, RezalicaProcessor& p, int busIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    /** A pad's effects: show another pad's chain. */
    void setBus (int b);

private:
    friend class MixerPanel;
    RezalicaProcessor& processor;
    int bus;
    juce::Label title;
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    juce::Slider fader;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;
    std::vector<std::unique_ptr<MixerSlotView>> slots;
    LevelMeter meter;
    juce::Rectangle<int> meterArea;
};

/** The mixer below the sampler: BUS A, B, C, D and MASTER, 4 third-party effects each. */
class MixerPanel : public juce::Component,
                   private juce::Timer,
                   private juce::ChangeListener
{
public:
    /** onlyChain >= 0: just that effect chain (the DECK's effects: no fader, no mute / solo). */
    explicit MixerPanel (RezalicaProcessor& p, int onlyChain = -1);
    ~MixerPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    RezalicaProcessor& getProcessor() noexcept { return processor; }
    void loadIntoSlot (int bus, int slot, const juce::PluginDescription&);
    void openEditor (int bus, int slot);
    void showSlotMenu (int bus, int slot, juce::Component* target);
    void closeAllEditors();
    /** The pad-effects view: show this pad's chain (rz::padChainBase + pad). */
    void showChain (int chain);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct EditorWindow;
    RezalicaProcessor& processor;
    std::vector<std::unique_ptr<MixerStrip>> strips;
    std::map<int, std::unique_ptr<EditorWindow>> editors; // key: bus * 10 + slot
};
} // namespace rz
