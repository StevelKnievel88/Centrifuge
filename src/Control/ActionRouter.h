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
    /** Loop size for the deck's loop button (beats). */
    double getLoopBeats (int deck) const { return loopBeats[(size_t) deck].load(); }
    int getPitchRangePercent (int deck) const;
    bool isVinylMode (int deck) const { return vinyl[(size_t) deck].load(); }

    void setJogSensitivity (double ticksPerRevolution);

    /** Stem matrix, shared by both decks: for control A..D, a bitmask of the stems it moves
        (bit 0 drums, 1 bass, 2 inst, 3 vocals). */
    void setStemMatrix (const std::array<int, 4>& masks);
    std::array<int, 4> getStemMatrix() const;

    /** The key a deck is effectively playing in (track key + key shift + tempo when key lock is off). */
    juce::String effectiveKeyText (int deck) const;

private:
    void updateStems (int deck);
    void keySync (int deck);

    AudioEngine& engine;
    std::array<std::atomic<int>, 4> rangeIndex;
    std::array<std::atomic<double>, 4> beatJump;
    std::array<std::atomic<double>, 4> loopBeats;
    std::array<std::atomic<bool>, 4> vinyl;

    // Stem state per deck: direct per-stem volume/mute, and the four matrix controls.
    std::array<std::array<std::atomic<float>, 4>, 2> stemVolume, controlVolume;
    std::array<std::array<std::atomic<bool>, 4>, 2> stemMute, controlMute;
    std::array<std::atomic<int>, 4> matrix;
    std::array<std::atomic<bool>, 2> keySyncLit;
};

} // namespace orbit
