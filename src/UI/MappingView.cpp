#include "MappingView.h"
#include "Theme.h"

namespace orbit
{

struct MappingView::LedPanel : juce::Component
{
    explicit LedPanel (MappingView& v) : view (v)
    {
        for (int r = 0; r < 4; ++r)
        {
            auto& row = rows[(size_t) r];
            addAndMakeVisible (row.slider);
            row.slider.setRange (0, 127, 1);
            row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
            row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
            row.slider.onValueChange = [this, r] { changed (r); };
            for (int k = 0; k < 2; ++k)
            {
                auto& b = k == 0 ? row.down : row.up;
                b.setButtonText (k == 0 ? "-1" : "+1");
                b.setTooltip (k == 0 ? "One number down" : "One number up");
                b.onClick = [this, r, k] { auto& s = rows[(size_t) r].slider; s.setValue (s.getValue() + (k == 0 ? -1 : 1), juce::sendNotificationSync); };
                addAndMakeVisible (b);
            }
            // both colour banks on every row: bright (1-63) and dim (64-127) round the XDJ-XZ's colour wheel, white, and MAX / OFF
            for (int bank = 0; bank < 2; ++bank)
                for (int i = 0; i < 10; ++i)
                {
                    auto* sw = new juce::TextButton();
                    row.swatches.add (sw);
                    addAndMakeVisible (sw);
                    const bool dim = bank == 1;
                    sw->getProperties().set ("psyched.swatch", true);
                    if (i < 8)
                    {
                        const auto col = rekordboxTrackColour (i + 1);
                        sw->setColour (juce::TextButton::buttonColourId, dim ? col.darker (0.6f) : col);
                        sw->setTooltip (juce::String (MidiManager::padColourName (i)) + (dim ? " (dim)" : " (bright)"));
                        sw->onClick = [this, r, col, dim] { rows[(size_t) r].slider.setValue (MidiManager::padWheelNumber (col, dim), juce::sendNotificationSync); };
                    }
                    else if (i == 8)
                    {
                        sw->setColour (juce::TextButton::buttonColourId, dim ? juce::Colour (0xff6a6a6a) : juce::Colours::white);
                        sw->setButtonText ("W");
                        sw->setTooltip (juce::String ("White") + (dim ? " (dim)" : " (bright)")
                                        + ". White isn't on the colour wheel: find it with the -1 / +1 arrows, then right-click here to keep that number");
                        sw->onClick = [this, r, dim, sw]
                        {
                            if (juce::ModifierKeys::currentModifiers.isPopupMenu()) return;
                            const int n = view.midi.getWhitePad (dim);
                            if (n > 0) rows[(size_t) r].slider.setValue (n, juce::sendNotificationSync);
                            else view.banner.setText ("White isn't set yet: find it with the -1 / +1 arrows, then right-click W to keep that number", juce::dontSendNotification);
                            juce::ignoreUnused (sw);
                        };
                        sw->addMouseListener (this, false);
                        sw->getProperties().set ("whiteRow", r);
                        sw->getProperties().set ("whiteDim", dim);
                    }
                    else
                    {
                        sw->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff202020));
                        sw->setButtonText (dim ? "OFF" : "MAX");
                        sw->setTooltip (dim ? "Off (0)" : "Full (127)");
                        sw->onClick = [this, r, dim] { rows[(size_t) r].slider.setValue (dim ? 0 : 127, juce::sendNotificationSync); };
                    }
                }
        }
        useShift.setButtonText ("Light the SHIFT layer its own way");
        useShift.setTooltip ("Off: holding SHIFT leaves the pad as it is (hot cue pads) / the unit's own SHIFT light");
        useShift.onClick = [this] { apply(); };
        addAndMakeVisible (useShift);
        setAction (nullptr);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* c = e.eventComponent;
        if (c == nullptr || ! c->getProperties().contains ("whiteRow") || ! e.mods.isPopupMenu()) return;
        const int r = c->getProperties()["whiteRow"];
        const bool dim = c->getProperties()["whiteDim"];
        const int n = (int) rows[(size_t) r].slider.getValue();
        view.midi.setWhitePad (dim, n);
        view.save();
        view.banner.setText (juce::String (dim ? "Dim" : "Bright") + " white is now " + juce::String (n), juce::dontSendNotification);
    }

    void setAction (const ActionDef* a)
    {
        actionId = a != nullptr ? a->id : juce::String();
        auto bs = a != nullptr ? view.midi.getBindingsFor (a->id) : std::vector<MidiBinding>();
        const bool ok = a != nullptr && ! bs.empty() && a->kind == ActionKind::button;
        hotCue = a != nullptr && a->id.fromFirstOccurrenceOf (".", false, false).startsWith ("hotcue.");
        title = a == nullptr ? juce::String ("LIGHTS") : "LIGHTS - " + a->group + "  >  " + a->label;
        note = a == nullptr ? juce::String ("Pick a function on the left")
             : a->kind != ActionKind::button ? juce::String ("Only buttons and pads have lights")
             : bs.empty() ? "Map " + a->label + " first, then set its lights here"
             : hotCue ? juce::String ("Hot cue pads: one colour with no hot cue, one with it set (or the cue's own colour - Settings > CONTROLLER)")
             : juce::String ("XDJ-XZ pads: 1-63 bright, 64-127 dim. The arrows step one number at a time");
        if (ok) binding = bs.front();
        const bool pad = ok && binding.type == MidiBinding::Type::note && (binding.number % 16) < 8;
        const char* names[4] = { hotCue ? "No hot cue" : "Lit", hotCue ? "Hot cue set" : "Unlit",
                                 hotCue ? "SHIFT: no cue" : "SHIFT lit", hotCue ? "SHIFT: cue set" : "SHIFT unlit" };
        // hot cue rows read top to bottom as empty then set (Unlit first)
        const int init[4] = { hotCue ? binding.ledOff : binding.ledOn, hotCue ? binding.ledOn : binding.ledOff,
                              hotCue ? juce::jmax (0, binding.shiftOff) : juce::jmax (0, binding.shiftOn),
                              hotCue ? juce::jmax (0, binding.shiftOn) : juce::jmax (0, binding.shiftOff) };
        loading = true;
        for (int r = 0; r < 4; ++r)
        {
            auto& row = rows[(size_t) r];
            row.label = names[r];
            const bool en = ok && (r < 2 || pad);
            row.slider.setValue (ok ? init[r] : 0, juce::dontSendNotification);
            for (juce::Component* c : { (juce::Component*) &row.slider, (juce::Component*) &row.down, (juce::Component*) &row.up }) c->setEnabled (en);
            for (auto* sw : row.swatches) sw->setEnabled (en);
        }
        useShift.setToggleState (ok && (binding.shiftOn >= 0 || binding.shiftOff >= 0), juce::dontSendNotification);
        useShift.setEnabled (pad);
        loading = false;
        repaint();
    }

    void changed (int r)
    {
        if (loading || actionId.isEmpty()) return;
        if (r >= 2 && ! useShift.getToggleState()) useShift.setToggleState (true, juce::dontSendNotification);
        apply();
        auto test = binding;
        if (r >= 2) test.number = binding.number + 8;   // the SHIFT layer: the pad's note + 8
        view.midi.sendTestLed (test, (int) rows[(size_t) r].slider.getValue());
    }

    void apply()
    {
        if (actionId.isEmpty()) return;
        const bool sh = useShift.getToggleState();
        const int v0 = (int) rows[0].slider.getValue(), v1 = (int) rows[1].slider.getValue(),
                  v2 = (int) rows[2].slider.getValue(), v3 = (int) rows[3].slider.getValue();
        const int on = hotCue ? v1 : v0, off = hotCue ? v0 : v1, shOn = hotCue ? v3 : v2, shOff = hotCue ? v2 : v3;
        auto all = view.midi.getBindings();
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].actionId == actionId)
            {
                all[(size_t) i].ledOn = on;
                all[(size_t) i].ledOff = off;
                all[(size_t) i].shiftOn = sh ? shOn : -1;
                all[(size_t) i].shiftOff = sh ? shOff : -1;
                view.midi.updateBinding (i, all[(size_t) i]);
            }
        binding.ledOn = on;
        binding.ledOff = off;
        view.midi.clearLedCache();
        view.save();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (psyched::Colours::field);
        g.fillRect (getLocalBounds());
        g.setColour (psyched::Colours::edge2);
        g.drawRect (getLocalBounds());
        psyched::paintLabel (g, title, juce::Rectangle<float> (10.0f, 6.0f, (float) getWidth() - 20.0f, 18.0f));
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (11.5f));
        g.drawText (note, 10, 24, getWidth() - 20, 16, juce::Justification::centredLeft, true);
        g.setFont (Theme::font (12.5f, true));
        for (int r = 0; r < 4; ++r)
        {
            g.setColour (rows[(size_t) r].slider.isEnabled() ? Theme::text : Theme::textFaint);
            g.drawText (rows[(size_t) r].label, 10, rowTop (r), 96, 24, juce::Justification::centredLeft, true);
        }
    }

    int rowTop (int r) const { return 72 + r * rowH(); }
    int rowH() const { return juce::jlimit (62, 90, (getHeight() - 80) / 4); }

    void resized() override
    {
        useShift.setBounds (10, 44, juce::jmin (300, getWidth() - 20), 22);
        for (int r = 0; r < 4; ++r)
        {
            auto& row = rows[(size_t) r];
            const int y = rowTop (r);
            auto line = juce::Rectangle<int> (106, y, getWidth() - 116, 24);
            row.down.setBounds (line.removeFromLeft (34));
            row.up.setBounds (line.removeFromRight (34));
            row.slider.setBounds (line.reduced (4, 0));
            const int sw = juce::jmax (16, (getWidth() - 116 - 9 * 3) / 10);
            for (int k = 0; k < row.swatches.size(); ++k)
            {
                const int bank = k / 10, i = k % 10;
                row.swatches[k]->setBounds (106 + i * (sw + 3), y + 28 + bank * 18, sw, 16);
            }
        }
    }

    struct Row { juce::String label; juce::Slider slider; juce::TextButton down, up; juce::OwnedArray<juce::TextButton> swatches; };
    MappingView& view;
    juce::String actionId, title, note;
    MidiBinding binding;
    bool hotCue = false, loading = false;
    std::array<Row, 4> rows;
    juce::ToggleButton useShift;
};

