#include "XzStems.h"
#include <juce_audio_formats/juce_audio_formats.h>
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

} // namespace XzStems
} // namespace orbit
