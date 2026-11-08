#pragma once
#include "Theme.h"
#include "../Audio/TrackAudio.h"

namespace orbit
{

/** One stem's control as drawn on the decks and PREP: a bipolar bar (centre = untouched) with the stem's name, and
    under it a small VOL / FILTER button. VOL: left of centre turns the stem down. FILTER: left = low-pass, right = high-pass.
    Returns the bar's and the mode button's areas (for clicks). */
inline void paintStemControl (juce::Graphics& g, juce::Rectangle<float> r, int stem, float value, int mode, bool ready,
                              juce::Rectangle<float>& barArea, juce::Rectangle<float>& modeArea)
{
    const auto sc = Theme::stemColour (stem);
    modeArea = r.removeFromBottom (juce::jlimit (11.0f, 14.0f, r.getHeight() * 0.36f));
    r.removeFromBottom (2.0f);
    barArea = r;

    // bar: filled from the centre to the value
    g.setColour (Theme::panelHi);
    g.fillRect (r);
    const bool muted = value <= 0.001f;
    const float cx = r.getX() + r.getWidth() * 0.5f, vx = r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, value);
    if (ready)
    {
        g.setColour (sc.withAlpha (muted ? 0.12f : 0.6f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, vx), r.getY(), juce::jmax (cx, vx) + 1.0f, r.getBottom()));
        if (mode == 0 && value > 0.5f)   // VOL: right of centre does nothing - shown faint
        {
            g.setColour (Theme::panelHi.withAlpha (0.6f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (cx + 1.0f, r.getY(), vx + 1.0f, r.getBottom()));
        }
    }
    g.setColour (ready ? sc.withAlpha (0.8f) : Theme::line);
    g.drawRect (r.reduced (0.5f), 1.0f);
    g.setColour ((ready ? sc : Theme::line).withAlpha (0.7f));
    g.fillRect (cx - 0.5f, r.getY() + 1.0f, 1.0f, r.getHeight() - 2.0f);   // the centre mark

    juce::String label = juce::String (stemName (stem)).toUpperCase();
    if (mode == 1 && value < 0.48f) label << " LP";
    if (mode == 1 && value > 0.52f) label << " HP";
    g.setColour (ready ? (muted ? Theme::textFaint : Theme::text) : Theme::textFaint);
    g.setFont (Theme::font (11.0f, true));
    g.drawText (label, r.toNearestInt(), juce::Justification::centred, true);

    // VOL / FILTER button underneath
    psyched::paintChip (g, modeArea, mode == 1 ? "FILTER" : "VOL", sc, mode == 1 ? psyched::ChipState::on : psyched::ChipState::off);
}

} // namespace orbit
