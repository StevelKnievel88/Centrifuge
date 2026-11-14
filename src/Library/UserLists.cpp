#include "UserLists.h"
#include "../Audio/MusicalKey.h"

namespace orbit
{

namespace
{
    juce::var rulesVar (const UserLists::Rules& r)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("bpmMin", r.bpmMin);
        o->setProperty ("bpmMax", r.bpmMax);
        o->setProperty ("keys", r.keys);
        o->setProperty ("ratingMin", r.ratingMin);
        o->setProperty ("genre", r.genre);
        o->setProperty ("colour", r.colour);
        o->setProperty ("stems", r.stems);
        o->setProperty ("tag", r.tag);
        o->setProperty ("text", r.text);
        return juce::var (o);
    }

    UserLists::Rules varRules (const juce::var& v)
    {
        UserLists::Rules r;
        r.bpmMin = (double) v.getProperty ("bpmMin", 0.0);
        r.bpmMax = (double) v.getProperty ("bpmMax", 0.0);
        r.keys = v.getProperty ("keys", {}).toString();
        r.ratingMin = (int) v.getProperty ("ratingMin", 0);
        r.genre = v.getProperty ("genre", {}).toString();
        r.colour = (int) v.getProperty ("colour", 0);
        r.stems = (int) v.getProperty ("stems", 0);
        r.tag = v.getProperty ("tag", {}).toString();
        r.text = v.getProperty ("text", {}).toString();
        return r;
    }

    juce::var idsVar (const std::vector<juce::int64>& ids)
    {
        juce::Array<juce::var> a;
        for (auto i : ids) a.add (i);
        return a;
    }

    std::vector<juce::int64> varIds (const juce::var& v)
    {
        std::vector<juce::int64> r;
        if (auto* a = v.getArray()) for (auto& x : *a) r.push_back ((juce::int64) x);
        return r;
    }

    juce::var historyVar (const UserLists::History& h)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", h.name);
        juce::Array<juce::var> a;
        for (auto& p : h.tracks)
        {
            auto* x = new juce::DynamicObject();
            x->setProperty ("t", p.track);
            x->setProperty ("at", p.time);
            a.add (juce::var (x));
        }
        o->setProperty ("tracks", a);
        return juce::var (o);
    }

    UserLists::History varHistory (const juce::var& v)
    {
        UserLists::History h;
        h.name = v.getProperty ("name", {}).toString();
        if (auto* a = v["tracks"].getArray())
            for (auto& x : *a) h.tracks.push_back ({ (juce::int64) x["t"], x["at"].toString() });
        return h;
    }
}

juce::File UserLists::file()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Centrifuge").getChildFile ("user-lists.json");
}

UserLists::UserLists()
{
    load();
    current.name = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M");
}

void UserLists::load()
{
    auto v = juce::JSON::parse (file());
    if (! v.isObject()) return;
    if (auto* a = v["lists"].getArray())
        for (auto& x : *a)
        {
            List l;
            l.id = (juce::int64) x["id"];
            l.parent = (juce::int64) x["parent"];
            l.name = x["name"].toString();
            l.kind = juce::jlimit (0, 2, (int) x["kind"]);
            l.seq = (int) x["seq"];
            l.tracks = varIds (x["tracks"]);
            l.rules = varRules (x["rules"]);
            userLists.push_back (l);
        }
    toPlayList = varIds (v["toPlay"]);
    if (auto* a = v["sessions"].getArray()) for (auto& x : *a) pastSessions.push_back (varHistory (x));
    if (auto* a = v["setLists"].getArray()) for (auto& x : *a) pastSetLists.push_back (varHistory (x));
    if (auto* a = v["tagCategories"].getArray())
        for (auto& x : *a)
        {
            TagCategory c;
            c.name = x["name"].toString();
            if (auto* t = x["tags"].getArray()) for (auto& s : *t) c.tags.add (s.toString());
            categories.push_back (c);
        }
    if (auto* o = v["trackLocks"].getDynamicObject())
        for (auto& p : o->getProperties())
            if ((int) p.value != 0) trackLocks[p.name.toString().getLargeIntValue()] = (int) p.value;
    if (auto* o = v["trackTags"].getDynamicObject())
        for (auto& p : o->getProperties())
        {
            juce::StringArray tags;
            if (auto* t = p.value.getArray()) for (auto& s : *t) tags.add (s.toString());
            trackTags[p.name.toString().getLargeIntValue()] = tags;
        }
}

