#pragma once
#include "TrackInfo.h"

namespace orbit
{

/** Default location of rekordbox 6/7's library database (%APPDATA%\Pioneer\rekordbox\master.db). */
juce::File defaultRekordboxDbFile();

/** The key rekordbox 6/7 use to encrypt master.db (publicly documented by pyrekordbox). */
juce::String defaultRekordboxDbKey();

/** Reads the rekordbox 6/7 collection. The database is copied to a temp folder first and only ever read,
    so your rekordbox library can never be modified. `shareDir` is where the ANLZ files live
    (defaults to <db folder>/share). Pass an empty key for an unencrypted database. */
std::unique_ptr<LibrarySource> loadRekordboxMasterDb (const juce::File& dbFile,
                                                      const juce::String& key,
                                                      juce::File shareDir = {});

} // namespace orbit
