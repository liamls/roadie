#include "Timeline.h"

using namespace roadie;
using Track = Session::Track;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

juce::Colour accentOf (Track t)
{
    switch (t)
    {
        case Track::amp:    return theme::ampAccent;
        case Track::loop:   return theme::loopAccent;
        case Track::camera: return theme::camAccent;
        default:            return theme::textMuted;
    }
}

icons::Icon iconOf (Track t)
{
    switch (t)
    {
        case Track::amp:    return icons::Icon::guitar; // the merged DI + ampli track
        case Track::loop:   return icons::Icon::loop;
        case Track::camera: return icons::Icon::camera;
        default:            return icons::Icon::guitar;
    }
}

// Track::amp is the merged "Guitare" track (raw input, amp processing and
// its take regions all live here); the amp controls themselves are still
// labelled "Ampli" in the Inspector.
juce::String nameOf (Track t)
{
    switch (t)
    {
        case Track::amp:    return "Guitare";
        case Track::loop:   return "Looper";
        case Track::camera: return utf8 ("Cam\xc3\xa9ra");
        default:            return "Guitare";
    }
}

juce::String formatDuration (double seconds)
{
    const int total = (int) seconds;
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

juce::String formatBars (double bars)
{
    const double rounded = std::round (bars * 2.0) / 2.0;
    return std::abs (rounded - std::round (rounded)) < 0.01 ? juce::String ((int) std::round (rounded))
                                                             : juce::String (rounded, 1).replaceCharacter ('.', ',');
}

// Peak (linear) → 0..1 display height on a 48 dB scale, so a quiet DI and a
// loud amp both read clearly.
float displayHeight (float peak)
{
    return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 48.0f) / 48.0f);
}

// Symmetric waveform envelope; `fadeFrom`..1 is drawn dimmer (older data
// ahead of the playhead in the sweeping lanes).
void drawPeaks (juce::Graphics& g, juce::Rectangle<float> area, const std::function<float (int)>& peakAt, int numPeaks,
                juce::Colour colour, float fadeFrom = 2.0f)
{
    if (numPeaks <= 0 || area.getWidth() < 2.0f)
        return;

    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.5f;
    const int columns = std::max (2, (int) (area.getWidth() / 2.0f));

    auto build = [&] (int fromColumn, int toColumn)
    {
        juce::Path p;
        if (toColumn <= fromColumn)
            return p;

        std::vector<float> heights;
        for (int c = fromColumn; c <= toColumn; ++c)
        {
            const int index = std::min (numPeaks - 1, c * numPeaks / columns);
            heights.push_back (std::max (0.04f, displayHeight (peakAt (index))));
        }

        const float step = area.getWidth() / (float) columns;
        p.startNewSubPath (area.getX() + fromColumn * step, mid - heights[0] * half);
        for (size_t i = 1; i < heights.size(); ++i)
            p.lineTo (area.getX() + (fromColumn + (float) i) * step, mid - heights[i] * half);
        for (size_t i = heights.size(); i-- > 0;)
            p.lineTo (area.getX() + (fromColumn + (float) i) * step, mid + heights[i] * half);
        p.closeSubPath();
        return p;
    };

    const int split = juce::jlimit (0, columns, (int) (fadeFrom * (float) columns));
    auto fresh = build (0, split);
    auto old = build (split, columns);

    g.setColour (colour.withAlpha (0.3f));
    g.fillPath (fresh);
    g.setColour (colour.withAlpha (0.9f));
    g.strokePath (fresh, juce::PathStrokeType (1.0f));

    g.setColour (colour.withAlpha (0.12f));
    g.fillPath (old);
    g.setColour (colour.withAlpha (0.35f));
    g.strokePath (old, juce::PathStrokeType (1.0f));
}

void drawChip (juce::Graphics& g, juce::Rectangle<float> region, const juce::String& text, juce::Colour colour)
{
    g.setColour (colour);
    g.setFont (fonts::display (10.0f));
    g.drawText (text, region.reduced (9.0f, 5.0f).withHeight (13.0f), juce::Justification::centredLeft, true);
}

