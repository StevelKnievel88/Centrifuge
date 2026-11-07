#include "MainComponent.h"
#include "Features.h"
#include "../Audio/MusicalKey.h"
#include "../Library/RekordboxMasterDb.h"

namespace orbit
{

MainComponent::MainComponent()
    : mapping (midi, [this] { midi.saveToFile (AppSettings::mappingFile()); }),
      stemsView (stems,
                 [this] { return browser.getVisibleTracks(); },
                 [this] { return browser.getSelectedTracks(); },
                 [this] { return browser.getCurrentListName(); },
                 [this] { return router.getStemMatrix(); },
                 [this] (std::array<int, 4> m) { settings.stemMatrix = m; router.setStemMatrix (m); settings.save(); }),
      settingsView (settings, engine, [this] { applySettings(); }, [this] { library.reloadPcLibrary(); }),
      exportView (centrifugeLibrary, fileLoader, stems,
                  { [this] { return browser.getVisibleTracks(); }, [this] { return browser.getSelectedTracks(); },
                    [this] { return browser.getCurrentListName(); },
                    [this]
                    {
                        for (auto& s : library.getSources())
                            if (s->kind == LibrarySource::Kind::usbExport) return s->root;
                        return juce::File();
                    } })
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

    loader.onLoaded = [this] (int deck, LoadedTrack::Ptr t)
    {
        if (deck < 2) (deck == 0 ? deck1 : deck2).setLoadingText ({});
        editor.trackLoaded (deck, t);
        waves.forceRedraw();
        if (t != nullptr && t->waveform != nullptr)
            browser.notePreviewWaveform (t->info.file.getFullPathName(), *t->waveform);
        // A browser preview: start it where it was clicked
        if (deck == AudioEngine::previewDeck && t != nullptr && pendingPreviewFraction >= 0.0
            && t->info.file.getFullPathName() == pendingPreviewPath)
        {
            engine.deck (deck).post (Deck::Cmd::seekFraction, pendingPreviewFraction);
            engine.deck (deck).post (Deck::Cmd::play);
        }
        pendingPreviewFraction = -1.0;
    };

    // Library editing
    if (Features::rekordboxCopy)
    {
        for (int d = 0; d < 2; ++d)
            editor.setEditTargets (d, settings.editR[(size_t) d], settings.editC[(size_t) d]);
        editor.setEditTargets (AudioEngine::prepDeck, settings.prepEditR, settings.prepEditC);
        editor.onTargetsChanged = [this] (int d, bool r, bool c)
        {
            if (d < 2) { settings.editR[(size_t) d] = r; settings.editC[(size_t) d] = c; }
            else if (d == AudioEngine::prepDeck) { settings.prepEditR = r; settings.prepEditC = c; }
            settings.save();
        };
    }
    else
        for (int d = 0; d < AudioEngine::totalDecks; ++d)
            editor.setEditTargets (d, false, true);   // one library: edits go to Centrifuge's version only
    editor.onMessage = [this] (const juce::String& m) { flash (m); };
    for (auto* dp : { &deck1, &deck2 })
    {
        dp->setEditor (&editor);
        dp->onRenameCue = [this] (int d, bool hot, int i, juce::String cur) { askRename (d, hot, i, cur); };
        dp->onSetBpm = [this] (int d) { askBpm (d); };
        dp->onDeckColour = [this] (int d, juce::Colour c)
        {
            settings.deckColours[(size_t) d] = c.getARGB();
            settings.save();
            applySettings();
            deck1.repaint(); deck2.repaint(); waves.repaint(); phaseMeter.repaint();
        };
        dp->onTrackDropped = [this] (int d) { loadDragged (d); };
    }
    waves.onTrackDropped = [this] (int d) { loadDragged (d); };
    prepView.onTrackDropped = [this] { loadDragged (AudioEngine::prepDeck); };
    prepView.onRenameCue = [this] (int d, bool hot, int i, juce::String cur) { askRename (d, hot, i, cur); };
    prepView.onSetBpm = [this] (int d) { askBpm (d); };
    prepView.setListWidth (settings.prepListWidth);
    prepView.onListWidthChanged = [this] (int w) { settings.prepListWidth = w; settings.save(); };
    browser.versionFor = [this] (const TrackInfo& t) { return editor.versionToLoad (0, t); };
    browser.onPreview = [this] (const TrackInfo& t, bool pc, double f) { startPreview (t, pc, f); };
    browser.onPreviewStop = [this] { engine.deck (AudioEngine::previewDeck).post (Deck::Cmd::pause); };
    browser.onTrackAction = [this] (const std::vector<TrackInfo>& t, const juce::String& a) { trackAction (t, a); };
    browser.onSetRating = [this] (const TrackInfo& t, int stars)
    {
        if (const auto id = libraryIdFor (t, true); id != 0) centrifugeLibrary.setRating (id, stars);
    };
    waves.cueName = [this] (int d, bool hot, int i) { return editor.cueName (d, hot, i); };
    waves.onRenameCue = [this] (int d, bool hot, int i, juce::String cur) { askRename (d, hot, i, cur); };
    browser.centrifugeSource = [this] (int view) { return centrifugeSource (view); };
    browser.onImport = [this] (juce::Component& c) { showImportMenu (c); };
    browser.onFilesDropped = [this] (const juce::Array<juce::File>& items) { addMusic (items); };
    browser.onExport = [this] (juce::Component& c) { showExportMenu (c); };
    loader.onError = [this] (int deck, const juce::String& err)
    {
        if (deck < 2) (deck == 0 ? deck1 : deck2).setLoadingText (err);
        else flash (err);
    };

