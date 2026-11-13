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
        for (size_t i = 0; i < 4; ++i) { mix (st.stemGain[i].load()); mix (st.stemFilter[i].load()); }
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

ScrollingWaveforms::ScrollingWaveforms (AudioEngine& e, std::vector<int> deckIds)
    : engine (e), ids (std::move (deckIds)), vblank (this, [this] (double t) { onVBlank (t); })
{
    setOpaque (true);
}

ScrollingWaveforms::~ScrollingWaveforms()
{
    stemWavePool.removeAllJobs (true, 3000);
}

void ScrollingWaveforms::setVisibleSeconds (double s)
{
    visibleSeconds = juce::jlimit (zoomMin, zoomMax, s);
    needsRedraw = true;
}

void ScrollingWaveforms::setLanes (int deck, LaneConfig top, LaneConfig bottom)
{
    if (deck < 0 || deck >= numSlots()) return;
    lanes[(size_t) deck] = { top, bottom };
    needsRedraw = true;
}

void ScrollingWaveforms::onVBlank (double t)
{
    bool moving = false;
    const double latency = engine.getOutputLatencySeconds();

    for (int d = 0; d < numSlots(); ++d)
    {
        auto& deck = engine.deck (engineDeck (d));
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
        const bool foldActive = editView[(size_t) d] && st.skipsOn.load();
        std::vector<std::pair<double, double>> map;
        if (dv.track != nullptr)
            for (auto& s : dv.track->playMap) map.push_back ({ s.start, s.end });
        if (sk != dv.lastSkips || foldActive != dv.lastFoldActive || map != dv.lastMap)
        {
            dv.lastSkips = sk;
            dv.lastMap = map;
            dv.lastFoldActive = foldActive;
            const double len = dv.track != nullptr && dv.track->audio != nullptr ? dv.track->audio->lengthSeconds() : 1.0e6;
            if (foldActive) dv.fold.set (map, sk, len);
            else dv.fold.set ({}, {}, len);
            dv.smoother = {};
            needsRedraw = true;
        }

        const bool playing = st.playing.load();
        const double rate = st.tempoRatio.load();
        dv.fold.reference = dv.shownPos;
        const double raw = dv.fold.toDisplay (st.positionSec.load() - (playing ? (latency - extraLead) * rate : 0.0), st.playSegment.load());
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
    if (numSlots() == 1) return r;
    const int h = (r.getHeight() - 2) / 2;
    return deck == 0 ? r.withHeight (h) : r.withTrimmedTop (h + 2);
}

int ScrollingWaveforms::phraseStripHeight (int slot) const
{
    const auto& tr = decks[(size_t) slot].track;
    return tr != nullptr && ! tr->phrases.empty() && deckBounds (slot).getHeight() > 60 ? 13 : 0;
}

juce::Rectangle<int> ScrollingWaveforms::laneArea (int slot) const
{
    auto r = deckBounds (slot);
    const int h = phraseStripHeight (slot);
    if (numSlots() == 2 && slot == 1) r.removeFromTop (h);
    else r.removeFromBottom (h);
    return r;
}

juce::Rectangle<int> ScrollingWaveforms::phraseStripArea (int slot) const
{
    auto r = deckBounds (slot);
    const int h = phraseStripHeight (slot);
    return numSlots() == 2 && slot == 1 ? r.removeFromTop (h) : r.removeFromBottom (h);
}

juce::Rectangle<int> ScrollingWaveforms::dividerBounds (int deck) const
{
    auto r = laneArea (deck);
    if (! hasBottomLane (deck)) return { r.getX(), r.getBottom(), r.getWidth(), 0 };
    const int topH = (int) std::round ((r.getHeight() - dividerH) * laneSplit[(size_t) deck]);
    return { r.getX(), r.getY() + topH, r.getWidth(), dividerH };
}

int ScrollingWaveforms::dividerAt (juce::Point<int> p) const
{
    for (int d = 0; d < numSlots(); ++d)
        if (hasBottomLane (d) && dividerBounds (d).expanded (0, touchMode ? 14 : 3).contains (p))
            return d;
    return -1;
}

bool ScrollingWaveforms::nearPlayhead (juce::Point<int> p) const
{
    return std::abs ((float) p.x - playheadX (getLocalBounds())) <= (touchMode ? 18.0f : 5.0f) && dividerAt (p) < 0;
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
        case stemsSplit:
            if (stemsHidden) return "3-BAND (STEMS HIDDEN)";
            return dv.stemWave != nullptr ? (cfg.style == stemsSplit ? "STEMS (A ROW EACH)" : "STEMS") : "3-BAND (NO STEMS YET)";
        case rgb:          return "RGB";
        case singleColour: return "SINGLE COLOUR";
        case stemDrums: case stemBass: case stemInst: case stemVocals:
            return dv.stemWave != nullptr ? juce::String (stemName (cfg.style - stemDrums)).toUpperCase() + " ONLY" : juce::String ("3-BAND (NO STEMS YET)");
        default:           return "3-BAND";
    }
}

