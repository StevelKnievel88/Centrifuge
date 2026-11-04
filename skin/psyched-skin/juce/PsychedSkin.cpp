#include "PsychedSkin.h"
#include "PsychedSkinFonts.h"

namespace psyched
{

juce::Colour colourFor (Role r)
{
    switch (r)
    {
        case Role::input:      return Colours::green;
        case Role::layer:      return Colours::cyan;
        case Role::map:        return Colours::mint;
        case Role::active:     return Colours::yellow;
        case Role::output:     return Colours::pink;
        case Role::modulation: return Colours::violet;
        case Role::sync:       return Colours::blue;
        case Role::alert:      return Colours::orange;
        case Role::warm:       return Colours::amber;
    }
    return Colours::yellow;
}

//==============================================================================
namespace Fonts
{
    namespace
    {
        juce::Typeface::Ptr load (const char* data, int size)
        {
            return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }

        struct Faces
        {
            juce::Typeface::Ptr display600 = load (PsychedSkinFonts::UnboundedSemiBold_ttf, PsychedSkinFonts::UnboundedSemiBold_ttfSize);
            juce::Typeface::Ptr display800 = load (PsychedSkinFonts::UnboundedExtraBold_ttf, PsychedSkinFonts::UnboundedExtraBold_ttfSize);
            juce::Typeface::Ptr body400    = load (PsychedSkinFonts::ManropeRegular_ttf, PsychedSkinFonts::ManropeRegular_ttfSize);
            juce::Typeface::Ptr body600    = load (PsychedSkinFonts::ManropeSemiBold_ttf, PsychedSkinFonts::ManropeSemiBold_ttfSize);
            juce::Typeface::Ptr body800    = load (PsychedSkinFonts::ManropeExtraBold_ttf, PsychedSkinFonts::ManropeExtraBold_ttfSize);
            juce::Typeface::Ptr mono500    = load (PsychedSkinFonts::JetBrainsMonoMedium_ttf, PsychedSkinFonts::JetBrainsMonoMedium_ttfSize);
            juce::Typeface::Ptr mono600    = load (PsychedSkinFonts::JetBrainsMonoSemiBold_ttf, PsychedSkinFonts::JetBrainsMonoSemiBold_ttfSize);
        };

        Faces& faces()
        {
            static Faces f;
            return f;
        }

        juce::Font make (juce::Typeface::Ptr t, float height)
        {
            return juce::Font (juce::FontOptions (height).withTypeface (t));
        }
    }

    juce::Typeface::Ptr typeface (const juce::String& family, Weight w)
    {
        auto& f = faces();
        if (family.containsIgnoreCase ("mono")) return w == Weight::regular ? f.mono500 : f.mono600;
        if (family.containsIgnoreCase ("display") || family.containsIgnoreCase ("unbounded"))
            return w == Weight::heavy ? f.display800 : f.display600;
        return w == Weight::heavy ? f.body800 : (w == Weight::semibold ? f.body600 : f.body400);
    }

    juce::Font display (float h, Weight w) { return make (typeface ("display", w), h); }
    juce::Font body (float h, Weight w)    { return make (typeface ("body", w), h); }
    juce::Font mono (float h, bool semi)   { return make (typeface ("mono", semi ? Weight::semibold : Weight::regular), h); }

