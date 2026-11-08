#pragma once
#include <set>
#include <map>
#include "WaveformRender.h"
#include "ScrollingWaveforms.h"
#include "ColourPicker.h"
#include "../App/TrackEditor.h"

namespace orbit
{

/** PREP: one track on the preview deck, with everything for preparing it - a big zoomable waveform, the beat grid
    editor, hot cues (and hot loops), locators (and loop / active / cycle loops), skips, beat jumps - and every
    locator, loop locator and skip in one list in track order. Edits go to the library versions lit on R / C. */
class PrepView : public juce::Component,
                 public juce::DragAndDropTarget,
                 private juce::Timer
{
public:
    static constexpr int deckIndex = AudioEngine::prepDeck;

    PrepView (AudioEngine& engine, TrackEditor& editor);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::MouseCursor getMouseCursor() override;
    void mouseMove (const juce::MouseEvent& e) override { const bool in = listArea.contains (e.getPosition()) || listEdge.contains (e.getPosition()); if (in || hoverList) { hoverList = in; repaint (listArea.getUnion (listEdge)); } }
    void mouseExit (const juce::MouseEvent&) override { if (hoverList) { hoverList = false; repaint (listArea); } }

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description == juce::var ("centrifuge-tracks"); }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override { dropHover = false; repaint(); if (onTrackDropped) onTrackDropped(); }

    std::function<void()> onTrackDropped;
    std::function<void (int deck, bool hotCue, int index, juce::String currentName)> onRenameCue;
    std::function<void (int deck)> onSetBpm;
    std::function<void (int width)> onListWidthChanged;   // after the list's edge is dragged
    void setEditView (bool on) { if (on != editView) { editView = on; overviewWidth = 0; repaint(); } }
    std::function<void()> onEditViewToggled;
    void showListTab (int t) { listTab = juce::jlimit (0, 2, t); listScroll = 0; repaint(); }
    void setListWidth (int w) { listWidth = juce::jlimit (200, 900, w); resized(); repaint(); }

    /** The zoomed waveform: lanes (the second can be off), split, colours and zoom - like the play decks. */
    ScrollingWaveforms& getWaves() { return waves; }
    void setOverviewStyle (int style, bool half, juce::Colour single) { overviewStyle = style; overviewHalf = half; singleColour = single; overviewWidth = 0; repaint(); }
    void setOverviewGrid (bool on) { if (on != overviewGrid) { overviewGrid = on; repaint(); } }
    std::function<void()> onOverviewGridToggled;
    std::function<void (juce::Colour)> onDeckColour;
    std::function<void (int stem, int mode)> onStemModeChanged;

private:
    struct Region { juce::Rectangle<float> r; juce::String action; int kind; };   // kind: 0 button, 1 overview, 2 zoom wave, 3 pad, 4 list row, 5 bpm,
                                                                                    //       6 list colour icon, 7 list delete (x), 8 list loop mode
    struct ListEntry { double time; int kind; int index; };                        // kind 0 locator, 1 skip, 2 hot cue, 3 phrase, 5 stem locator, 6 note

    void timerCallback() override;
    void action (const juce::String& id, bool down);
    void post (Deck::Cmd cmd, double value = 0.0, int index = 0, bool down = true);
    Deck& deck() { return engine.deck (deckIndex); }
    const Region* regionAt (juce::Point<float> p) const;
    void paintStems (juce::Graphics&, juce::Rectangle<float> area);
    void showStemModeMenu (int stem, juce::Rectangle<float> area);
    void showDeckColourMenu (juce::Rectangle<float> area);
    void addNoteAtPlayhead();
    void addStemLocatorAtPlayhead (juce::Rectangle<float> area);
    void addLoopLocatorAtPlayhead();
    void paintList (juce::Graphics&, juce::Rectangle<float> area);
    std::vector<ListEntry> listEntries() const;
    void showEntryMenu (const ListEntry& e, juce::Rectangle<float> area);
    void showPadMenu (int pad, juce::Rectangle<float> area);
    void showPhraseMenu (int index, juce::Rectangle<float> area);
    void showPhraseAddMenu();
    bool keyPressed (const juce::KeyPress&) override;

