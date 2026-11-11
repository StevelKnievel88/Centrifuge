# What the XDJ-XZ 1.26 player program (rbp) tells us

Extracted with `tools/xz/centrifuge_xz.py` (Centrifuge XZ Tools) from the official 1.26 firmware. The player
program is `images/pdj.tar.gz!/pdj/rbp`, an ELF32 ARM binary, SHA-256
`6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2` (matches the toolkit's pinned hash). It keeps
its full symbol table (~18,400 functions), so the layout is readable.

## The mixer link (ui::MixerCpuCom)

The player talks to the mixer's own microcontroller over a framed link:
- `disassemble()` = frames IN, mixer -> player. This carries every front-panel control as a 10-bit ADC value.
- `assemble()` = frame OUT, player -> mixer.

The mixer reports (from the hardware test screen, the same values the frame carries) for **every channel**:

    CH1..CH4:  TRIM, HI, MID, LOW, COLOR, FADER      (each "%03X", 0..0x3FF)
    MIC1/MIC2: HI, MID, LOW
    AUX TRIM, CROSS FADER, BOOTH LEVEL, MASTER LEVEL, MASTER EQ HI/MID/LOW

**So the player can read all four channels' EQ (HI/MID/LOW), TRIM, COLOR and fader positions.** That is the input
side of the "stems on the EQ knobs" idea, and it is available on channels 1-4.

## The catch: the player can't set the channel EQ

The OUT frame's setters are only:

    setBeatInfo, setSeratoTempo, setSeratoOriginalBpm, setSeratoMixerInput, setPortState,
    setTestPattern, startupMixerFunction

There is **no** `setChannelEq` / `setChannelGain` / `setStemGain`. The mixer applies the channel strip
(TRIM, HI/MID/LOW EQ, COLOR, fader) itself, in its own hardware, straight from the knob ADCs. The player only
observes those knobs and tells the mixer about Serato routing/tempo. The channel-strip DSP lives in the separate
mixer image (`images/MIXER.bin`, ~2 MiB), not in rbp.

Consequence: on a channel whose audio you're actually hearing, turning HI both (a) is readable by us for a stem and
(b) physically cuts that channel's highs in the mixer. We can't make the knob control a stem *only*, because we
can't tell the mixer to hold its EQ flat. This is why XZ Mods put its stem-on-knob mode on the spare channels.

## The usable scheme (what we'll build)

A Centrifuge deck's summed stereo plays on its normal channel (1 or 2) and is EQ'd/faded there as usual. The
**stem knobs come from a spare channel** whose knobs otherwise do nothing to what you hear:
- Deck 1 stems <- channel 3 knobs; Deck 2 stems <- channel 4 knobs.
- Each spare channel has TRIM + HI + MID + LOW (+ COLOR) = up to 5 knobs, read by the player.
- Default mapping (3 knobs, drums+bass tied): TRIM = drums+bass, HI = vocals, MID = inst, LOW = bass when untied.
- Per knob: VOL (12 o'clock = 100%, left fades to silence, right stays 100%) or FILTER (left LP, right HP),
  chosen per deck in our menu. All of this is done in rbp's own stem mixer, so any curve is ours to define.

Taking over channel 1/2's *own* EQ knobs (so the knob controls a stem with no EQ side-effect) would need changing
the mixer microcontroller image, a much bigger and riskier job - parked unless we ever want it.

## Longer stems

The 6:20 cap is the toolkit loading each deck's stems whole (<=128 MiB PCM/deck). Reading Centrifuge's stem files
from the USB in windows as the track plays removes the length cap; that's our stem playback path to write.

## Live playback state for our own screen

rbp exposes named engine/UI classes (PlayerInnards, DjEngineIF, grid/cue symbols, MixerLinkManager on-air/bpm/
slider info). Enough looks reachable to read playhead/BPM/key/cue/on-air for a Centrifuge-drawn screen, but each
value needs confirming on the unit. Until then the Centrifuge screen draws our menu/stem UI over the stock play
screen, same staging as before.
