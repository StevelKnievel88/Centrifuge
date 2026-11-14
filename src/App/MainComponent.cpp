#include "BinaryData.h"
#include "MainComponent.h"
#include "../UI/ListDialogs.h"
#include "../Library/UserLists.h"
#include "../Library/KeyOpinions.h"
#include "Features.h"
#include "../Audio/MusicalKey.h"
#include "../Library/RekordboxMasterDb.h"
#include "ShowInExplorer.h"
#include "../Audio/TrackAnalyser.h"
#include "../Library/TagWriter.h"
#include "../Library/PreviewWaveCache.h"

namespace orbit
{

MainComponent::MainComponent()
    : mapping (midi, [this] { midi.saveToFile (AppSettings::mappingFile()); }),
      stemsView (stems,
                 [this] { return browser.getVisibleTracks(); },
                 [this] { return browser.getSelectedTracks(); },
                 [this] { return browser.getCurrentListName(); },
                 [this] { return router.getStemMatrix(); },
                 [this] (std::array<int, 8> m) { settings.stemMatrix = m; router.setStemMatrix (m); settings.save(); }),
      settingsView (settings, engine, [this] { applySettings(); }, [this] { library.reloadPcLibrary(); }),
      stemSettingsView (settings, engine, [this] { applySettings(); }, [this] { library.reloadPcLibrary(); }, true),
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
    mapping.setDeckControllers (settings.deckController[0], settings.deckController[1]);
    mapping.onDeckController = [this] (int d, const juce::String& dev)
    {
        settings.deckController[(size_t) d] = dev;
        settings.save();
        midi.setDeckControllers (settings.deckController[0], settings.deckController[1]);
    };

    loader.onLoaded = [this] (int deck, LoadedTrack::Ptr t)
    {
        if (deck < 2) (deck == 0 ? deck1 : deck2).setLoadingText ({});
        if (deck < 2) applyAutoGain (deck, t);
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
        dp->onOpenSettings = [this] { showPage (3); };
        dp->onBrakeChanged = [this] (int d, double sec, double beats)
        {
            settings.brakeSec[(size_t) d] = sec;
            settings.brakeBeats[(size_t) d] = beats;
            settings.save();
        };
        dp->onJumpSlotChanged = [this] (int d, int slot, double beats, juce::uint32 c)
        {
            settings.jumpBeats[(size_t) (d * 4 + slot)] = beats;
            settings.jumpColours[(size_t) (d * 4 + slot)] = c;
            settings.save();
            midi.clearLedCache();
        };
        dp->viewWindow = [this] { return std::pair<double, float> { waves.getVisibleSeconds(), waves.getPlayheadPosition() }; };
        dp->onDeckColour = [this] (int d, juce::Colour c)
        {
            settings.deckColours[(size_t) d] = c.getARGB();
            settings.save();
            applySettings();
            deck1.repaint(); deck2.repaint(); waves.repaint(); phaseMeter.repaint();
        };
        dp->onTrackDropped = [this] (int d) { loadDragged (d); };
        dp->onEditViewToggled = [this] (int d) { settings.editView[(size_t) d] = ! settings.editView[(size_t) d]; settings.save(); applySettings(); };
        dp->onOverviewGridToggled = [this] { settings.overviewGrid = ! settings.overviewGrid; applySettings(); };
        dp->onStemModeChanged = [this] (int d, int stem, int mode)
        {
            for (int s = 0; s < 4; ++s) if (stem < 0 || stem == s) settings.stemModes[(size_t) d][(size_t) s] = mode;
            settings.save();
        };
    }
    prepView.onOverviewGridToggled = [this] { settings.overviewGrid = ! settings.overviewGrid; applySettings(); };
    prepView.onStemModeChanged = [this] (int stem, int mode)
    {
        for (int s = 0; s < 4; ++s) if (stem < 0 || stem == s) settings.stemModes[2][(size_t) s] = mode;
        settings.save();
    };
    prepView.onDeckColour = [this] (juce::Colour c)
    {
        settings.deckColours[2] = c.getARGB();
        applySettings();
        prepView.repaint(); phaseMeter.repaint(); prepView.getWaves().forceRedraw();
    };
    prepView.getWaves().onZoomChanged = [this] (double sec) { settings.prepVisibleSeconds = sec; settings.save(); };
    prepView.getWaves().onLaneSplitChanged = [this] (int, float share) { settings.prepLaneSplit = share; settings.save(); };
    prepView.getWaves().onLaneStyleChosen = [this] (int, int lane, int style)
    {
        settings.prepLaneStyle[(size_t) lane] = style;
        settings.save();
        applySettings();
        settingsView.refreshValues();
    };
    prepView.getWaves().onEditViewChanged = [this] (int, bool on) { settings.editView[2] = on; settings.save(); applySettings(); };
    browser.setColumnState (settings.browserColumns);
    browser.setXzColumnState (settings.browserXzColumns);
    browser.onXzColumnsChanged = [this] { settings.browserXzColumns = browser.getXzColumnState(); settings.save(); };
    browser.setInfoPanelOpen (settings.browserInfoOpen);
    browser.onInfoPanelToggled = [this] (bool open) { settings.browserInfoOpen = open; settings.save(); };
    browser.onEditInfo = [this] (const std::vector<TrackInfo>& tracks, const juce::String& field, const juce::String& value)
    {
        // batch edit: the field is set on every selected track (in Centrifuge's library)
        for (auto& t : tracks)
        {
            const auto id = libraryIdFor (t, true);
            if (id == 0) continue;
            if (field == "rating") { centrifugeLibrary.setRating (id, value.getIntValue()); continue; }
            auto lt = centrifugeLibrary.get (id);
            if (! lt) continue;
            auto info = lt->info;
            if (field == "title") info.title = value;
            else if (field == "artist") info.artist = value;
            else if (field == "album") info.album = value;
            else if (field == "genre") info.genre = value;
            else if (field == "label") info.label = value;
            else if (field == "comment") info.comment = value;
            else if (field == "key")
            {
                auto k = MusicalKey::parse (value);
                if (k.isValid()) { k.camelotStyle = false; info.key = k.toString(); } else info.key = value;
            }
            else if (field == "colour")
                info.colour = value.startsWith ("#") ? juce::Colour::fromString (value.substring (1))
                            : value.getIntValue() > 0 ? rekordboxTrackColour (value.getIntValue()) : juce::Colour();
            centrifugeLibrary.setInfo (id, info);
        }
        flash ("Set " + field + " on " + juce::String ((int) tracks.size()) + (tracks.size() == 1 ? " track" : " tracks"));
    };
    browser.onWriteTags = [this] (const std::vector<TrackInfo>& tracks) { writeTags (tracks); };
    browser.onExpandToggled = [this] { browserExpanded = ! browserExpanded; browser.setExpanded (browserExpanded); resized(); };
    browser.setTagColourNames (settings.tagColourNames);
    browser.onTagColourNames = [this] (bool on) { settings.tagColourNames = on; settings.save(); };
    browser.onHideToggled = [this] { settings.showBrowser = ! settings.showBrowser; settings.save(); resized(); };
    browser.onColumnsChanged = [this] (const juce::String& cols) { settings.browserColumns = cols; settings.save(); };
    waves.onTrackDropped = [this] (int d) { loadDragged (d); };
    prepView.onTrackDropped = [this] { loadDragged (AudioEngine::prepDeck); };
    prepView.onRenameCue = [this] (int d, bool hot, int i, juce::String cur) { askRename (d, hot, i, cur); };
    prepView.onSetBpm = [this] (int d) { askBpm (d); };
    prepView.onEditViewToggled = [this] { settings.editView[2] = ! settings.editView[2]; settings.save(); applySettings(); };
    phaseMeter.foldFor = [this] (int d) -> const TimeFold* { return d < 2 ? &waves.foldFor (d) : d == AudioEngine::prepDeck ? &prepView.getWaves().foldFor (0) : nullptr; };
    prepView.setListWidth (settings.prepListWidth);
    phaseMeter.setOptions (settings.phraseBars, settings.phraseFollow, settings.phraseBrackets);
    phaseMeter.onOptionsChanged = [this] (int bars, bool follow, bool br)
    {
        settings.phraseBars = bars; settings.phraseFollow = follow; settings.phraseBrackets = br;
        settings.save();
        xzView.getPhaseMeter().setOptions (bars, false, false);   // the XZ screen's meter matches
        resized();
        repaint();
    };
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
    browser.onTreeAction = [this] (const juce::String& node, const juce::String& action, const std::vector<TrackInfo>& tracks) { treeAction (node, action, tracks); };
    browser.masterKey = [this] { const int m = engine.getMasterDeck(); return m >= 0 && m < 2 ? router.effectiveKeyText (m) : juce::String(); };
    browser.onSetListToggled = [this] { toggleSetList(); };
    browser.setRowHeight (settings.browserRowHeight);
    browser.onRowHeightChanged = [this] (int h) { settings.browserRowHeight = h; settings.save(); };
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
    waves.onLaneSplitChanged = [this] (int d, float share)
    {
        settings.laneSplit[(size_t) d] = share;
        settings.save();
        waves.setLaneSplit (d, share);
        xzView.getWaves().setLaneSplit (d, share);
    };
    waves.onLaneStyleChosen = [this] (int d, int lane, int style)
    {
        if (d < 0 || d > 1) return;
        settings.laneStyle[(size_t) d][(size_t) lane] = style;
        settings.save();
        applySettings();
        settingsView.refreshValues();
    };
    waves.onPlayheadMoved = [this] (float f)
    {
        settings.playheadPosition = f;
        settings.save();
        waves.setPlayheadPosition (f);
        xzView.getWaves().setPlayheadPosition (f);
    };
    waves.onEditViewChanged = [this] (int d, bool on) { settings.editView[(size_t) d] = on; settings.save(); applySettings(); };
    engine.onDeviceChanged = [this] { settingsView.refreshRouting(); };

