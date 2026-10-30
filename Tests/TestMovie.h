#pragma once

#include <juce_graphics/juce_graphics.h>

namespace roadie::test
{
struct MovieInfo
{
    double durationSeconds = 0.0;
    bool hasVideo = false, hasAudio = false;
    int width = 0, height = 0;
};

bool writeTestMovie (const juce::File& file, int width, int height, double seconds);
MovieInfo inspectMovie (const juce::File& file);

// Number of near-white pixels in a region of the frame at `seconds`, the
// region given as fractions of the frame (0,0 = top-left).
int countBrightPixels (const juce::File& file, double seconds, juce::Rectangle<float> region);
} // namespace roadie::test
