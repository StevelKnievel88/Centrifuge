#include "BrowserView.h"
#include "../App/Features.h"
#include "Theme.h"
#include "../Library/Anlz.h"
#include "../Audio/MusicalKey.h"
#include "CentrifugeRotor.h"
#include "../Library/PreviewWaveCache.h"

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

namespace
{
    /** Each list's own track order (the # column), by list key: file paths in order. */
    struct ListOrders
    {
        static juce::File file() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Centrifuge").getChildFile ("list-orders.json"); }
        static juce::var& data()
        {
            static juce::var d = []
            {
                auto v = juce::JSON::parse (file());
                return v.isObject() ? v : juce::var (new juce::DynamicObject());
            }();
            return d;
        }
        static void save() { file().getParentDirectory().createDirectory(); file().replaceWithText (juce::JSON::toString (data())); }
    };
}

/** The table's header: right-click adds "Save this order as the list's # order". */
class BrowserView::Header : public juce::TableHeaderComponent
{
public:
    explicit Header (BrowserView& o) : owner (o) {}
    void addMenuItems (juce::PopupMenu& menu, int columnIdClicked) override
    {
        menu.addItem (9001, "Save this order as the list's # order (renumbers 1, 2, 3 ... as shown now)");
        menu.addItem (9002, "Reset the # order (back to the list's own order)");
        menu.addSeparator();
        juce::TableHeaderComponent::addMenuItems (menu, columnIdClicked);
    }
    void reactToMenuItem (int menuReturnId, int columnIdClicked) override
    {
        if (menuReturnId == 9001) owner.saveOrderAsDefault();
        else if (menuReturnId == 9002) owner.resetSavedOrder();
        else juce::TableHeaderComponent::reactToMenuItem (menuReturnId, columnIdClicked);
    }
private:
    BrowserView& owner;
};

//==============================================================================
BrowserView::BrowserView (LibraryManager& lib, StemManager& stems) : library (lib), stemManager (stems)
{
    table.setHeader (std::make_unique<Header> (*this));
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
    hdr.addColumn ("Stems", 8, 84, 40, 120);
    hdr.addColumn ("Cues", 9, 46, 36, 70);
    if (Features::rekordboxCopy) hdr.addColumn ("R/C", 10, 46, 36, 70);
    hdr.addColumn ("Rating", 12, 76, 60, 100);
    hdr.addColumn ("Plays", 13, 48, 40, 80);
    hdr.setStretchToFitActive (false);   // resizing a column moves the ones to its right along, keeping their widths
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
    addAndMakeVisible (infoButton);
    infoButton.setClickingTogglesState (false);
    infoButton.setTooltip ("Show / hide the track info panel (edit several tracks at once)");
    infoButton.onClick = [this] { setInfoPanelOpen (! infoOpen); if (onInfoPanelToggled) onInfoPanelToggled (infoOpen); };
    addChildComponent (infoPanel);
    infoPanel.onEdit = [this] (const std::vector<TrackInfo>& t, const juce::String& f, const juce::String& v) { if (onEditInfo) onEditInfo (t, f, v); };
    infoPanel.onWriteTags = [this] (const std::vector<TrackInfo>& t) { if (onWriteTags) onWriteTags (t); };
    addAndMakeVisible (expandButton);
    expandButton.setTooltip ("Make the browser fill the screen (and back)");
    expandButton.onClick = [this] { if (onExpandToggled) onExpandToggled(); };
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

    startTimer (2000);
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
    viewR.setVisible (Features::rekordboxCopy);
    viewC.setVisible (Features::rekordboxCopy);
    if (Features::rekordboxCopy)
    {
        viewR.setBounds (top.removeFromLeft (92));
        top.removeFromLeft (2);
        viewC.setBounds (top.removeFromLeft (98));
        top.removeFromLeft (12);
    }
    expandButton.setBounds (top.removeFromRight (84));
    top.removeFromRight (6);
    infoButton.setBounds (top.removeFromRight (60));
    top.removeFromRight (10);
    load2.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    load1.setBounds (top.removeFromRight (70));
    top.removeFromRight (10);
    countLabel.setBounds (top.removeFromRight (100));
    search.setBounds (top.removeFromLeft (juce::jmin (300, top.getWidth())));
    if (infoOpen) infoPanel.setBounds (r.removeFromRight (juce::jmin (320, r.getWidth() / 2)));
    table.setBounds (r);
}

