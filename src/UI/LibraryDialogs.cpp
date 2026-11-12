#include "LibraryDialogs.h"
#include "Theme.h"

namespace orbit
{

//==============================================================================
void OverlayCard::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.7f));
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 40, { 0, 12 }).drawForRectangle (g, card);
    psyched::paintPanel (g, card.toFloat(), accentColour);
    psyched::paintTitle (g, title, card.reduced (24, 18).removeFromTop (28).toFloat(), juce::Justification::centredLeft, Theme::text, 14.0f);
}

void OverlayCard::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (juce::jmin (cardWidth, getWidth() - 40), juce::jmin (cardHeight, getHeight() - 40));
    layoutCard (card);
}

//==============================================================================
ExportView::ExportView (CentrifugeLibrary& l, AudioFileLoader& fl, StemManager& sm, Sources s)
    : library (l), loader (fl), stems (sm), sources (std::move (s))
{
    cardHeight = 470;
    accentColour = Theme::output;
    for (auto* c : std::initializer_list<juce::Component*> { &what, &version, &path, &choose, &xzStems, &go, &cancel })
        addAndMakeVisible (c);
    version.addItem ("rekordbox copy  (as imported from rekordbox, with your R edits)", 1);
    version.addItem ("Centrifuge version, cut down to what rekordbox can hold", 2);
    version.setSelectedId (2, juce::dontSendNotification);
    path.setFont (Theme::font (13.5f));
    xzStems.setToggleState (true, juce::dontSendNotification);
    xzStems.setTooltip ("Writes Inst and Vocal stems (Drums + Bass is what's left) for tracks you've already separated. "
                        "WAV/FLAC at 44.1 kHz, up to 6:20 long.");
    go.getProperties().set ("psyched.solid", true);
    go.setColour (juce::TextButton::buttonOnColourId, Theme::output);

    choose.onClick = [this]
    {
        if (usb)
            chooser = std::make_unique<juce::FileChooser> ("Choose the USB stick (its top folder)", juce::File (path.getText()));
        else
            chooser = std::make_unique<juce::FileChooser> ("Save the rekordbox XML file", juce::File (path.getText()), "*.xml");
        const int flags = usb ? juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories
                              : juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;
        chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File()) path.setText (fc.getResult().getFullPathName(), false);
        });
    };
    go.onClick = [this] { if (! running) start(); };
    cancel.onClick = [this] { if (running) { stop = true; cancel.setButtonText ("STOPPING..."); } else close(); };
}

ExportView::~ExportView()
{
    stop = true;
    pool.removeAllJobs (true, 30000);
}

void ExportView::open (bool toUsb)
{
    usb = toUsb;
    title = usb ? "Export to a USB stick (rekordbox-compatible)" : "Export a rekordbox XML file";
    done = failed = false;
    {
        const juce::ScopedLock sl (statusLock);
        status = {};
    }
    what.clear (juce::dontSendNotification);
    const auto listName = sources.currentListName ? sources.currentListName() : juce::String();
    what.addItem ("Current list: " + (listName.isNotEmpty() ? listName : juce::String ("-")) + "  ("
                  + juce::String ((int) sources.currentList().size()) + " tracks)", 1);
    what.addItem ("Selected tracks  (" + juce::String ((int) sources.selected().size()) + ")", 2);
    what.addItem ("Whole Centrifuge library with its playlists  (" + juce::String (library.size()) + " tracks)", 3);
    what.setSelectedId (1, juce::dontSendNotification);

    if (usb)
    {
        auto stick = sources.defaultStick ? sources.defaultStick() : juce::File();
        path.setText (stick.getFullPathName(), false);
        path.setTextToShowWhenEmpty ("the USB stick's drive, e.g. E:\\", Theme::textFaint);
    }
    else
    {
        path.setText (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Centrifuge for rekordbox.xml").getFullPathName(), false);
    }
    go.setEnabled (true);
    cancel.setButtonText ("CLOSE");
    setVisible (true);
    toFront (true);
    resized();
    repaint();
}

void ExportView::layoutCard (juce::Rectangle<int> c)
{
    auto r = c.reduced (24, 18);
    r.removeFromTop (44);
    auto row = [&] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (10); return x; };
    row (18);
    what.setBounds (row (30));
    row (18);
    version.setBounds (row (30));
    row (18);
    auto p = row (30);
    choose.setBounds (p.removeFromRight (110));
    p.removeFromRight (8);
    path.setBounds (p);
    xzStems.setBounds (row (24));
    xzStems.setVisible (usb);
    auto buttons = c.reduced (24, 18).removeFromBottom (36);
    cancel.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (10);
    go.setBounds (buttons.removeFromRight (140));
}

