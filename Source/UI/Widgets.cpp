#include "Widgets.h"

namespace roadie::ui
{
namespace
{
void noFocus (juce::Component& c)
{
    c.setWantsKeyboardFocus (false);
    c.setMouseClickGrabsKeyboardFocus (false);
}
} // namespace

//==============================================================================
IconButton::IconButton (const juce::String& name, icons::Icon i, Style s)
    : juce::Button (name), icon (i), style (s)
{
    noFocus (*this);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat();
    const bool lit = on || getToggleState();

    juce::Colour iconColour = textMuted;
    juce::Colour fill = juce::Colours::transparentBlack;

    if (style == Style::record)
    {
        iconColour = rec;
        if (isOver || lit)
            fill = rec.withAlpha (lit ? 0.22f : 0.12f);
    }
    else
    {
        if (lit)
        {
            iconColour = accent.value_or (theme::text);
            fill = accent.has_value() ? accent->withAlpha (0.16f) : theme::text.withAlpha (0.08f);
        }
        else if (isOver)
        {
            iconColour = theme::text;
            fill = theme::text.withAlpha (0.04f);
        }
    }

    if (isDown)
        fill = fill.isTransparent() ? theme::text.withAlpha (0.08f) : fill.brighter (0.1f);

    if (blinking && (juce::Time::getMillisecondCounter() / 700) % 2 == 1)
        iconColour = iconColour.withMultipliedAlpha (0.35f);

    if (! isEnabled())
        iconColour = iconColour.withMultipliedAlpha (0.35f);

    if (style == Style::square)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 6.0f);
        icons::draw (g, icon, bounds.reduced (bounds.getHeight() * 0.25f), iconColour);
    }
    else
    {
        const float d = std::min (bounds.getWidth(), bounds.getHeight());
        const auto circle = bounds.withSizeKeepingCentre (d, d);
        g.setColour (fill);
        g.fillEllipse (circle);
        icons::draw (g, icon, circle.reduced (d * 0.28f), iconColour);
    }
}

