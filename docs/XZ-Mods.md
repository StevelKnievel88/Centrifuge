# Centrifuge on the XDJ-XZ (XZ Mods loader)

Goal: play Centrifuge's stems on the XDJ-XZ with no laptop, with a Centrifuge-look screen, the stems on the
channel EQ knobs and key shift. Built on [XZ Mods](https://github.com/OpticMystic/XZ-Mods) and its
[toolkit](https://github.com/OpticMystic/xdj-xz-toolkit) (MIT), which loads from `autoexec.bin` on the stick and
runs in RAM only: power off without the stick and the deck is stock again. Firmware 1.26 only.

## 1. Stem export (done: `src/Export/XzStems.*`)

The deck stores two stems and rebuilds the third as original - harmonics - vocals.

| Deck part  | Centrifuge stem | Knob (planned) |
|------------|-----------------|----------------|
| vocals     | Vocal           | EQ High        |
| harmonics  | Inst            | EQ Mid         |
| (residual) | Drums + Bass    | EQ Low         |

Layout on the stick:

```
mods/stemd-cache/centrifuge/<key[0..1]>/<key>/harmonics.flac
                                              vocals.flac
                                              meta        v=1, frames=<n>, harmonics=<gain>, vocals=<gain>
mods/xz-mods/cache-choice/<key>.txt           "centrifuge"
```

- Stems: stereo 16-bit FLAC at 44.1 kHz, exactly as long as the decoded track. Each is scaled by its `gain` so the
  loudest sample fits 16 bits; the deck divides it back out.
- `<key>`: FNV-1a 64 (offset 1469598103934665603) over the file length and frame count as little-endian u64s, the
  first 64 KiB and (if longer) the last 64 KiB of the track file on the stick. Lowercase 16-digit hex.
- Limits from the deck: WAV/FLAC 44.1 kHz sources only (other formats may decode to a different length on the deck),
  and at most 128 MiB of stem PCM, about 6:20.
- Checked by writing with Centrifuge's code and reading back with the toolkit's `stem_cache.c` / `stem_decode.c`:
  same key as the toolkit's builder, stems restored within 16-bit rounding.

## 2. Centrifuge screen skin (next)

A new display style next to the toolkit's `mods/ui/skin_*.c`: stems in Centrifuge's colours, original-track lane.

## 3. Stems on the EQ knobs (planned)

In stem mode each EQ knob sets a stem level instead of filtering:

- 12 o'clock = 100 % (unity, untouched)
- left of centre: fades down to silence at fully left
- right of centre: stays at 100 %, no boost

The toolkit's notes mention a "spare-channel EQ mode" reading the mixer ADC reports; that source isn't public yet.

## 4. Key shift (planned)

The toolkit has a semitone key-shift hook on the deck's time-stretch engine (`mods/key`). Stems are mixed in
before the stretch, so the shift applies once to the summed stems. Key Sync and the key display still need doing.
