#include "GridMoreMenu.h"
#include "DeckPanel.h"
#include "TrackMarks.h"
#include "../App/Features.h"
#include "Theme.h"
#include "../Audio/MusicalKey.h"
#include "../Control/Actions.h"
#include "StemControl.h"

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

    void chip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool lit, juce::Colour litColour)
    {
        psyched::paintChip (g, r, text, litColour, lit ? psyched::ChipState::on : psyched::ChipState::off);
    }
}

DeckPanel::DeckPanel (int d, AudioEngine& e, ActionRouter& r) : deck (d), engine (e), router (r)
{
    setOpaque (true);
    addAndMakeVisible (rotor);
    startTimerHz (20);
}

void DeckPanel::resized()
{
    auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
    rowInfo = r.removeFromTop (38.0f);
    rowStatus = r.removeFromTop (26.0f);
    r.removeFromTop (6.0f);
    // the overview gives way first, so the transport row keeps at least 30 px
    const float fixed = 6.0f + 8.0f + 26.0f + 6.0f + 22.0f + 6.0f + 30.0f;
    rowOverview = r.removeFromTop (juce::jlimit (22.0f, 56.0f, juce::jmin (r.getHeight() * 0.36f, r.getHeight() - fixed)));
    r.removeFromTop (8.0f);
    rowPads = r.removeFromTop (26.0f);
    r.removeFromTop (6.0f);
    rowJump = r.removeFromTop (22.0f);
    r.removeFromTop (6.0f);
    rowControls = r;
    overviewWidth = 0;
    repaint();
}

void DeckPanel::timerCallback()
{
    const auto& st = engine.deck (deck).state();
    const int s = st.trackSerial.load();
    if (s != serial)
    {
        serial = s;
        track = s != 0 ? engine.deck (deck).getLoadedTrack() : nullptr;
        overviewWidth = 0;
        loadingText = {};
        repaint();
        return;
    }
    if (const int ver = st.trackDataVersion.load(); ver != dataVersion)
    {
        dataVersion = ver;
        if (s != 0) track = engine.deck (deck).getLoadedTrack();
    }

    // Repaint only when something visible changed.
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (double v) { h ^= (juce::uint64) std::llround (v * 100.0); h *= 1099511628211ull; };
    mix (std::floor (st.positionSec.load() * 10.0)); mix (st.bpm.load()); mix (st.playing.load()); mix (st.sync.load());
    mix (st.master.load()); mix (st.keyLock.load()); mix (st.quantize.load()); mix (st.loopActive.load());
    mix (st.tempoRatio.load() * 100.0); mix (st.stemsReady.load()); mix (std::floor (st.stemsProgress.load() * 100.0f));
    mix (st.cuePointSec.load()); mix (st.endWarning.load() ? std::floor (juce::Time::getMillisecondCounter() / 400.0) : 0.0);
    for (size_t i = 0; i < 8; ++i) mix (st.hotStart[i].load());
    for (size_t i = 0; i < 4; ++i) mix (st.stemGain[i].load() * 100.0f);
    mix (st.keyShift.load()); mix (st.keyLock.load());
    mix (st.numLocators.load()); mix (st.numSkips.load()); mix (st.skipsOn.load()); mix (st.skipPendingFrom.load()); mix (st.cueEdits.load());
    for (size_t i = 0; i < 8; ++i) mix ((double) st.hotColour[i].load());
    mix (st.trackDataVersion.load());
    if (editor != nullptr) { mix (editor->editsRekordbox (deck)); mix (editor->editsCentrifuge (deck)); mix (editor->versionsDiffer (deck)); }
    mix (gridMode); mix (router.getLoopBeats (deck)); mix (st.autoLoopLocator.load()); mix (st.cyclesLeft.load());
    for (size_t i = 0; i < 8; ++i) mix (st.hotEnd[i].load());
    mix (Theme::deckColour (deck).getARGB()); mix (MusicalKey::camelotDisplay().load());
    rotor.setBounds (rotorArea.toNearestInt());
    rotor.setSpinning (st.stemsReady.load() && st.stemsProgress.load() < 0.999f);
    rotor.setProgress (st.stemsReady.load() ? st.stemsProgress.load() : 0.0f);
    mix (router.getPitchRangePercent (deck));
    mix (editView); mix (st.playSegment.load());
    if (editor != nullptr) { mix ((double) (editor->activeEdit (deck).hashCode() & 0xffff)); mix (editor->activeEditModified (deck)); mix (editor->hasEdits (deck)); }
    if (auto* n = noteShowing (st.positionSec.load())) mix (n->timeSec + 7.0 + std::floor (st.positionSec.load() * 10.0));
    for (size_t i = 0; i < 4; ++i) mix (st.stemMode[i].load());
    mix (overviewGrid); mix (st.stemsReady.load() && st.stemsProgress.load() < 0.999f ? std::floor (st.stemsProgress.load() * 400.0f) : -1.0);
    if (h != lastHash) { lastHash = h; repaint(); }
}

