#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "core/TrackAnalysis.h"

namespace deckrig
{
constexpr int numHotcues = 8;

// A decoded, analysed track held in memory.
struct TrackData
{
    juce::File file;
    juce::String title;
    juce::AudioBuffer<float> audio;   // stereo
    double sampleRate = 44100.0;
    BeatInfo beats;
    WaveformData wave;
    double lengthSeconds() const { return audio.getNumSamples() / sampleRate; }
};

// Decodes and analyses a file (worker thread). Returns nullptr and sets error on failure.
std::shared_ptr<TrackData> loadTrack (juce::AudioFormatManager&, const juce::File&, juce::String& error);

// Plays one track: CDJ-style cue, 8 hot cues, beat loops, tempo with sync, jog
// nudge/scratch/seek. Controls arrive as commands through a lock-free queue and
// are applied at the top of each audio block, so the MIDI thread, the UI and the
// audio thread never share mutable transport state.
class TrackPlayer
{
public:
    enum class Cmd
    {
        PlayToggle, CueDown, CueUp, SyncToggle, SyncOff,
        HotcueDown, HotcueUp, HotcueDelete,
        LoopBeats, LoopOff, LoopHalve, LoopDouble, Reloop, BeatJump,
        Seek, Jog, JogTouch, ScaleBpm, Eject
    };
    struct Command { Cmd type; double value = 0; int index = 0; };

    TrackPlayer();

    // Any thread except audio.
    void push (Command);

    // Message thread.
    void setTrack (std::shared_ptr<TrackData>, double cue = -1, const std::array<double, numHotcues>* hotcues = nullptr);
    std::shared_ptr<TrackData> getTrack() const { return track; }

    // Audio thread.
    void prepare (double sampleRate);
    // Adds the track into out (2 channels). tempoKnob 0..1 (0.5 = 0 %), range e.g. 0.08.
    void render (juce::AudioBuffer<float>& out, int numSamples, float& tempoKnob, float range, const TrackPlayer* syncMaster);

    // Published state, readable from any thread.
    std::atomic<double> position { 0 };     // seconds
    std::atomic<double> cuePoint { 0 };
    std::atomic<bool> playing { false }, synced { false }, looping { false }, loaded { false }, scratching { false };
    std::atomic<double> loopStart { 0 }, loopEnd { 0 }, loopBeats { 0 };
    std::array<std::atomic<double>, numHotcues> hotcues;  // < 0 = unset
    std::atomic<double> bpm { 0 }, firstBeat { 0 };       // track's own tempo and grid
    std::atomic<double> effectiveBpm { 0 };               // after tempo/sync
    std::atomic<double> tempoFactor { 1.0 };
    std::atomic<bool> vinylMode { true };                 // jog touch scratches (else nudges)
    std::atomic<int> changeCount { 0 };                   // bumps when cue/hot cues change (for saving)

private:
    void apply (const Command&, const TrackPlayer* syncMaster);
    float sampleAt (int ch, double pos) const noexcept;

    std::shared_ptr<TrackData> track;          // owned on the message thread
    juce::SpinLock trackLock;                  // guards audioTrack against swaps
    TrackData* audioTrack = nullptr;

    juce::AbstractFifo fifo { 512 };
    std::array<Command, 512> queue;
    juce::SpinLock pushLock;                   // several producers (MIDI + UI)

    double outRate = 44100.0;
    double pos = 0;                            // in track samples
    double speed = 0;                          // smoothed playback speed (1 = normal)
    double bend = 0;                           // jog nudge while playing, decays
    double scratchTicks = 0, scratchSpeed = 0;
    bool touch = false, cuePreview = false;
    int hotcuePreview = -1;
};
} // namespace deckrig
