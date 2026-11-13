#include "XzScreenView.h"
#include "TrackMarks.h"
#include "Theme.h"
#include "../Audio/MusicalKey.h"
#include "ButtonStyles.h"
#include "ColourPicker.h"
#include "CentrifugeRotor.h"

namespace orbit
{

namespace
{
    juce::String formatTime (double sec)
    {
        const bool neg = sec < 0;
        sec = std::abs (sec);
        const int m = (int) (sec / 60.0);
        const double s = sec - m * 60.0;
        auto str = juce::String (m) + ":" + juce::String (s, 1).paddedLeft ('0', 4);
        return neg ? "-" + str : str;
    }

    constexpr int meterH = 20, infoH = 142;
}

XzScreenView::XzScreenView (AudioEngine& e, ActionRouter& r, BrowserView& b)
    : engine (e), router (r), browser (b), waves (e, { 0, 1 }), meter (e)
{
    setOpaque (true);
    addAndMakeVisible (waves);
    addAndMakeVisible (meter);
    meter.setInterceptsMouseClicks (false, false);
    // a finger on the screen never scrubs (the jog wheels do); the lane names open a menu drawn on the screen
    waves.touchMode = true;
    waves.onLaneLabelTapped = [this] (int slot, int lane, juce::Rectangle<int> label) { showLaneMenu (slot, lane, label); };
    setSize (800, 480);
    setVisible (true);
}

void XzScreenView::setOverviewStyle (int style, bool half, juce::Colour single, bool singleFollowsDeck)
{
    overviewStyle = style;
    overviewHalf = half;
    singleColour = single;
    followDeck = singleFollowsDeck;
    for (auto& o : overviews) o.style = -1;
}

void XzScreenView::seekOverview (int d, float x)
{
    const auto& r = overviewRects[(size_t) d];
    if (r.getWidth() <= 0.0f) return;
    auto& dk = engine.deck (d);
    if (dk.state().trackSerial.load() == 0) return;
    const double f = juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()));
    const auto& fold = waves.foldFor (d);
    if (fold.isEmpty()) dk.post (Deck::Cmd::seekFraction, f);
    else dk.post (Deck::Cmd::seekSeconds, fold.toSource (f * fold.displayLength (dk.state().lengthSec.load())));
}

void XzScreenView::sendToWaves (int type, juce::Point<float> p)
{
    // the screen's touches go to the waveforms as mouse presses / drags, so they work just like on the computer
    auto local = p - waves.getPosition().toFloat();
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    const auto downLocal = downPos - waves.getPosition().toFloat();
    juce::MouseEvent ev (src, local, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &waves, &waves, now, downLocal, downTime, 1, type != 1 && downPos.getDistanceFrom (p) > 3.0f);
    if (type == 1) waves.mouseDown (ev);
    else if (type == 3) waves.mouseDrag (ev);
    else waves.mouseUp (ev.withNewPosition (local));
}

