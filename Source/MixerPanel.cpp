#include "MixerPanel.h"
#include "MidiControl.h"

namespace rz
{
//==============================================================================
struct MixerPanel::EditorWindow : public juce::DocumentWindow
{
    std::unique_ptr<rz::StayOnTopOfRezalica> onTopHelper;
    EditorWindow (MixerPanel& o, int k, juce::AudioProcessor& p)
        : juce::DocumentWindow (p.getName(), colours::background, juce::DocumentWindow::closeButton), owner (o), key (k)
    {
        juce::AudioProcessorEditor* ed = p.createEditorIfNeeded();
        if (ed == nullptr)
            ed = new juce::GenericAudioProcessorEditor (p); // plug-ins without their own window
        setUsingNativeTitleBar (true);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        onTopHelper = std::make_unique<rz::StayOnTopOfRezalica> (*this); // on top only while Rezalica is in front
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        // deletes this window (and the plug-in's editor) later, from the owner
        juce::Component::SafePointer<MixerPanel> safe (&owner);
        const int k = key;
        juce::MessageManager::callAsync ([safe, k] { if (safe != nullptr) safe->editors.erase (k); });
        setVisible (false);
    }

    MixerPanel& owner;
    int key;
};

//==============================================================================
void MixerSlotView::paint (juce::Graphics& g)
{
    using namespace colours;
    auto& processor = owner.getProcessor();
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto name = processor.getSlotName (bus, slot);
    const bool bypassed = processor.getBus (bus).slots[(size_t) slot].bypass.load();
    g.setColour (dragOver ? accent.withAlpha (0.35f) : juce::Colour (0xff0d0d0e));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (dragOver ? accent : juce::Colours::black);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    if (name.isEmpty())
    {
        g.setColour (dim.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("--", r, juce::Justification::centred);
        return;
    }
    g.setColour (bypassed ? dim : accent2);
    g.fillRoundedRectangle (r.withWidth (4.0f).reduced (0.0f, 3.0f), 2.0f);
    g.setColour (bypassed ? dim : text);
    g.setFont (juce::FontOptions (juce::jmin (12.0f, r.getHeight() * 0.6f), juce::Font::bold));
    g.drawFittedText (name + (bypassed ? "  (off)" : ""), r.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1);
}

void MixerSlotView::mouseDown (const juce::MouseEvent& e)
{
    auto& processor = owner.getProcessor();
    if (! e.mods.isPopupMenu() && processor.getSlotProcessor (bus, slot) != nullptr)
        owner.openEditor (bus, slot);
    else
        owner.showSlotMenu (bus, slot, this);
}

void MixerSlotView::itemDropped (const SourceDetails& d)
{
    dragOver = false;
    repaint();
    const auto id = d.description.toString().fromFirstOccurrenceOf ("rzplugin:", false, false);
    if (auto* desc = owner.getProcessor().pluginHost->findByIdentifier (id))
        owner.loadIntoSlot (bus, slot, *desc);
}

//==============================================================================
MixerStrip::MixerStrip (MixerPanel& o, RezalicaProcessor& p, int busIndex) : processor (p), bus (busIndex)
{
    title.setText (bus == deckChain ? juce::String ("EFFECTS") : bus >= padChainBase ? "EFFECTS  " + padLabel (bus - padChainBase)
                                       : bus > 0 ? busName (bus) + "  SEND" : busName (bus),
                   juce::dontSendNotification);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, bus == 0 ? colours::accent : colours::text);
    addAndMakeVisible (title);

    for (auto* b : { &muteButton, &soloButton })
    {
        b->setClickingTogglesState (true);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    muteButton.setColour (juce::TextButton::buttonOnColourId, colours::accent);
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4fa3ff));
    muteButton.setTooltip ("Mute this bus");
    soloButton.setTooltip ("Solo: only the soloed buses play");
    soloButton.setVisible (bus != 0);
    muteButton.onClick = [this] { processor.getBus (bus).mute = muteButton.getToggleState(); };
    soloButton.onClick = [this] { processor.getBus (bus).solo = soloButton.getToggleState(); };

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 14);
    fader.setWantsKeyboardFocus (false);
    fader.setColour (juce::Slider::trackColourId, colours::accent);
    fader.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff0d0d0e));
    if (bus == 0)
    {
        // the MASTER fader is the sampler's master volume
        masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getApvts(), "master", fader);
    }
    else
    {
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setTooltip ("SEND: how much of the pads on this bus goes into its effects (the effect amount). "
                          "The pads themselves always play dry at full level.");
        fader.setValue (processor.getBus (bus).gainDb.load(), juce::dontSendNotification);
        fader.onValueChange = [this] { processor.getBus (bus).gainDb = (float) fader.getValue(); };
    }
    fader.setTextValueSuffix (" dB");
    fader.setNumDecimalPlacesToDisplay (1);
    fader.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (fader);

    const int slotCount = bus >= padChainBase ? MixerBus::maxSlots : MixerBus::numSlots; // a pad has 6
    for (int s = 0; s < slotCount; ++s)
    {
        slots.push_back (std::make_unique<MixerSlotView> (o, bus, s));
        slots.back()->setTooltip ("Drag an effect here from BROWSER > PLUGINS. Click: open it, right-click: bypass / remove / choose");
        addAndMakeVisible (*slots.back());
    }
    addAndMakeVisible (meter);

    {
        const auto prefix = busName (bus).toLowerCase().removeCharacters (" ");
        rz::setLearnId (fader, prefix + ".fader");
        rz::setLearnId (muteButton, prefix + ".mute");
        rz::setLearnId (soloButton, prefix + ".solo");
    }
    if (bus == deckChain || bus >= padChainBase)
    {
        // the DECK's / a pad's effects: only the slots (the volume is the deck's / the pad's GAIN)
        for (auto* c : std::initializer_list<juce::Component*> { &muteButton, &soloButton, &fader, &meter })
            c->setVisible (false);
    }
}

