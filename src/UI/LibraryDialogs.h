#pragma once
#include "../Library/CentrifugeLibrary.h"
#include "../Export/Exporters.h"
#include "../Stems/StemManager.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace orbit
{

/** A dimmed overlay with a centred card (base for the library dialogs). */
class OverlayCard : public juce::Component
{
public:
    void paint (juce::Graphics& g) override;
    void resized() override;
    std::function<void()> onClose;

protected:
    void close() { setVisible (false); if (onClose) onClose(); }
    virtual void layoutCard (juce::Rectangle<int> card) = 0;
    juce::Rectangle<int> card;
    juce::String title;
    juce::Colour accentColour { 0xffffd339 };   // panel top border (function colour)
    int cardWidth = 640, cardHeight = 420;
};

/** Export to a rekordbox-compatible USB stick or a rekordbox XML file. */
class ExportView : public OverlayCard, private juce::Timer
{
public:
    struct Sources
    {
        std::function<std::vector<TrackInfo>()> currentList, selected;
        std::function<juce::String()> currentListName;
        std::function<juce::File()> defaultStick;
    };

    ExportView (CentrifugeLibrary& library, AudioFileLoader& loader, StemManager& stems, Sources sources);
    ~ExportView() override;

    /** toUsb: USB stick, otherwise rekordbox XML. */
    void open (bool toUsb);

    void paint (juce::Graphics&) override;

private:
    void layoutCard (juce::Rectangle<int> card) override;
    void timerCallback() override;
    void start();
    std::vector<ExportItem> collect (ExportList& playlists) const;

    CentrifugeLibrary& library;
    AudioFileLoader& loader;
    StemManager& stems;
    Sources sources;
    bool usb = true;

    juce::ComboBox what, version;
    juce::TextEditor path;
    juce::ToggleButton xzStems { "Also write stems for the XDJ-XZ (XZ Mods loader)" };
    juce::TextButton choose { "CHOOSE..." }, go { "EXPORT" }, cancel { "CLOSE" };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::ThreadPool pool { 1 };
    std::atomic<bool> running { false }, stop { false };
    std::atomic<float> progress { 0.0f };
    juce::CriticalSection statusLock;
    juce::String status, current;
    bool done = false, failed = false;
};

/** After an import: tracks you've edited in Centrifuge whose rekordbox data has also changed. */
class ConflictsView : public OverlayCard, private juce::ListBoxModel
{
public:
    explicit ConflictsView (CentrifugeLibrary& library);
    void show (std::vector<CentrifugeLibrary::Conflict> conflicts, const juce::String& importSummary);
    void paint (juce::Graphics&) override;

private:
    void layoutCard (juce::Rectangle<int> card) override;
    int getNumRows() override { return (int) conflicts.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void decide (int row, bool takeRekordbox);

    CentrifugeLibrary& library;
    std::vector<CentrifugeLibrary::Conflict> conflicts;
    std::vector<int> decided;   // 0 undecided, 1 kept mine, 2 took rekordbox's
    juce::String summary;
    juce::ListBox list { "conflicts", this };
    juce::TextButton keepAll { "KEEP ALL MINE" }, takeAll { "TAKE ALL FROM REKORDBOX" }, closeButton { "DONE" };
};

} // namespace orbit
