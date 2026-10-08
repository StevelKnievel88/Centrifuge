#include "RekordboxPdb.h"
#include <map>
#include <unordered_map>

namespace orbit
{
namespace
{
    enum PageType : uint32_t
    {
        tracks = 0, genres = 1, artists = 2, albums = 3, labels = 4, keys = 5, colors = 6,
        playlistTree = 7, playlistEntries = 8
    };

    // DeviceSQL files are little-endian.
    struct LE
    {
        const uint8_t* d;
        size_t n;

        bool has (size_t off, size_t len) const noexcept { return off + len <= n; }
        uint32_t u32 (size_t off) const noexcept
        {
            if (! has (off, 4)) return 0;
            return uint32_t (d[off]) | (uint32_t (d[off + 1]) << 8) | (uint32_t (d[off + 2]) << 16) | (uint32_t (d[off + 3]) << 24);
        }
        uint16_t u16 (size_t off) const noexcept { return has (off, 2) ? uint16_t (d[off] | (d[off + 1] << 8)) : 0; }
        uint8_t u8 (size_t off) const noexcept { return has (off, 1) ? d[off] : 0; }

        juce::String deviceSqlString (size_t off) const
        {
            if (! has (off, 1)) return {};
            const auto lk = d[off];

            if (lk == 0x40 || lk == 0x90)
            {
                const auto len = u16 (off + 1);
                if (len < 4 || ! has (off, len)) return {};
                const size_t bodyLen = len - 4u;
                const auto* body = d + off + 4;

                if (lk == 0x40)
                    return juce::String::fromUTF8 (reinterpret_cast<const char*> (body), (int) bodyLen);

                juce::String s;
                for (size_t i = 0; i + 1 < bodyLen; i += 2)
                {
                    auto c = (juce::juce_wchar) (body[i] | (body[i + 1] << 8));
                    if (c == 0) break;
                    s += juce::String::charToString (c);
                }
                return s;
            }

            if ((lk & 1) != 0)
            {
                const size_t len = (size_t) (lk >> 1);
                if (len < 1 || ! has (off, len)) return {};
                return juce::String::fromUTF8 (reinterpret_cast<const char*> (d + off + 1), (int) len - 1);
            }

            return {};
        }
    };

    struct RawTrack
    {
        uint32_t id = 0, artistId = 0, albumId = 0, genreId = 0, keyId = 0, labelId = 0, tempo = 0;
        uint32_t sampleRate = 0, bitrate = 0;
        uint16_t duration = 0;
        uint8_t colour = 0, rating = 0;
        juce::String title, filePath, analyzePath, dateAdded, comment;
    };

    struct RawPlaylist
    {
        uint32_t id = 0, parent = 0, sortOrder = 0;
        bool isFolder = false;
        juce::String name;
    };

    struct RawEntry
    {
        uint32_t index = 0, trackId = 0, playlistId = 0;
    };

    template <typename Fn>
    void forEachRow (const LE& b, uint32_t lenPage, uint32_t tableType, uint32_t firstPage, uint32_t lastPage, Fn&& fn)
    {
        const uint32_t numPagesInFile = (uint32_t) (b.n / lenPage);
        uint32_t page = firstPage;

        for (uint32_t guard = 0; guard <= numPagesInFile; ++guard)
        {
            const size_t base = (size_t) page * lenPage;
            if (! b.has (base, lenPage)) break;

            const auto pageIndex = b.u32 (base + 4);
            const auto type = b.u32 (base + 8);
            const auto nextPage = b.u32 (base + 12);

            const uint32_t packed = uint32_t (b.u8 (base + 24)) | (uint32_t (b.u8 (base + 25)) << 8) | (uint32_t (b.u8 (base + 26)) << 16);
            const uint32_t numRowOffsets = packed & 0x1fff;
            const auto flags = b.u8 (base + 27);
            const bool isDataPage = (flags & 0x40) == 0;
            const size_t heap = base + 40;

            if (pageIndex == page && type == tableType && isDataPage && numRowOffsets > 0 && numRowOffsets != 0x1fff)
            {
                const uint32_t numGroups = (numRowOffsets - 1) / 16 + 1;

                for (uint32_t g = 0; g < numGroups; ++g)
                {
                    const size_t groupBase = base + lenPage - (size_t) g * 0x24;
                    const auto present = b.u16 (groupBase - 4);

                    for (uint32_t r = 0; r < 16; ++r)
                    {
                        if (g * 16 + r >= numRowOffsets) break;
                        if (((present >> r) & 1) == 0) continue;
                        const auto ofs = b.u16 (groupBase - (6 + 2 * r));
                        const size_t row = heap + ofs;
                        if (row < base + lenPage)
                            fn (row);
                    }
                }
            }

            if (page == lastPage || nextPage == page) break;
            page = nextPage;
        }
    }

