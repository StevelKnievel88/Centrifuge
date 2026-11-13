#pragma once
#include "Theme.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/TimeFold.h"

namespace orbit
{

/** Both decks' phrase position, stacked (deck 1 above deck 2), as in Mirari's phrase meter:
    one block per beat over a 16-bar phrase, a small gap between bars and a wider one every 4 bars.
    Played blocks take the deck's colour (every other bar a shade darker); the current beat flashes. */
class PhaseMeter : public juce::Component, private juce::Timer
{
public:
    explicit PhaseMeter (AudioEngine& e) : engine (e)
    {
        startTimerHz (30);
    }

    /** On the PREP page the meter shows just PREP's deck. */
    void setPrepOnly (bool b) { if (b != prepOnly) { prepOnly = b; repaint(); } }

    /** Phrase length in bars (4 - 64), or follow the track's phrases (length and start from the phrase playing);
        brackets = stacked 4 / 16 / 32 / 64-bar brackets above the meter. */
    void setOptions (int bars, bool follow, bool showBrackets)
    {
        phraseBars = juce::jlimit (4, 64, bars);
        followPhrases = follow;
        brackets = showBrackets;
        repaint();
    }
    /** Right-click changed the options. */
    std::function<void (int bars, bool follow, bool brackets)> onOptionsChanged;
    /** Number of bracket levels shown for a phrase length (for the top bar's height). */
    static int bracketLevels (int bars) { int n = 0; for (int L : { 4, 16, 32, 64 }) if (L <= bars) ++n; return n; }
    static constexpr float bracketStep = 4.0f;

    void paint (juce::Graphics& g) override
    {
        // Always 32 bars of grey blocks (a block per bar); the phrase playing is outlined in the deck's colour over
        // as many bars as it lasts (an 8-bar phrase outlines the first quarter). No phrases: 16-bar phrases.
        auto r = getLocalBounds().toFloat();
        const float rowH = juce::jmax (4.0f, (r.getHeight() - 4.0f) / 2.0f);
        layoutBars = 32;
        if (prepOnly)
        {
            auto rr = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), rowH));
            paintRow (g, AudioEngine::prepDeck, rr);
            return;
        }
        paintRow (g, 0, r.removeFromTop (rowH));
        r.removeFromTop (4.0f);
        paintRow (g, 1, r.removeFromTop (rowH));
    }

