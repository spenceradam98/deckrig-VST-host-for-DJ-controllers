#include "HostEngine.h"

namespace deckrig
{
using namespace juce;

HostEngine::HostEngine()
{
    formats.registerBasicFormats();
    loadTrackCache();
    for (int d = 0; d < numDecks; ++d)
    {
        decks[(size_t) d] = std::make_unique<Deck> (d, library);
        decks[(size_t) d]->params.inputPair = -1;  // decks start silent until an input or instrument is chosen
    }
}

HostEngine::~HostEngine()
{
    shutdown();
    loader.removeAllJobs (true, 10000);
    for (int d = 0; d < numDecks; ++d) rememberTrack (d);
    saveTrackCache();
}

void HostEngine::start (const XmlElement* savedDeviceState)
{
    // Ask for 4 in / 4 out so a DJ interface's second stereo pair (headphones) is available.
    auto err = deviceManager.initialise (4, 4, savedDeviceState, true);
    if (err.isNotEmpty())
        DBG ("Audio device: " << err);
    deviceManager.addAudioCallback (this);
    deviceManager.addMidiInputDeviceCallback ({}, this);
    enableNewMidiInputs();
    refreshMidiOutput();
    startTimerHz (10);
}

void HostEngine::shutdown()
{
    stopTimer();
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    ledOut.reset();
}

//==============================================================================
void HostEngine::audioDeviceAboutToStart (AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    maxBlock = jmax (16, device->getCurrentBufferSizeSamples());
    mixBuffer.setSize (2, maxBlock);
    cueBuffer.setSize (2, maxBlock);
    for (auto& d : decks)
        d->prepare (sampleRate, maxBlock);
    for (int d = 0; d < numDecks; ++d)
    {
        deckGain[(size_t) d].reset (sampleRate, 0.015);
        deckCue[(size_t) d].reset (sampleRate, 0.015);
    }
    masterGain.reset (sampleRate, 0.02);
}

void HostEngine::audioDeviceStopped()
{
    for (auto& d : decks)
        d->release();
}

void HostEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                                   int numSamples, const AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr)
            FloatVectorOperations::clear (outputs[c], numSamples);

    const auto xf = crossfade (mixer.crossfader.load(), (XfadeCurve) mixer.curve.load());
    const float xfGain[numDecks] = { xf.a, xf.b };
    masterGain.setTargetValue (faderToGain (mixer.master.load()) * 1.0f);

    float peakL = 0, peakR = 0;
    std::array<float, numDecks> dPeakL {}, dPeakR {};
    const float* chunkIn[16] {};
    const int nIn = jmin (numInputs, 16);

    for (int pos = 0; pos < numSamples; pos += maxBlock)
    {
        const int n = jmin (maxBlock, numSamples - pos);
        for (int c = 0; c < nIn; ++c)
            chunkIn[c] = inputs[c] != nullptr ? inputs[c] + pos : nullptr;

        mixBuffer.clear (0, n);
        cueBuffer.clear (0, n);

        for (int d = 0; d < numDecks; ++d)
        {
            auto& deck = *decks[(size_t) d];
            deck.process (chunkIn, nIn, n, decks[(size_t) (1 - d)].get());
            auto& buf = deck.getBuffer();
            auto& g = deckGain[(size_t) d];
            auto& cg = deckCue[(size_t) d];
            g.setTargetValue (faderToGain (deck.params.volume.load()) * xfGain[d]);
            cg.setTargetValue (deck.params.pfl.load() ? 1.0f : 0.0f);
            const float* L = buf.getReadPointer (0);
            const float* R = buf.getReadPointer (1);
            float* mL = mixBuffer.getWritePointer (0); float* mR = mixBuffer.getWritePointer (1);
            float* cL = cueBuffer.getWritePointer (0); float* cR = cueBuffer.getWritePointer (1);
            const float fader = faderToGain (deck.params.volume.load());
            for (int i = 0; i < n; ++i)
            {
                const float gv = g.getNextValue(), cv = cg.getNextValue();
                mL[i] += L[i] * gv; mR[i] += R[i] * gv;
                cL[i] += L[i] * cv; cR[i] += R[i] * cv;
                dPeakL[(size_t) d] = jmax (dPeakL[(size_t) d], std::abs (L[i] * fader));
                dPeakR[(size_t) d] = jmax (dPeakR[(size_t) d], std::abs (R[i] * fader));
            }
        }

        // Master to outputs 1/2.
        float* mL = mixBuffer.getWritePointer (0); float* mR = mixBuffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float g = masterGain.getNextValue();
            mL[i] *= g; mR[i] *= g;
            peakL = jmax (peakL, std::abs (mL[i]));
            peakR = jmax (peakR, std::abs (mR[i]));
        }
        if (numOutputs > 0 && outputs[0] != nullptr) FloatVectorOperations::copy (outputs[0] + pos, mL, n);
        if (numOutputs > 1 && outputs[1] != nullptr) FloatVectorOperations::copy (outputs[1] + pos, mR, n);

        // Headphones to outputs 3/4 when the interface has them: cue blended with master.
        if (numOutputs > 3 && outputs[2] != nullptr && outputs[3] != nullptr)
        {
            const float mixAmt = mixer.cueMix.load(), lvl = faderToGain (mixer.cueLevel.load());
            for (int i = 0; i < n; ++i)
            {
                outputs[2][pos + i] = lvl * ((1.0f - mixAmt) * cueBuffer.getSample (0, i) + mixAmt * mL[i]);
                outputs[3][pos + i] = lvl * ((1.0f - mixAmt) * cueBuffer.getSample (1, i) + mixAmt * mR[i]);
            }
        }
    }

    auto decay = [] (std::atomic<float>& m, float v) { m = jmax (v, m.load() * 0.92f); };
    decay (mixer.meterL, peakL);
    decay (mixer.meterR, peakR);
    for (int d = 0; d < numDecks; ++d)
    {
        decay (decks[(size_t) d]->meterL, dPeakL[(size_t) d]);
        decay (decks[(size_t) d]->meterR, dPeakR[(size_t) d]);
    }
}

