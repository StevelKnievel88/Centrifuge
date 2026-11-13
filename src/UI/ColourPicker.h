#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

namespace orbit
{

/** Loop locator lines: active loops get an orange line, cycle loops a red one. */
constexpr juce::uint32 activeLoopLine = 0xffff8c00, cycleLoopLine = 0xffff2d2d;


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

/** A library of colours you've saved from the custom colour picker (with your own names), kept between sessions.
    There are two: the cue palette (hot cues, markers, phrases, decks...) and the button palette (button colours). */
struct SavedColours
{
    struct Entry { juce::String name; juce::Colour colour; };

    SavedColours (const char* fileName, const char* title) : fileName (fileName), title (title) {}

    static SavedColours& cues()    { static SavedColours c ("saved-colours.json", "Cue palette"); return c; }
    static SavedColours& buttons() { static SavedColours c ("button-colours.json", "Button palette"); return c; }
    static SavedColours& tags()    { static SavedColours c ("tag-colours.json", "Tag palette"); return c; }
    static std::array<SavedColours*, 3> all() { return { &cues(), &buttons(), &tags() }; }

    juce::File file() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Centrifuge").getChildFile (fileName);
    }
    std::vector<Entry>& list()
    {
        if (! loaded)
        {
            loaded = true;
            const auto json = juce::JSON::parse (file());
            if (auto* a = json.getArray())
                for (auto& e : *a)
                    entries.push_back ({ e["name"].toString(), juce::Colour ((juce::uint32) (juce::int64) e["argb"]) });
        }
        return entries;
    }
    void save()
    {
        juce::Array<juce::var> a;
        for (auto& e : list())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", e.name);
            o->setProperty ("argb", (juce::int64) e.colour.getARGB());
            a.add (juce::var (o));
        }
        file().getParentDirectory().createDirectory();
        file().replaceWithText (juce::JSON::toString (juce::var (a)));
    }
    void add (const juce::String& name, juce::Colour c)
    {
        list().push_back ({ name.isNotEmpty() ? name : c.toDisplayString (false), c.withAlpha (1.0f) });
        save();
    }
    void remove (int index)
    {
        if (index < 0 || index >= (int) list().size()) return;
        list().erase (list().begin() + index);
        save();
    }
    static constexpr int maxShown = 40;

    const char* fileName;
    juce::String title;
private:
    std::vector<Entry> entries;
    bool loaded = false;
};

/** Your saved colours from a library (ids baseId+20 ..), "Custom colour..." (baseId+16) and a submenu to delete
    saved ones (baseId+60 ..). */
inline void addSavedColourItems (juce::PopupMenu& m, int baseId, juce::Colour current, SavedColours& lib)
{
    const auto& saved = lib.list();
    if (! saved.empty())
    {
        m.addSeparator();
        m.addSectionHeader (lib.title);
        for (int i = 0; i < juce::jmin ((int) saved.size(), SavedColours::maxShown); ++i)
        {
            juce::PopupMenu::Item item (saved[(size_t) i].name);
            item.itemID = baseId + 20 + i;
            item.colour = saved[(size_t) i].colour;
            item.isTicked = current.getARGB() == saved[(size_t) i].colour.getARGB();
            m.addItem (item);
        }
    }
    m.addSeparator();
    m.addItem (baseId + 16, "Custom colour...  (+ in the picker saves it to the " + lib.title.toLowerCase() + ")");
    if (! saved.empty())
    {
        juce::PopupMenu del;
        for (int i = 0; i < juce::jmin ((int) saved.size(), SavedColours::maxShown); ++i)
        {
            juce::PopupMenu::Item item ("Delete \"" + saved[(size_t) i].name + "\"");
            item.itemID = baseId + 60 + i;
            item.colour = saved[(size_t) i].colour;
            del.addItem (item);
        }
        m.addSubMenu ("Delete a colour from the " + lib.title.toLowerCase(), del);
        // copy a colour into another palette, so they can be used everywhere
        juce::PopupMenu copy;
        int li = 0;
        for (auto* other : SavedColours::all())
        {
            if (other != &lib)
            {
                juce::PopupMenu sub;
                for (int i = 0; i < juce::jmin ((int) saved.size(), SavedColours::maxShown); ++i)
                {
                    juce::PopupMenu::Item item (saved[(size_t) i].name);
                    item.itemID = baseId + 5000 + li * 100 + i;
                    item.colour = saved[(size_t) i].colour;
                    sub.addItem (item);
                }
                sub.addSeparator();
                sub.addItem (baseId + 5000 + li * 100 + 99, "All of them");
                copy.addSubMenu ("To the " + other->title.toLowerCase(), sub);
            }
            ++li;
        }
        m.addSubMenu ("Copy to another palette", copy);
    }
}

/** The rekordbox colours as menu items (ids baseId .. baseId+15). */
inline void addCueColourPresets (juce::PopupMenu& m, int baseId, juce::Colour current)
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
}

/** Adds the rekordbox colours to a menu (ids baseId .. baseId+15), then the cue palette (see addSavedColourItems).
    Handle the result with handleCueColourMenu. */
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
    addSavedColourItems (m, baseId, current, SavedColours::cues());
}

class ColourPickerBubble;

/** Acts on a result from a menu built with addCueColourItems. Returns true if the id was one of its items.
    `set` gets the chosen colour (live while the custom picker is dragged). */
inline bool handleCueColourMenu (int result, int baseId, juce::Rectangle<int> screenArea, juce::Colour current,
                                 std::function<void (juce::Colour)> set);