void MixerStrip::refresh()
{
    auto& b = processor.getBus (bus);
    if (bus == deckChain || bus >= padChainBase) { for (auto& s : slots) s->repaint(); return; }
    muteButton.setToggleState (b.mute.load(), juce::dontSendNotification);
    soloButton.setToggleState (b.solo.load(), juce::dontSendNotification);
    if (bus != 0 && ! fader.isMouseButtonDown())
        fader.setValue (b.gainDb.load(), juce::dontSendNotification);
    meter.level = b.peak.load();
    meter.repaint();
    for (auto& s : slots) s->repaint();
}

void MixerStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().withTrimmedBottom (3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 6.0f);
    g.setGradientFill (juce::ColourGradient (colours::panel.brighter (0.18f), r.getX(), r.getY(), colours::panel.darker (0.35f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 6.0f, r.getRight() - 6.0f);
    g.setColour ((bus == 0 || bus == deckChain ? colours::accent : colours::accent.withAlpha (0.5f)));
    g.drawRoundedRectangle (r.reduced (2.5f), 4.0f, 1.0f);
}

void MixerStrip::resized()
{
    auto b = getLocalBounds().reduced (8, 6);
    auto top = b.removeFromTop (22);
    if (soloButton.isVisible()) { soloButton.setBounds (top.removeFromRight (26)); top.removeFromRight (4); }
    muteButton.setBounds (top.removeFromRight (26));
    title.setBounds (top);
    b.removeFromTop (4);

    if (fader.isVisible())
    {
        auto right = b.removeFromRight (juce::jmax (56, b.getWidth() * 30 / 100));
        meter.setBounds (right.removeFromRight (8));
        right.removeFromRight (2);
        fader.setBounds (right);
        b.removeFromRight (6);
    }
    if (bus >= padChainBase)
    {
        // a pad: 6 slots in 2 columns x 3 rows
        const int rows = 3, gap = 6;
        const int sh = juce::jmin (36, (b.getHeight() - (rows - 1) * gap) / rows);
        const int sw = (b.getWidth() - gap) / 2;
        for (int i = 0; i < (int) slots.size(); ++i)
            slots[(size_t) i]->setBounds (b.getX() + (i % 2) * (sw + gap), b.getY() + (i / 2) * (sh + gap), sw, sh);
        return;
    }
    const int sh = juce::jmin (30, (b.getHeight() - 3 * 4) / MixerBus::numSlots);
    for (auto& s : slots)
    {
        s->setBounds (b.removeFromTop (sh));
        b.removeFromTop (4);
    }
}

//==============================================================================
MixerPanel::MixerPanel (RezalicaProcessor& p, int onlyChain) : processor (p)
{
    if (onlyChain >= 0)
        strips.push_back (std::make_unique<MixerStrip> (*this, p, onlyChain));
    else
    {
        for (int bi = 1; bi < numBuses; ++bi)
            strips.push_back (std::make_unique<MixerStrip> (*this, p, bi));
        strips.push_back (std::make_unique<MixerStrip> (*this, p, 0)); // MASTER on the right
    }
    for (auto& s : strips) addAndMakeVisible (*s);
    processor.pluginsAboutToChange[this] = [this] { closeAllEditors(); };
    processor.mixerChanged.addChangeListener (this);
    startTimerHz (20);
}

MixerPanel::~MixerPanel()
{
    stopTimer();
    processor.mixerChanged.removeChangeListener (this);
    processor.pluginsAboutToChange.erase (this);
    closeAllEditors();
}

void MixerPanel::closeAllEditors() { editors.clear(); }

void MixerStrip::setBus (int b)
{
    if (b == bus) return;
    bus = b;
    title.setText (bus >= padChainBase ? "EFFECTS  " + padLabel (bus - padChainBase) : busName (bus), juce::dontSendNotification);
    for (auto& s : slots) s->setBus (b);
    repaint();
}

void MixerPanel::showChain (int chain)
{
    for (auto& s : strips) s->setBus (chain);
}

void MixerPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& s : strips) s->refresh();
}

