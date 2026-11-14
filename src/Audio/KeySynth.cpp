#include "KeySynth.h"
#include <set>

namespace orbit
{

namespace
{
    /** Any note. */
    struct AnySound : juce::SynthesiserSound
    {
        explicit AnySound (int p) : preset (p) {}
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
        const int preset;
    };

    /** The built-in bass sounds: a small subtractive voice (oscillators, a resonant low-pass with its own envelope,
        an amp envelope) and a plucked string for the double bass. */
    struct BassVoice : juce::SynthesiserVoice
    {
        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<AnySound*> (s) != nullptr; }

        void startNote (int note, float velocity, juce::SynthesiserSound* s, int) override
        {
            preset = static_cast<AnySound*> (s)->preset;
            freq = juce::MidiMessage::getMidiNoteInHertz (note);
            level = 0.35f + 0.65f * velocity;
            env = 0.0f; fenv = 1.0f; released = false;
            ph1 = ph2 = ph3 = 0.0;
            lp1 = lp2 = 0.0f;
            if (preset == KeySynth::doubleBass)
            {
                // Karplus-Strong: a burst of filtered noise in a delay line the length of the note
                const int len = juce::jmax (2, (int) std::round (getSampleRate() / freq));
                ks.assign ((size_t) len, 0.0f);
                juce::Random rnd;
                float prev = 0.0f;
                for (auto& x : ks) { const float n = rnd.nextFloat() * 2.0f - 1.0f; prev = prev * 0.55f + n * 0.45f; x = prev; }
                ksPos = 0;
            }
        }

        void stopNote (float, bool allowTail) override
        {
            if (! allowTail) { clearCurrentNote(); return; }
            released = true;
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        static float saw (double ph) { return (float) (2.0 * ph - 1.0); }

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;
            const double sr = getSampleRate();
            const double inc = freq / sr;
            // per sound: amp attack / decay / sustain / release (s), filter base / envelope amount (Hz), resonance
            float att = 0.003f, dec = 0.25f, sus = 0.0f, rel = 0.06f, cut = 180.0f, amt = 2600.0f, fdec = 0.12f, res = 0.35f;
            switch (preset)
            {
                case KeySynth::psyBass:   att = 0.002f; dec = 0.16f; sus = 0.0f;  rel = 0.03f; cut = 160.0f; amt = 2400.0f; fdec = 0.09f; res = 0.45f; break;
                case KeySynth::subBass:   att = 0.005f; dec = 0.30f; sus = 0.85f; rel = 0.12f; break;
                case KeySynth::reeseBass: att = 0.01f;  dec = 0.40f; sus = 0.8f;  rel = 0.2f;  cut = 420.0f; amt = 600.0f;  fdec = 0.5f;  res = 0.2f;  break;
                case KeySynth::acidBass:  att = 0.002f; dec = 0.30f; sus = 0.5f;  rel = 0.05f; cut = 220.0f; amt = 3200.0f; fdec = 0.18f; res = 0.85f; break;
                default: break;
            }
            const float aStep = 1.0f / (float) juce::jmax (1.0, att * sr);
            const float dMul = std::exp (-1.0f / (float) juce::jmax (1.0, dec * sr * 0.35));
            const float rMul = std::exp (-1.0f / (float) juce::jmax (1.0, rel * sr * 0.35));
            const float fMul = std::exp (-1.0f / (float) juce::jmax (1.0, fdec * sr * 0.35));
            for (int i = 0; i < num; ++i)
            {
                float s;
                if (preset == KeySynth::doubleBass)
                {
                    // the string: average neighbours (damping) round the loop
                    auto& a = ks[(size_t) ksPos];
                    const auto& b = ks[(size_t) ((ksPos + 1) % (int) ks.size())];
                    const float y = 0.4985f * (a + b);
                    s = a;
                    a = y;
                    ksPos = (ksPos + 1) % (int) ks.size();
                    if (released) { env *= rMul; } else env = 1.0f;
                    s *= 1.4f * env;
                    if (released && env < 0.0005f) { clearCurrentNote(); return; }
                }
                else
                {
                    // the amp envelope
                    if (released) env *= rMul;
                    else if (attacking) { env += aStep; if (env >= 1.0f) { env = 1.0f; attacking = false; } }
                    else env = sus + (env - sus) * dMul;
                    if (env <= 0.0f && ! attacking) env = 0.0f;
                    fenv *= fMul;

                    float osc;
                    if (preset == KeySynth::subBass)
                        osc = (float) std::sin (juce::MathConstants<double>::twoPi * ph1) * 0.9f
                              + (float) std::sin (juce::MathConstants<double>::twoPi * ph1 * 2.0) * 0.08f;
                    else if (preset == KeySynth::reeseBass)
                        osc = 0.5f * (saw (ph1) + saw (ph2)) * 0.8f + (float) std::sin (juce::MathConstants<double>::twoPi * ph3) * 0.5f;
                    else
                        osc = saw (ph1);
                    ph1 += inc; if (ph1 >= 1.0) ph1 -= 1.0;
                    ph2 += inc * 1.0072; if (ph2 >= 1.0) ph2 -= 1.0;   // detuned, for the reese's movement
                    ph3 += inc * 0.5; if (ph3 >= 1.0) ph3 -= 1.0;

                    if (preset == KeySynth::subBass) s = osc;
                    else
                    {
                        // a 2-pole resonant low-pass (state variable style)
                        const float fc = juce::jlimit (30.0f, (float) (sr * 0.45), cut + amt * fenv);
                        const float f = 2.0f * std::sin (juce::MathConstants<float>::pi * fc / (float) sr);
                        const float q = 1.0f - res * 0.9f;
                        lp1 += f * lp2;
                        const float hp = osc - lp1 - q * lp2;
                        lp2 += f * hp;
                        s = std::tanh (lp1 * 1.4f);
                    }
                    s *= env;
                    if (released && env < 0.0005f) { clearCurrentNote(); return; }
                }
                s *= level * 0.6f;
                for (int ch = 0; ch < out.getNumChannels(); ++ch) out.addSample (ch, start + i, s);
            }
        }