void XzScreenView::touch (int type, int x, int y)
{
    const juce::Point<float> p ((float) x, (float) y);
    if (popup.open)
    {
        // a menu is up: a tap picks from it, anywhere else closes it (the rest of that touch does nothing)
        if (type != 1) return;
        drag = Drag::none;
        popup.open = false;
        for (size_t i = 0; i < popup.rects.size() && i < popup.items.size(); ++i)
            if (popup.rects[i].contains (p) && popup.items[i].enabled)
            {
                if (popup.chosen) popup.chosen (popup.items[i].id);
                break;
            }
        return;
    }
    if (type == 1)
    {
        downPos = p;
        downTime = juce::Time::getCurrentTime();
        drag = Drag::none;
        if (browserShown)
        {
            // tapping anywhere but the search box ends typing in it
            if (! searchRect.contains (p) && browser.search.hasKeyboardFocus (true)) browser.table.grabKeyboardFocus();
            // a column's edge in the header: drag to make it wider / narrower (the XZ's own widths)
            if (colHeaderRect.expanded (0.0f, 6.0f).contains (p))
                for (auto& [id, right] : colEdges)
                    if (std::abs (p.x - right) < 12.0f)
                    {
                        resizingCol = id;
                        resizeStartX = p.x;
                        resizeStartW = (float) (browser.xzColumnWidths.count (id) ? browser.xzColumnWidths[id] : 0);
                        if (resizeStartW <= 0.0f) resizeStartW = (float) juce::jlimit (40, 360, browser.table.getHeader().getColumnWidth (id));
                        drag = Drag::browserList;
                        dragAxis = 3;
                        return;
                    }
            if (load1Rect.contains (p)) { browser.loadSelected (0); return; }
            if (load2Rect.contains (p)) { browser.loadSelected (1); return; }
            if (closeRect.contains (p)) { browserShown = false; return; }
            if (searchRect.contains (p)) { if (onSearchTapped) onSearchTapped(); return; }
            if (filterRect.contains (p)) { showFilterMenu(); return; }
            if (setListRect.contains (p)) { showSetListMenu(); return; }
            if (infoRect.contains (p)) { infoShown = ! infoShown; return; }
            dragInTree = treeArea.contains (p);
            if (dragInTree)
            {
                for (size_t i = 0; i < treeRows.size(); ++i)
                    if (treeRows[i].contains (p)) { browser.tapTreeRow (treeRowIndex[i]); break; }
            }
            else
            {
                for (size_t i = 0; i < browserRows.size(); ++i)
                    if (browserRows[i].contains (p))
                    {
                        browser.treeFocused = false;
                        browser.selectRowFromOutside (browserRowIndex[i]);
                        break;
                    }
            }
            drag = Drag::browserList;
            dragAxis = 0;
            listDragY = p.y;
            listDragX = p.x;
            return;
        }
        for (int d = 0; d < 2; ++d)
        {
            if (gridRects[(size_t) d].contains (p)) { if (onGridToggled) onGridToggled(); return; }
            if (badgeRects[(size_t) d].contains (p)) { showDeckColourMenu (d, badgeRects[(size_t) d]); return; }
            if (editViewRects[(size_t) d].contains (p)) { if (onEditViewToggled) onEditViewToggled (d); return; }
            if (editVerRects[(size_t) d].contains (p)) { showEditVersionMenu (d, editVerRects[(size_t) d]); return; }
            if (qRects[(size_t) d].contains (p))
            {
                ActionValue av;
                av.down = true;
                av.value = 1.0;
                router.perform ("d" + juce::String (d + 1) + ".quantize", av);
                return;
            }
            if (overviewRects[(size_t) d].contains (p)) { drag = Drag::overview; dragDeck = d; seekOverview (d, p.x); return; }
        }
        if (waves.getBounds().toFloat().contains (p)) { drag = Drag::waves; sendToWaves (1, p); }
        return;
    }
    if (drag == Drag::overview && dragDeck >= 0) seekOverview (dragDeck, p.x);
    else if (drag == Drag::waves) sendToWaves (type, p);
    else if (drag == Drag::browserList && type == 3)
    {
        // up / down moves the selection a row at a time; sideways (the track list) shows the other columns
        if (dragAxis == 3 && resizingCol > 0)
        {
            browser.xzColumnWidths[resizingCol] = (int) juce::jlimit (30.0f, 600.0f, resizeStartW + (p.x - resizeStartX));
            return;
        }
        if (dragAxis == 0 && (std::abs (p.x - listDragX) > 8.0f || std::abs (p.y - listDragY) > 8.0f))
            dragAxis = (! dragInTree && std::abs (p.x - listDragX) > std::abs (p.y - listDragY)) ? 2 : 1;
        if (dragAxis == 2)
        {
            colScroll = juce::jlimit (0.0f, colMaxScroll, colScroll - (p.x - listDragX));
            listDragX = p.x;
        }
        else if (dragAxis == 1)
        {
            const float rowH = dragInTree ? 26.0f : 28.0f;
            const int steps = (int) ((listDragY - p.y) / rowH);
            if (steps != 0)
            {
                if (dragInTree)
                {
                    const int next = juce::jlimit (0, juce::jmax (0, browser.getTreeRowCount() - 1), browser.getSelectedTreeRow() + steps);
                    browser.treeFocused = true;
                    if (auto* it = browser.tree.getItemOnRow (next)) { it->setSelected (true, true); browser.tree.scrollToKeepItemVisible (it); }
                }
                else
                    browser.selectRowFromOutside (juce::jlimit (0, juce::jmax (0, browser.getRowCount() - 1), browser.getSelectedRowIndex() + steps));
                listDragY -= (float) steps * rowH;
            }
        }
    }
    if (type == 2)
    {
        if (dragAxis == 3 && resizingCol > 0 && browser.onXzColumnsChanged) browser.onXzColumnsChanged();
        resizingCol = -1;
        drag = Drag::none;
        dragDeck = -1;
    }
}

void XzScreenView::openPopup (juce::String title, juce::Rectangle<float> anchor, std::vector<Popup::Item> items, bool swatches,
                              std::function<void (int)> chosen)
{
    popup.open = true;
    popup.title = std::move (title);
    popup.anchor = anchor;
    popup.items = std::move (items);
    popup.swatches = swatches;
    popup.chosen = std::move (chosen);
    popup.rects.clear();
}

void XzScreenView::showLaneMenu (int slot, int lane, juce::Rectangle<int> label)
{
    std::vector<Popup::Item> items;
    const int cur = waves.laneStyle (slot, lane);
    for (auto& [id, name] : ScrollingWaveforms::laneStyleChoices (lane))
        items.push_back ({ id, name, {}, id == cur });
    openPopup (lane == 0 ? "Top lane" : "Second lane", (label + waves.getPosition()).toFloat(), std::move (items), false,
               [this, slot, lane] (int id) { if (waves.onLaneStyleChosen) waves.onLaneStyleChosen (waves.slotDeck (slot), lane, id); });
}