MappingView::MappingView (MidiManager& m, std::function<void()> saveFn) : midi (m), save (std::move (saveFn))
{
    addAndMakeVisible (table);
    auto& hdr = table.getHeader();
    hdr.addColumn ("Function", 1, 380, 160, 600);
    hdr.addColumn ("Mapped to", 3, 300, 100, 800);
    hdr.addColumn ("Don't map", 4, 80, 80, 80);
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
    ledPanel = std::make_unique<LedPanel> (*this);
    addAndMakeVisible (*ledPanel);
    skipButton.getProperties().set ("psyched.solid", true);
    skipButton.setColour (juce::TextButton::buttonOnColourId, Theme::warn);
    setWantsKeyboardFocus (true);
    for (int d = 0; d < 2; ++d)
    {
        auto& cb = controllerBox[(size_t) d];
        addAndMakeVisible (cb);
        cb.setTooltip ("Which controller drives deck " + juce::String (d + 1));
        cb.onChange = [this, d]
        {
            const int id = controllerBox[(size_t) d].getSelectedId();
            const auto dev = id <= 1 ? juce::String() : controllerBox[(size_t) d].getItemText (id - 1);
            deckController[(size_t) d] = dev;
            if (onDeckController) onDeckController (d, dev);
        };
    }
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
        pausedGuided = -1;
        countdownUntil = 0.0;
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

    addAndMakeVisible (pressBox);
    pressBox.addItemList ({ "Any press", "Short press", "Long press" }, 1);
    pressBox.setTooltip ("One button, two functions: map both to it (LEARN + ADD), then make one Short and the other Long. "
                         "Long = held past the long-press time (Settings > CONTROLLER)");
    pressBox.onChange = [this]
    {
        auto* a = selectedAction();
        if (a == nullptr) return;
        auto all = midi.getBindings();
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].actionId == a->id)
            {
                all[(size_t) i].press = pressBox.getSelectedId() - 1;
                midi.updateBinding (i, all[(size_t) i]);
            }
        save();
        table.repaint();
    };

    addAndMakeVisible (sensitivity);
    sensitivity.setSliderStyle (juce::Slider::LinearHorizontal);
    sensitivity.setScrollWheelEnabled (false);
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
    banner.setFont (Theme::font (17.0f, true));
    banner.setColour (juce::Label::textColourId, Theme::accent);
    banner.setJustificationType (juce::Justification::centredLeft);
    banner.setMinimumHorizontalScale (1.0f);

    addAndMakeVisible (status);
    status.setFont (Theme::font (13.0f));
    status.setColour (juce::Label::textColourId, Theme::textDim);

    midi.onLearnChoice = [this] (const std::vector<MidiManager::LearnChoice>& c, int best) { showLearnChoice (c, best); };
    midi.onLearned = [this] (const MidiBinding& b)
    {
        save();
        table.repaint();
        updateDetail();
        if (guidedIndex >= 0)
        {
            guidedMapped.push_back (b.actionId);
            // That control was already doing an earlier step's job: the earlier one lost its mapping, so ask for it again at the end
            for (auto& lost : midi.getLastReplaced())
                if (lost != b.actionId && std::find (guidedSteps.begin() + guidedIndex + 1, guidedSteps.end(), lost) == guidedSteps.end())
                {
                    guidedSteps.push_back (lost);
                    guidedMapped.erase (std::remove (guidedMapped.begin(), guidedMapped.end(), lost), guidedMapped.end());
                }
            advanceGuided (false);
        }
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
    ledPanel.reset();
    midi.onLearned = nullptr;
    midi.onLearnChoice = nullptr;
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

void MappingView::cellClicked (int row, int col, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) rows.size() && rows[(size_t) row].action != nullptr && col == 4)
    {
        const auto id = rows[(size_t) row].action->id;
        midi.setNoMap (id, midi.getNoMap().count (id) == 0);
        save();
        table.repaintRow (row);
        return;
    }
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
    // the learn / guided setup display: what it's waiting for and the countdown, big enough to read from the decks
    const bool active = guidedIndex >= 0 || midi.getLearningAction().isNotEmpty();
    g.setColour (psyched::Colours::field);
    g.fillRect (guidedBox);
    g.setColour (active ? Theme::accent.withAlpha (0.8f) : psyched::Colours::edge2);
    g.drawRect (guidedBox, active ? 2 : 1);
    if (countdownUntil > 0.0 && guidedIndex >= 0)
    {
        const double left = juce::jmax (0.0, countdownUntil - juce::Time::getMillisecondCounterHiRes());
        g.setColour (Theme::warn);
        g.setFont (Theme::mono (40.0f, true));
        g.drawText (juce::String ((int) std::ceil (left / 1000.0)), guidedBox.withTrimmedRight (12).removeFromRight (50), juce::Justification::centredRight);
    }
}

