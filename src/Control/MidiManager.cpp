#include "MidiManager.h"

namespace orbit
{

//==============================================================================
juce::String MidiBinding::describe() const
{
    juce::String s = "Ch" + juce::String (channel) + " ";
    switch (type)
    {
        case Type::note:      s << "Note " << juce::String::toHexString (number).toUpperCase().paddedLeft ('0', 2); break;
        case Type::cc:        s << "CC " << juce::String::toHexString (number).toUpperCase().paddedLeft ('0', 2); break;
        case Type::pitchBend: s << "Pitch bend"; break;
    }
    switch (mode)
    {
        case Mode::absolute14:       s << " (14-bit)"; break;
        case Mode::relativeOffset64: s << " (rel 64)"; break;
        case Mode::relativeTwos:     s << " (rel 2's)"; break;
        case Mode::relativeSignBit:  s << " (rel sign)"; break;
        default: break;
    }
    if (invert) s << " inv";
    if (press == 1) s << " (short)";
    if (press == 2) s << " (long)";
    if (hasModifier())
    {
        MidiBinding m;
        m.channel = modChannel; m.type = modType; m.number = modNumber;
        s = "[" + m.describe() + "] + " + s;
    }
    return s;
}

juce::var MidiBinding::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("action", actionId);
    o->setProperty ("device", device);
    o->setProperty ("channel", channel);
    o->setProperty ("type", type == Type::note ? "note" : type == Type::cc ? "cc" : "pitchbend");
    o->setProperty ("number", number);
    const char* modes[] = { "abs7", "abs14", "rel64", "rel2s", "relsign" };
    o->setProperty ("mode", modes[(int) mode]);
    o->setProperty ("invert", invert);
    o->setProperty ("sensitivity", sensitivity);
    if (ledOn != 127) o->setProperty ("ledOn", ledOn);
    if (ledOff != 0) o->setProperty ("ledOff", ledOff);
    if (press != 0) o->setProperty ("press", press);
    if (hasModifier())
    {
        o->setProperty ("modType", modType == Type::note ? "note" : "cc");
        o->setProperty ("modChannel", modChannel);
        o->setProperty ("modNumber", modNumber);
    }
    return juce::var (o);
}

MidiBinding MidiBinding::fromVar (const juce::var& v)
{
    MidiBinding b;
    b.actionId = v["action"].toString();
    b.device = v["device"].toString();
    b.channel = juce::jlimit (1, 16, (int) v["channel"]);
    const auto t = v["type"].toString();
    b.type = t == "cc" ? Type::cc : t == "pitchbend" ? Type::pitchBend : Type::note;
    b.number = juce::jlimit (0, 127, (int) v["number"]);
    const auto m = v["mode"].toString();
    b.mode = m == "abs14" ? Mode::absolute14 : m == "rel64" ? Mode::relativeOffset64
           : m == "rel2s" ? Mode::relativeTwos : m == "relsign" ? Mode::relativeSignBit : Mode::absolute7;
    b.invert = (bool) v["invert"];
    b.sensitivity = v.hasProperty ("sensitivity") ? (double) v["sensitivity"] : 1.0;
    if (b.sensitivity == 0.0) b.sensitivity = 1.0;
    b.ledOn = v.hasProperty ("ledOn") ? juce::jlimit (0, 127, (int) v["ledOn"]) : 127;
    b.ledOff = v.hasProperty ("ledOff") ? juce::jlimit (0, 127, (int) v["ledOff"]) : 0;
    b.press = juce::jlimit (0, 2, (int) v.getProperty ("press", 0));
    if (v.hasProperty ("modNumber"))
    {
        b.modType = v["modType"].toString() == "cc" ? Type::cc : Type::note;
        b.modChannel = juce::jlimit (1, 16, (int) v["modChannel"]);
        b.modNumber = juce::jlimit (0, 127, (int) v["modNumber"]);
    }
    return b;
}

//==============================================================================
MidiManager::MidiManager (ActionRouter& r) : router (r)
{
    monitor.resize (256);
    rescanDevices();
    startTimer (40);
}

MidiManager::~MidiManager()
{
    stopTimer();
    for (auto& in : inputs) in->stop();
    inputs.clear();
    outputs.clear();
}

