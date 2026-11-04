#include "BrowserView.h"
#include "Theme.h"
#include "../Library/Anlz.h"
#include "../Audio/MusicalKey.h"
#include "CentrifugeRotor.h"

namespace orbit
{

class BrowserView::Item : public juce::TreeViewItem
{
public:
    Item (BrowserView& o, std::shared_ptr<const LibrarySource> s, const PlaylistNode* n, juce::String nm, juce::String k, bool all, bool sourceRoot)
        : owner (o), src (std::move (s)), node (n), name (std::move (nm)), key (std::move (k)), allTracks (all), isSourceRoot (sourceRoot) {}

    bool mightContainSubItems() override
    {
        if (isSourceRoot) return true;
        return node != nullptr && node->isFolder && ! node->children.empty();
    }
    bool canBeSelected() const override { return true; }
    juce::String getUniqueName() const override { return key; }
    int getItemHeight() const override { return 24; }

    bool isList() const { return allTracks || (node != nullptr && ! node->isFolder); }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (! isNowOpen || getNumSubItems() > 0) return;
        if (isSourceRoot)
        {
            addSubItem (new Item (owner, src, nullptr, "All Tracks", key + "/all", true, false));
            for (auto& c : src->playlists.children)
                addSubItem (new Item (owner, src, &c, c.name, key + "/" + c.id, false, false));
        }
        else if (node != nullptr)
        {
            for (auto& c : node->children)
                addSubItem (new Item (owner, src, &c, c.name, key + "/" + c.id, false, false));
        }
    }

    void itemSelectionChanged (bool isNow) override
    {
        if (isNow && isList())
            owner.showList (src, allTracks ? nullptr : node, isSourceRoot ? src->name : name, key);
    }

    void itemClicked (const juce::MouseEvent&) override
    {
        owner.treeFocused = true;
        if (! isList()) setOpen (! isOpen());
    }

    void paintItem (juce::Graphics& g, int w, int h) override
    {
        if (isSelected())
        {
            g.setColour (Theme::accent.withAlpha (owner.treeFocused ? 0.16f : 0.09f));
            g.fillRect (0, 0, w, h);
            g.setColour (Theme::accent);
            g.fillRect (0, 0, 2, h);
        }
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (4.0f, 0.0f);
        auto icon = r.removeFromLeft (16.0f).withSizeKeepingCentre (12.0f, 12.0f);
        if (isSourceRoot && src->kind == LibrarySource::Kind::centrifuge)
        {
            CentrifugeRotor::paintRotor (g, icon.expanded (3.0f), 0.4f, 0.0f, 1.0f, 0.0f, 0.0f);
        }
        else if (isSourceRoot)
        {
            g.setColour (src->kind == LibrarySource::Kind::usbExport ? Theme::warn : Theme::deckColour (0));
            g.fillRect (icon);
        }
        else if (node != nullptr && node->isFolder)
        {
            g.setColour (Theme::textDim);
            g.drawRect (icon, 1.2f);
        }
        else
        {
            g.setColour (allTracks ? Theme::accent : Theme::textDim);
            for (int i = 0; i < 3; ++i)
                g.fillRect (icon.getX(), icon.getY() + 1.0f + i * 4.0f, icon.getWidth(), 1.6f);
        }
        r.removeFromLeft (6.0f);
        g.setColour (isSourceRoot ? Theme::text : Theme::text.withAlpha (0.9f));
        g.setFont (Theme::font (13.5f, isSourceRoot));
        juce::String label = name;
        if (node != nullptr && node->isSmart) label += "  (smart)";
        if (isSourceRoot && src->error.isNotEmpty()) label += "  -  " + src->error;
        if (isSourceRoot && src->kind == LibrarySource::Kind::centrifuge)
            label += src->tracks.empty() ? juce::String ("  (empty - press IMPORT)")
                                         : (src->libraryView == 0 ? juce::String ("  -  rekordbox copy") : juce::String ("  -  Centrifuge"));
        g.drawText (label, r, juce::Justification::centredLeft, true);
    }

    BrowserView& owner;
    std::shared_ptr<const LibrarySource> src;
    const PlaylistNode* node;
    juce::String name, key;
    bool allTracks, isSourceRoot;
};

