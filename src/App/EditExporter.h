#pragma once
#include "../Audio/Deck.h"

namespace orbit
{

/** Renders a track as it plays with its edits (skips, rearranged / repeated phrases, loop markers, stem markers)
    to WAV files: the mix, and optionally each stem on its own. Runs a private deck offline (no audio device). */
struct EditExporter
{
    /** `base` is the file name without extension: writes "<base>.wav" and, with stems, "<base> (Drums).wav" etc.
        `progress` gets 0..1 and returns false to stop. Call from a background thread. */
    static bool render (LoadedTrack::Ptr track, StemAudio::Ptr stems, const juce::File& base, bool withStems,
                        std::function<bool (float)> progress, juce::String& error);
};

} // namespace orbit