void MixerPanel::timerCallback()
{
    for (auto& s : strips) s->refresh();
}

void MixerPanel::paint (juce::Graphics&) {}

void MixerPanel::resized()
{
    auto b = getLocalBounds();
    const int gap = 8;
    const int w = (b.getWidth() - gap * ((int) strips.size() - 1)) / (int) strips.size();
    for (auto& s : strips)
    {
        s->setBounds (b.removeFromLeft (w));
        b.removeFromLeft (gap);
    }
}

void MixerPanel::loadIntoSlot (int bus, int slot, const juce::PluginDescription& d)
{
    juce::String err;
    if (! processor.loadPluginIntoSlot (bus, slot, d, err))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load effect", d.name + ": " + err);
    for (auto& s : strips) s->refresh();
}

void MixerPanel::openEditor (int bus, int slot)
{
    auto* p = processor.getSlotProcessor (bus, slot);
    if (p == nullptr) return;
    const int key = bus * 10 + slot;
    if (auto it = editors.find (key); it != editors.end())
    {
        it->second->setVisible (true);
        it->second->toFront (true);
        return;
    }
    editors[key] = std::make_unique<EditorWindow> (*this, key, *p);
}

void MixerPanel::showSlotMenu (int bus, int slot, juce::Component* target)
{
    juce::PopupMenu m;
    const bool filled = processor.getSlotProcessor (bus, slot) != nullptr;
    m.addSectionHeader (busName (bus) + "  slot " + juce::String (slot + 1));
    if (filled)
    {
        m.addItem (1, "Open " + processor.getSlotName (bus, slot));
        m.addItem (2, "Bypass", true, processor.getBus (bus).slots[(size_t) slot].bypass.load());
        m.addItem (3, "Remove");
        m.addSeparator();
    }
    juce::PopupMenu choose;
    const auto effects = processor.pluginHost->getEffects();
    for (int i = 0; i < effects.size(); ++i)
        choose.addItem (100 + i, effects[i].name + "   (" + effects[i].manufacturerName + ")");
    if (effects.isEmpty())
        choose.addItem (99, "(no effects yet: BROWSER > PLUGINS > SCAN)", false);
    m.addSubMenu (filled ? "Replace with" : "Load effect", choose);

    juce::Component::SafePointer<MixerPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe, bus, slot, effects] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pr = safe->processor;
        if (r == 1) safe->openEditor (bus, slot);
        else if (r == 2) { auto& b = pr.getBus (bus).slots[(size_t) slot].bypass; b = ! b.load(); }
        else if (r == 3) pr.clearSlot (bus, slot);
        else if (r >= 100 && r < 100 + effects.size()) safe->loadIntoSlot (bus, slot, effects[r - 100]);
        for (auto& s : safe->strips) s->refresh();
    });
}
} // namespace rz
