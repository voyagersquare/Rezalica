#pragma once

#include "LookAndFeel.h"

namespace rz
{
/** Transport key with a symbol instead of text (record / play / pause / stop), on the same grey
    rubber button face as the rest of the UI. setActive() lights the symbol. */
class TransportButton : public juce::Button
{
public:
    enum Kind { record, play, pause, stop, metronome };
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::Button::mouseDown (e);
    }

    TransportButton (Kind k, const juce::String& name) : juce::Button (name), kind (k)
    {
        setWantsKeyboardFocus (false);
        setTooltip (name);
    }

    void setActive (bool a) { if (a != active) { active = a; repaint(); } }
    void setBlink (bool b)  { if (b != blink) { blink = b; repaint(); } }
    void setSwing (bool right) { if (right != swingRight) { swingRight = right; repaint(); } }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        using namespace colours;
        getLookAndFeel().drawButtonBackground (g, *this, buttonFace, highlighted, down);

        auto r = getLocalBounds().toFloat().reduced (5.0f);
        // the white frame of a transport key
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.drawRoundedRectangle (r, 4.0f, 1.6f);

        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        const auto c = r.getCentre().translated (0.0f, down ? 1.0f : 0.0f);
        const auto symbolOff = ink;

        switch (kind)
        {
            case record:
            {
                const bool lit = active && ! blink;
                auto dot = juce::Rectangle<float> (s * 1.15f, s * 1.15f).withCentre (c);
                if (lit)
                {
                    g.setColour (ledRed.withAlpha (0.35f));
                    g.fillEllipse (dot.expanded (4.0f));
                }
                g.setColour (lit ? ledRed : juce::Colour (0xffc42424));
                g.fillEllipse (dot);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawEllipse (dot, 1.0f);
                break;
            }
            case play:
            {
                juce::Path p;
                p.addTriangle (c.x - s * 0.42f, c.y - s * 0.55f, c.x - s * 0.42f, c.y + s * 0.55f, c.x + s * 0.58f, c.y);
                if (active)
                {
                    g.setColour (juce::Colour (0xff3ee05a).withAlpha (0.35f));
                    g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                g.setColour (active ? juce::Colour (0xff2bd14a) : symbolOff);
                g.fillPath (p);
                g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;
            }
            case pause:
            {
                const float bw = s * 0.32f, bh = s * 1.1f, gap = s * 0.22f;
                g.setColour (active && ! blink ? accent2 : symbolOff);
                g.fillRoundedRectangle (c.x - gap - bw, c.y - bh * 0.5f, bw, bh, 1.5f);
                g.fillRoundedRectangle (c.x + gap, c.y - bh * 0.5f, bw, bh, 1.5f);
                break;
            }
            case metronome:
            {
                // the classic metronome: a pyramid body, a scale window and a pendulum with its weight
                const float h = s * 1.5f, wb = s * 1.25f, wt = s * 0.45f;
                const float top = c.y - h * 0.5f, bottom = c.y + h * 0.5f;
                juce::Path body;
                body.startNewSubPath (c.x - wt * 0.5f, top);
                body.lineTo (c.x + wt * 0.5f, top);
                body.lineTo (c.x + wb * 0.5f, bottom);
                body.lineTo (c.x - wb * 0.5f, bottom);
                body.closeSubPath();
                const auto col = active ? (blink ? accent2.darker (0.3f) : accent2) : symbolOff;
                if (active)
                {
                    g.setColour (accent2.withAlpha (0.3f));
                    g.strokePath (body, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                g.setColour (col);
                g.strokePath (body, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                // base
                g.fillRoundedRectangle (c.x - wb * 0.6f, bottom - 1.0f, wb * 1.2f, 2.6f, 1.0f);
                // pendulum: swings when the metronome clicks
                const float angle = (active ? (swingRight ? 0.42f : -0.42f) : 0.38f);
                const juce::Point<float> pivot (c.x, bottom - h * 0.2f);
                const auto tip = pivot.getPointOnCircumference (h * 0.92f, angle);
                g.drawLine (juce::Line<float> (pivot, tip), 1.8f);
                const auto weight = pivot.getPointOnCircumference (h * 0.55f, angle);
                g.fillRoundedRectangle (juce::Rectangle<float> (s * 0.34f, s * 0.2f).withCentre (weight), 1.0f);
                break;
            }
            case stop:
            {
                g.setColour (symbolOff);
                g.fillRoundedRectangle (juce::Rectangle<float> (s * 1.0f, s * 1.0f).withCentre (c), 1.5f);
                break;
            }
        }
    }

private:
    Kind kind;
    bool active = false, blink = false, swingRight = false;
};
} // namespace rz

#include "Sequencer.h"

namespace rz
{
/** REC  PLAY  PAUSE  STOP  METRONOME as one block, for the pattern windows and the SONG window. */
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    enum Mode { stepPattern, pianoPattern, song };

    TransportBar (Sequencer& s, Mode m) : seq (s), mode (m)
    {
        for (auto* k : { &recKey, &playKey, &pauseKey, &stopKey, &clickKey })
            addAndMakeVisible (*k);
        recKey.setVisible (mode != song);
        recKey.onClick = [this]
        {
            auto& flag = mode == pianoPattern ? seq.pianoRecording : seq.recording;
            flag = ! flag.load();
        };
        playKey.onClick = [this]
        {
            if (seq.isPaused()) { seq.resumePlayback(); return; }
            if (mode == song) { if (! seq.isPlayingSong()) seq.playSong(); }
            else if (! seq.internalPlay.load()) seq.playPattern();
        };
        pauseKey.onClick = [this] { if (seq.isPaused()) seq.resumePlayback(); else seq.pausePlayback(); };
        stopKey.onClick = [this] { seq.stopPlayback(); if (onStop) onStop(); };
        clickKey.onClick = [this] { seq.metronome = ! seq.metronome.load(); };
        clickKey.onRightClick = [this]
        {
            juce::PopupMenu menu;
            menu.addSectionHeader ("Click volume");
            for (int i = 1; i <= 10; ++i)
                menu.addItem (i, juce::String (i * 10) + " %", true, juce::roundToInt (seq.metronomeLevel.load() * 10.0f) == i);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&clickKey), [this] (int r)
            {
                if (r > 0) seq.metronomeLevel = (float) r / 10.0f;
            });
        };
        timerCallback();
        startTimerHz (20);
    }