    // --- UI --------------------------------------------------------------------
    for (auto* c : std::initializer_list<juce::Component*> { &deck1, &deck2, &waves, &browser, &mapping, &stemsView, &settingsView, &prepView })
        addChildComponent (c);
    addAndMakeVisible (phaseMeter);
    stemsView.setSettingsPanel (&stemSettingsView);
    stemsView.isFilterMode = [this] (int d) { return router.isStemCtlFilterMode (d); };
    stemsView.toggleFilterMode = [this] (int d) { router.toggleStemCtlFilterMode (d); };

    // Tabs in screen order, with their page ids (PREP is page 4)
    const std::pair<juce::TextButton*, int> tabList[] = { { &tabPlay, 0 }, { &tabPrep, 4 }, { &tabMapping, 1 }, { &tabStems, 2 }, { &tabSettings, 3 } };
    for (auto [t, p] : tabList)
    {
        addAndMakeVisible (*t);
        t->getProperties().set ("psyched.mode", true);   // Cockpit mode switch: pressed = solid green
        t->setColour (juce::TextButton::buttonOnColourId, psyched::Colours::green);
        t->onClick = [this, p = p] { showPage (p); };
    }

    addAndMakeVisible (statusFold);
    statusFold.setTooltip ("Fold the status boxes away to the right (more room for the phrase meter)");
    statusFold.setColour (juce::TextButton::buttonOnColourId, psyched::Colours::cyan);
    statusFold.onClick = [this]
    {
        settings.statusFolded = ! settings.statusFolded;
        settings.save();
        resized();
        repaint();
    };

    addAndMakeVisible (masterVolume);
    addAndMakeVisible (recButton);
    recButton.stateNames = { "NOT RECORDING", "RECORDING", "PRESSED", "ARMED" };
    recButton.setColour (juce::TextButton::buttonOnColourId, Theme::bad);
    recButton.paintExtra = [this] (juce::Graphics& g, juce::Rectangle<float> b)
    {
        // a red dot that flashes while recording
        if (recorder.isRecording() && ! recorder.isWaiting() && (juce::Time::getMillisecondCounter() / 500) % 2 == 0)
        {
            const auto c = juce::Point<float> (b.getX() + 10.0f, b.getCentreY());
            psyched::paintLed (g, c, juce::Colours::red, true, 8.0f);
        }
    };
    recButton.setTooltip ("Record the master output (Settings > RECORD: folder, format, source, cue / split)");
    recButton.onClick = [this] { if (recorder.isRecording()) stopRecording(); else startRecording (false); };
    masterVolume.setSliderStyle (juce::Slider::LinearHorizontal);
    masterVolume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterVolume.setRange (0.0, 1.0, 0.0);
    masterVolume.setSkewFactor (2.0);
    masterVolume.setDoubleClickReturnValue (true, 1.0);
    masterVolume.setValue (settings.masterVolume, juce::dontSendNotification);
    masterVolume.setTooltip ("Centrifuge's output volume (double-click = full)");
    masterVolume.setColour (juce::Slider::trackColourId, Theme::accent);
    masterVolume.onValueChange = [this] { settings.masterVolume = (float) masterVolume.getValue(); engine.setMasterVolume (settings.masterVolume); };
    masterVolume.onDragEnd = [this] { settings.save(); };

    addChildComponent (about);
    addChildComponent (exportView);
    addChildComponent (conflictsView);