//==============================================================================
BrowserView::BrowserView (LibraryManager& lib, StemManager& stems) : library (lib), stemManager (stems)
{
    addAndMakeVisible (tree);
    tree.setRootItemVisible (false);
    tree.setDefaultOpenness (false);
    tree.setIndentSize (14);

    addAndMakeVisible (table);
    auto& hdr = table.getHeader();
    hdr.addColumn ("#", 1, 40, 30, 60);
    hdr.addColumn ("Title", 2, 300, 100, 2000);
    hdr.addColumn ("Preview", 11, 130, 70, 400);
    hdr.addColumn ("Artist", 3, 200, 80, 1000);
    hdr.addColumn ("BPM", 4, 64, 50, 90);
    hdr.addColumn ("Key", 5, 50, 40, 80);
    hdr.addColumn ("Time", 6, 60, 50, 90);
    hdr.addColumn ("Genre", 7, 120, 60, 400);
    hdr.addColumn ("Stems", 8, 56, 40, 80);
    hdr.addColumn ("Cues", 9, 46, 36, 70);
    hdr.addColumn ("R/C", 10, 46, 36, 70);
    hdr.addColumn ("Rating", 12, 76, 60, 100);
    hdr.addColumn ("Plays", 13, 48, 40, 80);
    hdr.setStretchToFitActive (true);
    table.setRowHeight (24);
    table.setMultipleSelectionEnabled (true);
    table.setColour (juce::ListBox::backgroundColourId, Theme::panel);

    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("Search", Theme::textFaint);
    search.setFont (Theme::font (14.0f));
    search.onTextChange = [this] { applyFilter(); };
    search.onEscapeKey = [this] { search.clear(); applyFilter(); };

    for (auto* b : { &load1, &load2 })
    {
        addAndMakeVisible (*b);
        b->setColour (juce::TextButton::buttonOnColourId, Theme::deckColour (b == &load1 ? 0 : 1));
    }
    load1.onClick = [this] { loadSelected (0); };
    load2.onClick = [this] { loadSelected (1); };

    for (auto* b : { &importButton, &exportButton, &viewR, &viewC })
    {
        addAndMakeVisible (*b);
    }
    importButton.setColour (juce::TextButton::buttonOnColourId, Theme::good);    // input
    exportButton.setColour (juce::TextButton::buttonOnColourId, Theme::output);  // output
    importButton.setTooltip ("Copy rekordbox's collection or a USB stick into Centrifuge's library (rekordbox is only read)");
    exportButton.setTooltip ("Write a rekordbox-compatible USB stick or a rekordbox XML file");
    viewR.setTooltip ("Show the library's rekordbox copy");
    viewC.setTooltip ("Show the library's Centrifuge version");
    importButton.onClick = [this] { if (onImport) onImport (importButton); };
    exportButton.onClick = [this] { if (onExport) onExport (exportButton); };
    viewR.onClick = [this] { setLibraryView (0); };
    viewC.onClick = [this] { setLibraryView (1); };
    viewC.setToggleState (true, juce::dontSendNotification);

    addAndMakeVisible (countLabel);
    countLabel.setFont (Theme::font (12.5f));
    countLabel.setColour (juce::Label::textColourId, Theme::textDim);
    countLabel.setJustificationType (juce::Justification::centredRight);

    rootItem = std::make_unique<Item> (*this, nullptr, nullptr, "root", "root", false, false);
    tree.setRootItem (rootItem.get());

    startTimer (4000);
}

BrowserView::~BrowserView()
{
    tree.setRootItem (nullptr);
    previewPool.removeAllJobs (true, 3000);
}

void BrowserView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::line);
    g.fillRect (tree.getRight(), 0, 1, getHeight());
    g.setColour (Theme::accent);
    g.fillRect (0, 0, getWidth(), 2);
}

bool BrowserView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory() || file.hasFileExtension ("mp3;wav;wave;aif;aiff;flac;m4a;aac;mp4;alac")) return true;
    }
    return false;
}

void BrowserView::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    juce::Array<juce::File> items;
    for (auto& f : files) items.add (juce::File (f));
    if (onFilesDropped) onFilesDropped (items);
}

