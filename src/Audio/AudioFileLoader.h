#pragma once
#include "TrackAudio.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

namespace orbit
{

/** Decodes a whole audio file into memory. Thread-safe: call from any background thread. */
class AudioFileLoader
{
public:
    AudioFileLoader();

    TrackAudio::Ptr load (const juce::File& file, juce::String& error, const std::atomic<bool>* cancel = nullptr);

    /** Also used to write/read stem cache files. */
    juce::AudioFormatManager& getFormatManager() { return formats; }

private:
    juce::AudioFormatManager formats;
    juce::CriticalSection lock;
};

} // namespace orbit