    SettingsView::xzScreenStatus = [this] { return xzLink.getStatus(); };
    xzLink.onTouch = [this] (int type, int x, int y) { xzView.touch (type, x, y); };
    // the XZ screen works like the play page: lanes, split, zoom and grid stay the same in both places
    xzView.getWaves().onLaneSplitChanged = waves.onLaneSplitChanged;
    xzView.getWaves().onLaneStyleChosen = waves.onLaneStyleChosen;
    xzView.getWaves().onPlayheadMoved = waves.onPlayheadMoved;
    xzView.getWaves().onZoomChanged = [this] (double s) { settings.visibleSeconds = s; settings.save(); applySettings(); };
    xzView.getPhaseMeter().foldFor = phaseMeter.foldFor;
    xzView.onGridToggled = [this]
    {
        settings.overviewGrid = ! settings.overviewGrid;
        settings.save();
        applySettings();
    };
    xzView.setEditor (&editor);
    xzView.getPhaseMeter().onOptionsChanged = [this] (int bars, bool, bool)
    {
        // chosen on the XZ screen: the computer's meter follows
        settings.phraseBars = bars;
        settings.save();
        phaseMeter.setOptions (bars, false, false);
        resized();
        repaint();
    };
    xzView.setListRunning = [] { return UserLists::get().setListRunning(); };
    xzView.onSetList = [this] (int choice)
    {
        // from the XZ screen: 0 = stop the set list, 1 = start it and record, 2 = start it without recording
        if (choice == 0) { if (UserLists::get().setListRunning()) toggleSetList(); }
        else startSetList (choice == 1);
    };
    xzView.onSearchTapped = [this]
    {
        // typing on the computer's keyboard goes straight into the browser's search
        if (auto* top = getTopLevelComponent()) top->toFront (true);
        browser.focusSearch();
    };
    xzView.onDeckColour = deck1.onDeckColour;
    xzView.onEditViewToggled = deck1.onEditViewToggled;
    xzTimer.f = [this]
    {
        const bool on = xzLink.isEnabled() && xzLink.isConnected();
        if (on && ! xzWasStreaming) xzView.startSplash();   // the start-up animation and logo when streaming starts
        xzWasStreaming = on;
        if (on) xzLink.submitFrame (xzView.render());
    };
    xzTimer.startTimerHz (25);

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
    xzTimer.stopTimer();
    SettingsView::xzScreenStatus = nullptr;
    xzLink.onTouch = nullptr;
    stopImport = true;
    importPool.removeAllJobs (false, 20000);
    stemPreviewPool.removeAllJobs (true, 5000);
    settings.audioDeviceState = engine.saveDeviceState();
    settings.browserColumns = browser.getColumnState();
    settings.save();
    editor.poll();
    midi.saveToFile (AppSettings::mappingFile());
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainComponent::applySettings()
{
    Theme::deckColours()[0] = juce::Colour (settings.deckColours[0]);
    Theme::deckColours()[1] = juce::Colour (settings.deckColours[1]);
    Theme::deckColours()[2] = juce::Colour (settings.deckColours[2]);
    for (size_t s = 0; s < 4; ++s) Theme::stemColours()[s] = juce::Colour (settings.stemColours[s]);
    MusicalKey::camelotDisplay() = settings.camelotKeys;
    overviewMarkerScale() = settings.markerScale;
    for (auto* dp : { &deck1, &deck2 }) dp->repaint();
    engine.setPreviewOutput (settings.previewOutput);
    repaint();
    browser.repaint();
    for (int d = 0; d < AudioEngine::maxDecks; ++d)
        engine.setDeckOutput (d, settings.deckOutputs[(size_t) d]);
    if (settings.sumToMaster >= 0)
        engine.setSumToMaster (settings.sumToMaster == 1);

    router.setJogSensitivity (settings.jogTicksPerRev);
    for (int d = 0; d < 2; ++d)
    {
        auto& dk = engine.deck (d);
        dk.post (Deck::Cmd::vinylTime, settings.brakeSec[(size_t) d], 0);
        dk.post (Deck::Cmd::brakeBeats, settings.brakeBeats[(size_t) d]);
        dk.post (Deck::Cmd::vinylTime, settings.startSec[(size_t) d], 1);
        dk.post (Deck::Cmd::vinylTime, settings.releaseSec[(size_t) d], 2);
        dk.post (Deck::Cmd::vinylBrakeSet, settings.brakeOn[(size_t) d] ? 1.0 : 0.0, 0);
        dk.post (Deck::Cmd::vinylBrakeSet, settings.startOn[(size_t) d] ? 1.0 : 0.0, 1);
        dk.post (Deck::Cmd::throwSet, settings.throwOn ? settings.throwSpeed : 0.0);
    }
    engine.setMasterVolume (settings.masterVolume);
    midi.setDeckControllers (settings.deckController[0], settings.deckController[1]);
    midi.setLedFeedbackEnabled (settings.ledFeedback);
    midi.setLongPressSeconds (settings.longPressSec);
    midi.setHotCuesInColour (settings.hotCuePadColours);
    midi.setJogDisplay (settings.jogDisplay);
    waves.setVisibleSeconds (settings.visibleSeconds);
    waves.setMaxFps (settings.maxFps);
    for (int d = 0; d < 2; ++d)
    {
        waves.setLanes (d, { settings.laneStyle[(size_t) d][0], settings.laneShape[(size_t) d][0] },
                           { settings.laneStyle[(size_t) d][1], settings.laneShape[(size_t) d][1] });
        waves.setLaneSplit (d, settings.laneSplit[(size_t) d]);
    }
    waves.setSingleColour (juce::Colour (settings.singleColour), settings.singleFollowsDeck);
    waves.setStemLayers (settings.stemLayerOrder, settings.stemOpacity);
    for (int d = 0; d < 2; ++d) waves.setEditView (d, settings.editView[(size_t) d]);
    waves.forceRedraw();
    {
        // PREP's own waveform
        auto& pw = prepView.getWaves();
        pw.setMaxFps (settings.maxFps);
        pw.setLanes (0, { settings.prepLaneStyle[0], settings.prepLaneShape[0] }, { settings.prepLaneStyle[1], settings.prepLaneShape[1] });
        pw.setLaneSplit (0, settings.prepLaneSplit);
        pw.setSingleColour (juce::Colour (settings.singleColour), settings.singleFollowsDeck);
        pw.setStemLayers (settings.stemLayerOrder, settings.stemOpacity);
        pw.setEditView (0, settings.editView[2]);
        pw.setVisibleSeconds (settings.prepVisibleSeconds);
        pw.forceRedraw();
        prepView.setOverviewStyle (settings.prepOverviewStyle, settings.prepOverviewShape == 1,
                                   settings.singleFollowsDeck ? Theme::deckColour (2) : juce::Colour (settings.singleColour));
        prepView.setOverviewGrid (settings.overviewGrid);
    }
    for (auto* dp : { &deck1, &deck2 })
    {
        dp->setOverviewGrid (settings.overviewGrid);
    }
    // What each stem control does (level / low-pass / high-pass), per deck
    for (int d = 0; d < 3; ++d)
        for (int s = 0; s < 4; ++s)
            engine.deck (d == 2 ? (int) AudioEngine::prepDeck : d).post (Deck::Cmd::stemMode, (double) settings.stemModes[(size_t) d][(size_t) s], s);
    deck1.setEditView (settings.editView[0]);
    deck2.setEditView (settings.editView[1]);
    prepView.setEditView (settings.editView[2]);
    waves.setPlayheadPosition (settings.playheadPosition);
    {
        // The XDJ-XZ screen shows the decks the same way as the play page
        auto& xw = xzView.getWaves();
        xw.setVisibleSeconds (settings.visibleSeconds);
        for (int d = 0; d < 2; ++d)
        {
            xw.setLanes (d, { settings.laneStyle[(size_t) d][0], settings.laneShape[(size_t) d][0] },
                            { settings.laneStyle[(size_t) d][1], settings.laneShape[(size_t) d][1] });
            xw.setLaneSplit (d, settings.laneSplit[(size_t) d]);
            xw.setEditView (d, settings.editView[(size_t) d]);
        }
        xw.setSingleColour (juce::Colour (settings.singleColour), settings.singleFollowsDeck);
        xw.setStemLayers (settings.stemLayerOrder, settings.stemOpacity);
        xw.setPlayheadPosition (settings.playheadPosition);
        xw.extraLead = settings.xzScreenLeadMs / 1000.0;
        xw.forceRedraw();
        xzView.setOverviewStyle (settings.overviewStyle, settings.overviewShape == 1, juce::Colour (settings.singleColour), settings.singleFollowsDeck);
        xzView.setOverviewGrid (settings.overviewGrid);
        xzView.getPhaseMeter().setOptions (settings.phraseBars, settings.phraseFollow, false);
        xzLink.setEnabled (settings.xzScreen);
    }
    for (auto* dp : { &deck1, &deck2 })
        dp->setOverviewStyle (settings.overviewStyle, settings.overviewShape == 1,
                              settings.singleFollowsDeck ? Theme::deckColour (dp == &deck1 ? 0 : 1) : juce::Colour (settings.singleColour));
    router.setStemMatrix (settings.stemMatrix);
    for (int i = 0; i < 8; ++i)
    {
        router.setJumpSlotBeats (i / 4, i % 4, settings.jumpBeats[(size_t) i]);
        router.setJumpSlotColour (i / 4, i % 4, settings.jumpColours[(size_t) i]);
    }
    browser.setPreviewStyle (settings.previewStyle, settings.previewShape == 1, juce::Colour (settings.previewColour));
    phaseMeter.setOptions (settings.phraseBars, settings.phraseFollow, settings.phraseBrackets);
    DeckPanel::artistFirst = settings.artistFirst;
    deck1.repaint(); deck2.repaint();

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
    ss.lossless = settings.stemsLossless;
    ss.findModel = [s = settings] (const juce::String& n) { return s.findModel (n); };
    stems.setSettings (ss);

    settings.save();
}

void MainComponent::loadToDeck (int deck, const TrackInfo& info, bool fromPc)
{
    loadToDeck (deck, info, fromPc, false);
}

void MainComponent::loadToDeck (int deck, const TrackInfo& info, bool fromPc, bool confirmed)
{
    if (deck >= 2)
    {
        // PREP / preview deck: never on air, so it's fine to stop it and load
        engine.deck (deck).post (Deck::Cmd::pause);
        loader.load (deck, info, fromPc, editor.versionToLoad (deck, info));
        return;
    }
    if (engine.deck (deck).state().playing.load() && ! confirmed)
    {
        // Loading onto a playing deck: ask first (the browser's select button = LOAD, back = CANCEL)
        if (loadConfirm != nullptr) return;
        loadConfirm = std::make_unique<juce::AlertWindow> ("Deck " + juce::String (deck + 1) + " is playing",
                                                           "Load " + info.displayArtistTitle() + " onto it anyway?",
                                                           juce::MessageBoxIconType::WarningIcon, this);
        loadConfirm->addButton ("LOAD", 1, juce::KeyPress (juce::KeyPress::returnKey));
        loadConfirm->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MainComponent> safe (this);
        loadConfirm->enterModalState (true, juce::ModalCallbackFunction::create ([safe, deck, info, fromPc] (int r)
        {
            if (safe == nullptr) return;
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->loadConfirm.reset(); });
            if (r == 1) safe->loadToDeck (deck, info, fromPc, true);
        }), false);
        return;
    }
    if (engine.deck (deck).state().playing.load())
        engine.deck (deck).post (Deck::Cmd::pause);
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
    else if (id == "browse.scroll")
    {
        // the browse knob zooms the scrolling waveforms while there's no browser to scroll: on the XZ screen when
        // its browser is closed (streaming), on the computer when the browser is hidden
        const bool browserUp = xzLink.isEnabled() && xzLink.isConnected() ? xzView.isBrowserShown()
                                                                             : browser.isShowing() && ! browser.isCollapsed();
        if (browserUp) browser.scroll ((int) std::round (v.delta));
        else zoomWaves (v.delta);
    }
    else if (id == "view.zoom") zoomWaves (v.delta);
    else if (id == "xz.stream")       { if (v.down) xzLink.sendCommand (1); }
    else if (id == "xz.stems")        { if (v.down) xzLink.sendCommand (2); }
    else if (id == "xz.mods")         { if (v.down) xzLink.sendCommand (3); }
    else if (id == "xz.browser")      { if (v.down) xzView.setBrowserShown (! xzView.isBrowserShown()); }
    else if (id == "browse.preview")  { if (v.down) browser.previewPlayStop(); }
    else if (id == "browse.enter")   { if (v.down) { if (loadConfirm != nullptr) loadConfirm->exitModalState (1); else browser.enter(); } }
    else if (id == "browse.back")    { if (v.down) { if (loadConfirm != nullptr) loadConfirm->exitModalState (0); else browser.back(); } }
    else if (id == "track.prev" || id == "track.next")
    {
        // previous / next track in the browser list (from the one on the deck); |<< near the start of a track goes back to its start first
        if (! v.down || deck < 0 || deck > 1) return;
        auto& dk = engine.deck (deck);
        auto t = dk.state().trackSerial.load() != 0 ? dk.getLoadedTrack() : nullptr;
        if (id == "track.prev" && t != nullptr && dk.state().positionSec.load() > 3.0) { dk.post (Deck::Cmd::seekSeconds, 0.0); return; }
        browser.loadNeighbour (deck, t != nullptr ? t->info.file : juce::File(), id == "track.next" ? 1 : -1);
    }
    else if (id == "browse.tree")    { if (v.down) browser.toggleFocus(); }
    else if (id == "browse.load1")   { if (v.down) browser.loadSelected (0); }
    else if (id == "browse.load2")   { if (v.down) browser.loadSelected (1); }
    else if (id == "view.browser")   { if (v.down) { settings.showBrowser = ! settings.showBrowser; resized(); settings.save(); if (settings.showBrowser) browser.focusTracks(); } }
    else if (id == "view.zoom")      { waves.setVisibleSeconds (waves.getVisibleSeconds() * std::pow (1.1, -v.delta)); }
    else if (id == "master.volume")  masterVolume.setValue (v.value);
    else if (id == "browse.tag")     { if (v.down) { auto sel = browser.getSelectedTracks(); if (! sel.empty()) trackAction ({ sel.front() }, "toplay.toggle"); } }
    else if (id == "browse.scrub")   browser.scrubPreview (v.delta);
    else if (id == "browse.scrubpos") browser.scrubPreviewTo (v.value);
    else if (id == "setlist.toggle") { if (v.down) toggleSetList(); }
    else if (id == "record.toggle")  { if (v.down) { if (recorder.isRecording()) stopRecording(); else startRecording (false); } }
    else if (id == "view.zoomin")    { if (v.down) waves.setVisibleSeconds (waves.getVisibleSeconds() / 1.25); }
    else if (id == "view.zoomout")   { if (v.down) waves.setVisibleSeconds (waves.getVisibleSeconds() * 1.25); }
    else if (id == "view.wavemode")  { if (v.down) waves.setStemsHidden (! waves.getStemsHidden()); }
    else if (id == "view.fold")      { if (v.down) { const bool on = ! settings.editView[0]; settings.editView = { on, on, on }; settings.save(); applySettings(); } }
}

