#pragma once
#include "TrackAudio.h"
#include "BeatGrid.h"
#include "WaveformData.h"
#include <atomic>
#include <memory>

namespace orbit
{

/** Everything about a track that a deck needs, bundled so it can be swapped in one go. */
struct LoadedTrack : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<LoadedTrack>;

    TrackInfo info;
    TrackAudio::Ptr audio;
    BeatGrid grid;
    std::vector<CuePoint> memoryCues;
    std::array<CuePoint, 8> hotCues {};
    std::array<bool, 8> hotCueSet {};
    WaveformData::Ptr waveform;          // 3-band (rekordbox analysis, or our own)
    WaveformDetail::Ptr detail;          // fine detail for zooming in
    AnlzWaveforms anlzWave;              // raw rekordbox waveforms, to draw in any style
    int serial = 0;

    // memoryCues / hotCues above hold the locators and hot cues as loaded (Centrifuge's own edits if any,
    // otherwise rekordbox's). Skips are Centrifuge-only. The deck holds the live, editable copies.
    std::vector<std::pair<double, double>> skips;
    juce::String cueKey;                 // identifies the track in Centrifuge's cue store
    double cueShift = 0.0;               // MP3 offset applied to all cue times (removed again when saving)

    /** The track's waveform in the requested style (message thread; cached). */
    WaveformData::Ptr waveformInStyle (WaveformData::Style s)
    {
        if (s == WaveformData::Style::threeBand || waveform == nullptr) return waveform;
        auto& slot = styled[(size_t) s];
        if (slot == nullptr)
        {
            if (anlzWave.numDetailEntries() > 0) slot = WaveformData::fromAnlz (anlzWave, s);
            if ((slot == nullptr || slot->style != s) && waveform->style == WaveformData::Style::threeBand)
                slot = WaveformData::deriveFrom3Band (*waveform, s);
            if (slot == nullptr) slot = waveform;
        }
        return slot;
    }
    std::array<WaveformData::Ptr, 3> styled;
};

/** One playback deck. All `process` work happens on the audio thread; controls arrive through a
    lock-free command queue so MIDI and UI never block audio. */
class Deck
{
public:
    explicit Deck (int index);
    ~Deck();

    int getIndex() const noexcept { return index; }

    //==========================================================================
    // Commands (any thread)
    enum class Cmd
    {
        load, attachStems, play, pause, playToggle, cue, hotCue, hotCueDelete, memoryCuePrev, memoryCueNext,
        setPitchFader, setPitchRange, pitchBend, tempoReset,
        jogTouch, jogTurn, setVinylMode,
        loopIn, loopOut, reloopExit, beatLoop, loopHalve, loopDouble, loopOff,
        beatJump, syncToggle, syncSet, masterSet, keyLockToggle, keyLockSet, quantizeToggle, seekFraction, seekSeconds,
        stemGain, stemMuteToggle, stemSolo, stemReset, eject, shift, keyShiftBy, keyShiftSet,
        quantizeSet, hotCueColour,
        locatorAdd, locatorDelete, locatorDeleteNearest, locatorColour,
        skipIn, skipOut, skipSet, skipAdd, skipDelete, skipDeleteNearest, skipsToggle, skipsSet,
        swapTrackData   // same audio, new grid / cues (ptr = LoadedTrack*); playback carries on
    };

    static constexpr int maxLocators = 32;
    static constexpr int maxSkips = 16;

    struct Command
    {
        Cmd cmd;
        double value = 0.0;   // generic numeric argument
        int index = 0;        // hot cue / stem index, etc.
        bool down = true;     // button pressed (true) or released (false)
        void* ptr = nullptr;  // LoadedTrack* / StemAudio* (kept alive by ReleasePool)
        int serial = 0;
        double value2 = 0.0;  // second argument (e.g. skip end)
    };

    void post (const Command& c);
    void post (Cmd cmd, double value = 0.0, int idx = 0, bool down = true) { post (Command { cmd, value, idx, down, nullptr, 0, 0.0 }); }
    void post2 (Cmd cmd, double value, double value2) { post (Command { cmd, value, 0, true, nullptr, 0, value2 }); }

    //==========================================================================
    // Audio thread
    void prepare (double deviceSampleRate, int maxBlockSize);

    /** Applies queued commands. Called by the engine before process(). */
    void handleCommands();