void BrowserView::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover) return;
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (psyched::Colours::void_.withAlpha (0.75f));
    g.fillRect (r);
    g.setColour (psyched::Colours::green);
    g.drawRect (r, 2.0f);
    psyched::paintTitle (g, "Drop to add to Centrifuge's library", r.withTrimmedBottom (r.getHeight() * 0.5f - 14.0f), juce::Justification::centredBottom,
                         psyched::Colours::green, 13.0f);
    psyched::paintLabel (g, "Folders become playlists. Each track is analysed for tempo, beat grid and key.",
                         r.withTrimmedTop (r.getHeight() * 0.5f + 8.0f).withHeight (20.0f), juce::Justification::centred, psyched::Colours::ink2);
}

void BrowserView::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (1);
    tree.setBounds (r.removeFromLeft (juce::jlimit (200, 320, getWidth() / 5)));
    r.removeFromLeft (1);
    auto top = r.removeFromTop (34).reduced (6, 4);
    importButton.setBounds (top.removeFromLeft (76));
    top.removeFromLeft (6);
    exportButton.setBounds (top.removeFromLeft (76));
    top.removeFromLeft (12);
    viewR.setBounds (top.removeFromLeft (92));
    top.removeFromLeft (2);
    viewC.setBounds (top.removeFromLeft (98));
    top.removeFromLeft (12);
    load2.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    load1.setBounds (top.removeFromRight (70));
    top.removeFromRight (10);
    countLabel.setBounds (top.removeFromRight (100));
    search.setBounds (top.removeFromLeft (juce::jmin (300, top.getWidth())));
    table.setBounds (r);
}

void BrowserView::refreshSources()
{
    auto openness = tree.getOpennessState (true);
    sources = library.getSources();
    if (centrifugeSource)
        if (auto c = centrifugeSource (libView))
            sources.insert (sources.begin(), c);

    rootItem->clearSubItems();
    for (auto& s : sources)
    {
        const auto key = s->kind == LibrarySource::Kind::centrifuge ? juce::String ("centrifuge")
                       : (s->kind == LibrarySource::Kind::pcCollection ? juce::String ("pc:") : juce::String ("usb:")) + s->root.getFullPathName();
        auto* item = new Item (*this, s, nullptr, s->name, key, false, true);
        rootItem->addSubItem (item);
    }

    if (openness != nullptr)
        tree.restoreOpennessState (*openness, true);
    else if (rootItem->getNumSubItems() > 0)
        rootItem->getSubItem (0)->setOpen (true);

    // Nothing useful showing yet: open the first source that actually has tracks.
    if (currentSource == nullptr || currentSource->tracks.empty())
        for (int i = 0; i < rootItem->getNumSubItems(); ++i)
            if (auto* item = dynamic_cast<Item*> (rootItem->getSubItem (i)); item != nullptr && ! item->src->tracks.empty())
            {
                item->setOpen (true);
                if (item->getNumSubItems() > 0)
                    item->getSubItem (0)->setSelected (true, true);
                break;
            }

    // Keep showing the same list if it still exists (sources are reloaded as new objects).
    if (currentSource != nullptr)
    {
        for (auto& s : sources)
            if (s->name == currentSource->name && s.get() != currentSource.get())
            {
                currentSource = s;
                if (auto* sel = dynamic_cast<Item*> (tree.getSelectedItem (0)))
                    showList (sel->src, sel->allTracks ? nullptr : sel->node, sel->name, sel->key);
                break;
            }
    }
}

void BrowserView::showList (std::shared_ptr<const LibrarySource> src, const PlaylistNode* node, const juce::String& name, const juce::String& key)
{
    currentSource = std::move (src);
    currentListName = name;
    currentKey = key;
    allRows.clear();
    if (currentSource != nullptr)
    {
        if (node == nullptr)
            for (int i = 0; i < (int) currentSource->tracks.size(); ++i) allRows.push_back (i);
        else
            allRows = node->trackIndices;
    }
    applyFilter();
    table.deselectAllRows();
    if (! rows.empty()) table.selectRow (0);
}

