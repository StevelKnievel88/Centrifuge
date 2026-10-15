#pragma once
#include "../Audio/AudioEngine.h"
#include "../Audio/AudioFileLoader.h"
#include "../Stems/StemManager.h"
#include "../Library/CueStore.h"

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

    void load (int deck, const TrackInfo& info, bool fromPcCollection);
    bool isLoading (int deck) const { return loading[(size_t) deck].load(); }

    std::function<void (int deck, LoadedTrack::Ptr)> onLoaded;          // message thread
    std::function<void (int deck, const juce::String& error)> onError;  // message thread

    /** Builds a LoadedTrack without audio-thread involvement (also used by the command line tools). */
    static LoadedTrack::Ptr build (const TrackInfo& info, TrackAudio::Ptr audio, bool fromPcCollection, const Options& o);

private:
    AudioEngine& engine;
    AudioFileLoader& fileLoader;
    StemManager& stemManager;

    juce::CriticalSection lock;
    Options options;
    std::atomic<int> serialCounter { 0 };
    std::array<std::atomic<int>, AudioEngine::maxDecks> pending {};
    std::array<std::atomic<bool>, AudioEngine::maxDecks> loading {};
    std::array<std::atomic<bool>, AudioEngine::maxDecks> cancel {};
    juce::ThreadPool pool { 2 };
};

} // namespace orbit
