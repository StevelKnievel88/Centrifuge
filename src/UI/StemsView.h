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
               std::function<std::array<int, 4>()> getMatrix,
               std::function<void (std::array<int, 4>)> setMatrix);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    StemManager& stems;
    std::function<std::vector<TrackInfo>()> getList, getSelected;
    std::function<juce::String()> getListName;

    juce::TextButton analyseList { "PRE-ANALYSE CURRENT LIST" }, analyseSelected { "PRE-ANALYSE SELECTED" }, stop { "STOP" };
    juce::TextButton openCache { "OPEN STEM FOLDER" };
    StemManager::Status shown;
    juce::String listName;
    std::function<std::array<int, 4>()> getMatrix;
    std::function<void (std::array<int, 4>)> setMatrix;
    juce::Rectangle<int> matrixArea;
    CentrifugeRotor rotor;
    juce::Rectangle<int> cell (int control, int stem) const;
};

} // namespace orbit
