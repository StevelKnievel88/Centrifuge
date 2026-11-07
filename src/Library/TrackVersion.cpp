#include "TrackVersion.h"
#include "RekordboxColours.h"
#include <juce_cryptography/juce_cryptography.h>

namespace orbit
{

namespace
{
    double r3 (double v) { return std::round (v * 1000.0) / 1000.0; }

    juce::var cueVar (const CuePoint& c)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("s", r3 (c.startSec));
        if (c.endSec > c.startSec) o->setProperty ("e", r3 (c.endSec));
        if (! c.colour.isTransparent()) o->setProperty ("c", c.colour.toString());
        if (c.comment.isNotEmpty()) o->setProperty ("n", c.comment);
        if (c.endSec > c.startSec && c.loopMode != 0) { o->setProperty ("m", c.loopMode); o->setProperty ("x", c.cycles); }
        return juce::var (o);
    }

    juce::var stemLocVar (const StemLocator& l)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("s", r3 (l.timeSec));
        o->setProperty ("t", juce::Array<juce::var> { l.target[0], l.target[1], l.target[2], l.target[3] });
        if (l.fadeBeats > 0.0f) o->setProperty ("f", l.fadeBeats);
        if (l.name.isNotEmpty()) o->setProperty ("n", l.name);
        return juce::var (o);
    }

    StemLocator varStemLoc (const juce::var& v)
    {
        StemLocator l;
        l.timeSec = (double) v.getProperty ("s", 0.0);
        if (auto* t = v["t"].getArray())
            for (int i = 0; i < juce::jmin (4, t->size()); ++i) l.target[(size_t) i] = juce::jlimit (-1, 1, (int) (*t)[i]);
        l.fadeBeats = (float) (double) v.getProperty ("f", 0.0);
        l.name = v.getProperty ("n", {}).toString();
        return l;
    }

    juce::var timesVar (const std::vector<double>& v)
    {
        juce::Array<juce::var> a;
        for (auto x : v) a.add (r3 (x));
        return a;
    }

    std::vector<double> varTimes (const juce::var& v)
    {
        std::vector<double> r;
        if (auto* a = v.getArray())
            for (auto& x : *a) r.push_back ((double) x);
        return r;
    }

    CuePoint varCue (const juce::var& v, CuePoint::Type type, int hot)
    {
        CuePoint c;
        c.type = type;
        c.hotIndex = hot;
        c.startSec = (double) v.getProperty ("s", 0.0);
        c.endSec = v.hasProperty ("e") ? (double) v["e"] : -1.0;
        if (v.hasProperty ("c")) c.colour = juce::Colour::fromString (v["c"].toString());
        c.comment = v.getProperty ("n", {}).toString();
        c.loopMode = juce::jlimit (0, 2, (int) v.getProperty ("m", 0));
        c.cycles = juce::jlimit (1, 64, (int) v.getProperty ("x", 4));
        return c;
    }
}

juce::var TrackVersion::toVar() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> g;
    g.ensureStorageAllocated ((int) beats.size() * 3);
    for (auto& b : beats)
    {
        g.add (r3 (b.timeSec));
        g.add (std::round (b.bpm * 100.0f) / 100.0f);
        g.add (b.beatInBar);
    }
    o->setProperty ("grid", g);

    juce::Array<juce::var> h;
    for (auto& c : hot) h.add (c ? cueVar (*c) : juce::var());
    o->setProperty ("hot", h);

    juce::Array<juce::var> m;
    for (auto& c : memory) m.add (cueVar (c));
    o->setProperty ("mem", m);

    juce::Array<juce::var> s;
    for (auto& k : skips) s.add (juce::Array<juce::var> { r3 (k.first), r3 (k.second) });
    o->setProperty ("skip", s);

    if (phrases)
    {
        juce::Array<juce::var> ph;
        for (auto& p : *phrases)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty ("s", r3 (p.startSec));
            if (p.endSec > p.startSec) po->setProperty ("e", r3 (p.endSec));
            po->setProperty ("n", p.name);
            po->setProperty ("c", p.colour.toString());
            if (p.repeats > 1) po->setProperty ("r", p.repeats);
            ph.add (juce::var (po));
        }
        o->setProperty ("phr", ph);
    }
    if (! stemLocs.empty())
    {
        juce::Array<juce::var> a;
        for (auto& l : stemLocs) a.add (stemLocVar (l));
        o->setProperty ("stl", a);
    }
    if (! notes.empty())
    {
        juce::Array<juce::var> a;
        for (auto& n : notes)
        {
            auto* no = new juce::DynamicObject();
            no->setProperty ("s", r3 (n.timeSec));
            no->setProperty ("t", n.text);
            no->setProperty ("c", n.colour.toString());
            a.add (juce::var (no));
        }
        o->setProperty ("note", a);
    }
    if (! arrangement.empty()) o->setProperty ("arr", timesVar (arrangement));
    if (! editVersions.empty())
    {
        juce::Array<juce::var> a;
        for (auto& e : editVersions)
        {
            auto* eo = new juce::DynamicObject();
            eo->setProperty ("n", e.name);
            juce::Array<juce::var> sk;
            for (auto& k : e.skips) sk.add (juce::Array<juce::var> { r3 (k.first), r3 (k.second) });
            eo->setProperty ("skip", sk);
            juce::Array<juce::var> sl;
            for (auto& l : e.stemLocs) sl.add (stemLocVar (l));
            eo->setProperty ("stl", sl);
            eo->setProperty ("arr", timesVar (e.arrangement));
            juce::Array<juce::var> rp;
            for (auto& r : e.repeats) rp.add (juce::Array<juce::var> { r3 (r.first), r.second });
            eo->setProperty ("rep", rp);
            juce::Array<juce::var> lm;
            for (auto& m : e.loopModes) lm.add (juce::Array<juce::var> { r3 (m.startSec), m.mode, m.cycles });
            eo->setProperty ("lm", lm);
            a.add (juce::var (eo));
        }
        o->setProperty ("edits", a);
    }
    if (activeEdit.isNotEmpty()) o->setProperty ("edit", activeEdit);
    return juce::var (o);
}