void MidiManager::rescanDevices()
{
    auto available = juce::MidiInput::getAvailableDevices();

    // Close inputs that disappeared
    for (auto it = inputs.begin(); it != inputs.end();)
    {
        bool still = false;
        for (auto& d : available) still = still || d.identifier == (*it)->getIdentifier();
        if (! still) { (*it)->stop(); it = inputs.erase (it); }
        else ++it;
    }

    // Open new inputs
    for (auto& d : available)
    {
        bool open = false;
        for (auto& in : inputs) open = open || in->getIdentifier() == d.identifier;
        if (open) continue;
        if (auto in = juce::MidiInput::openDevice (d.identifier, this))
        {
            in->start();
            inputs.push_back (std::move (in));
        }
    }

    // Outputs (for LED feedback), matched to inputs by name
    auto outs = juce::MidiOutput::getAvailableDevices();
    for (auto it = outputs.begin(); it != outputs.end();)
    {
        bool still = false;
        for (auto& o : outs) still = still || o.name == it->first;
        it = still ? std::next (it) : outputs.erase (it);
    }
    for (auto& o : outs)
        if (outputs.find (o.name) == outputs.end())
            if (auto out = juce::MidiOutput::openDevice (o.identifier))
                outputs[o.name] = std::move (out);

    juce::StringArray xdj;
    for (auto& in : inputs)
        if (in->getName().containsIgnoreCase ("XDJ"))
            xdj.add (in->getName());
    xdjNames = xdj.joinIntoString (", ");
    xdjConnected = ! xdj.isEmpty();
}

juce::StringArray MidiManager::getOpenInputNames() const
{
    juce::StringArray s;
    for (auto& in : inputs) s.add (in->getName());
    return s;
}

juce::String MidiManager::getControllerSummary() const
{
    juce::StringArray names;
    for (auto& in : inputs)
    {
        const auto n = in->getName();
        if (n.containsIgnoreCase ("Microsoft GS") || n.containsIgnoreCase ("Midi Through")) continue;
        names.addIfNotAlreadyThere (n);
    }
    return names.isEmpty() ? juce::String ("No device detected") : names.joinIntoString (", ");
}

void MidiManager::setDeckControllers (const juce::String& d1, const juce::String& d2)
{
    const juce::ScopedLock sl (bindingLock);
    deckDevice = { d1, d2 };
}

std::set<juce::String> MidiManager::getNoMap() const
{
    const juce::ScopedLock sl (bindingLock);
    return noMap;
}

void MidiManager::setNoMap (const juce::String& id, bool on)
{
    const juce::ScopedLock sl (bindingLock);
    if (on) noMap.insert (id); else noMap.erase (id);
}

void MidiManager::sendTestLed (const MidiBinding& b, int value)
{
    if (b.type == MidiBinding::Type::pitchBend) return;
    const auto msg = b.type == MidiBinding::Type::note ? juce::MidiMessage::noteOn (b.channel, b.number, (juce::uint8) juce::jlimit (0, 127, value))
                                                       : juce::MidiMessage::controllerEvent (b.channel, b.number, juce::jlimit (0, 127, value));
    for (auto& [name, out] : outputs)
        if (b.device.isEmpty() || name == b.device)
            out->sendMessageNow (msg);
    const juce::ScopedLock sl (bindingLock);
    ledCache.clear();
}

juce::String MidiManager::getXdjStatus() const
{
    if (xdjConnected) return "XDJ-XZ connected (" + xdjNames + ")";
    if (inputs.empty()) return "No MIDI devices";
    return "XDJ-XZ not found (" + juce::String ((int) inputs.size()) + " other MIDI device(s))";
}

//==============================================================================
std::vector<MidiBinding> MidiManager::getBindings() const
{
    const juce::ScopedLock sl (bindingLock);
    return bindings;
}

std::vector<MidiBinding> MidiManager::getBindingsFor (const juce::String& id) const
{
    const juce::ScopedLock sl (bindingLock);
    std::vector<MidiBinding> r;
    for (auto& b : bindings) if (b.actionId == id) r.push_back (b);
    return r;
}

void MidiManager::setBindings (std::vector<MidiBinding> b)
{
    const juce::ScopedLock sl (bindingLock);
    bindings = std::move (b);
    ledCache.clear();
}

void MidiManager::removeBindingsFor (const juce::String& id)
{
    const juce::ScopedLock sl (bindingLock);
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(), [&] (auto& b) { return b.actionId == id; }), bindings.end());
}

void MidiManager::updateBinding (int index, const MidiBinding& b)
{
    const juce::ScopedLock sl (bindingLock);
    if (index >= 0 && index < (int) bindings.size())
        bindings[(size_t) index] = b;
}

bool MidiManager::saveToFile (const juce::File& f) const
{
    juce::Array<juce::var> arr;
    for (auto& b : getBindings()) arr.add (b.toVar());
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "Centrifuge mapping v1");
    root->setProperty ("bindings", arr);
    juce::Array<juce::var> skip;
    for (auto& id : getNoMap()) skip.add (id);
    root->setProperty ("dontMap", skip);
    juce::Array<juce::var> pads;
    for (auto& p : padColours) pads.add (p.load());
    root->setProperty ("padColours", pads);
    root->setProperty ("hotCueColours", hotCueColours.load());
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (juce::var (root)));
}

