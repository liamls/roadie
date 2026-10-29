#include "RoadieLookAndFeel.h"
#include "Theme.h"

RoadieLookAndFeel::RoadieLookAndFeel()
{
   #if JUCE_MAC
    setDefaultSansSerifTypefaceName (".AppleSystemUIFont");
   #endif
    applyTheme();
}

void RoadieLookAndFeel::applyTheme()
{
    using namespace roadie::theme;

    // Stock V4 widgets (the audio settings dialog) read the colour scheme;
    // set it first, since applying a scheme resets every colour below.
    auto scheme = getAppearance() == Appearance::light ? juce::LookAndFeel_V4::getLightColourScheme()
                                                       : juce::LookAndFeel_V4::getDarkColourScheme();
    scheme.setUIColour (ColourScheme::windowBackground, surface);
    scheme.setUIColour (ColourScheme::widgetBackground, surfaceSunken);
    scheme.setUIColour (ColourScheme::menuBackground, surfaceRaised);
    scheme.setUIColour (ColourScheme::outline, border);
    scheme.setUIColour (ColourScheme::defaultText, text);
    scheme.setUIColour (ColourScheme::defaultFill, ampAccent);
    scheme.setUIColour (ColourScheme::highlightedText, text);
    scheme.setUIColour (ColourScheme::highlightedFill, surfaceRaised);
    scheme.setUIColour (ColourScheme::menuText, text);
    setColourScheme (scheme);

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::backgroundColourId, background);

    setColour (juce::Label::textColourId, text);

    setColour (juce::TextButton::buttonColourId, surfaceRaised);
    setColour (juce::TextButton::buttonOnColourId, ampAccent.withAlpha (0.18f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);

    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, text);
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
    setColour (juce::ComboBox::focusedOutlineColourId, text);

    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ListBox::textColourId, text);

    setColour (juce::ScrollBar::thumbColourId, surfaceRaised);

    setColour (juce::TextEditor::backgroundColourId, surfaceSunken);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::TextEditor::focusedOutlineColourId, textMuted);
    setColour (juce::TextEditor::highlightColourId, camAccent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, text);

    setColour (juce::AlertWindow::backgroundColourId, surface);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, border);

    setColour (juce::PopupMenu::backgroundColourId, surfaceRaised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textMuted);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, text.withAlpha (0.08f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);

    setColour (juce::TooltipWindow::backgroundColourId, surfaceRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, border);

    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, surfaceRaised);
    setColour (juce::DirectoryContentsDisplayComponent::textColourId, text);

}

void RoadieLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                          juce::Slider& slider)
{
    using namespace roadie::theme;

    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float diameter = std::min (bounds.getWidth(), bounds.getHeight());
    bounds = bounds.withSizeKeepingCentre (diameter, diameter);
    const auto centre = bounds.getCentre();
    const float radius = diameter * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    if (! slider.isEnabled())
        accent = accent.withMultipliedSaturation (0.2f).withMultipliedAlpha (0.5f);

    // Conic ring (mockup: 52 px knob, 38 px face → 7 px ring).
    const float ringThickness = std::max (4.0f, diameter * 0.135f);
    const float ringRadius = radius - ringThickness * 0.5f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (surfaceRaised);
    g.strokePath (track, juce::PathStrokeType (ringThickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    if (sliderPos > 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour (accent);
        g.strokePath (valueArc, juce::PathStrokeType (ringThickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // Sunken face with a soft inner shadow.
    const float faceRadius = radius - ringThickness;
    const auto face = juce::Rectangle<float> (faceRadius * 2.0f, faceRadius * 2.0f).withCentre (centre);
    g.setColour (surfaceSunken);
    g.fillEllipse (face);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), centre.x, face.getY(),
                                             juce::Colours::transparentBlack, centre.x, face.getY() + faceRadius * 0.6f, false));
    g.drawEllipse (face.reduced (0.5f), 1.2f);

    // Pointer: 2 px × ~1/3 of the face, from the rim inwards.
    juce::Path pointer;
    const float pointerLength = faceRadius * 0.62f;
    pointer.addRoundedRectangle (-1.0f, -faceRadius + 3.0f, 2.0f, pointerLength, 1.0f);
    g.setColour (slider.isEnabled() ? text : textFaint);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void RoadieLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::TwoValueHorizontal && style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    using namespace roadie::theme;
    const auto accent = slider.findColour (juce::Slider::trackColourId);
    const float centreY = (float) y + (float) height * 0.5f;
    const auto track = juce::Rectangle<float> ((float) x, centreY - 3.0f, (float) width, 6.0f);

    g.setColour (surfaceSunken);
    roadie::theme::fillSquircle (g, track, 3.0f);
    g.setColour (border);
    roadie::theme::drawSquircle (g, track, 3.0f, 1.0f);

    const bool twoValue = style == juce::Slider::TwoValueHorizontal;
    const float from = twoValue ? minSliderPos : (float) x;
    const float to = twoValue ? maxSliderPos : sliderPos;
    g.setColour (slider.isEnabled() ? accent : textFaint);
    roadie::theme::fillSquircle (g, juce::Rectangle<float> (from, centreY - 3.0f, std::max (0.0f, to - from), 6.0f), 3.0f);

    // Round handles with a ring of the panel colour, like the knob faces.
    auto drawThumb = [&] (float cx)
    {
        const auto thumb = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ cx, centreY });
        g.setColour (slider.isEnabled() ? text : textFaint);
        g.fillEllipse (thumb);
        g.setColour (slider.isEnabled() ? accent : border);
        g.drawEllipse (thumb.reduced (1.0f), 2.0f);
    };

    if (twoValue)
    {
        drawThumb (minSliderPos);
        drawThumb (maxSliderPos);
    }
    else
    {
        drawThumb (sliderPos);
    }
}

int RoadieLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    return slider.isHorizontal() ? 8 : LookAndFeel_V4::getSliderThumbRadius (slider);
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
    roadie::theme::fillSquircle (g, bounds, roadie::theme::radiusSm);
    g.setColour (roadie::theme::border);
    roadie::theme::drawSquircle (g, bounds, roadie::theme::radiusSm, 1.0f);
}

juce::Font RoadieLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return roadie::fonts::semibold (std::min (13.0f, (float) buttonHeight * 0.5f));
}

juce::Font RoadieLookAndFeel::getComboBoxFont (juce::ComboBox&) { return roadie::fonts::medium (13.0f); }
juce::Font RoadieLookAndFeel::getPopupMenuFont() { return roadie::fonts::medium (13.5f); }
juce::Font RoadieLookAndFeel::getLabelFont (juce::Label& label) { return label.getFont(); }
juce::Font RoadieLookAndFeel::getAlertWindowTitleFont() { return roadie::fonts::display (16.0f); }
juce::Font RoadieLookAndFeel::getAlertWindowMessageFont() { return roadie::fonts::body (13.5f); }
juce::Font RoadieLookAndFeel::getAlertWindowFont() { return roadie::fonts::body (13.0f); }

void RoadieLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    using namespace roadie::theme;
    const auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setColour (surfaceSunken);
    roadie::theme::fillSquircle (g, bounds, radiusSm);
    g.setColour (box.hasKeyboardFocus (true) ? textMuted : border);
    roadie::theme::drawSquircle (g, bounds, radiusSm, 1.0f);

    juce::Path arrow;
    const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (textMuted.withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void RoadieLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void RoadieLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    roadie::theme::fillSquircle (g, juce::Rectangle<float> (0, 0, (float) width, (float) height), roadie::theme::radiusSm);
}

void RoadieLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto colour = editor.hasKeyboardFocus (true) ? editor.findColour (juce::TextEditor::focusedOutlineColourId)
                                                       : editor.findColour (juce::TextEditor::outlineColourId);
    g.setColour (colour);
    roadie::theme::drawSquircle (g, juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f), roadie::theme::radiusSm, 1.0f);
}

void RoadieLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component, float x, float y, float w, float h,
                                     bool ticked, bool isEnabled, bool shouldDrawButtonAsHighlighted, bool)
{
    using namespace roadie::theme;
    const auto box = juce::Rectangle<float> (x, y, w, h).reduced (1.0f);
    const auto accent = component.findColour (juce::ToggleButton::tickColourId);

    g.setColour (ticked ? accent.withAlpha (0.18f) : surfaceSunken);
    roadie::theme::fillSquircle (g, box, 4.0f);
    g.setColour (ticked ? accent : (shouldDrawButtonAsHighlighted ? textMuted : border));
    roadie::theme::drawSquircle (g, box, 4.0f, 1.2f);

    if (ticked)
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + box.getWidth() * 0.22f, box.getCentreY());
        tick.lineTo (box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.25f);
        tick.lineTo (box.getRight() - box.getWidth() * 0.2f, box.getY() + box.getHeight() * 0.25f);
        g.setColour (isEnabled ? ink (accent) : textFaint);
        g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void RoadieLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    using namespace roadie::theme;
    const auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height);
    g.setColour (surfaceRaised);
    roadie::theme::fillSquircle (g, bounds, 6.0f);
    g.setColour (border);
    roadie::theme::drawSquircle (g, bounds.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (roadie::theme::text);
    g.setFont (roadie::fonts::medium (12.0f));
    g.drawFittedText (text, bounds.reduced (10.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> RoadieLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto font = roadie::fonts::medium (12.0f);
    const int w = std::min (320, (int) juce::GlyphArrangement::getStringWidth (font, tipText) + 22);
    const int lines = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, tipText) / 298.0f);
    const int h = 10 + std::max (1, lines) * 17;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h)
        .constrainedWithin (parentArea);
}

void RoadieLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                                       int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    juce::Rectangle<float> thumb = isScrollbarVertical
                                       ? juce::Rectangle<float> ((float) x + 3.0f, (float) thumbStartPosition, (float) width - 6.0f, (float) thumbSize)
                                       : juce::Rectangle<float> ((float) thumbStartPosition, (float) y + 3.0f, (float) thumbSize, (float) height - 6.0f);
    g.setColour (roadie::theme::textFaint.withAlpha (isMouseDown ? 0.7f : (isMouseOver ? 0.5f : 0.3f)));
    roadie::theme::fillSquircle (g, thumb, std::min (thumb.getWidth(), thumb.getHeight()) * 0.5f);
}
