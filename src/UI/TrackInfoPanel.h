#pragma once
#include "Theme.h"
#include "ColourPicker.h"
#include "../Library/TrackInfo.h"
#include "../Audio/MusicalKey.h"

namespace orbit
{

/** The browser's fold-out info panel: the selected tracks' info, where typing in a field sets it on every selected
    track (fields that differ show "(several)"). WRITE TO FILE TAGS copies the info into the audio files' own tags. */
class TrackInfoPanel : public juce::Component
{
public:
    /** field: "title", "artist", "album", "genre", "label", "comment", "key", "rating", "colour" */
    std::function<void (const std::vector<TrackInfo>&, const juce::String& field, const juce::String& value)> onEdit;
    std::function<void (const std::vector<TrackInfo>&)> onWriteTags;
    std::function<void (bool)> onTagColourNames;   // the tick box: titles and artists in their tag colour

    void setTagColourNames (bool on) { colourNames.setToggleState (on, juce::dontSendNotification); }

    TrackInfoPanel()
    {
        const char* labels[] = { "TITLE", "ARTIST", "ALBUM", "GENRE", "LABEL", "COMMENT", "KEY" };
        const char* ids[] = { "title", "artist", "album", "genre", "label", "comment", "key" };
        for (int i = 0; i < numText; ++i)
        {
            auto& f = fields[(size_t) i];
            f.id = ids[i];
            f.label = labels[i];
            addAndMakeVisible (f.editor);
            f.editor.setFont (Theme::font (13.0f));
            f.editor.setColour (juce::TextEditor::backgroundColourId, Theme::panelHi);
            f.editor.setColour (juce::TextEditor::outlineColourId, Theme::line);
            f.editor.setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
            f.editor.setColour (juce::TextEditor::textColourId, Theme::text);
            f.editor.setMultiLine (false);
            f.editor.onReturnKey = [this, i] { commit (i); };
            f.editor.onFocusLost = [this, i] { commit (i); };
            f.editor.onEscapeKey = [this, i] { auto& ff = fields[(size_t) i]; ff.editor.setText (ff.shown, false); };
        }
        addAndMakeVisible (rating);
        rating.addItemList ({ "No rating", "1 star", "2 stars", "3 stars", "4 stars", "5 stars" }, 1);
        rating.onChange = [this] { if (! updating && rating.getSelectedId() > 0 && onEdit) onEdit (tracks, "rating", juce::String (rating.getSelectedId() - 1)); };
        // the colour tag: rekordbox's 8, the cue colours, your Tag palette or a custom colour
        addAndMakeVisible (colour);
        colour.onClick = [this] { showColourMenu(); };
        addAndMakeVisible (colourNames);
        colourNames.setButtonText ("Colour titles and artists by their tag colour");
        colourNames.onClick = [this] { if (onTagColourNames) onTagColourNames (colourNames.getToggleState()); };
        addAndMakeVisible (writeTags);
        writeTags.setTooltip ("Write title, artist, album, genre, label, comment and key into the selected MP3 / FLAC files' own tags");
        writeTags.onClick = [this] { if (onWriteTags && ! tracks.empty()) onWriteTags (tracks); };
    }

    void setTracks (std::vector<TrackInfo> t)
    {
        // a field being typed in keeps its text until it's committed
        for (int i = 0; i < numText; ++i) if (fields[(size_t) i].editor.hasKeyboardFocus (true)) commit (i);
        tracks = std::move (t);
        const juce::ScopedValueSetter<bool> svs (updating, true);
        for (int i = 0; i < numText; ++i)
        {
            auto& f = fields[(size_t) i];
            bool same = true;
            juce::String v;
            for (size_t k = 0; k < tracks.size(); ++k)
            {
                const auto x = valueOf (tracks[k], f.id);
                if (k == 0) v = x; else same = same && x == v;
            }
            f.shown = same ? v : juce::String();
            f.editor.setText (f.shown, false);
            f.editor.setTextToShowWhenEmpty (tracks.empty() ? juce::String() : same ? juce::String ("-") : juce::String ("(several)"), Theme::textFaint);
            f.editor.setEnabled (! tracks.empty());
        }
        bool sameR = true, sameC = true;
        for (auto& x : tracks) { sameR = sameR && x.rating == tracks.front().rating; sameC = sameC && x.colour == tracks.front().colour; }
        rating.setSelectedId (! tracks.empty() && sameR ? tracks.front().rating + 1 : 0, juce::dontSendNotification);
        rating.setTextWhenNothingSelected (tracks.empty() ? juce::String() : "(several)");
        shownColour = ! tracks.empty() && sameC ? tracks.front().colour : juce::Colour();
        colour.setButtonText (tracks.empty() ? juce::String() : ! sameC ? juce::String ("(several)")
                              : shownColour.isTransparent() ? juce::String ("No colour") : colourName (shownColour));
        colour.setColour (juce::TextButton::buttonColourId, shownColour.isTransparent() ? Theme::panelHi : shownColour.darker (0.4f));
        colour.setEnabled (! tracks.empty());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::line);
        g.fillRect (0, 0, 1, getHeight());
        auto r = getLocalBounds().reduced (10, 8);
        psyched::paintTitle (g, "Track info", r.removeFromTop (20).withTrimmedRight (156).toFloat(), juce::Justification::centredLeft, Theme::accent, 11.0f);
        psyched::paintLabel (g, tracks.empty() ? juce::String ("Select tracks") : juce::String ((int) tracks.size()) + (tracks.size() == 1 ? " track" : " tracks selected - edits set them all"),
                             r.removeFromTop (16).withTrimmedRight (156).toFloat(), juce::Justification::centredLeft, Theme::textDim, 9.5f);
        for (auto& f : fields)
            psyched::paintLabel (g, f.label, f.labelArea.toFloat(), juce::Justification::centredLeft, Theme::textFaint, 9.0f);
        psyched::paintLabel (g, "RATING", ratingLabel.toFloat(), juce::Justification::centredLeft, Theme::textFaint, 9.0f);
        psyched::paintLabel (g, "COLOUR", colourLabel.toFloat(), juce::Justification::centredLeft, Theme::textFaint, 9.0f);
        if (noteArea.getHeight() > 12)
            psyched::paintLabel (g, "Saved in Centrifuge's library - your files change only with the button",
                                 noteArea.toFloat(), juce::Justification::topLeft, Theme::textFaint, 9.5f);
    }

