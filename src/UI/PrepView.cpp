#include "PrepView.h"
#include "Theme.h"
#include "../Audio/MusicalKey.h"

namespace orbit
{

namespace
{
    juce::String formatTime (double sec, bool tenths = true)
    {
        const bool neg = sec < 0;
        sec = std::abs (sec);
        const int m = (int) (sec / 60.0);
        const double s = sec - m * 60.0;
        auto str = juce::String (m) + ":" + (tenths ? juce::String (s, 1).paddedLeft ('0', 4) : juce::String ((int) s).paddedLeft ('0', 2));
        return neg ? "-" + str : str;
    }

    const juce::Colour skipColour { 0xff38d9ff };
}

PrepView::PrepView (AudioEngine& e, TrackEditor& ed) : engine (e), editor (ed)
{
    setOpaque (true);
    startTimerHz (30);
}

void PrepView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    listArea = r.removeFromRight (juce::jlimit (240, 360, r.getWidth() / 4));
    r.removeFromRight (10);
    rowHeader = r.removeFromTop (44);
    r.removeFromTop (6);
    rowOverview = r.removeFromTop (40);
    r.removeFromTop (6);
    rowGrid = r.removeFromBottom (26);
    r.removeFromBottom (6);
    rowPads = r.removeFromBottom (28);
    r.removeFromBottom (6);
    rowTransport = r.removeFromBottom (30);
    r.removeFromBottom (6);
    rowWave = r;
    overviewWidth = 0;
}

void PrepView::post (Deck::Cmd cmd, double value, int index, bool down)
{
    deck().post (cmd, value, index, down);
}

void PrepView::timerCallback()
{
    const auto& st = deck().state();
    const int s = st.trackSerial.load();
    if (s != serial || st.trackDataVersion.load() != dataVersion)
    {
        if (s != serial) { overviewWidth = 0; listScroll = 0; }
        serial = s;
        dataVersion = st.trackDataVersion.load();
        track = s != 0 ? deck().getLoadedTrack() : nullptr;
        repaint();
        return;
    }
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (double v) { h ^= (juce::uint64) std::llround (v * 1000.0); h *= 1099511628211ull; };
    mix (st.positionSec.load()); mix (st.playing.load()); mix (st.cueEdits.load()); mix (st.loopActive.load());
    mix (st.loopInSec.load()); mix (st.loopOutSec.load()); mix (st.skipPendingFrom.load()); mix (st.cyclesLeft.load());
    mix (editor.editsRekordbox (deckIndex)); mix (editor.editsCentrifuge (deckIndex)); mix (editor.versionsDiffer (deckIndex));
    mix (editor.currentBpm (deckIndex)); mix (visibleSeconds); mix (loopBeats); mix (MusicalKey::camelotDisplay().load());
    if (h != lastHash) { lastHash = h; repaint(); }
}

void PrepView::chip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool lit, juce::Colour c, const juce::String& id)
{
    psyched::paintChip (g, r, text, c, lit ? psyched::ChipState::on : psyched::ChipState::off);
    if (id.isNotEmpty()) regions.push_back ({ r, id, 0 });
}

