#include "PrepView.h"
#include "TrackMarks.h"
#include "../App/Features.h"
#include "Theme.h"
#include "../Audio/MusicalKey.h"
#include "../Library/PhrasePresets.h"

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
    addAndMakeVisible (waves);
    waves.setPlayheadPosition (0.3f);
    waves.setZoomRange (1.0, 120.0);
    waves.setScrubWhilePlaying (true);
    waves.selectedPhrase = [this] { return phraseSelected; };
    waves.loopBeatsForMenu = [this] { return loopBeats; };
    waves.cueName = [this] (int d, bool hot, int i) { return editor.cueName (d, hot, i); };
    waves.onRenameCue = [this] (int d, bool hot, int i, juce::String cur) { if (onRenameCue) onRenameCue (d, hot, i, cur); };
    waves.onTrackDropped = [this] (int) { if (onTrackDropped) onTrackDropped(); };
    startTimerHz (30);
}

void PrepView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    listArea = r.removeFromRight (juce::jlimit (200, juce::jmax (200, r.getWidth() - 500), listWidth));
    listEdge = r.removeFromRight (10);
    rowHeader = r.removeFromTop (44);
    r.removeFromTop (6);
    rowOverview = r.removeFromTop (46);
    // the stem controls sit beside the overview, like the decks' stem buttons
    rowStems = rowOverview.removeFromRight (juce::jmin (330, rowOverview.getWidth() / 3));
    rowOverview.removeFromRight (8);
    r.removeFromTop (6);
    rowGrid = r.removeFromBottom (26);
    r.removeFromBottom (6);
    rowPads = r.removeFromBottom (28);
    r.removeFromBottom (6);
    rowTransport = r.removeFromBottom (30);
    r.removeFromBottom (6);
    rowWave = r;
    waves.setBounds (rowWave);
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
    mix (editor.currentBpm (deckIndex)); mix (waves.getVisibleSeconds()); mix (loopBeats); mix (MusicalKey::camelotDisplay().load());
    for (size_t i = 0; i < 4; ++i) { mix (st.stemGain[i].load()); mix (st.stemMode[i].load()); }
    mix (st.stemsReady.load()); mix (std::floor (st.stemsProgress.load() * 400.0f)); mix (Theme::deckColour (deckIndex).getARGB());
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
        // PREP's colour badge (click to change the colour, like a deck's number)
        auto badge = r.removeFromLeft (30.0f).withSizeKeepingCentre (28.0f, 28.0f);
        g.setColour (Theme::deckColour (deckIndex));
        g.fillRect (badge);
        g.setColour (psyched::Colours::onSolid);
        g.setFont (Theme::display (15.0f, true));
        g.drawText ("P", badge, juce::Justification::centred);
        regions.push_back ({ badge, "deckcolour", 0 });
        r.removeFromLeft (8.0f);
        psyched::paintTitle (g, "Prep", r.removeFromLeft (56.0f), juce::Justification::centredLeft, Theme::deckColour (deckIndex), 13.0f);
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
            else
            {
                // The edit version (choose, save, rename, delete); EDITED is beside the overview
                right.removeFromLeft (62.0f);
                auto name = editor.activeEdit (deckIndex);
                const bool any = editor.hasEdits (deckIndex);
                juce::String text = name.isNotEmpty() ? name : (any ? juce::String ("UNSAVED EDITS") : juce::String ("NO EDITS"));
                if (name.isNotEmpty() && editor.activeEditModified (deckIndex)) text << " *";
                auto vc = right.removeFromLeft (70.0f).reduced (1.0f, 9.0f);
                vc = vc.withLeft (vc.getRight() - juce::jlimit (90.0f, 210.0f, psyched::Fonts::label().getStringWidthFloat ("EDIT: " + text.toUpperCase()) + 30.0f));
                vc = vc.withX (juce::jmax (r.getX() + 200.0f, vc.getX()));
                chip (g, vc, "EDIT: " + text, name.isNotEmpty() || any, Theme::sync.brighter (0.3f), "editver");
            }

            updateFold();
            const double pos = fold.toDisplay (st.positionSec.load(), st.playSegment.load()), len = fold.displayLength (st.lengthSec.load());
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
        // EDITED (overview and waveform as the track will play) and the grid toggle, beside the overview
        {
            auto left = r.removeFromLeft (62.0f);
            r.removeFromLeft (6.0f);
            chip (g, left.removeFromTop (left.getHeight() * 0.5f - 2.0f), "EDITED", editView, Theme::sync, "editview");
            left.removeFromTop (4.0f);
            paintGridToggle (g, left, overviewGrid, Theme::sync);
            regions.push_back ({ left, "overviewgrid", 0 });
        }
        auto progress = r.removeFromBottom (4.0f);
        r.removeFromBottom (1.0f);
        if (loaded && st.stemsReady.load() && st.stemsProgress.load() < 0.999f)
        {
            updateFold();
            paintStemProgressStrip (g, progress, st.stemsProgress.load(), st.lengthSec.load(), &fold, Theme::deckColour (deckIndex));
        }
        psyched::paintWell (g, r, false);
        regions.push_back ({ r, {}, 1 });
        if (loaded && track->waveform != nullptr)
        {
            const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
            const int pw = (int) std::lround (r.getWidth() * scale);
            updateFold();
            if (overviewWidth != pw || overview.isNull() || renderedFold != fold.version)
            {
                overviewWidth = pw;
                renderedFold = fold.version;
                const auto wdStyle = overviewStyle == 2 ? WaveformData::Style::rgb
                                   : overviewStyle == 3 ? WaveformData::Style::blue : WaveformData::Style::threeBand;
                auto wd = track->waveformInStyle (wdStyle);
                StripOptions o;
                o.overview = true;
                o.shape = overviewHalf ? StripOptions::halfUp : StripOptions::full;
                o.tint = overviewStyle == 3 ? singleColour : juce::Colour();
                o.fold = &fold;
                o.entriesPerSecond = wd->entriesPerSecond;
                const double entries = fold.isEmpty() ? (double) wd->size() : fold.displayLength (wd->lengthSeconds()) * wd->entriesPerSecond;
                overview = renderWaveformStrip (*wd, 0.0, entries / juce::jmax (1, pw), pw, (int) std::lround (r.getHeight() * scale), o);
            }
            g.drawImage (overview, r);
            const double len = fold.displayLength (st.lengthSec.load());
            if (len > 0.0)
            {
                const double t = fold.toDisplay (st.positionSec.load(), st.playSegment.load());
                fold.reference = t;
                auto xOfD = [&] (double d) { return r.getX() + (float) juce::jlimit (0.0, 1.0, d / len) * r.getWidth(); };
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRect (r.withRight (xOfD (t)));
                if (overviewGrid) paintOverviewGrid (g, r, track->grid, st.lengthSec.load(), &fold);
                paintOverviewMarks (g, r, TrackMarks::fromDeck (st, track->phrases, track.get()), false, &fold);
                // the part the zoomed waveform shows
                const double vis = waves.getVisibleSeconds();
                g.setColour (Theme::deckColour (deckIndex).withAlpha (0.16f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOfD (t - vis * 0.3), r.getY(), xOfD (t + vis * 0.7), r.getBottom()));
                if (st.stemsReady.load() && st.stemsProgress.load() < 0.999f)
                {
                    auto lab = juce::Rectangle<float> (r.getRight() - 112.0f, r.getBottom() - 13.0f, 112.0f, 13.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.75f));
                    g.fillRect (lab);
                    g.setColour (Theme::deckColour (deckIndex));
                    g.setFont (psyched::Fonts::label (9.0f));
                    g.drawText ("SEPARATING " + juce::String (juce::roundToInt (st.stemsProgress.load() * 100.0f)) + "%", lab, juce::Justification::centred);
                }
                g.setColour (juce::Colours::white);
                g.fillRect (xOfD (t) - 1.0f, r.getY() - 2.0f, 2.0f, r.getHeight() + 4.0f);
            }
        }
    }

    paintStems (g, rowStems.toFloat());

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
        chip (g, r.removeFromLeft (76.0f), "LOOP LOC", false, juce::Colour (activeLoopLine), "looploc.add");
        r.removeFromLeft (4.0f);
        chip (g, r.removeFromLeft (76.0f), "STEM LOC", false, Theme::stemColour (3), "stemloc.add");
        r.removeFromLeft (4.0f);
        chip (g, r.removeFromLeft (56.0f), "NOTE", false, Theme::text, "note.add");
        r.removeFromLeft (4.0f);
        const bool pending = st.skipPendingFrom.load() >= 0.0;
        chip (g, r.removeFromLeft (64.0f), pending ? "SKIP TO" : "SKIP", pending, skipColour, "skip.set");
        r.removeFromLeft (12.0f);

        auto zoom = r.removeFromRight (82.0f);
        const double vis = waves.getVisibleSeconds();
        chip (g, zoom.removeFromLeft (26.0f), "-", false, Theme::sync, "zoom.out");
        psyched::paintLabel (g, juce::String (vis, vis < 10.0 ? 1 : 0) + "s", zoom.removeFromLeft (30.0f), juce::Justification::centred, Theme::textDim, 9.5f);
        chip (g, zoom.removeFromLeft (26.0f), "+", false, Theme::sync, "zoom.in");
        r.removeFromRight (10.0f);

        psyched::paintLabel (g, "Jump", r.removeFromLeft (36.0f), juce::Justification::centredLeft);
        const std::pair<int, const char*> sizes[] = { { 1, "1 BEAT" }, { 4, "1 BAR" }, { 8, "2 BARS" }, { 16, "4 BARS" } };
        const float gw = juce::jlimit (60.0f, 100.0f, (r.getWidth() - 3 * 6.0f) / 4.0f);   // each size tight between its buttons
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
    {
        // grip for resizing the list
        const auto c = listEdge.toFloat().getCentre();
        const bool hot = resizingList || listEdge.contains (getMouseXYRelative());
        g.setColour (hot ? Theme::accent : Theme::line);
        for (int k = -1; k <= 1; ++k)
            g.fillRect (c.x - 0.75f + (float) k * 3.0f, c.y - 14.0f, 1.5f, 28.0f);
    }

    if (dropHover)
    {
        g.setColour (psyched::Colours::void_.withAlpha (0.7f));
        g.fillRect (getLocalBounds());
        g.setColour (Theme::accent);
        g.drawRect (getLocalBounds().toFloat().reduced (2.0f), 2.0f);
        psyched::paintTitle (g, "Drop to load into PREP", getLocalBounds().toFloat(), juce::Justification::centred, Theme::accent, 14.0f);
    }
}