    // List selection (like a file browser: click, ctrl + click to add / remove one, shift + click for a range)
    using EntryKey = std::pair<int, int>;                   // kind, index
    std::set<EntryKey> selection;
    EntryKey anchor { -1, -1 };
    ListEntry entryFor (int kind, int index) const;
    void clickEntry (const ListEntry& e, const juce::ModifierKeys& mods);
    std::vector<ListEntry> targetsFor (const ListEntry& e) const;    // the selection if e is in it, else just e
    void showColourMenu (const ListEntry& e, juce::Rectangle<float> area);
    void applyColour (const std::vector<ListEntry>& targets, juce::Colour c);
    void deleteEntries (std::vector<ListEntry> targets);
    juce::Colour colourOf (const ListEntry& e) const;

    // Rename in place: click an entry's name and type
    std::map<EntryKey, juce::Rectangle<float>> nameRects;   // where each visible name was drawn
    std::unique_ptr<juce::TextEditor> nameEditor;
    EntryKey renaming { -1, -1 };
    void startRename (const ListEntry& e);
    void finishRename (bool apply);
    juce::String nameOf (const ListEntry& e) const;
    void askText (const juce::String& title, const juce::String& current, std::function<void (juce::String)> done);
    void chip (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool lit, juce::Colour, const juce::String& action);

    AudioEngine& engine;
    TrackEditor& editor;
    LoadedTrack::Ptr track;
    int serial = -1, dataVersion = -1;
    juce::uint64 lastHash = 0;

    juce::Rectangle<int> rowHeader, rowOverview, rowWave, rowTransport, rowPads, rowGrid, listArea;
    std::vector<Region> regions;
    juce::Image overview;
    int overviewWidth = 0;
    int overviewStyle = 1;
    bool overviewHalf = false, overviewGrid = false;
    juce::Colour singleColour { 0xff3d9bff };
    ScrollingWaveforms waves { engine, { AudioEngine::prepDeck } };
    juce::Rectangle<int> rowStems;
    int dragStem = -1;
    double loopBeats = 4.0;
    int listScroll = 0;
    bool editView = true;
    TimeFold fold;
    std::vector<std::pair<double, double>> foldMapKey, foldSkipKey;
    bool foldOnKey = false;
    int renderedFold = -1;
    void updateFold();
    double displayNow() const;              // the playhead on the display timeline (edited view)
    void showEditVersionMenu (juce::Rectangle<float> area);
    void showAddMenu (juce::Rectangle<float> area);
    void showStemEditor (int index, juce::Rectangle<float> area);
    void showNoteMenu (int index, juce::Rectangle<float> area);
    void showNoteEditor (int index, juce::Rectangle<float> area);
    static juce::String stemLocatorSummary (const StemLocator& l);
    std::vector<int> phrasePlayOrder() const;            // phrase indexes in play order (a duplicated section appears again)
    int phraseIndexOf (int kind, int index) const;      // the phrase of a list entry (kind 3 phrase, kind 4 a copy)
    void setPlayOrder (const std::vector<int>& order);
    std::vector<ListEntry> shownEntries;
    float listRowsTop = 0.0f, listRowH = 24.0f;
    int dragPhrase = -1, dragTarget = -1;
    int listWidth = 300;
    bool resizingList = false;
    int resizeStartX = 0, resizeStartWidth = 0;
    juce::Rectangle<int> listEdge;   // drag to make the list wider / narrower
    int listTab = 0;          // 0 locators / loops / skips, 1 hot cues, 2 phrases
    int phraseSelected = -1;
    juce::String heldAction;
    bool dropHover = false, hoverList = false;
};

} // namespace orbit
