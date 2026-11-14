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

juce::File AppSettings::findModel (const juce::String& name) const
{
    auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    juce::Array<juce::File> dirs { modelFile().getParentDirectory(), exeDir.getChildFile ("models"),
                                   exeDir.getParentDirectory().getChildFile ("models"), downloadedModelsFolder() };
    for (auto& d : dirs)
        if (d.getChildFile (name).existsAsFile()) return d.getChildFile (name);
    return downloadedModelsFolder().getChildFile (name);
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
    num ("analysisRange", analysisRange);
    num ("previewOutput", previewOutput);
    boolean ("prepEditR", prepEditR);
    num ("prepListWidth", prepListWidth);
    boolean ("prepEditC", prepEditC);
    boolean ("camelotKeys", camelotKeys);
    if (auto* dc = v["deckColours"].getArray())
        for (int i = 0; i < juce::jmin (3, dc->size()); ++i)
            deckColours[(size_t) i] = (juce::uint32) (juce::int64) (*dc)[i];

    // Lanes (older settings had one main-lane mode and one original-lane style for both decks)
    if (auto* arr = v["laneStyle"].getArray())
    {
        for (int i = 0; i < juce::jmin (4, arr->size()); ++i) laneStyle[(size_t) i / 2][(size_t) i % 2] = juce::jlimit (i % 2 == 1 ? -1 : 0, 8, (int) (*arr)[i]);
    }
    else if (v.hasProperty ("mainLaneMode") || v.hasProperty ("waveformStyle"))
    {
        const int main = (int) v.getProperty ("mainLaneMode", 0) == 0 ? 0 : 1;
        const int orig = juce::jlimit (0, 2, (int) v.getProperty ("waveformStyle", 0)) + 1;
        laneStyle = { { { main, orig }, { main, orig } } };
    }
    if (auto* arr = v["laneShape"].getArray())
        for (int i = 0; i < juce::jmin (4, arr->size()); ++i) laneShape[(size_t) i / 2][(size_t) i % 2] = juce::jlimit (0, 2, (int) (*arr)[i]);
    // Older settings: one "stems in their own rows" switch for every stem lane
    if ((bool) v.getProperty ("stemsSeparate", false))
        for (auto& d : laneStyle) for (auto& l : d) if (l == 0) l = 4;
    if (auto* arr = v["prepLane"].getArray(); arr != nullptr && arr->size() == 4)
    {
        prepLaneStyle = { juce::jlimit (0, 8, (int) (*arr)[0]), juce::jlimit (-1, 8, (int) (*arr)[1]) };
        prepLaneShape = { juce::jlimit (0, 2, (int) (*arr)[2]), juce::jlimit (0, 2, (int) (*arr)[3]) };
    }
    num ("prepLaneSplit", prepLaneSplit);
    num ("prepOverviewStyle", prepOverviewStyle);
    num ("prepOverviewShape", prepOverviewShape);
    num ("prepVisibleSeconds", prepVisibleSeconds);
    boolean ("overviewGrid", overviewGrid);
    if (auto* arr = v["stemColours"].getArray())
        for (int i = 0; i < juce::jmin (4, arr->size()); ++i) stemColours[(size_t) i] = (juce::uint32) (juce::int64) (*arr)[i];
    if (auto* arr = v["stemModes"].getArray())
        for (int i = 0; i < juce::jmin (12, arr->size()); ++i) stemModes[(size_t) i / 4][(size_t) i % 4] = (int) (*arr)[i] > 0 ? 1 : 0;   // 0 VOL, 1 FILTER (older LP / HP = FILTER)
    num ("noteShowSeconds", noteShowSeconds);
    num ("noteLeadSeconds", noteLeadSeconds);
    if (auto* arr = v["collapsedSections"].getArray())
        for (auto& c : *arr) collapsedSections.addIfNotAlreadyThere (c.toString());
    str ("browserColumns", browserColumns);
    str ("browserXzColumns", browserXzColumns);
    boolean ("browserInfoOpen", browserInfoOpen);
    str ("windowState", windowState);
    num ("phraseBars", phraseBars);
    boolean ("phraseFollow", phraseFollow);
    phraseBrackets = false;   // (the brackets option was taken out)
    num ("previewStyle", previewStyle);
    num ("previewShape", previewShape);
    num ("overviewStyle", overviewStyle);
    num ("overviewShape", overviewShape);
    if (auto* ev = v["editView"].getArray())
        for (int i = 0; i < juce::jmin (3, ev->size()); ++i) editView[(size_t) i] = (bool) (*ev)[i];
    else if (v.hasProperty ("foldSkips"))
        editView = { (bool) v["foldSkips"], (bool) v["foldSkips"], (bool) v["foldSkips"] };
    if (v.hasProperty ("singleFollowsDeck")) singleFollowsDeck = (bool) v["singleFollowsDeck"];
    if (v.hasProperty ("singleColour")) singleColour = (juce::uint32) juce::Colour::fromString (v["singleColour"].toString()).getARGB();
    if (auto* arr = v["stemLayerOrder"].getArray(); arr != nullptr && arr->size() == 4)
    {
        std::array<int, 4> o {};
        int seen = 0;
        for (int i = 0; i < 4; ++i) { o[(size_t) i] = juce::jlimit (0, 3, (int) (*arr)[i]); seen |= 1 << o[(size_t) i]; }
        if (seen == 15) stemLayerOrder = o;   // must be each stem once
    }
    if (auto* arr = v["stemOpacity"].getArray())
        for (int i = 0; i < juce::jmin (4, arr->size()); ++i) stemOpacity[(size_t) i] = juce::jlimit (0.0f, 1.0f, (float) (double) (*arr)[i]);
    boolean ("foldSkips", foldSkips);
    num ("playheadPosition", playheadPosition);
    boolean ("quantize", quantize);
    if (auto* arr = v["editTargets"].getArray(); arr != nullptr && arr->size() == 4)
    {
        editR = { (bool) (*arr)[0], (bool) (*arr)[2] };
        editC = { (bool) (*arr)[1], (bool) (*arr)[3] };
        for (int d = 0; d < 2; ++d) if (! editR[(size_t) d] && ! editC[(size_t) d]) editC[(size_t) d] = true;
    }
    num ("deckPanelHeight", deckPanelHeight);
    if (auto* arr = v["laneSplit"].getArray())
        for (int i = 0; i < juce::jmin (2, arr->size()); ++i) laneSplit[(size_t) i] = juce::jlimit (0.1f, 0.9f, (float) (double) (*arr)[i]);
    if (auto* arr = v["stemMatrix"].getArray())
    {
        for (int i = 0; i < juce::jmin (8, arr->size()); ++i) stemMatrix[(size_t) i] = juce::jlimit (0, 15, (int) (*arr)[i]);
        if (arr->size() == 4)   // from before each deck had its own: both decks start the same
            for (int i = 0; i < 4; ++i) stemMatrix[(size_t) (i + 4)] = stemMatrix[(size_t) i];
    }
    if (auto* arr = v["jumpBeats"].getArray())
        for (int i = 0; i < juce::jmin (8, arr->size()); ++i) jumpBeats[(size_t) i] = juce::jlimit (0.0625, 128.0, (double) (*arr)[i]);
    if (auto* arr = v["jumpColours"].getArray())
        for (int i = 0; i < juce::jmin (8, arr->size()); ++i) jumpColours[(size_t) i] = (juce::uint32) (juce::int64) (*arr)[i];
    num ("visibleSeconds", visibleSeconds);
    num ("maxFps", maxFps);

    num ("jogTicksPerRev", jogTicksPerRev);
    boolean ("ledFeedback", ledFeedback);
    for (int d = 0; d < 2; ++d)
    {
        const auto n = juce::String (d + 1);
        num (("brakeSec" + n).toRawUTF8(), brakeSec[(size_t) d]);
        num (("brakeBeats" + n).toRawUTF8(), brakeBeats[(size_t) d]);
        num (("startSec" + n).toRawUTF8(), startSec[(size_t) d]);
        num (("releaseSec" + n).toRawUTF8(), releaseSec[(size_t) d]);
        boolean (("brakeOn" + n).toRawUTF8(), brakeOn[(size_t) d]);
        boolean (("startOn" + n).toRawUTF8(), startOn[(size_t) d]);
        str (("deckController" + n).toRawUTF8(), deckController[(size_t) d]);
    }
    boolean ("throwOn", throwOn);
    num ("browserRowHeight", browserRowHeight);
    num ("historySeconds", historySeconds);
    str ("recordFolder", recordFolder);
    num ("recordFormat", recordFormat);
    num ("recordSource", recordSource);
    str ("lamePath", lamePath);
    boolean ("recordCue", recordCue);
    boolean ("recordSplit", recordSplit);
    boolean ("recordArmed", recordArmed);
    recordFormat = juce::jlimit (0, 2, recordFormat);
    browserRowHeight = juce::jlimit (20, 72, browserRowHeight);
    historySeconds = juce::jlimit (5.0, 300.0, historySeconds);
    num ("longPressSec", longPressSec);
    boolean ("hotCuePadColours", hotCuePadColours);
    boolean ("jogDisplay", jogDisplay);
    boolean ("xzScreen", xzScreen);
    num ("xzScreenLeadMs", xzScreenLeadMs);
    xzScreenLeadMs = juce::jlimit (0.0, 400.0, xzScreenLeadMs);
    longPressSec = juce::jlimit (0.25, 1.0, longPressSec);
    num ("markerScale", markerScale);
    markerScale = juce::jlimit (0.75f, 3.0f, markerScale);
    if (v.hasProperty ("previewColour")) previewColour = (juce::uint32) (juce::int64) v["previewColour"];
    else previewColour = singleColour;
    num ("throwSpeed", throwSpeed);
    boolean ("autoGain", autoGain);
    num ("masterVolume", masterVolume);
    masterVolume = juce::jlimit (0.0f, 1.0f, masterVolume);
    throwSpeed = juce::jlimit (0.1, 4.0, throwSpeed);

    str ("modelPath", modelPath);
    str ("stemCachePath", stemCachePath);
    boolean ("stemsOnLoad", stemsOnLoad);
    boolean ("stemsUseGpu", stemsUseGpu);
    num ("stemQualityOnLoad", stemQualityOnLoad);
    num ("stemQualityBatch", stemQualityBatch);
    num ("stemThreads", stemThreads);
    boolean ("stemsLossless", stemsLossless);
    boolean ("stemsOgg320", stemsOgg320);

    boolean ("showBrowser", showBrowser);
    boolean ("statusFolded", statusFolded);
    boolean ("artistFirst", artistFirst);
    boolean ("autoMaster", autoMaster);
    boolean ("tagColourNames", tagColourNames);
    num ("browserHeight", browserHeight);

    visibleSeconds = juce::jlimit (1.0, 60.0, visibleSeconds);
    overviewStyle = juce::jlimit (1, 3, overviewStyle);
    prepOverviewStyle = juce::jlimit (1, 3, prepOverviewStyle);
    phraseBars = juce::jlimit (4, 64, phraseBars);
    previewStyle = juce::jlimit (1, 4, previewStyle);
    previewShape = juce::jlimit (0, 1, previewShape);
    prepOverviewShape = juce::jlimit (0, 1, prepOverviewShape);
    prepLaneSplit = juce::jlimit (0.1f, 0.9f, prepLaneSplit);
    prepVisibleSeconds = juce::jlimit (1.0, 120.0, prepVisibleSeconds);
    noteShowSeconds = juce::jlimit (0.0, 120.0, noteShowSeconds);
    noteLeadSeconds = juce::jlimit (0.0, 30.0, noteLeadSeconds);
    overviewShape = juce::jlimit (0, 1, overviewShape);
    playheadPosition = juce::jlimit (0.1f, 0.9f, playheadPosition);
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
    o->setProperty ("analysisRange", analysisRange);
    o->setProperty ("previewOutput", previewOutput);
    o->setProperty ("prepEditR", prepEditR);
    o->setProperty ("prepListWidth", prepListWidth);
    o->setProperty ("prepEditC", prepEditC);
    o->setProperty ("camelotKeys", camelotKeys);
    o->setProperty ("deckColours", juce::Array<juce::var> { (juce::int64) deckColours[0], (juce::int64) deckColours[1], (juce::int64) deckColours[2] });
    o->setProperty ("prepLane", juce::Array<juce::var> { prepLaneStyle[0], prepLaneStyle[1], prepLaneShape[0], prepLaneShape[1] });
    o->setProperty ("prepLaneSplit", prepLaneSplit);
    o->setProperty ("prepOverviewStyle", prepOverviewStyle);
    o->setProperty ("prepOverviewShape", prepOverviewShape);
    o->setProperty ("prepVisibleSeconds", prepVisibleSeconds);
    o->setProperty ("overviewGrid", overviewGrid);
    {
        juce::Array<juce::var> sc, sm, cs;
        for (auto c : stemColours) sc.add ((juce::int64) c);
        for (auto& d : stemModes) for (auto m : d) sm.add (m);
        for (auto& c : collapsedSections) cs.add (c);
        o->setProperty ("stemColours", sc);
        o->setProperty ("stemModes", sm);
        o->setProperty ("collapsedSections", cs);
    }
    o->setProperty ("noteShowSeconds", noteShowSeconds);
    o->setProperty ("noteLeadSeconds", noteLeadSeconds);
    o->setProperty ("browserColumns", browserColumns);
    o->setProperty ("browserXzColumns", browserXzColumns);
    o->setProperty ("browserInfoOpen", browserInfoOpen);
    o->setProperty ("windowState", windowState);
    o->setProperty ("phraseBars", phraseBars);
    o->setProperty ("phraseFollow", phraseFollow);
    o->setProperty ("phraseBrackets", phraseBrackets);
    o->setProperty ("previewStyle", previewStyle);
    o->setProperty ("previewShape", previewShape);

    o->setProperty ("laneStyle", juce::Array<juce::var> { laneStyle[0][0], laneStyle[0][1], laneStyle[1][0], laneStyle[1][1] });
    o->setProperty ("laneShape", juce::Array<juce::var> { laneShape[0][0], laneShape[0][1], laneShape[1][0], laneShape[1][1] });
    o->setProperty ("overviewStyle", overviewStyle);
    o->setProperty ("overviewShape", overviewShape);
    o->setProperty ("singleColour", juce::Colour (singleColour).toString());
    o->setProperty ("singleFollowsDeck", singleFollowsDeck);
    o->setProperty ("editView", juce::Array<juce::var> { editView[0], editView[1], editView[2] });
    o->setProperty ("stemLayerOrder", juce::Array<juce::var> { stemLayerOrder[0], stemLayerOrder[1], stemLayerOrder[2], stemLayerOrder[3] });
    o->setProperty ("stemOpacity", juce::Array<juce::var> { stemOpacity[0], stemOpacity[1], stemOpacity[2], stemOpacity[3] });
    o->setProperty ("foldSkips", foldSkips);
    o->setProperty ("playheadPosition", playheadPosition);
    o->setProperty ("quantize", quantize);
    o->setProperty ("editTargets", juce::Array<juce::var> { editR[0], editC[0], editR[1], editC[1] });
    o->setProperty ("deckPanelHeight", deckPanelHeight);
    o->setProperty ("laneSplit", juce::Array<juce::var> { laneSplit[0], laneSplit[1] });
    {
        juce::Array<juce::var> m;
        for (auto v : stemMatrix) m.add (v);
        o->setProperty ("stemMatrix", m);
        juce::Array<juce::var> jb, jc;
        for (auto v : jumpBeats) jb.add (v);
        for (auto v : jumpColours) jc.add ((juce::int64) v);
        o->setProperty ("jumpBeats", jb);
        o->setProperty ("jumpColours", jc);
    }
    o->setProperty ("visibleSeconds", visibleSeconds);
    o->setProperty ("maxFps", maxFps);

    o->setProperty ("jogTicksPerRev", jogTicksPerRev);
    for (int d = 0; d < 2; ++d)
    {
        const auto n = juce::String (d + 1);
        o->setProperty ("brakeSec" + n, brakeSec[(size_t) d]);
        o->setProperty ("brakeBeats" + n, brakeBeats[(size_t) d]);
        o->setProperty ("startSec" + n, startSec[(size_t) d]);
        o->setProperty ("releaseSec" + n, releaseSec[(size_t) d]);
        o->setProperty ("brakeOn" + n, brakeOn[(size_t) d]);
        o->setProperty ("startOn" + n, startOn[(size_t) d]);
        o->setProperty ("deckController" + n, deckController[(size_t) d]);
    }
    o->setProperty ("throwOn", throwOn);
    o->setProperty ("browserRowHeight", browserRowHeight);
    o->setProperty ("historySeconds", historySeconds);
    o->setProperty ("recordFolder", recordFolder);
    o->setProperty ("recordFormat", recordFormat);
    o->setProperty ("recordSource", recordSource);
    o->setProperty ("lamePath", lamePath);
    o->setProperty ("recordCue", recordCue);
    o->setProperty ("recordSplit", recordSplit);
    o->setProperty ("recordArmed", recordArmed);
    o->setProperty ("longPressSec", longPressSec);
    o->setProperty ("hotCuePadColours", hotCuePadColours);
    o->setProperty ("jogDisplay", jogDisplay);
    o->setProperty ("xzScreen", xzScreen);
    o->setProperty ("xzScreenLeadMs", xzScreenLeadMs);
    o->setProperty ("stemsLossless", stemsLossless);
    o->setProperty ("stemsOgg320", stemsOgg320);
    o->setProperty ("markerScale", (double) markerScale);
    o->setProperty ("previewColour", (juce::int64) previewColour);
    o->setProperty ("throwSpeed", throwSpeed);
    o->setProperty ("autoGain", autoGain);
    o->setProperty ("masterVolume", (double) masterVolume);
    o->setProperty ("ledFeedback", ledFeedback);

    o->setProperty ("modelPath", modelPath);
    o->setProperty ("stemCachePath", stemCachePath);
    o->setProperty ("stemsOnLoad", stemsOnLoad);
    o->setProperty ("stemsUseGpu", stemsUseGpu);
    o->setProperty ("stemQualityOnLoad", stemQualityOnLoad);
    o->setProperty ("stemQualityBatch", stemQualityBatch);
    o->setProperty ("stemThreads", stemThreads);

    o->setProperty ("showBrowser", showBrowser);
    o->setProperty ("statusFolded", statusFolded);
    o->setProperty ("artistFirst", artistFirst);
    o->setProperty ("autoMaster", autoMaster);
    o->setProperty ("tagColourNames", tagColourNames);
    o->setProperty ("browserHeight", browserHeight);

    folder().createDirectory();
    settingsFile().replaceWithText (juce::JSON::toString (juce::var (o)));
}

} // namespace orbit