void PrepView::updateFold()
{
    const auto& st = deck().state();
    std::vector<std::pair<double, double>> sk, map;
    const bool on = editView && st.skipsOn.load() && track != nullptr;
    if (on)
    {
        for (int i = 0; i < juce::jmin (st.numSkips.load(), Deck::maxSkips); ++i)
            sk.push_back ({ st.skipFrom[(size_t) i].load(), st.skipTo[(size_t) i].load() });
        for (auto& s : track->playMap) map.push_back ({ s.start, s.end });
    }
    if (on == foldOnKey && sk == foldSkipKey && map == foldMapKey) return;
    foldOnKey = on; foldSkipKey = sk; foldMapKey = map;
    fold.set (map, sk, track != nullptr && track->audio != nullptr ? track->audio->lengthSeconds() : 1.0e6);
}

double PrepView::displayNow() const
{
    const auto& st = engine.deck (deckIndex).state();
    return fold.toDisplay (st.positionSec.load(), st.playSegment.load());
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
        const auto& sl = editor.stemLocators (deckIndex);
        for (int i = 0; i < (int) sl.size(); ++i) v.push_back ({ sl[(size_t) i].timeSec, 5, i });
        const auto& nl = editor.notes (deckIndex);
        for (int i = 0; i < (int) nl.size(); ++i) v.push_back ({ nl[(size_t) i].timeSec, 6, i });
        std::stable_sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.time < b.time; });
    }
    else if (listTab == 1)
    {
        for (int i = 0; i < 8; ++i)
            if (st.hotStart[(size_t) i].load() >= 0.0) v.push_back ({ st.hotStart[(size_t) i].load(), 2, i });
    }
    else
    {
        // In play order: the rearranged order if there is one
        for (int i : phrasePlayOrder()) v.push_back ({ editor.phrases (deckIndex)[(size_t) i].startSec, 3, i });
    }
    return v;
}

