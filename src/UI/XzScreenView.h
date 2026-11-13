#pragma once
#include "ScrollingWaveforms.h"
#include "../Control/ActionRouter.h"

namespace orbit
{

/** What laptop mode shows on the XDJ-XZ's 800 x 480 screen: both decks' scrolling waveforms across the top,
    and under them an info panel per deck (title, artist, times, BPM, tempo, key, overview) - laid out like the
    XZ's own screen. Never shown on the computer; it is painted into a picture for XzScreenLink. */
class XzScreenView : public juce::Component
{
public:
    XzScreenView (AudioEngine& engine, ActionRouter& router);

    /** Same lanes, colours and zoom as the play page's waveforms. */
    ScrollingWaveforms& getWaves() { return waves; }
    void setOverviewStyle (int style, bool half, juce::Colour single);

    /** Advances the waveforms and paints the screen. */
    juce::Image render();

    /** A touch on the XZ screen: tapping a deck's overview jumps there. */
    void touch (bool down, int x, int y);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void paintInfo (juce::Graphics& g, int deck, juce::Rectangle<float> r);

    AudioEngine& engine;
    ActionRouter& router;
    ScrollingWaveforms waves;
    juce::Rectangle<int> infoArea;

    struct Overview { juce::Image image; int serial = -1, version = -1, fold = -1, style = -1; };
    std::array<Overview, 2> overviews;
    std::array<juce::Rectangle<float>, 2> overviewRects;
    int overviewStyle = 1;
    bool overviewHalf = false;
    juce::Colour singleColour;
};

} // namespace orbit
