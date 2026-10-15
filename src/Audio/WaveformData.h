#pragma once
#include "TrackAudio.h"
#include "../Library/Anlz.h"
#include <juce_graphics/juce_graphics.h>

namespace orbit
{

/** Waveform columns ready for drawing, 150 per second of audio (rekordbox's resolution).
    Heights are 0..255 (255 = full half-height of the lane). */
struct WaveformData : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<WaveformData>;

    enum class Style { threeBand, rgb, blue };

    Style style = Style::threeBand;
    double entriesPerSecond = 150.0;
    bool fromRekordbox = false;

    // threeBand: per-band heights. rgb/blue: `height` plus a colour per column.
    std::vector<uint8_t> low, mid, high;
    std::vector<uint8_t> height;
    std::vector<juce::uint32> colour;

    int size() const noexcept { return (int) (style == Style::threeBand ? low.size() : height.size()); }
    double lengthSeconds() const noexcept { return size() / entriesPerSecond; }

    /** Builds from rekordbox analysis, choosing the requested style if present (falls back to whatever exists). */
    static Ptr fromAnlz (const AnlzWaveforms& w, Style preferred);

    /** Analyses audio into a 3-band waveform (used when there is no rekordbox analysis). */
    static Ptr fromAudio (const TrackAudio& audio);

    /** Same, for one stem's audio (used to draw stem lanes). */
    static Ptr fromStem (const StemAudio& stems, int stemIndex);

    /** Builds an RGB or single-colour waveform from a 3-band one (for tracks rekordbox hasn't analysed). */
    static Ptr deriveFrom3Band (const WaveformData& threeBand, Style target);
};

/** Fine detail for zooming in: the track's peak level 1200 times a second, relative to the loudest peak of
    the 150-per-second waveform column it falls in (255 = that column's peak). Multiplying a waveform column by
    this brings back the transients that 150 columns per second can't show. */
struct WaveformDetail : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<WaveformDetail>;
    static constexpr double perSecond = 1200.0;
    std::vector<uint8_t> shape;
    static Ptr fromAudio (const TrackAudio& audio);
};

/** Per-stem waveform (peak per column, 600 per second). Filled in progressively while stems are separated. */
struct StemWaveform : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<StemWaveform>;
    double entriesPerSecond = 600.0;
    int total = 0;
    std::array<std::vector<uint8_t>, 4> peak;
    std::atomic<int> computed { 0 };

    double lengthSeconds() const noexcept { return total / entriesPerSecond; }

    static Ptr create (const StemAudio& stems);
    /** Computes columns that are covered by finished stem audio. Returns true when more were added. */
    bool update (const StemAudio& stems);
};

/** rekordbox 3-band colours (from Deep Symmetry's reverse-engineering notes). */
namespace WaveColours
{
    const juce::Colour low (32, 83, 217);
    const juce::Colour lowAndMid (169, 107, 39);
    const juce::Colour mid (242, 170, 60);
    const juce::Colour high (255, 255, 255);
}

} // namespace orbit
