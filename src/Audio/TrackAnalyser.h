#pragma once
#include "TrackAudio.h"
#include "../Library/TrackInfo.h"

namespace orbit
{

/** Centrifuge's own analysis for tracks that rekordbox hasn't analysed: tempo, a constant beat grid
    (with a guess at the bar start) and the musical key. Tuned for electronic music, which keeps a steady tempo. */
class TrackAnalyser
{
public:
    struct Options
    {
        double minBpm = 88.0, maxBpm = 175.0;   // the tempo is searched within this range (one octave)
        double hintBpm = 0.0;                    // from the file's tags, if any: used when it fits the audio
        bool wholeBpmWhenClose = true;           // 140.02 -> 140.00
        bool kickAlign = true;                   // line the grid up with the start of the kicks, to the sample
        double kickFrac = 0.15;                  // where on the kick's rise its "start" is (0 = first movement, 1 = its peak)
        double kickCutoffHz = 200.0;             // the low end the kick is found in (a kick starts higher and sweeps down)
        double kickSmoothSec = 0.005;            // energy smoothing
    };

    struct Result
    {
        double bpm = 0.0;
        std::vector<Beat> beats;
        juce::String key;         // rekordbox style: "Fm", "Ab", ...
        juce::String keyAlt;      // the second opinion (detectKeyAlt)
        double confidence = 0.0;  // 0..1, how well the grid lines up with the beats
    };

    static Result analyse (const TrackAudio& audio, const Options& options, const std::atomic<bool>* cancel = nullptr);

    /** Key only (chroma against key profiles). */
    static juce::String detectKey (const TrackAudio& audio);   // the first method (whole-mix chroma, Krumhansl profiles)
    /** Key v2: tuning corrected; root from the bass (the bass stem when given, weighted to bar downbeats and phrase
        starts / ends when beats are given), major / minor from the harmony (inst + vocal stems, or the mix). */
    static juce::String detectKeyV2 (const TrackAudio& audio, const StemAudio* stems, const std::vector<Beat>* beats);

    /** A second opinion, done the way KeyFinder (as used by Mixxx) does it: a chromagram over six octaves compared
        with Sha'ath's key profiles by cosine similarity. (A re-implementation of the method, not the library.) */
    static juce::String detectKeyAlt (const TrackAudio& audio);

    /** Where the kicks start relative to a set of beat times: each beat's kick is found in the low end (below ~120 Hz,
        filtered with no time shift) within +-45 ms, and its start is where its energy has risen kickFrac of the way
        from the level before it to its peak. Beats without a clear kick (breakdowns) are left out; the answer is the
        weighted median, so a few odd ones don't move it. */
    struct KickAlign { double offsetSec = 0.0; int used = 0, total = 0; double spreadMs = 0.0, strength = 0.0; bool ok = false; };
    static KickAlign kickOffset (const TrackAudio& audio, const std::vector<double>& beatTimes, double kickFrac,
                                 double cutoffHz = 200.0, double smoothSec = 0.005);

    /** Parts exposed for tests. */
    static double estimateTempo (const std::vector<float>& envelope, double framesPerSecond, double minBpm, double maxBpm);
};

} // namespace orbit
