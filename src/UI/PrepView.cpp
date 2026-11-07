#include "PrepView.h"
#include "TrackMarks.h"
#include "../App/Features.h"
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
    setWantsKeyboardFocus (true);   // Delete / Esc / Ctrl+A act on the list selection
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
        if (s != serial) { overviewWidth = 0; listScroll = 0; selection.clear(); anchor = { -1, -1 }; }
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
            // R / C edit targets (only while the rekordbox copy is switched on)
            if (Features::rekordboxCopy)
            {
                const bool er = editor.editsRekordbox (deckIndex), ec = editor.editsCentrifuge (deckIndex);
                auto rc = right.removeFromLeft (26.0f).reduced (1.0f, 9.0f);
                chip (g, rc, "R", er, juce::Colour (0xff4fa3ff), "edit.r");
                auto cc = right.removeFromLeft (26.0f).reduced (1.0f, 9.0f);
                chip (g, cc, "C", ec, Theme::accent, "edit.c");
                if (editor.versionsDiffer (deckIndex))
                    psyched::paintLabel (g, "R/C differ", right.removeFromLeft (80.0f).withTrimmedLeft (6.0f), juce::Justification::centredLeft, Theme::accent);
                else right.removeFromLeft (80.0f);
            }
            else right.removeFromLeft (132.0f);

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
                paintOverviewMarks (g, r, TrackMarks::fromDeck (st, track->phrases));
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

    const float top = (float) area.getY();
    const auto& phrases = track->phrases;
    const float phraseH = phrases.empty() ? 0.0f : 16.0f;
    const float h = (float) area.getHeight() - phraseH;
    if (! phrases.empty())
        paintPhraseStrip (g, juce::Rectangle<float> ((float) area.getX(), top + h, (float) area.getWidth(), phraseH), phrases,
                          st.lengthSec.load(), [&] (double sec) { return xOf (sec); }, phraseSelected);

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
        // The line shows a loop's mode: orange = active, red = cycle, otherwise the locator's colour
        const auto lineCol = e > s && mode == 1 ? juce::Colour (activeLoopLine) : e > s && mode == 2 ? juce::Colour (cycleLoopLine) : c;
        const float lineW = e > s && mode != 0 ? 2.0f : 1.0f;
        if (e > s)
        {
            g.setColour (c.withAlpha (st.autoLoopLocator.load() == i ? 0.25f : 0.12f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, top, xOf (e), top + h));
            g.setColour (lineCol.withAlpha (0.6f));
            g.fillRect (xOf (e) - lineW * 0.5f, top, lineW, h);
        }
        g.setColour (lineCol);
        g.fillRect (x - lineW * 0.5f, top, lineW, h);
        g.setColour (c);
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
    if (listTab == 0)
    {
        for (int i = 0; i < juce::jmin (st.numLocators.load(), Deck::maxLocators); ++i)
            v.push_back ({ st.locStart[(size_t) i].load(), 0, i });
        for (int i = 0; i < juce::jmin (st.numSkips.load(), Deck::maxSkips); ++i)
            v.push_back ({ st.skipFrom[(size_t) i].load(), 1, i });
        std::stable_sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.time < b.time; });
    }
    else if (listTab == 1)
    {
        for (int i = 0; i < 8; ++i)
            if (st.hotStart[(size_t) i].load() >= 0.0) v.push_back ({ st.hotStart[(size_t) i].load(), 2, i });
    }
    else
    {
        const auto& ph = editor.phrases (deckIndex);
        for (int i = 0; i < (int) ph.size(); ++i) v.push_back ({ ph[(size_t) i].startSec, 3, i });
    }
    return v;
}

