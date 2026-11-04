#include "TrackLoader.h"
#include "../Library/Anlz.h"

namespace orbit
{

TrackLoader::TrackLoader (AudioEngine& e, AudioFileLoader& l, StemManager& s)
    : engine (e), fileLoader (l), stemManager (s)
{
    for (auto& p : pending) p = 0;
    for (auto& b : loading) b = false;
    for (auto& c : cancel) c = false;
}

TrackLoader::~TrackLoader()
{
    for (auto& c : cancel) c = true;
    pool.removeAllJobs (true, 10000);
}

void TrackLoader::applyVersion (LoadedTrack& t, const TrackVersion& v)
{
    const double shift = t.cueShift;
    auto beats = v.beats;
    for (auto& b : beats) b.timeSec += shift;
    const double length = t.audio != nullptr ? t.audio->lengthSeconds() : t.info.durationSec;
    t.grid = BeatGrid (std::move (beats), t.info.bpm, length);

    auto shifted = [shift] (CuePoint c)
    {
        c.startSec += shift;
        if (c.endSec > 0) c.endSec += shift;
        return c;
    };
    for (size_t i = 0; i < 8; ++i)
    {
        t.hotCueSet[i] = v.hot[i].has_value();
        t.hotCues[i] = v.hot[i] ? shifted (*v.hot[i]) : CuePoint();
    }
    t.memoryCues.clear();
    for (auto& m : v.memory) t.memoryCues.push_back (shifted (m));
    std::sort (t.memoryCues.begin(), t.memoryCues.end(), [] (auto& a, auto& b) { return a.startSec < b.startSec; });
    t.skips.clear();
    for (auto& k : v.skips) t.skips.push_back ({ k.first + shift, k.second + shift });
}

LoadedTrack::Ptr TrackLoader::withVersion (const LoadedTrack& b, const TrackVersion& v)
{
    LoadedTrack::Ptr t = new LoadedTrack();
    t->info = b.info;
    t->audio = b.audio;
    t->waveform = b.waveform;
    t->detail = b.detail;
    t->anlzWave = b.anlzWave;
    t->styled = b.styled;
    t->serial = b.serial;
    t->cueKey = b.cueKey;
    t->cueShift = b.cueShift;
    applyVersion (*t, v);
    return t;
}

TrackVersion TrackLoader::versionOf (const LoadedTrack& t)
{
    TrackVersion v;
    const double shift = t.cueShift;
    v.beats = t.grid.getBeats();
    for (auto& b : v.beats) b.timeSec -= shift;
    auto unshifted = [shift] (CuePoint c)
    {
        c.startSec -= shift;
        if (c.endSec > 0) c.endSec -= shift;
        return c;
    };
    for (size_t i = 0; i < 8; ++i)
        if (t.hotCueSet[i]) v.hot[i] = unshifted (t.hotCues[i]);
    for (auto& m : t.memoryCues) v.memory.push_back (unshifted (m));
    for (auto& k : t.skips) v.skips.push_back ({ k.first - shift, k.second - shift });
    return v;
}

LoadedTrack::Ptr TrackLoader::build (const TrackInfo& info, TrackAudio::Ptr audio, bool fromPc, const Options& o,
                                     const std::optional<TrackVersion>& version)
{
    LoadedTrack::Ptr t = new LoadedTrack();
    t->info = info;
    t->audio = audio;

    const auto anlz = loadAnlz (info.anlzBase);

    // MP3 decoders differ in how much priming they skip; allow a global correction.
    double shift = 0.0;
    if (info.file.hasFileExtension ("mp3"))
        shift = o.mp3OffsetMs / 1000.0;

    auto beats = anlz.beats;
    for (auto& b : beats) b.timeSec += shift;
    const double length = audio != nullptr ? audio->lengthSeconds() : info.durationSec;
    t->grid = BeatGrid (std::move (beats), info.bpm, length);

    const auto& cues = (fromPc && o.preferDbCues && ! info.dbCues.empty()) ? info.dbCues : anlz.cues;
    for (auto c : cues)
    {
        c.startSec += shift;
        if (c.endSec > 0) c.endSec += shift;
        if (c.type == CuePoint::Type::hot && c.hotIndex >= 0 && c.hotIndex < 8)
        {
            t->hotCues[(size_t) c.hotIndex] = c;
            t->hotCueSet[(size_t) c.hotIndex] = true;
        }
        else if (c.type == CuePoint::Type::memory)
        {
            t->memoryCues.push_back (c);
        }
    }
    // Centrifuge's own edits replace rekordbox's cues (rekordbox's data is never changed).
    t->cueShift = shift;
    t->cueKey = CueStore::keyFor (info, length);
    if (o.cueStore != nullptr && ! version.has_value())
        if (auto e = o.cueStore->get (t->cueKey))
        {
            if (e->hotCues)
                for (size_t i = 0; i < 8; ++i)
                {
                    t->hotCueSet[i] = (*e->hotCues)[i].has_value();
                    if (t->hotCueSet[i])
                    {
                        auto c = *(*e->hotCues)[i];
                        c.startSec += shift;
                        if (c.endSec > 0) c.endSec += shift;
                        t->hotCues[i] = c;
                    }
                }
            if (e->locators)
            {
                t->memoryCues.clear();
                for (auto c : *e->locators)
                {
                    c.startSec += shift;
                    if (c.endSec > 0) c.endSec += shift;
                    t->memoryCues.push_back (c);
                }
            }
            for (auto& k : e->skips)
                t->skips.push_back ({ k.first + shift, k.second + shift });
        }
    std::sort (t->memoryCues.begin(), t->memoryCues.end(), [] (auto& a, auto& b) { return a.startSec < b.startSec; });

    t->anlzWave = anlz.waveforms;
    if (anlz.waveforms.numDetailEntries() > 0)
        t->waveform = WaveformData::fromAnlz (anlz.waveforms, WaveformData::Style::threeBand);
    else if (audio != nullptr)
        t->waveform = WaveformData::fromAudio (*audio);
    if (audio != nullptr)
        t->detail = WaveformDetail::fromAudio (*audio);

    if (version.has_value())
        applyVersion (*t, *version);

    return t;
}

void TrackLoader::load (int deck, const TrackInfo& info, bool fromPc, std::optional<TrackVersion> version)
{
    if (deck < 0 || deck >= AudioEngine::maxDecks) return;

    const int serial = ++serialCounter;
    pending[(size_t) deck] = serial;
    loading[(size_t) deck] = true;
    cancel[(size_t) deck] = true;   // stop any decode still running for this deck

    Options opts;
    {
        const juce::ScopedLock sl (lock);
        opts = options;
    }

    pool.addJob ([this, deck, info, fromPc, serial, opts, version]
    {
        cancel[(size_t) deck] = false;
        juce::String error;
        auto audio = fileLoader.load (info.file, error, &cancel[(size_t) deck]);

        if (pending[(size_t) deck].load() != serial) return; // superseded

        if (audio == nullptr)
        {
            juce::MessageManager::callAsync ([this, deck, error, serial]
            {
                if (pending[(size_t) deck].load() != serial) return;
                loading[(size_t) deck] = false;
                if (onError) onError (deck, error);
            });
            return;
        }

        auto track = build (info, audio, fromPc, opts, version);
        track->serial = serial;

        juce::MessageManager::callAsync ([this, deck, track, serial]
        {
            if (pending[(size_t) deck].load() != serial) return;
            loading[(size_t) deck] = false;

            auto& d = engine.deck (deck);
            engine.getReleasePool().add (track);
            d.setLoadedTrackForUi (track);

            Deck::Command c;
            c.cmd = Deck::Cmd::load;
            c.ptr = track.get();
            d.post (c);

            stemManager.requestForDeck (deck, track->info, track->audio, serial, [this, deck, serial] (StemAudio::Ptr stems)
            {
                if (pending[(size_t) deck].load() != serial || stems == nullptr) return;
                engine.getReleasePool().add (stems);
                engine.deck (deck).setStemsForUi (stems);
                Deck::Command sc;
                sc.cmd = Deck::Cmd::attachStems;
                sc.ptr = stems.get();
                sc.serial = serial;
                engine.deck (deck).post (sc);
            });

            if (onLoaded) onLoaded (deck, track);
        });
    });
}

} // namespace orbit