void DeckPanel::paint (juce::Graphics& g)
{
    const auto col = Theme::deckColour (deck);
    g.fillAll (Theme::bg);
    psyched::paintPanel (g, getLocalBounds().toFloat(), col);
    regions.clear();

    const auto& st = engine.deck (deck).state();
    const bool loaded = track != nullptr;

    // --- Info row ------------------------------------------------------------
    {
        auto r = rowInfo;
        auto badge = r.removeFromLeft (30.0f).withSizeKeepingCentre (28.0f, 28.0f);
        g.setColour (col);
        g.fillRect (badge);
        g.setColour (psyched::Colours::onSolid);
        g.setFont (Theme::display (15.0f, true));
        g.drawText (juce::String (deck + 1), badge, juce::Justification::centred);
        regions.push_back ({ badge, "deckcolour", 5 });
        r.removeFromLeft (10.0f);

        auto bpmArea = r.removeFromRight (110.0f);
        g.setColour (st.master.load() ? Theme::accent : Theme::text);
        g.setFont (Theme::mono (26.0f, true));
        const double bpm = st.bpm.load();
        g.drawText (loaded && bpm > 0 ? juce::String (bpm, 2) : "--.--", bpmArea.removeFromTop (28.0f), juce::Justification::centredRight);
        g.setColour (Theme::textDim);
        g.setFont (psyched::Fonts::label());
        g.drawText ("BPM", bpmArea, juce::Justification::topRight);

        if (loaded && track->info.key.isNotEmpty())
        {
            // [-] [key +n] [+] [KEY SYNC]
            const auto p = "d" + juce::String (deck + 1) + ".";
            const int shift = st.keyShift.load();
            auto syncChip = r.removeFromRight (40.0f).withSizeKeepingCentre (38.0f, 22.0f);
            chip (g, syncChip, "K.SYNC", router.ledState (*findAction (p + "key.sync")), Theme::good.darker (0.2f));
            regions.push_back ({ syncChip, p + "key.sync", 0 });
            r.removeFromRight (4.0f);

            auto plus = r.removeFromRight (18.0f).withSizeKeepingCentre (18.0f, 22.0f);
            auto keyChip = r.removeFromRight (76.0f).withSizeKeepingCentre (74.0f, 22.0f);   // one size for every key, shifted or not
            auto minus = r.removeFromRight (18.0f).withSizeKeepingCentre (18.0f, 22.0f);
            chip (g, minus, "-", false, Theme::good);
            chip (g, plus, "+", false, Theme::good);
            regions.push_back ({ minus, p + "key.down", 0 });
            regions.push_back ({ plus, p + "key.up", 0 });
            regions.push_back ({ keyChip, p + "key.reset", 0 });

            g.setColour (Theme::panelHi);
            g.fillRect (keyChip);
            g.setColour (shift != 0 ? Theme::warn : Theme::good);
            g.setFont (Theme::font (13.0f, true));
            auto keyText = MusicalKey::display (router.effectiveKeyText (deck));
            if (shift != 0) keyText << "  " << (shift > 0 ? "+" : "") << shift;
            g.drawText (keyText, keyChip, juce::Justification::centred);
            r.removeFromRight (6.0f);
        }

        if (loaded)
        {
            if (! track->info.colour.isTransparent())
            {
                g.setColour (track->info.colour);
                g.fillRect (r.removeFromLeft (3.0f).reduced (0.0f, 4.0f));
                r.removeFromLeft (7.0f);
            }
            g.setColour (Theme::text);
            g.setFont (Theme::font (16.5f, true));
            g.drawText (track->info.title, r.removeFromTop (21.0f), juce::Justification::bottomLeft, true);
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (13.0f));
            g.drawText (track->info.artist, r, juce::Justification::topLeft, true);
        }
        else
        {
            g.setColour (Theme::textFaint);
            g.setFont (Theme::font (15.0f));
            g.drawText (loadingText.isNotEmpty() ? loadingText : juce::String ("No track loaded"), r, juce::Justification::centredLeft);
        }
    }

    // --- Status row ------------------------------------------------------------
    {
        auto r = rowStatus;
        updateFold();
        // Times follow the edited view: how far into the track as it plays, and how long is really left
        fold.reference = fold.toDisplay (st.positionSec.load(), st.playSegment.load());
        const double pos = fold.reference, len = fold.displayLength (st.lengthSec.load());
        g.setFont (Theme::mono (17.0f, true));
        g.setColour (Theme::text);
        g.drawText (loaded ? formatTime (pos) : "0:00.0", r.removeFromLeft (78.0f), juce::Justification::centredLeft);
        const bool warn = st.endWarning.load() && (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
        g.setColour (warn ? Theme::bad : Theme::textDim);
        g.drawText (loaded ? formatTime (-(len - pos)) : "-0:00.0", r.removeFromLeft (86.0f), juce::Justification::centredLeft);

        // (The phase meter for both decks is in the top bar.)

        // Edit targets: R = rekordbox copy, C = Centrifuge version (both lit = edit both); GRID opens the grid editor
        if (! Features::rekordboxCopy)
        {
            const auto p = juce::String ("d") + juce::String (deck + 1) + ".";
            r.removeFromLeft (10.0f);
            auto gc = r.removeFromLeft (44.0f).reduced (1.0f, 3.0f);
            chip (g, gc, "GRID", gridMode, Theme::warn);
            regions.push_back ({ gc, p + "grid.mode", 0 });
            // Edited view on / off, and which edit version is playing
            r.removeFromLeft (4.0f);
            auto ev = r.removeFromLeft (58.0f).reduced (1.0f, 3.0f);
            chip (g, ev, "EDITED", editView, Theme::sync);
            regions.push_back ({ ev, p + "editview", 0 });
            if (loaded && editor != nullptr)
            {
                r.removeFromLeft (4.0f);
                auto name = editor->activeEdit (deck);
                const bool any = editor->hasEdits (deck);
                juce::String text = name.isNotEmpty() ? name : (any ? juce::String ("UNSAVED EDITS") : juce::String ("ORIGINAL"));
                if (name.isNotEmpty() && editor->activeEditModified (deck)) text << " *";
                // The longest wording that fits the space left: "EDIT: name", "name", then "EDIT"
                const float room = juce::jmax (36.0f, r.getWidth() - 450.0f);
                juce::String shown;
                for (auto cand : { "EDIT: " + text, text, juce::String ("EDIT") })
                    if (psyched::Fonts::label().getStringWidthFloat (cand.toUpperCase()) + 22.0f <= juce::jmin (room, 190.0f)) { shown = cand; break; }
                if (shown.isEmpty()) shown = "EDIT";
                const float w = juce::jmin (room, juce::jlimit (44.0f, 190.0f, psyched::Fonts::label().getStringWidthFloat (shown.toUpperCase()) + 22.0f));
                auto vc = r.removeFromLeft (w).reduced (1.0f, 3.0f);
                chip (g, vc, shown, name.isNotEmpty() || any, Theme::sync.brighter (0.3f));
                regions.push_back ({ vc, p + "editver", 0 });
            }
        }
        else
        {
            const auto p = juce::String ("d") + juce::String (deck + 1) + ".";
            r.removeFromLeft (10.0f);
            const bool er = editor != nullptr && editor->editsRekordbox (deck), ec = editor != nullptr && editor->editsCentrifuge (deck);
            auto rc = r.removeFromLeft (24.0f).reduced (1.0f, 3.0f);
            chip (g, rc, "R", er, juce::Colour (0xff4fa3ff));
            regions.push_back ({ rc, p + "edit.r", 0 });
            auto cc = r.removeFromLeft (24.0f).reduced (1.0f, 3.0f);
            chip (g, cc, "C", ec, Theme::accent);
            regions.push_back ({ cc, p + "edit.c", 0 });
            if (editor != nullptr && editor->versionsDiffer (deck))
            {
                g.setColour (Theme::accent);
                g.setFont (Theme::font (10.0f, true));
                g.drawText ("R/C DIFFER", r.removeFromLeft (64.0f), juce::Justification::centred);
            }
            r.removeFromLeft (4.0f);
            auto gc = r.removeFromLeft (44.0f).reduced (1.0f, 3.0f);
            chip (g, gc, "GRID", gridMode, Theme::warn);
            regions.push_back ({ gc, p + "grid.mode", 0 });
        }

        // Tempo + range
        auto right = r.removeFromRight (440.0f);
        const double pct = (st.tempoRatio.load() - 1.0) * 100.0;
        auto tempo = right.removeFromRight (100.0f);
        g.setColour (Theme::text);
        g.setFont (Theme::font (14.0f, true));
        auto pctArea = tempo.removeFromTop (16.0f);
        g.drawText ((pct >= 0 ? "+" : "") + juce::String (pct, 2) + "%", pctArea, juce::Justification::centredRight);
        regions.push_back ({ pctArea, "tempo.drag", 6 });   // drag up / down to change the tempo, double-click resets
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (10.5f));
        const int range = router.getPitchRangePercent (deck);
        const auto rangeText = range >= 100 ? juce::String ("WIDE") : "\xc2\xb1" + juce::String (range) + "%";
        g.drawText (rangeText, tempo, juce::Justification::centredRight);
        const float rw = psyched::Fonts::label().getStringWidthFloat (rangeText) + 12.0f;
        regions.push_back ({ tempo.withLeft (tempo.getRight() - rw), "d" + juce::String (deck + 1) + ".pitchrange", 0 });   // click: 6 / 10 / 25 / 50 % / wide

        right.removeFromRight (8.0f);
        const auto p = juce::String ("d") + juce::String (deck + 1) + ".";
        auto addChip = [&] (const juce::String& label, bool lit, juce::Colour c, const juce::String& action, float w)
        {
            auto cr = right.removeFromLeft (w).reduced (2.0f, 3.0f);
            chip (g, cr, label, lit, c);
            regions.push_back ({ cr, action, 0 });
        };
        addChip ("MASTER", st.master.load(), Theme::accent, p + "master", 52.0f);
        addChip ("SYNC", st.sync.load(), col, p + "sync", 40.0f);
        addChip ("MT", st.keyLock.load(), Theme::warn, p + "keylock", 28.0f);
        addChip ("QUANTIZE", st.quantize.load(), Theme::bad.withSaturation (0.6f), p + "quantize", 62.0f);
        addChip (st.vinylMode.load() ? "VINYL" : "CDJ", st.vinylMode.load(), Theme::warn, p + "jog.vinyl", 50.0f);
        addChip (st.slipping.load() ? "SLIPPING" : "SLIP", st.slip.load(), Theme::sync.brighter (0.2f), p + "slip", st.slipping.load() ? 64.0f : 40.0f);
        {
            // vinyl speed: brake (stop) / start, click for the choices
            const bool b = st.brakeOn.load(), s2 = st.startOn.load();
            const juce::String t = b && s2 ? "BRAKE+START" : b ? "BRAKE" : s2 ? "START" : "INSTANT";
            auto cr = right.removeFromLeft (t.length() > 8 ? 82.0f : 58.0f).reduced (2.0f, 3.0f);
            chip (g, cr, t, b || s2, Theme::warn.withRotatedHue (0.08f));
            regions.push_back ({ cr, "vinylspeed", 10 });
        }
    }

    // --- Overview --------------------------------------------------------------
    {
        auto r = rowOverview;
        // grid toggle on the left; stem separation progress in a strip underneath (against the track and playhead)
        auto toggle = r.removeFromLeft (20.0f);
        r.removeFromLeft (4.0f);
        paintGridToggle (g, toggle, overviewGrid, Theme::sync);
        regions.push_back ({ toggle, "overviewgrid", 7 });
        auto progress = r.removeFromBottom (4.0f);
        r.removeFromBottom (1.0f);
        if (loaded && st.stemsReady.load() && st.stemsProgress.load() < 0.999f)
        {
            updateFold();
            paintStemProgressStrip (g, progress, st.stemsProgress.load(), st.lengthSec.load(), &fold, col);
            g.setColour (col);
            g.setFont (psyched::Fonts::label (9.5f));
        }
        g.setColour (Theme::waveBg);
        g.fillRect (r);
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
                auto wd = wdStyle == WaveformData::Style::blue ? track->waveformForSingleColour() : track->waveformInStyle (wdStyle);
                StripOptions o;
                o.overview = true;
                o.shape = overviewHalf ? StripOptions::halfUp : StripOptions::full;
                o.tint = overviewStyle == 3 ? singleColour : juce::Colour();
                o.fold = &fold;
                o.entriesPerSecond = wd->entriesPerSecond;
                const double entries = fold.isEmpty() ? (double) wd->size() : fold.displayLength (wd->lengthSeconds()) * wd->entriesPerSecond;
                overview = renderWaveformStrip (*wd, 0.0, entries / juce::jmax (1, pw), pw,
                                                (int) std::lround (r.getHeight() * scale), o);
            }
            g.drawImage (overview, r);

            const double len = st.lengthSec.load();
            if (len > 0.0)
            {
                const double dlen = juce::jmax (0.001, fold.displayLength (len));
                fold.reference = fold.toDisplay (st.positionSec.load(), st.playSegment.load());
                auto xOf = [&] (double sec) { return r.getX() + (float) juce::jlimit (0.0, 1.0, fold.toDisplay (sec) / dlen) * r.getWidth(); };
                const float px = r.getX() + (float) juce::jlimit (0.0, 1.0, fold.reference / dlen) * r.getWidth();

                g.setColour (juce::Colours::black.withAlpha (0.28f));
                g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), px - r.getX(), r.getHeight()));

                if (st.loopInSec.load() >= 0 && st.loopOutSec.load() > st.loopInSec.load())
                {
                    g.setColour (Theme::accent.withAlpha (st.loopActive.load() ? 0.45f : 0.2f));
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (st.loopInSec.load()), r.getY(),
                                                                            juce::jmax (xOf (st.loopInSec.load()) + 2.0f, xOf (st.loopOutSec.load())), r.getBottom()));
                }

                // Skipped sections shaded; then every mark (shared with PREP and the browser preview)
                if (fold.isEmpty())
                    for (int i = 0; i < juce::jmin (st.numSkips.load(), Deck::maxSkips); ++i)
                    {
                        const float x0 = xOf (st.skipFrom[(size_t) i].load()), x1 = xOf (st.skipTo[(size_t) i].load());
                        if (x1 <= x0) continue;
                        g.setColour (juce::Colours::black.withAlpha (st.skipsOn.load() ? 0.62f : 0.35f));
                        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, r.getY(), x1, r.getBottom()));
                    }
                if (overviewGrid) paintOverviewGrid (g, r, track->grid, len, &fold, track->phrases.empty() ? 0.0f : juce::jmax (2.0f, juce::jmin (6.0f, r.getHeight() * 0.14f)));
                if (viewWindow)
                {
                    // the part the scrolling waveform shows
                    const auto [vis, ph] = viewWindow();
                    auto xOfD = [&] (double d) { return r.getX() + (float) (d / dlen) * r.getWidth(); };
                    paintOverviewViewBox (g, r, xOfD (fold.reference - vis * ph), xOfD (fold.reference + vis * (1.0 - ph)), col, ! track->phrases.empty());
                }
                paintOverviewMarks (g, r, TrackMarks::fromDeck (st, track->phrases, track.get()), false, &fold);
                if (st.stemsReady.load() && st.stemsProgress.load() < 0.999f)
                {
                    auto lab = juce::Rectangle<float> (r.getRight() - 112.0f, r.getBottom() - 13.0f, 112.0f, 13.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.75f));
                    g.fillRect (lab);
                    g.setColour (col);
                    g.setFont (psyched::Fonts::label (9.0f));
                    g.drawText ("SEPARATING " + juce::String (juce::roundToInt (st.stemsProgress.load() * 100.0f)) + "%", lab, juce::Justification::centred);
                }

                g.setColour (Theme::accent);
                g.fillRect (xOf (st.cuePointSec.load()) - 0.5f, r.getY(), 1.0f, r.getHeight());
                g.setColour (juce::Colours::white);
                g.fillRect (px - 1.0f, r.getY() - 2.0f, 2.0f, r.getHeight() + 4.0f);
            }
        }
    }

    // --- Beat grid editor (replaces the pads while GRID is on) -------------------
    if (gridMode)
    {
        auto r = rowPads;
        const auto p = juce::String ("d") + juce::String (deck + 1) + ".grid.";
        struct B { const char* label; const char* action; float w; };
        const B buttons[] = { { "<10ms", "shift.back10", 1.0f }, { "<1ms", "shift.back1", 1.0f }, { "1ms>", "shift.fwd1", 1.0f },
                              { "10ms>", "shift.fwd10", 1.0f }, { "ANCHOR", "anchor", 1.2f }, { "DOWNBEAT", "downbeat", 1.4f },
                              { "-0.1", "bpm.down", 0.9f }, { "-.01", "bpm.downfine", 0.9f }, { "BPM", "", 1.6f },
                              { "+.01", "bpm.upfine", 0.9f }, { "+0.1", "bpm.up", 0.9f }, { "/2", "halve", 0.7f },
                              { "x2", "double", 0.7f }, { "TAP", "tap", 0.9f }, { "MORE", "more", 1.0f }, { "UNDO", "undo", 1.0f }, { "RESET", "reset", 1.0f } };
        float total = 0.0f;
        for (auto& b : buttons) total += b.w;
        const float gap = 3.0f, unit = (r.getWidth() - gap * 16.0f) / total;
        float x = r.getX();
        for (auto& b : buttons)
        {
            auto br = juce::Rectangle<float> (x, r.getY(), unit * b.w, r.getHeight());
            x += unit * b.w + gap;
            if (juce::String (b.action).isEmpty())
            {
                const double bpm = editor != nullptr ? editor->currentBpm (deck) : 0.0;
                chip (g, br, bpm > 0 ? juce::String (bpm, 2) : juce::String ("BPM"), true, Theme::warn.withAlpha (0.8f));
                regions.push_back ({ br, "setbpm", 4 });
                continue;
            }
            chip (g, br, b.label, false, Theme::warn);
            regions.push_back ({ br, p + b.action, 0 });
        }
    }
    else
    // --- Hot cue pads ------------------------------------------------------------
    {
        auto r = rowPads;
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
            g.setColour (set ? juce::Colours::black : Theme::textFaint);
            g.setFont (Theme::font (12.0f, true));
            auto label = juce::String::charToString ((juce::juce_wchar) ('A' + i));
            if (set && st.hotEnd[(size_t) i].load() > st.hotStart[(size_t) i].load())
                drawLoopIcon (g, pad.removeFromLeft (pad.getHeight()).reduced (6.0f), juce::Colours::black, 1.4f);   // hot loop
            if (set && editor != nullptr)
                if (auto name = editor->cueName (deck, true, i); name.isNotEmpty()) label << "  " << name;
            g.drawText (label, pad.reduced (4.0f, 0.0f), juce::Justification::centred, true);
            regions.push_back ({ pad, "d" + juce::String (deck + 1) + ".hotcue." + juce::String (i + 1), 3 });
        }
    }

    // --- Beat jump: 1 beat, 1 bar, 2 bars, 4 bars, back and forward ----------------
    {
        auto r = rowJump;
        const auto p = juce::String ("d") + juce::String (deck + 1) + ".beatjump.";
        psyched::paintLabel (g, "Jump", r.removeFromLeft (36.0f), juce::Justification::centredLeft);
        const std::pair<int, const char*> sizes[] = { { 1, "1 BEAT" }, { 4, "1 BAR" }, { 8, "2 BARS" }, { 16, "4 BARS" } };
        // each size sits tight between its buttons; the rest of the row shows notes
        const float gw = juce::jlimit (78.0f, 104.0f, (r.getWidth() * 0.55f - 3 * 8.0f) / 4.0f);
        {
            // previous / next marker (hot cues and locators), when stopped
            auto rest = r.withTrimmedLeft (4 * gw + 3 * 8.0f + 10.0f);
            auto prev = rest.removeFromLeft (22.0f);
            rest.removeFromLeft (2.0f);
            auto next = rest.removeFromLeft (22.0f);
            rest.removeFromLeft (8.0f);
            const bool canCall = loaded && ! st.playing.load();
            chip (g, prev, "|<", false, canCall ? Theme::bad : Theme::textFaint);
            chip (g, next, ">|", false, canCall ? Theme::bad : Theme::textFaint);
            const auto pp = juce::String ("d") + juce::String (deck + 1) + ".";
            regions.push_back ({ prev, pp + "callprev", 0 });
            regions.push_back ({ next, pp + "callnext", 0 });
            paintNotesBox (g, rest);
        }
        for (int k = 0; k < 4; ++k)
        {
            auto grp = r.removeFromLeft (gw);
            if (k < 3) r.removeFromLeft (8.0f);
            auto back = grp.removeFromLeft (22.0f);
            auto fwd = grp.removeFromRight (22.0f);
            chip (g, back, "<", false, Theme::sync);
            chip (g, fwd, ">", false, Theme::sync);
            psyched::paintLabel (g, sizes[k].second, grp, juce::Justification::centred, Theme::textDim, 9.5f);
            regions.push_back ({ back, p + "back." + juce::String (sizes[k].first), 0 });
            regions.push_back ({ fwd, p + "fwd." + juce::String (sizes[k].first), 0 });
        }
    }

    // --- Transport + stems -------------------------------------------------------
    {
        auto r = rowControls;
        const auto p = juce::String ("d") + juce::String (deck + 1) + ".";
        auto vfit = [] (juce::Rectangle<float> b) { return b.withSizeKeepingCentre (b.getWidth(), juce::jmin (b.getHeight(), 24.0f)); };

        auto cue = r.removeFromLeft (58.0f).reduced (0.0f, 2.0f);
        const bool atCue = loaded && ! st.playing.load();
        g.setColour (atCue ? Theme::accent.withAlpha (0.18f) : Theme::panelHi);
        g.fillEllipse (cue.withSizeKeepingCentre (cue.getHeight(), cue.getHeight()));
        g.setColour (Theme::accent);
        g.drawEllipse (cue.withSizeKeepingCentre (cue.getHeight(), cue.getHeight()).reduced (1.0f), 2.0f);
        g.setFont (Theme::font (12.0f, true));
        g.drawText ("CUE", cue, juce::Justification::centred);
        regions.push_back ({ cue, p + "cue", 0 });

        r.removeFromLeft (6.0f);
        auto play = r.removeFromLeft (58.0f).reduced (0.0f, 2.0f);
        const bool playing = st.playing.load();
        auto pc = play.withSizeKeepingCentre (play.getHeight(), play.getHeight());
        g.setColour (playing ? Theme::good.withAlpha (0.2f) : Theme::panelHi);
        g.fillEllipse (pc);
        g.setColour (Theme::good);
        g.drawEllipse (pc.reduced (1.0f), 2.0f);
        juce::Path icon;
        const auto c = pc.getCentre();
        if (playing)
        {
            icon.addRectangle (c.x - 6.0f, c.y - 7.0f, 4.0f, 14.0f);
            icon.addRectangle (c.x + 2.0f, c.y - 7.0f, 4.0f, 14.0f);
        }
        else
            icon.addTriangle (c.x - 5.0f, c.y - 8.0f, c.x - 5.0f, c.y + 8.0f, c.x + 8.0f, c.y);
        g.fillPath (icon);
        regions.push_back ({ play, p + "play", 0 });

        // Loop: [<] halves the size (and a running loop), [size] sets / removes the loop, [>] doubles
        r.removeFromLeft (10.0f);
        {
            const double lb = router.getLoopBeats (deck);
            const auto sizeText = lb >= 1.0 ? juce::String ((int) std::lround (lb)) : "1/" + juce::String ((int) std::lround (1.0 / lb));
            auto lessBtn = vfit (r.removeFromLeft (22.0f));
            auto sizeBtn = vfit (r.removeFromLeft (44.0f)).reduced (1.0f, 0.0f);
            auto moreBtn = vfit (r.removeFromLeft (22.0f));
            chip (g, lessBtn, "<", false, Theme::accent);
            chip (g, sizeBtn, sizeText, st.loopActive.load(), Theme::accent);
            chip (g, moreBtn, ">", false, Theme::accent);
            regions.push_back ({ lessBtn, p + "loopsize.halve", 0 });
            regions.push_back ({ sizeBtn, p + "loop.toggle", 0 });
            regions.push_back ({ moreBtn, p + "loopsize.double", 0 });
            // manual loop: IN, OUT, RELOOP / EXIT
            r.removeFromLeft (4.0f);
            auto inBtn = vfit (r.removeFromLeft (26.0f));
            auto outBtn = vfit (r.removeFromLeft (32.0f)).reduced (1.0f, 0.0f);
            auto reBtn = vfit (r.removeFromLeft (26.0f));
            const bool hasLoop = st.loopInSec.load() >= 0.0 && st.loopOutSec.load() > st.loopInSec.load();
            chip (g, inBtn, "IN", st.loopInSec.load() >= 0.0 && ! hasLoop, Theme::accent);
            chip (g, outBtn, "OUT", st.loopActive.load(), Theme::accent);
            chip (g, reBtn, "RE", hasLoop && st.loopActive.load(), Theme::accent);
            regions.push_back ({ inBtn, p + "loop.in", 0 });
            regions.push_back ({ outBtn, p + "loop.out", 0 });
            regions.push_back ({ reBtn, p + "loop.reloop", 0 });
        }

        // Locator (memory) and skip: SKIP once to start, again to end
        r.removeFromLeft (6.0f);
        auto memBtn = vfit (r.removeFromLeft (66.0f));
        chip (g, memBtn, "MARKER", false, Theme::bad);
        regions.push_back ({ memBtn, p + "locator.add", 0 });
        r.removeFromLeft (6.0f);
        auto skipBtn = vfit (r.removeFromLeft (56.0f));
        const bool pending = st.skipPendingFrom.load() >= 0.0;
        chip (g, skipBtn, pending ? "SKIP TO" : "SKIP", pending, juce::Colour (0xff38d9ff));
        regions.push_back ({ skipBtn, p + "skip.set", 0 });

        // Stems (rotor shows separation progress); the mode chip switches all four between level and filter
        r.removeFromLeft (8.0f);
        rotorArea = r.removeFromLeft (r.getHeight()).reduced (1.0f);
        r.removeFromLeft (6.0f);
        const bool ready = st.stemsReady.load();
        const float prog = st.stemsProgress.load();
        {
            // VOL / FILTER for all four (MIX when they differ - pressing it sets them all to VOL)
            int m0 = st.stemMode[0].load();
            bool same = true;
            for (size_t k = 1; k < 4; ++k) same = same && st.stemMode[k].load() == m0;
            auto mc = vfit (r.removeFromLeft (46.0f));
            chip (g, mc, ! same ? "MIX" : m0 == 1 ? "FILTER" : "VOL", ! same || m0 != 0, Theme::sync);
            regions.push_back ({ mc, "stemmode", 8 });
            r.removeFromLeft (6.0f);
        }
        juce::ignoreUnused (prog);
        const float sw = (r.getWidth() - 3 * 6.0f) / 4.0f;
        for (int s = 0; s < 4; ++s)
        {
            auto b = juce::Rectangle<float> (r.getX() + s * (sw + 6.0f), r.getY() + 1.0f, sw, r.getHeight() - 2.0f);
            juce::Rectangle<float> bar, modeBtn;
            paintStemControl (g, b, s, st.stemGain[(size_t) s].load(), st.stemMode[(size_t) s].load(), ready, bar, modeBtn);
            regions.push_back ({ bar, p + "stem." + juce::String (s + 1) + ".mute", 2 });
            regions.push_back ({ modeBtn, "stemmode." + juce::String (s), 9 });
        }
    }
}