void MappingView::resized()
{
    auto r = getLocalBounds().reduced (12);

    // right: status and the MIDI monitor
    auto right = r.removeFromRight (juce::jmin (340, r.getWidth() / 4));
    r.removeFromRight (12);
    status.setBounds (right.removeFromTop (24));
    right.removeFromTop (26);
    monitor.setBounds (right);

    // left half: the search and the list of functions; middle: everything you map with
    auto mid = r.removeFromRight (r.getWidth() / 2);
    r.removeFromRight (12);
    filter.setBounds (r.removeFromTop (32));
    r.removeFromTop (6);
    table.setBounds (r);

    auto top = mid.removeFromTop (32);
    guidedButton.setBounds (top.removeFromRight (130));
    top.removeFromRight (8);
    for (int d = 1; d >= 0; --d)
    {
        controllerBox[(size_t) d].setBounds (top.removeFromRight ((top.getWidth() - 6) / (d + 1)).reduced (0, 2));
        if (d > 0) top.removeFromRight (6);
    }
    mid.removeFromTop (8);

    guidedBox = mid.removeFromTop (72);
    banner.setBounds (guidedBox.reduced (12, 4).withTrimmedRight (60));
    mid.removeFromTop (6);
    auto skipRow = mid.removeFromTop (30);
    stopButton.setBounds (skipRow.removeFromRight (90).reduced (0, 1));
    skipRow.removeFromRight (6);
    skipButton.setBounds (skipRow.removeFromRight (150).reduced (0, 1));
    mid.removeFromTop (8);

    auto buttons = mid.removeFromTop (32);
    const int bw = (buttons.getWidth() - 18) / 4;
    learnButton.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (6);
    learnAddButton.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (6);
    clearButton.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (6);
    invertButton.setBounds (buttons);
    mid.removeFromTop (6);

    auto boxes = mid.removeFromTop (28);
    modeBox.setBounds (boxes.removeFromLeft (boxes.getWidth() * 3 / 5));
    boxes.removeFromLeft (6);
    pressBox.setBounds (boxes);
    mid.removeFromTop (6);
    auto sens = mid.removeFromTop (26);
    sensLabel.setBounds (sens.removeFromLeft (80));
    sensitivity.setBounds (sens);
    mid.removeFromTop (10);

    ledPanel->setBounds (mid);
}

