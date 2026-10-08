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

namespace orbit
{

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // Used by the command-line demo / preview mode
    AudioEngine& getEngine() { return engine; }
    BrowserView& getBrowser() { return browser; }
    TrackLoader& getLoader() { return loader; }
    LibraryManager& getLibrary() { return library; }
    ActionRouter& getRouter() { return router; }
    void showPage (int page);

private:
    void timerCallback() override;
    void applySettings();
    void handleAppAction (const juce::String& id, int deck, const ActionValue& v);
    void loadToDeck (int deck, const TrackInfo& info, bool fromPc);

    struct SettingsHolder { AppSettings s; SettingsHolder() { s.load(); } } settingsHolder;
    AppSettings& settings = settingsHolder.s;

    OrbitLookAndFeel lookAndFeel;
    AudioEngine engine;
    ActionRouter router { engine };
    MidiManager midi { router };
    AudioFileLoader fileLoader;
    StemManager stems { fileLoader };
    TrackLoader loader { engine, fileLoader, stems };
    LibraryManager library;

    DeckPanel deck1 { 0, engine, router }, deck2 { 1, engine, router };
    ScrollingWaveforms waves { engine };
    BrowserView browser { library, stems };
    MappingView mapping;
    StemsView stemsView;
    SettingsView settingsView;

    juce::TextButton tabPlay { "PLAY" }, tabMapping { "MAPPING" }, tabStems { "STEMS" }, tabSettings { "SETTINGS" };
    int page = 0;

    juce::Rectangle<int> topBar;
    juce::String midiStatus, audioStatus, stemStatus;
    bool xdjConnected = false;
    double cpu = 0.0;
};

} // namespace orbit
