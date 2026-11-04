#include "PdbWriter.h"
#include "PdbStaticRows.h"
#include <map>

namespace orbit
{

namespace
{
    constexpr int pageSize = 4096;
    constexpr int heapStart = 0x28;
    constexpr int heapSize = pageSize - heapStart;   // 4056
    constexpr int groupBytes = 36;

    struct Bytes
    {
        std::vector<uint8_t> b;
        void u8 (uint32_t v)  { b.push_back ((uint8_t) v); }
        void u16 (uint32_t v) { u8 (v & 0xff); u8 ((v >> 8) & 0xff); }
        void u32 (uint32_t v) { u16 (v & 0xffff); u16 (v >> 16); }
        void raw (const std::vector<uint8_t>& v) { b.insert (b.end(), v.begin(), v.end()); }
        void at16 (size_t pos, uint32_t v) { b[pos] = (uint8_t) (v & 0xff); b[pos + 1] = (uint8_t) ((v >> 8) & 0xff); }
        size_t size() const { return b.size(); }
    };

    void put16 (std::vector<uint8_t>& p, size_t pos, uint32_t v) { p[pos] = (uint8_t) v; p[pos + 1] = (uint8_t) (v >> 8); }
    void put32 (std::vector<uint8_t>& p, size_t pos, uint32_t v) { put16 (p, pos, v & 0xffff); put16 (p, pos + 2, v >> 16); }

    bool isUtf16 (const std::vector<uint8_t>& enc) { return ! enc.empty() && enc[0] == 0x90; }

    /** A row plus where its "index shift" field is (0x20 x its position in the page). */
    struct Row
    {
        std::vector<uint8_t> data;
        int indexShiftAt = -1;
    };

    /** Appends strings after a row's offset array and fills the offsets in (u8 or u16 offsets). */
    void appendStrings (Bytes& r, size_t offsetArrayPos, bool wide, const std::vector<juce::String>& strings)
    {
        for (size_t i = 0; i < strings.size(); ++i)
        {
            auto enc = PdbWriter::encodeString (strings[i]);
            if (isUtf16 (enc))
                while (r.size() % 4 != 0) r.u8 (0);
            const auto ofs = (uint32_t) r.size();
            if (wide) r.at16 (offsetArrayPos + 2 + i * 2, ofs);
            else r.b[offsetArrayPos + 1 + i] = (uint8_t) ofs;
            r.raw (enc);
        }
    }

    Row trackRow (const PdbWriter::Track& t, uint32_t artistId, uint32_t albumId, uint32_t genreId, uint32_t keyId, uint32_t labelId)
    {
        Bytes r;
        r.u16 (0x24); r.u16 (0); r.u32 (0x000c0700); r.u32 (t.sampleRate); r.u32 (0); r.u32 (t.fileSize);
        r.u32 (juce::String (t.filePath).hashCode() & 0x7fffffff);
        r.u16 (0xfa80); r.u16 (0x05e7);
        r.u32 (0); r.u32 (keyId); r.u32 (0); r.u32 (labelId); r.u32 (0);
        r.u32 (t.bitrate); r.u32 (t.trackNumber); r.u32 (t.tempo); r.u32 (genreId); r.u32 (albumId); r.u32 (artistId); r.u32 (t.id);
        r.u16 (0); r.u16 (0); r.u16 (t.year); r.u16 (t.sampleDepth); r.u16 (juce::jmin<juce::uint32> (t.duration, 0xffff)); r.u16 (0x29);
        r.u8 ((uint32_t) juce::jlimit (0, 8, t.colour)); r.u8 ((uint32_t) juce::jlimit (0, 5, t.rating)); r.u16 ((uint32_t) t.fileType);
        jassert (r.size() == 0x5c);

        const size_t offs = r.size();
        r.u16 (3);
        for (int i = 0; i < 21; ++i) r.u16 (0);

        const auto fileName = t.filePath.fromLastOccurrenceOf ("/", false, false);
        appendStrings (r, offs, true, {
            {}, {}, "3", "2", {}, {}, {}, "ON", {}, {}, t.dateAdded, {}, {}, {},
            t.analyzePath, t.analyzeDate, t.comment, t.title, {}, fileName, t.filePath });
        return { r.b, 2 };
    }

