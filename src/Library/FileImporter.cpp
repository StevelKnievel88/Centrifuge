#include "FileImporter.h"
#include "TagReader.h"
#include "../Audio/MusicalKey.h"
#include "PreviewWaveCache.h"
#include <thread>

namespace orbit
{

namespace
{
    struct Dir
    {
        juce::File dir;
        std::vector<int> files;   // indices into the import's file list
        std::vector<Dir> subs;
        bool hasAudio() const
        {
            if (! files.empty()) return true;
            for (auto& s : subs) if (s.hasAudio()) return true;
            return false;
        }
    };

    int addFile (const juce::File& f, juce::Array<juce::File>& all)
    {
        const int i = all.indexOf (f);
        if (i >= 0) return i;
        all.add (f);
        return all.size() - 1;
    }

    Dir scan (const juce::File& dir, juce::Array<juce::File>& all, int depth = 0)
    {
        Dir d { dir, {}, {} };
        if (depth > 12) return d;
        auto entries = dir.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);
        entries.sort();
        for (auto& e : entries)
        {
            if (e.isDirectory()) { auto s = scan (e, all, depth + 1); if (s.hasAudio()) d.subs.push_back (std::move (s)); }
            else if (FileImporter::isAudioFile (e)) d.files.push_back (addFile (e, all));
        }
        return d;
    }

    /** A folder as playlists: a folder with sub-folders becomes a playlist folder (its own tracks go in a playlist
        of the same name inside it); a folder of tracks only becomes a playlist. */
    PlaylistNode toNode (const Dir& d, int& count)
    {
        PlaylistNode n;
        n.name = d.dir.getFileName();
        if (d.subs.empty())
        {
            n.trackIndices = d.files;
            ++count;
            return n;
        }
        n.isFolder = true;
        if (! d.files.empty())
        {
            PlaylistNode own;
            own.name = d.dir.getFileName();
            own.trackIndices = d.files;
            n.children.push_back (std::move (own));
            ++count;
        }
        for (auto& s : d.subs) n.children.push_back (toNode (s, count));
        return n;
    }
}

bool FileImporter::isAudioFile (const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension ("mp3;wav;wave;aif;aiff;flac;m4a;aac;mp4;alac");
}

bool FileImporter::analyseFile (AudioFileLoader& loader, const juce::File& f, const Options& o, TrackInfo& info,
                                TrackVersion& version, juce::String& error, const std::atomic<bool>* cancel)
{
    const auto tags = AudioTags::read (f);
    auto audio = loader.load (f, error, cancel);
    if (audio == nullptr || audio->numFrames <= 0)
    {
        if (error.isEmpty()) error = "Couldn't read " + f.getFileName();
        return false;
    }

    TrackAnalyser::Options ao;
    ao.minBpm = o.minBpm;
    ao.maxBpm = o.maxBpm;
    ao.hintBpm = tags.bpm;
    const auto r = TrackAnalyser::analyse (*audio, ao, cancel);
    if (cancel != nullptr && cancel->load()) return false;

    // The browser's preview waveform, kept so it shows straight away from now on
    if (auto w = WaveformData::fromAudio (*audio))
        PreviewWaveCache::save (f.getFullPathName(), PreviewWaveCache::make (*w));

    info = {};
    info.file = f;
    info.title = tags.title;
    info.artist = tags.artist;
    info.album = tags.album;
    info.genre = tags.genre;
    info.comment = tags.comment;
    info.label = tags.label;
    info.durationSec = audio->lengthSeconds();
    info.sampleRate = (int) audio->sampleRate;
    info.dateAdded = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
    info.bpm = r.bpm;

    // The file's own key tag (e.g. from Mixed In Key or the store) wins over our estimate.
    auto k = MusicalKey::parse (tags.key);
    if (k.isValid()) { k.camelotStyle = false; info.key = k.toString(); }
    else info.key = r.key;

    const bool lossless = f.hasFileExtension ("wav;wave;aif;aiff");
    info.bitrate = lossless ? (int) (audio->sampleRate * 16 * 2 / 1000)
                            : (int) std::lround ((double) f.getSize() * 8.0 / juce::jmax (1.0, info.durationSec) / 1000.0);

    // Grids are stored in rekordbox's time: undo the app's MP3 decoder correction.
    const double shift = f.hasFileExtension ("mp3") ? o.mp3OffsetMs / 1000.0 : 0.0;
    version = {};
    version.beats = r.beats;
    for (auto& b : version.beats) b.timeSec -= shift;
    version.beats.erase (std::remove_if (version.beats.begin(), version.beats.end(), [] (const Beat& b) { return b.timeSec < 0.0; }),
                         version.beats.end());
    return true;
}