    browser.onLoadToDeck = [this] (int deck, const TrackInfo& t, bool pc) { loadToDeck (deck, t, pc); };
    browser.chooseDeckForDoubleClick = [this]
    {
        if (page == 4) return (int) AudioEngine::prepDeck;   // PREP page: load into PREP
        // Load onto the deck that isn't playing (deck 1 if both are free)
        const bool p0 = engine.deck (0).state().playing.load(), p1 = engine.deck (1).state().playing.load();
        if (p0 && ! p1) return 1;
        return 0;
    };

    waves.onZoomChanged = [this] (double s) { settings.visibleSeconds = s; settings.save(); };
    waves.onLaneSplitChanged = [this] (int d, float share) { settings.laneSplit[(size_t) d] = share; settings.save(); };
    waves.onPlayheadMoved = [this] (float f) { settings.playheadPosition = f; settings.save(); };
    waves.onFoldChanged = [this] (bool f) { settings.foldSkips = f; settings.save(); };
    engine.onDeviceChanged = [this] { settingsView.refreshRouting(); };

    // --- UI --------------------------------------------------------------------
    for (auto* c : std::initializer_list<juce::Component*> { &deck1, &deck2, &waves, &browser, &mapping, &stemsView, &settingsView, &prepView })
        addChildComponent (c);
    addAndMakeVisible (phaseMeter);

    // Tabs in screen order, with their page ids (PREP is page 4)
    const std::pair<juce::TextButton*, int> tabList[] = { { &tabPlay, 0 }, { &tabPrep, 4 }, { &tabMapping, 1 }, { &tabStems, 2 }, { &tabSettings, 3 } };
    for (auto [t, p] : tabList)
    {
        addAndMakeVisible (*t);
        t->getProperties().set ("psyched.mode", true);   // Cockpit mode switch: pressed = solid green
        t->setColour (juce::TextButton::buttonOnColourId, psyched::Colours::green);
        t->onClick = [this, p = p] { showPage (p); };
    }

    addChildComponent (about);
    addChildComponent (exportView);
    addChildComponent (conflictsView);

    applySettings();
    showPage (0);
    setWantsKeyboardFocus (true);
    startTimerHz (10);

    splash = std::make_unique<CentrifugeSplash>();
    splash->onFinished = [this] { skipSplash(); };
    addAndMakeVisible (*splash);
}

void MainComponent::skipSplash()
{
    if (splash == nullptr) return;
    splash->setVisible (false);
    juce::MessageManager::callAsync ([sp = std::shared_ptr<CentrifugeSplash> (splash.release())] {});
    repaint();
}