void UserLists::save()
{
    ++changes;
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> lists;
    for (auto& l : userLists)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty ("id", l.id);
        x->setProperty ("parent", l.parent);
        x->setProperty ("name", l.name);
        x->setProperty ("kind", l.kind);
        x->setProperty ("seq", l.seq);
        x->setProperty ("tracks", idsVar (l.tracks));
        if (l.kind == dynamic) x->setProperty ("rules", rulesVar (l.rules));
        lists.add (juce::var (x));
    }
    o->setProperty ("lists", lists);
    o->setProperty ("toPlay", idsVar (toPlayList));
    juce::Array<juce::var> sess, sets;
    for (auto& h : pastSessions) sess.add (historyVar (h));
    if (! current.tracks.empty()) sess.add (historyVar (current));
    for (auto& h : pastSetLists) sets.add (historyVar (h));
    if (setListOn && ! setList.tracks.empty()) sets.add (historyVar (setList));
    o->setProperty ("sessions", sess);
    o->setProperty ("setLists", sets);
    juce::Array<juce::var> cats;
    for (auto& c : categories)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty ("name", c.name);
        juce::Array<juce::var> t;
        for (auto& s : c.tags) t.add (s);
        x->setProperty ("tags", t);
        cats.add (juce::var (x));
    }
    o->setProperty ("tagCategories", cats);
    auto* tt = new juce::DynamicObject();
    for (auto& [id, tags] : trackTags)
    {
        if (tags.isEmpty()) continue;
        juce::Array<juce::var> t;
        for (auto& s : tags) t.add (s);
        tt->setProperty (juce::String (id), t);
    }
    o->setProperty ("trackTags", juce::var (tt));
    auto* tl = new juce::DynamicObject();
    for (auto& [id, m] : trackLocks) tl->setProperty (juce::String (id), m);
    o->setProperty ("trackLocks", juce::var (tl));
    file().getParentDirectory().createDirectory();
    file().replaceWithText (juce::JSON::toString (juce::var (o)));
}

//==============================================================================
const UserLists::List* UserLists::find (juce::int64 id) const
{
    for (auto& l : userLists) if (l.id == id) return &l;
    return nullptr;
}

juce::int64 UserLists::add (juce::int64 parent, const juce::String& name, int kind)
{
    juce::int64 id = 1;
    int seq = 0;
    for (auto& l : userLists) { id = juce::jmax (id, l.id + 1); if (l.parent == parent) seq = juce::jmax (seq, l.seq + 1); }
    List l;
    l.id = id; l.parent = parent; l.name = name; l.kind = kind; l.seq = seq;
    userLists.push_back (l);
    save();
    return id;
}

void UserLists::rename (juce::int64 id, const juce::String& name)
{
    for (auto& l : userLists) if (l.id == id) l.name = name;
    save();
}

void UserLists::remove (juce::int64 id)
{
    std::set<juce::int64> gone { id };
    for (bool more = true; more;)
    {
        more = false;
        for (auto& l : userLists) if (gone.count (l.parent) && ! gone.count (l.id)) { gone.insert (l.id); more = true; }
    }
    userLists.erase (std::remove_if (userLists.begin(), userLists.end(), [&] (auto& l) { return gone.count (l.id) > 0; }), userLists.end());
    save();
}

juce::int64 UserLists::duplicate (juce::int64 id)
{
    const auto* src = find (id);
    if (src == nullptr || src->kind == folder) return 0;
    auto copy = *src;
    const auto nid = add (copy.parent, copy.name + " copy", copy.kind);
    for (auto& l : userLists) if (l.id == nid) { l.tracks = copy.tracks; l.rules = copy.rules; }
    save();
    return nid;
}

void UserLists::addTracks (juce::int64 id, const std::vector<juce::int64>& tracks)
{
    for (auto& l : userLists)
        if (l.id == id && l.kind == playlist)
            for (auto t : tracks)
                if (std::find (l.tracks.begin(), l.tracks.end(), t) == l.tracks.end()) l.tracks.push_back (t);
    save();
}