void PrepView::paintList (juce::Graphics& g, juce::Rectangle<float> r)
{
    psyched::paintPanel (g, r, Theme::bad, 8.0f);
    r.reduce (10.0f, 8.0f);

    // Tabs: locators / loops / skips, hot cues, phrases
    {
        auto tabs = r.removeFromTop (24.0f);
        const char* names[] = { "LOCATORS", "HOT CUES", "PHRASES" };
        const float w = (tabs.getWidth() - 8.0f) / 3.0f;
        for (int k = 0; k < 3; ++k)
        {
            auto t = tabs.removeFromLeft (w);
            if (k < 2) tabs.removeFromLeft (4.0f);
            chip (g, t, names[k], listTab == k, k == 0 ? Theme::bad : k == 1 ? Theme::good : Theme::warn, "tab." + juce::String (k));
        }
    }
    r.removeFromTop (6.0f);
    const char* hints[] = { "click: go there - icon: colour - ctrl / shift: select",
                            "click: go there - icon: colour - ctrl / shift: select",
                            "click: go there - icon: colour - ctrl / shift: select" };
    psyched::paintLabel (g, hints[listTab], r.removeFromTop (14.0f), juce::Justification::centredLeft, Theme::textFaint, 9.0f);
    r.removeFromTop (6.0f);

    if (listTab == 2 && track != nullptr)
    {
        auto add = r.removeFromBottom (26.0f);
        chip (g, add, "+ ADD PHRASE AT PLAYHEAD", false, Theme::warn, "phrase.add");
        r.removeFromBottom (6.0f);
    }

    const auto& st = deck().state();
    const auto entries = listEntries();
    nameRects.clear();
    // forget selected entries that no longer exist
    for (auto it = selection.begin(); it != selection.end();)
        it = std::any_of (entries.begin(), entries.end(), [&] (auto& e) { return e.kind == it->first && e.index == it->second; })
           ? std::next (it) : selection.erase (it);
    if (entries.empty())
    {
        const char* empty[] = { "Nothing yet. LOCATOR adds a locator (or a loop locator when a loop is set at the playhead); "
                                "SKIP once, then again, adds a skip.",
                                "No hot cues yet. Press an empty pad (A-H) to set one.",
                                "No phrases. Tracks analysed by rekordbox bring their phrases with them; "
                                "or add your own at the playhead (any name, any colour)." };
        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (12.5f));
        g.drawFittedText (track != nullptr ? empty[listTab] : "Load a track first.", r.removeFromTop (60.0f).toNearestInt(), juce::Justification::topLeft, 4);
        return;
    }
    const float rowH = 24.0f;
    const int fits = juce::jmax (1, (int) (r.getHeight() / rowH));
    listScroll = juce::jlimit (0, juce::jmax (0, (int) entries.size() - fits), listScroll);
    const double now = st.positionSec.load();
    const double bpm = editor.currentBpm (deckIndex);
    const auto& phrases = editor.phrases (deckIndex);

    for (int k = listScroll; k < (int) entries.size() && r.getHeight() >= rowH; ++k)
    {
        const auto& e = entries[(size_t) k];
        auto row = r.removeFromTop (rowH);
        bool here = std::abs (now - e.time) < 0.05;
        if (e.kind == 3)
        {
            const double end = e.index + 1 < (int) phrases.size() ? phrases[(size_t) e.index + 1].startSec : st.lengthSec.load();
            here = now >= e.time && now < end;
        }
        const bool sel = selection.count ({ e.kind, e.index }) > 0;
        g.setColour (sel ? Theme::sync.withAlpha (0.28f) : here ? Theme::accent.withAlpha (0.12f) : (k % 2 ? Theme::panel : juce::Colour (0xff0d0c17)));
        g.fillRect (row);
        if (sel)
        {
            g.setColour (Theme::sync);
            g.fillRect (row.getX(), row.getY(), 2.0f, row.getHeight());
        }
        const auto key = juce::String (e.kind) + ":" + juce::String (e.index);
        regions.push_back ({ row, key, 4 });

        // x on the far right deletes the entry
        {
            auto xr = row.removeFromRight (22.0f);
            regions.push_back ({ xr, key, 7 });
            const bool hover = xr.contains (getMouseXYRelative().toFloat());
            g.setColour (hover ? Theme::bad : Theme::textFaint);
            const auto c = xr.getCentre();
            const float d = 3.5f;
            g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d, hover ? 1.8f : 1.3f);
            g.drawLine (c.x - d, c.y + d, c.x + d, c.y - d, hover ? 1.8f : 1.3f);
        }

        auto icon = row.removeFromLeft (28.0f).reduced (4.0f, 4.0f);
        if (e.kind != 1) regions.push_back ({ icon.expanded (3.0f), key, 6 });   // click the icon: colour
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
        else if (e.kind == 2)
        {
            const auto c = juce::Colour (st.hotColour[(size_t) e.index].load());
            g.setColour (c);
            g.fillRect (icon);
            g.setColour (juce::Colours::black);
            g.setFont (Theme::font (11.0f, true));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + e.index)), icon, juce::Justification::centred);
            name = editor.cueName (deckIndex, true, e.index);
            const bool loop = st.hotEnd[(size_t) e.index].load() > st.hotStart[(size_t) e.index].load();
            if (name.isEmpty()) name = loop ? "Hot loop" : "Hot cue";
            if (loop)
            {
                const double len = st.hotEnd[(size_t) e.index].load() - st.hotStart[(size_t) e.index].load();
                const double beats = bpm > 0.0 ? len * bpm / 60.0 : 0.0;
                detail = "loop " + (beats > 0.0 ? juce::String (beats, std::abs (beats - std::round (beats)) < 0.05 ? 0 : 1) + " beats" : formatTime (len));
            }
        }
        else if (e.kind == 3)
        {
            const auto& p = phrases[(size_t) e.index];
            g.setColour (p.colour);
            g.fillRect (icon);
            name = p.name.isNotEmpty() ? p.name : "Phrase";
            const double end = e.index + 1 < (int) phrases.size() ? phrases[(size_t) e.index + 1].startSec
                             : (p.endSec > p.startSec ? p.endSec : st.lengthSec.load());
            const double bars = bpm > 0.0 ? (end - p.startSec) * bpm / 240.0 : 0.0;
            detail = bars > 0.0 ? juce::String (juce::roundToInt (bars)) + " bars" : formatTime (end - p.startSec);
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
                if (mode != 0)
                {
                    g.setColour (juce::Colour (mode == 1 ? activeLoopLine : cycleLoopLine));
                    g.fillRect (icon.getX(), icon.getBottom() + 1.0f, icon.getWidth(), 2.0f);
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
        auto detailArea = row.removeFromRight (104.0f).withTrimmedRight (4.0f);
        if (e.kind == 0 && st.locEnd[(size_t) e.index].load() > st.locStart[(size_t) e.index].load())
        {
            // a loop locator's mode ("loop / active / cycle"): click it to step through the three
            regions.push_back ({ detailArea, key, 8 });
            if (detailArea.contains (getMouseXYRelative().toFloat())) g.setColour (Theme::text);
        }
        g.drawText (detail, detailArea, juce::Justification::centredRight, true);
        g.setColour (Theme::text);
        g.setFont (Theme::font (12.5f, true));
        if (e.kind != 1)
        {
            // the name is editable: click it to rename here
            nameRects[{ e.kind, e.index }] = row;
            regions.push_back ({ row, key, 9 });
            if (row.contains (getMouseXYRelative().toFloat()) && renaming != EntryKey { e.kind, e.index })
            {
                g.setColour (Theme::line);
                g.drawRect (row.reduced (0.0f, 3.0f).withTrimmedLeft (-4.0f), 1.0f);
                g.setColour (Theme::text);
            }
        }
        if (renaming != EntryKey { e.kind, e.index })
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
    if (id.startsWith ("tab.")) { listTab = id.getTrailingIntValue(); listScroll = 0; selection.clear(); anchor = { -1, -1 }; repaint(); return; }
    if (track == nullptr && ! id.startsWith ("zoom.")) return;
    if (id.startsWith ("loopmode."))
    {
        const int i = id.getTrailingIntValue();
        const int mode = (st.locMode[(size_t) i].load() + 1) % 3;
        deck().post (Deck::Command { Deck::Cmd::locatorLoopMode, (double) mode, i, true, nullptr, 0, (double) st.locCycles[(size_t) i].load() });
        return;
    }
    if (id == "phrase.add") { showPhraseAddMenu(); return; }

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
    if (r->kind == 4 || r->kind == 6 || r->kind == 7 || r->kind == 8 || r->kind == 9)
    {
        grabKeyboardFocus();
        const int kind = r->action.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const int idx = r->action.fromFirstOccurrenceOf (":", false, false).getIntValue();
        const auto entry = entryFor (kind, idx);
        if (kind == 3) phraseSelected = idx;
        if (r->kind == 7 && ! e.mods.isPopupMenu()) { deleteEntries ({ entry }); return; }
        if (r->kind == 9 && ! e.mods.isPopupMenu() && ! e.mods.isCtrlDown() && ! e.mods.isShiftDown())
        {
            selection = { { kind, idx } };
            anchor = { kind, idx };
            startRename (entry);
            return;
        }
        if (r->kind == 8 && ! e.mods.isPopupMenu())
        {
            const int mode = (st.locMode[(size_t) idx].load() + 1) % 3;
            deck().post (Deck::Command { Deck::Cmd::locatorLoopMode, (double) mode, idx, true, nullptr, 0, (double) st.locCycles[(size_t) idx].load() });
            return;
        }
        if (r->kind == 6 && ! e.mods.isPopupMenu() && ! e.mods.isCtrlDown() && ! e.mods.isShiftDown())
        {
            if (selection.count ({ kind, idx }) == 0) { selection = { { kind, idx } }; anchor = { kind, idx }; }
            repaint();
            showColourMenu (entry, r->r);
            return;
        }
        if (e.mods.isPopupMenu())
        {
            if (selection.count ({ kind, idx }) == 0) { selection = { { kind, idx } }; anchor = { kind, idx }; repaint(); }
            if (selection.size() > 1) { showColourMenu (entry, r->r); return; }   // several selected: colour / delete them all
            if (kind == 2) showPadMenu (idx, r->r);
            else if (kind == 3) showPhraseMenu (idx, r->r);
            else showEntryMenu (entry, r->r);
            return;
        }
        clickEntry (entry, e.mods);
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
        if (nameEditor != nullptr) finishRename (true);
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
        if (r == 2) { safe->startRename (entry); return; }
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
        else if (r == 2) safe->startRename (safe->entryFor (2, pad));
        else if (r >= 100 && r < 116) setColour (juce::Colour (rekordboxCueColours()[(size_t) (r - 100)].argb));
        else if (r == 116) ColourPickerBubble::show (screen, current, setColour);
    });
}

