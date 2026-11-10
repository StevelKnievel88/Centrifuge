#pragma once
#include "../Control/MidiManager.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

namespace orbit
{

/** Map every XDJ-XZ control to any function: MIDI learn, guided setup, live MIDI monitor. */
class MappingView : public juce::Component,
                    private juce::TableListBoxModel,
                    private juce::Timer
{
public:
    MappingView (MidiManager& midi, std::function<void()> saveMapping);
    ~MappingView() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    /** Leaving the page stops listening for a control (otherwise every press would keep being learned instead of
        working); guided setup picks up at the same step when you come back. */
    void visibilityChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Which controller drives each deck (the dropdowns at the top). */
    std::function<void (int deck, const juce::String& device)> onDeckController;
    void setDeckControllers (const juce::String& d1, const juce::String& d2);

private:
    // actions table
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override;

    // monitor list
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected);

    void timerCallback() override;
    void learnSelected (bool add = false);
    void startGuided();
    void runGuided (bool onlyUnmapped);
    void advanceGuided (bool skip);
    void listenForStep();
    void showLearnChoice (const std::vector<MidiManager::LearnChoice>&, int best);
    void showLedEditor();
    void refreshControllerBoxes();
    static juce::String stepHint (const juce::String& id);
    void updateDetail();
    void refreshFilter();
    void revealAction (const juce::String& id);
    const ActionDef* selectedAction() const;

    struct ListAdapter : juce::ListBoxModel
    {
        MappingView& v;
        explicit ListAdapter (MappingView& o) : v (o) {}
        int getNumRows() override { return (int) v.monitorRows.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool s) override { v.paintListBoxItem (row, g, w, h, s); }
    } listAdapter { *this };

    MidiManager& midi;
    std::function<void()> save;

    /** The list is a fold-out tree: Deck 1 > Transport > Play. */
    struct Row { int level = 0; juce::String key, title; const ActionDef* action = nullptr; };
    std::vector<Row> rows;
    std::set<juce::String> expanded;
    static juce::String topOf (const ActionDef& a) { return a.group.upToFirstOccurrenceOf (" - ", false, false); }
    static juce::String subOf (const ActionDef& a) { return a.group.contains (" - ") ? a.group : juce::String(); }
    juce::TableListBox table { "actions", this };
    juce::ListBox monitor { "monitor", &listAdapter };
    std::vector<MidiManager::MonitorEntry> monitorRows;
    juce::uint32 lastCounter = 0;

    juce::TextEditor filter;
    juce::TextButton learnButton { "LEARN" }, learnAddButton { "LEARN + ADD" }, clearButton { "CLEAR" }, guidedButton { "GUIDED SETUP" },
                     skipButton { "SKIP  (TAB)" }, stopButton { "STOP" }, invertButton { "INVERT" }, ledButton { "LED..." };
    std::array<juce::ComboBox, 2> controllerBox;
    std::array<juce::String, 2> deckController;
    juce::StringArray lastDevices;
    juce::ComboBox modeBox;
    juce::Slider sensitivity;
    juce::Label sensLabel, banner, status;

    std::vector<juce::String> guidedSteps;
    int guidedIndex = -1;
    int pausedGuided = -1;
    double countdownUntil = 0.0;       // guided setup: the next step starts listening at this time (0 = listening)
    std::vector<juce::String> guidedMapped, guidedSkipped;
};

} // namespace orbit
