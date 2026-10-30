#include "Widgets.h"
#include "../I18n.h"

namespace roadie::ui
{
namespace
{
void noFocus (juce::Component& c)
{
    c.setWantsKeyboardFocus (false);
    c.setMouseClickGrabsKeyboardFocus (false);
}

double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

// Pressed controls sink a little (scaled down around their centre).
void pressIn (juce::Graphics& g, juce::Rectangle<float> bounds, bool isDown)
{
    if (isDown)
        g.addTransform (juce::AffineTransform::scale (0.95f, 0.95f, bounds.getCentreX(), bounds.getCentreY()));
}
} // namespace

//==============================================================================
// Repaints the components whose hover fade is still moving, at 60 Hz, and
// stops as soon as every fade has settled.
class HoverAnimator : public juce::DeletedAtShutdown,
                      private juce::Timer
{
public:
    ~HoverAnimator() override { clearSingletonInstance(); }

    void add (HoverFade* fade)
    {
        active.addIfNotAlreadyThere (fade);
        if (! isTimerRunning())
            startTimerHz (60);
    }

    void remove (HoverFade* fade)
    {
        active.removeAllInstancesOf (fade);
        if (active.isEmpty())
            stopTimer();
    }

    JUCE_DECLARE_SINGLETON_SINGLETHREADED_MINIMAL_INLINE (HoverAnimator)

private:
    void timerCallback() override
    {
        const double now = nowSeconds();
        for (int i = active.size(); --i >= 0;)
        {
            auto* fade = active.getUnchecked (i);
            // Not painted for a while (hidden, off screen): settle it silently.
            if (now - fade->lastTime > 0.3)
            {
                fade->progress = fade->target ? 1.0f : 0.0f;
                active.remove (i);
                continue;
            }
            fade->owner.repaint();
        }

        if (active.isEmpty())
            stopTimer();
    }

    juce::Array<HoverFade*> active;
};

HoverFade::~HoverFade()
{
    if (auto* animator = HoverAnimator::getInstanceWithoutCreating())
        animator->remove (this);
}

float HoverFade::get (bool hovered)
{
    const double now = nowSeconds();
    if (hovered != target)
    {
        target = hovered;
        lastTime = now - 1.0 / 60.0; // the first frame already moves
    }

    const float goal = target ? 1.0f : 0.0f;
    if (! juce::exactlyEqual (progress, goal))
    {
        const auto step = (float) ((now - lastTime) / durationSeconds);
        progress = target ? std::min (1.0f, progress + step) : std::max (0.0f, progress - step);

        if (juce::exactlyEqual (progress, goal))
            HoverAnimator::getInstance()->remove (this);
        else
            HoverAnimator::getInstance()->add (this);
    }
    lastTime = now;

    // Smoothstep: soft at both ends.
    return progress * progress * (3.0f - 2.0f * progress);
}

void HoverFade::reset (bool hovered)
{
    target = hovered;
    progress = hovered ? 1.0f : 0.0f;
    lastTime = nowSeconds();
    if (auto* animator = HoverAnimator::getInstanceWithoutCreating())
        animator->remove (this);
}

//==============================================================================
Splitter::Splitter (Orientation o) : orientation (o)
{
    noFocus (*this);
    setRepaintsOnMouseActivity (true);
    setMouseCursor (orientation == Orientation::vertical ? juce::MouseCursor::LeftRightResizeCursor
                                                         : juce::MouseCursor::UpDownResizeCursor);
    setTooltip (tr ("Drag to resize · double-click: default size"));
}

void Splitter::setCollapsed (bool shouldShowGrip)
{
    if (collapsed == shouldShowGrip)
        return;
    collapsed = shouldShowGrip;
    setTooltip (collapsed ? tr ("Drag to reopen the panel · double-click: default size")
                          : tr ("Drag to resize · double-click: default size"));
    repaint();
}

void Splitter::paint (juce::Graphics& g)
{
    // Only the hover line: the panels draw their own border underneath. A
    // folded panel leaves a grip, so there is always something to pull.
    const float amount = fade.get (dragging || isMouseOver());
    if (collapsed)
    {
        auto bounds = getLocalBounds().toFloat();
        const auto grip = orientation == Orientation::vertical ? bounds.withSizeKeepingCentre (4.0f, 36.0f)
                                                               : bounds.withSizeKeepingCentre (36.0f, 4.0f);
        g.setColour (theme::textFaint.interpolatedWith (theme::exportAccent, amount));
        roadie::theme::fillSquircle (g, grip, 2.0f);
    }
    if (amount <= 0.0f)
        return;

    auto bounds = getLocalBounds().toFloat();
    const float width = 1.0f + amount;
    const auto line = orientation == Orientation::vertical ? bounds.withSizeKeepingCentre (width, bounds.getHeight())
                                                           : bounds.withSizeKeepingCentre (bounds.getWidth(), width);
    g.setColour (theme::exportAccent.withAlpha (0.85f * amount));
    g.fillRect (line);
}

void Splitter::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    if (onDragStart)
        onDragStart();
    repaint();
}

