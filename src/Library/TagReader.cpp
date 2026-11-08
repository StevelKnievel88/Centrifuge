#include "TagReader.h"

namespace orbit
{

namespace
{
    using Bytes = std::vector<uint8_t>;

    uint32_t be32 (const uint8_t* p) { return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3]; }
    uint32_t le32 (const uint8_t* p) { return (uint32_t) p[3] << 24 | (uint32_t) p[2] << 16 | (uint32_t) p[1] << 8 | p[0]; }
    uint32_t syncsafe (const uint8_t* p) { return (uint32_t) (p[0] & 0x7f) << 21 | (uint32_t) (p[1] & 0x7f) << 14 | (uint32_t) (p[2] & 0x7f) << 7 | (p[3] & 0x7f); }

    bool readAt (juce::FileInputStream& in, juce::int64 pos, size_t n, Bytes& out)
    {
        out.resize (n);
        if (n == 0) return true;
        return in.setPosition (pos) && in.read (out.data(), (int) n) == (int) n;
    }

    juce::String latin1 (const uint8_t* p, size_t n)
    {
        juce::String s;
        s.preallocateBytes (n);
        for (size_t i = 0; i < n && p[i] != 0; ++i) s += (juce::juce_wchar) p[i];
        return s;
    }

    juce::String utf16 (const uint8_t* p, size_t n, bool bigEndian)
    {
        if (n >= 2 && p[0] == 0xff && p[1] == 0xfe) { bigEndian = false; p += 2; n -= 2; }
        else if (n >= 2 && p[0] == 0xfe && p[1] == 0xff) { bigEndian = true; p += 2; n -= 2; }
        juce::String s;
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            uint32_t c = bigEndian ? (uint32_t) (p[i] << 8 | p[i + 1]) : (uint32_t) (p[i + 1] << 8 | p[i]);
            if (c == 0) break;
            if (c >= 0xd800 && c < 0xdc00 && i + 3 < n)
            {
                const uint32_t lo = bigEndian ? (uint32_t) (p[i + 2] << 8 | p[i + 3]) : (uint32_t) (p[i + 3] << 8 | p[i + 2]);
                c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
                i += 2;
            }
            s += (juce::juce_wchar) c;
        }
        return s;
    }

    juce::String utf8 (const uint8_t* p, size_t n)
    {
        size_t len = 0;
        while (len < n && p[len] != 0) ++len;
        return juce::String::fromUTF8 ((const char*) p, (int) len);
    }

    /** An ID3v2 text value (first string only). */
    juce::String id3Text (const uint8_t* p, size_t n)
    {
        if (n < 1) return {};
        const int enc = p[0];
        ++p; --n;
        switch (enc)
        {
            case 1:  return utf16 (p, n, false).trim();
            case 2:  return utf16 (p, n, true).trim();
            case 3:  return utf8 (p, n).trim();
            default: return latin1 (p, n).trim();
        }
    }

    /** COMM: encoding, language, description\0, text. */
    juce::String id3Comment (const uint8_t* p, size_t n)
    {
        if (n < 5) return {};
        const int enc = p[0];
        size_t i = 4;
        const bool wide = enc == 1 || enc == 2;
        if (wide) { while (i + 1 < n && (p[i] != 0 || p[i + 1] != 0)) i += 2; i += 2; }
        else      { while (i < n && p[i] != 0) ++i; ++i; }
        if (i >= n) return {};
        Bytes b;
        b.push_back ((uint8_t) enc);
        b.insert (b.end(), p + i, p + n);
        return id3Text (b.data(), b.size());
    }

    juce::String cleanGenre (juce::String g)
    {
        // ID3 "(17)" / "(17)Rock" style numeric genres: keep any text, drop the number.
        while (g.startsWithChar ('(') && g.containsChar (')'))
        {
            const auto rest = g.fromFirstOccurrenceOf (")", false, false);
            if (rest.isEmpty()) return {};
            g = rest;
        }
        return g.trim();
    }

    void applyField (AudioTags& t, const juce::String& field, const juce::String& v)
    {
        if (v.isEmpty()) return;
        const auto f = field.toUpperCase();
        if (f == "TITLE")                                    { if (t.title.isEmpty()) t.title = v; }
        else if (f == "ARTIST")                              { if (t.artist.isEmpty()) t.artist = v; }
        else if (f == "ALBUM")                               { if (t.album.isEmpty()) t.album = v; }
        else if (f == "GENRE")                               { if (t.genre.isEmpty()) t.genre = cleanGenre (v); }
        else if (f == "COMMENT" || f == "DESCRIPTION")       { if (t.comment.isEmpty()) t.comment = v; }
        else if (f == "LABEL" || f == "ORGANIZATION" || f == "PUBLISHER") { if (t.label.isEmpty()) t.label = v; }
        else if (f == "KEY" || f == "INITIALKEY")            { if (t.key.isEmpty()) t.key = v; }
        else if (f == "BPM" || f == "TEMPO")                 { if (t.bpm <= 0.0) t.bpm = v.getDoubleValue(); }
    }

    //==============================================================================
    void parseId3v2 (const uint8_t* d, size_t n, AudioTags& t)
    {
        if (n < 10 || d[0] != 'I' || d[1] != 'D' || d[2] != '3') return;
        const int ver = d[3];
        const int flags = d[5];
        const size_t size = juce::jmin ((size_t) syncsafe (d + 6), n - 10);
        Bytes body (d + 10, d + 10 + size);
        if ((flags & 0x80) != 0 && ver < 4)
        {
            // whole-tag unsynchronisation: FF 00 -> FF
            Bytes u;
            for (size_t i = 0; i < body.size(); ++i)
            {
                u.push_back (body[i]);
                if (body[i] == 0xff && i + 1 < body.size() && body[i + 1] == 0) ++i;
            }
            body.swap (u);
        }
        size_t pos = 0;
        if ((flags & 0x40) != 0 && body.size() >= 4)
            pos = ver >= 4 ? syncsafe (body.data()) : be32 (body.data()) + 4;

        const bool v22 = ver == 2;
        const size_t hdr = v22 ? 6 : 10;
        while (pos + hdr <= body.size())
        {
            const uint8_t* f = body.data() + pos;
            if (f[0] == 0) break;
            juce::String id = latin1 (f, v22 ? 3 : 4);
            size_t fsize = v22 ? ((size_t) f[3] << 16 | (size_t) f[4] << 8 | f[5])
                               : (ver >= 4 ? syncsafe (f + 4) : be32 (f + 4));
            pos += hdr;
            if (fsize > body.size() - pos) break;
            const uint8_t* p = body.data() + pos;
            if (! v22 && ver >= 4 && (f[9] & 0x01) != 0 && fsize >= 4) { p += 4; fsize -= 4; }   // data length indicator

            static const std::pair<const char*, const char*> map[] = {
                { "TIT2", "TITLE" }, { "TT2", "TITLE" }, { "TPE1", "ARTIST" }, { "TP1", "ARTIST" },
                { "TALB", "ALBUM" }, { "TAL", "ALBUM" }, { "TCON", "GENRE" }, { "TCO", "GENRE" },
                { "TBPM", "BPM" }, { "TBP", "BPM" }, { "TKEY", "KEY" }, { "TKE", "KEY" },
                { "TPUB", "LABEL" }, { "TPB", "LABEL" } };
            for (auto& [fid, field] : map)
                if (id == fid) applyField (t, field, id3Text (p, fsize));
            if ((id == "COMM" || id == "COM") && t.comment.isEmpty())
                t.comment = id3Comment (p, fsize);
            pos += fsize;
        }
    }

    void parseId3v1 (const uint8_t* d, AudioTags& t)
    {
        if (d[0] != 'T' || d[1] != 'A' || d[2] != 'G') return;
        applyField (t, "TITLE", latin1 (d + 3, 30).trim());
        applyField (t, "ARTIST", latin1 (d + 33, 30).trim());
        applyField (t, "ALBUM", latin1 (d + 63, 30).trim());
    }

    //==============================================================================
    void parseVorbisComment (const uint8_t* p, size_t n, AudioTags& t)
    {
        if (n < 8) return;
        size_t pos = 4 + le32 (p);
        if (pos + 4 > n) return;
        const uint32_t count = le32 (p + pos);
        pos += 4;
        for (uint32_t i = 0; i < count && pos + 4 <= n; ++i)
        {
            const uint32_t len = le32 (p + pos);
            pos += 4;
            if (len > n - pos) break;
            const auto s = juce::String::fromUTF8 ((const char*) p + pos, (int) len);
            pos += len;
            applyField (t, s.upToFirstOccurrenceOf ("=", false, false), s.fromFirstOccurrenceOf ("=", false, false).trim());
        }
    }

    void readFlac (juce::FileInputStream& in, AudioTags& t)
    {
        juce::int64 pos = 4;
        Bytes h, b;
        for (int guard = 0; guard < 64; ++guard)
        {
            if (! readAt (in, pos, 4, h)) return;
            const bool last = (h[0] & 0x80) != 0;
            const int type = h[0] & 0x7f;
            const size_t len = (size_t) h[1] << 16 | (size_t) h[2] << 8 | h[3];
            if (type == 4 && len < (1u << 24))
            {
                if (readAt (in, pos + 4, len, b)) parseVorbisComment (b.data(), b.size(), t);
                return;
            }
            pos += 4 + (juce::int64) len;
            if (last) return;
        }
    }

    //==============================================================================
    /** Walks MP4 atoms between [start, end); calls fn (type, dataStart, dataEnd). */
    void mp4Atoms (juce::FileInputStream& in, juce::int64 start, juce::int64 end,
                   const std::function<void (const juce::String&, juce::int64, juce::int64)>& fn)
    {
        Bytes h;
        juce::int64 pos = start;
        for (int guard = 0; pos + 8 <= end && guard < 4096; ++guard)
        {
            if (! readAt (in, pos, 8, h)) return;
            juce::int64 size = be32 (h.data());
            const auto type = latin1 (h.data() + 4, 4);
            juce::int64 hdr = 8;
            if (size == 1)
            {
                Bytes x;
                if (! readAt (in, pos + 8, 8, x)) return;
                size = (juce::int64) be32 (x.data()) << 32 | be32 (x.data() + 4);
                hdr = 16;
            }
            else if (size == 0) size = end - pos;
            if (size < hdr || pos + size > end) return;
            fn (type, pos + hdr, pos + size);
            pos += size;
        }
    }

    void readMp4 (juce::FileInputStream& in, AudioTags& t)
    {
        const auto total = in.getTotalLength();
        mp4Atoms (in, 0, total, [&] (const juce::String& a, juce::int64 s, juce::int64 e)
        {
            if (a != "moov") return;
            mp4Atoms (in, s, e, [&] (const juce::String& b, juce::int64 s2, juce::int64 e2)
            {
                if (b != "udta") return;
                mp4Atoms (in, s2, e2, [&] (const juce::String& c, juce::int64 s3, juce::int64 e3)
                {
                    if (c != "meta") return;
                    mp4Atoms (in, s3 + 4, e3, [&] (const juce::String& d, juce::int64 s4, juce::int64 e4)   // meta is a full box
                    {
                        if (d != "ilst") return;
                        mp4Atoms (in, s4, e4, [&] (const juce::String& item, juce::int64 s5, juce::int64 e5)
                        {
                            mp4Atoms (in, s5, e5, [&] (const juce::String& data, juce::int64 s6, juce::int64 e6)
                            {
                                if (data != "data" || e6 - s6 < 8 || e6 - s6 > (1 << 20)) return;
                                Bytes v;
                                if (! readAt (in, s6 + 8, (size_t) (e6 - s6 - 8), v)) return;
                                const auto text = juce::String::fromUTF8 ((const char*) v.data(), (int) v.size()).trim();
                                const auto copyright = juce::String::charToString ((juce::juce_wchar) 0xa9);
                                if (item == copyright + "nam") applyField (t, "TITLE", text);
                                else if (item == copyright + "ART" || item == "aART") applyField (t, "ARTIST", text);
                                else if (item == copyright + "alb") applyField (t, "ALBUM", text);
                                else if (item == copyright + "gen") applyField (t, "GENRE", text);
                                else if (item == copyright + "cmt") applyField (t, "COMMENT", text);
                                else if (item == "tmpo" && v.size() >= 2) { if (t.bpm <= 0.0) t.bpm = (double) (v[0] << 8 | v[1]); }
                            });
                        });
                    });
                });
            });
        });
    }

    //==============================================================================
    /** RIFF (WAV, little-endian sizes) and IFF (AIFF, big-endian) chunks: INFO list and ID3 chunk. */
    void readChunks (juce::FileInputStream& in, AudioTags& t, bool riff)
    {
        const auto total = in.getTotalLength();
        juce::int64 pos = 12;
        Bytes h, b;
        for (int guard = 0; pos + 8 <= total && guard < 512; ++guard)
        {
            if (! readAt (in, pos, 8, h)) return;
            const auto id = latin1 (h.data(), 4);
            const juce::int64 len = riff ? le32 (h.data() + 4) : be32 (h.data() + 4);
            const auto body = pos + 8;
            if ((id == "id3 " || id == "ID3 " || id == "ID32") && len < (64 << 20))
            {
                if (readAt (in, body, (size_t) len, b)) parseId3v2 (b.data(), b.size(), t);
            }
            else if (riff && id == "LIST" && len >= 4 && len < (1 << 20) && readAt (in, body, (size_t) len, b) && latin1 (b.data(), 4) == "INFO")
            {
                size_t p = 4;
                while (p + 8 <= b.size())
                {
                    const auto sub = latin1 (b.data() + p, 4);
                    const size_t sl = le32 (b.data() + p + 4);
                    if (sl > b.size() - p - 8) break;
                    const auto v = juce::String::fromUTF8 ((const char*) b.data() + p + 8, (int) sl).trim();
                    if (sub == "INAM") applyField (t, "TITLE", v);
                    else if (sub == "IART") applyField (t, "ARTIST", v);
                    else if (sub == "IPRD") applyField (t, "ALBUM", v);
                    else if (sub == "IGNR") applyField (t, "GENRE", v);
                    else if (sub == "ICMT") applyField (t, "COMMENT", v);
                    p += 8 + sl + (sl & 1);
                }
            }
            else if (! riff && id == "NAME" && len < 4096 && readAt (in, body, (size_t) len, b))
                applyField (t, "TITLE", latin1 (b.data(), b.size()).trim());
            pos = body + len + (len & 1);
        }
    }
}

