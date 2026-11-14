// Headless tests for the mapping model and mixer maths.
#include "core/Mapping.h"
#include "core/MixerMath.h"
#include "core/Targets.h"
#include "BinaryData.h"
#include <cstdio>

using namespace deckrig;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool near (double a, double b, double eps = 1e-3) { return std::abs (a - b) <= eps; }

static void testDecode()
{
    const juce::uint8 cc[] = { 0xb1, 0x0a, 0x7f };
    auto r = decodeMidiBytes (cc, 3);
    CHECK (r.has_value() && r->address.type == MsgType::CC && r->address.channel == 2 && r->address.number == 10 && r->value == 127);

    const juce::uint8 on[] = { 0x95, 0x03, 0x40 }, onZero[] = { 0x95, 0x03, 0x00 }, off[] = { 0x85, 0x03, 0x40 };
    CHECK (decodeMidiBytes (on, 3)->noteOn);
    CHECK (decodeMidiBytes (onZero, 3)->noteOff);   // note-on velocity 0 counts as release
    CHECK (decodeMidiBytes (off, 3)->noteOff);
    CHECK (decodeMidiBytes (on, 3)->address == decodeMidiBytes (off, 3)->address);

    const juce::uint8 pb[] = { 0xe0, 0x00, 0x40 };
    CHECK (decodeMidiBytes (pb, 3)->value == 8192);
    const juce::uint8 clock[] = { 0xf8 };
    CHECK (! decodeMidiBytes (clock, 1).has_value());
}

static void testRelative()
{
    CHECK (decodeRelative (1, RelativeMode::TwosComplement) == 1);
    CHECK (decodeRelative (127, RelativeMode::TwosComplement) == -1);
    CHECK (decodeRelative (65, RelativeMode::TwosComplement) == -63);
    CHECK (decodeRelative (65, RelativeMode::Offset64) == 1);
    CHECK (decodeRelative (63, RelativeMode::Offset64) == -1);
    CHECK (decodeRelative (66, RelativeMode::SignBit) == -2);
}

static void testProfile()
{
    MappingProfile p;
    p.name = "Test";
    MidiAddress fader { MsgType::CC, 2, 0 };
    auto id = p.learn ("deck.A.volume", fader, ControlKind::Absolute);
    CHECK (p.controls.size() == 1 && p.bindings.size() == 1);
    // Learning the same hardware control for another target reuses the control.
    CHECK (p.learn ("deck.A.macro.1", fader, ControlKind::Absolute) == id);
    CHECK (p.controls.size() == 1 && p.bindings.size() == 2);
    // Re-learning a target replaces its old binding.
    p.learn ("deck.A.volume", { MsgType::CC, 2, 7 }, ControlKind::Absolute);
    CHECK (p.bindingsForTarget ("deck.A.volume").size() == 1);
    CHECK (p.controls.size() == 2);

    auto json = p.toJson();
    auto back = MappingProfile::fromJson (json);
    CHECK (back.has_value());
    CHECK (back->name == "Test" && back->controls.size() == 2 && back->bindings.size() == 2);
    CHECK (back->findControl (MidiAddress { MsgType::CC, 2, 7 }) != nullptr);

    juce::String err;
    CHECK (! MappingProfile::fromJson ("{\"format\":\"other\"}", &err).has_value() && err.isNotEmpty());
    CHECK (! MappingProfile::fromJson ("not json", &err).has_value());
}

static void testFactoryProfile()
{
    auto p = MappingProfile::fromJson (juce::String::fromUTF8 (BinaryData::herculesdjcontrolmixultra_json, BinaryData::herculesdjcontrolmixultra_jsonSize));
    CHECK (p.has_value());
    if (! p) return;
    CHECK (p->controls.size() > 40);
    // Every binding points at a real control and a real target.
    for (auto& b : p->bindings)
    {
        CHECK (p->findControl (b.control) != nullptr);
        if (findTarget (b.target) == nullptr) std::printf ("  unknown target %s\n", b.target.toRawUTF8());
        CHECK (findTarget (b.target) != nullptr);
    }
    // No two controls share a MIDI address.
    for (size_t i = 0; i < p->controls.size(); ++i)
        for (size_t j = i + 1; j < p->controls.size(); ++j)
            CHECK (p->controls[i].address != p->controls[j].address);
}

static void testMixerMaths()
{
    auto mid = crossfade (0.5f, XfadeCurve::Smooth);
    CHECK (near (mid.a, std::sqrt (0.5)) && near (mid.b, std::sqrt (0.5)));
    CHECK (near (crossfade (0.0f, XfadeCurve::Smooth).b, 0.0) && near (crossfade (1.0f, XfadeCurve::Smooth).a, 0.0));
    auto sharp = crossfade (0.5f, XfadeCurve::Sharp);
    CHECK (near (sharp.a, 1.0) && near (sharp.b, 1.0));
    CHECK (near (crossfade (0.0f, XfadeCurve::Sharp).b, 0.0));

    CHECK (near (eqKnobToDb (0.5f), 0.0));
    CHECK (near (eqKnobToDb (1.0f), 6.0));
    CHECK (eqKnobToDb (0.0f) <= -90.0f);
    CHECK (eqKnobToDb (0.3f) < 0.0f && eqKnobToDb (0.3f) > -40.0f);
    CHECK (near (gainKnobToDb (0.5f), 0.0));
    CHECK (near (faderToGain (1.0f), 1.0) && near (faderToGain (0.0f), 0.0));

    // Flat EQ passes a sine through unchanged; a low-band kill removes 50 Hz.
    auto rms = [] (Biquad& f, double freq)
    {
        double acc = 0; const int n = 48000;
        for (int i = 0; i < n; ++i)
        {
            const float y = f.process ((float) std::sin (2 * pi * freq * i / 48000.0), 0);
            if (i > n / 2) acc += (double) y * y;
        }
        return std::sqrt (acc / (n / 2 - 1));
    };
    Biquad flat; flat.lowShelf (48000, 200, 0.0);
    CHECK (near (rms (flat, 1000), std::sqrt (0.5), 1e-3));
    Biquad kill; kill.lowShelf (48000, 200, -60.0);
    CHECK (rms (kill, 50) < 0.05);
    Biquad keep; keep.lowShelf (48000, 200, -60.0);
    CHECK (rms (keep, 5000) > 0.6);

    bool lp = false, active = true;
    filterCutoff (0.5f, lp, active);
    CHECK (! active);
    const double fLow = filterCutoff (0.0f, lp, active);
    CHECK (active && lp && near (fLow, 40.0, 0.5));
    const double fHigh = filterCutoff (1.0f, lp, active);
    CHECK (active && ! lp && near (fHigh, 8000.0, 1.0));
}

static void testTargets()
{
    CHECK (findTarget ("mixer.crossfader") != nullptr);
    CHECK (findTarget ("deck.B.macro.8") != nullptr);
    CHECK (findTarget ("deck.A.slot.8.bypass") != nullptr && findTarget ("deck.A.slot.8.bypass")->toggle);
    CHECK (findTarget ("deck.C.volume") == nullptr);
    juce::StringArray ids;
    for (auto& t : allTargets()) ids.add (t.id);
    CHECK (ids.size() == (int) allTargets().size());
    ids.removeDuplicates (false);
    CHECK (ids.size() == (int) allTargets().size());
}

int main()
{
    testDecode();
    testRelative();
    testProfile();
    testFactoryProfile();
    testMixerMaths();
    testTargets();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
