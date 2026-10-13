#pragma once
#include "WaveformRender.h"
#include "CentrifugeRotor.h"
#include "../Control/ActionRouter.h"

namespace orbit
{

/** Track info, overview waveform, hot cue pads, transport and stem controls for one deck. */
class DeckPanel : public juce::Component,
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
    int serial = -1;
    juce::Image overview;
    int overviewWidth = 0;
    juce::String loadingText;

    juce::Rectangle<float> rowInfo, rowStatus, rowOverview, rowPads, rowControls;
    juce::String heldAction;
    int dragStem = -1;
    juce::uint64 lastHash = 0;
    CentrifugeRotor rotor;
    juce::Rectangle<float> rotorArea;
};

} // namespace orbit