void BrowserView::refreshSources()
{
    auto openness = tree.getOpennessState (true);
    sources = library.getSources();
    if (! Features::rekordboxCollection)
        sources.erase (std::remove_if (sources.begin(), sources.end(),
                                       [] (auto& s) { return s->kind == LibrarySource::Kind::pcCollection; }), sources.end());
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
    // The same list again (the library was refreshed): keep the selection and the scroll position
    const bool sameList = key.isNotEmpty() && key == currentKey && currentSource != nullptr;
    juce::StringArray keepSelected;
    const int keepScroll = table.getVerticalPosition() > 0.0 ? table.getViewport()->getViewPositionY() : 0;
    if (sameList)
        for (auto& t : getSelectedTracks()) keepSelected.add (t.file.getFullPathName());
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
        // The list's saved # order (tracks it doesn't know yet go after, as they were)
        if (auto* saved = key.isNotEmpty() ? ListOrders::data()[juce::Identifier (key)].getArray() : nullptr)
        {
            std::unordered_map<juce::String, int> pos;
            for (int i = 0; i < saved->size(); ++i) pos[(*saved)[i].toString()] = i;
            std::vector<std::pair<int, int>> keyed;
            for (int k = 0; k < (int) allRows.size(); ++k)
            {
                auto it = pos.find (currentSource->tracks[(size_t) allRows[(size_t) k]].file.getFullPathName());
                keyed.push_back ({ it != pos.end() ? it->second : 1000000 + k, allRows[(size_t) k] });
            }
            std::stable_sort (keyed.begin(), keyed.end(), [] (auto& a, auto& b) { return a.first < b.first; });
            for (size_t k = 0; k < keyed.size(); ++k) allRows[k] = keyed[k].second;
        }
    }
    numbers.clear();
    for (int k = 0; k < (int) allRows.size(); ++k) numbers[allRows[(size_t) k]] = k + 1;
    applyFilter();
    table.deselectAllRows();
    if (sameList && ! keepSelected.isEmpty())
    {
        juce::SparseSet<int> sel;
        for (int k = 0; k < (int) rows.size(); ++k)
            if (keepSelected.contains (currentSource->tracks[(size_t) rows[(size_t) k]].file.getFullPathName())) sel.addRange ({ k, k + 1 });
        table.setSelectedRows (sel, juce::dontSendNotification);
        table.getViewport()->setViewPosition (table.getViewport()->getViewPositionX(), keepScroll);
        if (infoOpen) infoPanel.setTracks (getSelectedTracks());
    }
    else if (! rows.empty()) table.selectRow (0);
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

    if (sortColumn >= 1 && currentSource != nullptr)
    {
        auto& tr = currentSource->tracks;
        std::stable_sort (rows.begin(), rows.end(), [&] (int a, int b)
        {
            const auto& x = tr[(size_t) a];
            const auto& y = tr[(size_t) b];
            int c = 0;
            switch (sortColumn)
            {
                case 1: c = numberOf (a) - numberOf (b); break;
                case 8: c = stemQualityOf (x) - stemQualityOf (y); break;
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
    if (infoOpen) infoPanel.setTracks (getSelectedTracks());
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
        case 1: s = juce::String (numberOf (rows[(size_t) row])); g.setColour (Theme::textFaint); just = juce::Justification::centredRight; break;
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
            const int q = stemQualityOf (t);
            if (q >= 0)
            {
                // The Centrifuge rotor with full tubes: this track's stems are ready - and at what quality.
                const float sz = juce::jmin ((float) h - 4.0f, 18.0f);
                CentrifugeRotor::paintRotor (g, juce::Rectangle<float> (4.0f, (float) h * 0.5f - sz * 0.5f, sz, sz),
                                             0.4f, 0.0f, 1.0f, 0.0f, 0.0f);
                const char* names[] = { "FAST", "BETTER", "HIGH", "BEST" };
                const juce::Colour cols[] = { Theme::textDim, juce::Colour (0xff6e8cff), juce::Colour (0xff2ee6d6), juce::Colour (0xffffd339) };
                g.setColour (cols[juce::jlimit (0, 3, q)]);
                g.setFont (psyched::Fonts::mono (10.0f, true));
                g.drawText (names[juce::jlimit (0, 3, q)], juce::Rectangle<int> ((int) sz + 9, 0, w - (int) sz - 10, h), juce::Justification::centredLeft, true);
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
            {
                // The stop square's place is always kept, so the waveform doesn't change size when a preview starts
                auto stop = area.removeFromLeft (area.getHeight());
                if (active)
                {
                    g.setColour (previewPlaying ? Theme::output : Theme::textDim);
                    g.fillRect (stop.reduced (3.0f));
                }
                area.removeFromLeft (3.0f);
            }
            auto it = previewWaves.find (path);
            if (it == previewWaves.end()) { requestPreviewWave (t); it = previewWaves.find (path); }
            const auto& pd = it->second;
            if (pd.low.empty())
            {
                if (pd.missing)
                {
                    g.setColour (Theme::textFaint);
                    g.setFont (psyched::Fonts::label (9.0f));
                    g.drawText ("NOT ANALYSED", area, juce::Justification::centred, true);
                }
                else
                {
                    g.setColour (Theme::line);
                    g.fillRect (area.withSizeKeepingCentre (area.getWidth(), 1.0f));
                }
            }
            else
            {
                paintPreview (g, area, pd);
                if (active)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.45f));
                    g.fillRect (area.withWidth (area.getWidth() * (float) previewFraction));
                }
            }
            if (pd.haveMarks)
                paintOverviewMarks (g, area, pd.marks, true);
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

void BrowserView::paintPreview (juce::Graphics& g, juce::Rectangle<float> area, const PreviewData& pd) const
{
    // A shrunk overview in the chosen style (Settings): 3-band, RGB, single colour or the four stems
    const int n = (int) pd.low.size();
    if (n <= 0) return;
    const float px = area.getWidth() / (float) n;
    const float wh = area.getHeight() - (pd.marks.phrases.empty() ? 0.0f : 3.0f);
    const float base = previewHalf ? area.getY() + wh : area.getY() + wh * 0.5f;
    auto bar = [&] (float x, float w, float h, juce::Colour c)
    {
        g.setColour (c);
        if (previewHalf) g.fillRect (x, base - h, w, h);
        else g.fillRect (x, base - h * 0.5f, w, h);
    };
    const bool stems = previewStyle == 4 && ! pd.stems[0].empty();
    for (int i = 0; i < n; ++i)
    {
        const float x = area.getX() + (float) i * px, w1 = juce::jmax (1.0f, px - 0.3f);
        const float lo = pd.low[(size_t) i] / 255.0f, mi = pd.mid[(size_t) i] / 255.0f, hi = pd.high[(size_t) i] / 255.0f;
        if (stems)
        {
            // overlaid back to front: inst, bass, vocals, drums
            for (int s : { 2, 1, 3, 0 })
            {
                const auto& v = pd.stems[(size_t) s];
                const size_t k = (size_t) juce::jmin ((int) v.size() - 1, i * (int) v.size() / n);
                bar (x, w1, v[k] / 255.0f * wh, Theme::stemColour (s).withAlpha (0.85f));
            }
            continue;
        }
        if (previewStyle == 2)
        {
            // RGB: lows red, mids green, highs blue, height from the loudest
            const float m = juce::jmax (lo, mi, hi, 0.001f);
            bar (x, w1, m * wh, juce::Colour::fromFloatRGBA (juce::jmin (1.0f, 0.25f + lo / m), juce::jmin (1.0f, 0.2f + mi / m * 0.9f), juce::jmin (1.0f, 0.2f + hi / m), 1.0f));
            continue;
        }
        if (previewStyle == 3)
        {
            // shades of one colour: bass deep, mids the colour, highs pale (each to its own height, tallest first)
            const juce::Colour deep = previewTint.withMultipliedSaturation (1.15f).withMultipliedBrightness (0.52f),
                               midC = previewTint.interpolatedWith (juce::Colours::white, 0.18f),
                               pale = previewTint.interpolatedWith (juce::Colours::white, 0.62f);
            std::pair<float, juce::Colour> layers[] = { { lo, deep }, { mi, midC }, { hi, pale } };
            std::sort (std::begin (layers), std::end (layers), [] (auto& a, auto& b) { return a.first > b.first; });
            for (auto& [h, c] : layers) bar (x, w1, h * wh, c);
            bar (x, w1, hi * 0.55f * wh, pale);
            continue;
        }
        // 3-band, like the deck overviews
        bar (x, w1, juce::jmax (lo, mi) * wh, lo > mi ? WaveColours::low : WaveColours::mid);
        bar (x, w1, juce::jmin (lo, mi) * wh, WaveColours::lowAndMid);
        bar (x, w1, hi * wh, WaveColours::high);
    }
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
        const float stopW = (float) table.getRowHeight() - 6.0f;
        if (active && f * (float) w < stopW + 2.0f) { if (onPreviewStop) onPreviewStop(); return; }
        f = juce::jlimit (0.0f, 1.0f, (f * (float) w - stopW - 5.0f) / juce::jmax (1.0f, (float) w - stopW - 9.0f));
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
    m.addItem (20, "Show in Explorer");
    if (anyLibrary)
    {
        m.addSeparator();
        m.addItem (10, Features::rekordboxCopy ? "Remove from the Centrifuge collection" : "Remove from Centrifuge's library");
        if (Features::rekordboxCopy) m.addItem (11, "Remove from the rekordbox copy");
        m.addItem (12, "Reset play count");
    }
    {
        // Clear marks / edits (asks first)
        juce::PopupMenu clear;
        clear.addItem (30, "All markers (and loop markers)");
        clear.addItem (31, "All skips");
        clear.addItem (32, "All hot cues");
        clear.addItem (33, "All phrases");
        clear.addItem (34, "All stem markers");
        clear.addItem (35, "All notes");
        clear.addItem (36, "Phrase order and repeats");
        clear.addItem (37, "Saved edit versions");
        clear.addSeparator();
        clear.addItem (38, "Everything above (the beat grid stays)");
        m.addSeparator();
        m.addSubMenu ("Clear", clear);
    }
    m.addSeparator();
    m.addItem (14, "Analyse  (waveform; grid, BPM and key where missing)");
    m.addItem (15, "Analyse again  (replace the grid, BPM and key)");
    m.addItem (13, "Separate stems now (pre-analysis quality - replaces lower-quality stems)");
    juce::Component::SafePointer<BrowserView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, tracks] (int r)
    {
        if (safe == nullptr || safe->onTrackAction == nullptr || r == 0) return;
        const char* actions[] = { "", "load.0", "load.1", "load.2" };
        const char* clears[] = { "clear.locators", "clear.skips", "clear.hotcues", "clear.phrases", "clear.stemlocs",
                                 "clear.notes", "clear.order", "clear.editversions", "clear.all" };
        if (r >= 1 && r <= 3) safe->onTrackAction (tracks, actions[r]);
        else if (r == 10) safe->onTrackAction (tracks, "remove.cf");
        else if (r == 11) safe->onTrackAction (tracks, "remove.rb");
        else if (r == 12) safe->onTrackAction (tracks, "plays.reset");
        else if (r == 13) safe->onTrackAction (tracks, "stems.separate");
        else if (r == 14) safe->onTrackAction (tracks, "analyse");
        else if (r == 15) safe->onTrackAction (tracks, "analyse.again");
        else if (r == 20) safe->onTrackAction (tracks, "reveal");
        else if (r >= 30 && r <= 38) safe->onTrackAction (tracks, clears[r - 30]);
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
    auto p = PreviewWaveCache::make (w);
    if (p.isEmpty()) return;
    PreviewWaveCache::save (path, p);   // so it's there straight away next time
    auto& pd = previewWaves[path];
    pd.low = std::move (p.low); pd.mid = std::move (p.mid); pd.high = std::move (p.high);
    pd.missing = false;
    table.repaint();
}

void BrowserView::notePreviewMarks (const juce::String& path, const TrackMarks& marks)
{
    auto& pd = previewWaves[path];
    if (pd.haveMarks && pd.marks == marks) return;
    pd.marks = marks;
    pd.haveMarks = true;
    table.repaint();
}

void BrowserView::requestPreviewWave (const TrackInfo& t)
{
    const auto path = t.file.getFullPathName();
    previewWaves[path] = {};   // placeholder while loading
    std::optional<TrackVersion> version;
    if (versionFor) version = versionFor (t);
    const auto anlz = t.anlzBase;
    const double length = t.durationSec;
    juce::Component::SafePointer<BrowserView> safe (this);
    previewPool.addJob ([safe, anlz, path, version, length]
    {
        // rekordbox's analysis if there is one, else the preview kept from Centrifuge's own analysis
        const auto data = anlz.existsAsFile() ? loadAnlz (anlz) : AnlzData();
        PreviewWave pw;
        if (data.waveforms.numDetailEntries() > 0)
            if (auto w = WaveformData::fromAnlz (data.waveforms, WaveformData::Style::threeBand)) pw = PreviewWaveCache::make (*w);
        if (pw.isEmpty()) PreviewWaveCache::load (path, pw);
        PreviewStems ps;
        PreviewWaveCache::loadStems (path, ps);
        const bool marksToo = anlz.existsAsFile() || version.has_value();
        TrackMarks marks;
        if (marksToo)
            marks = version ? TrackMarks::fromVersion (*version, length, data.phrases)
                            : TrackMarks::fromVersion (TrackVersion::fromRekordbox (data.beats, data.cues), length, data.phrases);
        juce::MessageManager::callAsync ([safe, pw, ps, path, marks, marksToo]
        {
            if (safe == nullptr) return;
            auto& pd = safe->previewWaves[path];
            if (! ps.isEmpty() && pd.stems[0].empty()) pd.stems = ps.stem;
            if (! pw.isEmpty() && pd.low.empty()) { pd.low = pw.low; pd.mid = pw.mid; pd.high = pw.high; }
            pd.missing = pd.low.empty();
            if (marksToo && ! pd.haveMarks) { pd.marks = marks; pd.haveMarks = true; }
            safe->table.repaint();
        });
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
    if (settingSort) return;
    // A column's header steps ascending > descending > off (off = the list's # order)
    if (columnId != 0 && columnId == sortColumn && ! sortForwards && forwards)
    {
        sortColumn = 0;
        sortForwards = true;
        const juce::ScopedValueSetter<bool> svs (settingSort, true);
        table.getHeader().setSortColumnId (0, true);
    }
    else
    {
        sortColumn = columnId;
        sortForwards = forwards;
    }
    applyFilter();
}

int BrowserView::stemQualityOf (const TrackInfo& t) const
{
    const auto path = t.file.getFullPathName();
    auto it = stemCache.find (path);
    if (it != stemCache.end()) return it->second;
    const int q = stemManager.cachedQuality (t);
    stemCache[path] = q;
    return q;
}

void BrowserView::saveOrderAsDefault()
{
    if (currentSource == nullptr || currentKey.isEmpty()) return;
    // the order shown now first (a search may hide some: they keep their order after)
    std::vector<int> order = rows;
    std::unordered_map<int, bool> shown;
    for (int i : rows) shown[i] = true;
    for (int i : allRows) if (! shown.count (i)) order.push_back (i);
    juce::Array<juce::var> paths;
    for (int i : order) paths.add (currentSource->tracks[(size_t) i].file.getFullPathName());
    if (auto* o = ListOrders::data().getDynamicObject()) o->setProperty (juce::Identifier (currentKey), paths);
    ListOrders::save();
    allRows = order;
    numbers.clear();
    for (int k = 0; k < (int) allRows.size(); ++k) numbers[allRows[(size_t) k]] = k + 1;
    sortColumn = 0;
    sortForwards = true;
    {
        const juce::ScopedValueSetter<bool> svs (settingSort, true);
        table.getHeader().setSortColumnId (0, true);
    }
    applyFilter();
}

void BrowserView::resetSavedOrder()
{
    if (currentKey.isEmpty()) return;
    if (auto* o = ListOrders::data().getDynamicObject()) o->removeProperty (juce::Identifier (currentKey));
    ListOrders::save();
    if (auto* sel = dynamic_cast<Item*> (tree.getSelectedItem (0)))
        showList (sel->src, sel->allTracks ? nullptr : sel->node, sel->name, sel->key);
}

void BrowserView::timerCallback()
{
    // Stem cache status can change in the background (batch analysis).
    stemCache.clear();
    table.repaint();
    // Column order / widths changed: keep them
    if (onColumnsChanged)
        if (const auto cs = getColumnState(); cs != lastColumnState) { lastColumnState = cs; onColumnsChanged (cs); }
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

void BrowserView::focusTracks()
{
    treeFocused = false;
    if (table.getSelectedRow() < 0 && ! rows.empty()) { table.selectRow (0); table.scrollToEnsureRowIsOnscreen (0); }
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
