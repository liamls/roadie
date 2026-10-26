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

inline juce::Colour background    { 0xff16171b };
inline juce::Colour surface       { 0xff1e2026 };
inline juce::Colour surfaceRaised { 0xff272a32 };
inline juce::Colour surfaceSunken { 0xff121318 };
inline juce::Colour border        { 0xff2e313a };

inline juce::Colour text      { 0xfff5f3ee };
inline juce::Colour textMuted { 0xff9a978f };
inline juce::Colour textFaint { 0xff6b6962 };

inline const juce::Colour ampAccent    { 0xffff7a3d };
inline const juce::Colour loopAccent   { 0xff2dd4bf };
inline const juce::Colour camAccent    { 0xffa78bfa };
inline const juce::Colour exportAccent { 0xffffd166 };

inline const juce::Colour ok   { 0xff4ade80 };
inline const juce::Colour warn { 0xfffbbf24 };
inline const juce::Colour rec  { 0xffe85d4a };

constexpr float radiusSm = 8.0f;
constexpr float radiusMd = 12.0f;
constexpr float radiusLg = 16.0f;

// Fixed colour per module — never a free palette per track.
enum class Module
{
    guitar,
    amp,
    loop,
    camera,
    exporting
};

juce::Colour accentFor (Module module);
} // namespace roadie::theme

// Typography. Target direction is Plus Jakarta Sans / Poppins (see the design
// doc); until those are embedded, Avenir Next — shipped with every macOS —
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
