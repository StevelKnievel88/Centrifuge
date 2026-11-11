#include "MasterRecorder.h"
#include "../Library/TagWriter.h"

namespace orbit
{

MasterRecorder::MasterRecorder() { thread.startThread(); }

MasterRecorder::~MasterRecorder()
{
    {
        const juce::ScopedLock sl (writerLock);
        writer.reset();
    }
    thread.stopThread (3000);
}

juce::String MasterRecorder::start (const Options& o, double sampleRate, bool armed)
{
    if (writer != nullptr) return "Already recording";
    options = o;
    rate = sampleRate > 0 ? sampleRate : 44100.0;
    if (! options.folder.createDirectory()) return "Can't make the folder " + options.folder.getFullPathName();
    baseName = "Centrifuge recording " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M");
    const char* ext[] = { ".wav", ".flac", ".mp3" };
    file = options.folder.getNonexistentChildFile (baseName, ext[juce::jlimit (0, 2, options.format)], false);

    std::unique_ptr<juce::AudioFormat> fmt;
    if (options.format == flac) fmt = std::make_unique<juce::FlacAudioFormat>();
    else if (options.format == mp3)
    {
#if JUCE_USE_LAME_AUDIO_FORMAT
        if (! options.lameExe.existsAsFile()) return "MP3 needs the LAME encoder (lame.exe) - set it in Settings > RECORD, or record WAV / FLAC";
        fmt = std::make_unique<juce::LAMEEncoderAudioFormat> (options.lameExe);
#else
        return "MP3 isn't available in this build - record WAV or FLAC";
#endif
    }
    else fmt = std::make_unique<juce::WavAudioFormat>();

    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (file);
    if (! static_cast<juce::FileOutputStream*> (os.get())->openedOk()) return "Can't write " + file.getFullPathName();
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (options.format == mp3 ? 16 : 24);
    if (options.format == mp3) opts = opts.withQualityOptionIndex (juce::jmax (0, fmt->getQualityOptions().indexOf ("320 kbps")));
    auto w = fmt->createWriterFor (os, opts);
    if (w == nullptr) return "Can't make the recording file";
    markers.clear();
    written = 0;
    waiting = armed;
    const juce::ScopedLock sl (writerLock);
    writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), thread, (int) rate * 10);   // 10 s of room in case the disk stalls
    return {};
}

void MasterRecorder::process (const float* l, const float* r, int n)
{
    const juce::ScopedTryLock sl (writerLock);
    if (! sl.isLocked() || writer == nullptr) return;
    if (waiting.load())
    {
        // armed: starts with the first sound (above about -50 dB)
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (l[i]), std::abs (r[i]));
        if (pk < 0.003f) return;
        waiting = false;
    }
    const float* ch[2] = { l, r };
    if (writer->write (ch, n)) written += n;
}

void MasterRecorder::addMarker (double at, const TrackInfo& info)
{
    if (writer == nullptr) return;
    markers.push_back ({ juce::jmax (0.0, at), info.artist, info.title });
}

juce::String MasterRecorder::stop (const juce::String& finalName)
{
    {
        const juce::ScopedLock sl (writerLock);
        if (writer == nullptr) return {};
        writer.reset();   // flushes and closes the file
    }
    auto f = file;
    if (finalName.isNotEmpty() && finalName != file.getFileNameWithoutExtension())
    {
        auto dest = file.getParentDirectory().getNonexistentChildFile (juce::File::createLegalFileName (finalName), file.getFileExtension(), false);
        if (file.moveFileTo (dest)) f = dest;
    }
    std::sort (markers.begin(), markers.end(), [] (auto& a, auto& b) { return a.at < b.at; });
    juce::StringArray made { f.getFileName() };
    if (options.cueFile && ! markers.empty()) made.add (writeCue (f));
    if (options.split && ! markers.empty()) made.add (splitFiles (f));
    made.removeEmptyStrings();
    return made.joinIntoString (", ");
}

juce::String MasterRecorder::writeCue (const juce::File& audio) const
{
    juce::String s;
    s << "TITLE \"" << audio.getFileNameWithoutExtension() << "\"\n";
    s << "FILE \"" << audio.getFileName() << "\" " << (audio.hasFileExtension ("mp3") ? "MP3" : "WAVE") << "\n";
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto& m = markers[i];
        const int frames = (int) std::lround (m.at * 75.0);   // cue sheets count 75 frames a second
        s << "  TRACK " << juce::String ((int) i + 1).paddedLeft ('0', 2) << " AUDIO\n";
        s << "    TITLE \"" << m.title.replace ("\"", "'") << "\"\n";
        s << "    PERFORMER \"" << m.artist.replace ("\"", "'") << "\"\n";
        s << "    INDEX 01 " << juce::String (frames / (75 * 60)).paddedLeft ('0', 2) << ":" << juce::String ((frames / 75) % 60).paddedLeft ('0', 2)
          << ":" << juce::String (frames % 75).paddedLeft ('0', 2) << "\n";
    }
    const auto cue = audio.withFileExtension ("cue");
    return cue.replaceWithText (s) ? cue.getFileName() : juce::String();
}

juce::String MasterRecorder::splitFiles (const juce::File& audio) const
{
    // A file per track, numbered and named after it, in a folder next to the recording (MP3 recordings split as WAV)
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (audio));
    if (reader == nullptr) return {};
    const auto dir = audio.getParentDirectory().getChildFile (audio.getFileNameWithoutExtension() + " (tracks)");
    dir.createDirectory();
    const bool asFlac = audio.hasFileExtension ("flac");
    const auto total = reader->lengthInSamples;
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto s0 = (juce::int64) (markers[i].at * reader->sampleRate);
        const auto s1 = i + 1 < markers.size() ? (juce::int64) (markers[i + 1].at * reader->sampleRate) : total;
        if (s1 <= s0) continue;
        const auto name = juce::File::createLegalFileName (juce::String ((int) i + 1).paddedLeft ('0', 2) + " " + markers[i].artist + " - " + markers[i].title);
        const auto out = dir.getChildFile (name + (asFlac ? ".flac" : ".wav"));
        out.deleteFile();
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (out);
        std::unique_ptr<juce::AudioFormat> fmt;
        if (asFlac) fmt = std::make_unique<juce::FlacAudioFormat>(); else fmt = std::make_unique<juce::WavAudioFormat>();
        auto w = fmt->createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (reader->sampleRate).withNumChannels (2).withBitsPerSample (24));
        if (w == nullptr) continue;
        w->writeFromAudioReader (*reader, s0, s1 - s0);
        w.reset();
        if (asFlac)
        {
            TagWriter::Fields tf;
            tf.title = markers[i].title;
            tf.artist = markers[i].artist;
            juce::String err;
            TagWriter::write (out, tf, err);
        }
    }
    return dir.getFileName() + "/";
}

} // namespace orbit
