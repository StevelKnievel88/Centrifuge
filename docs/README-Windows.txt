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
- Centrifuge finds any rekordbox USB stick you plug in (only ever read).
  Add your own music with Browser > IMPORT > Add a folder / Add audio files.
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
- STEMS page > settings (under the matrix) > Quality: one setting for tracks separated as they load,
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
- LOCATOR adds a locator (like a rekordbox memory cue, up to 32 per track).
- Loops: the "< 16 >" group sets the loop size (< halves, > doubles) and the
  middle button starts / ends a loop of that size.
- BEAT JUMP row: back / forward 1 beat, 1 bar, 2 bars or 4 bars.
- Loops on cues: stop at the start of a loop (or with the loop playing) and
  press an empty hot cue pad - it saves a hot loop (orange, with a loop
  icon). LOCATOR does the same as a loop locator.
- Right-click a loop locator to make it:
    Normal  plays when you press it, like any saved loop
    Active  the loop switches on by itself when the playhead reaches it
    Cycle   active, but plays the loop N times (2-32; its flag shows "4x"
            and counts down) and then carries on - it resets next time
  USB sticks and rekordbox XML get a plain loop (rekordbox has no loop
  modes).
- Tempo: drag the +0.00% readout up (faster) or down (slower) - hold shift
  for fine steps - and double-click it to reset. Click the range under it
  to step through 6% / 10% / 25% / 50% / WIDE.
- Play, pause and cue start and stop with a 2 ms fade, so no clicks.
- Loop locators: the line down from the head shows the mode - orange =
  active, red = cycle, otherwise the locator's own colour.
- Overviews show hot cues as triangles pointing up from the bottom (in the
  hot cue's colour), locators pointing down from the top, and phrases as a
  coloured strip along the bottom.
- Phrases (song sections) show as named, coloured bars along the bottom of
  the waveforms. Tracks analysed by rekordbox bring rekordbox's phrases;
  change them on the PREP page (PHRASES tab) - any name, any colour.
- If the same track is loaded on two decks (or PREP), an edit on one shows
  on the other straight away, and in the browser's preview.
- Click the coloured deck number to change a deck's colour (default,
  rekordbox colours or any colour). The phrase meter, deck badges and
  waveforms follow it.
- Phrase meter (top bar): decks 1 and 2 stacked, one block per beat over a
  16-bar phrase, every other bar a shade darker; the current beat flashes.
- SKIP: press at the start of a section, press again at its end. Playback
  jumps over it (snapped to the beat grid with QUANTIZE on) and the
  waveform folds it out. Your audio files are never changed.
- Right-click the waveform for: add locator here, start/end skip here,
  skips on/off, edited view on/off.
- Your edits are saved in %APPDATA%\Centrifuge\cues.json. rekordbox's
  library is never changed.