//==============================================================================
bool HostEngine::parseDeckTarget (const String& t, int& deck, String& rest)
{
    if (! t.startsWith ("deck.") || t.length() < 8)
        return false;
    const auto letter = t[5];
    deck = letter == 'A' ? 0 : letter == 'B' ? 1 : -1;
    rest = t.substring (7);
    return deck >= 0;
}

std::atomic<float>* HostEngine::findFloat (const String& t) const
{
    auto& m = const_cast<MixerParams&> (mixer);
    if (t == "mixer.crossfader") return &m.crossfader;
    if (t == "mixer.master") return &m.master;
    if (t == "mixer.cueMix") return &m.cueMix;
    if (t == "mixer.cueLevel") return &m.cueLevel;
    int d; String r;
    if (! parseDeckTarget (t, d, r)) return nullptr;
    auto& p = decks[(size_t) d]->params;
    if (r == "volume") return &p.volume;
    if (r == "gain") return &p.gain;
    if (r == "eqHigh") return &p.eqHigh;
    if (r == "eqMid") return &p.eqMid;
    if (r == "eqLow") return &p.eqLow;
    if (r == "filter") return &p.filter;
    if (r == "tempo") return &p.tempo;
    return nullptr;
}

std::atomic<bool>* HostEngine::findBool (const String& t) const
{
    int d; String r;
    if (! parseDeckTarget (t, d, r)) return nullptr;
    auto& p = decks[(size_t) d]->params;
    if (r == "pfl") return &p.pfl;
    if (r == "chainBypass") return &p.chainBypass;
    return nullptr;
}

float HostEngine::getValue (const String& t) const
{
    if (auto* f = findFloat (t)) return f->load();
    int d; String r;
    if (parseDeckTarget (t, d, r) && r.startsWith ("macro."))
        return decks[(size_t) d]->getMacroValue (r.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1);
    return 0.0f;
}

