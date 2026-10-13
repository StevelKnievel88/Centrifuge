#include "AudioEngine.h"

namespace orbit
{

/** Drives the decks in real time when no audio device could be opened (silent). */
class AudioEngine::ClockThread : public juce::Thread
{
public:
    explicit ClockThread (AudioEngine& e) : juce::Thread ("Centrifuge clock"), engine (e) {}

    void run() override
    {
        constexpr int block = 512;
        const double sr = 44100.0;
        std::vector<float> l (block), r (block);
        float* outs[2] = { l.data(), r.data() };
        auto next = juce::Time::getMillisecondCounterHiRes();

        while (! threadShouldExit())
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            engine.render (outs, 2, block);
            const auto t1 = juce::Time::getMillisecondCounterHiRes();
            const double period = 1000.0 * block / sr;
            engine.internalCpu = juce::jlimit (0.0, 1.0, (t1 - t0) / period);
            next += period;
            const auto wait = next - juce::Time::getMillisecondCounterHiRes();
            if (wait > 0) juce::Thread::sleep ((int) wait);
            else next = juce::Time::getMillisecondCounterHiRes();
        }
    }

private:
    AudioEngine& engine;
};

AudioEngine::AudioEngine()
{
    for (int i = 0; i < maxDecks; ++i)
    {
        decks[(size_t) i] = std::make_unique<Deck> (i);
        deckOutputs[(size_t) i] = i * 2;
    }
    deviceManager.addChangeListener (this);
}

AudioEngine::~AudioEngine()
{
    stopInternalClock();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioEngine::initialise (const juce::String& savedStateXml, int preferredBufferSize)
{
    std::unique_ptr<juce::XmlElement> saved;
    if (savedStateXml.isNotEmpty())
        saved = juce::parseXML (savedStateXml);

    // Prefer ASIO for the XDJ-XZ: lowest latency and lowest CPU overhead.
    deviceManager.initialise (0, 10, saved.get(), false, {}, nullptr);

    auto openXdj = [this, preferredBufferSize]() -> bool
    {
        const juce::StringArray preferredTypes { "ASIO", "Windows Audio (Exclusive Mode)", "Windows Audio", "CoreAudio" };
        for (auto& typeName : preferredTypes)
        {
            for (auto* type : deviceManager.getAvailableDeviceTypes())
            {
                if (type->getTypeName() != typeName) continue;
                type->scanForDevices();
                for (auto& name : type->getDeviceNames (false))
                {
                    if (! name.containsIgnoreCase ("XDJ")) continue;
                    deviceManager.setCurrentAudioDeviceType (typeName, true);
                    auto setup = deviceManager.getAudioDeviceSetup();
                    setup.outputDeviceName = name;
                    setup.inputDeviceName = {};
                    setup.useDefaultOutputChannels = false;
                    setup.outputChannels.clear();
                    setup.outputChannels.setRange (0, 10, true);
                    setup.useDefaultInputChannels = false;
                    setup.inputChannels.clear();
                    setup.bufferSize = preferredBufferSize;
                    if (deviceManager.setAudioDeviceSetup (setup, true).isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr)
                        return true;
                }
            }
        }
        return false;
    };

    if (saved == nullptr || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        if (! openXdj())
        {
            deviceManager.initialiseWithDefaultDevices (0, 2);
            if (auto* dev = deviceManager.getCurrentAudioDevice())
            {
                auto setup = deviceManager.getAudioDeviceSetup();
                setup.bufferSize = preferredBufferSize;
                deviceManager.setAudioDeviceSetup (setup, true);
                juce::ignoreUnused (dev);
            }
        }
    }

    deviceManager.addAudioCallback (this);

    if (deviceManager.getCurrentAudioDevice() == nullptr)
        startInternalClock();
}

juce::String AudioEngine::saveDeviceState() const
{
    if (auto xml = deviceManager.createStateXml())
        return xml->toString();
    return {};
}

void AudioEngine::setDeckOutput (int d, int ch)
{
    if (d >= 0 && d < maxDecks)
        deckOutputs[(size_t) d] = ch;
}

double AudioEngine::getCpuUsage() const
{
    if (clockThread != nullptr) return internalCpu.load();
    return const_cast<juce::AudioDeviceManager&> (deviceManager).getCpuUsage();
}

juce::String AudioEngine::getDeviceDescription() const
{
    if (clockThread != nullptr) return "No audio device (silent)";
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return d->getTypeName() + ": " + d->getName() + "  " + juce::String (d->getCurrentSampleRate() / 1000.0, 1)
               + " kHz / " + juce::String (d->getCurrentBufferSizeSamples());
    return "No audio device";
}

void AudioEngine::startInternalClock()
{
    if (clockThread != nullptr) return;
    audioDeviceAboutToStart (nullptr);
    clockThread = std::make_unique<ClockThread> (*this);
    clockThread->startThread (juce::Thread::Priority::high);
}

void AudioEngine::stopInternalClock()
{
    if (clockThread != nullptr)
    {
        clockThread->stopThread (2000);
        clockThread.reset();
    }
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (deviceManager.getCurrentAudioDevice() != nullptr)
        stopInternalClock();
    else
        startInternalClock();

    if (onDeviceChanged)
        onDeviceChanged();
}

void AudioEngine::autoRoute (int numOutputs)
{
    if (numOutputs >= 8)
    {
        sumToMaster = false;
        for (int d = 0; d < maxDecks; ++d)
            if (deckOutputs[(size_t) d] + 1 >= numOutputs) deckOutputs[(size_t) d] = d * 2;
    }
    else if (numOutputs >= 4)
    {
        sumToMaster = false;
    }
    else
    {
        sumToMaster = true;
    }
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr)
    {
        sampleRate = device->getCurrentSampleRate();
        blockSize = device->getCurrentBufferSizeSamples();
        outputLatency = (device->getOutputLatencyInSamples() + blockSize) / juce::jmax (1.0, sampleRate);
        autoRoute (device->getActiveOutputChannels().countNumberOfSetBits());
    }
    else
    {
        sampleRate = 44100.0;
        blockSize = 512;
        sumToMaster = true;
        outputLatency = 0.0;
    }

    const int maxBlock = juce::jmax (blockSize, 2048);
    scratchL.assign ((size_t) maxBlock, 0.0f);
    scratchR.assign ((size_t) maxBlock, 0.0f);
    for (auto& d : decks)
        d->prepare (sampleRate, maxBlock);
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                    float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    render (outputs, numOutputs, numSamples);
}