void BrowserView::applyFilter()
{
    rows.clear();
    const auto q = search.getText().trim().toLowerCase();
    for (int i : allRows)
    {
        if (currentSource == nullptr || i < 0 || i >= (int) currentSource->tracks.size()) continue;
        const auto& t = currentSource->tracks[(size_t) i];
        if (q.isEmpty() || t.title.toLowerCase().contains (q) || t.artist.toLowerCase().contains (q)
            || t.genre.toLowerCase().contains (q) || t.key.toLowerCase() == q || MusicalKey::display (t.key).toLowerCase() == q || t.album.toLowerCase().contains (q))
            rows.push_back (i);
    }

    if (sortColumn > 1 && currentSource != nullptr)
    {
        auto& tr = currentSource->tracks;
        std::stable_sort (rows.begin(), rows.end(), [&] (int a, int b)
        {
            const auto& x = tr[(size_t) a];
            const auto& y = tr[(size_t) b];
            int c = 0;
            switch (sortColumn)
            {
                case 2: c = x.title.compareNatural (y.title); break;
                case 3: c = x.artist.compareNatural (y.artist); break;
                case 4: c = x.bpm < y.bpm ? -1 : (x.bpm > y.bpm ? 1 : 0); break;
                case 5: c = x.key.compareNatural (y.key); break;
                case 6: c = x.durationSec < y.durationSec ? -1 : (x.durationSec > y.durationSec ? 1 : 0); break;
                case 7: c = x.genre.compareNatural (y.genre); break;
                case 9: c = x.numCues - y.numCues; break;
                case 10: c = (int) x.versionsDiffer - (int) y.versionsDiffer; break;
                case 12: c = x.rating - y.rating; break;
                case 13: c = x.playCount - y.playCount; break;
                default: break;
            }
            return sortForwards ? c < 0 : c > 0;
        });
    }

    countLabel.setText (juce::String ((int) rows.size()) + " tracks", juce::dontSendNotification);
    table.updateContent();
    table.repaint();
}

int BrowserView::getNumRows() { return (int) rows.size(); }

void BrowserView::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    g.setColour (row % 2 ? Theme::panel : juce::Colour (0xff0d0c17));
    g.fillRect (0, 0, w, h);
    if (selected)
    {
        g.setColour (Theme::accent.withAlpha (treeFocused ? 0.08f : 0.13f));
        g.fillRect (0, 0, w, h);
        g.setColour (Theme::accent);
        g.fillRect (w - 2, 0, 2, h);
    }
    if (row < (int) rows.size() && currentSource != nullptr)
    {
        const auto& t = currentSource->tracks[(size_t) rows[(size_t) row]];
        if (! t.colour.isTransparent())
        {
            g.setColour (t.colour);
            g.fillRect (0, 3, 3, h - 6);
        }
    }
}