void Splitter::mouseDrag (const juce::MouseEvent& e)
{
    if (onDrag)
        onDrag (orientation == Orientation::vertical ? e.getDistanceFromDragStartX() : e.getDistanceFromDragStartY());
}

void Splitter::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    if (onDragEnd)
        onDragEnd();
    repaint();
}

void Splitter::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick)
        onDoubleClick();
}

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
    const float hover = fade.get (isOver && isEnabled());
    pressIn (g, bounds, isDown);

    juce::Colour iconColour = textMuted;
    juce::Colour fill = juce::Colours::transparentBlack;

    if (style == Style::record)
    {
        iconColour = ink (rec);
        fill = rec.withAlpha (lit ? 0.3f : 0.16f + 0.1f * hover);
    }
    else
    {
        if (lit)
        {
            iconColour = accent.has_value() ? ink (*accent) : theme::text;
            fill = accent.has_value() ? accent->withAlpha (0.16f + 0.06f * hover) : theme::text.withAlpha (0.08f + 0.03f * hover);
        }
        else
        {
            iconColour = textMuted.interpolatedWith (theme::text, hover);
            fill = theme::text.withAlpha (0.06f * hover);
        }
    }

    if (isDown)
        fill = fill.getAlpha() < 20 ? theme::text.withAlpha (0.1f) : fill.brighter (0.1f);

    if (blinking && (juce::Time::getMillisecondCounter() / 700) % 2 == 1)
        iconColour = iconColour.withMultipliedAlpha (0.35f);

    if (! isEnabled())
        iconColour = iconColour.withMultipliedAlpha (0.35f);

    if (style == Style::square)
    {
        g.setColour (fill);
        roadie::theme::fillSquircle (g, bounds, 6.0f);
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
    const float hover = fade.get (isOver && isEnabled());
    pressIn (g, bounds, isDown);

    juce::Colour fill, outline, label, iconColour;
    switch (variant)
    {
        case Variant::solid:
            fill = accent;
            outline = accent;
            label = theme::onAccent;
            iconColour = label;
            break;
        case Variant::tinted:
            fill = accent.withAlpha (lit ? 0.24f : 0.12f);
            outline = accent;
            label = theme::text;
            iconColour = ink (accent);
            break;
        case Variant::neutral:
        default:
            fill = lit ? accent.withAlpha (0.14f) : surfaceRaised;
            outline = lit ? accent : border;
            label = theme::text;
            iconColour = lit ? ink (accent) : theme::text;
            break;
    }

    if (isDown)
        fill = fill.darker (0.15f);
    else if (hover > 0.0f)
        fill = variant == Variant::solid ? fill.brighter (0.08f * hover) : fill.overlaidWith (theme::text.withAlpha (0.05f * hover));
    if (variant == Variant::neutral && ! lit)
        outline = outline.interpolatedWith (textFaint, 0.6f * hover);

    const float alpha = isEnabled() ? 1.0f : 0.35f;
    g.setColour (fill.withMultipliedAlpha (alpha));
    roadie::theme::fillSquircle (g, bounds, radius);
    g.setColour (outline.withMultipliedAlpha (alpha));
    roadie::theme::drawSquircle (g, bounds, radius, 1.0f);

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
    // ⇧ while dragging: fine adjustment (incremental and slow).
    slider.setVelocityModeParameters (0.12, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
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
float Meter::toProportion (float linear)
{
    const float db = 20.0f * std::log10 (std::max (linear, 1.0e-6f));
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f);
}

void Meter::setLevel (float linear)
{
    target = std::max (target, linear); // the next frame takes the loudest block since the last one
    if (linear >= 1.0f && ! clipped)
    {
        clipped = true;
        setTooltip (tr ("Clipping — click to clear the indicator"));
        repaint();
    }
}

void Meter::resetClip()
{
    clipped = false;
    peak = displayed;
    repaint();
}

void Meter::tick()
{
    const double now = nowSeconds();
    const float dt = (float) juce::jlimit (0.0, 0.1, lastTick > 0.0 ? now - lastTick : 1.0 / 60.0);
    lastTick = now;

    // Instant attack, ~20 dB/s release (PPM-like), so peaks stay readable.
    const float before = displayed, peakBefore = peak;
    if (target >= displayed)
        displayed = target;
    else
        displayed = std::max (target, displayed * std::pow (10.0f, -20.0f * dt / 20.0f));
    target = 0.0f;

    if (displayed >= peak || now - peakTime > 1.5)
    {
        peak = displayed;
        peakTime = now;
    }

    if (std::abs (toProportion (displayed) - toProportion (before)) > 0.002f || ! juce::exactlyEqual (peak, peakBefore))
        repaint();
}

void Meter::paint (juce::Graphics& g)
{
    using namespace theme;
    const bool vertical = orientation == Orientation::vertical;
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    g.setColour (surfaceSunken);
    roadie::theme::fillSquircle (g, bounds, r);
    g.setColour (border);
    roadie::theme::drawSquircle (g, bounds, r, 1.0f);

    auto fill = bounds.reduced (1.0f);
    const float length = vertical ? fill.getHeight() : fill.getWidth();
    auto along = [&] (float proportion)
    {
        return vertical ? fill.withTop (fill.getBottom() - length * proportion) : fill.withWidth (length * proportion);
    };

    // Clip light at the far end (red until clicked).
    if (clipped)
    {
        g.setColour (rec);
        const float light = std::min (5.0f, length * 0.08f);
        roadie::theme::fillSquircle (g, vertical ? fill.withHeight (light) : fill.withLeft (fill.getRight() - light), r);
    }

    // 0 dBFS sits at 60/63 of the travel: the top 3 dB are the red zone.
    const float zero = 60.0f / 63.0f;
    juce::ColourGradient gradient = vertical ? juce::ColourGradient (ok, 0.0f, fill.getBottom(), rec, 0.0f, fill.getY(), false)
                                             : juce::ColourGradient (ok, fill.getX(), 0.0f, rec, fill.getRight(), 0.0f, false);
    gradient.addColour (zero * 0.8, ok.interpolatedWith (warn, 0.5f));
    gradient.addColour (zero - 0.04, warn);
    gradient.addColour (zero, rec);

    const float proportion = toProportion (displayed);
    if (proportion > 0.0f)
    {
        g.setGradientFill (gradient);
        g.saveState();
        g.reduceClipRegion (along (proportion).toNearestInt());
        roadie::theme::fillSquircle (g, fill, r);
        g.restoreState();
    }

    // Peak hold: a thin line where the last peak was.
    const float peakProportion = toProportion (peak);
    if (peakProportion > 0.01f)
    {
        g.setColour (peakProportion >= zero ? rec : text.withAlpha (0.8f));
        if (vertical)
            g.fillRect (fill.getX(), fill.getBottom() - length * peakProportion, fill.getWidth(), 1.5f);
        else
            g.fillRect (fill.getX() + length * peakProportion - 1.5f, fill.getY(), 1.5f, fill.getHeight());
    }
}

//==============================================================================
Fader::Fader (juce::Colour a, Orientation o) : accent (a), orientation (o)
{
    noFocus (*this);
    setMouseCursor (orientation == Orientation::vertical ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
    setRepaintsOnMouseActivity (true);
}

float Fader::toProportion (float db)
{
    // Piecewise console law: −∞ … −60 … −40 … −20 … −10 … 0 … +6.
    static constexpr float dbs[] = { -60.0f, -40.0f, -20.0f, -10.0f, 0.0f, 6.0f };
    static constexpr float pos[] = { 0.0f, 0.15f, 0.42f, 0.6f, 0.75f, 1.0f };
    if (db <= dbs[0])
        return 0.0f;
    for (int i = 1; i < 6; ++i)
        if (db <= dbs[i])
            return pos[i - 1] + (db - dbs[i - 1]) / (dbs[i] - dbs[i - 1]) * (pos[i] - pos[i - 1]);
    return 1.0f;
}

float Fader::fromProportion (float p)
{
    static constexpr float dbs[] = { -60.0f, -40.0f, -20.0f, -10.0f, 0.0f, 6.0f };
    static constexpr float pos[] = { 0.0f, 0.15f, 0.42f, 0.6f, 0.75f, 1.0f };
    p = juce::jlimit (0.0f, 1.0f, p);
    for (int i = 1; i < 6; ++i)
        if (p <= pos[i])
            return dbs[i - 1] + (p - pos[i - 1]) / (pos[i] - pos[i - 1]) * (dbs[i] - dbs[i - 1]);
    return maxDb;
}

juce::String Fader::format (float db)
{
    if (db <= minDb + 0.01f)
        return juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e dB");
    const auto rounded = std::round (db * 10.0f) / 10.0f;
    return (rounded > 0.0f ? "+" : (rounded < 0.0f ? juce::String::fromUTF8 ("\xe2\x88\x92") : juce::String()))
           + juce::String (std::abs (rounded), 1).replaceCharacter ('.', i18n::decimalPoint()) + " dB";
}

void Fader::setValue (float db)
{
    db = juce::jlimit (minDb, maxDb, db);
    if (juce::approximatelyEqual (db, value))
        return;
    value = db;
    setTooltip (tr ("Volume: ") + format (value) + tr (" · double-click or ⌥-click: 0 dB"));
    repaint();
}

void Fader::set (float db)
{
    // A small detent at unity.
    if (std::abs (db) < 0.4f)
        db = 0.0f;
    setValue (db);
    if (onChange)
        onChange (value);
}

juce::Rectangle<float> Fader::getTrack() const
{
    auto bounds = getLocalBounds().toFloat();
    return orientation == Orientation::vertical ? bounds.reduced (0.0f, 8.0f) : bounds.reduced (8.0f, 0.0f);
}

void Fader::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto track = getTrack();
    const bool vertical = orientation == Orientation::vertical;
    const float active = fade.get (isMouseOverOrDragging());
    const float p = toProportion (value);

    const float thickness = 4.0f;
    const auto groove = vertical ? track.withSizeKeepingCentre (thickness, track.getHeight())
                                 : track.withSizeKeepingCentre (track.getWidth(), thickness);
    g.setColour (border);
    roadie::theme::fillSquircle (g, groove, thickness * 0.5f);

    const auto filled = vertical ? groove.withTop (groove.getBottom() - groove.getHeight() * p) : groove.withWidth (groove.getWidth() * p);
    g.setColour (accent.withAlpha (0.65f + 0.3f * active));
    roadie::theme::fillSquircle (g, filled, thickness * 0.5f);

    // 0 dB tick.
    const float zero = toProportion (0.0f);
    g.setColour (textFaint);
    if (vertical)
        g.fillRect (juce::Rectangle<float> (groove.getRight() + 3.0f, track.getBottom() - track.getHeight() * zero - 0.5f, 5.0f, 1.0f));
    else
        g.fillRect (juce::Rectangle<float> (track.getX() + track.getWidth() * zero - 0.5f, groove.getBottom() + 3.0f, 1.0f, 4.0f));

    // Cap.
    const juce::Point<float> centre = vertical ? juce::Point<float> (track.getCentreX(), track.getBottom() - track.getHeight() * p)
                                               : juce::Point<float> (track.getX() + track.getWidth() * p, track.getCentreY());
    const auto cap = vertical ? juce::Rectangle<float> (18.0f + 2.0f * active, 10.0f).withCentre (centre)
                              : juce::Rectangle<float> (10.0f + 2.0f * active, 10.0f + 2.0f * active).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    roadie::theme::fillSquircle (g, cap.translated (0.0f, 1.0f), 3.0f);
    g.setColour (theme::text);
    roadie::theme::fillSquircle (g, cap, vertical ? 3.0f : cap.getHeight() * 0.5f);
    if (vertical)
    {
        g.setColour (surfaceSunken.withAlpha (0.7f));
        g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 6.0f, 1.0f));
    }

    // Horizontal: the value over the far end of the track while active.
    if (active > 0.01f && ! vertical)
    {
        const auto label = format (value);
        const auto font = fonts::semibold (9.5f);
        const float w = fonts::getTabularWidth (font, label) + 8.0f;
        auto tag = juce::Rectangle<float> (w, 12.0f).withCentre ({ centre.x < track.getCentreX() ? track.getRight() - w * 0.5f : track.getX() + w * 0.5f,
                                                                   track.getCentreY() });
        g.setColour (surfaceRaised.withAlpha (0.95f * active));
        roadie::theme::fillSquircle (g, tag, 4.0f);
        g.setColour (theme::text.withAlpha (active));
        g.setFont (font);
        fonts::drawTabular (g, label, tag, juce::Justification::centred);
    }

    // Value bubble while hovered or dragged.
    if (active > 0.01f && vertical)
    {
        const auto label = format (value);
        const auto font = fonts::semibold (10.5f);
        const float w = fonts::getTabularWidth (font, label) + 10.0f;
        auto bubble = juce::Rectangle<float> (w, 16.0f).withCentre (centre).withX (cap.getRight() + 4.0f);
        if (bubble.getRight() > (float) getWidth())
            bubble.setX (cap.getX() - 4.0f - w);
        g.setColour (surfaceRaised.withAlpha (0.95f * active));
        roadie::theme::fillSquircle (g, bubble, 5.0f);
        g.setColour (theme::text.withAlpha (active));
        g.setFont (font);
        fonts::drawTabular (g, label, bubble, juce::Justification::centred);
    }
}

