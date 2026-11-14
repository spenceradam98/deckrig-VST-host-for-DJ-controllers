#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

// Offline track analysis: tempo, beat grid and waveform overview.
// Plain C++ (no JUCE) so it is unit-testable and safe on a worker thread.
namespace deckrig
{
struct BeatInfo
{
    double bpm = 0.0;        // 0 = no steady beat found
    double firstBeat = 0.0;  // seconds to the first beat of the grid
    double confidence = 0.0; // 0..1, how clearly one tempo stood out
};

// Estimates tempo and grid phase from a mono signal. Tempo is folded into
// [minBpm, maxBpm) (default 70..180, which keeps house at ~125 and D&B at ~174).
BeatInfo detectBeats (const float* mono, int64_t numSamples, double sampleRate,
                      double minBpm = 70.0, double maxBpm = 180.0);

// Waveform overview: one value per `block` samples.
struct WaveformData
{
    int block = 256;
    std::vector<float> peak;  // max |x|, 0..1
    std::vector<float> low;   // share of energy below ~200 Hz, 0..1 (for colouring)
};

WaveformData computeWaveform (const float* left, const float* right, int64_t numSamples, double sampleRate, int block = 256);

// 4-point cubic Hermite interpolation; x in [0,1) between y1 and y2.
inline float hermite (float y0, float y1, float y2, float y3, float x) noexcept
{
    const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3,
                c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * x + c2) * x + c1) * x + c0;
}

// Beat-grid helpers (all times in seconds of the track).
inline double beatLength (double bpm) noexcept { return bpm > 0 ? 60.0 / bpm : 0.0; }
inline double beatPhase (double pos, double firstBeat, double bpm) noexcept
{
    const double len = beatLength (bpm);
    if (len <= 0) return 0;
    const double b = (pos - firstBeat) / len;
    return b - std::floor (b);
}
inline double nearestBeat (double pos, double firstBeat, double bpm) noexcept
{
    const double len = beatLength (bpm);
    if (len <= 0) return pos;
    return firstBeat + std::round ((pos - firstBeat) / len) * len;
}
// Offset (seconds) to add to `pos` so its beat phase matches `targetPhase`, moving at most half a beat.
inline double phaseCorrection (double pos, double firstBeat, double bpm, double targetPhase) noexcept
{
    double d = targetPhase - beatPhase (pos, firstBeat, bpm);
    if (d > 0.5) d -= 1.0;
    if (d < -0.5) d += 1.0;
    return d * beatLength (bpm);
}
} // namespace deckrig
