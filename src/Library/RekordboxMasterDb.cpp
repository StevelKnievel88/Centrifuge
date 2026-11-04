#include "RekordboxMasterDb.h"
#include "RekordboxColours.h"
#include "sqlite3mc.h"
#include <map>
#include <unordered_map>

namespace orbit
{
namespace
{
    struct Db
    {
        sqlite3* db = nullptr;
        ~Db() { if (db != nullptr) sqlite3_close (db); }

        bool exec (const char* sql, juce::String* error = nullptr)
        {
            char* err = nullptr;
            const int rc = sqlite3_exec (db, sql, nullptr, nullptr, &err);
            if (rc != SQLITE_OK && error != nullptr)
                *error = err != nullptr ? juce::String::fromUTF8 (err) : juce::String ("error ") + juce::String (rc);
            if (err != nullptr) sqlite3_free (err);
            return rc == SQLITE_OK;
        }

        bool hasColumn (const char* table, const char* column)
        {
            juce::String sql = "PRAGMA table_info(" + juce::String (table) + ")";
            sqlite3_stmt* st = nullptr;
            bool found = false;
            if (sqlite3_prepare_v2 (db, sql.toRawUTF8(), -1, &st, nullptr) == SQLITE_OK)
            {
                while (sqlite3_step (st) == SQLITE_ROW)
                    if (juce::String::fromUTF8 ((const char*) sqlite3_column_text (st, 1)) == column)
                        found = true;
            }
            sqlite3_finalize (st);
            return found;
        }

        template <typename Fn>
        bool query (const juce::String& sql, Fn&& rowFn, juce::String* error = nullptr)
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2 (db, sql.toRawUTF8(), -1, &st, nullptr) != SQLITE_OK)
            {
                if (error != nullptr) *error = juce::String::fromUTF8 (sqlite3_errmsg (db));
                return false;
            }
            while (sqlite3_step (st) == SQLITE_ROW)
                rowFn (st);
            sqlite3_finalize (st);
            return true;
        }
    };

    juce::String text (sqlite3_stmt* st, int col)
    {
        auto* t = sqlite3_column_text (st, col);
        return t != nullptr ? juce::String::fromUTF8 ((const char*) t) : juce::String();
    }

    int integer (sqlite3_stmt* st, int col) { return sqlite3_column_int (st, col); }

    /** rekordbox stores hot cue letters in djmdCue.Kind. Most libraries use 1..8 for A..H,
        but some versions skip 4 (D = 5 ... H = 9). Detect which scheme a library uses. */
    int hotCueIndexFromKind (int kind, bool skipsFour)
    {
        if (kind <= 0) return -1;
        if (! skipsFour) return juce::jlimit (0, 7, kind - 1);
        if (kind <= 3) return kind - 1;
        return juce::jlimit (0, 7, kind - 2);
    }

    juce::String notDeleted (Db& db, const char* table, const char* alias)
    {
        if (db.hasColumn (table, "rb_local_deleted"))
            return juce::String (" IFNULL(") + alias + ".rb_local_deleted,0)=0 ";
        return " 1 ";
    }
}

juce::File defaultRekordboxDbFile()
{
   #if JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Pioneer").getChildFile ("rekordbox").getChildFile ("master.db");
   #elif JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Pioneer").getChildFile ("rekordbox").getChildFile ("master.db");
   #else
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
             .getChildFile (".Pioneer").getChildFile ("rekordbox").getChildFile ("master.db");
   #endif
}

juce::String defaultRekordboxDbKey()
{
    return "402fd482c38817c35ffa8ffb8c7d93143b749e7d315df7a81732a1ff43608497";
}