void drawRegion (juce::Graphics& g, juce::Rectangle<float> region, juce::Colour colour, float fillAlpha = 0.12f, float borderAlpha = 0.35f)
{
    g.setColour (colour.withAlpha (fillAlpha));
    g.fillRoundedRectangle (region, theme::radiusSm);
    g.setColour (colour.withAlpha (borderAlpha));
    g.drawRoundedRectangle (region.reduced (0.5f), theme::radiusSm, 1.0f);
}

// Samples in the whole arrangement, i.e. what the ruler and the track lanes
// span left-to-right. AudioEngine only knows samples/clips/loop, not the
// Project's notion of "content end" — Session is what converts.
std::int64_t timelineWindowSamples (Session& session)
{
    return (std::int64_t) (session.getContentEnd() * session.getEngine().getSampleRate());
}
} // namespace

//==============================================================================
class Timeline::TrackRow : public juce::Component
{
public:
    TrackRow (Timeline& o, Session& s, Track t)
        : track (t), owner (o), session (s),
          muteButton (false, accentOf (t)), soloButton (true, accentOf (t))
    {
        muteButton.onClick = [this] { session.setMuted (track, muteButton.getToggleState()); };
        soloButton.onClick = [this] { session.setSolo (track, soloButton.getToggleState()); };
        addAndMakeVisible (muteButton);

        if (track == Track::camera)
            muteButton.setTooltip (utf8 ("Couper la cam\xc3\xa9ra pour les prises (audio seul)"));
        else
            addAndMakeVisible (soloButton);

        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void refreshState()
    {
        muteButton.setToggleState (session.isMuted (track), juce::dontSendNotification);
        soloButton.setToggleState (session.isSolo (track), juce::dontSendNotification);
        repaint();
    }

    void resized() override
    {
        auto head = getLocalBounds().removeFromLeft (headWidth).reduced (10, 0);
        auto toggles = head.removeFromRight (16);
        const int total = track == Track::camera ? 12 : 26;
        toggles = toggles.withSizeKeepingCentre (16, total);
        muteButton.setBounds (toggles.removeFromTop (12));
        toggles.removeFromTop (2);
        soloButton.setBounds (toggles.removeFromTop (12));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.setSelectedTrack (track);
        if (owner.onTrackSelected)
            owner.onTrackSelected (track);

        if (track == Track::camera && e.x > headWidth && ! session.getCamera().isOpen() && owner.onCameraClicked)
            owner.onCameraClicked();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        paintHead (g, bounds.removeFromLeft ((float) headWidth));
        paintLane (g, bounds);

        g.setColour (theme::border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    const Track track;

private:
    void paintHead (juce::Graphics& g, juce::Rectangle<float> area)
    {
        using namespace theme;
        const bool active = owner.getSelectedTrack() == track;
        const auto accent = accentOf (track);

        g.setColour (active ? surfaceRaised : surface);
        g.fillRect (area);
        if (active)
        {
            g.setColour (accent);
            g.fillRect (area.withWidth (3.0f));
        }
        g.setColour (border);
        g.drawVerticalLine ((int) area.getRight() - 1, area.getY(), area.getBottom());

        auto content = area.reduced (10.0f, 0.0f);
        content.removeFromRight (16.0f + 6.0f);

        auto iconBox = content.removeFromLeft (26.0f).withSizeKeepingCentre (26.0f, 26.0f);
        g.setColour (accent.withAlpha (0.15f));
        g.fillRoundedRectangle (iconBox, 7.0f);
        icons::draw (g, iconOf (track), iconBox.reduced (6.0f), accent, 1.9f);
        content.removeFromLeft (9.0f);

        auto labels = content.withSizeKeepingCentre (content.getWidth(), 32.0f);
        g.setColour (session.isMuted (track) ? textMuted : theme::text);
        g.setFont (fonts::semibold (12.5f));
        g.drawText (nameOf (track), labels.removeFromTop (17.0f), juce::Justification::bottomLeft, true);
        g.setColour (textFaint);
        g.setFont (fonts::medium (10.5f));
        g.drawText (subtitle(), labels, juce::Justification::topLeft, true);
    }

    juce::String subtitle() const
    {
        const auto& engine = session.getEngine();
        switch (track)
        {
            case Track::amp:
                if (session.isLoadingCapture())
                    return utf8 ("Chargement\xe2\x80\xa6");
                return session.getCaptureName().isNotEmpty() ? session.getCaptureName() : utf8 ("Aucune capture");

            case Track::loop:
            {
                const auto& looper = engine.getLooper();
                const auto bars = looper.getRecordedLengthSamples() / std::max (1.0, session.getSamplesPerBar());
                switch (looper.getState())
                {
                    case Looper::State::Empty:       return "Vide";
                    case Looper::State::Recording:   return utf8 ("Enregistrement\xe2\x80\xa6");
                    case Looper::State::Overdubbing: return utf8 ("Overdub\xe2\x80\xa6");
                    case Looper::State::Stopped:     return utf8 ("En pause \xc2\xb7 ") + formatBars (bars) + " mes.";
                    case Looper::State::Playing:
                    default:
                    {
                        const int layers = looper.getLayerCount();
                        return juce::String (layers) + (layers > 1 ? " couches" : " couche") + utf8 (" \xc2\xb7 ") + formatBars (bars) + " mes.";
                    }
                }
            }

            case Track::camera:
            default:
                if (! session.getCamera().isOpen())
                    return session.isCameraOpening() ? utf8 ("Ouverture\xe2\x80\xa6") : utf8 ("\xc3\x89teinte");
                if (session.isMuted (track))
                    return utf8 ("Coup\xc3\xa9" "e pour les prises");
                if (session.isCalibrated())
                {
                    const auto ms = juce::roundToInt (session.getSyncLagMs());
                    return utf8 ("Synchro ") + (ms >= 0 ? "+" : "") + juce::String (ms) + " ms";
                }
                return utf8 ("Synchro non calibr\xc3\xa9" "e");
        }
    }

    void paintGrid (juce::Graphics& g, juce::Rectangle<float> lane)
    {
        const auto window = (double) timelineWindowSamples (session);
        const double perBar = session.getSamplesPerBar();
        if (window <= 0.0 || perBar <= 0.0)
            return;

        const double pixelsPerBar = lane.getWidth() * perBar / window;
        int every = 1;
        while (pixelsPerBar * every < 18.0)
            every *= 2;

        g.setColour (theme::border.withAlpha (0.7f));
        for (int bar = every; bar * perBar < window; bar += every)
            g.drawVerticalLine ((int) (lane.getX() + bar * pixelsPerBar), lane.getY(), lane.getBottom());
    }

    void paintLane (juce::Graphics& g, juce::Rectangle<float> lane)
    {
        g.setColour (theme::surfaceSunken);
        g.fillRect (lane);
        paintGrid (g, lane);

        g.saveState();
        g.reduceClipRegion (lane.toNearestInt());

        switch (track)
        {
            case Track::amp:    paintAmp (g, lane); break;
            case Track::loop:   paintLoop (g, lane); break;
            case Track::camera: paintCamera (g, lane); break;
        }

        g.restoreState();
    }

    // The amp's own peak history (what AudioEngine actually tracks — see
    // AudioEngine::writeHistory); there's no separate raw-input history, so
    // this is the one live waveform the Guitare lane can show.
    void paintSweep (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto& engine = session.getEngine();
        const auto slots = juce::jlimit<std::int64_t> (0, AudioEngine::historyCapacity,
                                                         timelineWindowSamples (session) / std::max (1, engine.getHistorySlotSamples()));
        drawPeaks (g, area.reduced (0.0f, 4.0f),
                   [&engine] (int i) { return engine.getHistoryPeak (i); },
                   (int) slots, theme::ampAccent, owner.getPlayheadProportion());
    }

    void paintAmp (juce::Graphics& g, juce::Rectangle<float> lane)
    {
        auto region = lane.reduced (lane.getWidth() * 0.012f, 10.0f);
        const bool live = ! session.isMuted (Track::amp);
        drawRegion (g, region, theme::ampAccent, live ? 0.12f : 0.05f, live ? 0.35f : 0.15f);

        auto chipText = session.getCaptureName().isNotEmpty() ? session.getCaptureName() : utf8 ("Signal brut");
        if (session.getIrName().isNotEmpty())
            chipText << utf8 (" \xc2\xb7 ") << session.getIrName();
        drawChip (g, region, chipText, theme::ampAccent);

        paintSweep (g, region.withTrimmedTop (16.0f).reduced (2.0f, 2.0f));
    }

    void updateOverview()
    {
        const auto& looper = session.getEngine().getLooper();
        const int length = looper.getRecordedLengthSamples();
        const int layers = looper.getLayerCount();
        const double now = juce::Time::getMillisecondCounterHiRes();
        const bool overdubbing = looper.getState() == Looper::State::Overdubbing;

        if (length != overviewLength || layers != overviewLayers || (overdubbing && now - overviewTime > 250.0))
        {
            overview.resize (400);
            looper.getOverview (overview.data(), (int) overview.size());
            overviewLength = length;
            overviewLayers = layers;
            overviewTime = now;
        }
    }

    void paintLoop (juce::Graphics& g, juce::Rectangle<float> lane)
    {
        auto& engine = session.getEngine();
        const auto& looper = engine.getLooper();
        const auto state = looper.getState();
        const double window = (double) timelineWindowSamples (session);
        const double length = looper.getRecordedLengthSamples();
        const auto band = lane.reduced (0.0f, 10.0f);

        if (state == Looper::State::Recording)
        {
            auto region = band.withWidth ((float) (lane.getWidth() * length / window)).withTrimmedLeft (4.0f);
            drawRegion (g, region, theme::rec, 0.14f, 0.5f);
            drawChip (g, region, utf8 ("\xe2\x97\x8f Enregistrement"), theme::rec);
            g.saveState();
            g.reduceClipRegion (region.toNearestInt());
            paintSweep (g, lane.withTrimmedTop (14.0f).reduced (0.0f, 6.0f));
            g.restoreState();
            return;
        }

        if (length <= 0.0)
        {
            g.setColour (theme::textFaint);
            g.setFont (fonts::medium (11.5f));
            g.drawText (utf8 ("Pas encore de boucle \xe2\x80\x94 R ou \xe2\x9f\xb2 pour enregistrer"), lane.reduced (14.0f, 0.0f),
                        juce::Justification::centredLeft, true);
            return;
        }

        updateOverview();

        const float regionWidth = (float) (lane.getWidth() * length / window);
        const int repeats = (int) std::ceil (window / length);
        const bool muted = session.isMuted (Track::loop);

        for (int r = 0; r < repeats; ++r)
        {
            auto region = juce::Rectangle<float> (lane.getX() + regionWidth * (float) r, band.getY(), regionWidth, band.getHeight())
                              .reduced (4.0f, 0.0f);
            const float alpha = (r == 0 ? 1.0f : 0.72f) * (muted ? 0.4f : 1.0f);

            g.saveState();
            g.setOpacity (alpha);
            const auto colour = state == Looper::State::Overdubbing ? theme::loopAccent.brighter (0.15f) : theme::loopAccent;
            drawRegion (g, region, colour);

            const int layers = looper.getLayerCount();
            juce::String chip = r == 0 ? "Couche " + juce::String (std::max (1, layers)) : utf8 ("R\xc3\xa9p\xc3\xa9tition");
            if (state == Looper::State::Overdubbing && r == 0)
                chip = utf8 ("Overdub \xe2\x80\xa6 couche ") + juce::String (layers);
            drawChip (g, region, chip, colour);

            drawPeaks (g, region.withTrimmedTop (16.0f).reduced (4.0f, 3.0f),
                       [this] (int i) { return overview[(size_t) i]; }, (int) overview.size(), colour);
            g.restoreState();
        }
    }

    void paintCamera (juce::Graphics& g, juce::Rectangle<float> lane)
    {
        auto& camera = session.getCamera();
        auto region = lane.reduced (lane.getWidth() * 0.012f, 10.0f);

        std::vector<juce::Image> thumbs;
        juce::String chip;

        if (session.isRecordingTake() && camera.isRecording())
        {
            thumbs = camera.getRecordingThumbnails();
            chip = utf8 ("\xe2\x97\x8f REC ") + formatDuration (session.getTakeSeconds());
        }
        else if (session.getSelection() == Session::Selection::clip)
        {
            const auto& clips = session.getArrangement().clips;
            const int clipIndex = session.getSelectedClip();
            const int takeIndex = juce::isPositiveAndBelow (clipIndex, (int) clips.size()) ? clips[(size_t) clipIndex].take : -1;
            if (juce::isPositiveAndBelow (takeIndex, (int) session.getTakes().size()))
            {
                const auto& take = session.getTakes()[(size_t) takeIndex];
                thumbs = take.thumbnails;
                chip = take.name + utf8 (" \xc2\xb7 ") + formatDuration (take.durationSeconds) + (take.hasVideo() ? juce::String() : utf8 (" \xc2\xb7 audio seul"));
            }
        }

        const bool open = camera.isOpen();
        drawRegion (g, region, theme::camAccent, open ? 0.10f : 0.05f, open ? 0.30f : 0.15f);

        auto frames = region.reduced (6.0f);
        const float frameWidth = std::max (24.0f, frames.getHeight() * 16.0f / 9.0f);
        const int count = std::max (1, (int) (frames.getWidth() / (frameWidth + 3.0f)));
        const float w = (frames.getWidth() - 3.0f * (float) (count - 1)) / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const auto frame = juce::Rectangle<float> (frames.getX() + (float) i * (w + 3.0f), frames.getY(), w, frames.getHeight());
            juce::Path clip;
            clip.addRoundedRectangle (frame, 3.0f);

            if (! thumbs.empty())
            {
                const auto& image = thumbs[(size_t) i * thumbs.size() / (size_t) count];
                g.saveState();
                g.reduceClipRegion (clip);
                g.drawImage (image, frame, juce::RectanglePlacement::fillDestination);
                g.restoreState();
            }
            else
            {
                const bool odd = i % 2 == 1;
                const auto base = theme::surfaceSunken;
                g.setGradientFill (juce::ColourGradient (base.interpolatedWith (theme::camAccent, odd ? 0.26f : 0.22f), frame.getX(), frame.getY(),
                                                         base.interpolatedWith (theme::camAccent, odd ? 0.14f : 0.12f), frame.getRight(), frame.getBottom(), false));
                g.setOpacity (open ? 1.0f : 0.5f);
                g.fillPath (clip);
                g.setOpacity (1.0f);
            }
        }

        if (chip.isEmpty())
            chip = open ? utf8 ("Aucune prise film\xc3\xa9" "e \xe2\x80\x94 \xe2\x97\x8f ou T pour enregistrer")
                        : utf8 ("Cam\xc3\xa9ra \xc3\xa9teinte \xe2\x80\x94 clique pour l'activer");

        // Chip on a dark pill so it stays readable over the thumbnails.
        const auto font = fonts::display (10.0f);
        const float chipWidth = juce::GlyphArrangement::getStringWidth (font, chip) + 14.0f;
        auto pill = juce::Rectangle<float> (region.getX() + 6.0f, region.getY() + 6.0f, std::min (chipWidth, region.getWidth() - 12.0f), 16.0f);
        g.setColour (juce::Colour (0xff0e0c13).withAlpha (0.75f));
        g.fillRoundedRectangle (pill, 8.0f);
        g.setColour (session.isRecordingTake() ? theme::rec : theme::camAccent);
        g.setFont (font);
        g.drawText (chip, pill.reduced (7.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    Timeline& owner;
    Session& session;
    roadie::ui::MuteSoloButton muteButton, soloButton;
    std::vector<float> overview;
    int overviewLength = -1, overviewLayers = -1;
    double overviewTime = 0.0;
};

//==============================================================================
class Timeline::PlayheadOverlay : public juce::Component
{
public:
    explicit PlayheadOverlay (Timeline& o) : owner (o)
    {
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        const float laneWidth = (float) (getWidth() - headWidth);
        const float x = (float) headWidth + owner.getPlayheadProportion() * laneWidth;

        g.setColour (theme::text);
        g.fillRect (juce::Rectangle<float> (x - 0.75f, (float) rulerHeight, 1.5f, (float) (getHeight() - rulerHeight)));

        juce::Path head;
        head.addTriangle (x - 5.0f, (float) rulerHeight - 6.0f, x + 5.0f, (float) rulerHeight - 6.0f, x, (float) rulerHeight);
        g.fillPath (head);
    }

private:
    Timeline& owner;
};

//==============================================================================
class Timeline::CameraMonitor : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    CameraMonitor (Timeline& o, Session& s) : owner (o), session (s)
    {
        constrainer.setMinimumOnscreenAmounts (0xffffff, 0xffffff, 0xffffff, 0xffffff);
        setSize (smallWidth, heightFor (smallWidth));
        setTooltip (utf8 ("Glisse la barre pour d\xc3\xa9placer \xc2\xb7 double-clic pour agrandir"));
        shadower.setOwner (this);
    }

    void refresh()
    {
        const int frame = session.getCamera().getFrameCounter();
        if (frame != lastFrame || session.isRecordingTake() || session.isCameraOpening())
        {
            lastFrame = frame;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y <= barHeight)
            dragger.startDraggingComponent (this, e);
        else if (! session.getCamera().isOpen() && owner.onCameraClicked)
            owner.onCameraClicked();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mouseDownPosition.y <= barHeight)
            dragger.dragComponent (this, e, &constrainer);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        const int newWidth = getWidth() == smallWidth ? largeWidth : smallWidth;
        const auto bottomRight = getBounds().getBottomRight();
        setBounds (juce::Rectangle<int> (newWidth, heightFor (newWidth)).withBottomY (bottomRight.y).withRightX (bottomRight.x));
        keepInside();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (e.y <= barHeight ? juce::MouseCursor::DraggingHandCursor
                                         : (session.getCamera().isOpen() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor));
    }

    void keepInside()
    {
        if (auto* parent = getParentComponent())
        {
            auto area = parent->getLocalBounds().withTrimmedTop (rulerHeight).withTrimmedLeft (headWidth);
            setBounds (getBounds().constrainedWithin (area));
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        auto bounds = getLocalBounds().toFloat();

        juce::Path card;
        card.addRoundedRectangle (bounds, radiusMd);
        g.setColour (surface);
        g.fillPath (card);

        g.saveState();
        g.reduceClipRegion (card);

        // Title bar.
        auto bar = bounds.removeFromTop ((float) barHeight);
        g.setColour (camAccent.withAlpha (0.16f));
        g.fillRect (bar);
        auto barContent = bar.reduced (8.0f, 0.0f);
        icons::draw (g, icons::Icon::camera, barContent.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f), camAccent, 2.5f);
        barContent.removeFromLeft (5.0f);
        g.setColour (camAccent);
        g.setFont (fonts::display (10.0f).withExtraKerningFactor (0.04f));
        g.drawText (utf8 ("MONITEUR CAM\xc3\x89RA"), barContent, juce::Justification::centredLeft, false);
        for (int i = 0; i < 3; ++i)
            g.fillEllipse (bar.getRight() - 10.0f - (float) i * 5.0f, bar.getCentreY() - 1.5f, 3.0f, 3.0f);

        // Preview.
        auto preview = bounds;
        auto& camera = session.getCamera();
        const auto image = camera.isOpen() ? camera.getLatestFrame() : juce::Image();

        if (image.isValid())
        {
            g.saveState();
            // Mirrored, like a selfie view: moving your left hand moves it left.
            g.addTransform (juce::AffineTransform::scale (-1.0f, 1.0f, preview.getCentreX(), 0.0f));
            g.drawImage (image, preview, juce::RectanglePlacement::fillDestination);
            g.restoreState();
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d1828), preview.getX(), preview.getY(),
                                                     juce::Colour (0xff0e0c13), preview.getRight(), preview.getBottom(), false));
            g.fillRect (preview);
            g.setGradientFill (juce::ColourGradient (camAccent.withAlpha (0.25f), preview.getX() + preview.getWidth() * 0.32f, preview.getY() + preview.getHeight() * 0.3f,
                                                     camAccent.withAlpha (0.0f), preview.getX() + preview.getWidth() * 0.8f, preview.getBottom(), true));
            g.fillRect (preview);

            // The preview is a dark "screen" in both themes: fixed light text.
            g.setColour (juce::Colour (0xffb9b5ad));
            g.setFont (fonts::semibold (11.0f));
            const auto message = session.isCameraOpening() ? utf8 ("Ouverture de la cam\xc3\xa9ra\xe2\x80\xa6")
                                 : session.getCameraError().isNotEmpty() ? session.getCameraError()
                                 : CameraCapture::isSupported() ? utf8 ("Cam\xc3\xa9ra \xc3\xa9teinte \xe2\x80\x94 clique pour l'activer")
                                                                : utf8 ("Cam\xc3\xa9ra indisponible sur ce syst\xc3\xa8me");
            g.drawFittedText (message, preview.reduced (12.0f).toNearestInt(), juce::Justification::centred, 4);
        }

        // Framing guides.
        const auto guides = preview.reduced (preview.getWidth() * 0.09f, preview.getHeight() * 0.09f);
        juce::Path guidePath;
        guidePath.addRoundedRectangle (guides, 6.0f);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, guidePath, dashes, 2);
        g.setColour (text.withAlpha (0.18f));
        g.fillPath (dashed);

