#include "TrackEditor.h"
#include "Features.h"
#include "../Library/GridOps.h"

namespace orbit
{

namespace
{
    constexpr double sameTime = 0.002;

    bool sameCue (const std::optional<CuePoint>& a, const std::optional<CuePoint>& b)
    {
        if (a.has_value() != b.has_value()) return false;
        if (! a) return true;
        return std::abs (a->startSec - b->startSec) < sameTime && std::abs (a->endSec - b->endSec) < sameTime
            && a->colour.getARGB() == b->colour.getARGB();
    }

    int findAt (const std::vector<CuePoint>& v, double t)
    {
        for (int i = 0; i < (int) v.size(); ++i)
            if (std::abs (v[(size_t) i].startSec - t) < sameTime) return i;
        return -1;
    }

    int findSkip (const std::vector<std::pair<double, double>>& v, const std::pair<double, double>& k)
    {
        for (int i = 0; i < (int) v.size(); ++i)
            if (std::abs (v[(size_t) i].first - k.first) < sameTime && std::abs (v[(size_t) i].second - k.second) < sameTime) return i;
        return -1;
    }

    void sortMemory (TrackVersion& v)
    {
        std::sort (v.memory.begin(), v.memory.end(), [] (auto& a, auto& b) { return a.startSec < b.startSec; });
        std::sort (v.skips.begin(), v.skips.end());
    }
}

TrackEditor::TrackEditor (AudioEngine& e, CentrifugeLibrary& l) : engine (e), library (l) {}
TrackEditor::~TrackEditor() { flushAllSaves(); }

void TrackEditor::flushSave (const Session& s) const
{
    if (! s.pendR && ! s.pendC) return;
    if (s.libId > 0) library.saveVersions (s.libId, s.pendR ? &s.rb : nullptr, s.pendC ? &s.cf : nullptr);
    s.pendR = s.pendC = false;
    s.pendSince = 0;
}

void TrackEditor::flushAllSaves() const
{
    for (auto& s : sessions) flushSave (s);
}

std::optional<TrackVersion> TrackEditor::versionToLoad (int deck, const TrackInfo& info) const
{
    flushAllSaves();
    const auto key = info.libraryKey.isNotEmpty() ? info.libraryKey : CueStore::keyFor (info, info.durationSec);
    if (auto t = library.find (key))
        return sessions[(size_t) deck].c ? t->cf : t->rb;
    return std::nullopt;
}

void TrackEditor::trackLoaded (int deck, LoadedTrack::Ptr track)
{
    auto& s = sessions[(size_t) deck];
    flushSave (s);
    s.track = track;
    s.serial = track != nullptr ? track->serial : -1;
    s.undo.clear();
    s.taps.clear();
    s.libId = 0;
    s.dataVersion = -1;
    if (track == nullptr) return;

    if (auto t = library.find (track->cueKey))
    {
        s.libId = t->id;
        s.rb = t->rb;
        s.cf = t->cf;
    }
    else
    {
        s.rb = TrackLoader::versionOf (*track);
        s.cf = s.rb;
    }
    s.last = snapOf (playing (s));
    s.edits = engine.deck (deck).state().cueEdits.load();
}

TrackEditor::Snap TrackEditor::snapFromDeck (int deck, double shift) const
{
    const auto& st = engine.deck (deck).state();
    Snap n;
    for (size_t i = 0; i < 8; ++i)
    {
        const double hs = st.hotStart[i].load();
        if (hs < 0.0) continue;
        CuePoint c;
        c.type = CuePoint::Type::hot;
        c.hotIndex = (int) i;
        c.startSec = hs - shift;
        const double he = st.hotEnd[i].load();
        c.endSec = he > hs ? he - shift : -1.0;
        c.colour = juce::Colour (st.hotColour[i].load());
        n.hot[i] = c;
    }
    for (int i = 0; i < juce::jmin (st.numLocators.load(), Deck::maxLocators); ++i)
    {
        CuePoint c;
        c.startSec = st.locStart[(size_t) i].load() - shift;
        const double le = st.locEnd[(size_t) i].load();
        c.endSec = le > 0.0 ? le - shift : -1.0;
        c.colour = juce::Colour (st.locColour[(size_t) i].load());
        c.loopMode = st.locMode[(size_t) i].load();
        c.cycles = st.locCycles[(size_t) i].load();
        n.locs.push_back (c);
    }
    for (int i = 0; i < juce::jmin (st.numSkips.load(), Deck::maxSkips); ++i)
        n.skips.push_back ({ st.skipFrom[(size_t) i].load() - shift, st.skipTo[(size_t) i].load() - shift });
    return n;
}

TrackEditor::Snap TrackEditor::snapOf (const TrackVersion& v)
{
    Snap n;
    n.hot = v.hot;
    n.locs = v.memory;
    n.skips = v.skips;
    return n;
}

void TrackEditor::applyFull (TrackVersion& t, const Snap& now)
{
    // Everything as on the deck, keeping cue names (the deck doesn't carry them).
    for (size_t i = 0; i < 8; ++i)
    {
        auto c = now.hot[i];
        if (c && t.hot[i] && std::abs (t.hot[i]->startSec - c->startSec) < sameTime) c->comment = t.hot[i]->comment;
        t.hot[i] = c;
    }
    std::vector<CuePoint> mem;
    for (auto c : now.locs)
    {
        if (const int k = findAt (t.memory, c.startSec); k >= 0) c.comment = t.memory[(size_t) k].comment;
        c.type = CuePoint::Type::memory;
        mem.push_back (c);
    }
    t.memory = mem;
    t.skips = now.skips;
    sortMemory (t);
}

void TrackEditor::applyDiff (TrackVersion& t, const Snap& before, const Snap& now)
{
    // Only what changed on the deck is copied, so the other version keeps its own differences.
    for (size_t i = 0; i < 8; ++i)
        if (! sameCue (before.hot[i], now.hot[i]))
        {
            auto c = now.hot[i];
            if (c && t.hot[i] && std::abs (t.hot[i]->startSec - c->startSec) < sameTime) c->comment = t.hot[i]->comment;
            t.hot[i] = c;
        }

    for (auto& old : before.locs)
        if (findAt (now.locs, old.startSec) < 0)
            if (const int k = findAt (t.memory, old.startSec); k >= 0) t.memory.erase (t.memory.begin() + k);
    for (auto& c : now.locs)
    {
        const int b = findAt (before.locs, c.startSec);
        const int k = findAt (t.memory, c.startSec);
        if (b < 0 && k < 0) { auto m = c; m.type = CuePoint::Type::memory; t.memory.push_back (m); }
        else if (b >= 0 && k >= 0)
        {
            const auto& was = before.locs[(size_t) b];
            auto& m = t.memory[(size_t) k];
            if (was.colour != c.colour) m.colour = c.colour;
            if (was.loopMode != c.loopMode || was.cycles != c.cycles) { m.loopMode = c.loopMode; m.cycles = c.cycles; }
        }
    }

    for (auto& old : before.skips)
        if (findSkip (now.skips, old) < 0)
            if (const int k = findSkip (t.skips, old); k >= 0) t.skips.erase (t.skips.begin() + k);
    for (auto& k : now.skips)
        if (findSkip (before.skips, k) < 0 && findSkip (t.skips, k) < 0) t.skips.push_back (k);
    sortMemory (t);
}

void TrackEditor::ensureInLibrary (Session& s)
{
    if (s.libId > 0 || s.track == nullptr) return;
    s.libId = library.addTrack (s.track->info, s.rb);
}

void TrackEditor::save (Session& s, bool rb, bool cf)
{
    ensureInLibrary (s);
    s.pendR = s.pendR || rb;
    s.pendC = s.pendC || cf;
    if (s.pendSince == 0) s.pendSince = juce::jmax (1u, juce::Time::getMillisecondCounter());
}

void TrackEditor::swapDeck (int deck, const TrackVersion& v)
{
    auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return;
    auto t = TrackLoader::withVersion (*s.track, v);
    engine.getReleasePool().add (t);
    engine.deck (deck).replaceLoadedTrackForUi (t);
    Deck::Command c;
    c.cmd = Deck::Cmd::swapTrackData;
    c.ptr = t.get();
    engine.deck (deck).post (c);
    s.track = t;
    s.last = snapOf (v);
    s.dataVersion = -1;   // re-read the deck once it has swapped
}

void TrackEditor::poll()
{
    const auto now = juce::Time::getMillisecondCounter();
    for (auto& s : sessions)
        if (s.pendSince != 0 && now - s.pendSince > 400) flushSave (s);
    for (int d = 0; d < (int) sessions.size(); ++d)
    {
        auto& s = sessions[(size_t) d];
        const auto& st = engine.deck (d).state();
        if (s.track == nullptr || st.trackSerial.load() != s.serial) continue;

        const int dv = st.trackDataVersion.load();
        if (dv != s.dataVersion)
            s.dataVersion = dv;   // s.last already holds what the deck was given
        const int edits = st.cueEdits.load();
        if (edits == s.edits) continue;
        s.edits = edits;

        const auto now = snapFromDeck (d, s.track->cueShift);
        const bool playingCf = s.c;
        applyFull (playingCf ? s.cf : s.rb, now);
        if (playingCf && s.r)
            applyDiff (s.rb, s.last, now);

        bool resync = false;
        if (s.r)
        {
            auto limited = s.rb.limitedForRekordbox();
            if (! playingCf && ! limited.sameAs (s.rb))
            {
                resync = true;
                if (onMessage)
                    onMessage ("rekordbox can't hold that (skips, more than " + juce::String (TrackVersion::rekordboxMaxMemoryCues)
                               + " memory cues, or that exact colour) - turn on C to keep it in Centrifuge");
            }
            s.rb = limited;
        }
        save (s, s.r, s.c);
        if (resync) swapDeck (d, s.rb);
        else s.last = now;
        syncOthers (d);
    }
}

void TrackEditor::syncOthers (int deck)
{
    const auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return;
    for (int o = 0; o < (int) sessions.size(); ++o)
    {
        auto& so = sessions[(size_t) o];
        if (o == deck || so.track == nullptr || so.track->cueKey != s.track->cueKey) continue;
        if (engine.deck (o).state().trackSerial.load() != so.serial) continue;
        so.rb = s.rb;
        so.cf = s.cf;
        so.libId = s.libId;
        swapDeck (o, playing (so));
    }
}

void TrackEditor::setEditTargets (int deck, bool r, bool c)
{
    if (! r && ! c) return;   // one of them must stay on
    auto& s = sessions[(size_t) deck];
    const bool wasCf = s.c;
    s.r = r;
    s.c = c;
    if (s.track != nullptr && wasCf != s.c)
        swapDeck (deck, playing (s));
    if (onTargetsChanged) onTargetsChanged (deck, r, c);
}

bool TrackEditor::versionsDiffer (int deck) const
{
    const auto& s = sessions[(size_t) deck];
    return s.track != nullptr && ! s.rb.sameAs (s.cf);
}

double TrackEditor::currentBpm (int deck) const
{
    const auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return 0.0;
    const double pos = engine.deck (deck).state().positionSec.load() - s.track->cueShift;
    return GridOps::bpmAt (playing (s).beats, pos, s.track->info.bpm);
}

std::vector<float> TrackEditor::lowOnsets (const TrackAudio& a, double& hop)
{
    // Kick strength over time: low-passed (~150 Hz) energy every `hop` seconds, and how much it rises
    juce::IIRFilter lp1, lp2;
    lp1.setCoefficients (juce::IIRCoefficients::makeLowPass (a.sampleRate, 150.0));
    lp2.setCoefficients (juce::IIRCoefficients::makeLowPass (a.sampleRate, 150.0));
    const int step = juce::jmax (1, (int) (a.sampleRate * hop));
    hop = step / a.sampleRate;   // the real hop (whole samples)
    std::vector<float> flux;
    flux.reserve ((size_t) (a.numFrames / step + 1));
    float prev = 0.0f;
    for (int64_t s = 0; s + step <= a.numFrames; s += step)
    {
        double e = 0.0;
        for (int64_t i = s; i < s + step; ++i)
        {
            const float v = lp2.processSingleSampleRaw (lp1.processSingleSampleRaw (0.5f * (a.left[(size_t) i] + a.right[(size_t) i])));
            e += (double) v * v;
        }
        const float le = std::log1p ((float) (e / step) * 1000.0f);
        flux.push_back (juce::jmax (0.0f, le - prev));
        prev = le;
    }
    return flux;
}

void TrackEditor::gridAction (int deck, const juce::String& action)
{
    auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return;
    const auto& st = engine.deck (deck).state();
    const double pos = st.positionSec.load() - s.track->cueShift;
    const double len = s.track->audio != nullptr ? s.track->audio->lengthSeconds() : s.track->info.durationSec;
    const double fallbackBpm = s.track->info.bpm > 0 ? s.track->info.bpm : 120.0;

    if (action == "undo")
    {
        if (s.undo.empty()) { if (onMessage) onMessage ("Nothing to undo"); return; }
        auto [rb, cf] = s.undo.back();
        s.undo.pop_back();
        s.rb.beats = rb;
        s.cf.beats = cf;
        save (s, true, true);
        swapDeck (deck, playing (s));
        syncOthers (deck);
        return;
    }

    std::function<std::vector<Beat> (const std::vector<Beat>&)> op;
    if (action == "shift.back10")      op = [] (auto& b) { return GridOps::shift (b, -0.010); };
    else if (action == "shift.back1")  op = [] (auto& b) { return GridOps::shift (b, -0.001); };
    else if (action == "shift.fwd1")   op = [] (auto& b) { return GridOps::shift (b, 0.001); };
    else if (action == "shift.fwd10")  op = [] (auto& b) { return GridOps::shift (b, 0.010); };
    else if (action == "anchor")       op = [pos] (auto& b) { return GridOps::anchorAt (b, pos); };
    else if (action == "downbeat")     op = [pos] (auto& b) { return GridOps::setDownbeat (b, pos); };
    else if (action == "barhere")      op = [pos] (auto& b) { return GridOps::barStartsHere (b, pos); };
    else if (action == "beat.add")     op = [pos] (auto& b) { return GridOps::shiftCount (b, pos, 1); };
    else if (action == "beat.remove")  op = [pos] (auto& b) { return GridOps::shiftCount (b, pos, -1); };
    else if (action == "follow" || action == "follow.here")
    {
        if (s.track->audio == nullptr) return;
        double hop = 0.0025;
        auto flux = std::make_shared<std::vector<float>> (lowOnsets (*s.track->audio, hop));
        const double offset = s.track->cueShift;
        const bool here = action == "follow.here";
        op = [=] (auto& b) { return GridOps::followKicks (b, *flux, hop, offset, pos, len, here); };
        if (onMessage) onMessage ("Grid follows the kicks" + juce::String (here ? " from here" : "") + " - UNDO puts it back");
    }
    else if (action == "halve")        op = [=] (auto& b) { return GridOps::halve (b, pos, len, fallbackBpm); };
    else if (action == "double")       op = [=] (auto& b) { return GridOps::doubleTempo (b, pos, len, fallbackBpm); };
    else if (action.startsWith ("bpm.") || action.startsWith ("bpm="))
    {
        double delta = 0.0, target = -1.0;
        if (action == "bpm.up") delta = 0.1;
        else if (action == "bpm.down") delta = -0.1;
        else if (action == "bpm.upfine") delta = 0.01;
        else if (action == "bpm.downfine") delta = -0.01;
        else target = action.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        op = [=] (auto& b)
        {
            const double bpm = target > 0.0 ? target : std::round ((GridOps::bpmAt (b, pos, fallbackBpm) + delta) * 100.0) / 100.0;
            return GridOps::setBpm (b, bpm, pos, len);
        };
    }
    else if (action == "tap")
    {
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        if (! s.taps.empty() && now - s.taps.back() > 2.0) s.taps.clear();
        s.taps.push_back (now);
        if (s.taps.size() < 4)
        {
            if (onMessage) onMessage ("Tap " + juce::String ((int) s.taps.size()) + " - keep tapping on the beat");
            return;
        }
        const double bpm = std::round (60.0 * (double) (s.taps.size() - 1) / (s.taps.back() - s.taps.front()) * 100.0) / 100.0;
        if (onMessage) onMessage ("Tapped " + juce::String (bpm, 2) + " BPM");
        op = [=] (auto& b) { return GridOps::setBpm (b, bpm, pos, len); };
    }
    else if (action == "reset")
    {
        std::vector<Beat> orig;
        if (auto t = library.get (s.libId)) orig = t->importedGrid;
        if (orig.empty()) orig = CentrifugeLibrary::readRekordboxVersion (s.track->info).beats;
        op = [orig] (auto&) { return orig; };
    }
    else return;

    s.undo.push_back ({ s.rb.beats, s.cf.beats });
    if (s.undo.size() > 50) s.undo.erase (s.undo.begin());
    if (s.r) s.rb.beats = op (s.rb.beats.empty() ? TrackLoader::versionOf (*s.track).beats : s.rb.beats);
    if (s.c) s.cf.beats = op (s.cf.beats.empty() ? TrackLoader::versionOf (*s.track).beats : s.cf.beats);
    save (s, s.r, s.c);
    swapDeck (deck, playing (s));
    syncOthers (deck);
}

void TrackEditor::renameCue (int deck, bool hotCue, int index, const juce::String& name)
{
    auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return;
    const auto& st = engine.deck (deck).state();
    const double shift = s.track->cueShift;

    auto apply = [&] (TrackVersion& v)
    {
        if (hotCue)
        {
            if (index >= 0 && index < 8 && v.hot[(size_t) index]) v.hot[(size_t) index]->comment = name;
        }
        else if (index >= 0 && index < Deck::maxLocators)
        {
            const double t = st.locStart[(size_t) index].load() - shift;
            if (const int k = findAt (v.memory, t); k >= 0) v.memory[(size_t) k].comment = name;
        }
    };
    if (s.r) apply (s.rb);
    if (s.c) apply (s.cf);
    save (s, s.r, s.c);
    swapDeck (deck, playing (s));
    syncOthers (deck);
}

const std::vector<Phrase>& TrackEditor::phrases (int deck) const
{
    static const std::vector<Phrase> none;
    const auto& s = sessions[(size_t) deck];
    return s.track != nullptr ? s.track->phrases : none;
}

void TrackEditor::editPhrases (int deck, const std::function<void (std::vector<Phrase>&)>& fn)
{
    auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return;
    const double shift = s.track->cueShift;
    std::vector<Phrase> list;
    for (auto p : s.track->phrases) { p.startSec -= shift; if (p.endSec > 0) p.endSec -= shift; list.push_back (p); }
    fn (list);
    std::sort (list.begin(), list.end(), [] (auto& a, auto& b) { return a.startSec < b.startSec; });
    if (s.r) s.rb.phrases = list;
    if (s.c) s.cf.phrases = list;
    save (s, s.r, s.c);
    auto v = playing (s);
    if (! v.phrases) v.phrases = list;
    swapDeck (deck, v);
    syncOthers (deck);
}

void TrackEditor::addPhrase (int deck, double atSec, const juce::String& name, juce::Colour colour)
{
    const double shift = hasTrack (deck) ? sessions[(size_t) deck].track->cueShift : 0.0;
    editPhrases (deck, [=] (std::vector<Phrase>& list)
    {
        Phrase p;
        p.startSec = atSec - shift;
        p.name = name;
        p.colour = colour;
        // replaces a phrase that starts at the same place
        list.erase (std::remove_if (list.begin(), list.end(), [&] (auto& x) { return std::abs (x.startSec - p.startSec) < 0.01; }), list.end());
        list.push_back (p);
    });
}

void TrackEditor::setPhrase (int deck, int index, const Phrase& p)
{
    const double shift = hasTrack (deck) ? sessions[(size_t) deck].track->cueShift : 0.0;
    editPhrases (deck, [=] (std::vector<Phrase>& list)
    {
        if (index < 0 || index >= (int) list.size()) return;
        auto q = p;
        q.startSec -= shift;
        if (q.endSec > 0) q.endSec -= shift;
        list[(size_t) index] = q;
    });
}

void TrackEditor::setPhrases (int deck, const std::vector<std::pair<int, Phrase>>& changes)
{
    const double shift = hasTrack (deck) ? sessions[(size_t) deck].track->cueShift : 0.0;
    editPhrases (deck, [=] (std::vector<Phrase>& list)
    {
        for (auto& [index, p] : changes)
        {
            if (index < 0 || index >= (int) list.size()) continue;
            auto q = p;
            q.startSec -= shift;
            if (q.endSec > 0) q.endSec -= shift;
            list[(size_t) index] = q;
        }
    });
}

void TrackEditor::deletePhrase (int deck, int index)
{
    editPhrases (deck, [=] (std::vector<Phrase>& list)
    {
        if (index >= 0 && index < (int) list.size()) list.erase (list.begin() + index);
    });
}

void TrackEditor::editVersion (int deck, const std::function<void (TrackVersion&)>& fn, bool seedPhrases)
{
    auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return;
    poll();   // take in anything changed on the deck first (skips, loop modes)
    const double shift = s.track->cueShift;
    auto apply = [&] (TrackVersion& v)
    {
        if (seedPhrases && ! v.phrases)
        {
            // Phrase repeats live on the phrases: start from rekordbox's analysis if you haven't edited them.
            std::vector<Phrase> list;
            for (auto p : s.track->phrases) { p.startSec -= shift; if (p.endSec > 0) p.endSec -= shift; list.push_back (p); }
            v.phrases = list;
        }
        fn (v);
    };
    if (s.r) apply (s.rb);
    if (s.c) apply (s.cf);
    save (s, s.r, s.c);
    swapDeck (deck, playing (s));
    syncOthers (deck);
}

int TrackEditor::deckWithTrack (juce::int64 libId) const
{
    if (libId <= 0) return -1;
    for (int d = 0; d < AudioEngine::totalDecks; ++d)
        if (sessions[(size_t) d].track != nullptr && sessions[(size_t) d].libId == libId) return d;
    return -1;
}

void TrackEditor::editLibraryTrack (juce::int64 libId, const std::function<void (TrackVersion&)>& fn)
{
    if (libId <= 0) return;
    if (const int d = deckWithTrack (libId); d >= 0)
    {
        editVersion (d, fn);   // saves, updates the deck and any other deck with the same track
        return;
    }
    auto t = library.get (libId);
    if (! t) return;
    auto cf = t->cf;
    fn (cf);
    if (Features::rekordboxCopy)
    {
        auto rb = t->rb;
        fn (rb);
        library.saveVersions (libId, &rb, &cf);
    }
    else
        library.saveVersions (libId, nullptr, &cf);
}

void TrackEditor::setPhraseRepeats (int deck, int index, int repeats)
{
    if (index < 0 || index >= (int) phrases (deck).size()) return;
    auto p = phrases (deck)[(size_t) index];
    p.repeats = juce::jlimit (1, 64, repeats);
    setPhrase (deck, index, p);
}

const std::vector<double>& TrackEditor::arrangement (int deck) const
{
    static const std::vector<double> none;
    const auto& s = sessions[(size_t) deck];
    return s.track != nullptr ? s.track->arrangement : none;
}

void TrackEditor::setArrangement (int deck, const std::vector<double>& order)
{
    if (! hasTrack (deck)) return;
    const double shift = sessions[(size_t) deck].track->cueShift;
    editVersion (deck, [&] (TrackVersion& v)
    {
        v.arrangement.clear();
        for (auto t : order) v.arrangement.push_back (t - shift);
    }, true);
}

const std::vector<StemLocator>& TrackEditor::stemLocators (int deck) const
{
    static const std::vector<StemLocator> none;
    const auto& s = sessions[(size_t) deck];
    return s.track != nullptr ? s.track->stemLocs : none;
}

void TrackEditor::addStemLocator (int deck, const StemLocator& l)
{
    if (! hasTrack (deck)) return;
    const double shift = sessions[(size_t) deck].track->cueShift;
    editVersion (deck, [&] (TrackVersion& v)
    {
        auto x = l;
        x.timeSec -= shift;
        v.stemLocs.erase (std::remove_if (v.stemLocs.begin(), v.stemLocs.end(), [&] (auto& o) { return std::abs (o.timeSec - x.timeSec) < 0.01; }), v.stemLocs.end());
        v.stemLocs.push_back (x);
        std::sort (v.stemLocs.begin(), v.stemLocs.end(), [] (auto& a, auto& b) { return a.timeSec < b.timeSec; });
    });
}

void TrackEditor::setStemLocator (int deck, int index, const StemLocator& l)
{
    if (! hasTrack (deck) || index < 0 || index >= (int) stemLocators (deck).size()) return;
    const double shift = sessions[(size_t) deck].track->cueShift;
    const double was = stemLocators (deck)[(size_t) index].timeSec - shift;
    editVersion (deck, [&] (TrackVersion& v)
    {
        for (auto& o : v.stemLocs)
            if (std::abs (o.timeSec - was) < 0.01) { o = l; o.timeSec -= shift; }
        std::sort (v.stemLocs.begin(), v.stemLocs.end(), [] (auto& a, auto& b) { return a.timeSec < b.timeSec; });
    });
}

void TrackEditor::deleteStemLocator (int deck, int index)
{
    if (! hasTrack (deck) || index < 0 || index >= (int) stemLocators (deck).size()) return;
    const double was = stemLocators (deck)[(size_t) index].timeSec - sessions[(size_t) deck].track->cueShift;
    editVersion (deck, [&] (TrackVersion& v)
    {
        v.stemLocs.erase (std::remove_if (v.stemLocs.begin(), v.stemLocs.end(), [&] (auto& o) { return std::abs (o.timeSec - was) < 0.01; }), v.stemLocs.end());
    });
}

const std::vector<NoteLocator>& TrackEditor::notes (int deck) const
{
    static const std::vector<NoteLocator> none;
    const auto& s = sessions[(size_t) deck];
    return s.track != nullptr ? s.track->notes : none;
}

void TrackEditor::addNote (int deck, const NoteLocator& n)
{
    if (! hasTrack (deck)) return;
    const double shift = sessions[(size_t) deck].track->cueShift;
    editVersion (deck, [&] (TrackVersion& v)
    {
        auto x = n;
        x.timeSec -= shift;
        v.notes.push_back (x);
        std::sort (v.notes.begin(), v.notes.end(), [] (auto& a, auto& b) { return a.timeSec < b.timeSec; });
    });
}

void TrackEditor::setNote (int deck, int index, const NoteLocator& n)
{
    if (! hasTrack (deck) || index < 0 || index >= (int) notes (deck).size()) return;
    const double shift = sessions[(size_t) deck].track->cueShift;
    const auto was = notes (deck)[(size_t) index];
    editVersion (deck, [&] (TrackVersion& v)
    {
        for (auto& o : v.notes)
            if (std::abs (o.timeSec - (was.timeSec - shift)) < 0.01 && o.text == was.text) { o = n; o.timeSec -= shift; break; }
        std::sort (v.notes.begin(), v.notes.end(), [] (auto& a, auto& b) { return a.timeSec < b.timeSec; });
    });
}

void TrackEditor::deleteNote (int deck, int index)
{
    if (! hasTrack (deck) || index < 0 || index >= (int) notes (deck).size()) return;
    const double shift = sessions[(size_t) deck].track->cueShift;
    const auto was = notes (deck)[(size_t) index];
    editVersion (deck, [&] (TrackVersion& v)
    {
        for (auto it = v.notes.begin(); it != v.notes.end(); ++it)
            if (std::abs (it->timeSec - (was.timeSec - shift)) < 0.01 && it->text == was.text) { v.notes.erase (it); break; }
    });
}

std::vector<juce::String> TrackEditor::editVersionNames (int deck) const
{
    std::vector<juce::String> names;
    const auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return names;
    for (auto& e : playing (s).editVersions) names.push_back (e.name);
    return names;
}

juce::String TrackEditor::activeEdit (int deck) const
{
    const auto& s = sessions[(size_t) deck];
    return s.track != nullptr ? playing (s).activeEdit : juce::String();
}

bool TrackEditor::activeEditModified (int deck) const
{
    const auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return false;
    const auto& v = playing (s);
    if (v.activeEdit.isEmpty()) return false;
    for (auto& e : v.editVersions)
        if (e.name == v.activeEdit) return ! v.editsMatch (e);
    return false;
}

bool TrackEditor::hasEdits (int deck) const
{
    const auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return false;
    return ! playing (s).editsMatch (EditVersion {});
}

void TrackEditor::saveEditVersion (int deck, const juce::String& name)
{
    if (name.isEmpty()) return;
    editVersion (deck, [&] (TrackVersion& v)
    {
        auto e = v.snapshotEdit (name);
        bool replaced = false;
        for (auto& o : v.editVersions) if (o.name == name) { o = e; replaced = true; }
        if (! replaced) v.editVersions.push_back (e);
        v.activeEdit = name;
    });
}

void TrackEditor::applyEditVersion (int deck, int index)
{
    editVersion (deck, [&] (TrackVersion& v)
    {
        if (index >= 0 && index < (int) v.editVersions.size()) v.applyEdit (v.editVersions[(size_t) index]);
    }, true);
}

void TrackEditor::applyNoEdits (int deck)
{
    editVersion (deck, [] (TrackVersion& v) { v.clearEdits(); });
}

void TrackEditor::renameEditVersion (int deck, int index, const juce::String& name)
{
    if (name.isEmpty()) return;
    editVersion (deck, [&] (TrackVersion& v)
    {
        if (index < 0 || index >= (int) v.editVersions.size()) return;
        if (v.activeEdit == v.editVersions[(size_t) index].name) v.activeEdit = name;
        v.editVersions[(size_t) index].name = name;
    });
}

void TrackEditor::deleteEditVersion (int deck, int index)
{
    editVersion (deck, [&] (TrackVersion& v)
    {
        if (index < 0 || index >= (int) v.editVersions.size()) return;
        if (v.activeEdit == v.editVersions[(size_t) index].name) v.activeEdit = {};
        v.editVersions.erase (v.editVersions.begin() + index);
    });
}

juce::String TrackEditor::cueName (int deck, bool hotCue, int index) const
{
    const auto& s = sessions[(size_t) deck];
    if (s.track == nullptr) return {};
    const auto& v = playing (s);
    if (hotCue)
        return index >= 0 && index < 8 && v.hot[(size_t) index] ? v.hot[(size_t) index]->comment : juce::String();
    const auto& st = engine.deck (deck).state();
    if (index < 0 || index >= Deck::maxLocators) return {};
    const int k = findAt (v.memory, st.locStart[(size_t) index].load() - s.track->cueShift);
    return k >= 0 ? v.memory[(size_t) k].comment : juce::String();
}

} // namespace orbit