MainComponent::~MainComponent()
{
    stopImport = true;
    importPool.removeAllJobs (false, 20000);
    settings.audioDeviceState = engine.saveDeviceState();
    settings.save();
    editor.poll();
    midi.saveToFile (AppSettings::mappingFile());
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainComponent::applySettings()
{
    Theme::deckColours()[0] = juce::Colour (settings.deckColours[0]);
    Theme::deckColours()[1] = juce::Colour (settings.deckColours[1]);
    MusicalKey::camelotDisplay() = settings.camelotKeys;
    engine.setPreviewOutput (settings.previewOutput);
    repaint();
    browser.repaint();
    for (int d = 0; d < AudioEngine::maxDecks; ++d)
        engine.setDeckOutput (d, settings.deckOutputs[(size_t) d]);
    if (settings.sumToMaster >= 0)
        engine.setSumToMaster (settings.sumToMaster == 1);

    router.setJogSensitivity (settings.jogTicksPerRev);
    midi.setLedFeedbackEnabled (settings.ledFeedback);
    waves.setVisibleSeconds (settings.visibleSeconds);
    waves.setMaxFps (settings.maxFps);
    for (int d = 0; d < 2; ++d)
    {
        waves.setLanes (d, { settings.laneStyle[(size_t) d][0], settings.laneShape[(size_t) d][0] },
                           { settings.laneStyle[(size_t) d][1], settings.laneShape[(size_t) d][1] });
        waves.setLaneSplit (d, settings.laneSplit[(size_t) d]);
    }
    waves.setSingleColour (juce::Colour (settings.singleColour));
    waves.setStemLayers (settings.stemLayerOrder, settings.stemOpacity);
    waves.setStemsSeparate (settings.stemsSeparate);
    waves.setFoldSkips (settings.foldSkips);
    waves.setPlayheadPosition (settings.playheadPosition);
    for (auto* dp : { &deck1, &deck2 })
        dp->setOverviewStyle (settings.overviewStyle, settings.overviewShape == 1, juce::Colour (settings.singleColour));
    router.setStemMatrix (settings.stemMatrix);

    // Quantize: applied at start-up and when the setting changes (each deck's button can still toggle it).
    if (appliedQuantize != (int) settings.quantize)
    {
        appliedQuantize = (int) settings.quantize;
        for (int d = 0; d < AudioEngine::maxDecks; ++d)
            engine.deck (d).post (Deck::Cmd::quantizeSet, settings.quantize ? 1.0 : 0.0);
    }

    TrackLoader::Options lo;
    lo.mp3OffsetMs = settings.mp3OffsetMs;
    lo.cueStore = &cueStore;
    loader.setOptions (lo);

    StemManager::Settings ss;
    ss.modelFile = settings.modelFile();
    ss.cacheDir = settings.stemCacheDir();
    ss.useGpu = settings.stemsUseGpu;
    ss.cpuThreads = settings.stemThreads;
    ss.separateOnLoad = settings.stemsOnLoad;
    ss.qualityOnLoad = (StemQuality) juce::jlimit (0, 3, settings.stemQualityOnLoad);
    ss.qualityBatch = (StemQuality) juce::jlimit (0, 3, settings.stemQualityBatch);
    ss.findModel = [s = settings] (const juce::String& n) { return s.findModel (n); };
    stems.setSettings (ss);

    settings.save();
}

void MainComponent::loadToDeck (int deck, const TrackInfo& info, bool fromPc)
{
    if (deck >= 2)
    {
        // PREP / preview deck: never on air, so it's fine to stop it and load
        engine.deck (deck).post (Deck::Cmd::pause);
        loader.load (deck, info, fromPc, editor.versionToLoad (deck, info));
        return;
    }
    if (engine.deck (deck).state().playing.load())
    {
        (deck == 0 ? deck1 : deck2).setLoadingText ("Deck is playing - pause it to load");
        return;
    }
    (deck == 0 ? deck1 : deck2).setLoadingText ("Loading " + info.displayArtistTitle() + " ...");
    loader.load (deck, info, fromPc, editor.versionToLoad (deck, info));
}

void MainComponent::handleAppAction (const juce::String& id, int deck, const ActionValue& v)
{
    if (deck >= 0 && deck < 2)
    {
        if (! Features::rekordboxCopy && id.startsWith ("edit.")) return;
        if (id == "edit.r")        editor.setEditTargets (deck, ! editor.editsRekordbox (deck), editor.editsCentrifuge (deck));
        else if (id == "edit.c")   editor.setEditTargets (deck, editor.editsRekordbox (deck), ! editor.editsCentrifuge (deck));
        else if (id == "grid.mode") (deck == 0 ? deck1 : deck2).toggleGridMode();
        else if (id.startsWith ("grid.")) editor.gridAction (deck, id.fromFirstOccurrenceOf ("grid.", false, false));
        (deck == 0 ? deck1 : deck2).repaint();
        if (id.startsWith ("edit.") || id.startsWith ("grid.")) return;
    }

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
    else if (id == "view.wavemode")  { if (v.down) waves.setStemsHidden (! waves.getStemsHidden()); }
    else if (id == "view.fold")      { if (v.down) { settings.foldSkips = ! settings.foldSkips; waves.setFoldSkips (settings.foldSkips); settings.save(); } }
}

void MainComponent::showPage (int p)
{
    page = p;
    juce::TextButton* tabs[] = { &tabPlay, &tabMapping, &tabStems, &tabSettings, &tabPrep };
    for (int i = 0; i < 5; ++i)
        tabs[i]->setToggleState (i == p, juce::dontSendNotification);
    if (p == 3) settingsView.refreshValues();

    deck1.setVisible (p == 0);
    deck2.setVisible (p == 0);
    waves.setVisible (p == 0);
    browser.setVisible ((p == 0 && settings.showBrowser) || p == 4);
    prepView.setVisible (p == 4);
    phaseMeter.setPrepOnly (p == 4);
    mapping.setVisible (p == 1);
    stemsView.setVisible (p == 2);
    settingsView.setVisible (p == 3);
    resized();
}

void MainComponent::timerCallback()
{
    engine.getReleasePool().collect();
    editor.poll();
    countPlays();

    // Cue / locator / phrase edits show in the browser's preview straight away
    for (int d = 0; d < AudioEngine::totalDecks; ++d)
    {
        const auto& dst = engine.deck (d).state();
        const juce::int64 sig = ((juce::int64) dst.trackSerial.load() << 32) ^ ((juce::int64) dst.cueEdits.load() << 12) ^ (juce::int64) dst.trackDataVersion.load();
        if (sig == marksSig[(size_t) d]) continue;
        marksSig[(size_t) d] = sig;
        if (auto t = engine.deck (d).getLoadedTrack())
            browser.notePreviewMarks (t->info.file.getFullPathName(), TrackMarks::fromDeck (dst, t->phrases));
    }

    // The preview column follows the preview deck
    {
        const auto& pst = engine.deck (AudioEngine::previewDeck).state();
        auto pt = engine.deck (AudioEngine::previewDeck).getLoadedTrack();
        const double len = pst.lengthSec.load();
        browser.setPreviewState (pt != nullptr ? pt->info.file.getFullPathName() : juce::String(),
                                 len > 0.0 ? pst.positionSec.load() / len : 0.0, pst.playing.load());
    }

    // Library changed (import, edits): refresh the browser, at most every couple of seconds
    if (const int lc = centrifugeLibrary.getChangeCount(); lc != seenLibraryChanges
        && juce::Time::getMillisecondCounter() - lastLibraryRefresh > 2000)
    {
        seenLibraryChanges = lc;
        lastLibraryRefresh = juce::Time::getMillisecondCounter();
        browser.refreshSources();
    }

    const auto newMidi = midi.getXdjStatus();
    const bool newXdj = midi.isXdjConnected();
    const auto newAudio = engine.getDeviceDescription();
    const double newCpu = engine.getCpuUsage();
    const auto st = stems.getStatus();
    juce::String newStem = st.activity.isNotEmpty() ? st.activity + "  " + juce::String (juce::roundToInt (st.progress * 100.0)) + "%"
                                                    : (st.backend.isNotEmpty() ? "Stems: " + st.backend : juce::String ("Stems: ready"));

    if (importing.load() || juce::Time::getMillisecondCounter() < flashUntil + 500)
        repaint (topBar);
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
    psyched::paintBackdrop (g, getLocalBounds());
    psyched::paintHeader (g, topBar.toFloat());

    // Dividers (drag to resize)
    for (auto d : { dividerDecks, dividerBrowser })
        if (! d.isEmpty() && page == 0)
        {
            g.setColour (psyched::Colours::edge2);
            g.fillRect (d.withSizeKeepingCentre (40, 2));
        }

    // Logo (click for About)
    auto r = topBar.reduced (14, 0);
    auto logo = r.removeFromLeft (236).toFloat();
    logoArea = logo.toNearestInt();
    CentrifugeRotor::paintRotor (g, logo.withWidth (30.0f).withSizeKeepingCentre (30.0f, 30.0f), 0.4f, 0.0f, 1.0f, 0.0f, 0.0f);
    psyched::paintWordmark (g, logo.withTrimmedLeft (40.0f), "Centrifuge", 0, psyched::Signature::spin, 17.0f);

    // Status pills (right side)
    auto pills = topBar.reduced (10, 9).withLeft (phaseArea.getRight() + 14).toFloat();
    auto pill = [&] (const juce::String& text, juce::Colour dot, float w)
    {
        auto pr = pills.removeFromRight (w);
        drawPill (g, pr, text, dot);
        pills.removeFromRight (8.0f);
    };
    pill ("CPU " + juce::String (juce::roundToInt (cpu * 100.0)) + "%", cpu < 0.5 ? Theme::good : (cpu < 0.8 ? Theme::warn : Theme::bad), 84.0f);
    pill (audioStatus, audioStatus.startsWith ("No") ? Theme::bad : Theme::good, juce::jmin (330.0f, psyched::Fonts::label().getStringWidthFloat (audioStatus.toUpperCase()) + 34.0f));
    pill (xdjConnected ? juce::String ("XDJ-XZ connected") : juce::String ("XDJ-XZ not found"), xdjConnected ? Theme::good : Theme::bad, 164.0f);
    juce::String extra = stemStatus;
    juce::Colour extraColour = Theme::accent;
    if (importing.load())
    {
        const juce::ScopedLock sl (importNameLock);
        extra = importName + "  " + juce::String (juce::roundToInt (importProgress.load() * 100.0f)) + "%";
    }
    else if (juce::Time::getMillisecondCounter() < flashUntil)
    {
        extra = flashText;
        extraColour = Theme::warn;
    }
    if ((page == 0 || extra != stemStatus) && pills.getWidth() > 80.0f)
        pill (extra, extraColour, juce::jmin (pills.getWidth(), psyched::Fonts::label().getStringWidthFloat (extra.toUpperCase()) + 34.0f));
}

void MainComponent::resized()
{
    if (splash != nullptr) splash->setBounds (getLocalBounds());
    about.setBounds (getLocalBounds());
    exportView.setBounds (getLocalBounds());
    conflictsView.setBounds (getLocalBounds());
    auto r = getLocalBounds();
    topBar = r.removeFromTop (52);

    auto tabs = topBar.withTrimmedLeft (262).withWidth (5 * 92).reduced (0, 12);
    for (auto* t : { &tabPlay, &tabPrep, &tabMapping, &tabStems, &tabSettings })
    {
        t->setBounds (tabs.removeFromLeft (92));
        tabs.removeFromLeft (-1);
    }
    // Phrase meter: both decks stacked, to the right of the tabs
    phaseArea = juce::Rectangle<int> (262 + 5 * 92 + 18, topBar.getY() + 10, juce::jlimit (220, 520, (getWidth() - 262 - 5 * 92 - 18) / 2), topBar.getHeight() - 20);
    phaseMeter.setBounds (phaseArea);

    if (page == 0)
    {
        const int deckH = juce::jlimit (150, juce::jmax (150, r.getHeight() / 2), settings.deckPanelHeight);
        auto decks = r.removeFromTop (deckH);
        deck1.setBounds (decks.removeFromLeft (decks.getWidth() / 2).withTrimmedRight (1));
        deck2.setBounds (decks);
        dividerDecks = r.removeFromTop (5);

        if (settings.showBrowser)
        {
            const int browserH = juce::jlimit (120, juce::jmax (120, r.getHeight() - 160), settings.browserHeight);
            browser.setBounds (r.removeFromBottom (browserH));
            dividerBrowser = r.removeFromBottom (5);
            waves.setBounds (r);
        }
        else
        {
            dividerBrowser = {};
            waves.setBounds (r);
        }
        browser.setVisible (settings.showBrowser);
    }
    else if (page == 4)
    {
        dividerDecks = dividerBrowser = {};
        const int prepH = juce::jmax (360, (int) (r.getHeight() * 0.6));
        prepView.setBounds (r.removeFromTop (prepH));
        r.removeFromTop (4);
        browser.setBounds (r);
    }
    else
    {
        mapping.setBounds (r);
        stemsView.setBounds (r);
        settingsView.setBounds (r);
    }
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    const bool over = page == 0 && (dividerDecks.contains (e.getPosition()) || dividerBrowser.contains (e.getPosition()));
    setMouseCursor (over ? juce::MouseCursor::UpDownResizeCursor
                         : logoArea.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    dragging = 0;
    if (logoArea.contains (e.getPosition())) { about.show(); return; }
    if (page != 0) return;
    if (dividerDecks.contains (e.getPosition()))   { dragging = 1; dragStartValue = settings.deckPanelHeight; }
    if (dividerBrowser.contains (e.getPosition())) { dragging = 2; dragStartValue = browser.getHeight(); }
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == 1) settings.deckPanelHeight = juce::jmax (150, dragStartValue + e.getDistanceFromDragStartY());
    else if (dragging == 2) settings.browserHeight = juce::jmax (120, dragStartValue - e.getDistanceFromDragStartY());
    else return;
    resized();
}

void MainComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragging != 0) settings.save();
    dragging = 0;
}