void HostEngine::setValue (const String& t, float v)
{
    v = jlimit (0.0f, 1.0f, v);
    if (auto* f = findFloat (t))
    {
        f->store (v);
        int dk; String rr;
        if (parseDeckTarget (t, dk, rr) && rr == "tempo")
            decks[(size_t) dk]->player.push ({ TrackPlayer::Cmd::SyncOff });
        return;
    }
    int d; String r;
    if (parseDeckTarget (t, d, r) && r.startsWith ("macro."))
    {
        const int m = r.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        decks[(size_t) d]->setMacroValue (m, v);
        decks[(size_t) d]->selectedMacro = m;
    }
}

bool HostEngine::getToggle (const String& t) const
{
    if (auto* b = findBool (t)) return b->load();
    int d; String r;
    if (parseDeckTarget (t, d, r) && r.startsWith ("slot.") && r.endsWith (".bypass"))
    {
        const int s = r.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        auto& chain = decks[(size_t) d]->chain;
        return isPositiveAndBelow (s, chain.size()) && ! chain.isBypassed (s);  // LED on = plugin active
    }
    return false;
}

void HostEngine::setToggle (const String& t, bool on)
{
    if (auto* b = findBool (t)) { b->store (on); return; }
    int d; String r;
    if (parseDeckTarget (t, d, r) && r.startsWith ("slot.") && r.endsWith (".bypass"))
        decks[(size_t) d]->chain.setBypassed (r.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1, ! on);
}

void HostEngine::trigger (const String& t)
{
    if (t == "library.load") return;
    int d; String r;
    if (! parseDeckTarget (t, d, r)) return;
    auto& deck = *decks[(size_t) d];
    using C = TrackPlayer::Cmd;
    auto& pl = deck.player;
    if (r == "macroNext") deck.selectedMacro = (deck.selectedMacro + 1) % numMacros;
    else if (r == "macroPrev") deck.selectedMacro = (deck.selectedMacro + numMacros - 1) % numMacros;
    else if (r == "play") pl.push ({ C::PlayToggle });
    else if (r == "sync") pl.push ({ C::SyncToggle });
    else if (r == "loopHalve") pl.push ({ C::LoopHalve });
    else if (r == "loopDouble") pl.push ({ C::LoopDouble });
    else if (r == "reloop") pl.push ({ C::Reloop });
    else if (r == "beatjumpBack") pl.push ({ C::BeatJump, -4.0 });
    else if (r == "beatjumpFwd") pl.push ({ C::BeatJump, 4.0 });
    else if (r == "padMode.hotcue") deck.params.padMode = padHotcue;
    else if (r == "padMode.loop") deck.params.padMode = padLoop;
    else if (r == "padMode.fx") deck.params.padMode = padFx;
    else if (r == "padMode.instrument") deck.params.padMode = padInstrument;
    else if (r == "load")
        MessageManager::callAsync ([this, d] { if (onLoadSelected) onLoadSelected (d); });
}

void HostEngine::momentary (const String& t, bool down)
{
    int d; String r;
    if (! parseDeckTarget (t, d, r)) return;
    auto& pl = decks[(size_t) d]->player;
    using C = TrackPlayer::Cmd;
    if (r == "cue") pl.push ({ down ? C::CueDown : C::CueUp });
    else if (r == "jogTouch") pl.push ({ C::JogTouch, down ? 1.0 : 0.0 });
    else if (r == "shift")
    {
        if (down) shiftMask |= (1 << d); else shiftMask &= ~(1 << d);
    }
    else if (r.startsWith ("pad."))
        padPress (d, r.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1, down, 100, isShiftHeld());
}