void ExportView::paint (juce::Graphics& g)
{
    OverlayCard::paint (g);
    auto r = card.reduced (24, 18);
    r.removeFromTop (44);
    g.setFont (psyched::Fonts::label (10.5f));
    g.setColour (Theme::textDim);
    g.drawText ("WHAT", r.removeFromTop (18), juce::Justification::centredLeft);
    r.removeFromTop (50);
    g.drawText ("WHICH VERSION", r.removeFromTop (18), juce::Justification::centredLeft);
    r.removeFromTop (50);
    g.drawText (usb ? "USB STICK" : "FILE", r.removeFromTop (18), juce::Justification::centredLeft);
    r.removeFromTop (50);
    if (usb) r.removeFromTop (34);

    g.setFont (Theme::font (12.5f));
    g.setColour (Theme::textFaint);
    const juce::String note = usb
        ? "Copies the music to the stick and writes rekordbox's database and analysis files, so the XDJ-XZ reads it like a rekordbox "
          "export. The stick's previous rekordbox database is kept as a backup next to the new one. Your extras (skips, extra "
          "markers) are kept on the stick for Centrifuge. Test with a spare stick first."
        : "In rekordbox's Preferences, set this file as the \"rekordbox xml\" library. It then shows up in rekordbox's "
          "\"rekordbox xml\" tree, where you can import tracks and playlists into your collection.";
    g.drawFittedText (note, r.removeFromTop (54), juce::Justification::topLeft, 4);

    juce::String s;
    {
        const juce::ScopedLock sl (statusLock);
        s = status;
        if (running && current.isNotEmpty()) s = juce::String (juce::roundToInt (progress.load() * 100.0f)) + "%   " + current;
    }
    if (s.isNotEmpty())
    {
        auto bar = card.reduced (24, 18).removeFromBottom (36).withTrimmedRight (270);
        g.setColour (failed ? Theme::bad : done ? Theme::good : Theme::text);
        g.setFont (Theme::font (13.0f, true));
        g.drawFittedText (s, bar, juce::Justification::centredLeft, 2);
        if (running)
        {
            g.setColour (Theme::accent);
            g.fillRect (bar.removeFromBottom (3).withWidth ((int) (bar.getWidth() * progress.load())));
        }
    }
}

