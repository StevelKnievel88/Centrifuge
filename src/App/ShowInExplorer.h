#pragma once
#include <juce_core/juce_core.h>

namespace orbit
{

/** Opens the file manager at the files' folder(s) with the files selected (Windows Explorer: all of a folder's
    files highlighted in one window; elsewhere the folder is shown with the first file). */
void showInExplorer (const juce::Array<juce::File>& files);

} // namespace orbit