    Row idNameRow (uint32_t id, const juce::String& name)
    {
        Bytes r;
        r.u32 (id);
        r.raw (PdbWriter::encodeString (name));
        return { r.b, -1 };
    }

    Row keyRow (uint32_t id, const juce::String& name)
    {
        Bytes r;
        r.u32 (id); r.u32 (id);
        r.raw (PdbWriter::encodeString (name));
        return { r.b, -1 };
    }

    Row artistRow (uint32_t id, const juce::String& name)
    {
        const auto enc = PdbWriter::encodeString (name);
        const bool wide = 10 + enc.size() > 250 || isUtf16 (enc);
        Bytes r;
        r.u16 (wide ? 0x64 : 0x60); r.u16 (0); r.u32 (id);
        const size_t offs = r.size();
        if (wide) { r.u16 (3); r.u16 (0); } else { r.u8 (3); r.u8 (0); }
        appendStrings (r, offs, wide, { name });
        return { r.b, 2 };
    }

    Row albumRow (uint32_t id, uint32_t artistId, const juce::String& name)
    {
        const auto enc = PdbWriter::encodeString (name);
        const bool wide = 22 + enc.size() > 250 || isUtf16 (enc);
        Bytes r;
        r.u16 (wide ? 0x84 : 0x80); r.u16 (0); r.u32 (0); r.u32 (artistId); r.u32 (id); r.u32 (0);
        const size_t offs = r.size();
        if (wide) { r.u16 (3); r.u16 (0); } else { r.u8 (3); r.u8 (0); }
        appendStrings (r, offs, wide, { name });
        return { r.b, 2 };
    }

    Row playlistTreeRow (const PdbWriter::Playlist& p)
    {
        Bytes r;
        r.u32 (p.parent); r.u32 (0); r.u32 (p.sortOrder); r.u32 (p.id); r.u32 (p.folder ? 1 : 0);
        r.raw (PdbWriter::encodeString (p.name));
        return { r.b, -1 };
    }

    Row playlistEntryRow (uint32_t index, uint32_t trackId, uint32_t playlistId)
    {
        Bytes r;
        r.u32 (index); r.u32 (trackId); r.u32 (playlistId);
        return { r.b, -1 };
    }

    Row historyRow (uint32_t numTracks)
    {
        Bytes r;
        r.u16 (0x0280); r.u16 (0); r.u32 (numTracks); r.u32 (0);
        r.raw (PdbWriter::encodeString (juce::Time::getCurrentTime().formatted ("%Y-%m-%d")));
        r.u16 (0x1e19);
        r.raw (PdbWriter::encodeString ("1000"));
        r.raw (PdbWriter::encodeString ({}));
        return { r.b, 2 };
    }

    struct TableSpec
    {
        uint32_t type;
        std::vector<Row> rows;
        bool indexed;          // data pages listed in the table's index page (flags 0x34), else 0x24
        bool staticCounts;     // fixed tables use (rows, 0) in the data page header
    };

    size_t align4 (size_t n) { return (n + 3) & ~(size_t) 3; }

    /** Splits rows into pages: each page holds rows in its heap and 36-byte row groups at its end. */
    std::vector<std::vector<const Row*>> packRows (const std::vector<Row>& rows)
    {
        std::vector<std::vector<const Row*>> pages;
        std::vector<const Row*> cur;
        size_t used = 0;
        for (auto& r : rows)
        {
            const size_t len = align4 (r.data.size());
            const size_t groups = (cur.size() + 1 + 15) / 16;
            if (! cur.empty() && used + len + groups * groupBytes > (size_t) heapSize)
            {
                pages.push_back (cur);
                cur.clear();
                used = 0;
            }
            cur.push_back (&r);
            used += len;
        }
        if (! cur.empty()) pages.push_back (cur);
        return pages;
    }

