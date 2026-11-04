#include "Actions.h"

namespace orbit
{

static std::vector<ActionDef> buildActions()
{
    std::vector<ActionDef> a;
    auto add = [&] (juce::String id, juce::String label, juce::String group, ActionKind kind, int deck, bool led = false)
    {
        a.push_back ({ std::move (id), std::move (label), std::move (group), kind, deck, led });
    };

    for (int d = 0; d < 2; ++d)
    {
        const juce::String p = "d" + juce::String (d + 1) + ".";
        const juce::String D = "Deck " + juce::String (d + 1);
        const auto B = ActionKind::button, C = ActionKind::continuous, E = ActionKind::encoder;

        add (p + "play",        "Play / Pause",          D + " - Transport", B, d, true);
        add (p + "cue",         "Cue",                   D + " - Transport", B, d, true);
        add (p + "shift",       "Shift",                 D + " - Transport", B, d);
        add (p + "load",        "Load selected track",   D + " - Transport", B, d);
        add (p + "eject",       "Eject",                 D + " - Transport", B, d);
        add (p + "needle",      "Needle search (position)", D + " - Transport", C, d);

        add (p + "jog.touch",   "Jog: top touch",        D + " - Jog", B, d);
        add (p + "jog.turn",    "Jog: turn (top)",       D + " - Jog", E, d);
        add (p + "jog.side",    "Jog: turn (side / pitch bend)", D + " - Jog", E, d);
        add (p + "jog.vinyl",   "Vinyl mode on/off",     D + " - Jog", B, d, true);

        add (p + "pitch",       "Tempo slider",          D + " - Tempo", C, d);
        add (p + "pitchrange",  "Tempo range (6/10/16/Wide)", D + " - Tempo", B, d);
        add (p + "tempo.reset", "Tempo reset",           D + " - Tempo", B, d);
        add (p + "bend.up",     "Pitch bend +",          D + " - Tempo", B, d);
        add (p + "bend.down",   "Pitch bend -",          D + " - Tempo", B, d);
        add (p + "keylock",     "Master tempo (key lock)", D + " - Tempo", B, d, true);
        add (p + "sync",        "Beat sync",             D + " - Tempo", B, d, true);
        add (p + "master",      "Master",                D + " - Tempo", B, d, true);
        add (p + "quantize",    "Quantize",              D + " - Tempo", B, d, true);

        for (int h = 0; h < 8; ++h)
        {
            const juce::String letter = juce::String::charToString ((juce::juce_wchar) ('A' + h));
            add (p + "hotcue." + juce::String (h + 1), "Hot cue " + letter, D + " - Hot cues", B, d, true);
            add (p + "hotcue." + juce::String (h + 1) + ".delete", "Delete hot cue " + letter, D + " - Hot cues", B, d);
        }

        add (p + "callprev",       "Locator: previous (call)",    D + " - Locators & skips", B, d);
        add (p + "callnext",       "Locator: next (call)",        D + " - Locators & skips", B, d);
        add (p + "locator.add",    "Locator: add (memory)",       D + " - Locators & skips", B, d);
        add (p + "locator.delete", "Locator: delete current",     D + " - Locators & skips", B, d);
        add (p + "skip.set",       "Skip: set start, then end (press twice)", D + " - Locators & skips", B, d, true);
        add (p + "skip.in",        "Skip: start (skip from)",     D + " - Locators & skips", B, d, true);
        add (p + "skip.out",       "Skip: end (skip to)",         D + " - Locators & skips", B, d);
        add (p + "skip.delete",    "Skip: delete nearest",        D + " - Locators & skips", B, d);
        add (p + "skip.toggle",    "Skips on / off",              D + " - Locators & skips", B, d, true);

        add (p + "loop.in",     "Loop in",               D + " - Loops", B, d, true);
        add (p + "loop.out",    "Loop out",              D + " - Loops", B, d, true);
        add (p + "loop.reloop", "Reloop / exit",         D + " - Loops", B, d, true);
        add (p + "loop.halve",  "Loop 1/2X",             D + " - Loops", B, d);
        add (p + "loop.double", "Loop 2X",               D + " - Loops", B, d);
        for (auto beats : { "1/4", "1/2", "1", "2", "4", "8", "16", "32" })
            add (p + "beatloop." + juce::String (beats), juce::String (beats) + " beat loop", D + " - Loops", B, d, true);
        add (p + "beatjump.back", "Beat jump back",      D + " - Loops", B, d);
        add (p + "beatjump.fwd",  "Beat jump forward",   D + " - Loops", B, d);
        add (p + "beatjump.size", "Beat jump size (encoder)", D + " - Loops", E, d);

        const char* stemNames[] = { "Drums", "Bass", "Inst", "Vocals" };
        for (int s = 0; s < 4; ++s)
        {
            add (p + "stem." + juce::String (s + 1) + ".volume", juce::String (stemNames[s]) + " volume", D + " - Stems", C, d);
            add (p + "stem." + juce::String (s + 1) + ".mute",   juce::String (stemNames[s]) + " mute",   D + " - Stems", B, d, true);
            add (p + "stem." + juce::String (s + 1) + ".solo",   juce::String (stemNames[s]) + " solo",   D + " - Stems", B, d);
        }
        add (p + "stem.reset", "Stems: reset all", D + " - Stems", B, d);

        for (auto letter : { "A", "B", "C", "D" })
        {
            add (p + "stemctl." + juce::String (letter) + ".volume", "Stem control " + juce::String (letter) + " volume (matrix)", D + " - Stem controls", C, d);
            add (p + "stemctl." + juce::String (letter) + ".mute",   "Stem control " + juce::String (letter) + " mute (matrix)",   D + " - Stem controls", B, d, true);
        }

        add (p + "edit.r",    "Edit the rekordbox copy (R) on / off",   D + " - Library editing", B, d);
        add (p + "edit.c",    "Edit the Centrifuge version (C) on / off", D + " - Library editing", B, d);
        add (p + "grid.mode", "Beat grid editor on / off",              D + " - Library editing", B, d);
        const std::pair<const char*, const char*> gridActions[] = {
            { "shift.back10", "Grid: move 10 ms earlier" }, { "shift.back1", "Grid: move 1 ms earlier" },
            { "shift.fwd1", "Grid: move 1 ms later" }, { "shift.fwd10", "Grid: move 10 ms later" },
            { "anchor", "Grid: put the nearest beat on the playhead" }, { "downbeat", "Grid: nearest beat is beat 1" },
            { "bpm.up", "Grid: BPM +0.1" }, { "bpm.down", "Grid: BPM -0.1" }, { "bpm.upfine", "Grid: BPM +0.01" },
            { "bpm.downfine", "Grid: BPM -0.01" }, { "halve", "Grid: halve BPM" }, { "double", "Grid: double BPM" },
            { "tap", "Grid: tap tempo" }, { "undo", "Grid: undo" }, { "reset", "Grid: reset to rekordbox's grid" } };
        for (auto& [id, label] : gridActions)
            add (p + "grid." + id, label, D + " - Library editing", B, d);

        add (p + "key.up",    "Key +1 semitone",  D + " - Key", B, d);
        add (p + "key.down",  "Key -1 semitone",  D + " - Key", B, d);
        add (p + "key.reset", "Key reset",        D + " - Key", B, d);
        add (p + "key.shift", "Key shift (encoder)", D + " - Key", E, d);
        add (p + "key.sync",  "Key sync",         D + " - Key", B, d, true);
    }

    const auto B = ActionKind::button, E = ActionKind::encoder;
    add ("browse.scroll",   "Browse: scroll",          "Browser", E, -1);
    add ("browse.enter",    "Browse: select / open",   "Browser", B, -1);
    add ("browse.back",     "Browse: back",            "Browser", B, -1);
    add ("browse.tree",     "Browse: playlists / tracks focus", "Browser", B, -1);
    add ("browse.load1",    "Load to deck 1",          "Browser", B, -1);
    add ("browse.load2",    "Load to deck 2",          "Browser", B, -1);
    add ("view.browser",    "Show / hide browser",     "View", B, -1);
    add ("view.zoom",       "Waveform zoom (encoder)", "View", E, -1);
    add ("view.zoomin",     "Waveform zoom in",        "View", B, -1);
    add ("view.zoomout",    "Waveform zoom out",       "View", B, -1);
    add ("view.wavemode",   "Stem lanes: show stems / 3-band", "View", B, -1);
    add ("view.fold",       "Fold skipped sections on / off", "View", B, -1);
    return a;
}

const std::vector<ActionDef>& allActions()
{
    static const std::vector<ActionDef> actions = buildActions();
    return actions;
}

const ActionDef* findAction (const juce::String& id)
{
    for (auto& a : allActions())
        if (a.id == id)
            return &a;
    return nullptr;
}

} // namespace orbit