void PrepView::askText (const juce::String& title, const juce::String& current, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("t", current);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        if (r == 1) done (w->getTextEditorContents ("t").trim());
    }), true);
}

void PrepView::showPhraseAddMenu()
{
    const char* names[] = { "Intro", "Verse", "Build", "Drop", "Breakdown", "Chorus", "Bridge", "Outro" };
    juce::PopupMenu m;
    m.addSectionHeader ("Add a phrase at the playhead");
    for (int i = 0; i < 8; ++i) m.addItem (1 + i, names[i]);
    m.addSeparator();
    m.addItem (20, "Other name...");
    juce::Component::SafePointer<PrepView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        const double at = safe->deck().state().positionSec.load();
        const char* nm[] = { "Intro", "Verse", "Build", "Drop", "Breakdown", "Chorus", "Bridge", "Outro" };
        if (r >= 1 && r <= 8)
            safe->editor.addPhrase (deckIndex, at, nm[r - 1], defaultPhraseColour (nm[r - 1]));
        else if (r == 20)
            safe->askText ("New phrase", {}, [safe, at] (juce::String n)
            {
                if (safe != nullptr && n.isNotEmpty()) safe->editor.addPhrase (deckIndex, at, n, defaultPhraseColour (n));
            });
    });
}

void PrepView::showPhraseMenu (int index, juce::Rectangle<float> area)
{
    const auto& list = editor.phrases (deckIndex);
    if (index < 0 || index >= (int) list.size()) return;
    const auto p = list[(size_t) index];
    juce::PopupMenu m;
    m.addSectionHeader (p.name.isNotEmpty() ? p.name : juce::String ("Phrase"));
    addCueColourItems (m, 100, p.colour);
    m.addSeparator();
    m.addItem (2, "Rename...");
    m.addItem (3, "Move its start to the playhead");
    m.addItem (1, "Delete");
    const auto screen = localAreaToGlobal (area.toNearestInt());
    juce::Component::SafePointer<PrepView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (screen), [safe, index, p, screen] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& ed = safe->editor;
        auto setColour = [safe, index, p] (juce::Colour c) { if (safe != nullptr) { auto q = p; q.colour = c; safe->editor.setPhrase (deckIndex, index, q); } };
        if (r == 1) ed.deletePhrase (deckIndex, index);
        else if (r == 2) safe->startRename (safe->entryFor (3, index));
        else if (r == 3) { auto q = p; q.startSec = safe->deck().state().positionSec.load(); ed.setPhrase (deckIndex, index, q); }
        else if (r >= 100 && r < 116) setColour (juce::Colour (rekordboxCueColours()[(size_t) (r - 100)].argb));
        else if (r == 116) ColourPickerBubble::show (screen, p.colour, setColour);
    });
}