        // REC indicator.
        if (session.isRecordingTake())
        {
            const bool blinkOn = (juce::Time::getMillisecondCounter() / 700) % 2 == 0;
            auto recArea = juce::Rectangle<float> (preview.getX() + 7.0f, preview.getY() + 7.0f, 90.0f, 12.0f);
            g.setColour (rec.withAlpha (blinkOn ? 1.0f : 0.25f));
            g.fillEllipse (recArea.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
            recArea.removeFromLeft (5.0f);
            g.setColour (rec);
            g.setFont (fonts::display (10.0f));
            fonts::drawTabular (g, "REC " + formatDuration (session.getTakeSeconds()), recArea, juce::Justification::centredLeft);
        }

        // Sync pill.
        if (camera.isOpen())
        {
            const auto ms = juce::roundToInt (session.getSyncLagMs());
            const auto label = session.isCalibrated() ? utf8 ("Sync ") + (ms >= 0 ? "+" : "") + juce::String (ms) + " ms"
                                                      : utf8 ("Sync non calibr\xc3\xa9" "e");
            const auto font = fonts::display (9.5f);
            const float w = juce::GlyphArrangement::getStringWidth (font, label) + 14.0f;
            auto pill = juce::Rectangle<float> (preview.getRight() - 7.0f - w, preview.getBottom() - 7.0f - 17.0f, w, 17.0f);
            g.setColour (juce::Colour (0xff0e0c13).withAlpha (0.7f));
            g.fillRoundedRectangle (pill, 8.5f);
            g.setColour (session.isCalibrated() ? camAccent : warn);
            g.setFont (font);
            g.drawText (label, pill, juce::Justification::centred, false);
        }

        g.restoreState();

        g.setColour (camAccent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), radiusMd, 1.0f);
    }

    static int heightFor (int width) { return barHeight + width * 9 / 16; }

    static constexpr int smallWidth = 208;
    static constexpr int largeWidth = 360;
    static constexpr int barHeight = 22;

