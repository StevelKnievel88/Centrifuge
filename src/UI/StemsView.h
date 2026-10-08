#pragma once
#include "../Stems/StemManager.h"
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
               std::function<juce::String()> currentListName);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    StemManager& stems;
    std::function<std::vector<TrackInfo>()> getList, getSelected;
    std::function<juce::String()> getListName;

    juce::TextButton analyseList { "PRE-ANALYSE CURRENT LIST" }, analyseSelected { "PRE-ANALYSE SELECTED" }, stop { "STOP" };
    juce::TextButton openCache { "OPEN STEM FOLDER" };
    StemManager::Status shown;
    juce::String listName;
};

} // namespace orbit
