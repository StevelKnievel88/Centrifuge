Centrifuge - quick start
=====================

1. Install the Pioneer DJ XDJ-XZ driver (ASIO) if you haven't already
   (it's the same driver rekordbox uses).
2. Connect the XDJ-XZ by USB and put the unit in PC mode
   (the same way you would for rekordbox / PC control).
3. Run Centrifuge.exe from this folder. Keep the "models" folder next to it -
   that's the stem separation model.

First run
---------
- Centrifuge finds your rekordbox 6/7 collection automatically
  (%APPDATA%\Pioneer\rekordbox\master.db) and any rekordbox USB stick you plug in.
  Your rekordbox library is only ever read, never changed.
- Audio: Centrifuge picks the XDJ-XZ's ASIO driver if it can see it. Check
  SETTINGS > AUDIO OUTPUT: Deck 1 and Deck 2 should each go to the XDJ-XZ
  channel they play on, so the unit's own mixer, EQs and FX work as normal.
- Controls: go to MAPPING and press GUIDED SETUP. It asks you to press or move
  each control on the XDJ-XZ in turn (Skip anything you don't want).
  Everything else - including the four stem volumes per deck - can be mapped
  from the list: select a function, press LEARN, move the knob.
  The MIDI monitor on the right shows every message the unit sends.
- Tip for stem knobs: the XDJ-XZ only sends its mixer knobs over MIDI when
  "MIXER MIDI MESSAGES" is switched on in the unit's Utilities (hold MENU).
  Knobs on channels you aren't using (e.g. CH3/CH4) are ideal for stems,
  because they don't also change the sound of decks 1 and 2.

Stems
-----
- Each deck has Drums (red), Bass (green), Inst (blue) and Vocals (magenta).
  Inst also carries anything the separator didn't put elsewhere, so all four
  at full = the original track.
- STEMS page: the stem control matrix. Map "Stem control A-D" to knobs, then
  tick which stems each control moves (e.g. Drums + Bass on one knob).
- MAPPING: "LEARN + ADD" puts another function on a control without removing
  what's already there.
- Tracks you've pre-analysed (STEMS page) load their stems instantly.
- Anything else is separated in the background as soon as it's loaded; the
  stems fill in from the start of the track. With a graphics card this takes
  seconds; on CPU only it can take a minute or two per track.
- With every stem at full volume you hear the original file, untouched.

Waveforms
---------
- SETTINGS > WAVEFORMS: each deck has a top and a bottom lane. Pick Stems,
  3-Band, RGB or Single colour for each, full or half height (e.g. deck 1
  stems on top, deck 2 stems underneath, so the two originals sit together).
  Also: overview style, single-colour colour picker, stem layer order and
  the opacity of each stem.
- Drag the playhead left/right to see more of what's coming.
- Zoom with the mouse wheel; zoomed in, the waveform shows the fine detail.

Hot cues, locators and skips
----------------------------
- Hot cue pads: shift+click deletes, right-click picks a colour
  (rekordbox colours or any colour). The same works on the waveform flags.
- MEMORY adds a locator (like a rekordbox memory cue, up to 32 per track).
- SKIP: press at the start of a section, press again at its end. Playback
  jumps over it (snapped to the beat grid with QUANTIZE on) and the
  waveform folds it out. Your audio files are never changed.
- Right-click the waveform for: add locator here, start/end skip here,
  skips on/off, fold on/off.
- Your edits are saved in %APPDATA%\Centrifuge\cues.json. rekordbox's
  library is never changed.

Files
-----
Settings and mapping:  %APPDATA%\Centrifuge\
Stem cache:            %LOCALAPPDATA%\Centrifuge\Stems (about 25 MB per track)
