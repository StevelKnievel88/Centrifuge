#pragma once
#include "../Stems/StemManager.h"
#include "CentrifugeRotor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace orbit
{

/** Batch pre-analysis of stems and status of the separation engine. */
class StemsView : public juce::Component, private juce::Timer
{
public:
    StemsView (StemManager& stems,
               std::function<std::vector<TrackInfo>()> currentList,
               std::function<std::vector<TrackInfo>()> selectedTracks,
               std::function<juce::String()> currentListName,
               std::function<std::array<int, 8>()> getMatrix,
               std::function<void (std::array<int, 8>)> setMatrix);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    /** The stem settings (quality, models, GPU...) shown under the controls. */
    /** Each deck's VOLUME / FILTER switch for the knobs mapped to 'Stem control X (volume / filter)'. */
    std::function<bool (int deck)> isFilterMode;
    std::function<void (int deck)> toggleFilterMode;
    void setSettingsPanel (juce::Component* c) { settingsPanel = c; if (c != nullptr) addAndMakeVisible (c); resized(); }

private:
    void timerCallback() override;

    StemManager& stems;
    std::function<std::vector<TrackInfo>()> getList, getSelected;
    std::function<juce::String()> getListName;

    juce::TextButton analyseList { "PRE-ANALYSE CURRENT LIST" }, analyseSelected { "PRE-ANALYSE SELECTED" }, stop { "STOP" };
    juce::TextButton openCache { "OPEN STEM FOLDER" };
    StemManager::Status shown;
    juce::String listName;
    std::function<std::array<int, 8>()> getMatrix;
    std::function<void (std::array<int, 8>)> setMatrix;
    juce::Rectangle<int> matrixArea;
    juce::Rectangle<int> modeChip (int deck) const;
    juce::Component* settingsPanel = nullptr;
    CentrifugeRotor rotor;
    juce::Rectangle<int> cell (int control, int stem) const;
};

} // namespace orbit
