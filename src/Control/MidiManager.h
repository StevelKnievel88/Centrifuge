#pragma once
#include "ActionRouter.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <map>
#include <set>

namespace orbit
{

/** One MIDI control bound to one action. */
struct MidiBinding
{
    enum class Type { note, cc, pitchBend };
    enum class Mode { absolute7, absolute14, relativeOffset64, relativeTwos, relativeSignBit };

    juce::String actionId;
    juce::String device;        // input device name ("" = any device)
    int channel = 1;            // 1..16
    Type type = Type::note;
    int number = 0;             // note / controller number
    Mode mode = Mode::absolute7;
    bool invert = false;
    double sensitivity = 1.0;   // encoders: multiplier on steps
    int ledOn = 127, ledOff = 0;  // values sent to light the button (pads that take a colour from the value)
    int shiftOn = -1, shiftOff = -1;   // the pad's SHIFT layer (XDJ-XZ: its note + 8) while SHIFT is held; -1 = not set

    // Combo: only while this other button is held (e.g. SHIFT + LOAD = eject). modNumber < 0 = no combo.
    Type modType = Type::note;
    int modChannel = 1, modNumber = -1;
    bool hasModifier() const noexcept { return modNumber >= 0; }

    // Press length: 0 any, 1 short (a quick press), 2 long (held past the long-press time) - one button, two functions
    int press = 0;

    juce::String describe() const;
    juce::var toVar() const;
    static MidiBinding fromVar (const juce::var& v);
    bool sameControl (const MidiBinding& o) const noexcept
    {
        return channel == o.channel && type == o.type && number == o.number && device == o.device
            && modNumber == o.modNumber && (modNumber < 0 || (modType == o.modType && modChannel == o.modChannel)) && press == o.press;
    }
};

/** Finds the XDJ-XZ (and any other MIDI controller), routes its messages through the mapping,
    supports MIDI-learn for every action, and sends LED feedback back to the unit. */
class MidiManager : private juce::MidiInputCallback,
                    private juce::Timer
{
public:
    explicit MidiManager (ActionRouter& router);
    ~MidiManager() override;

    // Devices -----------------------------------------------------------------
    void rescanDevices();
    juce::StringArray getOpenInputNames() const;
    bool isXdjConnected() const { return xdjConnected.load(); }
    juce::String getXdjStatus() const;
    /** The connected controllers' names, or "No device detected". */
    juce::String getControllerSummary() const;
    /** Which device drives each deck ("" = any). */
    void setDeckControllers (const juce::String& deck1, const juce::String& deck2);

    // Mapping -----------------------------------------------------------------
    std::vector<MidiBinding> getBindings() const;
    std::vector<MidiBinding> getBindingsFor (const juce::String& actionId) const;
    void setBindings (std::vector<MidiBinding> b);
    void removeBindingsFor (const juce::String& actionId);
    void updateBinding (int index, const MidiBinding& b);
    bool saveToFile (const juce::File& f) const;
    bool loadFromFile (const juce::File& f);

    // Learn -------------------------------------------------------------------
    /** addToControl: keep whatever else is already on the control (one knob, several functions). */
    void startLearn (const juce::String& actionId, bool addToControl = false);
    std::vector<juce::String> getActionsSharingControl (const MidiBinding& b) const;
    /** The functions the last learn took a control away from (it was mapped to them before). */
    std::vector<juce::String> getLastReplaced() const { return lastReplaced; }
    void cancelLearn();
    juce::String getLearningAction() const;
    std::function<void (const MidiBinding&)> onLearned;   // message thread

    /** More than one control was heard while learning: the UI asks which one to use (message thread).
        Each entry describes a control; `best` is the most likely one. Answer with chooseLearned / cancelLearn. */
    struct LearnChoice { MidiBinding binding; juce::String what; int count = 0; };
    std::function<void (const std::vector<LearnChoice>&, int best)> onLearnChoice;
    void chooseLearned (int index);
    bool isChoosing() const;

    /** Functions guided setup always passes over ("don't map" ticks). Saved with the mapping. */
    std::set<juce::String> getNoMap() const;
    void setNoMap (const juce::String& actionId, bool noMap);
    /** Sends a test value to a binding's control (to try LED colours). */
    void sendTestLed (const MidiBinding& b, int value);

    // Monitor -----------------------------------------------------------------
    struct MonitorEntry
    {
        double timeMs = 0;
        juce::String device;
        juce::String text;
        juce::String action;
    };
    std::vector<MonitorEntry> getRecentMessages (int max) const;
    juce::uint32 getMessageCounter() const { return messageCounter.load(); }

    /** All distinct controls seen since startup, so the UI can show "every input on the unit". */
    struct SeenControl
    {
        juce::String device;
        int channel, number;
        MidiBinding::Type type;
        int lastValue;
        juce::uint32 count;
        double lastMs = 0.0;
    };
    std::vector<SeenControl> getSeenControls() const;

    /** Feeds a message in as if it came from a controller (tests). */
    void injectForTest (const juce::MidiMessage& m) { handleIncomingMidiMessage (nullptr, m); }
    void pollForTest() { timerCallback(); }

    // LEDs --------------------------------------------------------------------
    void setLedFeedbackEnabled (bool on) { ledFeedback = on; }
    /** Pad colours by name (the XDJ-XZ's pads light in a colour from the number sent). 0 = not set. Saved with the mapping. */
    static constexpr int numPadColours = 9;
    static const char* padColourName (int i);
    static juce::Colour padColourRef (int i);
    int getPadColour (int i) const { return padColours[(size_t) i].load(); }
    void setPadColour (int i, int number) { padColours[(size_t) i] = juce::jlimit (0, 127, number); clearLedCache(); }
    /** The pad colour number nearest to a colour (among the ones set), or -1 if none are set. */
    int padNumberFor (juce::Colour c) const;
    /** A colour's number on the XDJ-XZ's pad colour wheel (bright 1 - 63, dim 64 - 127), -1 for white / grey. */
    static int padWheelNumber (juce::Colour c, bool dim);
    /** White on the pads isn't on the colour wheel: the numbers you found for it (bright / dim), 0 = not set yet. */
    int getWhitePad (bool dim) const { return whitePad[dim ? 1 : 0].load(); }
    void setWhitePad (bool dim, int n) { whitePad[dim ? 1 : 0] = juce::jlimit (0, 127, n); }
    bool hotCuesInColour() const { return hotCueColours.load(); }
    void setHotCuesInColour (bool b) { hotCueColours = b; clearLedCache(); }
    /** Jog screens on the XDJ-XZ: the position bar turns with the track, and the cue marker shows the cue point. */
    void setJogDisplay (bool on) { jogDisplay = on; jogCache.fill (-1); jogMsbCache.fill (-1); }
    void clearLedCache() { const juce::ScopedLock sl (bindingLock); ledCache.clear(); jogCache.fill (-1); jogMsbCache.fill (-1); }

    /** How long a press has to be held to count as a long press (seconds, 0.25 - 1). */
    void setLongPressSeconds (double s) { longPressMs = juce::jlimit (250.0, 1000.0, s * 1000.0); }

private:
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m) override;
    void timerCallback() override;
    void finishLearn();
    void sendLeds();