    /** Renders `n` frames (overwrites the output). `syncRatio` > 0 forces the tempo (sync), and
        `phaseNudge` is a small rate correction to stay beat-aligned with the master. */
    void process (float* outL, float* outR, int n);

    // Sync support (audio thread only)
    bool wantsSync() const noexcept { return syncOn; }
    bool isPlayingNow() const noexcept { return playing || scratching; }
    double heardPositionSec() const noexcept;
    double currentBpm() const noexcept;        // effective BPM at the current position including tempo
    double baseBpmAtPosition() const noexcept; // track BPM at the current position (no tempo)
    double beatPositionNow() const noexcept;   // continuous beat number at heard position
    const BeatGrid* grid() const noexcept { return track != nullptr ? &track->grid : nullptr; }
    void setSyncTarget (double ratio, double phaseErrorBeats, bool snapPhase);
    void clearSyncTarget() noexcept { syncRatio = 0.0; }
    bool takeSyncSnapRequest() noexcept { const bool r = syncSnapRequested; syncSnapRequested = false; return r; }
    bool takeMasterRequest() noexcept { const bool r = masterRequested; masterRequested = false; return r; }
    void setIsMaster (bool m) noexcept { pub.master = m; }
    bool hasTrack() const noexcept { return track != nullptr; }
    void setJogResolution (double ticksPerRev) noexcept { ticksPerRevolution = juce::jmax (16.0, ticksPerRev); }

    //==========================================================================
    // State published for the UI (read from any thread)
    struct Published
    {
        std::atomic<int> trackSerial { 0 };
        std::atomic<double> positionSec { 0.0 };
        std::atomic<double> lengthSec { 0.0 };
        std::atomic<double> tempoRatio { 1.0 };     // effective (pitch fader / sync)
        std::atomic<double> bpm { 0.0 };            // effective BPM
        std::atomic<double> pitchRange { 0.10 };
        std::atomic<double> cuePointSec { 0.0 };
        std::atomic<double> loopInSec { -1.0 }, loopOutSec { -1.0 };
        std::atomic<bool> loopActive { false };
        std::atomic<bool> playing { false }, keyLock { false }, sync { false }, master { false }, quantize { true };
        std::atomic<int> keyShift { 0 };          // semitones
        std::atomic<bool> vinylMode { true }, jogTouched { false }, stemsReady { false };
        std::atomic<float> stemsProgress { 0.0f };   // 0..1 while separating
        std::array<std::atomic<double>, 8> hotStart, hotEnd;
        std::array<std::atomic<juce::uint32>, 8> hotColour;
        std::array<std::atomic<float>, 4> stemGain;
        std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
        std::atomic<bool> endWarning { false };

        // Locators (memory cues) and skips
        std::atomic<int> numLocators { 0 }, numSkips { 0 };
        std::array<std::atomic<double>, maxLocators> locStart, locEnd;
        std::array<std::atomic<juce::uint32>, maxLocators> locColour;
        std::array<std::atomic<double>, maxSkips> skipFrom, skipTo;
        std::atomic<bool> skipsOn { true };
        std::atomic<double> skipPendingFrom { -1.0 };
        /** Counts edits to hot cues, locators and skips (so the app knows to save them). */
        std::atomic<int> cueEdits { 0 };
        /** Increases when the track's grid / cue data is swapped (e.g. switching rekordbox / Centrifuge version). */
        std::atomic<int> trackDataVersion { 0 };

        Published()
        {
            for (auto& v : locStart) v = -1.0;
            for (auto& v : locEnd) v = -1.0;
            for (auto& v : locColour) v = 0;
            for (auto& v : skipFrom) v = -1.0;
            for (auto& v : skipTo) v = -1.0;
            for (auto& h : hotStart) h = -1.0;
            for (auto& h : hotEnd) h = -1.0;
            for (auto& c : hotColour) c = 0;
            for (auto& g : stemGain) g = 1.0f;
        }
    };

    const Published& state() const noexcept { return pub; }

    /** Message thread: the track currently on this deck (for the UI). */
    LoadedTrack::Ptr getLoadedTrack() const;
    void setLoadedTrackForUi (LoadedTrack::Ptr t);
    /** Same track, new grid / cue data (keeps the stems). */
    void replaceLoadedTrackForUi (LoadedTrack::Ptr t) { const juce::ScopedLock sl (uiTrackLock); uiTrack = std::move (t); }
    StemAudio::Ptr getStemsForUi() const { const juce::ScopedLock sl (uiTrackLock); return uiStems; }
    void setStemsForUi (StemAudio::Ptr s) { const juce::ScopedLock sl (uiTrackLock); uiStems = std::move (s); }

private:
    void apply (const Command& c);
    void loadTrack (LoadedTrack* t);
    void jumpTo (double sec, bool quantized);
    double quantizeTime (double sec) const noexcept;
    void setHotCueAt (int idx, double sec);
    void publish();
    void startStretch();