bool MidiManager::loadFromFile (const juce::File& f)
{
    auto v = juce::JSON::parse (f);
    if (! v.isObject()) return false;
    std::vector<MidiBinding> b;
    if (auto* arr = v["bindings"].getArray())
        for (auto& item : *arr)
        {
            auto mb = MidiBinding::fromVar (item);
            if (findAction (mb.actionId) != nullptr) b.push_back (mb);
        }
    setBindings (std::move (b));
    {
        const juce::ScopedLock sl (bindingLock);
        noMap.clear();
        if (auto* arr = v["dontMap"].getArray())
            for (auto& id : *arr) noMap.insert (id.toString());
        if (auto* arr = v["padColours"].getArray())
            for (int i = 0; i < juce::jmin (numPadColours, arr->size()); ++i) padColours[(size_t) i] = juce::jlimit (0, 127, (int) (*arr)[i]);
        hotCueColours = (bool) v.getProperty ("hotCueColours", true);
    }
    return true;
}

//==============================================================================
std::vector<juce::String> MidiManager::getActionsSharingControl (const MidiBinding& b) const
{
    const juce::ScopedLock sl (bindingLock);
    std::vector<juce::String> r;
    for (auto& x : bindings)
        if (x.sameControl (b) && x.actionId != b.actionId)
            r.push_back (x.actionId);
    return r;
}

void MidiManager::startLearn (const juce::String& id, bool addToControl)
{
    const juce::ScopedLock sl (learnLock);
    learnAdd = addToControl;
    learnAction = id;
    learnCands.clear();
    choosing = false;
    learnFirstMs = 0.0;
    // Anything still moving right now (a jog that's still spinning, a fader just let go of) is a leftover from the
    // last control, not the one being learned: ignore it until it has been still for a moment.
    learnIgnore.clear();
    const double now = juce::Time::getMillisecondCounterHiRes();
    const juce::SpinLock::ScopedLockType ml (monitorLock);
    for (auto& [k, c] : seen)
        if (now - c.lastMs < 400.0) learnIgnore[k] = c.lastMs;
}

void MidiManager::cancelLearn()
{
    const juce::ScopedLock sl (learnLock);
    learnAction = {};
    learnCands.clear();
    choosing = false;
}

bool MidiManager::isChoosing() const
{
    const juce::ScopedLock sl (learnLock);
    return choosing;
}

juce::String MidiManager::getLearningAction() const
{
    const juce::ScopedLock sl (learnLock);
    return learnAction;
}

MidiBinding MidiManager::withDetectedMode (const Candidate& c) const
{
    auto b = c.b;
    const auto* def = findAction (b.actionId);
    if (def != nullptr && def->kind == ActionKind::encoder && b.type == MidiBinding::Type::cc)
    {
        bool small = false, high = false, mid65 = false, around64 = true;
        for (int v : c.values)
        {
            small = small || (v >= 1 && v <= 20);
            high = high || (v >= 100);
            mid65 = mid65 || (v >= 65 && v <= 84);
            around64 = around64 && (v >= 40 && v <= 88);
        }
        if (around64)            b.mode = MidiBinding::Mode::relativeOffset64;
        else if (small && high)  b.mode = MidiBinding::Mode::relativeTwos;
        else if (small && mid65) b.mode = MidiBinding::Mode::relativeSignBit;
        else if (small)          b.mode = MidiBinding::Mode::relativeTwos;
        else                     b.mode = MidiBinding::Mode::relativeOffset64;
    }
    else if (def != nullptr && def->kind == ActionKind::continuous && b.type == MidiBinding::Type::cc)
        b.mode = c.saw14 ? MidiBinding::Mode::absolute14 : MidiBinding::Mode::absolute7;
    else if (b.type == MidiBinding::Type::pitchBend)
        b.mode = MidiBinding::Mode::absolute14;
    return b;
}

void MidiManager::bind (MidiBinding b, bool add)
{
    {
        const juce::ScopedLock sl (bindingLock);
        // Replace: the control does only this now. Add: other functions stay on the control too.
        bindings.erase (std::remove_if (bindings.begin(), bindings.end(),
                                        [&] (auto& x) { return x.actionId == b.actionId || (! add && x.sameControl (b)); }),
                        bindings.end());
        bindings.push_back (b);
        ledCache.clear();
    }
    if (onLearned)
        onLearned (b);
}

