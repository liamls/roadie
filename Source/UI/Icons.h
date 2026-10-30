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
    waveform,
    cycle,     // the cycle zone (two arrows around a rectangle), not the looper
    library,   // sidebar on the left
    inspector, // panel at the bottom
    timeline,  // regions on lanes: the Monter workspace
    song,      // a note: the backing track
    tuner,     // a needle over an arc
    monitor,   // a screen: the video monitor
    power,     // a block on / bypassed
    arrow      // → between the blocks of the chain
};

void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth = 2.0f);
} // namespace roadie::icons