Non-destructive edits
---------------------
All made on the PREP page, saved with the track in Centrifuge's library; the
audio file is never touched. The SKIP button (skips on/off) switches every
edit below on or off live.
- STEM LOCATORS: STEM LOC (under PREP's waveform) or + ADD AT PLAYHEAD... >
  Stem locator. From that point each stem fades out, fades in, or is left
  alone - straight away or over 1-32 beats - as a level fade or a low-pass /
  high-pass filter sweep. Click its icon in the list to change it. Needs the
  track's stems (decks separate them; PREP uses stems already made).
- PHRASE REPEAT: the x1 chip on a phrase (PHRASES tab) steps x1 > x2 > x3 >
  x4 > x8 - the phrase plays that many times in a row.
- SECTION REARRANGE: drag a phrase by its grip in the PHRASES tab to change
  the order the track plays in (RESET ORDER puts it back). Playback jumps at
  the joins; with a rearrangement the track ends when the last section ends.
- NOTE LOCATORS: NOTE (under PREP's waveform) or + ADD AT PLAYHEAD... > Note.
  A reminder shown in each deck's NOTES box (next to the beat jumps), from a
  little before its point until a set time after - both set in Settings >
  NOTE LOCATORS (2 s before / 8 s after by default; 0 = until the next note).
- EDIT VERSIONS: the EDIT chip (PREP header and every deck) picks a named set
  of skips, loop modes, stem locators, phrase order and repeats ("Radio
  edit", "Extended"...). On PREP: save the current edits as a new version,
  update, rename or delete. "No edits" plays the track as recorded. The
  version chosen last is what loads next time.
- EDITED VIEW: the EDITED chip on each deck and on PREP. On: overviews,
  scrolling waveforms, times and the phrase meter show the track as it will
  play (skips folded out, phrases in play order, repeats shown again, a
  dashed line at each join). Off: the whole track as recorded, every marker.

Stem controls: level or filter
------------------------------
Each stem button on a deck (and on PREP) can work as a volume (LVL), a
low-pass filter (LP - closes down as the control goes down) or a high-pass
filter (HP - closes up). Right-click a stem for that stem, or click the chip
before the four to switch all of them (LVL > LP > HP; MIX = they differ).
Your mapping is unchanged: the same knob now fades or filters. Remembered
per deck.

Waveforms and overviews
-----------------------
- Each lane: stems overlaid, stems a row each (drums, bass, inst, vocals),
  3-band, RGB or single colour - full or half height (Settings).
- Drag the line between a deck's two lanes: both decks take the same split.
- The beat grid carries on before a track starts and after it ends.
- The grid button left of each overview turns on bar lines and minute marks
  over the overviews.
- Stem separation progress shows as a strip under the deck's overview, so
  you can see how far it has got against the track and the playhead.
- Settings > STEM COLOURS AND LAYERS: each stem's colour (used everywhere).
- Settings sections fold away: click a section's title.

PREP page (F5)
--------------
- One track on its own, for getting it ready: big zoomable waveform (drag
  to scrub, wheel to zoom), overview (click to go anywhere), the beat grid
  editor, hot cues A-H, loops, locators, skips and beat jumps.
- PREP's waveform has its own settings (Settings > PREP - WAVEFORMS): the
  lane style, an optional second lane, the split between them, the overview
  style. Click the P badge to change PREP's colour.
- EDITED (edited view) and the overview grid button are left of the overview;
  the stem controls are to its right. Under the waveform: LOCATOR, LOOP LOC,
  STEM LOC, NOTE and SKIP add at the playhead.
- On the right, three tabs:
    LOCATORS  every locator, loop locator and skip, in track order. Click
              a loop locator's mode text (loop / active / cycle) to step
              through the three.
    HOT CUES  A-H with their names.
    PHRASES   the track's sections. + ADD PHRASE AT PLAYHEAD (Intro, Verse,
              Build, Drop, Breakdown, Chorus, Bridge, Outro or your own
              name); right-click to rename, recolour, move or delete.
  In every tab: click an entry to go there; click its name to rename it
  right there (Enter keeps it, Esc cancels); click its icon to change its
  colour; the x on the far right deletes it; right-click for more.
  Select several like in Windows Explorer - ctrl + click adds / removes
  one, shift + click selects a range, ctrl + A selects all - then click an
  icon (or right-click) to colour them all at once, or press Delete.
- Drag the list's left edge to make it wider or narrower (remembered).
- + ADD LOCATOR AT PLAYHEAD (LOCATORS tab) and + ADD PHRASE AT PLAYHEAD
  (PHRASES tab). The phrase menu's "Edit presets" adds, renames, recolours,
  removes and reorders the presets (or goes back to the standard ones). A
  preset's colour is what new phrases of that name get - e.g. Drop green,
  Breakdown red, Intro pink - and "Recolour this track's phrases" applies the
  preset colours to a track you've already done.
- Colours: every colour menu has the presets, then your saved colours, then
  "Custom colour...". In the custom picker, + SAVE COLOUR asks for a name and
  adds it to your saved colours; "Delete a saved colour" removes one.
- The phrase meter in the top bar shows just PREP while you're on PREP.
- Load a track: double-click it while on PREP, drag it onto PREP, or
  right-click it in the browser > Load into PREP.
- PREP and browser previews each have their own deck (a preview never
  replaces the track on PREP). Both play on the output chosen in
  Settings > PREP / previews play on (Automatic = the first output pair
  the decks aren't using, e.g. headphones).

Browser
-------
- STEMS column: the rotor shows a track has stems, with their quality
  (FAST / BETTER / HIGH / BEST). Right-click > "Separate stems now" (or
  pre-analysis) at a higher quality replaces lower-quality stems, and the
  column updates.
- PREVIEW column: a shrunk overview of each track (3-band waveform, hot
  cues, locators, skips and phrases) - click it to play from there on the
  preview output; click the square to stop. Tracks Centrifuge analysed keep
  their preview (it shows straight away after a restart); a track nobody has
  analysed yet says NOT ANALYSED.
- Column order and widths are remembered.
- Drag a track onto deck 1 or 2 (or the top / bottom half of the waveforms)
  to load it.
- RATING (click the stars) and PLAYS columns. A play is counted once a
  track has played for 30 seconds on deck 1 or 2.
- Right-click a track: load to deck 1 / 2 / PREP, remove from Centrifuge's
  library (your music files and rekordbox are never touched), reset play
  count. Ratings and plays are kept in Centrifuge's library (a track
  from rekordbox joins it once you edit it or play it).
- Settings > Key notation: Standard (Am, F#) or Camelot (8A, 2B).

Library
-------
- One library. The browser shows Centrifuge's library (the music you've
  added) and any rekordbox USB stick you plug in (only ever read). A track
  from a stick joins Centrifuge's library the first time you edit, rate or
  play it - cues, colours, names, locators, loops, skips, phrases and the
  beat grid are all saved there.
- (Your rekordbox PC collection, and the rekordbox-copy feature - a second,
  rekordbox-only version of every track with R / C buttons - are switched
  off for now and will come back later.)
- GRID on a deck opens the beat grid editor: nudge, anchor a beat to the
  playhead, set the downbeat, BPM up/down/set, halve/double, tap, undo, reset.
- Right-click a hot cue or locator to rename it or change its colour.
- Settings > WAVEFORMS > "Single colour follows each deck's colour": single-
  colour waveforms take the colour of the deck they're on.
- Settings > WAVEFORMS > Stems in the waveform: Overlaid, or Separate (a
  row each: drums, bass, inst, vocals).

Music that isn't in rekordbox
-----------------------------
- Browser > IMPORT > Add a folder... / Add audio files..., or drag files and
  folders onto the browser. MP3, WAV, AIFF, FLAC, M4A/AAC.
- Centrifuge reads the tags (title, artist, album, genre, BPM, key) and
  analyses each track itself: tempo, beat grid with the bar start, and key
  (a key in the file's tags is used instead). Takes about a second a track.
- A folder becomes a playlist; a folder with sub-folders becomes a playlist
  folder. Adding the same folder again picks up new files and refreshes it.
- These tracks export to USB sticks and rekordbox XML like any other, and
  can be fixed in the GRID editor if a downbeat is wrong.
- Settings > Tempo range: where the analysis looks for the BPM (88-175 suits
  most dance music; choose 70-140 for slower music).

Exporting
---------
- Browser > EXPORT > USB stick: writes a rekordbox-compatible stick for the
  XDJ-XZ (music, analysis files, database), from either version. The
  Centrifuge version is cut down to what rekordbox can hold. The stick's old
  rekordbox database is kept as a backup. Test with a spare stick first.
- "Also write stems for the XDJ-XZ" (on by default): for tracks you have
  already separated, writes Inst and Vocal stems to the stick in the layout
  the XZ Mods USB loader (XDJ-XZ firmware 1.26) reads, so stems play on the
  deck with no laptop. Drums + Bass is what's left of the track. Only WAV or
  FLAC at 44.1 kHz, up to 6:20 long (the deck's memory limit for stems).
  Don't re-tag or replace a track on the stick afterwards: its stems are
  matched to the exact file. See docs/XZ-Mods.md.
- Browser > EXPORT > rekordbox XML: a file rekordbox can import, to bring
  your edits back into rekordbox's own collection if you want to.

Files
-----
Settings and mapping:  %APPDATA%\Centrifuge\
Library:               %APPDATA%\Centrifuge\library.db
Stem cache:            %LOCALAPPDATA%\Centrifuge\Stems (about 25 MB per track)

Stems: if the graphics card resets
----------------------------------
- If Windows resets the graphics card during Best (it took too long or ran
  out of graphics memory - "GPU device instance has been suspended"),
  Centrifuge now finishes the track at High and uses High for the rest of
  the session. If the card resets again, it carries on with the CPU.
