#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace orbit
{

/** The grid editor's MORE menu: bar count changes from the playhead, and the grid following the kicks. */
inline void showGridMoreMenu (juce::Rectangle<int> screenArea, std::function<void (const juce::String& action)> act)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Beat grid - at the playhead");
    m.addItem (1, "A bar starts here (the count carries on from here)");
    m.addItem (2, "Add a beat here (the downbeat comes a beat later from here)");
    m.addItem (3, "Take a beat out here (the downbeat comes a beat earlier from here)");
    m.addSeparator();
    m.addSectionHeader ("Tempo changes in the music (speed-ups / slow-downs)");
    m.addItem (4, "Follow the kicks - whole track");
    m.addItem (5, "Follow the kicks - from here on");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea), [act] (int r)
    {
        const char* ids[] = { "", "barhere", "beat.add", "beat.remove", "follow", "follow.here" };
        if (r >= 1 && r <= 5 && act) act (ids[r]);
    });
}

} // namespace orbit