    juce::File resolveExportPath (const juce::File& usbRoot, const juce::String& path)
    {
        if (path.isEmpty()) return {};
        auto rel = path.replaceCharacter ('\\', '/').trimCharactersAtStart ("/");
        return usbRoot.getChildFile (rel);
    }
}

std::unique_ptr<LibrarySource> parseExportPdb (const juce::MemoryBlock& data, const juce::File& usbRoot)
{
    auto src = std::make_unique<LibrarySource>();
    src->kind = LibrarySource::Kind::usbExport;
    src->root = usbRoot;
    src->name = "USB " + usbRoot.getFileName();
    src->playlists.name = "Playlists";
    src->playlists.isFolder = true;

    LE b { static_cast<const uint8_t*> (data.getData()), data.getSize() };
    const auto lenPage = b.u32 (4);
    const auto numTables = b.u32 (8);

    if (lenPage < 512 || lenPage > 65536 || numTables == 0 || numTables > 64)
    {
        src->error = "Not a rekordbox export.pdb";
        return src;
    }

    std::unordered_map<uint32_t, juce::String> artistNames, albumNames, genreNames, keyNames, labelNames;
    std::vector<RawTrack> rawTracks;
    std::vector<RawPlaylist> rawPlaylists;
    std::vector<RawEntry> rawEntries;

    for (uint32_t t = 0; t < numTables; ++t)
    {
        const size_t tp = 28 + t * 16;
        const auto type = b.u32 (tp);
        const auto firstPage = b.u32 (tp + 8);
        const auto lastPage = b.u32 (tp + 12);

        switch (type)
        {
            case tracks:
                forEachRow (b, lenPage, type, firstPage, lastPage, [&] (size_t r)
                {
                    RawTrack tr;
                    tr.sampleRate = b.u32 (r + 8);
                    tr.keyId = b.u32 (r + 32);
                    tr.labelId = b.u32 (r + 40);
                    tr.bitrate = b.u32 (r + 48);
                    tr.tempo = b.u32 (r + 56);
                    tr.genreId = b.u32 (r + 60);
                    tr.albumId = b.u32 (r + 64);
                    tr.artistId = b.u32 (r + 68);
                    tr.id = b.u32 (r + 72);
                    tr.duration = b.u16 (r + 84);
                    tr.colour = b.u8 (r + 88);
                    tr.rating = b.u8 (r + 89);

                    auto str = [&] (int index) { return b.deviceSqlString (r + b.u16 (r + 94 + (size_t) index * 2)); };
                    tr.dateAdded = str (10);
                    tr.analyzePath = str (14);
                    tr.comment = str (16);
                    tr.title = str (17);
                    tr.filePath = str (20);
                    if (tr.id != 0)
                        rawTracks.push_back (std::move (tr));
                });
                break;

            case genres:
            case labels:
                forEachRow (b, lenPage, type, firstPage, lastPage, [&] (size_t r)
                {
                    (type == genres ? genreNames : labelNames)[b.u32 (r)] = b.deviceSqlString (r + 4);
                });
                break;

            case keys:
                forEachRow (b, lenPage, type, firstPage, lastPage, [&] (size_t r)
                {
                    keyNames[b.u32 (r)] = b.deviceSqlString (r + 8);
                });
                break;

            case artists:
                forEachRow (b, lenPage, type, firstPage, lastPage, [&] (size_t r)
                {
                    const auto subtype = b.u16 (r);
                    const size_t ofs = (subtype & 0x04) ? b.u16 (r + 10) : b.u8 (r + 9);
                    artistNames[b.u32 (r + 4)] = b.deviceSqlString (r + ofs);
                });
                break;

            case albums:
                forEachRow (b, lenPage, type, firstPage, lastPage, [&] (size_t r)
                {
                    const auto subtype = b.u16 (r);
                    const size_t ofs = (subtype & 0x04) ? b.u16 (r + 22) : b.u8 (r + 21);
                    albumNames[b.u32 (r + 12)] = b.deviceSqlString (r + ofs);
                });
                break;

            case playlistTree:
                forEachRow (b, lenPage, type, firstPage, lastPage, [&] (size_t r)
                {
                    RawPlaylist p;
                    p.parent = b.u32 (r);
                    p.sortOrder = b.u32 (r + 8);
                    p.id = b.u32 (r + 12);
                    p.isFolder = b.u32 (r + 16) != 0;
                    p.name = b.deviceSqlString (r + 20);
                    rawPlaylists.push_back (std::move (p));
                });
                break;

            case playlistEntries:
                forEachRow (b, lenPage, type, firstPage, lastPage, [&] (size_t r)
                {
                    rawEntries.push_back ({ b.u32 (r), b.u32 (r + 4), b.u32 (r + 8) });
                });
                break;

            default:
                break;
        }
    }

    // Tracks (dedupe by id, keep the last written row).
    std::map<uint32_t, size_t> trackIndexById;
    for (auto& rt : rawTracks)
    {
        TrackInfo t;
        t.id = juce::String (rt.id);
        t.title = rt.title;
        auto lookup = [] (auto& m, uint32_t k) { auto it = m.find (k); return it != m.end() ? it->second : juce::String(); };
        t.artist = lookup (artistNames, rt.artistId);
        t.album = lookup (albumNames, rt.albumId);
        t.genre = lookup (genreNames, rt.genreId);
        t.key = lookup (keyNames, rt.keyId);
        t.label = lookup (labelNames, rt.labelId);
        t.comment = rt.comment;
        t.dateAdded = rt.dateAdded;
        t.bpm = rt.tempo / 100.0;
        t.durationSec = rt.duration;
        t.rating = rt.rating / 20;
        t.sampleRate = (int) rt.sampleRate;
        t.bitrate = (int) rt.bitrate;
        t.colour = rekordboxTrackColour (rt.colour);
        t.file = resolveExportPath (usbRoot, rt.filePath);
        t.anlzBase = resolveExportPath (usbRoot, rt.analyzePath);

        auto existing = trackIndexById.find (rt.id);
        if (existing != trackIndexById.end())
            src->tracks[existing->second] = std::move (t);
        else
        {
            trackIndexById[rt.id] = src->tracks.size();
            src->tracks.push_back (std::move (t));
        }
    }

    // Playlist tree.
    std::map<uint32_t, std::vector<RawEntry>> entriesByPlaylist;
    for (auto& e : rawEntries)
        entriesByPlaylist[e.playlistId].push_back (e);

    std::function<void (PlaylistNode&, uint32_t, int)> build = [&] (PlaylistNode& node, uint32_t parentId, int depth)
    {
        if (depth > 32) return;
        std::vector<const RawPlaylist*> kids;
        for (auto& p : rawPlaylists)
            if (p.parent == parentId && p.id != parentId)
                kids.push_back (&p);
        std::sort (kids.begin(), kids.end(), [] (auto* a, auto* b2) { return a->sortOrder < b2->sortOrder; });

        for (auto* k : kids)
        {
            PlaylistNode child;
            child.id = juce::String (k->id);
            child.name = k->name;
            child.isFolder = k->isFolder;

            if (k->isFolder)
            {
                build (child, k->id, depth + 1);
            }
            else
            {
                auto& es = entriesByPlaylist[k->id];
                std::sort (es.begin(), es.end(), [] (auto& a, auto& b2) { return a.index < b2.index; });
                for (auto& e : es)
                {
                    auto it = trackIndexById.find (e.trackId);
                    if (it != trackIndexById.end())
                        child.trackIndices.push_back ((int) it->second);
                }
            }
            node.children.push_back (std::move (child));
        }
    };
    build (src->playlists, 0, 0);

    return src;
}

std::unique_ptr<LibrarySource> loadRekordboxUsbExport (const juce::File& usbRoot)
{
    auto pdbFile = usbRoot.getChildFile ("PIONEER/rekordbox/export.pdb");
    juce::MemoryBlock mb;
    if (! pdbFile.existsAsFile() || ! pdbFile.loadFileAsData (mb))
    {
        auto src = std::make_unique<LibrarySource>();
        src->kind = LibrarySource::Kind::usbExport;
        src->root = usbRoot;
        src->name = "USB " + usbRoot.getFullPathName();
        src->error = "No PIONEER/rekordbox/export.pdb found";
        return src;
    }

    auto src = parseExportPdb (mb, usbRoot);
    auto volume = usbRoot.getVolumeLabel();
    src->name = volume.isNotEmpty() ? volume + " (" + usbRoot.getFullPathName().trimCharactersAtEnd ("\\/") + ")"
                                    : "USB " + usbRoot.getFullPathName().trimCharactersAtEnd ("\\/");
    return src;
}

} // namespace orbit
