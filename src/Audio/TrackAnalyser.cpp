#include "TrackAnalyser.h"
#include "MusicalKey.h"
#include <juce_dsp/juce_dsp.h>
#include <numeric>
#include <iostream>

namespace orbit
{

namespace
{
    constexpr int fftOrder = 10, fftSize = 1 << fftOrder;   // 23 ms at 44.1 kHz
    constexpr double targetFps = 200.0;                       // onset frames per second (5 ms)

    struct Onsets
    {
        double fps = targetFps;
        int hop = 220;
        static constexpr int numBands = 24;
        std::vector<float> full, low, kick;
        std::vector<std::array<float, numBands>> bands;   // log-spaced band levels per frame (for bar starts)   // positive log-spectral flux up to 8 kHz / up to 150 Hz; linear power flux below 100 Hz
    };

    bool cancelled (const std::atomic<bool>* c) { return c != nullptr && c->load(); }

    Onsets onsetEnvelope (const TrackAudio& a, const std::atomic<bool>* cancel)
    {
        Onsets o;
        o.hop = juce::jmax (1, (int) std::lround (a.sampleRate / targetFps));
        o.fps = a.sampleRate / o.hop;
        const auto n = (int64_t) a.numFrames;
        const int frames = (int) juce::jmax<int64_t> (0, (n - fftSize) / o.hop + 1);
        o.full.assign ((size_t) frames, 0.0f);
        o.low.assign ((size_t) frames, 0.0f);
        o.kick.assign ((size_t) frames, 0.0f);
        o.bands.assign ((size_t) frames, {});

        juce::dsp::FFT fft (fftOrder);
        std::vector<float> window (fftSize), buf (fftSize * 2), prev (fftSize / 2 + 1, 0.0f), mag (fftSize / 2 + 1);
        float prevKick = 0.0f;
        for (int i = 0; i < fftSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);

        const double binHz = a.sampleRate / fftSize;
        const int topBin = juce::jmin (fftSize / 2, (int) (8000.0 / binHz));
        const int lowTop = juce::jmax (2, (int) std::ceil (150.0 / binHz));
        const int kickTop = juce::jmax (1, (int) (100.0 / binHz));
        std::array<int, Onsets::numBands + 1> edges {};
        for (int i = 0; i <= Onsets::numBands; ++i)
            edges[(size_t) i] = juce::jlimit (1, topBin, (int) std::lround (30.0 * std::pow (10000.0 / 30.0, (double) i / Onsets::numBands) / binHz));

        for (int f = 0; f < frames; ++f)
        {
            if ((f & 4095) == 0 && cancelled (cancel)) break;
            const auto start = (size_t) f * (size_t) o.hop;
            for (int i = 0; i < fftSize; ++i)
                buf[(size_t) i] = 0.5f * (a.left[start + (size_t) i] + a.right[start + (size_t) i]) * window[(size_t) i];
            std::fill (buf.begin() + fftSize, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);

            float full = 0.0f, low = 0.0f, kickPower = 0.0f;
            for (int b = 1; b <= kickTop; ++b) kickPower += buf[(size_t) b] * buf[(size_t) b];
            for (int i = 0; i < Onsets::numBands; ++i)
            {
                float sum = 0.0f;
                const int a0 = edges[(size_t) i], a1 = juce::jmax (a0 + 1, edges[(size_t) i + 1]);
                for (int b = a0; b < a1; ++b) sum += buf[(size_t) b];
                o.bands[(size_t) f][(size_t) i] = sum / (float) (a1 - a0);
            }
            for (int b = 1; b <= topBin; ++b)
            {
                mag[(size_t) b] = std::log1p (100.0f * buf[(size_t) b]);
                const float d = mag[(size_t) b] - prev[(size_t) b];
                if (d > 0.0f)
                {
                    full += d;
                    if (b <= lowTop) low += d;
                }
            }
            std::swap (mag, prev);
            o.full[(size_t) f] = full;
            o.low[(size_t) f] = low;
            // Kicks are far louder than bass notes in the sub band, which log flux hides (rolling basslines).
            o.kick[(size_t) f] = juce::jmax (0.0f, kickPower - prevKick);
            prevKick = kickPower;
        }
        return o;
    }

