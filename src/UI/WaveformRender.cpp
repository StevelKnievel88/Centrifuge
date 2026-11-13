#include "WaveformRender.h"

namespace orbit
{

namespace
{
    inline juce::PixelARGB px (juce::Colour c) noexcept
    {
        return juce::PixelARGB (255, c.getRed(), c.getGreen(), c.getBlue());
    }

    /** Vertical geometry for full (mirrored) or half waveforms. dist(y) is how far row y is from the baseline,
        and a column of height hh covers every row with dist < hh. */
    struct Geometry
    {
        int H, shape;
        float half, maxH;

        Geometry (int height, int s, bool overview) : H (height), shape (s)
        {
            half = (float) H * 0.5f;
            maxH = shape == StripOptions::full ? half - (overview ? 0.5f : 1.0f) : (float) H - 1.0f;
        }

        inline float dist (int y) const noexcept
        {
            const float yc = (float) y + 0.5f;
            if (shape == StripOptions::halfUp)   return (float) H - yc;
            if (shape == StripOptions::halfDown) return yc;
            return std::abs (yc - half);
        }

        /** Rows that can be touched by a column of height `top`. */
        inline juce::Range<int> rows (float top) const noexcept
        {
            if (shape == StripOptions::halfUp)   return { juce::jmax (0, (int) std::floor ((float) H - top - 1.0f)), H };
            if (shape == StripOptions::halfDown) return { 0, juce::jmin (H, (int) std::ceil (top + 1.0f)) };
            return { juce::jmax (0, (int) std::floor (half - top - 1.0f)), juce::jmin (H, (int) std::ceil (half + top + 1.0f)) };
        }
    };

    /** Works out which source entries a pixel column covers (through the fold, if any). */
    struct ColumnMap
    {
        const StripOptions& o;
        int n;

        inline double map (double e) const noexcept
        {
            return o.fold != nullptr && ! o.fold->isEmpty() ? o.fold->toSource (e / o.entriesPerSecond) * o.entriesPerSecond : e;
        }

        struct Col { int i0 = 0, i1 = 0; bool interpolate = false; int ia = 0; float f = 0.0f; double s0 = 0, s1 = 0; bool valid = false; };

        Col column (double d0, double epp) const noexcept
        {
            Col c;
            c.s0 = map (d0);
            c.s1 = map (d0 + epp);
            if (c.s1 <= 0.0 || c.s0 >= n) return c;
            if (c.s1 - c.s0 >= 1.0)
            {
                c.i0 = juce::jmax (0, (int) std::floor (c.s0));
                c.i1 = juce::jmin (n, juce::jmax (c.i0 + 1, (int) std::floor (c.s1)));
                c.valid = c.i1 > c.i0;
            }
            else
            {
                // Zoomed in past one entry per pixel: interpolate between entry centres.
                const double sc = map (d0 + epp * 0.5) - 0.5;
                c.ia = juce::jlimit (0, n - 1, (int) std::floor (sc));
                c.f = (float) juce::jlimit (0.0, 1.0, sc - std::floor (sc));
                if (sc < 0.0) c.f = 0.0f;
                c.interpolate = true;
                c.valid = true;
            }
            return c;
        }

        /** Fine-detail factor 0..1 (1 when no detail or zoomed out). */
        float detailFactor (const Col& c, double entriesPerSecond) const noexcept
        {
            if (o.detail == nullptr || o.detail->shape.empty() || c.s1 - c.s0 >= 2.0) return 1.0f;
            const double k = WaveformDetail::perSecond / entriesPerSecond;
            const int dn = (int) o.detail->shape.size();
            int j0 = (int) std::floor (c.s0 * k), j1 = juce::jmax (j0 + 1, (int) std::floor (c.s1 * k));
            j0 = juce::jlimit (0, dn - 1, j0);
            j1 = juce::jlimit (j0 + 1, dn, j1);
            int v = 0;
            for (int j = j0; j < j1; ++j) v = juce::jmax (v, (int) o.detail->shape[(size_t) j]);
            return v / 255.0f;
        }
    };

    template <typename Vec>
    inline float valueAt (const Vec& v, const ColumnMap::Col& c, int n) noexcept
    {
        if (c.interpolate)
        {
            const float a = v[(size_t) c.ia], b = v[(size_t) juce::jmin (n - 1, c.ia + 1)];
            return (a + (b - a) * c.f) / 255.0f;
        }
        int m = 0;
        for (int i = c.i0; i < c.i1; ++i) m = juce::jmax (m, (int) v[(size_t) i]);
        return m / 255.0f;
    }

