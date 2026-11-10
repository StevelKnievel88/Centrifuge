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

    /** Parts exposed for tests. */
    static double estimateTempo (const std::vector<float>& envelope, double framesPerSecond, double minBpm, double maxBpm);
};

} // namespace orbit
