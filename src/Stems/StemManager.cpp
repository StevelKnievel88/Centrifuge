#include "StemManager.h"
#include <juce_cryptography/juce_cryptography.h>

namespace orbit
{

namespace
{
    const char* cacheNames[] = { "drums", "bass", "vocals" };
    const int cacheStemIndex[] = { 0, 1, 3 }; // "other" is rebuilt as original - (drums + bass + vocals)
}

StemManager::StemManager (AudioFileLoader& l) : juce::Thread ("OrbitDJ stems"), loader (l)
{
    startThread (juce::Thread::Priority::low);
}

StemManager::~StemManager()
{
    cancelCurrent = true;
    cancelBatchFlag = true;
    signalThreadShouldExit();
    wake.signal();
    stopThread (8000);
    cachePool.removeAllJobs (true, 5000);
}

void StemManager::setSettings (const Settings& s)
{
    const juce::ScopedLock sl (lock);
    const bool modelChanged = s.modelFile != settings.modelFile || s.useGpu != settings.useGpu || s.cpuThreads != settings.cpuThreads;
    settings = s;
    if (modelChanged) modelTried = false;
}

StemManager::Settings StemManager::getSettings() const
{
    const juce::ScopedLock sl (lock);
    return settings;
}

juce::File StemManager::cacheFolderFor (const TrackInfo& info) const
{
    const auto f = info.file;
    const juce::String key = f.getFullPathName() + "|" + juce::String (f.getSize()) + "|" + juce::String (f.getLastModificationTime().toMilliseconds());
    const auto hash = juce::MD5 (key.toUTF8()).toHexString();
    return getSettings().cacheDir.getChildFile (hash.substring (0, 2)).getChildFile (hash);
}

bool StemManager::hasCachedStems (const TrackInfo& info) const
{
    const auto dir = cacheFolderFor (info);
    for (auto* n : cacheNames)
        if (! dir.getChildFile (juce::String (n) + ".ogg").existsAsFile())
            return false;
    return true;
}

StemManager::Status StemManager::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    auto s = status;
    {
        const juce::ScopedLock sl2 (lock);
        s.batchQueued = (int) batchJobs.size();
    }
    return s;
}

//==============================================================================
void StemManager::requestForDeck (int deck, const TrackInfo& info, TrackAudio::Ptr audio, int serial, StemsCallback onStems)
{
    cancelDeck (deck);
    if (audio == nullptr) return;

    if (hasCachedStems (info))
    {
        cachePool.addJob ([this, info, audio, onStems]
        {
            if (auto stems = readCache (info, *audio))
                juce::MessageManager::callAsync ([onStems, stems] { onStems (stems); });
        });
        return;
    }

    if (! getSettings().separateOnLoad) return;

    StemAudio::Ptr stems = new StemAudio();
    stems->sampleRate = audio->sampleRate;
    stems->numFrames = audio->numFrames;
    for (auto& ch : stems->channels) ch.assign ((size_t) audio->numFrames, 0);
    stems->validFrames = 0;

    {
        const juce::ScopedLock sl (lock);
        Job job;
        job.kind = Job::Kind::deck;
        job.deck = deck;
        job.serial = serial;
        job.info = info;
        job.audio = audio;
        job.stems = stems;
        job.onStems = onStems;
        deckJobs.push_back (std::move (job));
        // A running batch job gives way to the deck.
        if (runningDeck.load() < 0) cancelCurrent = true;
    }

    wake.signal();
}

void StemManager::cancelDeck (int deck)
{
    const juce::ScopedLock sl (lock);
    deckJobs.erase (std::remove_if (deckJobs.begin(), deckJobs.end(), [deck] (const Job& j) { return j.deck == deck; }), deckJobs.end());
    if (runningDeck.load() == deck) cancelCurrent = true;
}

void StemManager::addToBatch (const std::vector<TrackInfo>& tracks)
{
    {
        const juce::ScopedLock sl (lock);
        cancelBatchFlag = false;
        for (auto& t : tracks)
        {
            Job j;
            j.kind = Job::Kind::batch;
            j.info = t;
            batchJobs.push_back (std::move (j));
        }
    }
    wake.signal();
}

