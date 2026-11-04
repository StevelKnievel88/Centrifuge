#include "AnlzWriter.h"
#include "../Library/RekordboxColours.h"

namespace orbit
{

namespace
{
    struct BE
    {
        juce::MemoryOutputStream os;
        void u8 (uint32_t v)  { os.writeByte ((char) (uint8_t) v); }
        void u16 (uint32_t v) { u8 (v >> 8); u8 (v); }
        void u32 (uint32_t v) { u16 (v >> 16); u16 (v & 0xffff); }
        void cc (const char* f) { os.write (f, 4); }
        void zeros (size_t n) { for (size_t i = 0; i < n; ++i) u8 (0); }
        void utf16 (const juce::String& s, bool terminator)
        {
            for (auto c : s) u16 ((uint32_t) (c < 0x10000 ? c : '?'));
            if (terminator) u16 (0);
        }
        juce::MemoryBlock block() { return os.getMemoryBlock(); }
    };

    /** Wraps a body into a tag: fourcc, header length, tag length, then the header fields and body. */
    juce::MemoryBlock tag (const char* fourcc, const juce::MemoryBlock& headerFields, const juce::MemoryBlock& body)
    {
        BE t;
        const uint32_t lenHeader = 12 + (uint32_t) headerFields.getSize();
        t.cc (fourcc);
        t.u32 (lenHeader);
        t.u32 (lenHeader + (uint32_t) body.getSize());
        t.os.write (headerFields.getData(), headerFields.getSize());
        t.os.write (body.getData(), body.getSize());
        return t.block();
    }

    uint32_t ms (double sec) { return (uint32_t) juce::jmax (0LL, (long long) std::llround (sec * 1000.0)); }

    struct RawTag { juce::String fourcc; juce::MemoryBlock data; };

    std::vector<RawTag> readTags (const juce::File& f)
    {
        std::vector<RawTag> tags;
        juce::MemoryBlock mb;
        if (! f.existsAsFile() || ! f.loadFileAsData (mb)) return tags;
        auto* d = static_cast<const uint8_t*> (mb.getData());
        const size_t n = mb.getSize();
        auto u32 = [&] (size_t o) { return o + 4 <= n ? (uint32_t (d[o]) << 24) | (uint32_t (d[o + 1]) << 16) | (uint32_t (d[o + 2]) << 8) | d[o + 3] : 0u; };
        if (n < 12 || std::memcmp (d, "PMAI", 4) != 0) return tags;
        size_t p = u32 (4);
        while (p + 12 <= n)
        {
            const auto len = u32 (p + 8);
            if (len < 12 || p + len > n) break;
            tags.push_back ({ juce::String (juce::CharPointer_ASCII ((const char*) d + p), 4), juce::MemoryBlock (d + p, len) });
            p += len;
        }
        return tags;
    }

    bool writeFile (const juce::File& f, const std::vector<juce::MemoryBlock>& tags)
    {
        size_t total = 28;
        for (auto& t : tags) total += t.getSize();
        BE h;
        h.cc ("PMAI");
        h.u32 (28);
        h.u32 ((uint32_t) total);
        h.u32 (1); h.u32 (0x10000); h.u32 (0x10000); h.u32 (0);
        juce::FileOutputStream os (f);
        if (! os.openedOk()) return false;
        os.setPosition (0);
        os.truncate();
        const auto hb = h.block();
        os.write (hb.getData(), hb.getSize());
        for (auto& t : tags) os.write (t.getData(), t.getSize());
        os.flush();
        return ! os.getStatus().failed();
    }

    // --- Generated waveforms (only for tracks rekordbox never analysed) ------------------
    struct Col { float height, whiteness, r, g, b; };

    Col column (const WaveformData& w, int i0, int i1)
    {
        float lo = 0, mi = 0, hi = 0;
        for (int i = i0; i < i1 && i < w.size(); ++i)
        {
            lo = juce::jmax (lo, w.low[(size_t) i] / 255.0f);
            mi = juce::jmax (mi, w.mid[(size_t) i] / 255.0f);
            hi = juce::jmax (hi, w.high[(size_t) i] / 255.0f);
        }
        const float top = juce::jmax (lo, mi, hi);
        const float k = top > 0 ? 1.0f / top : 0.0f;
        return { top, top > 0 ? hi / top : 0.0f, lo * k, mi * k, hi * k };
    }

    juce::MemoryBlock monoEntries (const WaveformData& w, int count, double entriesPerOut)
    {
        BE b;
        for (int i = 0; i < count; ++i)
        {
            const auto c = column (w, (int) (i * entriesPerOut), juce::jmax ((int) (i * entriesPerOut) + 1, (int) ((i + 1) * entriesPerOut)));
            b.u8 ((uint32_t) juce::jlimit (0, 31, (int) std::lround (c.height * 31.0f))
                  | ((uint32_t) juce::jlimit (0, 7, (int) std::lround (c.whiteness * 7.0f)) << 5));
        }
        return b.block();
    }