void BrowserView::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (row >= (int) rows.size() || currentSource == nullptr) return;
    const auto& t = currentSource->tracks[(size_t) rows[(size_t) row]];

    juce::String s;
    auto just = juce::Justification::centredLeft;
    g.setColour (Theme::text);
    g.setFont (Theme::font (13.5f));

    switch (col)
    {
        case 1: s = juce::String (row + 1); g.setColour (Theme::textFaint); just = juce::Justification::centredRight; break;
        case 2: s = t.title; break;
        case 3: s = t.artist; g.setColour (Theme::textDim); break;
        case 4: s = t.bpm > 0 ? juce::String (t.bpm, 2) : juce::String(); just = juce::Justification::centredRight; break;
        case 5: s = MusicalKey::display (t.key); g.setColour (Theme::good); break;
        case 6:
        {
            const int sec = (int) t.durationSec;
            s = juce::String (sec / 60) + ":" + juce::String (sec % 60).paddedLeft ('0', 2);
            just = juce::Justification::centredRight;
            break;
        }
        case 7: s = t.genre; g.setColour (Theme::textDim); break;
        case 8:
        {
            auto it = stemCache.find (t.file.getFullPathName());
            bool has;
            if (it == stemCache.end()) { has = stemManager.hasCachedStems (t); stemCache[t.file.getFullPathName()] = has; }
            else has = it->second;
            if (has)
            {
                // The Centrifuge rotor with full tubes: this track's stems are ready.
                const float sz = juce::jmin ((float) h - 4.0f, 20.0f);
                CentrifugeRotor::paintRotor (g, juce::Rectangle<float> ((float) w * 0.5f - sz * 0.5f, (float) h * 0.5f - sz * 0.5f, sz, sz),
                                             0.4f, 0.0f, 1.0f, 0.0f, 0.0f);
            }
            return;
        }
        case 9:
            if (t.libraryId > 0) { s = juce::String (t.numCues) + (t.hasSkips ? "+" : ""); g.setColour (Theme::textDim); }
            just = juce::Justification::centred;
            break;
        case 10:
            if (t.libraryId > 0 && t.versionsDiffer)
            {
                // The rekordbox copy and the Centrifuge version are different
                auto b = juce::Rectangle<float> ((float) w * 0.5f - 15.0f, (float) h * 0.5f - 8.0f, 30.0f, 16.0f);
                g.setColour (Theme::accent.withAlpha (0.25f));
                g.fillRect (b);
                g.setColour (Theme::accent);
                g.setFont (Theme::font (11.0f, true));
                g.drawText ("R/C", b, juce::Justification::centred);
            }
            return;
        case 11:
        {
            // Preview waveform (click to play from there); the previewing track shows its position and a stop square
            const auto path = t.file.getFullPathName();
            const bool active = path == previewPath && previewPath.isNotEmpty();
            auto area = juce::Rectangle<float> (2.0f, 3.0f, (float) w - 4.0f, (float) h - 6.0f);
            if (active)
            {
                auto stop = area.removeFromLeft (area.getHeight());
                g.setColour (previewPlaying ? Theme::output : Theme::textDim);
                g.fillRect (stop.reduced (3.0f));
                area.removeFromLeft (3.0f);
            }
            auto it = previewWaves.find (path);
            if (it == previewWaves.end()) { requestPreviewWave (t); it = previewWaves.find (path); }
            const auto& wv = it->second;
            if (wv.empty())
            {
                g.setColour (Theme::line);
                g.fillRect (area.withSizeKeepingCentre (area.getWidth(), 1.0f));
            }
            else
            {
                const auto col = active ? Theme::output : Theme::textDim;
                const int n = (int) wv.size();
                const float px = area.getWidth() / (float) n;
                for (int i = 0; i < n; ++i)
                {
                    const float hh = juce::jmax (1.0f, area.getHeight() * (float) wv[(size_t) i] / 255.0f);
                    const bool played = active && (float) i / (float) n < (float) previewFraction;
                    g.setColour (played ? col : col.withAlpha (0.55f));
                    g.fillRect (area.getX() + (float) i * px, area.getCentreY() - hh * 0.5f, juce::jmax (1.0f, px - 0.5f), hh);
                }
            }
            if (active)
            {
                g.setColour (juce::Colours::white);
                g.fillRect (area.getX() + area.getWidth() * (float) previewFraction - 0.5f, area.getY() - 2.0f, 1.5f, area.getHeight() + 4.0f);
            }
            return;
        }
        case 12:
        {
            // Rating: five stars (click to set; click the same star again to clear)
            const float sz = juce::jmin ((float) h - 8.0f, ((float) w - 8.0f) / 5.0f);
            for (int i = 0; i < 5; ++i)
            {
                juce::Path star;
                const auto c = juce::Point<float> (4.0f + sz * (i + 0.5f), (float) h * 0.5f);
                star.addStar (c, 5, sz * 0.22f, sz * 0.5f);
                g.setColour (i < t.rating ? Theme::accent : Theme::line);
                g.fillPath (star);
            }
            return;
        }
        case 13:
            if (t.libraryId > 0) { s = juce::String (t.playCount); g.setColour (Theme::textDim); }
            just = juce::Justification::centred;
            break;
        default: break;
    }
    g.drawText (s, juce::Rectangle<int> (6, 0, w - 12, h), just, true);
}

float BrowserView::cellFraction (int col, const juce::MouseEvent& e) const
{
    auto& hdr = table.getHeader();
    const auto pos = hdr.getColumnPosition (hdr.getIndexOfColumnId (col, true));
    const auto p = e.getEventRelativeTo (const_cast<juce::TableListBox*> (&table)).position;
    return juce::jlimit (0.0f, 1.0f, (p.x - (float) pos.getX() + (float) table.getViewport()->getViewPositionX()) / (float) juce::jmax (1, pos.getWidth()));
}

