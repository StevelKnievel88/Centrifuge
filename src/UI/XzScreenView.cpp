#include "XzScreenView.h"
#include "TrackMarks.h"
#include "Theme.h"
#include "../Audio/MusicalKey.h"

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
}

XzScreenView::XzScreenView (AudioEngine& e, ActionRouter& r)
    : engine (e), router (r), waves (e, { 0, 1 })
{
    setOpaque (true);
    addAndMakeVisible (waves);
    setSize (800, 480);
    setVisible (true);
}

void XzScreenView::setOverviewStyle (int style, bool half, juce::Colour single)
{
    overviewStyle = style;
    overviewHalf = half;
    singleColour = single;
    for (auto& o : overviews) o.style = -1;
}

void XzScreenView::touch (bool down, int x, int y)
{
    if (! down) return;
    for (int d = 0; d < 2; ++d)
    {
        const auto& r = overviewRects[(size_t) d];
        if (! r.contains ((float) x, (float) y) || r.getWidth() <= 0.0f) continue;
        auto& dk = engine.deck (d);
        if (dk.state().trackSerial.load() == 0) return;
        const double f = juce::jlimit (0.0, 1.0, (double) (((float) x - r.getX()) / r.getWidth()));
        const auto& fold = waves.foldFor (d);
        if (fold.isEmpty()) dk.post (Deck::Cmd::seekFraction, f);
        else dk.post (Deck::Cmd::seekSeconds, fold.toSource (f * fold.displayLength (dk.state().lengthSec.load())));
        return;
    }
}

void XzScreenView::resized()
{
    auto r = getLocalBounds();
    waves.setBounds (r.removeFromTop (296));
    infoArea = r;
}