std::vector<int> PrepView::phrasePlayOrder() const
{
    const auto& ph = editor.phrases (deckIndex);
    const auto& arr = editor.arrangement (deckIndex);
    std::vector<int> order;
    std::vector<bool> used (ph.size(), false);
    for (auto a : arr)
        for (int i = 0; i < (int) ph.size(); ++i)
            if (! used[(size_t) i] && std::abs (ph[(size_t) i].startSec - a) < 0.05) { order.push_back (i); used[(size_t) i] = true; break; }
    for (int i = 0; i < (int) ph.size(); ++i) if (! used[(size_t) i]) order.push_back (i);
    return order;
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
    const char* hints[] = { "click: go there - icon: colour / stems - name: rename - ctrl / shift: select",
                            "click: go there - icon: colour - name: rename - ctrl / shift: select",
                            "drag the grip to rearrange - x1: repeat - icon: colour - name: rename" };
    psyched::paintLabel (g, hints[listTab], r.removeFromTop (14.0f), juce::Justification::centredLeft, Theme::textFaint, 9.0f);
    r.removeFromTop (6.0f);

    if (listTab == 2 && track != nullptr)
    {
        auto add = r.removeFromBottom (26.0f);
        if (! editor.arrangement (deckIndex).empty())
        {
            chip (g, add.removeFromRight (juce::jmin (120.0f, add.getWidth() * 0.4f)), "RESET ORDER", true, Theme::sync, "phrase.resetorder");
            add.removeFromRight (4.0f);
        }
        chip (g, add, "+ ADD PHRASE AT PLAYHEAD", false, Theme::warn, "phrase.add");
        r.removeFromBottom (6.0f);
    }
    else if (listTab == 0 && track != nullptr)
    {
        auto add = r.removeFromBottom (26.0f);
        chip (g, add, "+ ADD AT PLAYHEAD...", false, Theme::bad, "locator.addmenu");
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
    listRowsTop = r.getY();
    listRowH = rowH;
    shownEntries = entries;

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

        if (e.kind == 3)
        {
            // grip: drag to rearrange the phrases
            auto grip = row.removeFromLeft (12.0f);
            regions.push_back ({ grip, key, 10 });
            g.setColour (grip.contains (getMouseXYRelative().toFloat()) || dragPhrase == e.index ? Theme::sync : Theme::textFaint);
            for (int gy = 0; gy < 3; ++gy)
                for (int gx = 0; gx < 2; ++gx)
                    g.fillRect (grip.getX() + 3.0f + (float) gx * 4.0f, grip.getCentreY() - 5.0f + (float) gy * 4.0f, 2.0f, 2.0f);
        }
        auto icon = row.removeFromLeft (28.0f).reduced (4.0f, 4.0f);
        if (e.kind != 1) regions.push_back ({ icon.expanded (3.0f), key, 6 });   // click the icon: colour (stem locators: which stems)
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
            // phrase repeat: click to step x1 > x2 > x3 > x4 > x8
            auto rep = row.removeFromRight (30.0f).reduced (2.0f, 4.0f);
            row.removeFromRight (2.0f);
            const bool lit = p.repeats > 1;
            chip (g, rep, "x" + juce::String (p.repeats), lit, Theme::sync, {});
            regions.push_back ({ rep, key, 11 });
        }
        else if (e.kind == 5)
        {
            const auto& l = editor.stemLocators (deckIndex)[(size_t) e.index];
            for (int s = 0; s < 4; ++s)
            {
                auto b = juce::Rectangle<float> (icon.getX() + (float) (s % 2) * icon.getWidth() * 0.5f, icon.getY() + (float) (s / 2) * icon.getHeight() * 0.5f,
                                                 icon.getWidth() * 0.5f - 1.0f, icon.getHeight() * 0.5f - 1.0f);
                const int t = l.target[(size_t) s];
                g.setColour (Theme::stemColour (s).withAlpha (t > 0 ? 1.0f : t == 0 ? 0.25f : 0.0f));
                g.fillRect (b);
                g.setColour (Theme::stemColour (s).withAlpha (0.6f));
                g.drawRect (b, 1.0f);
            }
            name = l.name.isNotEmpty() ? l.name : stemLocatorSummary (l);
            detail = (l.mode == 1 ? juce::String ("LP ") : l.mode == 2 ? juce::String ("HP ") : juce::String())
                   + (l.fadeBeats > 0.0f ? juce::String (l.fadeBeats, l.fadeBeats < 1.0f ? 2 : 0) + " beats" : juce::String ("cut"));
        }
        else if (e.kind == 6)
        {
            const auto& n = editor.notes (deckIndex)[(size_t) e.index];
            g.setColour (n.colour);
            g.fillEllipse (icon.reduced (3.0f));
            name = n.text.isNotEmpty() ? n.text : juce::String ("Note");
            detail = "note";
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

    // While dragging a phrase: where it will go
    if (dragPhrase >= 0 && dragTarget >= 0)
    {
        const float y = listRowsTop + (float) (dragTarget - listScroll) * listRowH;
        g.setColour (Theme::sync);
        g.fillRect (listArea.toFloat().reduced (10.0f, 0.0f).withY (y - 1.0f).withHeight (2.0f));
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
    if (id == "phrase.resetorder") { editor.setArrangement (deckIndex, {}); return; }
    if (id == "editview") { if (onEditViewToggled) onEditViewToggled(); return; }
    if (id == "overviewgrid") { if (onOverviewGridToggled) onOverviewGridToggled(); return; }
    if (id == "note.add") { addNoteAtPlayhead(); return; }
    if (id == "looploc.add") { addLoopLocatorAtPlayhead(); return; }

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
    else if (id == "zoom.in" || id == "zoom.out")
    {
        waves.setVisibleSeconds (waves.getVisibleSeconds() * (id == "zoom.in" ? 1.0 / 1.5 : 1.5));
        if (waves.onZoomChanged) waves.onZoomChanged (waves.getVisibleSeconds());
    }
    repaint();
}

juce::MouseCursor PrepView::getMouseCursor()
{
    return listEdge.contains (getMouseXYRelative()) || resizingList ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor;
}

void PrepView::mouseDown (const juce::MouseEvent& e)
{
    if (listEdge.contains (e.getPosition()) && ! e.mods.isPopupMenu())
    {
        resizingList = true;
        resizeStartX = e.x;
        resizeStartWidth = listArea.getWidth();
        return;
    }
    auto* r = regionAt (e.position);
    if (r == nullptr) return;
    const auto& st = deck().state();

    if (r->kind == 1)   // overview: seek
    {
        const double f = juce::jlimit (0.0, 1.0, (double) ((e.position.x - r->r.getX()) / r->r.getWidth()));
        post (Deck::Cmd::seekSeconds, fold.toSource (f * fold.displayLength (st.lengthSec.load())));
        return;
    }
    if (r->action == "deckcolour") { showDeckColourMenu (r->r); return; }
    if (r->action == "stemloc.add" && ! e.mods.isPopupMenu()) { addStemLocatorAtPlayhead (r->r); return; }
    if (r->kind == 12)   // a stem: click mutes, drag sets its level (or filter), right-click: level / filter
    {
        const int stem = r->action.getTrailingIntValue();
        if (e.mods.isPopupMenu()) { showStemModeMenu (stem, r->r); return; }
        dragStem = stem;
        return;
    }
    if (r->kind == 13)
    {
        if (e.mods.isPopupMenu()) { showStemModeMenu (-1, r->r); return; }
        const int m = (st.stemMode[0].load() + 1) % 3;
        deck().post (Deck::Cmd::stemMode, (double) m, -1);
        if (onStemModeChanged) onStemModeChanged (-1, m);
        return;
    }
    if (r->kind == 10 && ! e.mods.isPopupMenu())
    {
        // start dragging a phrase to a new place in the play order
        dragPhrase = r->action.fromFirstOccurrenceOf (":", false, false).getIntValue();
        dragTarget = -1;
        return;
    }
    if (r->kind == 11 && ! e.mods.isPopupMenu())
    {
        const int idx = r->action.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (idx < (int) editor.phrases (deckIndex).size())
        {
            const int steps[] = { 1, 2, 3, 4, 8 };
            const int cur = editor.phrases (deckIndex)[(size_t) idx].repeats;
            int next = 1;
            for (int k = 0; k < 5; ++k) if (steps[k] == cur) next = steps[(k + 1) % 5];
            if (cur > 8) next = 1;
            editor.setPhraseRepeats (deckIndex, idx, next);
        }
        return;
    }
    if (r->action == "editver") { showEditVersionMenu (r->r); return; }
    if (r->action == "locator.addmenu" && ! e.mods.isPopupMenu()) { showAddMenu (r->r); return; }
    if (r->kind == 4 || r->kind == 6 || r->kind == 7 || r->kind == 8 || r->kind == 9 || r->kind == 11)
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
        if (kind == 5 && (r->kind == 6 || e.mods.isPopupMenu()) && ! e.mods.isCtrlDown() && ! e.mods.isShiftDown())
        {
            selection = { { kind, idx } };
            anchor = { kind, idx };
            showStemEditor (idx, r->r);
            return;
        }
        if (kind == 6 && e.mods.isPopupMenu() && selection.size() <= 1) { showNoteMenu (idx, r->r); return; }
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
    if (dragPhrase >= 0)
    {
        // where the dragged phrase would land in the play order (row boundary nearest the mouse)
        const int n = (int) shownEntries.size();
        dragTarget = juce::jlimit (0, n, listScroll + (int) std::round ((e.position.y - listRowsTop) / listRowH));
        repaint (listArea);
        return;
    }
    if (dragStem >= 0 && e.getDistanceFromDragStart() > 3)
    {
        for (auto& rg : regions)
            if (rg.kind == 12 && rg.action.getTrailingIntValue() == dragStem)
                deck().post (Deck::Cmd::stemGain, juce::jlimit (0.0, 1.0, (double) ((e.position.x - rg.r.getX()) / rg.r.getWidth())), dragStem);
        return;
    }
    if (resizingList)
    {
        listWidth = juce::jlimit (200, juce::jmax (200, getWidth() - 520), resizeStartWidth - (e.x - resizeStartX));
        overviewWidth = 0;
        resized();
        repaint();
        return;
    }
    if (auto* r = regionAt (e.getMouseDownPosition().toFloat()); r != nullptr && r->kind == 1)
        post (Deck::Cmd::seekSeconds, fold.toSource (juce::jlimit (0.0, 1.0, (double) ((e.position.x - r->r.getX()) / r->r.getWidth()))
                                                     * fold.displayLength (deck().state().lengthSec.load())));
}

void PrepView::mouseUp (const juce::MouseEvent& e)
{
    if (dragStem >= 0)
    {
        if (e.getDistanceFromDragStart() <= 3) deck().post (Deck::Cmd::stemMuteToggle, 0.0, dragStem, true);
        dragStem = -1;
        return;
    }
    if (dragPhrase >= 0)
    {
        const int dragged = dragPhrase, target = dragTarget;
        dragPhrase = dragTarget = -1;
        repaint (listArea);
        if (target < 0) return;
        auto order = phrasePlayOrder();
        const auto it = std::find (order.begin(), order.end(), dragged);
        if (it == order.end()) return;
        const int from = (int) (it - order.begin());
        int to = target > from ? target - 1 : target;
        if (to == from) return;
        order.erase (it);
        order.insert (order.begin() + juce::jlimit (0, (int) order.size(), to), dragged);
        // Saved as the phrases' start times in play order (back to "as the track goes" if that's what it is)
        bool natural = true;
        for (int i = 0; i < (int) order.size(); ++i) natural = natural && order[(size_t) i] == i;
        std::vector<double> times;
        if (! natural)
            for (int i : order) times.push_back (editor.phrases (deckIndex)[(size_t) i].startSec);
        editor.setArrangement (deckIndex, times);
        return;
    }
    if (resizingList)
    {
        resizingList = false;
        if (onListWidthChanged) onListWidthChanged (listWidth);
        return;
    }
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
        if (handleCueColourMenu (r, 100, screen, current, setColour)) {}
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
        else if (handleCueColourMenu (r, 100, screen, current, setColour)) {}
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
    const auto presets = PhrasePresets::get();
    const int n = (int) presets.size();
    juce::PopupMenu m;
    m.addSectionHeader ("Add a phrase at the playhead");
    for (int i = 0; i < n; ++i)
    {
        juce::PopupMenu::Item item (presets[(size_t) i].name);
        item.itemID = 1 + i;
        item.colour = presets[(size_t) i].colour;
        m.addItem (item);
    }
    m.addSeparator();
    m.addItem (1000, "Other name...");

    juce::PopupMenu edit, ren, rem, up, col;
    for (int i = 0; i < n; ++i)
    {
        ren.addItem (2000 + i, presets[(size_t) i].name);
        rem.addItem (3000 + i, presets[(size_t) i].name);
        if (i > 0) up.addItem (4000 + i, presets[(size_t) i].name);
        juce::PopupMenu::Item ci (presets[(size_t) i].name);
        ci.itemID = 5000 + i;
        ci.colour = presets[(size_t) i].colour;
        col.addItem (ci);
    }
    edit.addItem (1001, "Add a preset...");
    edit.addSubMenu ("Change a preset's colour", col, n > 0);
    edit.addSubMenu ("Rename a preset", ren, n > 0);
    edit.addSubMenu ("Remove a preset", rem, n > 0);
    edit.addSubMenu ("Move a preset up", up, n > 1);
    edit.addSeparator();
    edit.addItem (1003, "Recolour this track's phrases with the preset colours", track != nullptr && ! editor.phrases (deckIndex).empty());
    edit.addItem (1002, "Back to the standard presets (names and colours)");
    m.addSubMenu ("Edit presets", edit);

    juce::Component::SafePointer<PrepView> safe (this);
    const auto screen = localAreaToGlobal (juce::Rectangle<int> (getMouseXYRelative().x, getMouseXYRelative().y, 1, 1));
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, screen] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto list = PhrasePresets::get();
        const int count = (int) list.size();
        const double at = safe->deck().state().positionSec.load();
        if (r >= 1 && r <= count)
            safe->editor.addPhrase (deckIndex, at, list[(size_t) r - 1].name, list[(size_t) r - 1].colour);
        else if (r == 1000)
            safe->askText ("New phrase", {}, [safe, at] (juce::String nm)
            {
                if (safe != nullptr && nm.isNotEmpty()) safe->editor.addPhrase (deckIndex, at, nm, defaultPhraseColour (nm));
            });
        else if (r == 1001)
            safe->askText ("New phrase preset", {}, [] (juce::String nm)
            {
                if (nm.isEmpty()) return;
                auto l = PhrasePresets::get();
                for (auto& e : l) if (e.name.equalsIgnoreCase (nm)) return;
                l.push_back ({ nm, defaultPhraseColour (nm) });
                PhrasePresets::set (l);
            });
        else if (r == 1002) PhrasePresets::set (PhrasePresets::factory());
        else if (r == 1003)
        {
            // every phrase whose name matches a preset takes the preset's colour
            std::vector<std::pair<int, Phrase>> changes;
            const auto& ph = safe->editor.phrases (deckIndex);
            for (int i = 0; i < (int) ph.size(); ++i)
            {
                juce::Colour c;
                if (PhrasePresets::colourFor (ph[(size_t) i].name, c) && c != ph[(size_t) i].colour)
                {
                    auto p = ph[(size_t) i];
                    p.colour = c;
                    changes.push_back ({ i, p });
                }
            }
            if (! changes.empty()) safe->editor.setPhrases (deckIndex, changes);
        }
        else if (r >= 2000 && r < 2000 + count)
        {
            const int i = r - 2000;
            safe->askText ("Rename preset", list[(size_t) i].name, [i] (juce::String nm)
            {
                auto l = PhrasePresets::get();
                if (nm.isEmpty() || i >= (int) l.size()) return;
                l[(size_t) i].name = nm;
                PhrasePresets::set (l);
            });
        }
        else if (r >= 3000 && r < 3000 + count) { list.erase (list.begin() + (r - 3000)); PhrasePresets::set (list); }
        else if (r >= 4001 && r < 4000 + count) { std::swap (list[(size_t) (r - 4000)], list[(size_t) (r - 4001)]); PhrasePresets::set (list); }
        else if (r >= 5000 && r < 5000 + count)
        {
            // pick the preset's colour (the same colours as everywhere else, custom and saved ones too)
            const int i = r - 5000;
            const auto current = list[(size_t) i].colour;
            juce::PopupMenu cm;
            cm.addSectionHeader ("\"" + list[(size_t) i].name + "\" colour - new phrases of this name get it");
            addCueColourItems (cm, 100, current);
            cm.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [i, screen, current] (int res)
            {
                auto set = [i] (juce::Colour c)
                {
                    auto l = PhrasePresets::get();
                    if (i < (int) l.size()) { l[(size_t) i].colour = c; PhrasePresets::set (l); }
                };
                handleCueColourMenu (res, 100, screen, current, set);
            });
        }
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
        else if (handleCueColourMenu (r, 100, screen, p.colour, setColour)) {}
    });
}