void StemManager::cancelBatch()
{
    const juce::ScopedLock sl (lock);
    batchJobs.clear();
    cancelBatchFlag = true;
    if (runningDeck.load() < 0) cancelCurrent = true;
}

//==============================================================================
bool StemManager::ensureModel (juce::String& error)
{
    if (separator.isLoaded() && modelTried) return true;
    if (modelTried && ! separator.isLoaded()) { error = status.lastError; return false; }

    modelTried = true;
    const auto s = getSettings();
    DemucsSeparator::Options o;
    o.modelFile = s.modelFile;
    o.useGpu = s.useGpu;
    o.cpuThreads = s.cpuThreads;

    {
        const juce::ScopedLock sl (statusLock);
        status.activity = "Loading stem model...";
    }
    const bool ok = separator.load (o, error);
    const juce::ScopedLock sl (statusLock);
    status.backend = ok ? separator.getBackendDescription() : juce::String ("unavailable");
    status.modelAvailable = ok;
    if (! ok) status.lastError = error;
    return ok;
}

void StemManager::run()
{
    while (! threadShouldExit())
    {
        Job job;
        bool have = false;
        {
            const juce::ScopedLock sl (lock);
            if (! deckJobs.empty())
            {
                job = std::move (deckJobs.front());
                deckJobs.pop_front();
                have = true;
            }
            else if (! batchJobs.empty())
            {
                job = std::move (batchJobs.front());
                batchJobs.pop_front();
                have = true;
            }
            cancelCurrent = false;
            runningDeck = have && job.kind == Job::Kind::deck ? job.deck : -1;
        }

        if (! have)
        {
            {
                const juce::ScopedLock sl (statusLock);
                status.activity = {};
                status.progress = 0.0;
            }
            wake.wait (500);
            continue;
        }

        const bool ok = processJob (job);
        runningDeck = -1;

        if (job.kind == Job::Kind::batch)
        {
            const juce::ScopedLock sl (statusLock);
            if (ok) ++status.batchDone;
            else if (! cancelCurrent.load()) ++status.batchFailed;
        }

        // A batch job interrupted by a deck request goes back to the front of the queue.
        if (! ok && job.kind == Job::Kind::batch && cancelCurrent.load() && ! cancelBatchFlag.load() && ! threadShouldExit())
        {
            const juce::ScopedLock sl (lock);
            job.audio = nullptr;
            job.stems = nullptr;
            batchJobs.push_front (std::move (job));
        }
    }
}

bool StemManager::processJob (Job& job)
{
    auto setActivity = [this] (const juce::String& a, double p)
    {
        const juce::ScopedLock sl (statusLock);
        status.activity = a;
        status.progress = p;
    };

    const auto name = job.info.displayArtistTitle();

    if (job.kind == Job::Kind::batch && hasCachedStems (job.info))
        return true;

    juce::String error;
    if (! ensureModel (error))
        return false;

    if (job.audio == nullptr)
    {
        setActivity ("Decoding " + name, 0.0);
        job.audio = loader.load (job.info.file, error, &cancelCurrent);
        if (job.audio == nullptr)
        {
            const juce::ScopedLock sl (statusLock);
            status.lastError = name + ": " + error;
            return false;
        }
    }

    StemAudio::Ptr stems = job.stems;
    if (stems == nullptr)
        stems = new StemAudio();

    // Hand the (still empty) stems to the deck now: they fill in as separation progresses.
    if (job.onStems != nullptr)
    {
        auto cb = job.onStems;
        juce::MessageManager::callAsync ([cb, stems] { cb (stems); });
    }

    const juce::String prefix = job.kind == Job::Kind::deck ? "Deck " + juce::String (job.deck + 1) + ": separating " : "Pre-analysing ";
    const bool ok = separator.separate (*job.audio, *stems, [&] (double p)
    {
        setActivity (prefix + name, p);
        return ! cancelCurrent.load() && ! threadShouldExit();
    }, error);

    if (! ok)
    {
        if (! cancelCurrent.load())
        {
            const juce::ScopedLock sl (statusLock);
            status.lastError = name + ": " + error;
        }
        return false;
    }

    setActivity ("Saving stems for " + name, 1.0);
    writeCache (job.info, *stems);
    setActivity ({}, 0.0);
    return true;
}