void BrowserView::cellClicked (int row, int col, const juce::MouseEvent& e)
{
    treeFocused = false;
    tree.repaint();
    if (row < 0 || row >= (int) rows.size() || currentSource == nullptr) return;
    const auto& t = currentSource->tracks[(size_t) rows[(size_t) row]];

    if (e.mods.isPopupMenu())
    {
        if (! table.isRowSelected (row)) table.selectRow (row);
        showTrackMenu (row);
        return;
    }
    if (col == 11)
    {
        auto& hdr = table.getHeader();
        const int w = hdr.getColumnWidth (11);
        float f = cellFraction (11, e);
        const bool active = t.file.getFullPathName() == previewPath && previewPath.isNotEmpty();
        if (active)
        {
            const float stopW = (float) table.getRowHeight() - 6.0f;
            if (f * (float) w < stopW + 2.0f) { if (onPreviewStop) onPreviewStop(); return; }
            f = juce::jlimit (0.0f, 1.0f, (f * (float) w - stopW - 5.0f) / juce::jmax (1.0f, (float) w - stopW - 9.0f));
        }
        if (onPreview) onPreview (t, currentSource->kind == LibrarySource::Kind::pcCollection, (double) f);
        return;
    }
    if (col == 12 && t.libraryId > 0 && onSetRating)
    {
        const int stars = juce::jlimit (1, 5, (int) (cellFraction (12, e) * 5.0f) + 1);
        onSetRating (t, stars == t.rating ? 0 : stars);
    }
}

juce::var BrowserView::getDragSourceDescription (const juce::SparseSet<int>&)
{
    dragged = getSelectedTracks();
    draggedPc = currentSource != nullptr && currentSource->kind == LibrarySource::Kind::pcCollection;
    return dragged.empty() ? juce::var() : juce::var ("centrifuge-tracks");
}

void BrowserView::showTrackMenu (int)
{
    auto tracks = getSelectedTracks();
    if (tracks.empty()) return;
    bool anyLibrary = false;
    for (auto& t : tracks) anyLibrary = anyLibrary || t.libraryId > 0;
    const bool one = tracks.size() == 1;
    juce::PopupMenu m;
    m.addSectionHeader (one ? tracks.front().displayArtistTitle() : juce::String ((int) tracks.size()) + " tracks");
    m.addItem (1, "Load to deck 1", one);
    m.addItem (2, "Load to deck 2", one);
    m.addItem (3, "Load into PREP", one);
    if (anyLibrary)
    {
        m.addSeparator();
        m.addItem (10, "Remove from the Centrifuge collection");
        m.addItem (11, "Remove from the rekordbox copy");
        m.addItem (12, "Reset play count");
    }
    juce::Component::SafePointer<BrowserView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, tracks] (int r)
    {
        if (safe == nullptr || safe->onTrackAction == nullptr || r == 0) return;
        const char* actions[] = { "", "load.0", "load.1", "load.2" };
        if (r >= 1 && r <= 3) safe->onTrackAction (tracks, actions[r]);
        else if (r == 10) safe->onTrackAction (tracks, "remove.cf");
        else if (r == 11) safe->onTrackAction (tracks, "remove.rb");
        else if (r == 12) safe->onTrackAction (tracks, "plays.reset");
    });
}

void BrowserView::setPreviewState (const juce::String& path, double fraction, bool playing)
{
    if (path == previewPath && std::abs (fraction - previewFraction) < 0.002 && playing == previewPlaying) return;
    previewPath = path;
    previewFraction = fraction;
    previewPlaying = playing;
    table.repaint();
}

void BrowserView::notePreviewWaveform (const juce::String& path, const WaveformData& w)
{
    constexpr int points = 100;
    std::vector<uint8_t> v (points, 0);
    const int n = w.size();
    if (n <= 0) return;
    for (int i = 0; i < n; ++i)
    {
        const int b = juce::jmin (points - 1, i * points / n);
        int hgt = 0;
        if (w.style == WaveformData::Style::threeBand)
            hgt = juce::jmax ((int) w.low[(size_t) i], (int) w.mid[(size_t) i], (int) w.high[(size_t) i]);
        else hgt = w.height[(size_t) i];
        v[(size_t) b] = (uint8_t) juce::jmax ((int) v[(size_t) b], hgt);
    }
    previewWaves[path] = std::move (v);
    table.repaint();
}

void BrowserView::requestPreviewWave (const TrackInfo& t)
{
    const auto path = t.file.getFullPathName();
    previewWaves[path] = {};   // placeholder while loading (and when there's no analysis yet)
    if (! t.anlzBase.existsAsFile()) return;
    const auto anlz = t.anlzBase;
    juce::Component::SafePointer<BrowserView> safe (this);
    previewPool.addJob ([safe, anlz, path]
    {
        const auto data = loadAnlz (anlz);
        if (data.waveforms.numDetailEntries() <= 0) return;
        auto w = WaveformData::fromAnlz (data.waveforms, WaveformData::Style::threeBand);
        if (w == nullptr) return;
        juce::MessageManager::callAsync ([safe, w, path] { if (safe != nullptr) safe->notePreviewWaveform (path, *w); });
    });
}

