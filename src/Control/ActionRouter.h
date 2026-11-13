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
    /** A deck's published state (for controller displays and lights). */
    const Deck::Published& deckState (int deck) const { return engine.deck (deck).state(); }

    void setJogSensitivity (double ticksPerRevolution);

    /** Stem matrix, each deck its own: controls A-D move deck 1, E-H deck 2 (mapped as each deck's
        "stem control A-D"). For each control a bitmask of the stems it moves (bit 0 drums, 1 bass, 2 inst, 3 vocals). */
    void setStemMatrix (const std::array<int, 8>& masks);
    std::array<int, 8> getStemMatrix() const;

    /** The deck's four beat jump buttons: each its own size (beats) and colour (0 = default; it also lights the pads
        they're mapped to). Mapped as "beat jump 1-4 back / forward". */
    static constexpr double defaultJumpBeats[4] { 1.0, 4.0, 8.0, 16.0 };
    double getJumpSlotBeats (int deck, int slot) const { return jumpSlotBeats[(size_t) (deck * 4 + slot)].load(); }
    void setJumpSlotBeats (int deck, int slot, double beats) { jumpSlotBeats[(size_t) (deck * 4 + slot)] = beats; }
    juce::uint32 getJumpSlotColour (int deck, int slot) const { return jumpSlotColour[(size_t) (deck * 4 + slot)].load(); }
    void setJumpSlotColour (int deck, int slot, juce::uint32 c) { jumpSlotColour[(size_t) (deck * 4 + slot)] = c; }
    /** Which slot a "beatjump.back.N" / "beatjump.fwd.N" action is (N is the slot's default size), or -1. */
    static int jumpSlotOf (const juce::String& name);

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
    std::array<std::array<std::atomic<float>, 4>, 2> stemVolume, controlVolume, stemFilterV, controlFilter;
    std::array<std::array<std::atomic<bool>, 4>, 2> stemMute, controlMute;
    std::array<std::atomic<int>, 8> matrix;   // [deck * 4 + control]
    std::array<std::atomic<bool>, 2> keySyncLit;
public:
    bool isStemCtlFilterMode (int d) const { return d >= 0 && d < 2 && stemCtlFilterMode[(size_t) d].load(); }
    void toggleStemCtlFilterMode (int d);
private:
    std::array<std::atomic<bool>, 2> stemCtlFilterMode {};
    // the switched knobs (stemctl.X.knob): where each knob is, and a high-pass left where it was after switching
    // until the knob comes back to the centre (so switching never jumps)
    std::array<std::array<std::atomic<float>, 4>, 2> knobPos {};
    std::array<std::array<std::atomic<bool>, 4>, 2> hpHeld {};
    void applyStemKnob (int d, int c);   // per deck: the stem control knobs work the filters
    std::array<std::atomic<double>, 8> jumpSlotBeats { 1.0, 4.0, 8.0, 16.0, 1.0, 4.0, 8.0, 16.0 };
    std::array<std::atomic<juce::uint32>, 8> jumpSlotColour {};
};

} // namespace orbit
