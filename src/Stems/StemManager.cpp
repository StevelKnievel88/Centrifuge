#include "StemManager.h"
#include <juce_cryptography/juce_cryptography.h>

namespace orbit
{

namespace
{
    const char* cacheNames[] = { "drums", "bass", "vocals" };
    const int cacheStemIndex[] = { 0, 1, 3 }; // "other" is rebuilt as original - (drums + bass + vocals)
}

StemManager::StemManager (AudioFileLoader& l) : juce::Thread ("Centrifuge stems"), loader (l)
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
    const bool modelChanged = s.modelFile != settings.modelFile || s.useGpu != settings.useGpu || s.cpuThreads != settings.cpuThreads
                           || s.qualityOnLoad != settings.qualityOnLoad || s.qualityBatch != settings.qualityBatch;
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
        if (! dir.getChildFile (juce::String (n) + ".ogg").existsAsFile() && ! dir.getChildFile (juce::String (n) + ".flac").existsAsFile()
            && ! dir.getChildFile (juce::String (n) + ".mp3").existsAsFile())
            return false;
    return true;
}

StemAudio::Ptr StemManager::loadCachedStems (const TrackInfo& info, const TrackAudio& audio)
{
    return hasCachedStems (info) ? readCache (info, audio) : StemAudio::Ptr();
}

int StemManager::cachedQuality (const TrackInfo& info) const
{
    if (! hasCachedStems (info)) return -1;
    const auto f = cacheFolderFor (info).getChildFile ("quality.txt");
    return f.existsAsFile() ? juce::jlimit (0, 3, f.loadFileAsString().trim().getIntValue()) : 0;
}

StemQuality StemManager::achievable (StemQuality q) const
{
    const auto s = getSettings();
    auto fileFor = [&] (const juce::String& n) { return n == StemModels::fast ? s.modelFile : (s.findModel ? s.findModel (n) : s.modelFile.getSiblingFile (n)); };
    for (int level = (int) q; level > 0; --level)
    {
        bool all = true;
        for (auto& n : StemModels::required ((StemQuality) level)) all = all && fileFor (n).existsAsFile();
        if (all) return (StemQuality) level;
    }
    return StemQuality::fast;
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
bool StemManager::ensureModel (StemQuality quality, juce::String& error)
{
    // Models extracted since the last load make a higher level possible: load again.
    const auto possible = achievable (quality);
    if (modelTried && loadedFor == quality && separator.isLoaded() && separator.getQuality() != possible)
        modelTried = false;
    if (modelTried && loadedFor == quality)
    {
        if (separator.isLoaded()) return true;
        error = status.lastError;
        return false;
    }

    modelTried = true;
    loadedFor = quality;
    const auto s = getSettings();
    DemucsSeparator::Options o;
    o.modelFile = s.modelFile;
    o.findModel = s.findModel;
    o.quality = quality;
    o.useGpu = s.useGpu && ! gpuFailed;
    o.cpuThreads = s.cpuThreads;

    {
        const juce::ScopedLock sl (statusLock);
        status.activity = "Loading stem model...";
    }
    const bool ok = separator.load (o, error);
    const juce::ScopedLock sl (statusLock);
    status.backend = ok ? separator.getBackendDescription() + gpuNote : juce::String ("unavailable");
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

    auto wanted = job.kind == Job::Kind::batch ? getSettings().qualityBatch : getSettings().qualityOnLoad;
    if (bestGpuFailed && wanted == StemQuality::best) wanted = StemQuality::high;
    // Pre-analysis upgrades stems made at a lower quality; anything already as good is left alone.
    if (job.kind == Job::Kind::batch && cachedQuality (job.info) >= (int) achievable (wanted))
        return true;

    juce::String error;
    if (! ensureModel (wanted, error))
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
    auto run = [&]
    {
        return separator.separate (*job.audio, *stems, [&] (double p)
        {
            setActivity (prefix + name, p);
            return ! cancelCurrent.load() && ! threadShouldExit();
        }, error);
    };
    bool ok = run();

    // The graphics card was reset by Windows (it took too long or ran out of memory): step down and go again.
    for (int attempt = 0; ! ok && attempt < 2 && ! cancelCurrent.load() && isGpuDeviceLost (error); ++attempt)
    {
        if (separator.getQuality() == StemQuality::best && ! bestGpuFailed)
        {
            bestGpuFailed = true;
            gpuNote = " - Best stopped: the graphics card was reset, so High is used this session";
            wanted = StemQuality::high;
        }
        else
        {
            gpuFailed = true;
            gpuNote = " - the graphics card was reset, so the CPU is used this session";
        }
        modelTried = false;
        error = {};
        if (! ensureModel (wanted, error)) break;
        ok = run();
    }

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
    writeCache (job.info, *stems, separator.getQuality());
    setActivity ({}, 0.0);
    return true;
}

bool StemManager::isGpuDeviceLost (const juce::String& error)
{
    // DXGI_ERROR_DEVICE_REMOVED / _HUNG / _RESET, as DirectML reports them
    return error.containsIgnoreCase ("887A0005") || error.containsIgnoreCase ("887A0006") || error.containsIgnoreCase ("887A0007")
        || error.containsIgnoreCase ("device instance has been suspended") || error.containsIgnoreCase ("GetDeviceRemovedReason");
}

//==============================================================================
bool StemManager::writeCache (const TrackInfo& info, const StemAudio& stems, StemQuality q)
{
    const auto dir = cacheFolderFor (info);
    if (! dir.createDirectory()) return false;
    dir.getChildFile ("quality.txt").deleteFile();

    juce::OggVorbisAudioFormat ogg;
    juce::FlacAudioFormat flac;
    const bool lossless = getSettings().lossless;
    // MP3 320 (needs lame.exe; without it the stems are kept as Ogg)
    const bool mp3 = ! lossless && getSettings().mp3 && getSettings().lameExe.existsAsFile();
    juce::LAMEEncoderAudioFormat lame (getSettings().lameExe);
    int mp3Quality = 0;
    if (mp3) { const auto opts = lame.getQualityOptions(); for (int i = 0; i < opts.size(); ++i) if (opts[i].contains ("320")) mp3Quality = i; }
    const juce::String ext = lossless ? ".flac" : mp3 ? ".mp3" : ".ogg";
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

        auto writer = mp3 ? lame.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (stems.sampleRate)
                                                       .withNumChannels (2)
                                                       .withBitsPerSample (16)
                                                       .withQualityOptionIndex (mp3Quality))
                    : lossless ? flac.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                  .withSampleRate (stems.sampleRate)
                                                                  .withNumChannels (2)
                                                                  .withBitsPerSample (24))
                               : ogg.createWriterFor (stream, juce::AudioFormatWriterOptions{}
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
        // one copy only: the other formats go
        for (auto* other : { ".ogg", ".flac", ".mp3" })
            if (ext != other) dir.getChildFile (juce::String (cacheNames[c]) + other).deleteFile();
        tmp.moveFileTo (dir.getChildFile (juce::String (cacheNames[c]) + ext));
    }
    dir.getChildFile ("quality.txt").replaceWithText (juce::String ((int) q));
    return true;
}