FileImporter::Result FileImporter::run (CentrifugeLibrary& library, AudioFileLoader& loader, const juce::Array<juce::File>& items,
                                        const Options& o, std::function<bool (float, const juce::String&)> progress)
{
    Result res;
    juce::Array<juce::File> all;
    std::vector<Dir> dirs;
    for (auto& item : items)
    {
        if (item.isDirectory()) { auto d = scan (item, all); if (d.hasAudio()) dirs.push_back (std::move (d)); }
        else if (isAudioFile (item)) addFile (item, all);
    }
    if (all.isEmpty()) { res.errors.add ("No audio files found"); return res; }

    std::vector<juce::int64> ids ((size_t) all.size(), 0);
    std::vector<int> todo;

    // Tracks already in the library (same file name and length) are just linked, not analysed again.
    for (int i = 0; i < all.size(); ++i)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (loader.getFormatManager().createReaderFor (all[i]));
        if (reader != nullptr && reader->sampleRate > 0)
            if (auto t = library.find (CentrifugeLibrary::keyForFile (all[i], (double) reader->lengthInSamples / reader->sampleRate)))
            {
                ids[(size_t) i] = t->id;
                ++res.alreadyThere;
                continue;
            }
        todo.push_back (i);
    }

    std::atomic<bool> cancel { false };
    std::atomic<int> next { 0 }, done { 0 };
    juce::CriticalSection resLock;
    const int threads = o.threads > 0 ? o.threads
                                      : juce::jlimit (1, 3, (int) std::thread::hardware_concurrency() / 2);
    juce::String current;

    auto worker = [&]
    {
        for (;;)
        {
            const int n = next++;
            if (n >= (int) todo.size() || cancel.load()) return;
            const int i = todo[(size_t) n];
            { const juce::ScopedLock sl (resLock); current = all[i].getFileName(); }
            TrackInfo info;
            TrackVersion version;
            juce::String error;
            if (analyseFile (loader, all[i], o, info, version, error, &cancel))
            {
                auto [id, isNew] = library.addAnalysedTrack (info, version, "files");
                const juce::ScopedLock sl (resLock);
                ids[(size_t) i] = id;
                if (isNew) ++res.added; else ++res.alreadyThere;
            }
            else if (! cancel.load())
            {
                const juce::ScopedLock sl (resLock);
                ++res.failed;
                res.errors.add (all[i].getFileName() + ": " + error);
            }
            ++done;
        }
    };

    std::vector<std::thread> pool;
    for (int t = 0; t < juce::jmin (threads, (int) todo.size()); ++t) pool.emplace_back (worker);
    while (done.load() < (int) todo.size() && ! cancel.load())
    {
        juce::String name;
        { const juce::ScopedLock sl (resLock); name = current; }
        if (progress && ! progress (todo.empty() ? 1.0f : (float) done.load() / (float) todo.size(), name))
            cancel = true;
        juce::Thread::sleep (100);
    }
    for (auto& th : pool) th.join();
    res.cancelled = cancel.load();

    // Folders become playlists (only the tracks that made it into the library)
    for (auto& d : dirs)
    {
        PlaylistNode root;
        root.isFolder = true;
        root.children.push_back (toNode (d, res.playlists));
        library.setPlaylists ("folder:" + d.dir.getFullPathName(), root, ids);
    }
    return res;
}

} // namespace orbit
