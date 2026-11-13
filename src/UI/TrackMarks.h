#pragma once
#include "Theme.h"
#include "ColourPicker.h"
#include "../Audio/Deck.h"
#include "../Audio/TimeFold.h"
#include "../Library/TrackVersion.h"

namespace orbit
{

/** Height of an overview's phrase strip along the bottom (and the bar strip along the top when the grid is on). */
inline float overviewStripHeight (float overviewHeight, bool compact = false)
{
    return juce::jmax (2.0f, juce::jmin (compact ? 3.0f : 8.0f, overviewHeight * 0.18f));
}


/** Hot cues, locators, loops, skips and phrases of one track, in a form any overview can draw
    (deck overviews, PREP, the browser's preview column). Times in seconds of deck time. */
struct TrackMarks
{
    struct Mark { double start = 0.0, end = -1.0; juce::Colour colour; int kind = 0, mode = 0, hot = -1; };   // kind 0 locator, 1 hot cue, 2 skip, 3 stem locator (mode: which stems it turns on), 4 note
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

    static TrackMarks fromDeck (const Deck::Published& st, const std::vector<Phrase>& phrases, const LoadedTrack* t = nullptr)
    {
        TrackMarks m;
        if (t != nullptr) m.addEditMarks (t->stemLocs, t->notes);
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
        m.addEditMarks (v.stemLocs, v.notes);
        return m;
    }

