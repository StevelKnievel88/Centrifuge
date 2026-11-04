#include "ScrollingWaveforms.h"
#include "ColourPicker.h"
#include "TrackMarks.h"
#include "Theme.h"

namespace orbit
{

namespace
{
    constexpr int markerBand = 14;   // space above the top lane for cue flags
    constexpr int dividerH = 5;      // draggable gap between the two lanes of a deck
    const juce::Colour skipColour (0xff38d9ff);

    juce::uint64 stateHash (const Deck::Published& st)
    {
        juce::uint64 h = 1469598103934665603ull;
        auto mix = [&h] (double v) { h ^= (juce::uint64) std::llround (v * 1000.0); h *= 1099511628211ull; };
        mix (st.loopActive.load()); mix (st.loopInSec.load()); mix (st.loopOutSec.load()); mix (st.cuePointSec.load());
        for (size_t i = 0; i < 8; ++i) { mix (st.hotStart[i].load()); mix ((double) st.hotColour[i].load()); }
        for (size_t i = 0; i < 4; ++i) mix (st.stemGain[i].load());
        mix (std::floor (st.stemsProgress.load() * 100.0f));
        mix (st.stemsReady.load());
        mix (st.numLocators.load()); mix (st.numSkips.load()); mix (st.skipsOn.load()); mix (st.skipPendingFrom.load());
        for (int i = 0; i < st.numLocators.load() && i < Deck::maxLocators; ++i)
            { mix (st.locStart[(size_t) i].load()); mix ((double) st.locColour[(size_t) i].load()); }
        mix (st.cueEdits.load());
        return h;
    }

    /** Keeps a deck's stem waveform up to date while its stems are being separated. */
    class StemWaveJob : public juce::ThreadPoolJob
    {
    public:
        StemWaveJob (StemAudio::Ptr s, StemWaveform::Ptr w, std::shared_ptr<std::atomic<bool>> c)
            : juce::ThreadPoolJob ("stem waveform"), stems (s), wave (w), cancelled (std::move (c)) {}
        JobStatus runJob() override
        {
            while (! shouldExit() && ! cancelled->load())
            {
                wave->update (*stems);
                if (wave->computed.load() >= wave->total) return jobHasFinished;
                juce::Thread::sleep (250);
            }
            return jobHasFinished;
        }
    private:
        StemAudio::Ptr stems;
        StemWaveform::Ptr wave;
        std::shared_ptr<std::atomic<bool>> cancelled;
    };

    juce::int64 styleKeyFor (int style, int shape, juce::Colour tint, int foldVersion)
    {
        return (juce::int64) style | ((juce::int64) shape << 3) | ((juce::int64) tint.getARGB() << 8)
             | ((juce::int64) (foldVersion & 0xffffff) << 40);
    }

