#pragma once
#include <cmath>
#include <algorithm>

// Small, allocation-free DSP helpers used on the audio thread. Scalar C++ only
// (no SIMD intrinsics), so it runs on any x86-64 Mac and on Apple Silicon.
namespace deckrig
{
constexpr double pi = 3.14159265358979323846;

inline float dbToGain (float db) noexcept { return db <= -90.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) noexcept { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }

enum class XfadeCurve { Smooth, Sharp, Linear };

// Crossfader position x in 0..1 (0 = full A). Returns gains for A and B.
//   Smooth: constant power, both decks at -3 dB in the middle (blend mixing)
//   Sharp:  both decks full until near the edge, fast cut (scratch style)
//   Linear: straight line, -6 dB in the middle
struct XfadeGains { float a, b; };
inline XfadeGains crossfade (float x, XfadeCurve curve) noexcept
{
    x = std::clamp (x, 0.0f, 1.0f);
    switch (curve)
    {
        case XfadeCurve::Smooth:
            return { (float) std::cos (x * pi * 0.5), (float) std::sin (x * pi * 0.5) };
        case XfadeCurve::Sharp:
        {
            constexpr float edge = 0.06f;
            return { std::clamp ((1.0f - x) / edge, 0.0f, 1.0f), std::clamp (x / edge, 0.0f, 1.0f) };
        }
        case XfadeCurve::Linear:
            return { 1.0f - x, x };
    }
    return { 1.0f, 1.0f };
}

// EQ knob 0..1 -> dB. Centre (0.5) = 0 dB, top = +6 dB, bottom = kill (-inf).
inline float eqKnobToDb (float v) noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    if (v >= 0.5f) return (v - 0.5f) * 2.0f * 6.0f;
    if (v < 0.02f) return -100.0f;
    // Bottom half: 0 dB -> -40 dB on a curve that spends more travel near 0 dB.
    const float t = (0.5f - v) / 0.48f;
    return -40.0f * t * t;
}

// Gain knob 0..1 -> -12..+12 dB.
inline float gainKnobToDb (float v) noexcept { return (std::clamp (v, 0.0f, 1.0f) - 0.5f) * 24.0f; }

// Volume fader 0..1 -> linear gain with an audio taper (-inf at 0, 0 dB at 1).
inline float faderToGain (float v) noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    return v * v * v;   // roughly -18 dB at 55%, -6 dB at 80%
}

// Transposed direct form II biquad (RBJ cookbook), coefficients computed in place.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    void setIdentity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    void lowShelf (double fs, double f, double db, double q = 0.707) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * pi * f / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / (2 * q), sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
             (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }
    void highShelf (double fs, double f, double db, double q = 0.707) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * pi * f / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / (2 * q), sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
    void peak (double fs, double f, double db, double q) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * pi * f / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowPass (double fs, double f, double q) noexcept
    {
        const double w = 2 * pi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highPass (double fs, double f, double q) noexcept
    {
        const double w = 2 * pi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

    inline float process (float x, int ch) noexcept
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }
};

// Filter knob: 0.5 = off, below = low-pass sweeping down, above = high-pass sweeping up.
// Returns cutoff in Hz for the active side (exponential sweep 20 kHz <-> 40 Hz).
inline double filterCutoff (float knob, bool& isLowPass, bool& active) noexcept
{
    constexpr float dead = 0.02f;
    const float d = knob - 0.5f;
    active = std::abs (d) > dead;
    isLowPass = d < 0;
    const double t = std::clamp ((std::abs (d) - dead) / (0.5 - dead), 0.0, 1.0);
    return isLowPass ? 20000.0 * std::pow (40.0 / 20000.0, t) : 20.0 * std::pow (8000.0 / 20.0, t);
}
} // namespace deckrig
