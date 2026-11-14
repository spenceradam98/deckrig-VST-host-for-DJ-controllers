#pragma once
#include "Panels.h"

namespace deckrig
{
class MainComponent : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    MainComponent (HostEngine&, juce::PropertiesFile* settings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void setTheme (int index);
    void saveState();
    void setLearnMode (bool);
    void setMonitorVisible (bool);

    static MappingProfile loadFactoryProfile();
    static juce::File getMappingFile();

private:
    class LearnOverlay;
    class Wizard;
    class MappingList;
    class ScannerWindow;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showControllerMenu();
    void showAudioSettings();
    void openScanner();
    void startWizard();
    void saveProfile();
    void updateStatus();
    void themeChanged();

    HostEngine& engine;
    juce::PropertiesFile* settings;
    ThemedLookAndFeel lnf;
    int themeIndex = 0;

    juce::Label logo, status;
    juce::TextButton audioButton { "AUDIO / MIDI" }, pluginsButton { "PLUGINS" }, controllerButton { "CONTROLLER" },
                     learnButton { "MIDI LEARN" }, monitorButton { "MONITOR" };
    juce::ComboBox themeBox;

    std::unique_ptr<DeckPanel> deckA, deckB;
    MixerPanel mixer;
    juce::TextEditor monitorText;
    std::unique_ptr<MappingList> mappingList;
    std::unique_ptr<LearnOverlay> overlay;
    std::unique_ptr<Wizard> wizard;
    std::unique_ptr<ScannerWindow> scanner;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastMonitorCount = -1;
    juce::String lastMonitorTail;
    bool monitorVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace deckrig