    juce::Font label (float h) { return mono (h, true).withExtraKerningFactor (0.14f); }
    juce::Font value (float h) { return mono (h, false); }
    juce::Font title (float h) { return display (h, Weight::semibold).withExtraKerningFactor (0.18f); }
    juce::Font chip (float h)  { return mono (h, true).withExtraKerningFactor (0.12f); }
}

//==============================================================================
juce::Path chamferPath (juce::Rectangle<float> r, float c, bool tl, bool br)
{
    c = juce::jmin (c, r.getWidth() * 0.5f, r.getHeight() * 0.5f);
    juce::Path p;
    p.startNewSubPath (r.getX() + (tl ? c : 0.0f), r.getY());
    p.lineTo (r.getRight(), r.getY());
    p.lineTo (r.getRight(), r.getBottom() - (br ? c : 0.0f));
    p.lineTo (r.getRight() - (br ? c : 0.0f), r.getBottom());
    p.lineTo (r.getX(), r.getBottom());
    p.lineTo (r.getX(), r.getY() + (tl ? c : 0.0f));
    p.closeSubPath();
    return p;
}

void paintBackdrop (juce::Graphics& g, juce::Rectangle<int> area, juce::Rectangle<int> whole)
{
    if (whole.isEmpty()) whole = area;
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (area);

    g.setColour (Colours::void_);
    g.fillRect (area);

    // radial-gradient(ellipse 60% 50% at 45% 32%, #140b2a 0%, #07060d 65%)
    const auto w = (float) whole.getWidth(), h = (float) whole.getHeight();
    const juce::Point<float> c ((float) whole.getX() + w * 0.45f, (float) whole.getY() + h * 0.32f);
    const float rx = w * 0.6f, ry = h * 0.5f;
    {
        juce::Graphics::ScopedSaveState s2 (g);
        g.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, c.x, c.y));
        juce::ColourGradient grad (Colours::glowCore, c.x, c.y, Colours::void_, c.x + rx * 0.65f, c.y, true);
        g.setGradientFill (grad);
        g.fillRect (whole.toFloat().withHeight (whole.getHeight() * (rx / ry) + 1.0f)
                         .withY (c.y - (c.y - (float) whole.getY()) * (rx / ry)));
    }

    // 40px grid
    g.setColour (juce::Colour (110, 140, 255).withAlpha (0.045f));
    for (int x = whole.getX(); x < area.getRight(); x += (int) Shape::grid)
        if (x >= area.getX()) g.fillRect (x, area.getY(), 1, area.getHeight());
    for (int y = whole.getY(); y < area.getBottom(); y += (int) Shape::grid)
        if (y >= area.getY()) g.fillRect (area.getX(), y, area.getWidth(), 1);
}

void paintHeader (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d0b17), 0, r.getY(), juce::Colour (0xff08070f), 0, r.getBottom(), false));
    g.fillRect (r);
    g.setColour (Colours::header);
    g.fillRect (r.withTop (r.getBottom() - 1.0f));
}

void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour top, float cut)
{
    auto p = chamferPath (r, cut);
    g.setGradientFill (juce::ColourGradient (Colours::panel, 0, r.getY(), Colours::void2, 0, r.getBottom(), false));
    g.fillPath (p);
    g.setColour (Colours::edge);
    g.strokePath (chamferPath (r.reduced (0.5f), cut), juce::PathStrokeType (1.0f));

    // 2px top border in the panel's function colour (follows the chamfer)
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (r.withHeight (Shape::topBorder + cut).toNearestInt());
    g.reduceClipRegion (p);
    g.setColour (top);
    juce::Path line;
    line.startNewSubPath (r.getX() + cut * 0.0f, r.getY() + cut + 1.0f);
    line.lineTo (r.getX() + cut + 1.0f, r.getY());
    line.lineTo (r.getRight(), r.getY());
    g.strokePath (line, juce::PathStrokeType (Shape::topBorder * 2.0f));
}

void paintScanlines (juce::Graphics& g, juce::Rectangle<float> r, float alpha)
{
    g.setColour (juce::Colours::white.withAlpha (alpha));
    for (float y = std::floor (r.getY()); y < r.getBottom(); y += 3.0f)
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
}

void paintWell (juce::Graphics& g, juce::Rectangle<float> r, bool scan)
{
    g.setColour (Colours::field);
    g.fillRect (r);
    if (scan) paintScanlines (g, r);
    g.setColour (Colours::edge);
    g.drawRect (r, 1.0f);
}

void paintGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, int radius)
{
    juce::DropShadow (c, radius, {}).drawForPath (g, p);
}

void paintLed (juce::Graphics& g, juce::Point<float> centre, juce::Colour c, bool on, float size)
{
    auto r = juce::Rectangle<float> (size, size).withCentre (centre);
    if (on)
    {
        juce::Path p;
        p.addEllipse (r);
        paintGlow (g, p, c, (int) size);
    }
    g.setColour (on ? c : Colours::ledOff);
    g.fillEllipse (r);
}

