#include "App/MainComponent.h"
#include "Library/RekordboxMasterDb.h"
#include "Library/RekordboxPdb.h"
#include "Library/Anlz.h"
#include "Stems/DemucsSeparator.h"
#include "Library/CentrifugeLibrary.h"
#include "Export/Exporters.h"
#include "Audio/TrackAnalyser.h"
#include "Library/TagReader.h"
#include "Library/FileImporter.h"

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
            stage = 11;
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
            setResizeLimits (1100, 680, 10000, 10000);
            centreWithSize (1600, 940);
            setVisible (true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
        MainComponent* main = nullptr;
    };

private:
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<DemoDriver> demo;
};

} // namespace orbit

START_JUCE_APPLICATION (orbit::OrbitApplication)
