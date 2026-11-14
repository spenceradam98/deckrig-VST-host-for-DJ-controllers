#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/HostEngine.h"
#include "Theme.h"

namespace deckrig
{
// Any component with this property can be MIDI-learned by clicking it in learn mode.
inline void setTargetId (juce::Component& c, const juce::String& id) { c.getProperties().set ("target", id); }
inline juce::String getTargetId (const juce::Component& c) { return c.getProperties()["target"].toString(); }

inline const Theme& themeOf (const juce::Component& c)
{
    if (auto* lf = dynamic_cast<ThemedLookAndFeel*> (&c.getLookAndFeel()))
        return lf->getTheme();
    return getThemes().front();
}

// Rotary knob with a caption, bound to a continuous engine target.
class Knob : public juce::Component
{
public:
    Knob (HostEngine& e, const juce::String& target, const juce::String& caption, bool bipolar = false, float defaultValue = 0.5f);
    void resized() override;
    void syncFromEngine();
    void setCaption (const juce::String& s) { label.setText (s, juce::dontSendNotification); }
    void setRingColour (juce::Colour c) { slider.getProperties().set ("ring", (juce::int64) c.getARGB()); repaint(); }
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label label;
    std::function<void (const juce::MouseEvent&)> onRightClick;
    void mouseDown (const juce::MouseEvent&) override;

private:
    HostEngine& engine;
    juce::String target;
};

// Vertical or horizontal fader bound to a continuous target.
class Fader : public juce::Component
{
public:
    Fader (HostEngine& e, const juce::String& target, bool vertical, float defaultValue);
    void resized() override { slider.setBounds (getLocalBounds()); }
    void syncFromEngine();
    void setColour (juce::Colour c) { slider.getProperties().set ("ring", (juce::int64) c.getARGB()); repaint(); }
    juce::Slider slider;

private:
    HostEngine& engine;
    juce::String target;
};

// Lit button bound to a toggle target.
class ToggleKey : public juce::TextButton
{
public:
    ToggleKey (HostEngine& e, const juce::String& target, const juce::String& text);
    void syncFromEngine();
    void setLed (juce::Colour c) { getProperties().set ("ledColour", (juce::int64) c.getARGB()); }

private:
    HostEngine& engine;
    juce::String target;
};

class StereoMeter : public juce::Component
{
public:
    StereoMeter (std::atomic<float>& l, std::atomic<float>& r) : left (l), right (r) {}
    void paint (juce::Graphics&) override;
    void tick() { repaint(); }

private:
    std::atomic<float>& left;
    std::atomic<float>& right;
};

// The plugins in one deck's chain: click to open, right-click for options.
class ChainList : public juce::Component, private juce::ChangeListener
{
public:
    ChainList (HostEngine&, Deck&, std::function<void()> openScanner);
    ~ChainList() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void closeAllWindows();
    void rebuild();

private:
    class PluginWindow;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void showAddMenu();
    void showSlotMenu (int index);
    void openEditor (int index);
    void closeEditor (juce::AudioProcessor*);

    HostEngine& engine;
    Deck& deck;
    std::function<void()> openScanner;
    juce::TextButton addButton { "+ ADD PLUGIN" };
    juce::OwnedArray<ToggleKey> slotButtons;
    std::vector<std::unique_ptr<PluginWindow>> windows;
    bool suppressClick = false;
};
} // namespace deckrig
