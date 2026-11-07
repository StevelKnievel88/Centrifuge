#pragma once
#include "../Library/TrackInfo.h"
#include <cmath>

namespace orbit
{

/** Beat lookup over rekordbox's beat grid (times in seconds of the source audio). Read-only after creation. */
class BeatGrid
{
public:
    BeatGrid() = default;

    BeatGrid (std::vector<Beat> b, double fallbackBpm, double trackLengthSec)
        : beats (std::move (b))
    {
        // No analysed grid but a known BPM: make a straight grid from 0 so quantize/sync still work.
        if (beats.empty() && fallbackBpm > 20.0 && trackLengthSec > 0.0)
        {
            const double len = 60.0 / fallbackBpm;
            for (int i = 0; i * len < trackLengthSec + len; ++i)
                beats.push_back ({ i * len, (float) fallbackBpm, (i % 4) + 1 });
        }
    }

    bool isValid() const noexcept { return beats.size() >= 2; }
    const std::vector<Beat>& getBeats() const noexcept { return beats; }

    /** Index of the last beat at or before t (may be -1 before the first beat). */
    int beatIndexAt (double t) const noexcept
    {
        if (beats.empty()) return -1;
        auto it = std::upper_bound (beats.begin(), beats.end(), t, [] (double v, const Beat& b) { return v < b.timeSec; });
        return (int) (it - beats.begin()) - 1;
    }

    /** Length of the beat containing t, in seconds. Uses the grid's stored tempo: rekordbox rounds beat times
        to whole milliseconds, so the gap between two beats jitters (e.g. 121.21 / 120.97 for a 121 BPM track). */
    double beatLengthAt (double t) const noexcept
    {
        if (! isValid()) return 0.0;
        int i = juce::jlimit (0, (int) beats.size() - 2, beatIndexAt (t));
        if (beats[(size_t) i].bpm > 20.0f) return 60.0 / beats[(size_t) i].bpm;
        const double len = beats[(size_t) i + 1].timeSec - beats[(size_t) i].timeSec;
        return (len > 0.1 && len < 3.0) ? len : 0.5;
    }

    double bpmAt (double t) const noexcept
    {
        const double len = beatLengthAt (t);
        return len > 0.0 ? 60.0 / len : 0.0;
    }

    /** Continuous beat number at t (e.g. 12.25 = a quarter of the way through beat 12). Extrapolates outside the grid. */
    double beatPosition (double t) const noexcept
    {
        if (! isValid()) return 0.0;
        const auto n = (int) beats.size();
        int i = beatIndexAt (t);
        if (i < 0)
        {
            const double len = beats[1].timeSec - beats[0].timeSec;
            return (t - beats[0].timeSec) / len;
        }
        if (i >= n - 1)
        {
            const double len = beats[(size_t) n - 1].timeSec - beats[(size_t) n - 2].timeSec;
            return (n - 1) + (t - beats[(size_t) n - 1].timeSec) / len;
        }
        const double a = beats[(size_t) i].timeSec, b = beats[(size_t) i + 1].timeSec;
        return i + (t - a) / (b - a);
    }

    /** Inverse of beatPosition. */
    double timeAtBeat (double beat) const noexcept
    {
        if (! isValid()) return 0.0;
        const auto n = (int) beats.size();
        const int i = (int) std::floor (beat);
        if (i < 0)
            return beats[0].timeSec + beat * (beats[1].timeSec - beats[0].timeSec);
        if (i >= n - 1)
            return beats[(size_t) n - 1].timeSec + (beat - (n - 1)) * (beats[(size_t) n - 1].timeSec - beats[(size_t) n - 2].timeSec);
        const double a = beats[(size_t) i].timeSec, b = beats[(size_t) i + 1].timeSec;
        return a + (beat - i) * (b - a);
    }

    /** Nearest beat time to t. */
    double snap (double t) const noexcept
    {
        if (! isValid()) return t;
        return timeAtBeat (std::round (beatPosition (t)));
    }

    /** Calls fn (timeSec, beatInBar, beatIndex) for every beat from t0 to t1 - carried on at the first / last tempo
        before the track's first beat and after its last, so the grid also shows where the track is silent. */
    template <typename Fn>
    void forEachBeat (double t0, double t1, Fn&& fn) const
    {
        if (! isValid() || t1 < t0) return;
        const int n = (int) beats.size();
        const int k0 = (int) std::ceil (beatPosition (t0) - 1.0e-6), k1 = (int) std::floor (beatPosition (t1) + 1.0e-6);
        auto wrap = [] (int v) { return ((v % 4) + 4) % 4 + 1; };
        for (int k = k0; k <= k1 && k - k0 < 50000; ++k)
        {
            if (k >= 0 && k < n) fn (beats[(size_t) k].timeSec, beats[(size_t) k].beatInBar, k);
            else if (k < 0) fn (timeAtBeat (k), wrap (beats[0].beatInBar - 1 + k), k);
            else fn (timeAtBeat (k), wrap (beats[(size_t) n - 1].beatInBar - 1 + (k - (n - 1))), k);
        }
    }

    /** 1..4 position in the bar for the beat containing t. */
    int beatInBarAt (double t) const noexcept
    {
        const int i = beatIndexAt (t);
        return i >= 0 && i < (int) beats.size() ? beats[(size_t) i].beatInBar : 1;
    }

private:
    std::vector<Beat> beats;
};

} // namespace orbit
