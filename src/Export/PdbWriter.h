#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace orbit
{

/** Writes a rekordbox USB database (PIONEER/rekordbox/export.pdb, Pioneer's "DeviceSQL" format) laid out
    the way rekordbox 6 writes it. Format: Deep Symmetry's crate-digger analysis and rekordcrate. */
class PdbWriter
{
public:
    struct Track
    {
        juce::uint32 id = 0;
        juce::String title, artist, album, genre, key, label, comment, dateAdded;
        juce::String filePath;      // on the stick, e.g. /Contents/Artist/Album/track.mp3
        juce::String analyzePath;   // e.g. /PIONEER/USBANLZ/P016/0000875E/ANLZ0000.DAT
        juce::String analyzeDate;
        juce::uint32 sampleRate = 44100, fileSize = 0, bitrate = 0, tempo = 0;   // tempo in 1/100 BPM
        juce::uint32 duration = 0, year = 0, trackNumber = 0, sampleDepth = 16;
        int colour = 0;             // rekordbox colour id 0..8
        int rating = 0;             // 0..5
        int fileType = 1;           // 1 mp3, 4 m4a, 5 flac, 11 wav, 12 aiff
    };

    struct Playlist
    {
        juce::uint32 id = 0, parent = 0, sortOrder = 0;
        bool folder = false;
        juce::String name;
        std::vector<juce::uint32> trackIds;
    };

    static bool write (const juce::File& file, const std::vector<Track>& tracks, const std::vector<Playlist>& playlists,
                       juce::String& error);

    static int fileTypeFor (const juce::File& f);

    /** DeviceSQL string encoding (exposed for tests). */
    static std::vector<uint8_t> encodeString (const juce::String& s);
};

} // namespace orbit
