#include "XzStems.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <array>
#include <cmath>
#include <cstdio>

namespace orbit
{
namespace XzStems
{

namespace
{
    // The deck's hash: FNV-1a 64 with its own offset basis, over the file length and frame count
    // (little-endian u64s), the first 64 KiB and, for longer files, the last 64 KiB.
    constexpr uint64_t fnvOffset = 1469598103934665603ull;
    constexpr uint64_t fnvPrime = 1099511628211ull;
    constexpr int64_t window = 65536;

    uint64_t hashBytes (uint64_t h, const unsigned char* p, size_t n)
    {
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= fnvPrime; }
        return h;
    }

    uint64_t hashU64 (uint64_t h, uint64_t v)
    {
        unsigned char b[8];
        for (int i = 0; i < 8; ++i) b[i] = (unsigned char) (v >> (i * 8));
        return hashBytes (h, b, 8);
    }

    /** Writes one stereo 16-bit FLAC. Samples are already scaled by `gain` (the deck divides it back out). */
    bool writeFlac (const juce::File& file, const int16_t* left, const int16_t* right, int64_t frames, juce::String& error)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
        {
            error = "Couldn't write " + file.getFullPathName();
            return false;
        }
        juce::FlacAudioFormat flac;
        auto writer = flac.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                        .withSampleRate (44100.0)
                                                        .withNumChannels (2)
                                                        .withBitsPerSample (16));
        if (writer == nullptr) { error = "Couldn't create a FLAC writer"; return false; }

        constexpr int block = 1 << 14;
        std::vector<int> l ((size_t) block), r ((size_t) block);
        for (int64_t pos = 0; pos < frames; pos += block)
        {
            const int n = (int) std::min<int64_t> (block, frames - pos);
            for (int i = 0; i < n; ++i)
            {
                // JUCE's int path is full-scale 32-bit; the FLAC writer shifts back down to 16 bits.
                l[(size_t) i] = (int) left[pos + i] * 65536;
                r[(size_t) i] = (int) right[pos + i] * 65536;
            }
            const int* chans[] = { l.data(), r.data(), nullptr };
            if (! writer->write (chans, n)) { error = "Couldn't write " + file.getFullPathName(); return false; }
        }
        writer.reset();
        return file.getSize() > 0;
    }

    juce::String gainText (float g)
    {
        char s[32];
        std::snprintf (s, sizeof (s), "%.9g", (double) g);
        return s;
    }
}

bool isSupportedFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;flac");
}

juce::String trackKey (const juce::File& file, int64_t frames)
{
    if (frames <= 0) return {};
    juce::FileInputStream in (file);
    if (! in.openedOk()) return {};
    const int64_t length = in.getTotalLength();
    if (length <= 0) return {};

    uint64_t h = hashU64 (hashU64 (fnvOffset, (uint64_t) length), (uint64_t) frames);
    std::vector<unsigned char> buf ((size_t) window);
    const int first = (int) std::min (length, window);
    if (in.read (buf.data(), first) != first) return {};
    h = hashBytes (h, buf.data(), (size_t) first);
    if (length > window)
    {
        if (! in.setPosition (length - window) || in.read (buf.data(), (int) window) != (int) window) return {};
        h = hashBytes (h, buf.data(), (size_t) window);
    }
    return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16);
}