void ScrollingWaveforms::paint (juce::Graphics& g)
{
    markers.clear();
    g.fillAll (Theme::line);
    for (int d = 0; d < numSlots(); ++d)
        paintDeck (g, d, deckBounds (d));
}

juce::Rectangle<int> ScrollingWaveforms::drawLabel (juce::Graphics& g, juce::Rectangle<int> lane, const juce::String& text, bool atTop)
{
    if (lane.getHeight() < 20) return {};
    const auto f = psyched::Fonts::label (10.0f);
    const float w = f.getStringWidthFloat (text.toUpperCase()) + 14.0f;
    // left edge in line with the deck number square (above it is the badge and the lanes button)
    auto r = juce::Rectangle<float> ((float) lane.getX() + 6.0f, atTop ? (float) lane.getY() + 4.0f : (float) lane.getBottom() - 20.0f, w, 16.0f);
    if (r.intersects (labelAvoid)) r.setY (labelAvoid.getBottom() + 4.0f);
    g.setColour (psyched::Colours::field.withAlpha (0.85f));
    g.fillRect (r);
    g.setColour (psyched::Colours::edge2);
    g.drawRect (r, 1.0f);
    g.setColour (psyched::Colours::ink2);
    g.setFont (f);
    g.drawText (text.toUpperCase(), r, juce::Justification::centred);
    return r.toNearestInt();
}

std::vector<std::pair<int, juce::String>> ScrollingWaveforms::laneStyleChoices (int lane)
{
    std::vector<std::pair<int, juce::String>> v { { stems, "Stems (overlaid)" }, { stemsSplit, "Stems (a row each)" },
                                                   { threeBand, "3-Band" }, { rgb, "RGB" }, { singleColour, "Single colour" } };
    for (int s = 0; s < 4; ++s) v.push_back ({ stemDrums + s, juce::String (stemName (s)) + " only" });
    if (lane == 1) v.push_back ({ laneOff, "Off (one lane)" });
    return v;
}

void ScrollingWaveforms::showLaneStyleMenu (int slot, int lane)
{
    if (onLaneLabelTapped) { onLaneLabelTapped (slot, lane, labelRects[(size_t) slot][(size_t) lane]); return; }
    const int cur = lanes[(size_t) slot][(size_t) lane].style;
    juce::PopupMenu m;
    m.addSectionHeader (lane == 0 ? "Top lane" : "Second lane");
    m.addItem (stems + 10, "Stems (overlaid)", true, cur == stems);
    m.addItem (stemsSplit + 10, "Stems (a row each)", true, cur == stemsSplit);
    m.addItem (threeBand + 10, "3-Band", true, cur == threeBand);
    m.addItem (rgb + 10, "RGB", true, cur == rgb);
    m.addItem (singleColour + 10, "Single colour", true, cur == singleColour);
    m.addSeparator();
    for (int s = 0; s < 4; ++s)
        m.addItem (stemDrums + s + 10, juce::String (stemName (s)) + " only", true, cur == stemDrums + s);
    if (lane == 1) { m.addSeparator(); m.addItem (laneOff + 10, "Off (one lane)"); }
    juce::Component::SafePointer<ScrollingWaveforms> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (labelRects[(size_t) slot][(size_t) lane])),
                     [safe, slot, lane] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (safe->onLaneStyleChosen) safe->onLaneStyleChosen (safe->engineDeck (slot), lane, r - 10);
    });
}