void BrowserView::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) rows.size() || currentSource == nullptr) return;
    const int deck = chooseDeckForDoubleClick ? chooseDeckForDoubleClick() : 0;
    if (onLoadToDeck)
        onLoadToDeck (deck, currentSource->tracks[(size_t) rows[(size_t) row]], currentSource->kind == LibrarySource::Kind::pcCollection);
}

void BrowserView::sortOrderChanged (int columnId, bool forwards)
{
    sortColumn = columnId;
    sortForwards = forwards;
    applyFilter();
}

void BrowserView::timerCallback()
{
    // Stem cache status can change in the background (batch analysis).
    stemCache.clear();
    table.repaint();
}

//==============================================================================
void BrowserView::scroll (int delta)
{
    if (delta == 0) return;
    if (treeFocused)
    {
        auto* sel = tree.getSelectedItem (0);
        const int row = sel != nullptr ? sel->getRowNumberInTree() : -1;
        const int next = juce::jlimit (0, juce::jmax (0, tree.getNumRowsInTree() - 1), row + delta);
        if (auto* item = tree.getItemOnRow (next))
        {
            item->setSelected (true, true);
            tree.scrollToKeepItemVisible (item);
        }
    }
    else
    {
        const int row = juce::jlimit (0, juce::jmax (0, (int) rows.size() - 1), table.getSelectedRow() + delta);
        table.selectRow (row);
        table.scrollToEnsureRowIsOnscreen (row);
    }
    tree.repaint();
    table.repaint();
}

void BrowserView::enter()
{
    if (treeFocused)
    {
        if (auto* sel = dynamic_cast<Item*> (tree.getSelectedItem (0)))
        {
            if (! sel->isList()) { sel->setOpen (! sel->isOpen()); return; }
            treeFocused = false;
            if (table.getSelectedRow() < 0 && ! rows.empty()) table.selectRow (0);
        }
    }
    tree.repaint();
    table.repaint();
}

void BrowserView::back()
{
    treeFocused = true;
    tree.repaint();
    table.repaint();
}

void BrowserView::toggleFocus()
{
    treeFocused = ! treeFocused;
    tree.repaint();
    table.repaint();
}

void BrowserView::loadSelected (int deck)
{
    const int row = table.getSelectedRow();
    if (row < 0 || row >= (int) rows.size() || currentSource == nullptr) return;
    if (onLoadToDeck)
        onLoadToDeck (deck, currentSource->tracks[(size_t) rows[(size_t) row]], currentSource->kind == LibrarySource::Kind::pcCollection);
}

void BrowserView::setLibraryView (int v)
{
    libView = v;
    viewR.setToggleState (v == 0, juce::dontSendNotification);
    viewC.setToggleState (v == 1, juce::dontSendNotification);
    refreshSources();
}

std::vector<std::shared_ptr<const LibrarySource>> BrowserView::getRekordboxSources() const
{
    return library.getSources();
}

std::vector<int> BrowserView::getSelectedSourceIndices() const
{
    std::vector<int> r;
    auto sel = table.getSelectedRows();
    for (int i = 0; i < sel.size(); ++i)
        if (sel[i] >= 0 && sel[i] < (int) rows.size()) r.push_back (rows[(size_t) sel[i]]);
    return r;
}

std::vector<TrackInfo> BrowserView::getVisibleTracks() const
{
    std::vector<TrackInfo> r;
    if (currentSource != nullptr)
        for (int i : rows) r.push_back (currentSource->tracks[(size_t) i]);
    return r;
}

std::vector<TrackInfo> BrowserView::getSelectedTracks() const
{
    std::vector<TrackInfo> r;
    if (currentSource == nullptr) return r;
    auto sel = table.getSelectedRows();
    for (int i = 0; i < sel.size(); ++i)
    {
        const int row = sel[i];
        if (row >= 0 && row < (int) rows.size())
            r.push_back (currentSource->tracks[(size_t) rows[(size_t) row]]);
    }
    return r;
}

} // namespace orbit
