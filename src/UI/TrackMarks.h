#pragma once
#include "Theme.h"
#include "ColourPicker.h"
#include "../Audio/Deck.h"
#include "../Library/TrackVersion.h"

namespace orbit
{

/** Hot cues, locators, loops, skips and phrases of one track, in a form any overview can draw
    (deck overviews, PREP, the browser's preview column). Times in seconds of deck time. */
struct TrackMarks
{
    struct Mark { double start = 0.0, end = -1.0; juce::Colour colour; int kind = 0, mode = 0, hot = -1; };   // kind 0 locator, 1 hot cue, 2 skip
    std::vector<Mark> marks;
    std::vector<Phrase> phrases;
    double length = 0.0;

    bool operator== (const TrackMarks& o) const
    {
        if (marks.size() != o.marks.size() || phrases.size() != o.phrases.size() || std::abs (length - o.length) > 1.0e-6) return false;
        for (size_t i = 0; i < marks.size(); ++i)
        {
            const auto& a = marks[i]; const auto& b = o.marks[i];
            if (std::abs (a.start - b.start) > 1.0e-4 || std::abs (a.end - b.end) > 1.0e-4 || a.colour != b.colour || a.kind != b.kind || a.mode != b.mode) return false;
        }
        for (size_t i = 0; i < phrases.size(); ++i)
            if (std::abs (phrases[i].startSec - o.phrases[i].startSec) > 1.0e-4 || phrases[i].colour != o.phrases[i].colour || phrases[i].name != o.phrases[i].name) return false;
        return true;
    }

    static TrackMarks fromDeck (const Deck::Published& st, const std::vector<Phrase>& phrases)
    {
        TrackMarks m;
        m.length = st.lengthSec.load();
        m.phrases = phrases;
        for (int i = 0; i < juce::jmin (st.numLocators.load(), Deck::maxLocators); ++i)
            m.marks.push_back ({ st.locStart[(size_t) i].load(), st.locEnd[(size_t) i].load(), juce::Colour (st.locColour[(size_t) i].load()), 0,
                                 st.locMode[(size_t) i].load(), -1 });
        for (int i = 0; i < juce::jmin (st.numSkips.load(), Deck::maxSkips); ++i)
            m.marks.push_back ({ st.skipFrom[(size_t) i].load(), st.skipTo[(size_t) i].load(), juce::Colour (0xff38d9ff), 2, 0, -1 });
        for (int i = 0; i < 8; ++i)
            if (st.hotStart[(size_t) i].load() >= 0.0)
                m.marks.push_back ({ st.hotStart[(size_t) i].load(), st.hotEnd[(size_t) i].load(), juce::Colour (st.hotColour[(size_t) i].load()), 1, 0, i });
        return m;
    }

    static TrackMarks fromVersion (const TrackVersion& v, double length, const std::vector<Phrase>& rekordboxPhrases)
    {
        TrackMarks m;
        m.length = length;
        m.phrases = v.phrases ? *v.phrases : rekordboxPhrases;
        for (auto& c : v.memory)
            m.marks.push_back ({ c.startSec, c.endSec, c.colour.isTransparent() ? juce::Colour (0xffff2d2d) : c.colour, 0, c.isLoop() ? c.loopMode : 0, -1 });
        for (auto& k : v.skips)
            m.marks.push_back ({ k.first, k.second, juce::Colour (0xff38d9ff), 2, 0, -1 });
        for (size_t i = 0; i < 8; ++i)
            if (v.hot[i])
                m.marks.push_back ({ v.hot[i]->startSec, v.hot[i]->endSec, v.hot[i]->colour.isTransparent() ? juce::Colour (0xff28e214) : v.hot[i]->colour, 1, 0, (int) i });
        return m;
    }
};

/** Draws the marks over an overview waveform in `r`: a phrase strip along the bottom, locators as triangles
    pointing down from the top (loop locators: the line shows the mode - orange active, red cycle), skips as
    double arrows, and hot cues as triangles pointing up from the bottom in their colour. */
inline void paintOverviewMarks (juce::Graphics& g, juce::Rectangle<float> r, const TrackMarks& m, bool compact = false)
{
    if (m.length <= 0.0) return;
    auto xOf = [&] (double sec) { return r.getX() + (float) juce::jlimit (0.0, 1.0, sec / m.length) * r.getWidth(); };
    const float tri = compact ? 3.0f : 4.0f;

    // Phrases along the bottom
    float bottom = r.getBottom();
    if (! m.phrases.empty())
    {
        const float ph = juce::jmax (2.0f, juce::jmin (compact ? 3.0f : 6.0f, r.getHeight() * 0.14f));
        auto strip = juce::Rectangle<float> (r.getX(), r.getBottom() - ph, r.getWidth(), ph);
        for (size_t i = 0; i < m.phrases.size(); ++i)
        {
            const auto& p = m.phrases[i];
            const double end = i + 1 < m.phrases.size() ? m.phrases[i + 1].startSec : (p.endSec > p.startSec ? p.endSec : m.length);
            const float x0 = xOf (p.startSec), x1 = xOf (end);
            g.setColour (p.colour.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, strip.getY(), juce::jmax (x0 + 1.0f, x1 - 1.0f), strip.getBottom()));
        }
        bottom = strip.getY();
    }

