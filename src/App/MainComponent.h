#pragma once
#include "AppSettings.h"
#include "TrackLoader.h"
#include "../Control/MidiManager.h"
#include "../Library/LibraryManager.h"
#include "../UI/Theme.h"
#include "../UI/DeckPanel.h"
#include "../UI/ScrollingWaveforms.h"
#include "../UI/XzScreenView.h"
#include "../Control/XzScreenLink.h"
#include "../UI/BrowserView.h"
#include "../UI/ButtonStyles.h"
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
    static double loudnessOf (const TrackAudio& a);
    void applyAutoGain (int deck, LoadedTrack::Ptr t);
    std::map<int, double> loudness;   // by track serial (dB)
    juce::Rectangle<int> masterArea;
    juce::Slider masterVolume;
    float shownPeak = 0.0f;

    // Used by the command-line demo / preview mode
    AudioEngine& getEngine() { return engine; }
    BrowserView& getBrowser() { return browser; }
    TrackLoader& getLoader() { return loader; }
    TrackEditor& getEditor() { return editor; }
    LibraryManager& getLibrary() { return library; }
    ActionRouter& getRouter() { return router; }
    MidiManager& getMidi() { return midi; }
    void showPage (int page);
    void showAbout() { about.show(); }
    ScrollingWaveforms& getWaves() { return waves; }
    PrepView& getPrep() { return prepView; }
    juce::String getWindowState() const { return settings.windowState; }
    void setWindowState (const juce::String& s) { settings.windowState = s; settings.save(); }
    /** Demo / tests: change settings and apply them. */
    void demoSettings (const std::function<void (AppSettings&)>& fn) { fn (settings); applySettings(); resized(); repaint(); }
    DeckPanel& getDeckPanel (int d) { return d == 0 ? deck1 : deck2; }
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
    void loadToDeck (int deck, const TrackInfo& info, bool fromPc, bool confirmed);
    std::unique_ptr<juce::AlertWindow> loadConfirm;
    void showImportMenu (juce::Component& anchor);
    void showExportMenu (juce::Component& anchor);
    void runImport (std::shared_ptr<const LibrarySource> source, std::vector<int> onlyTracks);
  public:
    /** Adds audio files / folders that aren't in rekordbox (analysed by Centrifuge). */
    void addMusic (const juce::Array<juce::File>& items, int subfolders = -1);   // -1 = ask when a folder has subfolders
  private:
    void chooseMusic (bool folders);
    void askRename (int deck, bool hotCue, int index, const juce::String& current);
    void askBpm (int deck);
    void flash (const juce::String& text);
    void loadDragged (int deck);
    void startPreview (const TrackInfo& info, bool fromPc, double fraction);
    void trackAction (const std::vector<TrackInfo>& tracks, const juce::String& action);
    void countPlays();
    void clearTracks (const std::vector<TrackInfo>& tracks, const juce::String& action);
    void analyseTracks (const std::vector<TrackInfo>& tracks, bool replace);
    void writeTags (const std::vector<TrackInfo>& tracks);
    juce::int64 libraryIdFor (const TrackInfo& t, bool addIfMissing);
    juce::Rectangle<int> phaseArea;
    PhaseMeter phaseMeter { engine };
    PrepView prepView { engine, editor };
    StyledTextButton tabPrep { "PREP", "tab.prep" };
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
    MasterRecorder recorder;   // (before the engine, which feeds it)
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
    // Laptop mode: the decks drawn for the XDJ-XZ's screen and sent to it over the network cable
    XzScreenLink xzLink;
    struct FnTimer : juce::Timer { std::function<void()> f; void timerCallback() override { if (f) f(); } } xzTimer;
    BrowserView browser { library, stems };
    XzScreenView xzView { engine, router, browser };
public:
    juce::Image renderXzScreen() { return xzView.render(); }
private:   // (after the browser: it shows it)
    MappingView mapping;
    StemsView stemsView;
    SettingsView settingsView;
    SettingsView stemSettingsView;   // the STEMS settings, on the Stems page

    StyledTextButton tabPlay { "PLAY", "tab.play" }, tabMapping { "MAPPING", "tab.mapping" }, tabStems { "STEMS", "tab.stems" },
                     tabSettings { "SETTINGS", "tab.settings" };
    StyledTextButton statusFold { ">", "statusfold" };   // folds the status boxes away (the phrase meter gets their room)
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
    void withUserLists (LibrarySource& src);
    void treeAction (const juce::String& node, const juce::String& action, const std::vector<TrackInfo>& tracks);
    void askText (const juce::String& title, const juce::String& current, std::function<void (juce::String)> done);
    void toggleSetList();
    bool recordWithSetList = false;
    void startRecording (bool askSource);
    void stopRecording();
    StyledTextButton recButton { "REC", "rec" };
    juce::String recText;
    bool recNaming = false;
    int dragging = 0, dragStartValue = 0;
    bool browserExpanded = false;
    std::array<int, AudioEngine::totalDecks> stemPreviewSerial {};   // the track whose stems preview was saved
    juce::ThreadPool stemPreviewPool { 1 };
    std::unique_ptr<CentrifugeSplash> splash;
    juce::String midiStatus, audioStatus, stemStatus;
    bool xdjConnected = false;
    double cpu = 0.0;
};

} // namespace orbit
