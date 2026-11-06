#pragma once
#include "../Library/TrackVersion.h"
#include "../Audio/AudioFileLoader.h"

namespace orbit
{

/** One track to export, with the version (grid / cues) to write. */
struct ExportItem
{
    TrackInfo info;
    TrackVersion version;          // what goes into rekordbox's files
    TrackVersion centrifuge;       // the full Centrifuge version (kept on the stick for Centrifuge)
    juce::String key;              // Centrifuge's track key
};

/** A playlist (or folder) to export; `items` index into the export's track list. */
struct ExportList
{
    juce::String name;
    bool folder = false;
    std::vector<int> items;
    std::vector<ExportList> children;
};

/** Optional: also write stems for the XZ Mods loader on the XDJ-XZ (see XzStems.h). */
struct XzStemExport
{
    /** Finished stems for a track decoded from the stick, or null if it hasn't been separated. */
    std::function<StemAudio::Ptr (const TrackInfo&, const TrackAudio&)> loadStems;
};

struct ExportProgress
{
    std::function<bool (float progress, const juce::String& what)> report;   // return false to cancel
};

/** Writes a rekordbox XML collection (File > Import in rekordbox, or its "rekordbox xml" view). */
bool writeRekordboxXml (const juce::File& file, const std::vector<ExportItem>& items, const ExportList& playlists,
                        juce::String& error);

/** Writes a rekordbox-compatible USB stick: copies the audio to /Contents, writes the analysis files and
    PIONEER/rekordbox/export.pdb, plus Centrifuge's extras in PIONEER/Centrifuge. The stick's previous
    rekordbox database is kept as a backup next to the new one. */
bool exportToUsb (const juce::File& root, const std::vector<ExportItem>& items, const ExportList& playlists,
                  AudioFileLoader& loader, const ExportProgress& progress, juce::String& error, juce::String& summary,
                  const XzStemExport* xzStems = nullptr);

} // namespace orbit
