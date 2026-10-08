#pragma once
#include "WaveformRender.h"
#include "../Audio/AudioEngine.h"

namespace orbit
{

/** The rekordbox-style scrolling waveforms for both decks, stacked, with the playhead fixed in the centre.
    Repaints are synced to the display's refresh and skipped entirely while nothing moves. */
class ScrollingWaveforms : public juce::Component
{
public:
    explicit ScrollingWaveforms (AudioEngine& engine);

    void setVisibleSeconds (double s);
    double getVisibleSeconds() const { return visibleSeconds; }
    void setMaxFps (int fps) { maxFps = juce::jlimit (15, 240, fps); }
    void forceRedraw() { needsRedraw = true; }

    std::function<void (double newVisibleSeconds)> onZoomChanged;

    void paint (juce::Graphics&) override;
    void resized() override { needsRedraw = true; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Shared helper: the deck's display position, smoothed between audio blocks. */
    double displayPosition (int deck) const { return shownPos[(size_t) deck]; }

private:
    void onVBlank (double timestampSec);
    void paintLane (juce::Graphics& g, int deck, juce::Rectangle<int> r);
    juce::Rectangle<int> laneBounds (int deck) const;
    int laneAt (juce::Point<int> p) const;

    AudioEngine& engine;
    std::array<WaveformTileCache, 2> tiles;
    std::array<PositionSmoother, 2> smoothers;
    std::array<double, 2> shownPos { 0.0, 0.0 };
    std::array<LoadedTrack::Ptr, 2> tracks;
    std::array<int, 2> serials { -1, -1 };

    double visibleSeconds = 10.0;
    int maxFps = 60;
    double lastPaintTime = 0.0;
    bool needsRedraw = true;

    int dragDeck = -1;
    double dragStartPos = 0.0;

    juce::VBlankAttachment vblank;
};

} // namespace orbit
