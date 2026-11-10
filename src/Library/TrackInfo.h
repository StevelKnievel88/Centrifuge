#pragma once
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <array>

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
    // Loops (Centrifuge only): 0 = an ordinary loop (called up, not engaged), 1 = active loop (engages as soon as
    // playback runs into it), 2 = cycle loop (engages, plays `cycles` times, then carries on).
    int loopMode = 0;
    int cycles = 4;

    bool isLoop() const noexcept { return endSec > startSec; }
};

/** A phrase (song section) shown as a coloured bar under the waveform: from rekordbox's phrase analysis,
    or your own (any name, any colour). Each runs until the next one starts; the last one until endSec
    (or the end of the track). */
struct Phrase
{
    double startSec = 0.0;
    double endSec = -1.0;
    juce::String name;
    juce::Colour colour;
    int repeats = 1;            // phrase repeat: plays this many times in a row (Centrifuge only)
};

/** Stem locator: from this point the stems change - each one switched on, off, or left as it is - either at
    once or faded over a number of beats. Centrifuge only; needs the track's stems. */
struct StemLocator
{
    double timeSec = 0.0;
    std::array<int, 4> target { -1, -1, -1, -1 };   // drums, bass, inst, vocals: 1 on, 0 off, -1 unchanged
    float fadeBeats = 0.0f;
    int mode = 0;                                    // how the change happens: 0 level fade, 1 low-pass sweep, 2 high-pass sweep
    double fadeSec = 0.0;                            // worked out from the beat grid when loaded
    juce::String name;
    // Each stem can have its own time and way (e.g. vocals out with a high-pass over 16 beats, drums in by level over 8).
    // -1 = use the locator's fadeBeats / mode above.
    std::array<float, 4> stemFade { -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<int, 4> stemMode { -1, -1, -1, -1 };
    std::array<double, 4> stemFadeSec { 0.0, 0.0, 0.0, 0.0 };   // worked out when loaded
    float fadeFor (int s) const { return stemFade[(size_t) s] >= 0.0f ? stemFade[(size_t) s] : fadeBeats; }
    int modeFor (int s) const { return stemMode[(size_t) s] >= 0 ? stemMode[(size_t) s] : mode; }
    double fadeSecFor (int s) const { return stemFade[(size_t) s] >= 0.0f ? stemFadeSec[(size_t) s] : fadeSec; }
};

/** Note locator: a reminder that pops up on the deck when playback reaches it. */
struct NoteLocator
{
    double timeSec = 0.0;
    juce::String text;
    juce::Colour colour { 0xffffd339 };
    double leadSec = 2.0;    // shows this long before its point
    double showSec = 8.0;    // ... and stays up this long after (0 = until the next note)
};

/** A section's copy (a duplicate in the play order) has its own repeat count, kept in its play-order entry as a few
    milliseconds after the phrase's start (4 ms per pass; within the 50 ms used to match entries to phrases).
    0 = not set (the copy plays once). */
inline int copyRepeatsOf (double arrangementTime, double phraseStart)
{
    const int r = (int) std::lround ((arrangementTime - phraseStart) / 0.004);
    return r >= 1 && r <= 8 ? r : 0;
}
inline double copyTimeWithRepeats (double phraseStart, int repeats) { return repeats >= 1 ? phraseStart + 0.004 * repeats : phraseStart; }

/** One stretch of the track in the order it plays (section rearrange / phrase repeat). */
struct PlaySegment
{
    double start = 0.0, end = 0.0;
};

/** A named set of edits - skips, loop modes, stem locators, phrase order and repeats - to pick when loading. */
struct EditVersion
{
    struct LoopMode { double startSec; int mode, cycles; };
    juce::String name;
    std::vector<std::pair<double, double>> skips;
    std::vector<StemLocator> stemLocs;
    std::vector<double> arrangement;                 // phrase start times in play order (empty = as the track goes)
    std::vector<std::pair<double, int>> repeats;     // phrase start -> times it plays (only the ones > 1)
    std::vector<LoopMode> loopModes;                 // loop locators by start: mode and cycles
};

/** Colour for a phrase name: the phrase preset's colour (Edit presets on PREP), else rekordbox's section colours,
    else a neutral colour. (PhrasePresets.cpp) */
juce::Colour defaultPhraseColour (const juce::String& name);

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
    juce::String keyAlt;   // after analysis: the second key opinion (not stored with the track)
    double bpm = 0.0;
    double durationSec = 0.0;
    int rating = 0;
    int sampleRate = 0;
    int bitrate = 0;
    juce::Colour colour;

    juce::File file;            // the audio file
    juce::File anlzBase;        // .../ANLZ0000.DAT (siblings .EXT / .2EX are found automatically)

    std::vector<CuePoint> dbCues; // cues stored in the library database (PC collection)

    // Centrifuge library
    juce::int64 libraryId = 0;  // non-zero for tracks in Centrifuge's library
    bool versionsDiffer = false;// the rekordbox copy and the Centrifuge version are different
    int numCues = 0;            // hot cues + memory cues / locators in the version shown
    int playCount = 0;          // times played on a deck (Centrifuge's library)
    bool hasSkips = false;
    juce::String libraryKey;    // Centrifuge's key for the track (file name + length)

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
    enum class Kind { pcCollection, usbExport, folder, centrifuge };
    Kind kind = Kind::pcCollection;
    juce::String name;               // e.g. "rekordbox" or "USB (E:)"
    juce::File root;                 // master.db location or USB root
    std::vector<TrackInfo> tracks;
    PlaylistNode playlists;          // root folder
    juce::String error;              // non-empty if loading failed
    int libraryView = 0;             // centrifuge sources: 0 = rekordbox copy, 1 = Centrifuge version
};

} // namespace orbit
