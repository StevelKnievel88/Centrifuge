#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace orbit
{

/** The PREP page's keyboard: bass sounds (or your own samples) to play over the track and hear whether the key fits.
    Played from the on-screen keyboard (a MidiKeyboardState); mixed onto PREP's output by the audio engine. */
class KeySynth
{
public:
    enum Preset { psyBass, subBass, reeseBass, acidBass, doubleBass, numPresets };
    static const char* presetName (int p);

    KeySynth();

    juce::MidiKeyboardState& getKeyboardState() { return keyboard; }

    /** One of the built-in sounds. */
    void setPreset (int p);
    /** Your own sound: one sample played at every pitch (rootNote = the note it was recorded at). */
    bool setSingleSample (const juce::File& file, int rootNote);
    /** Your own sounds: a sample per note. Files named with a note (C2, F#3, Eb1 ...) go on that note; the rest go
        up the keys in name order from firstNote. Returns how many were loaded. */
    int setMultiSample (const juce::Array<juce::File>& files, int firstNote);

    void setVolume (float v) { volume = juce::jlimit (0.0f, 1.5f, v); }

    void prepare (double sampleRate);
    /** Adds the keyboard's sound into l / r (audio thread). */
    void renderAdd (float* l, float* r, int numSamples);

    static int noteFromName (const juce::String& fileName);   // "Bass C2.wav" -> 36 (C2 = 36), -1 = none

private:
    juce::MidiKeyboardState keyboard;
    juce::Synthesiser synth;
    juce::AudioFormatManager formats;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    double rate = 44100.0;
    std::atomic<float> volume { 0.8f };
    juce::CriticalSection setupLock;
};

} // namespace orbit