void DeckPanel::showPadMenu (int pad, juce::Rectangle<float> area)
{
    const auto& st = engine.deck (deck).state();
    if (pad < 0 || pad >= 8 || st.hotStart[(size_t) pad].load() < 0.0) return;
    const auto current = juce::Colour (st.hotColour[(size_t) pad].load());
    juce::PopupMenu menu;
    menu.addSectionHeader ("Hot cue " + juce::String::charToString ((juce::juce_wchar) ('A' + pad)));
    addCueColourItems (menu, 100, current);
    menu.addSeparator();
    menu.addItem (2, "Rename...");
    menu.addItem (1, "Delete   (shift + click)");
    const auto screen = localAreaToGlobal (area.toNearestInt());
    juce::Component::SafePointer<DeckPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (screen),
                        [safe, pad, screen, current] (int result)
    {
        if (safe == nullptr) return;
        auto& d = safe->engine.deck (safe->deck);
        auto setColour = [safe, pad] (juce::Colour c) { if (safe != nullptr) safe->engine.deck (safe->deck).post (Deck::Cmd::hotCueColour, (double) c.getARGB(), pad); };
        if (result == 1) d.post (Deck::Cmd::hotCueDelete, 0.0, pad, true);
        else if (result == 2 && safe->onRenameCue)
            safe->onRenameCue (safe->deck, true, pad, safe->editor != nullptr ? safe->editor->cueName (safe->deck, true, pad) : juce::String());
        else if (handleCueColourMenu (result, 100, screen, current, setColour)) {}
    });
}

