#pragma once
#include "../Library/LibraryManager.h"
#include "../Stems/StemManager.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <unordered_map>

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
    void loadSelected (int deck);

    std::vector<TrackInfo> getVisibleTracks() const;
    std::vector<TrackInfo> getSelectedTracks() const;
    juce::String getCurrentListName() const { return currentListName; }

    void resized() override;
    void paint (juce::Graphics&) override;

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
    void timerCallback() override;

    LibraryManager& library;
    StemManager& stemManager;

    std::vector<std::shared_ptr<const LibrarySource>> sources;
    std::shared_ptr<const LibrarySource> currentSource;
    std::vector<int> allRows, rows;   // indices into currentSource->tracks
    juce::String currentListName, currentKey;
    mutable std::unordered_map<juce::String, bool> stemCache;

    juce::TreeView tree;
    std::unique_ptr<juce::TreeViewItem> rootItem;
    juce::TableListBox table { "tracks", this };
    juce::TextEditor search;
    juce::TextButton load1 { "LOAD 1" }, load2 { "LOAD 2" };
    juce::TextButton importButton { "IMPORT" }, exportButton { "EXPORT" }, viewR { "REKORDBOX" }, viewC { "CENTRIFUGE" };
    int libView = 1;
    void setLibraryView (int v);
    juce::Label countLabel;
    bool treeFocused = false;
    bool dragHover = false;
    int sortColumn = 0;
    bool sortForwards = true;
};

} // namespace orbit