PrepView::ListEntry PrepView::entryFor (int kind, int idx) const
{
    const auto& st = engine.deck (deckIndex).state();
    double time = 0.0;
    if (kind == 0) time = st.locStart[(size_t) idx].load();
    else if (kind == 1) time = st.skipFrom[(size_t) idx].load();
    else if (kind == 2) time = st.hotStart[(size_t) idx].load();
    else if (idx < (int) editor.phrases (deckIndex).size()) time = editor.phrases (deckIndex)[(size_t) idx].startSec;
    return { time, kind, idx };
}

void PrepView::clickEntry (const ListEntry& e, const juce::ModifierKeys& mods)
{
    const EntryKey key { e.kind, e.index };
    if (mods.isShiftDown() && anchor.first >= 0)
    {
        // everything from the anchor to here, in list order
        const auto list = listEntries();
        int a = -1, b = -1;
        for (int i = 0; i < (int) list.size(); ++i)
        {
            if (list[(size_t) i].kind == anchor.first && list[(size_t) i].index == anchor.second) a = i;
            if (list[(size_t) i].kind == e.kind && list[(size_t) i].index == e.index) b = i;
        }
        if (a >= 0 && b >= 0)
        {
            if (! mods.isCtrlDown()) selection.clear();
            for (int i = juce::jmin (a, b); i <= juce::jmax (a, b); ++i)
                selection.insert ({ list[(size_t) i].kind, list[(size_t) i].index });
            repaint();
            return;
        }
    }
    if (mods.isCtrlDown() || mods.isCommandDown())
    {
        if (selection.count (key)) selection.erase (key); else selection.insert (key);
        anchor = key;
        repaint();
        return;
    }
    selection = { key };
    anchor = key;
    post (Deck::Cmd::seekSeconds, juce::jmax (0.0, e.time));
    repaint();
}

