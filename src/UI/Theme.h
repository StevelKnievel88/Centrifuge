#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace orbit
{

namespace Theme
{
    const juce::Colour bg          (0xff0b0c0e);
    const juce::Colour panel       (0xff141518);
    const juce::Colour panelHi     (0xff1c1e22);
    const juce::Colour line        (0xff2a2d33);
    const juce::Colour text        (0xffe8e9eb);
    const juce::Colour textDim     (0xff8b9099);
    const juce::Colour textFaint   (0xff5a5f68);
    const juce::Colour accent      (0xffff8a1f);   // orange
    const juce::Colour good        (0xff35d07f);
    const juce::Colour warn        (0xffffc22e);
    const juce::Colour bad         (0xffff4d4d);
    const juce::Colour waveBg      (0xff060708);

    inline juce::Colour stemColour (int stem)
    {
        static const juce::Colour c[] = { juce::Colour (0xffff3b3b),    // drums: red
                                          juce::Colour (0xff2ee05a),    // bass: green
                                          juce::Colour (0xff3d7bff),    // inst: blue
                                          juce::Colour (0xffff3df2) };  // vocals: magenta
        return c[juce::jlimit (0, 3, stem)];
    }

    inline juce::Colour deckColour (int deck) { return deck == 0 ? juce::Colour (0xff3aa3ff) : juce::Colour (0xffff8a1f); }

    juce::Font font (float height, bool bold = false);
    juce::Font mono (float height, bool bold = false);
}

class OrbitLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OrbitLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::font (13.0f); }
    juce::Font getPopupMenuFont() override { return Theme::font (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Theme::font (12.5f, true); }
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 10; }
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    static juce::Typeface::Ptr regular, semibold;
};

/** A small rounded status "pill" (e.g. XDJ-XZ connected). */
void drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour dot, bool bright = true);

} // namespace orbit
