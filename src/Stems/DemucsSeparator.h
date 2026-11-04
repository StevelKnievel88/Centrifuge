#pragma once
#include "../Audio/TrackAudio.h"
#include <functional>
#include <memory>

namespace orbit
{

/** How hard to work at separating stems (slower = cleaner). */
enum class StemQuality
{
    fast = 0,     // HTDemucs, one pass
    better = 1,   // HTDemucs, two passes on offset windows, averaged (~2x the time)
    high = 2,     // HTDemucs fine-tuned: a specialist model each for drums, bass and vocals (~3x)
    best = 3      // Mel-Band RoFormer vocals, then the fine-tuned drums and bass models on what's left (slowest)
};

inline juce::String stemQualityName (StemQuality q)
{
    switch (q)
    {
        case StemQuality::fast:   return "Fast";
        case StemQuality::better: return "Better";
        case StemQuality::high:   return "High";
        case StemQuality::best:   return "Best";
    }
    return {};
}

/** Model files for each quality level (file names in the models folder). */
struct StemModels
{
    static constexpr const char* fast = "htdemucs.onnx";
    static constexpr const char* ftDrums = "htdemucs_ft_drums.onnx";
    static constexpr const char* ftBass = "htdemucs_ft_bass.onnx";
    static constexpr const char* ftVocals = "htdemucs_ft_vocals.onnx";
    static constexpr const char* roformer = "melband_roformer_vocals.onnx";

    /** Files a quality level needs. */
    static juce::StringArray required (StemQuality q)
    {
        switch (q)
        {
            case StemQuality::fast:
            case StemQuality::better: return { fast };
            case StemQuality::high:   return { ftDrums, ftBass, ftVocals };
            case StemQuality::best:   return { ftDrums, ftBass, roformer };
        }
        return { fast };
    }
};

/** Separates tracks into 4 stems (drums, bass, other, vocals) with ONNX Runtime models.
    Uses the GPU via DirectML when available, otherwise the CPU. */
class DemucsSeparator
{
public:
    DemucsSeparator();
    ~DemucsSeparator();

    struct Options
    {
        juce::File modelFile;                          // htdemucs.onnx
        std::function<juce::File (const juce::String& name)> findModel;   // locates the other models (may return a missing file)
        StemQuality quality = StemQuality::fast;
        bool useGpu = true;
        int cpuThreads = 0;          // 0 = automatic (leaves cores free for audio)
        bool allowBestOnCpu = false; // Best's RoFormer needs a lot of memory on the CPU (tests only)
    };

    static bool isAvailable();       // false when built without ONNX Runtime

    /** Loads what `options.quality` needs. If models for that level are missing, it settles for the best
        level it can do (see getQuality()). */
    bool load (const Options& options, juce::String& error);
    bool isLoaded() const;
    juce::String getBackendDescription() const;
    StemQuality getQuality() const;           // the level actually loaded

    /** Separates `audio` into `out` (same sample rate and length as the input).
        Stems are written progressively: out.validFrames advances as each part is finished.
        `progress` gets 0..1 and returns false to cancel. */
    bool separate (const TrackAudio& audio, StemAudio& out,
                   const std::function<bool (double)>& progress, juce::String& error);

private:
    struct Model;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace orbit