void XzScreenView::showDeckColourMenu (int deck, juce::Rectangle<float> badge)
{
    // the preset colours (the hot cue colours), and the deck's usual colour
    std::vector<Popup::Item> items;
    const auto current = Theme::deckColour (deck);
    const auto usual = deck == 0 ? psyched::Colours::cyan : psyched::Colours::pink;
    items.push_back ({ 1000, "DEFAULT", usual, current == usual });
    const auto& cols = rekordboxCueColours();
    for (int i = 0; i < (int) cols.size(); ++i)
        items.push_back ({ i, cols[(size_t) i].name, juce::Colour (cols[(size_t) i].argb), current == juce::Colour (cols[(size_t) i].argb) });
    openPopup ("Deck " + juce::String (deck + 1) + " colour", badge, std::move (items), true, [this, deck] (int id)
    {
        if (! onDeckColour) return;
        if (id == 1000) onDeckColour (deck, deck == 0 ? psyched::Colours::cyan : psyched::Colours::pink);
        else if (id >= 0 && id < (int) rekordboxCueColours().size()) onDeckColour (deck, juce::Colour (rekordboxCueColours()[(size_t) id].argb));
    });
}

void XzScreenView::showEditVersionMenu (int deck, juce::Rectangle<float> area)
{
    if (editor == nullptr || ! editor->hasTrack (deck)) return;
    const auto names = editor->editVersionNames (deck);
    const auto active = editor->activeEdit (deck);
    std::vector<Popup::Item> items;
    for (int i = 0; i < (int) names.size(); ++i)
        items.push_back ({ 10 + i, names[(size_t) i], {}, names[(size_t) i] == active });
    if (names.empty()) items.push_back ({ -1, "(none saved yet - make them on PREP)", {}, false, false });
    items.push_back ({ 1, "Original (as recorded)", {}, active.isEmpty() && ! editor->hasEdits (deck) });
    openPopup ("Edit version - what plays", area, std::move (items), false, [this, deck] (int id)
    {
        if (editor == nullptr) return;
        if (id == 1) editor->applyNoEdits (deck);
        else if (id >= 10) editor->applyEditVersion (deck, id - 10);
    });
}

void XzScreenView::paintOverChildren (juce::Graphics& g)
{
    if (popup.open) paintPopup (g);
    if (splashStart >= 0.0)
    {
        // the start-up animation, stretched to 5 s
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        const float e = (float) ((now - splashStart) * 3.0 / 5.0);
        const float dt = splashLast < 0.0 ? 0.0f : (float) juce::jlimit (0.0, 0.1, (now - splashLast) * 3.0 / 5.0);
        splashLast = now;
        splashAngle = std::fmod (splashAngle + CentrifugeSplash::spinSpeedAt (e) * dt * 16.0f, juce::MathConstants<float>::twoPi);
        if (e >= 3.0f) splashStart = -1.0;
        else
        {
            g.setColour (psyched::Colours::void_);
            if (e < 2.6f) g.fillAll();
            CentrifugeSplash::paintFrame (g, getLocalBounds().toFloat(), e, splashAngle);
        }
    }
}

void XzScreenView::paintPopup (juce::Graphics& g)
{
    // dim what's behind, then the menu next to what was tapped (kept on the screen)
    const auto screen = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (screen);
    const float titleH = 26.0f, pad = 6.0f;
    juce::Rectangle<float> box;
    popup.rects.clear();
    if (popup.swatches)
    {
        const int cols = 6;
        const float cw = 62.0f, chh = 40.0f;
        const int rows = ((int) popup.items.size() + cols - 1) / cols;
        box = { 0.0f, 0.0f, cols * cw + pad * 2.0f, titleH + rows * chh + pad * 2.0f };
    }
    else
    {
        const float rowH = 34.0f;
        float w = 220.0f;
        for (auto& it : popup.items) w = juce::jmax (w, Theme::font (16.0f).getStringWidthFloat (it.text) + 50.0f);
        box = { 0.0f, 0.0f, juce::jmin (w, screen.getWidth() - 20.0f), juce::jmin (screen.getHeight() - 12.0f, titleH + popup.items.size() * rowH + pad * 2.0f) };
    }
    // below what was tapped if it fits, otherwise above it; then kept inside the screen
    float x = popup.anchor.getX(), y = popup.anchor.getBottom() + 4.0f;
    if (y + box.getHeight() > screen.getBottom() - 4.0f) y = popup.anchor.getY() - 4.0f - box.getHeight();
    x = juce::jlimit (6.0f, screen.getRight() - 6.0f - box.getWidth(), x);
    y = juce::jlimit (6.0f, juce::jmax (6.0f, screen.getBottom() - 6.0f - box.getHeight()), y);
    box.setPosition (x, y);

    g.setColour (psyched::Colours::panel);
    g.fillRect (box);
    g.setColour (Theme::accent);
    g.drawRect (box, 1.5f);
    auto r = box.reduced (pad);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f, true));
    g.drawText (popup.title.toUpperCase(), r.removeFromTop (titleH), juce::Justification::centredLeft, true);

    if (popup.swatches)
    {
        const int cols = 6;
        const float cw = r.getWidth() / cols, chh = 40.0f;
        for (size_t i = 0; i < popup.items.size(); ++i)
        {
            auto c = juce::Rectangle<float> (r.getX() + (float) (i % cols) * cw, r.getY() + (float) (i / cols) * chh, cw, chh);
            popup.rects.push_back (c);
            auto sw = c.reduced (4.0f);
            const auto& it = popup.items[i];
            g.setColour (it.colour);
            g.fillRect (sw);
            if (it.ticked) { g.setColour (juce::Colours::white); g.drawRect (sw.expanded (2.0f), 2.0f); }
            if (it.id == 1000)
            {
                g.setColour (psyched::Colours::onSolid);
                g.setFont (Theme::font (10.0f, true));
                g.drawText ("DEFAULT", sw, juce::Justification::centred);
            }
        }
        return;
    }
    const float rowH = juce::jmin (34.0f, r.getHeight() / (float) juce::jmax<size_t> (1, popup.items.size()));
    for (auto& it : popup.items)
    {
        auto row = r.removeFromTop (rowH);
        popup.rects.push_back (row);
        if (it.ticked)
        {
            g.setColour (Theme::accent.withAlpha (0.2f));
            g.fillRect (row);
            g.setColour (Theme::accent);
            g.fillRect (row.withWidth (3.0f));
        }
        g.setColour (! it.enabled ? Theme::textFaint : it.ticked ? Theme::accent : Theme::text);
        g.setFont (Theme::font (16.0f, it.ticked));
        g.drawText (it.text, row.withTrimmedLeft (12.0f), juce::Justification::centredLeft, true);
        g.setColour (Theme::line.withAlpha (0.4f));
        g.fillRect (row.removeFromBottom (1.0f));
    }
}

