#include "MappingView.h"
#include "Theme.h"

namespace orbit
{

MappingView::MappingView (MidiManager& m, std::function<void()> saveFn) : midi (m), save (std::move (saveFn))
{
    addAndMakeVisible (table);
    auto& hdr = table.getHeader();
    hdr.addColumn ("Function", 1, 380, 160, 600);
    hdr.addColumn ("Mapped to", 3, 300, 100, 800);
    hdr.setStretchToFitActive (true);
    table.setRowHeight (24);
    table.setColour (juce::ListBox::backgroundColourId, Theme::panel);

    addAndMakeVisible (monitor);
    monitor.setRowHeight (20);
    monitor.setColour (juce::ListBox::backgroundColourId, Theme::waveBg);

    addAndMakeVisible (filter);
    filter.setTextToShowWhenEmpty ("Search functions (e.g. hot cue, stem, skip) - or open the categories below", Theme::textFaint);
    filter.setFont (Theme::font (14.0f));
    filter.onTextChange = [this] { refreshFilter(); };

    for (auto* b : { &learnButton, &learnAddButton, &clearButton, &guidedButton, &skipButton, &stopButton, &invertButton })
        addAndMakeVisible (*b);
    learnButton.getProperties().set ("psyched.solid", true);
    learnButton.setColour (juce::TextButton::buttonOnColourId, Theme::map);
    invertButton.setClickingTogglesState (true);
    skipButton.setVisible (false);
    stopButton.setVisible (false);

    learnButton.onClick = [this] { learnSelected(); };
    learnAddButton.onClick = [this] { learnSelected (true); };
    learnAddButton.setTooltip ("Map this function to a control while keeping what's already on it (one knob, several functions)");
    clearButton.onClick = [this]
    {
        auto* a = selectedAction();
        if (a == nullptr) return;
        midi.removeBindingsFor (a->id);
        save();
        table.repaint();
        updateDetail();
    };
    guidedButton.onClick = [this] { startGuided(); };
    skipButton.onClick = [this] { advanceGuided (true); };
    stopButton.onClick = [this]
    {
        guidedIndex = -1;
        midi.cancelLearn();
        skipButton.setVisible (false);
        stopButton.setVisible (false);
        banner.setText ({}, juce::dontSendNotification);
    };
    invertButton.onClick = [this]
    {
        auto* a = selectedAction();
        if (a == nullptr) return;
        auto all = midi.getBindings();
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].actionId == a->id)
            {
                all[(size_t) i].invert = invertButton.getToggleState();
                midi.updateBinding (i, all[(size_t) i]);
            }
        save();
        table.repaint();
    };

    addAndMakeVisible (modeBox);
    modeBox.addItemList ({ "Absolute (7-bit)", "Absolute (14-bit)", "Relative (64 = 0)", "Relative (2's complement)", "Relative (sign bit)" }, 1);
    modeBox.onChange = [this]
    {
        auto* a = selectedAction();
        if (a == nullptr) return;
        auto all = midi.getBindings();
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].actionId == a->id)
            {
                all[(size_t) i].mode = (MidiBinding::Mode) (modeBox.getSelectedId() - 1);
                midi.updateBinding (i, all[(size_t) i]);
            }
        save();
        table.repaint();
    };

    addAndMakeVisible (sensitivity);
    sensitivity.setSliderStyle (juce::Slider::LinearHorizontal);
    sensitivity.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    sensitivity.setRange (0.1, 8.0, 0.05);
    sensitivity.setSkewFactorFromMidPoint (1.0);
    sensitivity.setValue (1.0, juce::dontSendNotification);
    sensitivity.onValueChange = [this]
    {
        auto* a = selectedAction();
        if (a == nullptr) return;
        auto all = midi.getBindings();
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].actionId == a->id)
            {
                all[(size_t) i].sensitivity = sensitivity.getValue();
                midi.updateBinding (i, all[(size_t) i]);
            }
    };
    sensitivity.onDragEnd = [this] { save(); };

    addAndMakeVisible (sensLabel);
    sensLabel.setText ("Sensitivity", juce::dontSendNotification);
    sensLabel.setFont (Theme::font (12.5f));
    sensLabel.setColour (juce::Label::textColourId, Theme::textDim);

    addAndMakeVisible (banner);
    banner.setFont (Theme::font (15.0f, true));
    banner.setColour (juce::Label::textColourId, Theme::accent);

    addAndMakeVisible (status);
    status.setFont (Theme::font (13.0f));
    status.setColour (juce::Label::textColourId, Theme::textDim);

    midi.onLearned = [this] (const MidiBinding& b)
    {
        save();
        table.repaint();
        updateDetail();
        if (guidedIndex >= 0) advanceGuided (false);
        else
        {
            auto* def = findAction (b.actionId);
            banner.setText ("Mapped " + (def != nullptr ? def->label : b.actionId) + "  ->  " + b.describe(), juce::dontSendNotification);
        }
    };

    refreshFilter();
    startTimerHz (10);
}