void HostEngine::padPress (int d, int pad, bool down, int velocity, bool shift)
{
    if (! isPositiveAndBelow (d, numDecks) || ! isPositiveAndBelow (pad, 8)) return;
    auto& deck = *decks[(size_t) d];
    auto& pl = deck.player;
    using C = TrackPlayer::Cmd;
    switch (deck.params.padMode.load())
    {
        case padHotcue:
            if (shift) { if (down) pl.push ({ C::HotcueDelete, 0, pad }); }
            else pl.push ({ down ? C::HotcueDown : C::HotcueUp, 0, pad });
            break;
        case padLoop:
            if (! down) break;
            if (shift)
            {
                static const double jumps[] = { -16, -4, -1, -0.25, 0.25, 1, 4, 16 };
                pl.push ({ C::BeatJump, jumps[pad] });
            }
            else if (pad < 6) pl.push ({ C::LoopBeats, loopPadBeats[pad] });
            else pl.push ({ pad == 6 ? C::LoopHalve : C::LoopDouble });
            break;
        case padFx:
            if (down)
                MessageManager::callAsync ([this, d, pad]
                {
                    const auto t = deckTarget (d, "slot." + String (pad + 1) + ".bypass");
                    setToggle (t, ! getToggle (t));
                    refreshLeds (false);
                });
            break;
        case padInstrument:
        default:
        {
            const double now = Time::getMillisecondCounterHiRes() * 0.001;
            if (down) deck.midiIn.addMessageToQueue (MidiMessage::noteOn (1, 36 + pad, (uint8) jlimit (1, 127, velocity)).withTimeStamp (now));
            else deck.midiIn.addMessageToQueue (MidiMessage::noteOff (1, 36 + pad).withTimeStamp (now));
            break;
        }
    }
}

bool HostEngine::getLed (const String& t) const
{
    int d; String r;
    if (! parseDeckTarget (t, d, r))
        return false;
    auto& deck = *decks[(size_t) d];
    auto& pl = deck.player;
    if (r == "play") return pl.playing.load();
    if (r == "cue") return pl.loaded && ! pl.playing;
    if (r == "sync") return pl.synced.load();
    if (r == "reloop") return pl.looping.load();
    if (r == "shift") return (shiftMask.load() & (1 << d)) != 0;
    if (r == "padMode.hotcue") return deck.params.padMode == padHotcue;
    if (r == "padMode.loop") return deck.params.padMode == padLoop;
    if (r == "padMode.fx") return deck.params.padMode == padFx;
    if (r == "padMode.instrument") return deck.params.padMode == padInstrument;
    if (r.startsWith ("pad."))
    {
        const int p = r.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        if (! isPositiveAndBelow (p, 8)) return false;
        switch (deck.params.padMode.load())
        {
            case padHotcue: return pl.hotcues[(size_t) p] >= 0;
            case padLoop:   return pl.looping && (p >= 6 || std::abs (pl.loopBeats - loopPadBeats[p]) < 1e-6);
            case padFx:     return getToggle (deckTarget (d, "slot." + String (p + 1) + ".bypass"));
            default:        return false;
        }
    }
    return getToggle (t);
}

//==============================================================================
MappingProfile HostEngine::getProfile() const
{
    const ScopedLock sl (profileLock);
    return profile;
}

void HostEngine::setProfile (const MappingProfile& p)
{
    {
        const ScopedLock sl (profileLock);
        profile = p;
    }
    ledSent.clear();
    refreshMidiOutput();
    refreshLeds (true);
    sendChangeMessage();
}

String HostEngine::getBindingSummary (const String& target) const
{
    const ScopedLock sl (profileLock);
    StringArray names;
    for (auto* b : profile.bindingsForTarget (target))
        if (auto* c = profile.findControl (b->control))
            names.add (c->label);
    return names.joinIntoString (", ");
}

void HostEngine::beginLearn (const String& t) { const ScopedLock sl (profileLock); learnTarget = t; }
void HostEngine::cancelLearn() { const ScopedLock sl (profileLock); learnTarget = {}; }
String HostEngine::getLearnTarget() const { const ScopedLock sl (profileLock); return learnTarget; }

//==============================================================================
void HostEngine::addMonitorLine (const String& s)
{
    const ScopedLock sl (monitorLock);
    monitor.push_back (s);
    while (monitor.size() > 300)
        monitor.pop_front();
}

StringArray HostEngine::getMonitorLines() const
{
    const ScopedLock sl (monitorLock);
    StringArray a;
    for (auto& s : monitor) a.add (s);
    return a;
}

void HostEngine::clearMonitor() { const ScopedLock sl (monitorLock); monitor.clear(); }
String HostEngine::getLastMidiDevice() const { const ScopedLock sl (monitorLock); return lastMidiDevice; }