//==============================================================================
PillButton::PillButton (const juce::String& text, Variant v, juce::Colour a, std::optional<icons::Icon> i)
    : juce::Button (text), variant (v), accent (a), icon (i)
{
    noFocus (*this);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int PillButton::getIdealWidth() const
{
    const auto font = fonts::semibold (fontSize);
    const float textWidth = juce::GlyphArrangement::getStringWidth (font, getButtonText());
    return (int) std::ceil (textWidth + (icon.has_value() ? fontSize + 7.0f : 0.0f) + 28.0f);
}

void PillButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = fullyRounded ? bounds.getHeight() * 0.5f : radiusMd;
    const bool lit = getToggleState();

    juce::Colour fill, outline, label, iconColour;
    switch (variant)
    {
        case Variant::solid:
            fill = accent;
            outline = accent;
            label = juce::Colour (0xff16171b);
            iconColour = label;
            break;
        case Variant::tinted:
            fill = accent.withAlpha (lit ? 0.24f : 0.12f);
            outline = accent;
            label = theme::text;
            iconColour = accent;
            break;
        case Variant::neutral:
        default:
            fill = lit ? accent.withAlpha (0.14f) : surfaceRaised;
            outline = lit ? accent : border;
            label = theme::text;
            iconColour = lit ? accent : theme::text;
            break;
    }

    if (isDown)
        fill = fill.darker (0.15f);
    else if (isOver)
        fill = variant == Variant::solid ? fill.brighter (0.08f) : fill.overlaidWith (theme::text.withAlpha (0.04f));

    const float alpha = isEnabled() ? 1.0f : 0.35f;
    g.setColour (fill.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (outline.withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    const auto font = fonts::semibold (fontSize);
    g.setFont (font);
    const float textWidth = juce::GlyphArrangement::getStringWidth (font, getButtonText());
    const float iconSize = icon.has_value() ? fontSize + 1.0f : 0.0f;
    const float gap = icon.has_value() ? 7.0f : 0.0f;
    float x = bounds.getCentreX() - (textWidth + iconSize + gap) * 0.5f;

    if (icon.has_value())
    {
        icons::draw (g, *icon, juce::Rectangle<float> (x, bounds.getCentreY() - iconSize * 0.5f, iconSize, iconSize),
                     iconColour.withMultipliedAlpha (alpha));
        x += iconSize + gap;
    }

    g.setColour (label.withMultipliedAlpha (alpha));
    g.drawText (getButtonText(), juce::Rectangle<float> (x, bounds.getY(), textWidth + 2.0f, bounds.getHeight()),
                juce::Justification::centredLeft, false);
}

//==============================================================================
Knob::Knob (const juce::String& c, juce::Colour accent) : caption (c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    slider.setMouseDragSensitivity (180);
    slider.setVelocityBasedMode (false);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.onValueChange = [this]
    {
        repaint();
        if (onChange)
            onChange ((float) slider.getValue());
    };
    slider.onDragStart = [this] { repaint(); };
    slider.onDragEnd = [this] { repaint(); };
    noFocus (slider);
    slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible (slider);
}

void Knob::setRange (juce::Range<float> range, float defaultValue)
{
    slider.setRange (range.getStart(), range.getEnd(), 0.1);
    slider.setDoubleClickReturnValue (true, defaultValue);
}

void Knob::setValue (float value)
{
    slider.setValue (value, juce::dontSendNotification);
    repaint();
}

void Knob::resized()
{
    auto area = getLocalBounds();
    const int size = std::min (area.getWidth(), area.getHeight() - 18);
    slider.setBounds (area.removeFromTop (size).withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const float size = std::min (area.getWidth(), area.getHeight() - 18.0f);
    area.removeFromTop (size + 4.0f);

    const bool showValue = slider.isMouseOverOrDragging();
    g.setColour (showValue ? theme::text : theme::textMuted);
    g.setFont (fonts::semibold (11.0f));

    if (showValue)
    {
        const auto v = slider.getValue();
        const auto text = (v > 0.05 ? "+" : "") + juce::String (v, 1) + unit;
        fonts::drawTabular (g, text, area.withHeight (14.0f), juce::Justification::centred);
    }
    else
    {
        g.drawText (caption, area.withHeight (14.0f), juce::Justification::centred, false);
    }
}

//==============================================================================
void Meter::setLevel (float linear)
{
    // Fast attack, ~300 ms release, so peaks stay readable at 15–30 fps.
    displayed = linear > displayed ? linear : displayed * 0.82f + linear * 0.18f;
    if (std::abs (displayed - level) > 0.002f)
    {
        level = displayed;
        repaint();
    }
}

void Meter::paint (juce::Graphics& g)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float r = bounds.getHeight() * 0.5f;
    g.setColour (surfaceSunken);
    g.fillRoundedRectangle (bounds, r);
    g.setColour (border);
    g.drawRoundedRectangle (bounds, r, 1.0f);

    const float db = 20.0f * std::log10 (std::max (level, 1.0e-6f));
    const float proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    if (proportion <= 0.0f)
        return;

    auto fill = bounds.reduced (1.0f);
    juce::ColourGradient gradient (ok, fill.getX(), 0.0f, warn, fill.getRight(), 0.0f, false);
    gradient.addColour (0.92, warn);
    gradient.addColour (1.0, rec);
    g.setGradientFill (gradient);
    g.saveState();
    g.reduceClipRegion (fill.withWidth (fill.getWidth() * proportion).toNearestInt());
    g.fillRoundedRectangle (fill, fill.getHeight() * 0.5f);
    g.restoreState();
}

//==============================================================================
void StatPill::setContent (const juce::String& newLabel, const juce::String& newValue, juce::Colour newDot)
{
    if (newLabel == label && newValue == value && newDot == dot)
        return;
    label = newLabel;
    value = newValue;
    dot = newDot;
    repaint();
}

int StatPill::getIdealWidth() const
{
    return (int) std::ceil (10.0f + 6.0f + 6.0f
                            + juce::GlyphArrangement::getStringWidth (fonts::medium (12.0f), label + " ")
                            + fonts::getTabularWidth (fonts::semibold (12.0f), value) + 12.0f);
}

void StatPill::paint (juce::Graphics& g)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (surfaceSunken);
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    g.setColour (border);
    g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.5f, 1.0f);

    auto inner = bounds.reduced (10.0f, 0.0f);
    g.setColour (dot);
    g.fillEllipse (inner.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
    inner.removeFromLeft (6.0f);

    const auto labelFont = fonts::medium (12.0f);
    g.setFont (labelFont);
    g.setColour (textMuted);
    const float labelWidth = juce::GlyphArrangement::getStringWidth (labelFont, label + " ");
    g.drawText (label, inner.removeFromLeft (labelWidth), juce::Justification::centredLeft, false);

    g.setFont (fonts::semibold (12.0f));
    g.setColour (text);
    fonts::drawTabular (g, value, inner, juce::Justification::centredLeft);
}

//==============================================================================
MuteSoloButton::MuteSoloButton (bool isSolo, juce::Colour a)
    : juce::Button (isSolo ? "S" : "M"), solo (isSolo), accent (a)
{
    setClickingTogglesState (true);
    noFocus (*this);
    setTooltip (isSolo ? juce::String::fromUTF8 ("Solo : n'\xc3\xa9" "couter que cette piste")
                       : juce::String::fromUTF8 ("Muet : couper cette piste au monitoring"));
}

void MuteSoloButton::paintButton (juce::Graphics& g, bool isOver, bool)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const bool lit = getToggleState();
    const auto litColour = solo ? accent : warn;

    g.setColour (lit ? litColour.withAlpha (0.22f) : surfaceSunken);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (lit ? litColour : (isOver ? textMuted : border));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    g.setColour (lit ? litColour : textFaint);
    g.setFont (fonts::display (8.5f));
    g.drawText (getButtonText(), bounds, juce::Justification::centred, false);
}

