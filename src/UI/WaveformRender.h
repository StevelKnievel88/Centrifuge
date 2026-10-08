#pragma once
#include "../Audio/WaveformData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace orbit
{

/** Renders waveform columns straight into pixels (software image), rekordbox style. */
juce::Image renderWaveformStrip (const WaveformData& w, double firstEntry, double entriesPerPixel,
                                 int width, int height, bool overview);

/** Caches pre-rendered tiles of a scrolling waveform so each frame is just a couple of image blits. */
class WaveformTileCache
{
public:
    static constexpr int tileWidth = 512;

    /** Returns true if the cache had to be cleared (new waveform, zoom or size). */
    bool configure (const WaveformData* w, double pixelsPerSecond, int height, float pixelScale);

    /** Draws the waveform so that time `centreTime` lands at x = `playheadX` within `area`. */
    void draw (juce::Graphics& g, juce::Rectangle<int> area, double centreTime, float playheadX);

private:
    const juce::Image& tile (int index);

    WaveformData::Ptr wave;
    const WaveformData* key = nullptr;
    double pps = 0.0;
    int h = 0;
    float scale = 1.0f;
    std::map<int, juce::Image> tiles;
    std::map<int, juce::uint32> lastUsed;
    juce::uint32 useCounter = 0;
};

/** Smooths the deck position between audio callbacks so scrolling looks fluid. */
struct PositionSmoother
{
    double shown = 0.0, lastRaw = -1.0, lastTime = 0.0;

    double update (double raw, bool playing, double rate, double nowSec)
    {
        const double dt = lastTime > 0.0 ? juce::jlimit (0.0, 0.1, nowSec - lastTime) : 0.0;
        lastTime = nowSec;
        if (! playing || lastRaw < 0.0)
        {
            shown = raw;
        }
        else
        {
            double predicted = shown + dt * rate;
            const double err = raw - predicted;
            if (std::abs (err) > 0.2) predicted = raw;  // a jump
            else predicted += err * 0.12;               // gently follow the audio clock
            shown = predicted;
        }
        lastRaw = raw;
        return shown;
    }
};

} // namespace orbit