    void addEditMarks (const std::vector<StemLocator>& stemLocs, const std::vector<NoteLocator>& notes)
    {
        for (auto& l : stemLocs)
        {
            int on = 0;
            for (int s = 0; s < 4; ++s) if (l.target[(size_t) s] != 0) on |= 1 << s;
            marks.push_back ({ l.timeSec, -1.0, juce::Colour (0xff7dffd6), 3, on, -1 });
        }
        for (auto& n : notes) marks.push_back ({ n.timeSec, -1.0, n.colour, 4, 0, -1 });
    }
};

/** Draws the marks over an overview waveform in `r`: a phrase strip along the bottom, locators as triangles
    pointing down from the top (loop locators: the line shows the mode - orange active, red cycle), skips as
    double arrows, stem locators as small squares, notes as dots, and hot cues as triangles pointing up from the
    bottom in their colour. With a fold (the edited view) everything is drawn where it plays - repeats more than once,
    skipped parts not at all. */
/** The part of the track the scrolling waveform shows, as a box on the overview (x0..x1): it stops above the phrase
    strip and is drawn before the marks so they stay bright; a 1-pixel outline in the deck's colour. */
inline void paintOverviewViewBox (juce::Graphics& g, juce::Rectangle<float> r, float x0, float x1, juce::Colour col, bool hasPhrases,
                                  float topStrip = 0.0f)
{
    const float h = r.getHeight();
    if (hasPhrases) r.removeFromBottom (overviewStripHeight (h) + 1.0f);
    if (topStrip > 0.0f) r.removeFromTop (topStrip + 1.0f);   // the bar grid's strip stays clear
    x0 = juce::jlimit (r.getX(), r.getRight(), x0);
    x1 = juce::jlimit (r.getX(), r.getRight(), x1);
    if (x1 - x0 < 2.0f) return;
    auto box = juce::Rectangle<float>::leftTopRightBottom (std::round (x0), r.getY(), std::round (x1), r.getBottom());
    g.setColour (col.withAlpha (0.14f));
    g.fillRect (box);
    g.setColour (col.withAlpha (0.9f));
    g.drawRect (box, 1.0f);
}

/** Size of the markers on overviews and browser previews (Settings > WAVEFORMS), 1 = standard. */
inline float& overviewMarkerScale() { static float s = 1.0f; return s; }

/** A marker shape with a thin dark outline, so it reads on bright waveforms. */
inline void fillOutlined (juce::Graphics& g, const juce::Path& p, juce::Colour c)
{
    g.setColour (c);
    g.fillPath (p);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

inline void paintOverviewMarks (juce::Graphics& g, juce::Rectangle<float> r, const TrackMarks& m, bool compact = false,
                                const TimeFold* fold = nullptr)
{
    if (m.length <= 0.0) return;
    const bool folded = fold != nullptr && ! fold->isEmpty();
    const double total = folded ? juce::jmax (0.001, fold->displayLength (m.length)) : m.length;
    auto xOfDisp = [&] (double d) { return r.getX() + (float) juce::jlimit (0.0, 1.0, d / total) * r.getWidth(); };
    auto xsOf = [&] (double sec)
    {
        std::vector<float> xs;
        if (! folded) xs.push_back (xOfDisp (sec));
        else for (auto d : fold->allDisplay (sec)) xs.push_back (xOfDisp (d));
        return xs;
    };
    const float tri = (compact ? 3.0f : 4.0f) * overviewMarkerScale();

    // Phrases along the bottom
    float bottom = r.getBottom();
    if (! m.phrases.empty())
    {
        const float ph = overviewStripHeight (r.getHeight(), compact);
        auto strip = juce::Rectangle<float> (r.getX(), r.getBottom() - ph, r.getWidth(), ph);
        auto block = [&] (double d0, double d1, juce::Colour c)
        {
            const float x0 = xOfDisp (d0), x1 = xOfDisp (d1);
            g.setColour (c.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, strip.getY(), juce::jmax (x0 + 1.0f, x1 - 1.0f), strip.getBottom()));
        };
        for (size_t i = 0; i < m.phrases.size(); ++i)
        {
            const auto& p = m.phrases[i];
            const double end = i + 1 < m.phrases.size() ? m.phrases[i + 1].startSec : (p.endSec > p.startSec ? p.endSec : m.length);
            if (! folded) { block (p.startSec, end, p.colour); continue; }
            for (auto& piece : fold->pieces)
            {
                const double a = juce::jmax (p.startSec, piece.src0), b = juce::jmin (end, piece.src1);
                if (b > a) block (piece.disp0 + (a - piece.src0), piece.disp0 + (b - piece.src0), p.colour);
            }
        }
        bottom = strip.getY();
    }

    // Edited view: where it jumps
    if (folded)
        for (auto seam : fold->seams())
        {
            g.setColour (Theme::accent.withAlpha (0.7f));
            g.fillRect (xOfDisp (seam) - 0.5f, r.getY(), 1.0f, bottom - r.getY());
        }

    for (auto& k : m.marks)
    {
        if (folded && k.kind == 2) continue;   // skips are folded out of the edited view
        for (const float x : xsOf (k.start))
        {
            if (k.kind == 2)
            {
                juce::Path p;
                p.addTriangle (x, r.getY(), x, r.getY() + tri * 1.5f, x + tri * 1.2f, r.getY() + tri * 0.75f);
                p.addTriangle (x + tri, r.getY(), x + tri, r.getY() + tri * 1.5f, x + tri * 2.2f, r.getY() + tri * 0.75f);
                fillOutlined (g, p, k.colour);
            }
            else if (k.kind == 0)
            {
                const bool loop = k.end > k.start;
                if (loop)
                {
                    const float xe = folded ? x + (float) ((k.end - k.start) / total) * r.getWidth() : xOfDisp (k.end);
                    g.setColour (k.colour.withAlpha (0.18f));
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, r.getY(), juce::jmax (x + 1.0f, xe), bottom));
                    if (k.mode != 0)
                    {
                        g.setColour (juce::Colour (k.mode == 1 ? activeLoopLine : cycleLoopLine));
                        g.fillRect (x - 0.5f, r.getY(), 1.5f, bottom - r.getY());
                    }
                }
                juce::Path p;
                p.addTriangle (x - tri, r.getY(), x + tri, r.getY(), x, r.getY() + tri * 1.3f);
                fillOutlined (g, p, k.colour);
            }
            else if (k.kind == 3)
            {
                // stem locator: a small square split into the four stem colours
                const float s = tri * 1.6f;
                for (int st = 0; st < 4; ++st)
                {
                    g.setColour (Theme::stemColour (st).withAlpha ((k.mode >> st) & 1 ? 1.0f : 0.25f));
                    g.fillRect (x - s * 0.5f + (float) (st % 2) * s * 0.5f, r.getY() + 1.0f + (float) (st / 2) * s * 0.5f, s * 0.5f, s * 0.5f);
                }
            }
            else if (k.kind == 4)
            {
                // note: a dot
                juce::Path p;
                p.addEllipse (x - tri * 0.7f, r.getY() + 1.0f, tri * 1.4f, tri * 1.4f);
                fillOutlined (g, p, k.colour);
            }
        }
    }
    // Hot cues last: triangles pointing up from the bottom
    for (auto& k : m.marks)
        if (k.kind == 1)
            for (const float x : xsOf (k.start))
            {
                juce::Path p;
                p.addTriangle (x - tri, bottom, x + tri, bottom, x, bottom - tri * 1.4f);
                fillOutlined (g, p, k.colour);
            }
}