PrepView::ListEntry PrepView::entryFor (int kind, int idx) const
{
    const auto& st = engine.deck (deckIndex).state();
    double time = 0.0;
    if (kind == 0) time = st.locStart[(size_t) idx].load();
    else if (kind == 1) time = st.skipFrom[(size_t) idx].load();
    else if (kind == 2) time = st.hotStart[(size_t) idx].load();
    else if (kind == 5) { if (idx < (int) editor.stemLocators (deckIndex).size()) time = editor.stemLocators (deckIndex)[(size_t) idx].timeSec; }
    else if (kind == 6) { if (idx < (int) editor.notes (deckIndex).size()) time = editor.notes (deckIndex)[(size_t) idx].timeSec; }
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
    if (e.kind == 6 && e.index < (int) editor.notes (deckIndex).size()) return editor.notes (deckIndex)[(size_t) e.index].colour;
    return {};
}

void PrepView::applyColour (const std::vector<ListEntry>& targets, juce::Colour c)
{
    for (auto& t : targets)
    {
        if (t.kind == 0) deck().post (Deck::Cmd::locatorColour, (double) c.getARGB(), t.index);
        else if (t.kind == 2) deck().post (Deck::Cmd::hotCueColour, (double) c.getARGB(), t.index);
    }
    std::vector<std::pair<int, Phrase>> phraseChanges;
    for (auto& t : targets)
        if (t.kind == 3 && t.index < (int) editor.phrases (deckIndex).size())
        {
            auto p = editor.phrases (deckIndex)[(size_t) t.index];
            p.colour = c;
            phraseChanges.push_back ({ t.index, p });
        }
    if (! phraseChanges.empty()) editor.setPhrases (deckIndex, phraseChanges);
    for (auto& t : targets)
        if (t.kind == 6 && t.index < (int) editor.notes (deckIndex).size())
        {
            auto n = editor.notes (deckIndex)[(size_t) t.index];
            n.colour = c;
            editor.setNote (deckIndex, t.index, n);
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
        else if (t.kind == 5) editor.deleteStemLocator (deckIndex, t.index);
        else if (t.kind == 6) editor.deleteNote (deckIndex, t.index);
    }
    selection.clear();
    anchor = { -1, -1 };
    repaint();
}

