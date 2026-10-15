#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

namespace orbit
{

/** rekordbox's hot cue / memory cue colour palette (as shown in rekordbox's cue colour picker). */
struct NamedColour { const char* name; juce::uint32 argb; };
inline const std::array<NamedColour, 16>& rekordboxCueColours()
{
    static const std::array<NamedColour, 16> c { {
        { "Pink",         0xffde44cf }, { "Magenta",   0xffb432ff }, { "Violet",     0xffaa72ff }, { "Purple",   0xff6473ff },
        { "Blue",         0xff305aff }, { "Sky blue",  0xff50b4ff }, { "Cyan",       0xff00e0ff }, { "Aqua",     0xff1fa392 },
        { "Teal",         0xff10b176 }, { "Green",     0xff28e214 }, { "Lime",       0xffa5e116 }, { "Olive",    0xffb4be04 },
        { "Yellow",       0xffc3af04 }, { "Orange",    0xffe0641b }, { "Red",        0xffe62828 }, { "Rose",     0xffff127b },
    } };
    return c;
}

/** Adds the rekordbox colours to a menu (ids baseId .. baseId+15) and a "Custom colour..." item (baseId+16). */
inline void addCueColourItems (juce::PopupMenu& m, int baseId, juce::Colour current)
{
    const auto& cols = rekordboxCueColours();
    for (int i = 0; i < (int) cols.size(); ++i)
    {
        juce::PopupMenu::Item item (cols[(size_t) i].name);
        item.itemID = baseId + i;
        item.colour = juce::Colour (cols[(size_t) i].argb);
        item.isTicked = current.getARGB() == cols[(size_t) i].argb;
        m.addItem (item);
    }
    m.addSeparator();
    m.addItem (baseId + 16, "Custom colour...");
}

/** A colour wheel / RGB picker in a call-out bubble. onChange is called live while the colour is edited. */
class ColourPickerBubble : public juce::Component, private juce::ChangeListener
{
public:
    ColourPickerBubble (juce::Colour initial, std::function<void (juce::Colour)> changed)
        : onChange (std::move (changed))
    {
        selector.setCurrentColour (initial, juce::dontSendNotification);
        selector.addChangeListener (this);
        selector.setColour (juce::ColourSelector::backgroundColourId, juce::Colour (0xff1c1e22));
        selector.setColour (juce::ColourSelector::labelTextColourId, juce::Colour (0xffe8e9eb));
        addAndMakeVisible (selector);
        setSize (300, 320);
    }

    void resized() override { selector.setBounds (getLocalBounds()); }

    static void show (juce::Rectangle<int> screenArea, juce::Colour initial, std::function<void (juce::Colour)> changed)
    {
        auto bubble = std::make_unique<ColourPickerBubble> (initial, std::move (changed));
        juce::CallOutBox::launchAsynchronously (std::move (bubble), screenArea, nullptr);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (onChange) onChange (selector.getCurrentColour().withAlpha (1.0f));
    }

    juce::ColourSelector selector { juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                                    | juce::ColourSelector::showColourspace | juce::ColourSelector::editableColour };
    std::function<void (juce::Colour)> onChange;
};

} // namespace orbit