Result write (const juce::File& stickRoot, const juce::File& track, const TrackAudio& audio,
              const StemAudio& stems, juce::String& reason)
{
    if (! isSupportedFile (track)) { reason = "not WAV/FLAC"; return Result::skipped; }
    if (std::abs (audio.sampleRate - 44100.0) > 0.5) { reason = "not 44.1 kHz"; return Result::skipped; }
    if (audio.numFrames > maxFrames) { reason = "longer than 6:20"; return Result::skipped; }
    if (stems.numFrames != audio.numFrames || ! stems.isComplete()) { reason = "stems incomplete"; return Result::skipped; }

    const auto key = trackKey (track, audio.numFrames);
    if (key.length() != 16) { reason = "couldn't read " + track.getFileName(); return Result::failed; }

    const auto n = (size_t) audio.numFrames;
    // Stem 2 = Inst (harmonics), stem 3 = Vocal. Scale each so its loudest sample fits 16 bits.
    struct Part { const char* name; int stem; float gain = 1.0f; std::vector<int16_t> l {}, r {}; };
    Part parts[] = { { "harmonics", 2, 1.0f, {}, {} }, { "vocals", 3, 1.0f, {}, {} } };
    for (auto& p : parts)
    {
        const auto& sl = stems.channels[(size_t) (p.stem * 2)];
        const auto& sr = stems.channels[(size_t) (p.stem * 2 + 1)];
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i)
            peak = std::max (peak, std::max (std::abs ((float) sl[i]), std::abs ((float) sr[i])) * StemAudio::toFloat);
        p.gain = peak > 0.999f ? 0.999f / peak : 1.0f;
        const float k = p.gain * StemAudio::toFloat * 32767.0f;
        p.l.resize (n);
        p.r.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            p.l[i] = (int16_t) juce::jlimit (-32767, 32767, (int) std::lrint ((float) sl[i] * k));
            p.r[i] = (int16_t) juce::jlimit (-32767, 32767, (int) std::lrint ((float) sr[i] * k));
        }
    }

    auto parent = stickRoot.getChildFile ("mods").getChildFile ("stemd-cache").getChildFile (separationId)
                           .getChildFile (key.substring (0, 2));
    if (! parent.createDirectory()) { reason = "couldn't create " + parent.getFullPathName(); return Result::failed; }

    // Build in a temporary folder, then swap it in so the deck never sees half an entry.
    auto tmp = parent.getChildFile (".tmp-" + key);
    tmp.deleteRecursively();
    if (! tmp.createDirectory()) { reason = "couldn't create " + tmp.getFullPathName(); return Result::failed; }
    for (auto& p : parts)
        if (! writeFlac (tmp.getChildFile (juce::String (p.name) + ".flac"), p.l.data(), p.r.data(), audio.numFrames, reason))
        {
            tmp.deleteRecursively();
            return Result::failed;
        }
    juce::String meta;
    meta << "v=1\nframes=" << juce::String ((juce::int64) audio.numFrames)
         << "\nharmonics=" << gainText (parts[0].gain) << "\nvocals=" << gainText (parts[1].gain) << "\n";
    if (! tmp.getChildFile ("meta").replaceWithData (meta.toRawUTF8(), meta.getNumBytesAsUTF8()))
    {
        tmp.deleteRecursively();
        reason = "couldn't write meta";
        return Result::failed;
    }

    auto target = parent.getChildFile (key);
    target.deleteRecursively();
    if (! tmp.moveFileTo (target)) { tmp.deleteRecursively(); reason = "couldn't move stems into place"; return Result::failed; }

    // Tell the loader which separation to use for this track.
    auto choiceDir = stickRoot.getChildFile ("mods").getChildFile ("xz-mods").getChildFile ("cache-choice");
    choiceDir.createDirectory();
    const juce::String choice = juce::String (separationId) + "\n";
    if (! choiceDir.getChildFile (key + ".txt").replaceWithData (choice.toRawUTF8(), choice.getNumBytesAsUTF8()))
    {
        reason = "couldn't write the cache choice";
        return Result::failed;
    }
    return Result::written;
}

bool fitsShortLayout (const juce::File& track, const TrackAudio& audio)
{
    return isSupportedFile (track) && std::abs (audio.sampleRate - 44100.0) <= 0.5 && audio.numFrames <= maxFrames;
}

//==============================================================================
// The streamed ("paged") layout.
namespace
{
    constexpr int pagedRate = 96000;
    constexpr int pageBytes = 131072;
    constexpr int pageFrames = pageBytes / 4;
    constexpr int waveEntriesPerSecond = 150;

    struct Role { const char* name; unsigned mask; };
    // bit 0 = Drums + Bass, bit 1 = Inst, bit 2 = Vocal
    const Role roles[] = { { "drums", 1 }, { "harmonics", 2 }, { "vocal", 4 }, { "full-mix", 7 },
                           { "instrumental", 3 }, { "vocals-drums", 5 }, { "vocals-harmonics", 6 } };
    constexpr int numRoles = 7;