void MainComponent::showPage (int p)
{
    page = p;
    juce::TextButton* tabs[] = { &tabPlay, &tabMapping, &tabStems, &tabSettings, &tabPrep };
    for (int i = 0; i < 5; ++i)
        tabs[i]->setToggleState (i == p, juce::dontSendNotification);
    if (p == 3) settingsView.refreshValues();
    if (p == 2) stemSettingsView.refreshValues();

    deck1.setVisible (p == 0 && ! browserExpanded);
    deck2.setVisible (p == 0 && ! browserExpanded);
    waves.setVisible (p == 0 && ! browserExpanded);
    browser.setVisible ((p == 0 && (settings.showBrowser || browserExpanded)) || p == 4);
    prepView.setVisible (p == 4 && ! browserExpanded);
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

    // Stems finished (or loaded from the cache) on a deck: keep a small stems preview for the browser
    for (int d = 0; d < 3; ++d)
    {
        const auto& dst = engine.deck (d).state();
        const int serial = dst.trackSerial.load();
        if (serial == 0 || serial == stemPreviewSerial[(size_t) d] || ! dst.stemsReady.load() || dst.stemsProgress.load() < 0.999f) continue;
        auto stemsNow = engine.deck (d).getStemsForUi();
        auto t = engine.deck (d).getLoadedTrack();
        if (stemsNow == nullptr || t == nullptr) continue;
        stemPreviewSerial[(size_t) d] = serial;
        const auto path = t->info.file.getFullPathName();
        juce::Component::SafePointer<MainComponent> safe (this);
        stemPreviewPool.addJob ([safe, stemsNow, path]
        {
            const auto ps = PreviewWaveCache::makeStems (*stemsNow);
            PreviewWaveCache::saveStems (path, ps);
            juce::MessageManager::callAsync ([safe, ps, path] { if (safe != nullptr && ! ps.isEmpty()) safe->browser.notePreviewStems (path, ps.stem); });
        });
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

    const auto newMidi = midi.getControllerSummary();
    for (int d = 0; d < 2; ++d)
    {
        // brake / start switched from the controller or the deck: remember it
        const auto& st = engine.deck (d).state();
        if (st.brakeOn.load() != settings.brakeOn[(size_t) d] || st.startOn.load() != settings.startOn[(size_t) d])
        {
            settings.brakeOn[(size_t) d] = st.brakeOn.load();
            settings.startOn[(size_t) d] = st.startOn.load();
            settings.save();
        }

    }
    {
        // REC button: time while recording, ARMED while it waits for sound
        juce::String t = "REC";
        if (recorder.isRecording())
        {
            const int sec = (int) recorder.elapsedSeconds();
            t = recNaming ? juce::String ("SAVING") : recorder.isWaiting() ? juce::String ("ARMED") : juce::String::formatted ("REC %d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
        }
        const int recState = ! recorder.isRecording() ? ButtonStyle::off : recorder.isWaiting() ? ButtonStyle::alt : ButtonStyle::on;
        if (t != recText || recState != recButton.altState)
        {
            recText = t;
            recButton.setButtonText (t);
            recButton.altState = recState;
        }
        if (recorder.isRecording()) recButton.repaint();   // the flashing dot
    }
    if (std::abs (engine.getMasterPeak() - shownPeak) > 0.01f) { shownPeak = engine.getMasterPeak(); repaint (masterArea); }
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
    {
        // The user's own wordmark (transparent PNG), fitted to the top bar
        static const juce::Image mark = juce::ImageCache::getFromMemory (BinaryData::centrifugetopbarlogo_png, BinaryData::centrifugetopbarlogo_pngSize);
        if (mark.isValid())
            g.drawImageWithin (mark, (int) logo.getX(), (int) logo.getY() + 8, (int) logo.getWidth(), (int) logo.getHeight() - 16,
                               juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid | juce::RectanglePlacement::onlyReduceInSize);
    }

    // Status pills (right side)
    auto pills = topBar.withSizeKeepingCentre (topBar.getWidth(), 34).reduced (10, 0).withLeft (phaseArea.getRight() + 14 + 30)
                       .withRight (recButton.getX() - 8).toFloat();
    float leftmostPill = (float) recButton.getX() - 8.0f;
    statusMenuRect = {};
    if (statusCompact)
    {
        // everything in one STATUS menu next to REC, its dot the worst of them
        pills = {};
        auto pr = juce::Rectangle<float> ((float) recButton.getX() - 8.0f - 112.0f, (float) recButton.getY(), 112.0f, (float) recButton.getHeight());
        const bool bad = audioStatus.startsWith ("No") || midiStatus == "No device detected" || cpu >= 0.8;
        drawPill (g, pr, juce::String ("STATUS ") + juce::String::charToString (0x25be), bad ? Theme::bad : cpu >= 0.5 ? Theme::warn : Theme::good);
        statusMenuRect = pr.toNearestInt();
        leftmostPill = pr.getX();
    }
    {
        // master volume: label, and a peak meter under the slider
        auto mr = masterArea.toFloat();
        psyched::paintLabel (g, "MASTER", mr.removeFromLeft (52.0f), juce::Justification::centredLeft, Theme::textDim, 9.5f);
        auto meter = masterVolume.getBounds().toFloat().removeFromBottom (3.0f).reduced (6.0f, 0.0f);
        g.setColour (psyched::Colours::edge2);
        g.fillRect (meter);
        const float pk = juce::jlimit (0.0f, 1.0f, shownPeak);
        g.setColour (pk > 0.98f ? Theme::bad : pk > 0.7f ? Theme::warn : Theme::good);
        g.fillRect (meter.withWidth (meter.getWidth() * std::sqrt (pk)));
    }
    auto pill = [&] (const juce::String& text, juce::Colour dot, float w)
    {
        if (pills.getWidth() < w) return;   // no room (narrow window): left out rather than squashed
        auto pr = pills.removeFromRight (w);
        drawPill (g, pr, text, dot);
        leftmostPill = pr.getX();
        pills.removeFromRight (8.0f);
    };
    pill ("CPU " + juce::String (juce::roundToInt (cpu * 100.0)) + "%", cpu < 0.5 ? Theme::good : (cpu < 0.8 ? Theme::warn : Theme::bad), 84.0f);
    pill (audioStatus, audioStatus.startsWith ("No") ? Theme::bad : Theme::good, juce::jmin (330.0f, psyched::Fonts::label().getStringWidthFloat (audioStatus.toUpperCase()) + 34.0f));
    {
        const bool none = midiStatus == "No device detected";
        pill (midiStatus, none ? Theme::bad : Theme::good, juce::jmin (260.0f, psyched::Fonts::label().getStringWidthFloat (midiStatus.toUpperCase()) + 34.0f));
    }
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
    // the fold button follows the leftmost box
    if (const int fx = (int) leftmostPill - 30; fx != statusFold.getX())
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::MessageManager::callAsync ([safe, fx] { if (safe != nullptr) safe->statusFold.setTopLeftPosition (fx, safe->statusFold.getY()); });
    }
}

void MainComponent::resized()
{
    if (splash != nullptr) splash->setBounds (getLocalBounds());
    about.setBounds (getLocalBounds());
    exportView.setBounds (getLocalBounds());
    conflictsView.setBounds (getLocalBounds());
    auto r = getLocalBounds();
    // the top bar grows a little while the phrase meter shows its bar brackets
    const int bracketRoom = settings.phraseBrackets ? (int) std::ceil (PhaseMeter::bracketLevels (settings.phraseFollow ? 64 : settings.phraseBars)
                                                                       * PhaseMeter::bracketStep + 2.0f) : 0;   // one set of brackets (shared between the meters)
    topBar = r.removeFromTop (52 + bracketRoom);
    masterArea = topBar.withSizeKeepingCentre (topBar.getWidth(), 34).reduced (10, 0).removeFromRight (190);
    masterVolume.setBounds (masterArea.withTrimmedLeft (54).reduced (0, 6));
    recButton.setBounds (masterArea.getX() - 112, masterArea.getY(), 104, masterArea.getHeight());   // as tall as the status boxes

    // page buttons, a little apart
    auto tabs = topBar.withTrimmedLeft (262).withWidth (5 * 92).withSizeKeepingCentre (5 * 92, 28);
    for (auto* t : { &tabPlay, &tabPrep, &tabMapping, &tabStems, &tabSettings })
    {
        t->setBounds (tabs.removeFromLeft (86));
        tabs.removeFromLeft (6);
    }
    // Phrase meter: both decks stacked, to the right of the tabs; the fold button after it (folded: the meter
    // takes the status boxes' room, up to REC)
    // The meter is as wide as its length needs (its blocks never stretch). When it and the status boxes don't both
    // fit, the status boxes become one STATUS menu next to REC.
    const int meterX = 262 + 5 * 92 + 18;
    const int meterH = topBar.getHeight() - 20;
    int meterW = (int) PhaseMeter::widthFor (settings.phraseBars, (float) meterH);
    const int roomRight = recButton.getX() - 8;
    statusCompact = settings.statusFolded || meterX + meterW + 44 + (int) fullStatusWidth() > roomRight;
    meterW = juce::jmin (meterW, roomRight - (statusCompact ? 130 : 0) - 14 - meterX);
    phaseArea = juce::Rectangle<int> (meterX, topBar.getY() + 10, juce::jmax (120, meterW), meterH);
    phaseMeter.setBounds (phaseArea);
    statusFold.setVisible (! statusCompact || settings.statusFolded);
    statusFold.setButtonText (settings.statusFolded ? "<" : ">");
    // the fold button sits just left of the status boxes (folded: just left of REC); paint() keeps it there
    statusFold.setBounds (recButton.getX() - 30, topBar.getCentreY() - 14, 22, 28);

    deck1.setVisible (page == 0 && ! browserExpanded);
    deck2.setVisible (page == 0 && ! browserExpanded);
    waves.setVisible (page == 0 && ! browserExpanded);
    prepView.setVisible (page == 4 && ! browserExpanded);
    if ((page == 0 || page == 4) && browserExpanded)
    {
        // The browser fills the screen (EXPAND / RESTORE on the browser)
        dividerDecks = dividerBrowser = {};
        browser.setVisible (true);
        browser.setCollapsed (false);
        browser.setBounds (r);
    }
    else if (page == 0)
    {
        // The decks keep a fixed height
        // (plus the overview's bar grid strip)
        const int deckH = juce::jlimit (150, juce::jmax (150, r.getHeight() / 2), settings.deckPanelHeight) + (int) DeckPanel::gridStripH + 1;
        auto decks = r.removeFromTop (deckH);
        deck1.setBounds (decks.removeFromLeft (decks.getWidth() / 2).withTrimmedRight (1));
        deck2.setBounds (decks);
        dividerDecks = {};
        r.removeFromTop (4);

        if (settings.showBrowser)
        {
            const int browserH = juce::jlimit (120, juce::jmax (120, r.getHeight() - 160), settings.browserHeight);
            browser.setBounds (r.removeFromBottom (browserH));
            dividerBrowser = r.removeFromBottom (5);
            waves.setBounds (r);
        }
        else
        {
            // hidden: just the browser's top bar stays (its SHOW button brings it back)
            dividerBrowser = {};
            browser.setBounds (r.removeFromBottom (BrowserView::barHeight));
            waves.setBounds (r);
        }
        browser.setCollapsed (! settings.showBrowser);
        browser.setVisible (true);
    }
    else if (page == 4)
    {
        dividerDecks = dividerBrowser = {};
        const int prepH = juce::jmax (360, (int) (r.getHeight() * 0.6));
        prepView.setBounds (r.removeFromTop (prepH));
        r.removeFromTop (4);
        browser.setCollapsed (false);
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
    const bool over = page == 0 && dividerBrowser.contains (e.getPosition());
    setMouseCursor (over ? juce::MouseCursor::UpDownResizeCursor
                         : logoArea.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

float MainComponent::fullStatusWidth() const
{
    // the status boxes side by side (as paint() lays them out)
    float w = 84.0f + 8.0f;
    w += juce::jmin (330.0f, psyched::Fonts::label().getStringWidthFloat (audioStatus.toUpperCase()) + 34.0f) + 8.0f;
    w += juce::jmin (260.0f, psyched::Fonts::label().getStringWidthFloat (midiStatus.toUpperCase()) + 34.0f) + 8.0f;
    w += juce::jmin (260.0f, psyched::Fonts::label().getStringWidthFloat (stemStatus.toUpperCase()) + 34.0f);
    return w;
}

void MainComponent::showStatusMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Status");
    auto line = [&m] (const juce::String& text, juce::Colour c) { juce::PopupMenu::Item it (text); it.colour = c; it.isEnabled = false; m.addItem (it); };
    line ("CPU " + juce::String (juce::roundToInt (cpu * 100.0)) + "%", cpu < 0.5 ? Theme::good : (cpu < 0.8 ? Theme::warn : Theme::bad));
    line (audioStatus, audioStatus.startsWith ("No") ? Theme::bad : Theme::good);
    line (midiStatus, midiStatus == "No device detected" ? Theme::bad : Theme::good);
    if (stemStatus.isNotEmpty()) line (stemStatus, Theme::accent);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (statusMenuRect)));
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    dragging = 0;
    if (statusMenuRect.contains (e.getPosition())) { showStatusMenu(); return; }
    if (logoArea.contains (e.getPosition())) { about.show(); return; }
    if (page != 0) return;
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
    const int lc = centrifugeLibrary.getChangeCount() * 100000 + UserLists::get().version();
    if (sourceCache[(size_t) view] == nullptr || sourceCacheVersion[(size_t) view] != lc)
    {
        auto src = centrifugeLibrary.makeSource (view);
        withUserLists (*src);
        sourceCache[(size_t) view] = src;
        sourceCacheVersion[(size_t) view] = lc;
    }
    return sourceCache[(size_t) view];
}

void MainComponent::withUserLists (LibrarySource& src)
{
    // The library tree: LOCATIONS (imported folders, as on the drive), PLAYLISTS (your own), TO PLAY, PLAY HISTORY
    auto& ul = UserLists::get();
    std::map<juce::int64, int> indexOf;
    for (int i = 0; i < (int) src.tracks.size(); ++i) indexOf[src.tracks[(size_t) i].libraryId] = i;
    auto indices = [&] (const std::vector<juce::int64>& ids, bool keepRepeats)
    {
        std::vector<int> r;
        for (auto id : ids)
            if (auto it = indexOf.find (id); it != indexOf.end())
                if (keepRepeats || std::find (r.begin(), r.end(), it->second) == r.end()) r.push_back (it->second);
        return r;
    };

    PlaylistNode locations;
    locations.id = "loc"; locations.name = "Locations"; locations.isFolder = true;
    locations.children = std::move (src.playlists.children);

    PlaylistNode mine;
    mine.id = "pl"; mine.name = "Playlists"; mine.isFolder = true;
    std::function<void (PlaylistNode&, juce::int64)> build = [&] (PlaylistNode& node, juce::int64 parent)
    {
        std::vector<const UserLists::List*> kids;
        for (auto& l : ul.lists()) if (l.parent == parent) kids.push_back (&l);
        std::sort (kids.begin(), kids.end(), [] (auto* a, auto* b) { return a->seq < b->seq; });
        for (auto* k : kids)
        {
            PlaylistNode c;
            c.id = "u" + juce::String (k->id);
            c.name = k->name;
            c.isFolder = k->kind == UserLists::folder;
            c.isSmart = k->kind == UserLists::dynamic;
            if (c.isFolder) build (c, k->id);
            else if (c.isSmart)
            {
                for (int i = 0; i < (int) src.tracks.size(); ++i)
                {
                    const auto& t = src.tracks[(size_t) i];
                    const int q = k->rules.stems != 0 ? stems.cachedQuality (t) : -1;
                    if (k->rules.any() && ul.matches (k->rules, t, q)) c.trackIndices.push_back (i);
                }
            }
            else c.trackIndices = indices (k->tracks, false);
            node.children.push_back (std::move (c));
        }
    };
    build (mine, 0);

    PlaylistNode toPlay;
    toPlay.id = "toplay"; toPlay.name = "Tag List  (" + juce::String ((int) ul.toPlay().size()) + ")";
    toPlay.trackIndices = indices (ul.toPlay(), false);

    PlaylistNode history;
    history.id = "hist"; history.name = "Play history"; history.isFolder = true;
    PlaylistNode sessions, sets;
    sessions.id = "hist.s"; sessions.name = "Session history"; sessions.isFolder = true;
    sets.id = "hist.l"; sets.name = "Set list history"; sets.isFolder = true;
    auto historyNode = [&] (const UserLists::History& h, const juce::String& id, const juce::String& name)
    {
        PlaylistNode n;
        n.id = id;
        n.name = name;
        std::vector<juce::int64> ids;
        for (auto& p : h.tracks) ids.push_back (p.track);
        n.trackIndices = indices (ids, true);
        return n;
    };
    sessions.children.push_back (historyNode (ul.session(), "hs.now", "This session (" + ul.session().name + ")"));
    for (int i = (int) ul.sessions().size(); --i >= 0;)
        sessions.children.push_back (historyNode (ul.sessions()[(size_t) i], "hs." + juce::String (i), ul.sessions()[(size_t) i].name));
    if (ul.setListRunning())
        sets.children.push_back (historyNode (ul.runningSetList(), "hl.now", ul.runningSetList().name + "  (recording)"));
    for (int i = (int) ul.setLists().size(); --i >= 0;)
        sets.children.push_back (historyNode (ul.setLists()[(size_t) i], "hl." + juce::String (i), ul.setLists()[(size_t) i].name));
    history.children.push_back (std::move (sessions));
    history.children.push_back (std::move (sets));

    src.playlists.children.clear();
    src.playlists.children.push_back (std::move (locations));
    src.playlists.children.push_back (std::move (mine));
    src.playlists.children.push_back (std::move (toPlay));
    src.playlists.children.push_back (std::move (history));
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

void MainComponent::addMusic (const juce::Array<juce::File>& items, int subfolders)
{
    if (importing.load()) { flash ("An import is already running"); return; }
    if (subfolders < 0)
    {
        bool hasSub = false;
        for (auto& f : items)
            hasSub = hasSub || (f.isDirectory() && ! f.findChildFiles (juce::File::findDirectories, false).isEmpty());
        if (hasSub)
        {
            juce::Component::SafePointer<MainComponent> safe (this);
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Add music")
                                            .withMessage ("This folder has folders inside it.")
                                            .withButton ("Include subfolders").withButton ("This folder only").withButton ("Cancel"),
                                          [safe, items] (int r) { if (safe != nullptr && r != 0) safe->addMusic (items, r == 1 ? 1 : 0); });
            return;
        }
        subfolders = 1;
    }
    importing = true;
    importProgress = 0.0f;
    {
        const juce::ScopedLock sl (importNameLock);
        importName = "Adding music";
    }
    FileImporter::Options o;
    std::tie (o.minBpm, o.maxBpm) = settings.analysisBpmRange();
    o.mp3OffsetMs = settings.mp3OffsetMs;
    o.subfolders = subfolders != 0;
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
    auto* w = new juce::AlertWindow (hotCue ? "Name hot cue " + juce::String::charToString ((juce::juce_wchar) ('A' + index)) : juce::String ("Name marker"),
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
    if (action == "reveal")
    {
        juce::Array<juce::File> files;
        for (auto& t : tracks) files.add (t.file);
        showInExplorer (files);
        return;
    }
    // playlists, TO PLAY, tags
    if (action.startsWith ("pl.add:") || action == "pl.new" || action == "toplay.toggle" || action.startsWith ("list.remove:")
        || action.startsWith ("tag:") || action == "tags.edit")
    {
        auto& ul = UserLists::get();
        std::vector<juce::int64> ids;
        for (auto& t : tracks) if (const auto id = libraryIdFor (t, true); id != 0) ids.push_back (id);
        if (action.startsWith ("pl.add:"))
        {
            ul.addTracks (action.fromFirstOccurrenceOf (":", false, false).getLargeIntValue(), ids);
            flash ("Added " + juce::String ((int) ids.size()) + (ids.size() == 1 ? " track" : " tracks"));
        }
        else if (action == "pl.new")
            askText ("New playlist", "", [this, ids] (juce::String name)
            {
                if (name.isEmpty()) return;
                UserLists::get().addTracks (UserLists::get().add (0, name, UserLists::playlist), ids);
                browser.refreshSources();
            });
        else if (action == "toplay.toggle")
        {
            int added = 0;
            for (auto id : ids) added += ul.toggleToPlay (id) ? 1 : 0;
            flash (added > 0 ? "Added to the Tag List" : "Removed from the Tag List");
        }
        else if (action.startsWith ("list.remove:"))
        {
            const auto node = action.fromFirstOccurrenceOf (":", false, false);
            if (node == "toplay") for (auto id : ids) ul.removeFromToPlay (id);
            else if (node.startsWith ("u")) ul.removeTracks (node.substring (1).getLargeIntValue(), ids);
        }
        else if (action.startsWith ("tag:"))
        {
            const bool on = action.fromFirstOccurrenceOf ("tag:", false, false).startsWith ("1");
            const auto tag = action.fromFirstOccurrenceOf ("tag:", false, false).fromFirstOccurrenceOf (":", false, false);
            for (auto id : ids) ul.setTag (id, tag, on);
        }
        else if (action == "tags.edit")
            juce::CallOutBox::launchAsynchronously (std::make_unique<TagsEditor> ([this] { browser.refreshSources(); }),
                                                    browser.getScreenBounds().withSizeKeepingCentre (10, 10), nullptr);
        browser.refreshSources();
        return;
    }
    if (action == "key.keep" || action == "key.alt")
    {
        int n = 0;
        for (auto& t : tracks)
        {
            const auto id = libraryIdFor (t, false);
            const auto* e = id != 0 ? KeyOpinions::get (id) : nullptr;
            if (e == nullptr) continue;
            if (action == "key.alt")
                if (auto lt = centrifugeLibrary.get (id)) { auto info = lt->info; info.key = e->alt; centrifugeLibrary.setInfo (id, info); }
            KeyOpinions::clear (id);
            ++n;
        }
        browser.refreshSources();
        flash (juce::String (action == "key.alt" ? "Second opinion key used on " : "Kept Centrifuge's key on ") + juce::String (n) + (n == 1 ? " track" : " tracks"));
        return;
    }
    if (action == "analyse" || action == "analyse.again")
    {
        analyseTracks (tracks, action == "analyse.again");
        return;
    }
    if (action.startsWith ("clear."))
    {
        const juce::String what = action == "clear.locators" ? "all markers" : action == "clear.skips" ? "all skips"
                                : action == "clear.hotcues" ? "all hot cues" : action == "clear.phrases" ? "all phrases"
                                : action == "clear.stemlocs" ? "all stem markers" : action == "clear.notes" ? "all notes"
                                : action == "clear.order" ? "the phrase order and repeats" : action == "clear.editversions" ? "the saved edit versions"
                                : "every marker, skip, hot cue, phrase, stem marker, note, phrase order and edit version";
        const auto n = juce::String ((int) tracks.size()) + (tracks.size() == 1 ? " track" : " tracks");
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Clear " + what + "?",
                                            "From " + n + " (" + tracks.front().displayArtistTitle() + (tracks.size() > 1 ? ", ..." : "") + ").\n"
                                            "The beat grid stays. This can't be undone.",
                                            "Clear", "Cancel", nullptr,
                                            juce::ModalCallbackFunction::create ([safe, tracks, action] (int r)
        {
            if (safe == nullptr || r != 1) return;
            safe->clearTracks (tracks, action);
        }));
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

void MainComponent::clearTracks (const std::vector<TrackInfo>& tracks, const juce::String& action)
{
    auto fn = [action] (TrackVersion& v)
    {
        const bool all = action == "clear.all";
        if (all || action == "clear.locators") v.memory.clear();
        if (all || action == "clear.skips") v.skips.clear();
        if (all || action == "clear.hotcues") for (auto& h : v.hot) h.reset();
        if (all || action == "clear.phrases") { v.phrases = std::vector<Phrase>(); v.arrangement.clear(); }
        if (all || action == "clear.stemlocs") v.stemLocs.clear();
        if (all || action == "clear.notes") v.notes.clear();
        if (all || action == "clear.order")
        {
            v.arrangement.clear();
            if (v.phrases) for (auto& p : *v.phrases) p.repeats = 1;
        }
        if (all || action == "clear.editversions") { v.editVersions.clear(); v.activeEdit = {}; }
    };
    int n = 0;
    for (auto& t : tracks)
    {
        const auto id = libraryIdFor (t, true);
        if (id == 0) continue;
        editor.editLibraryTrack (id, fn);
        browser.forgetPreview (t.file.getFullPathName());
        ++n;
    }
    flash (n > 0 ? "Cleared on " + juce::String (n) + (n == 1 ? " track" : " tracks") : juce::String ("Nothing to clear"));
}

void MainComponent::analyseTracks (const std::vector<TrackInfo>& tracks, bool replace)
{
    if (importing.load()) { flash ("Another job is running - try again when it's done"); return; }
    importing = true;
    importProgress = 0.0f;
    {
        const juce::ScopedLock sl (importNameLock);
        importName = "Analysing";
    }
    FileImporter::Options o;
    std::tie (o.minBpm, o.maxBpm) = settings.analysisBpmRange();
    o.mp3OffsetMs = settings.mp3OffsetMs;
    juce::Component::SafePointer<MainComponent> safe (this);
    importPool.addJob ([this, safe, tracks, o, replace]
    {
        int done = 0, failed = 0;
        for (size_t i = 0; i < tracks.size() && ! stopImport.load(); ++i)
        {
            const auto& t = tracks[i];
            {
                const juce::ScopedLock sl (importNameLock);
                importName = "Analysing " + t.displayArtistTitle();
            }
            importProgress = (float) i / (float) tracks.size();
            TrackInfo info;
            TrackVersion version;
            juce::String error;
            if (! FileImporter::analyseFile (fileLoader, t.file, o, info, version, error, &stopImport)) { ++failed; continue; }
            ++done;
            // Stems already made: the key from the bass stem (root) and the instrument / vocal stems (major or minor)
            if (stems.hasCachedStems (t))
                if (auto audio = fileLoader.load (t.file, error, &stopImport))
                    if (auto st = stems.loadCachedStems (t, *audio))
                        if (auto k = TrackAnalyser::detectKeyV2 (*audio, st.get(), &version.beats); k.isNotEmpty())
                            info.key = k;
            juce::MessageManager::callAsync ([safe, t, info, version, replace]
            {
                if (safe == nullptr) return;
                auto& self = *safe;
                const auto id = self.libraryIdFor (t, true);
                if (id != 0)
                {
                    // the grid: replaced, or only where there isn't one yet
                    self.editor.editLibraryTrack (id, [&version, replace] (TrackVersion& v)
                    {
                        if (replace || v.beats.size() < 2) v.beats = version.beats;
                    });
                    if (auto lt = self.centrifugeLibrary.get (id))
                    {
                        auto ni = lt->info;
                        if (replace || ni.bpm <= 0.0) ni.bpm = info.bpm;
                        if (replace || ni.key.isEmpty()) ni.key = info.key;
                        if (ni.durationSec <= 0.0) ni.durationSec = info.durationSec;
                        self.centrifugeLibrary.setInfo (id, ni);
                        // the two key detectors disagree: keep both until you pick one (right-click > Key)
                        const auto a = MusicalKey::parse (info.keyAlt), o = MusicalKey::parse (ni.key);
                        if (a.isValid() && o.isValid() && (a.pitch != o.pitch || a.minor != o.minor) && ni.key == info.key)
                            KeyOpinions::set (id, ni.key, info.keyAlt);
                        else if (ni.key == info.key)
                            KeyOpinions::clear (id);
                    }
                }
                self.browser.forgetPreview (t.file.getFullPathName());
            });
        }
        juce::MessageManager::callAsync ([safe, done, failed]
        {
            if (safe == nullptr) return;
            safe->importing = false;
            safe->browser.refreshSources();
            safe->flash ("Analysed " + juce::String (done) + (done == 1 ? " track" : " tracks") + (failed > 0 ? ", " + juce::String (failed) + " couldn't be read" : juce::String()));
        });
    });
}

void MainComponent::writeTags (const std::vector<TrackInfo>& tracks)
{
    if (importing.load()) { flash ("Another job is running - try again when it's done"); return; }
    // the library's info (with your edits) for each track
    std::vector<std::pair<juce::File, TagWriter::Fields>> jobs;
    for (auto& t : tracks)
    {
        auto info = t;
        if (const auto id = libraryIdFor (t, false); id != 0)
            if (auto lt = centrifugeLibrary.get (id)) info = lt->info;
        TagWriter::Fields f { info.title, info.artist, info.album, info.genre, info.label, info.comment, MusicalKey::display (info.key) };
        jobs.push_back ({ t.file, f });
    }
    importing = true;
    importProgress = 0.0f;
    {
        const juce::ScopedLock sl (importNameLock);
        importName = "Writing tags";
    }
    juce::Component::SafePointer<MainComponent> safe (this);
    importPool.addJob ([this, safe, jobs]
    {
        int done = 0;
        juce::StringArray errors;
        for (size_t i = 0; i < jobs.size() && ! stopImport.load(); ++i)
        {
            importProgress = (float) i / (float) jobs.size();
            juce::String err;
            if (TagWriter::write (jobs[i].first, jobs[i].second, err)) ++done; else errors.add (err);
        }
        juce::MessageManager::callAsync ([safe, done, errors]
        {
            if (safe == nullptr) return;
            safe->importing = false;
            safe->flash ("Wrote tags to " + juce::String (done) + (done == 1 ? " file" : " files")
                         + (errors.isEmpty() ? juce::String() : " - " + juce::String (errors.size()) + " not written: " + errors[0]));
        });
    });
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
        if (playedSeconds[(size_t) d] < settings.historySeconds) continue;
        playCounted[(size_t) d] = true;
        auto t = engine.deck (d).getLoadedTrack();
        if (t == nullptr) continue;
        auto info = t->info;
        if (info.durationSec <= 0.0) info.durationSec = st.lengthSec.load();
        if (const auto id = libraryIdFor (info, true); id != 0)
        {
            centrifugeLibrary.addPlay (id);
            UserLists::get().notePlayed (id);   // PLAY HISTORY (and the set list when one is running)
            if (recorder.isRecording())         // a track marker where it started playing (for the .cue / split files)
                recorder.addMarker (recorder.elapsedSeconds() - playedSeconds[(size_t) d], info);
            browser.refreshSources();
        }
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

double MainComponent::loudnessOf (const TrackAudio& a)
{
    // Gated loudness: mean power of the 400 ms blocks that aren't near-silent (roughly like LUFS)
    const int64_t block = juce::jmax<int64_t> (1, (int64_t) (a.sampleRate * 0.4));
    double sum = 0.0;
    int count = 0;
    for (int64_t s = 0; s + block <= a.numFrames; s += block)
    {
        double ms = 0.0;
        for (int64_t i = s; i < s + block; i += 4)
            ms += (double) a.left[(size_t) i] * a.left[(size_t) i] + (double) a.right[(size_t) i] * a.right[(size_t) i];
        ms /= (double) (block / 4) * 2.0;
        if (ms > 1.0e-5) { sum += ms; ++count; }   // above about -50 dBFS
    }
    return count > 0 ? 10.0 * std::log10 (sum / count) : -100.0;
}

void MainComponent::applyAutoGain (int deck, LoadedTrack::Ptr t)
{
    auto& dk = engine.deck (deck);
    if (! settings.autoGain || t == nullptr || t->audio == nullptr) { dk.post (Deck::Cmd::gainSet, 1.0); return; }
    const double mine = loudnessOf (*t->audio);
    loudness[t->serial] = mine;
    // match the master deck (the other deck, if it's the one playing)
    const int m = engine.getMasterDeck();
    const int other = 1 - deck;
    const int ref = (m == other) ? other : (engine.deck (other).state().playing.load() ? other : -1);
    auto refTrack = ref >= 0 ? engine.deck (ref).getLoadedTrack() : nullptr;
    if (refTrack == nullptr || refTrack->audio == nullptr || mine < -90.0) { dk.post (Deck::Cmd::gainSet, 1.0); return; }
    double theirs;
    if (auto it = loudness.find (refTrack->serial); it != loudness.end()) theirs = it->second;
    else theirs = loudness[refTrack->serial] = loudnessOf (*refTrack->audio);
    const double theirGainDb = 20.0 * std::log10 (juce::jmax (0.001f, engine.deck (ref).state().gain.load()));
    const double db = juce::jlimit (-12.0, 12.0, theirs + theirGainDb - mine);
    dk.post (Deck::Cmd::gainSet, std::pow (10.0, db / 20.0));
    flash ("Auto gain deck " + juce::String (deck + 1) + ": " + (db >= 0 ? "+" : "") + juce::String (db, 1) + " dB");
}

void MainComponent::askText (const juce::String& title, const juce::String& current, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", current);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        if (r == 1 && done) done (w->getTextEditorContents ("name").trim());
    }), true);
}

void MainComponent::treeAction (const juce::String& node, const juce::String& action, const std::vector<TrackInfo>& tracks)
{
    auto& ul = UserLists::get();
    const juce::int64 uid = node.startsWith ("u") ? node.substring (1).getLargeIntValue() : 0;
    auto refresh = [this] { browser.refreshSources(); };
    if (action.startsWith ("new."))
    {
        const int kind = action == "new.folder" ? UserLists::folder : action == "new.dynamic" ? UserLists::dynamic : UserLists::playlist;
        askText (kind == UserLists::folder ? "New folder" : kind == UserLists::dynamic ? "New dynamic playlist" : "New playlist", {},
                 [this, uid, kind, refresh] (juce::String name)
        {
            if (name.isEmpty()) return;
            const auto id = UserLists::get().add (uid, name, kind);
            refresh();
            if (kind == UserLists::dynamic) treeAction ("u" + juce::String (id), "rules", {});
        });
    }
    else if (action == "rename")
    {
        if (const auto* l = ul.find (uid))
            askText ("Rename", l->name, [uid, refresh] (juce::String name) { if (name.isNotEmpty()) { UserLists::get().rename (uid, name); refresh(); } });
    }
    else if (action == "rules")
    {
        if (const auto* l = ul.find (uid))
            juce::CallOutBox::launchAsynchronously (std::make_unique<RulesEditor> (l->rules, [this, uid] (const UserLists::Rules& r)
                                                    {
                                                        UserLists::get().setRules (uid, r);
                                                        browser.refreshSources();
                                                    }),
                                                    browser.getScreenBounds().withSizeKeepingCentre (10, 10), nullptr);
    }
    else if (action == "duplicate") { ul.duplicate (uid); refresh(); }
    else if (action == "delete")
    {
        const bool hist = node.startsWith ("hs.") || node.startsWith ("hl.");
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Delete").withMessage (hist ? "Delete this history list?" : "Delete this playlist / folder (the tracks stay in the library)?")
                                        .withButton ("Delete").withButton ("Cancel"),
                                      [node, uid, hist, refresh] (int r)
        {
            if (r != 1) return;
            if (hist) UserLists::get().deleteHistory (node.startsWith ("hl."), node.fromFirstOccurrenceOf (".", false, false).getIntValue());
            else UserLists::get().remove (uid);
            refresh();
        });
    }
    else if (action == "clear")
    {
        for (auto id : std::vector<juce::int64> (ul.toPlay())) ul.removeFromToPlay (id);
        refresh();
    }
    else if (action == "toplaylist" || action == "add.dragged")
    {
        std::vector<juce::int64> ids;
        for (auto& t : tracks) if (const auto id = libraryIdFor (t, true); id != 0) ids.push_back (id);
        if (action == "toplaylist")
        {
            const auto id = ul.add (0, browser.getCurrentListName().upToFirstOccurrenceOf ("  (", false, false), UserLists::playlist);
            ul.addTracks (id, ids);
        }
        else if (node == "toplay") { for (auto id : ids) if (std::find (ul.toPlay().begin(), ul.toPlay().end(), id) == ul.toPlay().end()) ul.toggleToPlay (id); }
        else ul.addTracks (uid, ids);
        refresh();
    }
    else if (action == "analyse") trackAction (tracks, "analyse");
    else if (action == "stems") trackAction (tracks, "stems.separate");
    else if (action == "export") openExport (true);
}