void AudioEngine::updateSync()
{
    for (int d = 0; d < maxDecks; ++d)
        if (decks[(size_t) d]->takeMasterRequest())
            masterDeck = d;

    int m = masterDeck.load();
    auto deckHasTrack = [this] (int i) { return i >= 0 && decks[(size_t) i]->hasTrack(); };

    if (! deckHasTrack (m) || ! decks[(size_t) m]->isPlayingNow())
    {
        int candidate = -1;
        for (int d = 0; d < maxDecks && candidate < 0; ++d)
            if (decks[(size_t) d]->hasTrack() && decks[(size_t) d]->isPlayingNow() && ! decks[(size_t) d]->wantsSync())
                candidate = d;
        for (int d = 0; d < maxDecks && candidate < 0; ++d)
            if (decks[(size_t) d]->hasTrack() && decks[(size_t) d]->isPlayingNow())
                candidate = d;
        if (candidate >= 0) m = candidate;
        else if (! deckHasTrack (m)) m = -1;
    }
    masterDeck = m;

    for (int d = 0; d < maxDecks; ++d)
    {
        auto& dk = *decks[(size_t) d];
        dk.setIsMaster (d == m);

        if (d == m || ! dk.wantsSync() || m < 0)
        {
            dk.clearSyncTarget();
            if (d == m) dk.takeSyncSnapRequest();
            continue;
        }

        auto& master = *decks[(size_t) m];
        const double mb = master.currentBpm();
        const double base = dk.baseBpmAtPosition();
        if (mb <= 0.0 || base <= 0.0)
        {
            dk.clearSyncTarget();
            continue;
        }

        double f = 1.0;
        const double raw = mb / base;
        while (raw / f > 1.45 && f < 8.0) f *= 2.0;
        while (raw / f < 0.69 && f > 0.125) f *= 0.5;
        const double ratio = raw / f;

        const bool snap = dk.takeSyncSnapRequest();
        double err = 0.0;
        const bool bothPlaying = master.isPlayingNow() && dk.isPlayingNow();
        if (bothPlaying && master.grid() != nullptr && master.grid()->isValid() && dk.grid() != nullptr && dk.grid()->isValid())
        {
            double e = master.beatPositionNow() / f - dk.beatPositionNow();
            err = e - std::floor (e + 0.5);
        }
        dk.setSyncTarget (ratio, err, snap && bothPlaying);
    }
}

void AudioEngine::render (float* const* outputs, int numOutputs, int numSamples)
{
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::clear (outputs[c], numSamples);

    const juce::SpinLock::ScopedTryLockType tryLock (renderLock);
    if (! tryLock.isLocked()) return;

    for (auto& d : decks)
        d->handleCommands();
    updateSync();

    const bool sum = sumToMaster.load();
    const int chunk = (int) scratchL.size();

    for (int start = 0; start < numSamples; start += chunk)
    {
        const int n = juce::jmin (chunk, numSamples - start);
        for (int d = 0; d < maxDecks; ++d)
        {
            decks[(size_t) d]->process (scratchL.data(), scratchR.data(), n);

            if (sum)
            {
                if (numOutputs > 0 && outputs[0] != nullptr) juce::FloatVectorOperations::add (outputs[0] + start, scratchL.data(), n);
                if (numOutputs > 1 && outputs[1] != nullptr) juce::FloatVectorOperations::add (outputs[1] + start, scratchR.data(), n);
            }
            else
            {
                const int ch = deckOutputs[(size_t) d].load();
                if (ch >= 0 && ch + 1 < numOutputs)
                {
                    if (outputs[ch] != nullptr)     juce::FloatVectorOperations::copy (outputs[ch] + start, scratchL.data(), n);
                    if (outputs[ch + 1] != nullptr) juce::FloatVectorOperations::copy (outputs[ch + 1] + start, scratchR.data(), n);
                }
            }
        }
    }
}

} // namespace orbit