    /** One role's .pgz file: a header and page table, then zlib pages of little-endian stereo 16-bit. */
    struct PageFile
    {
        juce::File file;
        std::unique_ptr<juce::FileOutputStream> out;
        juce::MemoryOutputStream table;
        juce::int64 offset = 0;
        juce::int64 rawBytes = 0;
        int pages = 0;

        bool open (const juce::File& f, int pageCount)
        {
            file = f;
            file.deleteFile();
            out = std::make_unique<juce::FileOutputStream> (file);
            if (! out->openedOk()) return false;
            offset = 24 + (juce::int64) pageCount * 48;
            return out->writeRepeatedByte (0, (size_t) offset);   // room for the header and table, filled in at the end
        }

        bool addPage (const void* raw, int bytes)
        {
            juce::MemoryOutputStream packed;
            {
                juce::GZIPCompressorOutputStream z (packed, 6);   // zlib format
                z.write (raw, (size_t) bytes);
            }
            const juce::SHA256 sum (raw, (size_t) bytes);
            table.writeInt64BigEndian (offset);
            table.writeIntBigEndian ((int) packed.getDataSize());
            table.writeIntBigEndian (bytes);
            table.write (sum.getRawData().getData(), 32);
            offset += (juce::int64) packed.getDataSize();
            rawBytes += bytes;
            ++pages;
            return out->write (packed.getData(), packed.getDataSize());
        }

        /** Writes the header and table; returns their SHA-256 (hex), or empty on error. */
        juce::String finish()
        {
            juce::MemoryOutputStream head;
            head.write ("OVPGZ001", 8);
            head.writeIntBigEndian (pageBytes);
            head.writeIntBigEndian (pages);
            head.writeInt64BigEndian (rawBytes);
            head.write (table.getData(), table.getDataSize());
            out->flush();
            if (! out->setPosition (0) || ! out->write (head.getData(), head.getDataSize())) return {};
            out->flush();
            const bool ok = out->getStatus().wasOk();
            out.reset();
            return ok ? juce::SHA256 (head.getData(), head.getDataSize()).toHexString() : juce::String();
        }
    };

    bool writeWaveform (const juce::File& f, const std::vector<unsigned char>& heights)
    {
        juce::MemoryOutputStream o;
        o.write ("PMAI", 4);
        o.writeIntBigEndian (12);
        o.writeIntBigEndian (12 + 24 + (int) heights.size());
        o.write ("PWV3", 4);
        o.writeIntBigEndian (24);
        o.writeIntBigEndian (24 + (int) heights.size());
        o.writeIntBigEndian (1);
        o.writeIntBigEndian ((int) heights.size());
        o.writeIntBigEndian (0);
        o.write (heights.data(), heights.size());
        return f.replaceWithData (o.getData(), o.getDataSize());
    }
}