    for (auto& k : m.marks)
    {
        const float x = xOf (k.start);
        if (k.kind == 2)
        {
            g.setColour (k.colour);
            juce::Path p;
            p.addTriangle (x, r.getY(), x, r.getY() + tri * 1.5f, x + tri * 1.2f, r.getY() + tri * 0.75f);
            p.addTriangle (x + tri, r.getY(), x + tri, r.getY() + tri * 1.5f, x + tri * 2.2f, r.getY() + tri * 0.75f);
            g.fillPath (p);
        }
        else if (k.kind == 0)
        {
            const bool loop = k.end > k.start;
            if (loop)
            {
                g.setColour (k.colour.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, r.getY(), juce::jmax (x + 1.0f, xOf (k.end)), bottom));
                if (k.mode != 0)
                {
                    g.setColour (juce::Colour (k.mode == 1 ? activeLoopLine : cycleLoopLine));
                    g.fillRect (x - 0.5f, r.getY(), 1.5f, bottom - r.getY());
                }
            }
            g.setColour (k.colour);
            juce::Path p;
            p.addTriangle (x - tri, r.getY(), x + tri, r.getY(), x, r.getY() + tri * 1.3f);
            g.fillPath (p);
        }
    }
    // Hot cues last: triangles pointing up from the bottom
    for (auto& k : m.marks)
        if (k.kind == 1)
        {
            const float x = xOf (k.start);
            juce::Path p;
            p.addTriangle (x - tri, bottom, x + tri, bottom, x, bottom - tri * 1.4f);
            g.setColour (k.colour);
            g.fillPath (p);
        }
}

/** Phrase bars under a scrolling waveform: coloured blocks with their names, `xOf` maps track time to x. */
inline void paintPhraseStrip (juce::Graphics& g, juce::Rectangle<float> strip, const std::vector<Phrase>& phrases, double length,
                              const std::function<float (double)>& xOf, int highlight = -1)
{
    if (phrases.empty()) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (strip.toNearestInt());
    g.setFont (psyched::Fonts::label (juce::jmin (10.0f, strip.getHeight() - 2.0f)));
    for (size_t i = 0; i < phrases.size(); ++i)
    {
        const auto& p = phrases[i];
        const double end = i + 1 < phrases.size() ? phrases[i + 1].startSec : (p.endSec > p.startSec ? p.endSec : length);
        const float x0 = xOf (p.startSec), x1 = xOf (end);
        if (x1 < strip.getX() || x0 > strip.getRight()) continue;
        auto b = juce::Rectangle<float>::leftTopRightBottom (x0, strip.getY(), juce::jmax (x0 + 1.0f, x1 - 1.0f), strip.getBottom());
        g.setColour (p.colour.withAlpha ((int) i == highlight ? 1.0f : 0.75f));
        g.fillRect (b);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        auto text = b.withTrimmedLeft (4.0f);
        if (text.getX() < strip.getX() + 4.0f && text.getRight() > strip.getX() + 40.0f) text.setLeft (strip.getX() + 4.0f);   // keep the name in view
        g.drawText (p.name.toUpperCase(), text, juce::Justification::centredLeft, true);
    }
}

} // namespace orbit
