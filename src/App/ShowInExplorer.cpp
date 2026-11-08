#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <shlobj.h>
 #pragma comment (lib, "shell32.lib")
 #pragma comment (lib, "ole32.lib")
#endif
#include "ShowInExplorer.h"
#include <map>
#include <vector>

namespace orbit
{

void showInExplorer (const juce::Array<juce::File>& files)
{
    std::map<juce::String, juce::Array<juce::File>> byFolder;
    for (auto& f : files)
        if (f.existsAsFile()) byFolder[f.getParentDirectory().getFullPathName()].add (f);

    for (auto& [folder, list] : byFolder)
    {
       #if defined (_WIN32)
        CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);   // already on for the message thread: harmless
        PIDLIST_ABSOLUTE dir = nullptr;
        if (SUCCEEDED (SHParseDisplayName (folder.toWideCharPointer(), nullptr, &dir, 0, nullptr)) && dir != nullptr)
        {
            std::vector<PIDLIST_ABSOLUTE> items;
            for (auto& f : list)
            {
                PIDLIST_ABSOLUTE item = nullptr;
                if (SUCCEEDED (SHParseDisplayName (f.getFullPathName().toWideCharPointer(), nullptr, &item, 0, nullptr)) && item != nullptr)
                    items.push_back (item);
            }
            std::vector<PCUITEMID_CHILD> children;
            for (auto* it : items) children.push_back ((PCUITEMID_CHILD) ILFindLastID (it));
            const bool ok = ! children.empty()
                         && SUCCEEDED (SHOpenFolderAndSelectItems (dir, (UINT) children.size(), children.data(), 0));
            for (auto* it : items) CoTaskMemFree (it);
            CoTaskMemFree (dir);
            if (ok) continue;
        }
       #endif
        list.getFirst().revealToUser();
    }
}

} // namespace orbit
