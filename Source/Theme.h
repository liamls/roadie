#pragma once

#include <juce_graphics/juce_graphics.h>

// Colour tokens from the "backstage chaleureux" direction (docs/design-direction.md).
// The neutral tokens are switched by setAppearance(): every paint reads them
// fresh, so a theme change is a repaint away. Module and semantic colours
// stay the same in both appearances.
namespace roadie::theme
{
enum class Appearance
{
    dark,
    light
};

void setAppearance (Appearance appearance);
Appearance getAppearance();

inline juce::Colour background    { 0xff181613 };
inline juce::Colour surface       { 0xff23201d };
inline juce::Colour surfaceRaised { 0xff2f2c28 };
inline juce::Colour surfaceSunken { 0xff100e0c };
inline juce::Colour border        { 0xff3d3a36 };

inline juce::Colour text      { 0xfff6f3ed };
inline juce::Colour textMuted { 0xffb0aca5 };
inline juce::Colour textFaint { 0xff87827d };

// Module accents are Logic Pro's region colours: the same green, orange,
// purple, blue and yellow a Logic session uses, so a track reads like it does
// in the DAW. Fixed per module, at similar lightness so none outshines another.
inline const juce::Colour ampAccent    { 0xff5cc45e }; // Guitare: green
inline const juce::Colour loopAccent   { 0xfff5a02e }; // Looper: orange
inline const juce::Colour camAccent    { 0xffa970ea }; // Vidéo: purple
inline const juce::Colour exportAccent { 0xfff6cd45 }; // yellow
inline const juce::Colour songAccent   { 0xff4093f0 }; // Morceau: blue, an imported audio file

// rec is a true red, kept well away from the amp orange.
inline const juce::Colour ok   { 0xff6fd88a }; // a minty green, apart from the Guitare green
inline const juce::Colour warn { 0xfffabb41 };
inline const juce::Colour rec  { 0xfff73a37 };

// Labels and icons drawn on a solid accent fill: dark in both appearances
// (13:1 on exportAccent, 5:1 on rec, where white would only reach 3.7:1).
inline const juce::Colour onAccent { 0xff100e0c };

// An accent (or ok / warn / rec) used as text or icon colour. In the light
// appearance the bright accents drown in the cream, so each one maps to a
// darker ink (4.5:1 on background); in the dark one the accent reads as is.
// Fills, outlines and meters keep the accent itself. Pass the base colour,
// before any alpha change.
juce::Colour ink (juce::Colour accent);

// Squircle corners (CSS `corner-shape: squircle`, a superellipse of
// exponent 4) instead of circular arcs: the curve eases in and out of the
// straight edge, so a card or a button reads as one continuous shape. Same
// `radius` as a rounded rectangle; use these instead of
// fill/drawRoundedRectangle everywhere.
juce::Path squircle (juce::Rectangle<float> area, float radius);
void fillSquircle (juce::Graphics& g, juce::Rectangle<float> area, float radius);
void drawSquircle (juce::Graphics& g, juce::Rectangle<float> area, float radius, float thickness);

constexpr float radiusSm = 8.0f;
constexpr float radiusMd = 12.0f;
constexpr float radiusLg = 16.0f;
} // namespace roadie::theme

// Typography. Target direction is Plus Jakarta Sans / Poppins (see the design
// doc); for now the system font, San Francisco (the one Logic Pro uses) —
// gives the same warm geometric grotesque without bundling font files.
// Sizes are em sizes (CSS px), not JUCE ascent+descent heights.
namespace roadie::fonts
{
juce::Font body (float size);     // Regular — labels, values
juce::Font medium (float size);   // Medium — secondary emphasis
juce::Font semibold (float size); // Demi Bold — names, buttons
juce::Font display (float size);  // Bold — titles, wordmark, big numbers

// Draws `text` with every digit in a fixed-width slot (tabular figures), so
// BPM, CPU and timers don't jitter as their values change.
void drawTabular (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                  juce::Justification justification);
float getTabularWidth (const juce::Font& font, const juce::String& text);
} // namespace roadie::fonts
