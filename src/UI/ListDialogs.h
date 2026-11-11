#pragma once
#include "Theme.h"
#include "../Library/UserLists.h"

namespace orbit
{

/** A dynamic playlist's rules: tracks that match all of them are in it (kept up to date by itself). */
class RulesEditor : public juce::Component
{
public:
    RulesEditor (UserLists::Rules r, std::function<void (const UserLists::Rules&)> changed) : rules (std::move (r)), onChange (std::move (changed))
    {
        auto text = [this] (juce::TextEditor& e, const juce::String& v, const juce::String& hint)
        {
            addAndMakeVisible (e);
            e.setText (v, false);
            e.setTextToShowWhenEmpty (hint, Theme::textFaint);
            e.onTextChange = [this] { apply(); };
        };
        text (bpmMin, rules.bpmMin > 0 ? juce::String (rules.bpmMin, 1) : juce::String(), "min");
        text (bpmMax, rules.bpmMax > 0 ? juce::String (rules.bpmMax, 1) : juce::String(), "max");
        text (keys, rules.keys, "keys, e.g. 8A, 9A, Am (empty = any)");
        text (genre, rules.genre, "genre contains...");
        text (words, rules.text, "title / artist contains...");
        for (auto* c : { &rating, &colour, &stems, &tag }) addAndMakeVisible (*c);
        rating.addItemList ({ "Any rating", "1 star +", "2 stars +", "3 stars +", "4 stars +", "5 stars" }, 1);
        rating.setSelectedId (rules.ratingMin + 1, juce::dontSendNotification);
        colour.addItem ("Any colour", 1);
        const char* cn[] = { "Pink", "Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple" };
        for (int i = 0; i < 8; ++i) colour.addItem (cn[i], i + 2);
        colour.setSelectedId (rules.colour + 1, juce::dontSendNotification);
        stems.addItemList ({ "With or without stems", "With stems", "Without stems" }, 1);
        stems.setSelectedId (rules.stems + 1, juce::dontSendNotification);
        tag.addItem ("Any tag", 1);
        int id = 2;
        for (auto& c : UserLists::get().tagCategories())
            for (auto& t : c.tags) { tag.addItem (c.name + " / " + t, id++); tagNames.add (c.name + "/" + t); }
        const int ti = tagNames.indexOf (rules.tag);
        tag.setSelectedId (ti >= 0 ? ti + 2 : 1, juce::dontSendNotification);
        for (auto* c : { &rating, &colour, &stems, &tag }) c->onChange = [this] { apply(); };
        setSize (340, 9 * 30 + 30);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff141221));
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (12.0f));
        g.drawText ("Tracks that match all of these are in the playlist", 8, 4, getWidth() - 16, 20, juce::Justification::centredLeft);
        g.drawText ("BPM", 8, 30, 40, 24, juce::Justification::centredLeft);
        g.drawText ("to", 176, 30, 24, 24, juce::Justification::centred);
    }

    void resized() override
    {
        bpmMin.setBounds (52, 30, 120, 24);
        bpmMax.setBounds (204, 30, 128, 24);
        int y = 60;
        for (juce::Component* c : { (juce::Component*) &keys, (juce::Component*) &rating, (juce::Component*) &colour, (juce::Component*) &genre,
                                    (juce::Component*) &stems, (juce::Component*) &tag, (juce::Component*) &words })
        { c->setBounds (8, y, getWidth() - 16, 24); y += 30; }
    }

private:
    void apply()
    {
        rules.bpmMin = bpmMin.getText().getDoubleValue();
        rules.bpmMax = bpmMax.getText().getDoubleValue();
        rules.keys = keys.getText().trim();
        rules.genre = genre.getText().trim();
        rules.text = words.getText().trim();
        rules.ratingMin = juce::jmax (0, rating.getSelectedId() - 1);
        rules.colour = juce::jmax (0, colour.getSelectedId() - 1);
        rules.stems = juce::jmax (0, stems.getSelectedId() - 1);
        rules.tag = tag.getSelectedId() >= 2 ? tagNames[tag.getSelectedId() - 2] : juce::String();
        if (onChange) onChange (rules);
    }

    UserLists::Rules rules;
    std::function<void (const UserLists::Rules&)> onChange;
    juce::TextEditor bpmMin, bpmMax, keys, genre, words;
    juce::ComboBox rating, colour, stems, tag;
    juce::StringArray tagNames;
};

/** Tag categories and their tags, one category per line: "Vibe: dark, uplifting, hypnotic". */
class TagsEditor : public juce::Component
{
public:
    explicit TagsEditor (std::function<void()> changed) : onChange (std::move (changed))
    {
        addAndMakeVisible (text);
        text.setMultiLine (true, false);
        text.setReturnKeyStartsNewLine (true);
        text.setFont (Theme::font (13.5f));
        juce::StringArray lines;
        for (auto& c : UserLists::get().tagCategories()) lines.add (c.name + ": " + c.tags.joinIntoString (", "));
        text.setText (lines.joinIntoString ("\n"), false);
        text.setTextToShowWhenEmpty ("Vibe: dark, uplifting, hypnotic\nSituation: warm-up, peak, closing", Theme::textFaint);
        addAndMakeVisible (save);
        save.onClick = [this]
        {
            std::vector<UserLists::TagCategory> cats;
            for (auto& line : juce::StringArray::fromLines (text.getText()))
            {
                if (line.trim().isEmpty()) continue;
                UserLists::TagCategory c;
                c.name = line.upToFirstOccurrenceOf (":", false, false).trim();
                for (auto& t : juce::StringArray::fromTokens (line.fromFirstOccurrenceOf (":", false, false), ",", ""))
                    if (t.trim().isNotEmpty()) c.tags.add (t.trim());
                if (c.name.isNotEmpty()) cats.push_back (c);
            }
            UserLists::get().setTagCategories (cats);
            if (onChange) onChange();
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
        };
        setSize (420, 260);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff141221));
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (12.0f));
        g.drawText ("One category per line - Category: tag, tag, tag", 8, 4, getWidth() - 16, 20, juce::Justification::centredLeft);
    }
    void resized() override
    {
        text.setBounds (8, 26, getWidth() - 16, getHeight() - 64);
        save.setBounds (getWidth() - 88, getHeight() - 32, 80, 24);
    }
private:
    std::function<void()> onChange;
    juce::TextEditor text;
    juce::TextButton save { "SAVE" };
};

} // namespace orbit
