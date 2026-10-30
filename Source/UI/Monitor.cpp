#include "Monitor.h"

using namespace roadie;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

juce::String formatDuration (double seconds)
{
    const int total = (int) seconds;
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

void drawDashedRect (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
{
    juce::Path path;
    path.addPath (roadie::theme::squircle (r, 4.0f));
    juce::Path dashed;
    const float dashes[] = { 4.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, path, dashes, 2);
    g.setColour (colour);
    g.fillPath (dashed);
}
} // namespace

Monitor::Monitor (Session& s) : session (s)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setTooltip (utf8 ("Double-clic : agrandir le moniteur"));
}

void Monitor::setPlaybackOnly (bool shouldBePlaybackOnly)
{
    if (playbackOnly != shouldBePlaybackOnly)
    {
        playbackOnly = shouldBePlaybackOnly;
        repaint();
    }
}

void Monitor::setCountIn (int beatsLeft)
{
    if (beatsLeft != countIn)
    {
        countIn = beatsLeft;
        repaint();
    }
}

bool Monitor::isLive() const
{
    return ! playbackOnly && ! session.isPreviewingArrangement();
}

void Monitor::refresh()
{
    const int frame = session.getCamera().getFrameCounter();
    const int previewFrame = session.getPreviewFrameCounter();
    const bool previewing = session.isPreviewingArrangement();
    if (frame != lastFrame || previewFrame != lastPreviewFrame || previewing != wasPreviewing
        || session.isRecordingTake() || session.isCameraOpening())
    {
        lastFrame = frame;
        lastPreviewFrame = previewFrame;
        wasPreviewing = previewing;
        repaint();
    }
}

juce::Rectangle<float> Monitor::fitPicture (juce::Rectangle<float> area, float aspect)
{
    if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f || aspect <= 0.0f)
        return area;
    if (area.getWidth() / area.getHeight() > aspect)
        return area.withSizeKeepingCentre (area.getHeight() * aspect, area.getHeight());
    return area.withSizeKeepingCentre (area.getWidth(), area.getWidth() / aspect);
}

void Monitor::mouseDown (const juce::MouseEvent&)
{
    if (! playbackOnly && ! session.getCamera().isOpen() && ! session.isCameraOpening() && onCameraClicked)
        onCameraClicked();
}

void Monitor::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClicked)
        onDoubleClicked();
}

void Monitor::mouseMove (const juce::MouseEvent&)
{
    setMouseCursor (! playbackOnly && ! session.getCamera().isOpen() ? juce::MouseCursor::PointingHandCursor
                                                                       : juce::MouseCursor::NormalCursor);
}