//==============================================================================
void PrepView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    regions.clear();
    const auto& st = deck().state();
    const bool loaded = track != nullptr;

    psyched::paintPanel (g, getLocalBounds().toFloat().reduced (2.0f), Theme::accent);

    // --- Header ------------------------------------------------------------------
    {
        auto r = rowHeader.toFloat();
        psyched::paintTitle (g, "Prep", r.removeFromLeft (64.0f), juce::Justification::centredLeft, Theme::accent, 13.0f);
        if (loaded)
        {
            auto right = r.removeFromRight (520.0f);
            // R / C edit targets
            const bool er = editor.editsRekordbox (deckIndex), ec = editor.editsCentrifuge (deckIndex);
            auto rc = right.removeFromLeft (26.0f).reduced (1.0f, 9.0f);
            chip (g, rc, "R", er, juce::Colour (0xff4fa3ff), "edit.r");
            auto cc = right.removeFromLeft (26.0f).reduced (1.0f, 9.0f);
            chip (g, cc, "C", ec, Theme::accent, "edit.c");
            if (editor.versionsDiffer (deckIndex))
                psyched::paintLabel (g, "R/C differ", right.removeFromLeft (80.0f).withTrimmedLeft (6.0f), juce::Justification::centredLeft, Theme::accent);
            else right.removeFromLeft (80.0f);

            const double pos = st.positionSec.load(), len = st.lengthSec.load();
            g.setFont (Theme::mono (17.0f, true));
            g.setColour (Theme::text);
            g.drawText (formatTime (pos), right.removeFromLeft (80.0f), juce::Justification::centredRight);
            g.setColour (Theme::textDim);
            g.drawText (formatTime (-(len - pos)), right.removeFromLeft (88.0f), juce::Justification::centredRight);

            auto keyR = right.removeFromLeft (80.0f).reduced (8.0f, 9.0f);
            g.setColour (Theme::panelHi);
            g.fillRect (keyR);
            g.setColour (Theme::good);
            g.setFont (Theme::font (13.0f, true));
            g.drawText (MusicalKey::display (track->info.key), keyR, juce::Justification::centred);

            auto bpmR = right;
            const double bpm = editor.currentBpm (deckIndex);
            g.setColour (Theme::text);
            g.setFont (Theme::mono (24.0f, true));
            g.drawText (bpm > 0 ? juce::String (bpm, 2) : "--.--", bpmR.removeFromTop (28.0f), juce::Justification::centredRight);
            psyched::paintLabel (g, "BPM", bpmR, juce::Justification::topRight);

            r.removeFromLeft (8.0f);
            g.setColour (Theme::text);
            g.setFont (Theme::font (17.0f, true));
            g.drawText (track->info.title, r.removeFromTop (24.0f), juce::Justification::bottomLeft, true);
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (13.0f));
            g.drawText (track->info.artist, r, juce::Justification::topLeft, true);
        }
        else
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (14.0f));
            g.drawText ("Drag a track here, double-click one in the browser, or right-click it > Load into PREP. "
                        "PREP plays on its own output (Settings > PREP / previews play on).",
                        r.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);
        }
    }

    // --- Overview -------------------------------------------------------------------
    {
        auto r = rowOverview.toFloat();
        psyched::paintWell (g, r, false);
        regions.push_back ({ r, {}, 1 });
        if (loaded && track->waveform != nullptr)
        {
            const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
            const int pw = (int) std::lround (r.getWidth() * scale);
            if (overviewWidth != pw || overview.isNull())
            {
                overviewWidth = pw;
                auto wd = track->waveform;
                StripOptions o;
                o.overview = true;
                overview = renderWaveformStrip (*wd, 0.0, (double) wd->size() / juce::jmax (1, pw), pw, (int) std::lround (r.getHeight() * scale), o);
            }
            g.drawImage (overview, r);
            const double len = st.lengthSec.load();
            if (len > 0.0)
            {
                auto xOf = [&] (double sec) { return r.getX() + (float) (sec / len) * r.getWidth(); };
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRect (r.withRight (xOf (st.positionSec.load())));
                for (int i = 0; i < juce::jmin (st.numLocators.load(), Deck::maxLocators); ++i)
                {
                    g.setColour (juce::Colour (st.locColour[(size_t) i].load()));
                    const float x = xOf (st.locStart[(size_t) i].load());
                    juce::Path tri;
                    tri.addTriangle (x - 3.5f, r.getY(), x + 3.5f, r.getY(), x, r.getY() + 5.0f);
                    g.fillPath (tri);
                }
                for (size_t i = 0; i < 8; ++i)
                    if (st.hotStart[i].load() >= 0.0)
                    {
                        g.setColour (juce::Colour (st.hotColour[i].load()));
                        g.fillRect (xOf (st.hotStart[i].load()) - 0.5f, r.getBottom() - 6.0f, 2.0f, 6.0f);
                    }
                // the part the zoomed waveform shows
                const double t = st.positionSec.load();
                g.setColour (Theme::accent.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (t - visibleSeconds * 0.3), r.getY(), xOf (t + visibleSeconds * 0.7), r.getBottom()));
                g.setColour (juce::Colours::white);
                g.fillRect (xOf (t) - 1.0f, r.getY() - 2.0f, 2.0f, r.getHeight() + 4.0f);
            }
        }
    }

    paintWaveArea (g, rowWave);

    // --- Transport: cue, play, loop, locator, skip, beat jump, zoom ------------------------
    {
        auto r = rowTransport.toFloat();
        const bool playing = st.playing.load();
        chip (g, r.removeFromLeft (56.0f), "CUE", loaded && ! playing, Theme::accent, "cue");
        r.removeFromLeft (4.0f);
        chip (g, r.removeFromLeft (64.0f), playing ? "PAUSE" : "PLAY", playing, Theme::good, "play");
        r.removeFromLeft (12.0f);
        const auto sizeText = loopBeats >= 1.0 ? juce::String ((int) std::lround (loopBeats)) : "1/" + juce::String ((int) std::lround (1.0 / loopBeats));
        chip (g, r.removeFromLeft (24.0f), "<", false, Theme::accent, "loopsize.halve");
        chip (g, r.removeFromLeft (46.0f).reduced (1.0f, 0.0f), sizeText, st.loopActive.load(), Theme::accent, "loop.toggle");
        chip (g, r.removeFromLeft (24.0f), ">", false, Theme::accent, "loopsize.double");
        r.removeFromLeft (8.0f);
        chip (g, r.removeFromLeft (70.0f), "LOCATOR", false, Theme::bad, "locator.add");
        r.removeFromLeft (4.0f);
        const bool pending = st.skipPendingFrom.load() >= 0.0;
        chip (g, r.removeFromLeft (64.0f), pending ? "SKIP TO" : "SKIP", pending, skipColour, "skip.set");
        r.removeFromLeft (12.0f);

        auto zoom = r.removeFromRight (82.0f);
        chip (g, zoom.removeFromLeft (26.0f), "-", false, Theme::sync, "zoom.out");
        psyched::paintLabel (g, juce::String (visibleSeconds, visibleSeconds < 10.0 ? 1 : 0) + "s", zoom.removeFromLeft (30.0f), juce::Justification::centred, Theme::textDim, 9.5f);
        chip (g, zoom.removeFromLeft (26.0f), "+", false, Theme::sync, "zoom.in");
        r.removeFromRight (10.0f);

        psyched::paintLabel (g, "Jump", r.removeFromLeft (36.0f), juce::Justification::centredLeft);
        const std::pair<int, const char*> sizes[] = { { 1, "1 BEAT" }, { 4, "1 BAR" }, { 8, "2 BARS" }, { 16, "4 BARS" } };
        const float gw = (r.getWidth() - 3 * 6.0f) / 4.0f;
        for (int k = 0; k < 4; ++k)
        {
            auto grp = r.removeFromLeft (gw);
            if (k < 3) r.removeFromLeft (6.0f);
            chip (g, grp.removeFromLeft (22.0f), "<", false, Theme::sync, "jump.back." + juce::String (sizes[k].first));
            chip (g, grp.removeFromRight (22.0f), ">", false, Theme::sync, "jump.fwd." + juce::String (sizes[k].first));
            psyched::paintLabel (g, sizes[k].second, grp, juce::Justification::centred, Theme::textDim, 9.5f);
        }
    }

    // --- Hot cue pads ------------------------------------------------------------------
    {
        auto r = rowPads.toFloat();
        const float w = (r.getWidth() - 7 * 5.0f) / 8.0f;
        for (int i = 0; i < 8; ++i)
        {
            auto pad = juce::Rectangle<float> (r.getX() + i * (w + 5.0f), r.getY(), w, r.getHeight());
            const bool set = st.hotStart[(size_t) i].load() >= 0.0;
            const auto c = juce::Colour (st.hotColour[(size_t) i].load());
            g.setColour (set ? c.withAlpha (0.9f) : Theme::panelHi);
            g.fillRect (pad);
            g.setColour (set ? c.brighter (0.3f) : Theme::line);
            g.drawRect (pad.reduced (0.5f), 1.0f);
            regions.push_back ({ pad, "hot." + juce::String (i), 3 });
            auto label = juce::String::charToString ((juce::juce_wchar) ('A' + i));
            if (set && st.hotEnd[(size_t) i].load() > st.hotStart[(size_t) i].load())
                drawLoopIcon (g, pad.removeFromLeft (pad.getHeight()).reduced (7.0f), juce::Colours::black, 1.4f);
            if (set)
                if (auto name = editor.cueName (deckIndex, true, i); name.isNotEmpty()) label << "  " << name;
            g.setColour (set ? juce::Colours::black : Theme::textFaint);
            g.setFont (Theme::font (12.0f, true));
            g.drawText (label, pad.reduced (4.0f, 0.0f), juce::Justification::centred, true);
        }
    }

    // --- Beat grid editor -----------------------------------------------------------------
    {
        auto r = rowGrid.toFloat();
        psyched::paintLabel (g, "Grid", r.removeFromLeft (36.0f), juce::Justification::centredLeft);
        struct B { const char* label; const char* action; float w; };
        const B buttons[] = { { "<10ms", "shift.back10", 1.0f }, { "<1ms", "shift.back1", 1.0f }, { "1ms>", "shift.fwd1", 1.0f },
                              { "10ms>", "shift.fwd10", 1.0f }, { "ANCHOR", "anchor", 1.2f }, { "DOWNBEAT", "downbeat", 1.4f },
                              { "-0.1", "bpm.down", 0.9f }, { "-.01", "bpm.downfine", 0.9f }, { "BPM", "", 1.6f },
                              { "+.01", "bpm.upfine", 0.9f }, { "+0.1", "bpm.up", 0.9f }, { "/2", "halve", 0.7f },
                              { "x2", "double", 0.7f }, { "TAP", "tap", 0.9f }, { "UNDO", "undo", 1.0f }, { "RESET", "reset", 1.0f } };
        float total = 0.0f;
        for (auto& b : buttons) total += b.w;
        const float gap = 3.0f, unit = (r.getWidth() - gap * 15.0f) / total;
        float x = r.getX();
        for (auto& b : buttons)
        {
            auto br = juce::Rectangle<float> (x, r.getY(), unit * b.w, r.getHeight());
            x += unit * b.w + gap;
            if (juce::String (b.action).isEmpty())
            {
                const double bpm = editor.currentBpm (deckIndex);
                psyched::paintChip (g, br, bpm > 0 ? juce::String (bpm, 2) : juce::String ("BPM"), Theme::warn, psyched::ChipState::on);
                regions.push_back ({ br, "setbpm", 5 });
                continue;
            }
            chip (g, br, b.label, false, Theme::warn, "grid." + juce::String (b.action));
        }
    }

    paintList (g, listArea.toFloat());

    if (dropHover)
    {
        g.setColour (psyched::Colours::void_.withAlpha (0.7f));
        g.fillRect (getLocalBounds());
        g.setColour (Theme::accent);
        g.drawRect (getLocalBounds().toFloat().reduced (2.0f), 2.0f);
        psyched::paintTitle (g, "Drop to load into PREP", getLocalBounds().toFloat(), juce::Justification::centred, Theme::accent, 14.0f);
    }
}

