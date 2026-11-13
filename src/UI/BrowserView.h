#pragma once
#include "ButtonStyles.h"
#include "TrackMarks.h"
#include "TrackInfoPanel.h"
#include "../Library/LibraryManager.h"
#include "../Stems/StemManager.h"
#include "../Audio/WaveformData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <unordered_map>
#include <map>
#include <set>

namespace orbit
{

/** rekordbox-style browser: sources and playlists on the left, tracks on the right. */
class BrowserView : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    private juce::TableListBoxModel,
                    private juce::Timer
{
public:
    BrowserView (LibraryManager& library, StemManager& stems);
    ~BrowserView() override;

    void refreshSources();

    /** deck, track, track comes from the PC collection */
    std::function<void (int, const TrackInfo&, bool)> onLoadToDeck;
    std::function<int()> chooseDeckForDoubleClick;

    /** Centrifuge's library as a source: view 0 = rekordbox copy, 1 = Centrifuge version. */
    std::function<std::shared_ptr<const LibrarySource> (int view)> centrifugeSource;
    std::function<void (juce::Component& anchor)> onImport, onExport;
    /** Audio files / folders dropped on the browser (added to Centrifuge's library). */
    std::function<void (const juce::Array<juce::File>&)> onFilesDropped;

    /** Preview: play this track from `fraction` (0..1) on the PREP / preview deck; stop it. */
    std::function<void (const TrackInfo&, bool fromPc, double fraction)> onPreview;
    std::function<void()> onPreviewStop;
    /** What the preview deck is playing (file path; empty = nothing) and where, for the preview column. */
    void setPreviewState (const juce::String& filePath, double fraction, bool playing);
    /** Short waveform for the preview column, from a loaded track (key = file path). */
    void notePreviewWaveform (const juce::String& filePath, const WaveformData& w);
    /** Hot cues, locators, skips and phrases for a track's preview (e.g. straight after an edit on a deck). */
    void notePreviewMarks (const juce::String& filePath, const TrackMarks& marks);
    /** The preview column's look: style 1 3-band, 2 RGB, 3 single colour, 4 stems; half = bars from the bottom. */
    void setPreviewStyle (int style, bool half, juce::Colour single) { previewStyle = style; previewHalf = half; previewTint = single; table.repaint(); }
    /** A track's stems preview (when its stems are made or loaded). */
    void notePreviewStems (const juce::String& filePath, const std::array<std::vector<uint8_t>, 4>& stems) { previewWaves[filePath].stems = stems; table.repaint(); }
    /** Looks a track's preview up again (after it was analysed or cleared). */
    void forgetPreview (const juce::String& filePath) { previewWaves.erase (filePath); table.repaint(); }
    /** The library version of a track, for its preview marks (empty when it isn't in the library). */
    std::function<std::optional<TrackVersion> (const TrackInfo&)> versionFor;

    /** Track menu actions: "load.0", "load.1", "load.2" (PREP), "remove.rb", "remove.cf", "plays.reset"; and ratings. */
    std::function<void (const std::vector<TrackInfo>&, const juce::String& action)> onTrackAction;
    std::function<void (const TrackInfo&, int stars)> onSetRating;
    /** The tracks being dragged out of the list (to a deck or PREP). */
    std::vector<TrackInfo> getDraggedTracks() const { return dragged; }
    bool draggedFromPc() const { return draggedPc; }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void paintOverChildren (juce::Graphics&) override;
    int getLibraryView() const { return libView; }
    /** The rekordbox sources (PC collection, USB sticks) currently known. */
    std::vector<std::shared_ptr<const LibrarySource>> getRekordboxSources() const;
    bool isShowingCentrifugeLibrary() const { return currentSource != nullptr && currentSource->kind == LibrarySource::Kind::centrifuge; }
    std::shared_ptr<const LibrarySource> getCurrentSource() const { return currentSource; }
    std::vector<int> getSelectedSourceIndices() const;

    // Hardware browsing
    void scroll (int delta);
    void enter();
    void back();
    void toggleFocus();
    /** The browse knob works on the track list straight away (a track is selected if none is). */
    void focusTracks();
    /** Puts the cursor in the search box (Ctrl+F). */
    void focusSearch() { search.grabKeyboardFocus(); search.selectAll(); }
    void loadSelected (int deck);
    /** Loads the track before / after (dir -1 / +1) the given one in the list onto a deck (the selection moves to it). */
    void loadNeighbour (int deck, const juce::File& current, int dir);

    std::vector<TrackInfo> getVisibleTracks() const;
    std::vector<TrackInfo> getSelectedTracks() const;
    juce::String getCurrentListName() const { return currentListName; }
    /** For the XDJ-XZ screen's browser: the list as shown, and its selection. */
    int getRowCount() const { return (int) rows.size(); }
    const TrackInfo* trackAtRow (int row) const
    {
        if (currentSource == nullptr || row < 0 || row >= (int) rows.size()) return nullptr;
        return &currentSource->tracks[(size_t) rows[(size_t) row]];
    }
    int getSelectedRowIndex() const { return table.getSelectedRow(); }
    void selectRowFromOutside (int row) { if (row >= 0 && row < (int) rows.size()) { table.selectRow (row); table.scrollToEnsureRowIsOnscreen (row); } }

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Column order and widths (to keep between sessions). */
    juce::String getColumnState() const { return table.getHeader().toString(); }
    void setColumnState (const juce::String& s) { if (s.isNotEmpty()) { table.getHeader().restoreFromString (s); lastColumnState = getColumnState(); } }
    std::function<void (const juce::String&)> onColumnsChanged;
    /** The fold-out info panel's edits (field, value) for the selected tracks, and "write to file tags". */
    std::function<void (const std::vector<TrackInfo>&, const juce::String& field, const juce::String& value)> onEditInfo;
    std::function<void (const std::vector<TrackInfo>&)> onWriteTags;
    void setInfoPanelOpen (bool open) { infoOpen = open; infoButton.setToggleState (open, juce::dontSendNotification); infoPanel.setVisible (open); if (open) infoPanel.setTracks (getSelectedTracks()); resized(); }
    std::function<void (bool)> onInfoPanelToggled;
    /** The browser's EXPAND button: fill the screen (over the decks / PREP) or go back. */
    std::function<void()> onExpandToggled;
    /** HIDE / SHOW on the browser's top bar: hidden, only that bar stays (so it can be shown again). */
    std::function<void()> onHideToggled;
    /** Titles and artists in their tag colour (the info panel's tick box). */
    inline static bool tagColourNames = false;
    void setTagColourNames (bool on) { tagColourNames = on; infoPanel.setTagColourNames (on); table.repaint(); }
    std::function<void (bool)> onTagColourNames;
    /** The preview's play / stop button (also mappable: Browser > Preview play / stop): stops a playing preview,
        or plays it again from where it stopped. */
    void previewPlayStop();
    void setCollapsed (bool c) { collapsed = c; hideButton.setButtonText (c ? "SHOW" : "HIDE"); resized(); repaint(); }
    bool isCollapsed() const { return collapsed; }
    static constexpr int barHeight = 42;   // the top bar (and the browser's height while hidden)
    void setExpanded (bool e) { expandButton.setToggleState (e, juce::dontSendNotification); expandButton.setButtonText (e ? "RESTORE" : "EXPAND"); }
  private:
    juce::String lastColumnState;
  public:

private:
    class Item;
    friend class Item;

    void showList (std::shared_ptr<const LibrarySource> src, const PlaylistNode* node, const juce::String& name, const juce::String& key);
    void applyFilter();

    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void sortOrderChanged (int columnId, bool forwards) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void showTrackMenu (int row);
    float cellFraction (int col, const juce::MouseEvent& e) const;
    void timerCallback() override;

    LibraryManager& library;
    StemManager& stemManager;

    std::vector<std::shared_ptr<const LibrarySource>> sources;
    std::shared_ptr<const LibrarySource> currentSource;
    std::vector<int> allRows, rows;   // indices into currentSource->tracks
    juce::String currentListName, currentKey;
    mutable std::unordered_map<juce::String, int> stemCache;   // stem quality of each file's cached stems (-1 = none)

    juce::TreeView tree;
    std::unique_ptr<juce::TreeViewItem> rootItem;
    juce::TableListBox table { "tracks", this };
    juce::TextEditor search;
    // (right-click any of them for its colours)
    StyledTextButton load1 { "LOAD 1", "browser.load1" }, load2 { "LOAD 2", "browser.load2" }, expandButton { "EXPAND", "browser.expand" },
                     infoButton { "INFO", "browser.info" }, hideButton { "HIDE", "browser.hide" };
    StyledTextButton filterButton { "FILTER", "browser.filter" }, rowsSmaller { "-", "browser.rows" }, rowsBigger { "+", "browser.rows" },
                     setListButton { "SET LIST", "browser.setlist" };
    void showFilterPanel();
    void showTreeMenu (Item& item);
    juce::String currentNodeId;
    std::map<int, juce::String> tagIds;   // track menu: tag item ids
    TrackInfoPanel infoPanel;
    bool infoOpen = false;
    void selectedRowsChanged (int) override { infoPanel.setTracks (getSelectedTracks()); }
    StyledTextButton importButton { "IMPORT", "browser.import" }, exportButton { "EXPORT", "browser.export" };
    bool collapsed = false;
    static constexpr float previewLead = 7.0f;   // room between the tag colour and the preview's play / stop button
    juce::TextButton viewR { "REKORDBOX" }, viewC { "CENTRIFUGE" };
    int libView = 1;
    void setLibraryView (int v);
    juce::Label countLabel;
    bool treeFocused = false;
    bool dragHover = false;
    std::vector<TrackInfo> dragged;
    bool draggedPc = false;
    juce::String previewPath;
    double previewFraction = 0.0;
    bool previewPlaying = false;
    struct PreviewData
    {
        std::vector<uint8_t> low, mid, high;   // empty = being loaded / no analysis
        bool missing = false;                  // looked: the track hasn't been analysed yet
        std::array<std::vector<uint8_t>, 4> stems;   // stem previews (empty = no stems yet)
        TrackMarks marks;
        bool haveMarks = false;
    };
    std::map<juce::String, PreviewData> previewWaves;   // by file path
    juce::ThreadPool previewPool { 1 };
    void requestPreviewWave (const TrackInfo& t);
    int previewStyle = 1;
    bool previewHalf = true;
    juce::Colour previewTint { 0xff3d9bff };
    void paintPreview (juce::Graphics& g, juce::Rectangle<float> area, const PreviewData& pd) const;
    int sortColumn = 0;
    bool sortForwards = true;
    bool settingSort = false;
    // # column: each track keeps its number in the list's own order (saved per list with "Save this order")
    std::unordered_map<int, int> numbers;   // track index -> number
    int numberOf (int trackIndex) const { auto it = numbers.find (trackIndex); return it != numbers.end() ? it->second : 0; }
    int stemQualityOf (const TrackInfo& t) const;
    class Header;
    friend class Header;
    friend class XzScreenView;   // the XZ screen's browser draws the same lists, columns and tree
  public:
    /** Columns shown on the XDJ-XZ screen's browser (its own list, chosen from the column header's right-click menu). */
    std::set<int> xzColumns { 2, 11, 3, 4, 5, 6 };
    std::function<void()> onXzColumnsChanged;
    juce::String getXzColumnState() const { juce::StringArray a; for (int c : xzColumns) a.add (juce::String (c)); return a.joinIntoString (","); }
    void setXzColumnState (const juce::String& s)
    {
        if (s.isEmpty()) return;
        xzColumns.clear();
        for (auto& t : juce::StringArray::fromTokens (s, ",", {})) if (t.getIntValue() > 0) xzColumns.insert (t.getIntValue());
    }
    /** The library tree for the XZ screen: rows as shown (open folders included). */
    int getTreeRowCount() const { return tree.getNumRowsInTree(); }
    int getTreeRowDepth (int row) const;
    bool treeRowIsFolder (int row) const;
    bool treeRowIsOpen (int row) const;
    bool treeRowIsSelected (int row) const;
    int getSelectedTreeRow() const { auto* s = tree.getSelectedItem (0); return s != nullptr ? s->getRowNumberInTree() : -1; }
    void paintTreeRow (juce::Graphics& g, int row, int w, int h);
    /** A tap on a tree row: selects it (a list shows its tracks), opens / closes a folder. */
    void tapTreeRow (int row);
    bool isTreeFocused() const { return treeFocused; }
    juce::String getSearchText() const { return search.getText(); }
    /** A tree list's menu action (new / rename / delete / duplicate / rules / analyse / stems / export / clear ...)
        on the list `nodeId` ("pl", "u12", "toplay", "hs.now", "loc", a location playlist's id ...), with its tracks. */
    std::function<void (const juce::String& nodeId, const juce::String& action, const std::vector<TrackInfo>& tracks)> onTreeAction;
    /** The key the master deck plays in (for the "compatible keys" filter). */
    std::function<juce::String()> masterKey;
    /** SET LIST button. */
    std::function<void()> onSetListToggled;
    void setSetListRunning (bool on) { setListButton.setToggleState (on, juce::dontSendNotification); setListButton.setButtonText (on ? "SET LIST ON" : "SET LIST"); }
    /** Row height (the preview column follows it). */
    std::function<void (int)> onRowHeightChanged;
    void setRowHeight (int h) { table.setRowHeight (juce::jlimit (20, 72, h)); table.repaint(); }
    /** Preview scrub for the highlighted track: a relative nudge (encoder), or a position 0..1 (dial). */
    void scrubPreview (double delta);
    void scrubPreviewTo (double fraction);

    struct Filter
    {
        double bpmMin = 0.0, bpmMax = 0.0;
        int keyMode = 0;          // 0 any, 1 compatible with the master deck's key
        int ratingMin = 0, colour = 0, stems = 0;
        juce::String genre, tag;
        bool any() const { return bpmMin > 0 || bpmMax > 0 || keyMode != 0 || ratingMin > 0 || colour > 0 || stems != 0 || genre.isNotEmpty() || tag.isNotEmpty(); }
    };
    Filter filter;
    void refilter() { applyFilter(); filterButton.setToggleState (filter.any(), juce::dontSendNotification); }

    /** Makes the current order (as sorted now) the list's # order, and turns sorting off. */
    void saveOrderAsDefault();
    void resetSavedOrder();
  private:
};

} // namespace orbit
