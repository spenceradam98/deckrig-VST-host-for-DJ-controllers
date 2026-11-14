// Headless tests for the mapping model and mixer maths.
#include "core/Mapping.h"
#include "core/MixerMath.h"
#include "core/Targets.h"
#include "core/TrackAnalysis.h"
#include "audio/TrackPlayer.h"
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

// Synthetic track: a decaying kick-like thump on every beat plus quiet hi-hat noise off-beat.
static std::vector<float> clickTrack (double bpm, double sr, double seconds, double offset)
{
    std::vector<float> x ((size_t) (sr * seconds), 0.0f);
    juce::Random rng (7);
    const double beat = 60.0 / bpm;
    for (double t = offset; t < seconds; t += beat)
    {
        const auto start = (size_t) (t * sr);
        for (size_t i = 0; i < (size_t) (0.12 * sr) && start + i < x.size(); ++i)
        {
            const double tt = i / sr;
            x[start + i] += (float) (0.9 * std::exp (-tt * 30.0) * std::sin (2 * pi * (55.0 + 80.0 * std::exp (-tt * 40.0)) * tt));
        }
        const auto hat = (size_t) ((t + beat * 0.5) * sr);
        for (size_t i = 0; i < (size_t) (0.02 * sr) && hat + i < x.size(); ++i)
            x[hat + i] += 0.08f * (rng.nextFloat() * 2.0f - 1.0f);
    }
    return x;
}

static void testBeatDetection()
{
    for (double bpm : { 92.0, 124.0, 128.0, 140.0, 174.0 })
    {
        const double sr = 44100, offset = 0.37;
        auto x = clickTrack (bpm, sr, 40.0, offset);
        auto info = detectBeats (x.data(), (int64_t) x.size(), sr);
        std::printf ("  tempo %.1f -> detected %.2f, first beat %.3f (phase err %.3f beats)\n", bpm, info.bpm, info.firstBeat,
                     std::abs (phaseCorrection (info.firstBeat, offset, bpm, 0.0)) / beatLength (bpm));
        CHECK (std::abs (info.bpm - bpm) < 0.5);
        // Grid lines up with the real beats to within 3 % of a beat.
        CHECK (std::abs (phaseCorrection (info.firstBeat, offset, bpm, 0.0)) / beatLength (bpm) < 0.03);
    }
    std::vector<float> silence (44100 * 10, 0.0f);
    CHECK (detectBeats (silence.data(), (int64_t) silence.size(), 44100).bpm == 0.0);

    CHECK (near (beatPhase (0.75, 0.25, 120.0), 0.0));
    CHECK (near (beatPhase (1.0, 0.25, 120.0), 0.5));
    CHECK (near (phaseCorrection (1.0, 0.0, 120.0, 0.1), 0.05));   // 0.1 beat ahead of phase 0 => +0.05 s
    CHECK (near (nearestBeat (1.12, 0.0, 120.0), 1.0));
    CHECK (near (hermite (0, 1, 2, 3, 0.5f), 1.5));
}

static std::shared_ptr<TrackData> makeTrack (double bpm, double seconds)
{
    auto t = std::make_shared<TrackData>();
    t->sampleRate = 44100;
    auto x = clickTrack (bpm, t->sampleRate, seconds, 0.0);
    t->audio.setSize (2, (int) x.size());
    for (int c = 0; c < 2; ++c) t->audio.copyFrom (c, 0, x.data(), (int) x.size());
    t->beats.bpm = bpm;
    t->beats.firstBeat = 0.0;
    t->title = "test";
    return t;
}

static void runFor (TrackPlayer& p, double seconds, float& tempo, float range = 0.08f, const TrackPlayer* master = nullptr)
{
    juce::AudioBuffer<float> buf (2, 512);
    for (int i = 0; i < (int) (seconds * 44100 / 512); ++i) { buf.clear(); p.render (buf, 512, tempo, range, master); }
}

