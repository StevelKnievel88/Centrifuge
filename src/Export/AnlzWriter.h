#pragma once
#include "../Library/TrackVersion.h"
#include "../Audio/WaveformData.h"

namespace orbit
{

/** Writes a track's rekordbox analysis files (ANLZ0000.DAT / .EXT / .2EX) for a USB export.
    When the track has rekordbox's own analysis files, their waveforms and other tags are copied and only the
    path, beat grid and cues are replaced. Otherwise everything is generated (waveforms from our own analysis). */
class AnlzWriter
{
public:
    struct Input
    {
        TrackVersion version;              // grid and cues (already limited to what rekordbox can hold)
        juce::String usbPath;              // audio file path on the stick, e.g. /Contents/Artist/Album/x.mp3
        juce::File originalDat;            // rekordbox's ANLZ0000.DAT for this track, if any
        WaveformData::Ptr generatedWave;   // 3-band waveform from our analysis (used when there is no original)
    };

    /** Writes datFile (and .EXT / .2EX next to it). */
    static bool write (const juce::File& datFile, const Input& in, juce::String& error);

    // Individual tags (big-endian), exposed for tests.
    static juce::MemoryBlock pathTag (const juce::String& path);
    static juce::MemoryBlock beatGridTag (const std::vector<Beat>& beats);
    static juce::MemoryBlock cueListTag (const TrackVersion& v, bool hotCues);          // PCOB
    static juce::MemoryBlock extendedCueListTag (const TrackVersion& v, bool hotCues);  // PCO2
};

} // namespace orbit
