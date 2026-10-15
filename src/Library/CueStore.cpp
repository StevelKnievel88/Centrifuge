#include "CueStore.h"

namespace orbit
{

namespace
{
    juce::var cueToVar (const CuePoint& c)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("s", c.startSec);
        if (c.endSec > c.startSec) o->setProperty ("e", c.endSec);
        o->setProperty ("c", c.colour.toString());
        if (c.comment.isNotEmpty()) o->setProperty ("n", c.comment);
        return juce::var (o);
    }

    CuePoint varToCue (const juce::var& v, CuePoint::Type type, int hotIndex)
    {
        CuePoint c;
        c.type = type;
        c.hotIndex = hotIndex;
        c.startSec = (double) v.getProperty ("s", 0.0);
        c.endSec = v.hasProperty ("e") ? (double) v["e"] : -1.0;
        c.colour = juce::Colour::fromString (v.getProperty ("c", "ff28e214").toString());
        c.comment = v.getProperty ("n", {}).toString();
        return c;
    }
}

CueStore::CueStore (juce::File f) : file (std::move (f))
{
    load();
}

juce::String CueStore::keyFor (const TrackInfo& info, double lengthSec)
{
    const double len = info.durationSec > 0.0 ? info.durationSec : lengthSec;
    return info.file.getFileName().toLowerCase() + "|" + juce::String ((int) std::lround (len));
}

void CueStore::load()
{
    auto root = juce::JSON::parse (file);
    auto* obj = root.getDynamicObject();
    if (obj == nullptr) return;

    const juce::ScopedLock sl (lock);
    for (auto& p : obj->getProperties())
    {
        Entry e;
        const auto& v = p.value;
        if (auto* arr = v["hot"].getArray())
        {
            std::array<std::optional<CuePoint>, 8> hot;
            for (int i = 0; i < juce::jmin (8, arr->size()); ++i)
                if ((*arr)[i].isObject())
                    hot[(size_t) i] = varToCue ((*arr)[i], CuePoint::Type::hot, i);
            e.hotCues = hot;
        }
        if (auto* arr = v["loc"].getArray())
        {
            std::vector<CuePoint> locs;
            for (auto& c : *arr) locs.push_back (varToCue (c, CuePoint::Type::memory, -1));
            e.locators = locs;
        }
        if (auto* arr = v["skip"].getArray())
            for (auto& s : *arr)
                if (auto* pair = s.getArray(); pair != nullptr && pair->size() == 2)
                    e.skips.push_back ({ (double) (*pair)[0], (double) (*pair)[1] });
        entries[p.name.toString()] = std::move (e);
    }
}

std::optional<CueStore::Entry> CueStore::get (const juce::String& key) const
{
    const juce::ScopedLock sl (lock);
    auto it = entries.find (key);
    if (it == entries.end()) return std::nullopt;
    return it->second;
}

void CueStore::put (const juce::String& key, const Entry& e)
{
    const juce::ScopedLock sl (lock);
    entries[key] = e;
    dirty = true;
}

void CueStore::flush()
{
    juce::var root;
    {
        const juce::ScopedLock sl (lock);
        if (! dirty) return;
        dirty = false;

        auto* obj = new juce::DynamicObject();
        for (auto& [key, e] : entries)
        {
            auto* o = new juce::DynamicObject();
            if (e.hotCues)
            {
                juce::Array<juce::var> hot;
                for (auto& h : *e.hotCues) hot.add (h ? cueToVar (*h) : juce::var());
                o->setProperty ("hot", hot);
            }
            if (e.locators)
            {
                juce::Array<juce::var> locs;
                for (auto& c : *e.locators) locs.add (cueToVar (c));
                o->setProperty ("loc", locs);
            }
            if (! e.skips.empty())
            {
                juce::Array<juce::var> sk;
                for (auto& s : e.skips) sk.add (juce::Array<juce::var> { s.first, s.second });
                o->setProperty ("skip", sk);
            }
            obj->setProperty (key, juce::var (o));
        }
        root = juce::var (obj);
    }

    file.getParentDirectory().createDirectory();
    auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    if (tmp.replaceWithText (juce::JSON::toString (root)))
        tmp.moveFileTo (file);
}

} // namespace orbit
