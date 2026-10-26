#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Line icons from the mockup (24×24 viewBox, medium stroke, round caps):
// a sober register — no emoji, no illustration.
namespace roadie::icons
{
enum class Icon
{
    previous,
    play,
    pause,
    stop,
    record,
    loop,
    exportArrow,
    gauge,
    search,
    guitar,
    amp,
    camera,
    plus,
    undo,
    clear,
    settings,
    metronome,
    folder,
    check,
    close,
    waveform
};

void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth = 2.0f);
} // namespace roadie::icons
