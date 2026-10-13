#include "AppSettings.h"

namespace orbit
{

juce::File AppSettings::folder()
{
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    auto dir = base.getChildFile ("Centrifuge");
    // Carry settings and mapping over from the OrbitDJ test builds.
    auto old = base.getChildFile ("OrbitDJ");
    if (! dir.exists() && old.isDirectory())
        old.copyDirectoryTo (dir);
    return dir;
}

juce::File AppSettings::modelFile() const
{
    if (modelPath.isNotEmpty()) return juce::File (modelPath);
    auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (auto dir : { exeDir, exeDir.getParentDirectory(), folder() })
    {
        auto f = dir.getChildFile ("models").getChildFile ("htdemucs.onnx");
        if (f.existsAsFile()) return f;
    }
    return exeDir.getChildFile ("models").getChildFile ("htdemucs.onnx");
}

juce::File AppSettings::stemCacheDir() const
{
    if (stemCachePath.isNotEmpty()) return juce::File (stemCachePath);
   #if JUCE_WINDOWS
    auto base = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
   #else
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #endif
    auto dir = base.getChildFile ("Centrifuge").getChildFile ("Stems");
    auto old = base.getChildFile ("OrbitDJ").getChildFile ("Stems");
    if (! dir.exists() && old.isDirectory())
    {
        dir.getParentDirectory().createDirectory();
        old.moveFileTo (dir);
    }
    return dir;
}

void AppSettings::load()
{
    auto v = juce::JSON::parse (settingsFile());
    if (! v.isObject()) return;

    auto str = [&] (const char* k, juce::String& out) { if (v.hasProperty (k)) out = v[k].toString(); };
    auto num = [&] (const char* k, auto& out) { if (v.hasProperty (k)) out = static_cast<std::decay_t<decltype (out)>> ((double) v[k]); };
    auto boolean = [&] (const char* k, bool& out) { if (v.hasProperty (k)) out = (bool) v[k]; };

    str ("rekordboxDbPath", rekordboxDbPath);
    str ("rekordboxDbKey", rekordboxDbKey);
    if (auto* arr = v["usbRoots"].getArray())
        for (auto& r : *arr) usbRoots.addIfNotAlreadyThere (r.toString());

    str ("audioDeviceState", audioDeviceState);
    num ("bufferSize", bufferSize);
    if (auto* arr = v["deckOutputs"].getArray())
        for (int i = 0; i < juce::jmin (2, arr->size()); ++i)
            deckOutputs[(size_t) i] = (int) (*arr)[i];
    num ("sumToMaster", sumToMaster);
    num ("mp3OffsetMs", mp3OffsetMs);

    num ("waveformStyle", waveformStyle);
    num ("mainLaneMode", mainLaneMode);
    num ("deckPanelHeight", deckPanelHeight);
    if (auto* arr = v["laneSplit"].getArray())
        for (int i = 0; i < juce::jmin (2, arr->size()); ++i) laneSplit[(size_t) i] = juce::jlimit (0.1f, 0.9f, (float) (double) (*arr)[i]);
    if (auto* arr = v["stemMatrix"].getArray())
        for (int i = 0; i < juce::jmin (4, arr->size()); ++i) stemMatrix[(size_t) i] = juce::jlimit (0, 15, (int) (*arr)[i]);
    num ("visibleSeconds", visibleSeconds);
    num ("maxFps", maxFps);

    num ("jogTicksPerRev", jogTicksPerRev);
    boolean ("ledFeedback", ledFeedback);

    str ("modelPath", modelPath);
    str ("stemCachePath", stemCachePath);
    boolean ("stemsOnLoad", stemsOnLoad);
    boolean ("stemsUseGpu", stemsUseGpu);
    num ("stemThreads", stemThreads);

    boolean ("showBrowser", showBrowser);
    num ("browserHeight", browserHeight);

    visibleSeconds = juce::jlimit (2.0, 60.0, visibleSeconds);
    maxFps = juce::jlimit (15, 240, maxFps);
    jogTicksPerRev = juce::jlimit (32.0, 16384.0, jogTicksPerRev);
}

void AppSettings::save() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("rekordboxDbPath", rekordboxDbPath);
    o->setProperty ("rekordboxDbKey", rekordboxDbKey);
    juce::Array<juce::var> roots;
    for (auto& r : usbRoots) roots.add (r);
    o->setProperty ("usbRoots", roots);

    o->setProperty ("audioDeviceState", audioDeviceState);
    o->setProperty ("bufferSize", bufferSize);
    o->setProperty ("deckOutputs", juce::Array<juce::var> { deckOutputs[0], deckOutputs[1] });
    o->setProperty ("sumToMaster", sumToMaster);
    o->setProperty ("mp3OffsetMs", mp3OffsetMs);

    o->setProperty ("waveformStyle", waveformStyle);
    o->setProperty ("mainLaneMode", mainLaneMode);
    o->setProperty ("deckPanelHeight", deckPanelHeight);
    o->setProperty ("laneSplit", juce::Array<juce::var> { laneSplit[0], laneSplit[1] });
    o->setProperty ("stemMatrix", juce::Array<juce::var> { stemMatrix[0], stemMatrix[1], stemMatrix[2], stemMatrix[3] });
    o->setProperty ("visibleSeconds", visibleSeconds);
    o->setProperty ("maxFps", maxFps);

    o->setProperty ("jogTicksPerRev", jogTicksPerRev);
    o->setProperty ("ledFeedback", ledFeedback);

    o->setProperty ("modelPath", modelPath);
    o->setProperty ("stemCachePath", stemCachePath);
    o->setProperty ("stemsOnLoad", stemsOnLoad);
    o->setProperty ("stemsUseGpu", stemsUseGpu);
    o->setProperty ("stemThreads", stemThreads);

    o->setProperty ("showBrowser", showBrowser);
    o->setProperty ("browserHeight", browserHeight);

    folder().createDirectory();
    settingsFile().replaceWithText (juce::JSON::toString (juce::var (o)));
}

} // namespace orbit
