#include "Icons.h"

namespace roadie::icons
{
namespace
{
struct Shape
{
    juce::Path path;
    bool filled = false;
};

juce::Path svg (std::initializer_list<const char*> parts)
{
    juce::Path p;
    for (auto* d : parts)
        p.addPath (juce::Drawable::parseSVGPath (d));
    return p;
}

Shape make (Icon icon)
{
    juce::Path p;

    switch (icon)
    {
        case Icon::previous:    return { svg ({ "M6 6h2v12H6z", "M20 6v12l-10-6z" }), true };
        case Icon::play:        return { svg ({ "M8 5l12 7-12 7z" }), true };
        case Icon::pause:       return { svg ({ "M7 5h3.5v14H7z", "M13.5 5H17v14h-3.5z" }), true };
        case Icon::stop:        p.addRoundedRectangle (6.0f, 6.0f, 12.0f, 12.0f, 1.5f); return { p, true };
        case Icon::record:      p.addEllipse (5.0f, 5.0f, 14.0f, 14.0f); return { p, true };
        case Icon::loop:        return { svg ({ "M17 2l4 4-4 4", "M21 6H8a5 5 0 0 0 0 10h1", "M7 22l-4-4 4-4", "M3 18h13a5 5 0 0 0 0-10h-1" }) };
        case Icon::exportArrow: return { svg ({ "M12 15V3", "M7 8l5-5 5 5", "M4 15v4a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4" }) };
        case Icon::gauge:       return { svg ({ "M4 12a8 8 0 0 1 16 0", "M12 12l4-5" }) };
        case Icon::search:      p.addEllipse (4.0f, 4.0f, 14.0f, 14.0f); p.addPath (svg ({ "M21 21l-4-4" })); return { p };
        case Icon::guitar:      p = svg ({ "M4 20l6-6", "M12 12l7-7 2 2-7 7" }); p.addEllipse (4.0f, 14.0f, 6.0f, 6.0f); return { p };
        case Icon::amp:         p.addRoundedRectangle (3.0f, 4.0f, 18.0f, 12.0f, 2.0f); p.addEllipse (6.0f, 8.0f, 4.0f, 4.0f); p.addEllipse (14.0f, 8.0f, 4.0f, 4.0f); p.addPath (svg ({ "M6 20h12" })); return { p };
        case Icon::camera:      p.addRoundedRectangle (2.0f, 7.0f, 15.0f, 11.0f, 2.0f); p.addPath (svg ({ "M17 10l5-3v10l-5-3" })); return { p };
        case Icon::plus:        return { svg ({ "M12 5v14", "M5 12h14" }) };
        case Icon::undo:        return { svg ({ "M3 7v6h6", "M3 13a9 9 0 1 0 3-7" }) };
        case Icon::clear:       p.addRoundedRectangle (5.0f, 5.0f, 14.0f, 14.0f, 2.0f); return { p };
        case Icon::settings:    p = svg ({ "M4 6h9", "M19 6h1", "M4 12h3", "M13 12h7", "M4 18h11", "M21 18h-0" });
                                p.addEllipse (13.0f, 4.0f, 4.0f, 4.0f); p.addEllipse (7.0f, 10.0f, 4.0f, 4.0f); p.addEllipse (15.0f, 16.0f, 4.0f, 4.0f);
                                return { p };
        case Icon::metronome:   return { svg ({ "M9 3h6l3 18H6z", "M12 15l5-7", "M8 15h8" }) };
        case Icon::folder:      return { svg ({ "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z" }) };
        case Icon::check:       return { svg ({ "M5 12l5 5 9-10" }) };
        case Icon::close:       return { svg ({ "M6 6l12 12", "M18 6L6 18" }) };
        case Icon::waveform:    return { svg ({ "M3 12h2", "M7 8v8", "M11 4v16", "M15 7v10", "M19 10v4" }) };
        case Icon::cycle:       return { svg ({ "M4 11V9a3 3 0 0 1 3-3h12", "M16 3l3 3-3 3", "M20 13v2a3 3 0 0 1-3 3H5", "M8 21l-3-3 3-3" }) };
        case Icon::library:     p.addRoundedRectangle (3.0f, 4.0f, 18.0f, 16.0f, 2.5f); p.addPath (svg ({ "M9 4v16", "M5.5 8h1.5", "M5.5 11h1.5" })); return { p };
        case Icon::timeline:    return { svg ({ "M3 6h9", "M15 6h6", "M3 12h5", "M11 12h10", "M3 18h12", "M18 18h3" }) };
        case Icon::song:        p = svg ({ "M9 18V5l11-2v13" }); p.addEllipse (3.0f, 15.0f, 6.0f, 6.0f); p.addEllipse (14.0f, 13.0f, 6.0f, 6.0f); return { p };
        case Icon::tuner:       return { svg ({ "M3 17a9 9 0 0 1 18 0", "M12 17l4-7", "M12 4v2", "M5.6 7.6l1.4 1.4", "M18.4 7.6l-1.4 1.4" }) };
        case Icon::monitor:     p.addRoundedRectangle (2.0f, 4.0f, 20.0f, 13.0f, 2.0f); p.addPath (svg ({ "M8 21h8", "M12 17v4" })); return { p };
        case Icon::power:       return { svg ({ "M12 3v8", "M6.3 6.3a8 8 0 1 0 11.4 0" }) };
        case Icon::arrow:       return { svg ({ "M4 12h15", "M14 7l5 5-5 5" }) };
        case Icon::cut:         p = svg ({ "M20 4L8.1 15.9", "M14.5 14.5L20 20", "M8.1 8.1L12 12" }); p.addEllipse (3.0f, 3.0f, 6.0f, 6.0f); p.addEllipse (3.0f, 15.0f, 6.0f, 6.0f); return { p };
        case Icon::trash:       return { svg ({ "M4 7h16", "M9 7V4h6v3", "M6 7l1 13h10l1-13", "M10 11v6", "M14 11v6" }) };
        case Icon::inspector:   p.addRoundedRectangle (3.0f, 4.0f, 18.0f, 16.0f, 2.5f); p.addPath (svg ({ "M3 14h18", "M8 17h0.01", "M12 17h0.01", "M16 17h0.01" })); return { p };
    }

    return {};
}
} // namespace

void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth)
{
    auto shape = make (icon);
    const float side = std::min (area.getWidth(), area.getHeight());
    const float scale = side / 24.0f;
    const auto transform = juce::AffineTransform::scale (scale)
                               .translated (area.getCentreX() - side * 0.5f, area.getCentreY() - side * 0.5f);

    g.setColour (colour);
    if (shape.filled)
    {
        g.fillPath (shape.path, transform);
    }
    else
    {
        // Stroke in icon space so the line weight scales with the icon.
        juce::Path stroked;
        juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createStrokedPath (stroked, shape.path);
        g.fillPath (stroked, transform);
    }
}
} // namespace roadie::icons