int MappingView::getNumRows() { return (int) rows.size(); }

void MappingView::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    if (row >= (int) rows.size()) return;
    const auto& rw = rows[(size_t) row];
    g.setColour (rw.action == nullptr ? (rw.level == 0 ? Theme::panelHi : Theme::panel.brighter (0.04f))
                                      : (row % 2 ? Theme::panel : Theme::panel.brighter (0.025f)));
    g.fillRect (0, 0, w, h);
    if (rw.action != nullptr && guidedIndex >= 0 && guidedIndex < (int) guidedSteps.size() && guidedSteps[(size_t) guidedIndex] == rw.action->id)
    {
        g.setColour (Theme::warn.withAlpha (0.18f));   // guided setup: the step it's on
        g.fillRect (0, 0, w, h);
    }
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
        else if (col == 3)
        {
            g.setColour (mapped > 0 ? Theme::good.withAlpha (0.8f) : Theme::textFaint);
            g.setFont (Theme::font (12.5f));
            g.drawText (juce::String (mapped) + " of " + juce::String (total) + " mapped", juce::Rectangle<int> (6, 0, w - 12, h),
                        juce::Justification::centredLeft, true);
        }
        return;
    }

    const auto* a = rw.action;
    const bool skipIt = midi.getNoMap().count (a->id) > 0;
    if (col == 4)
    {
        auto box = juce::Rectangle<float> (0, 0, 14, 14).withCentre ({ (float) w * 0.5f, (float) h * 0.5f });
        g.setColour (skipIt ? Theme::warn : Theme::textFaint);
        g.drawRect (box, 1.0f);
        if (skipIt)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 3, box.getCentreY());
            tick.lineTo (box.getCentreX() - 1, box.getBottom() - 3);
            tick.lineTo (box.getRight() - 3, box.getY() + 3);
            g.strokePath (tick, juce::PathStrokeType (2.0f));
        }
        return;
    }
    juce::String s;
    g.setFont (Theme::font (13.5f));
    g.setColour (skipIt ? Theme::textFaint : Theme::text);
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
    if (ledPanel != nullptr) ledPanel->setAction (a);
    if (a == nullptr) return;
    auto bs = midi.getBindingsFor (a->id);
    if (! bs.empty())
    {
        modeBox.setSelectedId ((int) bs.front().mode + 1, juce::dontSendNotification);
        invertButton.setToggleState (bs.front().invert, juce::dontSendNotification);
        sensitivity.setValue (bs.front().sensitivity, juce::dontSendNotification);
        pressBox.setSelectedId (bs.front().press + 1, juce::dontSendNotification);
    }
    pressBox.setEnabled (a->kind == ActionKind::button && ! bs.empty());
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