    /** Two right-pointing triangles with a line down from the first: the skip marker. */
    void drawSkipFlag (juce::Graphics& g, float x, float top, float h, juce::Colour c)
    {
        juce::Path p;
        const float tw = h * 0.62f;
        p.addTriangle (x, top, x, top + h, x + tw, top + h * 0.5f);
        p.addTriangle (x + tw * 0.8f, top, x + tw * 0.8f, top + h, x + tw * 1.8f, top + h * 0.5f);
        g.setColour (c);
        g.fillPath (p);
    }
}

ScrollingWaveforms::ScrollingWaveforms (AudioEngine& e)
    : engine (e), vblank (this, [this] (double t) { onVBlank (t); })
{
    setOpaque (true);
}

ScrollingWaveforms::~ScrollingWaveforms()
{
    stemWavePool.removeAllJobs (true, 3000);
}

void ScrollingWaveforms::setVisibleSeconds (double s)
{
    visibleSeconds = juce::jlimit (1.0, 60.0, s);
    needsRedraw = true;
}

void ScrollingWaveforms::setLanes (int deck, LaneConfig top, LaneConfig bottom)
{
    if (deck < 0 || deck > 1) return;
    lanes[(size_t) deck] = { top, bottom };
    needsRedraw = true;
}

void ScrollingWaveforms::onVBlank (double t)
{
    bool moving = false;
    const double latency = engine.getOutputLatencySeconds();

    for (int d = 0; d < 2; ++d)
    {
        auto& deck = engine.deck (d);
        auto& dv = decks[(size_t) d];
        const auto& st = deck.state();

        const int serial = st.trackSerial.load();
        if (serial != dv.serial)
        {
            dv.serial = serial;
            dv.track = serial != 0 ? deck.getLoadedTrack() : nullptr;
            dv.uiSkipFrom = -1.0;
            needsRedraw = true;
        }
        // Same track, new grid / cues (edited, or switched between the rekordbox and Centrifuge versions)
        if (const int ver = st.trackDataVersion.load(); ver != dv.dataVersion)
        {
            dv.dataVersion = ver;
            if (serial != 0) dv.track = deck.getLoadedTrack();
            needsRedraw = true;
        }

        // Stems arrived (or changed): start building their waveform in the background.
        auto stems = deck.getStemsForUi();
        if (stems != dv.stems)
        {
            dv.stems = stems;
            dv.stemWave = nullptr;
            if (dv.stemJobCancel != nullptr) dv.stemJobCancel->store (true);
            dv.stemJobCancel = std::make_shared<std::atomic<bool>> (false);
            if (stems != nullptr)
            {
                StemWaveform::Ptr w = new StemWaveform();
                w->total = (int) std::ceil ((double) stems->numFrames * w->entriesPerSecond / stems->sampleRate);
                for (auto& p : w->peak) p.assign ((size_t) w->total, 0);
                dv.stemWave = w;
                stemWavePool.addJob (new StemWaveJob (stems, w, dv.stemJobCancel), true);
            }
            dv.lastComputed = 0;
            needsRedraw = true;
        }
        if (dv.stemWave != nullptr)
        {
            const int c = dv.stemWave->computed.load();
            if (c - dv.lastComputed > 1200 || (c == dv.stemWave->total && c != dv.lastComputed))
            {
                dv.lastComputed = c;
                needsRedraw = true;
            }
        }

        // Skipped sections fold out of the waveform
        std::vector<std::pair<double, double>> sk;
        const int ns = juce::jmin (st.numSkips.load(), Deck::maxSkips);
        for (int i = 0; i < ns; ++i)
            sk.push_back ({ st.skipFrom[(size_t) i].load(), st.skipTo[(size_t) i].load() });
        const bool foldActive = foldSkips && st.skipsOn.load();
        if (sk != dv.lastSkips || foldActive != dv.lastFoldActive)
        {
            dv.lastSkips = sk;
            dv.lastFoldActive = foldActive;
            dv.fold.set (foldActive ? sk : std::vector<std::pair<double, double>>());
            dv.smoother = {};
            needsRedraw = true;
        }

        const bool playing = st.playing.load();
        const double rate = st.tempoRatio.load();
        const double raw = dv.fold.toDisplay (st.positionSec.load() - (playing ? latency * rate : 0.0));
        const double prev = dv.shownPos;
        dv.shownPos = dv.smoother.update (raw, playing, rate, t);
        if (std::abs (dv.shownPos - prev) > 1.0e-6) moving = true;

        const auto h = stateHash (st);
        if (h != dv.lastHash) { dv.lastHash = h; needsRedraw = true; }
    }

    if ((moving || needsRedraw) && t - lastPaintTime >= 1.0 / maxFps - 0.002)
    {
        lastPaintTime = t;
        needsRedraw = false;
        repaint();
    }
}

//==============================================================================
juce::Rectangle<int> ScrollingWaveforms::deckBounds (int deck) const
{
    auto r = getLocalBounds();
    const int h = (r.getHeight() - 2) / 2;
    return deck == 0 ? r.withHeight (h) : r.withTrimmedTop (h + 2);
}

juce::Rectangle<int> ScrollingWaveforms::dividerBounds (int deck) const
{
    auto r = deckBounds (deck);
    const int topH = (int) std::round ((r.getHeight() - dividerH) * laneSplit[(size_t) deck]);
    return { r.getX(), r.getY() + topH, r.getWidth(), dividerH };
}

int ScrollingWaveforms::dividerAt (juce::Point<int> p) const
{
    for (int d = 0; d < 2; ++d)
        if (dividerBounds (d).expanded (0, 3).contains (p))
            return d;
    return -1;
}

bool ScrollingWaveforms::nearPlayhead (juce::Point<int> p) const
{
    return std::abs ((float) p.x - playheadX (getLocalBounds())) <= 5.0f && dividerAt (p) < 0;
}

double ScrollingWaveforms::sourceTimeAt (int deck, float x) const
{
    const auto r = deckBounds (deck);
    const double pps = (double) r.getWidth() / visibleSeconds;
    const auto& dv = decks[(size_t) deck];
    return juce::jmax (0.0, dv.fold.toSource (dv.shownPos + (x - playheadX (r)) / pps));
}

juce::String ScrollingWaveforms::laneName (int deck, int lane) const
{
    const auto& cfg = lanes[(size_t) deck][(size_t) lane];
    const auto& dv = decks[(size_t) deck];
    switch (cfg.style)
    {
        case stems:
            if (stemsHidden) return "3-BAND (STEMS HIDDEN)";
            return dv.stemWave != nullptr ? "STEMS" : "3-BAND (NO STEMS YET)";
        case rgb:          return "RGB";
        case singleColour: return "SINGLE COLOUR";
        default:           return "3-BAND";
    }
}

void ScrollingWaveforms::paint (juce::Graphics& g)
{
    markers.clear();
    g.fillAll (Theme::line);
    paintDeck (g, 0, deckBounds (0));
    paintDeck (g, 1, deckBounds (1));
}

void ScrollingWaveforms::drawLabel (juce::Graphics& g, juce::Rectangle<int> lane, const juce::String& text, bool atTop)
{
    if (lane.getHeight() < 20) return;
    const auto f = psyched::Fonts::label (10.0f);
    const float w = f.getStringWidthFloat (text.toUpperCase()) + 14.0f;
    auto r = juce::Rectangle<float> ((float) lane.getX() + 34.0f, atTop ? (float) lane.getY() + 4.0f : (float) lane.getBottom() - 20.0f, w, 16.0f);
    g.setColour (psyched::Colours::field.withAlpha (0.85f));
    g.fillRect (r);
    g.setColour (psyched::Colours::edge2);
    g.drawRect (r, 1.0f);
    g.setColour (psyched::Colours::ink2);
    g.setFont (f);
    g.drawText (text.toUpperCase(), r, juce::Justification::centred);
}

void ScrollingWaveforms::paintDeck (juce::Graphics& g, int d, juce::Rectangle<int> r)
{
    auto div = dividerBounds (d);
    auto topLane = r.withBottom (div.getY());
    auto bottomLane = r.withTop (div.getBottom());

    paintLane (g, d, 0, topLane);
    paintLane (g, d, 1, bottomLane);

    // Phrase bars along the bottom of the deck's waveforms
    if (auto& tr = decks[(size_t) d].track; tr != nullptr && ! tr->phrases.empty() && bottomLane.getHeight() > 30)
    {
        const auto& dv = decks[(size_t) d];
        const double pps = (double) r.getWidth() / visibleSeconds;
        const float px0 = playheadX (r);
        auto strip = bottomLane.toFloat().removeFromBottom (12.0f);
        paintPhraseStrip (g, strip, tr->phrases, tr->audio != nullptr ? tr->audio->lengthSeconds() : tr->info.durationSec,
                          [&] (double sec) { return px0 + (float) ((dv.fold.toDisplay (sec) - dv.shownPos) * pps); });
    }

    // Divider with a small grip
    g.setColour (Theme::panel);
    g.fillRect (div);
    g.setColour (psyched::Colours::edge2);
    g.fillRect (div.withSizeKeepingCentre (36, 2).toFloat());

    // Playhead through both lanes (drag it left/right to see more of what's coming)
    const auto& st = engine.deck (d).state();
    const float playX = playheadX (r);
    g.setColour (st.playing.load() ? juce::Colour (0xffff2d2d) : juce::Colours::white);
    g.fillRect (playX - 1.0f, (float) r.getY(), 2.0f, (float) r.getHeight());
    juce::Path tri;
    tri.addTriangle (playX - 6.0f, (float) r.getY(), playX + 6.0f, (float) r.getY(), playX, (float) r.getY() + 7.0f);
    g.fillPath (tri);

    // Deck badge + lane labels (dark backing so they read over any waveform)
    auto badge = juce::Rectangle<float> ((float) r.getX() + 6.0f, (float) r.getY() + 4.0f, 22.0f, 18.0f);
    g.setColour (Theme::deckColour (d));
    g.fillRect (badge);
    g.setColour (psyched::Colours::onSolid);
    g.setFont (Theme::display (11.0f, true));
    g.drawText (juce::String (d + 1), badge, juce::Justification::centred);

    drawLabel (g, topLane, laneName (d, 0), false);
    drawLabel (g, bottomLane, laneName (d, 1), true);

    // Stem separation progress
    if (st.stemsReady.load())
    {
        const float p = st.stemsProgress.load();
        if (p < 0.999f)
        {
            for (int lane = 0; lane < 2; ++lane)
                if (lanes[(size_t) d][(size_t) lane].style == stems)
                {
                    auto lr = lane == 0 ? topLane : bottomLane;
                    g.setColour (Theme::deckColour (d).withAlpha (0.8f));
                    g.fillRect ((float) lr.getX(), (float) lr.getBottom() - 3.0f, (float) lr.getWidth() * p, 3.0f);
                }
            auto tr = juce::Rectangle<float> ((float) r.getX() + 34.0f, (float) r.getY() + 4.0f, 130.0f, 16.0f);
            g.setColour (juce::Colours::black.withAlpha (0.78f));
            g.fillRect (tr);
            g.setColour (Theme::deckColour (d));
            g.setFont (psyched::Fonts::label (10.0f));
            g.drawText ("SEPARATING " + juce::String (juce::roundToInt (p * 100.0f)) + "%", tr, juce::Justification::centred);
        }
    }
}

void ScrollingWaveforms::paintLane (juce::Graphics& g, int d, int lane, juce::Rectangle<int> r)
{
    g.setColour (Theme::waveBg);
    g.fillRect (r);
    if (r.getHeight() < 8) return;

    auto& dv = decks[(size_t) d];
    const auto& st = engine.deck (d).state();
    const auto cfg = lanes[(size_t) d][(size_t) lane];
    const bool flags = lane == 0;
    const float playX = playheadX (r);
    const double pps = (double) r.getWidth() / visibleSeconds;
    auto wave = flags ? r.withTrimmedTop (markerBand).withTrimmedBottom (3) : r.reduced (0, 2);
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();

    // Baseline
    g.setColour (Theme::line.withAlpha (0.6f));
    const float baseY = cfg.shape == StripOptions::halfUp ? (float) wave.getBottom() - 1.0f
                      : cfg.shape == StripOptions::halfDown ? (float) wave.getY() : (float) wave.getCentreY();
    g.fillRect ((float) r.getX(), baseY, (float) r.getWidth(), 1.0f);

    if (dv.track == nullptr || dv.track->waveform == nullptr)
    {
        if (flags)
        {
            g.setColour (Theme::textFaint);
            g.setFont (Theme::font (14.0f));
            g.drawText ("Deck " + juce::String (d + 1) + "  -  load a track", r, juce::Justification::centred);
        }
        return;
    }

    const double t = dv.shownPos;
    const auto& fold = dv.fold;
    auto xOf = [&] (double sec) { return playX + (float) ((fold.toDisplay (sec) - t) * pps); };
    const double tStart = t - (playX - (float) r.getX()) / pps;
    const double tEnd = tStart + (double) r.getWidth() / pps;

    // Loop region behind the waveform
    const double li = st.loopInSec.load(), lo = st.loopOutSec.load();
    if (li >= 0.0 && lo > li)
    {
        const bool active = st.loopActive.load();
        const float x0 = juce::jmax ((float) r.getX(), xOf (li)), x1 = juce::jmin ((float) r.getRight(), xOf (lo));
        if (x1 > x0)
        {
            g.setColour ((active ? Theme::accent : Theme::textFaint).withAlpha (active ? 0.22f : 0.12f));
            g.fillRect (x0, (float) wave.getY(), x1 - x0, (float) wave.getHeight());
        }
    }

    const float px = playX - (float) wave.getX();
    StripOptions opts;
    opts.shape = cfg.shape;
    opts.fold = &dv.fold;
    const double skipped = fold.skippedTotal();

    const bool showStems = cfg.style == stems && ! stemsHidden && dv.stemWave != nullptr && dv.stemWave->computed.load() > 0;
    if (showStems)
    {
        auto sw = dv.stemWave;
        const int valid = sw->computed.load();

        // While stems are still being separated, the not-yet-separated part shows the 3-band waveform faintly.
        if (valid < sw->total)
            if (auto wd = dv.track->waveform)
            {
                auto o = opts;
                o.entriesPerSecond = wd->entriesPerSecond;
                auto& tc = dv.underTiles[(size_t) lane];
                tc.configure (wd.get(), styleKeyFor (threeBand, cfg.shape, {}, fold.version), wd->entriesPerSecond,
                              wd->lengthSeconds() - skipped, pps, wave.getHeight(), scale,
                              [wd, o] (double first, double epp, int w, int h) { return renderWaveformStrip (*wd, first, epp, w, h, o); });
                juce::Graphics::ScopedSaveState ss (g);
                g.excludeClipRegion (wave.withRight ((int) xOf (valid / sw->entriesPerSecond)));
                tc.draw (g, wave, t, px, 0.3f);
            }

        // Four stems, each as bright as its knob is up (a muted stem stays as a faint ghost): overlaid,
        // or each in its own row (drums, bass, inst, vocals from the top) when Settings asks for that.
        auto o = opts;
        o.entriesPerSecond = sw->entriesPerSecond;
        if (stemsSeparate) o.shape = StripOptions::full;
        for (int s : stemOrder)
        {
            auto area = wave;
            if (stemsSeparate)
            {
                const int rowH = wave.getHeight() / 4;
                area = juce::Rectangle<int> (wave.getX(), wave.getY() + s * rowH, wave.getWidth(), s == 3 ? wave.getHeight() - 3 * rowH : rowH);
                if (s > 0) { g.setColour (Theme::line.withAlpha (0.5f)); g.fillRect (area.getX(), area.getY(), area.getWidth(), 1); }
            }
            auto& tc = dv.stemTiles[(size_t) lane][(size_t) s];
            const auto col = Theme::stemColour (s);
            tc.configure (sw.get(), styleKeyFor (s, stemsSeparate ? 7 : cfg.shape, {}, fold.version), sw->entriesPerSecond, sw->lengthSeconds() - skipped,
                          pps, area.getHeight(), scale,
                          [sw, s, col, o] (double first, double epp, int w, int h)
                          { return renderStemStrip (*sw, s, col, sw->computed.load(), first, epp, w, h, o); });
            tc.setValidEntries (valid);
            const float gain = st.stemGain[(size_t) s].load();
            tc.draw (g, area, t, px, (gain <= 0.001f ? 0.12f : 0.3f + 0.7f * gain) * (stemsSeparate ? 1.0f : stemOpacity[(size_t) juce::jlimit (0, 3, s)]));
        }
    }
    else
    {
        const int style = cfg.style == stems ? threeBand : cfg.style;
        const auto wdStyle = style == rgb ? WaveformData::Style::rgb
                           : style == singleColour ? WaveformData::Style::blue : WaveformData::Style::threeBand;
        auto wd = dv.track->waveformInStyle (wdStyle);
        if (wd != nullptr)
        {
            auto o = opts;
            o.entriesPerSecond = wd->entriesPerSecond;
            o.tint = style == singleColour ? singleTint : juce::Colour();
            auto detail = dv.track->detail;
            o.detail = detail.get();
            auto& tc = dv.laneTiles[(size_t) lane];
            tc.configure (wd.get(), styleKeyFor (style, cfg.shape, o.tint, fold.version), wd->entriesPerSecond,
                          wd->lengthSeconds() - skipped, pps, wave.getHeight(), scale,
                          [wd, detail, o] (double first, double epp, int w, int h) { return renderWaveformStrip (*wd, first, epp, w, h, o); });
            tc.draw (g, wave, t, px);
        }
    }

    // Skipped sections that aren't folded out are shaded
    if (fold.isEmpty())
        for (int i = 0; i < juce::jmin (st.numSkips.load(), Deck::maxSkips); ++i)
        {
            const double f = st.skipFrom[(size_t) i].load(), to = st.skipTo[(size_t) i].load();
            if (to <= f) continue;
            const float x0 = juce::jmax ((float) r.getX(), xOf (f)), x1 = juce::jmin ((float) r.getRight(), xOf (to));
            if (x1 <= x0) continue;
            g.setColour (juce::Colours::black.withAlpha (st.skipsOn.load() ? 0.6f : 0.35f));
            g.fillRect (x0, (float) wave.getY(), x1 - x0, (float) wave.getHeight());
            g.setColour (skipColour.withAlpha (0.25f));
            for (float x = x0 - std::fmod (x0, 10.0f); x < x1; x += 10.0f)
                g.drawLine (juce::jmax (x0, x), (float) wave.getBottom(), juce::jmin (x1, x + (float) wave.getHeight()), (float) wave.getY(), 1.0f);
        }

    juce::ignoreUnused (tStart, tEnd);
    paintOverlays (g, d, wave, r, flags);
}

void ScrollingWaveforms::paintOverlays (juce::Graphics& g, int d, juce::Rectangle<int> wave, juce::Rectangle<int> r, bool withFlags)
{
    auto& dv = decks[(size_t) d];
    const auto& st = engine.deck (d).state();
    const float playX = playheadX (r);
    const double pps = (double) r.getWidth() / visibleSeconds;
    const double t = dv.shownPos;
    const auto& fold = dv.fold;
    const double tStart = t - (playX - (float) r.getX()) / pps;
    const double tEnd = tStart + (double) r.getWidth() / pps;
    const double srcStart = fold.toSource (tStart), srcEnd = fold.toSource (tEnd);
    auto xOf = [&] (double sec) { return playX + (float) ((fold.toDisplay (sec) - t) * pps); };
    auto visible = [&] (double sec) { return sec >= srcStart - 0.5 && sec <= srcEnd + 0.5 && ! fold.hidden (sec); };
    auto& track = dv.track;
    const float flagTop = (float) r.getY() + 1.0f;

    // Beat grid
    if (track->grid.isValid())
    {
        const auto& beats = track->grid.getBeats();
        int i = juce::jmax (0, track->grid.beatIndexAt (srcStart));
        const bool dense = pps * track->grid.beatLengthAt (fold.toSource (t)) < 7.0;
        for (; i < (int) beats.size() && beats[(size_t) i].timeSec <= srcEnd; ++i)
        {
            const auto& b = beats[(size_t) i];
            const bool bar = b.beatInBar == 1;
            if ((dense && ! bar) || fold.hidden (b.timeSec)) continue;
            const float x = xOf (b.timeSec);
            g.setColour (bar ? juce::Colours::white.withAlpha (0.55f) : juce::Colours::white.withAlpha (0.18f));
            g.fillRect (x, (float) wave.getY(), bar ? 1.5f : 1.0f, (float) wave.getHeight());
            if (withFlags)
                g.fillRect (x - (bar ? 1.5f : 1.0f), (float) r.getY() + markerBand - 5.0f, bar ? 4.5f : 3.0f, 4.0f);
        }
    }

    // Loop borders
    const double li = st.loopInSec.load(), lo = st.loopOutSec.load();
    if (li >= 0.0 && lo > li)
    {
        g.setColour ((st.loopActive.load() ? Theme::accent : Theme::textFaint).withAlpha (0.9f));
        for (double edge : { li, lo })
            if (visible (edge))
                g.fillRect (xOf (edge) - 1.0f, (float) wave.getY(), 2.0f, (float) wave.getHeight());
    }

    // Skips: two right-pointing triangles with a line from the first (where playback jumps)
    const int ns = juce::jmin (st.numSkips.load(), Deck::maxSkips);
    for (int i = 0; i < ns; ++i)
    {
        const double f = st.skipFrom[(size_t) i].load(), to = st.skipTo[(size_t) i].load();
        if (f > srcEnd + 0.5 || to < srcStart - 0.5) continue;
        const float x = xOf (f);
        const auto c = st.skipsOn.load() ? skipColour : skipColour.withSaturation (0.0f).withAlpha (0.6f);
        g.setColour (c.withAlpha (0.85f));
        g.fillRect (x - 0.5f, (float) wave.getY(), 1.5f, (float) wave.getHeight());
        if (! fold.isEmpty()) continue;
        // Unfolded: also mark where it lands
        const float xe = xOf (to);
        g.setColour (c.withAlpha (0.6f));
        g.fillRect (xe - 0.5f, (float) wave.getY(), 1.0f, (float) wave.getHeight());
    }
    if (withFlags)
        for (int i = 0; i < ns; ++i)
        {
            const double f = st.skipFrom[(size_t) i].load();
            if (! visible (f) && fold.isEmpty()) continue;
            if (f < srcStart - 0.5 || f > srcEnd + 0.5) continue;
            const float x = xOf (f);
            const auto c = st.skipsOn.load() ? skipColour : skipColour.withSaturation (0.0f);
            drawSkipFlag (g, x, flagTop, (float) markerBand - 3.0f, c);
            markers.push_back ({ juce::Rectangle<float> (x - 2.0f, flagTop - 1.0f, 22.0f, (float) markerBand), d, markerSkip, i });
        }

    // A skip that has been started but not finished
    for (double pending : { st.skipPendingFrom.load(), dv.uiSkipFrom })
        if (pending >= 0.0 && visible (pending))
        {
            const float x = xOf (pending);
            g.setColour (skipColour.withAlpha (0.5f));
            for (float y = (float) wave.getY(); y < (float) wave.getBottom(); y += 6.0f)
                g.fillRect (x - 0.5f, y, 1.5f, 3.0f);
            if (withFlags)
            {
                juce::Path p;
                p.addTriangle (x, flagTop, x, flagTop + markerBand - 3.0f, x + 7.0f, flagTop + (markerBand - 3.0f) * 0.5f);
                g.fillPath (p);
            }
        }

    // Locators (memory cues): triangle pointing down with a vertical line
    const int nl = juce::jmin (st.numLocators.load(), Deck::maxLocators);
    for (int i = 0; i < nl; ++i)
    {
        const double s = st.locStart[(size_t) i].load();
        if (! visible (s)) continue;
        const float x = xOf (s);
        const auto c = juce::Colour (st.locColour[(size_t) i].load());
        const double le = st.locEnd[(size_t) i].load();
        const bool isLoop = le > s;
        const int mode = st.locMode[(size_t) i].load();
        if (isLoop)
        {
            // Loop locators: shade the loop
            g.setColour (c.withAlpha (st.autoLoopLocator.load() == i ? 0.22f : 0.10f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, (float) wave.getY(), juce::jmax (x + 1.0f, xOf (le)), (float) wave.getBottom()));
        }
        // The line down from the head tells the loop modes apart: active = orange, cycle = red, others as they were
        const auto lineCol = isLoop && mode == 1 ? juce::Colour (activeLoopLine) : isLoop && mode == 2 ? juce::Colour (cycleLoopLine) : c.withAlpha (0.8f);
        const float lineW = isLoop && mode != 0 ? 2.0f : 1.0f;
        g.setColour (lineCol);
        g.fillRect (x - lineW * 0.5f, (float) wave.getY(), lineW, (float) wave.getHeight());
        if (withFlags)
        {
            const float h = (float) markerBand - 3.0f;
            if (! isLoop)
            {
                juce::Path p;
                p.addTriangle (x - 5.5f, flagTop, x + 5.5f, flagTop, x, flagTop + 10.0f);
                g.setColour (c);
                g.fillPath (p);
            }
            else if (mode == 2)
            {
                // Cycle loop: the passes left (or the setting) as the head, e.g. "4x"
                const int left = st.autoLoopLocator.load() == i ? st.cyclesLeft.load() : st.locCycles[(size_t) i].load();
                auto box = juce::Rectangle<float> (x, flagTop, 22.0f, h);
                g.setColour (c);
                g.fillRect (box);
                g.setColour (juce::Colours::black);
                g.setFont (psyched::Fonts::mono (10.0f, true));
                g.drawText (juce::String (left) + "x", box, juce::Justification::centred);
            }
            else
            {
                // Loop (and active loop: filled head)
                auto box = juce::Rectangle<float> (x, flagTop, h, h);
                if (mode == 1) { g.setColour (c); g.fillRect (box); }
                drawLoopIcon (g, box.reduced (1.5f), mode == 1 ? juce::Colours::black : c, 1.5f);
            }
            markers.push_back ({ juce::Rectangle<float> (x - 7.0f, flagTop - 1.0f, isLoop ? 26.0f : 14.0f, (float) markerBand), d, markerLocator, i });
            if (cueName)
                if (auto name = cueName (d, false, i); name.isNotEmpty())
                {
                    g.setFont (Theme::font (10.0f, true));
                    g.setColour (c.brighter (0.4f));
                    g.drawText (name, juce::Rectangle<float> (x + 7.0f, flagTop - 1.0f, 120.0f, (float) markerBand - 2.0f), juce::Justification::centredLeft, true);
                }
        }
        const double e = st.locEnd[(size_t) i].load();
        if (e > s && visible (e))
        {
            g.setColour (lineCol.withMultipliedAlpha (0.6f));
            g.fillRect (xOf (e) - lineW * 0.5f, (float) wave.getY(), lineW, (float) wave.getHeight());
        }
    }

    // Hot cues
    g.setFont (Theme::font (10.5f, true));
    for (int i = 0; i < 8; ++i)
    {
        const double hs = st.hotStart[(size_t) i].load();
        if (hs < 0.0 || ! visible (hs)) continue;
        const float x = xOf (hs);
        const auto col = juce::Colour (st.hotColour[(size_t) i].load());
        g.setColour (col.withAlpha (0.85f));
        g.fillRect (x, (float) wave.getY(), 1.5f, (float) wave.getHeight());
        if (withFlags)
        {
            auto flag = juce::Rectangle<float> (x, flagTop, 14.0f, (float) markerBand - 2.0f);
            g.setColour (col);
            g.fillRect (flag);
            g.setColour (juce::Colours::black);
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)), flag, juce::Justification::centred);
            markers.push_back ({ flag.expanded (1.0f), d, markerHot, i });
            if (cueName)
                if (auto name = cueName (d, true, i); name.isNotEmpty())
                {
                    g.setColour (col.brighter (0.3f));
                    g.drawText (name, juce::Rectangle<float> (x + 16.0f, flagTop, 120.0f, (float) markerBand - 2.0f), juce::Justification::centredLeft, true);
                }
        }
    }

    // Cue point
    const double cp = st.cuePointSec.load();
    if (withFlags && visible (cp))
    {
        const float x = xOf (cp);
        juce::Path p;
        p.addTriangle (x - 6.0f, (float) r.getBottom(), x + 6.0f, (float) r.getBottom(), x, (float) r.getBottom() - 8.0f);
        g.setColour (Theme::accent);
        g.fillPath (p);
    }

    if (tStart < 0.0)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect ((float) r.getX(), (float) wave.getY(), xOf (0.0) - (float) r.getX(), (float) wave.getHeight());
    }
}