void MidiManager::finishLearn()
{
    std::vector<Candidate> cands;
    bool add = false;
    juce::String action;
    {
        const juce::ScopedLock sl (learnLock);
        if (learnAction.isEmpty() || learnCands.empty() || choosing) return;
        cands = learnCands;
        add = learnAdd;
        action = learnAction;
    }

    // A button pressed while another was held: offer the combo (held + pressed) as well, as the best guess
    {
        std::vector<Candidate> extra;
        for (auto& c : cands)
            for (auto& h : c.heldWith)
            {
                bool isCand = false;
                for (auto& o : cands) isCand = isCand || (o.b.device == h.device && o.b.type == h.type && o.b.channel == h.channel && o.b.number == h.number && &o == &c);
                if (isCand || h.type == MidiBinding::Type::pitchBend) continue;
                auto combo = c;
                combo.heldWith.clear();
                combo.b.modType = h.type;
                combo.b.modChannel = h.channel;
                combo.b.modNumber = h.number;
                extra.push_back (combo);
            }
        // the held button on its own isn't a choice when it was held before listening began
        for (auto& e : extra) cands.push_back (e);
    }

    if (cands.size() == 1)
    {
        {
            const juce::ScopedLock sl (learnLock);
            learnAction = {};
            learnCands.clear();
        }
        bind (withDetectedMode (cands.front()), add);
        return;
    }

    // Several controls were heard (e.g. touching the platter top sends "touch" just before "turn"): ask which one.
    const auto* def = findAction (action);
    const auto kind = def != nullptr ? def->kind : ActionKind::button;
    int best = 0;
    double bestScore = -1.0e9;
    std::vector<LearnChoice> choices;
    for (size_t i = 0; i < cands.size(); ++i)
    {
        const auto& c = cands[i];
        std::set<int> distinct (c.values.begin(), c.values.end());
        const bool isNote = c.b.type == MidiBinding::Type::note;
        const int presses = (int) std::count_if (c.values.begin(), c.values.end(), [] (int v) { return v > 0; });
        juce::String what;
        if (c.b.hasModifier()) what = "combo: hold the first, press the second";
        else if (isNote) what = presses <= 1 ? "pressed once" : "pressed " + juce::String (presses) + "x";
        else if (c.b.type == MidiBinding::Type::pitchBend) what = "moved";
        else if (distinct.size() <= 2) what = "button / touch";
        else what = "turned / moved (" + juce::String ((int) c.values.size()) + " messages)";

        double score = 0.0;
        if (kind == ActionKind::button)
            score = (isNote ? 100.0 : 0.0) + (distinct.size() <= 2 ? 50.0 : 0.0) - (double) c.values.size() - (c.firstMs - cands.front().firstMs) * 0.01
                  + (c.b.hasModifier() ? 500.0 + (c.firstMs - cands.front().firstMs) * 0.02 : 0.0);
        else
            score = (double) distinct.size() * 10.0 + (double) c.values.size() - (isNote ? 1000.0 : 0.0);
        if (score > bestScore) { bestScore = score; best = (int) i; }
        choices.push_back ({ withDetectedMode (c), what, (int) c.values.size() });
    }
    {
        const juce::ScopedLock sl (learnLock);
        choosing = true;
        pendingCands = cands;
        pendingChoice = choices;
    }
    if (onLearnChoice) onLearnChoice (choices, best);
    else chooseLearned (best);
}

void MidiManager::chooseLearned (int index)
{
    MidiBinding b;
    bool add = false;
    {
        const juce::ScopedLock sl (learnLock);
        if (! choosing || index < 0 || index >= (int) pendingCands.size()) return;
        b = withDetectedMode (pendingCands[(size_t) index]);
        add = learnAdd;
        choosing = false;
        learnAction = {};
        learnCands.clear();
        pendingCands.clear();
    }
    bind (b, add);
}

