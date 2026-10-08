#include "MainComponent.h"
#include "../Library/RekordboxMasterDb.h"

namespace orbit
{

MainComponent::MainComponent()
    : mapping (midi, [this] { midi.saveToFile (AppSettings::mappingFile()); }),
      stemsView (stems,
                 [this] { return browser.getVisibleTracks(); },
                 [this] { return browser.getSelectedTracks(); },
                 [this] { return browser.getCurrentListName(); }),
      settingsView (settings, engine, [this] { applySettings(); }, [this] { library.reloadPcLibrary(); })
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setOpaque (true);

    // --- Subsystems -----------------------------------------------------------
    engine.initialise (settings.audioDeviceState, settings.bufferSize);
    midi.loadFromFile (AppSettings::mappingFile());

    library.onChanged = [this] { browser.refreshSources(); };
    library.configure (settings.rekordboxDbPath.isNotEmpty() ? juce::File (settings.rekordboxDbPath) : defaultRekordboxDbFile(),
                       settings.rekordboxDbKey.isNotEmpty() ? settings.rekordboxDbKey : defaultRekordboxDbKey(),
                       settings.usbRoots);

    router.onAppAction = [this] (const juce::String& id, int deck, const ActionValue& v) { handleAppAction (id, deck, v); };

    loader.onLoaded = [this] (int deck, LoadedTrack::Ptr) { (deck == 0 ? deck1 : deck2).setLoadingText ({}); waves.forceRedraw(); };
    loader.onError = [this] (int deck, const juce::String& err) { (deck == 0 ? deck1 : deck2).setLoadingText (err); };

    browser.onLoadToDeck = [this] (int deck, const TrackInfo& t, bool pc) { loadToDeck (deck, t, pc); };
    browser.chooseDeckForDoubleClick = [this]
    {
        // Load onto the deck that isn't playing (deck 1 if both are free)
        const bool p0 = engine.deck (0).state().playing.load(), p1 = engine.deck (1).state().playing.load();
        if (p0 && ! p1) return 1;
        return 0;
    };

    waves.onZoomChanged = [this] (double s) { settings.visibleSeconds = s; settings.save(); };
    engine.onDeviceChanged = [this] { settingsView.refreshRouting(); };

    // --- UI --------------------------------------------------------------------
    for (auto* c : std::initializer_list<juce::Component*> { &deck1, &deck2, &waves, &browser, &mapping, &stemsView, &settingsView })
        addChildComponent (c);

    int i = 0;
    for (auto* t : { &tabPlay, &tabMapping, &tabStems, &tabSettings })
    {
        addAndMakeVisible (*t);
        t->setClickingTogglesState (true);
        t->setRadioGroupId (1001);
        t->setColour (juce::TextButton::buttonColourId, Theme::panel);
        const int p = i++;
        t->onClick = [this, p] { showPage (p); };
    }

    applySettings();
    showPage (0);
    setWantsKeyboardFocus (true);
    startTimerHz (4);
}

