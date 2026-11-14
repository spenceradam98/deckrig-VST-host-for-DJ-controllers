#include "TrackPlayer.h"

namespace deckrig
{
using namespace juce;

namespace
{
constexpr double jogSecondsPerTick = 1.8 / 256.0;  // one platter turn (33 rpm) ~ 256 ticks
}

std::shared_ptr<TrackData> loadTrack (AudioFormatManager& fm, const File& file, String& error)
{
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Can't read " + file.getFileName() + " (unsupported format or damaged file)";
        return nullptr;
    }
    const double sr = reader->sampleRate;
    const int64 maxLen = (int64) (sr * 60.0 * 30.0);  // 30 minutes
    if (reader->lengthInSamples <= 0 || sr <= 0)
    {
        error = file.getFileName() + " is empty";
        return nullptr;
    }
    if (reader->lengthInSamples > maxLen)
    {
        error = file.getFileName() + " is longer than 30 minutes";
        return nullptr;
    }

    auto t = std::make_shared<TrackData>();
    t->file = file;
    t->title = file.getFileNameWithoutExtension();
    t->sampleRate = sr;
    const int len = (int) reader->lengthInSamples;
    t->audio.setSize (2, len);
    reader->read (&t->audio, 0, len, 0, true, true);
    if (reader->numChannels == 1)
        t->audio.copyFrom (1, 0, t->audio, 0, 0, len);

    std::vector<float> mono ((size_t) len);
    auto* l = t->audio.getReadPointer (0);
    auto* r = t->audio.getReadPointer (1);
    for (int i = 0; i < len; ++i)
        mono[(size_t) i] = 0.5f * (l[i] + r[i]);
    t->beats = detectBeats (mono.data(), len, sr);
    t->wave = computeWaveform (l, r, len, sr);
    return t;
}

TrackPlayer::TrackPlayer()
{
    for (auto& h : hotcues) h = -1.0;
}

void TrackPlayer::push (Command c)
{
    const SpinLock::ScopedLockType sl (pushLock);
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) queue[(size_t) s1] = c;
    else if (n2 > 0) queue[(size_t) s2] = c;
    fifo.finishedWrite (n1 + n2);
}

void TrackPlayer::setTrack (std::shared_ptr<TrackData> t, double cue, const std::array<double, numHotcues>* cues)
{
    {
        const SpinLock::ScopedLockType sl (trackLock);
        audioTrack = t.get();
        pos = 0; speed = 0; bend = 0; scratchTicks = 0; scratchSpeed = 0;
        cuePreview = false; hotcuePreview = -1; touch = false;
        playing = false; synced = false; looping = false; scratching = false;
        position = 0;
        loaded = t != nullptr;
        bpm = t != nullptr ? t->beats.bpm : 0.0;
        firstBeat = t != nullptr ? t->beats.firstBeat : 0.0;
        cuePoint = cue >= 0 ? cue : (t != nullptr ? t->beats.firstBeat : 0.0);
        for (int i = 0; i < numHotcues; ++i)
            hotcues[(size_t) i] = cues != nullptr ? (*cues)[(size_t) i] : -1.0;
        if (t != nullptr) pos = cuePoint * t->sampleRate;
        position = cuePoint.load();
    }
    auto old = std::move (track);  // freed here, on the message thread, after the swap
    track = std::move (t);
    ++changeCount;
}

void TrackPlayer::prepare (double sampleRate)
{
    outRate = sampleRate;
}

float TrackPlayer::sampleAt (int ch, double p) const noexcept
{
    const int len = audioTrack->audio.getNumSamples();
    const auto* d = audioTrack->audio.getReadPointer (ch);
    const int i = (int) std::floor (p);
    const float x = (float) (p - i);
    auto at = [&] (int k) { return (k >= 0 && k < len) ? d[k] : 0.0f; };
    return hermite (at (i - 1), at (i), at (i + 1), at (i + 2), x);
}