//==============================================================================
std::shared_ptr<const LibrarySource> MainComponent::centrifugeSource (int view)
{
    view = juce::jlimit (0, 1, view);
    const int lc = centrifugeLibrary.getChangeCount();
    if (sourceCache[(size_t) view] == nullptr || sourceCacheVersion[(size_t) view] != lc)
    {
        sourceCache[(size_t) view] = centrifugeLibrary.makeSource (view);
        sourceCacheVersion[(size_t) view] = lc;
    }
    return sourceCache[(size_t) view];
}

void MainComponent::flash (const juce::String& text)
{
    flashText = text;
    flashUntil = juce::Time::getMillisecondCounter() + 5000;
    repaint (topBar);
}

void MainComponent::showImportMenu (juce::Component& anchor)
{
    if (importing.load()) { flash ("An import is already running"); return; }
    juce::PopupMenu m;
    std::vector<std::shared_ptr<const LibrarySource>> sources;
    auto cur = browser.getCurrentSource();
    const auto selected = browser.getSelectedSourceIndices();
    if (Features::rekordboxCopy)
    {
        m.addSectionHeader ("Copy into Centrifuge's library (rekordbox's files are only read)");
        sources = browser.getRekordboxSources();
        int id = 1;
        for (auto& s : sources)
        {
            const bool ok = s->error.isEmpty() && ! s->tracks.empty();
            m.addItem (id++, "Import " + s->name + "  (" + juce::String ((int) s->tracks.size()) + " tracks, with playlists)", ok);
        }
        const bool canSelected = cur != nullptr && cur->kind != LibrarySource::Kind::centrifuge && ! selected.empty();
        m.addSeparator();
        m.addItem (1000, "Import the selected tracks only  (" + juce::String ((int) selected.size()) + ")", canSelected);
    }
    m.addSectionHeader (Features::rekordboxCopy ? "Add music that isn't in rekordbox (Centrifuge analyses it)" : "Add music (Centrifuge analyses it)");
    m.addItem (2000, "Add a folder...  (becomes a playlist, sub-folders too)");
    m.addItem (2001, "Add audio files...");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                     [this, sources, cur, selected] (int r)
    {
        if (r >= 1 && r <= (int) sources.size()) runImport (sources[(size_t) r - 1], {});
        else if (r == 1000 && cur != nullptr) runImport (cur, selected);
        else if (r == 2000 || r == 2001) chooseMusic (r == 2000);
    });
}