void paintChip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour c, ChipState st, bool enabled, float cut)
{
    const float a = enabled ? 1.0f : 0.4f;
    auto shape = chamferPath (r, cut);
    auto inner = chamferPath (r.reduced (0.5f), cut);

    if (st == ChipState::solid)
    {
        paintGlow (g, shape, c.withAlpha (0.6f * a), 14);
        g.setColour (c.withMultipliedAlpha (a));
        g.fillPath (shape);
    }
    else
    {
        g.setColour (Colours::field);
        g.fillPath (shape);
        if (st == ChipState::on)
        {
            // inset 0 0 10px colour@30%
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (shape);
            juce::Path ring;
            ring.addRectangle (r.expanded (20.0f));
            ring.addPath (shape);
            ring.setUsingNonZeroWinding (false);
            juce::DropShadow (c.withAlpha (0.3f * a), 10, {}).drawForPath (g, ring);
        }
        g.setColour ((st == ChipState::on ? c : (st == ChipState::hover ? Colours::edgeHot : Colours::edge2)).withMultipliedAlpha (a));
        g.strokePath (inner, juce::PathStrokeType (1.0f));
    }

    if (text.isNotEmpty())
    {
        g.setFont (Fonts::chip (juce::jlimit (9.0f, 12.0f, r.getHeight() * 0.38f)));
        g.setColour ((st == ChipState::solid ? Colours::onSolid : (st == ChipState::on ? c : Colours::chipInk)).withMultipliedAlpha (a));
        g.drawFittedText (text.toUpperCase(), r.reduced (6.0f, 1.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
}

void paintLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c, float h)
{
    g.setFont (Fonts::label (h));
    g.setColour (c);
    g.drawText (t.toUpperCase(), r, j, true);
}

void paintTitle (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c, float h)
{
    g.setFont (Fonts::title (h));
    g.setColour (c);
    g.drawText (t.toUpperCase(), r, j, true);
}

void paintStatus (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Colour c, bool on)
{
    paintLed (g, { r.getX() + 4.0f, r.getCentreY() }, c, on);
    paintLabel (g, t, r.withTrimmedLeft (14.0f), juce::Justification::centredLeft, on ? Colours::ink2 : Colours::dim);
}

float paintWordmark (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, int sig, Signature kind, float h,
                     juce::Justification just)
{
    const auto font = Fonts::display (h, Fonts::Weight::heavy).withExtraKerningFactor (0.14f);

    juce::GlyphArrangement ga;
    ga.addLineOfText (font, text.toUpperCase(), 0.0f, 0.0f);
    auto bounds = ga.getBoundingBox (0, -1, true);
    // drop the trailing letter-spacing so the mark centres on its ink
    const float width = bounds.getWidth() - font.getHeight() * 0.14f * 0.5f;

    float x = r.getX();
    if (just.testFlags (juce::Justification::horizontallyCentred)) x = r.getCentreX() - width * 0.5f;
    else if (just.testFlags (juce::Justification::right)) x = r.getRight() - width;
    const float baseline = r.getCentreY() + (font.getAscent() - font.getDescent()) * 0.5f - font.getHeight() * 0.06f;
    ga.moveRangeOfGlyphs (0, -1, x, baseline);

    juce::Path all, plain, special;
    for (int i = 0; i < ga.getNumGlyphs(); ++i)
    {
        juce::Path gp;
        ga.getGlyph (i).createPath (gp);
        if (i == sig && kind != Signature::none)
        {
            auto gb = ga.getGlyph (i).getBounds();
            gp.applyTransform (kind == Signature::mirror ? juce::AffineTransform::scale (-1.0f, 1.0f, gb.getCentreX(), gb.getCentreY())
                                                         : juce::AffineTransform::rotation (juce::MathConstants<float>::pi, gb.getCentreX(), gb.getCentreY()));
            special.addPath (gp);
        }
        else
            plain.addPath (gp);
        all.addPath (gp);
    }

    // text-shadow: 0 0 14px rgba(255,63,160,.8), 0 0 2px pink
    paintGlow (g, all, Colours::pink.withAlpha (0.8f), 14);
    paintGlow (g, all, Colours::pink, 2);
    g.setColour (juce::Colours::white);
    g.fillPath (plain);
    g.setColour (Colours::pink);
    g.fillPath (special);
    return width;
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    using namespace Colours;
    setColour (juce::ResizableWindow::backgroundColourId, void_);
    setColour (juce::DocumentWindow::textColourId, ink);
    setColour (juce::Label::textColourId, ink);
    setColour (juce::TextButton::buttonColourId, field);
    setColour (juce::TextButton::buttonOnColourId, yellow);
    setColour (juce::TextButton::textColourOffId, chipInk);
    setColour (juce::TextButton::textColourOnId, yellow);
    setColour (juce::ComboBox::backgroundColourId, field);
    setColour (juce::ComboBox::outlineColourId, edge2);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::ComboBox::focusedOutlineColourId, yellow);
    setColour (juce::PopupMenu::backgroundColourId, menu);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::headerTextColourId, dim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, hover);
    setColour (juce::PopupMenu::highlightedTextColourId, ink);
    setColour (juce::TextEditor::backgroundColourId, field);
    setColour (juce::TextEditor::outlineColourId, edge2);
    setColour (juce::TextEditor::focusedOutlineColourId, yellow);
    setColour (juce::TextEditor::textColourId, ink);
    setColour (juce::TextEditor::highlightColourId, yellow.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, ink);
    setColour (juce::CaretComponent::caretColourId, yellow);
    setColour (juce::ListBox::backgroundColourId, void2);
    setColour (juce::ListBox::outlineColourId, edge);
    setColour (juce::ListBox::textColourId, ink);
    setColour (juce::TreeView::backgroundColourId, void2);
    setColour (juce::TreeView::selectedItemBackgroundColourId, yellow.withAlpha (0.12f));
    setColour (juce::TreeView::linesColourId, edge);
    setColour (juce::ScrollBar::thumbColourId, edge2);
    setColour (juce::ScrollBar::trackColourId, field);
    setColour (juce::TableHeaderComponent::backgroundColourId, void2);
    setColour (juce::TableHeaderComponent::textColourId, dim);
    setColour (juce::TableHeaderComponent::outlineColourId, edge2);
    setColour (juce::TableHeaderComponent::highlightColourId, hover);
    setColour (juce::ToggleButton::textColourId, ink);
    setColour (juce::ToggleButton::tickColourId, green);
    setColour (juce::ToggleButton::tickDisabledColourId, edge2);
    setColour (juce::Slider::trackColourId, yellow);
    setColour (juce::Slider::backgroundColourId, juce::Colour (0xff2a2546));
    setColour (juce::Slider::thumbColourId, ink);
    setColour (juce::Slider::rotarySliderFillColourId, yellow);
    setColour (juce::Slider::rotarySliderOutlineColourId, edge2);
    setColour (juce::Slider::textBoxTextColourId, ink);
    setColour (juce::Slider::textBoxBackgroundColourId, field);
    setColour (juce::Slider::textBoxOutlineColourId, edge2);
    setColour (juce::Slider::textBoxHighlightColourId, yellow.withAlpha (0.35f));
    setColour (juce::GroupComponent::outlineColourId, edge);
    setColour (juce::GroupComponent::textColourId, dim);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, ink);
    setColour (juce::AlertWindow::outlineColourId, edge);
    setColour (juce::TooltipWindow::backgroundColourId, menu);
    setColour (juce::TooltipWindow::textColourId, ink);
    setColour (juce::TooltipWindow::outlineColourId, edge2);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, edge2);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, yellow);
    setColour (juce::TabbedComponent::backgroundColourId, void_);
    setColour (juce::TabbedComponent::outlineColourId, edge);
    setColour (juce::ProgressBar::backgroundColourId, field);
    setColour (juce::ProgressBar::foregroundColourId, yellow);
    setColour (juce::HyperlinkButton::textColourId, cyan);

    auto& s = getCurrentColourScheme();
    using UI = ColourScheme::UIColour;
    s.setUIColour (UI::windowBackground, void_);
    s.setUIColour (UI::widgetBackground, field);
    s.setUIColour (UI::menuBackground, menu);
    s.setUIColour (UI::outline, edge2);
    s.setUIColour (UI::defaultText, ink);
    s.setUIColour (UI::defaultFill, yellow);
    s.setUIColour (UI::highlightedText, ink);
    s.setUIColour (UI::highlightedFill, hover);
    s.setUIColour (UI::menuText, ink);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    const auto style = f.getTypefaceStyle().toLowerCase();
    const auto w = style.contains ("extra") || style.contains ("black") || style.contains ("heavy") ? Fonts::Weight::heavy
                 : (f.isBold() || style.contains ("bold") || style.contains ("semi") ? Fonts::Weight::semibold : Fonts::Weight::regular);
    return Fonts::typeface (f.getTypefaceName(), w);
}