void TrackPlayer::apply (const Command& c, const TrackPlayer* master)
{
    if (audioTrack == nullptr) return;
    const double sr = audioTrack->sampleRate;
    const double len = audioTrack->audio.getNumSamples();
    const double posS = pos / sr;
    const double b = bpm.load() > 0 ? bpm.load() : 120.0;
    const double beat = 60.0 / b;

    switch (c.type)
    {
        case Cmd::PlayToggle:
            if (cuePreview || hotcuePreview >= 0) { cuePreview = false; hotcuePreview = -1; playing = true; }
            else playing = ! playing.load();
            break;

        case Cmd::CueDown:
            if (playing && ! cuePreview)
            {
                playing = false;
                pos = cuePoint * sr;
            }
            else
            {
                // Stopped: set the cue here (on the beat if close) and preview while held.
                double cue = posS;
                if (bpm > 0 && std::abs (nearestBeat (posS, firstBeat, bpm) - posS) < 0.05) cue = nearestBeat (posS, firstBeat, bpm);
                cuePoint = jmax (0.0, cue);
                pos = cuePoint * sr;
                cuePreview = true;
                playing = true;
                ++changeCount;
            }
            break;

        case Cmd::CueUp:
            if (cuePreview)
            {
                cuePreview = false;
                playing = false;
                pos = cuePoint * sr;
            }
            break;

        case Cmd::SyncToggle:
            if (synced) { synced = false; break; }
            if (master == nullptr || ! master->loaded || master->bpm <= 0 || bpm <= 0) break;
            synced = true;
            if (master->playing)
            {
                const double target = beatPhase (master->position, master->firstBeat, master->bpm);
                double np = pos + phaseCorrection (posS, firstBeat, bpm, target) * sr;
                if (np < 0) np += beatLength (bpm) * sr;
                pos = jlimit (0.0, len - 1, np);
            }
            break;

        case Cmd::SyncOff:
            synced = false;
            break;

        case Cmd::HotcueDown:
        {
            if (! isPositiveAndBelow (c.index, numHotcues)) break;
            auto& h = hotcues[(size_t) c.index];
            if (h < 0)
            {
                double at = posS;
                if (bpm > 0 && std::abs (nearestBeat (posS, firstBeat, bpm) - posS) < 0.05) at = nearestBeat (posS, firstBeat, bpm);
                h = jmax (0.0, at);
                ++changeCount;
            }
            else
            {
                pos = h * sr;
                if (! playing) { playing = true; hotcuePreview = c.index; }
            }
            break;
        }

        case Cmd::HotcueUp:
            if (hotcuePreview == c.index)
            {
                hotcuePreview = -1;
                playing = false;
                pos = hotcues[(size_t) c.index] * sr;
            }
            break;

        case Cmd::HotcueDelete:
            if (isPositiveAndBelow (c.index, numHotcues)) { hotcues[(size_t) c.index] = -1.0; ++changeCount; }
            break;

        case Cmd::LoopBeats:
        {
            if (looping && std::abs (loopBeats - c.value) < 1e-6) { looping = false; break; }
            const double start = bpm > 0 ? nearestBeat (posS, firstBeat, bpm) : posS;
            loopStart = jmax (0.0, start > posS + 0.02 ? start - beat : start);
            loopEnd = loopStart + c.value * beat;
            loopBeats = c.value;
            looping = true;
            break;
        }

        case Cmd::LoopOff:
            looping = false;
            break;

        case Cmd::LoopHalve:
        case Cmd::LoopDouble:
            if (loopEnd > loopStart)
            {
                const double beats = jlimit (1.0 / 32.0, 64.0, loopBeats * (c.type == Cmd::LoopHalve ? 0.5 : 2.0));
                loopBeats = beats;
                loopEnd = loopStart + beats * beat;
                if (looping && pos / sr >= loopEnd) pos = loopStart * sr;
            }
            break;

        case Cmd::Reloop:
            if (looping) looping = false;
            else if (loopEnd > loopStart) { looping = true; if (posS > loopEnd || posS < loopStart) pos = loopStart * sr; }
            break;

        case Cmd::BeatJump:
            pos = jlimit (0.0, len - 1, pos + c.value * beat * sr);
            if (looping) { loopStart = loopStart + c.value * beat; loopEnd = loopEnd + c.value * beat; }
            break;

        case Cmd::Seek:
            pos = jlimit (0.0, len - 1, c.value * len);
            break;

        case Cmd::Jog:
            if (touch && vinylMode)
                scratchTicks += c.value;
            else if (playing)
                bend = jlimit (-0.5, 0.5, bend + c.value * 0.004);
            else
                pos = jlimit (0.0, len - 1, pos + c.value * jogSecondsPerTick * 0.5 * sr);
            break;

        case Cmd::JogTouch:
            touch = c.value > 0.5;
            if (touch && vinylMode) { scratchSpeed = playing ? speed : 0.0; scratchTicks = 0; }
            scratching = touch && vinylMode;
            break;

        case Cmd::ScaleBpm:
            if (bpm > 0) bpm = jlimit (40.0, 300.0, bpm * c.value);
            break;

        case Cmd::Eject:
            break;
    }
}