void ScrollingWaveforms::paintDeck (juce::Graphics& g, int d, juce::Rectangle<int> full)
{
    auto r = laneArea (d);
    const bool two = hasBottomLane (d);
    auto div = dividerBounds (d);
    auto topLane = two ? r.withBottom (div.getY()) : r;
    auto bottomLane = two ? r.withTop (div.getBottom()) : r;

    paintLane (g, d, 0, topLane);
    if (two) paintLane (g, d, 1, bottomLane);

    // Phrase bars next to the deck's waveforms
    if (auto& tr = decks[(size_t) d].track; tr != nullptr && ! tr->phrases.empty() && phraseStripHeight (d) > 0)
    {
        const auto& dv = decks[(size_t) d];
        const double pps = (double) r.getWidth() / visibleSeconds;
        const float px0 = playheadX (r);
        g.setColour (juce::Colours::black);
        g.fillRect (phraseStripArea (d));
        auto strip = phraseStripArea (d).toFloat().reduced (0.0f, 1.0f);
        paintPhraseStripFolded (g, strip, tr->phrases, tr->audio != nullptr ? tr->audio->lengthSeconds() : tr->info.durationSec,
                                dv.fold, [&] (double disp) { return px0 + (float) ((disp - dv.shownPos) * pps); },
                                selectedPhrase ? selectedPhrase() : -1);
    }

    // Divider with a small grip
    if (two)
    {
        g.setColour (Theme::panel);
        g.fillRect (div);
        g.setColour (psyched::Colours::edge2);
        g.fillRect (div.withSizeKeepingCentre (36, 2).toFloat());
    }

    // Playhead through both lanes (drag it left/right to see more of what's coming)
    const auto& st = engine.deck (engineDeck (d)).state();
    const float playX = playheadX (r);
    g.setColour (st.playing.load() ? juce::Colour (0xffff2d2d) : juce::Colours::white);
    g.fillRect (playX - 1.0f, (float) full.getY(), 2.0f, (float) full.getHeight());
    juce::Path tri;
    tri.addTriangle (playX - 6.0f, (float) r.getY(), playX + 6.0f, (float) r.getY(), playX, (float) r.getY() + 7.0f);
    g.fillPath (tri);

    // Deck badge + lane labels (dark backing so they read over any waveform)
    auto badge = juce::Rectangle<float> ((float) r.getX() + 6.0f, (float) r.getY() + 4.0f, 22.0f, 18.0f);
    const int id = engineDeck (d);
    g.setColour (Theme::deckColour (id));
    g.fillRect (badge);
    g.setColour (psyched::Colours::onSolid);
    g.setFont (Theme::display (11.0f, true));
    g.drawText (id < 2 ? juce::String (id + 1) : juce::String ("P"), badge, juce::Justification::centred);

    labelAvoid = badge.withWidth (badge.getWidth() + 6.0f + 54.0f);
    labelRects[(size_t) d][0] = drawLabel (g, topLane, laneName (d, 0), ! two);
    labelRects[(size_t) d][1] = two ? drawLabel (g, bottomLane, laneName (d, 1), true) : juce::Rectangle<int>();
    labelAvoid = {};
    {
        // second lane on / off, right of the deck number square
        laneToggleRects[(size_t) d] = {};
        if (topLane.getHeight() >= 20)
        {
            auto tr = juce::Rectangle<int> ((int) badge.getRight() + 6, (int) badge.getY() + 1, 54, 16);
            g.setColour (psyched::Colours::field.withAlpha (0.85f));
            g.fillRect (tr);
            g.setColour (two ? Theme::deckColour (engineDeck (d)) : psyched::Colours::edge2);
            g.drawRect (tr, 1);
            g.setFont (psyched::Fonts::label (10.0f));
            g.setColour (two ? Theme::deckColour (engineDeck (d)) : psyched::Colours::ink2);
            g.drawText (two ? "2 LANES" : "1 LANE", tr, juce::Justification::centred);
            laneToggleRects[(size_t) d] = tr;
        }
    }
    // (Stem separation progress is shown under the deck's overview, against the whole track.)
}