    ActionRouter& router;

    mutable juce::CriticalSection bindingLock;
    std::vector<MidiBinding> bindings;

    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    std::map<juce::String, std::unique_ptr<juce::MidiOutput>> outputs; // by device name
    std::atomic<bool> xdjConnected { false };
    juce::String xdjNames;

    // 14-bit pairing: last MSB per device/channel/controller
    std::map<juce::String, int> msbCache;
    juce::SpinLock msbLock;

    // Learn state
    mutable juce::CriticalSection learnLock;
    juce::String learnAction;
    bool learnAdd = false;
    struct Candidate { MidiBinding b; std::vector<int> values; double firstMs = 0.0; bool saw14 = false;
                       std::vector<MidiBinding> heldWith; };   // buttons held down when this one was pressed (combos)
    std::map<juce::String, MidiBinding> held;      // buttons held down right now, by control
    std::map<juce::String, double> heldSince;
    std::vector<juce::String> lastReplaced;      // when each of those went down
    std::set<juce::String> continuousControls;     // CCs seen with in-between values: knobs / faders, never "held" buttons
    std::map<juce::String, std::vector<juce::String>> pressedActions;   // which actions a held control's press went to
    struct PendingPress { double ms = 0.0; std::vector<std::pair<const ActionDef*, ActionValue>> shortHits, longHits; bool longFired = false; };
    std::map<juce::String, PendingPress> pendingPresses;   // short / long: waiting to see how long the button is held
    std::atomic<double> longPressMs { 500.0 };
    std::array<std::atomic<int>, numPadColours> padColours {};
    std::array<std::atomic<int>, 2> whitePad {};
    std::atomic<bool> hotCueColours { true }, jogDisplay { true };
    std::array<int, 12> jogCache { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<std::atomic<int>, 2> padMode { 1, 1 };   // last pad mode button pressed on each deck (lights only)
    std::array<int, 12> jogMsbCache { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    void sendToXdj (const juce::MidiMessage& m);
    void sendJogDisplays();
    void firePending (const std::vector<std::pair<const ActionDef*, ActionValue>>& hits, bool down);
    std::vector<Candidate> learnCands;
    std::vector<LearnChoice> pendingChoice;
    std::vector<Candidate> pendingCands;
    bool choosing = false;
    double learnFirstMs = 0.0;
    std::map<juce::String, double> learnIgnore;   // controls still moving when learning began (leftovers)
    std::set<juce::String> noMap;
    std::array<juce::String, 2> deckDevice;
    MidiBinding withDetectedMode (const Candidate& c) const;
    void bind (MidiBinding b, bool add);

    // Monitor
    mutable juce::SpinLock monitorLock;
    std::vector<MonitorEntry> monitor;
    size_t monitorWrite = 0;
    std::atomic<juce::uint32> messageCounter { 0 };
    std::map<juce::String, SeenControl> seen;

    // LED cache
    std::map<juce::String, int> ledCache;
    std::atomic<bool> ledFeedback { true };
    int scanCountdown = 0;
};

} // namespace orbit
