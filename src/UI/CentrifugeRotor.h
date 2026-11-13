#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace orbit
{

/** The Centrifuge mark: a rotor seen from above with four sample tubes (one per stem) around a hub that
    holds a waveform. It spins while stems are being separated and the tubes fill with each stem's colour.
    Only animates while busy; completely idle otherwise. */
class CentrifugeRotor : public juce::Component
{
public:
    CentrifugeRotor();

    /** 0..1: how full the tubes are. */
    void setProgress (float p);
    /** Spins up when true, spins down to rest when false. */
    void setSpinning (bool shouldSpin);
    bool isAnimating() const noexcept { return speed > 0.001f || spinning; }

    void paint (juce::Graphics&) override;

    /** Draws the rotor. speed 0..1 adds motion blur; streaks 0..1 draws stem colour flying out to the tubes. */
    static void paintRotor (juce::Graphics& g, juce::Rectangle<float> area, float angle, float speed,
                            float fill, float streaks, float streakPhase);

private:
    void tick (double t);

    float progress = 0.0f, angle = 0.4f, speed = 0.0f, streakPhase = 0.0f;
    bool spinning = false;
    double lastTime = 0.0;
    juce::VBlankAttachment vblank;
};

/** Startup animation: still rotor -> spins up -> waveform flies out into the four stem tubes -> slows to a stop. */
class CentrifugeSplash : public juce::Component
{
public:
    CentrifugeSplash();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (auto f = onFinished) { onFinished = nullptr; f(); } }
    std::function<void()> onFinished;
    /** One frame of the animation at e seconds (3 s long; fades out from 2.6 s), the rotor at `angle`: for pictures
        that aren't on screen (the XDJ-XZ's). spinSpeedAt: how fast it spins then (0..1, radians = speed * dt * 16). */
    static void paintFrame (juce::Graphics& g, juce::Rectangle<float> bounds, float e, float angle);
    static float spinSpeedAt (float e);

private:
    void tick (double t);
    double start = -1.0, elapsed = 0.0;
    float angle = 0.4f;
    juce::VBlankAttachment vblank;
};

} // namespace orbit