TrackVersion TrackVersion::fromVar (const juce::var& v)
{
    TrackVersion t;
    if (auto* g = v["grid"].getArray())
        for (int i = 0; i + 2 < g->size(); i += 3)
            t.beats.push_back ({ (double) (*g)[i], (float) (double) (*g)[i + 1], (int) (*g)[i + 2] });
    if (auto* h = v["hot"].getArray())
        for (int i = 0; i < juce::jmin (8, h->size()); ++i)
            if ((*h)[i].isObject()) t.hot[(size_t) i] = varCue ((*h)[i], CuePoint::Type::hot, i);
    if (auto* m = v["mem"].getArray())
        for (auto& c : *m) t.memory.push_back (varCue (c, CuePoint::Type::memory, -1));
    if (auto* s = v["skip"].getArray())
        for (auto& k : *s)
            if (auto* p = k.getArray(); p != nullptr && p->size() == 2)
                t.skips.push_back ({ (double) (*p)[0], (double) (*p)[1] });
    if (auto* ph = v["phr"].getArray())
    {
        std::vector<Phrase> list;
        for (auto& p : *ph)
        {
            Phrase x;
            x.startSec = (double) p.getProperty ("s", 0.0);
            x.endSec = p.hasProperty ("e") ? (double) p["e"] : -1.0;
            x.name = p.getProperty ("n", {}).toString();
            x.colour = p.hasProperty ("c") ? juce::Colour::fromString (p["c"].toString()) : defaultPhraseColour (x.name);
            x.repeats = juce::jlimit (1, 64, (int) p.getProperty ("r", 1));
            list.push_back (x);
        }
        std::sort (list.begin(), list.end(), [] (auto& a, auto& b) { return a.startSec < b.startSec; });
        t.phrases = list;
    }
    if (auto* a = v["stl"].getArray())
        for (auto& l : *a) t.stemLocs.push_back (varStemLoc (l));
    std::sort (t.stemLocs.begin(), t.stemLocs.end(), [] (auto& a, auto& b) { return a.timeSec < b.timeSec; });
    if (auto* a = v["note"].getArray())
        for (auto& n : *a)
        {
            NoteLocator x;
            x.timeSec = (double) n.getProperty ("s", 0.0);
            x.text = n.getProperty ("t", {}).toString();
            if (n.hasProperty ("c")) x.colour = juce::Colour::fromString (n["c"].toString());
            t.notes.push_back (x);
        }
    std::sort (t.notes.begin(), t.notes.end(), [] (auto& a, auto& b) { return a.timeSec < b.timeSec; });
    t.arrangement = varTimes (v["arr"]);
    if (auto* a = v["edits"].getArray())
        for (auto& ev : *a)
        {
            EditVersion e;
            e.name = ev.getProperty ("n", {}).toString();
            if (auto* sk = ev["skip"].getArray())
                for (auto& k : *sk)
                    if (auto* p = k.getArray(); p != nullptr && p->size() == 2) e.skips.push_back ({ (double) (*p)[0], (double) (*p)[1] });
            if (auto* sl = ev["stl"].getArray())
                for (auto& l : *sl) e.stemLocs.push_back (varStemLoc (l));
            e.arrangement = varTimes (ev["arr"]);
            if (auto* rp = ev["rep"].getArray())
                for (auto& r : *rp)
                    if (auto* p = r.getArray(); p != nullptr && p->size() == 2) e.repeats.push_back ({ (double) (*p)[0], (int) (*p)[1] });
            if (auto* lm = ev["lm"].getArray())
                for (auto& m : *lm)
                    if (auto* p = m.getArray(); p != nullptr && p->size() == 3) e.loopModes.push_back ({ (double) (*p)[0], (int) (*p)[1], (int) (*p)[2] });
            t.editVersions.push_back (e);
        }
    t.activeEdit = v.getProperty ("edit", {}).toString();
    std::sort (t.memory.begin(), t.memory.end(), [] (auto& a, auto& b) { return a.startSec < b.startSec; });
    return t;
}