private:
    int maxBarsShown() const { return followPhrases ? 64 : phraseBars; }

    void timerCallback() override
    {
        bool changed = false;
        for (int d = 0; d < 3; ++d)
        {
            int bars = 0;
            const double b = beatPosition (d, bars);
            const int cur = b < 0.0 ? -1 : (int) std::floor (b);
            const int flash = b < 0.0 ? 0 : (int) ((b - std::floor (b)) * 8.0);
            if (cur != last[(size_t) d] || flash != lastFlash[(size_t) d] || bars != lastBars[(size_t) d]) changed = true;
            last[(size_t) d] = cur;
            lastFlash[(size_t) d] = flash;
            lastBars[(size_t) d] = bars;
        }
        if (changed) repaint();
    }

    /** Beat number within the phrase (fractional), or -1 when there's nothing to show; `bars` = the phrase's length. */
    double beatPosition (int d, int& bars) const
    {
        bars = 16;
        auto t = engine.deck (d).getLoadedTrack();
        if (t == nullptr || ! t->grid.isValid()) return -1.0;
        const auto& st = engine.deck (d).state();
        const double pos = st.positionSec.load();
        const auto& beats = t->grid.getBeats();

        // Following the track's phrases: count from the start of the phrase playing, over its length
        if (! t->phrases.empty())
        {
            const auto& ph = t->phrases;
            for (size_t i = 0; i < ph.size(); ++i)
            {
                const double s0 = ph[i].startSec;
                const double s1 = i + 1 < ph.size() ? ph[i + 1].startSec : (ph[i].endSec > s0 ? ph[i].endSec : st.lengthSec.load());
                if (pos < s0 || pos >= s1) continue;
                const double b0 = t->grid.beatPosition (s0), b1 = t->grid.beatPosition (s1);
                bars = juce::jlimit (1, 64, (int) std::lround ((b1 - b0) / 4.0));
                return juce::jmax (0.0, t->grid.beatPosition (pos) - b0);
            }
        }

        double b = t->grid.beatPosition (pos);
        // Edited view: count the beats as the track plays (rearranged / repeated phrases, skips left out)
        if (foldFor)
            if (auto* f = foldFor (d); f != nullptr && ! f->isEmpty())
            {
                const double disp = f->toDisplay (pos, st.playSegment.load());
                double sum = t->grid.beatPosition (f->pieces.front().src0);
                for (auto& p : f->pieces)
                {
                    const double len = p.src1 - p.src0;
                    if (disp >= p.disp0 + len) { sum += t->grid.beatPosition (p.src1) - t->grid.beatPosition (p.src0); continue; }
                    if (disp >= p.disp0) sum += t->grid.beatPosition (p.src0 + (disp - p.disp0)) - t->grid.beatPosition (p.src0);
                    break;
                }
                b = sum;
            }
        if (b < 0.0) return -1.0;
        return b + (double) (beats.front().beatInBar - 1);   // count from a downbeat
    }

    struct Layout { std::vector<float> xs; float bw = 0.0f; int n = 0, upb = 1, bars = 16, cur = -1; juce::Colour col; };

    /** Brackets for 4 / 16 / 32 / 64 bars (1-pixel lines). Between the meters they're stacked from `top` down with
        ticks pointing both ways; above PREP's meter they stack upwards from `top` with ticks pointing down. */
    void paintBrackets (juce::Graphics& g, const Layout& lay, float top, bool between)
    {
        if (lay.n <= 0) return;
        int level = 0;
        for (int L : { 4, 16, 32, 64 })
        {
            if (L > lay.bars) break;
            const float y = between ? std::floor (top + 1.0f + (float) level * bracketStep) + 0.5f
                                    : std::floor (top - (float) level * bracketStep) + 0.5f;
            g.setColour ((lay.cur >= 0 ? lay.col : Theme::textFaint).withAlpha (0.55f + 0.15f * (float) level));
            for (int b0 = 0; b0 < lay.bars; b0 += L)
            {
                const int i0 = b0 * lay.upb, i1 = juce::jmin (lay.n, (b0 + L) * lay.upb) - 1;
                const float xa = std::round (lay.xs[(size_t) i0]) + 0.5f, xb = std::round (lay.xs[(size_t) i1] + lay.bw) - 0.5f;
                g.drawHorizontalLine ((int) y, xa, xb);
                const float t0 = between ? y - 2.0f : y, t1 = y + (between ? 2.0f : 3.0f);
                g.drawVerticalLine ((int) xa, t0, t1);
                g.drawVerticalLine ((int) xb, t0, t1);
            }
            ++level;
        }
    }

    Layout paintRow (juce::Graphics& g, int d, juce::Rectangle<float> r)
    {
        const bool prep = d >= 2;
        const auto col = prep ? Theme::deckColour (2) : Theme::deckColour (d);
        auto badge = r.removeFromLeft (prep ? r.getHeight() * 2.6f : r.getHeight() + 2.0f).withSizeKeepingCentre (prep ? r.getHeight() * 2.4f : r.getHeight(), r.getHeight());
        g.setColour (col);
        g.fillRect (badge);
        g.setColour (psyched::Colours::onSolid);
        g.setFont (psyched::Fonts::mono (juce::jmax (7.0f, r.getHeight() - (prep ? 4.0f : 1.0f)), true));
        g.drawText (prep ? juce::String ("PREP") : juce::String (d + 1), badge.toNearestInt(), juce::Justification::centred);
        r.removeFromLeft (4.0f);

        int phrase = 16;
        const double b = beatPosition (d, phrase);
        const int bars = layoutBars;
        const int upb = 1;                               // a block per bar
        const int n = bars;
        const int inPhrase = juce::jlimit (1, n, phrase); // blocks the phrase covers
        const float gapUnit = 2.5f, gap4 = 7.0f, gap16 = 11.0f;
        auto gapAfter = [&] (int i)   // gap after block i
        {
            const int barEnd = i + 1;
            if (barEnd % 16 == 0) return gap16;
            if (barEnd % 4 == 0) return gap4;
            return gapUnit;
        };
        float gaps = 0.0f;
        for (int i = 0; i < n - 1; ++i) gaps += gapAfter (i);
        const float bw = juce::jmax (1.0f, (r.getWidth() - gaps) / (float) n);
        const double unitPos = b < 0.0 ? -1.0 : b / 4.0;
        const int cur = unitPos < 0.0 ? -1 : (int) std::floor (unitPos) % juce::jmax (1, phrase) % n;
        const double frac = b < 0.0 ? 0.0 : b - std::floor (b);   // flashes on every beat

        std::vector<float> xs ((size_t) n);
        float x = r.getX();
        for (int i = 0; i < n; ++i)
        {
            xs[(size_t) i] = x;
            auto blk = juce::Rectangle<float> (x, r.getY(), bw, r.getHeight());
            juce::Colour c = juce::Colour (0xff1d1a2e);
            const int bar = i / upb;
            if (cur >= 0 && i < cur)
                c = (bar / 4) % 2 == 1 ? col.darker (0.9f) : col;   // alternate 4-bar groups darker
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
            // the phrase's own blocks are outlined in the deck's colour; blocks past its end stay plain
            // (alternating light / darker with the bars, like the infill)
            if (i < inPhrase && i != cur)
            {
                g.setColour ((bar / 4) % 2 == 1 ? col.darker (0.9f) : col);
                g.drawRect (blk, 1.0f);
            }
            x += bw;
            if (i < n - 1) x += gapAfter (i);
        }

        Layout lay;
        lay.xs = std::move (xs);
        lay.bw = bw; lay.n = n; lay.upb = upb; lay.bars = bars; lay.cur = cur; lay.col = col;
        return lay;
    }

    AudioEngine& engine;
public:
    /** The edited view's time map for a deck (or nullptr): the meter then counts beats as the track plays. */
    std::function<const TimeFold* (int deck)> foldFor;
private:
    std::array<int, 3> last { -2, -2, -2 }, lastFlash { -1, -1, -1 }, lastBars { 0, 0, 0 };
    bool prepOnly = false;
    int phraseBars = 16;
    int layoutBars = 16;   // bars the meters are laid out for (following phrases: shared by both decks)
    bool followPhrases = false, brackets = false;
};

} // namespace orbit
