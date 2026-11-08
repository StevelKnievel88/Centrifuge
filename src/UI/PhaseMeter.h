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

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        m.addSectionHeader ("Phrase meter");
        for (int b : { 4, 8, 16, 32, 64 })
            m.addItem (b, juce::String (b) + " bars", true, ! followPhrases && phraseBars == b);
        m.addItem (100, "Follow the track's phrases (length and start from the phrase playing)", true, followPhrases);
        m.addSeparator();
        m.addItem (200, "Show bar brackets (4 / 16 / 32 / 64 bars)", true, brackets);
        juce::Component::SafePointer<PhaseMeter> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            int bars = safe->phraseBars;
            bool follow = safe->followPhrases, br = safe->brackets;
            if (r == 100) follow = ! follow;
            else if (r == 200) br = ! br;
            else { bars = r; follow = false; }
            safe->setOptions (bars, follow, br);
            if (safe->onOptionsChanged) safe->onOptionsChanged (bars, follow, br);
        });
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const int rows = prepOnly ? 1 : 2;
        const float band = brackets ? (float) bracketLevels (maxBarsShown()) * bracketStep + 2.0f : 0.0f;
        const float rowH = juce::jmax (4.0f, (r.getHeight() - 4.0f * (float) (rows - 1) - band * (float) rows) / (float) rows);
        if (prepOnly)
        {
            auto rr = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), band + rowH));
            paintRow (g, AudioEngine::prepDeck, rr.removeFromBottom (rowH), band);
            return;
        }
        for (int d = 0; d < 2; ++d)
        {
            r.removeFromTop (band);
            paintRow (g, d, r.removeFromTop (rowH), band);
            r.removeFromTop (4.0f);
        }
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
        bars = phraseBars;
        auto t = engine.deck (d).getLoadedTrack();
        if (t == nullptr || ! t->grid.isValid()) return -1.0;
        const auto& st = engine.deck (d).state();
        const double pos = st.positionSec.load();
        const auto& beats = t->grid.getBeats();

        // Following the track's phrases: count from the start of the phrase playing, over its length
        if (followPhrases && ! t->phrases.empty())
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

    void paintRow (juce::Graphics& g, int d, juce::Rectangle<float> r, float band)
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

        int bars = phraseBars;
        const double b = beatPosition (d, bars);
        // up to 16 bars: a block per beat; longer phrases: a block per bar (so they still fit)
        const bool perBeat = bars <= 16;
        const int upb = perBeat ? 4 : 1;                 // blocks per bar
        const int n = bars * upb;
        const float gapUnit = perBeat ? 1.0f : 1.5f, gapBar = 3.0f, gap4 = 8.0f, gap16 = 11.0f;
        auto gapAfter = [&] (int i)   // gap after block i
        {
            const int barEnd = (i + 1) % upb == 0 ? (i + 1) / upb : -1;
            if (barEnd > 0 && barEnd % 16 == 0) return gap16;
            if (barEnd > 0 && barEnd % 4 == 0) return gap4;
            if (barEnd > 0 && perBeat) return gapBar;
            return gapUnit;
        };
        float gaps = 0.0f;
        for (int i = 0; i < n - 1; ++i) gaps += gapAfter (i);
        const float bw = juce::jmax (1.0f, (r.getWidth() - gaps) / (float) n);
        const double unitPos = b < 0.0 ? -1.0 : b / (double) (4 / upb);
        const int cur = unitPos < 0.0 ? -1 : (int) std::floor (unitPos) % n;
        const double frac = unitPos < 0.0 ? 0.0 : unitPos - std::floor (unitPos);

        std::vector<float> xs ((size_t) n);
        float x = r.getX();
        for (int i = 0; i < n; ++i)
        {
            xs[(size_t) i] = x;
            auto blk = juce::Rectangle<float> (x, r.getY(), bw, r.getHeight());
            juce::Colour c = juce::Colour (0xff1d1a2e);
            const int bar = i / upb;
            if (cur >= 0 && i < cur)
                c = (perBeat ? bar % 2 == 1 : (bar / 4) % 2 == 1) ? col.darker (0.9f) : col;   // alternate bars (or 4-bar groups) darker
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
            if (i < n - 1) x += gapAfter (i);
        }

        // Brackets: 4 bars just above the blocks, then 16, 32 and 64 above those (1-pixel lines)
        if (brackets && band > 0.0f)
        {
            int level = 0;
            for (int L : { 4, 16, 32, 64 })
            {
                if (L > bars) break;
                const float y = std::floor (r.getY() - 3.0f - (float) level * bracketStep) + 0.5f;
                g.setColour ((cur >= 0 ? col : Theme::textFaint).withAlpha (0.55f + 0.15f * (float) level));
                for (int b0 = 0; b0 < bars; b0 += L)
                {
                    const int i0 = b0 * upb, i1 = juce::jmin (n, (b0 + L) * upb) - 1;
                    const float xa = std::round (xs[(size_t) i0]) + 0.5f, xb = std::round (xs[(size_t) i1] + bw) - 0.5f;
                    g.drawHorizontalLine ((int) y, xa, xb);
                    g.drawVerticalLine ((int) xa, y, y + 3.0f);
                    g.drawVerticalLine ((int) xb, y, y + 3.0f);
                }
                ++level;
            }
        }
    }

    AudioEngine& engine;
public:
    /** The edited view's time map for a deck (or nullptr): the meter then counts beats as the track plays. */
    std::function<const TimeFold* (int deck)> foldFor;
private:
    std::array<int, 3> last { -2, -2, -2 }, lastFlash { -1, -1, -1 }, lastBars { 0, 0, 0 };
    bool prepOnly = false;
    int phraseBars = 16;
    bool followPhrases = false, brackets = false;
};

} // namespace orbit