void MainComponent::runImport (std::shared_ptr<const LibrarySource> source, std::vector<int> only)
{
    importing = true;
    importProgress = 0.0f;
    {
        const juce::ScopedLock sl (importNameLock);
        importName = "Importing " + source->name;
    }
    juce::Component::SafePointer<MainComponent> safe (this);
    importPool.addJob ([this, safe, source, only]
    {
        auto res = centrifugeLibrary.import (*source, only.empty() ? nullptr : &only, &cueStore,
                                             [this] (float p) { importProgress = p; return ! stopImport.load(); });
        juce::MessageManager::callAsync ([safe, res, name = source->name]
        {
            if (safe == nullptr) return;
            safe->importing = false;
            safe->browser.refreshSources();
            juce::String summary = "Imported " + name + ": " + juce::String (res.added) + " new, " + juce::String (res.updated) + " updated, "
                                 + juce::String (res.unchanged) + " unchanged";
            if (res.error.isNotEmpty()) summary = "Import failed: " + res.error;
            if (! res.conflicts.empty())
                safe->conflictsView.show (res.conflicts, summary + ", " + juce::String ((int) res.conflicts.size()) + " need you to choose.");
            else
                safe->flash (summary);
        });
    });
}

void MainComponent::chooseMusic (bool folders)
{
    musicChooser = std::make_unique<juce::FileChooser> (folders ? "Add a folder of music" : "Add audio files",
                                                        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                        folders ? juce::String() : juce::String ("*.mp3;*.wav;*.aif;*.aiff;*.flac;*.m4a;*.aac;*.mp4;*.alac"));
    const int flags = juce::FileBrowserComponent::openMode
                    | (folders ? juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::canSelectFiles)
                    | juce::FileBrowserComponent::canSelectMultipleItems;
    musicChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        auto items = fc.getResults();
        if (! items.isEmpty()) addMusic (items);
    });
}

