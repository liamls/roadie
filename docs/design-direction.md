# Design direction — "Warm backstage"

An extract of the full design research (the developer's Obsidian notes, outside the repository). This document holds what is needed to build the final UI (Milestone 6) without reopening Obsidian.

## Interactive mockup

Interactive HTML mockup (DAW-style main screen: library, tracks coloured by module, floating camera monitor, contextual panel):
**https://claude.ai/code/artifact/747976f8-55b3-41c1-a689-b895727c86f3**

⚠️ Private link (belongs to the developer's Claude account) — open it with their session, it isn't publicly accessible.

## Guiding concept

> A dark, warm stage (not the cold black of a DAW) on which the module colours burst like spotlights — an echo of the roadie metaphor, organising the gear in the half-light of the wings.

Positioning: between the pro tools (Ableton, Logic, Resolve — dark, dense, technical jargon) and the consumer tools (GarageBand, Lava Studio — welcoming but limited). Roadie aims for the technical depth of the former with the visual warmth of the latter.

## Palette

Implemented in `Source/Theme.h` (revision 2, October 2026). Detailed proposal, with the Logic Pro / GarageBand / Ableton Live benchmark and a preview of the screen: **https://claude.ai/artifact/VvXv1geVkQMfh5bGUWWTLS** (private link, same remark as for the mockup).

What the benchmark of the three apps showed, and what this revision rests on:

- **Neutral chrome, colour reserved for content**: panels in near-untinted grey, colour only on regions, clips and states. Roadie keeps this principle with its per-module colour.
- **Marked surface steps**: about +16 in sRGB per level in Logic and GarageBand (`#18` → `#28` → `#38`). The v1 steps (about +8) made panels blur together.
- **Colour tells the type** (GarageBand: green instrument, blue audio, yellow Drummer): this is the "beginner" lever. The v1 accents were the exact Tailwind values (teal-400, violet-400…); they are recalibrated in OKLCH at similar lightness.
- **Red = recording, and nothing else** (Logic: pure red). The v1 `rec` was a coral too close to the amp's orange.

The v1 neutrals (`#16171B`…) actually leaned blue despite the "warm" intent; the v2 ones have a real, very light warm tint (OKLCH h between 67° and 85°, chroma ≤ 0.008 for surfaces and ≤ 0.011 for text).

| Token | Hex | Usage |
|---|---|---|
| `background` | `#181613` | Main background, warm charcoal, never pure black |
| `surface` | `#23201D` | Level 1 cards |
| `surfaceRaised` | `#2F2C28` | Level 2 cards / raised components |
| `surfaceSunken` | `#100E0C` | Recessed fields (displays, bars) |
| `border` | `#3D3A36` | Outlines |
| `text` | `#F6F3ED` | Main text, warm off-white (16:1 on the background) |
| `textMuted` | `#B0ACA5` | Secondary text (8:1) |
| `textFaint` | `#87827D` | Tertiary text (4.8:1, above the 4.5:1 threshold for small text) |

**Fixed colour per module** (never a free palette per track — too much for an uninitiated audience):

| Module | Hex | Token |
|---|---|---|
| 🟠 Amp | `#F9853B` | `ampAccent` |
| 🟢 Looper | `#47DAB4` | `loopAccent` |
| 🟣 Camera | `#B494F5` | `camAccent` |
| 🟡 Export | `#FBD452` | `exportAccent` — the only primary action colour |

Semantics (distinct from the colour accent, never reuse for anything else):

| Role | Hex | Token |
|---|---|---|
| OK / validated | `#61D779` | `ok` |
| Warning | `#FABB41` | `warn` — reserved for the CPU / latency gauges, so it isn't confused with Export |
| Active recording | `#F73A37` | `rec` |

Text and icons placed on a solid accent fill (filled button, selected region header, logo): `onAccent` `#100E0C`, dark in both themes (13:1 on yellow, 5:1 on red, where white would only reach 3.7:1).

## Typography

**Implemented with Avenir Next** (macOS system font, `roadie::fonts` in `Source/Theme.h`): same family as the target below, with no file to bundle and no license to check. Regular/Medium for the interface, Demi Bold for names and buttons, Bold for titles and big numbers; tabular figures simulated by `roadie::fonts::drawTabular` (BPM, CPU, timers). Outside macOS, falls back to the default font — bundle Plus Jakarta Sans / Poppins if the exact identity matters. Target direction (consistent with the dominant "rounded geometric grotesque" noted on creator/music apps in the design research):

- **Interface** (labels, values, lists): warm, rounded geometric grotesque — like *Inter*, *Plus Jakarta Sans* or *General Sans*
- **Titles / wordmark**: slightly condensed, round bold display — like *Poppins* or *Gilroy*
- **Numbers** (BPM, CPU load, duration): tabular variant to stay readable while playing

To bundle a custom font in JUCE: convert it to binary with `juce_add_binary_data` (CMake) then load it with `juce::Typeface::createSystemTypefaceFor()`. Mind the licenses of the chosen fonts before bundling them in a distributed binary.

## Shapes & components

- Moderate but generous border radii: 12-16px on cards/buttons — already applied (`RoadieLookAndFeel`, radii 8-16px depending on the component)
- **Knobs**: conic progress ring in the module's colour + pointer, recessed face — `RoadieLookAndFeel::drawRotarySlider`, colour taken from the slider (`roadie::ui::Knob` receives its module's accent)
- VU meters / waveforms in a gradient of the module's colour rather than the classic console green/red
- Line icons, medium stroke, rounded caps — a sober register, no emoji or childish illustration
- ~~Page navigation~~ → **single DAW-style screen, like the chosen mockup**: library on the left, Guitar / Amp / Looper / Camera tracks in the centre, inspector at the bottom. Selecting a track (click or keys 1/2/3) shows its settings in the inspector with its module's colour (accent bar on the track and the inspector). Export is a modal window (yellow Export button)

## Light/dark mode

Dark by default (comfort when playing/on stage, webcam contrast). **Light mode implemented** (View menu or ⇧⌘L, remembered): same 4 module colours, warm cream background rather than pure white, to stay in the "backstage" spirit and not "office". The neutral tokens of `Source/Theme.h` are switched live by `roadie::theme::setAppearance()`:

| Token | Dark | Light |
|---|---|---|
| `background` | `#181613` | `#F1ECE4` |
| `surface` | `#23201D` | `#F9F6F1` |
| `surfaceRaised` | `#2F2C28` | `#FEFCF9` |
| `surfaceSunken` | `#100E0C` | `#E5DFD6` |
| `border` | `#3D3A36` | `#D8D2C7` |
| `text` | `#F6F3ED` | `#221D18` |
| `textMuted` | `#B0ACA5` | `#69625B` |
| `textFaint` | `#87827D` | `#857F77` |

In light mode, `textMuted` reaches 5.1:1 on the background and `textFaint` 3.4:1 (against 2.6:1 in v1).

**Text and icons in accent colour** ("Camera" label, hint pills, track and button icons, ticks, status messages): go through `roadie::theme::ink (accent)`. In dark it returns the accent as is; in light, a darkened "ink" at 4.5:1 or more on the cream, where the vivid accents would disappear (Export yellow above all). Fills, outlines, gauges and waveforms keep the accent itself.

| Accent | Ink (light mode) |
|---|---|
| `ampAccent` | `#AF4B00` |
| `loopAccent` | `#097964` |
| `camAccent` | `#764EB7` |
| `exportAccent` | `#896600` |
| `ok` | `#217937` |
| `warn` | `#945F00` |
| `rec` | `#CC2827` |

The camera monitor's image area stays dark in both themes (it's a "screen").