//==============================================================================
const ScrollingWaveforms::Marker* ScrollingWaveforms::markerAt (juce::Point<float> p) const
{
    // Topmost first (hot cues are drawn last)
    for (auto it = markers.rbegin(); it != markers.rend(); ++it)
        if (it->r.contains (p))
            return &*it;
    return nullptr;
}

void ScrollingWaveforms::deleteMarker (const Marker& m)
{
    auto& deck = engine.deck (m.deck);
    if (m.kind == markerHot)          deck.post (Deck::Cmd::hotCueDelete, 0.0, m.index, true);
    else if (m.kind == markerLocator) deck.post (Deck::Cmd::locatorDelete, 0.0, m.index);
    else if (m.kind == markerSkip)    deck.post (Deck::Cmd::skipDelete, 0.0, m.index);
}

void ScrollingWaveforms::showMarkerMenu (const Marker& m)
{
    const auto& st = engine.deck (m.deck).state();
    juce::PopupMenu menu;
    juce::Colour current;
    if (m.kind == markerHot)
    {
        current = juce::Colour (st.hotColour[(size_t) m.index].load());
        menu.addSectionHeader ("Hot cue " + juce::String::charToString ((juce::juce_wchar) ('A' + m.index)));
    }
    else if (m.kind == markerLocator)
    {
        current = juce::Colour (st.locColour[(size_t) m.index].load());
        menu.addSectionHeader ("Locator " + juce::String (m.index + 1));
    }
    else
    {
        menu.addSectionHeader ("Skip");
        menu.addItem (2, "Skips on (this deck)", true, st.skipsOn.load());
        menu.addItem (3, "Fold skipped sections in the waveform", true, foldSkips);
        menu.addSeparator();
    }

    const bool loopLocator = m.kind == markerLocator && st.locEnd[(size_t) m.index].load() > st.locStart[(size_t) m.index].load();
    const int loopMode = loopLocator ? st.locMode[(size_t) m.index].load() : 0;
    const int loopCycles = loopLocator ? st.locCycles[(size_t) m.index].load() : 4;
    if (loopLocator)
    {
        addLoopModeItems (menu, 200, loopMode, loopCycles);
        menu.addSeparator();
    }
    if (m.kind != markerSkip)
    {
        addCueColourItems (menu, 100, current);
        menu.addSeparator();
        menu.addItem (4, "Rename...");
    }
    menu.addItem (1, m.kind == markerSkip ? "Delete skip" : "Delete   (shift + click)");

    const auto marker = m;
    const auto screen = localAreaToGlobal (m.r.toNearestInt());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (screen),
                        [this, marker, screen, current, loopCycles] (int result)
    {
        auto& deck = engine.deck (marker.deck);
        if (auto [mode, cycles] = loopModeFromMenu (result, 200, loopCycles); mode >= 0)
        {
            deck.post (Deck::Command { Deck::Cmd::locatorLoopMode, (double) mode, marker.index, true, nullptr, 0, (double) cycles });
            return;
        }
        auto setColour = [this, marker] (juce::Colour c)
        {
            engine.deck (marker.deck).post (marker.kind == markerHot ? Deck::Cmd::hotCueColour : Deck::Cmd::locatorColour,
                                            (double) c.getARGB(), marker.index);
        };
        if (result == 1) deleteMarker (marker);
        else if (result == 2) deck.post (Deck::Cmd::skipsToggle);
        else if (result == 3) { foldSkips = ! foldSkips; needsRedraw = true; if (onFoldChanged) onFoldChanged (foldSkips); }
        else if (result == 4 && onRenameCue)
            onRenameCue (marker.deck, marker.kind == markerHot, marker.index,
                         cueName ? cueName (marker.deck, marker.kind == markerHot, marker.index) : juce::String());
        else if (result >= 100 && result < 116) setColour (juce::Colour (rekordboxCueColours()[(size_t) (result - 100)].argb));
        else if (result == 116) ColourPickerBubble::show (screen, current, setColour);
    });
}

