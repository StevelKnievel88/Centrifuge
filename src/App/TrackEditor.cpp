#include "TrackEditor.h"
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

std::optional<TrackVersion> TrackEditor::versionToLoad (int deck, const TrackInfo& info) const
{
    const auto key = info.libraryKey.isNotEmpty() ? info.libraryKey : CueStore::keyFor (info, info.durationSec);
    if (auto t = library.find (key))
        return sessions[(size_t) deck].c ? t->cf : t->rb;
    return std::nullopt;
}

void TrackEditor::trackLoaded (int deck, LoadedTrack::Ptr track)
{
    auto& s = sessions[(size_t) deck];
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
        else if (b >= 0 && k >= 0 && before.locs[(size_t) b].colour != c.colour) t.memory[(size_t) k].colour = c.colour;
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
    library.saveVersions (s.libId, rb ? &s.rb : nullptr, cf ? &s.cf : nullptr);
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
    for (int d = 0; d < 2; ++d)
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
        return;
    }

    std::function<std::vector<Beat> (const std::vector<Beat>&)> op;
    if (action == "shift.back10")      op = [] (auto& b) { return GridOps::shift (b, -0.010); };
    else if (action == "shift.back1")  op = [] (auto& b) { return GridOps::shift (b, -0.001); };
    else if (action == "shift.fwd1")   op = [] (auto& b) { return GridOps::shift (b, 0.001); };
    else if (action == "shift.fwd10")  op = [] (auto& b) { return GridOps::shift (b, 0.010); };
    else if (action == "anchor")       op = [pos] (auto& b) { return GridOps::anchorAt (b, pos); };
    else if (action == "downbeat")     op = [pos] (auto& b) { return GridOps::setDownbeat (b, pos); };
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