void MainComponent::addMusic (const juce::Array<juce::File>& items)
{
    if (importing.load()) { flash ("An import is already running"); return; }
    importing = true;
    importProgress = 0.0f;
    {
        const juce::ScopedLock sl (importNameLock);
        importName = "Adding music";
    }
    FileImporter::Options o;
    std::tie (o.minBpm, o.maxBpm) = settings.analysisBpmRange();
    o.mp3OffsetMs = settings.mp3OffsetMs;
    juce::Component::SafePointer<MainComponent> safe (this);
    importPool.addJob ([this, safe, items, o]
    {
        auto res = FileImporter::run (centrifugeLibrary, fileLoader, items, o, [this] (float p, const juce::String& name)
        {
            importProgress = p;
            const juce::ScopedLock sl (importNameLock);
            importName = "Analysing " + name;
            return ! stopImport.load();
        });
        juce::MessageManager::callAsync ([safe, res]
        {
            if (safe == nullptr) return;
            safe->importing = false;
            safe->browser.refreshSources();
            juce::String summary = "Added " + juce::String (res.added) + " track" + (res.added == 1 ? "" : "s");
            if (res.alreadyThere > 0) summary << ", " << res.alreadyThere << " already in the library";
            if (res.playlists > 0) summary << ", " << res.playlists << " playlist" << (res.playlists == 1 ? "" : "s");
            if (res.failed > 0) summary << ", " << res.failed << " couldn't be read (" << res.errors[0] << ")";
            else if (res.added == 0 && res.alreadyThere == 0 && ! res.errors.isEmpty()) summary = res.errors[0];
            if (res.cancelled) summary << " (stopped)";
            safe->flash (summary);
        });
    });
}