void XzScreenView::resized()
{
    auto r = getLocalBounds();
    meter.setBounds (r.removeFromTop (meterH).reduced (6, 3));
    browserArea = r;
    infoArea = r.removeFromBottom (infoH);
    waves.setBounds (r);
}

juce::Image XzScreenView::render()
{
    waves.setVisible (! browserShown);
    waves.tick (juce::Time::getMillisecondCounterHiRes() / 1000.0);
    juce::Image img (juce::Image::RGB, getWidth(), getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g (img);
    paintEntireComponent (g, true);
    return img;
}

void XzScreenView::paint (juce::Graphics& g)
{
    g.fillAll (psyched::Colours::void_);
    if (browserShown)
    {
        paintBrowser (g, browserArea);
        return;
    }
    auto r = infoArea.toFloat();
    const float w = r.getWidth() / 2.0f;
    // each deck in a panel with its coloured top line and the slant, like on the computer
    paintInfo (g, 0, r.removeFromLeft (w).withTrimmedRight (1.0f));
    paintInfo (g, 1, r.withTrimmedLeft (1.0f));
}

void XzScreenView::paintBrowser (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    psyched::paintPanel (g, r, Theme::accent, 12.0f);
    r.removeFromTop (4.0f);

    // Top bar: the list, the search box (tap: type on the computer's keyboard), FILTER, SET LIST, INFO, LOAD 1 / 2, CLOSE
    auto head = r.removeFromTop (34.0f).reduced (10.0f, 4.0f);
    auto button = [&] (juce::Rectangle<float>& rect, float w, const juce::String& text, juce::Colour c, bool on, const juce::String& key)
    {
        rect = head.removeFromRight (w);
        head.removeFromRight (5.0f);
        paintStyledChip (g, rect, text, c, on, false, ButtonStyles::get().find (key));
        rect = rect.expanded (2.0f, 4.0f);
    };
    const bool setOn = setListRunning && setListRunning();
    button (closeRect, 56.0f, "CLOSE", Theme::text, false, "xz.close");
    button (load2Rect, 62.0f, "LOAD 2", Theme::deckColour (1), true, "browser.load2");
    button (load1Rect, 62.0f, "LOAD 1", Theme::deckColour (0), true, "browser.load1");
    button (infoRect, 46.0f, "INFO", Theme::accent, infoShown, "browser.info");
    button (setListRect, setOn ? 92.0f : 70.0f, setOn ? "SET LIST ON" : "SET LIST", Theme::bad, setOn, "browser.setlist");
    button (filterRect, 58.0f, "FILTER", Theme::sync, browser.filter.any(), "browser.filter");
    {
        // search
        auto sr = head.removeFromRight (juce::jmin (190.0f, head.getWidth() * 0.55f));
        head.removeFromRight (8.0f);
        const bool typing = browser.search.hasKeyboardFocus (true);
        g.setColour (psyched::Colours::field);
        g.fillRect (sr);
        g.setColour (typing ? Theme::accent : psyched::Colours::edge2);
        g.drawRect (sr, 1.0f);
        const auto text = browser.getSearchText();
        g.setFont (Theme::font (14.0f));
        g.setColour (text.isEmpty() ? Theme::textFaint : Theme::text);
        g.drawText (text.isEmpty() ? juce::String ("Search (tap, then type)") : text + (typing ? "|" : ""), sr.reduced (8.0f, 0.0f),
                    juce::Justification::centredLeft, true);
        searchRect = sr.expanded (2.0f, 4.0f);
    }
    g.setColour (Theme::text);
    g.setFont (Theme::font (16.0f, true));
    g.drawText (browser.getCurrentListName().isNotEmpty() ? browser.getCurrentListName() : juce::String ("Browser"), head, juce::Justification::centredLeft, true);

    r = r.reduced (6.0f, 2.0f);
    // the track's info down the right (INFO)
    if (infoShown)
    {
        auto info = r.removeFromRight (230.0f);
        r.removeFromRight (6.0f);
        paintBrowserInfo (g, info);
    }
    // the library tree on the left while the browser's focus is on it (the BACK button)
    treeRows.clear();
    treeRowIndex.clear();
    treeArea = {};
    if (browser.isTreeFocused())
    {
        treeArea = r.removeFromLeft (juce::jmin (260.0f, r.getWidth() * 0.4f));
        r.removeFromLeft (6.0f);
        g.setColour (psyched::Colours::field);
        g.fillRect (treeArea);
        g.setColour (Theme::accent.withAlpha (0.6f));
        g.drawRect (treeArea, 1.0f);
        const float th = 26.0f;
        auto tr = treeArea.reduced (2.0f);
        const int n = browser.getTreeRowCount(), visible = juce::jmax (1, (int) (tr.getHeight() / th));
        const int sel = browser.getSelectedTreeRow();
        const int first = juce::jlimit (0, juce::jmax (0, n - visible), (sel < 0 ? 0 : sel) - visible / 2);
        for (int i = first; i < juce::jmin (n, first + visible); ++i)
        {
            auto row = tr.removeFromTop (th);
            treeRows.push_back (row);
            treeRowIndex.push_back (i);
            const float indent = 6.0f + 14.0f * (float) browser.getTreeRowDepth (i);
            auto arrow = juce::Rectangle<float> (row.getX() + indent, row.getY(), 12.0f, th);
            if (browser.treeRowIsFolder (i))
            {
                juce::Path p;
                const auto c = arrow.getCentre();
                if (browser.treeRowIsOpen (i)) p.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
                else                           p.addTriangle (c.x - 2.0f, c.y - 4.0f, c.x - 2.0f, c.y + 4.0f, c.x + 3.0f, c.y);
                g.setColour (Theme::textDim);
                g.fillPath (p);
            }
            juce::Graphics::ScopedSaveState ss (g);
            const auto cell = row.withTrimmedLeft (indent + 14.0f);
            g.reduceClipRegion (cell.toNearestInt());
            g.setOrigin (cell.getTopLeft().toInt());
            browser.paintTreeRow (g, i, (int) cell.getWidth(), (int) th);
        }
    }

    // The track list: the columns chosen for the XZ (Mapping-style right-click on the browser's column header),
    // in the browser's order; drag sideways to see the ones that don't fit
    listArea = r;
    auto& hdr = browser.table.getHeader();
    std::vector<std::pair<int, float>> cols;
    float total = 0.0f;
    for (int i = 0; i < hdr.getNumColumns (false); ++i)
    {
        const int id = hdr.getColumnIdOfIndex (i, false);
        if (browser.xzColumns.count (id) == 0) continue;
        const float w = browser.xzColumnWidths.count (id) ? (float) browser.xzColumnWidths[id]
                                                          : (float) juce::jlimit (40, 360, hdr.getColumnWidth (id));
        cols.push_back ({ id, w });
        total += w;
    }
    if (cols.empty()) { cols.push_back ({ 2, 300.0f }); total = 300.0f; }
    // a single column wider than the list is shrunk to fit; otherwise the list scrolls sideways
    colMaxScroll = juce::jmax (0.0f, total - r.getWidth());
    colScroll = juce::jlimit (0.0f, colMaxScroll, colScroll);

    auto headRow = r.removeFromTop (22.0f);
    colHeaderRect = headRow;
    colEdges.clear();
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (headRow.toNearestInt());
        float x = headRow.getX() - colScroll;
        g.setFont (Theme::font (11.0f, true));
        for (auto& [id, w] : cols)
        {
            g.setColour (Theme::textDim);
            g.drawText (hdr.getColumnName (id).toUpperCase(), juce::Rectangle<float> (x + 4.0f, headRow.getY(), w - 8.0f, headRow.getHeight()),
                        juce::Justification::centredLeft, true);
            g.setColour (id == resizingCol ? Theme::accent : Theme::line);
            g.fillRect (x + w - (id == resizingCol ? 2.0f : 1.0f), headRow.getY() + 3.0f, id == resizingCol ? 3.0f : 1.0f, headRow.getHeight() - 6.0f);
            colEdges.push_back ({ id, x + w });
            x += w;
        }
        g.setColour (Theme::line);
        g.fillRect (headRow.removeFromBottom (1.0f));
    }
    if (colMaxScroll > 0.0f)
    {
        // where the columns are scrolled to
        auto bar = r.removeFromBottom (4.0f);
        g.setColour (Theme::line.withAlpha (0.5f));
        g.fillRect (bar);
        g.setColour (Theme::accent.withAlpha (0.8f));
        const float vis = r.getWidth() / total;
        g.fillRect (bar.getX() + bar.getWidth() * (colScroll / total), bar.getY(), bar.getWidth() * vis, bar.getHeight());
    }

    const float rowH = 28.0f;
    const int n = browser.getRowCount(), visible = juce::jmax (1, (int) (r.getHeight() / rowH));
    const int sel = browser.getSelectedRowIndex();
    const int first = juce::jlimit (0, juce::jmax (0, n - visible), (sel < 0 ? 0 : sel) - visible / 2);
    browserRows.clear();
    browserRowIndex.clear();
    juce::Graphics::ScopedSaveState listClip (g);
    g.reduceClipRegion (r.toNearestInt());
    for (int i = first; i < juce::jmin (n, first + visible); ++i)
    {
        auto row = r.removeFromTop (rowH);
        browserRows.push_back (row);
        browserRowIndex.push_back (i);
        if (i == sel)
        {
            g.setColour (Theme::accent.withAlpha (browser.isTreeFocused() ? 0.09f : 0.18f));
            g.fillRect (row);
            g.setColour (Theme::accent);
            g.fillRect (row.withWidth (3.0f));
        }
        float x = row.getX() - colScroll;
        for (auto& [id, w] : cols)
        {
            if (x + w > row.getX() && x < row.getRight())
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.setOrigin (juce::Point<float> (x, row.getY()).toInt());
                g.reduceClipRegion (0, 0, (int) w, (int) rowH);
                browser.paintCell (g, i, id, (int) w, (int) rowH, i == sel);
            }
            x += w;
        }
        g.setColour (Theme::line.withAlpha (0.5f));
        g.fillRect (row.removeFromBottom (1.0f));
    }
}

