#include "ScrollingWaveforms.h"
#include "Theme.h"

namespace orbit
{

namespace
{
    constexpr int markerBand = 14;   // space above the main lane for cue flags
    constexpr int dividerH = 5;      // draggable gap between the two lanes of a deck
    constexpr int stemDrawOrder[] = { 2, 1, 3, 0 };   // inst at the back, drums in front

    juce::uint64 stateHash (const Deck::Published& st)
    {
        juce::uint64 h = 1469598103934665603ull;
        auto mix = [&h] (double v) { h ^= (juce::uint64) std::llround (v * 1000.0); h *= 1099511628211ull; };
        mix (st.loopActive.load()); mix (st.loopInSec.load()); mix (st.loopOutSec.load()); mix (st.cuePointSec.load());
        for (size_t i = 0; i < 8; ++i) { mix (st.hotStart[i].load()); mix ((double) st.hotColour[i].load()); }
        for (size_t i = 0; i < 4; ++i) mix (st.stemGain[i].load());
        mix (std::floor (st.stemsProgress.load() * 100.0f));
        mix (st.stemsReady.load());
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
    visibleSeconds = juce::jlimit (2.0, 60.0, s);
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
            if (c - dv.lastComputed > 300 || (c == dv.stemWave->total && c != dv.lastComputed))
            {
                dv.lastComputed = c;
                needsRedraw = true;
            }
        }

