#include "PhrasePresets.h"
#include "TrackInfo.h"

namespace orbit
{

namespace
{
    juce::File presetFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Centrifuge").getChildFile ("phrase-presets.json");
    }

    // The colours rekordbox-style names had before presets had colours
    juce::Colour builtInColour (const juce::String& name)
    {
        const auto n = name.toLowerCase();
        if (n.startsWith ("intro"))     return juce::Colour (0xffe0565b);
        if (n.startsWith ("up"))        return juce::Colour (0xffff9f40);
        if (n.startsWith ("down"))      return juce::Colour (0xff4f7dff);
        if (n.startsWith ("chorus"))    return juce::Colour (0xff3cd36a);
        if (n.startsWith ("verse"))     return juce::Colour (0xff36b5e6);
        if (n.startsWith ("bridge"))    return juce::Colour (0xffffd339);
        if (n.startsWith ("outro"))     return juce::Colour (0xff9a6cff);
        if (n.startsWith ("build"))     return juce::Colour (0xffff9f40);
        if (n.startsWith ("drop"))      return juce::Colour (0xffff3fa0);
        if (n.startsWith ("breakdown")) return juce::Colour (0xff4f7dff);
        return juce::Colour (0xff8f88a8);
    }

    struct Store
    {
        juce::CriticalSection lock;
        std::vector<PhrasePresets::Entry> list;
        Store()
        {
            const auto json = juce::JSON::parse (presetFile());
            if (auto* a = json.getArray())
            {
                for (auto& v : *a)
                {
                    if (v.isString()) list.push_back ({ v.toString(), builtInColour (v.toString()) });   // older files: names only
                    else if (v.isObject())
                        list.push_back ({ v["name"].toString(), juce::Colour ((juce::uint32) (juce::int64) v["argb"]) });
                }
            }
            else list = PhrasePresets::factory();
        }
        void save()
        {
            juce::Array<juce::var> a;
            for (auto& e : list)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("name", e.name);
                o->setProperty ("argb", (juce::int64) e.colour.getARGB());
                a.add (juce::var (o));
            }
            presetFile().getParentDirectory().createDirectory();
            presetFile().replaceWithText (juce::JSON::toString (juce::var (a)));
        }
    };

    Store& store() { static Store s; return s; }
}

std::vector<PhrasePresets::Entry> PhrasePresets::factory()
{
    std::vector<Entry> v;
    for (auto* n : { "Intro", "Verse", "Build", "Drop", "Breakdown", "Chorus", "Bridge", "Outro" })
        v.push_back ({ n, builtInColour (n) });
    return v;
}

std::vector<PhrasePresets::Entry> PhrasePresets::get()
{
    auto& s = store();
    const juce::ScopedLock sl (s.lock);
    return s.list;
}

void PhrasePresets::set (std::vector<Entry> l)
{
    auto& s = store();
    const juce::ScopedLock sl (s.lock);
    s.list = std::move (l);
    s.save();
}

bool PhrasePresets::colourFor (const juce::String& name, juce::Colour& out)
{
    auto& s = store();
    const juce::ScopedLock sl (s.lock);
    const auto n = name.trim().toLowerCase();
    if (n.isEmpty()) return false;
    // exact name first, then the longest preset the name starts with ("Drop 2" -> "Drop")
    for (auto& e : s.list)
        if (e.name.trim().toLowerCase() == n) { out = e.colour; return true; }
    int best = -1, bestLen = 0;
    for (int i = 0; i < (int) s.list.size(); ++i)
    {
        const auto p = s.list[(size_t) i].name.trim().toLowerCase();
        if (p.isNotEmpty() && n.startsWith (p) && p.length() > bestLen) { best = i; bestLen = p.length(); }
    }
    if (best < 0) return false;
    out = s.list[(size_t) best].colour;
    return true;
}

juce::Colour defaultPhraseColour (const juce::String& name)
{
    juce::Colour c;
    if (PhrasePresets::colourFor (name, c)) return c;
    return builtInColour (name);
}

} // namespace orbit