static float cutOf (const juce::Component& c, float fallback = 0.0f)
{
    const auto& v = c.getProperties()["psyched.cut"];
    return v.isVoid() ? fallback : (float) v;
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    if ((bool) b.getProperties()["psyched.swatch"])   // colour-swatch button: fill with buttonColourId
    {
        auto r = b.getLocalBounds().toFloat();
        const auto sw = b.findColour (juce::TextButton::buttonColourId);
        g.setColour (sw);
        g.fillRect (r);
        g.setColour (hi || down ? Colours::ink : Colours::edge2);
        g.drawRect (r, 1.0f);
        return;
    }

    const auto c = b.findColour (juce::TextButton::buttonOnColourId);
    const bool solid = (bool) b.getProperties()["psyched.solid"] || ((bool) b.getProperties()["psyched.mode"] && b.getToggleState());
    auto st = solid ? ChipState::solid : (b.getToggleState() || down ? ChipState::on : (hi ? ChipState::hover : ChipState::off));
    paintChip (g, b.getLocalBounds().toFloat(), {}, c, st, b.isEnabled(), cutOf (b));
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto c = b.findColour (juce::TextButton::buttonOnColourId);
    const bool solid = (bool) b.getProperties()["psyched.solid"] || ((bool) b.getProperties()["psyched.mode"] && b.getToggleState());
    const bool on = b.getToggleState() || down;
    auto col = solid ? Colours::onSolid : (on ? c : b.findColour (juce::TextButton::textColourOffId));
    if ((bool) b.getProperties()["psyched.swatch"])
        col = b.findColour (juce::TextButton::buttonColourId).getPerceivedBrightness() > 0.55f ? Colours::onSolid : Colours::ink;
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (col.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.drawFittedText (b.getButtonText().toUpperCase(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 2, 0.8f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (r.getHeight()).withSizeKeepingCentre (14.0f, 14.0f);
    const auto c = b.findColour (juce::ToggleButton::tickColourId);
    if (b.getToggleState())
    {
        juce::Path p;
        p.addRectangle (box);
        paintGlow (g, p, c.withAlpha (0.6f), 8);
        g.setColour (c);
        g.fillRect (box);
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
        tick.lineTo (box.getX() + 6.0f, box.getBottom() - 3.5f);
        tick.lineTo (box.getRight() - 3.0f, box.getY() + 3.5f);
        g.setColour (Colours::onSolid);
        g.strokePath (tick, juce::PathStrokeType (1.8f));
    }
    else
    {
        g.setColour (Colours::field);
        g.fillRect (box);
        g.setColour (hi ? Colours::edgeHot : Colours::edge2);
        g.drawRect (box, 1.0f);
    }
    g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.setFont (Fonts::body (13.0f, Fonts::Weight::semibold));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (4), juce::Justification::centredLeft);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRect (r);
    g.setColour (box.hasKeyboardFocus (true) ? Colours::yellow : (box.isMouseOver (true) ? Colours::edgeHot : box.findColour (juce::ComboBox::outlineColourId)));
    g.drawRect (r, box.hasKeyboardFocus (true) ? 2.0f : 1.0f);
    juce::Path arrow;
    const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (2, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (Colours::menu);
    g.setColour (Colours::edge2);
    g.drawRect (0, 0, w, h, 1);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                     bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                                     const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (Colours::edge);
        g.fillRect (area.withSizeKeepingCentre (area.getWidth() - 8, 1));
        return;
    }
    auto r = area.toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour (Colours::hover);
        g.fillRect (r);
        g.setColour (Colours::yellow);
        g.fillRect (r.withWidth (2.0f));
    }
    auto col = textColour != nullptr ? *textColour : Colours::ink;
    if (! isActive) col = Colours::mute;

    auto left = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.28f);
    if (icon != nullptr)
        icon->drawWithin (g, left, juce::RectanglePlacement::centred, 1.0f);
    else if (isTicked)
        paintLed (g, left.getCentre(), Colours::green, true, 7.0f);

    if (hasSubMenu)
    {
        auto ar = r.removeFromRight (r.getHeight());
        juce::Path p;
        p.startNewSubPath (ar.getCentreX() - 2.0f, ar.getCentreY() - 4.0f);
        p.lineTo (ar.getCentreX() + 2.0f, ar.getCentreY());
        p.lineTo (ar.getCentreX() - 2.0f, ar.getCentreY() + 4.0f);
        g.setColour (Colours::dim);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }
    g.setFont (getPopupMenuFont());
    g.setColour (col);
    g.drawFittedText (text, r.reduced (4.0f, 0).toNearestInt(), juce::Justification::centredLeft, 1);
    if (shortcut.isNotEmpty())
    {
        g.setFont (Fonts::label (10.0f));
        g.setColour (Colours::mute);
        g.drawText (shortcut.toUpperCase(), r.reduced (8.0f, 0), juce::Justification::centredRight, true);
    }
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                 int thumbStart, int thumbSize, bool over, bool down)
{
    g.setColour (Colours::field);
    g.fillRect (x, y, w, h);
    auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (over || down ? Colours::edgeHot : Colours::edge2);
    g.fillRect (thumb);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float min, float max,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto acc = s.findColour (juce::Slider::trackColourId);
    const bool horiz = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
    const bool vert = style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical;
    if (! horiz && ! vert)
        return LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, min, max, style, s);

    if (horiz)
    {
        auto track = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 2.0f, (float) w, 4.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2546), track.getX(), 0, juce::Colour (0xff1c1930), track.getRight(), 0, false));
        g.fillRect (track);
        g.setColour (acc.withAlpha (0.55f));
        g.fillRect (track.withRight (pos));
        auto thumb = juce::Rectangle<float> (12.0f, 14.0f).withCentre ({ pos, track.getCentreY() });
        juce::Path p;
        p.addRectangle (thumb);
        paintGlow (g, p, acc, 8);
        g.setColour (s.findColour (juce::Slider::thumbColourId));
        g.fillRect (thumb);
    }
    else
    {
        auto track = juce::Rectangle<float> ((float) x + (float) w * 0.5f - 2.0f, (float) y, 4.0f, (float) h);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2546), 0, track.getY(), juce::Colour (0xff1c1930), 0, track.getBottom(), false));
        g.fillRect (track);
        g.setColour (acc.withAlpha (0.55f));
        g.fillRect (track.withTop (pos));
        auto thumb = juce::Rectangle<float> (14.0f, 12.0f).withCentre ({ track.getCentreX(), pos });
        juce::Path p;
        p.addRectangle (thumb);
        paintGlow (g, p, acc, 8);
        g.setColour (s.findColour (juce::Slider::thumbColourId));
        g.fillRect (thumb);
    }
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    const auto acc = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float angle = start + pos * (end - start);

    juce::Path bg, val;
    bg.addCentredArc (r.getCentreX(), r.getCentreY(), d * 0.5f - 2.0f, d * 0.5f - 2.0f, 0.0f, start, end, true);
    val.addCentredArc (r.getCentreX(), r.getCentreY(), d * 0.5f - 2.0f, d * 0.5f - 2.0f, 0.0f, start, angle, true);
    g.setColour (Colours::edge2);
    g.strokePath (bg, juce::PathStrokeType (3.0f));
    juce::Path stroke;
    juce::PathStrokeType (3.0f).createStrokedPath (stroke, val);
    paintGlow (g, stroke, acc.withAlpha (0.6f), 6);
    g.setColour (acc);
    g.fillPath (stroke);

    g.setColour (Colours::field);
    g.fillEllipse (r.reduced (d * 0.18f));
    g.setColour (Colours::edge2);
    g.drawEllipse (r.reduced (d * 0.18f), 1.0f);
    const auto tip = r.getCentre().getPointOnCircumference (d * 0.3f, angle);
    g.setColour (Colours::ink);
    g.drawLine ({ r.getCentre().getPointOnCircumference (d * 0.12f, angle), tip }, 2.0f);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (Fonts::value());
    l->setColour (juce::Label::outlineColourId, Colours::edge2);
    l->setColour (juce::Label::backgroundColourId, Colours::field);
    return l;
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int, int, juce::TextEditor& e)
{
    g.fillAll (e.findColour (juce::TextEditor::backgroundColourId));
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    if (! e.isEnabled()) return;
    if (e.hasKeyboardFocus (true) && ! e.isReadOnly())
    {
        g.setColour (e.findColour (juce::TextEditor::focusedOutlineColourId));
        g.drawRect (0, 0, w, h, 2);
    }
    else
    {
        g.setColour (e.findColour (juce::TextEditor::outlineColourId));
        g.drawRect (0, 0, w, h, 1);
    }
}