void PrepView::showColourMenu (const ListEntry& e, juce::Rectangle<float> area)
{
    const auto targets = targetsFor (e);
    std::vector<ListEntry> colourable;
    for (auto& t : targets) if (t.kind != 1 && t.kind != 5) colourable.push_back (t);
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
        if (handleCueColourMenu (r, 100, screen, current, set)) {}
    });
}

juce::String PrepView::nameOf (const ListEntry& e) const
{
    if (e.kind == 0) return editor.cueName (deckIndex, false, e.index);
    if (e.kind == 2) return editor.cueName (deckIndex, true, e.index);
    if (e.kind == 3 && e.index < (int) editor.phrases (deckIndex).size()) return editor.phrases (deckIndex)[(size_t) e.index].name;
    if (e.kind == 5 && e.index < (int) editor.stemLocators (deckIndex).size()) return editor.stemLocators (deckIndex)[(size_t) e.index].name;
    if (e.kind == 6 && e.index < (int) editor.notes (deckIndex).size()) return editor.notes (deckIndex)[(size_t) e.index].text;
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
    t.setTextToShowWhenEmpty (e.kind == 3 ? "Phrase name" : e.kind == 6 ? "Note text" : "Name", Theme::textFaint);
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
            else if (e.kind == 5 && e.index < (int) editor.stemLocators (deckIndex).size())
            {
                auto l = editor.stemLocators (deckIndex)[(size_t) e.index];
                l.name = text;
                editor.setStemLocator (deckIndex, e.index, l);
            }
            else if (e.kind == 6 && e.index < (int) editor.notes (deckIndex).size())
            {
                auto n = editor.notes (deckIndex)[(size_t) e.index];
                n.text = text;
                editor.setNote (deckIndex, e.index, n);
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


juce::String PrepView::stemLocatorSummary (const StemLocator& l)
{
    const char* names[] = { "Drums", "Bass", "Inst", "Vocals" };
    juce::StringArray on, off;
    for (int s = 0; s < 4; ++s)
    {
        if (l.target[(size_t) s] > 0) on.add (names[s]);
        else if (l.target[(size_t) s] == 0) off.add (names[s]);
    }
    juce::String t;
    if (! off.isEmpty()) t << off.joinIntoString (", ") << " out";
    if (! on.isEmpty()) t << (t.isNotEmpty() ? " - " : "") << on.joinIntoString (", ") << " in";
    return t.isNotEmpty() ? t : juce::String ("Stems (no change)");
}

namespace
{
    /** One stem's change in a stem locator: no change, fade out (off) or fade in (on). Drawn in the stem's colour. */
    class StemToggle : public juce::Component
    {
    public:
        int stem = 0, state = -1;   // -1 no change, 0 out, 1 in
        bool fades = true;
        std::function<void()> onClick;
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const auto c = Theme::stemColour (stem);
            const bool hover = isMouseOver();
            g.setColour (state == 1 ? c : state == 0 ? c.withAlpha (0.22f) : juce::Colour (0xff1d1a2e));
            g.fillRect (r);
            g.setColour (state < 0 ? (hover ? Theme::textDim : Theme::line) : c);
            g.drawRect (r, state < 0 ? 1.0f : 2.0f);
            const char* names[] = { "DRUMS", "BASS", "INST", "VOCALS" };
            const auto ink = state == 1 ? (c.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white) : Theme::text;
            g.setColour (ink);
            g.setFont (Theme::font (12.0f, true));
            g.drawText (names[juce::jlimit (0, 3, stem)], r.withTrimmedBottom (r.getHeight() * 0.45f), juce::Justification::centredBottom);
            g.setFont (psyched::Fonts::mono (10.5f, true));
            g.setColour (state == 1 ? ink : state == 0 ? c.brighter (0.5f) : Theme::textFaint);
            const juce::String what = state == 1 ? (fades ? "FADE IN" : "ON") : state == 0 ? (fades ? "FADE OUT" : "OFF") : "NO CHANGE";
            g.drawText (what, r.withTrimmedTop (r.getHeight() * 0.55f), juce::Justification::centredTop);
        }
        void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
    };

    /** Edits a stem locator: each stem fades in / out (or doesn't change), how long it takes, and whether it's a
        level fade or a low-pass / high-pass filter sweep. */
    class StemLocatorEditor : public juce::Component
    {
    public:
        StemLocatorEditor (StemLocator l, std::function<void (const StemLocator&)> changed, std::function<void()> deleted)
            : loc (std::move (l)), onChange (std::move (changed)), onDelete (std::move (deleted))
        {
            for (int s = 0; s < 4; ++s)
            {
                auto& b = buttons[(size_t) s];
                b.stem = s;
                addAndMakeVisible (b);
                b.onClick = [this, s]
                {
                    auto& t = loc.target[(size_t) s];
                    t = t < 0 ? 0 : t == 0 ? 1 : -1;   // no change > out > in > no change
                    refresh();
                    if (onChange) onChange (loc);
                };
            }
            addAndMakeVisible (fade);
            const float steps[] = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f };
            for (int i = 0; i < 7; ++i)
                fade.addItem (steps[i] == 0.0f ? juce::String ("Straight away (no fade)") : "Over " + juce::String ((int) steps[i]) + (steps[i] == 1.0f ? " beat" : " beats"), i + 1);
            int sel = 1;
            for (int i = 0; i < 7; ++i) if (std::abs (steps[i] - loc.fadeBeats) < 0.01f) sel = i + 1;
            fade.setSelectedId (sel, juce::dontSendNotification);
            fade.onChange = [this, steps]
            {
                loc.fadeBeats = steps[juce::jlimit (0, 6, fade.getSelectedId() - 1)];
                refresh();
                if (onChange) onChange (loc);
            };
            addAndMakeVisible (mode);
            mode.addItemList ({ "Level fade (volume)", "Low-pass filter sweep", "High-pass filter sweep" }, 1);
            mode.setSelectedId (juce::jlimit (0, 2, loc.mode) + 1, juce::dontSendNotification);
            mode.onChange = [this]
            {
                loc.mode = mode.getSelectedId() - 1;
                if (onChange) onChange (loc);
            };
            addAndMakeVisible (del);
            del.onClick = [this]
            {
                if (onDelete) onDelete();
                if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
            };
            refresh();
            setSize (380, 178);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff141221));
            psyched::paintLabel (g, "Stem locator - click a stem: no change / fade out / fade in", getLocalBounds().removeFromTop (22).reduced (8, 0).toFloat(),
                                 juce::Justification::centredLeft, Theme::textDim, 9.0f);
            auto r = getLocalBounds().reduced (8);
            r.removeFromTop (18 + 44 + 8);
            psyched::paintLabel (g, "HOW", r.removeFromTop (26).removeFromLeft (40).toFloat(), juce::Justification::centredLeft, Theme::textFaint, 9.0f);
            r.removeFromTop (6);
            psyched::paintLabel (g, "TIME", r.removeFromTop (26).removeFromLeft (40).toFloat(), juce::Justification::centredLeft, Theme::textFaint, 9.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            r.removeFromTop (18);
            auto row = r.removeFromTop (44);
            const int w = (row.getWidth() - 3 * 4) / 4;
            for (auto& b : buttons) { b.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4); }
            r.removeFromTop (8);
            mode.setBounds (r.removeFromTop (26).withTrimmedLeft (44));
            r.removeFromTop (6);
            auto bottom = r.removeFromTop (26).withTrimmedLeft (44);
            del.setBounds (bottom.removeFromRight (74));
            bottom.removeFromRight (6);
            fade.setBounds (bottom);
        }

    private:
        void refresh()
        {
            for (int s = 0; s < 4; ++s)
            {
                buttons[(size_t) s].state = loc.target[(size_t) s];
                buttons[(size_t) s].fades = loc.fadeBeats > 0.0f;
                buttons[(size_t) s].repaint();
            }
        }

        StemLocator loc;
        std::function<void (const StemLocator&)> onChange;
        std::function<void()> onDelete;
        std::array<StemToggle, 4> buttons;
        juce::ComboBox fade, mode;
        juce::TextButton del { "DELETE" };
    };
}

