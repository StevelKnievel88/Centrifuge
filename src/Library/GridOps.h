#pragma once
#include "TrackInfo.h"
#include <cmath>

namespace orbit
{

/** Beat grid edits. Every function returns the new grid; grids are sorted by time with beatInBar 1..4. */
namespace GridOps
{
    inline int nearestBeat (const std::vector<Beat>& b, double t)
    {
        if (b.empty()) return -1;
        auto it = std::lower_bound (b.begin(), b.end(), t, [] (const Beat& x, double v) { return x.timeSec < v; });
        int i = (int) (it - b.begin());
        if (i >= (int) b.size()) return (int) b.size() - 1;
        if (i > 0 && t - b[(size_t) i - 1].timeSec < b[(size_t) i].timeSec - t) --i;
        return i;
    }

    inline double bpmAt (const std::vector<Beat>& b, double t, double fallback)
    {
        const int i = nearestBeat (b, t);
        if (i < 0) return fallback;
        if (b[(size_t) i].bpm > 20.0f) return b[(size_t) i].bpm;
        if (i + 1 < (int) b.size()) return 60.0 / juce::jmax (0.1, b[(size_t) i + 1].timeSec - b[(size_t) i].timeSec);
        return fallback;
    }

    /** A constant-tempo grid through `anchorTime` (which gets beat-in-bar `anchorBeatInBar`), covering the track. */
    inline std::vector<Beat> constant (double bpm, double anchorTime, int anchorBeatInBar, double lengthSec)
    {
        std::vector<Beat> out;
        bpm = juce::jlimit (20.0, 400.0, bpm);
        const double len = 60.0 / bpm;
        const double first = anchorTime - std::floor (anchorTime / len) * len;
        const int stepsBack = (int) std::floor ((anchorTime - first) / len + 0.5);
        int bib = ((anchorBeatInBar - 1 - stepsBack) % 4 + 4) % 4 + 1;
        for (double t = first; t < lengthSec + len * 0.5; t += len)
        {
            out.push_back ({ t, (float) bpm, bib });
            bib = bib % 4 + 1;
        }
        return out;
    }

    /** Moves the whole grid by `seconds`. */
    inline std::vector<Beat> shift (std::vector<Beat> b, double seconds)
    {
        for (auto& x : b) x.timeSec += seconds;
        while (! b.empty() && b.front().timeSec < -0.0005) b.erase (b.begin());
        return b;
    }

    /** Moves the grid so the beat nearest `t` lands exactly on `t`. */
    inline std::vector<Beat> anchorAt (std::vector<Beat> b, double t)
    {
        const int i = nearestBeat (b, t);
        if (i < 0) return b;
        return shift (std::move (b), t - b[(size_t) i].timeSec);
    }

    /** Makes the beat nearest `t` the first beat of a bar (renumbers the whole grid). */
    inline std::vector<Beat> setDownbeat (std::vector<Beat> b, double t)
    {
        const int i = nearestBeat (b, t);
        if (i < 0) return b;
        for (int k = 0; k < (int) b.size(); ++k)
            b[(size_t) k].beatInBar = (((k - i) % 4) + 4) % 4 + 1;
        return b;
    }

    /** Sets a constant tempo, keeping the beat nearest `anchor` (normally the playhead) where it is. */
    inline std::vector<Beat> setBpm (const std::vector<Beat>& b, double bpm, double anchor, double lengthSec)
    {
        if (b.empty()) return constant (bpm, anchor, 1, lengthSec);
        const int i = nearestBeat (b, anchor);
        return constant (bpm, b[(size_t) i].timeSec, b[(size_t) i].beatInBar, lengthSec);
    }

    /** The beat nearest `t` starts a bar, and the bars count on from there (beats before it keep their numbers) -
        for a 2-beat fill or a bridge that throws the downbeat out. */
    inline std::vector<Beat> barStartsHere (std::vector<Beat> b, double t)
    {
        const int i = nearestBeat (b, t);
        if (i < 0) return b;
        for (int k = i; k < (int) b.size(); ++k) b[(size_t) k].beatInBar = (k - i) % 4 + 1;
        return b;
    }

    /** From the beat nearest `t` on, the count moves by `delta` beats: +1 adds a beat to the bar there (the downbeat
        comes a beat later), -1 takes one out. */
    inline std::vector<Beat> shiftCount (std::vector<Beat> b, double t, int delta)
    {
        const int i = nearestBeat (b, t);
        if (i < 0) return b;
        for (int k = i; k < (int) b.size(); ++k)
            b[(size_t) k].beatInBar = (((b[(size_t) k].beatInBar - 1 - delta) % 4) + 4) % 4 + 1;
        return b;
    }

