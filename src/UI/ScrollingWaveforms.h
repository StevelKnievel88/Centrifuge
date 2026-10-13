#pragma once
#include "WaveformRender.h"
#include "../Audio/AudioEngine.h"

namespace orbit
{

/** Scrolling waveforms for both decks. Each deck has two lanes that scroll together:
      - main lane: the stems overlaid in their colours (following the stem knobs), or rekordbox's 3-band view
      - original lane: the unaltered track, in its own style (3-band / RGB / single colour)
    The split between the lanes can be dragged. Repaints are synced to the display and skipped while nothing moves. */
class ScrollingWaveforms : public juce::Component
{
public:
    explicit ScrollingWaveforms (AudioEngine& engine);
    ~ScrollingWaveforms() override;

    void setVisibleSeconds (double s);
    double getVisibleSeconds() const { return visibleSeconds; }
    void setMaxFps (int fps) { maxFps = juce::jlimit (15, 240, fps); }
    void setOriginalStyle (WaveformData::Style s) { originalStyle = s; needsRedraw = true; }
    void setMainMode (int m) { mainMode = m; needsRedraw = true; }
    int getMainMode() const { return mainMode; }
    void setLaneSplit (int deck, float share) { laneSplit[(size_t) deck] = juce::jlimit (0.1f, 0.9f, share); needsRedraw = true; }
    float getLaneSplit (int deck) const { return laneSplit[(size_t) deck]; }
    void forceRedraw() { needsRedraw = true; }

    std::function<void (double newVisibleSeconds)> onZoomChanged;
    std::function<void (int deck, float share)> onLaneSplitChanged;

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
        int serial = -1;
        StemAudio::Ptr stems;
        StemWaveform::Ptr stemWave;
        WaveformTileCache mainTiles, originalTiles;
        std::array<WaveformTileCache, 4> stemTiles;
        PositionSmoother smoother;
        double shownPos = 0.0;
        juce::uint64 lastHash = 0;
        int lastComputed = 0;
        std::shared_ptr<std::atomic<bool>> stemJobCancel;
    };

    void onVBlank (double timestampSec);
    void paintDeck (juce::Graphics& g, int deck, juce::Rectangle<int> r);
    void paintLane (juce::Graphics& g, int deck, juce::Rectangle<int> r, bool mainLane);
    void paintOverlays (juce::Graphics& g, int deck, juce::Rectangle<int> lane, juce::Rectangle<int> whole, bool withFlags);
    juce::Rectangle<int> deckBounds (int deck) const;
    juce::Rectangle<int> dividerBounds (int deck) const;
    int dividerAt (juce::Point<int> p) const;

    AudioEngine& engine;
    std::array<DeckView, 2> decks;
    juce::ThreadPool stemWavePool { 2 };

    double visibleSeconds = 10.0;
    int maxFps = 60;
    int mainMode = 0;
    WaveformData::Style originalStyle = WaveformData::Style::threeBand;
    std::array<float, 2> laneSplit { 0.5f, 0.5f };
    double lastPaintTime = 0.0;
    bool needsRedraw = true;

    int dragDeck = -1, dragDivider = -1;
    double dragStartPos = 0.0;

    juce::VBlankAttachment vblank;
};

} // namespace orbit
