#include "App/MainComponent.h"
#include "UI/KeysWindow.h"
#include "App/EditExporter.h"
#include "App/TrackEditor.h"
#include "Library/GridOps.h"
#include "Library/TagWriter.h"
#include "Library/TagReader.h"
#include "Library/RekordboxMasterDb.h"
#include "Library/RekordboxPdb.h"
#include "Library/Anlz.h"
#include "Stems/DemucsSeparator.h"
#include "Library/CentrifugeLibrary.h"
#include "Export/Exporters.h"
#include "Export/XzStems.h"
#include "Audio/TrackAnalyser.h"
#include "Library/TagReader.h"
#include "Library/FileImporter.h"
#include "Audio/MasterRecorder.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace orbit
{

//==============================================================================
/** Command-line tools (no window): stem separation test and library dump. Returns -1 if not a CLI call. */
static int runCommandLine (const juce::StringArray& args)
{
    auto argAfter = [&] (const juce::String& flag) -> juce::String
    {
        const int i = args.indexOf (flag);
        return i >= 0 && i + 1 < args.size() ? args[i + 1].unquoted() : juce::String();
    };

    if (args.contains ("--rectest"))
    {
        // Records 3 tones (armed: 1 s of silence first is skipped), with a marker per "track", as FLAC: cue + split files
        const juce::File dir (argAfter ("--rectest"));
        dir.deleteRecursively();
        MasterRecorder rec;
        MasterRecorder::Options o;
        o.folder = dir; o.format = MasterRecorder::flac; o.cueFile = true; o.split = true;
        auto err = rec.start (o, 44100.0, true);
        if (err.isNotEmpty()) { std::cout << "rec error " << err << std::endl; return 1; }
        std::vector<float> l (512), r (512);
        double t = 0.0;
        int track = -1;
        for (int b = 0; b < (int) (7.0 * 44100 / 512); ++b)
        {
            const double sec = b * 512 / 44100.0;
            const int want = sec < 1.0 ? -1 : juce::jmin (2, (int) ((sec - 1.0) / 2.0));
            if (want != track && want >= 0)
            {
                track = want;
                TrackInfo ti; ti.artist = "Artist " + juce::String (track + 1); ti.title = "Tone " + juce::String (track + 1);
                rec.addMarker (rec.elapsedSeconds(), ti);
            }
            for (int i = 0; i < 512; ++i, t += 1.0 / 44100.0)
                l[(size_t) i] = r[(size_t) i] = want < 0 ? 0.0f : 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * (220.0 * (want + 1)) * t);
            rec.process (l.data(), r.data(), 512);
            juce::Thread::sleep (1);   // (roughly real-time, so the writer keeps up as it would)
        }
        std::cout << "rec elapsed " << rec.elapsedSeconds() << std::endl;
        std::cout << "rec made " << rec.stop ("Test set") << std::endl;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, true))
            std::cout << "  " << f.getRelativePathFrom (dir) << " " << f.getSize() << std::endl;
        std::cout << dir.getChildFile ("Test set.cue").loadFileAsString() << std::endl;
        return 0;
    }

    if (args.contains ("--gridtest"))
    {
        // A track that speeds up from 120 to 132 BPM over 64 beats: does "follow the kicks" find every kick?
        TrackAudio a;
        a.sampleRate = 44100.0;
        std::vector<double> kicks;
        double t = 2.0, bpm = 120.0;
        for (int k = 0; k < 160; ++k) { kicks.push_back (t); if (k >= 48 && k < 112) bpm += 12.0 / 64.0; t += 60.0 / bpm; }
        a.numFrames = (int64_t) ((t + 2.0) * a.sampleRate);
        a.left.assign ((size_t) a.numFrames, 0.0f);
        juce::Random rnd (1);
        for (size_t i = 0; i < a.left.size(); ++i) a.left[i] = (rnd.nextFloat() - 0.5f) * 0.05f;   // hiss
        for (double kt : kicks)
        {
            const auto s0 = (int64_t) (kt * a.sampleRate);
            for (int i = 0; i < 6000 && s0 + i < a.numFrames; ++i)
                a.left[(size_t) (s0 + i)] += 0.8f * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * i / a.sampleRate) * std::exp (-i / 2500.0);
        }
        a.right = a.left;
        auto grid = GridOps::constant (120.0, kicks[0], 1, a.lengthSeconds());
        double hop = 0.0025;
        const auto flux = TrackEditor::lowOnsets (a, hop);
        auto followed = GridOps::followKicks (grid, flux, hop, 0.0, kicks[0], a.lengthSeconds(), false);
        double worstBefore = 0.0, worstAfter = 0.0;
        for (double kt : kicks)
        {
            worstBefore = juce::jmax (worstBefore, std::abs (grid[(size_t) GridOps::nearestBeat (grid, kt)].timeSec - kt));
            worstAfter = juce::jmax (worstAfter, std::abs (followed[(size_t) GridOps::nearestBeat (followed, kt)].timeSec - kt));
        }
        std::printf ("grid test: worst kick offset %.1f ms with a constant grid, %.1f ms after following (beats %d -> %d)\n",
                     worstBefore * 1000.0, worstAfter * 1000.0, (int) grid.size(), (int) followed.size());
        return 0;
    }

    if (args.contains ("--tagtest"))
    {
        // Writes tags into a copy of each file given and reads them back (for testing TagWriter)
        int bad = 0;
        for (int i = args.indexOf ("--tagtest") + 1; i < args.size(); ++i)
        {
            const juce::File f (args[i].unquoted());
            TagWriter::Fields fl { "Test Title \xc3\xa9", "Test Artist", "Test Album", "Psytrance", "Test Label", "A comment", "8A" };
            juce::String err;
            const bool ok = TagWriter::write (f, fl, err);
            const auto back = AudioTags::read (f);
            std::printf ("%s: write %s %s | title '%s' artist '%s' album '%s' genre '%s' label '%s' comment '%s' key '%s'\n", f.getFileName().toRawUTF8(),
                         ok ? "ok" : "FAILED", err.toRawUTF8(), back.title.toRawUTF8(), back.artist.toRawUTF8(), back.album.toRawUTF8(), back.genre.toRawUTF8(),
                         back.label.toRawUTF8(), back.comment.toRawUTF8(), back.key.toRawUTF8());
            if (! ok || back.genre != "Psytrance") ++bad;
        }
        return bad;
    }

    if (args.contains ("--xzpaged-test"))
    {
        // Writes a made-up track + stems to <dir>/Contents and packs them in the streamed XZ layout, for checking
        // against the loader's own decoder. --seconds N (default 20), --rate R (default 44100).
        const auto root = juce::File (argAfter ("--xzpaged-test"));
        const double rate = argAfter ("--rate").isNotEmpty() ? argAfter ("--rate").getDoubleValue() : 44100.0;
        const double seconds = argAfter ("--seconds").isNotEmpty() ? argAfter ("--seconds").getDoubleValue() : 20.0;
        const auto n = (int64_t) (rate * seconds);
        const double freqs[4] = { 110.0, 55.0, 440.0, 1000.0 };   // drums, bass, inst, vocal
        StemAudio::Ptr stems = new StemAudio();
        TrackAudio::Ptr audio = new TrackAudio();
        stems->sampleRate = audio->sampleRate = rate;
        stems->numFrames = audio->numFrames = n;
        audio->left.assign ((size_t) n, 0.0f);
        audio->right.assign ((size_t) n, 0.0f);
        for (int s = 0; s < 4; ++s)
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& v = stems->channels[(size_t) (s * 2 + ch)];
                v.resize ((size_t) n);
                for (int64_t i = 0; i < n; ++i)
                {
                    const float x = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * freqs[s] * (double) i / rate);
                    v[(size_t) i] = StemAudio::fromFloat (x);
                    (ch == 0 ? audio->left : audio->right)[(size_t) i] += x;
                }
            }
        stems->validFrames = n;
        const auto track = root.getChildFile ("Contents").getChildFile ("Test Artist").getChildFile (juce::String::fromUTF8 ("Tr\xc3\xa4" "ck one.wav"));
        track.getParentDirectory().createDirectory();
        {
            juce::WavAudioFormat wav;
            track.deleteFile();
            std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (track);
            auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (16));
            if (w == nullptr) return 2;
            const float* chans[] = { audio->left.data(), audio->right.data() };
            w->writeFromFloatArrays (chans, 2, (int) n);
        }
        juce::String reason;
        const auto res = XzStems::writePaged (root, track, *audio, *stems, reason);
        std::cout << (res == XzStems::Result::written ? "written" : res == XzStems::Result::skipped ? "skipped: " : "failed: ") << reason
                  << "\n" << track.getFullPathName() << std::endl;
        return res == XzStems::Result::written ? 0 : 1;
    }

    if (args.contains ("--separate"))
    {
        const auto input = juce::File (argAfter ("--separate"));
        const auto outDir = juce::File (argAfter ("--out"));
        AppSettings s;
        s.load();
        const auto model = argAfter ("--model").isNotEmpty() ? juce::File (argAfter ("--model")) : s.modelFile();

        AudioFileLoader loader;
        juce::String err;
        auto audio = loader.load (input, err);
        if (audio == nullptr) { std::cerr << "decode failed: " << err << std::endl; return 2; }

        DemucsSeparator sep;
        DemucsSeparator::Options o;
        o.modelFile = model;
        o.useGpu = ! args.contains ("--cpu");
        o.allowBestOnCpu = args.contains ("--allow-cpu-best");
        // --quality fast|better|high|best (models are looked for next to --model)
        const auto q = argAfter ("--quality").toLowerCase();
        o.quality = q == "best" ? StemQuality::best : q == "high" ? StemQuality::high : q == "better" ? StemQuality::better : StemQuality::fast;
        if (! sep.load (o, err)) { std::cerr << err << std::endl; return 3; }
        if (q.isNotEmpty() && stemQualityName (sep.getQuality()).toLowerCase() != q)
        { std::cerr << "quality " << q << " not available: " << sep.getBackendDescription() << std::endl; return 3; }
        std::cout << "backend: " << sep.getBackendDescription() << std::endl;

        StemAudio stems;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        if (! sep.separate (*audio, stems, [] (double p) { std::cout << "\rprogress " << juce::roundToInt (p * 100) << "%   " << std::flush; return true; }, err))
        {
            std::cerr << "\nseparation failed: " << err << std::endl;
            return 4;
        }
        const auto secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        std::cout << "\nseparated " << audio->lengthSeconds() << " s of audio in " << secs << " s" << std::endl;

        outDir.createDirectory();
        juce::WavAudioFormat wav;
        const char* names[] = { "drums", "bass", "other", "vocals" };
        for (int st = 0; st < 4; ++st)
        {
            juce::AudioBuffer<float> buf (2, (int) stems.numFrames);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < (int) stems.numFrames; ++i)
                    buf.setSample (ch, i, stems.channels[(size_t) (st * 2 + ch)][(size_t) i] * StemAudio::toFloat);
            std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (outDir.getChildFile (juce::String (names[st]) + ".wav"));
            auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (stems.sampleRate)
                                                .withNumChannels (2).withBitsPerSample (32)
                                                .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            if (w == nullptr) return 5;
            w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }
        return 0;
    }

    if (args.contains ("--deck-test"))
    {
        // Plays a track through a real Deck with one stem muted and writes what comes out.
        const auto input = juce::File (argAfter ("--deck-test"));
        const auto outFile = juce::File (argAfter ("--out"));
        AudioFileLoader loader;
        juce::String err;
        auto audio = loader.load (input, err);
        if (audio == nullptr) return 2;
        DemucsSeparator sep;
        DemucsSeparator::Options o;
        o.modelFile = juce::File (argAfter ("--model"));
        o.useGpu = false;
        if (! sep.load (o, err)) return 3;
        StemAudio::Ptr stems = new StemAudio();
        if (! sep.separate (*audio, *stems, nullptr, err)) return 4;

        TrackInfo info;
        LoadedTrack::Ptr t = new LoadedTrack();
        t->info = info; t->audio = audio; t->serial = 1;
        const double rate = argAfter ("--rate").isNotEmpty() ? argAfter ("--rate").getDoubleValue() : 44100.0;
        Deck deck (0);
        deck.prepare (rate, 512);
        Deck::Command c; c.cmd = Deck::Cmd::load; c.ptr = t.get(); deck.post (c);
        Deck::Command a; a.cmd = Deck::Cmd::attachStems; a.ptr = stems.get(); a.serial = 1; deck.post (a);
        const int mute = argAfter ("--mute").getIntValue();
        deck.post (Deck::Cmd::stemGain, 0.0, mute);
        deck.post (Deck::Cmd::play);

        const int total = (int) (audio->lengthSeconds() * rate) - 2048;
        juce::AudioBuffer<float> out (2, total);
        for (int pos = 0; pos < total; pos += 512)
        {
            const int n = juce::jmin (512, total - pos);
            deck.handleCommands();
            deck.process (out.getWritePointer (0) + pos, out.getWritePointer (1) + pos, n);
        }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (outFile);
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (32)
                                        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        w->writeFromAudioSampleBuffer (out, 0, total);
        return 0;
    }

    if (args.contains ("--export-test"))
    {
        // Imports a rekordbox USB export into a fresh Centrifuge library, edits a track (both versions), then
        // exports it again as a USB stick and as rekordbox XML, and reads the new stick back.
        const auto usb = juce::File (argAfter ("--export-test"));
        const auto out = juce::File (argAfter ("--out"));
        out.deleteRecursively();
        out.createDirectory();
        auto src = loadRekordboxUsbExport (usb);
        if (src == nullptr || ! src->error.isEmpty()) { std::cerr << "couldn't read " << usb.getFullPathName() << std::endl; return 2; }

        if (args.contains ("--fake-audio"))
        {
            // Test exports often come without their music: put 2 s of silence where each file should be.
            for (auto& t : src->tracks)
                if (! t.file.existsAsFile() && t.file != juce::File())
                {
                    t.file.getParentDirectory().createDirectory();
                    juce::WavAudioFormat wav;
                    std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (t.file));
                    if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (44100).withNumChannels (2).withBitsPerSample (16)))
                    {
                        juce::AudioBuffer<float> silence (2, 88200);
                        silence.clear();
                        w->writeFromAudioSampleBuffer (silence, 0, silence.getNumSamples());
                    }
                }
        }

        CentrifugeLibrary lib (out.getChildFile ("library.db"));
        auto res = lib.import (*src, nullptr, nullptr, {});
        std::cout << "imported " << res.added << " tracks" << std::endl;

        if (args.contains ("--add-music"))
        {
            // Music that isn't in rekordbox: a folder with a sub-folder of generated kick tracks, analysed by Centrifuge.
            auto music = out.getChildFile ("added").getChildFile ("Club Night");
            const std::pair<const char*, double> tracksToMake[] = { { "Test Artist - Kick 140.wav", 140.0 }, { "Sub/Other Artist - Kick 128.wav", 128.0 } };
            for (auto& [name, bpm] : tracksToMake)
            {
                auto f = music.getChildFile (name);
                f.getParentDirectory().createDirectory();
                const double sr = 44100.0, first = 0.25;
                juce::AudioBuffer<float> buf (2, (int) (sr * 40.0));
                buf.clear();
                for (int k = 0; first + k * 60.0 / bpm < 39.5; ++k)
                {
                    const int start = (int) std::lround ((first + k * 60.0 / bpm) * sr);
                    double ph = 0.0;
                    for (int i = 0; i < (int) (0.25 * sr) && start + i < buf.getNumSamples(); ++i)
                    {
                        const double t = i / sr;
                        ph += juce::MathConstants<double>::twoPi * (50.0 + 120.0 * std::exp (-t * 40.0)) / sr;
                        const float v = (float) (std::sin (ph) * std::exp (-t * 9.0) * 0.8);
                        buf.addSample (0, start + i, v);
                        buf.addSample (1, start + i, v);
                    }
                }
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f));
                if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (16)))
                    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
            }
            AudioFileLoader musicLoader;
            auto added = FileImporter::run (lib, musicLoader, { music }, {}, {});
            std::cout << "added " << added.added << " tracks, " << added.playlists << " playlists, " << added.failed << " failed" << std::endl;
            for (auto& t : lib.allTracks())
                if (t.source == "files")
                {
                    std::cout << "  " << t.info.displayArtistTitle() << "  " << juce::String (t.info.bpm, 2) << " BPM  key " << t.info.key
                              << "  beats " << t.rb.beats.size() << "  first " << juce::String (t.rb.beats.empty() ? -1.0 : t.rb.beats.front().timeSec, 3) << std::endl;
                    const double expect = t.info.title.contains ("140") ? 140.0 : 128.0;
                    if (std::abs (t.info.bpm - expect) > 0.01 || t.rb.beats.empty() || std::abs (t.rb.beats.front().timeSec - 0.25) > 0.01
                        || t.info.artist.isEmpty())
                    { std::cerr << "analysis of added music failed" << std::endl; return 7; }
                }
            if (added.added != 2 || added.playlists != 2) { std::cerr << "adding music failed" << std::endl; return 7; }
        }

        std::vector<ExportItem> items;
        std::map<juce::int64, int> itemOf;
        for (auto& t : lib.allTracks())
        {
            auto cf = t.cf;
            // Centrifuge-only additions that must not reach rekordbox's files
            cf.skips.push_back ({ 10.0, 12.0 });
            for (int i = 0; i < 12; ++i) { CuePoint c; c.startSec = 1.0 + i; c.colour = juce::Colour (0xff123456); c.comment = "Loc " + juce::String (i); cf.memory.push_back (c); }
            CuePoint h; h.type = CuePoint::Type::hot; h.hotIndex = 2; h.startSec = 3.5; h.colour = juce::Colour (0xff00e0ff); h.comment = juce::String::fromUTF8 ("D\xc3\xa9j\xc3\xa0 vu");
            cf.hot[2] = h;
            lib.saveVersions (t.id, nullptr, &cf);
            itemOf[t.id] = (int) items.size();
            items.push_back ({ t.info, cf.limitedForRekordbox(), cf, t.key });
        }
        ExportList root;
        root.folder = true;
        for (auto& p : lib.allPlaylists())
        {
            if (p.parent != 0) continue;
            ExportList l;
            l.name = p.name;
            l.folder = p.folder;
            for (auto id : p.tracks) l.items.push_back (itemOf[id]);
            root.children.push_back (l);
        }
        ExportList all;
        all.name = "All exported";
        for (int i = 0; i < (int) items.size(); ++i) all.items.push_back (i);
        root.children.push_back (all);

        AudioFileLoader loader;
        juce::String err, summary;
        auto stick = out.getChildFile ("stick");
        stick.createDirectory();
        if (! exportToUsb (stick, items, root, loader, {}, err, summary)) { std::cerr << "export failed: " << err << std::endl; return 3; }
        std::cout << summary << std::endl;
        if (! writeRekordboxXml (out.getChildFile ("rekordbox.xml"), items, root, err)) { std::cerr << err << std::endl; return 4; }

        auto back = loadRekordboxUsbExport (stick);
        if (back == nullptr || back->error.isNotEmpty() || back->tracks.size() != items.size())
        {
            std::cerr << "read-back failed: " << (back != nullptr ? back->error : juce::String()) << std::endl;
            return 5;
        }
        for (auto& t : back->tracks)
        {
            auto v = CentrifugeLibrary::readRekordboxVersion (t);
            int hot = 0;
            for (auto& c : v.hot) if (c) ++hot;
            std::cout << "  " << t.displayArtistTitle() << "  bpm " << t.bpm << "  beats " << v.beats.size() << "  hot " << hot
                      << "  memory " << v.memory.size() << "  file " << (t.file.existsAsFile() ? "ok" : "MISSING") << std::endl;
            if (v.memory.size() > (size_t) TrackVersion::rekordboxMaxMemoryCues || ! v.hot[2] || v.hot[2]->comment.isEmpty())
            { std::cerr << "cue round trip failed" << std::endl; return 6; }
        }
        std::cout << "playlists: " << back->playlists.children.size() << std::endl;
        std::cout << "export test passed" << std::endl;
        return 0;
    }

    if (args.contains ("--render-icon"))
    {
        OrbitLookAndFeel laf;
        const int size = 512;
        juce::Image img (juce::Image::ARGB, size, size, true);
        {
            juce::Graphics g (img);
            CentrifugeRotor::paintRotor (g, juce::Rectangle<float> (0, 0, (float) size, (float) size).reduced (8.0f), 0.4f, 0.0f, 1.0f, 0.0f, 0.0f);
        }
        juce::File out (argAfter ("--render-icon"));
        out.deleteFile();
        juce::FileOutputStream os (out);
        return juce::PNGImageFormat().writeImageToStream (img, os) ? 0 : 1;
    }

    if (args.contains ("--mp3delay"))
    {
        // --mp3delay <lame>: how many samples MP3 stems come back late (an encode with LAME, a decode with JUCE)
        juce::File lame (argAfter ("--mp3delay"));
        for (double sr : { 44100.0, 48000.0 })
        {
            juce::AudioBuffer<float> b (2, (int) sr * 3);
            b.clear();
            for (int ch = 0; ch < 2; ++ch) b.setSample (ch, 20000, 0.9f);
            auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cf_delay.mp3");
            f.deleteFile();
            {
                juce::LAMEEncoderAudioFormat fmt (lame);
                int q = 0;
                const auto opts = fmt.getQualityOptions();
                for (int i = 0; i < opts.size(); ++i) if (opts[i].contains ("320")) q = i;
                std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
                auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (16).withQualityOptionIndex (q));
                if (w == nullptr) { std::printf ("no writer\n"); return 1; }
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
            juce::MP3AudioFormat mp3;
            std::unique_ptr<juce::AudioFormatReader> r (mp3.createReaderFor (new juce::FileInputStream (f), true));
            if (r == nullptr) { std::printf ("no reader\n"); return 1; }
            juce::AudioBuffer<float> in (2, (int) r->lengthInSamples);
            r->read (&in, 0, in.getNumSamples(), 0, true, true);
            int peak = 0; float best = 0.0f;
            for (int i = 0; i < in.getNumSamples(); ++i) if (std::abs (in.getSample (0, i)) > best) { best = std::abs (in.getSample (0, i)); peak = i; }
            std::printf ("sr %.0f: in %d out %d (len %lld vs %d) delay %d\n", sr, 20000, peak, (long long) r->lengthInSamples, b.getNumSamples(), peak - 20000);
        }
        return 0;
    }

    if (args.contains ("--keytest"))
    {
        // --keytest <USB stick root, folder or audio files...>: Centrifuge's key detection (first method and v2)
        // against the key rekordbox (or the file's tag, or "[Am]" in the file name) gives each track.
        struct Item { juce::File file; juce::String expected; };
        std::vector<Item> items;
        for (int i = args.indexOf ("--keytest") + 1; i < args.size(); ++i)
        {
            const juce::File f (args[i].unquoted());
            if (f.isDirectory() && f.getChildFile ("PIONEER").isDirectory())
            {
                if (auto src = loadRekordboxUsbExport (f))
                    for (auto& t : src->tracks) if (t.key.isNotEmpty()) items.push_back ({ t.file, t.key });
            }
            else if (f.isDirectory())
            {
                for (auto& e : f.findChildFiles (juce::File::findFiles, true, "*.mp3;*.wav;*.flac;*.aif;*.aiff;*.m4a"))
                    items.push_back ({ e, {} });
            }
            else if (f.existsAsFile()) items.push_back ({ f, {} });
            if (args[i].startsWith ("--")) break;
        }
        auto relation = [] (const MusicalKey& a, const MusicalKey& b) -> int
        {
            if (! a.isValid() || ! b.isValid()) return 5;
            if (a.pitch == b.pitch && a.minor == b.minor) return 0;                                   // exact
            if (a.minor == b.minor && ((a.pitch - b.pitch + 12) % 12 == 7 || (b.pitch - a.pitch + 12) % 12 == 7)) return 1;   // a fifth
            if (a.minor != b.minor && (a.minor ? (a.pitch + 3) % 12 == b.pitch : (b.pitch + 3) % 12 == a.pitch)) return 2;  // relative
            if (a.pitch == b.pitch) return 3;                                                         // parallel (major / minor)
            return 4;
        };
        const char* names[] = { "exact", "fifth", "relative", "major/minor", "other", "no key" };
        // also written to a text file on the desktop (a Windows app has no console to print to), opened at the end
        juce::String report;
        auto out = [&report] (const juce::String& line) { std::printf ("%s\n", line.toRawUTF8()); std::fflush (stdout); report << line << "\n"; };
        out ("Centrifuge key test: " + juce::String ((int) items.size()) + " tracks");
        std::array<int, 6> v1 {}, v2 {}, v3 {}, v4 {};
        AudioFileLoader loader;
        for (auto& it : items)
        {
            auto expected = it.expected;
            if (expected.isEmpty()) expected = AudioTags::read (it.file).key;
            if (expected.isEmpty() && it.file.getFileName().contains ("[")) expected = it.file.getFileName().fromFirstOccurrenceOf ("[", false, false).upToFirstOccurrenceOf ("]", false, false);
            const auto want = MusicalKey::parse (expected);
            if (! want.isValid()) continue;
            juce::String err;
            auto audio = loader.load (it.file, err);
            if (audio == nullptr) continue;
            TrackAnalyser::Options o;
            const auto r = TrackAnalyser::analyse (*audio, o);
            const auto k1 = MusicalKey::parse (TrackAnalyser::detectKey (*audio)), k2 = MusicalKey::parse (r.key);
            const int r1 = relation (k1, want), r2 = relation (k2, want);
            ++v1[(size_t) r1]; ++v2[(size_t) r2];
            // stems: "<file>.stems/{drums,bass,inst,vocals}.wav" next to it, or the stem cache
            juce::String stemText;
            const auto stemDir = it.file.getSiblingFile (it.file.getFileName().upToLastOccurrenceOf (".", false, false) + ".stems");
            if (stemDir.isDirectory())
            {
                StemAudio::Ptr st = new StemAudio();
                st->sampleRate = audio->sampleRate;
                st->numFrames = audio->numFrames;
                const char* sn[] = { "drums", "bass", "inst", "vocals" };
                bool ok = true;
                for (int k = 0; k < 4 && ok; ++k)
                {
                    juce::String e2;
                    auto sa = loader.load (stemDir.getChildFile (juce::String (sn[k]) + ".wav"), e2);
                    ok = sa != nullptr && sa->numFrames >= st->numFrames;
                    if (! ok) break;
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& dst = st->channels[(size_t) k * 2 + (size_t) c];
                        dst.resize ((size_t) st->numFrames);
                        const auto& src = c == 0 ? sa->left : sa->right;
                        for (int64_t i = 0; i < st->numFrames; ++i) dst[(size_t) i] = StemAudio::fromFloat (src[(size_t) i] * 0.5f);
                    }
                }
                if (ok)
                {
                    st->validFrames = st->numFrames;
                    const auto k3 = MusicalKey::parse (TrackAnalyser::detectKeyV2 (*audio, st.get(), &r.beats));
                    const int r3 = relation (k3, want);
                    ++v3[(size_t) r3];
                    stemText = "  v2+stems " + k3.toString() + " (" + names[r3] + ")";
                }
            }
            const auto k4 = MusicalKey::parse (TrackAnalyser::detectKeyAlt (*audio));
            const int r4 = relation (k4, want);
            ++v4[(size_t) r4];
            out (it.file.getFileName().substring (0, 50).paddedRight (' ', 50) + "  want " + want.toString().paddedRight (' ', 4)
                 + "  first " + k1.toString().paddedRight (' ', 4) + " (" + names[r1] + ")  v2 " + k2.toString().paddedRight (' ', 4)
                 + " (" + names[r2] + ")  second opinion " + k4.toString().paddedRight (' ', 4) + " (" + names[r4] + ")" + stemText);
        }
        auto row = [] (const char* name, const std::array<int, 6>& v)
        {
            return juce::String (name).paddedRight (' ', 10) + juce::String (v[0]).paddedLeft (' ', 5) + juce::String (v[1]).paddedLeft (' ', 7)
                 + juce::String (v[2]).paddedLeft (' ', 10) + juce::String (v[3]).paddedLeft (' ', 9) + juce::String (v[4]).paddedLeft (' ', 7);
        };
        out ({});
        out ("          exact  fifth  relative  maj/min  other");
        out (row ("first", v1));
        out (row ("v2", v2));
        out (row ("2nd opin.", v4));
        if (v3[0] + v3[1] + v3[2] + v3[3] + v3[4] > 0) out (row ("v2+stems", v3));
        if (! args.contains ("--no-report"))
        {
            const auto f = juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("Centrifuge key test.txt");
            if (f.replaceWithText (report)) f.startAsProcess();
        }
        return 0;
    }

    if (args.contains ("--analyse"))
    {
        // --analyse <audio file> [--min 88 --max 175]: tags and Centrifuge's own BPM / grid / key analysis
        const juce::File f (argAfter ("--analyse"));
        const auto tags = AudioTags::read (f);
        std::cout << "tags: " << tags.artist << " - " << tags.title << " | " << tags.album << " | " << tags.genre
                  << " | bpm " << tags.bpm << " | key " << tags.key << " | label " << tags.label << std::endl;
        AudioFileLoader loader;
        juce::String err;
        auto audio = loader.load (f, err);
        if (audio == nullptr) { std::cerr << err << std::endl; return 1; }
        TrackAnalyser::Options o;
        if (argAfter ("--min").isNotEmpty()) o.minBpm = argAfter ("--min").getDoubleValue();
        if (argAfter ("--max").isNotEmpty()) o.maxBpm = argAfter ("--max").getDoubleValue();
        if (! args.contains ("--no-hint")) o.hintBpm = tags.bpm;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = TrackAnalyser::analyse (*audio, o);
        std::cout << "analysis: " << juce::String (r.bpm, 3) << " BPM, key " << r.key << ", first beat "
                  << juce::String (r.beats.empty() ? 0.0 : r.beats.front().timeSec, 4) << " s (beat " << (r.beats.empty() ? 0 : r.beats.front().beatInBar)
                  << " of 4), beats " << r.beats.size() << ", confidence " << juce::String (r.confidence, 2)
                  << ", " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) << " s" << std::endl;
        if (args.contains ("--beats"))
            for (size_t i = 0; i < juce::jmin<size_t> (r.beats.size(), 16); ++i)
                std::cout << "  " << juce::String (r.beats[i].timeSec, 4) << " " << r.beats[i].beatInBar << std::endl;
        return 0;
    }

    if (args.contains ("--dump"))
    {
        const auto path = juce::File (argAfter ("--dump"));
        std::unique_ptr<LibrarySource> src = path.hasFileExtension ("db")
            ? loadRekordboxMasterDb (path, path.getFileName().contains ("unlocked") ? juce::String() : defaultRekordboxDbKey())
            : loadRekordboxUsbExport (path);
        std::cout << src->name << "  tracks: " << src->tracks.size() << "  error: " << src->error << std::endl;
        for (auto& t : src->tracks)
        {
            auto a = loadAnlz (t.anlzBase);
            std::cout << "  " << t.artist << " - " << t.title << "  " << t.bpm << " BPM  key " << t.key
                      << "  beats " << a.beats.size() << "  cues " << a.cues.size()
                      << "  waveform " << a.waveforms.numDetailEntries() << std::endl;
            if (args.contains ("--beats"))
                for (size_t i = 0; i < juce::jmin<size_t> (a.beats.size(), 6); ++i)
                    std::cout << "    " << juce::String (a.beats[i].timeSec, 4) << " " << a.beats[i].beatInBar << " " << a.beats[i].bpm << std::endl;
        }
        return 0;
    }

    return -1;
}