std::unique_ptr<LibrarySource> loadRekordboxMasterDb (const juce::File& dbFile, const juce::String& key, juce::File shareDir)
{
    auto src = std::make_unique<LibrarySource>();
    src->kind = LibrarySource::Kind::pcCollection;
    src->name = "rekordbox collection";
    src->root = dbFile;
    src->playlists.name = "Playlists";
    src->playlists.isFolder = true;

    if (! dbFile.existsAsFile())
    {
        src->error = "rekordbox library not found at " + dbFile.getFullPathName();
        return src;
    }

    if (shareDir == juce::File())
        shareDir = dbFile.getParentDirectory().getChildFile ("share");

    // Work on a private copy so rekordbox's own files are never touched or locked.
    auto copyDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Centrifuge-dbcopy");
    copyDir.createDirectory();
    auto copy = copyDir.getChildFile ("master.db");
    auto copyWal = copyDir.getChildFile ("master.db-wal");
    copyDir.getChildFile ("master.db-shm").deleteFile();
    copyWal.deleteFile();

    if (! dbFile.copyFileTo (copy))
    {
        src->error = "Couldn't copy " + dbFile.getFullPathName() + " (is it locked?)";
        return src;
    }
    auto wal = dbFile.getSiblingFile (dbFile.getFileName() + "-wal");
    if (wal.existsAsFile())
        wal.copyFileTo (copyWal);

    Db db;
    if (sqlite3_open_v2 (copy.getFullPathName().toRawUTF8(), &db.db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
    {
        src->error = "Couldn't open the rekordbox database copy";
        return src;
    }

    if (key.isNotEmpty())
    {
        juce::String pragmas = "PRAGMA cipher='sqlcipher'; PRAGMA legacy=4; PRAGMA key='" + key.removeCharacters ("'") + "';";
        db.exec (pragmas.toRawUTF8());
    }

    juce::String err;
    if (! db.exec ("SELECT count(*) FROM sqlite_master;", &err))
    {
        src->error = "Couldn't unlock the rekordbox database (wrong key?): " + err;
        return src;
    }

    // --- Tracks ----------------------------------------------------------
    std::unordered_map<juce::String, int> indexById;
    const juce::String trackSql =
        "SELECT c.ID, c.Title, a.Name, al.Name, g.Name, k.ScaleName, c.BPM, c.Length, c.FolderPath, "
        "c.AnalysisDataPath, c.Rating, c.ColorID, c.SampleRate, c.BitRate, c.Commnt, c.StockDate, l.Name "
        "FROM djmdContent c "
        "LEFT JOIN djmdArtist a ON a.ID = c.ArtistID "
        "LEFT JOIN djmdAlbum al ON al.ID = c.AlbumID "
        "LEFT JOIN djmdGenre g ON g.ID = c.GenreID "
        "LEFT JOIN djmdKey k ON k.ID = c.KeyID "
        "LEFT JOIN djmdLabel l ON l.ID = c.LabelID "
        "WHERE" + notDeleted (db, "djmdContent", "c");

    const bool ok = db.query (trackSql, [&] (sqlite3_stmt* st)
    {
        TrackInfo t;
        t.id = text (st, 0);
        t.title = text (st, 1);
        t.artist = text (st, 2);
        t.album = text (st, 3);
        t.genre = text (st, 4);
        t.key = text (st, 5);
        t.bpm = integer (st, 6) / 100.0;
        t.durationSec = integer (st, 7);
        const auto path = text (st, 8);
        if (juce::File::isAbsolutePath (path))
            t.file = juce::File (path);
        const auto anlz = text (st, 9);
        if (anlz.isNotEmpty())
            t.anlzBase = shareDir.getChildFile (anlz.trimCharactersAtStart ("/\\"));
        t.rating = integer (st, 10);
        t.colour = rekordboxTrackColour (text (st, 11).getIntValue());
        t.sampleRate = integer (st, 12);
        t.bitrate = integer (st, 13);
        t.comment = text (st, 14);
        t.dateAdded = text (st, 15);
        t.label = text (st, 16);

        if (t.title.isEmpty())
            t.title = t.file.getFileNameWithoutExtension();

        indexById[t.id] = (int) src->tracks.size();
        src->tracks.push_back (std::move (t));
    }, &err);

    if (! ok)
    {
        src->error = "Couldn't read tracks: " + err;
        return src;
    }

    // --- Cues --------------------------------------------------------------
    {
        struct RawCue { juce::String content; int in, out, kind; juce::String comment; int colour, colourIndex; };
        std::vector<RawCue> raw;
        int maxKind = 0;
        bool hasFour = false;
        // Cue colours: djmdCue.Color is the hot cue colour code, ColorTableIndex the memory cue colour (when present).
        const juce::String colourCols = juce::String (db.hasColumn ("djmdCue", "Color") ? ", c.Color" : ", -1")
                                      + (db.hasColumn ("djmdCue", "ColorTableIndex") ? ", c.ColorTableIndex" : ", -1");
        db.query ("SELECT ContentID, InMsec, OutMsec, Kind, Comment" + colourCols + " FROM djmdCue c WHERE" + notDeleted (db, "djmdCue", "c"),
                  [&] (sqlite3_stmt* st)
                  {
                      RawCue c { text (st, 0), integer (st, 1), integer (st, 2), integer (st, 3), text (st, 4), integer (st, 5), integer (st, 6) };
                      maxKind = std::max (maxKind, c.kind);
                      hasFour = hasFour || c.kind == 4;
                      raw.push_back (c);
                  });

        const bool skipsFour = maxKind == 9 && ! hasFour;
        for (auto& c : raw)
        {
            auto it = indexById.find (c.content);
            if (it == indexById.end()) continue;

            CuePoint cue;
            cue.type = c.kind > 0 ? CuePoint::Type::hot : CuePoint::Type::memory;
            cue.hotIndex = hotCueIndexFromKind (c.kind, skipsFour);
            cue.startSec = c.in / 1000.0;
            if (c.out > c.in) cue.endSec = c.out / 1000.0;
            cue.comment = c.comment;
            cue.colour = cue.type == CuePoint::Type::hot ? juce::Colour (0xff28e214) : juce::Colour (0xffe8102a);
            if (cue.type == CuePoint::Type::hot && c.colour > 0 && c.colour <= 62)
                cue.colour = rekordboxHotCueColour (c.colour);
            else if (cue.type == CuePoint::Type::memory && c.colourIndex > 0 && c.colourIndex <= 8)
                cue.colour = rekordboxMemoryColour (c.colourIndex);
            src->tracks[(size_t) it->second].dbCues.push_back (cue);
        }
    }

    // --- Playlists -------------------------------------------------------
    struct RawPlaylist { juce::String id, parent, name; int seq, attribute; };
    std::vector<RawPlaylist> playlists;
    db.query ("SELECT ID, ParentID, Name, Seq, Attribute FROM djmdPlaylist p WHERE" + notDeleted (db, "djmdPlaylist", "p"),
              [&] (sqlite3_stmt* st)
              {
                  playlists.push_back ({ text (st, 0), text (st, 1), text (st, 2), integer (st, 3), integer (st, 4) });
              });

    std::map<juce::String, std::vector<std::pair<int, int>>> songs; // playlist -> (trackNo, trackIndex)
    db.query ("SELECT PlaylistID, ContentID, TrackNo FROM djmdSongPlaylist s WHERE" + notDeleted (db, "djmdSongPlaylist", "s"),
              [&] (sqlite3_stmt* st)
              {
                  auto it = indexById.find (text (st, 1));
                  if (it != indexById.end())
                      songs[text (st, 0)].push_back ({ integer (st, 2), it->second });
              });

    std::function<void (PlaylistNode&, const juce::String&, int)> build = [&] (PlaylistNode& node, const juce::String& parent, int depth)
    {
        if (depth > 32) return;
        std::vector<const RawPlaylist*> kids;
        for (auto& p : playlists)
            if (p.parent == parent && p.attribute >= 0 && p.id != parent)
                kids.push_back (&p);
        std::sort (kids.begin(), kids.end(), [] (auto* a, auto* b) { return a->seq < b->seq; });

        for (auto* k : kids)
        {
            PlaylistNode child;
            child.id = k->id;
            child.name = k->name;
            child.isFolder = k->attribute == 1;
            child.isSmart = k->attribute == 4;

            if (child.isFolder)
                build (child, k->id, depth + 1);
            else
            {
                auto& s = songs[k->id];
                std::sort (s.begin(), s.end());
                for (auto& [no, idx] : s)
                    child.trackIndices.push_back (idx);
            }
            node.children.push_back (std::move (child));
        }
    };
    build (src->playlists, "root", 0);

    return src;
}

} // namespace orbit