void XzScreenView::paintBrowserInfo (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (psyched::Colours::field);
    g.fillRect (r);
    g.setColour (Theme::accent.withAlpha (0.6f));
    g.drawRect (r, 1.0f);
    r = r.reduced (8.0f, 6.0f);
    const auto sel = browser.getSelectedTracks();
    g.setFont (Theme::font (12.0f, true));
    g.setColour (Theme::accent);
    g.drawText ("TRACK INFO", r.removeFromTop (18.0f), juce::Justification::centredLeft);
    if (sel.empty())
    {
        g.setColour (Theme::textFaint);
        g.drawText ("No track selected", r.removeFromTop (20.0f), juce::Justification::centredLeft);
        return;
    }
    const auto& t = sel.front();
    auto field = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
    {
        if (r.getHeight() < 30.0f) return;
        g.setFont (Theme::font (10.0f, true));
        g.setColour (Theme::textFaint);
        g.drawText (label, r.removeFromTop (13.0f), juce::Justification::centredLeft);
        g.setFont (Theme::font (14.0f));
        g.setColour (c);
        g.drawText (value.isNotEmpty() ? value : juce::String ("-"), r.removeFromTop (18.0f), juce::Justification::centredLeft, true);
        r.removeFromTop (3.0f);
    };
    field ("TITLE", t.title, Theme::text);
    field ("ARTIST", t.artist, Theme::text);
    field ("ALBUM", t.album, Theme::textDim);
    field ("GENRE", t.genre, Theme::textDim);
    field ("LABEL", t.label, Theme::textDim);
    const auto kc = MusicalKey::wheelColour (t.key);
    field ("KEY / BPM", MusicalKey::display (t.key) + (t.bpm > 0 ? "   " + juce::String (t.bpm, 2) + " BPM" : juce::String()),
           kc.isTransparent() ? Theme::good : kc);
    field ("RATING", t.rating > 0 ? juce::String::repeatedString (juce::String::charToString (0x2605), t.rating) : juce::String(), Theme::warn);
    field ("COMMENT", t.comment, Theme::textDim);
}