void Fader::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isAltDown())
    {
        set (0.0f);
        return;
    }
    dragging = true;
    proportionAtDragStart = toProportion (value);
    repaint();
}

void Fader::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const auto track = getTrack();
    const float travel = orientation == Orientation::vertical ? track.getHeight() : track.getWidth();
    const float distance = orientation == Orientation::vertical ? (float) -e.getDistanceFromDragStartY() : (float) e.getDistanceFromDragStartX();
    const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
    set (fromProportion (proportionAtDragStart + distance * fine / std::max (1.0f, travel)));
}

void Fader::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

void Fader::mouseDoubleClick (const juce::MouseEvent&)
{
    set (0.0f);
}

void Fader::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float delta = (std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX) * (wheel.isReversed ? -1.0f : 1.0f);
    set (fromProportion (toProportion (value) + delta * (e.mods.isShiftDown() ? 0.02f : 0.1f)));
}

//==============================================================================
SegmentedControl::SegmentedControl (std::vector<Segment> s) : segments (std::move (s))
{
    noFocus (*this);
    for (size_t i = 0; i < segments.size(); ++i)
        fades.push_back (std::make_unique<HoverFade> (*this));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SegmentedControl::setSelected (int index)
{
    if (index != selected)
    {
        selected = index;
        repaint();
    }
}

int SegmentedControl::getIdealWidth() const
{
    int width = 4;
    for (const auto& segment : segments)
        width += 14 + 16 + 6 + (int) std::ceil (juce::GlyphArrangement::getStringWidth (fonts::semibold (12.5f), segment.label)) + 14;
    return width;
}

juce::Rectangle<int> SegmentedControl::getSegmentBounds (int index) const
{
    auto inner = getLocalBounds().reduced (2);
    int x = inner.getX();
    for (int i = 0; i < (int) segments.size(); ++i)
    {
        // Segments share the width as their ideal widths do (equally when squeezed).
        const int w = 14 + 16 + 6 + (int) std::ceil (juce::GlyphArrangement::getStringWidth (fonts::semibold (12.5f), segments[(size_t) i].label)) + 14;
        const int scaled = inner.getWidth() >= getIdealWidth() - 4 ? w * inner.getWidth() / std::max (1, getIdealWidth() - 4)
                                                                     : inner.getWidth() / (int) segments.size();
        if (i == index)
            return { x, inner.getY(), i == (int) segments.size() - 1 ? inner.getRight() - x : scaled, inner.getHeight() };
        x += scaled;
    }
    return {};
}

int SegmentedControl::segmentAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) segments.size(); ++i)
        if (getSegmentBounds (i).contains (p))
            return i;
    return -1;
}