void HostEngine::handleIncomingMidiMessage (MidiInput* source, const MidiMessage& msg)
{
    auto raw = decodeMidiBytes (msg.getRawData(), msg.getRawDataSize());
    const String dev = source != nullptr ? source->getName() : String ("?");
    {
        const ScopedLock sl (monitorLock);
        lastMidiDevice = dev;
    }
    if (! raw.has_value())
        return;

    String line = Time::getCurrentTime().formatted ("%H:%M:%S") + "  " + dev.substring (0, 24).paddedRight (' ', 24) + "  "
                + raw->address.toString().paddedRight (' ', 24) + "  value " + String (raw->value);
    if (raw->address.type == MsgType::Note) line << (raw->noteOn ? "  (on)" : "  (off)");

    if (onCapture != nullptr)
    {
        addMonitorLine (line);
        MessageManager::callAsync ([this, r = *raw] { if (onCapture) onCapture (r); });
        return;
    }

    const ScopedLock sl (profileLock);
    if (learnTarget.isNotEmpty())
    {
        auto* info = findTarget (learnTarget);
        const auto pref = info != nullptr ? info->preferredKind : ControlKind::Absolute;
        // Skip note-offs, and for jog targets skip the wheel's touch note so the spin is captured.
        const bool usable = (raw->address.type != MsgType::Note || raw->noteOn)
                         && ! (pref == ControlKind::Relative && raw->address.type == MsgType::Note);
        if (usable)
        {
            ControlKind kind = pref;
            if (raw->address.type == MsgType::Note && pref == ControlKind::Absolute) kind = ControlKind::Button;
            if (raw->address.type == MsgType::CC && pref == ControlKind::Pad) kind = ControlKind::Button;
            const auto id = profile.learn (learnTarget, raw->address, kind);
            String label = profile.findControl (id) != nullptr ? profile.findControl (id)->label : id;
            const auto t = learnTarget;
            learnTarget = {};
            line << "  -> learned " << t;
            addMonitorLine (line);
            MessageManager::callAsync ([this, t, label]
            {
                ledSent.clear();
                if (onLearned) onLearned (t, label);
                sendChangeMessage();
            });
            return;
        }
    }

    addMonitorLine (line);
    applyEvent (*raw);
}

void HostEngine::applyEvent (const RawMidi& e)
{
    auto* control = profile.findControl (e.address);
    if (control == nullptr)
        return;
    for (auto* b : profile.activeBindingsFor (control->id, isShiftHeld()))
        applyToTarget (*control, *b, e);
}

