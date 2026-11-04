#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PsychedSkin.h"

namespace orbit
{

/** Centrifuge's names for the Psyched Devices Cockpit skin (skin/psyched-skin). */
namespace Theme
{
    namespace P = psyched::Colours;
    const juce::Colour bg          = P::void_;
    const juce::Colour panel       = P::void2;
    const juce::Colour panelHi     = P::raise;
    const juce::Colour field       = P::field;
    const juce::Colour line        = P::edge;
    const juce::Colour line2       = P::edge2;
    const juce::Colour text        = P::ink;
    const juce::Colour textDim     = P::dim;
    const juce::Colour textFaint   = P::mute;
    const juce::Colour accent      = P::yellow;   // active stage / selection
    const juce::Colour output      = P::pink;     // export, brand
    const juce::Colour sync        = P::blue;     // sync, tempo
    const juce::Colour map         = P::mint;     // mapping
    const juce::Colour good        = P::green;
    const juce::Colour warn        = P::amber;
    const juce::Colour bad         = P::orange;
    const juce::Colour waveBg      = juce::Colour (0xff06050b);

    inline juce::Colour stemColour (int stem)
    {
        static const juce::Colour c[] = { juce::Colour (0xffff3b3b),    // drums: red
                                          juce::Colour (0xff2ee05a),    // bass: green
                                          juce::Colour (0xff3d7bff),    // inst: blue
                                          juce::Colour (0xffff3df2) };  // vocals: magenta
        return c[juce::jlimit (0, 3, stem)];
    }

    /** Decks are layers: deck 1 cyan, deck 2 pink. */
    inline juce::Colour deckColour (int deck) { return deck == 0 ? P::cyan : P::pink; }

    juce::Font font (float height, bool bold = false);      // Manrope
    juce::Font mono (float height, bool bold = false);      // JetBrains Mono
    juce::Font display (float height, bool heavy = false);  // Unbounded
}

class OrbitLookAndFeel : public psyched::LookAndFeel
{
public:
    OrbitLookAndFeel() = default;
};

/** Status readout in the top bar: LED + uppercase mono label in a field box. */
void drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour dot, bool bright = true);

} // namespace orbit
