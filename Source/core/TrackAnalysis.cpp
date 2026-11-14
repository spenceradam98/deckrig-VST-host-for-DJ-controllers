#include "TrackAnalysis.h"

namespace deckrig
{
BeatInfo detectBeats (const float* x, int64_t n, double sr, double minBpm, double maxBpm)
{
    BeatInfo info;
    if (x == nullptr || n < (int64_t) (sr * 4) || sr <= 0)
        return info;

    // 1. Onset strength: positive change in log energy of a low-passed signal,
    //    ~5.8 ms frames at 44.1 kHz. Low-passing favours kicks and bass.
    const int hop = std::max (64, (int) std::lround (sr / 172.0));
    const double fps = sr / hop;
    const int64_t frames = n / hop;
    std::vector<float> onset ((size_t) frames, 0.0f);
    {
        const double a = std::exp (-2.0 * 3.14159265358979 * 300.0 / sr);  // one-pole LP at 300 Hz
        double lp = 0, lp2 = 0, prev = 0;
        for (int64_t f = 0; f < frames; ++f)
        {
            double e = 0, eh = 0;
            for (int i = 0; i < hop; ++i)
            {
                const double s = x[f * hop + i];
                lp = (1 - a) * s + a * lp;
                lp2 = (1 - a) * lp + a * lp2;
                e += lp2 * lp2;
                eh += s * s;
            }
            // Compressed (log1p) so tiny rises out of near-silence, like a quiet hi-hat, stay small.
            const double le = std::log1p (100.0 * (e * 4.0 + eh * 0.25) / hop);
            onset[(size_t) f] = (float) std::max (0.0, le - prev);
            prev = le;
        }
        // Remove the local mean so sustained sections don't dominate.
        std::vector<float> smooth (onset.size());
        const int w = (int) (fps * 0.25);
        double acc = 0;
        for (int64_t f = 0; f < frames; ++f)
        {
            acc += onset[(size_t) f];
            if (f >= w) acc -= onset[(size_t) (f - w)];
            smooth[(size_t) f] = (float) (acc / std::min<int64_t> (f + 1, w));
        }
        for (size_t f = 0; f < onset.size(); ++f)
            onset[f] = std::max (0.0f, onset[f] - smooth[f]);
        // Light blur so peaks that straddle two frames still correlate at fractional lags.
        for (int pass = 0; pass < 2; ++pass)
        {
            float prevV = 0;
            for (size_t f = 0; f + 1 < onset.size(); ++f)
            {
                const float cur = onset[f];
                onset[f] = 0.25f * prevV + 0.5f * cur + 0.25f * onset[f + 1];
                prevV = cur;
            }
        }
    }

    // 2. Autocorrelation over tempo lags, weighted towards ~120 BPM (log-Gaussian).
    //    Each candidate is scored as a comb (lags L, 2L, 3L, 4L) so the true beat
    //    period beats look-alikes such as 2/3 or 3/2 of it.
    const int lagMin = (int) std::floor (fps * 60.0 / 240.0), lagMax = (int) std::ceil (fps * 60.0 / 40.0) * 4;
    const int64_t m = std::min<int64_t> (frames, (int64_t) (fps * 240));  // analyse up to 4 minutes
    std::vector<double> ac ((size_t) lagMax + 2, 0.0);
    for (int lag = lagMin; lag <= lagMax + 1; ++lag)
    {
        double s = 0;
        for (int64_t f = lag; f < m; ++f)
            s += (double) onset[(size_t) f] * onset[(size_t) (f - lag)];
        ac[(size_t) lag] = s / (double) std::max<int64_t> (1, m - lag);
    }
    auto acAt = [&] (double lag)
    {
        const int i = (int) lag;
        if (i < lagMin || i + 1 > lagMax + 1) return 0.0;
        const double fr = lag - i;
        return ac[(size_t) i] * (1 - fr) + ac[(size_t) i + 1] * fr;
    };

    double bestScore = -1, bestLag = 0, total = 0;
    const double lo = fps * 60.0 / maxBpm, hi = fps * 60.0 / minBpm;
    // Fractional lag steps: at fast tempos an integer lag drifts off the 3rd/4th beat.
    for (double lag = std::max (lo, (double) lagMin); lag <= std::min (hi, (double) lagMax / 4.0); lag += 0.1)
    {
        const double bpm = 60.0 * fps / lag;
        const double w = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.9, 2.0));
        const double score = w * (acAt (lag) + acAt (lag * 2.0) + acAt (lag * 3.0) + acAt (lag * 4.0));
        total += score * 0.1;
        if (score > bestScore) { bestScore = score; bestLag = lag; }
    }
    if (bestLag <= 0 || bestScore <= 0)
        return info;

    // If every other "beat" also carries a full-strength pulse, the beat is twice as fast.
    if (60.0 * fps / (bestLag * 0.5) < maxBpm)
    {
        double half = 0, full = 0;
        for (int k = 1; k <= 4; ++k) { half += acAt (bestLag * (k - 0.5)); full += acAt (bestLag * k); }
        if (half >= 0.6 * full) bestLag *= 0.5;
    }

    // Long-range refinement: the lag that best lines up 16 beats later.
    {
        double best = -1, refined = bestLag;
        for (double l = bestLag - 0.5; l <= bestLag + 0.5; l += 0.02)
        {
            double s = 0;
            for (int k = 1; k <= 16; ++k)
            {
                const double lag = l * k;
                const int64_t li = (int64_t) lag;
                if (li + 1 >= m) break;
                double sk = 0;
                for (int64_t f = li + 1; f < m; f += 3)
                {
                    const double fr = lag - li;
                    sk += onset[(size_t) f] * (onset[(size_t) (f - li)] * (1 - fr) + onset[(size_t) (f - li - 1)] * fr);
                }
                s += sk;
            }
            if (s > best) { best = s; refined = l; }
        }
        bestLag = refined;
    }

    info.bpm = 60.0 * fps / bestLag;
    while (info.bpm < minBpm) info.bpm *= 2;
    while (info.bpm >= maxBpm) info.bpm *= 0.5;
    info.confidence = std::clamp (total > 0 ? bestScore / total * 8.0 : 0.0, 0.0, 1.0);

    // 3. Grid phase: the offset whose pulse train collects the most onset energy.
    const double period = fps * 60.0 / info.bpm;
    const int steps = std::max (1, (int) std::ceil (period));
    double bestPhase = 0, bestSum = -1;
    for (int p = 0; p < steps * 4; ++p)
    {
        const double ph = p * 0.25;
        double s = 0;
        for (double f = ph; f + 1 < (double) m; f += period)
        {
            const int64_t i = (int64_t) f;
            s += onset[(size_t) i] + 0.5 * (i > 0 ? onset[(size_t) i - 1] : 0.0f);
        }
        if (s > bestSum) { bestSum = s; bestPhase = ph; }
    }
    // Onsets are detected at the end of the frame where energy rose; centre them.
    info.firstBeat = std::max (0.0, (bestPhase - 0.5) * hop / sr);
    return info;
}

WaveformData computeWaveform (const float* l, const float* r, int64_t n, double sr, int block)
{
    WaveformData w;
    w.block = block;
    const int64_t blocks = n / block + 1;
    w.peak.resize ((size_t) blocks, 0.0f);
    w.low.resize ((size_t) blocks, 0.0f);
    const double a = std::exp (-2.0 * 3.14159265358979 * 200.0 / std::max (1.0, sr));
    double lp = 0;
    for (int64_t b = 0; b < blocks; ++b)
    {
        float pk = 0;
        double e = 0, el = 0;
        const int64_t end = std::min (n, (b + 1) * block);
        for (int64_t i = b * block; i < end; ++i)
        {
            const float s = 0.5f * (l[i] + (r != nullptr ? r[i] : l[i]));
            pk = std::max (pk, std::abs (s));
            lp = (1 - a) * s + a * lp;
            e += (double) s * s;
            el += lp * lp;
        }
        w.peak[(size_t) b] = std::min (1.0f, pk);
        w.low[(size_t) b] = e > 1e-12 ? (float) std::clamp (el / e, 0.0, 1.0) : 0.0f;
    }
    return w;
}
} // namespace deckrig
