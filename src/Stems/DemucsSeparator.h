#pragma once
#include "../Audio/TrackAudio.h"
#include <functional>
#include <memory>

namespace orbit
{

/** Runs HTDemucs (4 stems: drums, bass, other, vocals) through ONNX Runtime.
    Uses the GPU via DirectML when available, otherwise the CPU. */
class DemucsSeparator
{
public:
    DemucsSeparator();
    ~DemucsSeparator();

    struct Options
    {
        juce::File modelFile;
        bool useGpu = true;
        int cpuThreads = 0;          // 0 = automatic (leaves cores free for audio)
    };

    static bool isAvailable();       // false when built without ONNX Runtime

    bool load (const Options& options, juce::String& error);
    bool isLoaded() const;
    juce::String getBackendDescription() const;

    /** Separates `audio` into `out` (same sample rate and length as the input).
        Stems are written progressively: out.validFrames advances as each part is finished.
        `progress` gets 0..1 and returns false to cancel. */
    bool separate (const TrackAudio& audio, StemAudio& out,
                   const std::function<bool (double)>& progress, juce::String& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace orbit
