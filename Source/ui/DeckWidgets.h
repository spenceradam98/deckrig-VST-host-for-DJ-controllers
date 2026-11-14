#pragma once
#include "Widgets.h"

namespace deckrig
{
// Scrolling close-up waveform around the playhead, with beat grid, cue, hot cues and loop.
class WaveformView : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveformView (Deck& d, bool overview) : deck (d), isOverview (overview) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    double secondsVisible = 8.0;

private:
    void seekTo (float x);
    Deck& deck;
    bool isOverview;
    float lastDragX = 0;
};

// Title, tempo and time readout.
class TrackInfo : public juce::Component
{
public:
    explicit TrackInfo (HostEngine& e, Deck& d) : engine (e), deck (d) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    HostEngine& engine;
    Deck& deck;
};

// Button that acts on press and release (CUE, pads).
class HoldButton : public juce::TextButton
{
public:
    explicit HoldButton (const juce::String& text) : juce::TextButton (text) {}
    std::function<void (bool down)> onHold;
    std::function<bool()> isLit;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { juce::TextButton::mouseDown (e); return; }
        juce::TextButton::mouseDown (e);
        if (onHold) onHold (true);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseUp (e);
        if (! e.mods.isPopupMenu() && onHold) onHold (false);
    }
    void sync() { const bool on = isLit && isLit(); if (on != getToggleState()) setToggleState (on, juce::dontSendNotification); }
};
} // namespace deckrig
