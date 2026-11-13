#pragma once
#include "WaveformRender.h"
#include "Theme.h"
#include "../Audio/AudioEngine.h"

namespace orbit
{

/** Scrolling waveforms for both decks. Each deck has a top and a bottom lane that scroll together; each lane
    can show the stems (overlaid in their colours, following the stem knobs), the 3-band, RGB or single-colour
    waveform, full or half height. The split between the lanes and the playhead position can be dragged.
    Skipped sections are folded out so the waveform shows the track as it will play.
    Repaints are synced to the display and skipped while nothing moves. */
class ScrollingWaveforms : public juce::Component,
                           public juce::DragAndDropTarget
{
public:
    // Tracks dragged from the browser: the top half loads deck 1, the bottom half deck 2
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description == juce::var ("centrifuge-tracks"); }
    void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
    void itemDragMove (const SourceDetails& d) override { const int h = d.localPosition.y < getHeight() / 2 ? 0 : 1; if (h != dropDeck) { dropDeck = h; repaint(); } }
    void itemDragExit (const SourceDetails&) override { dropDeck = -1; repaint(); }
    void itemDropped (const SourceDetails& d) override { const int h = d.localPosition.y < getHeight() / 2 ? 0 : 1; dropDeck = -1; repaint(); if (onTrackDropped) onTrackDropped (h); }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (dropDeck < 0) return;
        auto r = getLocalBounds().toFloat();
        auto half = dropDeck == 0 ? r.removeFromTop (r.getHeight() / 2) : r.removeFromBottom (r.getHeight() / 2);
        g.setColour (psyched::Colours::void_.withAlpha (0.65f));
        g.fillRect (half);
        g.setColour (Theme::deckColour (dropDeck));
        g.drawRect (half.reduced (2.0f), 2.0f);
        psyched::paintTitle (g, "Drop to load on deck " + juce::String (dropDeck + 1), half, juce::Justification::centred, Theme::deckColour (dropDeck), 14.0f);
    }
    std::function<void (int deck)> onTrackDropped;
    int dropDeck = -1;
    enum LaneStyle { laneOff = -1, stems = 0, threeBand = 1, rgb = 2, singleColour = 3, stemsSplit = 4,
                     stemDrums = 5, stemBass = 6, stemInst = 7, stemVocals = 8 };   // 5..8: one stem on its own
    static bool isSingleStem (int style) { return style >= stemDrums && style <= stemVocals; }
    /** Picked from the lane's name box (or the second lane switched on / off): deck, lane (0 / 1), style. */
    std::function<void (int deck, int lane, int style)> onLaneStyleChosen;
    struct LaneConfig { int style = threeBand; int shape = StripOptions::full; };

    /** deckIds: the engine decks shown, top to bottom ({ 0, 1 } on the play page, { prepDeck } on PREP). */
    explicit ScrollingWaveforms (AudioEngine& engine, std::vector<int> deckIds = { 0, 1 });
    int numSlots() const { return (int) ids.size(); }
    ~ScrollingWaveforms() override;

    void setVisibleSeconds (double s);
    void setZoomRange (double minSeconds, double maxSeconds) { zoomMin = minSeconds; zoomMax = maxSeconds; setVisibleSeconds (visibleSeconds); }
    /** Dragging the waveform scrubs even while the deck plays (PREP). */
    void setScrubWhilePlaying (bool b) { scrubWhilePlaying = b; }
    /** Highlighted phrase in the phrase strip (PREP's list selection); -1 = none. */
    std::function<int()> selectedPhrase;
    /** Size for "Add loop marker here" (beats). */
    std::function<double()> loopBeatsForMenu;
    double getVisibleSeconds() const { return visibleSeconds; }
    void setMaxFps (int fps) { maxFps = juce::jlimit (15, 240, fps); }
    void setLanes (int deck, LaneConfig top, LaneConfig bottom);
    void setSingleColour (juce::Colour c, bool followDeck = false) { singleTint = c; singleFollowsDeck = followDeck; needsRedraw = true; }
    /** Stem layers from back to front, and each stem's opacity (0..1). */
    void setStemLayers (std::array<int, 4> backToFront, std::array<float, 4> opacity)
    {
        stemOrder = backToFront; stemOpacity = opacity; needsRedraw = true;
    }
    /** Edited view (per deck): the waveform shows the track as it will play - skips folded out, phrases in their
        rearranged order, repeats shown again. Off = the whole track as recorded, with every marker. */
    void setFoldSkips (bool f) { editView = { f, f }; needsRedraw = true; }
    void setEditView (int deck, bool on) { editView[(size_t) deck] = on; needsRedraw = true; }
    bool getEditView (int deck) const { return editView[(size_t) deck]; }
    /** Display time (edited view) of a deck's playhead, and the edited timeline's length. */
    const TimeFold& foldFor (int deck) const { return decks[(size_t) deck].fold; }
    void setStemsHidden (bool h) { stemsHidden = h; needsRedraw = true; }
    bool getStemsHidden() const { return stemsHidden; }
    void setPlayheadPosition (float fraction) { playhead = juce::jlimit (0.1f, 0.9f, fraction); needsRedraw = true; }
    float getPlayheadPosition() const { return playhead; }
    void setLaneSplit (int deck, float share) { laneSplit[(size_t) deck] = juce::jlimit (0.1f, 0.9f, share); needsRedraw = true; }
    float getLaneSplit (int deck) const { return laneSplit[(size_t) deck]; }
    void forceRedraw() { needsRedraw = true; }
    /** For a copy that is never on screen (the XDJ-XZ screen): advance it by hand before painting it. */
    void tick (double timestampSec) { onVBlank (timestampSec); }
    /** The XDJ-XZ screen: touches never scrub or move the track (the jog wheels do that), the split and the playhead
        are easier to grab, and the lane names ask onLaneLabelTapped instead of opening a menu. */
    bool touchMode = false;
    /** Seconds the waveform is drawn ahead, to make up for the time the picture takes to reach a screen (XDJ-XZ). */
    double extraLead = 0.0;
    std::function<void (int slot, int lane, juce::Rectangle<int> label)> onLaneLabelTapped;
    /** The lane styles to choose from (id, name) and the one in use. */
    static std::vector<std::pair<int, juce::String>> laneStyleChoices (int lane);
    int laneStyle (int slot, int lane) const { return lanes[(size_t) slot][(size_t) lane].style; }
    int slotDeck (int slot) const { return ids[(size_t) slot]; }
    juce::Rectangle<float> labelAvoid;   // while painting: the deck badge and lanes button (lane names keep clear of them)

    std::function<void (double newVisibleSeconds)> onZoomChanged;
    std::function<void (int deck, float share)> onLaneSplitChanged;
    std::function<void (float fraction)> onPlayheadMoved;
    std::function<void (int deck, bool editView)> onEditViewChanged;
    std::function<juce::String (int deck, bool hotCue, int index)> cueName;
    std::function<void (int deck, bool hotCue, int index, juce::String currentName)> onRenameCue;

    void paint (juce::Graphics&) override;
    void resized() override { needsRedraw = true; }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct DeckView
    {
        LoadedTrack::Ptr track;
        int serial = -1, dataVersion = -1;
        StemAudio::Ptr stems;
        StemWaveform::Ptr stemWave;
        std::array<WaveformTileCache, 2> laneTiles, underTiles;
        std::array<std::array<WaveformTileCache, 4>, 2> stemTiles;
        PositionSmoother smoother;
        double shownPos = 0.0;      // display time (skipped sections folded out)
        juce::uint64 lastHash = 0;
        int lastComputed = 0;
        std::shared_ptr<std::atomic<bool>> stemJobCancel;
        TimeFold fold;
        std::vector<std::pair<double, double>> lastSkips;
        std::vector<std::pair<double, double>> lastMap;
        bool lastFoldActive = false;
        double uiSkipFrom = -1.0;   // skip started from the right-click menu
    };

    /** A clickable marker drawn on the waveform (for colour / delete). */
    struct Marker { juce::Rectangle<float> r; int deck; int kind; int index; };   // kind 0 hot cue, 1 locator, 2 skip
    enum { markerHot = 0, markerLocator = 1, markerSkip = 2 };

    void onVBlank (double timestampSec);
    void paintDeck (juce::Graphics& g, int deck, juce::Rectangle<int> r);
    void paintLane (juce::Graphics& g, int deck, int lane, juce::Rectangle<int> r);
    void paintOverlays (juce::Graphics& g, int deck, juce::Rectangle<int> wave, juce::Rectangle<int> lane, bool withFlags, bool stemRows = false);
    juce::Rectangle<int> drawLabel (juce::Graphics& g, juce::Rectangle<int> lane, const juce::String& text, bool atTop);
    std::array<std::array<juce::Rectangle<int>, 2>, 2> labelRects;   // lane name boxes (click: change the lane's type)
    std::array<juce::Rectangle<int>, 2> laneToggleRects;             // second lane on / off
    std::array<int, 2> lastBottomStyle { 1, 1 };
    /** The lanes button's cycle for an engine deck (2 lanes / top only / bottom only) and the styles it brings back. */
    struct LaneCycle { int mode = 1, top = threeBand, bottom = threeBand; };
    static LaneCycle& laneCycle (int engineDeckId) { static std::array<LaneCycle, 4> c; return c[(size_t) juce::jlimit (0, 3, engineDeckId)]; }
    void showLaneStyleMenu (int slot, int lane);
    juce::Rectangle<int> deckBounds (int deck) const;
    juce::Rectangle<int> dividerBounds (int deck) const;
    int dividerAt (juce::Point<int> p) const;
    float playheadX (juce::Rectangle<int> r) const { return (float) r.getX() + (float) r.getWidth() * playhead; }
    bool nearPlayhead (juce::Point<int> p) const;
    double sourceTimeAt (int deck, float x) const;
    const Marker* markerAt (juce::Point<float> p) const;
    void showMarkerMenu (const Marker& m);
    void showDeckMenu (int deck, double sourceTime);
    void deleteMarker (const Marker& m);
    juce::String laneName (int deck, int lane) const;

    AudioEngine& engine;
    std::vector<int> ids;
    std::array<DeckView, 2> decks;
    double zoomMin = 1.0, zoomMax = 60.0;
    bool scrubWhilePlaying = false;
    int engineDeck (int slot) const { return ids[(size_t) slot]; }
    bool hasBottomLane (int slot) const { return lanes[(size_t) slot][1].style != laneOff; }
    /** The phrase bars sit next to the waveform, not over it: under deck 1 (and PREP), above deck 2, so the two decks'
        phrases meet in the middle. */
    int phraseStripHeight (int slot) const;
    juce::Rectangle<int> laneArea (int slot) const;
    juce::Rectangle<int> phraseStripArea (int slot) const;
    juce::ThreadPool stemWavePool { 2 };

    double visibleSeconds = 10.0;
    int maxFps = 60;
    std::array<std::array<LaneConfig, 2>, 2> lanes { { { LaneConfig { stems, 0 }, LaneConfig { threeBand, 0 } },
                                                       { LaneConfig { stems, 0 }, LaneConfig { threeBand, 0 } } } };
    juce::Colour singleTint { 0xff3d9bff };
    bool singleFollowsDeck = false;
    std::array<int, 4> stemOrder { 2, 1, 3, 0 };
    std::array<float, 4> stemOpacity { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<bool, 2> editView { true, true };
    bool stemsHidden = false;
    float playhead = 0.5f;
    std::array<float, 2> laneSplit { 0.5f, 0.5f };
    double lastPaintTime = 0.0;
    bool needsRedraw = true;

    std::vector<Marker> markers;
    int dragDeck = -1, dragDivider = -1;
    bool dragPlayhead = false;
    double dragStartPos = 0.0;
    bool dragScrub = false;
    float dragLastX = 0.0f;

    juce::VBlankAttachment vblank;
};

} // namespace orbit