namespace
{
    bool closeTo (double a, double b) { return std::abs (a - b) < 0.05; }
}

EditVersion TrackVersion::snapshotEdit (const juce::String& name) const
{
    EditVersion e;
    e.name = name;
    e.skips = skips;
    e.stemLocs = stemLocs;
    e.arrangement = arrangement;
    if (phrases)
        for (auto& p : *phrases)
            if (p.repeats > 1) e.repeats.push_back ({ p.startSec, p.repeats });
    for (auto& m : memory)
        if (m.isLoop() && m.loopMode != 0) e.loopModes.push_back ({ m.startSec, m.loopMode, m.cycles });
    return e;
}

void TrackVersion::applyEdit (const EditVersion& e)
{
    skips = e.skips;
    stemLocs = e.stemLocs;
    arrangement = e.arrangement;
    if (phrases)
        for (auto& p : *phrases)
        {
            p.repeats = 1;
            for (auto& r : e.repeats) if (closeTo (r.first, p.startSec)) p.repeats = r.second;
        }
    for (auto& m : memory)
    {
        if (! m.isLoop()) continue;
        m.loopMode = 0;
        for (auto& l : e.loopModes) if (closeTo (l.startSec, m.startSec)) { m.loopMode = l.mode; m.cycles = l.cycles; }
    }
    activeEdit = e.name;
}

void TrackVersion::clearEdits()
{
    EditVersion none;
    applyEdit (none);
    activeEdit = {};
}

bool TrackVersion::editsMatch (const EditVersion& e) const
{
    auto a = snapshotEdit (e.name);
    auto toJ = [] (const EditVersion& x)
    {
        TrackVersion t;
        t.editVersions.push_back (x);
        return juce::JSON::toString (t.toVar()["edits"], true);
    };
    return toJ (a) == toJ (e);
}

TrackVersion TrackVersion::limitedForRekordbox() const
{
    TrackVersion r = *this;
    r.skips.clear();
    r.stemLocs.clear();
    r.notes.clear();
    r.arrangement.clear();
    r.editVersions.clear();
    r.activeEdit = {};
    if (r.phrases) for (auto& p : *r.phrases) p.repeats = 1;
    for (auto& h : r.hot)
        if (h && ! h->colour.isTransparent())
            h->colour = rekordboxHotCueColour (nearestRekordboxHotCueCode (h->colour));
    for (auto& h : r.hot)
        if (h) h->loopMode = 0;
    for (auto& m : r.memory)
    {
        m.loopMode = 0;   // rekordbox has ordinary loops only
        const int id = nearestRekordboxMemoryColourId (m.colour);
        m.colour = id > 0 ? rekordboxMemoryColour (id) : juce::Colour();
    }
    if ((int) r.memory.size() > rekordboxMaxMemoryCues)
        r.memory.resize ((size_t) rekordboxMaxMemoryCues);
    return r;
}

bool TrackVersion::sameGrid (const TrackVersion& o) const
{
    if (beats.size() != o.beats.size()) return false;
    for (size_t i = 0; i < beats.size(); ++i)
        if (std::abs (beats[i].timeSec - o.beats[i].timeSec) > 0.0005 || beats[i].beatInBar != o.beats[i].beatInBar
            || std::abs (beats[i].bpm - o.beats[i].bpm) > 0.005f)
            return false;
    return true;
}

TrackVersion TrackVersion::fromRekordbox (const std::vector<Beat>& beats, const std::vector<CuePoint>& cues)
{
    TrackVersion t;
    t.beats = beats;
    for (auto& c : cues)
    {
        if (c.type == CuePoint::Type::hot && c.hotIndex >= 0 && c.hotIndex < 8)
            t.hot[(size_t) c.hotIndex] = c;
        else if (c.type == CuePoint::Type::memory)
            t.memory.push_back (c);
    }
    std::sort (t.memory.begin(), t.memory.end(), [] (auto& a, auto& b) { return a.startSec < b.startSec; });
    return t;
}

juce::String TrackVersion::hash() const
{
    return juce::MD5 (toJson().toUTF8()).toHexString().substring (0, 16);
}

} // namespace orbit
