#pragma once
#include "../Audio/TrackAudio.h"
#include <juce_core/juce_core.h>
#include <functional>

namespace orbit
{

/** Writes Centrifuge's stems onto a USB stick in the layout the XZ Mods loader reads on the XDJ-XZ
    (firmware 1.26), so stems play on the deck with no laptop.

    The deck keeps two stems and works out the third itself:
      harmonics = Centrifuge's Inst   (EQ Mid)
      vocals    = Centrifuge's Vocal  (EQ High)
      the rest  = original - harmonics - vocals = Drums + Bass  (EQ Low)

    On the stick:
      mods/stemd-cache/<id>/<key[0..1]>/<key>/harmonics.flac, vocals.flac, meta
      mods/xz-mods/cache-choice/<key>.txt   (one line: <id>)
    <key> is the deck's hash of the track file on the stick, so the track there must not change afterwards. */
namespace XzStems
{
    /** Folder name under mods/stemd-cache. */
    inline constexpr const char* separationId = "centrifuge";

    /** The deck decodes at most 128 MiB of stem PCM per track (two stereo 16-bit stems = 8 bytes a frame),
        which is about 6 min 20 s at 44.1 kHz. */
    inline constexpr int64_t maxFrames = (int64_t) 128 * 1024 * 1024 / 8;

    /** Only uncompressed or lossless 44.1 kHz files decode to the same length on the deck as here. */
    bool isSupportedFile (const juce::File& f);

    /** The deck's 16-hex-digit key for a track file and its decoded 44.1 kHz frame count. Empty on error. */
    juce::String trackKey (const juce::File& trackOnStick, int64_t frames);

    enum class Result { written, skipped, failed };

    /** Writes (or replaces) the stems for one track. `audio` must be the decoded `trackOnStick`;
        `stems` must be complete and aligned with it. `reason` says why on skipped / failed. */
    Result write (const juce::File& stickRoot, const juce::File& trackOnStick, const TrackAudio& audio,
                  const StemAudio& stems, juce::String& reason);

    /** True if the short in-memory layout above can hold this track (WAV/FLAC, 44.1 kHz, up to 6:20). */
    bool fitsShortLayout (const juce::File& trackOnStick, const TrackAudio& audio);

    /** The streamed layout, for everything the short one can't hold: any length, any file type.
        The deck reads it in pages straight off the stick instead of loading it all, so there is no 6:20 limit.
        It is much bigger on the stick: seven 96 kHz stereo files per track (each fader on its own, every pair,
        and the full mix the deck lines the stems up against) - roughly 2.5 MB a second before compression.

        On the stick (the track must be under <stick>/Contents):
          CDJMODS/index.json                              which bundle belongs to which track
          CDJMODS/stems/<bundle>/overcue-manifest.json
          CDJMODS/stems/<bundle>/stems-sidecar-<role>.s16le.pgz
          CDJMODS/stems/<bundle>/stems-<role>-waveform.EXT   (drums, harmonics, vocal)
        Roles: drums = Drums + Bass, harmonics = Inst, vocal = Vocal. */
    Result writePaged (const juce::File& stickRoot, const juce::File& trackOnStick, const TrackAudio& audio,
                       const StemAudio& stems, juce::String& reason,
                       const std::function<bool (float)>& progress = {});   // called often; return false to stop
}

} // namespace orbit
