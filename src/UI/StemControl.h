#pragma once
#include "Theme.h"
#include "../Audio/TrackAudio.h"

namespace orbit
{

/** One stem's controls as drawn on the decks and PREP: a volume bar over a filter bar, both outlined in the stem's colour.

    Volume: filled from the left to the stem's level (full = untouched), the stem's name across it.
    Filter: a centre block that is always lit, low-pass filling out to the left of it and high-pass out to the right,
    with LP / HP at the ends. Both controls store 0..1 with 0.5 = untouched; the filter's centre third is "open".
    Returns both bars' areas (for clicks and drags). */
inline void paintStemControl (juce::Graphics& g, juce::Rectangle<float> r, int stem, float volume, float filter, bool ready,
                              juce::Rectangle<float>& volArea, juce::Rectangle<float>& filterArea, int dim = 0)
{
    // dim: 1 = the volume bar, 2 = the filter bar (the one the switched knobs aren't working: dimmer, grey text)
    const auto sc = Theme::stemColour (stem);
    const float gap = r.getHeight() >= 24.0f ? 2.0f : 1.0f;
    volArea = r.removeFromTop ((r.getHeight() - gap) * 0.5f);
    r.removeFromTop (gap);
    filterArea = r;

    const auto fill = sc.darker (0.55f);
    const auto edge = ready ? sc.darker (0.2f) : Theme::line;
    const float fontH = juce::jlimit (8.0f, 11.0f, volArea.getHeight() * 0.72f);

    // --- Volume ---------------------------------------------------------------------------
    {
        auto b = volArea;
        g.setColour (juce::Colours::black);
        g.fillRect (b);
        const float level = juce::jlimit (0.0f, 1.0f, volume * 2.0f);
        if (ready && level > 0.0f)
        {
            g.setColour (dim == 1 ? fill.withMultipliedBrightness (0.45f) : fill);
            g.fillRect (b.reduced (3.0f).withWidth ((b.getWidth() - 6.0f) * level));   // a black gap inside the outline
        }
        g.setColour (edge);
        g.drawRect (b, 1.0f);
        g.setColour (ready && dim != 1 ? (level <= 0.001f ? Theme::textFaint : juce::Colours::white) : Theme::textFaint);
        g.setFont (Theme::font (fontH, true));
        g.drawText (juce::String (stemName (stem)).toUpperCase(), b.toNearestInt(), juce::Justification::centred, false);
    }

    // --- Filter -------------------------------------------------------------------------------
    {
        auto b = filterArea;
        g.setColour (juce::Colours::black);
        g.fillRect (b);
        auto in = b.reduced (3.0f);
        const float third = in.getWidth() / 3.0f;
        auto left = in.withWidth (third), centre = in.withX (in.getX() + third).withWidth (third),
             right = in.withX (in.getX() + 2.0f * third).withWidth (third);
        if (ready)
        {
            g.setColour (dim == 2 ? fill.withMultipliedBrightness (0.45f) : fill);
            g.fillRect (centre.reduced (0.5f, 0.0f));   // always lit
            const float f = juce::jlimit (0.0f, 1.0f, filter);
            if (f < 0.5f)   // low-pass: grows out to the left from the centre block
            {
                const float w = left.getWidth() * (0.5f - f) / 0.5f;
                g.fillRect (left.withLeft (left.getRight() - w).withTrimmedRight (0.5f));
            }
            else if (f > 0.5f)   // high-pass: grows out to the right
            {
                const float w = right.getWidth() * (f - 0.5f) / 0.5f;
                g.fillRect (right.withWidth (w).withTrimmedLeft (0.5f));
            }
        }
        g.setColour (edge);
        g.drawRect (b, 1.0f);
        g.setColour (ready && dim != 2 ? juce::Colours::white : Theme::textFaint);
        const float small = juce::jmax (7.5f, fontH - 1.0f);
        g.setFont (Theme::font (small, true));
        if (left.getWidth() >= 14.0f)
        {
            g.drawText ("LP", left.reduced (2.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, false);
            g.drawText ("HP", right.reduced (2.0f, 0.0f).toNearestInt(), juce::Justification::centredRight, false);
        }
        if (centre.getWidth() >= 26.0f)
            g.drawText ("FILTER", centre.toNearestInt(), juce::Justification::centred, false);
    }
}

/** Value of the volume bar at x (full width = untouched = 0.5). */
inline double stemVolumeAt (juce::Rectangle<float> bar, float x)
{
    return 0.5 * juce::jlimit (0.0, 1.0, (double) ((x - bar.getX()) / juce::jmax (1.0f, bar.getWidth())));
}

/** Value of the filter bar at x: left third low-pass (0..0.5), centre third open (0.5), right third high-pass (0.5..1). */
inline double stemFilterAt (juce::Rectangle<float> bar, float x)
{
    const double f = juce::jlimit (0.0, 1.0, (double) ((x - bar.getX()) / juce::jmax (1.0f, bar.getWidth())));
    if (f < 1.0 / 3.0) return f * 1.5;
    if (f > 2.0 / 3.0) return 0.5 + (f - 2.0 / 3.0) * 1.5;
    return 0.5;
}

/** The stem filters' resonance: a small knob (0..1) in place of the old icon. */
inline void paintResonanceKnob (juce::Graphics& g, juce::Rectangle<float> r, float value, juce::Colour colour = Theme::sync)
{
    const float side = juce::jmax (8.0f, juce::jmin (r.getWidth(), r.getHeight() - 9.0f));
    auto b = r.withHeight (side).withSizeKeepingCentre (side, side).withY (r.getY());
    auto label = r.withTop (b.getBottom());
    const float v = juce::jlimit (0.0f, 1.0f, value);
    const float a0 = -juce::MathConstants<float>::pi * 0.75f, a1 = juce::MathConstants<float>::pi * 0.75f;   // 7:30 to 4:30
    const auto c = b.getCentre();
    const float rad = side * 0.42f;
    juce::Path bg, val;
    bg.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
    val.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a0 + (a1 - a0) * v, true);
    g.setColour (Theme::panelHi);
    g.fillEllipse (b.reduced (side * 0.24f));
    g.setColour (Theme::line);
    g.strokePath (bg, juce::PathStrokeType (2.0f));
    g.setColour (colour);
    g.strokePath (val, juce::PathStrokeType (2.5f));
    const float ang = a0 + (a1 - a0) * v, rr = side * 0.26f;
    g.drawLine (c.x, c.y, c.x + rr * std::sin (ang), c.y - rr * std::cos (ang), 2.0f);
    psyched::paintLabel (g, "RES", label, juce::Justification::centredTop, Theme::textDim, 8.0f);
}

} // namespace orbit