static void testPlayer()
{
    using C = TrackPlayer::Cmd;
    TrackPlayer p;
    p.prepare (44100);
    p.setTrack (makeTrack (120.0, 30.0));
    float tempo = 0.5f;
    CHECK (p.loaded && ! p.playing && near (p.position, 0.0));

    p.push ({ C::PlayToggle });
    runFor (p, 2.0, tempo);
    CHECK (p.playing && std::abs (p.position - 2.0) < 0.05);

    // Tempo +8 % (fader at top of an 8 % range) plays faster.
    tempo = 1.0f;
    const double before = p.position;
    runFor (p, 1.0, tempo);
    CHECK (std::abs ((p.position - before) - 1.08) < 0.03);
    CHECK (std::abs (p.effectiveBpm - 129.6) < 0.01);
    tempo = 0.5f;

    // 4-beat loop (2 s at 120 BPM) keeps the playhead inside it.
    p.push ({ C::LoopBeats, 4.0 });
    runFor (p, 0.05, tempo);
    CHECK (p.looping && near (p.loopEnd - p.loopStart, 2.0));
    runFor (p, 5.0, tempo);
    CHECK (p.position >= p.loopStart - 0.01 && p.position <= p.loopEnd + 0.01);
    p.push ({ C::LoopOff });

    // Hot cue: first press stores, later press jumps.
    p.push ({ C::HotcueDown, 0, 2 });
    runFor (p, 0.02, tempo);
    const double hc = p.hotcues[2];
    CHECK (hc >= 0);
    runFor (p, 1.0, tempo);
    p.push ({ C::HotcueDown, 0, 2 });
    runFor (p, 0.02, tempo);
    CHECK (std::abs (p.position - hc) < 0.05);
    p.push ({ C::HotcueDelete, 0, 2 });
    runFor (p, 0.02, tempo);
    CHECK (p.hotcues[2] < 0);

    // CDJ cue: while playing, CUE returns to the cue point and stops.
    p.push ({ C::CueDown }); p.push ({ C::CueUp });
    runFor (p, 0.1, tempo);
    CHECK (! p.playing && std::abs (p.position - p.cuePoint) < 0.01);
    // While stopped, CUE sets the cue and previews only while held.
    p.push ({ C::Seek, 0.5 });
    runFor (p, 0.02, tempo);
    p.push ({ C::CueDown });
    runFor (p, 0.5, tempo);
    CHECK (p.playing && std::abs (p.cuePoint - 15.0) < 0.05);
    p.push ({ C::CueUp });
    runFor (p, 0.05, tempo);
    CHECK (! p.playing && std::abs (p.position - p.cuePoint) < 0.01);

    // Sync: a 125 BPM deck follows a 120 BPM master and moves its tempo fader.
    TrackPlayer q;
    q.prepare (44100);
    q.setTrack (makeTrack (125.0, 30.0));
    float qTempo = 0.5f;
    p.push ({ C::PlayToggle });
    runFor (p, 0.3, tempo);
    q.push ({ C::PlayToggle });
    q.push ({ C::SyncToggle });
    juce::AudioBuffer<float> buf (2, 512);
    for (int i = 0; i < 200; ++i) { buf.clear(); p.render (buf, 512, tempo, 0.08f, nullptr); q.render (buf, 512, qTempo, 0.08f, &p); }
    CHECK (q.synced && std::abs (q.effectiveBpm - 120.0) < 0.01);
    CHECK (qTempo < 0.5f);
    // Beats line up after sync.
    const double phaseP = beatPhase (p.position, 0.0, 120.0), phaseQ = beatPhase (q.position, 0.0, 125.0);
    CHECK (std::abs (phaseP - phaseQ) < 0.05 || std::abs (phaseP - phaseQ) > 0.95);

    // Playing to the end stops.
    p.push ({ C::Seek, 0.999 });
    runFor (p, 0.5, tempo);
    CHECK (! p.playing);
}

static void testShiftBindings()
{
    MappingProfile p;
    p.controls.push_back ({ "k", "Knob", { MsgType::CC, 1, 5 } });
    Binding a; a.control = "k"; a.target = "deck.A.filter";
    Binding b; b.control = "k"; b.target = "deck.A.macro.1"; b.shift = true;
    p.bindings = { a, b };
    CHECK (p.activeBindingsFor ("k", false).size() == 1 && p.activeBindingsFor ("k", false)[0]->target == "deck.A.filter");
    CHECK (p.activeBindingsFor ("k", true).size() == 1 && p.activeBindingsFor ("k", true)[0]->target == "deck.A.macro.1");
    auto back = MappingProfile::fromJson (p.toJson());
    CHECK (back && back->bindings[1].shift && ! back->bindings[0].shift);
    p.bindings = { a };
    CHECK (p.activeBindingsFor ("k", true).size() == 1);  // no shift binding: falls back
}

int main()
{
    testBeatDetection();
    testPlayer();
    testShiftBindings();
    testDecode();
    testRelative();
    testProfile();
    testFactoryProfile();
    testMixerMaths();
    testTargets();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
