# Psyched Devices: Cockpit skin, v1.0.0

This is the standard skin for every Psyched Devices program. It is taken from Mirari's main Cockpit skin, so anything built with it looks like part of the same family.

```
tokens.json          colours, type, shapes and effects (the source of truth for any other tech)
css/psyched-skin.css web, Electron and artifact pages (same class names as Mirari)
juce/                JUCE 8 apps: PsychedSkin.h/.cpp + psyched_skin.cmake
fonts/               Unbounded, Manrope, JetBrains Mono (static weights, SIL OFL)
specimen.html        every component on one page
```

## Rules

1. **Colour means function.** Pick a colour by what the control does, not by how it looks:

| Colour | Hex | Use |
|---|---|---|
| green | `#3cf06a` | inputs, armed, OK and connected, the pressed mode switch |
| cyan | `#2ee6d6` | layers, lanes, channels, decks |
| mint | `#7dffd6` | mapping, routing, MIDI, controllers |
| yellow | `#ffd339` | the active stage or selection, the focus ring, slider glow |
| pink | `#ff3fa0` | output, record, export, the brand glow |
| violet | `#c77dff` | modulation, automation, effects |
| blue | `#6e8cff` | sync, clock, tempo |
| orange | `#ff4b1f` | alerts, errors, destructive actions |
| amber | `#f5b54a` | warnings, pending, busy |

2. **Dark violet surfaces.** Surfaces go from page `#050409` through void `#07060d`, panel gradient `#110f1d` to `#0a0912`, raise `#171428` and field `#0b0a13`. Lines are `#24203a` and `#2c2844`; hover lines are `#4d4670`.
3. **Three typefaces.** Each has one job:
   - Unbounded 800 is for the wordmark only. Unbounded 600, uppercase with .18em tracking, is for titles.
   - Manrope is for running text.
   - JetBrains Mono is for labels (600, 10px, uppercase, .14em), values (500, tabular) and chips.
4. **Square corners, chamfered panels.** Radius is always 0. Panels have the top-left and bottom-right corners cut (14px, or 8px when small) and a 2px top border in their function colour.
5. **Glow means "on".** Glow appears only on things that are lit:
   - LEDs
   - pressed chips (an inner glow)
   - solid buttons (an outer glow)
   - the wordmark (pink)
   - the slider thumb

   Nothing that is off glows.
6. **Backdrop.** A radial violet bloom sits at 45%/32%, under a 40px grid of faint blue lines. Readouts and wells get scanlines.
7. **Wordmark.** The name is in caps with one signature letter in pink (Mirari mirrors its R, Centrifuge spins its C), white letters and a pink glow.

## Web

```html
<link rel="stylesheet" href="psyched-skin/css/psyched-skin.css">
<body class="app">
  <header class="hdr"><span class="word">MI<span class="mr">R</span>ARI</span></header>
  <section class="panel chamf" style="--c:var(--pink)"><div class="ttl">Output</div></section>
  <button class="chip on" style="--c:var(--cyan)">Deck 1</button>
  <button class="chip solid pink">Export</button>
  <span class="led on"></span> <span class="lbl">Connected</span> <span class="val">128.00</span>
</body>
```

For a page that must be a single file (an artifact), load the fonts from Google Fonts. The URL is at the top of the CSS.

## JUCE 8

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/skin/psyched-skin/juce/psyched_skin.cmake)
psyched_skin_add(MyApp)
```

```cpp
#include "PsychedSkin.h"
psyched::LookAndFeel laf;                         // owned by your main component
juce::LookAndFeel::setDefaultLookAndFeel (&laf);

// in paint():
psyched::paintBackdrop (g, getLocalBounds());
psyched::paintHeader (g, header.toFloat());
psyched::paintWordmark (g, logoArea, "Mirari", 2, psyched::Signature::mirror);
psyched::paintPanel (g, area, psyched::Colours::pink);
psyched::paintChip (g, r, "Sync", psyched::Colours::blue, psyched::ChipState::on);
psyched::paintLed (g, centre, psyched::Colours::green, true);
psyched::paintLabel (g, "Tempo", r);  g.setFont (psyched::Fonts::value());
```

Buttons take their function colour from `TextButton::buttonOnColourId`. You can also set these component properties:

- `psyched.solid`: a filled primary button
- `psyched.mode`: a mode-switch tab, solid green when on
- `psyched.swatch`: a colour-swatch button
- `psyched.cut`: chamfers a button

## Max for Live and other tools

Take the hex values from `tokens.json`. In Max, set the patcher's `bgcolor` to void and use `panel` with `edge` borders. Use the function colours for `activebgoncolor` and `textoncolor`. For fonts, use Manrope for text and JetBrains Mono for numbers when they are installed, otherwise Arial and Arial Mono.

## Fonts

Unbounded, Manrope and JetBrains Mono are used under the SIL Open Font License 1.1. The licences are in `fonts/`.
