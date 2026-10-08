#pragma once
#include "ActionRouter.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <map>

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

    juce::String describe() const;
    juce::var toVar() const;
    static MidiBinding fromVar (const juce::var& v);
    bool sameControl (const MidiBinding& o) const noexcept
    {
        return channel == o.channel && type == o.type && number == o.number && device == o.device;
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

    // Mapping -----------------------------------------------------------------
    std::vector<MidiBinding> getBindings() const;
    std::vector<MidiBinding> getBindingsFor (const juce::String& actionId) const;
    void setBindings (std::vector<MidiBinding> b);
    void removeBindingsFor (const juce::String& actionId);
    void updateBinding (int index, const MidiBinding& b);
    bool saveToFile (const juce::File& f) const;
    bool loadFromFile (const juce::File& f);

    // Learn -------------------------------------------------------------------
    void startLearn (const juce::String& actionId);
    void cancelLearn();
    juce::String getLearningAction() const;
    std::function<void (const MidiBinding&)> onLearned;   // message thread

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
    };
    std::vector<SeenControl> getSeenControls() const;

    // LEDs --------------------------------------------------------------------
    void setLedFeedbackEnabled (bool on) { ledFeedback = on; }

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
    bool learnHasCandidate = false;
    MidiBinding learnCandidate;
    double learnStartedMs = 0.0;
    std::vector<int> learnValues;
    bool learnSaw14BitLsb = false;

    // Monitor
    mutable juce::SpinLock monitorLock;
    std::vector<MonitorEntry> monitor;
    size_t monitorWrite = 0;
    std::atomic<juce::uint32> messageCounter { 0 };
    std::map<juce::String, SeenControl> seen;

    // LED cache
    std::map<juce::String, bool> ledCache;
    std::atomic<bool> ledFeedback { true };
    int scanCountdown = 0;
};

} // namespace orbit
