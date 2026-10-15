#include "Theme.h"
#include "BinaryData.h"

namespace orbit
{

juce::Typeface::Ptr OrbitLookAndFeel::regular;
juce::Typeface::Ptr OrbitLookAndFeel::semibold;

juce::Font Theme::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height).withStyle (bold ? "Bold" : "Regular"));
}

juce::Font Theme::mono (float height, bool bold)
{
    return font (height, bold);
}

OrbitLookAndFeel::OrbitLookAndFeel()
{
    if (regular == nullptr)
        regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_otf, BinaryData::InterRegular_otfSize);
    if (semibold == nullptr)
        semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);

    using C = juce::ResizableWindow;
    setColour (C::backgroundColourId, Theme::bg);
    setColour (juce::DocumentWindow::textColourId, Theme::text);
    setColour (juce::Label::textColourId, Theme::text);
    setColour (juce::TextButton::buttonColourId, Theme::panelHi);
    setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    setColour (juce::TextButton::textColourOffId, Theme::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, Theme::panelHi);
    setColour (juce::ComboBox::outlineColourId, Theme::line);
    setColour (juce::ComboBox::textColourId, Theme::text);
    setColour (juce::ComboBox::arrowColourId, Theme::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Theme::panelHi);
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextEditor::backgroundColourId, Theme::panelHi);
    setColour (juce::TextEditor::outlineColourId, Theme::line);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
    setColour (juce::TextEditor::textColourId, Theme::text);
    setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, Theme::accent);
    setColour (juce::ListBox::backgroundColourId, Theme::panel);
    setColour (juce::ListBox::outlineColourId, Theme::line);
    setColour (juce::TreeView::backgroundColourId, Theme::panel);
    setColour (juce::TreeView::selectedItemBackgroundColourId, Theme::accent.withAlpha (0.25f));
    setColour (juce::TreeView::linesColourId, Theme::line);
    setColour (juce::ScrollBar::thumbColourId, Theme::line.brighter (0.3f));
    setColour (juce::TableHeaderComponent::backgroundColourId, Theme::panelHi);
    setColour (juce::TableHeaderComponent::textColourId, Theme::textDim);
    setColour (juce::TableHeaderComponent::outlineColourId, Theme::line);
    setColour (juce::TableHeaderComponent::highlightColourId, Theme::line);
    setColour (juce::ToggleButton::textColourId, Theme::text);
    setColour (juce::ToggleButton::tickColourId, Theme::accent);
    setColour (juce::Slider::trackColourId, Theme::accent);
    setColour (juce::Slider::backgroundColourId, Theme::line);
    setColour (juce::Slider::thumbColourId, Theme::text);
    setColour (juce::Slider::textBoxTextColourId, Theme::text);
    setColour (juce::Slider::textBoxBackgroundColourId, Theme::panelHi);
    setColour (juce::Slider::textBoxOutlineColourId, Theme::line);
    setColour (juce::GroupComponent::outlineColourId, Theme::line);
    setColour (juce::GroupComponent::textColourId, Theme::textDim);
    setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
    setColour (juce::AlertWindow::textColourId, Theme::text);
    setColour (juce::AlertWindow::outlineColourId, Theme::line);

    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::windowBackground, Theme::bg);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::widgetBackground, Theme::panelHi);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::menuBackground, Theme::panelHi);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::outline, Theme::line);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::defaultText, Theme::text);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::defaultFill, Theme::accent);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::highlightedText, juce::Colours::black);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::highlightedFill, Theme::accent);
    getCurrentColourScheme().setUIColour (ColourScheme::UIColour::menuText, Theme::text);
}

juce::Typeface::Ptr OrbitLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.isBold() || f.getTypefaceStyle().containsIgnoreCase ("bold") || f.getTypefaceStyle().containsIgnoreCase ("semi"))
        return semibold != nullptr ? semibold : LookAndFeel_V4::getTypefaceForFont (f);
    return regular != nullptr ? regular : LookAndFeel_V4::getTypefaceForFont (f);
}

void OrbitLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto base = b.findColour (on ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
    if (! b.isEnabled()) base = base.withMultipliedAlpha (0.4f);
    if (down) base = base.brighter (0.15f);
    else if (hi) base = base.brighter (0.07f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? base.brighter (0.2f) : Theme::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void OrbitLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                  .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 2);
}

void OrbitLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.22f);
    g.setColour (b.getToggleState() ? Theme::accent : (hi ? Theme::line.brighter (0.2f) : Theme::line));
    g.fillRoundedRectangle (box, 3.0f);
    if (b.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + box.getWidth() * 0.22f, box.getCentreY());
        tick.lineTo (box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.25f);
        tick.lineTo (box.getRight() - box.getWidth() * 0.2f, box.getY() + box.getHeight() * 0.25f);
        g.setColour (juce::Colours::black);
        g.strokePath (tick, juce::PathStrokeType (2.0f));
    }
    g.setColour (Theme::text.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.setFont (Theme::font (13.5f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (6), juce::Justification::centredLeft);
}

void OrbitLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (Theme::panelHi);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Theme::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (Theme::textDim);
    g.fillPath (arrow);
}

void OrbitLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                      int thumbStart, int thumbSize, bool over, bool)
{
    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float> ((float) x + 2, (float) thumbStart, (float) w - 4, (float) thumbSize)
                                            : juce::Rectangle<float> ((float) thumbStart, (float) y + 2, (float) thumbSize, (float) h - 4);
    g.setColour (Theme::line.brighter (over ? 0.5f : 0.25f));
    g.fillRoundedRectangle (thumb, 3.0f);
}

void OrbitLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
        return LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);

    auto track = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 2.0f, (float) w, 4.0f);
    g.setColour (Theme::line);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (s.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (track.withRight (pos), 2.0f);
    g.setColour (Theme::text);
    g.fillEllipse (pos - 6.0f, (float) y + (float) h * 0.5f - 6.0f, 12.0f, 12.0f);
}

juce::Label* OrbitLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (Theme::font (13.0f));
    l->setColour (juce::Label::outlineColourId, Theme::line);
    return l;
}

void drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour dot, bool bright)
{
    g.setColour (Theme::panelHi);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (Theme::line);
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
    auto dotR = juce::Rectangle<float> (r.getX() + 10.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour (dot);
    g.fillEllipse (dotR);
    g.setColour (bright ? Theme::text : Theme::textDim);
    g.setFont (Theme::font (12.5f));
    g.drawText (text, r.withTrimmedLeft (24.0f).withTrimmedRight (10.0f), juce::Justification::centredLeft, true);
}

} // namespace orbit