    void readResampled (float* outL, float* outR, int n, double step);
    inline void frameAt (int64_t idx, float& l, float& r, const float* g) const noexcept;
    inline void sampleAt (double p, float& l, float& r, const float* g) const noexcept;
    void addLocator (double sec, juce::Colour colour, double endSec = -1.0);
    int nearestLocator (double sec, double within) const noexcept;
    void addSkip (double from, double to);
    void sortSkips();
    void cueEdited() noexcept { ++editCount; }

    const int index;
    double deviceRate = 44100.0;
    int maxBlock = 512;

    // Command queue
    static constexpr int queueSize = 512;
    juce::AbstractFifo fifo { queueSize };
    std::array<Command, queueSize> queue;
    juce::CriticalSection producerLock;

    // Audio-thread state
    LoadedTrack::Ptr track;
    StemAudio::Ptr stems;
    double pos = 0.0;               // read position in source frames
    double srcRate = 44100.0;
    bool playing = false;
    double cuePoint = 0.0;          // seconds
    bool cuePreview = false;
    int hotPreview = -1;
    double hotPreviewReturn = 0.0;
    double pitchFader = 0.0;        // -1..1
    double pitchRange = 0.10;
    bool pitchPickup = false;       // after sync off, wait for fader to reach current tempo
    double tempoRatio = 1.0;
    double bendRate = 0.0;          // jog pitch bend, decays
    double syncRatio = 0.0;         // > 0 when sync is driving tempo
    double phaseNudge = 0.0;
    bool syncOn = false, syncSnapRequested = false;
    bool keyLock = false, quantize = true, vinylMode = true, shiftHeld = false;
    int keyShift = 0;
    double lastTranspose = 0.0;

    // Jog / scratch
    bool jogTouched = false, scratching = false;
    double jogTicksPending = 0.0;   // accumulated since last block
    double scratchVelocity = 0.0;   // source seconds per real second
    double ticksPerRevolution = 720.0;
    static constexpr double secondsPerRevolution = 60.0 / (100.0 / 3.0); // 33 1/3 rpm

    // Loops (seconds)
    double loopIn = -1.0, loopOut = -1.0;
    bool loopActive = false;
    bool loopInPending = false;

    // Hot cues (deck-local copy; set/delete happen here)
    std::array<CuePoint, 8> hot {};
    std::array<bool, 8> hotSet {};

    // Locators (memory cues), sorted by time, and skips (sorted, non-overlapping)
    struct Loc { double start = 0.0, end = -1.0; juce::uint32 colour = 0; };
    std::array<Loc, maxLocators> locs {};
    int numLocs = 0;
    struct Skip { double from = 0.0, to = 0.0; };
    std::array<Skip, maxSkips> skips {};
    int numSkipsLocal = 0;
    bool skipsOn = true;
    double skipPending = -1.0;
    int editCount = 0;
    int dataVersion = 0;

    // Short cross-fade when playback jumps inside a block (loop wrap or skip)
    static constexpr int xfadeLen = 64;
    double xfPos = 0.0;
    int xfRemain = 0;
    double heldRatio = 1.0;
    bool masterRequested = false;

    // Stems
    std::array<float, 4> stemTarget { 1, 1, 1, 1 };
    std::array<float, 4> stemCurrent { 1, 1, 1, 1 };
    std::array<bool, 4> stemMuted { false, false, false, false };
    int64_t stemsValid = 0;

    // Key lock
    struct Stretcher;
    std::unique_ptr<Stretcher> stretch;
    bool stretchActive = false;
    std::vector<float> tmpL, tmpR;
    double stretchCarry = 0.0;
    double stretchLatencyFrames = 0.0; // in device-rate frames

    // Click suppression after jumps
    std::vector<float> fadeTailL, fadeTailR;
    int fadeTailLen = 0;

    Published pub;

    mutable juce::CriticalSection uiTrackLock;
    LoadedTrack::Ptr uiTrack;
    StemAudio::Ptr uiStems;

    JUCE_DECLARE_NON_COPYABLE (Deck)
};

} // namespace orbit