//==============================================================================
void MidiManager::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    const auto device = source != nullptr ? source->getName() : juce::String();
    const int ch = m.getChannel();
    if (ch <= 0) return; // sysex / clock etc.

    MidiBinding::Type type;
    int number = 0, raw = 0;
    bool isRelease = false;

    if (m.isNoteOnOrOff())
    {
        type = MidiBinding::Type::note;
        number = m.getNoteNumber();
        raw = m.isNoteOn() ? m.getVelocity() : 0;
        isRelease = raw == 0;
    }
    else if (m.isController())
    {
        type = MidiBinding::Type::cc;
        number = m.getControllerNumber();
        raw = m.getControllerValue();
    }
    else if (m.isPitchWheel())
    {
        type = MidiBinding::Type::pitchBend;
        raw = m.getPitchWheelValue();
    }
    else return;

    ++messageCounter;

    // Buttons held down right now (for combos: SHIFT + LOAD ...)
    const auto ctlKey = device + "|" + juce::String ((int) type) + "|" + juce::String (ch) + "|" + juce::String (number);
    std::vector<MidiBinding> heldNow;
    {
        const juce::ScopedLock sl (learnLock);
        for (auto& [k, hb] : held) if (k != ctlKey) heldNow.push_back (hb);
        const bool buttonLike = type == MidiBinding::Type::note || (type == MidiBinding::Type::cc && (raw == 127 || raw == 0));
        if (buttonLike)
        {
            if (raw > 0)
            {
                MidiBinding hb;
                hb.device = device; hb.channel = ch; hb.type = type; hb.number = number;
                held[ctlKey] = hb;
            }
            else held.erase (ctlKey);
        }
    }

    // --- Monitor ------------------------------------------------------------
    {
        juce::String text = "Ch" + juce::String (ch) + "  ";
        if (type == MidiBinding::Type::note) text << "Note " << juce::String::toHexString (number).toUpperCase().paddedLeft ('0', 2) << (raw > 0 ? "  on  " : "  off ") << raw;
        else if (type == MidiBinding::Type::cc) text << "CC   " << juce::String::toHexString (number).toUpperCase().paddedLeft ('0', 2) << "  = " << raw;
        else text << "Pitch bend = " << raw;

        juce::String actionLabel;
        {
            const juce::ScopedLock sl (bindingLock);
            for (auto& b : bindings)
            {
                const bool lsb = b.mode == MidiBinding::Mode::absolute14 && b.type == MidiBinding::Type::cc && number == b.number + 32;
                if (b.channel == ch && b.type == type && (b.number == number || lsb || type == MidiBinding::Type::pitchBend)
                    && (b.device.isEmpty() || b.device == device))
                    if (auto* def = findAction (b.actionId)) { actionLabel = def->group + ": " + def->label; break; }
            }
        }

        const juce::SpinLock::ScopedLockType sl (monitorLock);
        auto& e = monitor[monitorWrite % monitor.size()];
        e.timeMs = juce::Time::getMillisecondCounterHiRes();
        e.device = device;
        e.text = text;
        e.action = actionLabel;
        ++monitorWrite;

        const auto key = device + "|" + juce::String ((int) type) + "|" + juce::String (ch) + "|" + juce::String (number);
        auto& s = seen[key];
        s.device = device; s.channel = ch; s.number = number; s.type = type; s.lastValue = raw; ++s.count;
        s.lastMs = e.timeMs;
    }

    // --- Learn ---------------------------------------------------------------
    {
        const juce::ScopedLock sl (learnLock);
        if (learnAction.isNotEmpty())
        {
            if (choosing) return;
            const double now = juce::Time::getMillisecondCounterHiRes();
            const auto key = device + "|" + juce::String ((int) type) + "|" + juce::String (ch) + "|" + juce::String (number);
            if (auto it = learnIgnore.find (key); it != learnIgnore.end())
            {
                const double prev = it->second;
                it->second = now;
                if (now - prev < 350.0) return;   // still the leftover movement
                learnIgnore.erase (it);
            }

            const auto* def = findAction (learnAction);
            auto same = [&] (const Candidate& c) { return c.b.device == device && c.b.channel == ch && c.b.type == type && c.b.number == number; };
            auto it = std::find_if (learnCands.begin(), learnCands.end(), same);

            if (type == MidiBinding::Type::cc && number >= 32 && number < 64)
            {
                // the LSB of a 14-bit fader: belongs to the MSB 32 below
                for (auto& c : learnCands)
                    if (c.b.device == device && c.b.channel == ch && c.b.type == type && c.b.number == number - 32) { c.saw14 = true; return; }
                if (it == learnCands.end() && def != nullptr && def->kind == ActionKind::continuous) return;
            }

            if (it != learnCands.end()) { it->values.push_back (raw); return; }
            if (isRelease || (def != nullptr && def->kind == ActionKind::button && type == MidiBinding::Type::cc && raw == 0))
                return;   // a release on its own isn't a control being pressed

            Candidate c;
            c.heldWith = heldNow;
            c.b.actionId = learnAction;
            c.b.device = device;
            c.b.channel = ch;
            c.b.type = type;
            c.b.number = number;
            c.values = { raw };
            c.firstMs = now;
            if (learnCands.empty()) learnFirstMs = now;
            learnCands.push_back (c);
            return;
        }
    }

    // --- Dispatch --------------------------------------------------------------
    struct Hit { const ActionDef* def; ActionValue v; bool combo; int press; };
    const bool isPress = (type == MidiBinding::Type::note && raw > 0) || (type == MidiBinding::Type::cc && raw > 0);
    const bool isLift = (type == MidiBinding::Type::note && raw == 0) || (type == MidiBinding::Type::cc && raw == 0);

    // Short / long presses: the release (or holding past the long-press time) decides which one fires
    if (isLift)
    {
        PendingPress pp;
        bool had = false;
        {
            const juce::ScopedLock sl (learnLock);
            if (auto it = pendingPresses.find (ctlKey); it != pendingPresses.end()) { pp = it->second; pendingPresses.erase (it); had = true; }
        }
        if (had)
        {
            if (pp.longFired) firePending (pp.longHits, false);
            else { firePending (pp.shortHits, true); firePending (pp.shortHits, false); }
        }
    }
    juce::Array<Hit> hits;

    // A button's release goes to whatever its press went to (even if the combo's held button was let go first)
    std::vector<juce::String> releaseTo;
    if (isLift)
    {
        const juce::ScopedLock sl (learnLock);
        if (auto it = pressedActions.find (ctlKey); it != pressedActions.end()) { releaseTo = it->second; pressedActions.erase (it); }
    }

    {
        const juce::ScopedLock sl (bindingLock);
        auto modHeld = [&] (const MidiBinding& b)
        {
            for (auto& h : heldNow)
                if (h.type == b.modType && h.channel == b.modChannel && h.number == b.modNumber && (b.device.isEmpty() || h.device == b.device))
                    return true;
            return false;
        };
        for (auto& b : bindings)
        {
            if (b.channel != ch || b.type != type) continue;
            if (b.device.isNotEmpty() && b.device != device) continue;

            const bool is14 = b.mode == MidiBinding::Mode::absolute14;
            const bool isLsb = is14 && type == MidiBinding::Type::cc && number == b.number + 32;
            if (type != MidiBinding::Type::pitchBend && number != b.number && ! isLsb) continue;

            const auto* def = findAction (b.actionId);
            if (def == nullptr) continue;
            if (def->deck >= 0 && def->deck < 2 && deckDevice[(size_t) def->deck].isNotEmpty() && deckDevice[(size_t) def->deck] != device)
                continue;   // this deck is driven by another controller
            if (! releaseTo.empty())
            {
                if (std::find (releaseTo.begin(), releaseTo.end(), b.actionId) == releaseTo.end()) continue;
            }
            else if (b.hasModifier() && ! modHeld (b)) continue;

            ActionValue v;
            double value = raw / 127.0;

            if (type == MidiBinding::Type::pitchBend)
                value = raw / 16383.0;
            else if (is14 && type == MidiBinding::Type::cc)
            {
                const auto key = device + "|" + juce::String (ch) + "|" + juce::String (b.number);
                const juce::SpinLock::ScopedLockType ml (msbLock);
                if (isLsb)
                {
                    const int msb = msbCache.count (key) ? msbCache[key] : 0;
                    value = (msb * 128 + raw) / 16383.0;
                }
                else
                {
                    msbCache[key] = raw;
                    value = (raw * 128) / 16383.0;
                }
            }

            if (b.invert) value = 1.0 - value;
            v.value = juce::jlimit (0.0, 1.0, value);
            v.down = b.invert ? raw == 0 : raw > 0;

            if (def->kind == ActionKind::encoder)
            {
                double delta = 0.0;
                switch (b.mode)
                {
                    case MidiBinding::Mode::relativeOffset64: delta = raw - 64; break;
                    case MidiBinding::Mode::relativeTwos:     delta = raw < 64 ? raw : raw - 128; break;
                    case MidiBinding::Mode::relativeSignBit:  delta = (raw & 0x40) ? -(raw & 0x3f) : (raw & 0x3f); break;
                    default:
                        if (type == MidiBinding::Type::note) delta = raw > 0 ? 1.0 : 0.0;
                        else delta = raw - 64;
                        break;
                }
                v.delta = delta * b.sensitivity * (b.invert ? -1.0 : 1.0);
                if (v.delta == 0.0) continue;
            }

            hits.add ({ def, v, b.hasModifier(), def->kind == ActionKind::button ? b.press : 0 });
        }
    }

    // A combo that matches takes over the control: SHIFT + LOAD ejects instead of loading
    bool anyCombo = false;
    for (auto& h : hits) anyCombo = anyCombo || h.combo;
    if (anyCombo && releaseTo.empty())
        hits.removeIf ([] (const Hit& h) { return ! h.combo; });

    if (isPress)
    {
        PendingPress pp;
        pp.ms = juce::Time::getMillisecondCounterHiRes();
        for (auto& h : hits)
            if (h.press == 1) pp.shortHits.push_back ({ h.def, h.v });
            else if (h.press == 2) pp.longHits.push_back ({ h.def, h.v });
        if (! pp.shortHits.empty() || ! pp.longHits.empty())
        {
            const juce::ScopedLock sl (learnLock);
            pendingPresses[ctlKey] = pp;
        }
        hits.removeIf ([] (const Hit& h) { return h.press != 0; });
    }
    else if (isLift)
        hits.removeIf ([] (const Hit& h) { return h.press != 0; });

    if (isPress && ! hits.isEmpty())
    {
        std::vector<juce::String> ids;
        for (auto& h : hits) ids.push_back (h.def->id);
        const juce::ScopedLock sl (learnLock);
        pressedActions[ctlKey] = ids;
    }

    for (auto& h : hits)
        router.perform (*h.def, h.v);
}

