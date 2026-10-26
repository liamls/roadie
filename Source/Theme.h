#pragma once

#include <juce_graphics/juce_graphics.h>

// Colour tokens from the "backstage chaleureux" direction (Obsidian note, §11.4).
namespace roadie::theme
{
inline const juce::Colour background    { 0xff16171b };
inline const juce::Colour surface       { 0xff1e2026 };
inline const juce::Colour surfaceRaised { 0xff242730 };
inline const juce::Colour surfaceSunken { 0xff121318 };
inline const juce::Colour border        { 0xff2e313a };

inline const juce::Colour text      { 0xfff5f3ee };
inline const juce::Colour textMuted { 0xff9a978f };
inline const juce::Colour textFaint { 0xff6b6962 };

inline const juce::Colour ampAccent    { 0xffff7a3d };
inline const juce::Colour loopAccent   { 0xff2dd4bf };
inline const juce::Colour camAccent    { 0xffa78bfa };
inline const juce::Colour exportAccent { 0xffffd166 };

inline const juce::Colour ok   { 0xff4ade80 };
inline const juce::Colour warn { 0xfffbbf24 };
inline const juce::Colour rec  { 0xffe85d4a };
} // namespace roadie::theme
