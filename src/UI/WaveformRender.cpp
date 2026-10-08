#include "WaveformRender.h"

namespace orbit
{

namespace
{
    inline juce::PixelARGB px (juce::Colour c) noexcept
    {
        return juce::PixelARGB (255, c.getRed(), c.getGreen(), c.getBlue());
    }
}

juce::Image renderWaveformStrip (const WaveformData& w, double firstEntry, double epp, int width, int height, bool overview)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, width), juce::jmax (1, height), true, juce::SoftwareImageType());
    const int n = w.size();
    if (n == 0 || height < 4) return img;

    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    const float half = (float) height * 0.5f;
    const float maxH = half - (overview ? 0.5f : 1.0f);

    const auto cLow = px (WaveColours::low), cMid = px (WaveColours::mid), cBoth = px (WaveColours::lowAndMid), cHigh = px (WaveColours::high);

    for (int x = 0; x < width; ++x)
    {
        const double e0 = firstEntry + x * epp;
        const double e1 = e0 + epp;
        int i0 = (int) std::floor (e0);
        int i1 = juce::jmax (i0 + 1, (int) std::floor (e1));
        if (i1 <= 0 || i0 >= n) continue;
        i0 = juce::jmax (0, i0);
        i1 = juce::jmin (n, i1);

        if (w.style == WaveformData::Style::threeBand)
        {
            int lo = 0, mi = 0, hi = 0;
            for (int i = i0; i < i1; ++i)
            {
                lo = juce::jmax (lo, (int) w.low[(size_t) i]);
                mi = juce::jmax (mi, (int) w.mid[(size_t) i]);
                hi = juce::jmax (hi, (int) w.high[(size_t) i]);
            }
            const float hl = lo / 255.0f * maxH, hm = mi / 255.0f * maxH, hh = hi / 255.0f * maxH;
            const float both = juce::jmin (hl, hm), outer = juce::jmax (hl, hm);
            const auto outerCol = hl > hm ? cLow : cMid;

            for (int y = 0; y < height; ++y)
            {
                const float d = std::abs ((float) y + 0.5f - half);
                if (d >= outer && d >= hh) continue;
                auto* p = reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y));
                if (d < hh)        *p = cHigh;
                else if (d < both) *p = cBoth;
                else               *p = outerCol;
            }
        }
        else
        {
            int hgt = 0;
            juce::uint32 col = 0xff2080ff;
            for (int i = i0; i < i1; ++i)
                if (w.height[(size_t) i] >= hgt) { hgt = w.height[(size_t) i]; col = w.colour[(size_t) i]; }
            const float hh = hgt / 255.0f * maxH;
            const auto c = juce::Colour (col);
            const auto pc = px (c), pcCore = px (c.brighter (0.35f));
            for (int y = 0; y < height; ++y)
            {
                const float d = std::abs ((float) y + 0.5f - half);
                if (d >= hh) continue;
                *reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y)) = d < hh * 0.35f ? pcCore : pc;
            }
        }
    }
    return img;
}

bool WaveformTileCache::configure (const WaveformData* w, double pixelsPerSecond, int height, float pixelScale)
{
    pixelScale = juce::jlimit (1.0f, 4.0f, pixelScale);
    if (w == key && std::abs (pixelsPerSecond - pps) < 1.0e-9 && height == h && std::abs (pixelScale - scale) < 0.01f)
        return false;
    scale = pixelScale;
    key = w;
    wave = const_cast<WaveformData*> (w);
    pps = pixelsPerSecond;
    h = height;
    tiles.clear();
    lastUsed.clear();
    return true;
}

const juce::Image& WaveformTileCache::tile (int index)
{
    lastUsed[index] = ++useCounter;
    auto it = tiles.find (index);
    if (it != tiles.end()) return it->second;

    // Keep a small working set (visible tiles plus a few either side).
    if (tiles.size() > 12)
    {
        int oldest = index;
        juce::uint32 oldestUse = std::numeric_limits<juce::uint32>::max();
        for (auto& [i, u] : lastUsed)
            if (u < oldestUse && tiles.count (i)) { oldest = i; oldestUse = u; }
        tiles.erase (oldest);
        lastUsed.erase (oldest);
    }

    // Rendered at physical resolution (sharp on high-DPI screens), 2px wider so tiles overlap without seams.
    const double physPps = pps * scale;
    const double entriesPerPixel = wave->entriesPerSecond / physPps;
    const int physTile = (int) std::lround (tileWidth * scale);
    const double firstEntry = (double) index * tileWidth / pps * wave->entriesPerSecond;
    return tiles[index] = renderWaveformStrip (*wave, firstEntry, entriesPerPixel, physTile + 2,
                                               (int) std::lround ((float) h * scale), false);
}

void WaveformTileCache::draw (juce::Graphics& g, juce::Rectangle<int> area, double centreTime, float playheadX)
{
    if (wave == nullptr || pps <= 0.0) return;

    const double globalLeft = centreTime * pps - (double) playheadX;   // global pixel at area.getX()
    const int firstTile = (int) std::floor (globalLeft / tileWidth);
    const int lastTile = (int) std::floor ((globalLeft + area.getWidth()) / tileWidth);
    const int maxTile = (int) std::ceil (wave->lengthSeconds() * pps / tileWidth);

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (area);

    for (int t = juce::jmax (0, firstTile); t <= juce::jmin (lastTile, maxTile); ++t)
    {
        const float x = (float) (area.getX() + (double) t * tileWidth - globalLeft);
        g.drawImageTransformed (tile (t), juce::AffineTransform::scale (1.0f / scale).translated (x, (float) area.getY()));
    }
}

} // namespace orbit
