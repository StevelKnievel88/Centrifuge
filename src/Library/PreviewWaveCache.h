#pragma once
#include "../Audio/WaveformData.h"
#include "../Audio/TrackAudio.h"
#include <array>
#include <juce_core/juce_core.h>
#include <vector>

namespace orbit
{

/** The browser's small preview waveforms (160 points of low / mid / high), kept on disk so tracks Centrifuge
    analysed itself (no rekordbox ANLZ file) still show theirs after a restart. One small file per track. */
struct PreviewWave
{
    std::vector<uint8_t> low, mid, high;
    bool isEmpty() const noexcept { return low.empty(); }
};

/** The four stems' preview (drums, bass, inst, vocals), 160 points each, kept once a track's stems exist. */
struct PreviewStems
{
    std::array<std::vector<uint8_t>, 4> stem;
    bool isEmpty() const noexcept { return stem[0].empty(); }
};

namespace PreviewWaveCache
{
    constexpr int points = 160;

    inline juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Centrifuge").getChildFile ("previews");
    }

    inline juce::File fileFor (const juce::String& audioPath)
    {
        return folder().getChildFile (juce::String::toHexString (audioPath.hashCode64()) + ".pw");
    }

    /** Shrinks a waveform to the preview's 160 points. */
    inline PreviewWave make (const WaveformData& w)
    {
        PreviewWave p;
        const int n = w.size();
        if (n <= 0) return p;
        p.low.assign (points, 0); p.mid.assign (points, 0); p.high.assign (points, 0);
        for (int i = 0; i < n; ++i)
        {
            const size_t b = (size_t) juce::jmin (points - 1, (int) ((juce::int64) i * points / n));
            if (w.style == WaveformData::Style::threeBand)
            {
                p.low[b] = juce::jmax (p.low[b], w.low[(size_t) i]);
                p.mid[b] = juce::jmax (p.mid[b], w.mid[(size_t) i]);
                p.high[b] = juce::jmax (p.high[b], w.high[(size_t) i]);
            }
            else
                p.mid[b] = juce::jmax (p.mid[b], w.height[(size_t) i]);
        }
        return p;
    }

    inline void save (const juce::String& audioPath, const PreviewWave& p)
    {
        if (p.isEmpty() || audioPath.isEmpty()) return;
        folder().createDirectory();
        juce::MemoryOutputStream out;
        out.writeString ("PW1");
        out.writeInt ((int) p.low.size());
        out.write (p.low.data(), p.low.size());
        out.write (p.mid.data(), p.mid.size());
        out.write (p.high.data(), p.high.size());
        fileFor (audioPath).replaceWithData (out.getData(), out.getDataSize());
    }

    inline juce::File stemsFileFor (const juce::String& audioPath)
    {
        return folder().getChildFile (juce::String::toHexString (audioPath.hashCode64()) + ".ps");
    }

    /** Peaks of each stem over 160 points (call from a background thread: it reads the whole track). */
    inline PreviewStems makeStems (const StemAudio& a)
    {
        PreviewStems p;
        const auto n = juce::jmin (a.numFrames, a.validFrames.load());
        if (n <= 0) return p;
        for (auto& v : p.stem) v.assign (points, 0);
        const int64_t step = juce::jmax ((int64_t) 1, n / (points * 64));   // sample every so often - plenty for a peak
        for (int s = 0; s < 4; ++s)
        {
            const auto& L = a.channels[(size_t) s * 2];
            const auto& R = a.channels[(size_t) s * 2 + 1];
            std::array<int, points> peak {};
            for (int64_t i = 0; i < n; i += step)
            {
                const int b = (int) juce::jmin ((int64_t) points - 1, i * points / n);
                peak[(size_t) b] = juce::jmax (peak[(size_t) b], std::abs ((int) L[(size_t) i]), std::abs ((int) R[(size_t) i]));
            }
            for (int b = 0; b < points; ++b)   // stems are stored with 6 dB of headroom
                p.stem[(size_t) s][(size_t) b] = (uint8_t) juce::jlimit (0, 255, peak[(size_t) b] * 2 * 255 / 32767);
        }
        return p;
    }

    inline void saveStems (const juce::String& audioPath, const PreviewStems& p)
    {
        if (p.isEmpty() || audioPath.isEmpty()) return;
        folder().createDirectory();
        juce::MemoryOutputStream out;
        out.writeString ("PS1");
        out.writeInt ((int) p.stem[0].size());
        for (auto& v : p.stem) out.write (v.data(), v.size());
        stemsFileFor (audioPath).replaceWithData (out.getData(), out.getDataSize());
    }

    inline bool loadStems (const juce::String& audioPath, PreviewStems& p)
    {
        juce::MemoryBlock mb;
        if (! stemsFileFor (audioPath).loadFileAsData (mb)) return false;
        juce::MemoryInputStream in (mb, false);
        if (in.readString() != "PS1") return false;
        const int n = in.readInt();
        if (n <= 0 || n > 4096 || in.getNumBytesRemaining() < (juce::int64) n * 4) return false;
        for (auto& v : p.stem) { v.resize ((size_t) n); in.read (v.data(), n); }
        return true;
    }

    inline bool load (const juce::String& audioPath, PreviewWave& p)
    {
        juce::MemoryBlock mb;
        if (! fileFor (audioPath).loadFileAsData (mb)) return false;
        juce::MemoryInputStream in (mb, false);
        if (in.readString() != "PW1") return false;
        const int n = in.readInt();
        if (n <= 0 || n > 4096 || in.getNumBytesRemaining() < (juce::int64) n * 3) return false;
        p.low.resize ((size_t) n); p.mid.resize ((size_t) n); p.high.resize ((size_t) n);
        in.read (p.low.data(), n);
        in.read (p.mid.data(), n);
        in.read (p.high.data(), n);
        return true;
    }
}

} // namespace orbit