MappingView::~MappingView()
{
    midi.onLearned = nullptr;
    midi.cancelLearn();
}

const ActionDef* MappingView::selectedAction() const
{
    const int r = table.getSelectedRow();
    return r >= 0 && r < (int) rows.size() ? rows[(size_t) r].action : nullptr;
}

void MappingView::refreshFilter()
{
    const auto* keep = selectedAction();
    rows.clear();
    const auto q = filter.getText().trim().toLowerCase();
    const bool searching = q.isNotEmpty();

    juce::String lastTop, lastSub;
    for (auto& a : allActions())
    {
        if (searching && ! (a.group + " " + a.label).toLowerCase().contains (q)) continue;
        const auto top = topOf (a), sub = subOf (a);
        if (top != lastTop)
        {
            rows.push_back ({ 0, top, top, nullptr });
            lastTop = top;
            lastSub = {};
        }
        const bool topOpen = searching || expanded.count (top) > 0;
        if (! topOpen) continue;
        if (sub.isNotEmpty() && sub != lastSub)
        {
            rows.push_back ({ 1, sub, sub.fromFirstOccurrenceOf (" - ", false, false), nullptr });
            lastSub = sub;
        }
        const bool subOpen = sub.isEmpty() || searching || expanded.count (sub) > 0;
        if (subOpen)
            rows.push_back ({ sub.isEmpty() ? 1 : 2, a.id, a.label, &a });
    }
    table.updateContent();
    table.deselectAllRows();
    if (keep != nullptr)
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i].action == keep) table.selectRow (i, true, true);
    table.repaint();
}

void MappingView::revealAction (const juce::String& id)
{
    if (auto* a = findAction (id))
    {
        expanded.insert (topOf (*a));
        if (subOf (*a).isNotEmpty()) expanded.insert (subOf (*a));
    }
    refreshFilter();
}

void MappingView::cellClicked (int row, int, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].action != nullptr) return;
    if (filter.getText().trim().isNotEmpty()) return;   // everything is open while searching
    const auto key = rows[(size_t) row].key;
    if (expanded.count (key)) expanded.erase (key);
    else expanded.insert (key);
    refreshFilter();
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i].action == nullptr && rows[(size_t) i].key == key) { table.selectRow (i); break; }
}

void MappingView::paint (juce::Graphics& g)
{
    psyched::paintLabel (g, "MIDI monitor - move anything on the XDJ-XZ", monitor.getBounds().withY (monitor.getY() - 22).withHeight (20).toFloat());
}

void MappingView::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto right = r.removeFromRight (juce::jmin (420, r.getWidth() / 3));
    r.removeFromRight (12);

    status.setBounds (right.removeFromTop (24));
    right.removeFromTop (26);
    monitor.setBounds (right);

    auto top = r.removeFromTop (32);
    guidedButton.setBounds (top.removeFromRight (130));
    top.removeFromRight (8);
    filter.setBounds (top);
    r.removeFromTop (6);

    auto bannerRow = r.removeFromTop (30);
    stopButton.setBounds (bannerRow.removeFromRight (70).reduced (0, 2));
    bannerRow.removeFromRight (6);
    skipButton.setBounds (bannerRow.removeFromRight (70).reduced (0, 2));
    banner.setBounds (bannerRow);

    auto bottom = r.removeFromBottom (36);
    r.removeFromBottom (6);
    table.setBounds (r);

    learnButton.setBounds (bottom.removeFromLeft (90).reduced (0, 3));
    bottom.removeFromLeft (6);
    learnAddButton.setBounds (bottom.removeFromLeft (110).reduced (0, 3));
    bottom.removeFromLeft (6);
    clearButton.setBounds (bottom.removeFromLeft (80).reduced (0, 3));
    bottom.removeFromLeft (6);
    invertButton.setBounds (bottom.removeFromLeft (80).reduced (0, 3));
    bottom.removeFromLeft (12);
    modeBox.setBounds (bottom.removeFromLeft (210).reduced (0, 4));
    bottom.removeFromLeft (12);
    sensLabel.setBounds (bottom.removeFromLeft (76));
    sensitivity.setBounds (bottom.removeFromLeft (220).reduced (0, 4));
}