juce::Image XzScreenView::render()
{
    waves.tick (juce::Time::getMillisecondCounterHiRes() / 1000.0);
    juce::Image img (juce::Image::RGB, getWidth(), getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g (img);
    paintEntireComponent (g, true);
    return img;
}

void XzScreenView::paint (juce::Graphics& g)
{
    g.fillAll (psyched::Colours::void_);
    auto r = infoArea.toFloat();
    g.setColour (psyched::Colours::edge2);
    g.fillRect (r.removeFromTop (2.0f));
    const float w = r.getWidth() / 2.0f;
    paintInfo (g, 0, r.removeFromLeft (w).reduced (8.0f, 6.0f));
    g.setColour (psyched::Colours::edge2);
    g.fillRect (r.removeFromLeft (2.0f));
    paintInfo (g, 1, r.reduced (8.0f, 6.0f));
}

void XzScreenView::paintInfo (juce::Graphics& g, int deck, juce::Rectangle<float> r)
{
    auto& dk = engine.deck (deck);
    const auto& st = dk.state();
    auto track = st.trackSerial.load() != 0 ? dk.getLoadedTrack() : nullptr;
    const bool loaded = track != nullptr;
    const auto col = Theme::deckColour (deck);

    // Title row: deck badge, title, BPM
    auto top = r.removeFromTop (24.0f);
    auto badge = top.removeFromLeft (24.0f);
    g.setColour (col);
    g.fillRect (badge);
    g.setColour (psyched::Colours::onSolid);
    g.setFont (Theme::display (14.0f, true));
    g.drawText (juce::String (deck + 1), badge, juce::Justification::centred);
    top.removeFromLeft (8.0f);

    auto bpmArea = top.removeFromRight (92.0f);
    const double bpm = st.bpm.load();
    g.setColour (st.master.load() ? Theme::accent : Theme::text);
    g.setFont (Theme::mono (21.0f, true));
    g.drawText (loaded && bpm > 0 ? juce::String (bpm, 2) : "--.--", bpmArea, juce::Justification::centredRight);

    g.setColour (loaded ? Theme::text : Theme::textFaint);
    g.setFont (Theme::font (15.0f, true));
    g.drawText (loaded ? track->info.title : juce::String ("No track loaded"), top, juce::Justification::centredLeft, true);

    // Artist, key and tempo
    auto second = r.removeFromTop (20.0f);
    auto tempo = second.removeFromRight (92.0f);
    const double pct = (st.tempoRatio.load() - 1.0) * 100.0;
    g.setColour (std::abs (pct) < 0.005 ? Theme::textDim : Theme::text);
    g.setFont (Theme::mono (13.0f));
    g.drawText ((pct >= 0 ? "+" : "") + juce::String (pct, 2) + "%", tempo, juce::Justification::centredRight);
    if (loaded && track->info.key.isNotEmpty())
    {
        auto keyBox = second.removeFromRight (64.0f).reduced (2.0f, 1.0f);
        const int shift = st.keyShift.load();
        g.setColour (Theme::panelHi);
        g.fillRect (keyBox);
        g.setColour (shift != 0 ? Theme::warn : Theme::good);
        g.setFont (Theme::font (12.0f, true));
        auto keyText = MusicalKey::display (router.effectiveKeyText (deck));
        if (shift != 0) keyText << " " << (shift > 0 ? "+" : "") << shift;
        g.drawText (keyText, keyBox, juce::Justification::centred);
    }
    second.removeFromLeft (32.0f);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.5f));
    if (loaded) g.drawText (track->info.artist, second, juce::Justification::centredLeft, true);

    // Times (as the edit plays) and status chips
    TimeFold fold = waves.foldFor (deck);
    fold.reference = fold.toDisplay (st.positionSec.load(), st.playSegment.load());
    const double len = st.lengthSec.load();
    const double dlen = juce::jmax (0.001, fold.displayLength (len));
    auto times = r.removeFromTop (40.0f);
    g.setColour (Theme::text);
    g.setFont (Theme::mono (28.0f, true));
    g.drawText (loaded ? formatTime (fold.reference) : "0:00.0", times.removeFromLeft (150.0f), juce::Justification::centredLeft);
    const bool warn = st.endWarning.load() && (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
    g.setColour (warn ? Theme::bad : Theme::textDim);
    g.setFont (Theme::mono (17.0f));
    g.drawText (loaded ? formatTime (-(dlen - fold.reference)) : "-0:00.0", times.removeFromLeft (100.0f), juce::Justification::centredLeft);
    auto chips = times.reduced (0.0f, 9.0f);
    auto addChip = [&] (const juce::String& text, bool lit, juce::Colour c)
    {
        if (! lit) return;
        auto b = chips.removeFromRight (juce::jmax (40.0f, psyched::Fonts::label().getStringWidthFloat (text) + 14.0f));
        psyched::paintChip (g, b, text, c, psyched::ChipState::on);
        chips.removeFromRight (4.0f);
    };
    addChip ("MASTER", st.master.load(), Theme::accent);
    addChip ("SYNC", st.sync.load(), Theme::sync);
    addChip ("SLIP", st.slip.load(), Theme::warn);
    addChip ("LOOP", st.loopActive.load(), Theme::accent);

    // Overview
    r.removeFromTop (4.0f);
    auto ov = r.withTrimmedBottom (2.0f);
    overviewRects[(size_t) deck] = ov;
    g.setColour (psyched::Colours::field);
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
        const auto wdStyle = overviewStyle == 2 ? WaveformData::Style::rgb
                           : overviewStyle == 3 ? WaveformData::Style::blue : WaveformData::Style::threeBand;
        auto wd = wdStyle == WaveformData::Style::blue ? track->waveformForSingleColour() : track->waveformInStyle (wdStyle);
        StripOptions so;
        so.overview = true;
        so.shape = overviewHalf ? StripOptions::halfUp : StripOptions::full;
        so.tint = overviewStyle == 3 ? singleColour : juce::Colour();
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
    paintOverviewMarks (g, ov, TrackMarks::fromDeck (st, track->phrases, track.get()), true, &fold);
    g.setColour (juce::Colours::white);
    g.fillRect (juce::Rectangle<float> (px - 1.0f, ov.getY(), 2.0f, ov.getHeight()));
}

} // namespace orbit