/** A stem locator's icon for one stem: a filled square = on straight away, an X = off straight away, a ramp up
    (bottom-left to top-right, the triangle under it filled) = fades in, a ramp down = fades out, a faint outline = no change. */
inline void paintStemChangeIcon (juce::Graphics& g, juce::Rectangle<float> b, int stem, int target, bool fades)
{
    const auto c = Theme::stemColour (stem);
    if (target < 0) { g.setColour (c.withAlpha (0.35f)); g.drawRect (b, 1.0f); return; }
    g.setColour (c.withAlpha (0.9f));
    if (! fades)
    {
        if (target > 0) g.fillRect (b);
        else
        {
            g.drawRect (b, 1.0f);
            g.drawLine (b.getX(), b.getY(), b.getRight(), b.getBottom(), 1.2f);
            g.drawLine (b.getX(), b.getBottom(), b.getRight(), b.getY(), 1.2f);
        }
        return;
    }
    juce::Path tri;
    if (target > 0) tri.addTriangle (b.getX(), b.getBottom(), b.getRight(), b.getY(), b.getRight(), b.getBottom());   // fades in
    else tri.addTriangle (b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getBottom());                 // fades out
    g.fillPath (tri);
    g.drawRect (b, 1.0f);
}

/** A stem locator on a zoomed waveform: a line, a flag with the four stems' icons and, for each stem it changes,
    a line in the stem's colour - rising across the fade for a fade in, falling for a fade out (straight down for a cut).
    `area` is the waveform; with `stemRows` (stems a row each) each stem's line is drawn in its own row, otherwise the
    lines sit side by side across the whole height. */
inline void paintStemLocatorFlag (juce::Graphics& g, const StemLocator& l, float x, float flagTop, float pixelsPerSecond,
                                  juce::Rectangle<float> area, bool stemRows, bool withFlag = true)
{
    const auto mint = juce::Colour (0xff7dffd6);
    g.setColour (mint.withAlpha (0.6f));
    g.fillRect (x - 0.5f, area.getY(), 1.0f, area.getHeight());
    float longest = 0.0f;
    for (int s = 0; s < 4; ++s) if (l.target[(size_t) s] >= 0) longest = juce::jmax (longest, (float) l.fadeSecFor (s) * pixelsPerSecond);
    if (longest > 2.0f)
    {
        g.setColour (mint.withAlpha (0.06f));
        g.fillRect (juce::Rectangle<float> (x, area.getY(), longest, area.getHeight()));
    }
    int k = 0;
    for (int s = 0; s < 4; ++s)
    {
        const int t = l.target[(size_t) s];
        if (t < 0) continue;
        auto row = area;
        float off = 0.0f;
        if (stemRows)
        {
            const float rh = area.getHeight() / 4.0f;
            row = juce::Rectangle<float> (area.getX(), area.getY() + rh * (float) (3 - s), area.getWidth(), rh).reduced (0.0f, 2.0f);   // vocals top, drums bottom
        }
        else
            off = (float) k++ * 3.0f;   // side by side
        g.setColour (Theme::stemColour (s));
        const float fadeWidth = (float) l.fadeSecFor (s) * pixelsPerSecond;
        if (fadeWidth > 2.0f)
        {
            juce::Path ramp;
            const float y0 = t > 0 ? row.getBottom() : row.getY(), y1 = t > 0 ? row.getY() : row.getBottom();
            ramp.startNewSubPath (x + off, y0);
            ramp.lineTo (x + off + fadeWidth, y1);
            g.strokePath (ramp, juce::PathStrokeType (1.6f));
        }
        else
            g.fillRect (x + off - 0.75f, row.getY(), 1.5f, row.getHeight());
    }
    if (! withFlag) return;
    auto flag = juce::Rectangle<float> (x, flagTop, 4 * 10.0f + 4.0f, 12.0f);
    g.setColour (psyched::Colours::void_.withAlpha (0.85f));
    g.fillRect (flag);
    for (int s = 0; s < 4; ++s)
        paintStemChangeIcon (g, juce::Rectangle<float> (flag.getX() + 2.0f + (float) s * 10.0f, flag.getY() + 2.0f, 8.0f, 8.0f),
                             s, l.target[(size_t) s], l.fadeFor (s) > 0.0f);
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
        g.drawText (p.name.toUpperCase() + (p.repeats > 1 ? "  x" + juce::String (p.repeats) : juce::String()), text, juce::Justification::centredLeft, true);
    }
}

