#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <utility>

namespace orbit
{

/** Maps track time to "display time" for the edited view: the track as it will actually play - skipped sections
    folded out, rearranged phrases in their new order, repeated phrases shown again. Built from the play map (the
    stretches of the track in play order) minus the skips. Empty = straight through (display time = track time).

    A stretch of the track can appear more than once (a repeat), so going from track time to display time needs to
    know which pass: the one nearest `reference` (set it to the display time of the playhead) is used. */
struct TimeFold
{
    struct Piece { double src0, src1, disp0; int segment; };
    std::vector<Piece> pieces;
    int version = 0;
    double reference = 0.0;

    bool isEmpty() const noexcept { return pieces.empty(); }

    /** Skips only (track played straight through). */
    void set (std::vector<std::pair<double, double>> skips)
    {
        set ({}, std::move (skips), (std::numeric_limits<double>::max)() / 4);
    }

    /** segments: (start, end) stretches of the track in play order (empty = all of it); skips: (from, to). */
    void set (const std::vector<std::pair<double, double>>& segments, std::vector<std::pair<double, double>> skips, double length)
    {
        std::sort (skips.begin(), skips.end());
        std::vector<std::pair<double, double>> sk;
        for (auto& k : skips)
        {
            if (k.second <= k.first + 0.005) continue;
            if (! sk.empty() && k.first < sk.back().second) sk.back().second = std::max (sk.back().second, k.second);
            else sk.push_back (k);
        }
        pieces.clear();
        if (! segments.empty() || ! sk.empty())
        {
            auto segs = segments;
            if (segs.empty()) segs.push_back ({ 0.0, length });
            double d = 0.0;
            for (int i = 0; i < (int) segs.size(); ++i)
            {
                double a = segs[(size_t) i].first;
                const double b = segs[(size_t) i].second;
                for (auto& k : sk)
                {
                    if (k.second <= a || k.first >= b) continue;
                    if (k.first > a) { pieces.push_back ({ a, k.first, d, i }); d += k.first - a; }
                    a = std::max (a, k.second);
                }
                if (b > a) { pieces.push_back ({ a, b, d, i }); d += b - a; }
            }
            // Straight through with no skips: nothing to fold.
            if (pieces.size() == 1 && pieces[0].src0 <= 0.0 && segments.empty() && sk.empty()) pieces.clear();
        }
        ++version;
    }

    /** Display time of track time t: the pass nearest `reference` (or in `segment` when that's given). */
    double toDisplay (double t, int segment = -1) const noexcept
    {
        if (pieces.empty()) return t;
        double best = 0.0, bestDist = (std::numeric_limits<double>::max)();
        bool found = false;
        for (auto& p : pieces)
        {
            if (t >= p.src0 - 1.0e-6 && t <= p.src1 + 1.0e-6)
            {
                const double d = p.disp0 + (t - p.src0);
                if (segment >= 0 && p.segment == segment) return d;
                const double dist = std::abs (d - reference);
                if (dist < bestDist) { bestDist = dist; best = d; found = true; }
            }
        }
        if (found) return best;
        // Not shown (skipped / outside the plan): where it would sit - just after the piece before it.
        for (auto it = pieces.rbegin(); it != pieces.rend(); ++it)
            if (it->src1 <= t) return it->disp0 + (it->src1 - it->src0);
        return 0.0;
    }

    /** Every display time where track time t shows (repeats show more than once). */
    std::vector<double> allDisplay (double t) const
    {
        std::vector<double> r;
        if (pieces.empty()) { r.push_back (t); return r; }
        for (auto& p : pieces)
            if (t >= p.src0 - 1.0e-6 && t < p.src1 - 1.0e-6) r.push_back (p.disp0 + (t - p.src0));
        return r;
    }

    double toSource (double d) const noexcept
    {
        if (pieces.empty()) return d;
        if (d < 0.0) return pieces.front().src0 + d;
        for (auto& p : pieces)
            if (d < p.disp0 + (p.src1 - p.src0)) return p.src0 + (d - p.disp0);
        const auto& l = pieces.back();
        return l.src1 + (d - l.disp0 - (l.src1 - l.src0));
    }

    /** True when t isn't played (inside a skip, or left out of the plan). */
    bool hidden (double t) const noexcept
    {
        if (pieces.empty()) return false;
        for (auto& p : pieces)
            if (t > p.src0 - 1.0e-6 && t < p.src1 + 1.0e-6) return false;
        return true;
    }

    /** Length of the display timeline for a track of this length. */
    double displayLength (double sourceLength) const noexcept
    {
        if (pieces.empty()) return sourceLength;
        double sum = 0.0;
        for (auto& p : pieces) sum += std::min (p.src1, sourceLength) - std::min (p.src0, sourceLength);
        return sum;
    }

    /** Where display time d jumps (a piece boundary where the track time isn't continuous) - for drawing seams. */
    std::vector<double> seams() const
    {
        std::vector<double> r;
        for (size_t i = 1; i < pieces.size(); ++i)
            if (std::abs (pieces[i].src0 - pieces[i - 1].src1) > 0.01) r.push_back (pieces[i].disp0);
        return r;
    }
};

} // namespace orbit