void PrepView::paintWaveArea (juce::Graphics& g, juce::Rectangle<int> area)
{
    psyched::paintWell (g, area.toFloat(), false);
    regions.push_back ({ area.toFloat(), {}, 2 });
    if (track == nullptr || track->waveform == nullptr) return;
    const auto& st = deck().state();

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (area.reduced (1));
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const double t = smoother.update (st.positionSec.load(), st.playing.load(), st.tempoRatio.load(), now);
    const double pps = area.getWidth() / visibleSeconds;
    const float playX = (float) area.getX() + (float) area.getWidth() * 0.3f;
    auto xOf = [&] (double sec) { return playX + (float) ((sec - t) * pps); };
    const double tStart = t - (playX - (float) area.getX()) / pps, tEnd = tStart + area.getWidth() / pps;

    auto wd = track->waveform;
    auto detail = track->detail;
    StripOptions o;
    o.entriesPerSecond = wd->entriesPerSecond;
    o.detail = detail.get();
    tiles.configure (wd.get(), 1, wd->entriesPerSecond, wd->lengthSeconds(), pps, area.getHeight(), g.getInternalContext().getPhysicalPixelScaleFactor(),
                     [wd, detail, o] (double first, double epp, int w, int h) { return renderWaveformStrip (*wd, first, epp, w, h, o); });
    tiles.draw (g, area, t, playX - (float) area.getX());

    const float top = (float) area.getY(), h = (float) area.getHeight();

    // Beat grid with bar numbers
    if (track->grid.isValid())
    {
        const auto& beats = track->grid.getBeats();
        int bar = 0;
        for (size_t i = 0; i < beats.size() && beats[i].timeSec <= tEnd; ++i)
        {
            const auto& b = beats[i];
            if (b.beatInBar == 1) ++bar;
            if (b.timeSec < tStart) continue;
            const float x = xOf (b.timeSec);
            g.setColour (juce::Colours::white.withAlpha (b.beatInBar == 1 ? 0.55f : 0.18f));
            g.fillRect (x, top, b.beatInBar == 1 ? 1.5f : 1.0f, h);
            if (b.beatInBar == 1 && pps * track->grid.beatLengthAt (b.timeSec) * 4.0 > 28.0)
                psyched::paintLabel (g, juce::String (bar), juce::Rectangle<float> (x + 3.0f, top + h - 14.0f, 40.0f, 12.0f),
                                     juce::Justification::centredLeft, Theme::textFaint, 9.0f);
        }
    }

    // Loop
    const double li = st.loopInSec.load(), lo = st.loopOutSec.load();
    if (li >= 0.0 && lo > li)
    {
        g.setColour (Theme::accent.withAlpha (st.loopActive.load() ? 0.22f : 0.08f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (li), top, xOf (lo), top + h));
    }

    // Skips
    for (int i = 0; i < juce::jmin (st.numSkips.load(), Deck::maxSkips); ++i)
    {
        const double f = st.skipFrom[(size_t) i].load(), to = st.skipTo[(size_t) i].load();
        if (to < tStart || f > tEnd) continue;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (f), top, xOf (to), top + h));
        g.setColour (skipColour);
        g.fillRect (xOf (f) - 0.5f, top, 1.5f, h);
        juce::Path p;
        const float x = xOf (f);
        p.addTriangle (x, top + 2.0f, x, top + 10.0f, x + 6.0f, top + 6.0f);
        p.addTriangle (x + 5.0f, top + 2.0f, x + 5.0f, top + 10.0f, x + 11.0f, top + 6.0f);
        g.fillPath (p);
    }

    // Locators (loop locators: shaded, with a loop head, or the passes left for a cycle loop)
    for (int i = 0; i < juce::jmin (st.numLocators.load(), Deck::maxLocators); ++i)
    {
        const double s = st.locStart[(size_t) i].load(), e = st.locEnd[(size_t) i].load();
        if ((e > s ? e : s) < tStart || s > tEnd) continue;
        const float x = xOf (s);
        const auto c = juce::Colour (st.locColour[(size_t) i].load());
        const int mode = st.locMode[(size_t) i].load();
        if (e > s)
        {
            g.setColour (c.withAlpha (st.autoLoopLocator.load() == i ? 0.25f : 0.12f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, top, xOf (e), top + h));
            g.setColour (c.withAlpha (0.6f));
            g.fillRect (xOf (e) - 0.5f, top, 1.0f, h);
        }
        g.setColour (c);
        g.fillRect (x - 0.5f, top, 1.0f, h);
        if (e <= s)
        {
            juce::Path p;
            p.addTriangle (x - 6.0f, top, x + 6.0f, top, x, top + 10.0f);
            g.fillPath (p);
        }
        else if (mode == 2)
        {
            const int left = st.autoLoopLocator.load() == i ? st.cyclesLeft.load() : st.locCycles[(size_t) i].load();
            auto box = juce::Rectangle<float> (x, top, 24.0f, 14.0f);
            g.fillRect (box);
            g.setColour (juce::Colours::black);
            g.setFont (psyched::Fonts::mono (10.0f, true));
            g.drawText (juce::String (left) + "x", box, juce::Justification::centred);
        }
        else
        {
            auto box = juce::Rectangle<float> (x, top, 14.0f, 14.0f);
            if (mode == 1) g.fillRect (box);
            drawLoopIcon (g, box.reduced (1.5f), mode == 1 ? juce::Colours::black : c, 1.5f);
        }
        if (auto name = editor.cueName (deckIndex, false, i); name.isNotEmpty())
        {
            g.setColour (c.brighter (0.4f));
            g.setFont (Theme::font (11.0f, true));
            g.drawText (name, juce::Rectangle<float> (x + (e > s ? 28.0f : 8.0f), top, 160.0f, 14.0f), juce::Justification::centredLeft, true);
        }
    }

    // Hot cues
    for (int i = 0; i < 8; ++i)
    {
        const double hs = st.hotStart[(size_t) i].load();
        if (hs < tStart - 1.0 || hs > tEnd || hs < 0.0) continue;
        const float x = xOf (hs);
        const auto col = juce::Colour (st.hotColour[(size_t) i].load());
        g.setColour (col.withAlpha (0.85f));
        g.fillRect (x, top, 1.5f, h);
        auto flag = juce::Rectangle<float> (x, top + h - 16.0f, 16.0f, 16.0f);
        g.setColour (col);
        g.fillRect (flag);
        g.setColour (juce::Colours::black);
        g.setFont (Theme::font (11.0f, true));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)), flag, juce::Justification::centred);
    }

    // Playhead
    g.setColour (st.playing.load() ? juce::Colour (0xffff2d2d) : juce::Colours::white);
    g.fillRect (playX - 1.0f, top, 2.0f, h);
}

