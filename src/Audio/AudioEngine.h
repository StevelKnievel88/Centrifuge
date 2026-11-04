#pragma once
#include "Deck.h"
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
    static constexpr int previewDeck = 2;       // a third deck for PREP and browser previews (not synced)
    static constexpr int totalDecks = 3;

    AudioEngine();
    ~AudioEngine() override;

    /** Opens the saved device, else an XDJ-XZ (ASIO first), else the default output.
        Falls back to a silent internal clock if no device opens, so the app is usable without hardware. */
    void initialise (const juce::String& savedStateXml, int preferredBufferSize);
    juce::String saveDeviceState() const;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
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
    std::atomic<int> masterDeck { -1 };

    std::vector<float> scratchL, scratchR;
    double sampleRate = 44100.0;
    int blockSize = 512;

    class ClockThread;
    std::unique_ptr<ClockThread> clockThread;
    std::atomic<double> internalCpu { 0.0 };
    std::atomic<double> outputLatency { 0.0 };

    ReleasePool releasePool;
    juce::SpinLock renderLock;
};

} // namespace orbit
