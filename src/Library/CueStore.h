#pragma once
#include "TrackInfo.h"
#include <array>
#include <map>
#include <optional>

namespace orbit
{

/** Centrifuge's own cue data: hot cues and locators you've edited, plus skips. Saved in
    %APPDATA%\Centrifuge\cues.json - rekordbox's library and your audio files are never changed.
    Thread-safe. */
class CueStore
{
public:
    struct Entry
    {
        std::optional<std::array<std::optional<CuePoint>, 8>> hotCues;   // set once you've edited them
        std::optional<std::vector<CuePoint>> locators;                    // set once you've edited them
        std::vector<std::pair<double, double>> skips;
    };

    explicit CueStore (juce::File file);

    /** The key a track is stored under (the same track on USB and in the PC collection shares it). */
    static juce::String keyFor (const TrackInfo& info, double lengthSec);

    std::optional<Entry> get (const juce::String& key) const;
    void put (const juce::String& key, const Entry& e);

    /** Writes pending changes to disk (call from a timer). */
    void flush();

private:
    void load();
    juce::File file;
    mutable juce::CriticalSection lock;
    std::map<juce::String, Entry> entries;
    bool dirty = false;
};

} // namespace orbit
