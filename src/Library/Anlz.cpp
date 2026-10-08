#include "Anlz.h"

namespace orbit
{
namespace
{
    // ANLZ files are big-endian.
    struct BE
    {
        const uint8_t* d;
        size_t n;

        bool has (size_t off, size_t len) const noexcept { return off + len <= n; }
        uint32_t u32 (size_t off) const noexcept
        {
            if (! has (off, 4)) return 0;
            return (uint32_t (d[off]) << 24) | (uint32_t (d[off + 1]) << 16) | (uint32_t (d[off + 2]) << 8) | uint32_t (d[off + 3]);
        }
        uint16_t u16 (size_t off) const noexcept
        {
            if (! has (off, 2)) return 0;
            return uint16_t ((d[off] << 8) | d[off + 1]);
        }
        uint8_t u8 (size_t off) const noexcept { return has (off, 1) ? d[off] : 0; }
        bool fourcc (size_t off, const char* cc) const noexcept
        {
            return has (off, 4) && std::memcmp (d + off, cc, 4) == 0;
        }
        juce::String utf16be (size_t off, size_t numBytes) const
        {
            if (! has (off, numBytes)) return {};
            juce::String s;
            for (size_t i = 0; i + 1 < numBytes; i += 2)
            {
                auto c = (juce::juce_wchar) ((d[off + i] << 8) | d[off + i + 1]);
                if (c == 0) break;
                s += juce::String::charToString (c);
            }
            return s;
        }
    };

    juce::Colour memoryCueColour (int colourId)
    {
        auto c = rekordboxTrackColour (colourId);
        return c.isTransparent() ? juce::Colour (0xffe8102a) : c;
    }

    const juce::Colour defaultHotCueColour (0xff28e214);

    void parsePco2 (const BE& b, size_t tagStart, size_t lenHeader, size_t lenTag, AnlzData& out)
    {
        const auto listType = b.u32 (tagStart + 12); // 0 memory, 1 hot
        size_t p = tagStart + lenHeader;
        const size_t end = tagStart + lenTag;

        while (p + 12 <= end && b.fourcc (p, "PCP2"))
        {
            const auto lenEntry = b.u32 (p + 8);
            if (lenEntry < 44 || p + lenEntry > end) break;

            CuePoint cue;
            const auto hot = b.u32 (p + 12);
            const auto type = b.u8 (p + 16);
            const auto timeMs = b.u32 (p + 20);
            const auto loopMs = b.u32 (p + 24);
            const auto colourId = b.u8 (p + 28);
            const auto lenComment = b.u32 (p + 40);

            cue.type = (listType == 1 || hot > 0) ? CuePoint::Type::hot : CuePoint::Type::memory;
            cue.hotIndex = hot > 0 ? (int) hot - 1 : -1;
            cue.startSec = timeMs / 1000.0;
            if (type == 2 && loopMs > timeMs)
                cue.endSec = loopMs / 1000.0;

            if (lenComment > 0 && lenComment < lenEntry)
                cue.comment = b.utf16be (p + 44, lenComment);

            const size_t colourOff = p + 44 + lenComment;
            if (cue.type == CuePoint::Type::hot)
            {
                if (colourOff + 4 <= p + lenEntry && b.u8 (colourOff) != 0)
                    cue.colour = juce::Colour (b.u8 (colourOff + 1), b.u8 (colourOff + 2), b.u8 (colourOff + 3));
                else
                    cue.colour = cue.isLoop() ? juce::Colour (0xffff8c00) : defaultHotCueColour;
            }
            else
            {
                cue.colour = memoryCueColour (colourId);
            }

            out.cues.push_back (cue);
            p += lenEntry;
        }
    }

    void parsePcob (const BE& b, size_t tagStart, size_t lenHeader, size_t lenTag, AnlzData& out)
    {
        const auto listType = b.u32 (tagStart + 12);
        size_t p = tagStart + lenHeader;
        const size_t end = tagStart + lenTag;

        while (p + 12 <= end && b.fourcc (p, "PCPT"))
        {
            const auto lenEntry = b.u32 (p + 8);
            if (lenEntry < 40 || p + lenEntry > end) break;

            CuePoint cue;
            const auto hot = b.u32 (p + 12);
            const auto type = b.u8 (p + 28);
            const auto timeMs = b.u32 (p + 32);
            const auto loopMs = b.u32 (p + 36);

            cue.type = (listType == 1 || hot > 0) ? CuePoint::Type::hot : CuePoint::Type::memory;
            cue.hotIndex = hot > 0 ? (int) hot - 1 : -1;
            cue.startSec = timeMs / 1000.0;
            if (type == 2 && loopMs > timeMs && loopMs != 0xffffffff)
                cue.endSec = loopMs / 1000.0;
            cue.colour = cue.type == CuePoint::Type::hot ? defaultHotCueColour : memoryCueColour (0);

            out.cues.push_back (cue);
            p += lenEntry;
        }
    }