Result writePaged (const juce::File& stickRoot, const juce::File& track, const TrackAudio& audio,
                   const StemAudio& stems, juce::String& reason)
{
    if (stems.numFrames != audio.numFrames || ! stems.isComplete()) { reason = "stems incomplete"; return Result::skipped; }
    if (audio.sampleRate < 8000.0 || audio.numFrames <= 0) { reason = "empty track"; return Result::skipped; }
    const auto contents = stickRoot.getChildFile ("Contents");
    if (! track.isAChildOf (contents)) { reason = "not under Contents on the stick"; return Result::skipped; }
    const juce::String filePath = "/Contents/" + track.getRelativePathFrom (contents).replaceCharacter ('\\', '/');

    const double ratio = audio.sampleRate / (double) pagedRate;
    const auto outFrames = (juce::int64) std::floor ((double) audio.numFrames / ratio);
    if (outFrames < 1 || outFrames > 500000000) { reason = "too long"; return Result::skipped; }
    const int pageCount = (int) ((outFrames + pageFrames - 1) / pageFrames);

    const juce::String sourceSha = juce::SHA256 (track).toHexString();
    if (sourceSha.length() != 64) { reason = "couldn't read " + track.getFileName(); return Result::failed; }
    const juce::String bundle = sourceSha.substring (0, 16);

    // The three faders at the source rate: 0 = Drums + Bass, 1 = Inst, 2 = Vocal.
    const auto n = (size_t) audio.numFrames;
    auto base = [&stems] (int fader, int ch, size_t i) -> float
    {
        if (fader == 0) return ((float) stems.channels[(size_t) ch][i] + (float) stems.channels[(size_t) (2 + ch)][i]) * StemAudio::toFloat;
        return (float) stems.channels[(size_t) ((fader + 1) * 2 + ch)][i] * StemAudio::toFloat;
    };

    // Each file is scaled so its loudest sample fits 16 bits (with room for the resampler); the deck divides it back out.
    std::array<float, numRoles> gain {};
    {
        std::array<float, numRoles> peak {};
        for (size_t i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                const float b[3] = { base (0, ch, i), base (1, ch, i), base (2, ch, i) };
                for (int r = 0; r < numRoles; ++r)
                {
                    float v = 0.0f;
                    for (int k = 0; k < 3; ++k) if (roles[r].mask & (1u << k)) v += b[k];
                    peak[(size_t) r] = std::max (peak[(size_t) r], std::abs (v));
                }
            }
        for (int r = 0; r < numRoles; ++r)
            gain[(size_t) r] = peak[(size_t) r] > 0.94f ? 0.94f / peak[(size_t) r] : 1.0f;
    }

    auto parent = stickRoot.getChildFile ("CDJMODS").getChildFile ("stems");
    if (! parent.createDirectory()) { reason = "couldn't create " + parent.getFullPathName(); return Result::failed; }
    auto tmp = parent.getChildFile (".tmp-" + bundle);
    tmp.deleteRecursively();
    if (! tmp.createDirectory()) { reason = "couldn't create " + tmp.getFullPathName(); return Result::failed; }
    auto fail = [&] (const juce::String& why) { tmp.deleteRecursively(); reason = why; return Result::failed; };

    std::array<PageFile, numRoles> files;
    for (int r = 0; r < numRoles; ++r)
        if (! files[(size_t) r].open (tmp.getChildFile ("stems-sidecar-" + juce::String (roles[r].name) + ".s16le.pgz"), pageCount))
            return fail ("couldn't write to the stick");

    // Resample the three faders to 96 kHz a page at a time and write every role's page from them.
    std::array<juce::WindowedSincInterpolator, 6> resamplers;
    const int inBlock = (int) std::ceil (pageFrames * ratio) + 64;
    std::array<std::vector<float>, 6> in, out;
    for (auto& v : in) v.resize ((size_t) inBlock);
    for (auto& v : out) v.resize ((size_t) pageFrames);
    std::vector<unsigned char> page ((size_t) pageBytes);

    const int waveStep = pagedRate / waveEntriesPerSecond;
    std::array<std::vector<unsigned char>, 3> wave;
    std::array<float, 3> wavePeak {};
    int waveCount = 0;

    juce::int64 inPos = 0;
    for (juce::int64 done = 0; done < outFrames; done += pageFrames)
    {
        const int frames = (int) std::min<juce::int64> (pageFrames, outFrames - done);
        int used = 0;
        for (int c = 0; c < 6; ++c)
        {
            auto& src = in[(size_t) c];
            for (int i = 0; i < inBlock; ++i)
            {
                const auto at = inPos + i;
                src[(size_t) i] = at < audio.numFrames ? base (c / 2, c % 2, (size_t) at) : 0.0f;
            }
            used = resamplers[(size_t) c].process (ratio, src.data(), out[(size_t) c].data(), frames);
        }
        inPos += used;

        for (int r = 0; r < numRoles; ++r)
        {
            const float k = gain[(size_t) r] * 32767.0f;
            for (int i = 0; i < frames; ++i)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float v = 0.0f;
                    for (int f = 0; f < 3; ++f) if (roles[r].mask & (1u << f)) v += out[(size_t) (f * 2 + ch)][(size_t) i];
                    const int s = juce::jlimit (-32767, 32767, (int) std::lrint (v * k));
                    page[(size_t) (i * 4 + ch * 2)] = (unsigned char) (s & 0xff);
                    page[(size_t) (i * 4 + ch * 2 + 1)] = (unsigned char) ((s >> 8) & 0xff);
                }
            if (! files[(size_t) r].addPage (page.data(), frames * 4)) return fail ("couldn't write to the stick (is it full?)");
        }

        for (int i = 0; i < frames; ++i)
        {
            for (int f = 0; f < 3; ++f)
                wavePeak[(size_t) f] = std::max (wavePeak[(size_t) f],
                                                 std::max (std::abs (out[(size_t) (f * 2)][(size_t) i]), std::abs (out[(size_t) (f * 2 + 1)][(size_t) i])));
            if (++waveCount == waveStep)
            {
                for (int f = 0; f < 3; ++f)
                {
                    wave[(size_t) f].push_back ((unsigned char) juce::jlimit (0, 31, (int) std::lrint (std::sqrt (wavePeak[(size_t) f]) * 31.0f)));
                    wavePeak[(size_t) f] = 0.0f;
                }
                waveCount = 0;
            }
        }
    }

    auto* rolesObj = new juce::DynamicObject();
    for (int r = 0; r < numRoles; ++r)
    {
        const auto tableSha = files[(size_t) r].finish();
        if (tableSha.length() != 64) return fail ("couldn't write to the stick (is it full?)");
        auto* o = new juce::DynamicObject();
        o->setProperty ("loudness_gain", (double) gain[(size_t) r]);
        o->setProperty ("bytes", (juce::int64) (outFrames * 4));
        o->setProperty ("page_table_sha256", tableSha);
        rolesObj->setProperty (roles[r].name, juce::var (o));
    }
    for (int f = 0; f < 3; ++f)
    {
        if (wave[(size_t) f].empty()) wave[(size_t) f].push_back (0);
        if (! writeWaveform (tmp.getChildFile ("stems-" + juce::String (roles[f].name) + "-waveform.EXT"), wave[(size_t) f]))
            return fail ("couldn't write to the stick");
    }

    {
        auto* runtime = new juce::DynamicObject();
        runtime->setProperty ("sample_rate", pagedRate);
        runtime->setProperty ("channels", 2);
        runtime->setProperty ("format", "s16le");
        runtime->setProperty ("frames", outFrames);
        auto* source = new juce::DynamicObject();
        source->setProperty ("sha256", sourceSha);
        source->setProperty ("file_path", filePath);
        auto* manifest = new juce::DynamicObject();
        manifest->setProperty ("schema", "overcue-stems/4");
        manifest->setProperty ("written_by", "Centrifuge");
        manifest->setProperty ("runtime", juce::var (runtime));
        manifest->setProperty ("source", juce::var (source));
        manifest->setProperty ("roles", juce::var (rolesObj));
        const auto text = juce::JSON::toString (juce::var (manifest));
        if (! tmp.getChildFile ("overcue-manifest.json").replaceWithData (text.toRawUTF8(), text.getNumBytesAsUTF8()))
            return fail ("couldn't write to the stick");
    }

    auto target = parent.getChildFile (bundle);
    target.deleteRecursively();
    if (! tmp.moveFileTo (target)) return fail ("couldn't move stems into place");

    // The index says which bundle belongs to which track. One bundle per track path.
    const auto indexFile = stickRoot.getChildFile ("CDJMODS").getChildFile ("index.json");
    auto index = juce::JSON::parse (indexFile);
    auto* tracks = new juce::DynamicObject();
    if (auto* old = index.getDynamicObject())
        if (auto* oldTracks = old->getProperty ("tracks").getDynamicObject())
            for (auto& p : oldTracks->getProperties())
                if (p.value.getProperty ("file_path", {}).toString() != filePath)
                    tracks->setProperty (p.name, p.value);
    auto* entry = new juce::DynamicObject();
    entry->setProperty ("file_path", filePath);
    entry->setProperty ("bundle", bundle);
    tracks->setProperty (bundle, juce::var (entry));
    auto* root = new juce::DynamicObject();
    root->setProperty ("schema", "overcue-index/1");
    root->setProperty ("tracks", juce::var (tracks));
    const auto text = juce::JSON::toString (juce::var (root));
    auto indexTmp = indexFile.getSiblingFile ("index.json.tmp");
    if (! indexTmp.replaceWithData (text.toRawUTF8(), text.getNumBytesAsUTF8()) || ! indexTmp.moveFileTo (indexFile))
    {
        reason = "couldn't write the stem index";
        return Result::failed;
    }
    return Result::written;
}

} // namespace XzStems
} // namespace orbit