void HostEngine::applyToTarget (const ControlDef& c, const Binding& b, const RawMidi& e)
{
    const auto& t = b.target;
    int d = -1; String rest;
    const bool isDeck = parseDeckTarget (t, d, rest);
    const auto* info = findTarget (t);
    const bool pressed = e.address.type == MsgType::Note ? e.noteOn : e.value >= 64;

    // Pads keep their velocity for instruments.
    if (isDeck && rest.startsWith ("pad."))
    {
        padPress (d, rest.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1, pressed,
                  e.address.type == MsgType::Note ? e.value : 100, isShiftHeld());
        MessageManager::callAsync ([this] { refreshLeds (false); });
        return;
    }

    if (info != nullptr && info->momentary)
    {
        momentary (t, pressed);
        MessageManager::callAsync ([this] { refreshLeds (false); });
        return;
    }

    if (info != nullptr && (info->toggle || info->trigger))
    {
        if (! pressed) return;
        if (auto* flag = findBool (t))
            flag->store (! flag->load());
        else if (info->toggle)
            MessageManager::callAsync ([this, t] { setToggle (t, ! getToggle (t)); });
        else
            trigger (t);
        MessageManager::callAsync ([this] { refreshLeds (false); });
        return;
    }

    // Continuous targets.
    float abs01 = -1.0f, rel = 0.0f;
    if (c.kind == ControlKind::Relative)
        rel = (float) decodeRelative (e.value, c.relative) * b.sensitivity * (b.invert ? -1.0f : 1.0f);
    else if (e.address.type == MsgType::PitchBend)
        abs01 = (float) e.value / 16383.0f;
    else if (e.address.type == MsgType::Note)
        abs01 = e.noteOn ? 1.0f : 0.0f;
    else
        abs01 = (float) e.value / 127.0f;
    if (abs01 >= 0.0f && b.invert) abs01 = 1.0f - abs01;

    if (t == "library.scroll")
    {
        const int size = librarySize.load();
        if (size > 0)
            librarySelection = jlimit (0, size - 1, librarySelection.load() + (int) std::round (rel != 0 ? rel : (abs01 >= 0.5f ? 1 : -1)));
        return;
    }

    if (auto* f = findFloat (t))
    {
        if (abs01 >= 0.0f)
        {
            // Soft takeover: a fader only grabs the value once it reaches or crosses it,
            // so moving it after a sync, a preset or an on-screen change doesn't jump.
            const float cur = f->load();
            auto it = lastHardware.find (c.id + t);
            const float last = it != lastHardware.end() ? it->second : -1.0f;
            lastHardware[c.id + t] = abs01;
            const bool crossed = last >= 0.0f && ((last <= cur && abs01 >= cur) || (last >= cur && abs01 <= cur));
            if (softTakeover && c.address.type != MsgType::Note && std::abs (abs01 - cur) > 0.04f && ! crossed)
                return;
            f->store (abs01);
        }
        else
            f->store (jlimit (0.0f, 1.0f, f->load() + rel / 128.0f));
        if (isDeck && rest == "tempo")
            decks[(size_t) d]->player.push ({ TrackPlayer::Cmd::SyncOff });
        return;
    }

    if (isDeck && rest == "jog" && decks[(size_t) d]->player.loaded)
    {
        decks[(size_t) d]->player.push ({ TrackPlayer::Cmd::Jog, (double) rel });
        return;
    }

    if (isDeck && (rest == "jog" || rest == "macroJog" || rest.startsWith ("macro.")))
    {
        const int m = rest.startsWith ("macro.") ? rest.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1 : -1;
        const float delta = rel / 128.0f;
        MessageManager::callAsync ([this, d, m, abs01, delta]
        {
            auto& deck = *decks[(size_t) d];
            const int macro = m >= 0 ? m : deck.selectedMacro.load();
            if (m >= 0) deck.selectedMacro = m;
            deck.setMacroValue (macro, abs01 >= 0.0f ? abs01 : deck.getMacroValue (macro) + delta);
        });
    }
}

//==============================================================================
void HostEngine::timerCallback()
{
    static int tick = 0;
    if (++tick % 20 == 0)  // every 2 s: pick up hot-plugged controllers
    {
        enableNewMidiInputs();
        refreshMidiOutput();
    }
    refreshLeds (false);
    for (int d = 0; d < numDecks; ++d)
        if (decks[(size_t) d]->player.changeCount.load() != savedChange[(size_t) d])
            rememberTrack (d);
    if (trackCacheDirty && tick % 50 == 0)
        saveTrackCache();
}

//==============================================================================
static File trackCacheFile()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile (DECKRIG_BRAND_NAME).getChildFile (DECKRIG_PRODUCT_NAME).getChildFile ("Tracks.json");
}

void HostEngine::loadTrackCache()
{
    trackCache = JSON::parse (trackCacheFile());
    if (! trackCache.isObject()) trackCache = var (new DynamicObject());
}

void HostEngine::saveTrackCache()
{
    if (! trackCacheDirty) return;
    auto f = trackCacheFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (JSON::toString (trackCache, true));
    trackCacheDirty = false;
}

void HostEngine::rememberTrack (int d)
{
    auto& pl = decks[(size_t) d]->player;
    savedChange[(size_t) d] = pl.changeCount.load();
    auto t = pl.getTrack();
    if (t == nullptr) return;
    auto* o = new DynamicObject();
    o->setProperty ("cue", pl.cuePoint.load());
    o->setProperty ("bpm", pl.bpm.load());
    Array<var> hc;
    for (auto& h : pl.hotcues) hc.add (h.load());
    o->setProperty ("hotcues", hc);
    if (auto* obj = trackCache.getDynamicObject())
        obj->setProperty (t->file.getFullPathName(), var (o));
    trackCacheDirty = true;
}

