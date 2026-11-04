#pragma once
#include "Theme.h"
#include "../Audio/AudioEngine.h"

namespace orbit
{

/** Both decks' phrase position, stacked (deck 1 above deck 2), as in Mirari's phrase meter:
    one block per beat over a 16-bar phrase, a small gap between bars and a wider one every 4 bars.
    Played blocks take the deck's colour (every other bar a shade darker); the current beat flashes. */
class PhaseMeter : public juce::Component, private juce::Timer
{
public:
    static constexpr int beatsPerPhrase = 64;

    explicit PhaseMeter (AudioEngine& e) : engine (e)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float rowH = juce::jmax (4.0f, (r.getHeight() - 4.0f) / 2.0f);
        paintRow (g, 0, r.removeFromTop (rowH));
        r.removeFromTop (4.0f);
        paintRow (g, 1, r.removeFromTop (rowH));
    }

private:
    void timerCallback() override
    {
        bool changed = false;
        for (int d = 0; d < 2; ++d)
        {
            const double b = beatPosition (d);
            const int cur = b < 0.0 ? -1 : (int) std::floor (b);
            const int flash = b < 0.0 ? 0 : (int) ((b - std::floor (b)) * 8.0);
            if (cur != last[(size_t) d] || flash != lastFlash[(size_t) d]) changed = true;
            last[(size_t) d] = cur;
            lastFlash[(size_t) d] = flash;
        }
        if (changed) repaint();
    }

    /** Beat number from the track's first bar (fractional), or -1 when there's nothing to show. */
    double beatPosition (int d) const
    {
        auto t = engine.deck (d).getLoadedTrack();
        if (t == nullptr || ! t->grid.isValid()) return -1.0;
        const double pos = engine.deck (d).state().positionSec.load();
        const auto& beats = t->grid.getBeats();
        const double b = t->grid.beatPosition (pos);
        if (b < 0.0) return -1.0;
        return b + (double) (beats.front().beatInBar - 1);   // count from a downbeat
    }

    void paintRow (juce::Graphics& g, int d, juce::Rectangle<float> r)
    {
        const auto col = Theme::deckColour (d);
        auto badge = r.removeFromLeft (r.getHeight() + 2.0f).withSizeKeepingCentre (r.getHeight(), r.getHeight());
        g.setColour (col);
        g.fillRect (badge);
        g.setColour (psyched::Colours::onSolid);
        g.setFont (psyched::Fonts::mono (juce::jmax (7.0f, r.getHeight() - 1.0f), true));
        g.drawText (juce::String (d + 1), badge, juce::Justification::centred);
        r.removeFromLeft (4.0f);

        const double b = beatPosition (d);
        const int n = beatsPerPhrase, bars = n / 4;
        const float gapBeat = 1.0f, gapBar = 3.0f, gap4 = 6.0f;
        const float gaps = (n - bars) * gapBeat + (bars - bars / 4) * gapBar + (bars / 4 - 1) * gap4;
        const float bw = juce::jmax (1.0f, (r.getWidth() - gaps) / (float) n);
        const int cur = b < 0.0 ? -1 : (int) std::floor (b) % n;
        const double frac = b < 0.0 ? 0.0 : b - std::floor (b);

        float x = r.getX();
        for (int i = 0; i < n; ++i)
        {
            auto blk = juce::Rectangle<float> (x, r.getY(), bw, r.getHeight());
            juce::Colour c = juce::Colour (0xff1d1a2e);
            if (cur >= 0 && i < cur)
                c = (i / 4) % 2 == 1 ? col.darker (0.9f) : col;   // every other bar darker
            if (i == cur)
            {
                const float flash = (float) std::exp (-frac * 6.0);
                juce::Path p;
                p.addRectangle (blk);
                psyched::paintGlow (g, p, col.withAlpha (0.4f + 0.6f * flash), (int) (3 + 8 * flash));
                c = col.brighter (0.4f + 0.8f * flash);
            }
            g.setColour (c);
            g.fillRect (blk);
            x += bw;
            if (i == n - 1) break;
            x += (i % 16 == 15) ? gap4 : (i % 4 == 3) ? gapBar : gapBeat;
        }
    }

    AudioEngine& engine;
    std::array<int, 2> last { -2, -2 }, lastFlash { -1, -1 };
};

} // namespace orbit