    /** Removes the slowly-moving level so only onsets remain (moving average over ~0.4 s, half-wave rectified). */
    std::vector<float> detrend (const std::vector<float>& x, int radius)
    {
        std::vector<double> cum (x.size() + 1, 0.0);
        for (size_t i = 0; i < x.size(); ++i) cum[i + 1] = cum[i] + x[i];
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            const size_t a = i > (size_t) radius ? i - (size_t) radius : 0, b = juce::jmin (x.size(), i + (size_t) radius + 1);
            const double mean = (cum[b] - cum[a]) / (double) (b - a);
            y[i] = (float) juce::jmax (0.0, (double) x[i] - mean);
        }
        return y;
    }

    void normalise (std::vector<float>& x)
    {
        double sum = 0.0;
        for (auto v : x) sum += v;
        const double mean = x.empty() ? 0.0 : sum / (double) x.size();
        if (mean > 0.0) for (auto& v : x) v = (float) (v / mean);
    }

    float at (const std::vector<float>& e, double pos)
    {
        if (pos < 0.0) return 0.0f;
        const auto i = (size_t) pos;
        if (i + 1 >= e.size()) return 0.0f;
        const float f = (float) (pos - (double) i);
        return e[i] * (1.0f - f) + e[i + 1] * f;
    }

    /** Peak of e within [centre - r, centre + r], with parabolic interpolation. Returns {position, height}. */
    std::pair<double, float> peakNear (const std::vector<float>& e, double centre, double r)
    {
        const int a = juce::jmax (1, (int) std::floor (centre - r)), b = juce::jmin ((int) e.size() - 2, (int) std::ceil (centre + r));
        int best = -1;
        float h = 0.0f;
        for (int i = a; i <= b; ++i)
            if (e[(size_t) i] > h) { h = e[(size_t) i]; best = i; }
        if (best < 0) return { centre, 0.0f };
        const float l = e[(size_t) best - 1], m = e[(size_t) best], rr = e[(size_t) best + 1];
        const float den = l - 2.0f * m + rr;
        const double off = std::abs (den) > 1e-9f ? juce::jlimit (-0.5, 0.5, (double) (0.5f * (l - rr) / den)) : 0.0;
        return { best + off, h };
    }

    struct Fit { double phase = 0.0, period = 0.0, inliers = 0.0; };

