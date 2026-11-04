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
- Settings > STEMS > Quality: one setting for tracks separated as they load,
  another for pre-analysis (STEMS page). Pre-analysis upgrades stems made at
  a lower quality; a loaded track always uses whatever stems it already has.
    Fast    HTDemucs, one pass (quickest)
    Better  two offset passes averaged (about 2x the time)
    High    fine-tuned specialist models for drums, bass and vocals (about 3x)
    Best    Mel-Band RoFormer vocals (cleanest vocals), then the fine-tuned
            drums and bass models on the rest (slowest - best with a GPU)
- High and Best need extra models (too big for this zip): Settings > GET
  MODELS opens the "models" release. High needs
  Centrifuge-Models-FineTuned.zip, Best needs that and
  Centrifuge-Models-RoFormer.zip. Extract them into this folder (they go into
  models\). Until they're there, the next level down is used.

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

Libraries: rekordbox copy and Centrifuge
----------------------------------------
- Browser > IMPORT copies your rekordbox collection (or a USB stick) into
  Centrifuge's own library: tracks, playlists, beat grids, hot cues, memory
  cues, their colours and names. rekordbox's files are only ever read.
- Every track then has two versions: the REKORDBOX copy (only what rekordbox
  can hold) and the CENTRIFUGE version (also locators beyond rekordbox's limit,
  skips, ...). The browser's REKORDBOX / CENTRIFUGE buttons switch the view;
  the R/C column marks tracks whose two versions differ.
- Each deck has R and C buttons: edits (cues, colours, names, locators, skips,
  beat grid) go to whichever are lit - both lit = both libraries. The deck
  plays the Centrifuge version while C is lit, otherwise the rekordbox copy.
- Importing again updates tracks you haven't edited. Tracks you have edited,
  whose rekordbox data has also changed, are listed so you can choose.
- GRID on a deck opens the beat grid editor: nudge, anchor a beat to the
  playhead, set the downbeat, BPM up/down/set, halve/double, tap, undo, reset.
- Right-click a hot cue or locator to rename it or change its colour.

Music that isn't in rekordbox
-----------------------------
- Browser > IMPORT > Add a folder... / Add audio files..., or drag files and
  folders onto the browser. MP3, WAV, AIFF, FLAC, M4A/AAC.
- Centrifuge reads the tags (title, artist, album, genre, BPM, key) and
  analyses each track itself: tempo, beat grid with the bar start, and key
  (a key in the file's tags is used instead). Takes about a second a track.
- A folder becomes a playlist; a folder with sub-folders becomes a playlist
  folder. Adding the same folder again picks up new files and refreshes it.
- These tracks have R and C versions like any other, export to USB sticks and
  rekordbox XML, and can be fixed in the GRID editor if a downbeat is wrong.
- If you later import the same track from rekordbox, rekordbox's analysis
  replaces Centrifuge's (unless you've edited it - then you're asked).
- Settings > Tempo range: where the analysis looks for the BPM (88-175 suits
  most dance music; choose 70-140 for slower music).

Exporting
---------
- Browser > EXPORT > USB stick: writes a rekordbox-compatible stick for the
  XDJ-XZ (music, analysis files, database), from either version. The
  Centrifuge version is cut down to what rekordbox can hold. The stick's old
  rekordbox database is kept as a backup. Test with a spare stick first.
- Browser > EXPORT > rekordbox XML: a file rekordbox can import, to bring
  your edits back into rekordbox's own collection if you want to.

Files
-----
Settings and mapping:  %APPDATA%\Centrifuge\
Library:               %APPDATA%\Centrifuge\library.db
Stem cache:            %LOCALAPPDATA%\Centrifuge\Stems (about 25 MB per track)
