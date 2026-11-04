#pragma once
#include "TrackVersion.h"
#include "CueStore.h"
#include <map>
#include <memory>

struct sqlite3;

namespace orbit
{

/** Centrifuge's own library (%APPDATA%\Centrifuge\library.db). Every track holds two versions:
      - the rekordbox copy: grid and cues as imported from rekordbox, limited to what rekordbox can hold
      - the Centrifuge version: starts as a duplicate, can hold more (locators, skips)
    Importing only ever reads rekordbox's files. Thread-safe. */
class CentrifugeLibrary
{
public:
    struct Track
    {
        juce::int64 id = 0;
        juce::String key;
        TrackInfo info;
        TrackVersion rb, cf;
        bool rbEdited = false, cfEdited = false;
        bool differ = false;                // the two versions are different
        juce::String importHash, source;
        std::vector<Beat> importedGrid;     // rekordbox's grid at import (for "reset grid")
    };

    struct Conflict
    {
        juce::int64 id = 0;
        juce::String title;
        TrackVersion rekordboxNow;
        juce::String newHash;
    };

    struct ImportResult
    {
        int added = 0, updated = 0, unchanged = 0;
        std::vector<Conflict> conflicts;
        juce::String error;
    };

    struct Playlist
    {
        juce::int64 id = 0, parent = 0;
        juce::String name, source;
        bool folder = false;
        int seq = 0;
        std::vector<juce::int64> tracks;
    };

    explicit CentrifugeLibrary (juce::File dbFile);
    ~CentrifugeLibrary();

    bool isOpen() const noexcept { return db != nullptr; }
    juce::String getError() const { return openError; }
    int size() const;

    /** Reads rekordbox's grid and cues for a track (ANLZ files, plus master.db cues for the PC collection). */
    static TrackVersion readRekordboxVersion (const TrackInfo& info);

    /** Imports a rekordbox source (PC collection or USB). `onlyTracks` limits it to some of the source's
        tracks (and skips playlists). Tracks you've edited in Centrifuge whose rekordbox data has changed
        since the last import come back as conflicts. `legacy` brings in cue edits from older versions. */
    ImportResult import (const LibrarySource& source, const std::vector<int>* onlyTracks, CueStore* legacy,
                         std::function<bool (float progress)> progress);

    /** Settles a conflict: take rekordbox's data (keeps your skips) or keep yours. */
    void resolve (const Conflict& c, bool takeRekordbox);

    std::optional<Track> find (const juce::String& key) const;
    std::optional<Track> get (juce::int64 id) const;
    std::vector<Track> allTracks() const;
    std::vector<Playlist> allPlaylists() const;

    /** Adds one track (used when a track that wasn't imported is edited). Returns its id. */
    juce::int64 addTrack (const TrackInfo& info, const TrackVersion& rekordboxVersion);

    /** Adds a track analysed by Centrifuge (not from rekordbox). Both versions start as `version`.
        If the track is already in the library, nothing changes. Returns its id and whether it was new. */
    std::pair<juce::int64, bool> addAnalysedTrack (const TrackInfo& info, const TrackVersion& version, const juce::String& source);

    /** Replaces the playlists that came from one source (e.g. an imported folder) with `root`'s children.
        Track indices in `root` refer to `ids`. */
    void setPlaylists (const juce::String& source, const PlaylistNode& root, const std::vector<juce::int64>& ids);

    /** Library key for a file (file name + length in whole seconds), as used for every track. */
    static juce::String keyForFile (const juce::File& f, double lengthSec);

    /** Saves one or both versions of a track (null = leave as it is). */
    void saveVersions (juce::int64 id, const TrackVersion* rb, const TrackVersion* cf);

    /** The library as a browser source: view 0 = rekordbox copy, 1 = Centrifuge version. */
    std::shared_ptr<LibrarySource> makeSource (int view) const;

    /** Increases whenever the library changes (so views know to refresh). */
    int getChangeCount() const noexcept { return changeCount.load(); }

    static juce::var infoToVar (const TrackInfo& t);
    static TrackInfo infoFromVar (const juce::var& v);

private:
    bool exec (const juce::String& sql);
    void loadAll();
    void writeTrack (const Track& t);
    void replacePlaylists (const juce::String& source, const PlaylistNode& root, const std::vector<juce::int64>& idsBySourceIndex,
                           const juce::String& folderName);

    sqlite3* db = nullptr;
    juce::String openError;
    mutable juce::CriticalSection lock;
    std::map<juce::int64, Track> tracks;
    std::map<juce::String, juce::int64> byKey;
    std::vector<Playlist> playlists;
    std::atomic<int> changeCount { 0 };
};

} // namespace orbit
