#include "TagWriter.h"

namespace orbit
{

namespace
{
    // ---------------------------------------------------------------- helpers
    juce::uint32 be32 (const uint8_t* p) { return ((juce::uint32) p[0] << 24) | ((juce::uint32) p[1] << 16) | ((juce::uint32) p[2] << 8) | p[3]; }
    juce::uint32 syncsafe (const uint8_t* p) { return ((juce::uint32) (p[0] & 0x7f) << 21) | ((juce::uint32) (p[1] & 0x7f) << 14) | ((juce::uint32) (p[2] & 0x7f) << 7) | (p[3] & 0x7f); }
    void putBe32 (juce::MemoryOutputStream& o, juce::uint32 v) { o.writeByte ((char) (v >> 24)); o.writeByte ((char) (v >> 16)); o.writeByte ((char) (v >> 8)); o.writeByte ((char) v); }
    void putSyncsafe (juce::MemoryOutputStream& o, juce::uint32 v) { o.writeByte ((char) ((v >> 21) & 0x7f)); o.writeByte ((char) ((v >> 14) & 0x7f)); o.writeByte ((char) ((v >> 7) & 0x7f)); o.writeByte ((char) (v & 0x7f)); }
    void putLe32 (juce::MemoryOutputStream& o, juce::uint32 v) { o.writeByte ((char) v); o.writeByte ((char) (v >> 8)); o.writeByte ((char) (v >> 16)); o.writeByte ((char) (v >> 24)); }
    juce::uint32 le32 (const uint8_t* p) { return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24); }