void SegmentedControl::paint (juce::Graphics& g)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float r = bounds.getHeight() * 0.5f;
    g.setColour (surfaceSunken);
    roadie::theme::fillSquircle (g, bounds, r);
    g.setColour (border);
    roadie::theme::drawSquircle (g, bounds, r, 1.0f);

    for (int i = 0; i < (int) segments.size(); ++i)
    {
        auto area = getSegmentBounds (i).toFloat();
        const bool active = i == selected;
        const float hover = fades[(size_t) i]->get (i == hovered && ! active);

        if (active)
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            roadie::theme::fillSquircle (g, area.translated (0.0f, 1.0f), area.getHeight() * 0.5f);
            g.setColour (surfaceRaised.brighter (getAppearance() == Appearance::dark ? 0.15f : 0.0f));
            roadie::theme::fillSquircle (g, area, area.getHeight() * 0.5f);
            g.setColour (border.brighter (0.3f));
            roadie::theme::drawSquircle (g, area.reduced (0.5f), area.getHeight() * 0.5f, 1.0f);
        }
        else if (hover > 0.0f)
        {
            g.setColour (theme::text.withAlpha (0.06f * hover));
            roadie::theme::fillSquircle (g, area, area.getHeight() * 0.5f);
        }

        const auto colour = active ? theme::text : textMuted.interpolatedWith (theme::text, hover);
        // Too narrow for the word: the icon alone (the tooltip names it).
        const float needed = 14.0f + 16.0f + 6.0f + juce::GlyphArrangement::getStringWidth (fonts::semibold (12.5f), segments[(size_t) i].label) + 10.0f;
        if (area.getWidth() < needed)
        {
            icons::draw (g, segments[(size_t) i].icon, area.withSizeKeepingCentre (16.0f, 16.0f), colour, 2.0f);
            continue;
        }
        auto content = area.reduced (14.0f, 0.0f);
        icons::draw (g, segments[(size_t) i].icon, content.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f), colour, 2.0f);
        content.removeFromLeft (6.0f);
        g.setColour (colour);
        g.setFont (fonts::semibold (12.5f));
        g.drawText (segments[(size_t) i].label, content, juce::Justification::centredLeft, false);
    }
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const int index = segmentAt (e.getPosition());
    if (index != hovered)
    {
        hovered = index;
        setTooltip (index >= 0 ? segments[(size_t) index].tooltip : juce::String());
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    const int index = segmentAt (e.getPosition());
    if (index >= 0 && index != selected)
    {
        setSelected (index);
        if (onChange)
            onChange (index);
    }
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
                            + (label.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (fonts::medium (12.0f), label + " "))
                            + fonts::getTabularWidth (fonts::semibold (12.0f), value) + 12.0f);
}