void TrackPlayer::render (AudioBuffer<float>& out, int n, float& tempoKnob, float range, const TrackPlayer* master)
{
    const SpinLock::ScopedTryLockType tl (trackLock);
    if (! tl.isLocked())
        return;

    int s1, n1, s2, n2;
    fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) apply (queue[(size_t) (s1 + i)], master);
    for (int i = 0; i < n2; ++i) apply (queue[(size_t) (s2 + i)], master);
    fifo.finishedRead (n1 + n2);

    if (audioTrack == nullptr)
        return;

    const double sr = audioTrack->sampleRate;
    const double len = audioTrack->audio.getNumSamples();
    const double ratio = sr / outRate;
    const double blockSec = n / outRate;

    // Tempo: follow the other deck while synced, else the tempo fader.
    double factor;
    if (synced && master != nullptr && master->loaded && master->effectiveBpm > 0 && bpm > 0)
    {
        factor = master->effectiveBpm / bpm;
        tempoKnob = (float) jlimit (0.0, 1.0, 0.5 + (factor - 1.0) / (2.0 * range));
    }
    else
        factor = 1.0 + (tempoKnob - 0.5) * 2.0 * range;
    tempoFactor = factor;
    effectiveBpm = bpm * factor;

    double target;
    if (touch && vinylMode)
    {
        const double want = scratchTicks * jogSecondsPerTick / blockSec;
        scratchTicks = 0;
        scratchSpeed += (want - scratchSpeed) * 0.45;
        target = scratchSpeed;
    }
    else if (playing)
    {
        target = factor * (1.0 + bend);
        bend *= 0.88;
    }
    else
        target = 0.0;

    const double k = 1.0 - std::exp (-1.0 / (0.004 * outRate));  // ~4 ms speed smoothing (no clicks on stop/start)
    float* L = out.getWritePointer (0);
    float* R = out.getWritePointer (1);
    const bool loop = looping && loopEnd > loopStart;
    const double ls = loopStart * sr, le = loopEnd * sr;

    for (int i = 0; i < n; ++i)
    {
        speed += (target - speed) * k;
        if (std::abs (speed) > 1.0e-5 || std::abs (target) > 1.0e-5)
        {
            L[i] += sampleAt (0, pos);
            R[i] += sampleAt (1, pos);
            pos += speed * ratio;
            if (loop && speed > 0 && pos >= le) pos -= (le - ls);
            if (pos >= len - 1) { pos = len - 1; if (! touch) { playing = false; target = 0; } }
            if (pos < 0) pos = 0;
        }
    }
    position = pos / sr;
}
} // namespace deckrig
