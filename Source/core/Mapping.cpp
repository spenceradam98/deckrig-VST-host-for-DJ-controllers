#include "Mapping.h"

namespace deckrig
{
using namespace juce;

String toString (MsgType t)
{
    switch (t) { case MsgType::CC: return "cc"; case MsgType::Note: return "note"; case MsgType::PitchBend: return "pitchbend"; }
    return "cc";
}
String toString (ControlKind k)
{
    switch (k) { case ControlKind::Absolute: return "absolute"; case ControlKind::Relative: return "relative";
                 case ControlKind::Button: return "button"; case ControlKind::Pad: return "pad"; }
    return "absolute";
}
String toString (RelativeMode m)
{
    switch (m) { case RelativeMode::TwosComplement: return "twos"; case RelativeMode::Offset64: return "offset64";
                 case RelativeMode::SignBit: return "signbit"; }
    return "twos";
}
MsgType msgTypeFrom (const String& s)
{
    if (s.equalsIgnoreCase ("note")) return MsgType::Note;
    if (s.equalsIgnoreCase ("pitchbend")) return MsgType::PitchBend;
    return MsgType::CC;
}
ControlKind controlKindFrom (const String& s)
{
    if (s.equalsIgnoreCase ("relative")) return ControlKind::Relative;
    if (s.equalsIgnoreCase ("button")) return ControlKind::Button;
    if (s.equalsIgnoreCase ("pad")) return ControlKind::Pad;
    return ControlKind::Absolute;
}
RelativeMode relativeModeFrom (const String& s)
{
    if (s.equalsIgnoreCase ("offset64")) return RelativeMode::Offset64;
    if (s.equalsIgnoreCase ("signbit")) return RelativeMode::SignBit;
    return RelativeMode::TwosComplement;
}

String MidiAddress::toString() const
{
    switch (type)
    {
        case MsgType::CC:        return "CC ch" + String (channel) + " #" + String (number) + " (0x" + String::toHexString (number).paddedLeft ('0', 2).toUpperCase() + ")";
        case MsgType::Note:      return "Note ch" + String (channel) + " #" + String (number) + " (0x" + String::toHexString (number).paddedLeft ('0', 2).toUpperCase() + ")";
        case MsgType::PitchBend: return "Pitch bend ch" + String (channel);
    }
    return {};
}

std::optional<RawMidi> decodeMidiBytes (const uint8* d, int size)
{
    if (d == nullptr || size < 2)
        return std::nullopt;
    const int status = d[0] & 0xf0, ch = (d[0] & 0x0f) + 1;
    RawMidi r;
    r.address.channel = ch;
    switch (status)
    {
        case 0xb0:
            if (size < 3) return std::nullopt;
            r.address.type = MsgType::CC; r.address.number = d[1] & 0x7f; r.value = d[2] & 0x7f;
            return r;
        case 0x90:
        case 0x80:
            if (size < 3) return std::nullopt;
            r.address.type = MsgType::Note; r.address.number = d[1] & 0x7f; r.value = d[2] & 0x7f;
            r.noteOn = status == 0x90 && r.value > 0;
            r.noteOff = ! r.noteOn;
            return r;
        case 0xe0:
            if (size < 3) return std::nullopt;
            r.address.type = MsgType::PitchBend; r.value = (d[1] & 0x7f) | ((d[2] & 0x7f) << 7);
            return r;
        default:
            return std::nullopt;
    }
}

int decodeRelative (int v, RelativeMode mode) noexcept
{
    v &= 0x7f;
    switch (mode)
    {
        case RelativeMode::TwosComplement: return v < 64 ? v : v - 128;
        case RelativeMode::Offset64:       return v - 64;
        case RelativeMode::SignBit:        return v < 64 ? v : -(v - 64);
    }
    return 0;
}

const ControlDef* MappingProfile::findControl (const String& id) const
{
    for (auto& c : controls) if (c.id == id) return &c;
    return nullptr;
}
ControlDef* MappingProfile::findControl (const String& id)
{
    for (auto& c : controls) if (c.id == id) return &c;
    return nullptr;
}
const ControlDef* MappingProfile::findControl (const MidiAddress& a) const
{
    for (auto& c : controls) if (c.address == a) return &c;
    return nullptr;
}
std::vector<const Binding*> MappingProfile::bindingsFor (const String& controlId) const
{
    std::vector<const Binding*> out;
    for (auto& b : bindings) if (b.control == controlId) out.push_back (&b);
    return out;
}
std::vector<const Binding*> MappingProfile::activeBindingsFor (const String& controlId, bool shiftHeld) const
{
    std::vector<const Binding*> normal, shifted;
    for (auto& b : bindings)
        if (b.control == controlId)
            (b.shift ? shifted : normal).push_back (&b);
    if (shiftHeld && ! shifted.empty()) return shifted;
    return normal;
}

std::vector<const Binding*> MappingProfile::bindingsForTarget (const String& target) const
{
    std::vector<const Binding*> out;
    for (auto& b : bindings) if (b.target == target) out.push_back (&b);
    return out;
}

void MappingProfile::unbindTarget (const String& target)
{
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(), [&] (auto& b) { return b.target == target; }), bindings.end());
}