//==============================================================================
bool StemManager::writeCache (const TrackInfo& info, const StemAudio& stems)
{
    const auto dir = cacheFolderFor (info);
    if (! dir.createDirectory()) return false;

    juce::OggVorbisAudioFormat ogg;
    const int quality = getSettings().oggQualityIndex;
    constexpr int block = 1 << 15;
    juce::AudioBuffer<float> buf (2, block);

    for (int c = 0; c < 3; ++c)
    {
        const int s = cacheStemIndex[c];
        auto tmp = dir.getChildFile (juce::String (cacheNames[c]) + ".tmp");
        tmp.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (tmp);
        if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk()) return false;

        auto writer = ogg.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (stems.sampleRate)
                                                       .withNumChannels (2)
                                                       .withBitsPerSample (16)
                                                       .withQualityOptionIndex (quality));
        if (writer == nullptr) return false;

        for (int64_t pos = 0; pos < stems.numFrames; pos += block)
        {
            const int n = (int) std::min<int64_t> (block, stems.numFrames - pos);
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* src = stems.channels[(size_t) (s * 2 + ch)].data() + pos;
                auto* dst = buf.getWritePointer (ch);
                for (int i = 0; i < n; ++i) dst[i] = (float) src[i] * StemAudio::toFloat * 0.5f; // -6 dB, restored on read
            }
            writer->writeFromAudioSampleBuffer (buf, 0, n);
        }
        writer.reset();
        tmp.moveFileTo (dir.getChildFile (juce::String (cacheNames[c]) + ".ogg"));
    }
    return true;
}

StemAudio::Ptr StemManager::readCache (const TrackInfo& info, const TrackAudio& audio)
{
    const auto dir = cacheFolderFor (info);
    juce::OggVorbisAudioFormat ogg;

    StemAudio::Ptr stems = new StemAudio();
    stems->sampleRate = audio.sampleRate;
    stems->numFrames = audio.numFrames;
    for (auto& ch : stems->channels) ch.assign ((size_t) audio.numFrames, 0);

    std::array<std::vector<float>, 2> sumOf3;
    for (auto& v : sumOf3) v.assign ((size_t) audio.numFrames, 0.0f);

    constexpr int block = 1 << 15;
    juce::AudioBuffer<float> buf (2, block);

    for (int c = 0; c < 3; ++c)
    {
        auto f = dir.getChildFile (juce::String (cacheNames[c]) + ".ogg");
        std::unique_ptr<juce::AudioFormatReader> reader (ogg.createReaderFor (new juce::FileInputStream (f), true));
        if (reader == nullptr) return {};

        const int s = cacheStemIndex[c];
        const int64_t len = std::min<int64_t> (reader->lengthInSamples, audio.numFrames);
        for (int64_t pos = 0; pos < len; pos += block)
        {
            const int n = (int) std::min<int64_t> (block, len - pos);
            reader->read (&buf, 0, n, pos, true, true);
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* src = buf.getReadPointer (juce::jmin (ch, (int) reader->numChannels - 1));
                auto* dst = stems->channels[(size_t) (s * 2 + ch)].data() + pos;
                auto* sum = sumOf3[(size_t) ch].data() + pos;
                for (int i = 0; i < n; ++i)
                {
                    const float v = src[i] * 2.0f;
                    dst[i] = StemAudio::fromFloat (v);
                    sum[i] += v;
                }
            }
        }
    }

    for (int64_t i = 0; i < audio.numFrames; ++i)
    {
        stems->channels[4][(size_t) i] = StemAudio::fromFloat (audio.left[(size_t) i] - sumOf3[0][(size_t) i]);
        stems->channels[5][(size_t) i] = StemAudio::fromFloat (audio.right[(size_t) i] - sumOf3[1][(size_t) i]);
    }

    stems->validFrames = stems->numFrames;
    return stems;
}

} // namespace orbit
