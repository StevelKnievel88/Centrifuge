#include "MappingView.h"
#include "Theme.h"

namespace orbit
{

MappingView::MappingView (MidiManager& m, std::function<void()> saveFn) : midi (m), save (std::move (saveFn))
{
    addAndMakeVisible (table);
    auto& hdr = table.getHeader();
    hdr.addColumn ("Section", 1, 170, 80, 300);
    hdr.addColumn ("Function", 2, 230, 100, 400);
    hdr.addColumn ("Mapped to", 3, 260, 100, 800);
    hdr.setStretchToFitActive (true);
    table.setRowHeight (24);
    table.setColour (juce::ListBox::backgroundColourId, Theme::panel);

    addAndMakeVisible (monitor);
    monitor.setRowHeight (20);
    monitor.setColour (juce::ListBox::backgroundColourId, Theme::waveBg);

    addAndMakeVisible (filter);
    filter.setTextToShowWhenEmpty ("Filter functions (e.g. deck 1, hot cue, stem)", Theme::textFaint);
    filter.setFont (Theme::font (14.0f));
    filter.onTextChange = [this] { refreshFilter(); };

    for (auto* b : { &learnButton, &clearButton, &guidedButton, &skipButton, &stopButton, &invertButton })
        addAndMakeVisible (*b);
    learnButton.setColour (juce::TextButton::buttonColourId, Theme::accent.withAlpha (0.85f));
    learnButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    invertButton.setClickingTogglesState (true);
    skipButton.setVisible (false);
    stopButton.setVisible (false);

    learnButton.onClick = [this] { learnSelected(); };
    clearButton.onClick = [this]
    {
        const int r = table.getSelectedRow();
        if (r < 0 || r >= (int) rows.size()) return;
        midi.removeBindingsFor (rows[(size_t) r]->id);
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
        const int r = table.getSelectedRow();
        if (r < 0 || r >= (int) rows.size()) return;
        auto all = midi.getBindings();
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].actionId == rows[(size_t) r]->id)
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
        const int r = table.getSelectedRow();
        if (r < 0 || r >= (int) rows.size()) return;
        auto all = midi.getBindings();
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].actionId == rows[(size_t) r]->id)
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
        const int r = table.getSelectedRow();
        if (r < 0 || r >= (int) rows.size()) return;
        auto all = midi.getBindings();
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].actionId == rows[(size_t) r]->id)
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

void MappingView::refreshFilter()
{
    rows.clear();
    const auto q = filter.getText().trim().toLowerCase();
    for (auto& a : allActions())
        if (q.isEmpty() || (a.group + " " + a.label).toLowerCase().contains (q))
            rows.push_back (&a);
    table.updateContent();
    table.repaint();
}

void MappingView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.0f, true));
    g.drawText ("MIDI MONITOR - move anything on the XDJ-XZ", monitor.getBounds().withY (monitor.getY() - 22).withHeight (20),
                juce::Justification::centredLeft);
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
    g.setColour (row % 2 ? Theme::panel : Theme::panel.brighter (0.025f));
    g.fillRect (0, 0, w, h);
    if (row < (int) rows.size() && rows[(size_t) row]->id == midi.getLearningAction())
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
    const auto* a = rows[(size_t) row];
    juce::String s;
    g.setFont (Theme::font (13.5f));
    g.setColour (Theme::text);
    if (col == 1) { s = a->group; g.setColour (Theme::textDim); }
    else if (col == 2) s = a->label;
    else
    {
        juce::StringArray parts;
        for (auto& b : midi.getBindingsFor (a->id))
            parts.add ((b.device.isNotEmpty() ? b.device + ": " : juce::String()) + b.describe());
        s = parts.joinIntoString ("   |   ");
        g.setColour (s.isEmpty() ? Theme::textFaint : Theme::good);
        if (s.isEmpty()) s = "-";
    }
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
    const int r = table.getSelectedRow();
    if (r < 0 || r >= (int) rows.size()) return;
    auto bs = midi.getBindingsFor (rows[(size_t) r]->id);
    if (! bs.empty())
    {
        modeBox.setSelectedId ((int) bs.front().mode + 1, juce::dontSendNotification);
        invertButton.setToggleState (bs.front().invert, juce::dontSendNotification);
        sensitivity.setValue (bs.front().sensitivity, juce::dontSendNotification);
    }
    const bool encoder = rows[(size_t) r]->kind == ActionKind::encoder;
    sensitivity.setEnabled (encoder);
    sensLabel.setEnabled (encoder);
}

void MappingView::learnSelected()
{
    const int r = table.getSelectedRow();
    if (r < 0 || r >= (int) rows.size()) return;
    const auto* a = rows[(size_t) r];
    midi.startLearn (a->id);
    banner.setText ("Waiting: move or press the control for  " + a->group + "  >  " + a->label, juce::dontSendNotification);
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

    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i]->id == id)
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
