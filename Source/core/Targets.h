#pragma once
#include "Mapping.h"

// Everything a controller can drive. Ids are stable strings stored in mapping files.
namespace deckrig
{
constexpr int numDecks = 2;
constexpr int numMacros = 8;
constexpr int maxSlots = 8;

struct TargetInfo
{
    juce::String id, label, group;
    ControlKind preferredKind = ControlKind::Absolute;
    bool toggle = false;      // button flips state; LED shows it
    bool trigger = false;     // button fires once on press
};

inline juce::String deckLetter (int deck) { return deck == 0 ? "A" : "B"; }
inline juce::String deckTarget (int deck, const juce::String& what) { return "deck." + deckLetter (deck) + "." + what; }

inline const std::vector<TargetInfo>& allTargets()
{
    static const std::vector<TargetInfo> list = []
    {
        std::vector<TargetInfo> t;
        auto add = [&] (juce::String id, juce::String label, juce::String group, ControlKind k = ControlKind::Absolute, bool toggle = false, bool trigger = false)
        { t.push_back ({ id, label, group, k, toggle, trigger }); };

        add ("mixer.crossfader", "Crossfader", "Mixer");
        add ("mixer.master", "Master level", "Mixer");
        add ("mixer.cueMix", "Headphone cue/master mix", "Mixer");
        add ("mixer.cueLevel", "Headphone level", "Mixer");

        for (int d = 0; d < numDecks; ++d)
        {
            const auto g = "Deck " + deckLetter (d);
            add (deckTarget (d, "volume"), g + " volume fader", g);
            add (deckTarget (d, "gain"), g + " gain", g);
            add (deckTarget (d, "eqHigh"), g + " EQ high", g);
            add (deckTarget (d, "eqMid"), g + " EQ mid", g);
            add (deckTarget (d, "eqLow"), g + " EQ low", g);
            add (deckTarget (d, "filter"), g + " filter", g);
            add (deckTarget (d, "pfl"), g + " headphone cue (PFL)", g, ControlKind::Button, true);
            add (deckTarget (d, "chainBypass"), g + " FX chain bypass", g, ControlKind::Button, true);
            add (deckTarget (d, "jog"), g + " jog: nudge selected macro", g, ControlKind::Relative);
            add (deckTarget (d, "macroNext"), g + " select next macro", g, ControlKind::Button, false, true);
            add (deckTarget (d, "macroPrev"), g + " select previous macro", g, ControlKind::Button, false, true);
            add (deckTarget (d, "pads"), g + " pads: play deck instrument", g, ControlKind::Pad);
            for (int m = 1; m <= numMacros; ++m)
                add (deckTarget (d, "macro." + juce::String (m)), g + " macro " + juce::String (m), g);
            for (int s = 1; s <= maxSlots; ++s)
                add (deckTarget (d, "slot." + juce::String (s) + ".bypass"), g + " plugin " + juce::String (s) + " on/off", g, ControlKind::Button, true);
        }
        return t;
    }();
    return list;
}

inline const TargetInfo* findTarget (const juce::String& id)
{
    for (auto& t : allTargets())
        if (t.id == id) return &t;
    return nullptr;
}
} // namespace deckrig