void MappingView::visibilityChanged()
{
    if (! isShowing())
    {
        if (guidedIndex >= 0) pausedGuided = guidedIndex;
        guidedIndex = -1;
        countdownUntil = 0.0;
        midi.cancelLearn();
        return;
    }
    if (pausedGuided >= 0 && pausedGuided < (int) guidedSteps.size())
    {
        // back on the page: carry on from the step we were on
        guidedIndex = pausedGuided;
        pausedGuided = -1;
        skipButton.setVisible (true);
        stopButton.setVisible (true);
        listenForStep();
    }
}

juce::String MappingView::stepHint (const juce::String& id)
{
    const auto n = id.fromFirstOccurrenceOf (".", false, false);
    if (n == "jog.touch") return "touch the top of the platter (press it, don't turn)";
    if (n == "jog.turn")  return "hold the top of the platter and turn it";
    if (n == "jog.side")  return "turn the platter by its side, without touching the top";
    if (n == "jog.vinyl") return "press the VINYL / CDJ button";
    if (n == "pitch")     return "move the tempo slider";
    if (id == "browse.scroll") return "turn the browse knob";
    return "press / move it now";
}

void MappingView::startGuided()
{
    pausedGuided = -1;
    const auto skip = midi.getNoMap();
    bool anyMapped = false;
    // Every control the XDJ-XZ has for a deck (and the browser), in the same order as the list on screen
    static const juce::StringArray wanted { "play", "cue", "shift", "load", "jog.touch", "jog.turn", "jog.side", "jog.vinyl", "slip",
                                            "pitch", "pitchrange", "tempo.reset", "keylock", "sync", "master", "quantize",
                                            "hotcue.1", "hotcue.2", "hotcue.3", "hotcue.4", "hotcue.5", "hotcue.6", "hotcue.7", "hotcue.8",
                                            "callprev", "callnext", "loop.in", "loop.out", "loop.reloop", "beatloop.4",
                                            "beatjump.back", "beatjump.fwd", "stemctl.A.volume", "stemctl.B.volume", "stemctl.C.volume",
                                            "stemctl.D.volume", "padmode.1", "padmode.2", "padmode.3", "padmode.4" };
    static const juce::StringArray browseWanted { "browse.scroll", "browse.enter", "browse.back", "browse.tree", "browse.load1",
                                                  "browse.load2", "browse.tag", "view.browser" };
    guidedSteps.clear();
    // in the same order as the list on screen (deck 1 from top to bottom, then deck 2, then the browser)
    for (auto& a : allActions())
    {
        const bool deckStep = a.deck >= 0 && a.deck < 2 && wanted.contains (a.id.fromFirstOccurrenceOf (".", false, false));
        const bool browseStep = browseWanted.contains (a.id);
        if (! (deckStep || browseStep) || skip.count (a.id)) continue;
        guidedSteps.push_back (a.id);
        anyMapped = anyMapped || ! midi.getBindingsFor (a.id).empty();
    }
    if (! anyMapped) { runGuided (false); return; }

    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Guided setup").withIconType (juce::MessageBoxIconType::QuestionIcon)
                                    .withMessage ("Some controls are already mapped.\n\nContinue: only the ones not mapped yet.\nStart fresh: clear every mapping and go through them all.")
                                    .withButton ("Continue").withButton ("Start fresh").withButton ("Cancel"),
                                  [this] (int r)
    {
        if (r == 1) { runGuided (true); return; }
        if (r != 2) return;
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Start fresh").withIconType (juce::MessageBoxIconType::WarningIcon)
                                        .withMessage ("Clear ALL mappings (every function, not just the guided ones) and start again?")
                                        .withButton ("Clear and start").withButton ("Cancel"),
                                      [this] (int r2)
        {
            if (r2 != 1) return;
            midi.setBindings ({});
            save();
            table.repaint();
            runGuided (false);
        });
    });
}

