#include "CentrifugeLibrary.h"
#include "Anlz.h"
#include "sqlite3mc.h"

namespace orbit
{

namespace
{
    struct Stmt
    {
        sqlite3_stmt* st = nullptr;
        Stmt (sqlite3* db, const juce::String& sql) { sqlite3_prepare_v2 (db, sql.toRawUTF8(), -1, &st, nullptr); }
        ~Stmt() { sqlite3_finalize (st); }
        Stmt& bind (int i, const juce::String& s) { sqlite3_bind_text (st, i, s.toRawUTF8(), -1, SQLITE_TRANSIENT); return *this; }
        Stmt& bind (int i, juce::int64 v) { sqlite3_bind_int64 (st, i, v); return *this; }
        bool step() { return st != nullptr && sqlite3_step (st) == SQLITE_ROW; }
        bool run() { if (st == nullptr) return false; const int rc = sqlite3_step (st); return rc == SQLITE_DONE || rc == SQLITE_ROW; }
        juce::String text (int c) const { auto* t = sqlite3_column_text (st, c); return t != nullptr ? juce::String::fromUTF8 ((const char*) t) : juce::String(); }
        juce::int64 i64 (int c) const { return sqlite3_column_int64 (st, c); }
    };

    juce::var beatsToVar (const std::vector<Beat>& beats)
    {
        TrackVersion v;
        v.beats = beats;
        return v.toVar()["grid"];
    }

    std::vector<Beat> beatsFromVar (const juce::var& g)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("grid", g);
        return TrackVersion::fromVar (juce::var (o)).beats;
    }

    /** Takes colours (and names) from the ANLZ cue list for cues that master.db holds without them. */
    void fillCueDetails (std::vector<CuePoint>& dbCues, const std::vector<CuePoint>& anlzCues)
    {
        for (auto& c : dbCues)
            for (auto& a : anlzCues)
                if (a.type == c.type && a.hotIndex == c.hotIndex && std::abs (a.startSec - c.startSec) < 0.005)
                {
                    const bool defaultColour = c.colour == juce::Colour (0xff28e214) || c.colour == juce::Colour (0xffe8102a);
                    if (defaultColour && ! a.colour.isTransparent()) c.colour = a.colour;
                    if (c.comment.isEmpty()) c.comment = a.comment;
                    break;
                }
    }
}