void DeckPanel::showDeckColourMenu (juce::Rectangle<float> area)
{
    const auto current = Theme::deckColour (deck);
    juce::PopupMenu menu;
    menu.addSectionHeader ("Deck " + juce::String (deck + 1) + " colour");
    menu.addItem (200, "Default (" + juce::String (deck == 0 ? "cyan" : "pink") + ")");
    menu.addSeparator();
    addCueColourItems (menu, 100, current);
    const auto screen = localAreaToGlobal (area.toNearestInt());
    juce::Component::SafePointer<DeckPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (screen),
                        [safe, screen, current] (int result)
    {
        if (safe == nullptr || safe->onDeckColour == nullptr) return;
        auto set = [safe] (juce::Colour c) { if (safe != nullptr && safe->onDeckColour) safe->onDeckColour (safe->deck, c); };
        if (result == 200) set (safe->deck == 0 ? psyched::Colours::cyan : psyched::Colours::pink);
        else if (handleCueColourMenu (result, 100, screen, current, set)) {}
    });
}

const DeckPanel::Region* DeckPanel::regionAt (juce::Point<float> p) const
{
    for (auto& r : regions)
        if (r.r.contains (p))
            return &r;
    return nullptr;
}

void DeckPanel::press (const juce::String& id, bool down)
{
    ActionValue v;
    v.down = down;
    router.perform (id, v);
}