    /** Single-colour waveform shaded by frequency content: darker where the sound is mostly bass, paler (towards
        white) where it is mostly highs - the single-colour version of what the RGB waveform does with hue. */
    juce::Colour shaded (juce::Colour tint, float low, float mid, float high)
    {
        const float sum = low + mid + high + 1.0e-4f;
        const float lowShare = low / sum, highShare = (high + 0.5f * mid) / sum;
        auto c = tint.withMultipliedBrightness (juce::jlimit (0.3f, 1.0f, 1.15f - 0.85f * lowShare));
        return c.interpolatedWith (juce::Colours::white, juce::jlimit (0.0f, 0.75f, (highShare - 0.25f) * 1.1f));
    }

}

juce::Image renderWaveformStrip (const WaveformData& w, double firstEntry, double epp, int width, int height, const StripOptions& o)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, width), juce::jmax (1, height), true, juce::SoftwareImageType());
    const int n = w.size();
    if (n == 0 || height < 4) return img;

    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    const Geometry geo (height, o.shape, o.overview);
    const ColumnMap cm { o, n };
    const bool single = ! o.tint.isTransparent();

    const auto cLow = px (WaveColours::low), cMid = px (WaveColours::mid), cBoth = px (WaveColours::lowAndMid), cHigh = px (WaveColours::high);

    for (int x = 0; x < width; ++x)
    {
        const auto c = cm.column (firstEntry + x * epp, epp);
        if (! c.valid) continue;
        const float detail = cm.detailFactor (c, w.entriesPerSecond);

        if (w.style == WaveformData::Style::threeBand)
        {
            const float lo = valueAt (w.low, c, n) * detail, mi = valueAt (w.mid, c, n) * detail, hi = valueAt (w.high, c, n) * detail;
            if (single)
            {
                // One colour in three shades, like rekordbox: the highs are pale (towards white), the mids the colour
                // itself, the bass deep and dark. Each band fills out to its own level, so where the bass reaches
                // furthest (kicks) the peaks are dark around a paler core, and without bass (breakdowns) it's pale.
                const float hl = lo * geo.maxH, hm = mi * geo.maxH, hhi = hi * geo.maxH;
                const float hh = juce::jmax (hl, hm, hhi);
                const auto pale = px (o.tint.interpolatedWith (juce::Colours::white, 0.62f));
                const auto mid = px (o.tint.interpolatedWith (juce::Colours::white, 0.18f));
                const auto deep = px (o.tint.withMultipliedSaturation (1.15f).withMultipliedBrightness (0.52f));
                const auto rr = geo.rows (hh);
                for (int y = rr.getStart(); y < rr.getEnd(); ++y)
                {
                    const float d = geo.dist (y);
                    if (d >= hh) continue;
                    // the band that reaches out here and is the "lowest" frequency to do so sets the shade
                    const auto* c = d < hl ? &deep : d < hm ? &mid : &pale;
                    if (d < hhi && d < hm && d < hl)
                        c = hl > hm * 1.15f ? &deep : &mid;   // inside all three: the bass / mid balance decides
                    if (d < hhi * 0.55f) c = hl > juce::jmax (hm, hhi) * 1.6f ? &mid : &pale;   // a pale core where the highs are
                    *reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y)) = *c;
                }
                continue;
            }
            const float hl = lo * geo.maxH, hm = mi * geo.maxH, hh = hi * geo.maxH;
            const float both = juce::jmin (hl, hm), outer = juce::jmax (hl, hm);
            const auto outerCol = hl > hm ? cLow : cMid;
            const auto rr = geo.rows (juce::jmax (outer, hh));
            for (int y = rr.getStart(); y < rr.getEnd(); ++y)
            {
                const float d = geo.dist (y);
                if (d >= outer && d >= hh) continue;
                auto* p = reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y));
                if (d < hh)        *p = cHigh;
                else if (d < both) *p = cBoth;
                else               *p = outerCol;
            }
        }
        else
        {
            const float hh = valueAt (w.height, c, n) * detail * geo.maxH;
            juce::uint32 col = 0xff2080ff;
            if (c.interpolate) col = w.colour[(size_t) (c.f < 0.5f ? c.ia : juce::jmin (n - 1, c.ia + 1))];
            else
            {
                int best = -1;
                for (int i = c.i0; i < c.i1; ++i)
                    if (w.height[(size_t) i] >= best) { best = w.height[(size_t) i]; col = w.colour[(size_t) i]; }
            }
            auto colour = juce::Colour (col);
            // rekordbox's RGB waveform: red = lows, green = mids, blue = highs
            if (single) colour = shaded (o.tint, colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
            const auto pc = px (colour), pcCore = px (colour.brighter (0.35f));
            const auto rr = geo.rows (hh);
            for (int y = rr.getStart(); y < rr.getEnd(); ++y)
            {
                const float d = geo.dist (y);
                if (d >= hh) continue;
                *reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y)) = d < hh * 0.35f ? pcCore : pc;
            }
        }
    }
    return img;
}