void MainComponent::showExportMenu (juce::Component& anchor)
{
    juce::PopupMenu m;
    m.addItem (1, "Export to a USB stick (rekordbox-compatible)...");
    m.addItem (2, "Export a rekordbox XML file (to import into rekordbox)...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor), [this] (int r)
    {
        if (r == 1 || r == 2) exportView.open (r == 1);
    });
}

void MainComponent::askRename (int deck, bool hotCue, int index, const juce::String& current)
{
    auto* w = new juce::AlertWindow (hotCue ? "Name hot cue " + juce::String::charToString ((juce::juce_wchar) ('A' + index)) : juce::String ("Name locator"),
                                     "Saved to the library versions that are switched on (R / C).", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", current);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, deck, hotCue, index] (int r)
    {
        if (r == 1) editor.renameCue (deck, hotCue, index, w->getTextEditorContents ("name").trim());
    }), true);
}

void MainComponent::askBpm (int deck)
{
    auto* w = new juce::AlertWindow (deck >= 2 ? juce::String ("Set BPM - PREP") : "Set BPM - deck " + juce::String (deck + 1),
                                     "Makes a constant grid at this tempo, keeping the beat nearest the playhead where it is.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("bpm", juce::String (editor.currentBpm (deck), 2));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, deck] (int r)
    {
        const double bpm = w->getTextEditorContents ("bpm").getDoubleValue();
        if (r == 1 && bpm >= 20.0 && bpm <= 400.0) editor.gridAction (deck, "bpm=" + juce::String (bpm, 2));
    }), true);
}

void MainComponent::loadDragged (int deck)
{
    const auto tracks = browser.getDraggedTracks();
    if (tracks.empty()) return;
    loadToDeck (deck, tracks.front(), browser.draggedFromPc());
}

void MainComponent::startPreview (const TrackInfo& info, bool fromPc, double fraction)
{
    auto& d = engine.deck (AudioEngine::previewDeck);
    auto t = d.getLoadedTrack();
    if (t != nullptr && t->info.file == info.file)
    {
        d.post (Deck::Cmd::seekFraction, juce::jlimit (0.0, 1.0, fraction));
        d.post (Deck::Cmd::play);
        return;
    }
    pendingPreviewPath = info.file.getFullPathName();
    pendingPreviewFraction = juce::jlimit (0.0, 1.0, fraction);
    loadToDeck (AudioEngine::previewDeck, info, fromPc);
}

