#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <atomic>

namespace orbit
{

/** Musical key helpers: parse rekordbox key names ("Fm", "C#m", "Bb", "8A"), shift them, and find harmonic matches. */
struct MusicalKey
{
    int pitch = -1;        // 0 = C ... 11 = B, -1 = unknown
    bool minor = false;
    bool camelotStyle = false;

    bool isValid() const noexcept { return pitch >= 0; }

    /** How keys are shown in the app: false = standard ("Fm"), true = Camelot ("4A"). */
    static std::atomic<bool>& camelotDisplay() { static std::atomic<bool> c { false }; return c; }

    /** A key as text in the chosen notation (anything unrecognised is shown as it is). */
    static juce::String display (const juce::String& text)
    {
        auto k = parse (text);
        if (! k.isValid()) return text;
        k.camelotStyle = camelotDisplay().load();
        return k.toString();
    }

    /** The key's colour on the Camelot wheel (B = the outer, stronger ring; A = the inner, paler one). */
    static juce::Colour wheelColour (const juce::String& text)
    {
        auto k = parse (text);
        if (! k.isValid()) return {};
        static const juce::uint32 outer[12] { 0xff5cf0b4, 0xff84f27e, 0xffb4f25a, 0xfff2de6c, 0xfff8a37c, 0xfff5838f,
                                              0xfff07bb0, 0xffe07ad4, 0xffc27bea, 0xff9d8bf2, 0xff7ab3f2, 0xff4fe0e8 };   // 1B .. 12B
        const auto c = juce::Colour (outer[(size_t) (k.camelotNumber() - 1)]);
        return k.minor ? c.interpolatedWith (juce::Colours::white, 0.3f) : c;
    }

    static MusicalKey parse (const juce::String& text)
    {
        MusicalKey k;
        auto s = text.trim();
        if (s.isEmpty()) return k;

        // Camelot: 1A..12A / 1B..12B
        if (juce::CharacterFunctions::isDigit (s[0]))
        {
            const int n = s.getIntValue();
            const auto letter = juce::CharacterFunctions::toUpperCase (s.getLastCharacter());
            if (n < 1 || n > 12 || (letter != 'A' && letter != 'B')) return k;
            k.minor = letter == 'A';
            k.camelotStyle = true;
            // number = (7p + c) mod 12 + 1  =>  p = 7 * (number - 1 - c) mod 12   (7 is its own inverse mod 12)
            const int c = k.minor ? 4 : 7;
            k.pitch = (((7 * (n - 1 - c)) % 12) + 12) % 12;
            return k;
        }

        static const char* letters = "C D EF G A B";
        const auto first = juce::CharacterFunctions::toUpperCase (s[0]);
        int p = -1;
        for (int i = 0; i < 12; ++i)
            if (letters[i] == first) p = i;
        if (p < 0) return k;

        int idx = 1;
        if (s.length() > 1 && (s[1] == '#' || s[1] == 0x266F)) { ++p; ++idx; }
        else if (s.length() > 1 && (s[1] == 'b' || s[1] == 0x266D)) { --p; ++idx; }
        const auto rest = s.substring (idx).trim().toLowerCase();
        k.minor = rest.startsWith ("m") && ! rest.startsWith ("maj");
        k.pitch = (p + 12) % 12;
        return k;
    }

    int camelotNumber() const noexcept { return ((7 * pitch + (minor ? 4 : 7)) % 12) + 1; }

    MusicalKey shifted (int semitones) const noexcept
    {
        auto k = *this;
        if (k.isValid()) k.pitch = ((k.pitch + semitones) % 12 + 12) % 12;
        return k;
    }

    juce::String toString() const
    {
        if (! isValid()) return {};
        if (camelotStyle) return juce::String (camelotNumber()) + (minor ? "A" : "B");
        static const char* majorNames[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        static const char* minorNames[] = { "Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm" };
        return minor ? minorNames[pitch] : majorNames[pitch];
    }

    /** 0 = same key, 1 = harmonic neighbour (relative major/minor or +-1 on the Camelot wheel), 9 = clash. */
    static int compatibility (const MusicalKey& a, const MusicalKey& b) noexcept
    {
        if (! a.isValid() || ! b.isValid()) return 9;
        const int na = a.camelotNumber(), nb = b.camelotNumber();
        if (na == nb && a.minor == b.minor) return 0;
        if (na == nb) return 1;
        const int d = std::abs (na - nb);
        if (a.minor == b.minor && (d == 1 || d == 11)) return 1;
        return 9;
    }

    /** Smallest key shift (semitones, -6..6) that makes `mine` match or harmonise with `target`. 0 if none needed / possible. */
    static int syncShift (const MusicalKey& mine, const MusicalKey& target)
    {
        if (! mine.isValid() || ! target.isValid()) return 0;
        int best = 0, bestScore = 100;
        for (int s : { 0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6 })
        {
            const int score = compatibility (mine.shifted (s), target) * 3 + std::abs (s);
            if (score < bestScore) { bestScore = score; best = s; }
        }
        return best;
    }
};

} // namespace orbit