void LookAndFeel::drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& h)
{
    g.fillAll (h.findColour (juce::TableHeaderComponent::backgroundColourId));
    g.setColour (h.findColour (juce::TableHeaderComponent::outlineColourId));
    g.fillRect (0, h.getHeight() - 1, h.getWidth(), 1);
    for (int i = h.getNumColumns (true); --i >= 0;)
        g.fillRect (h.getColumnPosition (i).removeFromRight (1).reduced (0, 6));
}

void LookAndFeel::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent& h, const juce::String& name, int,
                                         int w, int hgt, bool over, bool down, int flags)
{
    auto r = juce::Rectangle<int> (w, hgt);
    if (over || down)
    {
        g.setColour (h.findColour (juce::TableHeaderComponent::highlightColourId));
        g.fillRect (r.withTrimmedBottom (1));
    }
    r.reduce (8, 0);
    const bool sorted = (flags & (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0;
    if (sorted)
    {
        auto ar = r.removeFromRight (10).toFloat();
        juce::Path p;
        const bool fwd = (flags & juce::TableHeaderComponent::sortedForwards) != 0;
        p.addTriangle (ar.getCentreX() - 3.5f, ar.getCentreY() + (fwd ? -2.0f : 2.0f), ar.getCentreX() + 3.5f,
                       ar.getCentreY() + (fwd ? -2.0f : 2.0f), ar.getCentreX(), ar.getCentreY() + (fwd ? 3.0f : -3.0f));
        g.setColour (Colours::yellow);
        g.fillPath (p);
    }
    paintLabel (g, name, r.toFloat(), juce::Justification::centredLeft, sorted ? Colours::ink2 : h.findColour (juce::TableHeaderComponent::textColourId));
}

void LookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    const bool front = b.isFrontTab();
    auto r = b.getLocalBounds().toFloat();
    const auto c = b.getTabBackgroundColour();
    const auto acc = c.isTransparent() || c == juce::Colours::white ? Colours::yellow : c;
    g.setColour (front ? Colours::raise : (over ? Colours::hover : Colours::field));
    g.fillRect (r);
    if (front)
    {
        g.setColour (acc);
        g.fillRect (r.withHeight (2.0f));
    }
    g.setColour (Colours::edge2);
    g.drawRect (r, 1.0f);
    g.setFont (Fonts::chip (10.5f));
    g.setColour (front ? Colours::ink : Colours::chipInk);
    g.drawText (b.getButtonText().toUpperCase(), r, juce::Justification::centred);
}

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& w, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    auto r = w.getLocalBounds().toFloat();
    g.fillAll (Colours::void_);
    paintPanel (g, r, Colours::yellow, 0.0f);
    g.setColour (Colours::ink);
    layout.draw (g, textArea.toFloat());
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (Colours::menu);
    g.setColour (Colours::edge2);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (Colours::ink);
    g.setFont (Fonts::body (12.5f, Fonts::Weight::semibold));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 4);
}

void LookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 24, { 0, 8 }).drawForPath (g, path);
    g.setColour (Colours::menu);
    g.fillPath (path);
    g.setColour (Colours::edge2);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void LookAndFeel::drawGroupComponentOutline (juce::Graphics& g, int w, int h, const juce::String& text, const juce::Justification&, juce::GroupComponent& gc)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    paintPanel (g, r.withTrimmedTop (18.0f), gc.findColour (juce::GroupComponent::outlineColourId) == Colours::edge ? Colours::edge2
                                                                                                           : gc.findColour (juce::GroupComponent::outlineColourId));
    paintTitle (g, text, r.removeFromTop (14.0f), juce::Justification::centredLeft, gc.findColour (juce::GroupComponent::textColourId), 10.0f);
}

void LookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& bar, int w, int h, double progress, const juce::String& text)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (bar.findColour (juce::ProgressBar::backgroundColourId));
    g.fillRect (r);
    g.setColour (Colours::edge2);
    g.drawRect (r, 1.0f);
    const auto c = bar.findColour (juce::ProgressBar::foregroundColourId);
    if (progress >= 0.0 && progress <= 1.0)
    {
        auto fill = r.reduced (2.0f).withWidth ((float) ((w - 4) * progress));
        juce::Path p;
        p.addRectangle (fill);
        paintGlow (g, p, c.withAlpha (0.5f), 8);
        g.setColour (c);
        g.fillRect (fill);
    }
    if (text.isNotEmpty())
        paintLabel (g, text, r, juce::Justification::centred, Colours::ink);
}

} // namespace psyched