    /** Width for a given key size. */
    int getIdealWidth (int keyWidth = 44) const { const int n = mode == song ? 4 : 5; return n * keyWidth + (n - 1) * 4; }

    void resized() override
    {
        auto b = getLocalBounds();
        const int n = mode == song ? 4 : 5;
        const int w = (b.getWidth() - (n - 1) * 4) / n;
        for (auto* k : { &recKey, &playKey, &pauseKey, &stopKey, &clickKey })
        {
            if (! k->isVisible()) continue;
            k->setBounds (b.removeFromLeft (w));
            b.removeFromLeft (4);
        }
    }

    std::function<void()> onStop;

private:
    void timerCallback() override
    {
        const bool blinkOn = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
        const bool playing = mode == song ? seq.isPlayingSong() : seq.internalPlay.load();
        const bool rec = mode == pianoPattern ? seq.pianoRecording.load() : seq.recording.load();
        playKey.setActive (playing);
        pauseKey.setActive (seq.isPaused());
        pauseKey.setBlink (seq.isPaused() && ! blinkOn);
        recKey.setActive (rec);
        recKey.setBlink (rec && (seq.countingIn.load() || ! seq.internalPlay.load()) && ! blinkOn);
        clickKey.setActive (seq.metronome.load() || seq.countingIn.load());
        const double ppqPerMs = juce::jmax (20.0f, seq.effectiveBpm.load()) / 60000.0;
        clickKey.setSwing (seq.internalPlay.load() ? ((juce::int64) (juce::Time::getMillisecondCounterHiRes() * ppqPerMs)) % 2 == 0 : true);
    }

    Sequencer& seq;
    Mode mode;
    TransportButton recKey { TransportButton::record, "REC: record what you play into this pattern (uses the count-in)" },
                    playKey { TransportButton::play, "PLAY (space bar). After PAUSE it carries on" },
                    pauseKey { TransportButton::pause, "PAUSE: stop here, PLAY carries on from the same place" },
                    stopKey { TransportButton::stop, "STOP (back to the start)" },
                    clickKey { TransportButton::metronome, "Metronome: a click on every beat at the project tempo (right-click: click volume)" };
};
} // namespace rz
