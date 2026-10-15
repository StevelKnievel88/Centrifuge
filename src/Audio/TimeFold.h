#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>
#include <utility>

namespace orbit
{

/** Maps track time to "display time" with skipped sections folded out, so the scrolling waveform
    shows a track as it will actually play. Skips are (from, to) pairs in seconds of the source audio. */
struct TimeFold
{
    std::vector<std::pair<double, double>> skips;   // sorted by start, non-overlapping, to > from
    int version = 0;

    bool isEmpty() const noexcept { return skips.empty(); }

    void set (std::vector<std::pair<double, double>> s)
    {
        std::sort (s.begin(), s.end());
        skips.clear();
        for (auto& k : s)
        {
            if (k.second <= k.first + 0.005) continue;
            if (! skips.empty() && k.first < skips.back().second)
                skips.back().second = std::max (skips.back().second, k.second);
            else
                skips.push_back (k);
        }
        ++version;
    }

    double toDisplay (double t) const noexcept
    {
        double off = 0.0;
        for (auto& s : skips)
        {
            if (t >= s.second)      off += s.second - s.first;
            else if (t > s.first) { off += t - s.first; break; }
            else break;
        }
        return t - off;
    }

    double toSource (double d) const noexcept
    {
        double t = d;
        for (auto& s : skips)
        {
            if (t >= s.first) t += s.second - s.first;
            else break;
        }
        return t;
    }

    /** True when t lies inside a skipped section (and so isn't shown). */
    bool hidden (double t) const noexcept
    {
        for (auto& s : skips)
            if (t > s.first + 1.0e-6 && t < s.second - 1.0e-6) return true;
        return false;
    }

    double skippedTotal() const noexcept
    {
        double sum = 0.0;
        for (auto& s : skips) sum += s.second - s.first;
        return sum;
    }
};

} // namespace orbit
