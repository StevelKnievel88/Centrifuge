#include "App/MainComponent.h"
#include "Library/RekordboxMasterDb.h"
#include "Library/RekordboxPdb.h"
#include "Library/Anlz.h"
#include "Stems/DemucsSeparator.h"

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
        if (! sep.load (o, err)) { std::cerr << err << std::endl; return 3; }
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
            main.getLoader().load (0, usb->tracks[0], false);
            main.getLoader().load (1, usb->tracks[1], false);
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

            // A few hot cues on deck 1, then play from the drop
            for (auto [sec, cue] : { std::pair<double, const char*> { 15.2, "d1.hotcue.1" }, { 30.3, "d1.hotcue.2" }, { 60.4, "d1.hotcue.3" }, { 90.5, "d1.hotcue.5" } })
            {
                d1.post (Deck::Cmd::seekSeconds, sec);
                press (cue);
            }
            d1.post (Deck::Cmd::seekSeconds, 58.0);
            d1.post (Deck::Cmd::setPitchFader, 0.12);
            press ("d1.master");
            press ("d1.play");

            // Deck 2: synced, key lock on, a 4-beat loop running
            for (auto [sec, cue] : { std::pair<double, const char*> { 8.1, "d2.hotcue.1" }, { 40.0, "d2.hotcue.4" } })
            {
                d2.post (Deck::Cmd::seekSeconds, sec);
                press (cue);
            }
            d2.post (Deck::Cmd::seekSeconds, 33.0);
            press ("d2.keylock");
            press ("d2.sync");
            press ("d2.play");
            stage = 2;
            t0 = ticks;
        }
        else if (stage == 2 && ticks - t0 == 12)
        {
            ActionValue down; down.down = true;
            main.getRouter().perform ("d2.beatloop.4", down);
        }
        else if (stage == 2 && ticks - t0 > 24)
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
            stage = 7;
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
    const juce::String getApplicationName() override       { return "OrbitDJ"; }
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
        MainWindow() : DocumentWindow ("OrbitDJ", Theme::bg, DocumentWindow::allButtons)
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
