#pragma once
#include <juce_core/juce_core.h>
#include <map>

namespace orbit
{

/** Tracks where the two key detectors disagree: Centrifuge's key and the second opinion, by library id, until you
    pick one (kept in key-opinions.json next to the settings). */
namespace KeyOpinions
{
    struct Entry { juce::String ours, alt; };

    inline juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Centrifuge").getChildFile ("key-opinions.json");
    }

    inline std::map<juce::int64, Entry>& all()
    {
        static std::map<juce::int64, Entry> m = []
        {
            std::map<juce::int64, Entry> r;
            auto v = juce::JSON::parse (file());
            if (auto* o = v.getDynamicObject())
                for (auto& p : o->getProperties())
                    r[p.name.toString().getLargeIntValue()] = { p.value["ours"].toString(), p.value["alt"].toString() };
            return r;
        }();
        return m;
    }

    inline void save()
    {
        auto* o = new juce::DynamicObject();
        for (auto& [id, e] : all())
        {
            auto* x = new juce::DynamicObject();
            x->setProperty ("ours", e.ours);
            x->setProperty ("alt", e.alt);
            o->setProperty (juce::String (id), juce::var (x));
        }
        file().getParentDirectory().createDirectory();
        file().replaceWithText (juce::JSON::toString (juce::var (o)));
    }

    inline const Entry* get (juce::int64 id) { auto it = all().find (id); return it != all().end() ? &it->second : nullptr; }
    inline void set (juce::int64 id, const juce::String& ours, const juce::String& alt) { all()[id] = { ours, alt }; save(); }
    inline void clear (juce::int64 id) { if (all().erase (id) > 0) save(); }
}

} // namespace orbit
