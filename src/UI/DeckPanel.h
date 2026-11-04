#pragma once
#include "WaveformRender.h"
#include "Theme.h"
#include "CentrifugeRotor.h"
#include "ColourPicker.h"
#include "../Control/ActionRouter.h"
#include "../App/TrackEditor.h"

namespace orbit
{

/** Track info, overview waveform, hot cue pads, transport and stem controls for one deck. */
class DeckPanel : public juce::Component,
                  public juce::DragAndDropTarget,
                  private juce::Timer
{
public:
    DeckPanel (int deckIndex, AudioEngine& engine, ActionRouter& router);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void setLoadingText (const juce::String& s) { loadingText = s; repaint(); }

    // Tracks dragged from the browser load onto this deck
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description == juce::var ("centrifuge-tracks"); }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override { dropHover = false; repaint(); if (onTrackDropped) onTrackDropped (deck); }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (! dropHover) return;
        g.setColour (psyched::Colours::void_.withAlpha (0.7f));
        g.fillRect (getLocalBounds());
        g.setColour (Theme::deckColour (deck));
        g.drawRect (getLocalBounds().toFloat().reduced (2.0f), 2.0f);
        psyched::paintTitle (g, "Drop to load on deck " + juce::String (deck + 1), getLocalBounds().toFloat(), juce::Justification::centred, Theme::deckColour (deck), 14.0f);
    }
    std::function<void (int deck)> onTrackDropped;

    void setEditor (TrackEditor* e) { editor = e; }
    void toggleGridMode() { gridMode = ! gridMode; repaint(); }
    std::function<void (int deck, bool hotCue, int index, juce::String currentName)> onRenameCue;
    std::function<void (int deck)> onSetBpm;
    std::function<void (int deck, juce::Colour)> onDeckColour;

    /** Overview waveform: style 1 = 3-band, 2 = RGB, 3 = single colour; half = bars from the bottom only. */
    void setOverviewStyle (int style, bool half, juce::Colour single)
    {
        overviewStyle = style; overviewHalf = half; singleColour = single;
        overviewWidth = 0;
        repaint();
    }

private:
    void timerCallback() override;
    void press (const juce::String& id, bool down);

    struct Region { juce::Rectangle<float> r; juce::String action; int kind; }; // kind 0 button, 1 overview, 2 stem gain
    std::vector<Region> regions;
    const Region* regionAt (juce::Point<float> p) const;

    const int deck;
    AudioEngine& engine;
    ActionRouter& router;

    LoadedTrack::Ptr track;
    int serial = -1, dataVersion = -1;
    juce::Image overview;
    int overviewWidth = 0;
    juce::String loadingText;

    juce::Rectangle<float> rowInfo, rowStatus, rowOverview, rowPads, rowJump, rowControls;
    void showDeckColourMenu (juce::Rectangle<float> area);
    juce::String heldAction;
    int dragStem = -1;
    juce::uint64 lastHash = 0;
    CentrifugeRotor rotor;
    juce::Rectangle<float> rotorArea;
    int overviewStyle = 1;
    bool overviewHalf = false;
    juce::Colour singleColour { 0xff3d9bff };
    void showPadMenu (int pad, juce::Rectangle<float> area);
    TrackEditor* editor = nullptr;
    bool gridMode = false;
    bool dropHover = false;
};

} // namespace orbit
