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
    bool stemsSeparate = false;

    enum LaneStyle { stems = 0, threeBand = 1, rgb = 2, singleColour = 3 };
    struct LaneConfig { int style = threeBand; int shape = StripOptions::full; };

    explicit ScrollingWaveforms (AudioEngine& engine);
    ~ScrollingWaveforms() override;

    void setVisibleSeconds (double s);
    double getVisibleSeconds() const { return visibleSeconds; }
    void setMaxFps (int fps) { maxFps = juce::jlimit (15, 240, fps); }
    void setLanes (int deck, LaneConfig top, LaneConfig bottom);
    void setSingleColour (juce::Colour c) { singleTint = c; needsRedraw = true; }
    /** Stem layers from back to front, and each stem's opacity (0..1). */
    void setStemLayers (std::array<int, 4> backToFront, std::array<float, 4> opacity)
    {
        stemOrder = backToFront; stemOpacity = opacity; needsRedraw = true;
    }
    void setFoldSkips (bool f) { foldSkips = f; needsRedraw = true; }
    bool getFoldSkips() const { return foldSkips; }
    /** Stems each in their own row (true) instead of overlaid. */
    void setStemsSeparate (bool b) { if (b != stemsSeparate) { stemsSeparate = b; forceRedraw(); } }
    void setStemsHidden (bool h) { stemsHidden = h; needsRedraw = true; }
    bool getStemsHidden() const { return stemsHidden; }
    void setPlayheadPosition (float fraction) { playhead = juce::jlimit (0.1f, 0.9f, fraction); needsRedraw = true; }
    void setLaneSplit (int deck, float share) { laneSplit[(size_t) deck] = juce::jlimit (0.1f, 0.9f, share); needsRedraw = true; }
    float getLaneSplit (int deck) const { return laneSplit[(size_t) deck]; }
    void forceRedraw() { needsRedraw = true; }

    std::function<void (double newVisibleSeconds)> onZoomChanged;
    std::function<void (int deck, float share)> onLaneSplitChanged;
    std::function<void (float fraction)> onPlayheadMoved;
    std::function<void (bool fold)> onFoldChanged;
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
        bool lastFoldActive = false;
        double uiSkipFrom = -1.0;   // skip started from the right-click menu
    };

    /** A clickable marker drawn on the waveform (for colour / delete). */
    struct Marker { juce::Rectangle<float> r; int deck; int kind; int index; };   // kind 0 hot cue, 1 locator, 2 skip
    enum { markerHot = 0, markerLocator = 1, markerSkip = 2 };

    void onVBlank (double timestampSec);
    void paintDeck (juce::Graphics& g, int deck, juce::Rectangle<int> r);
    void paintLane (juce::Graphics& g, int deck, int lane, juce::Rectangle<int> r);
    void paintOverlays (juce::Graphics& g, int deck, juce::Rectangle<int> wave, juce::Rectangle<int> lane, bool withFlags);
    void drawLabel (juce::Graphics& g, juce::Rectangle<int> lane, const juce::String& text, bool atTop);
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
    std::array<DeckView, 2> decks;
    juce::ThreadPool stemWavePool { 2 };

    double visibleSeconds = 10.0;
    int maxFps = 60;
    std::array<std::array<LaneConfig, 2>, 2> lanes { { { LaneConfig { stems, 0 }, LaneConfig { threeBand, 0 } },
                                                       { LaneConfig { stems, 0 }, LaneConfig { threeBand, 0 } } } };
    juce::Colour singleTint { 0xff3d9bff };
    std::array<int, 4> stemOrder { 2, 1, 3, 0 };
    std::array<float, 4> stemOpacity { 1.0f, 1.0f, 1.0f, 1.0f };
    bool foldSkips = true, stemsHidden = false;
    float playhead = 0.5f;
    std::array<float, 2> laneSplit { 0.5f, 0.5f };
    double lastPaintTime = 0.0;
    bool needsRedraw = true;

    std::vector<Marker> markers;
    int dragDeck = -1, dragDivider = -1;
    bool dragPlayhead = false;
    double dragStartPos = 0.0;

    juce::VBlankAttachment vblank;
};

} // namespace orbit