CentrifugeLibrary::CentrifugeLibrary (juce::File file)
{
    file.getParentDirectory().createDirectory();
    if (sqlite3_open_v2 (file.getFullPathName().toRawUTF8(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        openError = "Couldn't open " + file.getFullPathName();
        if (db != nullptr) { sqlite3_close (db); db = nullptr; }
        return;
    }
    exec ("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
    exec ("CREATE TABLE IF NOT EXISTS tracks (id INTEGER PRIMARY KEY, key TEXT UNIQUE, info TEXT, source TEXT, rb TEXT, cf TEXT, "
          "rb_edited INTEGER DEFAULT 0, cf_edited INTEGER DEFAULT 0, import_hash TEXT, imported_grid TEXT)");
    exec ("ALTER TABLE tracks ADD COLUMN hidden INTEGER DEFAULT 0");   // fails harmlessly when it already exists
    exec ("ALTER TABLE tracks ADD COLUMN plays INTEGER DEFAULT 0");
    exec ("CREATE TABLE IF NOT EXISTS playlists (id INTEGER PRIMARY KEY, parent INTEGER, name TEXT, folder INTEGER, seq INTEGER, source TEXT)");
    exec ("CREATE TABLE IF NOT EXISTS playlist_tracks (playlist INTEGER, pos INTEGER, track INTEGER)");
    loadAll();
}

CentrifugeLibrary::~CentrifugeLibrary()
{
    if (db != nullptr) sqlite3_close (db);
}

bool CentrifugeLibrary::exec (const juce::String& sql)
{
    char* err = nullptr;
    const bool ok = sqlite3_exec (db, sql.toRawUTF8(), nullptr, nullptr, &err) == SQLITE_OK;
    if (err != nullptr) { DBG (err); sqlite3_free (err); }
    return ok;
}

int CentrifugeLibrary::size() const
{
    const juce::ScopedLock sl (lock);
    return (int) tracks.size();
}

juce::var CentrifugeLibrary::infoToVar (const TrackInfo& t)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", t.id);
    o->setProperty ("title", t.title);
    o->setProperty ("artist", t.artist);
    o->setProperty ("album", t.album);
    o->setProperty ("genre", t.genre);
    o->setProperty ("key", t.key);
    o->setProperty ("label", t.label);
    o->setProperty ("comment", t.comment);
    o->setProperty ("dateAdded", t.dateAdded);
    o->setProperty ("bpm", t.bpm);
    o->setProperty ("duration", t.durationSec);
    o->setProperty ("rating", t.rating);
    o->setProperty ("sampleRate", t.sampleRate);
    o->setProperty ("bitrate", t.bitrate);
    o->setProperty ("colour", t.colour.isTransparent() ? juce::String() : t.colour.toString());
    o->setProperty ("file", t.file.getFullPathName());
    o->setProperty ("anlz", t.anlzBase.getFullPathName());
    return juce::var (o);
}

TrackInfo CentrifugeLibrary::infoFromVar (const juce::var& v)
{
    TrackInfo t;
    t.id = v["id"].toString();
    t.title = v["title"].toString();
    t.artist = v["artist"].toString();
    t.album = v["album"].toString();
    t.genre = v["genre"].toString();
    t.key = v["key"].toString();
    t.label = v["label"].toString();
    t.comment = v["comment"].toString();
    t.dateAdded = v["dateAdded"].toString();
    t.bpm = (double) v["bpm"];
    t.durationSec = (double) v["duration"];
    t.rating = (int) v["rating"];
    t.sampleRate = (int) v["sampleRate"];
    t.bitrate = (int) v["bitrate"];
    if (v["colour"].toString().isNotEmpty()) t.colour = juce::Colour::fromString (v["colour"].toString());
    if (v["file"].toString().isNotEmpty()) t.file = juce::File (v["file"].toString());
    if (v["anlz"].toString().isNotEmpty()) t.anlzBase = juce::File (v["anlz"].toString());
    return t;
}

void CentrifugeLibrary::loadAll()
{
    const juce::ScopedLock sl (lock);
    tracks.clear();
    byKey.clear();
    playlists.clear();

    Stmt q (db, "SELECT id, key, info, source, rb, cf, rb_edited, cf_edited, import_hash, imported_grid, hidden, plays FROM tracks");
    while (q.step())
    {
        Track t;
        t.id = q.i64 (0);
        t.key = q.text (1);
        t.info = infoFromVar (juce::JSON::parse (q.text (2)));
        t.source = q.text (3);
        t.rb = TrackVersion::fromJson (q.text (4));
        t.cf = TrackVersion::fromJson (q.text (5));
        t.rbEdited = q.i64 (6) != 0;
        t.cfEdited = q.i64 (7) != 0;
        t.importHash = q.text (8);
        t.importedGrid = beatsFromVar (juce::JSON::parse (q.text (9)));
        t.hidden = (int) q.i64 (10);
        t.plays = (int) q.i64 (11);
        t.differ = ! t.rb.sameAs (t.cf);
        byKey[t.key] = t.id;
        tracks[t.id] = std::move (t);
    }

    Stmt p (db, "SELECT id, parent, name, folder, seq, source FROM playlists ORDER BY parent, seq");
    while (p.step())
        playlists.push_back ({ p.i64 (0), p.i64 (1), p.text (2), p.text (5), p.i64 (3) != 0, (int) p.i64 (4), {} });

    Stmt e (db, "SELECT playlist, track FROM playlist_tracks ORDER BY playlist, pos");
    while (e.step())
        for (auto& pl : playlists)
            if (pl.id == e.i64 (0)) { pl.tracks.push_back (e.i64 (1)); break; }
}

void CentrifugeLibrary::writeTrack (const Track& tc)
{
    auto& t = const_cast<Track&> (tc);
    t.differ = ! t.rb.sameAs (t.cf);
    Stmt s (db, "INSERT OR REPLACE INTO tracks (id, key, info, source, rb, cf, rb_edited, cf_edited, import_hash, imported_grid, hidden, plays) "
                "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    s.bind (1, t.id).bind (2, t.key).bind (3, juce::JSON::toString (infoToVar (t.info), true)).bind (4, t.source)
     .bind (5, t.rb.toJson()).bind (6, t.cf.toJson()).bind (7, (juce::int64) t.rbEdited).bind (8, (juce::int64) t.cfEdited)
     .bind (9, t.importHash).bind (10, juce::JSON::toString (beatsToVar (t.importedGrid), true))
     .bind (11, (juce::int64) t.hidden).bind (12, (juce::int64) t.plays);
    s.run();
}

TrackVersion CentrifugeLibrary::readRekordboxVersion (const TrackInfo& info)
{
    const auto anlz = loadAnlz (info.anlzBase);
    auto cues = info.dbCues;
    if (cues.empty()) cues = anlz.cues;
    else fillCueDetails (cues, anlz.cues);
    return TrackVersion::fromRekordbox (anlz.beats, cues);
}

CentrifugeLibrary::ImportResult CentrifugeLibrary::import (const LibrarySource& src, const std::vector<int>* onlyTracks,
                                                           CueStore* legacy, std::function<bool (float)> progress)
{
    ImportResult res;
    if (db == nullptr) { res.error = openError; return res; }

    const juce::String sourceTag = src.kind == LibrarySource::Kind::pcCollection ? juce::String ("rekordbox")
                                                                                 : "usb:" + src.name;

    // Centrifuge's extras stored on a USB stick by a Centrifuge export
    std::map<juce::String, TrackVersion> stickExtras;
    if (src.kind == LibrarySource::Kind::usbExport)
    {
        auto f = src.root.getChildFile ("PIONEER").getChildFile ("Centrifuge").getChildFile ("library.json");
        if (auto* obj = juce::JSON::parse (f).getDynamicObject())
            for (auto& p : obj->getProperties())
                stickExtras[p.name.toString()] = TrackVersion::fromVar (p.value);
    }

    std::vector<int> indices;
    if (onlyTracks != nullptr) indices = *onlyTracks;
    else for (int i = 0; i < (int) src.tracks.size(); ++i) indices.push_back (i);

    std::vector<juce::int64> idsBySourceIndex (src.tracks.size(), 0);
    exec ("BEGIN");
    for (size_t n = 0; n < indices.size(); ++n)
    {
        const int i = indices[n];
        if (i < 0 || i >= (int) src.tracks.size()) continue;
        if (progress && ! progress ((float) n / (float) juce::jmax<size_t> (1, indices.size()))) break;

        const auto& info = src.tracks[(size_t) i];
        const auto key = CueStore::keyFor (info, info.durationSec);
        const auto rbNow = readRekordboxVersion (info);
        const auto hash = rbNow.hash();

        const juce::ScopedLock sl (lock);
        auto it = byKey.find (key);
        if (it == byKey.end())
        {
            Track t;
            t.id = tracks.empty() ? 1 : tracks.rbegin()->first + 1;
            t.key = key;
            t.info = info;
            t.info.dbCues.clear();
            t.source = sourceTag;
            t.rb = rbNow;
            t.cf = rbNow;
            t.importHash = hash;
            t.importedGrid = rbNow.beats;

            if (auto e = stickExtras.find (key); e != stickExtras.end())
            {
                t.cf = e->second;
                t.cfEdited = true;
            }
            else if (legacy != nullptr)
            {
                // Edits made with Centrifuge 0.3 (cues.json) become the Centrifuge version.
                if (auto old = legacy->get (key))
                {
                    if (old->hotCues)
                        for (size_t h = 0; h < 8; ++h) t.cf.hot[h] = (*old->hotCues)[h];
                    if (old->locators) t.cf.memory = *old->locators;
                    t.cf.skips = old->skips;
                    t.cfEdited = true;
                }
            }

            writeTrack (t);
            byKey[key] = t.id;
            idsBySourceIndex[(size_t) i] = t.id;
            tracks[t.id] = std::move (t);
            ++res.added;
            continue;
        }

        auto& t = tracks[it->second];
        idsBySourceIndex[(size_t) i] = t.id;
        t.hidden &= ~1;   // imported again: back in the rekordbox copy
        // Track details always follow rekordbox (title, file location, ...)
        auto newInfo = info;
        newInfo.dbCues.clear();
        t.info = newInfo;

        if (t.importHash == hash)
        {
            writeTrack (t);
            ++res.unchanged;
        }
        else if (! t.rbEdited && ! t.cfEdited)
        {
            t.rb = rbNow;
            t.cf = rbNow;
            t.importHash = hash;
            t.importedGrid = rbNow.beats;
            writeTrack (t);
            ++res.updated;
        }
        else
        {
            writeTrack (t);
            res.conflicts.push_back ({ t.id, t.info.displayArtistTitle(), rbNow, hash });
        }
    }

    if (onlyTracks == nullptr)
        replacePlaylists (sourceTag, src.playlists, idsBySourceIndex,
                          src.kind == LibrarySource::Kind::pcCollection ? juce::String() : src.name);
    exec ("COMMIT");
    ++changeCount;
    return res;
}

void CentrifugeLibrary::replacePlaylists (const juce::String& source, const PlaylistNode& root,
                                          const std::vector<juce::int64>& idsBySourceIndex, const juce::String& folderName)
{
    const juce::ScopedLock sl (lock);
    // Remove this source's playlists, then add them again as they are now.
    std::vector<juce::int64> gone;
    for (auto& p : playlists) if (p.source == source) gone.push_back (p.id);
    for (auto id : gone)
    {
        Stmt (db, "DELETE FROM playlist_tracks WHERE playlist=?").bind (1, id).run();
        Stmt (db, "DELETE FROM playlists WHERE id=?").bind (1, id).run();
    }
    playlists.erase (std::remove_if (playlists.begin(), playlists.end(), [&] (auto& p) { return p.source == source; }), playlists.end());

    juce::int64 nextId = 1;
    for (auto& p : playlists) nextId = juce::jmax (nextId, p.id + 1);

    auto addNode = [&] (juce::int64 parent, const juce::String& name, bool folder, int seq, const std::vector<int>* trackIdx)
    {
        Playlist p { nextId++, parent, name, source, folder, seq, {} };
        if (trackIdx != nullptr)
            for (int i : *trackIdx)
                if (i >= 0 && i < (int) idsBySourceIndex.size() && idsBySourceIndex[(size_t) i] != 0)
                    p.tracks.push_back (idsBySourceIndex[(size_t) i]);
        Stmt (db, "INSERT INTO playlists (id, parent, name, folder, seq, source) VALUES (?,?,?,?,?,?)")
            .bind (1, p.id).bind (2, p.parent).bind (3, p.name).bind (4, (juce::int64) p.folder).bind (5, (juce::int64) p.seq).bind (6, source).run();
        for (size_t k = 0; k < p.tracks.size(); ++k)
            Stmt (db, "INSERT INTO playlist_tracks (playlist, pos, track) VALUES (?,?,?)").bind (1, p.id).bind (2, (juce::int64) k).bind (3, p.tracks[k]).run();
        playlists.push_back (p);
        return p.id;
    };

    std::function<void (const PlaylistNode&, juce::int64)> walk = [&] (const PlaylistNode& node, juce::int64 parent)
    {
        int seq = 0;
        for (auto& c : node.children)
        {
            if (c.isSmart) continue;   // smart playlists depend on rekordbox's rules
            const auto id = addNode (parent, c.name, c.isFolder, seq++, c.isFolder ? nullptr : &c.trackIndices);
            if (c.isFolder) walk (c, id);
        }
    };

    juce::int64 base = 0;
    if (folderName.isNotEmpty())
    {
        int seq = 0;
        for (auto& p : playlists) if (p.parent == 0) seq = juce::jmax (seq, p.seq + 1);
        base = addNode (0, folderName, true, seq, nullptr);
    }
    walk (root, base);
}

void CentrifugeLibrary::resolve (const Conflict& c, bool takeRekordbox)
{
    const juce::ScopedLock sl (lock);
    auto it = tracks.find (c.id);
    if (it == tracks.end()) return;
    auto& t = it->second;
    if (takeRekordbox)
    {
        const auto skips = t.cf.skips;
        t.rb = c.rekordboxNow;
        t.cf = c.rekordboxNow;
        t.cf.skips = skips;
        t.rbEdited = false;
        t.cfEdited = ! skips.empty();
        t.importedGrid = c.rekordboxNow.beats;
    }
    t.importHash = c.newHash;
    writeTrack (t);
    ++changeCount;
}

std::optional<CentrifugeLibrary::Track> CentrifugeLibrary::find (const juce::String& key) const
{
    const juce::ScopedLock sl (lock);
    auto it = byKey.find (key);
    if (it == byKey.end()) return std::nullopt;
    return tracks.at (it->second);
}

std::optional<CentrifugeLibrary::Track> CentrifugeLibrary::get (juce::int64 id) const
{
    const juce::ScopedLock sl (lock);
    auto it = tracks.find (id);
    if (it == tracks.end()) return std::nullopt;
    return it->second;
}

std::vector<CentrifugeLibrary::Track> CentrifugeLibrary::allTracks() const
{
    const juce::ScopedLock sl (lock);
    std::vector<Track> v;
    v.reserve (tracks.size());
    for (auto& [id, t] : tracks) v.push_back (t);
    return v;
}

std::vector<CentrifugeLibrary::Playlist> CentrifugeLibrary::allPlaylists() const
{
    const juce::ScopedLock sl (lock);
    return playlists;
}

juce::int64 CentrifugeLibrary::addTrack (const TrackInfo& info, const TrackVersion& rbVersion)
{
    const auto key = CueStore::keyFor (info, info.durationSec);
    const juce::ScopedLock sl (lock);
    if (auto it = byKey.find (key); it != byKey.end()) return it->second;
    Track t;
    t.id = tracks.empty() ? 1 : tracks.rbegin()->first + 1;
    t.key = key;
    t.info = info;
    t.info.dbCues.clear();
    t.source = info.libraryKey.isNotEmpty() ? juce::String ("centrifuge") : juce::String ("loaded");
    t.rb = t.cf = rbVersion;
    t.importHash = rbVersion.hash();
    t.importedGrid = rbVersion.beats;
    writeTrack (t);
    byKey[key] = t.id;
    const auto id = t.id;
    tracks[id] = std::move (t);
    ++changeCount;
    return id;
}

std::pair<juce::int64, bool> CentrifugeLibrary::addAnalysedTrack (const TrackInfo& info, const TrackVersion& version, const juce::String& source)
{
    const auto key = CueStore::keyFor (info, info.durationSec);
    const juce::ScopedLock sl (lock);
    if (auto it = byKey.find (key); it != byKey.end()) return { it->second, false };
    Track t;
    t.id = tracks.empty() ? 1 : tracks.rbegin()->first + 1;
    t.key = key;
    t.info = info;
    t.info.dbCues.clear();
    t.source = source;
    t.rb = t.cf = version;
    t.importHash = "centrifuge-analysis";   // never matches a rekordbox hash: a later rekordbox import replaces it unless edited
    t.importedGrid = version.beats;
    exec ("BEGIN");
    writeTrack (t);
    exec ("COMMIT");
    byKey[key] = t.id;
    const auto id = t.id;
    tracks[id] = std::move (t);
    ++changeCount;
    return { id, true };
}

void CentrifugeLibrary::setPlaylists (const juce::String& source, const PlaylistNode& root, const std::vector<juce::int64>& ids)
{
    if (db == nullptr) return;
    exec ("BEGIN");
    replacePlaylists (source, root, ids, {});
    {
        // After everything else at the top level
        const juce::ScopedLock sl (lock);
        int offset = 0;
        for (auto& p : playlists) if (p.parent == 0 && p.source != source) offset = juce::jmax (offset, p.seq + 1);
        for (auto& p : playlists)
            if (p.parent == 0 && p.source == source)
            {
                p.seq += offset;
                Stmt (db, "UPDATE playlists SET seq=? WHERE id=?").bind (1, (juce::int64) p.seq).bind (2, p.id).run();
            }
    }
    exec ("COMMIT");
    ++changeCount;
}

juce::String CentrifugeLibrary::keyForFile (const juce::File& f, double lengthSec)
{
    TrackInfo t;
    t.file = f;
    return CueStore::keyFor (t, lengthSec);
}

void CentrifugeLibrary::removeFromView (juce::int64 id, int view)
{
    bool remove = false;
    {
        const juce::ScopedLock sl (lock);
        auto it = tracks.find (id);
        if (it == tracks.end()) return;
        it->second.hidden |= view == 0 ? 1 : 2;
        remove = it->second.hidden == 3;
        if (! remove) Stmt (db, "UPDATE tracks SET hidden=? WHERE id=?").bind (1, (juce::int64) it->second.hidden).bind (2, id).run();
    }
    if (remove) deleteTrack (id);
    else ++changeCount;
}

void CentrifugeLibrary::deleteTrack (juce::int64 id)
{
    const juce::ScopedLock sl (lock);
    auto it = tracks.find (id);
    if (it == tracks.end()) return;
    byKey.erase (it->second.key);
    tracks.erase (it);
    for (auto& p : playlists)
        p.tracks.erase (std::remove (p.tracks.begin(), p.tracks.end(), id), p.tracks.end());
    exec ("BEGIN");
    Stmt (db, "DELETE FROM tracks WHERE id=?").bind (1, id).run();
    Stmt (db, "DELETE FROM playlist_tracks WHERE track=?").bind (1, id).run();
    exec ("COMMIT");
    ++changeCount;
}

void CentrifugeLibrary::addPlay (juce::int64 id)
{
    const juce::ScopedLock sl (lock);
    auto it = tracks.find (id);
    if (it == tracks.end()) return;
    ++it->second.plays;
    Stmt (db, "UPDATE tracks SET plays=? WHERE id=?").bind (1, (juce::int64) it->second.plays).bind (2, id).run();
    ++changeCount;
}

void CentrifugeLibrary::resetPlays (juce::int64 id)
{
    const juce::ScopedLock sl (lock);
    auto it = tracks.find (id);
    if (it == tracks.end()) return;
    it->second.plays = 0;
    Stmt (db, "UPDATE tracks SET plays=0 WHERE id=?").bind (1, id).run();
    ++changeCount;
}

void CentrifugeLibrary::setRating (juce::int64 id, int stars)
{
    const juce::ScopedLock sl (lock);
    auto it = tracks.find (id);
    if (it == tracks.end()) return;
    it->second.info.rating = juce::jlimit (0, 5, stars);
    Stmt (db, "UPDATE tracks SET info=? WHERE id=?").bind (1, juce::JSON::toString (infoToVar (it->second.info), true)).bind (2, id).run();
    ++changeCount;
}

void CentrifugeLibrary::saveVersions (juce::int64 id, const TrackVersion* rb, const TrackVersion* cf)
{
    const juce::ScopedLock sl (lock);
    auto it = tracks.find (id);
    if (it == tracks.end()) return;
    auto& t = it->second;
    if (rb != nullptr) { t.rb = *rb; t.rbEdited = true; }
    if (cf != nullptr) { t.cf = *cf; t.cfEdited = true; }
    t.differ = ! t.rb.sameAs (t.cf);
    Stmt s (db, "UPDATE tracks SET rb=?, cf=?, rb_edited=?, cf_edited=? WHERE id=?");
    s.bind (1, t.rb.toJson()).bind (2, t.cf.toJson()).bind (3, (juce::int64) t.rbEdited).bind (4, (juce::int64) t.cfEdited).bind (5, id);
    s.run();
    ++changeCount;
}

std::shared_ptr<LibrarySource> CentrifugeLibrary::makeSource (int view) const
{
    auto src = std::make_shared<LibrarySource>();
    src->kind = LibrarySource::Kind::centrifuge;
    src->libraryView = view;
    src->name = "Centrifuge library";
    src->playlists.name = "Playlists";
    src->playlists.isFolder = true;
    if (db == nullptr) { src->error = openError; return src; }

    const juce::ScopedLock sl (lock);
    std::map<juce::int64, int> indexOf;
    for (auto& [id, t] : tracks)
    {
        if ((t.hidden & (view == 0 ? 1 : 2)) != 0) continue;   // removed from this collection
        TrackInfo info = t.info;
        info.playCount = t.plays;
        const auto& v = view == 0 ? t.rb : t.cf;
        info.libraryId = id;
        info.libraryKey = t.key;
        if (! v.beats.empty() && v.beats.front().bpm > 0.0f) info.bpm = v.beats.front().bpm;
        int n = (int) v.memory.size();
        for (auto& h : v.hot) if (h) ++n;
        info.numCues = n;
        info.hasSkips = ! v.skips.empty();
        info.versionsDiffer = t.differ;
        indexOf[id] = (int) src->tracks.size();
        src->tracks.push_back (std::move (info));
    }

    std::function<void (PlaylistNode&, juce::int64)> build = [&] (PlaylistNode& node, juce::int64 parent)
    {
        std::vector<const Playlist*> kids;
        for (auto& p : playlists) if (p.parent == parent && p.id != parent) kids.push_back (&p);
        std::sort (kids.begin(), kids.end(), [] (auto* a, auto* b) { return a->seq < b->seq; });
        for (auto* k : kids)
        {
            PlaylistNode c;
            c.id = juce::String (k->id);
            c.name = k->name;
            c.isFolder = k->folder;
            if (k->folder) build (c, k->id);
            else
                for (auto tid : k->tracks)
                    if (auto f = indexOf.find (tid); f != indexOf.end()) c.trackIndices.push_back (f->second);
            node.children.push_back (std::move (c));
        }
    };
    build (src->playlists, 0);
    return src;
}

} // namespace orbit
