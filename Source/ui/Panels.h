#pragma once
#include "Widgets.h"

namespace deckrig
{
class DeckPanel : public juce::Component
{
public:
    DeckPanel (HostEngine&, Deck&, std::function<void()> openScanner);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();          // 30 Hz from the main component
    void themeChanged();
    void refreshInputs();
    ChainList chainList;

private:
    void showMacroMenu (int macro);

    HostEngine& engine;
    Deck& deck;
    juce::Label title, inputLabel, macroTitle;
    juce::ComboBox input;
    juce::OwnedArray<Knob> macros;
    Knob gain, high, mid, low, filter;
    Fader volume;
    ToggleKey pfl, fxBypass;
    StereoMeter meter;
};

class MixerPanel : public juce::Component
{
public:
    explicit MixerPanel (HostEngine&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    void themeChanged();

private:
    HostEngine& engine;
    juce::Label title, xfLabel;
    Knob master, cueMix, cueLevel;
    Fader crossfader;
    juce::ComboBox curve;
    StereoMeter meter;
};

void paintPanel (juce::Graphics&, const juce::Component&, juce::Colour edgeTint);
} // namespace deckrig
