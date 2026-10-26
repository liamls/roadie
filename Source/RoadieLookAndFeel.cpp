#include "RoadieLookAndFeel.h"
#include "Theme.h"

RoadieLookAndFeel::RoadieLookAndFeel()
{
    using namespace roadie::theme;

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::backgroundColourId, background);

    setColour (juce::Label::textColourId, text);

    setColour (juce::TextButton::buttonColourId, surfaceRaised);
    setColour (juce::TextButton::buttonOnColourId, ampAccent.withAlpha (0.18f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);

    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, ampAccent);
    setColour (juce::ToggleButton::tickDisabledColourId, textFaint);

    setColour (juce::Slider::rotarySliderFillColourId, ampAccent);
    setColour (juce::Slider::rotarySliderOutlineColourId, surfaceRaised);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, surfaceSunken);
    setColour (juce::Slider::textBoxOutlineColourId, border);

    setColour (juce::ComboBox::backgroundColourId, surfaceSunken);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, border);
    setColour (juce::ComboBox::arrowColourId, textMuted);

    setColour (juce::ListBox::backgroundColourId, surface);
    setColour (juce::ListBox::textColourId, text);

    setColour (juce::ScrollBar::thumbColourId, surfaceRaised);

    setColour (juce::TextEditor::backgroundColourId, surfaceSunken);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::TextEditor::focusedOutlineColourId, ampAccent);

    setColour (juce::AlertWindow::backgroundColourId, surface);
    setColour (juce::AlertWindow::textColourId, text);

    setColour (juce::PopupMenu::backgroundColourId, surfaceRaised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ampAccent.withAlpha (0.2f));
}

void RoadieLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                           juce::Slider& slider)
{
    using namespace roadie::theme;

    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    auto centre = bounds.getCentre();
    auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    constexpr float trackThickness = 4.0f;
    auto trackRadius = radius - trackThickness * 0.5f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (surfaceRaised);
    g.strokePath (track, juce::PathStrokeType (trackThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour (accent);
        g.strokePath (valueArc, juce::PathStrokeType (trackThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto faceRadius = radius - 9.0f;
    g.setColour (surfaceSunken);
    g.fillEllipse (centre.x - faceRadius, centre.y - faceRadius, faceRadius * 2.0f, faceRadius * 2.0f);

    juce::Path pointer;
    pointer.startNewSubPath (centre.x, centre.y - faceRadius + 5.0f);
    pointer.lineTo (centre.x, centre.y - faceRadius + 5.0f + (faceRadius - 12.0f));
    pointer.applyTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));
    g.setColour (text);
    g.strokePath (pointer, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void RoadieLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

    auto baseColour = backgroundColour;
    if (shouldDrawButtonAsDown)
        baseColour = baseColour.darker (0.2f);
    else if (shouldDrawButtonAsHighlighted)
        baseColour = baseColour.brighter (0.08f);

    if (! button.isEnabled())
        baseColour = baseColour.withMultipliedAlpha (0.45f);

    g.setColour (baseColour);
    g.fillRoundedRectangle (bounds, 8.0f);

    if (button.getToggleState())
    {
        g.setColour (roadie::theme::ampAccent);
        g.drawRoundedRectangle (bounds, 8.0f, 1.3f);
    }
}
