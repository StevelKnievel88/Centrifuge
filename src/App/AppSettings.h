#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace orbit
{

/** Everything the user can configure, saved as JSON in %APPDATA%\OrbitDJ\settings.json. */
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
    double mp3OffsetMs = 0.0;          // grid/cue correction for MP3s if your decoder differs from rekordbox's

    // Waveforms
    int waveformStyle = 0;             // 0 = 3-band, 1 = RGB, 2 = blue
    double visibleSeconds = 10.0;      // width of the scrolling waveform
    int maxFps = 60;

    // Control
    double jogTicksPerRev = 720.0;
    bool ledFeedback = true;

    // Stems
    juce::String modelPath;            // empty = models/htdemucs.onnx next to the app
    juce::String stemCachePath;        // empty = %LOCALAPPDATA%\OrbitDJ\Stems
    bool stemsOnLoad = true;
    bool stemsUseGpu = true;
    int stemThreads = 0;

    // UI
    bool showBrowser = true;
    int browserHeight = 300;

    static juce::File folder();
    static juce::File settingsFile() { return folder().getChildFile ("settings.json"); }
    static juce::File mappingFile()  { return folder().getChildFile ("mapping.json"); }

    juce::File modelFile() const;
    juce::File stemCacheDir() const;

    void load();
    void save() const;
};

} // namespace orbit