void PrepView::showStemEditor (int index, juce::Rectangle<float> area)
{
    if (index < 0 || index >= (int) editor.stemLocators (deckIndex).size()) return;
    const auto l = editor.stemLocators (deckIndex)[(size_t) index];
    const double at = l.timeSec;
    juce::Component::SafePointer<PrepView> safe (this);
    // Finds the locator again by its time (indexes move when others are added or removed)
    auto indexOf = [safe, at]
    {
        if (safe == nullptr) return -1;
        const auto& list = safe->editor.stemLocators (deckIndex);
        for (int i = 0; i < (int) list.size(); ++i) if (std::abs (list[(size_t) i].timeSec - at) < 0.01) return i;
        return -1;
    };
    auto content = std::make_unique<StemLocatorEditor> (l,
        [safe, indexOf] (const StemLocator& nl) { if (safe != nullptr) if (const int i = indexOf(); i >= 0) safe->editor.setStemLocator (deckIndex, i, nl); },
        [safe, indexOf] { if (safe != nullptr) if (const int i = indexOf(); i >= 0) safe->editor.deleteStemLocator (deckIndex, i); });
    juce::CallOutBox::launchAsynchronously (std::move (content), localAreaToGlobal (area.toNearestInt()), nullptr);
}

void PrepView::showNoteMenu (int index, juce::Rectangle<float> area)
{
    if (index < 0 || index >= (int) editor.notes (deckIndex).size()) return;
    const auto n = editor.notes (deckIndex)[(size_t) index];
    juce::PopupMenu m;
    m.addSectionHeader ("Note");
    addCueColourItems (m, 100, n.colour);
    m.addSeparator();
    m.addItem (2, "Edit text");
    m.addItem (1, "Delete");
    const auto screen = localAreaToGlobal (area.toNearestInt());
    juce::Component::SafePointer<PrepView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (screen), [safe, index, n, screen] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) { safe->editor.deleteNote (deckIndex, index); return; }
        if (r == 2) { safe->startRename (safe->entryFor (6, index)); return; }
        auto set = [safe] (juce::Colour c) { if (safe != nullptr) safe->applyColour ({ safe->entryFor (6, safe->selection.empty() ? -1 : safe->selection.begin()->second) }, c); };
        auto setOne = [safe, index] (juce::Colour c) { if (safe != nullptr) safe->applyColour ({ safe->entryFor (6, index) }, c); };
        juce::ignoreUnused (set);
        if (handleCueColourMenu (r, 100, screen, n.colour, setOne)) {}
    });
}

