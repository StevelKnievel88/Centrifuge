#pragma once
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace orbit
{

struct CuePoint
{
    enum class Type { memory, hot };
    Type type = Type::memory;
    int hotIndex = -1;          // 0 = A ... 7 = H
    double startSec = 0.0;
    double endSec = -1.0;       // > startSec when this is a loop
    juce::Colour colour;
    juce::String comment;

    bool isLoop() const noexcept { return endSec > startSec; }
};

struct Beat
{
    double timeSec = 0.0;
    float bpm = 0.0f;
    int beatInBar = 1;          // 1..4
};

/** rekordbox's eight track colours (ID 1-8). */
inline juce::Colour rekordboxTrackColour (int id)
{
    switch (id)
    {
        case 1:  return juce::Colour (0xfff870d0); // pink
        case 2:  return juce::Colour (0xffe8102a); // red
        case 3:  return juce::Colour (0xffff8c00); // orange
        case 4:  return juce::Colour (0xffffd500); // yellow
        case 5:  return juce::Colour (0xff16c00d); // green
        case 6:  return juce::Colour (0xff00d2e6); // aqua
        case 7:  return juce::Colour (0xff1e5cff); // blue
        case 8:  return juce::Colour (0xff9a3cff); // purple
        default: return juce::Colours::transparentBlack;
    }
}

struct TrackInfo
{
    juce::String id;
    juce::String title, artist, album, genre, key, label, comment, dateAdded;
    double bpm = 0.0;
    double durationSec = 0.0;
    int rating = 0;
    int sampleRate = 0;
    int bitrate = 0;
    juce::Colour colour;

    juce::File file;            // the audio file
    juce::File anlzBase;        // .../ANLZ0000.DAT (siblings .EXT / .2EX are found automatically)

    std::vector<CuePoint> dbCues; // cues stored in the library database (PC collection)

    juce::String displayArtistTitle() const
    {
        return artist.isEmpty() ? title : artist + " - " + title;
    }
};

struct PlaylistNode
{
    juce::String id;
    juce::String name;
    bool isFolder = false;
    bool isSmart = false;
    std::vector<PlaylistNode> children;
    std::vector<int> trackIndices;   // indices into LibrarySource::tracks
};

struct LibrarySource
{
    enum class Kind { pcCollection, usbExport, folder };
    Kind kind = Kind::pcCollection;
    juce::String name;               // e.g. "rekordbox" or "USB (E:)"
    juce::File root;                 // master.db location or USB root
    std::vector<TrackInfo> tracks;
    PlaylistNode playlists;          // root folder
    juce::String error;              // non-empty if loading failed
};

} // namespace orbit