void HostEngine::loadTrack (int d, const File& file)
{
    if (! isPositiveAndBelow (d, numDecks)) return;
    rememberTrack (d);
    const int gen = ++loadGeneration[(size_t) d];
    loading[(size_t) d] = true;
    loader.addJob ([this, d, file, gen]
    {
        String err;
        auto t = deckrig::loadTrack (formats, file, err);
        MessageManager::callAsync ([this, d, t, err, gen, file]
        {
            if (gen != loadGeneration[(size_t) d]) return;  // a newer load replaced this one
            loading[(size_t) d] = false;
            if (t != nullptr)
            {
                auto saved = trackCache.getProperty (file.getFullPathName(), {});
                std::array<double, numHotcues> hc;
                hc.fill (-1.0);
                if (auto* arr = saved.getProperty ("hotcues", {}).getArray())
                    for (int i = 0; i < jmin (numHotcues, arr->size()); ++i)
                        hc[(size_t) i] = (double) (*arr)[i];
                const double cue = saved.isObject() ? (double) saved.getProperty ("cue", -1.0) : -1.0;
                if (saved.isObject() && (double) saved.getProperty ("bpm", 0.0) > 0)
                    t->beats.bpm = (double) saved.getProperty ("bpm", 0.0);
                decks[(size_t) d]->player.setTrack (t, cue, &hc);
                savedChange[(size_t) d] = decks[(size_t) d]->player.changeCount.load();
            }
            if (onTrackLoaded) onTrackLoaded (d, err);
            sendChangeMessage();
        });
    });
}

void HostEngine::enableNewMidiInputs()
{
    auto inputs = MidiInput::getAvailableDevices();
    for (auto& in : inputs)
        if (! knownInputs.contains (in))
            deviceManager.setMidiInputDeviceEnabled (in.identifier, true);
    knownInputs = inputs;
}

void HostEngine::refreshMidiOutput()
{
    String match;
    {
        const ScopedLock sl (profileLock);
        match = profile.deviceMatch;
    }
    if (match.isEmpty())
        match = getLastMidiDevice();
    if (match.isEmpty())
        return;

    for (auto& out : MidiOutput::getAvailableDevices())
    {
        if (out.name.containsIgnoreCase (match) || match.containsIgnoreCase (out.name))
        {
            if (ledOut != nullptr && ledOutId == out.identifier)
                return;
            ledOut = MidiOutput::openDevice (out.identifier);
            ledOutId = ledOut != nullptr ? out.identifier : String();
            ledSent.clear();
            return;
        }
    }
}

void HostEngine::refreshLeds (bool force)
{
    if (ledOut == nullptr)
        return;
    std::vector<std::pair<ControlDef, bool>> changes;
    {
        const ScopedLock sl (profileLock);
        for (auto& c : profile.controls)
        {
            if (! c.led) continue;
            bool on = false, any = false;
            for (auto* b : profile.activeBindingsFor (c.id, false))
                if (auto* info = findTarget (b->target); info != nullptr && info->led)
                {
                    any = true;
                    on = on || getLed (b->target);
                }
            if (! any) continue;
            auto it = ledSent.find (c.id);
            if (force || it == ledSent.end() || it->second != on)
            {
                ledSent[c.id] = on;
                changes.push_back ({ c, on });
            }
        }
    }
    for (auto& [c, on] : changes)
    {
        const auto vel = (uint8) (on ? 127 : 0);
        if (c.address.type == MsgType::CC)
            ledOut->sendMessageNow (MidiMessage::controllerEvent (c.address.channel, c.address.number, vel));
        else
            ledOut->sendMessageNow (MidiMessage::noteOn (c.address.channel, c.address.number, vel));
    }
}