    /** Robust least-squares fit of beat positions t_k = phase + k * period to the onset peaks near a first guess.
        Weak peaks (breakdowns, where the nearest onset is a hat or a bass note) and outliers are dropped. */
    Fit refineGrid (const std::vector<float>& e, double phase, double period)
    {
        Fit fit { phase, period, 0.0 };
        const int beats = (int) ((double) e.size() / period);
        if (beats < 8) return fit;

        struct P { int k; double pos; double h; };
        std::vector<P> pts;
        for (int k = 0; k < beats; ++k)
        {
            auto [pos, h] = peakNear (e, phase + k * period, 0.1 * period);
            if (h > 0.0f) pts.push_back ({ k, pos, (double) h });
        }
        if (pts.size() < 8) return fit;
        std::vector<double> hs;
        for (auto& p : pts) hs.push_back (p.h);
        std::sort (hs.begin(), hs.end());
        const double threshold = 0.35 * hs[hs.size() * 3 / 4];
        pts.erase (std::remove_if (pts.begin(), pts.end(), [&] (const P& p) { return p.h < threshold; }), pts.end());

        std::vector<char> use (pts.size(), 1);
        double a = phase, b = period;
        for (int it = 0; it < 6; ++it)
        {
            double sw = 0, sk = 0, st = 0, skk = 0, skt = 0;
            int used = 0;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                if (! use[i]) continue;
                const double w = pts[i].h, k = pts[i].k;
                sw += w; sk += w * k; st += w * pts[i].pos; skk += w * k * k; skt += w * k * pts[i].pos;
                ++used;
            }
            const double den = sw * skk - sk * sk;
            if (used < 8 || std::abs (den) < 1e-9) break;
            const double nb = (sw * skt - sk * st) / den;
            if (std::abs (nb - period) > 0.03 * period) break;   // wandered off: keep the last good fit
            b = nb;
            a = (st - b * sk) / sw;

            // Drop outliers: further than 3 x the median absolute residual (at least 1.5 frames)
            std::vector<double> res;
            for (size_t i = 0; i < pts.size(); ++i) res.push_back (std::abs (pts[i].pos - (a + b * pts[i].k)));
            auto sorted = res;
            std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
            const double limit = juce::jmax (1.5, 3.0 * sorted[sorted.size() / 2]);
            for (size_t i = 0; i < pts.size(); ++i) use[i] = res[i] <= limit;
            fit = { a, b, 0.0 };
        }
        int inliers = 0;
        for (auto u : use) inliers += u;
        fit.inliers = (double) inliers / beats;
        return fit;
    }

    double fittedPhase (const std::vector<float>& e, double phase, double period)
    {
        // With the period fixed, the phase is the weighted mean offset of the onset peaks.
        const int beats = (int) ((double) e.size() / period);
        double sw = 0, so = 0;
        for (int k = 0; k < beats; ++k)
        {
            auto [pos, h] = peakNear (e, phase + k * period, 0.06 * period);
            if (h <= 0.0f) continue;
            const double w = std::sqrt ((double) h);
            sw += w; so += w * (pos - (phase + k * period));
        }
        return sw > 0 ? phase + so / sw : phase;
    }
}

//==============================================================================
double TrackAnalyser::estimateTempo (const std::vector<float>& e, double fps, double minBpm, double maxBpm)
{
    if (e.size() < 64) return 0.0;
    const int maxLag = juce::jmin ((int) e.size() - 1, (int) std::ceil (4.0 * 60.0 * fps / minBpm) + 2);
    std::vector<double> acf ((size_t) maxLag + 1, 0.0);
    const size_t n = e.size();
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        const float* a = e.data();
        const float* b = e.data() + lag;
        const size_t m = n - (size_t) lag;
        for (size_t i = 0; i < m; ++i) s += (double) a[i] * b[i];
        acf[(size_t) lag] = s / (double) m;
    }
    auto acfAt = [&] (double lag)
    {
        const auto i = (size_t) lag;
        if (i + 1 >= acf.size()) return 0.0;
        const double f = lag - (double) i;
        return acf[i] * (1.0 - f) + acf[i + 1] * f;
    };

    double best = 0.0, bestScore = -1.0;
    for (double bpm = minBpm; bpm <= maxBpm + 1e-9; bpm += 0.05)
    {
        const double lag = 60.0 * fps / bpm;
        const double s = acfAt (lag) + 0.5 * acfAt (2.0 * lag) + 0.25 * acfAt (4.0 * lag);
        if (s > bestScore) { bestScore = s; best = bpm; }
    }
    return best;
}