std::vector<PrepView::ListEntry> PrepView::targetsFor (const ListEntry& e) const
{
    if (selection.count ({ e.kind, e.index }) == 0) return { e };
    std::vector<ListEntry> v;
    for (auto& k : selection) v.push_back (entryFor (k.first, k.second));
    return v;
}

juce::Colour PrepView::colourOf (const ListEntry& e) const
{
    const auto& st = engine.deck (deckIndex).state();
    if (e.kind == 0) return juce::Colour (st.locColour[(size_t) e.index].load());
    if (e.kind == 2) return juce::Colour (st.hotColour[(size_t) e.index].load());
    if (e.kind == 3 && e.index < (int) editor.phrases (deckIndex).size()) return editor.phrases (deckIndex)[(size_t) e.index].colour;
    return {};
}

void PrepView::applyColour (const std::vector<ListEntry>& targets, juce::Colour c)
{
    for (auto& t : targets)
    {
        if (t.kind == 0) deck().post (Deck::Cmd::locatorColour, (double) c.getARGB(), t.index);
        else if (t.kind == 2) deck().post (Deck::Cmd::hotCueColour, (double) c.getARGB(), t.index);
        else if (t.kind == 3 && t.index < (int) editor.phrases (deckIndex).size())
        {
            auto p = editor.phrases (deckIndex)[(size_t) t.index];
            p.colour = c;
            editor.setPhrase (deckIndex, t.index, p);
        }
    }
    repaint();
}

void PrepView::deleteEntries (std::vector<ListEntry> targets)
{
    // Highest index first, so deleting one doesn't move the others
    std::sort (targets.begin(), targets.end(), [] (auto& a, auto& b) { return a.kind != b.kind ? a.kind < b.kind : a.index > b.index; });
    for (auto& t : targets)
    {
        if (t.kind == 0) deck().post (Deck::Cmd::locatorDelete, 0.0, t.index);
        else if (t.kind == 1) deck().post (Deck::Cmd::skipDelete, 0.0, t.index);
        else if (t.kind == 2) deck().post (Deck::Cmd::hotCueDelete, 0.0, t.index);
        else if (t.kind == 3) editor.deletePhrase (deckIndex, t.index);
    }
    selection.clear();
    anchor = { -1, -1 };
    repaint();
}

void PrepView::showColourMenu (const ListEntry& e, juce::Rectangle<float> area)
{
    const auto targets = targetsFor (e);
    std::vector<ListEntry> colourable;
    for (auto& t : targets) if (t.kind != 1) colourable.push_back (t);
    juce::PopupMenu m;
    m.addSectionHeader (targets.size() > 1 ? juce::String ((int) targets.size()) + " selected" : juce::String ("Colour"));
    const auto current = colourable.empty() ? juce::Colour() : colourOf (colourable.front());
    if (! colourable.empty()) addCueColourItems (m, 100, current);
    if (targets.size() > 1)
    {
        m.addSeparator();
        m.addItem (1, "Delete all " + juce::String ((int) targets.size()) + "   (Delete key)");
    }
    const auto screen = localAreaToGlobal (area.toNearestInt());
    juce::Component::SafePointer<PrepView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (screen),
                     [safe, targets, colourable, screen, current] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) { safe->deleteEntries (targets); return; }
        auto set = [safe, colourable] (juce::Colour c) { if (safe != nullptr) safe->applyColour (colourable, c); };
        if (r >= 100 && r < 116) set (juce::Colour (rekordboxCueColours()[(size_t) (r - 100)].argb));
        else if (r == 116) ColourPickerBubble::show (screen, current, set);
    });
}

