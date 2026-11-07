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
    if (v.phrases)
    {
        t.phrases.clear();
        for (auto p : *v.phrases)
        {
            p.startSec += shift;
            if (p.endSec > 0) p.endSec += shift;
            t.phrases.push_back (p);
        }
    }
    t.stemLocs.clear();
    for (auto l : v.stemLocs)
    {
        l.timeSec += shift;
        // fade length in seconds at the tempo where it starts
        const double beatLen = t.grid.isValid() ? t.grid.beatLengthAt (l.timeSec) : (t.info.bpm > 0 ? 60.0 / t.info.bpm : 0.5);
        l.fadeSec = juce::jmax (0.0, (double) l.fadeBeats * beatLen);
        t.stemLocs.push_back (l);
    }
    std::sort (t.stemLocs.begin(), t.stemLocs.end(), [] (auto& a, auto& b) { return a.timeSec < b.timeSec; });
    t.notes.clear();
    for (auto n : v.notes) { n.timeSec += shift; t.notes.push_back (n); }
    t.arrangement.clear();
    for (auto a : v.arrangement) t.arrangement.push_back (a + shift);
    t.playMap = compilePlayMap (t.phrases, t.arrangement, length);
}

std::vector<PlaySegment> TrackLoader::compilePlayMap (const std::vector<Phrase>& phrasesIn, const std::vector<double>& arrangement, double length)
{
    std::vector<PlaySegment> map;
    auto phrases = phrasesIn;
    std::sort (phrases.begin(), phrases.end(), [] (auto& a, auto& b) { return a.startSec < b.startSec; });
    const int n = (int) phrases.size();
    if (n == 0 || length <= 0.0) return map;

    bool anyRepeat = false;
    for (auto& p : phrases) anyRepeat = anyRepeat || p.repeats > 1;
    if (arrangement.empty() && ! anyRepeat) return map;

    // Play order: the arrangement (by phrase start), then any phrase it leaves out, in track order.
    std::vector<int> order;
    std::vector<bool> used ((size_t) n, false);
    for (auto a : arrangement)
        for (int i = 0; i < n; ++i)
            if (! used[(size_t) i] && std::abs (phrases[(size_t) i].startSec - a) < 0.05) { order.push_back (i); used[(size_t) i] = true; break; }
    for (int i = 0; i < n; ++i) if (! used[(size_t) i]) order.push_back (i);

    auto endOf = [&] (int i) { return i + 1 < n ? phrases[(size_t) i + 1].startSec : length; };
    if (phrases[0].startSec > 0.01) map.push_back ({ 0.0, phrases[0].startSec });   // anything before the first phrase
    for (int i : order)
    {
        const double s = juce::jlimit (0.0, length, phrases[(size_t) i].startSec), e = juce::jlimit (0.0, length, endOf (i));
        if (e <= s + 0.01) continue;
        for (int r = 0; r < juce::jlimit (1, 64, phrases[(size_t) i].repeats); ++r)
        {
            if (! map.empty() && std::abs (map.back().end - s) < 0.001) map.back().end = e;   // carries straight on
            else map.push_back ({ s, e });
        }
    }
    if (map.size() == 1 && map[0].start < 0.01 && map[0].end > length - 0.01) map.clear();   // nothing changed
    return map;
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
    t->phrases = b.phrases;
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

    for (auto p : anlz.phrases)
    {
        p.startSec += shift;
        if (p.endSec > 0) p.endSec += shift;
        t->phrases.push_back (p);
    }

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
    if (deck < 0 || deck >= AudioEngine::totalDecks) return;

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

            // PREP uses stems already made (so stem locators can be heard there); it never starts a separation.
            // Browser previews never use stems.
            if (deck < AudioEngine::maxDecks || (deck == AudioEngine::prepDeck && stemManager.hasCachedStems (track->info)))
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