String MappingProfile::learn (const String& target, const MidiAddress& a, ControlKind kindHint)
{
    String id;
    if (auto* existing = findControl (a))
    {
        id = existing->id;
    }
    else
    {
        ControlDef c;
        c.address = a;
        c.kind = kindHint;
        c.led = kindHint == ControlKind::Button && a.type == MsgType::Note;
        c.learned = true;
        c.id = "learned." + toString (a.type) + "." + String (a.channel) + "." + String (a.number);
        c.label = a.toString();
        controls.push_back (c);
        id = c.id;
    }
    unbindTarget (target);
    Binding b;
    b.control = id;
    b.target = target;
    bindings.push_back (b);
    return id;
}

var MappingProfile::toVar() const
{
    auto* root = new DynamicObject();
    root->setProperty ("format", "deckrig-mapping");
    root->setProperty ("version", 1);
    root->setProperty ("name", name);
    root->setProperty ("deviceMatch", deviceMatch);
    root->setProperty ("notes", notes);

    Array<var> cs;
    for (auto& c : controls)
    {
        auto* o = new DynamicObject();
        o->setProperty ("id", c.id);
        o->setProperty ("label", c.label);
        o->setProperty ("type", toString (c.address.type));
        o->setProperty ("channel", c.address.channel);
        o->setProperty ("number", c.address.number);
        o->setProperty ("kind", toString (c.kind));
        if (c.kind == ControlKind::Relative) o->setProperty ("relative", toString (c.relative));
        if (c.led) o->setProperty ("led", true);
        o->setProperty ("learned", c.learned);
        cs.add (var (o));
    }
    root->setProperty ("controls", cs);

    Array<var> bs;
    for (auto& b : bindings)
    {
        auto* o = new DynamicObject();
        o->setProperty ("control", b.control);
        o->setProperty ("target", b.target);
        if (b.sensitivity != 1.0f) o->setProperty ("sensitivity", b.sensitivity);
        if (b.invert) o->setProperty ("invert", true);
        if (b.shift) o->setProperty ("shift", true);
        bs.add (var (o));
    }
    root->setProperty ("bindings", bs);
    return var (root);
}

MappingProfile MappingProfile::fromVar (const var& v)
{
    MappingProfile p;
    p.name = v.getProperty ("name", "Untitled").toString();
    p.deviceMatch = v.getProperty ("deviceMatch", {}).toString();
    p.notes = v.getProperty ("notes", {}).toString();
    if (auto* arr = v.getProperty ("controls", {}).getArray())
        for (auto& o : *arr)
        {
            ControlDef c;
            c.id = o.getProperty ("id", {}).toString();
            if (c.id.isEmpty()) continue;
            c.label = o.getProperty ("label", c.id).toString();
            c.address.type = msgTypeFrom (o.getProperty ("type", "cc").toString());
            c.address.channel = jlimit (1, 16, (int) o.getProperty ("channel", 1));
            c.address.number = jlimit (0, 127, (int) o.getProperty ("number", 0));
            c.kind = controlKindFrom (o.getProperty ("kind", "absolute").toString());
            c.relative = relativeModeFrom (o.getProperty ("relative", "twos").toString());
            c.led = (bool) o.getProperty ("led", false);
            c.learned = (bool) o.getProperty ("learned", false);
            p.controls.push_back (c);
        }
    if (auto* arr = v.getProperty ("bindings", {}).getArray())
        for (auto& o : *arr)
        {
            Binding b;
            b.control = o.getProperty ("control", {}).toString();
            b.target = o.getProperty ("target", {}).toString();
            b.sensitivity = (float) (double) o.getProperty ("sensitivity", 1.0);
            b.invert = (bool) o.getProperty ("invert", false);
            b.shift = (bool) o.getProperty ("shift", false);
            if (b.control.isNotEmpty() && b.target.isNotEmpty())
                p.bindings.push_back (b);
        }
    return p;
}

String MappingProfile::toJson() const
{
    return JSON::toString (toVar(), false);
}

std::optional<MappingProfile> MappingProfile::fromJson (const String& text, String* error)
{
    var v;
    auto res = JSON::parse (text, v);
    if (res.failed())
    {
        if (error) *error = res.getErrorMessage();
        return std::nullopt;
    }
    if (v.getProperty ("format", {}).toString() != "deckrig-mapping")
    {
        if (error) *error = "Not a DeckRig mapping file";
        return std::nullopt;
    }
    return fromVar (v);
}
} // namespace deckrig