std::vector<ExportItem> ExportView::collect (ExportList& playlists) const
{
    const bool useCf = version.getSelectedId() == 2;
    std::vector<ExportItem> items;
    std::map<juce::int64, int> itemOfTrack;
    playlists = {};
    playlists.folder = true;

    auto addTrack = [&] (const TrackInfo& info) -> int
    {
        std::optional<CentrifugeLibrary::Track> t;
        if (info.libraryId > 0) t = library.get (info.libraryId);
        if (! t) t = library.find (info.libraryKey.isNotEmpty() ? info.libraryKey : CueStore::keyFor (info, info.durationSec));
        if (t)
        {
            if (auto f = itemOfTrack.find (t->id); f != itemOfTrack.end()) return f->second;
            itemOfTrack[t->id] = (int) items.size();
            items.push_back ({ t->info, (useCf ? t->cf : t->rb).limitedForRekordbox(), t->cf, t->key });
        }
        else
        {
            // Not in the library yet: rekordbox's own data
            auto v = CentrifugeLibrary::readRekordboxVersion (info);
            items.push_back ({ info, v.limitedForRekordbox(), v, CueStore::keyFor (info, info.durationSec) });
        }
        return (int) items.size() - 1;
    };

    const int choice = what.getSelectedId();
    if (choice == 3)
    {
        for (auto& t : library.allTracks())
        {
            itemOfTrack[t.id] = (int) items.size();
            items.push_back ({ t.info, (useCf ? t.cf : t.rb).limitedForRekordbox(), t.cf, t.key });
        }
        const auto pls = library.allPlaylists();
        std::function<void (ExportList&, juce::int64)> build = [&] (ExportList& node, juce::int64 parent)
        {
            std::vector<const CentrifugeLibrary::Playlist*> kids;
            for (auto& p : pls) if (p.parent == parent && p.id != parent) kids.push_back (&p);
            std::sort (kids.begin(), kids.end(), [] (auto* a, auto* b) { return a->seq < b->seq; });
            for (auto* k : kids)
            {
                ExportList l;
                l.name = k->name;
                l.folder = k->folder;
                if (k->folder) build (l, k->id);
                else for (auto id : k->tracks) if (auto f = itemOfTrack.find (id); f != itemOfTrack.end()) l.items.push_back (f->second);
                node.children.push_back (std::move (l));
            }
        };
        build (playlists, 0);
        ExportList all;
        all.name = "All tracks (Centrifuge)";
        for (int i = 0; i < (int) items.size(); ++i) all.items.push_back (i);
        playlists.children.push_back (all);
    }
    else
    {
        const auto tracks = choice == 2 ? sources.selected() : sources.currentList();
        ExportList l;
        l.name = choice == 2 ? juce::String ("Centrifuge export") : (sources.currentListName ? sources.currentListName() : juce::String ("Centrifuge export"));
        for (auto& t : tracks) l.items.push_back (addTrack (t));
        playlists.children.push_back (l);
    }
    return items;
}

void ExportView::start()
{
    const juce::File target (path.getText().trim());
    if (path.getText().trim().isEmpty() || (usb && ! target.isDirectory()))
    {
        const juce::ScopedLock sl (statusLock);
        status = usb ? "Choose the USB stick first" : "Choose where to save the file";
        failed = true;
        repaint();
        return;
    }

    auto playlists = std::make_shared<ExportList>();
    auto items = std::make_shared<std::vector<ExportItem>> (collect (*playlists));
    if (items->empty())
    {
        const juce::ScopedLock sl (statusLock);
        status = "Nothing to export";
        failed = true;
        repaint();
        return;
    }

    running = true;
    stop = false;
    done = failed = false;
    progress = 0.0f;
    go.setEnabled (false);
    cancel.setButtonText ("STOP");
    startTimerHz (10);

    const bool toUsb = usb;
    const bool withXz = usb && xzStems.getToggleState();
    pool.addJob ([this, target, items, playlists, toUsb, withXz]
    {
        juce::String err, summary;
        bool ok;
        if (toUsb)
        {
            ExportProgress p;
            p.report = [this] (float f, const juce::String& what)
            {
                progress = f;
                const juce::ScopedLock sl (statusLock);
                current = what;
                return ! stop.load();
            };
            XzStemExport xz;
            xz.loadStems = [this] (const TrackInfo& info, const TrackAudio& audio) { return stems.loadCachedStems (info, audio); };
            ok = exportToUsb (target, *items, *playlists, loader, p, err, summary, withXz ? &xz : nullptr);
        }
        else
        {
            ok = writeRekordboxXml (target, *items, *playlists, err);
            summary = juce::String ((int) items->size()) + " tracks written to " + target.getFileName();
        }
        const juce::ScopedLock sl (statusLock);
        status = ok ? "Done: " + summary : "Export failed: " + err;
        current = {};
        failed = ! ok;
        done = ok;
        running = false;
    });
}