void PrepView::showAddMenu (juce::Rectangle<float> area)
{
    if (track == nullptr) return;
    juce::PopupMenu m;
    m.addSectionHeader ("Add at the playhead");
    m.addItem (1, "Locator  (a loop locator if a loop is set here)");
    m.addItem (4, "Loop locator  (" + juce::String (loopBeats, loopBeats < 1.0 ? 2 : 0) + " beats from here)");
    m.addItem (2, "Stem locator - change the stems from here...");
    m.addItem (3, "Note - a reminder that pops up on the deck...");
    juce::Component::SafePointer<PrepView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (area.toNearestInt())),
                     [safe, area] (int r)
    {
        if (safe == nullptr || safe->track == nullptr || r == 0) return;
        double at = safe->deck().state().positionSec.load();
        // snap to the nearest beat
        if (safe->track->grid.isValid()) at = safe->track->grid.timeAtBeat (std::round (safe->track->grid.beatPosition (at)));
        juce::ignoreUnused (at);
        if (r == 1) safe->post (Deck::Cmd::locatorAdd, -1.0);
        else if (r == 2) safe->addStemLocatorAtPlayhead (area);
        else if (r == 3) safe->addNoteAtPlayhead();
        else if (r == 4) safe->addLoopLocatorAtPlayhead();
    });
}

void PrepView::showEditVersionMenu (juce::Rectangle<float> area)
{
    if (track == nullptr) return;
    const auto names = editor.editVersionNames (deckIndex);
    const auto active = editor.activeEdit (deckIndex);
    juce::PopupMenu m;
    m.addSectionHeader ("Edit versions - skips, loop modes, stem locators, phrase order and repeats");
    for (int i = 0; i < (int) names.size(); ++i)
        m.addItem (10 + i, names[(size_t) i], true, names[(size_t) i] == active);
    m.addItem (1, "No edits (the track as recorded)", true, active.isEmpty() && ! editor.hasEdits (deckIndex));
    m.addSeparator();
    m.addItem (2, "Save the current edits as a new version...");
    if (active.isNotEmpty())
        m.addItem (3, "Update \"" + active + "\" with the current edits", editor.activeEditModified (deckIndex));
    if (! names.empty())
    {
        juce::PopupMenu ren, del;
        for (int i = 0; i < (int) names.size(); ++i)
        {
            ren.addItem (100 + i, names[(size_t) i]);
            del.addItem (200 + i, names[(size_t) i]);
        }
        m.addSubMenu ("Rename", ren);
        m.addSubMenu ("Delete", del);
    }
    juce::Component::SafePointer<PrepView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (area.toNearestInt())),
                     [safe, names, active] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& ed = safe->editor;
        if (r == 1) ed.applyNoEdits (deckIndex);
        else if (r == 2)
            safe->askText ("Name this edit version (e.g. Radio edit, Extended, Peak-time)", {}, [safe] (juce::String n)
            {
                if (safe != nullptr && n.isNotEmpty()) safe->editor.saveEditVersion (deckIndex, n);
            });
        else if (r == 3) ed.saveEditVersion (deckIndex, active);
        else if (r >= 200) ed.deleteEditVersion (deckIndex, r - 200);
        else if (r >= 100)
        {
            const int i = r - 100;
            safe->askText ("Rename edit version", names[(size_t) i], [safe, i] (juce::String n)
            {
                if (safe != nullptr && n.isNotEmpty()) safe->editor.renameEditVersion (deckIndex, i, n);
            });
        }
        else if (r >= 10) ed.applyEditVersion (deckIndex, r - 10);
    });
}

