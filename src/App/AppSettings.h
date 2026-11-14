#pragma once
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <array>

namespace orbit
{

/** Everything the user can configure, saved as JSON in %APPDATA%\Centrifuge\settings.json. */
struct AppSettings
{
    // Library
    juce::String rekordboxDbPath;      // empty = default location
    juce::String rekordboxDbKey;       // empty = built-in key
    juce::StringArray usbRoots;        // extra folders to treat as USB exports (drives are scanned automatically)

    // Audio
    juce::String audioDeviceState;     // JUCE AudioDeviceManager XML
    int bufferSize = 512;
    std::array<int, 2> deckOutputs { 0, 2 };
    int previewOutput = -1;            // PREP / browser previews: first output channel, -1 = automatic
    bool prepEditR = true, prepEditC = true;
    int prepListWidth = 300;   // PREP page: width of the locators / hot cues / phrases list
    int sumToMaster = -1;              // -1 auto, 0 off, 1 on
    double mp3OffsetMs = 0.0;
    std::array<juce::uint32, 3> deckColours { 0xff2ee6d6, 0xffff3fa0, 0xffffd339 };   // deck 1, deck 2, PREP
    bool camelotKeys = false;          // show keys in Camelot notation
    int analysisRange = 1;             // tempo range for analysing music added from files: 0 = 70-140, 1 = 88-175, 2 = 100-200, 3 = 120-240
    std::pair<double, double> analysisBpmRange() const
    {
        static const double r[][2] = { { 70, 140 }, { 88, 175 }, { 100, 200 }, { 120, 240 } };
        const int i = juce::jlimit (0, 3, analysisRange);
        return { r[i][0], r[i][1] };
    }          // grid/cue correction for MP3s if your decoder differs from rekordbox's

    // Waveforms
    // Each deck's top [0] and bottom [1] lane: style 0 = stems (overlaid), 1 = 3-band, 2 = RGB, 3 = single colour,
    // 4 = stems (a row each), -1 = off (PREP's second lane only);
    // shape 0 = full, 1 = half (bars grow up), 2 = half (bars grow down).
    std::array<std::array<int, 2>, 2> laneStyle { { { 0, 1 }, { 0, 1 } } };
    std::array<std::array<int, 2>, 2> laneShape { { { 0, 0 }, { 0, 0 } } };
    int overviewStyle = 1;             // deck overview: 1 = 3-band, 2 = RGB, 3 = single colour
    int overviewShape = 0;             // 0 = full, 1 = half
    // PREP's waveform: its own lanes (the second can be off), split, overview and zoom
    std::array<int, 2> prepLaneStyle { 0, -1 }, prepLaneShape { 0, 0 };
    float prepLaneSplit = 0.6f;
    int prepOverviewStyle = 1, prepOverviewShape = 0;
    double prepVisibleSeconds = 8.0;
    bool overviewGrid = false;         // bar lines and minute marks over the overviews
    std::array<juce::uint32, 4> stemColours { 0xffff3b3b, 0xff2ee05a, 0xff3d7bff, 0xffff3df2 };   // drums, bass, inst, vocals
    std::array<std::array<int, 4>, 3> stemModes {};   // per deck (1, 2, PREP) and stem: 0 level, 1 low-pass, 2 high-pass
    double noteShowSeconds = 8.0;      // a note locator stays up this long after playback reaches it (0 = until the next one)
    double noteLeadSeconds = 2.0;      // ... and shows this long before
    juce::StringArray collapsedSections;   // Settings sections folded away
    juce::String browserColumns;
    juce::String browserXzColumns = "2,11,3,4,5,6";   // the XDJ-XZ screen browser's columns (ids)
    bool browserInfoOpen = false;
    juce::String windowState;          // the main window's position, size and maximised state      // the browser's track info panel
    int phraseBars = 16;               // phrase meter length (bars)
    bool phraseFollow = false;         // ... or follow the track's phrases
    bool phraseBrackets = false;       // 4 / 16 / 32 / 64-bar brackets over the meter
    int previewStyle = 1;              // browser preview column: 1 3-band, 2 RGB, 3 single colour, 4 stems
    int previewShape = 1;              // 0 full, 1 half (bars from the bottom)       // browser column order and widths
    juce::uint32 singleColour = 0xff3d9bff;
    juce::uint32 previewColour = 0xff3d9bff;
    float markerScale = 1.0f;                  // size of the markers on overviews and browser previews   // the browser preview column's single colour
    bool singleFollowsDeck = false;
    std::array<bool, 3> editView { true, true, true };   // decks 1, 2 and PREP: show the edited version (as it will play)    // single-colour waveforms use each deck's colour instead
    std::array<int, 4> stemLayerOrder { 2, 1, 3, 0 };          // stems from back to front (0 drums, 1 bass, 2 inst, 3 vocals)
    std::array<float, 4> stemOpacity { 1.0f, 1.0f, 1.0f, 1.0f }; // per stem (drums, bass, inst, vocals)
    bool foldSkips = true;             // fold skipped sections out of the scrolling waveform
    float playheadPosition = 0.5f;     // across the scrolling waveform (0.5 = centre)
    std::array<float, 2> laneSplit { 0.5f, 0.5f };   // top lane share of each deck's waveform area
    int deckPanelHeight = 210;
    std::array<double, 8> jumpBeats { 1.0, 4.0, 8.0, 16.0, 1.0, 4.0, 8.0, 16.0 };   // each deck's 4 beat jump buttons
    std::array<juce::uint32, 8> jumpColours {};                                        // ... and their colours (0 = default)
    std::array<int, 8> stemMatrix { 1, 2, 4, 8, 1, 2, 4, 8 };    // stem controls A-D (deck 1), E-H (deck 2) -> stems (bit 0 drums .. bit 3 vocals)
    double visibleSeconds = 10.0;      // width of the scrolling waveform
    int maxFps = 60;

