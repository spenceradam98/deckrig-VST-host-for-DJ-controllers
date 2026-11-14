#pragma once
#include "DeckWidgets.h"

namespace deckrig
{
class DeckPanel : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  public juce::DragAndDropTarget
{
public:
    DeckPanel (HostEngine&, Deck&, std::function<void()> openScanner);
    ~DeckPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();          // 30 Hz from the main component
    void themeChanged();
    void refreshInputs();
    ChainList chainList;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    void showMacroMenu (int macro);
    void showRigMenu();
    void updatePadLabels();

    HostEngine& engine;
    Deck& deck;
    juce::Label title, inputLabel, macroTitle;
    juce::ComboBox input;
    juce::TextButton tempoRange;
    juce::TextButton rigButton { "RIG" };
    TrackInfo info;
    WaveformView wave, overview;
    juce::TextButton play { "PLAY" }, sync { "SYNC" };
    HoldButton cue { "CUE" };
    juce::OwnedArray<juce::TextButton> modeButtons;
    juce::OwnedArray<HoldButton> pads;
    juce::OwnedArray<Knob> macros;
    Knob gain, high, mid, low, filter;
    Fader volume, tempo;
    ToggleKey pfl, fxBypass;
    StereoMeter meter;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastPadMode = -1, lastChainSize = -1;
    bool dragOver = false;
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
