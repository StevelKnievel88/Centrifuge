#pragma once
/*  Psyched Devices — Cockpit skin v1.0.0 for JUCE 8.
    The house skin for every Psyched Devices program (taken from Mirari's main Cockpit skin).

    CMake:   include(psyched-skin/juce/psyched_skin.cmake)   psyched_skin_add(MyApp)
    Startup: static psyched::LookAndFeel laf;  juce::LookAndFeel::setDefaultLookAndFeel (&laf);

    Colour = function: green inputs, cyan layers/decks, mint mapping, yellow active stage, pink output/brand,
    violet modulation, blue sync, orange alerts, amber warnings. See README.md.
*/
#include <juce_gui_basics/juce_gui_basics.h>

namespace psyched
{

namespace Colours
{
    // surfaces
    const juce::Colour page     (0xff050409);
    const juce::Colour void_    (0xff07060d);
    const juce::Colour void2    (0xff0a0912);
    const juce::Colour panel    (0xff110f1d);
    const juce::Colour raise    (0xff171428);
    const juce::Colour field    (0xff0b0a13);
    const juce::Colour menu     (0xff0d0b17);
    const juce::Colour hover    (0xff1a1730);
    const juce::Colour glowCore (0xff140b2a);
    // lines
    const juce::Colour edge     (0xff24203a);
    const juce::Colour edge2    (0xff2c2844);
    const juce::Colour edgeHot  (0xff4d4670);
    const juce::Colour header   (0xff1d1a2e);
    // text
    const juce::Colour ink      (0xffece9ff);
    const juce::Colour ink2     (0xffd9d6f0);
    const juce::Colour chipInk  (0xffbdb9d8);
    const juce::Colour dim      (0xff9a97b6);
    const juce::Colour mute     (0xff6f6b8c);
    const juce::Colour onSolid  (0xff08070f);
    const juce::Colour ledOff   (0xff3a3654);
    // colour = function
    const juce::Colour green    (0xff3cf06a);   // inputs, armed, OK
    const juce::Colour cyan     (0xff2ee6d6);   // layers, lanes, decks
    const juce::Colour mint     (0xff7dffd6);   // mapping, routing
    const juce::Colour yellow   (0xffffd339);   // active stage, focus
    const juce::Colour pink     (0xffff3fa0);   // output, export, brand
    const juce::Colour violet   (0xffc77dff);   // modulation, effects
    const juce::Colour blue     (0xff6e8cff);   // sync, clock, tempo
    const juce::Colour orange   (0xffff4b1f);   // alerts, destructive
    const juce::Colour amber    (0xfff5b54a);   // warnings, busy
}

enum class Role { input, layer, map, active, output, modulation, sync, alert, warm };
juce::Colour colourFor (Role);

namespace Fonts
{
    enum class Weight { regular, semibold, heavy };
    juce::Font display (float height, Weight = Weight::heavy);   // Unbounded 600 / 800
    juce::Font body    (float height, Weight = Weight::regular); // Manrope 400 / 600 / 800
    juce::Font mono    (float height, bool semibold = false);    // JetBrains Mono 500 / 600

    juce::Font label (float height = 10.0f);   // mono 600, tracking .14em   (draw UPPERCASE)
    juce::Font value (float height = 12.0f);   // mono 500, tabular
    juce::Font title (float height = 11.0f);   // display 600, tracking .18em (draw UPPERCASE)
    juce::Font chip  (float height = 10.0f);   // mono 600, tracking .12em   (draw UPPERCASE)

    juce::Typeface::Ptr typeface (const juce::String& family, Weight);
}

namespace Shape
{
    constexpr float chamfer = 14.0f, chamferSmall = 8.0f, chipHeight = 28.0f, grid = 40.0f, led = 8.0f, topBorder = 2.0f;
}

//==============================================================================
// Painting helpers — use these in custom components so every app shares one look.

/** Rectangle with the top-left and bottom-right corners cut (Cockpit's .chamf). */
juce::Path chamferPath (juce::Rectangle<float>, float cut = Shape::chamfer, bool topLeft = true, bool bottomRight = true);

/** App backdrop: radial violet glow + 40px blue grid. Pass the whole window as `whole` when painting a sub-area. */
void paintBackdrop (juce::Graphics&, juce::Rectangle<int> area, juce::Rectangle<int> whole = {});

/** Header bar gradient with its 1px bottom line. */
void paintHeader (juce::Graphics&, juce::Rectangle<float>);

/** Panel: gradient, 1px edge, 2px function-colour top border, chamfered corners (cut = 0 for square). */
void paintPanel (juce::Graphics&, juce::Rectangle<float>, juce::Colour top = Colours::edge2, float cut = Shape::chamfer);

/** Inset well for displays/waveforms: field colour + edge, optional scanlines. */
void paintWell (juce::Graphics&, juce::Rectangle<float>, bool scanlines = true);

void paintScanlines (juce::Graphics&, juce::Rectangle<float>, float alpha = 0.035f);

/** Soft glow around a path (CSS box-shadow 0 0 radius colour). */
void paintGlow (juce::Graphics&, const juce::Path&, juce::Colour, int radius);

void paintLed (juce::Graphics&, juce::Point<float> centre, juce::Colour, bool on, float size = Shape::led);

enum class ChipState { off, hover, on, solid };
void paintChip (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, juce::Colour c, ChipState, bool enabled = true, float cut = 0.0f);

void paintLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification = juce::Justification::centredLeft,
                 juce::Colour = Colours::dim, float height = 10.0f);
void paintTitle (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification = juce::Justification::centredLeft,
                 juce::Colour = Colours::ink, float height = 11.0f);

/** Status readout: LED + uppercase mono label, e.g. "XDJ-XZ  CONNECTED". */
void paintStatus (juce::Graphics&, juce::Rectangle<float>, const juce::String&, juce::Colour, bool on = true);

enum class Signature { none, mirror, spin };
/** The pink-glow wordmark. `signatureIndex` picks the one pink letter (Mirari mirrors its R). Returns the width used. */
float paintWordmark (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, int signatureIndex = -1,
                     Signature = Signature::mirror, float height = 21.0f, juce::Justification = juce::Justification::centredLeft);

//==============================================================================
/** The Cockpit LookAndFeel. Buttons use TextButton::buttonOnColourId as their function colour.
    Component properties: "psyched.solid" = true for a filled primary button, "psyched.mode" = true for a mode-switch
    button (solid when toggled on — give it buttonOnColourId green), "psyched.swatch" = true for a colour swatch
    (filled with buttonColourId), "psyched.cut" = chamfer size. */
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Fonts::chip(); }
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Fonts::value(); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override { return Fonts::body (13.0f, Fonts::Weight::semibold); }
    int getPopupMenuBorderSize() override { return 4; }

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 10; }

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;

    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& name, int columnId,
                                int w, int h, bool isMouseOver, bool isMouseDown, int columnFlags) override;

    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}

    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    juce::Font getAlertWindowTitleFont() override { return Fonts::title (13.0f); }
    juce::Font getAlertWindowMessageFont() override { return Fonts::body (13.0f); }
    juce::Font getAlertWindowFont() override { return Fonts::body (13.0f); }

    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    void drawGroupComponentOutline (juce::Graphics&, int w, int h, const juce::String& text, const juce::Justification&, juce::GroupComponent&) override;
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int w, int h, double progress, const juce::String& text) override;
};

} // namespace psyched
