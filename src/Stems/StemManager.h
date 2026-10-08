#pragma once
#include "DemucsSeparator.h"
#include "../Audio/AudioFileLoader.h"
#include "../Library/TrackInfo.h"
#include <deque>

namespace orbit
{

/** Finds, creates and caches stems.
    - Tracks already separated load from the cache in a second or two.
    - Otherwise, when a track is loaded on a deck, separation starts in the background and the
      stems fill in progressively from the start of the track (usable before it's finished).
    - Batch pre-analysis separates whole playlists ahead of time. */
class StemManager : private juce::Thread
{
public:
    explicit StemManager (AudioFileLoader& loader);
    ~StemManager() override;

    struct Settings
    {
        juce::File modelFile;
        juce::File cacheDir;
        bool useGpu = true;
        int cpuThreads = 0;
        bool separateOnLoad = true;
        int oggQualityIndex = 5;   // 160 kbps per stem
    };
    void setSettings (const Settings& s);
    Settings getSettings() const;

    using StemsCallback = std::function<void (StemAudio::Ptr)>;

    /** Called when a track is loaded on a deck. `onStems` runs on the message thread once stems
        exist (complete from cache, or growing while being separated). */
    void requestForDeck (int deck, const TrackInfo& info, TrackAudio::Ptr audio, int serial, StemsCallback onStems);
    void cancelDeck (int deck);

    void addToBatch (const std::vector<TrackInfo>& tracks);
    void cancelBatch();

    bool hasCachedStems (const TrackInfo& info) const;
    juce::File cacheFolderFor (const TrackInfo& info) const;

    struct Status
    {
        juce::String activity;      // what the worker is doing now
        double progress = 0.0;      // of the current job
        int batchQueued = 0, batchDone = 0, batchFailed = 0;
        juce::String backend;       // GPU / CPU
        juce::String lastError;
        bool modelAvailable = false;
    };
    Status getStatus() const;

private:
    struct Job
    {
        enum class Kind { deck, batch } kind;
        int deck = -1;
        int serial = 0;
        TrackInfo info;
        TrackAudio::Ptr audio;
        StemAudio::Ptr stems;
        StemsCallback onStems;
    };

    void run() override;
    bool ensureModel (juce::String& error);
    bool processJob (Job& job);
    bool writeCache (const TrackInfo& info, const StemAudio& stems);
    StemAudio::Ptr readCache (const TrackInfo& info, const TrackAudio& audio);

    AudioFileLoader& loader;
    DemucsSeparator separator;
    bool modelTried = false;

    mutable juce::CriticalSection lock;
    Settings settings;
    std::deque<Job> deckJobs, batchJobs;
    std::atomic<int> runningDeck { -1 };
    std::atomic<bool> cancelCurrent { false };
    std::atomic<bool> cancelBatchFlag { false };
    juce::WaitableEvent wake;

    mutable juce::CriticalSection statusLock;
    Status status;

    juce::ThreadPool cachePool { 2 };
};

} // namespace orbit
