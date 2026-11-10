#include "EditExporter.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace orbit
{

namespace
{
    /** One pass through the track on a private deck. solo = -1: the mix; 0..3: only that stem. */
    bool renderPass (LoadedTrack::Ptr track, StemAudio::Ptr stems, int solo, const juce::File& file,
                     const std::function<bool (float)>& progress, float p0, float p1, juce::String& error)
    {
        const double rate = track->audio->sampleRate;
        constexpr int block = 512;
        auto deck = std::make_unique<Deck> (90);
        deck->prepare (rate, block);

        Deck::Command load;
        load.cmd = Deck::Cmd::load;
        load.ptr = track.get();
        deck->post (load);
        if (stems != nullptr)
        {
            Deck::Command sc;
            sc.cmd = Deck::Cmd::attachStems;
            sc.ptr = stems.get();
            sc.serial = track->serial;
            deck->post (sc);
        }
        deck->post (Deck::Cmd::quantizeSet, 0.0);
        {
            // An "active" loop marker loops until someone exits it - nobody will here, so it plays as a plain marker.
            // (Cycle loops play their passes as set.)
            juce::AudioBuffer<float> warm (2, block);
            deck->handleCommands();
            deck->process (warm.getWritePointer (0), warm.getWritePointer (1), block);
            const auto& st = deck->state();
            for (int i = 0; i < juce::jmin (st.numLocators.load(), Deck::maxLocators); ++i)
                if (st.locMode[(size_t) i].load() == 1)
                    deck->post (Deck::Command { Deck::Cmd::locatorLoopMode, 0.0, i, true, nullptr, 0, (double) st.locCycles[(size_t) i].load() });
        }
        if (solo >= 0)
            for (int s = 0; s < 4; ++s)
            {
                deck->post (Deck::Cmd::stemMode, 0.0, s);
                deck->post (Deck::Cmd::stemGain, s == solo ? 0.5 : 0.0, s);   // centre = untouched, far left = silent
            }
        deck->post (Deck::Cmd::seekSeconds, 0.0);
        deck->post (Deck::Cmd::play);

        file.deleteFile();
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (file);
        if (! static_cast<juce::FileOutputStream*> (os.get())->openedOk()) { error = "Can't write " + file.getFullPathName(); return false; }
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
        if (writer == nullptr) { error = "Can't write WAV"; return false; }

        juce::AudioBuffer<float> buf (2, block);
        const double length = track->audio->lengthSeconds();
        const int64_t cap = (int64_t) (rate * juce::jmax (60.0, length * 3.0));   // a loop marker that never lets go can't run forever
        int64_t done = 0;
        bool started = false;
        while (done < cap)
        {
            deck->handleCommands();
            deck->process (buf.getWritePointer (0), buf.getWritePointer (1), block);
            const bool playing = deck->state().playing.load();
            if (playing) started = true;
            if (started && ! playing) break;   // reached the end
            writer->writeFromAudioSampleBuffer (buf, 0, block);
            done += block;
            if ((done / block) % 200 == 0 && progress)
                if (! progress (p0 + (p1 - p0) * (float) juce::jlimit (0.0, 1.0, (double) done / (rate * juce::jmax (1.0, length)))))
                {
                    writer.reset();
                    file.deleteFile();
                    error = "Stopped";
                    return false;
                }
        }
        return true;
    }
}

bool EditExporter::render (LoadedTrack::Ptr track, StemAudio::Ptr stems, const juce::File& base, bool withStems,
                           std::function<bool (float)> progress, juce::String& error)
{
    if (track == nullptr || track->audio == nullptr || track->audio->numFrames == 0) { error = "No track"; return false; }
    if (withStems && (stems == nullptr || ! stems->isComplete())) { error = "This track's stems aren't ready yet"; return false; }
    const int passes = withStems ? 5 : 1;
    base.getParentDirectory().createDirectory();
    if (! renderPass (track, stems, -1, base.withFileExtension ("wav"), progress, 0.0f, 1.0f / (float) passes, error)) return false;
    if (! withStems) return true;
    const char* names[] = { "Drums", "Bass", "Inst", "Vocals" };
    for (int s = 0; s < 4; ++s)
    {
        const auto f = base.getParentDirectory().getChildFile (base.getFileNameWithoutExtension() + " (" + names[s] + ").wav");
        if (! renderPass (track, stems, s, f, progress, (float) (s + 1) / (float) passes, (float) (s + 2) / (float) passes, error)) return false;
    }
    return true;
}

} // namespace orbit
