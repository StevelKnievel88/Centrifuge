#include "ActionRouter.h"

namespace orbit
{

namespace
{
    constexpr double pitchRanges[] = { 0.06, 0.10, 0.16, 1.00 };

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
        vinyl[(size_t) d] = true;
        if (d < AudioEngine::maxDecks)
            engine.deck (d).post (Deck::Cmd::setPitchRange, pitchRanges[1]);
    }
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
    else if (name == "load")
    {
        if (v.down && onAppAction)
        {
            auto fn = onAppAction;
            juce::MessageManager::callAsync ([fn, d, v] { fn ("deck.load", d, v); });
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
            const int idx = (rangeIndex[(size_t) d].load() + 1) % 4;
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
    else if (name == "beatjump.back") { if (v.down) button (C::beatJump, -beatJump[(size_t) d].load()); }
    else if (name == "beatjump.fwd")  { if (v.down) button (C::beatJump,  beatJump[(size_t) d].load()); }
    else if (name == "beatjump.size")
    {
        double b = beatJump[(size_t) d].load();
        b = v.delta > 0 ? juce::jmin (64.0, b * 2.0) : juce::jmax (0.125, b * 0.5);
        beatJump[(size_t) d] = b;
    }
    else if (name.startsWith ("stem."))
    {
        const int s = name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        if (name.endsWith (".volume"))     deck.post (C::stemGain, v.value, s);
        else if (name.endsWith (".mute"))  { if (v.down) button (C::stemMuteToggle, 0.0, s); }
        else if (name.endsWith (".solo"))  { if (v.down) button (C::stemSolo, 0.0, s); }
        else if (name == "stem.reset")     { if (v.down) button (C::stemReset); }
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
    if (name.startsWith ("stem.") && name.endsWith (".mute"))
    {
        const int s = name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        return s >= 0 && s < 4 && st.stemGain[(size_t) s].load() <= 0.0f;
    }
    return false;
}

} // namespace orbit