void MainComponent::startRecording (bool)
{
    if (recorder.isRecording() || recNaming) return;
    MasterRecorder::Options o;
    o.folder = settings.recordingsFolder();
    o.format = settings.recordFormat;
    o.source = settings.recordSource;
    o.lameExe = juce::File (settings.lamePath);
    o.cueFile = settings.recordCue;
    o.split = settings.recordSplit;
    if (o.source >= 0)
        if (auto err = engine.enableInputs(); err.isNotEmpty()) { flash ("Can't record from the input: " + err); return; }
    if (auto err = recorder.start (o, engine.getSampleRate(), settings.recordArmed); err.isNotEmpty()) { flash (err); return; }
    engine.setRecorder (&recorder, o.source);
    recText = {};
    flash (settings.recordArmed ? "Recording armed - it starts with the first sound" : "Recording");
}

void MainComponent::stopRecording()
{
    if (! recorder.isRecording() || recNaming) return;
    recNaming = true;
    engine.setRecorder (nullptr, -1);
    const auto def = recorder.defaultName();
    // The name is asked for after it stops (the file is closed first, so nothing is lost if the box is left open)
    auto* w = new juce::AlertWindow ("Recording stopped", "Name the recording", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", def);
    w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Keep the default name", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->addButton ("DISCARD...", 2);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, def] (int r)
    {
        if (r == 2)
        {
            // throw it away - after asking
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Discard the recording?")
                                            .withMessage ("The recording will be deleted. This can't be undone.")
                                            .withButton ("DISCARD").withButton ("Keep it"),
                                          [this, def] (int k)
            {
                if (k == 1) { recorder.discard(); flash ("Recording discarded"); }
                else flash ("Saved " + recorder.stop (def));
                recNaming = false;
                recText = {};
            });
            return;
        }
        const auto name = r == 1 ? w->getTextEditorContents ("name").trim() : def;
        const auto made = recorder.stop (name.isNotEmpty() ? name : def);
        recNaming = false;
        recText = {};
        flash ("Saved " + made);
    }), true);
    recText = {};
}

