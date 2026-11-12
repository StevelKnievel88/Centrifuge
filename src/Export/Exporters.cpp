#include "Exporters.h"
#include "AnlzWriter.h"
#include "PdbWriter.h"
#include "XzStems.h"
#include "../Library/RekordboxColours.h"
#include <map>

namespace orbit
{

namespace
{
    int trackColourId (juce::Colour c)
    {
        if (c.isTransparent()) return 0;
        int best = 0, bestD = std::numeric_limits<int>::max();
        for (int i = 1; i <= 8; ++i)
        {
            const int d = colourDistance (c, rekordboxTrackColour (i));
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    double versionBpm (const ExportItem& it)
    {
        if (! it.version.beats.empty() && it.version.beats.front().bpm > 0.0f) return it.version.beats.front().bpm;
        return it.info.bpm;
    }

    juce::String fileUrl (const juce::File& f)
    {
        auto path = f.getFullPathName().replaceCharacter ('\\', '/');
        juce::String out;
        const auto utf8 = path.toStdString();
        for (unsigned char c : utf8)
        {
            if (juce::CharacterFunctions::isLetterOrDigit ((juce::juce_wchar) c) || juce::String ("/:-_.~").containsChar ((juce::juce_wchar) c))
                out << (char) c;
            else
                out << "%" << juce::String::toHexString ((int) c).paddedLeft ('0', 2).toUpperCase();
        }
        if (! out.startsWith ("/")) out = "/" + out;
        return "file://localhost" + out;
    }

    juce::String kindFor (const juce::File& f)
    {
        const auto e = f.getFileExtension().toLowerCase();
        if (e == ".mp3") return "MP3 File";
        if (e == ".wav") return "WAV File";
        if (e == ".flac") return "FLAC File";
        if (e == ".aif" || e == ".aiff") return "AIFF File";
        if (e == ".m4a" || e == ".aac" || e == ".mp4") return "M4A File";
        return "Audio File";
    }

    juce::String safeName (juce::String s, const juce::String& fallback)
    {
        s = juce::File::createLegalFileName (s.trim()).trimCharactersAtEnd (". ");
        return s.isEmpty() ? fallback : s.substring (0, 80);
    }
}

//==============================================================================
bool writeRekordboxXml (const juce::File& file, const std::vector<ExportItem>& items, const ExportList& playlists,
                        juce::String& error)
{
    juce::XmlElement root ("DJ_PLAYLISTS");
    root.setAttribute ("Version", "1.0.0");
    auto* product = root.createNewChildElement ("PRODUCT");
    product->setAttribute ("Name", "Centrifuge");
    product->setAttribute ("Version", JUCE_APPLICATION_VERSION_STRING);
    product->setAttribute ("Company", "Centrifuge");

    static const char* trackColours[] = { "", "0xFF007F", "0xFF0000", "0xFFA500", "0xFFFF00", "0x00FF00", "0x25FDE9", "0x0000FF", "0x660099" };

    auto* collection = root.createNewChildElement ("COLLECTION");
    collection->setAttribute ("Entries", (int) items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& it = items[i];
        const auto& t = it.info;
        auto* tr = collection->createNewChildElement ("TRACK");
        tr->setAttribute ("TrackID", (int) i + 1);
        tr->setAttribute ("Name", t.title);
        tr->setAttribute ("Artist", t.artist);
        tr->setAttribute ("Album", t.album);
        tr->setAttribute ("Genre", t.genre);
        tr->setAttribute ("Kind", kindFor (t.file));
        tr->setAttribute ("Size", juce::String (t.file.getSize()));
        tr->setAttribute ("TotalTime", (int) std::lround (t.durationSec));
        tr->setAttribute ("AverageBpm", juce::String (versionBpm (it), 2));
        tr->setAttribute ("DateAdded", t.dateAdded.isNotEmpty() ? t.dateAdded.substring (0, 10) : juce::Time::getCurrentTime().formatted ("%Y-%m-%d"));
        tr->setAttribute ("BitRate", t.bitrate);
        tr->setAttribute ("SampleRate", t.sampleRate);
        tr->setAttribute ("Comments", t.comment);
        tr->setAttribute ("Rating", juce::jlimit (0, 5, t.rating) * 51);
        tr->setAttribute ("Location", fileUrl (t.file));
        tr->setAttribute ("Tonality", t.key);
        tr->setAttribute ("Label", t.label);
        if (const int c = trackColourId (t.colour); c > 0) tr->setAttribute ("Colour", trackColours[c]);

        // Beat grid: one TEMPO entry where the grid starts and wherever the tempo changes
        const auto& beats = it.version.beats;
        for (size_t b = 0; b < beats.size(); ++b)
            if (b == 0 || std::abs (beats[b].bpm - beats[b - 1].bpm) > 0.005f)
            {
                auto* tempo = tr->createNewChildElement ("TEMPO");
                tempo->setAttribute ("Inizio", juce::String (beats[b].timeSec, 3));
                tempo->setAttribute ("Bpm", juce::String (beats[b].bpm, 2));
                tempo->setAttribute ("Metro", "4/4");
                tempo->setAttribute ("Battito", beats[b].beatInBar);
            }

        auto mark = [&] (const CuePoint& c, int num)
        {
            auto* pm = tr->createNewChildElement ("POSITION_MARK");
            pm->setAttribute ("Name", c.comment);
            pm->setAttribute ("Type", c.isLoop() ? 4 : 0);
            pm->setAttribute ("Start", juce::String (c.startSec, 3));
            if (c.isLoop()) pm->setAttribute ("End", juce::String (c.endSec, 3));
            pm->setAttribute ("Num", num);
            if (num >= 0 && ! c.colour.isTransparent())
            {
                pm->setAttribute ("Red", c.colour.getRed());
                pm->setAttribute ("Green", c.colour.getGreen());
                pm->setAttribute ("Blue", c.colour.getBlue());
            }
        };
        for (auto& m : it.version.memory) mark (m, -1);
        for (int h = 0; h < 8; ++h)
            if (it.version.hot[(size_t) h]) mark (*it.version.hot[(size_t) h], h);
    }

    auto* pls = root.createNewChildElement ("PLAYLISTS");
    std::function<void (juce::XmlElement*, const ExportList&)> addNode = [&] (juce::XmlElement* parent, const ExportList& l)
    {
        auto* node = parent->createNewChildElement ("NODE");
        node->setAttribute ("Name", l.name);
        if (l.folder)
        {
            node->setAttribute ("Type", 0);
            node->setAttribute ("Count", (int) l.children.size());
            for (auto& c : l.children) addNode (node, c);
        }
        else
        {
            node->setAttribute ("Type", 1);
            node->setAttribute ("KeyType", 0);
            node->setAttribute ("Entries", (int) l.items.size());
            for (int i : l.items)
                node->createNewChildElement ("TRACK")->setAttribute ("Key", i + 1);
        }
    };
    auto* rootNode = pls->createNewChildElement ("NODE");
    rootNode->setAttribute ("Type", 0);
    rootNode->setAttribute ("Name", "ROOT");
    rootNode->setAttribute ("Count", (int) playlists.children.size());
    for (auto& c : playlists.children) addNode (rootNode, c);

    if (! root.writeTo (file))
    {
        error = "Couldn't write " + file.getFullPathName();
        return false;
    }
    return true;
}

//==============================================================================
bool exportToUsb (const juce::File& root, const std::vector<ExportItem>& items, const ExportList& lists,
                  AudioFileLoader& loader, const ExportProgress& progress, juce::String& error, juce::String& summary,
                  const XzStemExport* xz)
{
    if (! root.isDirectory()) { error = "The export folder doesn't exist: " + root.getFullPathName(); return false; }

    auto pioneer = root.getChildFile ("PIONEER");
    auto rbDir = pioneer.getChildFile ("rekordbox");
    rbDir.createDirectory();

    // Keep the stick's previous rekordbox database
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    for (auto name : { "export.pdb", "exportExt.pdb" })
    {
        auto f = rbDir.getChildFile (name);
        if (f.existsAsFile())
            f.moveFileTo (rbDir.getChildFile (juce::String (name) + ".before-centrifuge-" + stamp));
    }

    std::vector<PdbWriter::Track> pdbTracks;
    auto extras = std::make_unique<juce::DynamicObject>();
    int copied = 0, skipped = 0;
    int xzWritten = 0, xzNoStems = 0, xzFailed = 0;
    std::map<juce::String, int> xzSkipped;   // reason -> count
    const auto today = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");

    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& it = items[i];
        const auto& t = it.info;
        if (progress.report && ! progress.report ((float) i / (float) juce::jmax<size_t> (1, items.size()), t.displayArtistTitle()))
        {
            error = "Export cancelled";
            return false;
        }
        if (! t.file.existsAsFile()) { ++skipped; continue; }

        // Audio: /Contents/Artist/Album/file
        const auto artistDir = safeName (t.artist, "UnknownArtist");
        const auto albumDir = safeName (t.album, "UnknownAlbum");
        auto dest = root.getChildFile ("Contents").getChildFile (artistDir).getChildFile (albumDir).getChildFile (t.file.getFileName());
        dest.getParentDirectory().createDirectory();
        if (! dest.existsAsFile() || dest.getSize() != t.file.getSize())
        {
            if (! t.file.copyFileTo (dest)) { error = "Couldn't copy " + t.file.getFullPathName(); return false; }
            ++copied;
        }
        const auto usbPath = "/Contents/" + artistDir + "/" + albumDir + "/" + t.file.getFileName();

        // Analysis files: /PIONEER/USBANLZ/Pxxx/xxxxxxxx/ANLZ0000.DAT
        const auto h = (juce::uint32) usbPath.hashCode64();
        const auto anlzRel = "/PIONEER/USBANLZ/P" + juce::String::toHexString ((juce::int64) (h & 0xfff)).paddedLeft ('0', 3).toUpperCase()
                           + "/" + juce::String::toHexString ((juce::int64) ((h >> 4) | 0x10000000u)).paddedLeft ('0', 8).toUpperCase() + "/ANLZ0000.DAT";
        AnlzWriter::Input in;
        in.version = it.version;
        in.usbPath = usbPath;
        in.originalDat = t.anlzBase.existsAsFile() ? t.anlzBase : juce::File();
        if (in.originalDat == juce::File())
        {
            juce::String loadError;
            if (auto audio = loader.load (t.file, loadError, nullptr))
                in.generatedWave = WaveformData::fromAudio (*audio);
        }
        juce::String anlzError;
        if (! AnlzWriter::write (root.getChildFile (anlzRel.substring (1)), in, anlzError)) { error = anlzError; return false; }

        PdbWriter::Track p;
        p.id = (juce::uint32) i + 1;
        p.title = t.title;
        p.artist = t.artist;
        p.album = t.album;
        p.genre = t.genre;
        p.key = t.key;
        p.label = t.label;
        p.comment = t.comment;
        p.dateAdded = t.dateAdded.isNotEmpty() ? t.dateAdded.substring (0, 10) : today;
        p.filePath = usbPath;
        p.analyzePath = anlzRel;
        p.analyzeDate = today;
        p.sampleRate = (juce::uint32) (t.sampleRate > 0 ? t.sampleRate : 44100);
        p.fileSize = (juce::uint32) t.file.getSize();
        p.bitrate = (juce::uint32) juce::jmax (0, t.bitrate);
        p.tempo = (juce::uint32) std::lround (versionBpm (it) * 100.0);
        p.duration = (juce::uint32) std::lround (t.durationSec);
        p.colour = trackColourId (t.colour);
        p.rating = juce::jlimit (0, 5, t.rating);
        p.fileType = PdbWriter::fileTypeFor (t.file);
        pdbTracks.push_back (p);

        if (it.key.isNotEmpty())
            extras->setProperty (it.key, it.centrifuge.toVar());

        // Stems for the XDJ-XZ (Centrifuge loader). Hashed from the copy on the stick, so it must stay unchanged.
        if (xz != nullptr && xz->loadStems)
        {
            if (progress.report && ! progress.report ((float) i / (float) juce::jmax<size_t> (1, items.size()), "XZ stems: " + t.displayArtistTitle()))
            {
                error = "Export cancelled";
                return false;
            }
            juce::String loadError;
            auto audio = loader.load (dest, loadError, nullptr);
            if (audio == nullptr) { ++xzFailed; continue; }
            auto stems = xz->loadStems (t, *audio);
            if (stems == nullptr) { ++xzNoStems; continue; }
            juce::String reason;
            const auto withinTrack = [&] (float f)
            {
                return ! progress.report
                    || progress.report (((float) i + f) / (float) juce::jmax<size_t> (1, items.size()), "XZ stems: " + t.displayArtistTitle());
            };
            if (! withinTrack (0.0f)) { error = "Export cancelled"; return false; }
            // Short WAV/FLAC tracks use the small in-memory layout; everything else is streamed (no 6:20 limit).
            switch (XzStems::fitsShortLayout (dest, *audio) ? XzStems::write (root, dest, *audio, *stems, reason)
                                                            : XzStems::writePaged (root, dest, *audio, *stems, reason, withinTrack))
            {
                case XzStems::Result::written: ++xzWritten; break;
                case XzStems::Result::skipped:
                    if (reason == "stopped") { error = "Export cancelled"; return false; }
                    ++xzSkipped[reason];
                    break;
                case XzStems::Result::failed:  ++xzFailed; break;
            }
        }
    }

    // Playlists (ids follow the tree order; only tracks that made it onto the stick)
    std::map<int, juce::uint32> trackIdForItem;
    for (auto& p : pdbTracks) trackIdForItem[(int) p.id - 1] = p.id;
    std::vector<PdbWriter::Playlist> pls;
    std::function<void (const ExportList&, juce::uint32)> walk = [&] (const ExportList& l, juce::uint32 parent)
    {
        juce::uint32 sort = 0;
        for (auto& c : l.children)
        {
            PdbWriter::Playlist p;
            p.id = (juce::uint32) pls.size() + 1;
            p.parent = parent;
            p.sortOrder = sort++;
            p.folder = c.folder;
            p.name = c.name;
            for (int i : c.items)
                if (auto f = trackIdForItem.find (i); f != trackIdForItem.end()) p.trackIds.push_back (f->second);
            const auto id = p.id;
            pls.push_back (p);
            if (c.folder) walk (c, id);
        }
    };
    walk (lists, 0);

    if (! PdbWriter::write (rbDir.getChildFile ("export.pdb"), pdbTracks, pls, error)) return false;

    auto cfDir = pioneer.getChildFile ("Centrifuge");
    cfDir.createDirectory();
    cfDir.getChildFile ("library.json").replaceWithText (juce::JSON::toString (juce::var (extras.release()), true));

    summary = juce::String ((int) pdbTracks.size()) + " tracks and " + juce::String ((int) pls.size()) + " playlists written ("
            + juce::String (copied) + " audio files copied)";
    if (skipped > 0) summary << ", " << skipped << " skipped (audio file not found)";
    if (xz != nullptr)
    {
        summary << ". XZ stems: " << xzWritten << " written";
        if (xzNoStems > 0) summary << ", " << xzNoStems << " not separated yet";
        for (auto& [why, count] : xzSkipped) summary << ", " << count << " " << why;
        if (xzFailed > 0) summary << ", " << xzFailed << " failed";
    }
    return true;
}

} // namespace orbit
