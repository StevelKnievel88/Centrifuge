#include "DeckPanel.h"
#include "Theme.h"
#include "../Control/Actions.h"

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
    rowOverview = r.removeFromTop (juce::jlimit (30.0f, 56.0f, r.getHeight() * 0.36f));
    r.removeFromTop (8.0f);
    rowPads = r.removeFromTop (26.0f);
    r.removeFromTop (8.0f);
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
    mix (gridMode);
    rotor.setBounds (rotorArea.toNearestInt());
    rotor.setSpinning (st.stemsReady.load() && st.stemsProgress.load() < 0.999f);
    rotor.setProgress (st.stemsReady.load() ? st.stemsProgress.load() : 0.0f);
    mix (router.getPitchRangePercent (deck));
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
            auto keyChip = r.removeFromRight (shift != 0 ? 66.0f : 44.0f).withSizeKeepingCentre (shift != 0 ? 64.0f : 42.0f, 22.0f);
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
            auto keyText = router.effectiveKeyText (deck);
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
        const double pos = st.positionSec.load(), len = st.lengthSec.load();
        g.setFont (Theme::mono (17.0f, true));
        g.setColour (Theme::text);
        g.drawText (loaded ? formatTime (pos) : "0:00.0", r.removeFromLeft (78.0f), juce::Justification::centredLeft);
        const bool warn = st.endWarning.load() && (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
        g.setColour (warn ? Theme::bad : Theme::textDim);
        g.drawText (loaded ? formatTime (-(len - pos)) : "-0:00.0", r.removeFromLeft (86.0f), juce::Justification::centredLeft);

        // Beat counter (position in the bar)
        r.removeFromLeft (6.0f);
        int beatInBar = 0;
        if (loaded && track->grid.isValid() && pos >= track->grid.getBeats().front().timeSec)
            beatInBar = track->grid.beatInBarAt (pos);
        for (int i = 1; i <= 4; ++i)
        {
            auto b = r.removeFromLeft (14.0f).withSizeKeepingCentre (10.0f, 10.0f);
            g.setColour (i == beatInBar ? (i == 1 ? Theme::bad : col) : Theme::line);
            g.fillRect (b);
        }

        // Edit targets: R = rekordbox copy, C = Centrifuge version (both lit = edit both); GRID opens the grid editor
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
        auto right = r.removeFromRight (300.0f);
        const double pct = (st.tempoRatio.load() - 1.0) * 100.0;
        auto tempo = right.removeFromRight (100.0f);
        g.setColour (Theme::text);
        g.setFont (Theme::font (14.0f, true));
        g.drawText ((pct >= 0 ? "+" : "") + juce::String (pct, 2) + "%", tempo.removeFromTop (16.0f), juce::Justification::centredRight);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (10.5f));
        const int range = router.getPitchRangePercent (deck);
        g.drawText (range >= 100 ? juce::String ("WIDE") : "\xc2\xb1" + juce::String (range) + "%", tempo, juce::Justification::centredRight);

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
    }

    // --- Overview --------------------------------------------------------------
    {
        auto r = rowOverview;
        g.setColour (Theme::waveBg);
        g.fillRect (r);
        regions.push_back ({ r, {}, 1 });

        if (loaded && track->waveform != nullptr)
        {
            const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
            const int pw = (int) std::lround (r.getWidth() * scale);
            if (overviewWidth != pw || overview.isNull())
            {
                overviewWidth = pw;
                const auto wdStyle = overviewStyle == 2 ? WaveformData::Style::rgb
                                   : overviewStyle == 3 ? WaveformData::Style::blue : WaveformData::Style::threeBand;
                auto wd = track->waveformInStyle (wdStyle);
                StripOptions o;
                o.overview = true;
                o.shape = overviewHalf ? StripOptions::halfUp : StripOptions::full;
                o.tint = overviewStyle == 3 ? singleColour : juce::Colour();
                overview = renderWaveformStrip (*wd, 0.0, (double) wd->size() / juce::jmax (1, pw), pw,
                                                (int) std::lround (r.getHeight() * scale), o);
            }
            g.drawImage (overview, r);

            const double len = st.lengthSec.load();
            if (len > 0.0)
            {
                auto xOf = [&] (double sec) { return r.getX() + (float) (sec / len) * r.getWidth(); };
                const float px = xOf (st.positionSec.load());

                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), px - r.getX(), r.getHeight()));

                if (st.loopInSec.load() >= 0 && st.loopOutSec.load() > st.loopInSec.load())
                {
                    g.setColour (Theme::accent.withAlpha (st.loopActive.load() ? 0.45f : 0.2f));
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (st.loopInSec.load()), r.getY(),
                                                                            juce::jmax (xOf (st.loopInSec.load()) + 2.0f, xOf (st.loopOutSec.load())), r.getBottom()));
                }

                // Skipped sections
                for (int i = 0; i < juce::jmin (st.numSkips.load(), Deck::maxSkips); ++i)
                {
                    const float x0 = xOf (st.skipFrom[(size_t) i].load()), x1 = xOf (st.skipTo[(size_t) i].load());
                    if (x1 <= x0) continue;
                    g.setColour (juce::Colours::black.withAlpha (st.skipsOn.load() ? 0.62f : 0.35f));
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, r.getY(), x1, r.getBottom()));
                    g.setColour (juce::Colour (0xff38d9ff).withAlpha (st.skipsOn.load() ? 0.9f : 0.4f));
                    g.fillRect (x0 - 0.5f, r.getY(), 1.0f, r.getHeight());
                    juce::Path p;
                    p.addTriangle (x0, r.getY(), x0, r.getY() + 6.0f, x0 + 5.0f, r.getY() + 3.0f);
                    p.addTriangle (x0 + 4.0f, r.getY(), x0 + 4.0f, r.getY() + 6.0f, x0 + 9.0f, r.getY() + 3.0f);
                    g.fillPath (p);
                }

                // Locators
                for (int i = 0; i < juce::jmin (st.numLocators.load(), Deck::maxLocators); ++i)
                {
                    g.setColour (juce::Colour (st.locColour[(size_t) i].load()));
                    const float x = xOf (st.locStart[(size_t) i].load());
                    juce::Path tri;
                    tri.addTriangle (x - 3.5f, r.getY(), x + 3.5f, r.getY(), x, r.getY() + 5.0f);
                    g.fillPath (tri);
                }
                for (size_t i = 0; i < 8; ++i)
                {
                    const double hs = st.hotStart[i].load();
                    if (hs < 0.0) continue;
                    g.setColour (juce::Colour (st.hotColour[i].load()));
                    g.fillRect (xOf (hs) - 0.5f, r.getBottom() - 6.0f, 2.0f, 6.0f);
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
            if (set && editor != nullptr)
                if (auto name = editor->cueName (deck, true, i); name.isNotEmpty()) label << "  " << name;
            g.drawText (label, pad.reduced (4.0f, 0.0f), juce::Justification::centred, true);
            regions.push_back ({ pad, "d" + juce::String (deck + 1) + ".hotcue." + juce::String (i + 1), 3 });
        }
    }

    // --- Transport + stems -------------------------------------------------------
    {
        auto r = rowControls;
        const auto p = juce::String ("d") + juce::String (deck + 1) + ".";

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

        r.removeFromLeft (10.0f);
        auto loopBtn = r.removeFromLeft (64.0f).reduced (0.0f, 6.0f);
        chip (g, loopBtn, "LOOP 4", st.loopActive.load(), Theme::accent);
        regions.push_back ({ loopBtn, p + "beatloop.4", 0 });

        // Locator (memory) and skip: SKIP once to start, again to end
        r.removeFromLeft (6.0f);
        auto memBtn = r.removeFromLeft (64.0f).reduced (0.0f, 6.0f);
        chip (g, memBtn, "MEMORY", false, Theme::bad);
        regions.push_back ({ memBtn, p + "locator.add", 0 });
        r.removeFromLeft (6.0f);
        auto skipBtn = r.removeFromLeft (56.0f).reduced (0.0f, 6.0f);
        const bool pending = st.skipPendingFrom.load() >= 0.0;
        chip (g, skipBtn, pending ? "SKIP TO" : "SKIP", pending, juce::Colour (0xff38d9ff));
        regions.push_back ({ skipBtn, p + "skip.set", 0 });

        // Stems (rotor shows separation progress)
        r.removeFromLeft (8.0f);
        rotorArea = r.removeFromLeft (r.getHeight()).reduced (1.0f);
        r.removeFromLeft (8.0f);
        const bool ready = st.stemsReady.load();
        const float prog = st.stemsProgress.load();
        const float sw = (r.getWidth() - 3 * 6.0f) / 4.0f;
        for (int s = 0; s < 4; ++s)
        {
            auto b = juce::Rectangle<float> (r.getX() + s * (sw + 6.0f), r.getY() + 2.0f, sw, r.getHeight() - 4.0f);
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
            g.setColour (ready ? (gain <= 0.0f ? Theme::textFaint : Theme::text) : Theme::textFaint);
            g.setFont (Theme::font (11.0f, true));
            g.drawText (juce::String (stemName (s)).toUpperCase(), b, juce::Justification::centred);
            regions.push_back ({ b, p + "stem." + juce::String (s + 1) + ".mute", 2 });
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
        else if (result >= 100 && result < 116) setColour (juce::Colour (rekordboxCueColours()[(size_t) (result - 100)].argb));
        else if (result == 116) ColourPickerBubble::show (screen, current, setColour);
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

void DeckPanel::mouseDown (const juce::MouseEvent& e)
{
    auto* r = regionAt (e.position);
    if (r == nullptr) return;
    if (e.mods.isPopupMenu() && r->kind != 3) return;

    if (r->kind == 1)
    {
        engine.deck (deck).post (Deck::Cmd::seekFraction, (e.position.x - r->r.getX()) / r->r.getWidth());
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