TrackAnalyser::Result TrackAnalyser::analyse (const TrackAudio& audio, const Options& opt, const std::atomic<bool>* cancel)
{
    Result r;
    if (audio.numFrames < (int64_t) (audio.sampleRate * 4)) return r;

    auto on = onsetEnvelope (audio, cancel);
    if (cancelled (cancel)) return r;

    const int radius = (int) (0.2 * on.fps);
    auto full = detrend (on.full, radius), low = detrend (on.low, radius);
    auto kick = on.kick;
    normalise (full);
    normalise (low);
    normalise (kick);
    for (auto& v : kick) v = juce::jmin (v, 20.0f);   // keep a few huge hits from dominating
    // Kicks carry the beat in electronic music: weight the low band.
    std::vector<float> e (full.size());
    for (size_t i = 0; i < e.size(); ++i) e[i] = full[i] + 1.5f * low[i];

    // Tempo
    double bpm = estimateTempo (e, on.fps, opt.minBpm, opt.maxBpm);
    if (bpm <= 0.0) return r;

    double hint = opt.hintBpm;
    if (hint > 0.0)
    {
        while (hint < opt.minBpm * 0.98) hint *= 2.0;
        while (hint > opt.maxBpm * 1.02) hint *= 0.5;
        // Trust the tag when it matches the audio's tempo (or is very close to it).
        if (std::abs (hint - bpm) / bpm < 0.03) bpm = hint;
    }

    // Beat positions follow the kick: phase and fine tempo come from the kick-weighted envelope.
    std::vector<float> ek (full.size());
    for (size_t i = 0; i < ek.size(); ++i) ek[i] = 0.5f * full[i] + 2.0f * kick[i];

    // Fine tempo and phase together: comb over the whole track (the autocorrelation is only good to ~0.3%).
    double period = 60.0 * on.fps / bpm;
    double phase = 0.0;
    {
        double bestComb = -1.0, bestBpm = bpm;
        const double lo = bpm * 0.994, hi = bpm * 1.006;
        for (double cand = lo; cand <= hi; cand += 0.01)
        {
            const double per = 60.0 * on.fps / cand;
            for (double p = 0.0; p < per; p += 0.5)
            {
                double sum = 0.0;
                for (double x = p; x < (double) ek.size(); x += per) sum += at (ek, x);
                if (sum > bestComb) { bestComb = sum; phase = p; bestBpm = cand; }
            }
        }
        bpm = bestBpm;
        period = 60.0 * on.fps / bpm;
    }

    // Refine tempo and phase from where the onsets actually are
    auto fit = refineGrid (ek, phase, period);
    if (std::getenv ("CF_ANALYSE_DEBUG") != nullptr)
        std::cerr << "tempo estimate " << bpm << " phase " << phase << " -> fit period " << fit.period << " (" << 60.0 * on.fps / fit.period
                  << " BPM) phase " << fit.phase << " inliers " << fit.inliers << std::endl;
    period = fit.period;
    phase = fit.phase;
    bpm = 60.0 * on.fps / period;

    if (hint > 0.0 && std::abs (bpm - hint) < 0.06) bpm = hint;
    else if (opt.wholeBpmWhenClose && std::abs (bpm - std::round (bpm)) < 0.04) bpm = std::round (bpm);
    period = 60.0 * on.fps / bpm;
    phase = fittedPhase (ek, phase, period);
    r.confidence = fit.inliers;

    // Flux peaks a little after a sound starts; this lines the grid up with the start of the kick.
    const double latency = (fftSize * 0.5 - on.hop * 0.5) / audio.sampleRate - 0.014;
    double t0 = (phase * on.hop) / audio.sampleRate + latency;
    const double beatSec = 60.0 / bpm;
    t0 -= std::floor (t0 / beatSec) * beatSec;   // first beat at or after 0

    // Bar start: the sound changes most from one beat to the next on the one (new bass notes, chords, parts),
    // so the beat position (of four) with the biggest average change is the downbeat.
    const int numBeats = (int) ((audio.lengthSeconds() - t0) / beatSec) + 1;
    std::array<double, 4> barScore {};
    std::array<int, 4> barCount {};
    {
        std::array<float, Onsets::numBands> prevBeat {};
        bool havePrev = false;
        for (int k = 0; k < numBeats; ++k)
        {
            const int f0 = (int) (((t0 + k * beatSec - latency) * audio.sampleRate) / on.hop);
            const int f1 = (int) (((t0 + (k + 1) * beatSec - latency) * audio.sampleRate) / on.hop);
            if (f0 < 0 || f1 > (int) on.bands.size() || f1 <= f0) { havePrev = false; continue; }
            std::array<float, Onsets::numBands> cur {};
            for (int f = f0; f < f1; ++f)
                for (int i = 0; i < Onsets::numBands; ++i) cur[(size_t) i] += on.bands[(size_t) f][(size_t) i];
            for (auto& v : cur) v = std::log1p (100.0f * v / (float) (f1 - f0));
            if (havePrev)
            {
                double d = 0.0;
                for (int i = 0; i < Onsets::numBands; ++i) d += juce::square ((double) cur[(size_t) i] - prevBeat[(size_t) i]);
                barScore[(size_t) (k % 4)] += std::sqrt (d);
                ++barCount[(size_t) (k % 4)];
            }
            prevBeat = cur;
            havePrev = true;
        }
        for (int i = 0; i < 4; ++i) if (barCount[(size_t) i] > 0) barScore[(size_t) i] /= barCount[(size_t) i];
    }
    const int one = (int) (std::max_element (barScore.begin(), barScore.end()) - barScore.begin());
    if (std::getenv ("CF_ANALYSE_DEBUG") != nullptr)
        std::cerr << "bar scores " << barScore[0] << " " << barScore[1] << " " << barScore[2] << " " << barScore[3] << std::endl;

    for (int k = 0; k < numBeats; ++k)
        r.beats.push_back ({ t0 + k * beatSec, (float) bpm, ((k - one) % 4 + 4) % 4 + 1 });
    r.bpm = bpm;

    if (! cancelled (cancel))
        r.key = detectKey (audio);
    return r;
}

