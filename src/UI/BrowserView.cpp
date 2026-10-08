#include "BrowserView.h"
#include "Theme.h"

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
            g.setColour (Theme::accent.withAlpha (owner.treeFocused ? 0.3f : 0.16f));
            g.fillRect (0, 0, w, h);
        }
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (4.0f, 0.0f);
        auto icon = r.removeFromLeft (16.0f).withSizeKeepingCentre (12.0f, 12.0f);
        if (isSourceRoot)
        {
            g.setColour (src->kind == LibrarySource::Kind::usbExport ? Theme::warn : Theme::deckColour (0));
            g.fillRoundedRectangle (icon, 2.0f);
        }
        else if (node != nullptr && node->isFolder)
        {
            g.setColour (Theme::textDim);
            g.drawRoundedRectangle (icon, 2.0f, 1.2f);
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
    hdr.addColumn ("Artist", 3, 200, 80, 1000);
    hdr.addColumn ("BPM", 4, 64, 50, 90);
    hdr.addColumn ("Key", 5, 50, 40, 80);
    hdr.addColumn ("Time", 6, 60, 50, 90);
    hdr.addColumn ("Genre", 7, 120, 60, 400);
    hdr.addColumn ("Stems", 8, 56, 40, 80);
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
        b->setColour (juce::TextButton::buttonColourId, Theme::panelHi);
    }
    load1.onClick = [this] { loadSelected (0); };
    load2.onClick = [this] { loadSelected (1); };

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
}

void BrowserView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::line);
    g.fillRect (tree.getRight(), 0, 1, getHeight());
    g.fillRect (0, 0, getWidth(), 1);
}

void BrowserView::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (1);
    tree.setBounds (r.removeFromLeft (juce::jlimit (200, 320, getWidth() / 5)));
    r.removeFromLeft (1);
    auto top = r.removeFromTop (34).reduced (6, 4);
    load2.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    load1.setBounds (top.removeFromRight (70));
    top.removeFromRight (10);
    countLabel.setBounds (top.removeFromRight (140));
    search.setBounds (top.removeFromLeft (juce::jmin (360, top.getWidth())));
    table.setBounds (r);
}

void BrowserView::refreshSources()
{
    auto openness = tree.getOpennessState (true);
    sources = library.getSources();

    rootItem->clearSubItems();
    for (auto& s : sources)
    {
        const auto key = (s->kind == LibrarySource::Kind::pcCollection ? juce::String ("pc:") : juce::String ("usb:")) + s->root.getFullPathName();
        auto* item = new Item (*this, s, nullptr, s->name, key, false, true);
        rootItem->addSubItem (item);
    }

    if (openness != nullptr)
        tree.restoreOpennessState (*openness, true);
    else if (rootItem->getNumSubItems() > 0)
        rootItem->getSubItem (0)->setOpen (true);

    // If the selection restore didn't trigger a list, show the first source's tracks.
    if (currentSource == nullptr && ! sources.empty())
        if (auto* first = dynamic_cast<Item*> (rootItem->getSubItem (0)))
        {
            first->setOpen (true);
            if (first->getNumSubItems() > 0)
                first->getSubItem (0)->setSelected (true, true);
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
            || t.genre.toLowerCase().contains (q) || t.key.toLowerCase() == q || t.album.toLowerCase().contains (q))
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
    g.setColour (row % 2 ? Theme::panel : Theme::panel.brighter (0.025f));
    g.fillRect (0, 0, w, h);
    if (selected)
    {
        g.setColour (Theme::accent.withAlpha (treeFocused ? 0.14f : 0.26f));
        g.fillRect (0, 0, w, h);
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
        case 5: s = t.key; g.setColour (Theme::good); break;
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
                g.setColour (Theme::good);
                g.fillEllipse ((float) w * 0.5f - 4.0f, (float) h * 0.5f - 4.0f, 8.0f, 8.0f);
            }
            return;
        }
        default: break;
    }
    g.drawText (s, juce::Rectangle<int> (6, 0, w - 12, h), just, true);
}

void BrowserView::cellClicked (int, int, const juce::MouseEvent&)
{
    treeFocused = false;
    tree.repaint();
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