int MappingView::getNumRows() { return (int) rows.size(); }

void MappingView::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    if (row >= (int) rows.size()) return;
    const auto& rw = rows[(size_t) row];
    g.setColour (rw.action == nullptr ? (rw.level == 0 ? Theme::panelHi : Theme::panel.brighter (0.04f))
                                      : (row % 2 ? Theme::panel : Theme::panel.brighter (0.025f)));
    g.fillRect (0, 0, w, h);
    if (rw.action != nullptr && rw.action->id == midi.getLearningAction())
    {
        const bool blink = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
        g.setColour (Theme::accent.withAlpha (blink ? 0.45f : 0.25f));
        g.fillRect (0, 0, w, h);
    }
    else if (selected)
    {
        g.setColour (Theme::accent.withAlpha (0.2f));
        g.fillRect (0, 0, w, h);
    }
}

void MappingView::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (row >= (int) rows.size()) return;
    const auto& rw = rows[(size_t) row];
    const bool searching = filter.getText().trim().isNotEmpty();

    if (rw.action == nullptr)
    {
        // Category: arrow, name, and how many of its functions are mapped
        int total = 0, mapped = 0;
        for (auto& a : allActions())
            if ((rw.level == 0 ? topOf (a) : a.group) == rw.key)
            {
                ++total;
                if (! midi.getBindingsFor (a.id).empty()) ++mapped;
            }
        if (col == 1)
        {
            const int indent = 8 + rw.level * 22;
            const bool open = searching || expanded.count (rw.key) > 0;
            juce::Path arrow;
            const float cx = (float) indent + 5.0f, cy = (float) h * 0.5f;
            if (open) arrow.addTriangle (cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f, cx, cy + 4.0f);
            else      arrow.addTriangle (cx - 3.0f, cy - 5.0f, cx - 3.0f, cy + 5.0f, cx + 4.0f, cy);
            g.setColour (rw.level == 0 ? Theme::accent : Theme::textDim);
            g.fillPath (arrow);
            g.setColour (Theme::text);
            g.setFont (Theme::font (rw.level == 0 ? 14.5f : 13.5f, true));
            g.drawText (rw.title, juce::Rectangle<int> (indent + 18, 0, w - indent - 24, h), juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (mapped > 0 ? Theme::good.withAlpha (0.8f) : Theme::textFaint);
            g.setFont (Theme::font (12.5f));
            g.drawText (juce::String (mapped) + " of " + juce::String (total) + " mapped", juce::Rectangle<int> (6, 0, w - 12, h),
                        juce::Justification::centredLeft, true);
        }
        return;
    }

    const auto* a = rw.action;
    juce::String s;
    g.setFont (Theme::font (13.5f));
    g.setColour (Theme::text);
    if (col == 1)
    {
        const int indent = 8 + rw.level * 22 + 18;
        g.drawText (searching ? a->group + "  >  " + a->label : a->label, juce::Rectangle<int> (indent, 0, w - indent - 6, h),
                    juce::Justification::centredLeft, true);
        return;
    }
    juce::StringArray parts;
    for (auto& b : midi.getBindingsFor (a->id))
    {
        auto text = (b.device.isNotEmpty() ? b.device + ": " : juce::String()) + b.describe();
        const auto shared = midi.getActionsSharingControl (b);
        if (! shared.empty()) text << "  (+" << (int) shared.size() << " more on this control)";
        parts.add (text);
    }
    s = parts.joinIntoString ("   |   ");
    g.setColour (s.isEmpty() ? Theme::textFaint : Theme::good);
    if (s.isEmpty()) s = "-";
    g.drawText (s, juce::Rectangle<int> (6, 0, w - 12, h), juce::Justification::centredLeft, true);
}

void MappingView::cellDoubleClicked (int, int, const juce::MouseEvent&)
{
    learnSelected();
}

void MappingView::selectedRowsChanged (int)
{
    updateDetail();
}