std::vector<PrepView::ListEntry> PrepView::listEntries() const
{
    const auto& st = engine.deck (deckIndex).state();
    std::vector<ListEntry> v;
    for (int i = 0; i < juce::jmin (st.numLocators.load(), Deck::maxLocators); ++i)
        v.push_back ({ st.locStart[(size_t) i].load(), 0, i });
    for (int i = 0; i < juce::jmin (st.numSkips.load(), Deck::maxSkips); ++i)
        v.push_back ({ st.skipFrom[(size_t) i].load(), 1, i });
    std::stable_sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.time < b.time; });
    return v;
}

void PrepView::paintList (juce::Graphics& g, juce::Rectangle<float> r)
{
    psyched::paintPanel (g, r, Theme::bad, 8.0f);
    r.reduce (10.0f, 8.0f);
    psyched::paintTitle (g, "Locators, loops & skips", r.removeFromTop (20.0f), juce::Justification::centredLeft, Theme::text, 11.0f);
    psyched::paintLabel (g, "in track order - click to go there, right-click for more", r.removeFromTop (14.0f), juce::Justification::centredLeft, Theme::textFaint, 9.0f);
    r.removeFromTop (6.0f);
    const auto& st = deck().state();
    const auto entries = listEntries();
    if (entries.empty())
    {
        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (12.5f));
        g.drawFittedText ("Nothing yet. LOCATOR adds a locator (or a loop locator when a loop is set at the playhead); "
                          "SKIP once, then again, adds a skip.", r.removeFromTop (60.0f).toNearestInt(), juce::Justification::topLeft, 4);
        return;
    }
    const float rowH = 24.0f;
    const int fits = juce::jmax (1, (int) (r.getHeight() / rowH));
    listScroll = juce::jlimit (0, juce::jmax (0, (int) entries.size() - fits), listScroll);
    const double now = st.positionSec.load();
    const double bpm = editor.currentBpm (deckIndex);

    for (int k = listScroll; k < (int) entries.size() && r.getHeight() >= rowH; ++k)
    {
        const auto& e = entries[(size_t) k];
        auto row = r.removeFromTop (rowH);
        const bool here = std::abs (now - e.time) < 0.05;
        g.setColour (here ? Theme::accent.withAlpha (0.12f) : (k % 2 ? Theme::panel : juce::Colour (0xff0d0c17)));
        g.fillRect (row);
        regions.push_back ({ row, juce::String (e.kind) + ":" + juce::String (e.index), 4 });

        auto icon = row.removeFromLeft (28.0f).reduced (4.0f, 4.0f);
        juce::String detail, name;
        if (e.kind == 1)
        {
            g.setColour (skipColour);
            juce::Path p;
            p.addTriangle (icon.getX(), icon.getY() + 2.0f, icon.getX(), icon.getBottom() - 2.0f, icon.getX() + 7.0f, icon.getCentreY());
            p.addTriangle (icon.getX() + 7.0f, icon.getY() + 2.0f, icon.getX() + 7.0f, icon.getBottom() - 2.0f, icon.getX() + 14.0f, icon.getCentreY());
            g.fillPath (p);
            name = "Skip";
            detail = "to " + formatTime (st.skipTo[(size_t) e.index].load());
        }
        else
        {
            const auto c = juce::Colour (st.locColour[(size_t) e.index].load());
            const double s = st.locStart[(size_t) e.index].load(), en = st.locEnd[(size_t) e.index].load();
            const int mode = st.locMode[(size_t) e.index].load();
            if (en > s)
            {
                if (mode == 2)
                {
                    g.setColour (c);
                    g.fillRect (icon);
                    g.setColour (juce::Colours::black);
                    g.setFont (psyched::Fonts::mono (9.5f, true));
                    g.drawText (juce::String (st.locCycles[(size_t) e.index].load()) + "x", icon, juce::Justification::centred);
                }
                else
                {
                    if (mode == 1) { g.setColour (c); g.fillRect (icon.withWidth (icon.getHeight())); }
                    drawLoopIcon (g, icon.withWidth (icon.getHeight()).reduced (1.0f), mode == 1 ? juce::Colours::black : c, 1.4f);
                }
                const double beats = bpm > 0.0 ? (en - s) * bpm / 60.0 : 0.0;
                detail = (mode == 1 ? "active " : mode == 2 ? "cycle " : "loop ")
                       + (beats > 0.0 ? juce::String (beats, beats < 1.0 ? 2 : (std::abs (beats - std::round (beats)) < 0.05 ? 0 : 1)) + " beats" : formatTime (en - s));
            }
            else
            {
                juce::Path p;
                p.addTriangle (icon.getX(), icon.getY() + 2.0f, icon.getX() + 12.0f, icon.getY() + 2.0f, icon.getX() + 6.0f, icon.getBottom() - 2.0f);
                g.setColour (c);
                g.fillPath (p);
            }
            name = editor.cueName (deckIndex, false, e.index);
            if (name.isEmpty()) name = en > s ? "Loop" : "Locator";
        }
        g.setFont (psyched::Fonts::value (11.5f));
        g.setColour (Theme::text);
        g.drawText (formatTime (e.time), row.removeFromLeft (58.0f), juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (psyched::Fonts::value (10.5f));
        g.drawText (detail, row.removeFromRight (110.0f).withTrimmedRight (4.0f), juce::Justification::centredRight, true);
        g.setColour (Theme::text);
        g.setFont (Theme::font (12.5f, true));
        g.drawText (name, row, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
const PrepView::Region* PrepView::regionAt (juce::Point<float> p) const
{
    for (auto it = regions.rbegin(); it != regions.rend(); ++it)
        if (it->r.contains (p)) return &*it;
    return nullptr;
}

void PrepView::action (const juce::String& id, bool down)
{
    using C = Deck::Cmd;
    const auto& st = deck().state();
    if (id == "cue") { post (C::cue, 0.0, 0, down); return; }
    if (id.startsWith ("hot."))
    {
        const int i = id.fromFirstOccurrenceOf (".", false, false).getIntValue();
        post (C::hotCue, 0.0, i, down);
        return;
    }
    if (! down) return;
    if (track == nullptr && ! id.startsWith ("zoom.")) return;

    if (id == "play") post (C::playToggle);
    else if (id == "loop.toggle") { if (st.loopActive.load()) post (C::loopOff); else post (C::beatLoop, loopBeats); }
    else if (id == "loopsize.halve" || id == "loopsize.double")
    {
        const bool up = id == "loopsize.double";
        loopBeats = up ? juce::jmin (512.0, loopBeats * 2.0) : juce::jmax (1.0 / 32.0, loopBeats * 0.5);
        if (st.loopActive.load()) post (up ? C::loopDouble : C::loopHalve);
    }
    else if (id == "locator.add") post (C::locatorAdd, -1.0);
    else if (id == "skip.set") post (C::skipSet);
    else if (id.startsWith ("jump."))
    {
        const double beats = id.fromLastOccurrenceOf (".", false, false).getDoubleValue();
        post (C::beatJump, id.startsWith ("jump.back.") ? -beats : beats);
    }
    else if (id.startsWith ("grid.")) editor.gridAction (deckIndex, id.fromFirstOccurrenceOf ("grid.", false, false));
    else if (id == "edit.r") editor.setEditTargets (deckIndex, ! editor.editsRekordbox (deckIndex), editor.editsCentrifuge (deckIndex));
    else if (id == "edit.c") editor.setEditTargets (deckIndex, editor.editsRekordbox (deckIndex), ! editor.editsCentrifuge (deckIndex));
    else if (id == "zoom.in") visibleSeconds = juce::jmax (1.0, visibleSeconds / 1.5);
    else if (id == "zoom.out") visibleSeconds = juce::jmin (120.0, visibleSeconds * 1.5);
    repaint();
}

void PrepView::mouseDown (const juce::MouseEvent& e)
{
    auto* r = regionAt (e.position);
    if (r == nullptr) return;
    const auto& st = deck().state();

    if (r->kind == 1)   // overview: seek
    {
        post (Deck::Cmd::seekFraction, (e.position.x - r->r.getX()) / r->r.getWidth());
        return;
    }
    if (r->kind == 2)   // zoomed waveform: drag to scrub, right-click for a menu
    {
        if (track == nullptr) return;
        const double pps = rowWave.getWidth() / visibleSeconds;
        const float playX = (float) rowWave.getX() + (float) rowWave.getWidth() * 0.3f;
        const double at = st.positionSec.load() + (e.position.x - playX) / pps;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Add locator here");
            m.addItem (2, "Add loop locator here (" + juce::String (loopBeats, loopBeats < 1.0 ? 2 : 0) + " beats)");
            juce::Component::SafePointer<PrepView> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, at] (int res)
            {
                if (safe == nullptr || safe->track == nullptr) return;
                if (res == 1) safe->post (Deck::Cmd::locatorAdd, juce::jmax (0.0, at));
                if (res == 2)
                {
                    const auto& g = safe->track->grid;
                    double s = at, en = at + safe->loopBeats * 0.5;
                    if (g.isValid())
                    {
                        const double b = std::round (g.beatPosition (at));
                        s = g.timeAtBeat (b);
                        en = g.timeAtBeat (b + safe->loopBeats);
                    }
                    safe->deck().post (Deck::Command { Deck::Cmd::locatorLoopAdd, juce::jmax (0.0, s), 0, true, nullptr, 0, en });
                }
            });
            return;
        }
        scrubbing = true;
        scrubStartTime = st.positionSec.load();
        scrubStartX = e.position.x;
        return;
    }
    if (r->kind == 4)
    {
        const int kind = r->action.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const int idx = r->action.fromFirstOccurrenceOf (":", false, false).getIntValue();
        const ListEntry entry { kind == 1 ? st.skipFrom[(size_t) idx].load() : st.locStart[(size_t) idx].load(), kind, idx };
        if (e.mods.isPopupMenu()) showEntryMenu (entry, r->r);
        else post (Deck::Cmd::seekSeconds, juce::jmax (0.0, entry.time));
        return;
    }
    if (r->kind == 5) { if (onSetBpm) onSetBpm (deckIndex); return; }
    if (r->kind == 3 && e.mods.isPopupMenu())
    {
        showPadMenu (r->action.fromFirstOccurrenceOf (".", false, false).getIntValue(), r->r);
        return;
    }
    if (e.mods.isPopupMenu()) return;
    if (r->kind == 3 && e.mods.isShiftDown())
    {
        post (Deck::Cmd::hotCueDelete, 0.0, r->action.fromFirstOccurrenceOf (".", false, false).getIntValue());
        return;
    }
    heldAction = r->action;
    action (heldAction, true);
}

