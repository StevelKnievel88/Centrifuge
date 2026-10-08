#pragma once
#include "TrackInfo.h"

namespace orbit
{

/** Reads a rekordbox USB export: <usbRoot>/PIONEER/rekordbox/export.pdb (DeviceSQL format). */
std::unique_ptr<LibrarySource> loadRekordboxUsbExport (const juce::File& usbRoot);

/** Parses export.pdb bytes. File paths in the export are resolved against usbRoot. */
std::unique_ptr<LibrarySource> parseExportPdb (const juce::MemoryBlock& pdb, const juce::File& usbRoot);

} // namespace orbit