/** Phrase bars for the edited view: each stretch of the track drawn where it plays (`dispToX` maps display time
    to x). Repeated phrases show each time; rearranged ones in their new order. */
inline void paintPhraseStripFolded (juce::Graphics& g, juce::Rectangle<float> strip, const std::vector<Phrase>& phrases, double length,
                                    const TimeFold& fold, const std::function<float (double)>& dispToX, int highlight = -1)
{
    if (fold.isEmpty())
    {
        paintPhraseStrip (g, strip, phrases, length, [&] (double sec) { return dispToX (sec); }, highlight);
        return;
    }
    if (phrases.empty()) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (strip.toNearestInt());
    g.setFont (psyched::Fonts::label (juce::jmin (10.0f, strip.getHeight() - 2.0f)));
    for (auto& piece : fold.pieces)
        for (size_t i = 0; i < phrases.size(); ++i)
        {
            const auto& p = phrases[i];
            const double end = i + 1 < phrases.size() ? phrases[i + 1].startSec : (p.endSec > p.startSec ? p.endSec : length);
            const double a = juce::jmax (p.startSec, piece.src0), b = juce::jmin (end, piece.src1);
            if (b <= a) continue;
            const float x0 = dispToX (piece.disp0 + (a - piece.src0)), x1 = dispToX (piece.disp0 + (b - piece.src0));
            if (x1 < strip.getX() || x0 > strip.getRight()) continue;
            auto r = juce::Rectangle<float>::leftTopRightBottom (x0, strip.getY(), juce::jmax (x0 + 1.0f, x1 - 1.0f), strip.getBottom());
            g.setColour (p.colour.withAlpha ((int) i == highlight ? 1.0f : 0.75f));
            g.fillRect (r);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            auto text = r.withTrimmedLeft (4.0f);
            if (text.getX() < strip.getX() + 4.0f && text.getRight() > strip.getX() + 40.0f) text.setLeft (strip.getX() + 4.0f);
            g.drawText (p.name.toUpperCase(), text, juce::Justification::centredLeft, true);
        }
}

/** Bar lines and minute marks over an overview (the overview grid toggle). Minutes are of the track as it plays
    (the edited view's timeline); bar lines are drawn every 4, 8, 16 ... bars so they never crowd. */
/** The overview's grid: a ruler along the top (a short tick per bar, longer every 4, 16 and 64 bars), stronger lines
    down through the waveform every 16 bars (or wider when zoomed out), and minute marks along the bottom, above the
    phrase strip (`bottomInset` = its height). */
