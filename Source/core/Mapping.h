#pragma once
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

// Controller mapping model. Pure juce_core so it can be unit-tested headless.
//
// A profile has two layers:
//   controls  - what the hardware sends: a named physical control and its MIDI address
//   bindings  - what DeckRig does with it: control id -> host target id
// Re-learning the hardware (the "Learn controller" wizard) rewrites controls only,
// so bindings survive when a controller's MIDI numbers turn out different.
namespace deckrig
{
enum class MsgType { CC, Note, PitchBend };

// How a control's values are read.
//   Absolute: faders and knobs, 0..127 -> 0..1
//   Relative: jog wheels and endless encoders, signed steps
//   Button:   note or CC, press/release; toggle targets flip on press
//   Pad:      note with velocity, passed through to the deck's instrument
enum class ControlKind { Absolute, Relative, Button, Pad };

// Relative encoder encodings seen on DJ controllers.
//   TwosComplement: 1..63 = +1..+63, 65..127 = -63..-1 (Hercules, most others)
//   Offset64:       64 = 0, 65 = +1, 63 = -1
//   SignBit:        1..63 = +, 65..127 = -(v-64)
enum class RelativeMode { TwosComplement, Offset64, SignBit };

struct MidiAddress
{
    MsgType type = MsgType::CC;
    int channel = 1;   // 1..16
    int number = 0;    // CC or note number (ignored for pitch bend)

    bool operator== (const MidiAddress& o) const noexcept
    {
        return type == o.type && channel == o.channel && (type == MsgType::PitchBend || number == o.number);
    }
    bool operator!= (const MidiAddress& o) const noexcept { return ! (*this == o); }
    juce::String toString() const;  // e.g. "CC 2/10", "Note 1/0x13"
};

struct ControlDef
{
    juce::String id;     // stable key, e.g. "deckA.jog"
    juce::String label;  // shown in UI, e.g. "Deck A jog wheel"
    MidiAddress address;
    ControlKind kind = ControlKind::Absolute;
    RelativeMode relative = RelativeMode::TwosComplement;
    bool led = false;    // echo state back as note-on velocity 127/0
    bool learned = false; // true once captured from the real hardware
};

struct Binding
{
    juce::String control;  // ControlDef::id
    juce::String target;   // host target id, see Targets.h
    float sensitivity = 1.0f; // relative controls: units per step = sensitivity / 128
    bool invert = false;
    bool shift = false;    // only while a Shift button is held
};

// One decoded incoming MIDI event.
struct RawMidi
{
    MidiAddress address;
    int value = 0;        // 0..127, or 0..16383 for pitch bend
    bool noteOn = false;  // note-on with velocity > 0
    bool noteOff = false;
};

std::optional<RawMidi> decodeMidiBytes (const juce::uint8* data, int size);

// Signed step count for a relative control value.
int decodeRelative (int value, RelativeMode mode) noexcept;

struct MappingProfile
{
    juce::String name { "Untitled" };
    juce::String deviceMatch; // case-insensitive substring of the MIDI device name
    juce::String notes;
    std::vector<ControlDef> controls;
    std::vector<Binding> bindings;

    const ControlDef* findControl (const juce::String& id) const;
    ControlDef* findControl (const juce::String& id);
    const ControlDef* findControl (const MidiAddress&) const;
    std::vector<const Binding*> bindingsFor (const juce::String& controlId) const;
    // Bindings that apply right now: shift bindings while shift is held (falling back
    // to normal ones if the control has none), normal bindings otherwise.
    std::vector<const Binding*> activeBindingsFor (const juce::String& controlId, bool shiftHeld) const;
    std::vector<const Binding*> bindingsForTarget (const juce::String& target) const;

    // Binds target to the control with this address, creating a control if none
    // exists. Replaces any previous binding of that target. Returns the control id.
    juce::String learn (const juce::String& target, const MidiAddress&, ControlKind kindHint);
    void unbindTarget (const juce::String& target);

    juce::var toVar() const;
    static MappingProfile fromVar (const juce::var&);
    juce::String toJson() const;
    static std::optional<MappingProfile> fromJson (const juce::String&, juce::String* error = nullptr);
};

juce::String toString (MsgType);
juce::String toString (ControlKind);
juce::String toString (RelativeMode);
MsgType msgTypeFrom (const juce::String&);
ControlKind controlKindFrom (const juce::String&);
RelativeMode relativeModeFrom (const juce::String&);
} // namespace deckrig
