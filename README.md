# OrbitDJ

A lightweight Windows DJ player for the Pioneer XDJ-XZ that uses your rekordbox library.

- Reads the rekordbox 6/7 collection (`master.db`) and rekordbox USB exports (`export.pdb`), including playlists, beat grids, hot/memory cues and rekordbox's own 3-band / RGB waveform analysis. Read-only.
- Detects the XDJ-XZ, shows every MIDI message it sends, and lets you map any control to any function (MIDI learn and a guided setup), with LED feedback.
- Two decks, each sent to its own XDJ-XZ channel so the hardware mixer does the mixing.
- rekordbox-style scrolling waveforms, synced to the display refresh and redrawn only when something moves.
- Key lock (master tempo), beat sync, quantize, hot cues, loops, beat jump, jog scratching.
- Stem separation (HTDemucs, ONNX Runtime with DirectML on the GPU): pre-analyse playlists in batch, or let it separate in the background as soon as a track is loaded. Stem volumes can be mapped to any knob.

## Download

Every push to `main` builds `OrbitDJ-Windows.zip` and attaches it to the **latest** release (see the Releases section of this repository). Extract it and run `OrbitDJ.exe`. See `docs/README-Windows.txt` for first-run steps.

## Building

CMake + MSVC (Visual Studio 2022). Dependencies are fetched automatically (JUCE, SQLite3 Multiple Ciphers, Signalsmith Stretch). Optional:

- `-DORT_ROOT=<folder>` - ONNX Runtime (`include/`, `lib/`) for stem separation
- `-DASIO_SDK_DIR=<folder>` - Steinberg ASIO SDK for ASIO output

See `.github/workflows/build.yml` for the exact steps, including exporting the stem model (`tools/export_htdemucs_onnx.py`) and the check that the C++ stem pipeline matches the official demucs output.

## Credits

JUCE; SQLite3 Multiple Ciphers (utelle); Signalsmith Stretch; ONNX Runtime; HTDemucs (Meta) and Sevag Hanssian's demucs.onnx export; rekordbox file format research by Deep Symmetry (crate-digger), Holzhaus (rekordcrate) and Dylan Jones (pyrekordbox); Inter typeface.
