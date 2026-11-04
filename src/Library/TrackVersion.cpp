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
        return juce::var (o);
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
    std::sort (t.memory.begin(), t.memory.end(), [] (auto& a, auto& b) { return a.startSec < b.startSec; });
    return t;
}

TrackVersion TrackVersion::limitedForRekordbox() const
{
    TrackVersion r = *this;
    r.skips.clear();
    for (auto& h : r.hot)
        if (h && ! h->colour.isTransparent())
            h->colour = rekordboxHotCueColour (nearestRekordboxHotCueCode (h->colour));
    for (auto& m : r.memory)
    {
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
