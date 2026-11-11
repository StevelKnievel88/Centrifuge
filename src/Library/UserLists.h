#pragma once
#include "TrackInfo.h"
#include <map>
#include <set>

namespace orbit
{

/** What lives in Centrifuge's library besides the tracks (user-lists.json next to the settings):
      - your own playlists and folders (the PLAYLISTS branch), including dynamic playlists that fill themselves from rules
      - the TO PLAY list
      - PLAY HISTORY: each session (from opening Centrifuge) and each SET LIST
      - tags: your categories, the tags in each, and which tracks have them
    Tracks are kept by library id. Message thread only. */
class UserLists
{
public:
    struct Rules
    {
        double bpmMin = 0.0, bpmMax = 0.0;       // 0 = no limit
        juce::String keys;                       // comma-separated (e.g. "8A, 9A, Am"); empty = any
        int ratingMin = 0;
        juce::String genre;                      // contains (empty = any)
        int colour = 0;                          // rekordbox colour 1..8 (0 = any)
        int stems = 0;                           // 0 any, 1 with stems, 2 without
        juce::String tag;                        // "Category/Tag" (empty = any)
        juce::String text;                       // title / artist contains
        bool any() const { return bpmMin > 0 || bpmMax > 0 || keys.isNotEmpty() || ratingMin > 0 || genre.isNotEmpty() || colour > 0 || stems > 0 || tag.isNotEmpty() || text.isNotEmpty(); }
    };

    enum Kind { folder = 0, playlist = 1, dynamic = 2 };
    struct List
    {
        juce::int64 id = 0, parent = 0;
        juce::String name;
        int kind = playlist;
        int seq = 0;
        std::vector<juce::int64> tracks;
        Rules rules;
    };

    struct Played { juce::int64 track = 0; juce::String time; };
    struct History { juce::String name; std::vector<Played> tracks; };

    struct TagCategory { juce::String name; juce::StringArray tags; };

    static UserLists& get() { static UserLists u; return u; }

    // Playlists ----------------------------------------------------------------------
    const std::vector<List>& lists() const { return userLists; }
    const List* find (juce::int64 id) const;
    juce::int64 add (juce::int64 parent, const juce::String& name, int kind);
    void rename (juce::int64 id, const juce::String& name);
    void remove (juce::int64 id);                 // and everything inside a folder
    juce::int64 duplicate (juce::int64 id);
    void addTracks (juce::int64 id, const std::vector<juce::int64>& tracks);
    void removeTracks (juce::int64 id, const std::vector<juce::int64>& tracks);
    void setRules (juce::int64 id, const Rules& r);

    // To play ---------------------------------------------------------------------------
    const std::vector<juce::int64>& toPlay() const { return toPlayList; }
    /** Adds the track, or takes it out if it's already there (like the XDJ-XZ's TAG TRACK / REMOVE). Returns true if added. */
    bool toggleToPlay (juce::int64 track);
    void removeFromToPlay (juce::int64 track);

    // History ---------------------------------------------------------------------------
    /** This session (since Centrifuge was opened), the earlier ones, and the set lists. */
    History& session() { return current; }
    const std::vector<History>& sessions() const { return pastSessions; }
    const std::vector<History>& setLists() const { return pastSetLists; }
    bool setListRunning() const { return setListOn; }
    void startSetList();
    void stopSetList();
    const History& runningSetList() const { return setList; }
    /** A track counted as played (session, and the set list when one is running). */
    void notePlayed (juce::int64 track);
    void deleteHistory (bool setLists, int index);

    // Tags ------------------------------------------------------------------------------
    const std::vector<TagCategory>& tagCategories() const { return categories; }
    void setTagCategories (std::vector<TagCategory> c);
    juce::StringArray tagsOf (juce::int64 track) const;   // "Category/Tag"
    void setTag (juce::int64 track, const juce::String& tag, bool on);

    /** Whether a track (as shown in the browser) matches a dynamic playlist's rules. */
    bool matches (const Rules& r, const TrackInfo& t, int stemQuality) const;

    int version() const { return changes; }
    void save();

private:
    UserLists();
    void load();
    static juce::File file();

    std::vector<List> userLists;
    std::vector<juce::int64> toPlayList;
    History current, setList;
    bool setListOn = false;
    std::vector<History> pastSessions, pastSetLists;
    std::vector<TagCategory> categories;
    std::map<juce::int64, juce::StringArray> trackTags;
    int changes = 0;
};

} // namespace orbit