juce::String PrepView::nameOf (const ListEntry& e) const
{
    if (e.kind == 0) return editor.cueName (deckIndex, false, e.index);
    if (e.kind == 2) return editor.cueName (deckIndex, true, e.index);
    if (e.kind == 3 && e.index < (int) editor.phrases (deckIndex).size()) return editor.phrases (deckIndex)[(size_t) e.index].name;
    return {};
}

void PrepView::startRename (const ListEntry& e)
{
    if (e.kind == 1) return;
    if (nameEditor != nullptr) finishRename (true);
    // make sure the row is visible, so there's a place to type
    if (nameRects.find ({ e.kind, e.index }) == nameRects.end())
    {
        const auto list = listEntries();
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i].kind == e.kind && list[(size_t) i].index == e.index) listScroll = i;
        repaint();
        juce::Component::SafePointer<PrepView> safe (this);
        juce::MessageManager::callAsync ([safe, e] { if (safe != nullptr && safe->nameRects.count ({ e.kind, e.index })) safe->startRename (e); });
        return;
    }
    renaming = { e.kind, e.index };
    nameEditor = std::make_unique<juce::TextEditor>();
    auto& t = *nameEditor;
    t.setFont (Theme::font (12.5f, true));
    t.setColour (juce::TextEditor::backgroundColourId, psyched::Colours::void_);
    t.setColour (juce::TextEditor::textColourId, Theme::text);
    t.setColour (juce::TextEditor::outlineColourId, Theme::sync);
    t.setColour (juce::TextEditor::focusedOutlineColourId, Theme::sync);
    t.setIndents (4, 3);
    t.setText (nameOf (e), false);
    t.setTextToShowWhenEmpty (e.kind == 3 ? "Phrase name" : "Name", Theme::textFaint);
    t.onReturnKey = [this] { finishRename (true); };
    t.onEscapeKey = [this] { finishRename (false); };
    t.onFocusLost = [this] { finishRename (true); };
    addAndMakeVisible (t);
    t.setBounds (nameRects[{ e.kind, e.index }].reduced (0.0f, 2.0f).withTrimmedLeft (-4.0f).withTrimmedRight (-2.0f).toNearestInt());
    t.grabKeyboardFocus();
    t.selectAll();
    repaint (listArea);
}

void PrepView::finishRename (bool apply)
{
    if (nameEditor == nullptr) return;
    const auto key = renaming;
    const auto text = nameEditor->getText().trim();
    renaming = { -1, -1 };
    // removed later: we may be inside one of the editor's own callbacks
    auto old = std::shared_ptr<juce::TextEditor> (nameEditor.release());
    old->setVisible (false);
    juce::MessageManager::callAsync ([old] {});
    if (apply && key.first >= 0)
    {
        const auto e = entryFor (key.first, key.second);
        if (text != nameOf (e))
        {
            if (e.kind == 0) editor.renameCue (deckIndex, false, e.index, text);
            else if (e.kind == 2) editor.renameCue (deckIndex, true, e.index, text);
            else if (e.kind == 3 && e.index < (int) editor.phrases (deckIndex).size())
            {
                auto p = editor.phrases (deckIndex)[(size_t) e.index];
                p.name = text;
                editor.setPhrase (deckIndex, e.index, p);
            }
        }
    }
    grabKeyboardFocus();
    repaint (listArea);
}

bool PrepView::keyPressed (const juce::KeyPress& k)
{

    if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && ! selection.empty())
    {
        std::vector<ListEntry> v;
        for (auto& s : selection) v.push_back (entryFor (s.first, s.second));
        deleteEntries (v);
        return true;
    }
    if (k == juce::KeyPress::escapeKey && ! selection.empty()) { selection.clear(); repaint(); return true; }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        for (auto& e : listEntries()) selection.insert ({ e.kind, e.index });
        repaint();
        return true;
    }
    return false;
}

} // namespace orbit