void ExportView::timerCallback()
{
    repaint();
    if (! running)
    {
        stopTimer();
        go.setEnabled (true);
        cancel.setButtonText ("CLOSE");
    }
}

//==============================================================================
ConflictsView::ConflictsView (CentrifugeLibrary& l) : library (l)
{
    cardWidth = 760;
    cardHeight = 520;
    title = "rekordbox has changed tracks you've edited";
    for (auto* c : std::initializer_list<juce::Component*> { &list, &keepAll, &takeAll, &closeButton })
        addAndMakeVisible (c);
    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, Theme::panelHi);
    keepAll.onClick = [this] { for (int i = 0; i < (int) conflicts.size(); ++i) if (decided[(size_t) i] == 0) decide (i, false); };
    takeAll.onClick = [this] { for (int i = 0; i < (int) conflicts.size(); ++i) if (decided[(size_t) i] == 0) decide (i, true); };
    closeButton.onClick = [this]
    {
        // Anything left undecided keeps your version (it will be asked again after the next import).
        close();
    };
}

void ConflictsView::show (std::vector<CentrifugeLibrary::Conflict> c, const juce::String& importSummary)
{
    conflicts = std::move (c);
    decided.assign (conflicts.size(), 0);
    summary = importSummary;
    list.updateContent();
    setVisible (true);
    toFront (true);
    resized();
    repaint();
}

void ConflictsView::layoutCard (juce::Rectangle<int> c)
{
    auto r = c.reduced (24, 18);
    r.removeFromTop (44 + 46);
    auto buttons = r.removeFromBottom (36);
    r.removeFromBottom (10);
    list.setBounds (r);
    closeButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (10);
    takeAll.setBounds (buttons.removeFromRight (220));
    buttons.removeFromRight (10);
    keepAll.setBounds (buttons.removeFromRight (150));
}

void ConflictsView::paint (juce::Graphics& g)
{
    OverlayCard::paint (g);
    auto r = card.reduced (24, 18);
    r.removeFromTop (44);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawFittedText (summary + "\nFor each track: keep your version, or take rekordbox's new grid and cues (your skips are kept). "
                      "Click KEEP or TAKE on a row.", r.removeFromTop (40), juce::Justification::topLeft, 3);
}

void ConflictsView::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row >= (int) conflicts.size()) return;
    g.setColour (row % 2 ? Theme::field : Theme::panel);
    g.fillRect (0, 0, w, h);
    g.setColour (Theme::text);
    g.setFont (Theme::font (13.5f));
    g.drawText (conflicts[(size_t) row].title, 10, 0, w - 230, h, juce::Justification::centredLeft, true);

    const int d = decided[(size_t) row];
    auto chip = [&] (juce::Rectangle<int> b, const juce::String& t, bool lit, juce::Colour c)
    {
        psyched::paintChip (g, b.toFloat(), t, c, lit ? psyched::ChipState::on : psyched::ChipState::off);
    };
    chip ({ w - 220, 4, 100, h - 8 }, d == 1 ? "KEPT MINE" : "KEEP MINE", d == 1, Theme::good);
    chip ({ w - 112, 4, 100, h - 8 }, d == 2 ? "TOOK REKORDBOX" : "TAKE REKORDBOX", d == 2, Theme::accent);
}

void ConflictsView::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) conflicts.size() || decided[(size_t) row] != 0) return;
    const int w = list.getWidth() - list.getVerticalScrollBar().getWidth();
    if (e.x >= w - 220 && e.x < w - 120) decide (row, false);
    else if (e.x >= w - 112 && e.x < w - 12) decide (row, true);
}

void ConflictsView::decide (int row, bool takeRekordbox)
{
    library.resolve (conflicts[(size_t) row], takeRekordbox);
    decided[(size_t) row] = takeRekordbox ? 2 : 1;
    list.repaint();
}

} // namespace orbit
