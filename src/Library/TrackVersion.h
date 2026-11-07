#pragma once
#include "TrackInfo.h"
#include <array>
#include <optional>

namespace orbit
{

/** One library's version of a track's performance data: beat grid, hot cues, memory cues / locators
    and skips. The Centrifuge library keeps two of these per track: the rekordbox copy and Centrifuge's own.
    Times are in seconds of the source audio, as rekordbox stores them (no MP3 offset applied). */
struct TrackVersion
{
    std::vector<Beat> beats;
    std::array<std::optional<CuePoint>, 8> hot;
    std::vector<CuePoint> memory;                       // rekordbox memory cues = Centrifuge locators
    std::vector<std::pair<double, double>> skips;       // Centrifuge only
    // Phrases you've edited (Centrifuge only). Unset = show rekordbox's own phrase analysis, if the track has one.
    std::optional<std::vector<Phrase>> phrases;

    // Non-destructive edits (Centrifuge only)
    std::vector<StemLocator> stemLocs;
    std::vector<NoteLocator> notes;
    std::vector<double> arrangement;                    // section rearrange: phrase start times in play order
    std::vector<EditVersion> editVersions;              // named sets of edits
    juce::String activeEdit;                            // name of the edit version last chosen ("" = none)

    EditVersion snapshotEdit (const juce::String& name) const;
    /** Makes this version's edits those of `e` (phrase repeats need `phrases` to be set). */
    void applyEdit (const EditVersion& e);
    /** No edits: no skips, stem locators, rearrangement or repeats; loop locators back to ordinary loops. */
    void clearEdits();
    bool editsMatch (const EditVersion& e) const;

    // What each library can hold.
    static constexpr int centrifugeMaxLocators = 32;
    static constexpr int rekordboxMaxMemoryCues = 10;

    juce::var toVar() const;
    static TrackVersion fromVar (const juce::var& v);
    juce::String toJson() const { return juce::JSON::toString (toVar(), true); }
    static TrackVersion fromJson (const juce::String& s) { return fromVar (juce::JSON::parse (s)); }

    /** A cut-down copy that rekordbox can hold: colours snapped to rekordbox's palettes, no skips,
        at most rekordboxMaxMemoryCues memory cues. */
    TrackVersion limitedForRekordbox() const;

    bool sameAs (const TrackVersion& o) const { return toJson() == o.toJson(); }
    bool sameGrid (const TrackVersion& o) const;

    /** Builds a version from cue/grid data as rekordbox delivers it. */
    static TrackVersion fromRekordbox (const std::vector<Beat>& beats, const std::vector<CuePoint>& cues);

    /** A short stable hash (to notice when rekordbox's data for a track has changed since the import). */
    juce::String hash() const;
};

} // namespace orbit
