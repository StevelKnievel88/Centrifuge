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

/** Loop locator modes: ids baseId (normal), baseId+1 (active), baseId+10+n (cycle n times: 2, 4, 8, 16, 32). */
inline void addLoopModeItems (juce::PopupMenu& m, int baseId, int mode, int cycles)
{
    m.addSectionHeader ("Loop");
    m.addItem (baseId, "Ordinary loop (called up with the locator)", true, mode == 0);
    m.addItem (baseId + 1, "Active loop (engages when playback runs into it)", true, mode == 1);
    juce::PopupMenu cyc;
    for (int n : { 2, 4, 8, 16, 32 })
        cyc.addItem (baseId + 10 + n, juce::String (n) + "x", true, mode == 2 && cycles == n);
    m.addSubMenu (mode == 2 ? "Cycle loop (" + juce::String (cycles) + "x - plays that many times, then carries on)"
                            : juce::String ("Cycle loop (plays a set number of times, then carries on)"), cyc, true, nullptr, mode == 2);
}

/** Result of addLoopModeItems: {mode, cycles}, or {-1, 0} if the id isn't one of its items. */
inline std::pair<int, int> loopModeFromMenu (int result, int baseId, int currentCycles)
{
    if (result == baseId) return { 0, currentCycles };
    if (result == baseId + 1) return { 1, currentCycles };
    if (result > baseId + 10 && result <= baseId + 10 + 32) return { 2, result - baseId - 10 };
    return { -1, 0 };
}

/** Loop symbol (a circular arrow) for loop locators and loop buttons. */
inline void drawLoopIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float thickness = 1.6f)
{
    const auto b = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())).reduced (thickness);
    juce::Path arc;
    arc.addCentredArc (b.getCentreX(), b.getCentreY(), b.getWidth() * 0.5f, b.getHeight() * 0.5f, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.2f, true);
    g.setColour (c);
    g.strokePath (arc, juce::PathStrokeType (thickness));
    const auto tip = b.getCentre().getPointOnCircumference (b.getWidth() * 0.5f, juce::MathConstants<float>::twoPi - 0.2f);
    juce::Path head;
    const float a = b.getWidth() * 0.32f;
    head.addTriangle (tip.x - a, tip.y - a * 0.2f, tip.x + a * 0.6f, tip.y - a * 0.9f, tip.x + a * 0.3f, tip.y + a * 0.7f);
    g.fillPath (head);
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