MainComponent::~MainComponent()
{
    settings.audioDeviceState = engine.saveDeviceState();
    settings.save();
    midi.saveToFile (AppSettings::mappingFile());
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainComponent::applySettings()
{
    for (int d = 0; d < AudioEngine::maxDecks; ++d)
        engine.setDeckOutput (d, settings.deckOutputs[(size_t) d]);
    if (settings.sumToMaster >= 0)
        engine.setSumToMaster (settings.sumToMaster == 1);

    router.setJogSensitivity (settings.jogTicksPerRev);
    midi.setLedFeedbackEnabled (settings.ledFeedback);
    waves.setVisibleSeconds (settings.visibleSeconds);
    waves.setMaxFps (settings.maxFps);

    TrackLoader::Options lo;
    lo.waveformStyle = (WaveformData::Style) juce::jlimit (0, 2, settings.waveformStyle);
    lo.mp3OffsetMs = settings.mp3OffsetMs;
    loader.setOptions (lo);

    StemManager::Settings ss;
    ss.modelFile = settings.modelFile();
    ss.cacheDir = settings.stemCacheDir();
    ss.useGpu = settings.stemsUseGpu;
    ss.cpuThreads = settings.stemThreads;
    ss.separateOnLoad = settings.stemsOnLoad;
    stems.setSettings (ss);

    settings.save();
}

void MainComponent::loadToDeck (int deck, const TrackInfo& info, bool fromPc)
{
    if (engine.deck (deck).state().playing.load())
    {
        (deck == 0 ? deck1 : deck2).setLoadingText ("Deck is playing - pause it to load");
        return;
    }
    (deck == 0 ? deck1 : deck2).setLoadingText ("Loading " + info.displayArtistTitle() + " ...");
    loader.load (deck, info, fromPc);
}

void MainComponent::handleAppAction (const juce::String& id, int deck, const ActionValue& v)
{
    if (id == "deck.load")           browser.loadSelected (deck);
    else if (id == "browse.scroll")  browser.scroll ((int) std::round (v.delta));
    else if (id == "browse.enter")   { if (v.down) browser.enter(); }
    else if (id == "browse.back")    { if (v.down) browser.back(); }
    else if (id == "browse.tree")    { if (v.down) browser.toggleFocus(); }
    else if (id == "browse.load1")   { if (v.down) browser.loadSelected (0); }
    else if (id == "browse.load2")   { if (v.down) browser.loadSelected (1); }
    else if (id == "view.browser")   { if (v.down) { settings.showBrowser = ! settings.showBrowser; resized(); settings.save(); } }
    else if (id == "view.zoom")      { waves.setVisibleSeconds (waves.getVisibleSeconds() * std::pow (1.1, -v.delta)); }
    else if (id == "view.zoomin")    { if (v.down) waves.setVisibleSeconds (waves.getVisibleSeconds() / 1.25); }
    else if (id == "view.zoomout")   { if (v.down) waves.setVisibleSeconds (waves.getVisibleSeconds() * 1.25); }
}

void MainComponent::showPage (int p)
{
    page = p;
    juce::TextButton* tabs[] = { &tabPlay, &tabMapping, &tabStems, &tabSettings };
    tabs[p]->setToggleState (true, juce::dontSendNotification);

    deck1.setVisible (p == 0);
    deck2.setVisible (p == 0);
    waves.setVisible (p == 0);
    browser.setVisible (p == 0 && settings.showBrowser);
    mapping.setVisible (p == 1);
    stemsView.setVisible (p == 2);
    settingsView.setVisible (p == 3);
    resized();
}

void MainComponent::timerCallback()
{
    engine.getReleasePool().collect();

    const auto newMidi = midi.getXdjStatus();
    const bool newXdj = midi.isXdjConnected();
    const auto newAudio = engine.getDeviceDescription();
    const double newCpu = engine.getCpuUsage();
    const auto st = stems.getStatus();
    juce::String newStem = st.activity.isNotEmpty() ? st.activity + "  " + juce::String (juce::roundToInt (st.progress * 100.0)) + "%"
                                                    : (st.backend.isNotEmpty() ? "Stems: " + st.backend : juce::String ("Stems: ready"));

    if (newMidi != midiStatus || newXdj != xdjConnected || newAudio != audioStatus || std::abs (newCpu - cpu) > 0.005 || newStem != stemStatus)
    {
        midiStatus = newMidi;
        xdjConnected = newXdj;
        audioStatus = newAudio;
        cpu = newCpu;
        stemStatus = newStem;
        repaint (topBar);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);

    g.setColour (Theme::panel);
    g.fillRect (topBar);
    g.setColour (Theme::line);
    g.fillRect (topBar.withTop (topBar.getBottom() - 1));

    // Logo
    auto r = topBar.reduced (14, 0);
    auto logo = r.removeFromLeft (120).toFloat();
    const auto c = logo.withWidth (24.0f).withSizeKeepingCentre (22.0f, 22.0f);
    g.setColour (Theme::deckColour (0));
    g.fillEllipse (c.reduced (6.0f));
    g.setColour (Theme::accent);
    g.drawEllipse (c, 2.0f);
    g.setColour (Theme::text);
    g.setFont (Theme::font (17.0f, true));
    g.drawText ("OrbitDJ", logo.withTrimmedLeft (30.0f), juce::Justification::centredLeft);

    // Status pills (right side)
    auto pills = topBar.reduced (10, 9).toFloat();
    auto pill = [&] (const juce::String& text, juce::Colour dot, float w)
    {
        auto pr = pills.removeFromRight (w);
        drawPill (g, pr, text, dot);
        pills.removeFromRight (8.0f);
    };
    pill ("Audio CPU " + juce::String (juce::roundToInt (cpu * 100.0)) + "%", cpu < 0.5 ? Theme::good : (cpu < 0.8 ? Theme::warn : Theme::bad), 128.0f);
    pill (audioStatus, audioStatus.startsWith ("No") ? Theme::bad : Theme::good, juce::jmin (330.0f, Theme::font (12.5f).getStringWidthFloat (audioStatus) + 40.0f));
    pill (xdjConnected ? juce::String ("XDJ-XZ connected") : juce::String ("XDJ-XZ not found"), xdjConnected ? Theme::good : Theme::bad, 150.0f);
    if (page == 0)
        pill (stemStatus, Theme::accent, juce::jmin (300.0f, Theme::font (12.5f).getStringWidthFloat (stemStatus) + 40.0f));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar = r.removeFromTop (44);

    auto tabs = topBar.withTrimmedLeft (150).withWidth (420).reduced (0, 8);
    for (auto* t : { &tabPlay, &tabMapping, &tabStems, &tabSettings })
    {
        t->setBounds (tabs.removeFromLeft (98));
        tabs.removeFromLeft (6);
    }

    if (page == 0)
    {
        auto decks = r.removeFromTop (juce::jlimit (190, 230, r.getHeight() / 4));
        deck1.setBounds (decks.removeFromLeft (decks.getWidth() / 2).withTrimmedRight (1));
        deck2.setBounds (decks);
        r.removeFromTop (1);

        if (settings.showBrowser)
        {
            const int waveH = juce::jlimit (200, 380, (int) (r.getHeight() * 0.48));
            waves.setBounds (r.removeFromTop (waveH));
            browser.setBounds (r);
        }
        else
            waves.setBounds (r);
        browser.setVisible (settings.showBrowser);
    }
    else
    {
        mapping.setBounds (r);
        stemsView.setBounds (r);
        settingsView.setBounds (r);
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    // Handy keyboard shortcuts (the XDJ-XZ remains the main controller).
    if (k == juce::KeyPress::F1Key) { showPage (0); return true; }
    if (k == juce::KeyPress::F2Key) { showPage (1); return true; }
    if (k == juce::KeyPress::F3Key) { showPage (2); return true; }
    if (k == juce::KeyPress::F4Key) { showPage (3); return true; }
    return false;
}

} // namespace orbit
