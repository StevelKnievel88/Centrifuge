#include "ScrollingWaveforms.h"
#include "Theme.h"

namespace orbit
{

namespace
{
    constexpr int markerBand = 14;   // space above the waveform for cue labels

    juce::uint64 stateHash (const Deck::Published& st)
    {
        juce::uint64 h = 1469598103934665603ull;
        auto mix = [&h] (double v) { h ^= (juce::uint64) std::llround (v * 1000.0); h *= 1099511628211ull; };
        mix (st.loopActive.load()); mix (st.loopInSec.load()); mix (st.loopOutSec.load()); mix (st.cuePointSec.load());
        for (size_t i = 0; i < 8; ++i) { mix (st.hotStart[i].load()); mix ((double) st.hotColour[i].load()); }
        mix (std::floor (st.stemsProgress.load() * 100.0f));
        mix (st.stemsReady.load());
        return h;
    }
}

ScrollingWaveforms::ScrollingWaveforms (AudioEngine& e)
    : engine (e), vblank (this, [this] (double t) { onVBlank (t); })
{
    setOpaque (true);
}

void ScrollingWaveforms::setVisibleSeconds (double s)
{
    visibleSeconds = juce::jlimit (2.0, 60.0, s);
    needsRedraw = true;
}

void ScrollingWaveforms::onVBlank (double t)
{
    bool moving = false;
    static thread_local std::array<juce::uint64, 2> lastHash { 0, 0 };
    const double latency = engine.getOutputLatencySeconds();

    for (int d = 0; d < 2; ++d)
    {
        auto& deck = engine.deck (d);
        const auto& st = deck.state();

        const int serial = st.trackSerial.load();
        if (serial != serials[(size_t) d])
        {
            serials[(size_t) d] = serial;
            tracks[(size_t) d] = serial != 0 ? deck.getLoadedTrack() : nullptr;
            needsRedraw = true;
        }

        const bool playing = st.playing.load();
        const double rate = st.tempoRatio.load();
        const double raw = st.positionSec.load() - (playing ? latency * rate : 0.0);
        const double prev = shownPos[(size_t) d];
        shownPos[(size_t) d] = smoothers[(size_t) d].update (raw, playing, rate, t);
        if (std::abs (shownPos[(size_t) d] - prev) > 1.0e-6) moving = true;

        const auto h = stateHash (st);
        if (h != lastHash[(size_t) d]) { lastHash[(size_t) d] = h; needsRedraw = true; }
    }

    if ((moving || needsRedraw) && t - lastPaintTime >= 1.0 / maxFps - 0.002)
    {
        lastPaintTime = t;
        needsRedraw = false;
        repaint();
    }
}

juce::Rectangle<int> ScrollingWaveforms::laneBounds (int deck) const
{
    auto r = getLocalBounds();
    const int h = (r.getHeight() - 1) / 2;
    return deck == 0 ? r.withHeight (h) : r.withTrimmedTop (h + 1);
}

int ScrollingWaveforms::laneAt (juce::Point<int> p) const
{
    return laneBounds (0).contains (p) ? 0 : 1;
}

void ScrollingWaveforms::paint (juce::Graphics& g)
{
    g.fillAll (Theme::line);
    paintLane (g, 0, laneBounds (0));
    paintLane (g, 1, laneBounds (1));
}

void ScrollingWaveforms::paintLane (juce::Graphics& g, int d, juce::Rectangle<int> r)
{
    g.setColour (Theme::waveBg);
    g.fillRect (r);

    const auto& st = engine.deck (d).state();
    const auto deckCol = Theme::deckColour (d);
    const float playX = (float) r.getX() + (float) r.getWidth() * 0.5f;
    const double pps = (double) r.getWidth() / visibleSeconds;
    auto wave = r.withTrimmedTop (markerBand).withTrimmedBottom (4);
    auto& track = tracks[(size_t) d];

    // centre line
    g.setColour (Theme::line.withAlpha (0.6f));
    g.fillRect ((float) r.getX(), (float) wave.getCentreY(), (float) r.getWidth(), 1.0f);

    if (track == nullptr || track->waveform == nullptr)
    {
        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (14.0f));
        g.drawText ("Deck " + juce::String (d + 1) + "  -  load a track", r, juce::Justification::centred);
    }
    else
    {
        const double t = shownPos[(size_t) d];
        const double tStart = t - (playX - (float) r.getX()) / pps;
        const double tEnd = tStart + (double) r.getWidth() / pps;
        auto xOf = [&] (double sec) { return playX + (float) ((sec - t) * pps); };

        // Loop region (behind the waveform)
        const double li = st.loopInSec.load(), lo = st.loopOutSec.load();
        if (li >= 0.0 && lo > li && lo > tStart && li < tEnd)
        {
            const bool active = st.loopActive.load();
            const float x0 = juce::jmax ((float) r.getX(), xOf (li)), x1 = juce::jmin ((float) r.getRight(), xOf (lo));
            g.setColour ((active ? Theme::accent : Theme::textFaint).withAlpha (active ? 0.22f : 0.12f));
            g.fillRect (x0, (float) wave.getY(), x1 - x0, (float) wave.getHeight());
        }

        tiles[(size_t) d].configure (track->waveform.get(), pps, wave.getHeight(),
                                     g.getInternalContext().getPhysicalPixelScaleFactor());
        tiles[(size_t) d].draw (g, wave, t, playX - (float) wave.getX());

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
                g.setColour (bar ? juce::Colour (0xffff3b3b).withAlpha (0.75f) : juce::Colours::white.withAlpha (0.22f));
                g.fillRect (x, (float) wave.getY(), bar ? 1.5f : 1.0f, (float) wave.getHeight());
                g.fillRect (x - (bar ? 1.5f : 1.0f), (float) r.getY() + markerBand - 5.0f, bar ? 4.5f : 3.0f, 4.0f);
            }
        }

        // Loop borders
        if (li >= 0.0 && lo > li)
        {
            g.setColour ((st.loopActive.load() ? Theme::accent : Theme::textFaint).withAlpha (0.9f));
            for (double edge : { li, lo })
                if (edge >= tStart && edge <= tEnd)
                    g.fillRect (xOf (edge) - 1.0f, (float) wave.getY(), 2.0f, (float) wave.getHeight());
        }

        // Memory cues (red, top)
        for (auto& m : track->memoryCues)
        {
            if (m.startSec < tStart || m.startSec > tEnd) continue;
            const float x = xOf (m.startSec);
            juce::Path p;
            p.addTriangle (x - 5.0f, (float) r.getY() + 1.0f, x + 5.0f, (float) r.getY() + 1.0f, x, (float) r.getY() + 9.0f);
            g.setColour (m.colour.isTransparent() ? Theme::bad : m.colour);
            g.fillPath (p);
        }

        // Hot cues (coloured flags with letters)
        g.setFont (Theme::font (10.5f, true));
        for (int i = 0; i < 8; ++i)
        {
            const double hs = st.hotStart[(size_t) i].load();
            if (hs < 0.0 || hs < tStart - 1.0 || hs > tEnd) continue;
            const float x = xOf (hs);
            const auto col = juce::Colour (st.hotColour[(size_t) i].load());
            g.setColour (col.withAlpha (0.85f));
            g.fillRect (x, (float) wave.getY(), 1.5f, (float) wave.getHeight());
            auto flag = juce::Rectangle<float> (x, (float) r.getY() + 1.0f, 14.0f, (float) markerBand - 2.0f);
            g.setColour (col);
            g.fillRoundedRectangle (flag, 2.0f);
            g.setColour (juce::Colours::black);
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)), flag, juce::Justification::centred);
        }

        // Cue point (orange triangle at the bottom, CDJ style)
        const double cp = st.cuePointSec.load();
        if (cp >= tStart && cp <= tEnd)
        {
            const float x = xOf (cp);
            juce::Path p;
            p.addTriangle (x - 6.0f, (float) r.getBottom(), x + 6.0f, (float) r.getBottom(), x, (float) r.getBottom() - 8.0f);
            g.setColour (Theme::accent);
            g.fillPath (p);
        }

        // Track start / end shading
        if (tStart < 0.0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect ((float) r.getX(), (float) wave.getY(), xOf (0.0) - (float) r.getX(), (float) wave.getHeight());
        }
    }

    // Playhead
    const bool playing = st.playing.load();
    g.setColour (playing ? juce::Colour (0xffff2d2d) : juce::Colours::white);
    g.fillRect (playX - 1.0f, (float) r.getY(), 2.0f, (float) r.getHeight());
    juce::Path tri;
    tri.addTriangle (playX - 6.0f, (float) r.getY(), playX + 6.0f, (float) r.getY(), playX, (float) r.getY() + 7.0f);
    g.fillPath (tri);

    // Deck badge
    auto badge = juce::Rectangle<float> ((float) r.getX() + 6.0f, (float) r.getY() + 4.0f, 22.0f, 18.0f);
    g.setColour (deckCol);
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (juce::Colours::black);
    g.setFont (Theme::font (13.0f, true));
    g.drawText (juce::String (d + 1), badge, juce::Justification::centred);

    // Stem separation progress
    if (st.stemsReady.load())
    {
        const float p = st.stemsProgress.load();
        if (p < 0.999f)
        {
            g.setColour (deckCol.withAlpha (0.8f));
            g.fillRect ((float) r.getX(), (float) r.getBottom() - 3.0f, (float) r.getWidth() * p, 3.0f);
            g.setFont (Theme::font (11.0f, true));
            g.drawText ("STEMS " + juce::String (juce::roundToInt (p * 100.0f)) + "%",
                        r.withTrimmedLeft (34).withHeight (22).toFloat(), juce::Justification::centredLeft);
        }
    }
}

void ScrollingWaveforms::mouseDown (const juce::MouseEvent& e)
{
    dragDeck = laneAt (e.getPosition());
    dragStartPos = shownPos[(size_t) dragDeck];
}

void ScrollingWaveforms::mouseDrag (const juce::MouseEvent& e)
{
    if (dragDeck < 0) return;
    auto& deck = engine.deck (dragDeck);
    if (deck.state().playing.load()) return;
    const double pps = (double) getWidth() / visibleSeconds;
    deck.post (Deck::Cmd::seekSeconds, juce::jmax (0.0, dragStartPos - e.getDistanceFromDragStartX() / pps));
}

void ScrollingWaveforms::mouseUp (const juce::MouseEvent&)
{
    dragDeck = -1;
}

void ScrollingWaveforms::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    setVisibleSeconds (visibleSeconds * std::pow (1.25, -w.deltaY * 4.0));
    if (onZoomChanged) onZoomChanged (visibleSeconds);
}

} // namespace orbit
