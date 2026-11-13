#include "XzScreenView.h"
#include "TrackMarks.h"
#include "Theme.h"
#include "../Audio/MusicalKey.h"
#include "ButtonStyles.h"

namespace orbit
{

namespace
{
    juce::String formatTime (double sec)
    {
        const bool neg = sec < 0;
        sec = std::abs (sec);
        const int m = (int) (sec / 60.0);
        const double s = sec - m * 60.0;
        auto str = juce::String (m) + ":" + juce::String (s, 1).paddedLeft ('0', 4);
        return neg ? "-" + str : str;
    }

    constexpr int meterH = 20, infoH = 140;
}

XzScreenView::XzScreenView (AudioEngine& e, ActionRouter& r, BrowserView& b)
    : engine (e), router (r), browser (b), waves (e, { 0, 1 }), meter (e)
{
    setOpaque (true);
    addAndMakeVisible (waves);
    addAndMakeVisible (meter);
    meter.setInterceptsMouseClicks (false, false);
    setSize (800, 480);
    setVisible (true);
}

void XzScreenView::setOverviewStyle (int style, bool half, juce::Colour single, bool singleFollowsDeck)
{
    overviewStyle = style;
    overviewHalf = half;
    singleColour = single;
    followDeck = singleFollowsDeck;
    for (auto& o : overviews) o.style = -1;
}

void XzScreenView::seekOverview (int d, float x)
{
    const auto& r = overviewRects[(size_t) d];
    if (r.getWidth() <= 0.0f) return;
    auto& dk = engine.deck (d);
    if (dk.state().trackSerial.load() == 0) return;
    const double f = juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()));
    const auto& fold = waves.foldFor (d);
    if (fold.isEmpty()) dk.post (Deck::Cmd::seekFraction, f);
    else dk.post (Deck::Cmd::seekSeconds, fold.toSource (f * fold.displayLength (dk.state().lengthSec.load())));
}

void XzScreenView::sendToWaves (int type, juce::Point<float> p)
{
    // the screen's touches go to the waveforms as mouse presses / drags, so they work just like on the computer
    auto local = p - waves.getPosition().toFloat();
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const auto downLocal = downPos - waves.getPosition().toFloat();
    juce::MouseEvent ev (src, local, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &waves, &waves, now, downLocal, downTime, 1, type != 1 && downPos.getDistanceFrom (p) > 3.0f);
    if (type == 1) waves.mouseDown (ev);
    else if (type == 3) waves.mouseDrag (ev);
    else waves.mouseUp (ev.withNewPosition (local));
}

void XzScreenView::touch (int type, int x, int y)
{
    const juce::Point<float> p ((float) x, (float) y);
    if (type == 1)
    {
        downPos = p;
        downTime = juce::Time::getCurrentTime();
        drag = Drag::none;
        if (browserShown)
        {
            if (load1Rect.contains (p)) { browser.loadSelected (0); return; }
            if (load2Rect.contains (p)) { browser.loadSelected (1); return; }
            if (closeRect.contains (p)) { browserShown = false; return; }
            for (size_t i = 0; i < browserRows.size(); ++i)
                if (browserRows[i].contains (p)) { browser.selectRowFromOutside (browserRowIndex[i]); break; }
            drag = Drag::browserList;
            listDragY = p.y;
            return;
        }
        for (int d = 0; d < 2; ++d)
        {
            if (gridRects[(size_t) d].contains (p)) { if (onGridToggled) onGridToggled(); return; }
            if (qRects[(size_t) d].contains (p))
            {
                ActionValue av;
                av.down = true;
                av.value = 1.0;
                router.perform ("d" + juce::String (d + 1) + ".quantize", av);
                return;
            }
            if (overviewRects[(size_t) d].contains (p)) { drag = Drag::overview; dragDeck = d; seekOverview (d, p.x); return; }
        }
        if (waves.getBounds().toFloat().contains (p)) { drag = Drag::waves; sendToWaves (1, p); }
        return;
    }
    if (drag == Drag::overview && dragDeck >= 0) seekOverview (dragDeck, p.x);
    else if (drag == Drag::waves) sendToWaves (type, p);
    else if (drag == Drag::browserList && type == 3)
    {
        // drag the list up / down: the selection moves a row at a time
        const float rowH = 26.0f;
        const int steps = (int) ((listDragY - p.y) / rowH);
        if (steps != 0)
        {
            browser.selectRowFromOutside (juce::jlimit (0, juce::jmax (0, browser.getRowCount() - 1), browser.getSelectedRowIndex() + steps));
            listDragY -= (float) steps * rowH;
        }
    }
    if (type == 2) { drag = Drag::none; dragDeck = -1; }
}