//==============================================================================
/** Scripted demo used to produce the preview screenshots. */
class DemoDriver : private juce::Timer
{
public:
    DemoDriver (MainComponent& m, juce::File shotsDir) : main (m), shots (std::move (shotsDir)) { startTimer (250); }

private:
    void timerCallback() override
    {
        ++ticks;
        auto sources = main.getLibrary().getSources();
        std::shared_ptr<const LibrarySource> usb;
        for (auto& s : sources)
            if (s->kind == LibrarySource::Kind::usbExport && s->tracks.size() >= 2) usb = s;

        if (stage == 0 && usb != nullptr)
        {
            main.importForDemo (usb);   // copy the stick into Centrifuge's library first
            stage = -1;
        }
        else if (stage == -1 && ! main.isImporting())
        {
            auto lib = main.librarySource (1);
            if (lib == nullptr || lib->tracks.size() < 2) return;
            main.getBrowser().refreshSources();
            main.getLoader().load (0, lib->tracks[0], false);
            main.getLoader().load (1, lib->tracks[1], false);
            stage = 1;
            t0 = ticks;
        }
        else if (stage == 1 && main.getEngine().deck (0).state().trackSerial.load() != 0 && main.getEngine().deck (1).state().trackSerial.load() != 0)
        {
            auto& d1 = main.getEngine().deck (0);
            auto& d2 = main.getEngine().deck (1);
            ActionValue down; down.down = true;
            ActionValue up;   up.down = false;
            auto press = [&] (const char* id) { main.getRouter().perform (id, down); main.getRouter().perform (id, up); };

            // Deck 1: hot cues, key +2, inst turned down and vocals muted (shows the stem lane following the knobs)
            for (auto [sec, cue] : { std::pair<double, const char*> { 2.0, "d1.hotcue.1" }, { 9.9, "d1.hotcue.2" }, { 30.4, "d1.hotcue.3" } })
            {
                d1.post (Deck::Cmd::seekSeconds, sec);
                press (cue);
            }
            d1.post (Deck::Cmd::seekSeconds, 6.5);
            press ("d1.master");
            press ("d1.key.up"); press ("d1.key.up");
            ActionValue inst; inst.value = 0.25;
            main.getRouter().perform ("d1.stem.3.volume", inst);
            press ("d1.stem.4.mute");

            // Deck 1: a locator and a skip (folded out of the waveform)
            d1.post (Deck::Cmd::locatorAdd, 4.0);
            d1.post (Deck::Cmd::locatorAdd, 14.0);
            d1.post2 (Deck::Cmd::skipAdd, 11.0, 13.0);
            d1.post (Deck::Cmd::locatorColour, (double) 0xff28e214u, 1);

            // Deck 2: synced, key sync to deck 1, a 4-beat loop running
            for (auto [sec, cue] : { std::pair<double, const char*> { 8.1, "d2.hotcue.1" }, { 40.0, "d2.hotcue.4" } })
            {
                d2.post (Deck::Cmd::seekSeconds, sec);
                press (cue);
            }
            d2.post (Deck::Cmd::seekSeconds, 33.0);
            d2.post (Deck::Cmd::setPitchFader, 0.4);
            press ("d2.sync");
            press ("d2.play");
            ActionValue g; g.down = true;
            main.getRouter().perform ("d2.grid.mode", g);
            stage = 2;
            t0 = ticks;
        }
        else if (stage == 2 && ticks - t0 == 12)
        {
            ActionValue down; down.down = true;
            main.getRouter().perform ("d2.beatloop.4", down);
            main.getRouter().perform ("d2.key.sync", down);
        }
        else if (stage == 2 && ticks - t0 > 24
                 && (main.getEngine().deck (0).state().stemsProgress.load() > 0.08f || ticks - t0 > 400))
        {
            snap ("01_play.png");
            stage = 3;
            t0 = ticks;
        }
        else if (stage == 3 && ticks - t0 > 8)
        {
            snap ("02_play_later.png");
            if (shots != juce::File())
            {
                // what laptop mode streams to the XDJ-XZ screen
                auto img = main.renderXzScreen();
                juce::FileOutputStream os (shots.getChildFile ("02b_xz_screen.png"));
                os.setPosition (0);
                os.truncate();
                juce::PNGImageFormat().writeImageToStream (img, os);
                // and its browser: the tree (focus on it), the columns, the info
                main.getXzView().setBrowserShown (true);
                main.renderXzScreen();
                main.getXzView().touch (1, 570, 40); main.getXzView().touch (2, 570, 40);   // INFO
                main.getBrowser().back();
                auto bimg = main.renderXzScreen();
                juce::FileOutputStream bos (shots.getChildFile ("02c_xz_browser.png"));
                bos.setPosition (0);
                bos.truncate();
                juce::PNGImageFormat().writeImageToStream (bimg, bos);
                main.getXzView().touch (1, 570, 40); main.getXzView().touch (2, 570, 40);
                main.getBrowser().focusTracks();
                main.getXzView().setBrowserShown (false);
                main.getXzView().startSplash();
                main.renderXzScreen();
                juce::Thread::sleep (2500);
                auto simg = main.renderXzScreen();
                juce::FileOutputStream sos (shots.getChildFile ("02d_xz_splash.png"));
                sos.setPosition (0);
                sos.truncate();
                juce::PNGImageFormat().writeImageToStream (simg, sos);
                {
                    // the PREP keyboard, and a quick listen to each sound (offline)
                    KeysPanel kp (main.getEngine());
                    kp.setSize (720, 210);
                    auto kimg = kp.createComponentSnapshot (kp.getLocalBounds(), true, 1.0f);
                    juce::FileOutputStream kos (shots.getChildFile ("10_keys.png"));
                    kos.setPosition (0); kos.truncate();
                    juce::PNGImageFormat().writeImageToStream (kimg, kos);
                    KeySynth ks;
                    ks.prepare (44100.0);
                    for (int p = 0; p < KeySynth::numPresets; ++p)
                    {
                        ks.setPreset (p);
                        ks.getKeyboardState().noteOn (1, 36, 0.9f);
                        std::vector<float> l (44100, 0.0f), r (44100, 0.0f);
                        for (int pos = 0; pos < 44100; pos += 512) ks.renderAdd (l.data() + pos, r.data() + pos, juce::jmin (512, 44100 - pos));
                        ks.getKeyboardState().noteOff (1, 36, 0.0f);
                        float peak = 0.0f; bool bad = false;
                        for (float x : l) { peak = juce::jmax (peak, std::abs (x)); bad = bad || ! std::isfinite (x); }
                        std::printf ("keys %s: peak %.3f %s\n", KeySynth::presetName (p), peak, bad ? "NaN!" : "");
                        std::vector<float> tail (8192, 0.0f), tr (8192, 0.0f);
                        for (int pos = 0; pos < 8192; pos += 512) ks.renderAdd (tail.data() + pos, tr.data() + pos, 512);
                    }
                    std::printf ("keys name C2 %d F#3 %d Eb1 %d none %d\n", KeySynth::noteFromName ("Bass C2"), KeySynth::noteFromName ("Pluck F#3"),
                                 KeySynth::noteFromName ("x_Eb1"), KeySynth::noteFromName ("kick"));
                }
            }
            main.showPage (1);
            stage = 4;
            t0 = ticks;
        }
        else if (stage == 4 && ticks - t0 > 4)
        {
            snap ("03_mapping.png");
            main.showPage (3);
            stage = 5;
            t0 = ticks;
        }
        else if (stage == 5 && ticks - t0 > 4)
        {
            snap ("04_settings.png");
            main.showPage (2);
            stage = 6;
            t0 = ticks;
        }
        else if (stage == 6 && ticks - t0 > 4)
        {
            snap ("05_stems.png");
            main.showPage (0);
            main.getWaves().setVisibleSeconds (2.0);
            stage = 7;
            t0 = ticks;
        }
        else if (stage == 7 && ticks - t0 > 4)
        {
            snap ("06_zoomed.png");
            main.getWaves().setVisibleSeconds (10.0);
            main.showAbout();
            stage = 8;
            t0 = ticks;
        }
        else if (stage == 8 && ticks - t0 > 8)
        {
            snap ("07_about.png");
            stage = 9;
            t0 = ticks;
        }
        else if (stage == 9 && ticks - t0 > 2)
        {
            if (auto* a = main.findChildWithID ("about")) a->setVisible (false);
            main.openExport (true);
            stage = 10;
            t0 = ticks;
        }
        else if (stage == 10 && ticks - t0 > 4)
        {
            snap ("08_export.png");
            main.closeExport();
            // PREP: one track with a locator, a cycle loop locator, an active loop locator and a skip
            auto lib = main.librarySource (1);
            main.getLoader().load (AudioEngine::prepDeck, lib->tracks[0], false);
            stage = 11;
            t0 = ticks;
        }
        else if (stage == 11 && main.getEngine().deck (AudioEngine::prepDeck).state().trackSerial.load() != 0)
        {
            auto& p = main.getEngine().deck (AudioEngine::prepDeck);
            p.post (Deck::Cmd::locatorAdd, 3.0);
            p.post (Deck::Command { Deck::Cmd::locatorLoopAdd, 8.0, 0, true, nullptr, 0, 10.0 });
            p.post (Deck::Command { Deck::Cmd::locatorLoopAdd, 20.0, 0, true, nullptr, 0, 22.0 });
            p.post2 (Deck::Cmd::skipAdd, 14.0, 16.0);
            p.post (Deck::Cmd::seekSeconds, 6.0);
            stage = 12;
            t0 = ticks;
        }
        else if (stage == 12 && ticks - t0 == 2)
        {
            auto& p = main.getEngine().deck (AudioEngine::prepDeck);
            p.post (Deck::Command { Deck::Cmd::locatorLoopMode, 2.0, 1, true, nullptr, 0, 4.0 });
            p.post (Deck::Command { Deck::Cmd::locatorLoopMode, 1.0, 2, true, nullptr, 0, 4.0 });
            p.post (Deck::Cmd::hotCue, 0.0, 0, true);
            p.post (Deck::Cmd::hotCue, 0.0, 0, false);
            main.showPage (4);
        }
        else if (stage == 12 && ticks - t0 > 8)
        {
            snap ("09_prep.png");
            stage = 13;
            t0 = ticks;
            if (juce::SystemStats::getEnvironmentVariable ("CENTRIFUGE_STRESS", {}).isNotEmpty())
            {
                auto& ed = main.getEditor();
                const char* nm[] = { "Intro", "Chorus", "Verse", "Outro" };
                for (int i = 0; i < 4; ++i) ed.addPhrase (AudioEngine::prepDeck, 4.0 + i * 15.0, nm[i], juce::Colours::red);
                stage = 20;
            }
            if (juce::SystemStats::getEnvironmentVariable ("CENTRIFUGE_EDITTEST", {}).isNotEmpty())
            {
                // Phrases Intro 0-15, Verse 15-30, Chorus 30-45, Outro 45-end; play Intro, Chorus x2, Verse, Outro;
                // a stem locator (vocals off) at 20 s, a note at 8 s, and an edit version saved.
                auto& ed = main.getEditor();
                const int D = AudioEngine::prepDeck;
                while (! ed.phrases (D).empty()) ed.deletePhrase (D, (int) ed.phrases (D).size() - 1);
                const char* nm[] = { "Intro", "Verse", "Chorus", "Outro" };
                for (int i = 0; i < 4; ++i) ed.addPhrase (D, i * 15.0, nm[i], defaultPhraseColour (nm[i]));
                ed.setPhraseRepeats (D, 2, 2);
                ed.setArrangement (D, { 0.0, 30.0, 15.0, 30.0, 45.0 });   // the Chorus duplicated after the Verse
                while (! ed.notes (D).empty()) ed.deleteNote (D, (int) ed.notes (D).size() - 1);
                StemLocator l; l.timeSec = 20.0; l.target = { -1, -1, -1, 0 }; l.fadeBeats = 4.0f;
                ed.addStemLocator (D, l);
                NoteLocator n; n.timeSec = 8.0; n.text = "Big vocal sample - kill the bass"; ed.addNote (D, n);
                ed.saveEditVersion (D, "Extended");
                const auto& map = main.getEngine().deck (D).getLoadedTrack()->playMap;
                std::printf ("playmap:");
                for (auto& sgm : map) std::printf (" [%.1f-%.1f]", sgm.start, sgm.end);
                std::printf ("\nedit versions: %d active '%s' modified %d\n", (int) ed.editVersionNames (D).size(),
                             ed.activeEdit (D).toRawUTF8(), (int) ed.activeEditModified (D));
                std::fflush (stdout);
                main.getEngine().deck (D).post (Deck::Cmd::seekSeconds, 14.0);
                main.getEngine().deck (D).post (Deck::Cmd::play);
                stage = 30;
                t0 = ticks;
            }
        }
        else if (stage >= 30 && stage < 40)
        {
            // Seek just before each join and check where playback goes (ticks are 250 ms)
            auto& d = main.getEngine().deck (AudioEngine::prepDeck);
            const auto& st = d.state();
            const int k = ticks - t0;
            if (k == 8)
            {
                std::printf ("after Intro end -> %.2f s (seg %d), expect ~31 (Chorus, seg 1)\n", st.positionSec.load(), st.playSegment.load());
                d.post (Deck::Cmd::seekSeconds, 44.0);
            }
            if (k == 16)
            {
                std::printf ("after Chorus pass 1 -> %.2f s (seg %d), expect ~31 (Chorus again, seg 2)\n", st.positionSec.load(), st.playSegment.load());
                d.post (Deck::Cmd::seekSeconds, 44.0);
            }
            if (k == 24)
            {
                std::printf ("after Chorus pass 2 -> %.2f s (seg %d), expect ~16 (Verse, seg 3)\n", st.positionSec.load(), st.playSegment.load());
                d.post (Deck::Cmd::seekSeconds, 21.0);
            }
            if (k == 32)
            {
                std::printf ("stem auto at %.2f s: vocals %.2f (expect 0 after the 4-beat fade)\n", st.positionSec.load(), st.stemAuto[3].load());
                d.post (Deck::Cmd::seekSeconds, 7.5);
                d.post (Deck::Cmd::pause);
                main.showPage (4);
            }
            if (k == 34)
            {
                // PREP with two lanes (stems a row each over 3-band), the overview grid on
                main.getPrep().getWaves().setLanes (0, { 4, 0 }, { 1, 0 });
                main.getPrep().setOverviewGrid (true);
                main.getDeckPanel (0).setOverviewGrid (true);
                main.getDeckPanel (1).setOverviewGrid (true);
                main.demoSettings ([] (AppSettings& s) { s.phraseBrackets = true; s.phraseBars = 64; s.browserInfoOpen = true; });
                main.getBrowser().setInfoPanelOpen (true);
                main.getPrep().showListTab (2);
            }
            if (k == 36)
            {
                snap ("11_edits_prep.png");
                main.showPage (0);
                // deck 1 has the same track: it takes the edits too; stand just before the note
                main.getEngine().deck (0).post (Deck::Cmd::pause);
                main.getEngine().deck (0).post (Deck::Cmd::seekSeconds, 7.6);
            }
            if (k == 37) main.demoSettings ([] (AppSettings& s) { s.phraseBars = 16; });
            if (k == 39) snap ("12_edits_play.png");
            if (k == 40) main.showPage (2);
            if (k == 42)
            {
                snap ("13_stems_page.png");
                main.showPage (3);
                // stem controls as filters: bass low-pass at 30 %, inst high-pass at 50 %, while deck 1 plays
                auto& d0 = main.getEngine().deck (0);
                if (! d0.state().stemsReady.load())
                    if (auto t = d0.getLoadedTrack(); t != nullptr && t->audio != nullptr)
                    {
                        // no separation model here: fake stems (a quarter of the track each) so the filters run
                        StemAudio::Ptr st = new StemAudio();
                        st->sampleRate = t->audio->sampleRate;
                        st->numFrames = t->audio->numFrames;
                        for (int c = 0; c < 8; ++c)
                        {
                            auto& src = (c % 2) == 0 ? t->audio->left : t->audio->right;
                            st->channels[(size_t) c].resize ((size_t) st->numFrames);
                            for (int64_t i = 0; i < st->numFrames; ++i) st->channels[(size_t) c][(size_t) i] = StemAudio::fromFloat (src[(size_t) i] * 0.25f);
                        }
                        st->validFrames = st->numFrames;
                        main.getEngine().getReleasePool().add (st);
                        d0.setStemsForUi (st);
                        Deck::Command sc;
                        sc.cmd = Deck::Cmd::attachStems;
                        sc.ptr = st.get();
                        sc.serial = d0.state().trackSerial.load();
                        d0.post (sc);
                    }
                d0.post (Deck::Cmd::stemMode, 1.0, 1);
                d0.post (Deck::Cmd::stemMode, 2.0, 2);
                d0.post (Deck::Cmd::stemGain, 0.3, 1);
                d0.post (Deck::Cmd::stemGain, 0.5, 2);
                d0.post (Deck::Cmd::seekSeconds, 20.0);
                d0.post (Deck::Cmd::play);
            }
            if (k == 44) snap ("14_settings.png");
            if (k == 52)
            {
                const auto& st = main.getEngine().deck (0).state();
                std::printf ("filter test: pos %.2f stems %d modes %d %d %d %d peak %.3f %.3f\n", st.positionSec.load(), (int) st.stemsReady.load(),
                             st.stemMode[0].load(), st.stemMode[1].load(), st.stemMode[2].load(), st.stemMode[3].load(), st.peakL.load(), st.peakR.load());
                main.showPage (0);
                main.getEngine().deck (0).post (Deck::Cmd::pause);
                main.getEngine().deck (0).post (Deck::Cmd::seekSeconds, 0.6);   // the grid carries on before the track starts
            }
            if (k == 54) snap ("15_play_filters.png");
            // --- vinyl: brake, backspin, slip ------------------------------------------
            auto& d0 = main.getEngine().deck (0);
            const auto& vs = d0.state();
            if (k == 56)
            {
                d0.post (Deck::Cmd::vinylTime, 1.0, 0);
                d0.post (Deck::Cmd::vinylBrakeSet, 1.0, 0);
                d0.post (Deck::Cmd::seekSeconds, 30.0);
                d0.post (Deck::Cmd::play);
            }
            if (k == 60) { vt = vs.positionSec.load(); d0.post (Deck::Cmd::playToggle); }
            if (k == 61) std::printf ("brake: braking %d shown playing %d\n", (int) vs.braking.load(), (int) vs.playing.load());
            if (k == 66) std::printf ("brake: moved %.2f s after pause (expect ~0.5), braking %d\n", vs.positionSec.load() - vt, (int) vs.braking.load());
            if (k == 68)
            {
                d0.post (Deck::Cmd::vinylBrakeSet, 0.0, 0);
                d0.post (Deck::Cmd::play);
                d0.post (Deck::Cmd::jogTouch, 0.0, 0, true);
            }
            if (k == 69) { vt = vs.positionSec.load(); d0.post (Deck::Cmd::jogTurn, -400.0); }
            if (k == 70) { d0.post (Deck::Cmd::jogTurn, -400.0); d0.post (Deck::Cmd::jogTouch, 0.0, 0, false); d0.post (Deck::Cmd::jogTurn, -300.0); }
            if (k == 71) std::printf ("backspin: moved %.2f s (expect negative)\n", vs.positionSec.load() - vt);
            if (k == 74) std::printf ("backspin: playing again %d\n", (int) vs.playing.load());
            if (k == 76)
            {
                d0.post (Deck::Cmd::slipSet, 1.0);
                vt = vs.positionSec.load();
                d0.post (Deck::Cmd::beatLoop, 1.0);
            }
            if (k == 84)
            {
                std::printf ("slip: slipping %d, pos %.2f\n", (int) vs.slipping.load(), vs.positionSec.load() - vt);
                d0.post (Deck::Cmd::loopOff);
            }
            if (k == 85)
            {
                std::printf ("slip: after loop off %.2f s on (expect ~2.25)\n", vs.positionSec.load() - vt);
                {
                    // export PREP's edited track (play map: Intro, Chorus x2, Verse, Chorus copy, Outro)
                    auto pt = main.getEngine().deck (AudioEngine::prepDeck).getLoadedTrack();
                    juce::String err;
                    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile ("export-test");
                    const bool ok = EditExporter::render (pt, nullptr, out, false, nullptr, err);
                    std::unique_ptr<juce::AudioFormatReader> rd (juce::WavAudioFormat().createReaderFor (new juce::FileInputStream (out.withFileExtension ("wav")), true));
                    std::printf ("export: ok %d %s length %.1f s (track %.1f s)\n", (int) ok, err.toRawUTF8(),
                                 rd != nullptr ? (double) rd->lengthInSamples / rd->sampleRate : -1.0, pt != nullptr ? pt->audio->lengthSeconds() : -1.0);
                }
                {
                    // combos and short / long presses
                    auto& midi = main.getMidi();
                    auto saved = midi.getBindings();
                    auto mk = [] (const char* id, int note, int mod, int press)
                    {
                        MidiBinding b; b.actionId = id; b.channel = 1; b.type = MidiBinding::Type::note; b.number = note;
                        if (mod >= 0) { b.modType = MidiBinding::Type::note; b.modChannel = 1; b.modNumber = mod; }
                        b.press = press;
                        return b;
                    };
                    midi.setBindings ({ mk ("d2.quantize", 0x10, -1, 1), mk ("d2.keylock", 0x10, -1, 2),
                                        mk ("d2.master", 0x11, -1, 0), mk ("d2.slip", 0x11, 0x20, 0) });
                    const auto& s2 = main.getEngine().deck (1).state();
                    auto on = [&] (int n) { midi.injectForTest (juce::MidiMessage::noteOn (1, n, (juce::uint8) 127)); };
                    auto off = [&] (int n) { midi.injectForTest (juce::MidiMessage::noteOff (1, n)); };
                    auto settle = [&] { for (int i = 0; i < 5; ++i) { juce::Thread::sleep (20); } };
                    const bool q0 = s2.quantize.load(), k0 = s2.keyLock.load(), sl0 = s2.slip.load();
                    on (0x10); off (0x10); settle();                                                  // short
                    on (0x10); juce::Thread::sleep (700); midi.pollForTest(); off (0x10); settle();   // long
                    on (0x20); on (0x11); off (0x11); off (0x20); settle();                           // SHIFT + 0x11
                    juce::Thread::sleep (100);
                    std::printf ("midi: short->quantize %d, long->keylock %d, combo->slip %d (each 1 = changed)\n",
                                 (int) (s2.quantize.load() != q0), (int) (s2.keyLock.load() != k0), (int) (s2.slip.load() != sl0));
                    // stem controls by MIDI on both decks (a knob turned fully down mutes the matrix's stem)
                    auto cc = [] (const char* id, int ch, int n)
                    {
                        MidiBinding b; b.actionId = id; b.channel = ch; b.type = MidiBinding::Type::cc; b.number = n;
                        b.mode = MidiBinding::Mode::absolute7;
                        return b;
                    };
                    midi.setBindings ({ cc ("d1.stemctl.A.knob", 1, 20), cc ("d2.stemctl.A.knob", 2, 20),
                                        cc ("d1.stemctl.B.volume", 1, 21), cc ("d2.stemctl.B.volume", 2, 21) });
                    for (int ch = 1; ch <= 2; ++ch)
                    {
                        midi.injectForTest (juce::MidiMessage::controllerEvent (ch, 20, 40));
                        midi.injectForTest (juce::MidiMessage::controllerEvent (ch, 20, 0));
                        midi.injectForTest (juce::MidiMessage::controllerEvent (ch, 21, 40));
                        midi.injectForTest (juce::MidiMessage::controllerEvent (ch, 21, 0));
                    }
                    settle(); juce::Thread::sleep (100);
                    for (int d = 0; d < 2; ++d)
                    {
                        const auto& st = main.getEngine().deck (d).state();
                        std::printf ("stems midi deck %d: gains %.2f %.2f %.2f %.2f\n", d + 1, st.stemGain[0].load(), st.stemGain[1].load(),
                                     st.stemGain[2].load(), st.stemGain[3].load());
                    }
                    { ActionValue r; r.down = true; main.getRouter().perform ("d1.stem.reset", r); main.getRouter().perform ("d2.stem.reset", r); }
                    midi.setBindings (saved);
                }
                std::printf ("edit test done\n"); std::fflush (stdout); stage = 40;
            }
        }
        else if (stage == 20)
        {
            // what dragging the custom colour picker with three phrases selected does
            auto& ed = main.getEditor();
            for (int round = 0; round < 15; ++round)
                for (int i = 0; i < 3; ++i)
                {
                    if (i >= (int) ed.phrases (AudioEngine::prepDeck).size()) continue;
                    auto p = ed.phrases (AudioEngine::prepDeck)[(size_t) i];
                    p.colour = juce::Colour::fromHSV ((float) ((ticks * 15 + round) % 100) / 100.0f, 0.8f, 0.9f, 1.0f);
                    ed.setPhrase (AudioEngine::prepDeck, i, p);
                }
            if (ticks - t0 > 60) { std::printf ("stress ok\n"); std::fflush (stdout); snap ("10_stress.png"); stage = 21; }
        }
    }