void MainComponent::zoomWaves (double steps)
{
    // each step of the knob: 10 % closer (clockwise) or further out
    if (steps == 0.0) return;
    waves.setVisibleSeconds (waves.getVisibleSeconds() * std::pow (1.1, -steps));
    settings.visibleSeconds = waves.getVisibleSeconds();   // (saved with the other settings on the way out)
    xzView.getWaves().setVisibleSeconds (settings.visibleSeconds);
}

void MainComponent::toggleSetList()
{
    auto& ul = UserLists::get();
    if (ul.setListRunning())
    {
        ul.stopSetList();
        browser.setSetListRunning (false);
        if (recorder.isRecording() && recordWithSetList) stopRecording();
        flash ("Set list stopped - it's in Play history > Set list history");
        browser.refreshSources();
        return;
    }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Start a set list")
                                    .withMessage ("The tracks you play go into a new set list.\n\nRecord the audio too?")
                                    .withButton ("Set list + record").withButton ("Set list only").withButton ("Cancel"),
                                  [this] (int r) { if (r != 0) startSetList (r == 1); });
}

void MainComponent::startSetList (bool record)
{
    if (UserLists::get().setListRunning()) return;
    UserLists::get().startSetList();
    browser.setSetListRunning (true);
    browser.refreshSources();
    recordWithSetList = record && ! recorder.isRecording();
    if (record && ! recorder.isRecording()) startRecording (false);
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    // Handy keyboard shortcuts (the XDJ-XZ remains the main controller).
    if (k == juce::KeyPress::F1Key) { showPage (0); return true; }
    if (k == juce::KeyPress::F2Key) { showPage (1); return true; }
    if (k == juce::KeyPress::F3Key) { showPage (2); return true; }
    if (k == juce::KeyPress::F4Key) { showPage (3); return true; }
    if (k == juce::KeyPress::F5Key) { showPage (4); return true; }
    if (k == juce::KeyPress ('f', juce::ModifierKeys::commandModifier, 0) || k == juce::KeyPress ('F', juce::ModifierKeys::commandModifier, 0))
    {
        if (page != 0) showPage (0);
        if (! settings.showBrowser) { settings.showBrowser = true; resized(); settings.save(); }
        browser.focusSearch();
        return true;
    }
    return false;
}

} // namespace orbit
