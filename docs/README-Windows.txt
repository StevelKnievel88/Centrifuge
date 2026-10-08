OrbitDJ - quick start
=====================

1. Install the Pioneer DJ XDJ-XZ driver (ASIO) if you haven't already
   (it's the same driver rekordbox uses).
2. Connect the XDJ-XZ by USB and put the unit in PC mode
   (the same way you would for rekordbox / PC control).
3. Run OrbitDJ.exe from this folder. Keep the "models" folder next to it -
   that's the stem separation model.

First run
---------
- OrbitDJ finds your rekordbox 6/7 collection automatically
  (%APPDATA%\Pioneer\rekordbox\master.db) and any rekordbox USB stick you plug in.
  Your rekordbox library is only ever read, never changed.
- Audio: OrbitDJ picks the XDJ-XZ's ASIO driver if it can see it. Check
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
- Tracks you've pre-analysed (STEMS page) load their stems instantly.
- Anything else is separated in the background as soon as it's loaded; the
  stems fill in from the start of the track. With a graphics card this takes
  seconds; on CPU only it can take a minute or two per track.
- With every stem at full volume you hear the original file, untouched.

Files
-----
Settings and mapping:  %APPDATA%\OrbitDJ\
Stem cache:            %LOCALAPPDATA%\OrbitDJ\Stems (about 25 MB per track)
