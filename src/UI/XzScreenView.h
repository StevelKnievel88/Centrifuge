#pragma once
#include "ScrollingWaveforms.h"
#include "PhaseMeter.h"
#include "BrowserView.h"
#include "../Control/ActionRouter.h"
#include "../App/TrackEditor.h"

namespace orbit
{

/** What laptop mode shows on the XDJ-XZ's 800 x 480 screen: the phrase meter along the top, both decks' scrolling
    waveforms, and under them a panel per deck (title, BPM, artist, key, tempo, range, Q, times, overview with its
    grid button) - or the browser, when it's switched on. Never shown on the computer; it is painted into a
    picture for XzScreenLink. Touches work like the mouse on the computer: tap or drag the overviews, drag the
    waveforms (scrub), the playhead and the split between lanes, tap the lane buttons, the grid button and Q. */
class XzScreenView : public juce::Component
{
public:
    XzScreenView (AudioEngine& engine, ActionRouter& router, BrowserView& browser);

    /** Same lanes, colours and zoom as the play page's waveforms. */
    ScrollingWaveforms& getWaves() { return waves; }
    PhaseMeter& getPhaseMeter() { return meter; }
    void setOverviewStyle (int style, bool half, juce::Colour single, bool singleFollowsDeck);
    void setOverviewGrid (bool on) { overviewGrid = on; }
    std::function<void()> onGridToggled;
    /** For the EDITED button and the edit version menu, and the deck number's colours. */
    void setEditor (TrackEditor* e) { editor = e; }
    std::function<void (int deck)> onEditViewToggled;
    std::function<void (int deck, juce::Colour)> onDeckColour;
    /** The browser's SET LIST button: is one running; 0 = stop it, 1 = start + record, 2 = start without recording. */
    std::function<bool()> setListRunning;
    std::function<void (int choice)> onSetList;
    /** The search box was tapped: the computer's keyboard types into the browser's search. */
    std::function<void()> onSearchTapped;

    /** The browser instead of the deck panels (mappable: XDJ-XZ screen > browser on / off). */
    void setBrowserShown (bool b) { browserShown = b; }
    bool isBrowserShown() const { return browserShown; }

    /** The start-up animation and logo over the screen for 5 s (when streaming starts). */
    void startSplash() { splashStart = juce::Time::getMillisecondCounterHiRes() / 1000.0; splashAngle = 0.4f; splashLast = -1.0; }

    /** Advances the waveforms and paints the screen. */
    juce::Image render();

    /** A touch on the XZ screen: type 1 down, 3 moved, 2 up. */
    void touch (int type, int x, int y);

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

private:
    void paintInfo (juce::Graphics& g, int deck, juce::Rectangle<float> r);
    void paintBrowser (juce::Graphics& g, juce::Rectangle<int> r);
    void seekOverview (int deck, float x);
    void sendToWaves (int type, juce::Point<float> p);

    /** A menu drawn on the screen itself (a pop-up window can't be shown there): a list, or colour swatches. */
    struct Popup
    {
        struct Item { int id; juce::String text; juce::Colour colour; bool ticked = false; bool enabled = true; };
        bool open = false, swatches = false;
        juce::String title;
        juce::Rectangle<float> anchor;
        std::vector<Item> items;
        std::vector<juce::Rectangle<float>> rects;
        std::function<void (int id)> chosen;
    };
    void openPopup (juce::String title, juce::Rectangle<float> anchor, std::vector<Popup::Item> items, bool swatches,
                    std::function<void (int)> chosen);
    void paintPopup (juce::Graphics& g);
    void showLaneMenu (int slot, int lane, juce::Rectangle<int> label);
    void showDeckColourMenu (int deck, juce::Rectangle<float> badge);
    void showEditVersionMenu (int deck, juce::Rectangle<float> area);
    Popup popup;
    double splashStart = -1.0, splashLast = -1.0;
    float splashAngle = 0.4f;
    TrackEditor* editor = nullptr;
    std::array<juce::Rectangle<float>, 2> badgeRects, editViewRects, editVerRects;

    AudioEngine& engine;
    ActionRouter& router;
    BrowserView& browser;
    ScrollingWaveforms waves;
    PhaseMeter meter;
    juce::Rectangle<int> infoArea, browserArea;

    struct Overview { juce::Image image; int serial = -1, version = -1, fold = -1, style = -1; };
    std::array<Overview, 2> overviews;
    std::array<juce::Rectangle<float>, 2> overviewRects, gridRects, qRects;
    int overviewStyle = 1;
    bool overviewHalf = false, followDeck = false, overviewGrid = false, browserShown = false;
    juce::Colour singleColour;

    // the touch in progress: what it started on
    enum class Drag { none, overview, waves, browserList };
    Drag drag = Drag::none;
    int dragDeck = -1;
    juce::Point<float> downPos;
    juce::Time downTime;
    // browser page
    std::vector<juce::Rectangle<float>> browserRows, treeRows;
    std::vector<int> browserRowIndex, treeRowIndex;
    juce::Rectangle<float> load1Rect, load2Rect, closeRect, searchRect, filterRect, setListRect, infoRect, listArea, treeArea;
    float listDragY = 0.0f, listDragX = 0.0f, colScroll = 0.0f, colMaxScroll = 0.0f;
    std::vector<std::pair<int, float>> colEdges;   // the column header: each column's id and its right edge (on the screen)
    juce::Rectangle<float> colHeaderRect;
    int resizingCol = -1;
    float resizeStartW = 0.0f, resizeStartX = 0.0f;
    std::function<void()> onXzColumnsResized;
    int dragAxis = 0;              // browser drags: 0 not decided yet, 1 up / down (the selection), 2 sideways (the columns)
    bool dragInTree = false, infoShown = false;
    void paintBrowserInfo (juce::Graphics& g, juce::Rectangle<float> r);
    void showFilterMenu();
    void showSetListMenu();
};

} // namespace orbit