//==============================================================================
std::unique_ptr<XmlElement> HostEngine::createSessionXml() const
{
    auto xml = std::make_unique<XmlElement> ("DECKRIG_SESSION");
    auto* mx = xml->createNewChildElement ("MIXER");
    mx->setAttribute ("crossfader", mixer.crossfader.load());
    mx->setAttribute ("master", mixer.master.load());
    mx->setAttribute ("cueMix", mixer.cueMix.load());
    mx->setAttribute ("cueLevel", mixer.cueLevel.load());
    mx->setAttribute ("curve", mixer.curve.load());
    for (auto& d : decks)
    {
        auto* e = xml->createNewChildElement ("DECK");
        auto& p = d->params;
        e->setAttribute ("index", d->index);
        e->setAttribute ("gain", p.gain.load());
        e->setAttribute ("eqHigh", p.eqHigh.load());
        e->setAttribute ("eqMid", p.eqMid.load());
        e->setAttribute ("eqLow", p.eqLow.load());
        e->setAttribute ("filter", p.filter.load());
        e->setAttribute ("volume", p.volume.load());
        e->setAttribute ("pfl", p.pfl.load());
        e->setAttribute ("chainBypass", p.chainBypass.load());
        e->setAttribute ("inputPair", p.inputPair.load());
        e->setAttribute ("tempo", p.tempo.load());
        e->setAttribute ("tempoRange", p.tempoRange.load());
        e->setAttribute ("padMode", p.padMode.load());
        if (auto t = d->player.getTrack())
            e->setAttribute ("track", t->file.getFullPathName());
        e->addChildElement (d->chain.toXml().release());
        for (int m = 0; m < numMacros; ++m)
        {
            auto& a = d->macros[(size_t) m];
            if (! a.isAssigned()) continue;
            auto* me = e->createNewChildElement ("MACRO");
            me->setAttribute ("index", m);
            me->setAttribute ("slot", a.slot);
            me->setAttribute ("param", a.param);
        }
    }
    return xml;
}

void HostEngine::restoreSessionXml (const XmlElement& xml)
{
    if (auto* mx = xml.getChildByName ("MIXER"))
    {
        mixer.crossfader = (float) mx->getDoubleAttribute ("crossfader", 0.5);
        mixer.master = (float) mx->getDoubleAttribute ("master", 0.8);
        mixer.cueMix = (float) mx->getDoubleAttribute ("cueMix", 0.0);
        mixer.cueLevel = (float) mx->getDoubleAttribute ("cueLevel", 0.8);
        mixer.curve = mx->getIntAttribute ("curve", 0);
    }
    for (auto* e : xml.getChildWithTagNameIterator ("DECK"))
    {
        const int i = e->getIntAttribute ("index", -1);
        if (! isPositiveAndBelow (i, numDecks)) continue;
        auto& d = *decks[(size_t) i];
        auto& p = d.params;
        p.gain = (float) e->getDoubleAttribute ("gain", 0.5);
        p.eqHigh = (float) e->getDoubleAttribute ("eqHigh", 0.5);
        p.eqMid = (float) e->getDoubleAttribute ("eqMid", 0.5);
        p.eqLow = (float) e->getDoubleAttribute ("eqLow", 0.5);
        p.filter = (float) e->getDoubleAttribute ("filter", 0.5);
        p.volume = (float) e->getDoubleAttribute ("volume", 0.8);
        p.pfl = e->getBoolAttribute ("pfl");
        p.chainBypass = e->getBoolAttribute ("chainBypass");
        p.inputPair = e->getIntAttribute ("inputPair", -1);
        p.tempo = (float) e->getDoubleAttribute ("tempo", 0.5);
        p.tempoRange = e->getIntAttribute ("tempoRange", 0);
        p.padMode = e->getIntAttribute ("padMode", 0);
        if (File track (e->getStringAttribute ("track")); e->hasAttribute ("track") && track.existsAsFile())
            loadTrack (i, track);
        if (auto* c = e->getChildByName ("CHAIN"))
            d.chain.fromXml (*c);
        for (auto* me : e->getChildWithTagNameIterator ("MACRO"))
            d.assignMacro (me->getIntAttribute ("index"), me->getIntAttribute ("slot", -1), me->getIntAttribute ("param", -1));
    }
    sendChangeMessage();
}
} // namespace deckrig
