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
    bool momentary = false;   // acts on press and on release (cue, shift, jog touch)
    bool led = false;         // has a state to show on the controller's LED
};

inline juce::String deckLetter (int deck) { return deck == 0 ? "A" : "B"; }
inline juce::String deckTarget (int deck, const juce::String& what) { return "deck." + deckLetter (deck) + "." + what; }

inline const std::vector<TargetInfo>& allTargets()
{
    static const std::vector<TargetInfo> list = []
    {
        std::vector<TargetInfo> t;
        auto add = [&] (juce::String id, juce::String label, juce::String group, ControlKind k = ControlKind::Absolute, bool toggle = false, bool trigger = false)
        { t.push_back ({ id, label, group, k, toggle, trigger, false, toggle }); };
        auto trig = [&] (juce::String id, juce::String label, juce::String group, bool led = false)
        { t.push_back ({ id, label, group, ControlKind::Button, false, true, false, led }); };
        auto hold = [&] (juce::String id, juce::String label, juce::String group, ControlKind k = ControlKind::Button, bool led = false)
        { t.push_back ({ id, label, group, k, false, false, true, led }); };

        add ("mixer.crossfader", "Crossfader", "Mixer");
        add ("mixer.master", "Master level", "Mixer");
        add ("mixer.cueMix", "Headphone cue/master mix", "Mixer");
        add ("mixer.cueLevel", "Headphone level", "Mixer");
        t.push_back ({ "library.scroll", "Library: scroll", "Library", ControlKind::Relative });

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
            trig (deckTarget (d, "play"), g + " play / pause", g, true);
            hold (deckTarget (d, "cue"), g + " cue", g, ControlKind::Button, true);
            trig (deckTarget (d, "sync"), g + " sync", g, true);
            trig (deckTarget (d, "load"), g + " load selected track", g);
            add (deckTarget (d, "tempo"), g + " tempo fader", g);
            add (deckTarget (d, "jog"), g + " jog wheel (scratch / nudge / seek)", g, ControlKind::Relative);
            hold (deckTarget (d, "jogTouch"), g + " jog wheel touch", g);
            hold (deckTarget (d, "shift"), g + " shift", g, ControlKind::Button, true);
            add (deckTarget (d, "macroJog"), g + " nudge selected macro", g, ControlKind::Relative);
            trig (deckTarget (d, "macroNext"), g + " select next macro", g);
            trig (deckTarget (d, "macroPrev"), g + " select previous macro", g);
            trig (deckTarget (d, "padMode.hotcue"), g + " pad mode: hot cues", g, true);
            trig (deckTarget (d, "padMode.loop"), g + " pad mode: loops", g, true);
            trig (deckTarget (d, "padMode.fx"), g + " pad mode: plugin on/off", g, true);
            trig (deckTarget (d, "padMode.instrument"), g + " pad mode: play instrument", g, true);
            for (int p = 1; p <= 8; ++p)
                hold (deckTarget (d, "pad." + juce::String (p)), g + " pad " + juce::String (p), g, ControlKind::Pad, true);
            trig (deckTarget (d, "loopHalve"), g + " loop halve", g);
            trig (deckTarget (d, "loopDouble"), g + " loop double", g);
            trig (deckTarget (d, "reloop"), g + " loop on/off", g, true);
            trig (deckTarget (d, "beatjumpBack"), g + " beat jump back 4", g);
            trig (deckTarget (d, "beatjumpFwd"), g + " beat jump forward 4", g);
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
