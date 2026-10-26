#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Custom LookAndFeel implementing the "backstage chaleureux" direction:
// warm near-black ground, orange accent, rounded cards, ringed knobs.
class RoadieLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RoadieLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};
