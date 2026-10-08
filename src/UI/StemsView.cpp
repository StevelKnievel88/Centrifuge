#include "StemsView.h"
#include "Theme.h"

namespace orbit
{

StemsView::StemsView (StemManager& s,
                      std::function<std::vector<TrackInfo>()> list,
                      std::function<std::vector<TrackInfo>()> selected,
                      std::function<juce::String()> name)
    : stems (s), getList (std::move (list)), getSelected (std::move (selected)), getListName (std::move (name))
{
    for (auto* b : { &analyseList, &analyseSelected, &stop, &openCache })
        addAndMakeVisible (*b);
    analyseList.setColour (juce::TextButton::buttonColourId, Theme::accent.withAlpha (0.85f));
    analyseList.setColour (juce::TextButton::textColourOffId, juce::Colours::black);

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
}

void StemsView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    auto r = getLocalBounds().reduced (24);

    g.setColour (Theme::text);
    g.setFont (Theme::font (22.0f, true));
    g.drawText ("Stems", r.removeFromTop (34), juce::Justification::centredLeft);

    g.setFont (Theme::font (14.0f));
    g.setColour (Theme::textDim);
    g.drawFittedText ("Tracks are split into Drums, Bass, Instruments and Vocals. Pre-analysed tracks load their stems instantly; "
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
    g.fillRoundedRectangle (bar, 3.0f);
    if (shown.activity.isNotEmpty())
    {
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) shown.progress), 3.0f);
    }
    r.removeFromTop (6);

    line ("Batch", juce::String (shown.batchQueued) + " queued,  " + juce::String (shown.batchDone) + " done"
                   + (shown.batchFailed > 0 ? ",  " + juce::String (shown.batchFailed) + " failed" : juce::String()), Theme::text);
    line ("Current list", listName.isNotEmpty() ? listName : juce::String ("-"), Theme::text);
    if (shown.lastError.isNotEmpty())
        line ("Last problem", shown.lastError, Theme::bad);
}

} // namespace orbit
