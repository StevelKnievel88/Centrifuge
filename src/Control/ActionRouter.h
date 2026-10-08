#pragma once
#include "Actions.h"
#include "../Audio/AudioEngine.h"

namespace orbit
{

/** Turns control events (from MIDI, keyboard or the UI) into deck commands and app actions. Thread-safe. */
class ActionRouter
{
public:
    explicit ActionRouter (AudioEngine& engine);

    /** Can be called from any thread (MIDI callbacks included). */
    void perform (const ActionDef& action, const ActionValue& v);
    void perform (const juce::String& actionId, const ActionValue& v);

    /** Called on the message thread for browser / view actions. */
    std::function<void (const juce::String& actionId, int deck, const ActionValue&)> onAppAction;

    /** Whether the LED for this action should be lit (message thread). */
    bool ledState (const ActionDef& action) const;

    double getBeatJumpBeats (int deck) const { return beatJump[(size_t) deck].load(); }
    int getPitchRangePercent (int deck) const;
    bool isVinylMode (int deck) const { return vinyl[(size_t) deck].load(); }

    void setJogSensitivity (double ticksPerRevolution);

private:
    AudioEngine& engine;
    std::array<std::atomic<int>, 4> rangeIndex;
    std::array<std::atomic<double>, 4> beatJump;
    std::array<std::atomic<bool>, 4> vinyl;
};

} // namespace orbit
