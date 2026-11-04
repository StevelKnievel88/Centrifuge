#pragma once
#include <juce_core/juce_core.h>

namespace orbit
{

/** Reads the usual text tags from an audio file without decoding it:
    ID3v2 (MP3, and the ID3 chunk in WAV / AIFF), ID3v1, FLAC Vorbis comments, MP4 / M4A items and WAV INFO.
    Anything missing is left empty; the title falls back to the file name ("Artist - Title" is split). */
struct AudioTags
{
    juce::String title, artist, album, genre, comment, label, key;
    double bpm = 0.0;

    static AudioTags read (const juce::File& file);
};

} // namespace orbit