void MappingView::updateDetail()
{
    auto* a = selectedAction();
    if (a == nullptr) return;
    auto bs = midi.getBindingsFor (a->id);
    if (! bs.empty())
    {
        modeBox.setSelectedId ((int) bs.front().mode + 1, juce::dontSendNotification);
        invertButton.setToggleState (bs.front().invert, juce::dontSendNotification);
        sensitivity.setValue (bs.front().sensitivity, juce::dontSendNotification);
    }
    const bool encoder = a->kind == ActionKind::encoder;
    sensitivity.setEnabled (encoder);
    sensLabel.setEnabled (encoder);
}

void MappingView::learnSelected (bool add)
{
    const auto* a = selectedAction();
    if (a == nullptr)
    {
        banner.setText ("Open a category and pick a function first", juce::dontSendNotification);
        return;
    }
    midi.startLearn (a->id, add);
    banner.setText (juce::String (add ? "Adding: " : "Waiting: ") + "move or press the control for  " + a->group + "  >  " + a->label,
                    juce::dontSendNotification);
    table.repaint();
}

void MappingView::startGuided()
{
    guidedSteps.clear();
    for (int d = 1; d <= 2; ++d)
    {
        const auto p = "d" + juce::String (d) + ".";
        for (auto s : { "play", "cue", "sync", "master", "keylock", "shift", "jog.touch", "jog.turn", "jog.side",
                        "pitch", "tempo.reset", "loop.in", "loop.out", "loop.reloop", "beatjump.back", "beatjump.fwd" })
            guidedSteps.push_back (p + s);
        for (int h = 1; h <= 8; ++h)
            guidedSteps.push_back (p + "hotcue." + juce::String (h));
        guidedSteps.push_back (p + "load");
    }
    for (auto s : { "browse.scroll", "browse.enter", "browse.back" })
        guidedSteps.push_back (s);

    guidedIndex = -1;
    skipButton.setVisible (true);
    stopButton.setVisible (true);
    advanceGuided (false);
}

void MappingView::advanceGuided (bool)
{
    ++guidedIndex;
    if (guidedIndex >= (int) guidedSteps.size())
    {
        guidedIndex = -1;
        midi.cancelLearn();
        skipButton.setVisible (false);
        stopButton.setVisible (false);
        banner.setText ("Guided setup finished. Map the stem volumes (and anything else) from the list.", juce::dontSendNotification);
        return;
    }

    const auto id = guidedSteps[(size_t) guidedIndex];
    const auto* a = findAction (id);
    midi.startLearn (id);
    banner.setText ("[" + juce::String (guidedIndex + 1) + "/" + juce::String ((int) guidedSteps.size()) + "]  "
                    + (a != nullptr ? a->group + "  >  " + a->label : id) + "  -  press / move it now",
                    juce::dontSendNotification);

    revealAction (id);
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i].action != nullptr && rows[(size_t) i].action->id == id)
        {
            table.selectRow (i);
            table.scrollToEnsureRowIsOnscreen (i);
        }
}

void MappingView::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row >= (int) monitorRows.size()) return;
    const auto& e = monitorRows[(size_t) row];
    g.setFont (Theme::font (12.5f));
    g.setColour (Theme::textFaint);
    g.drawText (e.device, juce::Rectangle<int> (6, 0, 90, h), juce::Justification::centredLeft, true);
    g.setColour (Theme::text);
    g.drawText (e.text, juce::Rectangle<int> (100, 0, 150, h), juce::Justification::centredLeft, true);
    g.setColour (e.action.isNotEmpty() ? Theme::good : Theme::textFaint);
    g.drawText (e.action.isNotEmpty() ? e.action : juce::String ("(not mapped)"), juce::Rectangle<int> (252, 0, w - 256, h),
                juce::Justification::centredLeft, true);
}

void MappingView::timerCallback()
{
    const auto c = midi.getMessageCounter();
    if (c != lastCounter)
    {
        lastCounter = c;
        monitorRows = midi.getRecentMessages (200);
        monitor.updateContent();
        monitor.repaint();
    }

    const auto seen = midi.getSeenControls();
    status.setText (midi.getXdjStatus() + "    |    " + juce::String ((int) seen.size()) + " controls seen    |    "
                    + juce::String ((int) midi.getBindings().size()) + " mappings", juce::dontSendNotification);

    if (midi.getLearningAction().isNotEmpty())
        table.repaint();
}

} // namespace orbit