//==============================================================================
void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification justification)
{
    g.setColour (theme::textMuted);
    g.setFont (fonts::display (11.0f).withExtraKerningFactor (0.04f));
    g.drawText (toUpperFrench (text), area, justification, false);
}

juce::String toUpperFrench (const juce::String& text)
{
    // Not juce::String::toUpperCase(): on macOS it maps "è" to "Î" (sic).
    // Labels only need ASCII plus the French accented letters.
    juce::String upper;
    upper.preallocateBytes (text.getNumBytesAsUTF8() + 4);
    for (auto c : text)
    {
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';
        else if ((c >= 0xe0 && c <= 0xfe && c != 0xf7))
            c -= 0x20; // Latin-1 lower → upper: à→À, é→É, è→È, ç→Ç, ô→Ô…
        upper += juce::String::charToString (c);
    }
    return upper;
}

juce::String truncateMiddle (const juce::String& text, const juce::Font& font, float maxWidth)
{
    if (juce::GlyphArrangement::getStringWidth (font, text) <= maxWidth)
        return text;

    // Keep both ends: capture names often share a long prefix and differ at the end.
    const auto ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");
    int keep = text.length() - 1;
    while (keep > 2)
    {
        const int head = (keep + 1) / 2, tail = keep / 2;
        const auto candidate = text.substring (0, head).trimEnd() + ellipsis + text.substring (text.length() - tail).trimStart();
        if (juce::GlyphArrangement::getStringWidth (font, candidate) <= maxWidth)
            return candidate;
        --keep;
    }
    return ellipsis;
}
} // namespace roadie::ui
