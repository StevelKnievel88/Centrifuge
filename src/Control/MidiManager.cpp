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
        if (isNote) what = presses <= 1 ? "pressed once" : "pressed " + juce::String (presses) + "x";
        else if (c.b.type == MidiBinding::Type::pitchBend) what = "moved";
        else if (distinct.size() <= 2) what = "button / touch";
        else what = "turned / moved (" + juce::String ((int) c.values.size()) + " messages)";

        double score = 0.0;
        if (kind == ActionKind::button)
            score = (isNote ? 100.0 : 0.0) + (distinct.size() <= 2 ? 50.0 : 0.0) - (double) c.values.size() - (c.firstMs - cands.front().firstMs) * 0.01;
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
    struct Hit { const ActionDef* def; ActionValue v; };
    juce::Array<Hit> hits;

    {
        const juce::ScopedLock sl (bindingLock);
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

            hits.add ({ def, v });
        }
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

void MidiManager::timerCallback()
{
    // Finish learning continuous / encoder controls after a short listening window.
    bool finish = false;
    {
        const juce::ScopedLock sl (learnLock);
        // listen a little after the first control so a "touch" followed by a "turn" are both heard
        finish = learnAction.isNotEmpty() && ! learnCands.empty() && ! choosing
                 && juce::Time::getMillisecondCounterHiRes() - learnFirstMs > 650.0;
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

void MidiManager::sendLeds()
{
    auto all = getBindings();
    for (auto& b : all)
    {
        const auto* def = findAction (b.actionId);
        if (def == nullptr || ! def->hasLed || b.type == MidiBinding::Type::pitchBend) continue;

        const bool on = router.ledState (*def);
        const auto key = b.device + "|" + b.describe();
        auto it = ledCache.find (key);
        if (it != ledCache.end() && it->second == on) continue;
        ledCache[key] = on;

        const int value = on ? b.ledOn : b.ledOff;
        const auto msg = b.type == MidiBinding::Type::note
                           ? juce::MidiMessage::noteOn (b.channel, b.number, (juce::uint8) value)
                           : juce::MidiMessage::controllerEvent (b.channel, b.number, value);

        for (auto& [name, out] : outputs)
            if (b.device.isEmpty() || name == b.device)
                out->sendMessageNow (msg);
    }
}

} // namespace orbit