void Monitor::paint (juce::Graphics& g)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat();
    const bool live = isLive();
    auto& camera = session.getCamera();

    g.setColour (surface);
    g.fillRect (bounds);

    // Title bar: what the picture is, and where it comes from.
    auto bar = bounds.removeFromTop ((float) barHeight).reduced (12.0f, 0.0f);
    icons::draw (g, icons::Icon::monitor, bar.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, 14.0f), ink (camAccent), 2.2f);
    bar.removeFromLeft (7.0f);

    const auto badge = ! live ? juce::String ("LECTURE") : session.isRecordingTake() ? juce::String ("REC") : juce::String ("DIRECT");
    const auto badgeColour = ! live ? loopAccent : (session.isRecordingTake() ? rec : camAccent);
    const auto badgeFont = fonts::display (9.0f).withExtraKerningFactor (0.06f);
    const float badgeWidth = juce::GlyphArrangement::getStringWidth (badgeFont, badge) + 12.0f;
    auto badgeArea = bar.removeFromRight (badgeWidth).withSizeKeepingCentre (badgeWidth, 15.0f);
    g.setColour (badgeColour.withAlpha (0.2f));
    roadie::theme::fillSquircle (g, badgeArea, 7.5f);
    g.setColour (ink (badgeColour));
    g.setFont (badgeFont);
    g.drawText (badge, badgeArea, juce::Justification::centred, false);

    g.setColour (theme::text);
    g.setFont (fonts::semibold (12.0f));
    const auto title = playbackOnly ? utf8 ("Moniteur \xc2\xb7 cam\xc3\xa9ra coup\xc3\xa9" "e en Monter")
                                    : (camera.isOpen() ? session.getCameraDescription() : utf8 ("Moniteur"));
    g.drawText (title, bar.withTrimmedRight (8.0f), juce::Justification::centredLeft, true);

    // Picture, letterboxed in a dark "screen" (dark in both themes).
    auto screen = bounds.reduced (8.0f, 0.0f).withTrimmedBottom (8.0f);
    g.setColour (juce::Colour (0xff0b0a0d));
    roadie::theme::fillSquircle (g, screen, radiusSm);

    const auto image = live ? (camera.isOpen() ? camera.getLatestFrame() : juce::Image()) : session.getPreviewFrame();
    const float aspect = image.isValid() ? (float) image.getWidth() / (float) std::max (1, image.getHeight()) : 16.0f / 9.0f;
    const auto picture = fitPicture (screen.reduced (4.0f), aspect);

    g.saveState();
    juce::Path clip;
    clip.addPath (roadie::theme::squircle (screen, radiusSm));
    g.reduceClipRegion (clip);

    if (image.isValid())
    {
        g.saveState();
        // Live is mirrored, like a selfie view; playback shows the movie as it will be exported.
        if (live)
            g.addTransform (juce::AffineTransform::scale (-1.0f, 1.0f, picture.getCentreX(), 0.0f));
        g.drawImage (image, picture, juce::RectanglePlacement::stretchToFit);
        g.restoreState();
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d1828), picture.getX(), picture.getY(),
                                                 juce::Colour (0xff0e0c13), picture.getRight(), picture.getBottom(), false));
        g.fillRect (picture);
        g.setGradientFill (juce::ColourGradient (camAccent.withAlpha (0.22f), picture.getX() + picture.getWidth() * 0.32f, picture.getY() + picture.getHeight() * 0.3f,
                                                 camAccent.withAlpha (0.0f), picture.getX() + picture.getWidth() * 0.8f, picture.getBottom(), true));
        g.fillRect (picture);

        g.setColour (juce::Colour (0xffb9b5ad));
        g.setFont (fonts::semibold (std::max (11.0f, std::min (15.0f, picture.getWidth() / 28.0f))));
        const auto message = ! live ? (playbackOnly ? utf8 ("Pas de vid\xc3\xa9o sous la t\xc3\xaa" "te de lecture")
                                                    : utf8 ("Pas de vid\xc3\xa9o ici"))
                             : session.isCameraOpening() ? utf8 ("Ouverture de la cam\xc3\xa9ra\xe2\x80\xa6")
                             : session.getCameraError().isNotEmpty() ? session.getCameraError()
                             : CameraCapture::isSupported() ? utf8 ("Cam\xc3\xa9ra \xc3\xa9teinte \xe2\x80\x94 clique pour l'activer")
                                                            : utf8 ("Cam\xc3\xa9ra indisponible sur ce syst\xc3\xa8me");
        g.drawFittedText (message, picture.reduced (14.0f).toNearestInt(), juce::Justification::centred, 4);
    }

    // Framing guides, live only: the 9:16 crop of a vertical export inside
    // the 16:9 picture, with a little margin for the platforms' buttons.
    if (live && picture.getHeight() > 60.0f)
    {
        const auto light = juce::Colour (0xfff6f3ed);
        const auto vertical = fitPicture (picture, 9.0f / 16.0f);
        drawDashedRect (g, vertical.reduced (1.0f), light.withAlpha (0.28f));
        drawDashedRect (g, picture.reduced (picture.getWidth() * 0.05f, picture.getHeight() * 0.05f), light.withAlpha (0.14f));

        g.setFont (fonts::display (9.0f));
        g.setColour (light.withAlpha (0.45f));
        g.drawText ("9:16", vertical.withHeight (16.0f).reduced (6.0f, 0.0f).translated (0.0f, 4.0f), juce::Justification::centredLeft, false);
        g.drawText ("16:9", picture.reduced (picture.getWidth() * 0.05f + 6.0f, picture.getHeight() * 0.05f + 4.0f).withHeight (12.0f),
                    juce::Justification::centredRight, false);
    }

    // REC indicator.
    if (session.isRecordingTake())
    {
        const bool blinkOn = (juce::Time::getMillisecondCounter() / 700) % 2 == 0;
        auto recArea = juce::Rectangle<float> (picture.getX() + 10.0f, picture.getY() + 10.0f, 110.0f, 14.0f);
        g.setColour (rec.withAlpha (blinkOn ? 1.0f : 0.25f));
        g.fillEllipse (recArea.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f));
        recArea.removeFromLeft (6.0f);
        g.setColour (rec);
        g.setFont (fonts::display (11.5f));
        fonts::drawTabular (g, "REC " + formatDuration (session.getTakeSeconds()), recArea, juce::Justification::centredLeft);
    }

    // Count-in: big beats over the picture.
    if (countIn > 0)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (picture);
        g.setColour (juce::Colour (0xfff6f3ed));
        g.setFont (fonts::display (std::min (picture.getHeight() * 0.5f, 140.0f)));
        g.drawText (juce::String (countIn), picture, juce::Justification::centred, false);
    }

    // Sync pill.
    if (live && camera.isOpen())
    {
        const auto ms = juce::roundToInt (session.getSyncLagMs());
        const auto label = session.isCalibrated() ? utf8 ("Sync ") + (ms >= 0 ? "+" : "") + juce::String (ms) + " ms"
                                                  : utf8 ("Sync non calibr\xc3\xa9" "e");
        const auto font = fonts::display (9.5f);
        const float w = juce::GlyphArrangement::getStringWidth (font, label) + 14.0f;
        auto pill = juce::Rectangle<float> (picture.getRight() - 9.0f - w, picture.getBottom() - 9.0f - 17.0f, w, 17.0f);
        g.setColour (juce::Colour (0xff0e0c13).withAlpha (0.7f));
        roadie::theme::fillSquircle (g, pill, 8.5f);
        g.setColour (session.isCalibrated() ? camAccent : warn);
        g.setFont (font);
        g.drawText (label, pill, juce::Justification::centred, false);
    }

    g.restoreState();

    g.setColour (! live ? loopAccent.withAlpha (0.5f) : (session.isRecordingTake() ? rec : border));
    roadie::theme::drawSquircle (g, screen.reduced (0.5f), radiusSm, 1.0f);
}
