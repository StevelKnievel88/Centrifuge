#pragma once
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace orbit
{

/** The phrase names offered by + ADD PHRASE (factory ones and your own), each with its colour. Editable, kept
    between sessions; a preset's colour is also the default colour for phrases of that name (rekordbox's too). */
namespace PhrasePresets
{
    struct Entry { juce::String name; juce::Colour colour; };

    std::vector<Entry> get();
    void set (std::vector<Entry> list);
    std::vector<Entry> factory();
    /** The colour of the preset called `name` (case doesn't matter, "Drop 2" matches "Drop"). */
    bool colourFor (const juce::String& name, juce::Colour& out);
}

} // namespace orbit
