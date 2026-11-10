#include "ActionRouter.h"
#include "../Audio/MusicalKey.h"

namespace orbit
{

namespace
{
    constexpr double pitchRanges[] = { 0.06, 0.10, 0.25, 0.50, 1.00 };   // click the range to step through them

    double parseBeats (const juce::String& s)
    {
        if (s.contains ("/"))
            return s.upToFirstOccurrenceOf ("/", false, false).getDoubleValue()
                 / juce::jmax (1.0, s.fromFirstOccurrenceOf ("/", false, false).getDoubleValue());
        return s.getDoubleValue();
    }
}

ActionRouter::ActionRouter (AudioEngine& e) : engine (e)
{
    for (int d = 0; d < 4; ++d)
    {
        rangeIndex[(size_t) d] = 1;  // ±10 %
        beatJump[(size_t) d] = 4.0;
        loopBeats[(size_t) d] = 4.0;
        vinyl[(size_t) d] = true;
        if (d < AudioEngine::maxDecks)
            engine.deck (d).post (Deck::Cmd::setPitchRange, pitchRanges[1]);
    }
    for (int d = 0; d < 2; ++d)
    {
        for (int s = 0; s < 4; ++s)
        {
            stemVolume[(size_t) d][(size_t) s] = 0.5f;      // stem controls are bipolar: centre = unchanged
            controlVolume[(size_t) d][(size_t) s] = 0.5f;
            stemMute[(size_t) d][(size_t) s] = false;
            controlMute[(size_t) d][(size_t) s] = false;
        }
        keySyncLit[(size_t) d] = false;
    }
    for (int c = 0; c < 4; ++c)
        matrix[(size_t) c] = 1 << c;   // A = drums, B = bass, C = inst, D = vocals
}

void ActionRouter::setStemMatrix (const std::array<int, 4>& m)
{
    for (int c = 0; c < 4; ++c)
        matrix[(size_t) c] = m[(size_t) c] & 0xf;
    for (int d = 0; d < AudioEngine::maxDecks; ++d)
        updateStems (d);
}

std::array<int, 4> ActionRouter::getStemMatrix() const
{
    return { matrix[0].load(), matrix[1].load(), matrix[2].load(), matrix[3].load() };
}

void ActionRouter::updateStems (int d)
{
    // Stem controls are bipolar (centre = unchanged). Each stem follows whichever of its own control and the matrix
    // controls it's ticked on is turned furthest from the centre.
    for (int s = 0; s < 4; ++s)
    {
        float g = stemVolume[(size_t) d][(size_t) s].load();
        bool muted = stemMute[(size_t) d][(size_t) s].load();
        for (int c = 0; c < 4; ++c)
            if ((matrix[(size_t) c].load() >> s) & 1)
            {
                const float cv = controlVolume[(size_t) d][(size_t) c].load();
                if (std::abs (cv - 0.5f) > std::abs (g - 0.5f)) g = cv;
                muted = muted || controlMute[(size_t) d][(size_t) c].load();
            }
        engine.deck (d).post (Deck::Cmd::stemGain, muted ? 0.0 : (double) g, s);
    }
}

juce::String ActionRouter::effectiveKeyText (int d) const
{
    auto t = engine.deck (d).getLoadedTrack();
    if (t == nullptr) return {};
    const auto& st = engine.deck (d).state();
    auto key = MusicalKey::parse (t->info.key);
    if (! key.isValid()) return t->info.key;
    double offset = st.keyShift.load();
    if (! st.keyLock.load()) offset += 12.0 * std::log2 (juce::jmax (0.05, st.tempoRatio.load()));
    return key.shifted ((int) std::lround (offset)).toString();
}

void ActionRouter::keySync (int d)
{
    const int other = 1 - d;
    auto mine = engine.deck (d).getLoadedTrack();
    auto theirs = engine.deck (other).getLoadedTrack();
    if (mine == nullptr || theirs == nullptr) return;

    auto myKey = MusicalKey::parse (mine->info.key);
    const auto target = MusicalKey::parse (effectiveKeyText (other));
    if (! myKey.isValid() || ! target.isValid()) return;

    // Key sync works with key lock on, so the tempo doesn't move the key afterwards.
    const auto& st = engine.deck (d).state();
    if (! st.keyLock.load()) engine.deck (d).post (Deck::Cmd::keyLockSet, 1.0);
    engine.deck (d).post (Deck::Cmd::keyShiftSet, (double) MusicalKey::syncShift (myKey, target));
    keySyncLit[(size_t) d] = true;
}

int ActionRouter::getPitchRangePercent (int d) const
{
    return (int) std::lround (pitchRanges[rangeIndex[(size_t) d].load()] * 100.0);
}

void ActionRouter::setJogSensitivity (double ticks)
{
    for (int d = 0; d < AudioEngine::maxDecks; ++d)
        engine.deck (d).setJogResolution (ticks);
}

void ActionRouter::perform (const juce::String& id, const ActionValue& v)
{
    if (auto* a = findAction (id))
        perform (*a, v);
}

void ActionRouter::perform (const ActionDef& a, const ActionValue& v)
{
    using C = Deck::Cmd;

    if (a.deck < 0)
    {
        if (onAppAction)
        {
            auto id = a.id;
            auto fn = onAppAction;
            juce::MessageManager::callAsync ([fn, id, v] { fn (id, -1, v); });
        }
        return;
    }

    const int d = a.deck;
    auto& deck = engine.deck (d);
    const auto name = a.id.fromFirstOccurrenceOf (".", false, false); // e.g. "hotcue.3"

    auto button = [&] (C cmd, double value = 0.0, int idx = 0) { deck.post (cmd, value, idx, v.down); };

    if      (name == "play")        { if (v.down) button (C::playToggle); }
    else if (name == "cue")         button (C::cue);
    else if (name == "shift")       button (C::shift);
    else if (name == "eject")       { if (v.down) button (C::eject); }
    else if (name == "needle")      deck.post (C::seekFraction, v.value);
    else if (name == "callprev")    { if (v.down) button (C::memoryCuePrev); }
    else if (name == "callnext")    { if (v.down) button (C::memoryCueNext); }
    else if (name == "locator.add")    { if (v.down) deck.post (C::locatorAdd, -1.0); }
    else if (name == "locator.delete") { if (v.down) button (C::locatorDeleteNearest); }
    else if (name == "skip.set")       { if (v.down) button (C::skipSet); }
    else if (name == "skip.in")        { if (v.down) button (C::skipIn); }
    else if (name == "skip.out")       { if (v.down) button (C::skipOut); }
    else if (name == "skip.delete")    { if (v.down) button (C::skipDeleteNearest); }
    else if (name == "skip.toggle")    { if (v.down) button (C::skipsToggle); }
    else if (name == "load")
    {
        if (v.down && onAppAction)
        {
            auto fn = onAppAction;
            juce::MessageManager::callAsync ([fn, d, v] { fn ("deck.load", d, v); });
        }
    }
    else if (name.startsWith ("edit.") || name.startsWith ("grid."))
    {
        // Library editing is handled by the app (message thread)
        if (v.down && onAppAction)
        {
            auto fn = onAppAction;
            auto id = name;
            juce::MessageManager::callAsync ([fn, d, id, v] { fn (id, d, v); });
        }
    }
    else if (name == "jog.touch")   button (C::jogTouch);
    else if (name == "jog.turn" || name == "jog.side") deck.post (C::jogTurn, v.delta);
    else if (name == "jog.vinyl")
    {
        if (v.down)
        {
            const bool on = ! vinyl[(size_t) d].load();
            vinyl[(size_t) d] = on;
            deck.post (C::setVinylMode, on ? 1.0 : 0.0);
        }
    }
    else if (name == "pitch")       deck.post (C::setPitchFader, v.value * 2.0 - 1.0);
    else if (name == "pitchrange")
    {
        if (v.down)
        {
            const int idx = (rangeIndex[(size_t) d].load() + 1) % (int) std::size (pitchRanges);
            rangeIndex[(size_t) d] = idx;
            deck.post (C::setPitchRange, pitchRanges[idx]);
        }
    }
    else if (name == "tempo.reset") { if (v.down) button (C::tempoReset); }
    else if (name == "bend.up")     deck.post (C::pitchBend, v.down ? 0.04 : 0.0);
    else if (name == "bend.down")   deck.post (C::pitchBend, v.down ? -0.04 : 0.0);
    else if (name == "keylock")     { if (v.down) button (C::keyLockToggle); }
    else if (name == "sync")        { if (v.down) button (C::syncToggle); }
    else if (name == "master")      { if (v.down) button (C::masterSet); }
    else if (name == "quantize")    { if (v.down) button (C::quantizeToggle); }
    else if (name.startsWith ("hotcue."))
    {
        const int idx = name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        if (name.endsWith (".delete")) { if (v.down) button (C::hotCueDelete, 0.0, idx); }
        else button (C::hotCue, 0.0, idx);
    }
    else if (name == "loop.in")     { if (v.down) button (C::loopIn); }
    else if (name == "loop.out")    { if (v.down) button (C::loopOut); }
    else if (name == "loop.reloop") { if (v.down) button (C::reloopExit); }
    else if (name == "loop.halve")  { if (v.down) button (C::loopHalve); }
    else if (name == "loop.double") { if (v.down) button (C::loopDouble); }
    else if (name.startsWith ("beatloop."))
    {
        if (v.down) button (C::beatLoop, parseBeats (name.fromFirstOccurrenceOf (".", false, false)));
    }
    else if (name == "loop.toggle")
    {
        if (v.down)
        {
            if (engine.deck (d).state().loopActive.load()) button (C::loopOff);
            else button (C::beatLoop, loopBeats[(size_t) d].load());
        }
    }
    else if (name == "loopsize.halve" || name == "loopsize.double")
    {
        if (v.down)
        {
            const bool up = name == "loopsize.double";
            const double b = loopBeats[(size_t) d].load();
            loopBeats[(size_t) d] = up ? juce::jmin (512.0, b * 2.0) : juce::jmax (1.0 / 32.0, b * 0.5);
            if (engine.deck (d).state().loopActive.load()) button (up ? C::loopDouble : C::loopHalve);
        }
    }
    else if (name.startsWith ("beatjump.back.") || name.startsWith ("beatjump.fwd."))
    {
        const double beats = name.fromLastOccurrenceOf (".", false, false).getDoubleValue();
        if (beats > 0.0) button (C::beatJump, name.startsWith ("beatjump.back.") ? -beats : beats);   // release ends a slip jump
    }
    else if (name == "beatjump.back") button (C::beatJump, -beatJump[(size_t) d].load());
    else if (name == "beatjump.fwd")  button (C::beatJump,  beatJump[(size_t) d].load());
    else if (name == "slip")          { if (v.down) button (C::slipToggle); }
    else if (name == "vinyl.brake")   { if (v.down) deck.post (C::vinylBrakeSet, deck.state().brakeOn.load() ? 0.0 : 1.0, 0); }
    else if (name == "vinyl.start")   { if (v.down) deck.post (C::vinylBrakeSet, deck.state().startOn.load() ? 0.0 : 1.0, 1); }
    else if (name == "beatjump.size")
    {
        double b = beatJump[(size_t) d].load();
        b = v.delta > 0 ? juce::jmin (64.0, b * 2.0) : juce::jmax (0.125, b * 0.5);
        beatJump[(size_t) d] = b;
    }
    else if (name == "stem.reset")
    {
        if (v.down && d < 2)
        {
            for (int s = 0; s < 4; ++s)
            {
                stemVolume[(size_t) d][(size_t) s] = 0.5f; controlVolume[(size_t) d][(size_t) s] = 0.5f;
                stemMute[(size_t) d][(size_t) s] = false;  controlMute[(size_t) d][(size_t) s] = false;
            }
            updateStems (d);
        }
    }
    else if (name.startsWith ("stem.") && d < 2)
    {
        const int s = juce::jlimit (0, 3, name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1);
        if (name.endsWith (".volume"))     stemVolume[(size_t) d][(size_t) s] = (float) v.value;
        else if (name.endsWith (".mute"))  { if (! v.down) return; stemMute[(size_t) d][(size_t) s] = ! stemMute[(size_t) d][(size_t) s].load(); }
        else if (name.endsWith (".solo"))
        {
            if (! v.down) return;
            bool alreadySolo = ! stemMute[(size_t) d][(size_t) s].load();
            for (int o = 0; o < 4; ++o) if (o != s) alreadySolo = alreadySolo && stemMute[(size_t) d][(size_t) o].load();
            for (int o = 0; o < 4; ++o) stemMute[(size_t) d][(size_t) o] = alreadySolo ? false : (o != s);
        }
        updateStems (d);
    }
    else if (name.startsWith ("stemctl.") && d < 2)
    {
        const auto letter = name.fromFirstOccurrenceOf (".", false, false).substring (0, 1);
        const int c = juce::jlimit (0, 3, (int) (letter[0] - 'A'));
        if (name.endsWith (".volume"))     controlVolume[(size_t) d][(size_t) c] = (float) v.value;
        else if (name.endsWith (".mute"))  { if (! v.down) return; controlMute[(size_t) d][(size_t) c] = ! controlMute[(size_t) d][(size_t) c].load(); }
        updateStems (d);
    }
    else if (name == "key.up")    { if (v.down) { deck.post (C::keyShiftBy, 1.0);  keySyncLit[(size_t) (d & 1)] = false; } }
    else if (name == "key.down")  { if (v.down) { deck.post (C::keyShiftBy, -1.0); keySyncLit[(size_t) (d & 1)] = false; } }
    else if (name == "key.shift") { deck.post (C::keyShiftBy, v.delta > 0 ? 1.0 : -1.0); keySyncLit[(size_t) (d & 1)] = false; }
    else if (name == "key.reset") { if (v.down) { deck.post (C::keyShiftSet, 0.0); keySyncLit[(size_t) (d & 1)] = false; } }
    else if (name == "key.sync")
    {
        if (! v.down || d >= 2) return;
        if (keySyncLit[(size_t) d].load()) { deck.post (C::keyShiftSet, 0.0); keySyncLit[(size_t) d] = false; }
        else keySync (d);
    }
}

bool ActionRouter::ledState (const ActionDef& a) const
{
    if (a.deck < 0) return false;
    const auto& st = engine.deck (a.deck).state();
    const auto name = a.id.fromFirstOccurrenceOf (".", false, false);
    const bool loaded = st.trackSerial.load() != 0;

    if (name == "play")      return st.playing.load();
    if (name == "cue")       return loaded && ! st.playing.load();
    if (name == "sync")      return st.sync.load();
    if (name == "master")    return st.master.load();
    if (name == "keylock")   return st.keyLock.load();
    if (name == "quantize")  return st.quantize.load();
    if (name == "jog.vinyl") return st.vinylMode.load();
    if (name == "slip")      return st.slip.load();
    if (name == "vinyl.brake") return st.brakeOn.load();
    if (name == "vinyl.start") return st.startOn.load();
    if (name == "skip.toggle") return st.skipsOn.load();
    if (name == "skip.set" || name == "skip.in") return st.skipPendingFrom.load() >= 0.0;
    if (name.startsWith ("loop."))     return st.loopActive.load();
    if (name.startsWith ("hotcue."))
    {
        const int idx = name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        return idx >= 0 && idx < 8 && st.hotStart[(size_t) idx].load() >= 0.0;
    }
    if (name.startsWith ("beatloop."))
    {
        if (! st.loopActive.load()) return false;
        const double bpm = st.bpm.load() / juce::jmax (0.01, st.tempoRatio.load());
        if (bpm <= 0) return false;
        const double beats = (st.loopOutSec.load() - st.loopInSec.load()) * bpm / 60.0;
        return std::abs (beats - parseBeats (name.fromFirstOccurrenceOf (".", false, false))) < 0.05;
    }
    if (a.deck >= 2) return false;
    if (name.startsWith ("stem.") && name.endsWith (".mute"))
    {
        const int s = name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        return s >= 0 && s < 4 && stemMute[(size_t) a.deck][(size_t) s].load();
    }
    if (name.startsWith ("stemctl.") && name.endsWith (".mute"))
    {
        const int c = juce::jlimit (0, 3, (int) (name.fromFirstOccurrenceOf (".", false, false)[0] - 'A'));
        return controlMute[(size_t) a.deck][(size_t) c].load();
    }
    if (name == "key.sync") return keySyncLit[(size_t) a.deck].load() && st.keyShift.load() != 0;
    return false;
}

} // namespace orbit
