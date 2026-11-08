#pragma once
#include <juce_core/juce_core.h>

namespace orbit
{

/** Writes track info into an audio file's own tags: MP3 (ID3v2, other frames kept) and FLAC (Vorbis comments, other
    comments kept). The file is rewritten through a temporary copy that replaces it only when everything went well.
    Empty fields are left as they are in the file. */
struct TagWriter
{
    struct Fields { juce::String title, artist, album, genre, label, comment, key; };
    static bool canWrite (const juce::File& f);
    static bool write (const juce::File& file, const Fields& fields, juce::String& error);
};

} // namespace orbit
