#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "hosting/PluginChain.h"
#include "TrackPlayer.h"
#include "core/MixerMath.h"
#include "core/Targets.h"

namespace deckrig
{
// Knob/fader positions, all 0..1, written by UI and MIDI, read by the audio thread.
struct DeckParams
{
    std::atomic<float> gain { 0.5f }, eqHigh { 0.5f }, eqMid { 0.5f }, eqLow { 0.5f }, filter { 0.5f }, volume { 0.8f };
    std::atomic<bool> pfl { false }, chainBypass { false };
    std::atomic<int> inputPair { 0 };  // -1 = no live input, 0 = device inputs 1/2, 1 = 3/4 ...
    std::atomic<float> tempo { 0.5f };  // 0.5 = 0 %
    std::atomic<int> tempoRange { 0 };  // index into tempoRanges
    std::atomic<int> padMode { 0 };     // PadMode
};

enum PadMode { padHotcue = 0, padLoop = 1, padFx = 2, padInstrument = 3 };
constexpr float tempoRanges[] = { 0.08f, 0.16f, 0.50f };
constexpr double loopPadBeats[] = { 0.5, 1, 2, 4, 8, 16 };

// A macro knob drives one parameter of one plugin in the deck's chain.
struct MacroAssign
{
    int slot = -1, param = -1;
    juce::String label;  // cached "Plugin: Param" for the UI
    bool isAssigned() const noexcept { return slot >= 0 && param >= 0; }
};

// One deck: live input and/or instrument -> plugin chain -> gain -> 3-band EQ -> filter.
// The engine applies the volume fader, crossfader and cue routing when mixing.
class Deck
{
public:
    Deck (int index, PluginLibrary&);

    void prepare (double sampleRate, int maxBlock);
    void release();

    // Audio thread. Renders numSamples into getBuffer() (stereo, pre-fader).
    // `other` is the deck that sync follows.
    void process (const float* const* inputs, int numInputs, int numSamples, const Deck* other);
    const juce::AudioBuffer<float>& getBuffer() const noexcept { return buffer; }

    // Message thread
    juce::AudioProcessorParameter* getMacroParameter (int macro) const;
    float getMacroValue (int macro) const;
    void setMacroValue (int macro, float value01);
    void assignMacro (int macro, int slot, int param);
    void validateMacros();  // after the chain changes

    // Rig preset: the plugin chain with every plugin's state, plus macro assignments.
    std::unique_ptr<juce::XmlElement> createRigXml() const;
    void restoreRigXml (const juce::XmlElement&);

    const int index;
    TrackPlayer player;
    PluginChain chain;
    DeckParams params;
    std::array<MacroAssign, numMacros> macros;
    std::atomic<int> selectedMacro { 0 };
    juce::MidiMessageCollector midiIn;   // pads -> instrument, filled from the MIDI thread
    std::atomic<float> meterL { 0 }, meterR { 0 };

private:
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    double sampleRate = 44100.0;
    Biquad low, mid, high, filt;
    float lastLow = -1, lastMid = -1, lastHigh = -1, lastFilter = -1;
    bool filterOn = false;
    juce::SmoothedValue<float> trim;
    void updateFilters();
};
} // namespace deckrig