void MappingView::runGuided (bool onlyUnmapped)
{
    if (onlyUnmapped)
        guidedSteps.erase (std::remove_if (guidedSteps.begin(), guidedSteps.end(), [this] (auto& id) { return ! midi.getBindingsFor (id).empty(); }),
                           guidedSteps.end());
    guidedMapped.clear();
    guidedSkipped.clear();
    guidedIndex = -1;
    countdownUntil = 0.0;
    skipButton.setVisible (true);
    stopButton.setVisible (true);
    grabKeyboardFocus();
    advanceGuided (false);
}

void MappingView::advanceGuided (bool skip)
{
    if (skip && guidedIndex >= 0 && guidedIndex < (int) guidedSteps.size())
        guidedSkipped.push_back (guidedSteps[(size_t) guidedIndex]);
    midi.cancelLearn();
    ++guidedIndex;
    if (guidedIndex >= (int) guidedSteps.size())
    {
        guidedIndex = -1;
        countdownUntil = 0.0;
        skipButton.setVisible (false);
        stopButton.setVisible (false);
        juce::String msg = "Guided setup finished: " + juce::String ((int) guidedMapped.size()) + " mapped";
        if (! guidedSkipped.empty())
        {
            juce::StringArray names;
            for (auto& id : guidedSkipped)
                if (auto* a = findAction (id)) names.add (a->group.upToFirstOccurrenceOf (" - ", false, false) + " " + a->label);
            msg << ", skipped: " << names.joinIntoString (", ");
        }
        banner.setText (msg, juce::dontSendNotification);
        banner.setTooltip (msg);
        table.repaint();
        return;
    }

    const auto id = guidedSteps[(size_t) guidedIndex];
    revealAction (id);
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i].action != nullptr && rows[(size_t) i].action->id == id)
        {
            table.selectRow (i);
            table.scrollToEnsureRowIsOnscreen (i);
        }

    // A short pause before listening, so a jog / fader still moving doesn't land on the next step (Enter = start now)
    const bool first = guidedMapped.empty() && guidedSkipped.empty();
    countdownUntil = (skip || first) ? 0.0 : juce::Time::getMillisecondCounterHiRes() + 3000.0;
    if (countdownUntil <= 0.0) listenForStep();
    else timerCallback();
}