//==============================================================================
std::vector<MidiManager::MonitorEntry> MidiManager::getRecentMessages (int max) const
{
    const juce::SpinLock::ScopedLockType sl (monitorLock);
    std::vector<MonitorEntry> r;
    const size_t n = std::min ((size_t) max, std::min (monitorWrite, monitor.size()));
    for (size_t i = 0; i < n; ++i)
        r.push_back (monitor[(monitorWrite - 1 - i) % monitor.size()]);
    return r;
}

std::vector<MidiManager::SeenControl> MidiManager::getSeenControls() const
{
    const juce::SpinLock::ScopedLockType sl (monitorLock);
    std::vector<SeenControl> r;
    for (auto& [k, v] : seen) r.push_back (v);
    return r;
}

void MidiManager::firePending (const std::vector<std::pair<const ActionDef*, ActionValue>>& hits, bool down)
{
    for (auto [def, v] : hits)
    {
        v.down = down;
        if (def != nullptr) router.perform (*def, v);
    }
}

void MidiManager::timerCallback()
{
    // Long presses: held past the long-press time
    {
        std::vector<std::vector<std::pair<const ActionDef*, ActionValue>>> fire;
        {
            const juce::ScopedLock sl (learnLock);
            const double now = juce::Time::getMillisecondCounterHiRes();
            for (auto& [k, pp] : pendingPresses)
                if (! pp.longFired && ! pp.longHits.empty() && now - pp.ms >= longPressMs.load())
                {
                    pp.longFired = true;
                    fire.push_back (pp.longHits);
                }
        }
        for (auto& f : fire) firePending (f, true);
    }

    // Finish learning continuous / encoder controls after a short listening window.
    bool finish = false;
    {
        const juce::ScopedLock sl (learnLock);
        // listen a little after the first control so a "touch" followed by a "turn" are both heard
        // ... and while a button heard is still held down (the first half of a combo), keep listening for up to 5 s
        bool stillHeld = false;
        for (auto& c : learnCands)
            stillHeld = stillHeld || held.count (c.b.device + "|" + juce::String ((int) c.b.type) + "|" + juce::String (c.b.channel) + "|" + juce::String (c.b.number)) > 0;
        const double el = juce::Time::getMillisecondCounterHiRes() - learnFirstMs;
        finish = learnAction.isNotEmpty() && ! learnCands.empty() && ! choosing && el > 650.0 && (! stillHeld || el > 5000.0);
    }
    if (finish) finishLearn();

    if (++scanCountdown >= 50) // ~2 s
    {
        scanCountdown = 0;
        rescanDevices();
    }

    if (ledFeedback)
        sendLeds();
}