        int preset = 0;
        double freq = 55.0, ph1 = 0.0, ph2 = 0.0, ph3 = 0.0;
        float level = 1.0f, env = 0.0f, fenv = 1.0f, lp1 = 0.0f, lp2 = 0.0f;
        bool released = false, attacking = true;
        std::vector<float> ks;
        int ksPos = 0;
    };

    struct ResetAttack : BassVoice
    {
        void startNote (int note, float velocity, juce::SynthesiserSound* s, int p) override { attacking = true; BassVoice::startNote (note, velocity, s, p); }
    };
}

const char* KeySynth::presetName (int p)
{
    switch (p)
    {
        case psyBass:    return "Psy bass (rolling pluck)";
        case subBass:    return "Sub bass (sine)";
        case reeseBass:  return "Warp / reese bass (detuned)";
        case acidBass:   return "Acid bass (resonant)";
        case doubleBass: return "Double bass (plucked string)";
        default:         return "";
    }
}

KeySynth::KeySynth()
{
    formats.registerBasicFormats();
    setPreset (psyBass);
}

void KeySynth::setPreset (int p)
{
    const juce::ScopedLock sl (setupLock);
    synth.allNotesOff (0, false);
    synth.clearVoices();
    synth.clearSounds();
    for (int i = 0; i < 8; ++i) synth.addVoice (new ResetAttack());
    synth.addSound (new AnySound (juce::jlimit (0, numPresets - 1, p)));
}

bool KeySynth::setSingleSample (const juce::File& file, int rootNote)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    if (r == nullptr) return false;
    juce::BigInteger all;
    all.setRange (0, 128, true);
    const juce::ScopedLock sl (setupLock);
    synth.allNotesOff (0, false);
    synth.clearVoices();
    synth.clearSounds();
    for (int i = 0; i < 8; ++i) synth.addVoice (new juce::SamplerVoice());
    synth.addSound (new juce::SamplerSound (file.getFileNameWithoutExtension(), *r, all, rootNote, 0.002, 0.08, 20.0));
    return true;
}

int KeySynth::noteFromName (const juce::String& name)
{
    // the last note name in it: a letter A-G, maybe # or b, then an octave (C2 = 36: octave numbering with C4 = 60)
    int found = -1;
    const auto s = name.toUpperCase();
    for (int i = 0; i < s.length(); ++i)
    {
        const juce::juce_wchar c = s[i];
        if (c < 'A' || c > 'G') continue;
        if (i > 0 && juce::CharacterFunctions::isLetter (s[i - 1])) continue;
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        int n = base[c - 'A'];
        int j = i + 1;
        if (j < s.length() && (s[j] == '#' || name[j] == 'b')) { n += s[j] == '#' ? 1 : -1; ++j; }
        bool neg = false;
        if (j < s.length() && s[j] == '-') { neg = true; ++j; }
        if (j < s.length() && juce::CharacterFunctions::isDigit (s[j]))
        {
            const int oct = (s[j] - '0') * (neg ? -1 : 1);
            found = juce::jlimit (0, 127, (oct + 1) * 12 + n);
        }
    }
    return found;
}

int KeySynth::setMultiSample (const juce::Array<juce::File>& filesIn, int firstNote)
{
    auto files = filesIn;
    files.sort();
    struct Item { juce::File f; int note; };
    std::vector<Item> items;
    std::set<int> used;
    for (auto& f : files) if (const int n = noteFromName (f.getFileNameWithoutExtension()); n >= 0) { items.push_back ({ f, n }); used.insert (n); }
    int next = firstNote;
    for (auto& f : files)
        if (noteFromName (f.getFileNameWithoutExtension()) < 0)
        {
            while (used.count (next)) ++next;
            items.push_back ({ f, next });
            used.insert (next++);
        }
    const juce::ScopedLock sl (setupLock);
    synth.allNotesOff (0, false);
    synth.clearVoices();
    synth.clearSounds();
    for (int i = 0; i < 8; ++i) synth.addVoice (new juce::SamplerVoice());
    int count = 0;
    for (auto& it : items)
    {
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (it.f));
        if (r == nullptr) continue;
        juce::BigInteger one;
        one.setBit (it.note);
        synth.addSound (new juce::SamplerSound (it.f.getFileNameWithoutExtension(), *r, one, it.note, 0.002, 0.08, 20.0));
        ++count;
    }
    return count;
}

void KeySynth::prepare (double sampleRate)
{
    rate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    buffer.setSize (2, 4096);
}

void KeySynth::renderAdd (float* l, float* r, int numSamples)
{
    const juce::ScopedTryLock sl (setupLock);
    if (! sl.isLocked()) return;
    if (buffer.getNumSamples() < numSamples)
    {
        for (int pos = 0; pos < numSamples; pos += buffer.getNumSamples())
            renderAdd (l + pos, r + pos, juce::jmin (buffer.getNumSamples(), numSamples - pos));
        return;
    }
    midi.clear();
    keyboard.processNextMidiBuffer (midi, 0, numSamples, true);
    buffer.clear (0, numSamples);
    synth.renderNextBlock (buffer, midi, 0, numSamples);
    const float v = volume.load();
    juce::FloatVectorOperations::addWithMultiply (l, buffer.getReadPointer (0), v, numSamples);
    juce::FloatVectorOperations::addWithMultiply (r, buffer.getReadPointer (1), v, numSamples);
}

} // namespace orbit