    std::vector<uint8_t> dataPage (uint32_t index, uint32_t type, uint32_t next, const std::vector<const Row*>& rows,
                                   bool indexed, bool staticCounts)
    {
        std::vector<uint8_t> p ((size_t) pageSize, 0);
        const int n = (int) rows.size();
        size_t used = 0;
        std::vector<uint16_t> offsets;
        for (int i = 0; i < n; ++i)
        {
            auto data = rows[(size_t) i]->data;
            if (rows[(size_t) i]->indexShiftAt >= 0)
                put16 (data, (size_t) rows[(size_t) i]->indexShiftAt, (uint32_t) (0x20 * i));
            offsets.push_back ((uint16_t) used);
            std::copy (data.begin(), data.end(), p.begin() + heapStart + (long) used);
            used += align4 (data.size());
        }

        const int groups = (n + 15) / 16;
        int freeSize = heapSize - (int) used;
        for (int g = 0; g < groups; ++g)
        {
            const size_t gb = (size_t) (pageSize - g * groupBytes);
            int inGroup = juce::jmin (16, n - g * 16);
            uint32_t mask = 0;
            for (int k = 0; k < inGroup; ++k)
            {
                put16 (p, gb - 6 - 2 * (size_t) k, offsets[(size_t) (g * 16 + k)]);
                mask |= 1u << k;
            }
            put16 (p, gb - 4, mask);
            put16 (p, gb - 2, staticCounts ? mask : (1u << (inGroup - 1)));
            freeSize -= 4 + 2 * inGroup;
        }

        put32 (p, 4, index);
        put32 (p, 8, type);
        put32 (p, 12, next);
        put32 (p, 16, 1);
        put32 (p, 24, (uint32_t) n | ((uint32_t) n << 13) | ((indexed ? 0x34u : 0x24u) << 24));
        put16 (p, 28, (uint32_t) juce::jmax (0, freeSize));
        put16 (p, 30, (uint32_t) used);
        put16 (p, 32, staticCounts ? (uint32_t) n : 1u);
        put16 (p, 34, staticCounts ? 0u : (uint32_t) juce::jmax (0, n - 1));
        return p;
    }

