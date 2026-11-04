#pragma once
#include <juce_graphics/juce_graphics.h>

namespace orbit
{

/** rekordbox's hot cue colour codes (1..62) as stored in ANLZ PCO2 entries and master.db (djmdCue.Color).
    Table from Deep Symmetry's beat-link (CueList.findRekordboxColor). */
inline juce::Colour rekordboxHotCueColour (int code)
{
    static const juce::uint32 table[63] = {
        0,
        0xff305aff, 0xff5073ff, 0xff508cff, 0xff50a0ff, 0xff50b4ff, 0xff50b0f2, 0xff50aee8, 0xff45acdb,
        0xff00e0ff, 0xff19daf0, 0xff32d2e6, 0xff21b4b9, 0xff20aaa0, 0xff1fa392, 0xff19a08c, 0xff14a584,
        0xff14aa7d, 0xff10b176, 0xff30d26e, 0xff37de5a, 0xff3ceb50, 0xff28e214, 0xff7dc13d, 0xff8cc832,
        0xff9bd723, 0xffa5e116, 0xffa5dc0a, 0xffaad208, 0xffb4c805, 0xffb4be04, 0xffbab404, 0xffc3af04,
        0xffe1aa00, 0xffffa000, 0xffff9600, 0xffff8c00, 0xffff7500, 0xffe0641b, 0xffe0461e, 0xffe0301e,
        0xffe02823, 0xffe62828, 0xffff376f, 0xffff2d6f, 0xffff127b, 0xfff51e8c, 0xffeb2da0, 0xffe637b4,
        0xffde44cf, 0xffde448d, 0xffe630b4, 0xffe619dc, 0xffe600ff, 0xffdc00ff, 0xffcc00ff, 0xffb432ff,
        0xffb93cff, 0xffc542ff, 0xffaa5aff, 0xffaa72ff, 0xff8272ff, 0xff6473ff };
    if (code <= 0 || code > 62) return {};
    return juce::Colour (table[code]);
}

inline int colourDistance (juce::Colour a, juce::Colour b)
{
    const int dr = a.getRed() - b.getRed(), dg = a.getGreen() - b.getGreen(), db = a.getBlue() - b.getBlue();
    return dr * dr * 2 + dg * dg * 4 + db * db * 3;
}

/** The rekordbox hot cue colour code nearest to any colour (rekordbox can only store these 62). */
inline int nearestRekordboxHotCueCode (juce::Colour c)
{
    int best = 22, bestD = std::numeric_limits<int>::max();   // 22 = rekordbox's default green
    for (int i = 1; i <= 62; ++i)
    {
        const int d = colourDistance (c, rekordboxHotCueColour (i));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

/** rekordbox's eight memory cue / track colours (ID 1-8). */
inline juce::Colour rekordboxMemoryColour (int id)
{
    static const juce::uint32 table[9] = { 0, 0xfff870d0, 0xffe8102a, 0xffff8c00, 0xffffd500,
                                           0xff16c00d, 0xff00d2e6, 0xff1e5cff, 0xff9a3cff };
    if (id <= 0 || id > 8) return {};
    return juce::Colour (table[id]);
}

/** Memory cues in rekordbox can only take one of eight colours (0 = none / default red). */
inline int nearestRekordboxMemoryColourId (juce::Colour c)
{
    if (c.isTransparent()) return 0;
    int best = 2, bestD = std::numeric_limits<int>::max();
    for (int i = 1; i <= 8; ++i)
    {
        const int d = colourDistance (c, rekordboxMemoryColour (i));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

} // namespace orbit