        const bool playing = st.playing.load();
        const double rate = st.tempoRatio.load();
        const double raw = st.positionSec.load() - (playing ? latency * rate : 0.0);
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
    const int mainH = (int) std::round ((r.getHeight() - dividerH) * laneSplit[(size_t) deck]);
    return { r.getX(), r.getY() + mainH, r.getWidth(), dividerH };
}

int ScrollingWaveforms::dividerAt (juce::Point<int> p) const
{
    for (int d = 0; d < 2; ++d)
        if (dividerBounds (d).expanded (0, 3).contains (p))
            return d;
    return -1;
}

void ScrollingWaveforms::paint (juce::Graphics& g)
{
    g.fillAll (Theme::line);
    paintDeck (g, 0, deckBounds (0));
    paintDeck (g, 1, deckBounds (1));
}

void ScrollingWaveforms::paintDeck (juce::Graphics& g, int d, juce::Rectangle<int> r)
{
    auto div = dividerBounds (d);
    auto mainLane = r.withBottom (div.getY());
    auto origLane = r.withTop (div.getBottom());

    paintLane (g, d, mainLane, true);
    paintLane (g, d, origLane, false);

    // Divider with a small grip
    g.setColour (Theme::panel);
    g.fillRect (div);
    g.setColour (Theme::line.brighter (0.4f));
    g.fillRoundedRectangle (div.withSizeKeepingCentre (36, 2).toFloat(), 1.0f);

    // Playhead through both lanes
    const auto& st = engine.deck (d).state();
    const float playX = (float) r.getX() + (float) r.getWidth() * 0.5f;
    g.setColour (st.playing.load() ? juce::Colour (0xffff2d2d) : juce::Colours::white);
    g.fillRect (playX - 1.0f, (float) r.getY(), 2.0f, (float) r.getHeight());
    juce::Path tri;
    tri.addTriangle (playX - 6.0f, (float) r.getY(), playX + 6.0f, (float) r.getY(), playX, (float) r.getY() + 7.0f);
    g.fillPath (tri);

    // Deck badge + lane labels
    auto badge = juce::Rectangle<float> ((float) r.getX() + 6.0f, (float) r.getY() + 4.0f, 22.0f, 18.0f);
    g.setColour (Theme::deckColour (d));
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (juce::Colours::black);
    g.setFont (Theme::font (13.0f, true));
    g.drawText (juce::String (d + 1), badge, juce::Justification::centred);

    auto& dv = decks[(size_t) d];
    const bool showingStems = mainMode == 0 && dv.stemWave != nullptr && dv.stemWave->computed.load() > 0;
    g.setFont (Theme::font (10.0f, true));
    g.setColour (Theme::textFaint);
    g.drawText (showingStems ? "STEMS" : "3-BAND", mainLane.withTrimmedLeft (8).removeFromBottom (16), juce::Justification::centredLeft);
    if (origLane.getHeight() > 16)
        g.drawText ("ORIGINAL", origLane.withTrimmedLeft (8).withHeight (16), juce::Justification::centredLeft);

    // Stem separation progress
    if (st.stemsReady.load())
    {
        const float p = st.stemsProgress.load();
        if (p < 0.999f)
        {
            g.setColour (Theme::deckColour (d).withAlpha (0.8f));
            g.fillRect ((float) mainLane.getX(), (float) mainLane.getBottom() - 3.0f, (float) mainLane.getWidth() * p, 3.0f);
            g.setFont (Theme::font (11.0f, true));
            g.drawText ("SEPARATING " + juce::String (juce::roundToInt (p * 100.0f)) + "%",
                        juce::Rectangle<int> (r.getX() + 90, r.getY() + 4, 200, 18), juce::Justification::centredLeft);
        }
    }
}

void ScrollingWaveforms::paintLane (juce::Graphics& g, int d, juce::Rectangle<int> r, bool mainLane)
{
    g.setColour (Theme::waveBg);
    g.fillRect (r);
    if (r.getHeight() < 8) return;

    auto& dv = decks[(size_t) d];
    const auto& st = engine.deck (d).state();
    const float playX = (float) r.getX() + (float) r.getWidth() * 0.5f;
    const double pps = (double) r.getWidth() / visibleSeconds;
    auto wave = mainLane ? r.withTrimmedTop (markerBand).withTrimmedBottom (3) : r.reduced (0, 2);
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();

    g.setColour (Theme::line.withAlpha (0.6f));
    g.fillRect ((float) r.getX(), (float) wave.getCentreY(), (float) r.getWidth(), 1.0f);

    if (dv.track == nullptr || dv.track->waveform == nullptr)
    {
        if (mainLane)
        {
            g.setColour (Theme::textFaint);
            g.setFont (Theme::font (14.0f));
            g.drawText ("Deck " + juce::String (d + 1) + "  -  load a track", r, juce::Justification::centred);
        }
        return;
    }

    const double t = dv.shownPos;
    const double tStart = t - (playX - (float) r.getX()) / pps;
    const double tEnd = tStart + (double) r.getWidth() / pps;
    auto xOf = [&] (double sec) { return playX + (float) ((sec - t) * pps); };

    // Loop region behind the waveform
    const double li = st.loopInSec.load(), lo = st.loopOutSec.load();
    if (li >= 0.0 && lo > li && lo > tStart && li < tEnd)
    {
        const bool active = st.loopActive.load();
        const float x0 = juce::jmax ((float) r.getX(), xOf (li)), x1 = juce::jmin ((float) r.getRight(), xOf (lo));
        g.setColour ((active ? Theme::accent : Theme::textFaint).withAlpha (active ? 0.22f : 0.12f));
        g.fillRect (x0, (float) wave.getY(), x1 - x0, (float) wave.getHeight());
    }

    const float px = playX - (float) wave.getX();

    if (mainLane && mainMode == 0 && dv.stemWave != nullptr && dv.stemWave->computed.load() > 0)
    {
        // While stems are still being separated, the not-yet-separated part shows the 3-band waveform faintly.
        if (dv.stemWave->computed.load() < dv.stemWave->total)
            if (auto wd = dv.track->waveform)
            {
                dv.mainTiles.configure (wd.get(), 0, wd->entriesPerSecond, wd->lengthSeconds(), pps, wave.getHeight(), scale,
                                        [wd] (double first, double epp, int w, int h) { return renderWaveformStrip (*wd, first, epp, w, h, false); });
                juce::Graphics::ScopedSaveState ss (g);
                const float sepX = xOf (dv.stemWave->computed.load() / dv.stemWave->entriesPerSecond);
                g.excludeClipRegion (wave.withRight ((int) sepX));
                dv.mainTiles.draw (g, wave, t, px, 0.3f);
            }

        // Four stems overlaid, each as bright as its knob is up (a muted stem stays as a faint ghost).
        auto sw = dv.stemWave;
        const int valid = sw->computed.load();
        for (int s : stemDrawOrder)
        {
            auto& tc = dv.stemTiles[(size_t) s];
            const auto col = Theme::stemColour (s);
            tc.configure (sw.get(), s, sw->entriesPerSecond, sw->lengthSeconds(), pps, wave.getHeight(), scale,
                          [sw, s, col] (double first, double epp, int w, int h)
                          { return renderStemStrip (*sw, s, col, sw->computed.load(), first, epp, w, h); });
            tc.setValidEntries (valid);
            const float gain = st.stemGain[(size_t) s].load();
            tc.draw (g, wave, t, px, gain <= 0.001f ? 0.12f : 0.3f + 0.7f * gain);
        }
    }
    else
    {
        const auto style = mainLane ? WaveformData::Style::threeBand : originalStyle;
        auto wd = dv.track->waveformInStyle (style);
        if (wd != nullptr)
        {
            auto& tc = mainLane ? dv.mainTiles : dv.originalTiles;
            tc.configure (wd.get(), (int) style, wd->entriesPerSecond, wd->lengthSeconds(), pps, wave.getHeight(), scale,
                          [wd] (double first, double epp, int w, int h) { return renderWaveformStrip (*wd, first, epp, w, h, false); });
            tc.draw (g, wave, t, px);
        }
    }

    paintOverlays (g, d, wave, r, mainLane);
}

void ScrollingWaveforms::paintOverlays (juce::Graphics& g, int d, juce::Rectangle<int> wave, juce::Rectangle<int> r, bool withFlags)
{
    auto& dv = decks[(size_t) d];
    const auto& st = engine.deck (d).state();
    const float playX = (float) r.getX() + (float) r.getWidth() * 0.5f;
    const double pps = (double) r.getWidth() / visibleSeconds;
    const double t = dv.shownPos;
    const double tStart = t - (playX - (float) r.getX()) / pps;
    const double tEnd = tStart + (double) r.getWidth() / pps;
    auto xOf = [&] (double sec) { return playX + (float) ((sec - t) * pps); };
    auto& track = dv.track;

    // Beat grid
    if (track->grid.isValid())
    {
        const auto& beats = track->grid.getBeats();
        int i = juce::jmax (0, track->grid.beatIndexAt (tStart));
        const bool dense = pps * track->grid.beatLengthAt (t) < 7.0;
        for (; i < (int) beats.size() && beats[(size_t) i].timeSec <= tEnd; ++i)
        {
            const auto& b = beats[(size_t) i];
            const bool bar = b.beatInBar == 1;
            if (dense && ! bar) continue;
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
            if (edge >= tStart && edge <= tEnd)
                g.fillRect (xOf (edge) - 1.0f, (float) wave.getY(), 2.0f, (float) wave.getHeight());
    }

    // Memory cues
    for (auto& m : track->memoryCues)
    {
        if (m.startSec < tStart || m.startSec > tEnd) continue;
        const float x = xOf (m.startSec);
        g.setColour (m.colour.isTransparent() ? Theme::bad : m.colour);
        if (withFlags)
        {
            juce::Path p;
            p.addTriangle (x - 5.0f, (float) r.getY() + 1.0f, x + 5.0f, (float) r.getY() + 1.0f, x, (float) r.getY() + 9.0f);
            g.fillPath (p);
        }
        else
            g.fillRect (x - 0.5f, (float) wave.getY(), 1.0f, 6.0f);
    }

    // Hot cues
    g.setFont (Theme::font (10.5f, true));
    for (int i = 0; i < 8; ++i)
    {
        const double hs = st.hotStart[(size_t) i].load();
        if (hs < 0.0 || hs < tStart - 1.0 || hs > tEnd) continue;
        const float x = xOf (hs);
        const auto col = juce::Colour (st.hotColour[(size_t) i].load());
        g.setColour (col.withAlpha (0.85f));
        g.fillRect (x, (float) wave.getY(), 1.5f, (float) wave.getHeight());
        if (withFlags)
        {
            auto flag = juce::Rectangle<float> (x, (float) r.getY() + 1.0f, 14.0f, (float) markerBand - 2.0f);
            g.setColour (col);
            g.fillRoundedRectangle (flag, 2.0f);
            g.setColour (juce::Colours::black);
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)), flag, juce::Justification::centred);
        }
    }

    // Cue point
    const double cp = st.cuePointSec.load();
    if (withFlags && cp >= tStart && cp <= tEnd)
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
void ScrollingWaveforms::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (dividerAt (e.getPosition()) >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void ScrollingWaveforms::mouseDown (const juce::MouseEvent& e)
{
    dragDivider = dividerAt (e.getPosition());
    if (dragDivider >= 0) return;
    dragDeck = deckBounds (0).contains (e.getPosition()) ? 0 : 1;
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
    if (dragDeck < 0) return;
    auto& deck = engine.deck (dragDeck);
    if (deck.state().playing.load()) return;
    const double pps = (double) getWidth() / visibleSeconds;
    deck.post (Deck::Cmd::seekSeconds, juce::jmax (0.0, dragStartPos - e.getDistanceFromDragStartX() / pps));
}

void ScrollingWaveforms::mouseUp (const juce::MouseEvent&)
{
    if (dragDivider >= 0 && onLaneSplitChanged)
        onLaneSplitChanged (dragDivider, laneSplit[(size_t) dragDivider]);
    dragDeck = dragDivider = -1;
}

void ScrollingWaveforms::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    setVisibleSeconds (visibleSeconds * std::pow (1.25, -w.deltaY * 4.0));
    if (onZoomChanged) onZoomChanged (visibleSeconds);
}

} // namespace orbit
