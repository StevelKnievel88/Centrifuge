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