void DeckPanel::updateFold()
{
    const auto& st = engine.deck (deck).state();
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

const NoteLocator* DeckPanel::noteShowing (double pos) const
{
    // A note shows from a little before playback reaches it until a set time after (or until the next note).
    if (track == nullptr) return nullptr;
    const NoteLocator* found = nullptr;
    for (auto& n : track->notes)
    {
        // each note's own timing; "0" = stays until the next note
        const bool beforeNext = &n == &track->notes.back() || pos < (&n + 1)->timeSec - (&n + 1)->leadSec;
        if (pos >= n.timeSec - n.leadSec && (n.showSec <= 0.0 ? beforeNext : pos <= n.timeSec + n.showSec)) found = &n;
    }
    return found;
}

void DeckPanel::paintNotesBox (juce::Graphics& g, juce::Rectangle<float> r)
{
    // The notes box: shows only note locators, as playback comes up to them
    if (r.getWidth() < 60.0f) return;
    const auto& st = engine.deck (deck).state();
    auto* n = noteShowing (st.positionSec.load());
    g.setColour (n != nullptr ? psyched::Colours::void_ : Theme::panel);
    g.fillRect (r);
    g.setColour (n != nullptr ? n->colour : Theme::line);
    g.drawRect (r, 1.0f);
    if (n == nullptr)
    {
        psyched::paintLabel (g, "NOTES", r.reduced (8.0f, 0.0f), juce::Justification::centredLeft, Theme::textFaint, 9.0f);
        return;
    }
    g.fillRect (r.removeFromLeft (4.0f));
    r.removeFromLeft (6.0f);
    const double until = n->timeSec - st.positionSec.load();
    g.setFont (psyched::Fonts::mono (9.5f, true));
    g.drawText (until > 0.05 ? "IN " + juce::String (until, 1) + "s" : juce::String ("NOTE"), r.removeFromLeft (52.0f), juce::Justification::centredLeft);
    g.setColour (Theme::text);
    g.setFont (Theme::font (13.0f, true));
    g.drawText (n->text, r.reduced (2.0f, 0.0f), juce::Justification::centredLeft, true);
}

void DeckPanel::setStemMode (int stem, int mode)
{
    engine.deck (deck).post (Deck::Cmd::stemMode, (double) mode, stem);
    if (onStemModeChanged) onStemModeChanged (deck, stem, mode);
}

void DeckPanel::showStemModeMenu (int stem, juce::Rectangle<float> area)
{
    const auto& st = engine.deck (deck).state();
    const int cur = stem >= 0 ? st.stemMode[(size_t) stem].load() : -1;
    juce::PopupMenu m;
    const char* labels[] = { "VOL - left of centre turns it down", "FILTER - left of centre low-pass, right high-pass" };
    if (stem >= 0)
    {
        m.addSectionHeader (juce::String (stemName (stem)) + " control");
        for (int k = 0; k < 2; ++k) m.addItem (1 + k, labels[k], true, cur == k);
        m.addSeparator();
    }
    m.addSectionHeader ("All four stems on this deck");
    for (int k = 0; k < 2; ++k) m.addItem (11 + k, labels[k]);
    juce::Component::SafePointer<DeckPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (area.toNearestInt())),
                     [safe, stem] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 11) safe->setStemMode (-1, r - 11);
        else safe->setStemMode (stem, r - 1);
    });
}

