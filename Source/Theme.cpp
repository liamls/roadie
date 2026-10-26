#include "Theme.h"

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
        background    = juce::Colour (0xfff3eee5);
        surface       = juce::Colour (0xfffbf8f2);
        surfaceRaised = juce::Colour (0xfffffdf9);
        surfaceSunken = juce::Colour (0xffe9e2d6);
        border        = juce::Colour (0xffdcd3c4);
        text          = juce::Colour (0xff1f1c17);
        textMuted     = juce::Colour (0xff6e685e);
        textFaint     = juce::Colour (0xff9c9588);
    }
    else
    {
        background    = juce::Colour (0xff16171b);
        surface       = juce::Colour (0xff1e2026);
        surfaceRaised = juce::Colour (0xff272a32);
        surfaceSunken = juce::Colour (0xff121318);
        border        = juce::Colour (0xff2e313a);
        text          = juce::Colour (0xfff5f3ee);
        textMuted     = juce::Colour (0xff9a978f);
        textFaint     = juce::Colour (0xff6b6962);
    }
}

Appearance getAppearance()
{
    return current;
}

juce::Colour accentFor (Module module)
{
    switch (module)
    {
        case Module::amp:       return ampAccent;
        case Module::loop:      return loopAccent;
        case Module::camera:    return camAccent;
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
    return juce::Font (juce::FontOptions ("Avenir Next", style, 1.0f).withPointHeight (size));
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
juce::Font semibold (float size) { return make ("Demi Bold", size); }
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