const char* MidiManager::padColourName (int i)
{
    static const char* n[numPadColours] = { "Pink", "Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "White" };
    return n[juce::jlimit (0, numPadColours - 1, i)];
}

juce::Colour MidiManager::padColourRef (int i)
{
    return i < 8 ? rekordboxTrackColour (i + 1) : juce::Colours::white;
}

int MidiManager::padWheelNumber (juce::Colour c, bool dim)
{
    // The XDJ-XZ's pad colours run round the colour wheel: blue > cyan > green > yellow > orange > red > pink >
    // purple > back to blue, over 1 - 63 (bright) and again over 64 - 127 (dim).
    if (c.getSaturation() < 0.15f) return -1;   // no white / grey on the wheel
    const double hue = c.getHue() * 360.0;
    double pos = std::fmod (240.0 - hue + 720.0, 360.0) / 360.0;   // 0 at blue, going towards cyan
    const int n = 1 + (int) std::lround (pos * 62.0);
    return dim ? juce::jmin (127, n + 63) : n;
}

int MidiManager::padNumberFor (juce::Colour c) const
{
    // named colours you've set take priority; otherwise straight from the colour wheel
    bool anySet = false;
    for (auto& p : padColours) anySet = anySet || p.load() > 0;
    if (! anySet) return padWheelNumber (c, false);
    int best = -1;
    float bestD = 1.0e9f;
    for (int i = 0; i < numPadColours; ++i)
    {
        const int n = padColours[(size_t) i].load();
        if (n <= 0) continue;
        const auto r = padColourRef (i);
        // hue matters most; white is matched by low saturation
        const float dh = std::abs (c.getHue() - r.getHue()), hueD = juce::jmin (dh, 1.0f - dh);
        const float d = (i == 8 ? 0.0f : hueD * 4.0f) + std::abs (c.getSaturation() - r.getSaturation()) + 0.5f * std::abs (c.getBrightness() - r.getBrightness());
        if (d < bestD) { bestD = d; best = n; }
    }
    return best;
}

void MidiManager::sendToXdj (const juce::MidiMessage& m)
{
    for (auto& [name, out] : outputs)
        if (name.containsIgnoreCase ("XDJ"))
            out->sendMessageNow (m);
}

void MidiManager::sendJogDisplays()
{
    // Channel 12 CCs (from the XDJ-XZ MIDI message list): position bar (0 - 359 degrees, MSB cc d / LSB cc 32 + d),
    // cue marker (MSB cc 4 + d / LSB cc 36 + d, 0x7F 0x7F hides it); the LOAD buttons light with note d on channel 12.
    constexpr double secondsPerRev = 60.0 / (100.0 / 3.0);   // 33 1/3 rpm
    for (int d = 0; d < 2; ++d)
    {
        const auto& st = router.deckState (d);
        const bool loaded = st.trackSerial.load() != 0;
        auto angle = [&] (double sec) { const double r = std::fmod (sec / secondsPerRev, 1.0); return juce::jlimit (0, 359, (int) ((r < 0 ? r + 1.0 : r) * 360.0)); };
        const int pos = loaded ? angle (st.positionSec.load()) : 0;
        const int cue = loaded ? angle (st.cuePointSec.load()) : 0x3fff;
        const int load = st.playing.load() ? 0 : 127;   // LOAD lights on a deck that isn't playing (safe to load)
        const int vals[3] = { pos, cue, load };
        for (int k = 0; k < 3; ++k)
        {
            auto& c = jogCache[(size_t) (d * 3 + k)];
            if (c == vals[k]) continue;
            c = vals[k];
            if (k == 2) { sendToXdj (juce::MidiMessage::noteOn (12, d, (juce::uint8) vals[k])); continue; }
            const int base = k == 0 ? d : 4 + d;
            const int msb = vals[k] == 0x3fff ? 0x7f : vals[k] >> 7, lsb = vals[k] == 0x3fff ? 0x7f : vals[k] & 0x7f;
            sendToXdj (juce::MidiMessage::controllerEvent (12, base, msb));
            sendToXdj (juce::MidiMessage::controllerEvent (12, base + 32, lsb));
        }
    }
}

void MidiManager::sendLeds()
{
    if (jogDisplay.load()) sendJogDisplays();
    auto all = getBindings();
    std::set<juce::String> heldNow;
    {
        const juce::ScopedLock sl (learnLock);
        for (auto& [k, h] : held) heldNow.insert (k);
    }
    for (auto& b : all)
    {
        const auto* def = findAction (b.actionId);
        if (def == nullptr || b.type == MidiBinding::Type::pitchBend || def->kind != ActionKind::button) continue;   // buttons / pads only

        int value;
        const auto name = b.actionId.fromFirstOccurrenceOf (".", false, false);
        if (def->deck >= 0 && def->deck < 2 && name.startsWith ("hotcue.") && ! name.endsWith (".delete"))
        {
            // hot cue pads: the cue's own colour (when pad colours are set), dim when empty
            const int idx = name.fromFirstOccurrenceOf (".", false, false).getIntValue() - 1;
            const auto& st = router.deckState (def->deck);
            const bool set = idx >= 0 && idx < 8 && st.hotStart[(size_t) idx].load() >= 0.0;
            const int coloured = set && hotCueColours.load() ? padNumberFor (juce::Colour (st.hotColour[(size_t) idx].load())) : -1;
            value = ! set ? b.ledOff : coloured > 0 ? coloured : b.ledOn;
        }
        else if (def->hasLed)
            value = router.ledState (*def) ? b.ledOn : b.ledOff;
        else
        {
            // one-shot functions (beat jump, delete ...): their unlit colour, lit while pressed
            const auto key = b.device + "|" + juce::String ((int) b.type) + "|" + juce::String (b.channel) + "|" + juce::String (b.number);
            bool pressed = false;
            for (auto& k : heldNow) pressed = pressed || (k.endsWith (key.fromFirstOccurrenceOf ("|", true, false)) && (b.device.isEmpty() || k.startsWith (b.device)));
            value = pressed ? b.ledOn : b.ledOff;
        }

        const auto cacheKey = b.device + "|" + b.describe() + "|" + b.actionId;
        {
            const juce::ScopedLock sl (bindingLock);
            auto it = ledCache.find (cacheKey);
            if (it != ledCache.end() && it->second == value) continue;
            ledCache[cacheKey] = value;
        }
        const auto msg = b.type == MidiBinding::Type::note
                           ? juce::MidiMessage::noteOn (b.channel, b.number, (juce::uint8) value)
                           : juce::MidiMessage::controllerEvent (b.channel, b.number, value);

        for (auto& [nm, out] : outputs)
            if (b.device.isEmpty() || nm == b.device)
                out->sendMessageNow (msg);
    }
}

} // namespace orbit
