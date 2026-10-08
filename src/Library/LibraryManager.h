#pragma once
#include "TrackInfo.h"
#include <juce_events/juce_events.h>
#include <memory>

namespace orbit
{

/** Holds the PC rekordbox collection plus any rekordbox USB sticks that are plugged in.
    Loading happens on a background thread; `onChanged` fires on the message thread. */
class LibraryManager : private juce::Timer
{
public:
    LibraryManager();
    ~LibraryManager() override;

    void configure (const juce::File& masterDb, const juce::String& key, const juce::StringArray& extraUsbRoots);
    void reloadPcLibrary();

    std::vector<std::shared_ptr<const LibrarySource>> getSources() const;
    std::function<void()> onChanged;

private:
    void timerCallback() override;
    void scanUsb();

    juce::File dbFile;
    juce::String dbKey;
    juce::StringArray extraRoots;

    mutable juce::CriticalSection lock;
    std::shared_ptr<const LibrarySource> pc;
    std::map<juce::String, std::shared_ptr<const LibrarySource>> usb;   // keyed by root path
    std::map<juce::String, juce::int64> usbStamp;                       // export.pdb modification time

    juce::ThreadPool pool { 1 };
};

} // namespace orbit