private:
    Timeline& owner;
    Session& session;
    juce::ComponentDragger dragger;
    juce::ComponentBoundsConstrainer constrainer;
    juce::DropShadower shadower { juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 24, { 0, 10 }) };
    int lastFrame = -1;
};

//==============================================================================
Timeline::Timeline (Session& s) : session (s)
{
    for (auto t : { Track::amp, Track::loop, Track::camera })
    {
        rows.push_back (std::make_unique<TrackRow> (*this, session, t));
        addAndMakeVisible (*rows.back());
    }

    playhead = std::make_unique<PlayheadOverlay> (*this);
    addAndMakeVisible (*playhead);

    monitor = std::make_unique<CameraMonitor> (*this, session);
    addAndMakeVisible (*monitor);
}

Timeline::~Timeline() = default;

void Timeline::setSelectedTrack (Track track)
{
    selected = track;
    for (auto& r : rows)
        r->repaint();
}

float Timeline::getPlayheadProportion() const
{
    const auto& engine = session.getEngine();
    const auto window = timelineWindowSamples (session);
    if (window <= 0)
        return 0.0f;
    return (float) (engine.getTransportPosition() % window) / (float) window;
}

void Timeline::refresh()
{
    for (auto& r : rows)
        r->refreshState();
    playhead->repaint();
    monitor->refresh();
    repaint (0, 0, getWidth(), rulerHeight);
}

