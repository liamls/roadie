#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace roadie::theme
{
namespace
{
Appearance current = Appearance::dark;
} // namespace

void setAppearance (Appearance appearance)
{
    current = appearance;

    if (appearance == Appearance::light)
    {
        // Warm cream rather than office white: still "backstage", lights up.
        background    = juce::Colour (0xfff1ece4);
        surface       = juce::Colour (0xfff9f6f1);
        surfaceRaised = juce::Colour (0xfffefcf9);
        surfaceSunken = juce::Colour (0xffe5dfd6);
        border        = juce::Colour (0xffd8d2c7);
        text          = juce::Colour (0xff221d18);
        textMuted     = juce::Colour (0xff69625b);
        textFaint     = juce::Colour (0xff857f77);
    }
    else
    {
        // Truly warm charcoal, with surface steps wide enough to tell the
        // panels apart without borders.
        background    = juce::Colour (0xff181613);
        surface       = juce::Colour (0xff23201d);
        surfaceRaised = juce::Colour (0xff2f2c28);
        surfaceSunken = juce::Colour (0xff100e0c);
        border        = juce::Colour (0xff3d3a36);
        text          = juce::Colour (0xfff6f3ed);
        textMuted     = juce::Colour (0xffb0aca5);
        textFaint     = juce::Colour (0xff87827d);
    }
}

Appearance getAppearance()
{
    return current;
}

juce::Path squircle (juce::Rectangle<float> area, float radius)
{
    juce::Path path;
    const float r = std::min ({ radius, area.getWidth() * 0.5f, area.getHeight() * 0.5f });
    if (r < 1.0f)
    {
        path.addRectangle (area);
        return path;
    }

    // |x|^4 + |y|^4 = r^4 on each corner: x = r·sgn(cos)·|cos|^(2/4).
    constexpr int steps = 14;
    constexpr float exponent = 0.5f;
    const juce::Point<float> centres[4] = { { area.getX() + r, area.getY() + r },
                                            { area.getRight() - r, area.getY() + r },
                                            { area.getRight() - r, area.getBottom() - r },
                                            { area.getX() + r, area.getBottom() - r } };
    const float starts[4] = { 180.0f, 270.0f, 0.0f, 90.0f };
    for (int corner = 0; corner < 4; ++corner)
        for (int i = 0; i <= steps; ++i)
        {
            const float theta = juce::degreesToRadians (starts[corner] + 90.0f * (float) i / (float) steps);
            const float c = std::cos (theta), s = std::sin (theta);
            const float x = centres[corner].x + r * (c < 0.0f ? -1.0f : 1.0f) * std::pow (std::abs (c), exponent);
            const float y = centres[corner].y + r * (s < 0.0f ? -1.0f : 1.0f) * std::pow (std::abs (s), exponent);
            if (corner == 0 && i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
    path.closeSubPath();
    return path;
}

void fillSquircle (juce::Graphics& g, juce::Rectangle<float> area, float radius)
{
    g.fillPath (squircle (area, radius));
}

void drawSquircle (juce::Graphics& g, juce::Rectangle<float> area, float radius, float thickness)
{
    g.strokePath (squircle (area, radius), juce::PathStrokeType (thickness));
}

juce::Colour ink (juce::Colour accent)
{
    if (current != Appearance::light)
        return accent;

    struct Ink
    {
        juce::Colour accent, ink;
    };
    static const Ink inks[] = {
        { ampAccent, juce::Colour (0xff1f7a34) },
        { loopAccent, juce::Colour (0xff9a5200) },
        { camAccent, juce::Colour (0xff7a4fc0) },
        { songAccent, juce::Colour (0xff1f6aae) },
        { exportAccent, juce::Colour (0xff896600) },
        { ok, juce::Colour (0xff1c7a3a) },
        { warn, juce::Colour (0xff945f00) },
        { rec, juce::Colour (0xffcc2827) },
    };
    for (const auto& entry : inks)
        if (entry.accent == accent)
            return entry.ink;
    return accent;
}

juce::Colour accentFor (Module module)
{
    switch (module)
    {
        case Module::amp:       return ampAccent;
        case Module::loop:      return loopAccent;
        case Module::camera:    return camAccent;
        case Module::song:      return songAccent;
        case Module::exporting: return exportAccent;
        case Module::guitar:
        default:                return textMuted;
    }
}
} // namespace roadie::theme

namespace roadie::fonts
{
namespace
{
juce::Font make (const char* style, float size)
{
   #if JUCE_MAC
    // San Francisco, the system font (the one Logic Pro uses).
    return juce::Font (juce::FontOptions (".AppleSystemUIFont", style, 1.0f).withPointHeight (size));
   #else
    const bool bold = juce::String (style).containsIgnoreCase ("bold");
    return juce::Font (juce::FontOptions (size * 1.25f, bold ? juce::Font::bold : juce::Font::plain));
   #endif
}

float digitWidth (const juce::Font& font)
{
    float widest = 0.0f;
    for (juce::juce_wchar c = '0'; c <= '9'; ++c)
        widest = std::max (widest, juce::GlyphArrangement::getStringWidth (font, juce::String::charToString (c)));
    return widest;
}
} // namespace

juce::Font body (float size) { return make ("Regular", size); }
juce::Font medium (float size) { return make ("Medium", size); }
juce::Font semibold (float size) { return make ("Semibold", size); }
juce::Font display (float size) { return make ("Bold", size); }

float getTabularWidth (const juce::Font& font, const juce::String& text)
{
    const float slot = digitWidth (font);
    float width = 0.0f;
    for (auto c : text)
        width += juce::CharacterFunctions::isDigit (c) ? slot
                                                        : juce::GlyphArrangement::getStringWidth (font, juce::String::charToString (c));
    return width;
}

void drawTabular (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification justification)
{
    const auto font = g.getCurrentFont();
    const float slot = digitWidth (font);
    const float total = getTabularWidth (font, text);

    float x = area.getX();
    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - total * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = area.getRight() - total;

    for (auto c : text)
    {
        const auto glyph = juce::String::charToString (c);
        const float natural = juce::GlyphArrangement::getStringWidth (font, glyph);
        const float cell = juce::CharacterFunctions::isDigit (c) ? slot : natural;
        g.drawText (glyph, juce::Rectangle<float> (x, area.getY(), cell, area.getHeight()), juce::Justification::centred, false);
        x += cell;
    }
}
} // namespace roadie::fonts