void ScrollingWaveforms::paintLane (juce::Graphics& g, int d, int lane, juce::Rectangle<int> r)
{
    g.setColour (juce::Colours::black);
    g.fillRect (r);
    if (r.getHeight() < 8) return;

    auto& dv = decks[(size_t) d];
    const auto& st = engine.deck (engineDeck (d)).state();
    const auto cfg = lanes[(size_t) d][(size_t) lane];
    const bool stemsSeparate = cfg.style == stemsSplit;
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
            g.drawText (engineDeck (d) < 2 ? "Deck " + juce::String (engineDeck (d) + 1) + "  -  load a track" : juce::String ("PREP  -  load a track"),
                        r, juce::Justification::centred);
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
    const auto displayLen = [&fold] (double len) { return fold.displayLength (len); };

    const bool oneStem = isSingleStem (cfg.style);
    const bool showStems = (cfg.style == stems || cfg.style == stemsSplit || oneStem) && ! stemsHidden && dv.stemWave != nullptr && dv.stemWave->computed.load() > 0;
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
                              displayLen (wd->lengthSeconds()), pps, wave.getHeight(), scale,
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
            if (oneStem && s != cfg.style - stemDrums) continue;
            auto area = wave;
            if (stemsSeparate)
            {
                // vocals at the top, then inst, bass, drums at the bottom
                const int row = 3 - s;
                const int rowH = wave.getHeight() / 4;
                area = juce::Rectangle<int> (wave.getX(), wave.getY() + row * rowH, wave.getWidth(), row == 3 ? wave.getHeight() - 3 * rowH : rowH);
                if (row > 0) { g.setColour (Theme::line.withAlpha (0.5f)); g.fillRect (area.getX(), area.getY(), area.getWidth(), 1); }
            }
            auto& tc = dv.stemTiles[(size_t) lane][(size_t) s];
            const auto col = Theme::stemColour (s);
            tc.configure (sw.get(), styleKeyFor (s, stemsSeparate ? 7 : cfg.shape, col, fold.version), sw->entriesPerSecond, displayLen (sw->lengthSeconds()),
                          pps, area.getHeight(), scale,
                          [sw, s, col, o] (double first, double epp, int w, int h)
                          { return renderStemStrip (*sw, s, col, sw->computed.load(), first, epp, w, h, o); });
            tc.setValidEntries (valid);
            // how much of the stem is let through (its control is bipolar: centre = all of it)
            // (each stem has a volume and a filter: left of centre low-pass, right high-pass)
            const float cv = st.stemGain[(size_t) s].load(), fv = st.stemFilter[(size_t) s].load();
            const bool highPass = fv > 0.52f;
            float gain = juce::jmin (1.0f, cv * 2.0f) * (fv < 0.48f ? fv / 0.48f : fv > 0.52f ? (1.0f - fv) / 0.48f : 1.0f);
            gain *= juce::jlimit (0.0f, 1.0f, st.stemAuto[(size_t) s].load());   // stem locators too
            const float op = (stemsSeparate || oneStem) ? 1.0f : stemOpacity[(size_t) juce::jlimit (0, 3, s)];
            if (gain >= 0.999f) { tc.draw (g, area, t, px, op); continue; }
            // Turned down / low-passed: shrinks in towards the centre; high-passed: each column hollows out from the
            // centre in proportion to its own height (the loud parts open up first, so the shape stays). A faint
            // ghost of the whole stem stays, and fully off, its outline in the stem's colour.
            tc.draw (g, area, t, px, 0.16f * op);
            const float cy = (stemsSeparate || oneStem) ? (float) area.getCentreY() : baseY;
            auto direct = [&] (StripOptions so, float alpha)
            {
                // drawn straight for this frame (it changes as the knob turns, so it isn't cached)
                const double eps = sw->entriesPerSecond;
                const double left = (t * pps - (double) px) / pps * eps;
                auto img = renderStemStrip (*sw, s, col, valid, left, eps / pps, area.getWidth(), area.getHeight(), so);
                g.setOpacity (alpha);
                g.drawImageAt (img, area.getX(), area.getY());
                g.setOpacity (1.0f);
            };
            if (gain > 0.001f)
            {
                juce::Graphics::ScopedSaveState ss (g);
                if (highPass)
                {
                    auto so = o;
                    so.hollow = 1.0f - gain;
                    direct (so, 0.85f * op);
                }
                else
                {
                    g.addTransform (juce::AffineTransform::scale (1.0f, gain, 0.0f, cy));
                    tc.draw (g, area, t, px, (0.45f + 0.5f * gain) * op);
                }
            }
            // whenever it's turned down / filtered at all, the whole stem's outline stays, in its edge colour
            auto so = o;
            so.outline = true;
            direct (so, op);
        }
    }
    else
    {
        const int style = cfg.style == stems || cfg.style == stemsSplit ? threeBand : cfg.style;
        const auto wdStyle = style == rgb ? WaveformData::Style::rgb
                           : style == singleColour ? WaveformData::Style::blue : WaveformData::Style::threeBand;
        auto wd = wdStyle == WaveformData::Style::blue ? dv.track->waveformForSingleColour() : dv.track->waveformInStyle (wdStyle);
        if (wd != nullptr)
        {
            auto o = opts;
            o.entriesPerSecond = wd->entriesPerSecond;
            o.tint = style == singleColour ? (singleFollowsDeck ? Theme::deckColour (engineDeck (d)) : singleTint) : juce::Colour();
            auto detail = dv.track->detail;
            o.detail = detail.get();
            auto& tc = dv.laneTiles[(size_t) lane];
            tc.configure (wd.get(), styleKeyFor (style, cfg.shape, o.tint, fold.version), wd->entriesPerSecond,
                          displayLen (wd->lengthSeconds()), pps, wave.getHeight(), scale,
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
    paintOverlays (g, d, wave, r, flags, showStems && stemsSeparate);
}

void ScrollingWaveforms::paintOverlays (juce::Graphics& g, int d, juce::Rectangle<int> wave, juce::Rectangle<int> r, bool withFlags, bool stemRows)
{
    auto& dv = decks[(size_t) d];
    const auto& st = engine.deck (engineDeck (d)).state();
    const float playX = playheadX (r);
    const double pps = (double) r.getWidth() / visibleSeconds;
    const double t = dv.shownPos;
    const auto& fold = dv.fold;
    const double tStart = t - (playX - (float) r.getX()) / pps;
    const double tEnd = tStart + (double) r.getWidth() / pps;
    auto xOf = [&] (double sec) { return playX + (float) ((fold.toDisplay (sec) - t) * pps); };
    // On screen and played (in the edited view, a rearranged track's times aren't in order on screen)
    auto visible = [&] (double sec) { const float x = xOf (sec); return ! fold.hidden (sec) && x >= (float) r.getX() - 30.0f && x <= (float) r.getRight() + 30.0f; };
    const double srcStart = fold.isEmpty() ? tStart : 0.0, srcEnd = fold.isEmpty() ? tEnd : 1.0e9;
    auto& track = dv.track;
    const float flagTop = (float) r.getY() + 1.0f;

    // Each stretch of the track on screen: source range and where it starts on the display timeline
    struct Span { double s0, s1, d0; };
    std::vector<Span> spans;
    if (fold.isEmpty()) spans.push_back ({ tStart, tEnd, tStart });
    else
        for (auto& p : fold.pieces)
        {
            const double d1 = p.disp0 + (p.src1 - p.src0);
            if (d1 < tStart || p.disp0 > tEnd) continue;
            const double a = p.src0 + juce::jmax (0.0, tStart - p.disp0), b = p.src0 + (juce::jmin (tEnd, d1) - p.disp0);
            spans.push_back ({ a, b, p.disp0 + (a - p.src0) });
        }
    // Before the track starts and after it ends, the grid carries on (so a grid lined up on the drop shows its
    // first bar even when the track starts late)
    if (! fold.isEmpty())
    {
        if (tStart < 0.0) spans.push_back ({ fold.pieces.front().src0 + tStart, fold.pieces.front().src0, tStart });
        const double dEnd = fold.displayLength ((std::numeric_limits<double>::max)() / 4);
        if (tEnd > dEnd) spans.push_back ({ fold.pieces.back().src1, fold.pieces.back().src1 + (tEnd - dEnd), dEnd });
    }

    // Beat grid
    if (track->grid.isValid())
    {
        const bool dense = pps * track->grid.beatLengthAt (fold.toSource (t)) < 7.0;
        for (auto& sp : spans)
            track->grid.forEachBeat (sp.s0, sp.s1, [&] (double bt, int beatInBar, int)
            {
                const bool bar = beatInBar == 1;
                if (dense && ! bar) return;
                const float x = playX + (float) ((sp.d0 + (bt - sp.s0) - t) * pps);
                g.setColour (bar ? juce::Colours::white.withAlpha (0.55f) : juce::Colours::white.withAlpha (0.18f));
                g.fillRect (x, (float) wave.getY(), bar ? 1.5f : 1.0f, (float) wave.getHeight());
                if (withFlags)
                    g.fillRect (x - (bar ? 1.5f : 1.0f), (float) r.getY() + markerBand - 5.0f, bar ? 4.5f : 3.0f, 4.0f);
            });
    }

    // Edited view: where the track jumps (a rearranged or repeated phrase) - a dashed seam
    for (auto seam : fold.seams())
    {
        const float x = playX + (float) ((seam - t) * pps);
        if (x < (float) r.getX() || x > (float) r.getRight()) continue;
        g.setColour (Theme::accent.withAlpha (0.8f));
        for (float y = (float) wave.getY(); y < (float) wave.getBottom(); y += 8.0f)
            g.fillRect (x - 1.0f, y, 2.0f, 4.0f);
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
        if (fold.isEmpty() ? (f > srcEnd + 0.5 || to < srcStart - 0.5) : ! visible (f)) continue;
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
            if (fold.isEmpty() ? (f < srcStart - 0.5 || f > srcEnd + 0.5) : ! visible (f)) continue;
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
                if (auto name = cueName (engineDeck (d), false, i); name.isNotEmpty())
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

    // Stem locators (the four stems' state from here) and notes
    for (auto& l : track->stemLocs)
        if (visible (l.timeSec))
            paintStemLocatorFlag (g, l, xOf (l.timeSec), withFlags ? flagTop + markerBand : (float) wave.getY(), (float) pps,
                                  wave.toFloat(), stemRows, withFlags);
    for (auto& n : track->notes)
        if (visible (n.timeSec))
        {
            const float x = xOf (n.timeSec);
            g.setColour (n.colour.withAlpha (0.8f));
            for (float y = (float) wave.getY(); y < (float) wave.getBottom(); y += 6.0f) g.fillRect (x - 0.5f, y, 1.5f, 3.0f);
            if (withFlags)
            {
                auto box = juce::Rectangle<float> (x + 3.0f, (float) wave.getY() + 2.0f, juce::jmin (220.0f, Theme::font (11.0f).getStringWidthFloat (n.text) + 12.0f), 15.0f);
                g.setColour (psyched::Colours::void_.withAlpha (0.8f));
                g.fillRect (box);
                g.setColour (n.colour);
                g.setFont (Theme::font (11.0f, true));
                g.drawText (n.text, box.withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);
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
                if (auto name = cueName (engineDeck (d), true, i); name.isNotEmpty())
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
    auto& deck = engine.deck (engineDeck (m.deck));
    if (m.kind == markerHot)          deck.post (Deck::Cmd::hotCueDelete, 0.0, m.index, true);
    else if (m.kind == markerLocator) deck.post (Deck::Cmd::locatorDelete, 0.0, m.index);
    else if (m.kind == markerSkip)    deck.post (Deck::Cmd::skipDelete, 0.0, m.index);
}

void ScrollingWaveforms::showMarkerMenu (const Marker& m)
{
    const auto& st = engine.deck (engineDeck (m.deck)).state();
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
        menu.addSectionHeader ("Marker " + juce::String (m.index + 1));
    }
    else
    {
        menu.addSectionHeader ("Skip");
        menu.addItem (2, "Skips on (this deck)", true, st.skipsOn.load());
        menu.addItem (3, "Edited view - as it will play (this deck)", true, editView[(size_t) m.deck]);
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
        auto& deck = engine.deck (engineDeck (marker.deck));
        if (auto [mode, cycles] = loopModeFromMenu (result, 200, loopCycles); mode >= 0)
        {
            deck.post (Deck::Command { Deck::Cmd::locatorLoopMode, (double) mode, marker.index, true, nullptr, 0, (double) cycles });
            return;
        }
        auto setColour = [this, marker] (juce::Colour c)
        {
            engine.deck (engineDeck (marker.deck)).post (marker.kind == markerHot ? Deck::Cmd::hotCueColour : Deck::Cmd::locatorColour,
                                            (double) c.getARGB(), marker.index);
        };
        if (result == 1) deleteMarker (marker);
        else if (result == 2) deck.post (Deck::Cmd::skipsToggle);
        else if (result == 3) { auto& ev = editView[(size_t) marker.deck]; ev = ! ev; needsRedraw = true; if (onEditViewChanged) onEditViewChanged (engineDeck (marker.deck), ev); }
        else if (result == 4 && onRenameCue)
            onRenameCue (engineDeck (marker.deck), marker.kind == markerHot, marker.index,
                         cueName ? cueName (engineDeck (marker.deck), marker.kind == markerHot, marker.index) : juce::String());
        else if (handleCueColourMenu (result, 100, screen, current, setColour)) {}
    });
}

void ScrollingWaveforms::showDeckMenu (int d, double sourceTime)
{
    auto& dv = decks[(size_t) d];
    if (dv.track == nullptr) return;
    const auto& st = engine.deck (engineDeck (d)).state();

    auto fmt = [] (double s) { return juce::String ((int) s / 60) + ":" + juce::String (std::fmod (s, 60.0), 1).paddedLeft ('0', 4); };
    juce::PopupMenu menu;
    const double lb = loopBeatsForMenu ? loopBeatsForMenu() : 4.0;
    menu.addSectionHeader ((engineDeck (d) < 2 ? "Deck " + juce::String (engineDeck (d) + 1) : juce::String ("PREP")) + "  at " + fmt (sourceTime));
    menu.addItem (1, "Add marker here");
    menu.addItem (8, "Add loop marker here (" + juce::String (lb, lb < 1.0 ? 2 : 0) + " beats)");
    if (dv.uiSkipFrom < 0.0)
        menu.addItem (2, "Start skip here");
    else
    {
        menu.addItem (3, "End skip here  (skips from " + fmt (dv.uiSkipFrom) + ")");
        menu.addItem (4, "Cancel skip start");
    }
    menu.addSeparator();
    menu.addItem (5, "Skips on (this deck)", true, st.skipsOn.load());
    menu.addItem (6, "Edited view - as it will play (skips folded out, phrases in play order)", true, editView[(size_t) d]);
    menu.addItem (7, "Show stems in stem lanes", true, ! stemsHidden);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [this, d, sourceTime, lb] (int result)
    {
        auto& deck = engine.deck (engineDeck (d));
        auto& v = decks[(size_t) d];
        switch (result)
        {
            case 1: deck.post (Deck::Cmd::locatorAdd, sourceTime); break;
            case 2: v.uiSkipFrom = sourceTime; needsRedraw = true; break;
            case 3: deck.post2 (Deck::Cmd::skipAdd, v.uiSkipFrom, sourceTime); v.uiSkipFrom = -1.0; break;
            case 4: v.uiSkipFrom = -1.0; needsRedraw = true; break;
            case 5: deck.post (Deck::Cmd::skipsToggle); break;
            case 6: editView[(size_t) d] = ! editView[(size_t) d]; needsRedraw = true; if (onEditViewChanged) onEditViewChanged (engineDeck (d), editView[(size_t) d]); break;
            case 7: stemsHidden = ! stemsHidden; needsRedraw = true; break;
            case 8:
            {
                double s0 = sourceTime, s1 = sourceTime + lb * 0.5;
                if (v.track != nullptr && v.track->grid.isValid())
                {
                    const double b = std::round (v.track->grid.beatPosition (sourceTime));
                    s0 = v.track->grid.timeAtBeat (b);
                    s1 = v.track->grid.timeAtBeat (b + lb);
                }
                deck.post (Deck::Command { Deck::Cmd::locatorLoopAdd, juce::jmax (0.0, s0), 0, true, nullptr, 0, s1 });
                break;
            }
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
    const int d = numSlots() == 1 || deckBounds (0).contains (e.getPosition()) ? 0 : 1;

    for (int s = 0; s < numSlots(); ++s)
    {
        for (int l = 0; l < 2; ++l)
            if (labelRects[(size_t) s][(size_t) l].contains (e.getPosition())) { showLaneStyleMenu (s, l); return; }
        if (laneToggleRects[(size_t) s].contains (e.getPosition()))
        {
            const bool two = hasBottomLane (s);
            if (two) lastBottomStyle[(size_t) s] = lanes[(size_t) s][1].style;
            if (onLaneStyleChosen) onLaneStyleChosen (engineDeck (s), 1, two ? (int) laneOff : lastBottomStyle[(size_t) s]);
            return;
        }
    }
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
    if (touchMode) return;   // the jog wheels scrub; a finger on the screen never moves the track
    dragDeck = d;
    dragStartPos = decks[(size_t) dragDeck].shownPos;
    // While playing, grabbing the waveform holds it like a hand on the platter; dragging scrubs it (you hear it)
    auto& dk = engine.deck (engineDeck (d));
    dragScrub = dk.state().playing.load();
    dragLastX = e.position.x;
    if (dragScrub) dk.post (Deck::Cmd::scrubHold, 1.0, 0, true);
}

void ScrollingWaveforms::mouseDrag (const juce::MouseEvent& e)
{
    if (dragDivider >= 0)
    {
        auto r = laneArea (dragDivider);
        const float share = (float) (e.y - r.getY() - dividerH / 2) / (float) juce::jmax (1, r.getHeight() - dividerH);
        // The decks mirror each other: deck 2's split is flipped so the lanes meet in the middle the same way
        const float s0 = dragDivider == 0 ? share : 1.0f - share;
        for (int k = 0; k < numSlots(); ++k) setLaneSplit (k, juce::jlimit (0.1f, 0.9f, k == 0 ? s0 : 1.0f - s0));
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
    auto& deck = engine.deck (engineDeck (dragDeck));
    if (dragScrub)
    {
        // waveform moved by dx pixels = that much of the track, as scrub clicks (1/40 of a platter turn each)
        const double pps = (double) getWidth() / visibleSeconds;
        const double dsec = -(e.position.x - dragLastX) / pps;
        dragLastX = e.position.x;
        if (dsec != 0.0) deck.post (Deck::Cmd::scrub, dsec * 40.0 / Deck::secondsPerRevolution);
        return;
    }
    const double pps = (double) getWidth() / visibleSeconds;
    const double disp = juce::jmax (0.0, dragStartPos - e.getDistanceFromDragStartX() / pps);
    deck.post (Deck::Cmd::seekSeconds, decks[(size_t) dragDeck].fold.toSource (disp));
}

void ScrollingWaveforms::mouseUp (const juce::MouseEvent&)
{
    if (dragScrub && dragDeck >= 0) engine.deck (engineDeck (dragDeck)).post (Deck::Cmd::scrubHold, 0.0, 0, false);
    dragScrub = false;
    if (dragDivider >= 0 && onLaneSplitChanged)
        for (int k = 0; k < numSlots(); ++k) onLaneSplitChanged (engineDeck (k), laneSplit[(size_t) k]);
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