void MappingView::listenForStep()
{
    countdownUntil = 0.0;
    if (guidedIndex < 0 || guidedIndex >= (int) guidedSteps.size()) return;
    const auto id = guidedSteps[(size_t) guidedIndex];
    const auto* a = findAction (id);
    midi.startLearn (id);
    banner.setText ("[" + juce::String (guidedIndex + 1) + "/" + juce::String ((int) guidedSteps.size()) + "]  "
                    + (a != nullptr ? a->group + "  >  " + a->label : id) + "  -  " + stepHint (id),
                    juce::dontSendNotification);
    table.repaint();
}

bool MappingView::keyPressed (const juce::KeyPress& k)
{
    if (guidedIndex < 0) return false;
    if (k == juce::KeyPress::returnKey && countdownUntil > 0.0) { listenForStep(); return true; }
    if (k == juce::KeyPress::tabKey) { advanceGuided (true); return true; }
    if (k == juce::KeyPress::escapeKey) { stopButton.onClick(); return true; }
    return false;
}

void MappingView::showLearnChoice (const std::vector<MidiManager::LearnChoice>& choices, int best)
{
    const auto* a = findAction (midi.getLearningAction());
    juce::PopupMenu m;
    m.addSectionHeader ("Which control is " + (a != nullptr ? a->label : juce::String ("it")) + "?");
    for (int i = 0; i < (int) choices.size(); ++i)
    {
        const auto& c = choices[(size_t) i];
        m.addItem (i + 1, c.binding.describe() + "  -  " + c.what + (i == best ? "   (best guess)" : ""), true, i == best);
    }
    m.addSeparator();
    m.addItem (1000, "Listen again");
    m.addItem (1001, "Cancel");
    banner.setText ("Heard " + juce::String ((int) choices.size()) + " controls - pick the one to use", juce::dontSendNotification);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&banner).withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::downwards)
                       .withInitiallySelectedItem (best + 1),
                     [this] (int r)
    {
        if (r >= 1 && r < 1000) { midi.chooseLearned (r - 1); return; }
        const auto id = midi.getLearningAction();
        midi.cancelLearn();
        if (r == 1000 && id.isNotEmpty())
        {
            if (guidedIndex >= 0) listenForStep();
            else { midi.startLearn (id); banner.setText ("Waiting: move or press the control again", juce::dontSendNotification); }
        }
        else if (guidedIndex < 0) banner.setText ({}, juce::dontSendNotification);
        else listenForStep();
    });
}

