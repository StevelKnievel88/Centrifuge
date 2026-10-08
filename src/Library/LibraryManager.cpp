#include "LibraryManager.h"
#include "RekordboxMasterDb.h"
#include "RekordboxPdb.h"

namespace orbit
{

LibraryManager::LibraryManager()
{
    startTimer (3000);
}

LibraryManager::~LibraryManager()
{
    stopTimer();
    pool.removeAllJobs (true, 10000);
}

void LibraryManager::configure (const juce::File& masterDb, const juce::String& key, const juce::StringArray& extra)
{
    dbFile = masterDb;
    dbKey = key;
    extraRoots = extra;
    reloadPcLibrary();
    scanUsb();
}

void LibraryManager::reloadPcLibrary()
{
    auto file = dbFile;
    auto key = dbKey;
    pool.addJob ([this, file, key]
    {
        std::shared_ptr<const LibrarySource> src = loadRekordboxMasterDb (file, key);
        {
            const juce::ScopedLock sl (lock);
            pc = src;
        }
        juce::MessageManager::callAsync ([this] { if (onChanged) onChanged(); });
    });
}

std::vector<std::shared_ptr<const LibrarySource>> LibraryManager::getSources() const
{
    const juce::ScopedLock sl (lock);
    std::vector<std::shared_ptr<const LibrarySource>> r;
    if (pc != nullptr) r.push_back (pc);
    for (auto& [k, v] : usb) r.push_back (v);
    return r;
}

void LibraryManager::timerCallback()
{
    scanUsb();
}

void LibraryManager::scanUsb()
{
    juce::Array<juce::File> roots;
    juce::File::findFileSystemRoots (roots);
    for (auto& e : extraRoots)
        if (juce::File::isAbsolutePath (e))
            roots.add (juce::File (e));

    std::map<juce::String, juce::int64> found;
    for (auto& r : roots)
    {
        const auto path = r.getFullPathName();
       #if JUCE_WINDOWS
        if (path.startsWithIgnoreCase ("A:") || path.startsWithIgnoreCase ("B:")) continue;
       #endif
        auto pdb = r.getChildFile ("PIONEER").getChildFile ("rekordbox").getChildFile ("export.pdb");
        if (pdb.existsAsFile())
            found[path] = pdb.getLastModificationTime().toMilliseconds();
    }

    bool changed = false;
    {
        const juce::ScopedLock sl (lock);
        for (auto it = usb.begin(); it != usb.end();)
        {
            if (found.find (it->first) == found.end())
            {
                usbStamp.erase (it->first);
                it = usb.erase (it);
                changed = true;
            }
            else ++it;
        }
    }

    for (auto& [path, stamp] : found)
    {
        bool needLoad;
        {
            const juce::ScopedLock sl (lock);
            auto it = usbStamp.find (path);
            needLoad = it == usbStamp.end() || it->second != stamp;
            usbStamp[path] = stamp;
        }
        if (! needLoad) continue;

        auto root = juce::File (path);
        pool.addJob ([this, root, path = path]
        {
            std::shared_ptr<const LibrarySource> src = loadRekordboxUsbExport (root);
            {
                const juce::ScopedLock sl (lock);
                if (usbStamp.find (path) != usbStamp.end())
                    usb[path] = src;
            }
            juce::MessageManager::callAsync ([this] { if (onChanged) onChanged(); });
        });
    }

    if (changed && onChanged)
        onChanged();
}

} // namespace orbit