void PrepView::addNoteAtPlayhead()
{
    if (track == nullptr) return;
    double at = deck().state().positionSec.load();
    if (track->grid.isValid()) at = track->grid.timeAtBeat (std::round (track->grid.beatPosition (at)));
    juce::Component::SafePointer<PrepView> safe (this);
    askText ("Note at " + formatTime (at), {}, [safe, at] (juce::String text)
    {
        if (safe == nullptr || text.isEmpty()) return;
        NoteLocator n;
        n.timeSec = at;
        n.text = text;
        safe->editor.addNote (deckIndex, n);
    });
}

void PrepView::addStemLocatorAtPlayhead (juce::Rectangle<float> area)
{
    if (track == nullptr) return;
    double at = deck().state().positionSec.load();
    if (track->grid.isValid()) at = track->grid.timeAtBeat (std::round (track->grid.beatPosition (at)));
    StemLocator l;
    l.timeSec = at;
    l.target = { -1, -1, -1, 0 };   // a start: vocals out
    l.fadeBeats = 4.0f;
    editor.addStemLocator (deckIndex, l);
    const auto& list = editor.stemLocators (deckIndex);
    for (int i = 0; i < (int) list.size(); ++i)
        if (std::abs (list[(size_t) i].timeSec - at) < 0.01) { listTab = 0; showStemEditor (i, area); break; }
}

void PrepView::addLoopLocatorAtPlayhead()
{
    if (track == nullptr) return;
    const double at = deck().state().positionSec.load();
    double s0 = at, s1 = at + loopBeats * 0.5;
    if (track->grid.isValid())
    {
        const double b = std::round (track->grid.beatPosition (at));
        s0 = track->grid.timeAtBeat (b);
        s1 = track->grid.timeAtBeat (b + loopBeats);
    }
    deck().post (Deck::Command { Deck::Cmd::locatorLoopAdd, juce::jmax (0.0, s0), 0, true, nullptr, 0, s1 });
}

void PrepView::paintStems (juce::Graphics& g, juce::Rectangle<float> r)
{
    // PREP's stem controls (click mutes, drag sets the level, right-click: level / low-pass / high-pass)
    const auto& st = deck().state();
    const bool ready = st.stemsReady.load();
    int m0 = st.stemMode[0].load();
    bool same = true;
    for (size_t k = 1; k < 4; ++k) same = same && st.stemMode[k].load() == m0;
    const char* names[] = { "LVL", "LP", "HP" };
    auto mc = r.removeFromLeft (36.0f);
    psyched::paintChip (g, mc, same ? names[juce::jlimit (0, 2, m0)] : "MIX", Theme::sync, (! same || m0 != 0) ? psyched::ChipState::on : psyched::ChipState::off);
    regions.push_back ({ mc, "stemmode", 13 });
    r.removeFromLeft (6.0f);
    const float sw = (r.getWidth() - 3 * 5.0f) / 4.0f;
    for (int s = 0; s < 4; ++s)
    {
        auto b = juce::Rectangle<float> (r.getX() + (float) s * (sw + 5.0f), r.getY(), sw, r.getHeight());
        const float gain = st.stemGain[(size_t) s].load();
        const auto sc = Theme::stemColour (s);
        g.setColour (Theme::panelHi);
        g.fillRect (b);
        if (ready)
        {
            g.setColour (sc.withAlpha (gain <= 0.0f ? 0.12f : 0.6f));
            g.fillRect (b.withTrimmedRight (b.getWidth() * (1.0f - juce::jmax (gain, 0.04f))));
        }
        g.setColour (ready ? sc.withAlpha (0.8f) : Theme::line);
        g.drawRect (b.reduced (0.5f), 1.0f);
        const int mode = st.stemMode[(size_t) s].load();
        g.setColour (ready ? (gain <= 0.0f ? Theme::textFaint : Theme::text) : Theme::textFaint);
        g.setFont (Theme::font (11.0f, true));
        g.drawText (juce::String (stemName (s)).toUpperCase() + (mode == 1 ? " LP" : mode == 2 ? " HP" : ""), b, juce::Justification::centred);
        regions.push_back ({ b, "stem." + juce::String (s), 12 });
    }
    if (! ready)
        psyched::paintLabel (g, track != nullptr ? "NO STEMS YET" : "", r.withY (r.getBottom() - 11.0f).withHeight (11.0f), juce::Justification::centred, Theme::textFaint, 8.0f);
}

void PrepView::showStemModeMenu (int stem, juce::Rectangle<float> area)
{
    const auto& st = deck().state();
    const int cur = stem >= 0 ? st.stemMode[(size_t) stem].load() : -1;
    juce::PopupMenu m;
    const char* labels[] = { "Level (volume)", "Low-pass filter (closes down as the control goes down)", "High-pass filter (closes up as the control goes down)" };
    if (stem >= 0)
    {
        m.addSectionHeader (juce::String (stemName (stem)) + " control");
        for (int k = 0; k < 3; ++k) m.addItem (1 + k, labels[k], true, cur == k);
        m.addSeparator();
    }
    m.addSectionHeader ("All four stems");
    for (int k = 0; k < 3; ++k) m.addItem (11 + k, labels[k]);
    juce::Component::SafePointer<PrepView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (area.toNearestInt())),
                     [safe, stem] (int r)
    {
        if (safe == nullptr || r == 0) return;
        const int s = r >= 11 ? -1 : stem, mode = r >= 11 ? r - 11 : r - 1;
        safe->deck().post (Deck::Cmd::stemMode, (double) mode, s);
        if (safe->onStemModeChanged) safe->onStemModeChanged (s, mode);
    });
}

void PrepView::showDeckColourMenu (juce::Rectangle<float> area)
{
    const auto current = Theme::deckColour (deckIndex);
    juce::PopupMenu menu;
    menu.addSectionHeader ("PREP colour");
    menu.addItem (200, "Default (yellow)");
    menu.addSeparator();
    addCueColourItems (menu, 100, current);
    const auto screen = localAreaToGlobal (area.toNearestInt());
    juce::Component::SafePointer<PrepView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (screen), [safe, screen, current] (int result)
    {
        if (safe == nullptr || safe->onDeckColour == nullptr) return;
        auto set = [safe] (juce::Colour c) { if (safe != nullptr && safe->onDeckColour) safe->onDeckColour (c); };
        if (result == 200) set (psyched::Colours::yellow);
        else handleCueColourMenu (result, 100, screen, current, set);
    });
}

} // namespace orbit
