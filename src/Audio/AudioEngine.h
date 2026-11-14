#pragma once
#include "KeySynth.h"
#include "Deck.h"
#include "MasterRecorder.h"
#include <juce_audio_devices/juce_audio_devices.h>

namespace orbit
{

/** Owns the audio device and the decks. Each deck is sent to its own stereo output pair so the
    XDJ-XZ's hardware mixer does the mixing (EQ, faders, FX), exactly like rekordbox in PC mode. */
class AudioEngine : private juce::AudioIODeviceCallback,
                    private juce::ChangeListener
{
public:
    static constexpr int maxDecks = 2;          // the two performance decks
    static constexpr int prepDeck = 2;          // the PREP page's deck (not synced, plays on the preview output)
    static constexpr int previewDeck = 3;       // browser previews (its own deck, so a preview never replaces PREP's track)
    static constexpr int totalDecks = 4;

    AudioEngine();
    ~AudioEngine() override;

    /** Opens the saved device, else an XDJ-XZ (ASIO first), else the default output.
        Falls back to a silent internal clock if no device opens, so the app is usable without hardware. */
    void initialise (const juce::String& savedStateXml, int preferredBufferSize);
    juce::String saveDeviceState() const;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    /** The PREP page's keyboard (bass sounds / your samples over the track). */
    KeySynth& getKeySynth() { return keySynth; }
    Deck& deck (int i) { return *decks[(size_t) juce::jlimit (0, totalDecks - 1, i)]; }
    const Deck& deck (int i) const { return *decks[(size_t) juce::jlimit (0, totalDecks - 1, i)]; }

    /** Where PREP / browser previews play: first output channel (0-based), -1 = automatic. Mixed in (added). */
    void setPreviewOutput (int firstChannel) { previewOutput = firstChannel; }
    int getPreviewOutputInUse() const { return previewInUse.load(); }

    /** Output routing: first output channel (0-based) used by each deck. -1 = not routed. */
    void setDeckOutput (int deckIndex, int firstChannel);
    int getDeckOutput (int deckIndex) const { return deckOutputs[(size_t) deckIndex].load(); }

    /** When true, all decks are summed onto outputs 1/2 (for testing without the XDJ-XZ mixer). */
    void setSumToMaster (bool shouldSum) { sumToMaster = shouldSum; }
    bool isSummingToMaster() const { return sumToMaster.load(); }

    /** Centrifuge's own output level, 0..1 (for playing through the computer with nothing else connected). */
    void setMasterVolume (float v) { masterVolume = juce::jlimit (0.0f, 1.0f, v); }
    float getMasterVolume() const { return masterVolume.load(); }
    /** Peak of outputs 1/2 (decays), for the top bar meter. */
    float getMasterPeak() const { return masterPeak.load(); }

    /** Recording: Centrifuge's mix (decks summed, before the master volume) or an input pair (firstInput >= 0). nullptr stops feeding it. */
    void setRecorder (MasterRecorder* r, int firstInput) { recordInput = firstInput; recorder = r; }
    /** The device's input pairs ("1/2", ...), for the recording source. */
    juce::StringArray inputPairNames() const;
    /** Opens the device's inputs (they're closed unless something records from them). Returns an error, or empty. */
    juce::String enableInputs();
    double getSampleRate() const { return sampleRate; }

    int getMasterDeck() const { return masterDeck.load(); }
    double getCpuUsage() const;
    double getOutputLatencySeconds() const { return outputLatency.load(); }
    bool isRunningOnInternalClock() const { return clockThread != nullptr; }
    juce::String getDeviceDescription() const;

    /** Called on the message thread when the device changes (to update UI/routing). */
    std::function<void()> onDeviceChanged;

    ReleasePool& getReleasePool() { return releasePool; }

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void render (float* const* outputs, int numOutputs, int numSamples);
    void updateSync();
    void autoRoute (int numOutputs);
    void startInternalClock();
    void stopInternalClock();

    juce::AudioDeviceManager deviceManager;
    std::array<std::unique_ptr<Deck>, totalDecks> decks;
    std::atomic<int> previewOutput { -1 }, previewInUse { 0 };
    std::array<std::atomic<int>, maxDecks> deckOutputs;
    std::atomic<bool> sumToMaster { false };
    std::atomic<float> masterVolume { 1.0f }, masterPeak { 0.0f };
    float masterNow = 1.0f;
    std::atomic<int> masterDeck { -1 };

    std::vector<float> scratchL, scratchR, mixL, mixR;
    std::atomic<MasterRecorder*> recorder { nullptr };
    std::atomic<int> recordInput { -1 };
    double sampleRate = 44100.0;
    int blockSize = 512;

    class ClockThread;
    std::unique_ptr<ClockThread> clockThread;
    std::atomic<double> internalCpu { 0.0 };
    std::atomic<double> outputLatency { 0.0 };

    ReleasePool releasePool;
    juce::SpinLock renderLock;
    KeySynth keySynth;
};

} // namespace orbit
