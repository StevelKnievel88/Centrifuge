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
    ~TrackEditor();

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
    static std::vector<float> lowOnsets (const TrackAudio& a, double& hop);   // hop in: wanted, out: actual
    double currentBpm (int deck) const;

    /** Phrases shown for the deck's track (deck time). Editing them saves your own phrase list (Centrifuge),
        starting from rekordbox's phrase analysis if you haven't edited them before. */
    const std::vector<Phrase>& phrases (int deck) const;
    void addPhrase (int deck, double atSec, const juce::String& name, juce::Colour colour);
    void setPhrase (int deck, int index, const Phrase& p);   // p in deck time
    void deletePhrase (int deck, int index);

    void setPhraseRepeats (int deck, int index, int repeats);

    /** Section rearrange: phrase starts (deck time) in the order they should play; empty = as the track goes. */
    const std::vector<double>& arrangement (int deck) const;
    void setArrangement (int deck, const std::vector<double>& phraseStartsInPlayOrder);

    /** Stem locators and note locators (deck time). */
    const std::vector<StemLocator>& stemLocators (int deck) const;
    void addStemLocator (int deck, const StemLocator& l);
    void setStemLocator (int deck, int index, const StemLocator& l);
    void deleteStemLocator (int deck, int index);
    const std::vector<NoteLocator>& notes (int deck) const;
    void addNote (int deck, const NoteLocator& n);
    void setNote (int deck, int index, const NoteLocator& n);
    void deleteNote (int deck, int index);

    /** Edit versions: named sets of skips, loop modes, stem locators, phrase order and repeats. */
    std::vector<juce::String> editVersionNames (int deck) const;
    juce::String activeEdit (int deck) const;        // "" when none chosen
    bool activeEditModified (int deck) const;        // edits changed since it was chosen / saved
    bool hasEdits (int deck) const;
    void saveEditVersion (int deck, const juce::String& name);   // a new one, or replaces one with that name
    void applyEditVersion (int deck, int index);
    void applyNoEdits (int deck);
    void renameEditVersion (int deck, int index, const juce::String& name);
    void deleteEditVersion (int deck, int index);

    /** Changes a library track's saved version (Centrifuge's; the rekordbox copy too while it's switched on) -
        and any deck that has the track loaded takes the change straight away. For the browser's Clear menu and Analyse. */
    void editLibraryTrack (juce::int64 libId, const std::function<void (TrackVersion&)>& fn);
    /** A deck that has this library track loaded (-1 = none). */
    int deckWithTrack (juce::int64 libId) const;

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
        // Edits reach the library database at most a few times a second (dragging a colour picker sends dozens)
        mutable bool pendR = false, pendC = false;
        mutable juce::uint32 pendSince = 0;
    };
    void flushSave (const Session& s) const;
    void flushAllSaves() const;

    Snap snapFromDeck (int deck, double shift) const;
    static Snap snapOf (const TrackVersion& v);
    static void applyFull (TrackVersion& target, const Snap& now);
    static void applyDiff (TrackVersion& target, const Snap& before, const Snap& now);
    void ensureInLibrary (Session& s);
    void save (Session& s, bool rb, bool cf);
    void swapDeck (int deck, const TrackVersion& v);
    void editPhrases (int deck, const std::function<void (std::vector<Phrase>&)>& fn);
    /** Changes the saved version(s) (times without the MP3 shift) and updates the deck. */
    void editVersion (int deck, const std::function<void (TrackVersion&)>& fn, bool seedPhrases = false);
public:
    /** Several phrases changed in one go (one save, one deck update). */
    void setPhrases (int deck, const std::vector<std::pair<int, Phrase>>& changes);
private:
    void syncOthers (int deck);   // the same track on another deck takes the edit straight away
    const TrackVersion& playing (const Session& s) const { return s.c ? s.cf : s.rb; }

    AudioEngine& engine;
    CentrifugeLibrary& library;
    std::array<Session, AudioEngine::totalDecks> sessions;   // decks 1, 2, PREP and the browser preview
};

} // namespace orbit
