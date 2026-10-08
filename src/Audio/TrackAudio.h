#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace orbit
{

/** A fully decoded track held in memory (planar stereo float). Shared between the loader,
    the audio thread and the UI; freed on the message thread by ReleasePool. */
struct TrackAudio : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<TrackAudio>;

    double sampleRate = 44100.0;
    int64_t numFrames = 0;
    std::vector<float> left, right;

    double lengthSeconds() const noexcept { return sampleRate > 0 ? (double) numFrames / sampleRate : 0.0; }
};

/** Separated stems for a track, aligned sample-for-sample with its TrackAudio.
    Order follows the model: drums, bass, other (melody/instruments), vocals. Stored as 16-bit to halve memory. */
struct StemAudio : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<StemAudio>;
    static constexpr int numStems = 4;

    double sampleRate = 44100.0;
    int64_t numFrames = 0;
    std::array<std::vector<int16_t>, numStems * 2> channels; // stem s: [2s] left, [2s+1] right

    /** Frames that are finished (separation fills stems progressively while the track plays). */
    std::atomic<int64_t> validFrames { 0 };
    bool isComplete() const noexcept { return validFrames.load() >= numFrames; }

    // 16-bit with 6 dB of headroom: separated stems can individually exceed full scale.
    static constexpr float toFloat = 2.0f / 32767.0f;
    static inline int16_t fromFloat (float v) noexcept
    {
        const float x = v * (32767.0f / 2.0f);
        return (int16_t) (x >= 32767.0f ? 32767 : x <= -32767.0f ? -32767 : (int) std::lrint (x));
    }
};

inline const char* stemName (int index)
{
    static const char* names[] = { "Drums", "Bass", "Inst", "Vocal" };
    return (index >= 0 && index < 4) ? names[index] : "";
}

/** Keeps references to shared audio objects so the audio thread never frees memory.
    Call collect() periodically on the message thread. */
class ReleasePool
{
public:
    template <typename T>
    void add (const juce::ReferenceCountedObjectPtr<T>& p)
    {
        if (p == nullptr) return;
        const juce::ScopedLock sl (lock);
        objects.add (p.get());
    }

    void collect()
    {
        const juce::ScopedLock sl (lock);
        for (int i = objects.size(); --i >= 0;)
            if (objects.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
                objects.remove (i);
    }

private:
    juce::CriticalSection lock;
    juce::ReferenceCountedArray<juce::ReferenceCountedObject> objects;
};

} // namespace orbit
