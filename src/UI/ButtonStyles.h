#pragma once
#include "Theme.h"
#include "ColourPicker.h"

namespace orbit
{

/** Your own colours for a button: outline, infill and text, each for off / on / pressed. A colour left at
    "default" (fully transparent) keeps the button's usual look. */
struct ButtonStyle
{
    enum Part { outline = 0, infill = 1, text = 2 };
    enum State { off = 0, on = 1, pressed = 2 };
    std::array<juce::Colour, 9> c {};   // [state * 3 + part]

    juce::Colour get (int state, int part) const { return c[(size_t) (state * 3 + part)]; }
    void set (int state, int part, juce::Colour col) { c[(size_t) (state * 3 + part)] = col; }
    bool isSet (int state, int part) const { return get (state, part).getAlpha() != 0; }
    bool anyIn (int state) const { return isSet (state, 0) || isSet (state, 1) || isSet (state, 2); }
    bool isEmpty() const { return ! anyIn (0) && ! anyIn (1) && ! anyIn (2); }
};

/** Every button's own colours, by function (the same on both decks), kept between sessions. */
class ButtonStyles
{
public:
    static ButtonStyles& get() { static ButtonStyles s; return s; }

    /** The key for a button: its action without the deck ("d1.sync" -> "sync"). */
    static juce::String keyFor (const juce::String& action)
    {
        if (action.length() > 3 && action[0] == 'd' && juce::CharacterFunctions::isDigit (action[1]) && action[2] == '.')
            return action.substring (3);
        return action;
    }

    const ButtonStyle* find (const juce::String& action) const
    {
        if (styles.empty() || action.isEmpty()) return nullptr;
        auto it = styles.find (keyFor (action));
        return it == styles.end() ? nullptr : &it->second;
    }
    ButtonStyle styleFor (const juce::String& action) const { auto* s = find (action); return s != nullptr ? *s : ButtonStyle(); }

    void set (const juce::String& action, const ButtonStyle& s)
    {
        if (s.isEmpty()) styles.erase (keyFor (action));
        else styles[keyFor (action)] = s;
        ++version;
        save();
        if (onChange) onChange();
    }
    int getVersion() const { return version; }
    std::function<void()> onChange;   // repaint the pages

private:
    ButtonStyles() { load(); }

    static juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Centrifuge").getChildFile ("button-styles.json");
    }
    void load()
    {
        const auto json = juce::JSON::parse (file());
        if (auto* o = json.getDynamicObject())
            for (auto& p : o->getProperties())
                if (auto* a = p.value.getArray())
                {
                    ButtonStyle s;
                    for (int i = 0; i < juce::jmin (9, a->size()); ++i) s.c[(size_t) i] = juce::Colour ((juce::uint32) (juce::int64) (*a)[i]);
                    if (! s.isEmpty()) styles[p.name.toString()] = s;
                }
    }
    void save() const
    {
        auto* o = new juce::DynamicObject();
        for (auto& [k, s] : styles)
        {
            juce::Array<juce::var> a;
            for (auto& col : s.c) a.add ((juce::int64) col.getARGB());
            o->setProperty (k, a);
        }
        file().getParentDirectory().createDirectory();
        file().replaceWithText (juce::JSON::toString (juce::var (o)));
    }

    std::map<juce::String, ButtonStyle> styles;
    int version = 0;
};

