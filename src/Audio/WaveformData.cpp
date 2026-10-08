#include "WaveformData.h"
#include <cmath>

namespace orbit
{
namespace
{
    uint8_t toByte (float v) noexcept { return (uint8_t) juce::jlimit (0, 255, (int) std::lround (v * 255.0f)); }

    /** Simple biquad (RBJ cookbook), enough for drawing-quality band splitting. */
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        static Biquad lowpass (double sr, double f, double q = 0.707)
        {
            const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
            const double a0 = 1 + al;
            Biquad b;
            b.b0 = (float) ((1 - c) / 2 / a0); b.b1 = (float) ((1 - c) / a0); b.b2 = b.b0;
            b.a1 = (float) (-2 * c / a0); b.a2 = (float) ((1 - al) / a0);
            return b;
        }
        static Biquad highpass (double sr, double f, double q = 0.707)
        {
            const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
            const double a0 = 1 + al;
            Biquad b;
            b.b0 = (float) ((1 + c) / 2 / a0); b.b1 = (float) (-(1 + c) / a0); b.b2 = b.b0;
            b.a1 = (float) (-2 * c / a0); b.a2 = (float) ((1 - al) / a0);
            return b;
        }
        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    template <typename GetSample>
    WaveformData::Ptr analyse (int64_t numFrames, double sampleRate, GetSample&& get)
    {
        WaveformData::Ptr w = new WaveformData();
        w->style = WaveformData::Style::threeBand;
        w->entriesPerSecond = 150.0;

        const double perCol = sampleRate / w->entriesPerSecond;
        const int cols = (int) std::ceil ((double) numFrames / perCol);
        if (cols <= 0) return w;

        std::vector<float> lo ((size_t) cols), mi ((size_t) cols), hi ((size_t) cols);

        auto lp1 = Biquad::lowpass (sampleRate, 180.0), lp2 = Biquad::lowpass (sampleRate, 180.0);
        auto hp1 = Biquad::highpass (sampleRate, 2800.0), hp2 = Biquad::highpass (sampleRate, 2800.0);

        int64_t i = 0;
        for (int c = 0; c < cols; ++c)
        {
            const auto end = std::min<int64_t> (numFrames, (int64_t) ((c + 1) * perCol));
            float sl = 0, sm = 0, sh = 0;
            int n = 0;
            for (; i < end; ++i, ++n)
            {
                const float x = get (i);
                const float l = lp2.process (lp1.process (x));
                const float h = hp2.process (hp1.process (x));
                const float m = x - l - h;
                sl += l * l; sm += m * m; sh += h * h;
            }
            const float k = n > 0 ? 1.0f / (float) n : 0.0f;
            lo[(size_t) c] = std::sqrt (sl * k);
            mi[(size_t) c] = std::sqrt (sm * k);
            hi[(size_t) c] = std::sqrt (sh * k);
        }

        // Normalise against the loud parts of the track so quiet tracks still fill the lane.
        std::vector<float> ref (lo.size());
        for (size_t c = 0; c < ref.size(); ++c)
            ref[c] = std::max (lo[c], mi[c]);
        auto sorted = ref;
        const size_t q = (size_t) (0.995 * (double) (sorted.size() - 1));
        std::nth_element (sorted.begin(), sorted.begin() + (long) q, sorted.end());
        const float norm = sorted[q] > 1.0e-6f ? 0.92f / sorted[q] : 1.0f;

        w->low.resize ((size_t) cols);
        w->mid.resize ((size_t) cols);
        w->high.resize ((size_t) cols);
        for (size_t c = 0; c < (size_t) cols; ++c)
        {
            w->low[c] = toByte (lo[c] * norm);
            w->mid[c] = toByte (mi[c] * norm * 1.05f);
            w->high[c] = toByte (hi[c] * norm * 1.6f);
        }
        return w;
    }
}

WaveformData::Ptr WaveformData::fromAnlz (const AnlzWaveforms& src, Style preferred)
{
    WaveformData::Ptr w = new WaveformData();
    w->fromRekordbox = true;
    w->entriesPerSecond = 150.0;

    auto have3 = ! src.detail3Band.empty();
    auto haveRgb = ! src.detailRgb.empty();
    auto haveBlue = ! src.detailMono.empty();

    Style style = preferred;
    if (style == Style::threeBand && ! have3) style = haveRgb ? Style::rgb : Style::blue;
    if (style == Style::rgb && ! haveRgb) style = have3 ? Style::threeBand : Style::blue;
    if (style == Style::blue && ! haveBlue) style = have3 ? Style::threeBand : Style::rgb;
    w->style = style;

    if (style == Style::threeBand && have3)
    {
        const size_t n = src.detail3Band.size() / 3;
        w->low.resize (n); w->mid.resize (n); w->high.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const float m = src.detail3Band[i * 3] / 127.0f;
            const float h = src.detail3Band[i * 3 + 1] / 127.0f;
            const float l = src.detail3Band[i * 3 + 2] / 127.0f;
            w->low[i] = toByte (l * 1.0f);
            w->mid[i] = toByte (m * 0.82f);
            w->high[i] = toByte (h * 0.42f);
        }
    }
    else if (style == Style::rgb && haveRgb)
    {
        const size_t n = src.detailRgb.size() / 2;
        w->height.resize (n); w->colour.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const int bits = (src.detailRgb[i * 2] << 8) | src.detailRgb[i * 2 + 1];
            const int r = (bits >> 13) & 7, b = (bits >> 10) & 7, g = (bits >> 7) & 7;
            w->height[i] = toByte (((bits >> 2) & 0x1f) / 31.0f);
            w->colour[i] = juce::Colour ((uint8_t) (r * 255 / 7), (uint8_t) (g * 255 / 7), (uint8_t) (b * 255 / 7)).getARGB();
        }
    }
    else if (haveBlue)
    {
        static const juce::uint32 blues[8] = { 0xff006890, 0xff0088b0, 0xff00a8e8, 0xff00b8d8,
                                               0xff78b8d8, 0xff88c0e8, 0xff88c0e8, 0xffc8e0e8 };
        const size_t n = src.detailMono.size();
        w->height.resize (n); w->colour.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const auto v = src.detailMono[i];
            w->height[i] = toByte ((v & 0x1f) / 31.0f);
            w->colour[i] = blues[(v >> 5) & 7];
        }
    }
    return w;
}

WaveformData::Ptr WaveformData::fromAudio (const TrackAudio& a)
{
    const float* l = a.left.data();
    const float* r = a.right.data();
    return analyse (a.numFrames, a.sampleRate, [l, r] (int64_t i) { return 0.5f * (l[i] + r[i]); });
}

WaveformData::Ptr WaveformData::fromStem (const StemAudio& s, int stem)
{
    const auto* l = s.channels[(size_t) stem * 2].data();
    const auto* r = s.channels[(size_t) stem * 2 + 1].data();
    return analyse (s.numFrames, s.sampleRate, [l, r] (int64_t i) { return 0.5f * StemAudio::toFloat * (float) (l[i] + r[i]); });
}

} // namespace orbit
