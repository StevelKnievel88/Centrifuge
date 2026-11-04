#pragma once
#include "TrackLoader.h"
#include "../Library/CentrifugeLibrary.h"

namespace orbit
{

/** Connects what you edit on a deck (hot cues, locators, skips, beat grid, cue names) to the library.
    Each deck has two edit buttons: R (the rekordbox copy) and C (the Centrifuge version); edits go to
    whichever are lit. The deck plays the Centrifuge version while C is lit, otherwise the rekordbox copy.
    Message thread only. */
class TrackEditor
{
public:
    TrackEditor (AudioEngine& engine, CentrifugeLibrary& library);

    /** Which version a deck should play for a track about to be loaded (empty when it isn't in the library). */
    std::optional<TrackVersion> versionToLoad (int deck, const TrackInfo& info) const;

    void trackLoaded (int deck, LoadedTrack::Ptr track);

    /** Saves edits made on the decks (call from a timer). */
    void poll();

    bool editsRekordbox (int deck) const { return sessions[(size_t) deck].r; }
    bool editsCentrifuge (int deck) const { return sessions[(size_t) deck].c; }
    void setEditTargets (int deck, bool rekordbox, bool centrifuge);
    bool versionsDiffer (int deck) const;
    bool hasTrack (int deck) const { return sessions[(size_t) deck].track != nullptr; }

    /** Beat grid edits: shift.back10, shift.back1, shift.fwd1, shift.fwd10, anchor, downbeat, bpm.up, bpm.down,
        bpm.upfine, bpm.downfine, halve, double, tap, undo, reset, or "bpm=128.00". */
    void gridAction (int deck, const juce::String& action);
    double currentBpm (int deck) const;

    void renameCue (int deck, bool hotCue, int index, const juce::String& name);
    juce::String cueName (int deck, bool hotCue, int index) const;

    std::function<void (const juce::String&)> onMessage;
    std::function<void (int deck, bool r, bool c)> onTargetsChanged;

private:
    struct Snap
    {
        std::array<std::optional<CuePoint>, 8> hot;
        std::vector<CuePoint> locs;
        std::vector<std::pair<double, double>> skips;
    };

    struct Session
    {
        int serial = -1, edits = 0, dataVersion = -1;
        LoadedTrack::Ptr track;
        juce::int64 libId = 0;
        TrackVersion rb, cf;
        bool r = true, c = true;
        Snap last;
        std::vector<std::pair<std::vector<Beat>, std::vector<Beat>>> undo;
        std::vector<double> taps;
    };

    Snap snapFromDeck (int deck, double shift) const;
    static Snap snapOf (const TrackVersion& v);
    static void applyFull (TrackVersion& target, const Snap& now);
    static void applyDiff (TrackVersion& target, const Snap& before, const Snap& now);
    void ensureInLibrary (Session& s);
    void save (Session& s, bool rb, bool cf);
    void swapDeck (int deck, const TrackVersion& v);
    const TrackVersion& playing (const Session& s) const { return s.c ? s.cf : s.rb; }

    AudioEngine& engine;
    CentrifugeLibrary& library;
    std::array<Session, 2> sessions;
};

} // namespace orbit