void DeckPanel::showEditVersionMenu (juce::Rectangle<float> area)
{
    if (editor == nullptr || ! editor->hasTrack (deck)) return;
    const auto names = editor->editVersionNames (deck);
    const auto active = editor->activeEdit (deck);
    juce::PopupMenu m;
    m.addSectionHeader ("Edit version - what plays");
    for (int i = 0; i < (int) names.size(); ++i)
        m.addItem (10 + i, names[(size_t) i], true, names[(size_t) i] == active);
    if (names.empty()) m.addItem (-1, "(none saved yet - make them on PREP)", false);
    m.addSeparator();
    m.addItem (1, "Original (the track as recorded)", true, active.isEmpty() && ! editor->hasEdits (deck));
    juce::Component::SafePointer<DeckPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (area.toNearestInt())),
                     [safe] (int r)
    {
        if (safe == nullptr || safe->editor == nullptr || r == 0) return;
        if (r == 1) safe->editor->applyNoEdits (safe->deck);
        else if (r >= 10) safe->editor->applyEditVersion (safe->deck, r - 10);
    });
}

void DeckPanel::mouseDown (const juce::MouseEvent& e)
{
    auto* r = regionAt (e.position);
    if (r == nullptr) return;
    if (r->kind == 2 && e.mods.isPopupMenu()) return;   // (no right-click menu: the VOL / FILTER buttons do it)
    if (r->kind == 8)
    {
        if (e.mods.isPopupMenu()) return;
        // all four: VOL <> FILTER (from MIX: VOL)
        const auto& st = engine.deck (deck).state();
        bool same = true;
        for (size_t k = 1; k < 4; ++k) same = same && st.stemMode[k].load() == st.stemMode[0].load();
        setStemMode (-1, ! same ? 0 : 1 - st.stemMode[0].load());
        return;
    }
    if (r->kind == 9)
    {
        const int s = r->action.getTrailingIntValue();
        if (e.mods.isPopupMenu()) return;
        setStemMode (s, 1 - engine.deck (deck).state().stemMode[(size_t) s].load());
        return;
    }
    if (r->kind == 2 && e.getNumberOfClicks() >= 2)
    {
        // double-click a stem: back to the centre (untouched); the first click's mute is undone
        const auto stemId = "d" + juce::String (deck + 1) + ".stem." + juce::String (r->action.fromFirstOccurrenceOf ("stem.", false, false).getIntValue());
        press (stemId + ".mute", true);
        ActionValue av;
        av.value = 0.5;
        router.perform ("d" + juce::String (deck + 1) + ".stem." + juce::String (r->action.fromFirstOccurrenceOf ("stem.", false, false).getIntValue()) + ".volume", av);
        return;
    }
    if (r->kind == 7)
    {
        if (onOverviewGridToggled) onOverviewGridToggled();
        return;
    }
    if (r->action.endsWith (".grid.more"))
    {
        auto* ed = editor;
        const int dk = deck;
        showGridMoreMenu (localAreaToGlobal (r->r.toNearestInt()), [ed, dk] (const juce::String& a) { if (ed != nullptr) ed->gridAction (dk, a); });
        return;
    }
    if (r->kind == 10)
    {
        const auto& st = engine.deck (deck).state();
        const bool b = st.brakeOn.load(), s2 = st.startOn.load();
        juce::PopupMenu m;
        m.addSectionHeader ("Vinyl speed - deck " + juce::String (deck + 1));
        m.addItem (1, "Instant start, braked stop", true, ! s2 && b);
        m.addItem (2, "Braked start, instant stop", true, s2 && ! b);
        m.addItem (3, "Both braked", true, s2 && b);
        m.addItem (4, "Both instant", true, ! s2 && ! b);
        m.addSeparator();
        m.addItem (5, "Times... (Settings > VINYL)");
        const auto area = r->r;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (area.toNearestInt())), [this] (int res)
        {
            if (res == 5) { if (onOpenSettings) onOpenSettings(); return; }
            if (res < 1 || res > 4) return;
            auto& dk = engine.deck (deck);
            dk.post (Deck::Cmd::vinylBrakeSet, (res == 1 || res == 3) ? 1.0 : 0.0, 0);
            dk.post (Deck::Cmd::vinylBrakeSet, (res == 2 || res == 3) ? 1.0 : 0.0, 1);
        });
        return;
    }
    if (e.mods.isPopupMenu() && r->kind != 3) return;

    if (r->kind == 1)
    {
        const double f = juce::jlimit (0.0, 1.0, (double) ((e.position.x - r->r.getX()) / r->r.getWidth()));
        if (fold.isEmpty()) engine.deck (deck).post (Deck::Cmd::seekFraction, f);
        else
        {
            const auto& st = engine.deck (deck).state();
            engine.deck (deck).post (Deck::Cmd::seekSeconds, fold.toSource (f * fold.displayLength (st.lengthSec.load())));
        }
        return;
    }
    if (r->action.endsWith (".editview"))
    {
        if (onEditViewToggled) onEditViewToggled (deck);
        return;
    }
    if (r->action.endsWith (".editver"))
    {
        showEditVersionMenu (r->r);
        return;
    }
    if (r->kind == 2)
    {
        dragStem = r->action.fromFirstOccurrenceOf ("stem.", false, false).getIntValue() - 1;
        return;
    }
    if (r->kind == 4)
    {
        if (onSetBpm) onSetBpm (deck);
        return;
    }
    if (r->kind == 5)
    {
        showDeckColourMenu (r->r);
        return;
    }
    if (r->kind == 6)
    {
        if (e.getNumberOfClicks() >= 2)
        {
            engine.deck (deck).post (Deck::Cmd::tempoReset);
            tempoDrag = false;
            return;
        }
        const auto& st = engine.deck (deck).state();
        tempoDrag = true;
        tempoDragStart = st.tempoRatio.load();
        return;
    }
    if (r->action.endsWith ("grid.mode"))
    {
        toggleGridMode();
        return;
    }
    if (r->kind == 3 && e.mods.isPopupMenu())
    {
        showPadMenu (r->action.fromLastOccurrenceOf (".", false, false).getIntValue() - 1, r->r);
        return;
    }

    auto action = r->action;
    if (e.mods.isShiftDown() && action.contains (".hotcue."))
        action += ".delete";
    heldAction = action;
    press (action, true);
}

void DeckPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (tempoDrag)
    {
        // Up = faster, down = slower: 0.01 % per pixel (shift for 0.002 %), within the pitch range
        const auto& st = engine.deck (deck).state();
        const double perPx = e.mods.isShiftDown() ? 0.00002 : 0.0001;
        const double ratio = tempoDragStart - e.getDistanceFromDragStartY() * perPx;
        const double range = juce::jmax (0.01, st.pitchRange.load());
        engine.deck (deck).post (Deck::Cmd::setPitchFader, juce::jlimit (-1.0, 1.0, (ratio - 1.0) / range));
        return;
    }
    if (dragStem >= 0 && e.getDistanceFromDragStart() > 3)
    {
        for (auto& r : regions)
            if (r.kind == 2 && r.action.contains ("stem." + juce::String (dragStem + 1) + "."))
            {
                const double v = juce::jlimit (0.0, 1.0, (double) ((e.position.x - r.r.getX()) / r.r.getWidth()));
            {
                ActionValue av;
                av.value = v;
                router.perform ("d" + juce::String (deck + 1) + ".stem." + juce::String (dragStem + 1) + ".volume", av);
            }
            }
        return;
    }
    if (regionAt (e.getMouseDownPosition().toFloat()) != nullptr && regionAt (e.getMouseDownPosition().toFloat())->kind == 1)
        if (auto* r = regionAt (e.getMouseDownPosition().toFloat()))
            engine.deck (deck).post (Deck::Cmd::seekFraction, juce::jlimit (0.0f, 1.0f, (e.position.x - r->r.getX()) / r->r.getWidth()));
}

void DeckPanel::mouseUp (const juce::MouseEvent& e)
{
    tempoDrag = false;
    if (dragStem >= 0)
    {
        if (e.getDistanceFromDragStart() <= 3)
            press ("d" + juce::String (deck + 1) + ".stem." + juce::String (dragStem + 1) + ".mute", true);
        dragStem = -1;
        return;
    }
    if (heldAction.isNotEmpty())
    {
        press (heldAction, false);
        heldAction = {};
    }
}

} // namespace orbit