//==============================================================================
juce::String TrackAnalyser::detectKey (const TrackAudio& a)
{
    constexpr int order = 14, size = 1 << order;   // 16384: ~2.7 Hz per bin at 44.1 kHz
    if (a.numFrames < size * 2) return {};

    juce::dsp::FFT fft (order);
    std::vector<float> window (size), buf (size * 2);
    for (int i = 0; i < size; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);

    // Bins 55 Hz .. 1760 Hz mapped to pitch classes (triangular weighting by distance from the semitone)
    const double binHz = a.sampleRate / size;
    struct Map { int bin; int pc; float w; };
    std::vector<Map> maps;
    for (int b = (int) (55.0 / binHz); b <= (int) (1760.0 / binHz); ++b)
    {
        if (b < 1) continue;
        const double midi = 69.0 + 12.0 * std::log2 (b * binHz / 440.0);
        const double nearest = std::round (midi);
        const float w = (float) juce::jmax (0.0, 1.0 - 2.0 * std::abs (midi - nearest));
        if (w > 0.0f) maps.push_back ({ b, ((int) nearest % 12 + 12) % 12, w });
    }

    std::array<double, 12> chroma {};
    const int hop = size / 2;
    for (int64_t start = 0; start + size <= a.numFrames; start += hop)
    {
        for (int i = 0; i < size; ++i)
            buf[(size_t) i] = 0.5f * (a.left[(size_t) start + (size_t) i] + a.right[(size_t) start + (size_t) i]) * window[(size_t) i];
        std::fill (buf.begin() + size, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);

        std::array<double, 12> frame {};
        for (auto& m : maps) frame[(size_t) m.pc] += m.w * std::sqrt ((double) buf[(size_t) m.bin]);
        const double mx = *std::max_element (frame.begin(), frame.end());
        if (mx > 1e-6)
            for (int i = 0; i < 12; ++i) chroma[(size_t) i] += frame[(size_t) i] / mx;
    }

    // Krumhansl-Kessler key profiles
    static const double majorP[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorP[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto correlate = [&] (const double* prof, int tonic)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) ((i + tonic) % 12)]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = chroma[(size_t) ((i + tonic) % 12)] - mx, y = prof[i] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : -1.0;
    };

    MusicalKey best;
    double bestR = -2.0;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (int minor = 0; minor < 2; ++minor)
        {
            const double rr = correlate (minor ? minorP : majorP, tonic);
            if (rr > bestR) { bestR = rr; best.pitch = tonic; best.minor = minor != 0; }
        }
    return bestR > 0.0 ? best.toString() : juce::String();
}

} // namespace orbit