AudioTags AudioTags::read (const juce::File& file)
{
    AudioTags t;
    juce::FileInputStream in (file);
    if (in.openedOk())
    {
        Bytes h;
        if (readAt (in, 0, 12, h))
        {
            const auto magic = latin1 (h.data(), 4);
            if (magic.startsWith ("ID3"))   // "ID3" + its version byte
            {
                Bytes tag;
                const size_t size = syncsafe (h.data() + 6) + 10;
                if (size < (64u << 20) && readAt (in, 0, size, tag)) parseId3v2 (tag.data(), tag.size(), t);
            }
            else if (magic == "fLaC") readFlac (in, t);
            else if (magic == "RIFF" && latin1 (h.data() + 8, 4) == "WAVE") readChunks (in, t, true);
            else if (magic == "FORM") readChunks (in, t, false);
            else if (latin1 (h.data() + 4, 4) == "ftyp") readMp4 (in, t);

            // ID3v1 at the end of MP3s
            if ((t.title.isEmpty() || t.artist.isEmpty()) && in.getTotalLength() > 128)
            {
                Bytes v1;
                if (readAt (in, in.getTotalLength() - 128, 128, v1)) parseId3v1 (v1.data(), t);
            }
        }
    }

    if (t.title.isEmpty())
    {
        auto name = file.getFileNameWithoutExtension().replaceCharacter ('_', ' ').trim();
        if (name.contains (" - "))
        {
            if (t.artist.isEmpty()) t.artist = name.upToFirstOccurrenceOf (" - ", false, false).trim();
            name = name.fromFirstOccurrenceOf (" - ", false, false).trim();
        }
        t.title = name;
    }
    if (t.bpm < 20.0 || t.bpm > 400.0) t.bpm = 0.0;
    return t;
}

} // namespace orbit