    // Library editing: per deck, edit the rekordbox copy (R) and / or the Centrifuge version (C)
    std::array<bool, 2> editR { true, true }, editC { true, true };

    // Control
    bool quantize = true;              // quantize on for both decks at start-up
    double jogTicksPerRev = 720.0;
    bool ledFeedback = true;
    double longPressSec = 0.5;
    bool hotCuePadColours = true;      // hot cue pads light in their cue's colour
    double xzScreenLeadMs = 90.0;      // the XZ screen's waveforms drawn this far ahead (the picture takes a moment to get there)
    bool xzScreen = true;              // laptop mode: show Centrifuge's decks on the XDJ-XZ screen (network cable + loader)
    bool rbKeepAlive = true;           // talk to the XDJ-XZ like rekordbox (lets Centrifuge colour the pad mode buttons)
    bool jogDisplay = true;            // drive the XDJ-XZ's jog screens (position bar, cue marker) and LOAD lights         // a button held this long is a long press (0.25 - 1 s)
    // Vinyl speed adjust, per deck: brake (stop), start, release after a scratch (seconds), and brake / start on or off
    std::array<double, 2> brakeSec { 0.8, 0.8 }, startSec { 0.4, 0.4 }, releaseSec { 0.0, 0.0 };
    std::array<bool, 2> brakeOn { false, false }, startOn { false, false };
    std::array<double, 2> brakeBeats { 0.0, 0.0 };   // the brake synced to beats (0 = brakeSec instead)
    bool throwOn = true;               // backspin / throw plays out after letting go of a fast spin
    double throwSpeed = 0.6;           // how fast a spin has to be (x normal speed) to count as a throw
    bool autoGain = false;             // match each loaded track's loudness to the master deck
    float masterVolume = 1.0f;         // Centrifuge's own output level
    std::array<juce::String, 2> deckController;   // which MIDI device drives each deck ("" = any)

    // Stems
    juce::String modelPath;            // empty = models/htdemucs.onnx next to the app
    juce::String stemCachePath;        // empty = %LOCALAPPDATA%\Centrifuge\Stems
    bool stemsOnLoad = true;
    bool stemsUseGpu = true;
    int stemQualityOnLoad = 0;         // 0 Fast, 1 Better, 2 High, 3 Best (see StemQuality)
    int stemQualityBatch = 1;          // for pre-analysis
    int stemThreads = 0;
    bool stemsOgg320 = false;
    bool stemsMp3 = false;             // stems as MP3 320 kbps (needs lame.exe)          // compressed stems at 320 kbps (instead of 160)
    bool stemsLossless = false;        // stems kept as FLAC at the track's own sample rate (much bigger) instead of Ogg

    // UI
    bool showBrowser = true;
    bool statusFolded = false;     // top bar: status boxes folded away
    bool artistFirst = false;
    bool autoMaster = true;        // the incoming track becomes MASTER once it takes over      // decks: artist left of the title
    bool tagColourNames = false;   // browser: title and artist in the track's tag colour
    int browserHeight = 300;
    int browserRowHeight = 24;
    double historySeconds = 30.0;

    // Record
    juce::String recordFolder;         // empty = Music/Centrifuge Recordings
    int recordFormat = 0;              // 0 WAV, 1 FLAC, 2 MP3 320
    int recordSource = -1;             // -1 = Centrifuge's mix, otherwise the first channel of an input pair
    juce::String lamePath;             // lame.exe, for MP3
    bool recordCue = true, recordSplit = false;
    bool recordArmed = true;           // starts with the first sound
    juce::File recordingsFolder() const
    {
        return recordFolder.isNotEmpty() ? juce::File (recordFolder)
                                         : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Centrifuge Recordings");
    }      // a track counts as played (history, play count) after this long on air

    static juce::File folder();
    static juce::File settingsFile() { return folder().getChildFile ("settings.json"); }
    static juce::File mappingFile()  { return folder().getChildFile ("mapping.json"); }

    juce::File modelFile() const;
    /** Where a stem model file is (or would be): next to Centrifuge.exe, or in the downloads folder. */
    juce::File findModel (const juce::String& name) const;
    static juce::File downloadedModelsFolder() { return folder().getChildFile ("models"); }
    juce::File stemCacheDir() const;

    void load();
    void save() const;
};

} // namespace orbit
