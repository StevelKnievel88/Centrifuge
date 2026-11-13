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
    /** Edited view: the overview (and times) show the track as it will play. */
    void setEditView (bool on) { if (on != editView) { editView = on; overviewWidth = 0; repaint(); } }
    std::function<void (int deck)> onEditViewToggled;
    /** Bar lines and minute marks over the overview. */
    void setOverviewGrid (bool on) { if (on != overviewGrid) { overviewGrid = on; repaint(); } }
    std::function<void()> onOverviewGridToggled;
    /** A stem's control (or all four: stem -1) changed between level / low-pass / high-pass. */
    std::function<void (int deck, int stem, int mode)> onStemModeChanged;
    std::function<void()> onOpenSettings;
    /** A beat jump button's size or colour was changed on the deck (slot 0-3). */
    std::function<void (int deck, int slot, double beats, juce::uint32 colour)> onJumpSlotChanged;
    /** The deck's vinyl speed knob: brake time in seconds, or synced to beats (0 = not synced). */
    std::function<void (int deck, double seconds, double beats)> onBrakeChanged;
    /** The scrolling waveform's visible seconds and playhead position (0..1), for the view box on the overview. */
    std::function<std::pair<double, float>()> viewWindow;

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
    juce::String heldAction, flashAction;
public:
    struct ChipDrawn { juce::Rectangle<float> r; juce::String text; bool lit; juce::Colour colour; };
private:
    std::vector<ChipDrawn> chipsDrawn;
    double flashUntil = 0.0;
    static juce::String jumpSizeText (double beats);
    void showJumpSlotMenu (int slot, juce::Rectangle<float> area, bool colour);
    int dragStem = -1, dragFilter = -1;
    bool resoDrag = false, scrubDrag = false, brakeDrag = false;
    double brakeDragStart = 0.0;
    void setBrake (double seconds, double beats);
    float scrubLastX = 0.0f;
    float resoStart = 0.25f;
    juce::uint64 lastHash = 0;
    juce::Rectangle<float> resoArea;
    int overviewStyle = 1;
    bool overviewHalf = false;
    juce::Colour singleColour { 0xff3d9bff };
    void showPadMenu (int pad, juce::Rectangle<float> area);
    TrackEditor* editor = nullptr;
    bool gridMode = false;
    bool editView = true;
    TimeFold fold;
    std::vector<std::pair<double, double>> foldMapKey, foldSkipKey;
    bool foldOnKey = false;
    int renderedFold = -1;
    void updateFold();
    void showEditVersionMenu (juce::Rectangle<float> area);
    void paintNotesBox (juce::Graphics& g, juce::Rectangle<float> r);
    void showStemModeMenu (int stem, juce::Rectangle<float> area);
    void setStemMode (int stem, int mode);
    bool overviewGrid = false;

    const NoteLocator* noteShowing (double pos) const;
    bool dropHover = false;
    bool tempoDrag = false;
    double tempoDragStart = 1.0;
};

} // namespace orbit