    void resized() override
    {
        // the button sits at the top so it's always in view, however short the browser is
        auto r = getLocalBounds().reduced (10, 8);
        auto head = r.removeFromTop (36);
        writeTags.setBounds (head.removeFromRight (150).withSizeKeepingCentre (150, 26));
        noteArea = {};
        const int rowH = juce::jlimit (20, 24, (r.getHeight() - 10 * 3 - 22) / 9);
        auto row = [&] (juce::Rectangle<int>& label) { auto x = r.removeFromTop (rowH); label = x.removeFromLeft (64); r.removeFromTop (3); return x; };
        for (auto& f : fields) f.editor.setBounds (row (f.labelArea));
        rating.setBounds (row (ratingLabel));
        colour.setBounds (row (colourLabel));
        colourNames.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        noteArea = r.removeFromTop (30);
    }

private:
    static juce::String valueOf (const TrackInfo& t, const juce::String& id)
    {
        if (id == "title") return t.title;
        if (id == "artist") return t.artist;
        if (id == "album") return t.album;
        if (id == "genre") return t.genre;
        if (id == "label") return t.label;
        if (id == "comment") return t.comment;
        if (id == "key") return MusicalKey::display (t.key);
        return {};
    }

    static juce::String colourName (juce::Colour c)
    {
        const char* names[] = { "Pink", "Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple" };
        for (int i = 1; i <= 8; ++i) if (rekordboxTrackColour (i) == c) return names[i - 1];
        for (auto& n : rekordboxCueColours()) if (n.argb == c.getARGB()) return n.name;
        for (auto& e : SavedColours::tags().list()) if (e.colour == c) return e.name;
        return c.toDisplayString (false);
    }

    void showColourMenu()
    {
        if (tracks.empty()) return;
        juce::PopupMenu m;
        m.addItem (1, "No colour", true, shownColour.isTransparent());
        m.addSeparator();
        const char* names[] = { "Pink", "Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple" };
        for (int i = 1; i <= 8; ++i)
        {
            juce::PopupMenu::Item it (names[i - 1]);
            it.itemID = 10 + i;
            it.colour = rekordboxTrackColour (i);
            it.isTicked = shownColour == rekordboxTrackColour (i);
            m.addItem (it);
        }
        juce::PopupMenu more;
        addCueColourPresets (more, 100, shownColour);
        m.addSubMenu ("More colours", more);
        addSavedColourItems (m, 200, shownColour, SavedColours::tags());
        juce::Component::SafePointer<TrackInfoPanel> safe (this);
        const auto area = colour.getScreenBounds();
        const auto cur = shownColour.isTransparent() ? rekordboxTrackColour (1) : shownColour;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&colour), [safe, area, cur] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto set = [safe] (juce::Colour c) { if (safe != nullptr && safe->onEdit) safe->onEdit (safe->tracks, "colour", "#" + c.withAlpha (1.0f).toString()); };
            if (r == 1) { if (safe->onEdit) safe->onEdit (safe->tracks, "colour", "0"); }
            else if (r > 10 && r <= 18) { if (safe->onEdit) safe->onEdit (safe->tracks, "colour", juce::String (r - 10)); }
            else if (r >= 100 && r < 116) set (juce::Colour (rekordboxCueColours()[(size_t) (r - 100)].argb));
            else handleSavedColourMenu (r, 200, area, cur, set, SavedColours::tags());
        });
    }

    void commit (int i)
    {
        auto& f = fields[(size_t) i];
        const auto v = f.editor.getText().trim();
        if (updating || tracks.empty() || v == f.shown) return;
        if (v.isEmpty() && f.shown.isEmpty()) return;   // "(several)" left alone
        f.shown = v;
        if (onEdit) onEdit (tracks, f.id, v);
    }

    static constexpr int numText = 7;
    struct Field { juce::String id, label, shown; juce::TextEditor editor; juce::Rectangle<int> labelArea; };
    std::array<Field, numText> fields;
    juce::ComboBox rating;
    juce::TextButton colour;
    juce::ToggleButton colourNames;
    juce::Colour shownColour;
    juce::TextButton writeTags { "WRITE TO FILE TAGS" };
    juce::Rectangle<int> ratingLabel, colourLabel, noteArea;
    std::vector<TrackInfo> tracks;
    bool updating = false;
};

} // namespace orbit