    void copyEntries (const BE& b, size_t tagStart, size_t lenHeader, size_t lenTag,
                      size_t bytesPerEntryExpected, std::vector<uint8_t>& dest)
    {
        const auto bytesPerEntry = b.u32 (tagStart + 12);
        const auto numEntries = b.u32 (tagStart + 16);
        if (bytesPerEntry != bytesPerEntryExpected) return;

        const size_t start = tagStart + lenHeader;
        size_t bytes = (size_t) bytesPerEntry * numEntries;
        bytes = std::min (bytes, (tagStart + lenTag > start) ? tagStart + lenTag - start : 0);
        if (! b.has (start, bytes)) return;
        dest.assign (b.d + start, b.d + start + bytes);
    }
}

bool parseAnlzFile (const juce::MemoryBlock& data, AnlzData& out)
{
    BE b { static_cast<const uint8_t*> (data.getData()), data.getSize() };
    if (! b.fourcc (0, "PMAI")) return false;

    const auto fileHeader = b.u32 (4);
    size_t p = fileHeader;

    std::vector<CuePoint> extendedCues, legacyCues;
    AnlzData scratch;

    while (p + 12 <= b.n)
    {
        const auto lenHeader = b.u32 (p + 4);
        const auto lenTag = b.u32 (p + 8);
        if (lenTag < 12 || lenHeader < 12 || p + lenTag > b.n) break;

        if (b.fourcc (p, "PQTZ"))
        {
            const auto num = b.u32 (p + 20);
            size_t q = p + lenHeader;
            std::vector<Beat> beats;
            beats.reserve (num);
            for (uint32_t i = 0; i < num && q + 8 <= p + lenTag; ++i, q += 8)
            {
                Beat beat;
                beat.beatInBar = juce::jlimit (1, 4, (int) b.u16 (q));
                beat.bpm = b.u16 (q + 2) / 100.0f;
                beat.timeSec = b.u32 (q + 4) / 1000.0;
                beats.push_back (beat);
            }
            if (! beats.empty())
                out.beats = std::move (beats);
        }
        else if (b.fourcc (p, "PCO2"))
        {
            scratch.cues.clear();
            parsePco2 (b, p, lenHeader, lenTag, scratch);
            extendedCues.insert (extendedCues.end(), scratch.cues.begin(), scratch.cues.end());
        }
        else if (b.fourcc (p, "PCOB"))
        {
            scratch.cues.clear();
            parsePcob (b, p, lenHeader, lenTag, scratch);
            legacyCues.insert (legacyCues.end(), scratch.cues.begin(), scratch.cues.end());
        }
        else if (b.fourcc (p, "PWV7"))
        {
            copyEntries (b, p, lenHeader, lenTag, 3, out.waveforms.detail3Band);
        }
        else if (b.fourcc (p, "PWV5"))
        {
            copyEntries (b, p, lenHeader, lenTag, 2, out.waveforms.detailRgb);
        }
        else if (b.fourcc (p, "PWV3"))
        {
            copyEntries (b, p, lenHeader, lenTag, 1, out.waveforms.detailMono);
        }
        else if (b.fourcc (p, "PPTH"))
        {
            const auto lenPath = b.u32 (p + 12);
            out.pathTag = b.utf16be (p + lenHeader, lenPath);
        }

        p += lenTag;
    }

    // Extended (nxs2) cue lists carry everything the legacy ones do plus colours and comments.
    if (! extendedCues.empty())
        out.cues = std::move (extendedCues);
    else if (! legacyCues.empty() && out.cues.empty())
        out.cues = std::move (legacyCues);

    out.foundAny = true;
    return true;
}

AnlzData loadAnlz (const juce::File& datFile)
{
    AnlzData result;
    if (datFile == juce::File()) return result;

    // Parse .DAT first, then the extended files which supersede it.
    for (auto ext : { ".DAT", ".EXT", ".2EX" })
    {
        auto f = datFile.withFileExtension (ext);
        if (! f.existsAsFile())
            f = datFile.withFileExtension (juce::String (ext).toLowerCase());
        if (! f.existsAsFile()) continue;

        juce::MemoryBlock mb;
        if (f.loadFileAsData (mb))
            parseAnlzFile (mb, result);
    }

    std::sort (result.cues.begin(), result.cues.end(),
               [] (const CuePoint& a, const CuePoint& b) { return a.startSec < b.startSec; });
    return result;
}

} // namespace orbit
