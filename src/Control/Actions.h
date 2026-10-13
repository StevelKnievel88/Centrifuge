#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace orbit
{

/** How an action wants its control data. */
enum class ActionKind
{
    button,      // press / release
    continuous,  // absolute 0..1 (fader, knob)
    encoder      // relative steps (jog wheel, browse knob)
};

struct ActionDef
{
    juce::String id;        // e.g. "d1.play"
    juce::String label;     // e.g. "Play / Pause"
    juce::String group;     // e.g. "Deck 1 - Transport"
    ActionKind kind = ActionKind::button;
    int deck = -1;          // 0..3, or -1 for global
    bool hasLed = false;    // sends LED feedback when mapped to a button
};

/** Every mappable function in Centrifuge. */
const std::vector<ActionDef>& allActions();
const ActionDef* findAction (const juce::String& id);

/** A control event delivered to an action. */
struct ActionValue
{
    bool down = false;       // buttons
    double value = 0.0;      // continuous 0..1
    double delta = 0.0;      // encoders (+/- steps)
};

} // namespace orbit
