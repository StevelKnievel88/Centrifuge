#pragma once
#include "TrackInfo.h"

namespace orbit
{

/** Raw waveform data found in rekordbox analysis files. */
struct AnlzWaveforms
{
    // Detail ("scrolling") waveforms, 150 entries per second of audio.
    std::vector<uint8_t> detail3Band;   // PWV7, 3 bytes per entry: mid, high, low
    std::vector<uint8_t> detailRgb;     // PWV5, 2 bytes per entry (big endian bit-field)
    std::vector<uint8_t> detailMono;    // PWV3, 1 byte per entry (5 bit height, 3 bit whiteness)

    int numDetailEntries() const noexcept
    {
        if (! detail3Band.empty()) return (int) detail3Band.size() / 3;
        if (! detailRgb.empty())   return (int) detailRgb.size() / 2;
        return (int) detailMono.size();
    }
};

/** Everything OrbitDJ uses from a track's ANLZ0000.DAT / .EXT / .2EX files. */
struct AnlzData
{
    bool foundAny = false;
    std::vector<Beat> beats;
    std::vector<CuePoint> cues;
    AnlzWaveforms waveforms;
    juce::String pathTag;       // PPTH: the audio file path rekordbox analysed
};

/** Reads the analysis files that sit next to the given .DAT file (any of the three may be missing). */
AnlzData loadAnlz (const juce::File& datFile);

/** Parses a single ANLZ file into `out` (merging with what's already there). Returns false if not an ANLZ file. */
bool parseAnlzFile (const juce::MemoryBlock& data, AnlzData& out);

} // namespace orbit