    std::vector<juce::MemoryBlock> generatedWaveTags (const WaveformData& w, bool ext)
    {
        std::vector<juce::MemoryBlock> tags;
        const int n = w.size();
        if (n <= 0) return tags;
        if (! ext)
        {
            BE h1; h1.u32 (400); h1.u32 (0x10000);
            tags.push_back (tag ("PWAV", h1.block(), monoEntries (w, 400, n / 400.0)));
            BE h2; h2.u32 (100); h2.u32 (0x10000);
            auto tiny = monoEntries (w, 100, n / 100.0);
            for (size_t i = 0; i < tiny.getSize(); ++i) tiny[i] = (char) ((uint8_t) tiny[i] & 0x1f) / 2;
            tags.push_back (tag ("PWV2", h2.block(), tiny));
        }
        else
        {
            BE h3; h3.u32 (1); h3.u32 ((uint32_t) n); h3.u32 (0x00960000);
            tags.push_back (tag ("PWV3", h3.block(), monoEntries (w, n, 1.0)));
            BE h5; h5.u32 (2); h5.u32 ((uint32_t) n); h5.u32 (0x00960305);
            BE b5;
            for (int i = 0; i < n; ++i)
            {
                const auto c = column (w, i, i + 1);
                const uint32_t r = (uint32_t) juce::jlimit (0, 7, (int) std::lround (c.r * 7)), g = (uint32_t) juce::jlimit (0, 7, (int) std::lround (c.g * 7)),
                               bl = (uint32_t) juce::jlimit (0, 7, (int) std::lround (c.b * 7)), ht = (uint32_t) juce::jlimit (0, 31, (int) std::lround (c.height * 31));
                b5.u16 ((r << 13) | (g << 10) | (bl << 7) | (ht << 2));
            }
            tags.push_back (tag ("PWV5", h5.block(), b5.block()));
        }
        return tags;
    }
}

juce::MemoryBlock AnlzWriter::pathTag (const juce::String& path)
{
    BE body;
    body.utf16 (path, true);
    BE h;
    h.u32 ((uint32_t) body.os.getDataSize());
    return tag ("PPTH", h.block(), body.block());
}

juce::MemoryBlock AnlzWriter::beatGridTag (const std::vector<Beat>& beats)
{
    BE h;
    h.u32 (0); h.u32 (0x80000); h.u32 ((uint32_t) beats.size());
    BE body;
    for (auto& b : beats)
    {
        body.u16 ((uint32_t) juce::jlimit (1, 4, b.beatInBar));
        body.u16 ((uint32_t) juce::jlimit (0, 65535, (int) std::lround (b.bpm * 100.0)));
        body.u32 (ms (b.timeSec));
    }
    return tag ("PQTZ", h.block(), body.block());
}

namespace
{
    std::vector<CuePoint> cuesOf (const TrackVersion& v, bool hot)
    {
        std::vector<CuePoint> out;
        if (hot) { for (auto& h : v.hot) if (h) out.push_back (*h); }
        else out = v.memory;
        return out;
    }
}

juce::MemoryBlock AnlzWriter::cueListTag (const TrackVersion& v, bool hot)
{
    const auto cues = cuesOf (v, hot);
    BE body;
    for (size_t i = 0; i < cues.size(); ++i)
    {
        const auto& c = cues[i];
        body.cc ("PCPT");
        body.u32 (0x1c);
        body.u32 (0x38);
        body.u32 (hot ? (uint32_t) c.hotIndex + 1 : 0);
        body.u32 (4);                                           // enabled
        body.u32 (0x10000);
        body.u16 (i == 0 ? 0xffff : (uint32_t) i - 1);          // order: previous
        body.u16 (i + 1 == cues.size() ? 0xffff : (uint32_t) i + 1);   // order: next
        body.u8 (c.isLoop() ? 2 : 1);
        body.u8 (0);
        body.u16 (1000);
        body.u32 (ms (c.startSec));
        body.u32 (c.isLoop() ? ms (c.endSec) : 0xffffffff);
        body.zeros (16);
    }
    BE h;
    h.u32 (hot ? 1 : 0); h.u16 (0); h.u16 ((uint32_t) cues.size()); h.u32 (0xffffffff);
    return tag ("PCOB", h.block(), body.block());
}

juce::MemoryBlock AnlzWriter::extendedCueListTag (const TrackVersion& v, bool hot)
{
    const auto cues = cuesOf (v, hot);
    BE body;
    for (auto& c : cues)
    {
        const uint32_t lenComment = c.comment.isNotEmpty() ? (uint32_t) (c.comment.length() + 1) * 2 : 0;
        const uint32_t lenEntry = (48 + lenComment + 3) / 4 * 4 + 8;
        body.cc ("PCP2");
        body.u32 (0x10);
        body.u32 (lenEntry);
        body.u32 (hot ? (uint32_t) c.hotIndex + 1 : 0);
        body.u8 (c.isLoop() ? 2 : 1);
        body.zeros (3);
        body.u32 (ms (c.startSec));
        body.u32 (c.isLoop() ? ms (c.endSec) : 0xffffffff);
        body.u8 (hot ? 0 : (uint32_t) nearestRekordboxMemoryColourId (c.colour));
        body.zeros (7);
        body.u16 (0); body.u16 (0);     // loop size as a fraction (unused)
        body.u32 (lenComment);
        if (lenComment > 0) body.utf16 (c.comment, true);
        if (hot && ! c.colour.isTransparent())
        {
            const int code = nearestRekordboxHotCueCode (c.colour);
            const auto col = rekordboxHotCueColour (code);
            body.u8 ((uint32_t) code); body.u8 (col.getRed()); body.u8 (col.getGreen()); body.u8 (col.getBlue());
        }
        else body.zeros (4);
        body.zeros (lenEntry - 48 - lenComment);
    }
    BE h;
    h.u32 (hot ? 1 : 0); h.u16 ((uint32_t) cues.size()); h.u16 (0);
    return tag ("PCO2", h.block(), body.block());
}

bool AnlzWriter::write (const juce::File& dat, const Input& in, juce::String& error)
{
    dat.getParentDirectory().createDirectory();
    const auto& v = in.version;

    auto rebuild = [&] (const juce::File& original, bool isDat, bool isExt, std::vector<juce::MemoryBlock> fallback)
    {
        auto tags = readTags (original);
        if (tags.empty()) return fallback;
        std::vector<juce::MemoryBlock> out;
        bool cuesDone = false, extCuesDone = false, gridDone = false;
        for (auto& t : tags)
        {
            if (t.fourcc == "PPTH") out.push_back (pathTag (in.usbPath));
            else if (t.fourcc == "PQTZ") { out.push_back (beatGridTag (v.beats)); gridDone = true; }
            else if (t.fourcc == "PQT2") continue;   // extended grid: would disagree with an edited grid
            else if (t.fourcc == "PCOB")
            {
                if (! cuesDone) { out.push_back (cueListTag (v, true)); out.push_back (cueListTag (v, false)); cuesDone = true; }
            }
            else if (t.fourcc == "PCO2")
            {
                if (! extCuesDone) { out.push_back (extendedCueListTag (v, true)); out.push_back (extendedCueListTag (v, false)); extCuesDone = true; }
            }
            else out.push_back (t.data);
        }
        if (isDat && ! gridDone) out.push_back (beatGridTag (v.beats));
        if ((isDat || isExt) && ! cuesDone) { out.push_back (cueListTag (v, true)); out.push_back (cueListTag (v, false)); }
        if (isExt && ! extCuesDone) { out.push_back (extendedCueListTag (v, true)); out.push_back (extendedCueListTag (v, false)); }
        return out;
    };

    const auto orig = in.originalDat;
    auto sibling = [&] (const juce::File& base, const char* ext)
    {
        auto f = base.withFileExtension (ext);
        if (! f.existsAsFile()) f = base.withFileExtension (juce::String (ext).toLowerCase());
        return f;
    };

    // .DAT
    std::vector<juce::MemoryBlock> datTags { pathTag (in.usbPath), beatGridTag (v.beats) };
    if (in.generatedWave != nullptr)
        for (auto& t : generatedWaveTags (*in.generatedWave, false)) datTags.push_back (t);
    datTags.push_back (cueListTag (v, true));
    datTags.push_back (cueListTag (v, false));
    if (orig != juce::File()) datTags = rebuild (sibling (orig, ".DAT"), true, false, datTags);
    if (! writeFile (dat, datTags)) { error = "Couldn't write " + dat.getFullPathName(); return false; }

    // .EXT
    std::vector<juce::MemoryBlock> extTags { pathTag (in.usbPath) };
    if (in.generatedWave != nullptr)
        for (auto& t : generatedWaveTags (*in.generatedWave, true)) extTags.push_back (t);
    for (auto t : { cueListTag (v, true), cueListTag (v, false), extendedCueListTag (v, true), extendedCueListTag (v, false) })
        extTags.push_back (t);
    if (orig != juce::File()) extTags = rebuild (sibling (orig, ".EXT"), false, true, extTags);
    if (! writeFile (dat.withFileExtension (".EXT"), extTags)) { error = "Couldn't write the .EXT file"; return false; }

    // .2EX (3-band waveforms): only copied (with the new path) when rekordbox made one
    if (orig != juce::File() && sibling (orig, ".2EX").existsAsFile())
    {
        auto twoEx = rebuild (sibling (orig, ".2EX"), false, false, {});
        if (! twoEx.empty() && ! writeFile (dat.withFileExtension (".2EX"), twoEx)) { error = "Couldn't write the .2EX file"; return false; }
    }
    return true;
}

} // namespace orbit