    void snap (const juce::String& name)
    {
        if (shots == juce::File()) return;
        shots.createDirectory();
        auto img = main.createComponentSnapshot (main.getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (shots.getChildFile (name));
        os.setPosition (0);
        os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    }

    MainComponent& main;
    juce::File shots;
    int ticks = 0, stage = 0, t0 = 0;
    double vt = 0.0;
};

//==============================================================================
class OrbitApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Centrifuge"; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& commandLine) override
    {
       #if JUCE_WINDOWS
        SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX); // no "insert disk" pop-ups while scanning drives
       #endif

        const auto args = juce::StringArray::fromTokens (commandLine, true);
        const int cli = runCommandLine (args);
        if (cli >= 0)
        {
            setApplicationReturnValue (cli);
            quit();
            return;
        }

        if (args.contains ("--demo"))
        {
            const int i = args.indexOf ("--demo");
            AppSettings s;
            s.load();
            s.usbRoots.addIfNotAlreadyThere (args[i + 1].unquoted());
            s.save();
        }

        juce::Desktop::getInstance().setScreenSaverEnabled (false);
        window = std::make_unique<MainWindow>();

        if (args.contains ("--demo"))
        {
            const int i = args.indexOf ("--shots");
            demo = std::make_unique<DemoDriver> (*window->main, i >= 0 ? juce::File (args[i + 1].unquoted()) : juce::File());
        }
    }

    void shutdown() override
    {
        demo.reset();
        window.reset();
        juce::Desktop::getInstance().setScreenSaverEnabled (true);
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow() : DocumentWindow ("Centrifuge", Theme::bg, DocumentWindow::allButtons)
        {
            main = new MainComponent();
            setUsingNativeTitleBar (true);
            setContentOwned (main, false);
            setResizable (true, true);
            setResizeLimits (1100, 640, 10000, 10000);
            // fits the screen at any resolution / Windows scaling (1080p at 125 % is only 1536 x 864 points),
            // then where you left it last time
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            const auto area = display != nullptr ? display->userArea : juce::Rectangle<int> (0, 0, 1600, 940);
            centreWithSize (juce::jmin (1600, area.getWidth() - 20), juce::jmin (940, area.getHeight() - 40));
            if (const auto st = main->getWindowState(); st.isNotEmpty()) restoreWindowStateFromString (st);
            setVisible (true);
        }
        void closeButtonPressed() override
        {
            main->setWindowState (getWindowStateAsString());
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
        MainComponent* main = nullptr;
    };

private:
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<DemoDriver> demo;
};

} // namespace orbit

START_JUCE_APPLICATION (orbit::OrbitApplication)