void ScrollingWaveforms::showDeckMenu (int d, double sourceTime)
{
    auto& dv = decks[(size_t) d];
    if (dv.track == nullptr) return;
    const auto& st = engine.deck (d).state();

    auto fmt = [] (double s) { return juce::String ((int) s / 60) + ":" + juce::String (std::fmod (s, 60.0), 1).paddedLeft ('0', 4); };
    juce::PopupMenu menu;
    menu.addSectionHeader ("Deck " + juce::String (d + 1) + "  at " + fmt (sourceTime));
    menu.addItem (1, "Add locator here");
    if (dv.uiSkipFrom < 0.0)
        menu.addItem (2, "Start skip here");
    else
    {
        menu.addItem (3, "End skip here  (skips from " + fmt (dv.uiSkipFrom) + ")");
        menu.addItem (4, "Cancel skip start");
    }
    menu.addSeparator();
    menu.addItem (5, "Skips on (this deck)", true, st.skipsOn.load());
    menu.addItem (6, "Fold skipped sections in the waveform", true, foldSkips);
    menu.addItem (7, "Show stems in stem lanes", true, ! stemsHidden);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [this, d, sourceTime] (int result)
    {
        auto& deck = engine.deck (d);
        auto& v = decks[(size_t) d];
        switch (result)
        {
            case 1: deck.post (Deck::Cmd::locatorAdd, sourceTime); break;
            case 2: v.uiSkipFrom = sourceTime; needsRedraw = true; break;
            case 3: deck.post2 (Deck::Cmd::skipAdd, v.uiSkipFrom, sourceTime); v.uiSkipFrom = -1.0; break;
            case 4: v.uiSkipFrom = -1.0; needsRedraw = true; break;
            case 5: deck.post (Deck::Cmd::skipsToggle); break;
            case 6: foldSkips = ! foldSkips; needsRedraw = true; if (onFoldChanged) onFoldChanged (foldSkips); break;
            case 7: stemsHidden = ! stemsHidden; needsRedraw = true; break;
            default: break;
        }
    });
}