void PrepView::mouseDrag (const juce::MouseEvent& e)
{
    if (scrubbing && track != nullptr)
    {
        const double pps = rowWave.getWidth() / visibleSeconds;
        post (Deck::Cmd::seekSeconds, juce::jmax (0.0, scrubStartTime - (e.position.x - scrubStartX) / pps));
        return;
    }
    if (auto* r = regionAt (e.getMouseDownPosition().toFloat()); r != nullptr && r->kind == 1)
        post (Deck::Cmd::seekFraction, juce::jlimit (0.0f, 1.0f, (e.position.x - r->r.getX()) / r->r.getWidth()));
}

void PrepView::mouseUp (const juce::MouseEvent&)
{
    scrubbing = false;
    if (heldAction.isNotEmpty())
    {
        action (heldAction, false);
        heldAction = {};
    }
}

void PrepView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (listArea.contains (e.getPosition()))
    {
        listScroll += w.deltaY < 0 ? 1 : -1;
        repaint (listArea);
        return;
    }
    if (rowWave.contains (e.getPosition()))
    {
        visibleSeconds = juce::jlimit (1.0, 120.0, visibleSeconds * (w.deltaY > 0 ? 1.0 / 1.2 : 1.2));
        repaint();
    }
}

void PrepView::showEntryMenu (const ListEntry& entry, juce::Rectangle<float> area)
{
    const auto& st = deck().state();
    juce::PopupMenu m;
    const int i = entry.index;
    if (entry.kind == 1)
    {
        m.addSectionHeader ("Skip");
        m.addItem (1, "Delete skip");
    }
    else
    {
        const bool loop = st.locEnd[(size_t) i].load() > st.locStart[(size_t) i].load();
        m.addSectionHeader (loop ? "Loop locator" : "Locator");
        if (loop) { addLoopModeItems (m, 200, st.locMode[(size_t) i].load(), st.locCycles[(size_t) i].load()); m.addSeparator(); }
        addCueColourItems (m, 100, juce::Colour (st.locColour[(size_t) i].load()));
        m.addSeparator();
        m.addItem (2, "Rename...");
        m.addItem (1, "Delete");
    }
    const auto screen = localAreaToGlobal (area.toNearestInt());
    const int cycles = entry.kind == 0 ? st.locCycles[(size_t) i].load() : 4;
    const auto current = entry.kind == 0 ? juce::Colour (st.locColour[(size_t) i].load()) : juce::Colour();
    juce::Component::SafePointer<PrepView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (screen),
                     [safe, entry, cycles, screen, current] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& d = safe->deck();
        if (r == 1) { d.post (entry.kind == 1 ? Deck::Cmd::skipDelete : Deck::Cmd::locatorDelete, 0.0, entry.index); return; }
        if (r == 2 && safe->onRenameCue) { safe->onRenameCue (deckIndex, false, entry.index, safe->editor.cueName (deckIndex, false, entry.index)); return; }
        if (auto [mode, n] = loopModeFromMenu (r, 200, cycles); mode >= 0)
        {
            d.post (Deck::Command { Deck::Cmd::locatorLoopMode, (double) mode, entry.index, true, nullptr, 0, (double) n });
            return;
        }
        auto setColour = [safe, entry] (juce::Colour c) { if (safe != nullptr) safe->deck().post (Deck::Cmd::locatorColour, (double) c.getARGB(), entry.index); };
        if (r >= 100 && r < 116) setColour (juce::Colour (rekordboxCueColours()[(size_t) (r - 100)].argb));
        else if (r == 116) ColourPickerBubble::show (screen, current, setColour);
    });
}