StemAudio::Ptr StemManager::readCache (const TrackInfo& info, const TrackAudio& audio)
{
    const auto dir = cacheFolderFor (info);
    juce::OggVorbisAudioFormat ogg;
    juce::FlacAudioFormat flac;

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
        auto f = dir.getChildFile (juce::String (cacheNames[c]) + ".flac");
        const bool isFlac = f.existsAsFile();
        auto mp3File = dir.getChildFile (juce::String (cacheNames[c]) + ".mp3");
        const bool isMp3 = ! isFlac && mp3File.existsAsFile();
        if (! isFlac) f = isMp3 ? mp3File : dir.getChildFile (juce::String (cacheNames[c]) + ".ogg");
        juce::MP3AudioFormat mp3Format;
        std::unique_ptr<juce::AudioFormatReader> reader (isFlac ? flac.createReaderFor (new juce::FileInputStream (f), true)
                                                         : isMp3 ? mp3Format.createReaderFor (new juce::FileInputStream (f), true)
                                                                 : ogg.createReaderFor (new juce::FileInputStream (f), true));
        if (reader == nullptr) return {};
        // MP3 comes back 1105 samples late (LAME's 576 + the decoder's 529): read from there so the stems line up
        const int64_t skip = isMp3 ? 1105 : 0;

        const int s = cacheStemIndex[c];
        const int64_t len = std::min<int64_t> (reader->lengthInSamples - skip, audio.numFrames);
        for (int64_t pos = 0; pos < len; pos += block)
        {
            const int n = (int) std::min<int64_t> (block, len - pos);
            reader->read (&buf, 0, n, pos + skip, true, true);
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