    std::vector<uint8_t> indexPage (uint32_t index, uint32_t type, uint32_t next, const std::vector<uint32_t>& entries)
    {
        std::vector<uint8_t> p ((size_t) pageSize, 0);
        put32 (p, 4, index);
        put32 (p, 8, type);
        put32 (p, 12, next);
        put32 (p, 16, 1);
        put32 (p, 24, 0x64u << 24);
        const size_t h = 0x20;
        put16 (p, h + 0, 0x1fff);
        put16 (p, h + 2, 0x1fff);
        put16 (p, h + 4, 0x03ec);
        put16 (p, h + 6, (uint32_t) entries.size());
        put32 (p, h + 8, index);
        put32 (p, h + 12, entries.empty() ? 0x03ffffffu : entries.front());
        put32 (p, h + 16, 0x03ffffff);
        put32 (p, h + 20, 0);
        put16 (p, h + 24, (uint32_t) entries.size());
        put16 (p, h + 26, 0x1fff);
        size_t e = h + 28;
        for (int i = 0; i < 1004; ++i, e += 4)
            put32 (p, e, i < (int) entries.size() ? entries[(size_t) i] << 3 : 0x1ffffff8u);
        return p;   // the last 20 bytes stay zero
    }
}

std::vector<uint8_t> PdbWriter::encodeString (const juce::String& s)
{
    std::vector<uint8_t> out;
    bool ascii = true;
    for (auto c : s) if (c < 0x20 || c > 0x7e) { ascii = false; break; }

    if (ascii)
    {
        const auto bytes = s.toStdString();
        if (bytes.size() <= 126)
        {
            out.push_back ((uint8_t) (((bytes.size() + 1) << 1) | 1));
            out.insert (out.end(), bytes.begin(), bytes.end());
            return out;
        }
        const auto len = juce::jmin<size_t> (bytes.size(), 0xfff0);
        out.push_back (0x40);
        out.push_back ((uint8_t) ((len + 4) & 0xff));
        out.push_back ((uint8_t) ((len + 4) >> 8));
        out.push_back (0);
        out.insert (out.end(), bytes.begin(), bytes.begin() + (long) len);
        return out;
    }

    std::vector<uint16_t> u16;
    for (auto c : s)
    {
        if (c >= 0x10000)
        {
            const auto v = (uint32_t) c - 0x10000;
            u16.push_back ((uint16_t) (0xd800 + (v >> 10)));
            u16.push_back ((uint16_t) (0xdc00 + (v & 0x3ff)));
        }
        else u16.push_back ((uint16_t) c);
    }
    const size_t len = u16.size() * 2;
    out.push_back (0x90);
    out.push_back ((uint8_t) ((len + 4) & 0xff));
    out.push_back ((uint8_t) ((len + 4) >> 8));
    out.push_back (0);
    for (auto c : u16) { out.push_back ((uint8_t) (c & 0xff)); out.push_back ((uint8_t) (c >> 8)); }
    return out;
}

int PdbWriter::fileTypeFor (const juce::File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    if (e == ".mp3") return 1;
    if (e == ".m4a" || e == ".aac" || e == ".mp4") return 4;
    if (e == ".flac") return 5;
    if (e == ".wav") return 11;
    if (e == ".aif" || e == ".aiff") return 12;
    return 0;
}

bool PdbWriter::write (const juce::File& file, const std::vector<Track>& tracks, const std::vector<Playlist>& playlists,
                       juce::String& error)
{
    // --- Lookup tables -------------------------------------------------------------
    std::map<juce::String, uint32_t> artists, genres, labels, keys;
    std::map<std::pair<juce::String, uint32_t>, uint32_t> albums;
    std::vector<Row> artistRows, genreRows, labelRows, keyRows, albumRows, trackRows;

    auto idFor = [] (std::map<juce::String, uint32_t>& m, std::vector<Row>& rows, const juce::String& name, auto makeRow) -> uint32_t
    {
        if (name.isEmpty()) return 0;
        auto it = m.find (name);
        if (it != m.end()) return it->second;
        const auto id = (uint32_t) m.size() + 1;
        m[name] = id;
        rows.push_back (makeRow (id, name));
        return id;
    };

    for (auto& t : tracks)
    {
        const auto artistId = idFor (artists, artistRows, t.artist, artistRow);
        const auto genreId = idFor (genres, genreRows, t.genre, idNameRow);
        const auto labelId = idFor (labels, labelRows, t.label, idNameRow);
        const auto keyId = idFor (keys, keyRows, t.key, keyRow);
        uint32_t albumId = 0;
        if (t.album.isNotEmpty())
        {
            auto k = std::make_pair (t.album, artistId);
            auto it = albums.find (k);
            if (it != albums.end()) albumId = it->second;
            else
            {
                albumId = (uint32_t) albums.size() + 1;
                albums[k] = albumId;
                albumRows.push_back (albumRow (albumId, artistId, t.album));
            }
        }
        trackRows.push_back (trackRow (t, artistId, albumId, genreId, keyId, labelId));
    }

    std::vector<Row> treeRows, entryRows;
    for (auto& p : playlists)
    {
        treeRows.push_back (playlistTreeRow (p));
        for (size_t i = 0; i < p.trackIds.size(); ++i)
            entryRows.push_back (playlistEntryRow ((uint32_t) i + 1, p.trackIds[i], p.id));
    }

    auto staticRows = [] (const std::vector<std::vector<uint8_t>>& src)
    {
        std::vector<Row> v;
        for (auto& r : src) v.push_back ({ r, -1 });
        return v;
    };

    std::vector<TableSpec> tables;
    tables.push_back ({ 0, trackRows, true, false });
    tables.push_back ({ 1, genreRows, true, false });
    tables.push_back ({ 2, artistRows, true, false });
    tables.push_back ({ 3, albumRows, true, false });
    tables.push_back ({ 4, labelRows, false, false });
    tables.push_back ({ 5, keyRows, false, false });
    tables.push_back ({ 6, staticRows (PdbStatic::colors()), false, true });
    tables.push_back ({ 7, treeRows, true, false });
    tables.push_back ({ 8, entryRows, true, false });
    for (uint32_t t : { 9u, 10u, 11u, 12u, 13u, 14u, 15u })
        tables.push_back ({ t, {}, t == 11 || t == 12, false });
    tables.push_back ({ 16, staticRows (PdbStatic::columns()), false, true });
    tables.push_back ({ 17, staticRows (PdbStatic::menu()), false, true });
    tables.push_back ({ 18, staticRows (PdbStatic::table18()), false, true });
    tables.push_back ({ 19, { historyRow ((uint32_t) tracks.size()) }, true, false });

    // --- Page allocation -----------------------------------------------------------
    struct Layout { uint32_t first = 0, last = 0, emptyCandidate = 0; std::vector<uint32_t> dataPages; std::vector<std::vector<const Row*>> packed; };
    std::vector<Layout> layout (tables.size());
    uint32_t nextPage = 1;
    for (size_t i = 0; i < tables.size(); ++i)
    {
        auto& L = layout[i];
        L.packed = packRows (tables[i].rows);
        L.first = nextPage++;
        for (size_t k = 0; k < L.packed.size(); ++k) L.dataPages.push_back (nextPage++);
        L.last = L.dataPages.empty() ? L.first : L.dataPages.back();
    }
    for (auto& L : layout) L.emptyCandidate = nextPage++;   // past the end of the file, as rekordbox does
    const uint32_t nextUnused = nextPage;

    std::vector<std::vector<uint8_t>> pages;
    {
        std::vector<uint8_t> h ((size_t) pageSize, 0);
        put32 (h, 4, pageSize);
        put32 (h, 8, (uint32_t) tables.size());
        put32 (h, 12, nextUnused);
        put32 (h, 16, 1);
        put32 (h, 20, 2);
        for (size_t i = 0; i < tables.size(); ++i)
        {
            const size_t o = 0x1c + i * 16;
            put32 (h, o, tables[i].type);
            put32 (h, o + 4, layout[i].emptyCandidate);
            put32 (h, o + 8, layout[i].first);
            put32 (h, o + 12, layout[i].last);
        }
        pages.push_back (std::move (h));
    }

    for (size_t i = 0; i < tables.size(); ++i)
    {
        const auto& T = tables[i];
        const auto& L = layout[i];
        std::vector<uint32_t> listed;
        if (T.indexed) listed = L.dataPages;
        pages.push_back (indexPage (L.first, T.type, L.dataPages.empty() ? L.emptyCandidate : L.dataPages.front(), listed));
        for (size_t k = 0; k < L.packed.size(); ++k)
        {
            const uint32_t next = k + 1 < L.dataPages.size() ? L.dataPages[k + 1] : L.emptyCandidate;
            pages.push_back (dataPage (L.dataPages[k], T.type, next, L.packed[k], T.indexed, T.staticCounts));
        }
    }

    file.getParentDirectory().createDirectory();
    auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    tmp.deleteFile();
    {
        juce::FileOutputStream os (tmp);
        if (! os.openedOk()) { error = "Couldn't write " + tmp.getFullPathName(); return false; }
        for (auto& p : pages) os.write (p.data(), p.size());
        os.flush();
        if (os.getStatus().failed()) { error = os.getStatus().getErrorMessage(); return false; }
    }
    file.deleteFile();
    if (! tmp.moveFileTo (file)) { error = "Couldn't replace " + file.getFullPathName(); return false; }
    return true;
}

} // namespace orbit