void PrepView::showPadMenu (int pad, juce::Rectangle<float> area)
{
    const auto& st = deck().state();
    if (pad < 0 || pad >= 8 || st.hotStart[(size_t) pad].load() < 0.0) return;
    const auto current = juce::Colour (st.hotColour[(size_t) pad].load());
    juce::PopupMenu m;
    m.addSectionHeader ("Hot cue " + juce::String::charToString ((juce::juce_wchar) ('A' + pad)));
    addCueColourItems (m, 100, current);
    m.addSeparator();
    m.addItem (2, "Rename...");
    m.addItem (1, "Delete   (shift + click)");
    const auto screen = localAreaToGlobal (area.toNearestInt());
    juce::Component::SafePointer<PrepView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (screen), [safe, pad, screen, current] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto setColour = [safe, pad] (juce::Colour c) { if (safe != nullptr) safe->deck().post (Deck::Cmd::hotCueColour, (double) c.getARGB(), pad); };
        if (r == 1) safe->deck().post (Deck::Cmd::hotCueDelete, 0.0, pad);
        else if (r == 2 && safe->onRenameCue) safe->onRenameCue (deckIndex, true, pad, safe->editor.cueName (deckIndex, true, pad));
        else if (r >= 100 && r < 116) setColour (juce::Colour (rekordboxCueColours()[(size_t) (r - 100)].argb));
        else if (r == 116) ColourPickerBubble::show (screen, current, setColour);
    });
}

} // namespace orbit
