#pragma once
#include "../Audio/AudioEngine.h"
#include "../Audio/AudioFileLoader.h"
#include "../Stems/StemManager.h"
#include "../Library/CueStore.h"
#include "../Library/TrackVersion.h"

namespace orbit
{

/** Loads a track onto a deck: decodes audio, reads rekordbox analysis (grid, cues, waveform)
    and asks the StemManager for stems. All heavy work happens off the message thread. */
class TrackLoader
{
public:
    TrackLoader (AudioEngine& engine, AudioFileLoader& loader, StemManager& stems);
    ~TrackLoader();

    struct Options
    {
        WaveformData::Style waveformStyle = WaveformData::Style::threeBand;
        double mp3OffsetMs = 0.0;
        bool preferDbCues = true;   // PC collection: cues from master.db rather than ANLZ
        CueStore* cueStore = nullptr;   // Centrifuge's own hot cue / locator edits and skips
    };
    void setOptions (const Options& o) { const juce::ScopedLock sl (lock); options = o; }

    /** `version`: the library version to play (grid, cues, skips); empty = rekordbox's data as it is. */
    void load (int deck, const TrackInfo& info, bool fromPcCollection, std::optional<TrackVersion> version = std::nullopt);
    bool isLoading (int deck) const { return loading[(size_t) deck].load(); }

    std::function<void (int deck, LoadedTrack::Ptr)> onLoaded;          // message thread
    std::function<void (int deck, const juce::String& error)> onError;  // message thread

    /** Builds a LoadedTrack without audio-thread involvement (also used by the command line tools). */
    static LoadedTrack::Ptr build (const TrackInfo& info, TrackAudio::Ptr audio, bool fromPcCollection, const Options& o,
                                   const std::optional<TrackVersion>& version = std::nullopt);

    /** Replaces a track's grid and cues with a library version (times in rekordbox seconds; the MP3 offset is applied). */
    static void applyVersion (LoadedTrack& t, const TrackVersion& v);
    /** A copy of `base` (same audio, waveforms) carrying a different version. */
    static LoadedTrack::Ptr withVersion (const LoadedTrack& base, const TrackVersion& v);
    /** The grid and cues a loaded track carries, as a library version. */
    static TrackVersion versionOf (const LoadedTrack& t);

private:
    AudioEngine& engine;
    AudioFileLoader& fileLoader;
    StemManager& stemManager;

    juce::CriticalSection lock;
    Options options;
    std::atomic<int> serialCounter { 0 };
    std::array<std::atomic<int>, AudioEngine::totalDecks> pending {};
    std::array<std::atomic<bool>, AudioEngine::totalDecks> loading {};
    std::array<std::atomic<bool>, AudioEngine::totalDecks> cancel {};
    juce::ThreadPool pool { 2 };
};

} // namespace orbit