void XzScreenView::resized()
{
    auto r = getLocalBounds();
    meter.setBounds (r.removeFromTop (meterH).reduced (6, 3));
    browserArea = r;
    infoArea = r.removeFromBottom (infoH);
    waves.setBounds (r);
}

juce::Image XzScreenView::render()
{
    waves.setVisible (! browserShown);
    waves.tick (juce::Time::getMillisecondCounterHiRes() / 1000.0);
    juce::Image img (juce::Image::RGB, getWidth(), getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g (img);
    paintEntireComponent (g, true);
    return img;
}

void XzScreenView::paint (juce::Graphics& g)
{
    g.fillAll (psyched::Colours::void_);
    if (browserShown)
    {
        paintBrowser (g, browserArea);
        return;
    }
    auto r = infoArea.toFloat();
    const float w = r.getWidth() / 2.0f;
    // each deck in a panel with its coloured top line and the slant, like on the computer
    paintInfo (g, 0, r.removeFromLeft (w).withTrimmedRight (1.0f));
    paintInfo (g, 1, r.withTrimmedLeft (1.0f));
}

void XzScreenView::paintBrowser (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    psyched::paintPanel (g, r, Theme::accent, 12.0f);
    r.removeFromTop (4.0f);
    auto head = r.removeFromTop (34.0f).reduced (10.0f, 4.0f);
    closeRect = head.removeFromRight (70.0f);
    head.removeFromRight (6.0f);
    load2Rect = head.removeFromRight (80.0f);
    head.removeFromRight (6.0f);
    load1Rect = head.removeFromRight (80.0f);
    psyched::paintChip (g, closeRect, "CLOSE", Theme::text, psyched::ChipState::off);
    psyched::paintChip (g, load2Rect, "LOAD 2", Theme::deckColour (1), psyched::ChipState::on);
    psyched::paintChip (g, load1Rect, "LOAD 1", Theme::deckColour (0), psyched::ChipState::on);
    g.setColour (Theme::text);
    g.setFont (Theme::font (16.0f, true));
    g.drawText (browser.getCurrentListName().isNotEmpty() ? browser.getCurrentListName() : juce::String ("Browser"), head, juce::Justification::centredLeft, true);

    r = r.reduced (8.0f, 2.0f);
    const float rowH = 26.0f;
    const int n = browser.getRowCount(), visible = juce::jmax (1, (int) (r.getHeight() / rowH));
    const int sel = browser.getSelectedRowIndex();
    const int first = juce::jlimit (0, juce::jmax (0, n - visible), (sel < 0 ? 0 : sel) - visible / 2);
    browserRows.clear();
    browserRowIndex.clear();
    for (int i = first; i < juce::jmin (n, first + visible); ++i)
    {
        auto row = r.removeFromTop (rowH);
        const auto* t = browser.trackAtRow (i);
        if (t == nullptr) continue;
        browserRows.push_back (row);
        browserRowIndex.push_back (i);
        if (i == sel)
        {
            g.setColour (Theme::accent.withAlpha (0.18f));
            g.fillRect (row);
            g.setColour (Theme::accent);
            g.fillRect (row.withWidth (3.0f));
        }
        auto c = row.reduced (8.0f, 0.0f);
        if (! t->colour.isTransparent()) { g.setColour (t->colour); g.fillRect (c.removeFromLeft (3.0f).reduced (0.0f, 5.0f)); c.removeFromLeft (6.0f); }
        auto time = c.removeFromRight (52.0f);
        auto key = c.removeFromRight (56.0f);
        auto bpm = c.removeFromRight (64.0f);
        auto title = c.removeFromLeft (c.getWidth() * 0.6f);
        g.setFont (Theme::font (15.0f, i == sel));
        g.setColour (Theme::text);
        g.drawText (t->title, title, juce::Justification::centredLeft, true);
        g.setColour (Theme::textDim);
        g.drawText (t->artist, c.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);
        g.setFont (Theme::mono (14.0f));
        g.setColour (Theme::text);
        g.drawText (t->bpm > 0 ? juce::String (t->bpm, 2) : juce::String(), bpm, juce::Justification::centredRight);
        const auto kc = MusicalKey::wheelColour (t->key);
        g.setColour (kc.isTransparent() ? Theme::good : kc);
        g.drawText (MusicalKey::display (t->key), key, juce::Justification::centredRight);
        g.setColour (Theme::textDim);
        const int sec = (int) t->durationSec;
        g.drawText (juce::String (sec / 60) + ":" + juce::String (sec % 60).paddedLeft ('0', 2), time, juce::Justification::centredRight);
        g.setColour (Theme::line.withAlpha (0.5f));
        g.fillRect (row.removeFromBottom (1.0f));
    }
}

void XzScreenView::paintInfo (juce::Graphics& g, int deck, juce::Rectangle<float> panel)
{
    auto& dk = engine.deck (deck);
    const auto& st = dk.state();
    auto track = st.trackSerial.load() != 0 ? dk.getLoadedTrack() : nullptr;
    const bool loaded = track != nullptr;
    const auto col = Theme::deckColour (deck);

    psyched::paintPanel (g, panel, col, 10.0f);
    auto r = panel.reduced (8.0f, 0.0f).withTrimmedTop (5.0f).withTrimmedBottom (4.0f);

    // Title row: deck badge, title, BPM
    auto top = r.removeFromTop (22.0f);
    auto badge = top.removeFromLeft (22.0f);
    g.setColour (col);
    g.fillRect (badge);
    g.setColour (psyched::Colours::onSolid);
    g.setFont (Theme::display (13.0f, true));
    g.drawText (juce::String (deck + 1), badge, juce::Justification::centred);
    top.removeFromLeft (8.0f);
    auto bpmArea = top.removeFromRight (84.0f);
    const double bpm = st.bpm.load();
    g.setColour (st.master.load() ? Theme::accent : Theme::text);
    g.setFont (Theme::mono (20.0f, true));
    g.drawText (loaded && bpm > 0 ? juce::String (bpm, 2) : "--.--", bpmArea, juce::Justification::centredRight);
    g.setColour (loaded ? Theme::text : Theme::textFaint);
    g.setFont (Theme::font (15.0f, true));
    g.drawText (loaded ? track->info.title : juce::String ("No track loaded"), top, juce::Justification::centredLeft, true);
    r.removeFromTop (2.0f);

    // Artist, key, tempo and its range
    auto second = r.removeFromTop (17.0f);
    auto range = second.removeFromRight (44.0f);
    {
        const double rr = st.pitchRange.load();
        g.setColour (Theme::textDim);
        g.setFont (Theme::mono (11.0f));
        g.drawText (rr >= 0.99 ? juce::String ("WIDE") : juce::String::charToString (0xb1) + juce::String ((int) std::lround (rr * 100.0)) + "%",
                    range, juce::Justification::centredRight);
    }
    auto tempo = second.removeFromRight (66.0f);
    const double pct = (st.tempoRatio.load() - 1.0) * 100.0;
    g.setColour (std::abs (pct) < 0.005 ? Theme::textDim : Theme::text);
    g.setFont (Theme::mono (13.0f));
    g.drawText ((pct >= 0 ? "+" : "") + juce::String (pct, 2) + "%", tempo, juce::Justification::centredRight);
    if (loaded && track->info.key.isNotEmpty())
    {
        auto keyBox = second.removeFromRight (62.0f).reduced (2.0f, 0.0f);
        const int shift = st.keyShift.load();
        auto keyText = MusicalKey::display (router.effectiveKeyText (deck));
        if (shift != 0) keyText << " " << (shift > 0 ? "+" : "") << shift;
        const auto kc = shift != 0 ? Theme::warn : Theme::good;
        g.setColour (psyched::Colours::field);
        g.fillRect (keyBox);
        g.setColour (kc);
        g.drawRect (keyBox, 1.0f);
        g.setFont (Theme::font (12.0f, true));
        g.drawText (keyText, keyBox, juce::Justification::centred);
    }
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.5f));
    if (loaded) g.drawText (track->info.artist, second.withTrimmedLeft (30.0f), juce::Justification::centredLeft, true);
    r.removeFromTop (2.0f);

    // Times and status chips (Q = quantize, the same on both decks)
    TimeFold fold = waves.foldFor (deck);
    fold.reference = fold.toDisplay (st.positionSec.load(), st.playSegment.load());
    const double len = st.lengthSec.load();
    const double dlen = juce::jmax (0.001, fold.displayLength (len));
    auto times = r.removeFromTop (24.0f);
    g.setColour (Theme::text);
    g.setFont (Theme::mono (21.0f, true));
    g.drawText (loaded ? formatTime (fold.reference) : "0:00.0", times.removeFromLeft (92.0f), juce::Justification::centredLeft);
    const bool warn = st.endWarning.load() && (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
    g.setColour (warn ? Theme::bad : Theme::textDim);
    g.setFont (Theme::mono (15.0f));
    g.drawText (loaded ? formatTime (-(dlen - fold.reference)) : "-0:00.0", times.removeFromLeft (80.0f), juce::Justification::centredLeft);
    auto chips = times.reduced (0.0f, 2.0f);
    {
        auto q = chips.removeFromRight (22.0f);
        psyched::paintChip (g, q, "Q", Theme::bad.withSaturation (0.6f), st.quantize.load() ? psyched::ChipState::on : psyched::ChipState::off);
        qRects[(size_t) deck] = q.expanded (4.0f);
        chips.removeFromRight (4.0f);
    }
    auto addChip = [&] (const juce::String& text, bool lit, juce::Colour c)
    {
        if (! lit) return;
        auto b = chips.removeFromRight (juce::jmax (38.0f, psyched::Fonts::label().getStringWidthFloat (text) + 12.0f));
        psyched::paintChip (g, b, text, c, psyched::ChipState::on);
        chips.removeFromRight (4.0f);
    };
    addChip ("MASTER", st.master.load(), Theme::accent);
    addChip ("SYNC", st.sync.load(), Theme::sync);
    addChip ("SLIP", st.slip.load(), Theme::warn);
    addChip ("LOOP", st.loopActive.load(), Theme::accent);

    // Overview, with the grid button on its left
    r.removeFromTop (3.0f);
    auto gridBtn = r.removeFromLeft (16.0f);
    paintGridToggle (g, gridBtn, overviewGrid, styledColour ("overviewgrid", Theme::sync));
    gridRects[(size_t) deck] = gridBtn.expanded (4.0f, 0.0f);
    r.removeFromLeft (4.0f);
    auto ov = r;
    overviewRects[(size_t) deck] = ov;
    g.setColour (juce::Colours::black);
    g.fillRect (ov);
    if (! loaded || track->waveform == nullptr) return;

    auto& o = overviews[(size_t) deck];
    const int pw = (int) ov.getWidth(), ph = (int) ov.getHeight();
    if (o.serial != st.trackSerial.load() || o.version != st.trackDataVersion.load() || o.fold != fold.version
        || o.style != overviewStyle || o.image.getWidth() != pw || o.image.getHeight() != ph)
    {
        o.serial = st.trackSerial.load();
        o.version = st.trackDataVersion.load();
        o.fold = fold.version;
        o.style = overviewStyle;
        // the same look as the deck's overview on the computer
        const auto wdStyle = overviewStyle == 2 ? WaveformData::Style::rgb
                           : overviewStyle == 3 ? WaveformData::Style::blue : WaveformData::Style::threeBand;
        auto wd = wdStyle == WaveformData::Style::blue ? track->waveformForSingleColour() : track->waveformInStyle (wdStyle);
        StripOptions so;
        so.overview = true;
        so.shape = overviewHalf ? StripOptions::halfUp : StripOptions::full;
        so.tint = overviewStyle == 3 ? (followDeck ? col : singleColour) : juce::Colour();
        so.fold = &fold;
        so.entriesPerSecond = wd->entriesPerSecond;
        const double entries = fold.isEmpty() ? (double) wd->size() : fold.displayLength (wd->lengthSeconds()) * wd->entriesPerSecond;
        o.image = renderWaveformStrip (*wd, 0.0, entries / juce::jmax (1, pw), pw, ph, so);
    }
    g.drawImage (o.image, ov);
    if (len <= 0.0) return;

    const float px = ov.getX() + (float) juce::jlimit (0.0, 1.0, fold.reference / dlen) * ov.getWidth();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (ov.getX(), ov.getY(), px - ov.getX(), ov.getHeight()));
    if (overviewGrid)
        paintOverviewGrid (g, ov, track->grid, len, &fold, track->phrases.empty() ? 0.0f : overviewStripHeight (ov.getHeight()),
                           overviewStripHeight (ov.getHeight()));
    paintOverviewMarks (g, ov, TrackMarks::fromDeck (st, track->phrases, track.get()), true, &fold);
    g.setColour (juce::Colours::white);
    g.fillRect (juce::Rectangle<float> (px - 1.0f, ov.getY(), 2.0f, ov.getHeight()));
}

} // namespace orbit