void XzScreenView::showFilterMenu()
{
    // the browser's filter, a tap per choice (each tap steps that one on; the menu stays up)
    auto& f = browser.filter;
    const char* colours[] = { "any", "pink", "red", "orange", "yellow", "green", "aqua", "blue", "purple" };
    std::vector<Popup::Item> items;
    items.push_back ({ 1, juce::String ("Key: ") + (f.keyMode == 1 ? "goes with the master deck" : "any"), {}, f.keyMode == 1 });
    items.push_back ({ 2, "Rating: " + (f.ratingMin > 0 ? juce::String (f.ratingMin) + (f.ratingMin < 5 ? " stars +" : " stars") : juce::String ("any")), {}, f.ratingMin > 0 });
    items.push_back ({ 3, juce::String ("Colour: ") + colours[juce::jlimit (0, 8, f.colour)], f.colour > 0 ? rekordboxTrackColour (f.colour) : juce::Colour(), f.colour > 0 });
    items.push_back ({ 4, juce::String ("Stems: ") + (f.stems == 1 ? "with stems" : f.stems == 2 ? "without stems" : "with or without"), {}, f.stems > 0 });
    items.push_back ({ 5, "Clear the filter", {}, false, f.any() });
    openPopup ("Filter (tap to step through)", filterRect, std::move (items), false, [this] (int id)
    {
        auto& ff = browser.filter;
        if (id == 1) ff.keyMode = ff.keyMode == 1 ? 0 : 1;
        else if (id == 2) ff.ratingMin = (ff.ratingMin + 1) % 6;
        else if (id == 3) ff.colour = (ff.colour + 1) % 9;
        else if (id == 4) ff.stems = (ff.stems + 1) % 3;
        else if (id == 5) { ff = {}; browser.refilter(); return; }
        browser.refilter();
        showFilterMenu();
    });
}

