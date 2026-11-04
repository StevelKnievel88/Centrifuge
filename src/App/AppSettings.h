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
    int sumToMaster = -1;              // -1 auto, 0 off, 1 on
    double mp3OffsetMs = 0.0;
    int analysisRange = 1;             // tempo range for analysing music added from files: 0 = 70-140, 1 = 88-175, 2 = 100-200, 3 = 120-240
    std::pair<double, double> analysisBpmRange() const
    {
        static const double r[][2] = { { 70, 140 }, { 88, 175 }, { 100, 200 }, { 120, 240 } };
        const int i = juce::jlimit (0, 3, analysisRange);
        return { r[i][0], r[i][1] };
    }          // grid/cue correction for MP3s if your decoder differs from rekordbox's

    // Waveforms
    // Each deck's top [0] and bottom [1] lane: style 0 = stems, 1 = 3-band, 2 = RGB, 3 = single colour;
    // shape 0 = full, 1 = half (bars grow up), 2 = half (bars grow down).
    std::array<std::array<int, 2>, 2> laneStyle { { { 0, 1 }, { 0, 1 } } };
    std::array<std::array<int, 2>, 2> laneShape { { { 0, 0 }, { 0, 0 } } };
    int overviewStyle = 1;             // deck overview: 1 = 3-band, 2 = RGB, 3 = single colour
    int overviewShape = 0;             // 0 = full, 1 = half
    juce::uint32 singleColour = 0xff3d9bff;
    std::array<int, 4> stemLayerOrder { 2, 1, 3, 0 };          // stems from back to front (0 drums, 1 bass, 2 inst, 3 vocals)
    std::array<float, 4> stemOpacity { 1.0f, 1.0f, 1.0f, 1.0f }; // per stem (drums, bass, inst, vocals)
    bool foldSkips = true;             // fold skipped sections out of the scrolling waveform
    float playheadPosition = 0.5f;     // across the scrolling waveform (0.5 = centre)
    std::array<float, 2> laneSplit { 0.5f, 0.5f };   // top lane share of each deck's waveform area
    int deckPanelHeight = 210;
    std::array<int, 4> stemMatrix { 1, 2, 4, 8 };    // stem controls A-D -> stems (bit 0 drums .. bit 3 vocals)
    double visibleSeconds = 10.0;      // width of the scrolling waveform
    int maxFps = 60;

    // Library editing: per deck, edit the rekordbox copy (R) and / or the Centrifuge version (C)
    std::array<bool, 2> editR { true, true }, editC { true, true };

    // Control
    bool quantize = true;              // quantize on for both decks at start-up
    double jogTicksPerRev = 720.0;
    bool ledFeedback = true;

    // Stems
    juce::String modelPath;            // empty = models/htdemucs.onnx next to the app
    juce::String stemCachePath;        // empty = %LOCALAPPDATA%\Centrifuge\Stems
    bool stemsOnLoad = true;
    bool stemsUseGpu = true;
    int stemQualityOnLoad = 0;         // 0 Fast, 1 Better, 2 High, 3 Best (see StemQuality)
    int stemQualityBatch = 1;          // for pre-analysis
    int stemThreads = 0;

    // UI
    bool showBrowser = true;
    int browserHeight = 300;

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
