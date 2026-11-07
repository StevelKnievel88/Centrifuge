#pragma once
#include "TrackMarks.h"
#include "../Library/LibraryManager.h"
#include "../Stems/StemManager.h"
#include "../Audio/WaveformData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <unordered_map>
#include <map>

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
    juce::TextButton load1 { "LOAD 1" }, load2 { "LOAD 2" };
    juce::TextButton importButton { "IMPORT" }, exportButton { "EXPORT" }, viewR { "REKORDBOX" }, viewC { "CENTRIFUGE" };
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
        TrackMarks marks;
        bool haveMarks = false;
    };
    std::map<juce::String, PreviewData> previewWaves;   // by file path
    juce::ThreadPool previewPool { 1 };
    void requestPreviewWave (const TrackInfo& t);
    int sortColumn = 0;
    bool sortForwards = true;
};

} // namespace orbit