    /** Writes `head` followed by the original file's bytes from `keepFrom`, via a temporary file, then swaps it in. */
    bool rewrite (const juce::File& file, const juce::MemoryBlock& head, juce::int64 keepFrom, juce::String& error)
    {
        auto temp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".centrifuge-tags" + file.getFileExtension());
        temp.deleteFile();
        {
            juce::FileInputStream in (file);
            if (! in.openedOk()) { error = "couldn't read " + file.getFileName(); return false; }
            juce::FileOutputStream out (temp);
            if (! out.openedOk()) { error = "couldn't write next to " + file.getFileName() + " (read-only?)"; return false; }
            out.write (head.getData(), head.getSize());
            in.setPosition (keepFrom);
            if (out.writeFromInputStream (in, -1) < in.getTotalLength() - keepFrom) { out.flush(); temp.deleteFile(); error = "copying " + file.getFileName() + " failed"; return false; }
            out.flush();
            if (out.getStatus().failed()) { temp.deleteFile(); error = out.getStatus().getErrorMessage(); return false; }
        }
        if (! temp.replaceFileIn (file)) { temp.deleteFile(); error = "couldn't replace " + file.getFileName() + " (is it open in another program?)"; return false; }
        return true;
    }

    // ---------------------------------------------------------------- MP3: ID3v2
    bool writeId3 (const juce::File& file, const TagWriter::Fields& f, juce::String& error)
    {
        auto inp = std::make_unique<juce::FileInputStream> (file);
        auto& in = *inp;
        if (! in.openedOk()) { error = "couldn't read " + file.getFileName(); return false; }
        uint8_t hdr[10] = {};
        in.read (hdr, 10);
        int major = 3;
        juce::int64 oldEnd = 0;
        juce::MemoryBlock keptFrames;
        if (hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3')
        {
            major = hdr[3] >= 4 ? 4 : 3;
            const uint8_t flags = hdr[5];
            const juce::uint32 size = syncsafe (hdr + 6);
            oldEnd = 10 + (juce::int64) size + ((flags & 0x10) != 0 ? 10 : 0);
            if (hdr[3] < 3) { error = file.getFileName() + " has an old ID3v2." + juce::String ((int) hdr[3]) + " tag - not changed"; return false; }
            if ((flags & 0x80) != 0) { error = file.getFileName() + "'s tag uses unsynchronisation - not changed"; return false; }
            juce::MemoryBlock body;
            in.readIntoMemoryBlock (body, (juce::int64) size);
            auto* d = static_cast<const uint8_t*> (body.getData());
            size_t pos = 0;
            if ((flags & 0x40) != 0 && body.getSize() >= 4)   // extended header: skip it
                pos = (size_t) (major == 4 ? syncsafe (d) : be32 (d) + 4);
            // keep every frame we don't set
            const juce::StringArray ours { "TIT2", "TPE1", "TALB", "TCON", "TPUB", "COMM", "TKEY" };
            while (pos + 10 <= body.getSize())
            {
                const juce::String id (reinterpret_cast<const char*> (d + pos), 4);
                if (d[pos] == 0) break;   // padding
                const juce::uint32 fs = major == 4 ? syncsafe (d + pos + 4) : be32 (d + pos + 4);
                if (pos + 10 + fs > body.getSize()) break;
                if (! ours.contains (id)) keptFrames.append (d + pos, 10 + fs);
                pos += 10 + fs;
            }
        }
        inp.reset();   // closed before the file is replaced

        juce::MemoryOutputStream frames;
        frames.write (keptFrames.getData(), keptFrames.getSize());
        auto textFrame = [&] (const char* id, const juce::String& text, bool comment)
        {
            if (text.isEmpty()) return;
            juce::MemoryOutputStream body;
            if (major == 4)
            {
                body.writeByte (3);   // UTF-8
                if (comment) { body.write ("eng", 3); body.writeByte (0); }
                body.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
            }
            else
            {
                body.writeByte (1);   // UTF-16 with BOM
                auto utf16 = [&] (const juce::String& s)
                {
                    body.writeByte ((char) 0xff); body.writeByte ((char) 0xfe);
                    for (auto c = s.toUTF16(); *c != 0; ++c) { body.writeByte ((char) (*c & 0xff)); body.writeByte ((char) (*c >> 8)); }
                };
                if (comment) { body.write ("eng", 3); utf16 ({}); body.writeByte (0); body.writeByte (0); }
                utf16 (text);
            }
            frames.write (id, 4);
            if (major == 4) putSyncsafe (frames, (juce::uint32) body.getDataSize()); else putBe32 (frames, (juce::uint32) body.getDataSize());
            frames.writeByte (0); frames.writeByte (0);
            frames.write (body.getData(), body.getDataSize());
        };
        textFrame ("TIT2", f.title, false);
        textFrame ("TPE1", f.artist, false);
        textFrame ("TALB", f.album, false);
        textFrame ("TCON", f.genre, false);
        textFrame ("TPUB", f.label, false);
        textFrame ("COMM", f.comment, true);
        textFrame ("TKEY", f.key, false);

        const juce::uint32 padding = 1024;
        juce::MemoryOutputStream tag;
        tag.write ("ID3", 3);
        tag.writeByte ((char) major); tag.writeByte (0);
        tag.writeByte (0);   // no flags
        putSyncsafe (tag, (juce::uint32) frames.getDataSize() + padding);
        tag.write (frames.getData(), frames.getDataSize());
        for (juce::uint32 i = 0; i < padding; ++i) tag.writeByte (0);
        return rewrite (file, tag.getMemoryBlock(), oldEnd, error);
    }

    // ---------------------------------------------------------------- FLAC: Vorbis comments
    bool writeFlac (const juce::File& file, const TagWriter::Fields& f, juce::String& error)
    {
        auto inp = std::make_unique<juce::FileInputStream> (file);
        auto& in = *inp;
        if (! in.openedOk()) { error = "couldn't read " + file.getFileName(); return false; }
        char magic[4] = {};
        in.read (magic, 4);
        if (juce::String (magic, 4) != "fLaC") { error = file.getFileName() + " isn't a plain FLAC file"; return false; }
        struct Block { int type; juce::MemoryBlock data; };
        std::vector<Block> blocks;
        for (bool last = false; ! last;)
        {
            uint8_t h[4] = {};
            if (in.read (h, 4) != 4) { error = file.getFileName() + ": damaged metadata"; return false; }
            last = (h[0] & 0x80) != 0;
            const int type = h[0] & 0x7f;
            const int len = (h[1] << 16) | (h[2] << 8) | h[3];
            Block b { type, {} };
            if (len > 0 && in.readIntoMemoryBlock (b.data, len) != (size_t) len) { error = file.getFileName() + ": damaged metadata"; return false; }
            blocks.push_back (std::move (b));
        }
        const auto audioStart = in.getPosition();
        inp.reset();   // closed before the file is replaced

        // the comments: keep everything we don't set
        juce::String vendor = "Centrifuge";
        juce::StringArray entries;
        for (auto& b : blocks)
            if (b.type == 4)
            {
                auto* d = static_cast<const uint8_t*> (b.data.getData());
                const size_t n = b.data.getSize();
                size_t p = 0;
                if (n < 8) break;
                const auto vl = le32 (d); p = 4;
                if (p + vl > n) break;
                vendor = juce::String::fromUTF8 (reinterpret_cast<const char*> (d + p), (int) vl); p += vl;
                if (p + 4 > n) break;
                const auto count = le32 (d + p); p += 4;
                for (juce::uint32 i = 0; i < count && p + 4 <= n; ++i)
                {
                    const auto l = le32 (d + p); p += 4;
                    if (p + l > n) break;
                    entries.add (juce::String::fromUTF8 (reinterpret_cast<const char*> (d + p), (int) l)); p += l;
                }
            }
        auto set = [&] (const juce::StringArray& keys, const juce::String& value)
        {
            if (value.isEmpty()) return;
            for (int i = entries.size(); --i >= 0;)
                for (auto& k : keys)
                    if (entries[i].upToFirstOccurrenceOf ("=", false, false).equalsIgnoreCase (k)) { entries.remove (i); break; }
            entries.add (keys[0] + "=" + value);
        };
        set ({ "TITLE" }, f.title);
        set ({ "ARTIST" }, f.artist);
        set ({ "ALBUM" }, f.album);
        set ({ "GENRE" }, f.genre);
        set ({ "LABEL", "ORGANIZATION", "PUBLISHER" }, f.label);
        set ({ "COMMENT", "DESCRIPTION" }, f.comment);
        set ({ "INITIALKEY", "KEY" }, f.key);
        juce::MemoryOutputStream vc;
        putLe32 (vc, (juce::uint32) vendor.getNumBytesAsUTF8());
        vc.write (vendor.toRawUTF8(), vendor.getNumBytesAsUTF8());
        putLe32 (vc, (juce::uint32) entries.size());
        for (auto& e : entries) { putLe32 (vc, (juce::uint32) e.getNumBytesAsUTF8()); vc.write (e.toRawUTF8(), e.getNumBytesAsUTF8()); }

        bool placed = false;
        for (auto& b : blocks) if (b.type == 4) { b.data = vc.getMemoryBlock(); placed = true; break; }
        if (! placed) blocks.insert (blocks.begin() + 1, Block { 4, vc.getMemoryBlock() });   // after STREAMINFO

        juce::MemoryOutputStream head;
        head.write ("fLaC", 4);
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const auto len = (juce::uint32) blocks[i].data.getSize();
            if (len >= (1u << 24)) { error = file.getFileName() + ": a metadata block is too big"; return false; }
            head.writeByte ((char) ((i + 1 == blocks.size() ? 0x80 : 0) | blocks[i].type));
            head.writeByte ((char) (len >> 16)); head.writeByte ((char) (len >> 8)); head.writeByte ((char) len);
            head.write (blocks[i].data.getData(), len);
        }
        return rewrite (file, head.getMemoryBlock(), audioStart, error);
    }
}

bool TagWriter::canWrite (const juce::File& f)
{
    return f.hasFileExtension ("mp3;flac");
}

bool TagWriter::write (const juce::File& file, const Fields& fields, juce::String& error)
{
    if (! file.existsAsFile()) { error = file.getFileName() + " not found"; return false; }
    if (file.hasFileExtension ("mp3")) return writeId3 (file, fields, error);
    if (file.hasFileExtension ("flac")) return writeFlac (file, fields, error);
    error = file.getFileName() + ": tags can be written to MP3 and FLAC files";
    return false;
}

} // namespace orbit