    /** Lets the grid follow the music's own tempo changes (produced speed-ups / slow-downs): from the beat nearest
        `anchor`, each next beat goes on the strongest kick near where it's expected, and the tempo follows what it
        finds (where there's no kick, e.g. a breakdown, it carries on at the tempo it had). `flux` = low-frequency onset
        strength every `hop` seconds (track time = grid time + `offset`). forwardOnly keeps the beats before `anchor`. */
    inline std::vector<Beat> followKicks (const std::vector<Beat>& b, const std::vector<float>& flux, double hop, double offset,
                                          double anchor, double lengthSec, bool forwardOnly)
    {
        if (b.size() < 2 || flux.size() < 16) return b;
        double mean = 0.0, sq = 0.0;
        for (float v : flux) { mean += v; sq += (double) v * v; }
        mean /= (double) flux.size();
        const double sd = std::sqrt (juce::jmax (0.0, sq / (double) flux.size() - mean * mean));
        const double strong = mean + 1.2 * sd;
        auto peakNear = [&] (double t, double win, double& best) -> double
        {
            const int i0 = juce::jmax (0, (int) ((t + offset - win) / hop)), i1 = juce::jmin ((int) flux.size() - 1, (int) ((t + offset + win) / hop));
            int bi = -1; best = 0.0;
            for (int i = i0; i <= i1; ++i)
                if (flux[(size_t) i] > best) { best = flux[(size_t) i]; bi = i; }
            if (bi < 0) return t;
            // the kick starts where its rise begins, not at the top of it
            int on = bi;
            while (on > i0 && flux[(size_t) on - 1] > 0.25 * flux[(size_t) bi]) --on;
            return on * hop - offset;
        };

        const int a = nearestBeat (b, anchor);
        double p0 = juce::jlimit (0.2, 2.0, b[(size_t) juce::jmin (a + 1, (int) b.size() - 1)].timeSec - b[(size_t) juce::jmax (0, a == (int) b.size() - 1 ? a - 1 : a)].timeSec);
        std::vector<Beat> fwd, back;
        auto walk = [&] (int dir, std::vector<Beat>& out)
        {
            double t = b[(size_t) a].timeSec, period = p0;
            int bib = b[(size_t) a].beatInBar;
            for (int guard = 0; guard < 100000; ++guard)
            {
                const double expect = t + dir * period;
                if (expect < -period * 0.5 || expect > lengthSec + period) break;
                double strength = 0.0;
                const double found = peakNear (expect, period * 0.12, strength);
                const double next = strength > strong ? found : expect;
                const double step = std::abs (next - t);
                if (strength > strong && step > period * 0.85 && step < period * 1.15)
                    period = period * 0.75 + step * 0.25;   // the tempo follows the kicks, smoothly
                t = dir > 0 ? t + juce::jlimit (period * 0.85, period * 1.15, next - t) : t - juce::jlimit (period * 0.85, period * 1.15, t - next);
                bib = dir > 0 ? bib % 4 + 1 : (bib + 2) % 4 + 1;
                if (t < 0.0) break;
                out.push_back ({ t, (float) (60.0 / period), bib });
            }
        };
        walk (1, fwd);
        std::vector<Beat> res;
        if (forwardOnly) { for (int k = 0; k <= a; ++k) res.push_back (b[(size_t) k]); }
        else
        {
            walk (-1, back);
            for (auto it = back.rbegin(); it != back.rend(); ++it) res.push_back (*it);
            res.push_back (b[(size_t) a]);
        }
        for (auto& x : fwd) res.push_back (x);
        // each beat's tempo from the gap to the next one
        for (size_t k = 0; k + 1 < res.size(); ++k)
        {
            const double gap = res[k + 1].timeSec - res[k].timeSec;
            if (gap > 0.1) res[k].bpm = (float) (60.0 / gap);
        }
        return res;
    }

    inline std::vector<Beat> halve (const std::vector<Beat>& b, double anchor, double len, double fallbackBpm)
    {
        return setBpm (b, bpmAt (b, anchor, fallbackBpm) * 0.5, anchor, len);
    }

    inline std::vector<Beat> doubleTempo (const std::vector<Beat>& b, double anchor, double len, double fallbackBpm)
    {
        return setBpm (b, bpmAt (b, anchor, fallbackBpm) * 2.0, anchor, len);
    }
}

} // namespace orbit