void MainComponent::trackAction (const std::vector<TrackInfo>& tracks, const juce::String& action)
{
    if (tracks.empty()) return;
    const bool pc = browser.getCurrentSource() != nullptr && browser.getCurrentSource()->kind == LibrarySource::Kind::pcCollection;
    if (action == "stems.separate")
    {
        stems.addToBatch (tracks);
        flash ("Separating stems for " + juce::String ((int) tracks.size()) + (tracks.size() == 1 ? " track" : " tracks")
               + " - tracks that already have stems at this quality are skipped");
        return;
    }
    if (action.startsWith ("load."))
    {
        const int d = action.getTrailingIntValue();
        loadToDeck (d, tracks.front(), pc);
        if (d == AudioEngine::prepDeck) showPage (4);
        return;
    }
    int n = 0;
    for (auto& t : tracks)
    {
        if (t.libraryId == 0) continue;
        ++n;
        if (action == "remove.cf" && ! Features::rekordboxCopy) centrifugeLibrary.deleteTrack (t.libraryId);
        else if (action == "remove.cf")   centrifugeLibrary.removeFromView (t.libraryId, 1);
        else if (action == "remove.rb")   centrifugeLibrary.removeFromView (t.libraryId, 0);
        else if (action == "plays.reset") centrifugeLibrary.resetPlays (t.libraryId);
    }
    if (n == 0) { flash ("Only tracks in Centrifuge's library can be removed or reset"); return; }
    const auto what = juce::String (n) + (n == 1 ? " track" : " tracks");
    if (action == "remove.cf")        flash ("Removed " + what + " from Centrifuge's library (your music files are untouched)");
    else if (action == "remove.rb")   flash ("Removed " + what + " from the rekordbox copy (rekordbox itself is unchanged)");
    else if (action == "plays.reset") flash ("Play count reset for " + what);
}

void MainComponent::countPlays()
{
    // A play counts once a track has played for 30 seconds on deck 1 or 2.
    const auto now = juce::Time::getMillisecondCounter();
    const double dt = lastTick == 0 ? 0.0 : juce::jmin (1.0, (now - lastTick) / 1000.0);
    lastTick = now;
    for (int d = 0; d < 2; ++d)
    {
        const auto& st = engine.deck (d).state();
        const int s = st.trackSerial.load();
        if (s != playSerial[(size_t) d]) { playSerial[(size_t) d] = s; playedSeconds[(size_t) d] = 0.0; playCounted[(size_t) d] = false; }
        if (s == 0 || playCounted[(size_t) d] || ! st.playing.load()) continue;
        playedSeconds[(size_t) d] += dt;
        if (playedSeconds[(size_t) d] < 30.0) continue;
        playCounted[(size_t) d] = true;
        auto t = engine.deck (d).getLoadedTrack();
        if (t == nullptr) continue;
        auto info = t->info;
        if (info.durationSec <= 0.0) info.durationSec = st.lengthSec.load();
        if (const auto id = libraryIdFor (info, true); id != 0) centrifugeLibrary.addPlay (id);
    }
}

juce::int64 MainComponent::libraryIdFor (const TrackInfo& t, bool addIfMissing)
{
    // Tracks from a USB stick join Centrifuge's library the first time they're rated, played or edited.
    if (t.libraryId != 0) return t.libraryId;
    if (auto found = centrifugeLibrary.find (CueStore::keyFor (t, t.durationSec))) return found->id;
    if (! addIfMissing || t.file == juce::File()) return 0;
    return centrifugeLibrary.addTrack (t, CentrifugeLibrary::readRekordboxVersion (t));
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    // Handy keyboard shortcuts (the XDJ-XZ remains the main controller).
    if (k == juce::KeyPress::F1Key) { showPage (0); return true; }
    if (k == juce::KeyPress::F2Key) { showPage (1); return true; }
    if (k == juce::KeyPress::F3Key) { showPage (2); return true; }
    if (k == juce::KeyPress::F4Key) { showPage (3); return true; }
    if (k == juce::KeyPress::F5Key) { showPage (4); return true; }
    return false;
}

} // namespace orbit