void ScrollingWaveforms::mouseMove (const juce::MouseEvent& e)
{
    if (dividerAt (e.getPosition()) >= 0)       setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (markerAt (e.position) != nullptr)  setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (nearPlayhead (e.getPosition()))    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else                                        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ScrollingWaveforms::mouseDown (const juce::MouseEvent& e)
{
    dragDeck = dragDivider = -1;
    dragPlayhead = false;
    const int d = deckBounds (0).contains (e.getPosition()) ? 0 : 1;

    if (e.mods.isPopupMenu())
    {
        if (auto* m = markerAt (e.position)) showMarkerMenu (*m);
        else showDeckMenu (d, sourceTimeAt (d, e.position.x));
        return;
    }
    if (auto* m = markerAt (e.position))
    {
        if (e.mods.isShiftDown()) deleteMarker (*m);
        return;
    }

    dragDivider = dividerAt (e.getPosition());
    if (dragDivider >= 0) return;
    if (nearPlayhead (e.getPosition())) { dragPlayhead = true; return; }
    dragDeck = d;
    dragStartPos = decks[(size_t) dragDeck].shownPos;
}

void ScrollingWaveforms::mouseDrag (const juce::MouseEvent& e)
{
    if (dragDivider >= 0)
    {
        auto r = deckBounds (dragDivider);
        const float share = (float) (e.y - r.getY() - dividerH / 2) / (float) juce::jmax (1, r.getHeight() - dividerH);
        setLaneSplit (dragDivider, share);
        repaint();
        return;
    }
    if (dragPlayhead)
    {
        setPlayheadPosition ((float) e.x / (float) juce::jmax (1, getWidth()));
        repaint();
        return;
    }
    if (dragDeck < 0) return;
    auto& deck = engine.deck (dragDeck);
    if (deck.state().playing.load()) return;
    const double pps = (double) getWidth() / visibleSeconds;
    const double disp = juce::jmax (0.0, dragStartPos - e.getDistanceFromDragStartX() / pps);
    deck.post (Deck::Cmd::seekSeconds, decks[(size_t) dragDeck].fold.toSource (disp));
}

void ScrollingWaveforms::mouseUp (const juce::MouseEvent&)
{
    if (dragDivider >= 0 && onLaneSplitChanged)
        onLaneSplitChanged (dragDivider, laneSplit[(size_t) dragDivider]);
    if (dragPlayhead && onPlayheadMoved)
        onPlayheadMoved (playhead);
    dragDeck = dragDivider = -1;
    dragPlayhead = false;
}

void ScrollingWaveforms::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    setVisibleSeconds (visibleSeconds * std::pow (1.25, -w.deltaY * 4.0));
    if (onZoomChanged) onZoomChanged (visibleSeconds);
}

} // namespace orbit
