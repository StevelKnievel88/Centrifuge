#pragma once
#include "../Audio/WaveformData.h"
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