void Timeline::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (surfaceSunken);

    auto ruler = getLocalBounds().removeFromTop (rulerHeight).toFloat();
    g.setColour (border);
    g.drawHorizontalLine (rulerHeight - 1, 0.0f, (float) getWidth());

    auto gutter = ruler.removeFromLeft ((float) headWidth);
    g.setColour (textFaint);
    g.setFont (fonts::semibold (9.5f).withExtraKerningFactor (0.06f));
    g.drawText ("MESURES", gutter.reduced (12.0f, 0.0f), juce::Justification::centredLeft, false);

    const double window = (double) timelineWindowSamples (session);
    const double perBar = session.getSamplesPerBar();
    if (window <= 0.0 || perBar <= 0.0)
        return;

    const double pixelsPerBar = ruler.getWidth() * perBar / window;
    int every = 1;
    while (pixelsPerBar * every < 30.0)
        every *= 2;

    g.setFont (fonts::medium (10.5f));
    for (int bar = 0; bar * perBar < window; ++bar)
    {
        const float x = ruler.getX() + (float) (bar * pixelsPerBar);
        if (bar % every == 0)
        {
            g.setColour (border);
            g.drawVerticalLine ((int) x, ruler.getY(), ruler.getBottom());
            g.setColour (textFaint);
            fonts::drawTabular (g, juce::String (bar + 1), { x + 4.0f, ruler.getY() + 8.0f, 30.0f, ruler.getHeight() - 8.0f },
                                juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (border);
            g.drawVerticalLine ((int) x, ruler.getBottom() - 6.0f, ruler.getBottom());
        }
    }

    // Loop bracket across the first pass.
    const auto& looper = session.getEngine().getLooper();
    const double length = looper.getRecordedLengthSamples();
    if (length > 0.0 && looper.getState() != Looper::State::Recording)
    {
        const float width = (float) (ruler.getWidth() * length / window);
        auto bracket = juce::Rectangle<float> (ruler.getX() + 4.0f, ruler.getY() + 2.0f, width - 8.0f, 9.0f);
        const auto label = "boucle " + formatBars (length / perBar) + " mes.";
        const auto font = fonts::display (9.5f);
        const float labelWidth = juce::GlyphArrangement::getStringWidth (font, label);
        g.setColour (loopAccent);
        g.setFont (font);
        g.drawText (label, bracket.withWidth (labelWidth + 2.0f), juce::Justification::centredLeft, false);
        g.setColour (loopAccent.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> (bracket.getX() + labelWidth + 6.0f, bracket.getCentreY(), std::max (0.0f, bracket.getWidth() - labelWidth - 6.0f), 1.0f));
    }
}

void Timeline::resized()
{
    auto area = getLocalBounds().withTrimmedTop (rulerHeight);
    const int rowHeight = juce::jlimit (66, 110, area.getHeight() / (int) rows.size());
    for (auto& r : rows)
        r->setBounds (area.removeFromTop (rowHeight));

    playhead->setBounds (getLocalBounds());

    if (! monitorPlaced && getWidth() > 0)
    {
        monitor->setTopLeftPosition (getWidth() - monitor->getWidth() - 16, getHeight() - monitor->getHeight() - 16);
        monitorPlaced = true;
    }
    monitor->keepInside();
    monitor->toFront (false);
}
