#pragma once
#include "../Audio/WaveformData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace orbit
{

/** Renders waveform columns straight into pixels (software image), rekordbox style. */
juce::Image renderWaveformStrip (const WaveformData& w, double firstEntry, double entriesPerPixel,
                                 int width, int height, bool overview);

/** Renders one stem's waveform in a single colour (columns beyond `validEntries` are left empty). */
juce::Image renderStemStrip (const StemWaveform& w, int stem, juce::Colour colour, int validEntries,
                             double firstEntry, double entriesPerPixel, int width, int height);

/** Caches pre-rendered tiles of a scrolling waveform so each frame is just a couple of image blits. */
class WaveformTileCache
{
public:
    static constexpr int tileWidth = 512;
    using Renderer = std::function<juce::Image (double firstEntry, double entriesPerPixel, int width, int height)>;

    /** Returns true if the cache had to be cleared (new waveform, zoom, size or style). */
    bool configure (const void* key, int styleKey, double entriesPerSecond, double lengthSeconds,
                    double pixelsPerSecond, int height, float pixelScale, Renderer renderer);

    /** For data that grows (stems being separated): tiles drawn before their data existed get redrawn. */
    void setValidEntries (int n) { validEntries = n; }

    /** Draws the waveform so that time `centreTime` lands at x = `playheadX` within `area`. */
    void draw (juce::Graphics& g, juce::Rectangle<int> area, double centreTime, float playheadX, float opacity = 1.0f);

private:
    struct Tile { juce::Image image; int validAtRender = 0; juce::uint32 lastUsed = 0; };
    const juce::Image& tile (int index);

    const void* key = nullptr;
    int style = -1;
    double eps = 150.0, length = 0.0, pps = 0.0;
    int h = 0;
    float scale = 1.0f;
    int validEntries = std::numeric_limits<int>::max();
    Renderer render;
    std::map<int, Tile> tiles;
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
