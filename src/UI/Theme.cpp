#include "Theme.h"

namespace orbit
{

juce::Font Theme::font (float height, bool bold)
{
    return psyched::Fonts::body (height, bold ? psyched::Fonts::Weight::semibold : psyched::Fonts::Weight::regular);
}

juce::Font Theme::mono (float height, bool bold)
{
    return psyched::Fonts::mono (height, bold);
}

juce::Font Theme::display (float height, bool heavy)
{
    return psyched::Fonts::display (height, heavy ? psyched::Fonts::Weight::heavy : psyched::Fonts::Weight::semibold);
}

void drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour dot, bool bright)
{
    g.setColour (psyched::Colours::field);
    g.fillRect (r);
    g.setColour (bright ? dot.withAlpha (0.85f) : psyched::Colours::edge2);   // outlined in its dot's colour
    g.drawRect (r, 1.0f);
    psyched::paintLed (g, { r.getX() + 12.0f, r.getCentreY() }, dot, bright);
    psyched::paintLabel (g, text, r.withTrimmedLeft (22.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft,
                         bright ? psyched::Colours::ink2 : psyched::Colours::dim, 10.0f);
}

} // namespace orbit
