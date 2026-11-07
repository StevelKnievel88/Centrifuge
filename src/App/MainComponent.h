#pragma once
#include "AppSettings.h"
#include "TrackLoader.h"
#include "../Control/MidiManager.h"
#include "../Library/LibraryManager.h"
#include "../UI/Theme.h"
#include "../UI/DeckPanel.h"
#include "../UI/ScrollingWaveforms.h"
#include "../UI/BrowserView.h"
#include "../UI/MappingView.h"
#include "../UI/StemsView.h"
#include "../UI/SettingsView.h"
#include "../UI/CentrifugeRotor.h"
#include "../UI/AboutView.h"
#include "../UI/PrepView.h"
#include "../UI/PhaseMeter.h"
#include "../Library/CueStore.h"
#include "../UI/LibraryDialogs.h"
#include "TrackEditor.h"
#include "../Library/FileImporter.h"

namespace orbit
{

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void skipSplash();
    bool keyPressed (const juce::KeyPress&) override;

    // Used by the command-line demo / preview mode
    AudioEngine& getEngine() { return engine; }
    BrowserView& getBrowser() { return browser; }
    TrackLoader& getLoader() { return loader; }
    TrackEditor& getEditor() { return editor; }
    LibraryManager& getLibrary() { return library; }
    ActionRouter& getRouter() { return router; }
    void showPage (int page);
    void showAbout() { about.show(); }
    ScrollingWaveforms& getWaves() { return waves; }
    void importForDemo (std::shared_ptr<const LibrarySource> s) { runImport (s, {}); }
    bool isImporting() const { return importing.load(); }
    std::shared_ptr<const LibrarySource> librarySource (int view) { return centrifugeSource (view); }
    void openExport (bool usb) { exportView.open (usb); }
    void closeExport() { exportView.setVisible (false); }

private:
    void timerCallback() override;
    void applySettings();
    void handleAppAction (const juce::String& id, int deck, const ActionValue& v);
    void loadToDeck (int deck, const TrackInfo& info, bool fromPc);
    void showImportMenu (juce::Component& anchor);
    void showExportMenu (juce::Component& anchor);
    void runImport (std::shared_ptr<const LibrarySource> source, std::vector<int> onlyTracks);
  public:
    /** Adds audio files / folders that aren't in rekordbox (analysed by Centrifuge). */
    void addMusic (const juce::Array<juce::File>& items);
  private:
    void chooseMusic (bool folders);
    void askRename (int deck, bool hotCue, int index, const juce::String& current);
    void askBpm (int deck);
    void flash (const juce::String& text);
    void loadDragged (int deck);
    void startPreview (const TrackInfo& info, bool fromPc, double fraction);
    void trackAction (const std::vector<TrackInfo>& tracks, const juce::String& action);
    void countPlays();
    juce::int64 libraryIdFor (const TrackInfo& t, bool addIfMissing);
    juce::Rectangle<int> phaseArea;
    PhaseMeter phaseMeter { engine };
    PrepView prepView { engine, editor };
    juce::TextButton tabPrep { "PREP" };
    juce::String pendingPreviewPath;
    double pendingPreviewFraction = -1.0;
    std::array<int, 2> playSerial { -1, -1 };
    std::array<double, 2> playedSeconds { 0.0, 0.0 };
    std::array<bool, 2> playCounted { false, false };
    juce::uint32 lastTick = 0;
    std::array<juce::int64, AudioEngine::totalDecks> marksSig {};
    std::shared_ptr<const LibrarySource> centrifugeSource (int view);

    struct SettingsHolder { AppSettings s; SettingsHolder() { s.load(); } } settingsHolder;
    AppSettings& settings = settingsHolder.s;

    OrbitLookAndFeel lookAndFeel;
    CueStore cueStore { AppSettings::folder().getChildFile ("cues.json") };
    AudioEngine engine;
    ActionRouter router { engine };
    MidiManager midi { router };
    AudioFileLoader fileLoader;
    StemManager stems { fileLoader };
    TrackLoader loader { engine, fileLoader, stems };
    LibraryManager library;
    CentrifugeLibrary centrifugeLibrary { AppSettings::folder().getChildFile ("library.db") };
    TrackEditor editor { engine, centrifugeLibrary };

    DeckPanel deck1 { 0, engine, router }, deck2 { 1, engine, router };
    ScrollingWaveforms waves { engine };
    BrowserView browser { library, stems };
    MappingView mapping;
    StemsView stemsView;
    SettingsView settingsView;

    juce::TextButton tabPlay { "PLAY" }, tabMapping { "MAPPING" }, tabStems { "STEMS" }, tabSettings { "SETTINGS" };
    int page = 0;

    juce::Rectangle<int> topBar, dividerDecks, dividerBrowser, logoArea;
    AboutView about;
    ExportView exportView;
    ConflictsView conflictsView { centrifugeLibrary };
    int appliedQuantize = -1;
    juce::ThreadPool importPool { 1 };
    std::atomic<bool> importing { false };
    std::atomic<float> importProgress { 0.0f };
    juce::String importName, flashText;
    juce::CriticalSection importNameLock;
    std::atomic<bool> stopImport { false };
    std::unique_ptr<juce::FileChooser> musicChooser;
    juce::uint32 flashUntil = 0;
    int seenLibraryChanges = -1;
    juce::uint32 lastLibraryRefresh = 0;
    std::array<std::shared_ptr<const LibrarySource>, 2> sourceCache;
    std::array<int, 2> sourceCacheVersion { -1, -1 };
    int dragging = 0, dragStartValue = 0;
    std::unique_ptr<CentrifugeSplash> splash;
    juce::String midiStatus, audioStatus, stemStatus;
    bool xdjConnected = false;
    double cpu = 0.0;
};

} // namespace orbit