void XzScreenView::showSetListMenu()
{
    if (setListRunning && setListRunning())
    {
        openPopup ("Set list", setListRect, { { 0, "Stop the set list" }, { -1, "Keep it going" } }, false,
                   [this] (int id) { if (id == 0 && onSetList) onSetList (0); });
        return;
    }
    openPopup ("Start a set list (the tracks you play go into it)", setListRect,
               { { 1, "Set list + record" }, { 2, "Set list only" }, { -1, "Cancel" } }, false,
               [this] (int id) { if (id > 0 && onSetList) onSetList (id); });
}

void XzScreenView::paintInfo (juce::Graphics& g, int deck, juce::Rectangle<float> panel)
{
    auto& dk = engine.deck (deck);
    const auto& st = dk.state();
    auto track = st.trackSerial.load() != 0 ? dk.getLoadedTrack() : nullptr;
    const bool loaded = track != nullptr;
    const auto col = Theme::deckColour (deck);

    psyched::paintPanel (g, panel, col, 10.0f);
    // the number square sits the same distance from the top and the left (clear of the slanted corner)
    const float edge = 8.0f;
    auto r = panel.reduced (edge, 0.0f).withTrimmedTop (edge).withTrimmedBottom (4.0f);
    const auto p = "d" + juce::String (deck + 1) + ".";
    auto chipStyle = [&] (const juce::String& action) { return ButtonStyles::get().find (action); };

    // Title row: deck number (tap: its colour), title, BPM
    auto top = r.removeFromTop (24.0f);
    auto badge = top.removeFromLeft (24.0f);
    g.setColour (col);
    g.fillRect (badge);
    g.setColour (psyched::Colours::onSolid);
    g.setFont (Theme::display (15.0f, true));
    g.drawText (juce::String (deck + 1), badge, juce::Justification::centred);
    badgeRects[(size_t) deck] = badge.expanded (6.0f);
    top.removeFromLeft (8.0f);
    auto bpmArea = top.removeFromRight (96.0f);
    const double bpm = st.bpm.load();
    g.setColour (st.master.load() ? Theme::accent : Theme::text);
    g.setFont (Theme::mono (24.0f, true));
    g.drawText (loaded && bpm > 0 ? juce::String (bpm, 2) : "--.--", bpmArea, juce::Justification::centredRight);
    g.setColour (loaded ? Theme::text : Theme::textFaint);
    g.setFont (Theme::font (18.0f, true));
    g.drawText (loaded ? track->info.title : juce::String ("No track loaded"), top, juce::Justification::centredLeft, true);
    r.removeFromTop (2.0f);

    // Artist, key, tempo and its range
    auto second = r.removeFromTop (20.0f);
    auto range = second.removeFromRight (54.0f);
    {
        const double rr = st.pitchRange.load();
        g.setColour (Theme::textDim);
        g.setFont (Theme::mono (14.0f));
        g.drawText (rr >= 0.99 ? juce::String ("WIDE") : juce::String::charToString (0xb1) + juce::String ((int) std::lround (rr * 100.0)) + "%",
                    range, juce::Justification::centredRight);
    }
    auto tempo = second.removeFromRight (80.0f);
    const double pct = (st.tempoRatio.load() - 1.0) * 100.0;
    g.setColour (std::abs (pct) < 0.005 ? Theme::textDim : Theme::text);
    g.setFont (Theme::mono (16.0f, true));
    g.drawText ((pct >= 0 ? "+" : "") + juce::String (pct, 2) + "%", tempo, juce::Justification::centredRight);
    if (loaded && track->info.key.isNotEmpty())
    {
        auto keyBox = second.removeFromRight (64.0f).reduced (3.0f, 0.0f);
        const int shift = st.keyShift.load();
        auto keyText = MusicalKey::display (router.effectiveKeyText (deck));
        if (shift != 0) keyText << " " << (shift > 0 ? "+" : "") << shift;
        const auto kc = shift != 0 ? Theme::warn : Theme::good;
        g.setColour (psyched::Colours::field);
        g.fillRect (keyBox);
        g.setColour (kc);
        g.drawRect (keyBox, 1.0f);
        g.setFont (Theme::font (13.0f, true));
        g.drawText (keyText, keyBox, juce::Justification::centred);
    }
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (16.0f));
    if (loaded) g.drawText (track->info.artist, second.withTrimmedLeft (32.0f), juce::Justification::centredLeft, true);
    r.removeFromTop (1.0f);

    // Times (the same size, sitting clear of the overview), MASTER, EDITED and the edit version, Q
    TimeFold fold = waves.foldFor (deck);
    fold.reference = fold.toDisplay (st.positionSec.load(), st.playSegment.load());
    const double len = st.lengthSec.load();
    const double dlen = juce::jmax (0.001, fold.displayLength (len));
    auto times = r.removeFromTop (24.0f);
    {
        auto t = times.translated (0.0f, -2.0f);
        g.setColour (Theme::text);
        g.setFont (Theme::mono (20.0f, true));
        g.drawText (loaded ? formatTime (fold.reference) : "0:00.0", t.removeFromLeft (76.0f), juce::Justification::centredLeft);
        const bool warn = st.endWarning.load() && (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
        g.setColour (warn ? Theme::bad : Theme::textDim);
        g.drawText (loaded ? formatTime (-(dlen - fold.reference)) : "-0:00.0", t.removeFromLeft (88.0f), juce::Justification::centredLeft);
        times.removeFromLeft (166.0f);
    }
    auto chips = times.reduced (0.0f, 2.0f).translated (0.0f, -1.0f);
    {
        auto q = chips.removeFromRight (24.0f);
        paintStyledChip (g, q, "Q", Theme::bad.withSaturation (0.6f), st.quantize.load(), false, chipStyle (p + "quantize"));
        qRects[(size_t) deck] = q.expanded (4.0f);
        chips.removeFromRight (4.0f);
    }
    {
        auto m = chips.removeFromLeft (54.0f);
        paintStyledChip (g, m, "MASTER", Theme::accent, st.master.load(), false, chipStyle (p + "master"));
        chips.removeFromLeft (4.0f);
        const bool ev = waves.getEditView (deck);
        auto e = chips.removeFromLeft (54.0f);
        paintStyledChip (g, e, "EDITED", Theme::sync, ev, false, chipStyle (p + "editview"));
        editViewRects[(size_t) deck] = e.expanded (2.0f, 4.0f);
        chips.removeFromLeft (4.0f);
        editVerRects[(size_t) deck] = {};
        if (loaded && editor != nullptr && chips.getWidth() > 30.0f)
        {
            auto name = editor->activeEdit (deck);
            const bool any = editor->hasEdits (deck);
            juce::String text = name.isNotEmpty() ? name : (any ? juce::String ("UNSAVED") : juce::String ("ORIGINAL"));
            if (name.isNotEmpty() && editor->activeEditModified (deck)) text << " *";
            paintStyledChip (g, chips, text + " " + juce::String::charToString (0x25be), Theme::sync.brighter (0.3f), name.isNotEmpty() || any, false,
                             chipStyle (p + "editver"));
            editVerRects[(size_t) deck] = chips.expanded (2.0f, 4.0f);
        }
    }

    // Overview, with the grid button on its left
    r.removeFromTop (2.0f);
    gridRects[(size_t) deck] = {};
    auto ov = r;
    overviewRects[(size_t) deck] = ov;
    g.setColour (juce::Colours::black);
    g.fillRect (ov);
    if (! loaded || track->waveform == nullptr) return;

    // the waveform fits between the bar strip along the top (grid on) and the phrase strip along the bottom
    const float barStrip = 7.0f;   // the bar grid's strip, always shown
    const float phraseStrip = track->phrases.empty() ? 0.0f : overviewStripHeight (ov.getHeight(), true);
    auto waveArea = ov.withTrimmedTop (barStrip > 0.0f ? barStrip + 1.0f : 0.0f).withTrimmedBottom (phraseStrip > 0.0f ? phraseStrip + 1.0f : 0.0f);
    auto& o = overviews[(size_t) deck];
    const int pw = (int) waveArea.getWidth(), ph = (int) waveArea.getHeight();
    if (o.serial != st.trackSerial.load() || o.version != st.trackDataVersion.load() || o.fold != fold.version
        || o.style != overviewStyle || o.image.getWidth() != pw || o.image.getHeight() != ph)
    {
        o.serial = st.trackSerial.load();
        o.version = st.trackDataVersion.load();
        o.fold = fold.version;
        o.style = overviewStyle;
        // the same look as the deck's overview on the computer
        const auto wdStyle = overviewStyle == 2 ? WaveformData::Style::rgb
                           : overviewStyle == 3 ? WaveformData::Style::blue : WaveformData::Style::threeBand;
        auto wd = wdStyle == WaveformData::Style::blue ? track->waveformForSingleColour() : track->waveformInStyle (wdStyle);
        StripOptions so;
        so.overview = true;
        so.shape = overviewHalf ? StripOptions::halfUp : StripOptions::full;
        so.tint = overviewStyle == 3 ? (followDeck ? col : singleColour) : juce::Colour();
        so.fold = &fold;
        so.entriesPerSecond = wd->entriesPerSecond;
        const double entries = fold.isEmpty() ? (double) wd->size() : fold.displayLength (wd->lengthSeconds()) * wd->entriesPerSecond;
        o.image = renderWaveformStrip (*wd, 0.0, entries / juce::jmax (1, pw), pw, ph, so);
    }
    g.drawImage (o.image, waveArea);
    if (len <= 0.0) return;

    const float px = ov.getX() + (float) juce::jlimit (0.0, 1.0, fold.reference / dlen) * ov.getWidth();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (ov.getX(), ov.getY(), px - ov.getX(), ov.getHeight()));
    paintOverviewGrid (g, ov, track->grid, len, &fold, track->phrases.empty() ? 0.0f : overviewStripHeight (ov.getHeight(), true), barStrip);
    paintOverviewMarks (g, ov, TrackMarks::fromDeck (st, track->phrases, track.get()), true, &fold);
    g.setColour (juce::Colours::white);
    g.fillRect (juce::Rectangle<float> (px - 1.0f, ov.getY(), 2.0f, ov.getHeight()));
}

} // namespace orbit