/** Loop locator modes: ids baseId (normal), baseId+1 (active), baseId+10+n (cycle n times: 2, 4, 8, 16, 32). */
inline void addLoopModeItems (juce::PopupMenu& m, int baseId, int mode, int cycles)
{
    m.addSectionHeader ("Loop");
    m.addItem (baseId, "Ordinary loop (called up with the marker)", true, mode == 0);
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
class ColourPickerBubble : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    ~ColourPickerBubble() override { flush(); }

    ColourPickerBubble (juce::Colour initial, std::function<void (juce::Colour)> changed, SavedColours& library)
        : onChange (std::move (changed)), lib (library)
    {
        selector.setCurrentColour (initial, juce::dontSendNotification);
        selector.addChangeListener (this);
        selector.setColour (juce::ColourSelector::backgroundColourId, juce::Colour (0xff1c1e22));
        selector.setColour (juce::ColourSelector::labelTextColourId, juce::Colour (0xffe8e9eb));
        addAndMakeVisible (selector);

        // + saves the colour (with a name you type) to the saved colours under the presets
        addAndMakeVisible (saveButton);
        saveButton.setTooltip ("Save this colour to the " + lib.title.toLowerCase());
        saveButton.onClick = [this]
        {
            nameBox.setText ({}, false);
            nameBox.setVisible (true);
            okButton.setVisible (true);
            saveButton.setVisible (false);
            nameBox.grabKeyboardFocus();
        };
        addChildComponent (nameBox);
        nameBox.setTextToShowWhenEmpty ("Name this colour, then Enter", juce::Colour (0xff8a8aa0));
        nameBox.onReturnKey = [this] { saveNamed(); };
        nameBox.onEscapeKey = [this] { endNaming(); };
        addChildComponent (okButton);
        okButton.onClick = [this] { saveNamed(); };
        addAndMakeVisible (savedLabel);
        savedLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9a97b6));
        savedLabel.setFont (juce::FontOptions (12.0f));
        setSize (300, 356);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromBottom (32).reduced (6, 4);
        selector.setBounds (r);
        saveButton.setBounds (bar.removeFromLeft (150));
        savedLabel.setBounds (bar.withTrimmedLeft (6));
        auto nb = getLocalBounds().removeFromBottom (32).reduced (6, 4);
        okButton.setBounds (nb.removeFromRight (50));
        nb.removeFromRight (4);
        nameBox.setBounds (nb);
    }

    static void show (juce::Rectangle<int> screenArea, juce::Colour initial, std::function<void (juce::Colour)> changed,
                      SavedColours& library = SavedColours::cues())
    {
        auto bubble = std::make_unique<ColourPickerBubble> (initial, std::move (changed), library);
        juce::CallOutBox::launchAsynchronously (std::move (bubble), screenArea, nullptr);
    }

private:
    // Dragging around the colour space sends a stream of changes: pass them on at most ~20 times a second,
    // and always the last one.
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        pending = true;
        if (! isTimerRunning()) { flush(); startTimer (50); }
    }
    void timerCallback() override
    {
        if (pending) flush(); else stopTimer();
    }
    void flush()
    {
        if (! pending) return;
        pending = false;
        if (onChange) onChange (selector.getCurrentColour().withAlpha (1.0f));
    }
    bool pending = false;

    void saveNamed()
    {
        lib.add (nameBox.getText().trim(), selector.getCurrentColour());
        savedLabel.setText ("Saved \"" + lib.list().back().name + "\" to the " + lib.title.toLowerCase(), juce::dontSendNotification);
        endNaming();
    }
    void endNaming()
    {
        nameBox.setVisible (false);
        okButton.setVisible (false);
        saveButton.setVisible (true);
    }

    juce::TextButton saveButton { "+  SAVE COLOUR" }, okButton { "OK" };
    juce::TextEditor nameBox;
    juce::Label savedLabel;
    juce::ColourSelector selector { juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                                    | juce::ColourSelector::showColourspace | juce::ColourSelector::editableColour };
    std::function<void (juce::Colour)> onChange;
    SavedColours& lib;
};

/** Acts on a result from addSavedColourItems: returns true if it was one of its items. */
inline bool handleSavedColourMenu (int result, int baseId, juce::Rectangle<int> screenArea, juce::Colour current,
                                   std::function<void (juce::Colour)> set, SavedColours& lib)
{
    const auto& saved = lib.list();
    if (result == baseId + 16)
        ColourPickerBubble::show (screenArea, current, set, lib);
    else if (result >= baseId + 20 && result < baseId + 20 + (int) saved.size() && result < baseId + 60)
        set (saved[(size_t) (result - baseId - 20)].colour);
    else if (result >= baseId + 60 && result < baseId + 100)
        lib.remove (result - baseId - 60);
    else if (result >= baseId + 5000 && result < baseId + 5300)
    {
        auto* to = SavedColours::all()[(size_t) ((result - baseId - 5000) / 100)];
        const int i = (result - baseId - 5000) % 100;
        if (i == 99) { for (auto& e : saved) to->add (e.name, e.colour); }
        else if (i < (int) saved.size()) to->add (saved[(size_t) i].name, saved[(size_t) i].colour);
    }
    else
        return false;
    return true;
}

inline bool handleCueColourMenu (int result, int baseId, juce::Rectangle<int> screenArea, juce::Colour current,
                                 std::function<void (juce::Colour)> set)
{
    if (result >= baseId && result < baseId + 16)
    {
        set (juce::Colour (rekordboxCueColours()[(size_t) (result - baseId)].argb));
        return true;
    }
    return handleSavedColourMenu (result, baseId, screenArea, current, set, SavedColours::cues());
}

} // namespace orbit
