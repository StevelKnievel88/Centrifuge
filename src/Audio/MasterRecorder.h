#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Library/TrackInfo.h"

namespace orbit
{

/** Records the master output (Centrifuge's mix, or an input pair such as the XDJ-XZ's master) to WAV / FLAC / MP3.
    The audio thread hands blocks to a threaded writer (no file work on the audio thread). Track markers (when each
    track went on air) give a .cue file and / or the recording split into a file per track afterwards. */
class MasterRecorder
{
public:
    enum Format { wav = 0, flac = 1, mp3 = 2 };
    struct Options
    {
        juce::File folder;
        int format = wav;
        int source = -1;            // -1 = Centrifuge's mix (outputs summed), otherwise the first input channel of a pair
        juce::File lameExe;         // for MP3 (LAME encoder)
        bool cueFile = true, split = false;
    };

    MasterRecorder();
    ~MasterRecorder();

    /** armed: waits for sound before it starts. Returns an error, or empty. */
    juce::String start (const Options& o, double sampleRate, bool armed);
    /** Stops; the recording is renamed to `finalName` (no extension) and the cue / split files written. Returns what was made. */
    juce::String stop (const juce::String& finalName);
    bool isRecording() const { return writer != nullptr; }
    bool isWaiting() const { return waiting.load(); }
    double elapsedSeconds() const { return rate > 0 ? (double) written.load() / rate : 0.0; }
    juce::String defaultName() const { return baseName; }
    int getSource() const { return options.source; }

    /** A track went on air at `atSeconds` into the recording. */
    void addMarker (double atSeconds, const TrackInfo& info);

    /** Audio thread: the block to record (stereo). */
    void process (const float* l, const float* r, int n);

private:
    struct Marker { double at; juce::String artist, title; };
    juce::String writeCue (const juce::File& audio) const;
    juce::String splitFiles (const juce::File& audio) const;

    Options options;
    juce::TimeSliceThread thread { "Centrifuge recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::CriticalSection writerLock;
    juce::File file;
    juce::String baseName;
    double rate = 44100.0;
    std::atomic<juce::int64> written { 0 };
    std::atomic<bool> waiting { false };
    std::vector<Marker> markers;
};

} // namespace orbit