void UserLists::removeTracks (juce::int64 id, const std::vector<juce::int64>& tracks)
{
    for (auto& l : userLists)
        if (l.id == id)
            l.tracks.erase (std::remove_if (l.tracks.begin(), l.tracks.end(), [&] (auto t) { return std::find (tracks.begin(), tracks.end(), t) != tracks.end(); }), l.tracks.end());
    save();
}

void UserLists::setRules (juce::int64 id, const Rules& r)
{
    for (auto& l : userLists) if (l.id == id) l.rules = r;
    save();
}

bool UserLists::toggleToPlay (juce::int64 track)
{
    auto it = std::find (toPlayList.begin(), toPlayList.end(), track);
    const bool add = it == toPlayList.end();
    if (add) toPlayList.push_back (track); else toPlayList.erase (it);
    save();
    return add;
}

void UserLists::removeFromToPlay (juce::int64 track)
{
    toPlayList.erase (std::remove (toPlayList.begin(), toPlayList.end(), track), toPlayList.end());
    save();
}

void UserLists::startSetList()
{
    if (setListOn) return;
    setListOn = true;
    setList = {};
    setList.name = "Set list " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M");
    save();
}

void UserLists::stopSetList()
{
    if (! setListOn) return;
    setListOn = false;
    if (! setList.tracks.empty()) pastSetLists.push_back (setList);
    setList = {};
    save();
}

void UserLists::notePlayed (juce::int64 track)
{
    const auto at = juce::Time::getCurrentTime().formatted ("%H:%M:%S");
    current.tracks.push_back ({ track, at });
    if (setListOn) setList.tracks.push_back ({ track, at });
    save();
}

void UserLists::deleteHistory (bool sets, int index)
{
    auto& v = sets ? pastSetLists : pastSessions;
    if (index >= 0 && index < (int) v.size()) v.erase (v.begin() + index);
    save();
}

void UserLists::setTagCategories (std::vector<TagCategory> c)
{
    categories = std::move (c);
    save();
}

const char* UserLists::lockName (int bit)
{
    switch (bit)
    {
        case lockGrid: return "Beat grid";
        case lockPhrases: return "Phrases";
        case lockHotCues: return "Hot cues";
        case lockMarkers: return "Markers";
        case lockLoops: return "Loops";
        case lockSkips: return "Skips";
        case lockStemMarkers: return "Stem markers";
        case lockNotes: return "Notes";
        case lockRating: return "Rating";
        case lockColour: return "Colour";
        case lockTags: return "Tags";
        case lockInfo: return "Title, artist, album, genre, label, comment";
        case lockKey: return "Key";
        default: return "";
    }
}

juce::StringArray UserLists::tagsOf (juce::int64 track) const
{
    auto it = trackTags.find (track);
    return it != trackTags.end() ? it->second : juce::StringArray();
}

void UserLists::setTag (juce::int64 track, const juce::String& tag, bool on)
{
    auto& t = trackTags[track];
    if (on) t.addIfNotAlreadyThere (tag); else t.removeString (tag);
    save();
}

bool UserLists::matches (const Rules& r, const TrackInfo& t, int stemQuality) const
{
    if (r.bpmMin > 0 && t.bpm < r.bpmMin - 0.005) return false;
    if (r.bpmMax > 0 && t.bpm > r.bpmMax + 0.005) return false;
    if (r.ratingMin > 0 && t.rating < r.ratingMin) return false;
    if (r.genre.isNotEmpty() && ! t.genre.containsIgnoreCase (r.genre)) return false;
    if (r.colour > 0 && t.colour != rekordboxTrackColour (r.colour)) return false;
    if (r.stems == 1 && stemQuality < 0) return false;
    if (r.stems == 2 && stemQuality >= 0) return false;
    if (r.text.isNotEmpty() && ! (t.title.containsIgnoreCase (r.text) || t.artist.containsIgnoreCase (r.text))) return false;
    if (r.tag.isNotEmpty() && ! tagsOf (t.libraryId).contains (r.tag)) return false;
    if (r.keys.isNotEmpty())
    {
        const auto k = MusicalKey::parse (t.key);
        bool any = false;
        for (auto& s : juce::StringArray::fromTokens (r.keys, ",;", ""))
        {
            const auto w = MusicalKey::parse (s.trim());
            any = any || (k.isValid() && w.isValid() && k.pitch == w.pitch && k.minor == w.minor);
        }
        if (! any) return false;
    }
    return true;
}

} // namespace orbit