juce::Image renderStemStrip (const StemWaveform& w, int stem, juce::Colour colour, int validEntries,
                             double firstEntry, double epp, int width, int height, const StripOptions& o)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, width), juce::jmax (1, height), true, juce::SoftwareImageType());
    const int n = juce::jmin (w.total, validEntries);
    if (n <= 0 || height < 4) return img;

    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    const Geometry geo (height, o.shape, false);
    const ColumnMap cm { o, n };
    const auto& data = w.peak[(size_t) stem];
    auto body = juce::PixelARGB (215, colour.getRed(), colour.getGreen(), colour.getBlue());
    const auto bright = colour.brighter (0.45f);
    auto core = juce::PixelARGB (240, bright.getRed(), bright.getGreen(), bright.getBlue());
    body.premultiply();
    core.premultiply();

    for (int x = 0; x < width; ++x)
    {
        const auto c = cm.column (firstEntry + x * epp, epp);
        if (! c.valid) continue;
        const float hh = valueAt (data, c, n) * geo.maxH;
        const auto rr = geo.rows (hh);
        // high-passed: each column hollowed out in proportion to its own height, so the shape stays; turned fully
        // down: only its edge
        const float inner = o.outline ? hh - 1.6f : hh * o.hollow;
        for (int y = rr.getStart(); y < rr.getEnd(); ++y)
        {
            const float d = geo.dist (y);
            if (d >= hh || d < inner) continue;
            *reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y)) = o.outline || (o.hollow <= 0.0f && d < hh * 0.3f) ? core : body;
        }
    }
    return img;
}

bool WaveformTileCache::configure (const void* k, juce::int64 styleKey, double entriesPerSecond, double lengthSeconds,
                                   double pixelsPerSecond, int height, float pixelScale, Renderer r)
{
    pixelScale = juce::jlimit (1.0f, 4.0f, pixelScale);
    render = std::move (r);
    length = lengthSeconds;
    if (k == key && styleKey == style && std::abs (pixelsPerSecond - pps) < 1.0e-9 && height == h && std::abs (pixelScale - scale) < 0.01f)
        return false;
    key = k;
    style = styleKey;
    eps = entriesPerSecond;
    scale = pixelScale;
    pps = pixelsPerSecond;
    h = height;
    tiles.clear();
    return true;
}

const juce::Image& WaveformTileCache::tile (int index)
{
    const double firstEntry = (double) index * tileWidth / pps * eps;
    const double tileEntries = (double) tileWidth / pps * eps;

    auto it = tiles.find (index);
    if (it != tiles.end())
    {
        auto& t = it->second;
        const bool wasPartial = t.validAtRender < firstEntry + tileEntries;
        if (! wasPartial || validEntries <= t.validAtRender)
        {
            t.lastUsed = ++useCounter;
            return t.image;
        }
    }

    // Keep a small working set (visible tiles plus a few either side).
    if (tiles.size() > 12)
    {
        auto oldest = tiles.begin();
        for (auto i = tiles.begin(); i != tiles.end(); ++i)
            if (i->second.lastUsed < oldest->second.lastUsed) oldest = i;
        if (oldest->first != index) tiles.erase (oldest);
    }

    // Rendered at physical resolution (sharp on high-DPI screens), 2px wider so tiles overlap without seams.
    const double entriesPerPixel = eps / (pps * scale);
    const int physTile = (int) std::lround (tileWidth * scale);
    auto& t = tiles[index];
    t.image = render (firstEntry, entriesPerPixel, physTile + 2, (int) std::lround ((float) h * scale));
    t.validAtRender = validEntries;
    t.lastUsed = ++useCounter;
    return t.image;
}

void WaveformTileCache::draw (juce::Graphics& g, juce::Rectangle<int> area, double centreTime, float playheadX, float opacity)
{
    if (render == nullptr || pps <= 0.0 || opacity <= 0.01f) return;

    const double globalLeft = centreTime * pps - (double) playheadX;   // global pixel at area.getX()
    const int firstTile = (int) std::floor (globalLeft / tileWidth);
    const int lastTile = (int) std::floor ((globalLeft + area.getWidth()) / tileWidth);
    const int maxTile = (int) std::ceil (length * pps / tileWidth);

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (area);
    g.setOpacity (opacity);

    for (int t = juce::jmax (0, firstTile); t <= juce::jmin (lastTile, maxTile); ++t)
    {
        const float x = (float) (area.getX() + (double) t * tileWidth - globalLeft);
        g.drawImageTransformed (tile (t), juce::AffineTransform::scale (1.0f / scale).translated (x, (float) area.getY()));
    }
}

} // namespace orbit
