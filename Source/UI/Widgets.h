#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "../Theme.h"
#include "Icons.h"

// Small reusable pieces of the mockup. None of them grab keyboard focus:
// the main window keeps it so the looper shortcuts work at all times.
namespace roadie::ui
{
// Round icon button: the transport cluster, toolbar actions.
class IconButton : public juce::Button
{
public:
    enum class Style
    {
        transport, // 32 px circle, muted icon, hover tint (.t-btn)
        record,    // same, red icon
        square     // small rounded square, for toolbars
    };

    IconButton (const juce::String& name, icons::Icon icon, Style style = Style::transport);

    void setIcon (icons::Icon newIcon) { icon = newIcon; repaint(); }
    // "is-on": a persistent lit state (playing, metronome on…).
    void setOn (bool shouldBeOn) { on = shouldBeOn; repaint(); }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setBlinking (bool shouldBlink) { blinking = shouldBlink; repaint(); }

    void paintButton (juce::Graphics&, bool isOver, bool isDown) override;

private:
    icons::Icon icon;
    Style style;
    bool on = false, blinking = false;
    std::optional<juce::Colour> accent;
};

// Text button in the mockup's variants.
class PillButton : public juce::Button
{
public:
    enum class Variant
    {
        neutral, // raised surface + border (.loop-actions button)
        tinted,  // accent outline + 12 % accent fill (.export-btn, .primary, .calibrate-btn)
        solid    // accent fill, dark text (.export-cta)
    };

    PillButton (const juce::String& text, Variant variant = Variant::neutral,
                juce::Colour accent = theme::ampAccent, std::optional<icons::Icon> icon = std::nullopt);

    void setVariant (Variant v) { variant = v; repaint(); }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setIcon (std::optional<icons::Icon> i) { icon = i; repaint(); }
    void setFullyRounded (bool shouldBe) { fullyRounded = shouldBe; repaint(); }
    void setFontSize (float size) { fontSize = size; repaint(); }

    int getIdealWidth() const;

    void paintButton (juce::Graphics&, bool isOver, bool isDown) override;

private:
    Variant variant;
    juce::Colour accent;
    std::optional<icons::Icon> icon;
    bool fullyRounded = false;
    float fontSize = 13.0f;
};

// Ringed knob with its caption; shows the value while hovered or dragged.
class Knob : public juce::Component
{
public:
    Knob (const juce::String& caption, juce::Colour accent);

    void setRange (juce::Range<float> range, float defaultValue);
    void setValue (float value);
    float getValue() const { return (float) slider.getValue(); }
    void setUnit (const juce::String& unitSuffix) { unit = unitSuffix; }
    void setTooltip (const juce::String& tip) { slider.setTooltip (tip); }

    std::function<void (float)> onChange;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class KnobSlider : public juce::Slider
    {
    public:
        using juce::Slider::Slider;
        void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); if (auto* p = getParentComponent()) p->repaint(); }
        void mouseExit (const juce::MouseEvent& e) override { juce::Slider::mouseExit (e); if (auto* p = getParentComponent()) p->repaint(); }
    };

    KnobSlider slider;
    juce::String caption, unit = " dB";
};

// Horizontal level meter: ok → warn gradient, red near clipping.
class Meter : public juce::Component,
              public juce::SettableTooltipClient
{
public:
    void setLevel (float linear);
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f, displayed = 0.0f;
};

// Rounded status pill: dot + label + strong value (CPU 23%, Latence 7.8 ms).
class StatPill : public juce::Component
{
public:
    void setContent (const juce::String& label, const juce::String& value, juce::Colour dot);
    int getIdealWidth() const;
    void paint (juce::Graphics&) override;

private:
    juce::String label, value;
    juce::Colour dot = theme::ok;
};

// Tiny stacked Mute / Solo toggle of a track header.
class MuteSoloButton : public juce::Button
{
public:
    MuteSoloButton (bool isSolo, juce::Colour accent);
    void paintButton (juce::Graphics&, bool isOver, bool isDown) override;

private:
    bool solo;
    juce::Colour accent;
};

juce::String toUpperFrench (const juce::String& text);
juce::String truncateMiddle (const juce::String& text, const juce::Font& font, float maxWidth);

// Uppercase section label (.label in the mockup).
void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       juce::Justification justification = juce::Justification::centredLeft);
} // namespace roadie::ui