/** A chip-style button with your colours on top of its usual look (style may be null). */
inline void paintStyledChip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour lit,
                             bool isOn, bool isPressed, const ButtonStyle* s)
{
    const int st = isPressed ? ButtonStyle::pressed : isOn ? ButtonStyle::on : ButtonStyle::off;
    const bool lookOn = isOn || isPressed;
    if (s == nullptr || ! s->anyIn (st))
    {
        psyched::paintChip (g, r, text, lit, lookOn ? psyched::ChipState::on : psyched::ChipState::off);
        return;
    }
    psyched::paintChip (g, r, {}, lit, lookOn ? psyched::ChipState::on : psyched::ChipState::off);
    if (s->isSet (st, ButtonStyle::infill))
    {
        g.setColour (s->get (st, ButtonStyle::infill));
        g.fillRect (r.reduced (0.5f));
    }
    const auto outline = s->isSet (st, ButtonStyle::outline) ? s->get (st, ButtonStyle::outline) : lookOn ? lit : psyched::Colours::edge2;
    g.setColour (outline);
    g.drawRect (r.reduced (0.5f), 1.0f);
    if (text.isNotEmpty())
    {
        g.setFont (psyched::Fonts::chip (juce::jlimit (9.0f, 12.0f, r.getHeight() * 0.38f)));
        g.setColour (s->isSet (st, ButtonStyle::text) ? s->get (st, ButtonStyle::text) : lookOn ? lit : psyched::Colours::chipInk);
        g.drawFittedText (text.toUpperCase(), r.reduced (6.0f, 1.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
}

/** Right-click a button: its outline, infill and text colours for off / on / pressed. Colours come from the
    presets and the button palette (the picker's + saves to the button palette). */
class ButtonStyleEditor : public juce::Component
{
public:
    ButtonStyleEditor (juce::String actionId, juce::String label, juce::Colour litColour)
        : action (std::move (actionId)), name (std::move (label)), lit (litColour)
    {
        style = ButtonStyles::get().styleFor (action);
        for (int i = 0; i < 9; ++i)
        {
            auto* b = swatches.add (new juce::TextButton());
            b->onClick = [this, i] { pick (i / 3, i % 3); };
            addAndMakeVisible (b);
        }
        resetButton.onClick = [this] { style = {}; commit(); };
        addAndMakeVisible (resetButton);
        copyButton.setTooltip ("Copy these colours (paste them onto another button)");
        copyButton.onClick = [this] { clipboard() = style; hasClip() = true; pasteButton.setEnabled (true); };
        addAndMakeVisible (copyButton);
        pasteButton.onClick = [this] { if (hasClip()) { style = clipboard(); commit(); } };
        pasteButton.setEnabled (hasClip());
        addAndMakeVisible (pasteButton);
        refresh();
        setSize (380, 236);
    }

    static void show (juce::Rectangle<int> screenArea, const juce::String& action, const juce::String& label, juce::Colour lit)
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<ButtonStyleEditor> (action, label, lit), screenArea, nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        psyched::paintLabel (g, "BUTTON COLOURS - " + name, juce::Rectangle<float> (12.0f, 8.0f, (float) getWidth() - 24.0f, 16.0f));
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (11.0f));
        g.drawText ("The same on both decks. Right-click any button to set its colours.", 12, 24, getWidth() - 24, 14, juce::Justification::centredLeft);
        const char* states[3] = { "OFF", "ON", "PRESSED" };
        const char* parts[3] = { "OUTLINE", "INFILL", "TEXT" };
        for (int p = 0; p < 3; ++p)
            psyched::paintLabel (g, parts[p], cell (0, p).withY (44.0f).withHeight (14.0f), juce::Justification::centred, Theme::textDim, 9.0f);
        psyched::paintLabel (g, "PREVIEW", cell (0, 3).withY (44.0f).withHeight (14.0f), juce::Justification::centred, Theme::textDim, 9.0f);
        for (int s = 0; s < 3; ++s)
        {
            psyched::paintLabel (g, states[s], juce::Rectangle<float> (12.0f, cell (s, 0).getY(), 60.0f, 24.0f), juce::Justification::centredLeft, Theme::text, 10.0f);
            paintStyledChip (g, cell (s, 3), name.length() > 8 ? name.substring (0, 8) : name, lit, s >= 1, s == 2, &style);
        }
    }

    void resized() override
    {
        for (int i = 0; i < 9; ++i) swatches[i]->setBounds (cell (i / 3, i % 3).toNearestInt());
        auto bar = getLocalBounds().removeFromBottom (36).reduced (12, 6);
        resetButton.setBounds (bar.removeFromRight (90));
        bar.removeFromRight (6);
        pasteButton.setBounds (bar.removeFromRight (70));
        bar.removeFromRight (6);
        copyButton.setBounds (bar.removeFromRight (70));
    }

private:
    juce::Rectangle<float> cell (int state, int col) const
    {
        const float x0 = 78.0f, w = 66.0f, gap = 6.0f;
        return { x0 + col * (w + gap), 62.0f + state * 34.0f, col == 3 ? 76.0f : w, 26.0f };
    }

    void refresh()
    {
        for (int i = 0; i < 9; ++i)
        {
            const auto c = style.get (i / 3, i % 3);
            auto* b = swatches[i];
            b->setButtonText (c.getAlpha() == 0 ? "DEFAULT" : juce::String());
            b->setColour (juce::TextButton::buttonColourId, c.getAlpha() == 0 ? psyched::Colours::field : c);
            b->setTooltip (c.getAlpha() == 0 ? "Default - click to choose" : c.toDisplayString (false));
        }
        repaint();
    }

    void commit()
    {
        ButtonStyles::get().set (action, style);
        refresh();
    }

    void pick (int state, int part)
    {
        juce::PopupMenu m;
        m.addItem (1, "Default", true, ! style.isSet (state, part));
        m.addSeparator();
        static const std::array<std::pair<const char*, juce::uint32>, 5> neutrals { {
            { "White", 0xffffffff }, { "Light grey", 0xffbdb9d8 }, { "Grey", 0xff6f6b8c }, { "Dark grey", 0xff24203a }, { "Black", 0xff000000 } } };
        const auto current = style.isSet (state, part) ? style.get (state, part) : lit;
        for (int i = 0; i < (int) neutrals.size(); ++i)
        {
            juce::PopupMenu::Item it (neutrals[(size_t) i].first);
            it.itemID = 2 + i;
            it.colour = juce::Colour (neutrals[(size_t) i].second);
            it.isTicked = style.isSet (state, part) && current.getARGB() == neutrals[(size_t) i].second;
            m.addItem (it);
        }
        m.addSeparator();
        const auto& cols = rekordboxCueColours();
        for (int i = 0; i < (int) cols.size(); ++i)
        {
            juce::PopupMenu::Item it (cols[(size_t) i].name);
            it.itemID = 100 + i;
            it.colour = juce::Colour (cols[(size_t) i].argb);
            it.isTicked = style.isSet (state, part) && current.getARGB() == cols[(size_t) i].argb;
            m.addItem (it);
        }
        addSavedColourItems (m, 200, current, SavedColours::buttons());
        const auto area = swatches[state * 3 + part]->getScreenBounds();
        juce::Component::SafePointer<ButtonStyleEditor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (swatches[state * 3 + part]),
                         [safe, state, part, area, current] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto set = [safe, state, part] (juce::Colour c) { if (safe != nullptr) { safe->style.set (state, part, c.withAlpha (1.0f)); safe->commit(); } };
            if (r == 1) { safe->style.set (state, part, {}); safe->commit(); }
            else if (r >= 2 && r < 7)
            {
                static const juce::uint32 n[5] { 0xffffffff, 0xffbdb9d8, 0xff6f6b8c, 0xff24203a, 0xff000000 };
                set (juce::Colour (n[r - 2]));
            }
            else if (r >= 100 && r < 116) set (juce::Colour (rekordboxCueColours()[(size_t) (r - 100)].argb));
            else handleSavedColourMenu (r, 200, area, current, set, SavedColours::buttons());
        });
    }

    static ButtonStyle& clipboard() { static ButtonStyle c; return c; }
    static bool& hasClip() { static bool b = false; return b; }

    juce::String action, name;
    juce::Colour lit;
    ButtonStyle style;
    juce::OwnedArray<juce::TextButton> swatches;
    juce::TextButton resetButton { "RESET" }, copyButton { "COPY" }, pasteButton { "PASTE" };
};

} // namespace orbit
