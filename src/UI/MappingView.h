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
    void advanceGuided (bool skip);
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
                     skipButton { "SKIP" }, stopButton { "STOP" }, invertButton { "INVERT" };
    juce::ComboBox modeBox;
    juce::Slider sensitivity;
    juce::Label sensLabel, banner, status;

    std::vector<juce::String> guidedSteps;
    int guidedIndex = -1;
};

} // namespace orbit
