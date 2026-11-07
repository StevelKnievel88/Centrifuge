#include "StemsView.h"
#include "Theme.h"

namespace orbit
{

StemsView::StemsView (StemManager& s,
                      std::function<std::vector<TrackInfo>()> list,
                      std::function<std::vector<TrackInfo>()> selected,
                      std::function<juce::String()> name,
                      std::function<std::array<int, 4>()> gm,
                      std::function<void (std::array<int, 4>)> sm)
    : stems (s), getList (std::move (list)), getSelected (std::move (selected)), getListName (std::move (name)),
      getMatrix (std::move (gm)), setMatrix (std::move (sm))
{
    addAndMakeVisible (rotor);
    for (auto* b : { &analyseList, &analyseSelected, &stop, &openCache })
        addAndMakeVisible (*b);
    analyseList.getProperties().set ("psyched.solid", true);
    analyseList.setColour (juce::TextButton::buttonOnColourId, Theme::accent);

    analyseList.onClick = [this] { stems.addToBatch (getList()); };
    analyseSelected.onClick = [this] { stems.addToBatch (getSelected()); };
    stop.onClick = [this] { stems.cancelBatch(); };
    openCache.onClick = [this]
    {
        auto dir = stems.getSettings().cacheDir;
        dir.createDirectory();
        dir.startAsProcess();
    };
    startTimerHz (4);
}

void StemsView::timerCallback()
{
    shown = stems.getStatus();
    listName = getListName();
    rotor.setSpinning (shown.activity.isNotEmpty());
    rotor.setProgress (shown.activity.isNotEmpty() ? (float) shown.progress : 1.0f);
    repaint();
}

void StemsView::resized()
{
    auto r = getLocalBounds().reduced (24);
    r.removeFromTop (240);
    auto row = r.removeFromTop (36);
    analyseList.setBounds (row.removeFromLeft (240));
    row.removeFromLeft (10);
    analyseSelected.setBounds (row.removeFromLeft (200));
    row.removeFromLeft (10);
    stop.setBounds (row.removeFromLeft (90));
    row.removeFromLeft (10);
    openCache.setBounds (row.removeFromLeft (170));

    r.removeFromTop (40);
    matrixArea = r.removeFromTop (5 * 34 + 64).withWidth (juce::jmin (r.getWidth(), 560));
    r.removeFromTop (12);
    if (settingsPanel != nullptr) settingsPanel->setBounds (r.withTrimmedLeft (-20).withWidth (juce::jmin (r.getWidth() + 20, 880)));
    auto right = getLocalBounds().reduced (24).removeFromRight (juce::jmin (260, getWidth() / 4));
    rotor.setBounds (right.removeFromTop (right.getWidth()).reduced (20));
}

juce::Rectangle<int> StemsView::cell (int control, int stem) const
{
    const int top = matrixArea.getY() + 64 + 34, left = matrixArea.getX() + 150;
    return { left + stem * 96, top + control * 34, 88, 28 };
}

void StemsView::mouseDown (const juce::MouseEvent& e)
{
    auto m = getMatrix();
    for (int c = 0; c < 4; ++c)
        for (int s = 0; s < 4; ++s)
            if (cell (c, s).contains (e.getPosition()))
            {
                m[(size_t) c] ^= (1 << s);
                setMatrix (m);
                repaint();
                return;
            }
}

void StemsView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (24);
    psyched::paintTitle (g, "Stems", r.removeFromTop (34).toFloat(), juce::Justification::centredLeft, Theme::text, 16.0f);
    r.removeFromRight (juce::jmin (260, getWidth() / 4) + 10);

    g.setFont (Theme::font (14.0f));
    g.setColour (Theme::textDim);
    g.drawFittedText ("Tracks are split into Drums, Bass, Inst and Vocals. Pre-analysed tracks load their stems instantly; "
                      "anything else is separated in the background as soon as it's loaded on a deck (stems fill in from the start "
                      "of the track). Map each stem's volume to any knob on the XDJ-XZ in the Mapping page.",
                      r.removeFromTop (44), juce::Justification::topLeft, 3);
    r.removeFromTop (10);

    auto line = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
    {
        auto row = r.removeFromTop (24);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (13.5f));
        g.drawText (label, row.removeFromLeft (150), juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (Theme::font (13.5f, true));
        g.drawText (value, row, juce::Justification::centredLeft, true);
    };

    line ("Engine", shown.backend.isNotEmpty() ? shown.backend : juce::String ("starts with the first track"),
          shown.modelAvailable ? Theme::good : Theme::text);
    line ("Now", shown.activity.isNotEmpty() ? shown.activity : juce::String ("idle"), Theme::text);

    auto bar = r.removeFromTop (14).withWidth (juce::jmin (520, r.getWidth())).toFloat().reduced (0.0f, 3.0f).withTrimmedLeft (150.0f);
    g.setColour (Theme::panelHi);
    g.fillRect (bar);
    if (shown.activity.isNotEmpty())
    {
        g.setColour (Theme::accent);
        g.fillRect (bar.withWidth (bar.getWidth() * (float) shown.progress));
    }
    r.removeFromTop (6);

    line ("Batch", juce::String (shown.batchQueued) + " queued,  " + juce::String (shown.batchDone) + " done"
                   + (shown.batchFailed > 0 ? ",  " + juce::String (shown.batchFailed) + " failed" : juce::String()), Theme::text);
    line ("Current list", listName.isNotEmpty() ? listName : juce::String ("-"), Theme::text);
    if (shown.lastError.isNotEmpty())
        line ("Last problem", shown.lastError, Theme::bad);

    // --- Stem matrix --------------------------------------------------------------
    auto mr = matrixArea;
    g.setColour (Theme::accent);
    g.setFont (Theme::font (12.5f, true));
    g.drawText ("STEM CONTROLS  (shared by both decks)", mr.removeFromTop (22), juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.5f));
    g.drawText ("Map 'Stem control A-D' to knobs in Mapping, then tick which stems each one moves.",
                mr.removeFromTop (8 + 34).withTrimmedTop (8), juce::Justification::topLeft);

    const auto m = getMatrix();
    for (int s = 0; s < 4; ++s)
    {
        auto h = cell (0, s).translated (0, -34);
        g.setColour (Theme::stemColour (s));
        g.setFont (Theme::font (13.0f, true));
        g.drawText (juce::String (stemName (s)).toUpperCase(), h, juce::Justification::centred);
    }
    for (int c = 0; c < 4; ++c)
    {
        auto label = juce::Rectangle<int> (matrixArea.getX(), cell (c, 0).getY(), 140, 28);
        g.setColour (Theme::text);
        g.setFont (Theme::font (13.5f, true));
        g.drawText ("Control " + juce::String::charToString ((juce::juce_wchar) ('A' + c)), label, juce::Justification::centredLeft);
        for (int s = 0; s < 4; ++s)
        {
            const bool on = (m[(size_t) c] >> s) & 1;
            auto b = cell (c, s).toFloat();
            g.setColour (on ? Theme::stemColour (s).withAlpha (0.75f) : Theme::panelHi);
            g.fillRect (b);
            g.setColour (on ? Theme::stemColour (s) : Theme::line);
            g.drawRect (b.reduced (0.5f), 1.0f);
            if (on)
            {
                g.setColour (juce::Colours::black);
                g.setFont (Theme::font (14.0f, true));
                g.drawText ("ON", b, juce::Justification::centred);
            }
        }
    }
}

} // namespace orbit