inline void paintOverviewGrid (juce::Graphics& g, juce::Rectangle<float> r, const BeatGrid& grid, double sourceLength, const TimeFold* fold,
                               float bottomInset = 0.0f, float topStrip = 0.0f)
{
    if (sourceLength <= 0.0) return;
    const double dlen = juce::jmax (0.001, fold != nullptr ? fold->displayLength (sourceLength) : sourceLength);
    auto xOfD = [&] (double d) { return r.getX() + (float) (d / dlen) * r.getWidth(); };
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt());
    const float bottom = r.getBottom() - bottomInset;
    // the bar ticks get a black strip of their own along the top (like the phrase strip along the bottom)
    if (topStrip > 0.0f)
    {
        g.setColour (juce::Colours::black);
        g.fillRect (r.withHeight (topStrip + 1.0f));
    }
    if (grid.isValid())
    {
        const double barSec = grid.beatLengthAt (sourceLength * 0.5) * 4.0;
        const double pxPerBar = r.getWidth() * barSec / dlen;
        // the finest level that still leaves room between ticks
        int finest = 1;
        while (finest < 64 && pxPerBar * finest < 3.0) finest *= 4;
        int lineEvery = 16;
        while (lineEvery < 256 && pxPerBar * lineEvery < 14.0) lineEvery *= 4;
        int bar = 0;
        grid.forEachBeat (0.0, sourceLength, [&] (double t, int beatInBar, int)
        {
            if (beatInBar != 1) return;
            const int b = bar++;
            if (b % finest != 0) return;
            const float full = topStrip > 0.0f ? topStrip : 10.0f;
            const float len = b % 16 == 0 ? full : b % 4 == 0 ? full * 0.65f : full * 0.42f;   // 16 bars / 4 bars / a bar
            const bool line = b % lineEvery == 0;
            auto draw = [&] (float x)
            {
                x = std::round (x) + 0.5f;
                if (line)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.3f));
                    g.fillRect (x - 0.5f, r.getY() + topStrip, 1.0f, bottom - r.getY() - topStrip);
                }
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                if (topStrip <= 0.0f) g.fillRect (x - 1.0f, r.getY(), 2.0f, len + 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.92f));
                g.fillRect (x - 0.5f, r.getY(), 1.0f, len);
            };
            if (fold == nullptr || fold->isEmpty()) draw (xOfD (t));
            else for (double d : fold->allDisplay (t)) draw (xOfD (d));
        });
    }
    // minute marks along the bottom (like rekordbox), with the minute
    g.setFont (psyched::Fonts::mono (8.5f, true));
    for (int m = 1; m * 60.0 < dlen; ++m)
    {
        const float x = std::round (xOfD (m * 60.0)) + 0.5f;
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (x - 1.0f, bottom - 6.0f, 3.0f, 6.0f);
        g.setColour (juce::Colours::white);
        g.fillRect (x - 0.5f, bottom - 5.0f, 1.5f, 5.0f);
        auto lab = juce::Rectangle<float> (x + 2.0f, bottom - 9.0f, 12.0f, 9.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (lab);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawText (juce::String (m), lab, juce::Justification::centredLeft);
    }
}

/** Stem separation progress as a strip under an overview: the separated part of the track lit, so it can be read
    against the track and the playhead. (fraction = separated share of the track, from its start) */
inline void paintStemProgressStrip (juce::Graphics& g, juce::Rectangle<float> strip, float fraction, double sourceLength,
                                    const TimeFold* fold, juce::Colour colour)
{
    g.setColour (Theme::line.withAlpha (0.7f));
    g.fillRect (strip);
    if (sourceLength <= 0.0) return;
    const double done = sourceLength * juce::jlimit (0.0f, 1.0f, fraction);
    if (fold == nullptr || fold->isEmpty())
    {
        g.setColour (colour);
        g.fillRect (strip.withWidth (strip.getWidth() * fraction));
        return;
    }
    const double dlen = juce::jmax (0.001, fold->displayLength (sourceLength));
    g.setColour (colour);
    for (auto& p : fold->pieces)
    {
        const double a = p.src0, b = juce::jmin (p.src1, done);
        if (b <= a) continue;
        const float x0 = strip.getX() + (float) (p.disp0 / dlen) * strip.getWidth();
        const float x1 = strip.getX() + (float) ((p.disp0 + (b - a)) / dlen) * strip.getWidth();
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, strip.getY(), juce::jmax (x0 + 1.0f, x1), strip.getBottom()));
    }
}

/** A small toggle that draws a grid icon (the overview grid switch). */
inline void paintGridToggle (juce::Graphics& g, juce::Rectangle<float> r, bool on, juce::Colour colour)
{
    psyched::paintChip (g, r, {}, colour, on ? psyched::ChipState::on : psyched::ChipState::off);
    auto ic = r.withSizeKeepingCentre (juce::jmin (12.0f, r.getWidth() - 6.0f), juce::jmin (12.0f, r.getHeight() - 6.0f));
    g.setColour (on ? colour : Theme::textDim);
    for (int k = 0; k < 3; ++k)
        g.fillRect (ic.getX() + (float) k * ic.getWidth() / 2.0f - (k == 2 ? 1.0f : 0.0f), ic.getY(), 1.0f, ic.getHeight());
    g.fillRect (ic.getX(), ic.getY(), ic.getWidth(), 1.0f);
}

} // namespace orbit