void MappingView::setDeckControllers (const juce::String& d1, const juce::String& d2)
{
    deckController = { d1, d2 };
    lastDevices.clear();
    refreshControllerBoxes();
}

void MappingView::refreshControllerBoxes()
{
    auto names = midi.getOpenInputNames();
    for (auto& dc : deckController) if (dc.isNotEmpty()) names.addIfNotAlreadyThere (dc);
    if (names == lastDevices && controllerBox[0].getNumItems() > 0) return;
    lastDevices = names;
    for (int d = 0; d < 2; ++d)
    {
        auto& cb = controllerBox[(size_t) d];
        cb.clear (juce::dontSendNotification);
        cb.addItem ("Deck " + juce::String (d + 1) + ": any controller", 1);
        for (int i = 0; i < names.size(); ++i) cb.addItem (names[i], i + 2);
        const int idx = names.indexOf (deckController[(size_t) d]);
        cb.setSelectedId (deckController[(size_t) d].isEmpty() || idx < 0 ? 1 : idx + 2, juce::dontSendNotification);
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

    refreshControllerBoxes();
    repaint (guidedBox.expanded (2));
    if (guidedIndex >= 0 && countdownUntil > 0.0)
    {
        const double left = countdownUntil - juce::Time::getMillisecondCounterHiRes();
        if (left <= 0.0) listenForStep();
        else
        {
            const auto id = guidedSteps[(size_t) guidedIndex];
            const auto* a = findAction (id);
            banner.setText ("NEXT:  " + (a != nullptr ? a->group + "  >  " + a->label : id) + "\nENTER = now   TAB = skip   ESC = stop",
                            juce::dontSendNotification);
        }
    }
    const auto seen = midi.getSeenControls();
    status.setText (midi.getControllerSummary() + "    |    " + juce::String ((int) seen.size()) + " controls seen    |    "
                    + juce::String ((int) midi.getBindings().size()) + " mappings", juce::dontSendNotification);

    if (midi.getLearningAction().isNotEmpty())
        table.repaint();
}

} // namespace orbit