void StatPill::mouseUp (const juce::MouseEvent& e)
{
    if (onClick && getLocalBounds().contains (e.getPosition()))
        onClick();
}

void StatPill::mouseEnter (const juce::MouseEvent&)
{
    setMouseCursor (onClick ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setRepaintsOnMouseActivity (onClick != nullptr);
}

void StatPill::paint (juce::Graphics& g)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (onClick && isMouseOver() ? surfaceRaised : surfaceSunken);
    roadie::theme::fillSquircle (g, bounds, bounds.getHeight() * 0.5f);
    g.setColour (border);
    roadie::theme::drawSquircle (g, bounds, bounds.getHeight() * 0.5f, 1.0f);

    auto inner = bounds.reduced (10.0f, 0.0f);
    g.setColour (dot);
    g.fillEllipse (inner.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
    inner.removeFromLeft (6.0f);

    const auto labelFont = fonts::medium (12.0f);
    g.setFont (labelFont);
    g.setColour (textMuted);
    const float labelWidth = label.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (labelFont, label + " ");
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
    setTooltip (isSolo ? tr ("Solo: listen to this track only")
                       : tr ("Mute: silence this track in the monitor"));
}

void MuteSoloButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const bool lit = getToggleState();
    const auto litColour = solo ? accent : warn;
    const float hover = fade.get (isOver);
    pressIn (g, bounds, isDown);

    g.setColour (lit ? litColour.withAlpha (0.22f + 0.08f * hover) : surfaceSunken.interpolatedWith (surfaceRaised, hover));
    roadie::theme::fillSquircle (g, bounds, 3.0f);
    g.setColour (lit ? litColour : borderStrong.interpolatedWith (textMuted, hover));
    roadie::theme::drawSquircle (g, bounds, 4.0f, 1.0f);
    g.setColour (lit ? ink (litColour) : textMuted.interpolatedWith (theme::text, hover));
    g.setFont (fonts::display (10.0f));
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
