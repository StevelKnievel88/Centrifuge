#pragma once
#include "CentrifugeLibrary.h"
#include "../Audio/AudioFileLoader.h"
#include "../Audio/TrackAnalyser.h"

namespace orbit
{

/** Adds audio files and folders that aren't in rekordbox to Centrifuge's library.
    Each new track is decoded and analysed (tempo, beat grid, key) and its tags are read. A folder becomes a
    playlist (with sub-folders as playlist folders), so it exports to rekordbox and the XDJ-XZ like any other. */
class FileImporter
{
public:
    struct Options
    {
        double minBpm = 88.0, maxBpm = 175.0;
        double mp3OffsetMs = 0.0;      // the app's MP3 grid correction (grids are stored in rekordbox's time)
        int threads = 0;               // 0 = automatic
        bool subfolders = true;        // folders: include their sub-folders
    };

    struct Result
    {
        int added = 0, alreadyThere = 0, failed = 0, playlists = 0;
        juce::StringArray errors;
        bool cancelled = false;
    };

    /** progress (0..1, current file name) returns false to stop. Call from a background thread. */
    static Result run (CentrifugeLibrary& library, AudioFileLoader& loader, const juce::Array<juce::File>& items,
                       const Options& options, std::function<bool (float, const juce::String&)> progress);

    static bool isAudioFile (const juce::File& f);

    /** Analyses one file into library form (used by run and the command-line test). */
    static bool analyseFile (AudioFileLoader& loader, const juce::File& f, const Options& options,
                             TrackInfo& info, TrackVersion& version, juce::String& error, const std::atomic<bool>* cancel = nullptr);
};

} // namespace orbit
