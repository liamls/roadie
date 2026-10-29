#include "Timeline.h"

using namespace roadie;
using Track = Session::Track;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

// Top to bottom: the video band, then the audio tracks (A1, A2, A3).
constexpr Track allTracks[] = { Track::camera, Track::amp, Track::loop, Track::song };

juce::Colour accentOf (Track t)
{
    switch (t)
    {
        case Track::amp:    return theme::ampAccent;
        case Track::loop:   return theme::loopAccent;
        case Track::camera: return theme::camAccent;
        case Track::song:   return theme::songAccent;
        default:            return theme::textMuted;
    }
}

icons::Icon iconOf (Track t)
{
    switch (t)
    {
        case Track::amp:    return icons::Icon::guitar;
        case Track::loop:   return icons::Icon::loop;
        case Track::camera: return icons::Icon::camera;
        case Track::song:   return icons::Icon::song;
        default:            return icons::Icon::guitar;
    }
}

// Track::amp is the "Guitare" track (the amp and its take regions); the amp
// controls themselves are still labelled "Ampli" in the Inspector.
juce::String nameOf (Track t)
{
    switch (t)
    {
        case Track::amp:    return "Guitare";
        case Track::loop:   return "Looper";
        case Track::camera: return utf8 ("Vid\xc3\xa9o");
        case Track::song:   return "Morceau";
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

// Peak (linear) → 0..1 display height on a 54 dB scale, so quiet and loud
// takes both read clearly without filling the region.
float displayHeight (float peak)
{
    return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 48.0f) / 54.0f);
}

// Symmetric waveform over `area`, one column every 2 px; `peakAt (x0, x1)`
// returns the peak of what lies under that pixel span. Only the columns
// inside `visible` are built.
void drawWaveform (juce::Graphics& g, juce::Rectangle<float> area, juce::Rectangle<float> visible,
                   const std::function<float (float, float)>& peakAt, juce::Colour colour)
{
    const float from = std::max (area.getX(), visible.getX());
    const float to = std::min (area.getRight(), visible.getRight());
    if (to - from < 2.0f || area.getHeight() < 4.0f)
        return;

    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.5f;
    std::vector<float> heights;
    for (float x = from; x < to; x += 2.0f)
        heights.push_back (std::max (0.03f, displayHeight (peakAt (x, std::min (to, x + 2.0f)))));

    juce::Path p;
    p.startNewSubPath (from, mid - heights[0] * half);
    for (size_t i = 1; i < heights.size(); ++i)
        p.lineTo (from + 2.0f * (float) i, mid - heights[i] * half);
    for (size_t i = heights.size(); i-- > 0;)
        p.lineTo (from + 2.0f * (float) i, mid + heights[i] * half);
    p.closeSubPath();

    g.setColour (colour.withMultipliedAlpha (0.55f));
    g.fillPath (p);
}

constexpr float regionRadius = 5.0f;
constexpr float titleStrip = 15.0f;

// A region the GarageBand way: tinted body, a coloured title strip with the
// name, a border that lights up on hover (`hover` fades from 0 to 1) and
// turns solid when selected.
void drawRegionFrame (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, const juce::String& name,
                      bool selected, float hover, bool dim)
{
    const float alpha = dim ? 0.45f : 1.0f;
    juce::Path shape;
    shape.addPath (roadie::theme::squircle (r, regionRadius));

    g.setColour (colour.withAlpha ((selected ? 0.26f : 0.15f + 0.05f * hover) * alpha));
    g.fillPath (shape);

    g.saveState();
    g.reduceClipRegion (shape);
    g.setColour (colour.withAlpha ((selected ? 0.9f : 0.32f + 0.13f * hover) * alpha));
    g.fillRect (r.withHeight (titleStrip));
    g.restoreState();

    g.setColour (colour.withAlpha ((selected ? 1.0f : 0.45f + 0.3f * hover) * alpha));
    roadie::theme::drawSquircle (g, r.reduced (0.5f), regionRadius, selected ? 1.5f : 1.0f);

    if (name.isNotEmpty() && r.getWidth() > 18.0f)
    {
        g.setColour (selected && ! dim ? theme::onAccent : theme::text.withAlpha (0.85f * alpha));
        g.setFont (fonts::semibold (10.0f));
        g.drawText (name, r.withHeight (titleStrip).reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

// Lit bar on the edge the mouse can trim.
void drawTrimHandle (juce::Graphics& g, juce::Rectangle<float> r, bool left, juce::Colour colour)
{
    const auto bar = left ? r.withWidth (3.0f).reduced (0.0f, 3.0f).translated (1.0f, 0.0f)
                          : r.withLeft (r.getRight() - 3.0f).reduced (0.0f, 3.0f).translated (-1.0f, 0.0f);
    g.setColour (colour.brighter (0.3f));
    roadie::theme::fillSquircle (g, bar, 1.5f);
}

// Chip on a dark pill, readable over thumbnails and grid alike.
void drawHint (juce::Graphics& g, juce::Rectangle<float> lane, const juce::String& text, juce::Colour colour)
{
    const auto font = fonts::semibold (10.5f);
    const float w = std::min (lane.getWidth() - 20.0f, juce::GlyphArrangement::getStringWidth (font, text) + 18.0f);
    auto pill = juce::Rectangle<float> (lane.getX() + 10.0f, lane.getCentreY() - 10.0f, w, 20.0f);
    g.setColour (theme::surface.withAlpha (0.9f));
    roadie::theme::fillSquircle (g, pill, 10.0f);
    g.setColour (colour.withAlpha (0.4f));
    roadie::theme::drawSquircle (g, pill.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (theme::ink (colour));
    g.setFont (font);
    g.drawText (text, pill.reduced (9.0f, 0.0f), juce::Justification::centredLeft, true);
}
} // namespace

//==============================================================================
// Record-arm toggle of a track header: a red dot, lit when armed.
class ArmButton : public juce::Button
{
public:
    ArmButton() : juce::Button ("Arm")
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void paintButton (juce::Graphics& g, bool isOver, bool) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        g.setColour (on ? theme::rec.withAlpha (0.22f) : theme::text.withAlpha (isOver ? 0.1f : 0.05f));
        roadie::theme::fillSquircle (g, bounds, 3.0f);
        g.setColour (on ? theme::rec : theme::border);
        roadie::theme::drawSquircle (g, bounds.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (on ? theme::rec : theme::textFaint);
        g.fillEllipse (bounds.withSizeKeepingCentre (7.0f, 7.0f));
    }
};

//==============================================================================
class Timeline::TrackHeader : public juce::Component
{
public:
    TrackHeader (Timeline& o, Session& s, Track t)
        : track (t), owner (o), session (s),
          muteButton (false, accentOf (t)), soloButton (true, accentOf (t)), fader (accentOf (t), roadie::ui::Fader::Orientation::horizontal)
    {
        muteButton.onClick = [this] { session.setMuted (track, muteButton.getToggleState()); };
        soloButton.onClick = [this] { session.setSolo (track, soloButton.getToggleState()); };
        armButton.onClick = [this] { session.setArmed (track, armButton.getToggleState()); };

        // Guitare and Vidéo record: they arm (●); the video band has nothing else.
        if (track == Track::amp || track == Track::camera)
        {
            armButton.setTooltip (track == Track::amp ? utf8 ("Armer : \xe2\x97\x8f enregistre une prise de guitare")
                                                      : utf8 ("Armer : \xe2\x97\x8f filme aussi (d\xc3\xa9sarm\xc3\xa9" "e : audio seul)"));
            addAndMakeVisible (armButton);
        }

        if (track != Track::camera)
        {
            addAndMakeVisible (muteButton);
            addAndMakeVisible (soloButton);
            fader.onChange = [this] (float db) { session.setTrackVolume (track, db); };
            addAndMakeVisible (fader);
            addAndMakeVisible (meter);
        }

        setRepaintsOnMouseActivity (true);
    }

    void refreshState()
    {
        muteButton.setToggleState (session.isMuted (track), juce::dontSendNotification);
        soloButton.setToggleState (session.isSolo (track), juce::dontSendNotification);
        armButton.setToggleState (session.isArmed (track), juce::dontSendNotification);
        if (track != Track::camera)
        {
            if (! fader.isMouseButtonDown())
                fader.setValue (session.getTrackVolume (track));
            auto& engine = session.getEngine();
            meter.setLevel (track == Track::amp ? engine.getGuitarPeak() : (track == Track::loop ? engine.getLoopPeak() : engine.getSongPeak()));
        }

        const auto text = subtitle();
        if (text != shownSubtitle)
        {
            shownSubtitle = text;
            repaint();
        }
    }

    void resized() override
    {
        auto content = getLocalBounds().reduced (12, 0).withTrimmedLeft (3);
        const bool roomy = getHeight() >= 64 && track != Track::camera;
        auto top = roomy ? content.removeFromTop (getHeight() / 2 + 6).withTrimmedTop (8) : content;

        // [●] M S from the right edge inwards.
        const int toggleCount = (track == Track::amp ? 3 : (track == Track::camera ? 1 : 2));
        auto toggles = top.removeFromRight (toggleCount * 20 - 2).withSizeKeepingCentre (toggleCount * 20 - 2, 16);
        if (track == Track::amp || track == Track::camera)
        {
            armButton.setBounds (toggles.removeFromLeft (18));
            toggles.removeFromLeft (2);
        }
        muteButton.setBounds (toggles.removeFromLeft (18));
        toggles.removeFromLeft (2);
        soloButton.setBounds (toggles.removeFromLeft (18));
        labels = top.withTrimmedLeft (34).withTrimmedRight (6);
        const int iconSize = getHeight() < 40 ? 18 : 26;
        iconArea = top.withWidth (26).withSizeKeepingCentre (iconSize, iconSize);

        fader.setVisible (roomy);
        meter.setVisible (roomy);
        if (roomy)
        {
            auto bottom = content.withTrimmedBottom (8);
            fader.setBounds (bottom.removeFromTop (18).withTrimmedLeft (26));
            meter.setBounds (bottom.removeFromTop (5).withTrimmedLeft (33).withTrimmedRight (5));
        }
    }

    // The bottom edge sets the track's height (double-click: default).
    bool onBottomEdge (const juce::MouseEvent& e) const { return e.y >= getHeight() - 5; }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (onBottomEdge (e) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        resizing = onBottomEdge (e);
        heightAtDragStart = owner.getTrackHeight (track);
        if (resizing)
            return;
        owner.setSelectedTrack (track);
        if (owner.onTrackSelected)
            owner.onTrackSelected (track);

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            auto* timeline = &owner;
            auto* s = &session;
            if (track == Track::loop)
            {
                menu.addItem ("Effacer la boucle", s->hasLoop(), false, [s] { s->clearLoop(); });
                menu.addItem ("Supprimer la piste Looper", true, false, [timeline] { timeline->confirmRemoveLooperTrack(); });
            }
            else if (track == Track::song)
            {
                menu.addItem (utf8 ("Choisir un morceau\xe2\x80\xa6"), ! s->isImportingSong(), false, [timeline] { if (timeline->onImportSong) timeline->onImportSong(); });
                menu.addItem (utf8 ("Importer depuis YouTube\xe2\x80\xa6"), ! s->isImportingSong() && ! s->isDownloadingSong(), false,
                              [timeline] { if (timeline->onImportYouTube) timeline->onImportYouTube(); });
                menu.addItem ("Retirer le morceau", s->hasSong(), false, [s] { s->removeSong(); });
            }
            else
            {
                return;
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (resizing)
            owner.setTrackHeight (track, heightAtDragStart + e.getDistanceFromDragStartY());
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (resizing && owner.onTrackHeightsChanged)
            owner.onTrackHeightsChanged();
        resizing = false;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! onBottomEdge (e))
            return;
        owner.setTrackHeight (track, Timeline::defaultTrackHeight (track));
        if (owner.onTrackHeightsChanged)
            owner.onTrackHeightsChanged();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        auto area = getLocalBounds().toFloat();
        const bool active = owner.getSelectedTrack() == track;
        const auto accent = accentOf (track);

        const float hover = fade.get (isMouseOver (true));
        g.setColour (active ? surfaceRaised : surface.interpolatedWith (surfaceRaised, 0.45f * hover));
        g.fillRect (area);
        if (active)
        {
            g.setColour (accent);
            g.fillRect (area.withWidth (3.0f));
        }
        g.setColour (border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, area.getRight());

        auto box = iconArea.toFloat();
        g.setColour (accent.withAlpha (active ? 0.24f : 0.15f));
        roadie::theme::fillSquircle (g, box, 7.0f);
        icons::draw (g, iconOf (track), box.reduced (6.0f), theme::ink (accent), 1.9f);

        g.setColour (session.isMuted (track) ? textMuted : theme::text);
        g.setFont (fonts::semibold (12.5f));
        if (getHeight() < 40) // thin strip: the name alone
        {
            g.drawText (nameOf (track), labels.toFloat(), juce::Justification::centredLeft, true);
            return;
        }
        auto textArea = labels.toFloat().withSizeKeepingCentre ((float) labels.getWidth(), 30.0f);
        g.drawText (nameOf (track), textArea.removeFromTop (16.0f), juce::Justification::bottomLeft, true);
        g.setColour (textFaint);
        g.setFont (fonts::medium (10.5f));
        g.drawText (shownSubtitle, textArea, juce::Justification::topLeft, true);
    }

    const Track track;

private:
    juce::String subtitle() const
    {
        const auto& engine = session.getEngine();
        switch (track)
        {
            case Track::amp:
                if (session.isRecordingTake())
                    return utf8 ("\xe2\x97\x8f Prise ") + formatDuration (session.getTakeSeconds());
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
                    case Looper::State::Stopped:
                    case Looper::State::Playing:
                    default:
                    {
                        const int layers = looper.getLayerCount();
                        return juce::String (layers) + (layers > 1 ? " couches" : " couche") + utf8 (" \xc2\xb7 ") + formatBars (bars) + " mes.";
                    }
                }
            }

            case Track::song:
                if (session.isImportingSong())
                    return utf8 ("Analyse\xe2\x80\xa6");
                if (! session.hasSong())
                    return utf8 ("Aucun \xe2\x80\x94 glisse un MP3 ici");
                return juce::String (session.getSong().bpm, 1).replaceCharacter ('.', ',') + utf8 (" BPM \xc2\xb7 ") + session.getSong().title;

            case Track::camera:
            default:
                if (session.isCameraSuspended())
                    return utf8 ("Coup\xc3\xa9" "e en Monter");
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

    Timeline& owner;
    Session& session;
    roadie::ui::MuteSoloButton muteButton, soloButton;
    ArmButton armButton;
    roadie::ui::Fader fader;
    roadie::ui::Meter meter;
    juce::Rectangle<int> labels, iconArea;
    juce::String shownSubtitle;
    bool resizing = false;
    int heightAtDragStart = 0;
    roadie::ui::HoverFade fade { *this };
};

//==============================================================================
// Master, under the tracks: the whole output, its fader and a stereo meter.
class Timeline::MasterHeader : public juce::Component
{
public:
    explicit MasterHeader (Session& s)
        : session (s), fader (theme::text.withAlpha (0.7f), roadie::ui::Fader::Orientation::horizontal)
    {
        fader.onChange = [this] (float db) { session.setMasterVolume (db); };
        addAndMakeVisible (fader);
        addAndMakeVisible (left);
        addAndMakeVisible (right);
        left.setTooltip (utf8 ("Sortie gauche \xe2\x80\x94 un clic \xc3\xa9teint le t\xc3\xa9moin de saturation"));
        right.setTooltip (utf8 ("Sortie droite \xe2\x80\x94 un clic \xc3\xa9teint le t\xc3\xa9moin de saturation"));
    }

    void refresh()
    {
        if (! fader.isMouseButtonDown())
            fader.setValue (session.getMasterVolume());
        left.setLevel (session.getEngine().getOutputPeakLeft());
        right.setLevel (session.getEngine().getOutputPeakRight());
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (surface);
        g.setColour (border);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
        g.setColour (theme::text);
        g.setFont (fonts::semibold (12.5f));
        g.drawText ("Master", nameArea, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (15, 0);
        nameArea = area.removeFromLeft (56);
        auto meters = area.removeFromRight (std::min (64, area.getWidth() / 3)).withSizeKeepingCentre (std::min (64, area.getWidth() / 3), 11);
        left.setBounds (meters.removeFromTop (5));
        meters.removeFromTop (1);
        right.setBounds (meters.removeFromTop (5));
        area.removeFromRight (8);
        fader.setBounds (area.withSizeKeepingCentre (area.getWidth(), 18));
    }

    static constexpr int preferredHeight = 40;

private:
    Session& session;
    roadie::ui::Fader fader;
    roadie::ui::Meter left, right;
    juce::Rectangle<int> nameArea;
};

//==============================================================================
// Top-left corner, above the headers: − [zoom] +.
class Timeline::ZoomControl : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    explicit ZoomControl (Timeline& o) : owner (o)
    {
        setTooltip (utf8 ("Zoom horizontal \xe2\x80\x94 \xe2\x8c\x98\xe2\x86\x90 / \xe2\x8c\x98\xe2\x86\x92, ou pincer le trackpad"));
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const auto minus = area.removeFromLeft (14.0f);
        const auto plus = area.removeFromRight (14.0f);
        const auto colour = theme::textFaint.interpolatedWith (theme::textMuted, fade.get (isMouseOverOrDragging()));

        g.setColour (colour);
        g.fillRect (juce::Rectangle<float> (7.0f, 1.5f).withCentre (minus.getCentre()));
        g.fillRect (juce::Rectangle<float> (7.0f, 1.5f).withCentre (plus.getCentre()));
        g.fillRect (juce::Rectangle<float> (1.5f, 7.0f).withCentre (plus.getCentre()));

        track = area.reduced (6.0f, 0.0f);
        const float y = track.getCentreY();
        g.setColour (theme::border);
        roadie::theme::fillSquircle (g, track.withHeight (3.0f).withCentre ({ track.getCentreX(), y }), 1.5f);
        const float x = track.getX() + proportion() * track.getWidth();
        g.setColour (colour);
        g.fillEllipse (x - 4.5f, y - 4.5f, 9.0f, 9.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.position.x < 14.0f)
            owner.zoomBy (1.0 / 1.5);
        else if (e.position.x > (float) getWidth() - 14.0f)
            owner.zoomBy (1.5);
        else
            dragTo (e.position.x);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mouseDownPosition.x >= 14.0f && e.mouseDownPosition.x <= (float) getWidth() - 14.0f)
            dragTo (e.position.x);
    }

private:
    static double logMin() { return std::log (minPixelsPerSecond); }
    static double logMax() { return std::log (maxPixelsPerSecond); }
    float proportion() const { return (float) ((std::log (owner.pixelsPerSecond) - logMin()) / (logMax() - logMin())); }

    void dragTo (float x)
    {
        const double p = juce::jlimit (0.0, 1.0, (double) ((x - track.getX()) / std::max (1.0f, track.getWidth())));
        owner.zoomBy (std::exp (logMin() + p * (logMax() - logMin())) / owner.pixelsPerSecond);
    }

    Timeline& owner;
    juce::Rectangle<float> track;
    roadie::ui::HoverFade fade { *this };
};

//==============================================================================
// Bar ruler: click or drag to move the playhead (snapped to the beat, ⌘ for
// free), drag in the top band to draw or move the cycle zone, click the zone
// to toggle it.
class Timeline::Ruler : public juce::Component
{
public:
    Ruler (Timeline& o, Session& s) : owner (o), session (s) {}

    static constexpr float bandHeight = 11.0f;

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        auto bounds = getLocalBounds().toFloat();
        g.setColour (surface);
        g.fillRect (bounds);
        g.setColour (border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, bounds.getRight());

        // Cycle band.
        const auto cycle = session.getCycleRange();
        const bool hoverBand = hover.y >= 0.0f && hover.y < bandHeight + 2.0f;
        if (cycle.getLength() > 0.01)
        {
            const float x0 = owner.timeToX (cycle.getStart()), x1 = owner.timeToX (cycle.getEnd());
            auto band = juce::Rectangle<float> (x0, 2.0f, x1 - x0, bandHeight);
            const bool on = session.isCycleOn();
            g.setColour (exportAccent.withAlpha (on ? (hoverBand ? 1.0f : 0.85f) : (hoverBand ? 0.4f : 0.25f)));
            roadie::theme::fillSquircle (g, band, 3.0f);
        }
        else if (hoverBand)
        {
            g.setColour (textFaint.withAlpha (0.6f));
            g.setFont (fonts::medium (9.5f));
            g.drawText (utf8 ("Glisse ici pour cr\xc3\xa9" "er une zone de cycle"), juce::Rectangle<float> (hover.x + 8.0f, 0.0f, 220.0f, bandHeight + 4.0f),
                        juce::Justification::centredLeft, false);
        }

        // Bars and beats.
        const double bar = session.getSecondsPerBar();
        const double beat = session.getSecondsPerBeat();
        const double pps = owner.pixelsPerSecond;
        int every = 1;
        while (bar * pps * every < 34.0)
            every *= 2;
        const bool beats = beat * pps >= 9.0;

        const double t0 = owner.xToTime (0.0f), t1 = owner.xToTime (bounds.getWidth());
        g.setFont (fonts::medium (10.5f));
        for (int b = std::max (0, (int) std::floor (t0 / bar)); b * bar <= t1; ++b)
        {
            const float x = owner.timeToX (b * bar);
            if (b % every == 0)
            {
                g.setColour (border.brighter (0.3f));
                g.drawVerticalLine ((int) x, bandHeight + 4.0f, bounds.getBottom());
                g.setColour (textMuted);
                fonts::drawTabular (g, juce::String (b + 1), { x + 4.0f, bandHeight + 3.0f, 34.0f, bounds.getHeight() - bandHeight - 4.0f },
                                    juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (border.brighter (0.2f));
                g.drawVerticalLine ((int) x, bounds.getBottom() - 7.0f, bounds.getBottom());
            }

            if (beats)
            {
                g.setColour (border);
                for (int k = 1; k < session.getBeatsPerBar(); ++k)
                    g.drawVerticalLine ((int) owner.timeToX (b * bar + k * beat), bounds.getBottom() - 4.0f, bounds.getBottom());
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hover = e.position;
        owner.setHoverX (e.position.x);
        updateCursor (e.position);
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = { -1.0f, -1.0f };
        owner.setHoverX (-1.0f);
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        moved = false;
        downTime = owner.xToTime (e.position.x);
        origin = session.getCycleRange();
        mode = Mode::none;

        if (e.position.y < bandHeight + 3.0f)
        {
            const bool hasRange = origin.getLength() > 0.05;
            const float x0 = owner.timeToX (origin.getStart()), x1 = owner.timeToX (origin.getEnd());
            if (hasRange && std::abs (e.position.x - x0) < 6.0f)      mode = Mode::cycleLeft;
            else if (hasRange && std::abs (e.position.x - x1) < 6.0f) mode = Mode::cycleRight;
            else if (hasRange && e.position.x > x0 && e.position.x < x1) mode = Mode::cycleMove;
            else                                                       mode = Mode::cycleCreate;
        }
        else
        {
            mode = Mode::scrub;
            session.locate (owner.snapPlayhead (downTime, e.mods.isCommandDown()));
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 2)
            moved = true;
        if (! moved)
            return;

        const bool snapping = ! e.mods.isCommandDown();
        const double t = owner.xToTime (e.position.x);
        const double step = owner.getGridStep();
        const double dt = t - downTime;
        hover = e.position;
        owner.setHoverX (e.position.x);

        switch (mode)
        {
            case Mode::scrub:
                session.locate (owner.snapPlayhead (t, ! snapping));
                break;
            case Mode::cycleCreate:
            {
                const double a = owner.snap (downTime, snapping), b = owner.snap (t, snapping);
                if (std::abs (b - a) > 0.01)
                    session.setCycleRange (std::min (a, b), std::max (a, b));
                break;
            }
            case Mode::cycleMove:
            {
                const double start = std::max (0.0, owner.snap (origin.getStart() + dt, snapping));
                session.setCycleRange (start, start + origin.getLength());
                break;
            }
            case Mode::cycleLeft:
                session.setCycleRange (std::min (owner.snap (origin.getStart() + dt, snapping), origin.getEnd() - step), origin.getEnd());
                break;
            case Mode::cycleRight:
                session.setCycleRange (origin.getStart(), std::max (owner.snap (origin.getEnd() + dt, snapping), origin.getStart() + step));
                break;
            case Mode::none:
                break;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        const bool cycleMode = mode != Mode::none && mode != Mode::scrub;
        if (cycleMode && ! moved && mode == Mode::cycleMove)
            session.toggleCycle();
        else if (cycleMode && moved && ! session.isCycleOn() && session.getCycleRange().getLength() > 0.05)
            session.setCycleOn (true); // drawing a zone turns it on, as in GarageBand
        mode = Mode::none;
        repaint();
    }

private:
    enum class Mode { none, scrub, cycleCreate, cycleMove, cycleLeft, cycleRight };

    void updateCursor (juce::Point<float> p)
    {
        auto cursor = juce::MouseCursor::NormalCursor;
        if (p.y < bandHeight + 3.0f)
        {
            const auto cycle = session.getCycleRange();
            const float x0 = owner.timeToX (cycle.getStart()), x1 = owner.timeToX (cycle.getEnd());
            if (cycle.getLength() > 0.05 && (std::abs (p.x - x0) < 6.0f || std::abs (p.x - x1) < 6.0f))
                cursor = juce::MouseCursor::LeftRightResizeCursor;
            else if (cycle.getLength() > 0.05 && p.x > x0 && p.x < x1)
                cursor = juce::MouseCursor::PointingHandCursor;
            else
                cursor = juce::MouseCursor::CrosshairCursor;
        }
        else
        {
            cursor = juce::MouseCursor::PointingHandCursor;
        }
        setMouseCursor (cursor);
    }

    Timeline& owner;
    Session& session;
    Mode mode = Mode::none;
    bool moved = false;
    double downTime = 0.0;
    juce::Range<double> origin;
    juce::Point<float> hover { -1.0f, -1.0f };
};

//==============================================================================
// The lanes: grid, regions, and the mouse editing of the arrangement. While
// a region is dragged, the edited copy is drawn (and the engine keeps
// playing the original); the edit is applied once, on mouse up, so a whole
// drag is one ⌘Z.
class Timeline::ArrangeView : public juce::Component
{
public:
    ArrangeView (Timeline& o, Session& s) : owner (o), session (s) {}

    // Cheap fingerprint of what the lanes show, to skip repaints when idle.
    std::vector<double> signature()
    {
        const auto& a = session.getArrangement();
        const auto& looper = session.getEngine().getLooper();
        std::vector<double> sig { (double) a.clips.size(), a.loopAnchor, a.loopStart, a.loopEnd,
                                  (double) session.getSelection(), (double) session.getSelectedClip(),
                                  (double) session.getTakes().size(), (double) looper.getRecordedLengthSamples(),
                                  (double) looper.getLayerCount(), (double) looper.getState(),
                                  session.isCycleOn() ? 1.0 : 0.0, session.getCycleRange().getStart(), session.getCycleRange().getEnd(),
                                  session.getCamera().isOpen() ? 1.0 : 0.0,
                                  a.songStart, session.getSong().bpm, (double) session.getSong().sections.size(), session.getSongSpeed(),
                                  (double) session.getSong().semitones, session.isRenderingSong() ? 1.0 : 0.0,
                                  session.getSongMedia() != nullptr ? 1.0 : 0.0, session.isImportingSong() ? 1.0 : 0.0 };
        for (const auto& c : a.clips)
            sig.insert (sig.end(), { (double) c.take, c.start, c.sourceStart, c.length });
        for (auto t : allTracks)
            sig.push_back (session.isMuted (t) ? 1.0 : 0.0);
        return sig;
    }

    bool isLive() const
    {
        const auto state = session.getEngine().getLooper().getState();
        return session.isRecordingTake() || state == Looper::State::Recording || state == Looper::State::Overdubbing;
    }

    bool isDragging() const noexcept { return drag.active; }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (surfaceSunken);
        g.fillRect (bounds);

        const auto& arrangement = drag.active && drag.moved ? drag.edited : session.getArrangement();

        for (auto t : allTracks)
        {
            if (owner.getSelectedTrack() == t)
            {
                g.setColour (text.withAlpha (0.025f));
                g.fillRect (laneBounds (t));
            }
        }

        paintGrid (g, bounds);

        if (session.isCycleOn())
        {
            const auto cycle = session.getCycleRange();
            const float x0 = owner.timeToX (cycle.getStart()), x1 = owner.timeToX (cycle.getEnd());
            g.setColour (exportAccent.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (x0, 0.0f, x1 - x0, (float) owner.lanesBottom()));
        }

        for (auto t : allTracks)
        {
            g.setColour (border);
            g.drawHorizontalLine ((int) laneBounds (t).getBottom() - 1, 0.0f, bounds.getRight());
        }

        paintCamera (g, arrangement);
        paintGuitar (g, arrangement);
        paintLoop (g, arrangement);
        paintSong (g, arrangement);
    }

    //==============================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        owner.setHoverX (e.position.x);
        const auto hit = hitTestAt (e.position);
        if (hit != hover)
        {
            setHover (hit);
            repaint();
        }
        setMouseCursor (hit.kind == Kind::section ? juce::MouseCursor::PointingHandCursor
                        : hit.zone == Zone::body || hit.kind == Kind::none ? juce::MouseCursor::NormalCursor
                                                                           : juce::MouseCursor::LeftRightResizeCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        owner.setHoverX (-1.0f);
        if (hover.kind != Kind::none)
        {
            setHover ({});
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto lane = laneAt (e.position.y);
        const auto hit = hitTestAt (e.position);
        drag = {};

        if (lane.has_value())
        {
            owner.setSelectedTrack (*lane);
            if (owner.onTrackSelected)
                owner.onTrackSelected (*lane);
        }

        if (hit.kind == Kind::clip)
            session.selectClip (hit.index);
        else if (hit.kind == Kind::loop)
            session.selectLoopRegion();
        else
            session.clearSelection();

        if (e.mods.isPopupMenu())
        {
            showMenu (hit, lane, owner.snap (owner.xToTime (e.position.x), true));
            return;
        }

        // A part of the song: cycle over it (again: cycle off).
        if (hit.kind == Kind::section)
        {
            session.cycleSection (hit.index);
            return;
        }

        // Any other click in the lanes puts the playhead there (a region is
        // selected and dragged all the same).
        if (! session.isRecordingTake())
            session.locate (owner.snapPlayhead (owner.xToTime (e.position.x), e.mods.isCommandDown()));

        if (hit.kind == Kind::none)
        {
            if (lane == Track::camera && ! session.getCamera().isOpen() && owner.onCameraClicked)
                owner.onCameraClicked();
            if (lane == Track::song && ! session.hasSong() && ! session.isImportingSong() && owner.onImportSong)
                owner.onImportSong();
            return;
        }

        drag.active = true;
        drag.hit = hit;
        drag.origin = session.getArrangement();
        drag.edited = drag.origin;
        drag.downTime = owner.xToTime (e.position.x);
        drag.duplicate = hit.kind == Kind::clip && hit.zone == Zone::body && e.mods.isAltDown();
        drag.loopVisibleEnd = loopDisplayEnd (drag.origin);
        if (hit.zone == Zone::body)
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        owner.setHoverX (e.position.x);
        if (! drag.active)
            return;
        if (! drag.moved && e.getDistanceFromDragStart() < 3)
            return;
        drag.moved = true;

        const bool snapping = ! e.mods.isCommandDown();
        const double dt = owner.xToTime (e.position.x) - drag.downTime;
        const double minimum = 0.05;
        auto edited = drag.origin;

        if (drag.hit.kind == Kind::clip)
        {
            const auto& orig = drag.origin.clips[(size_t) drag.hit.index];
            auto clip = orig;
            const double takeLength = session.getTakeLength (orig.take);

            switch (drag.hit.zone)
            {
                case Zone::body:
                    clip.start = std::max (0.0, owner.snap (orig.start + dt, snapping));
                    break;
                case Zone::left:
                {
                    double start = owner.snap (orig.start + dt, snapping);
                    start = juce::jlimit (std::max (0.0, orig.start - orig.sourceStart), orig.end() - minimum, start);
                    clip.sourceStart = orig.sourceStart + (start - orig.start);
                    clip.length = orig.end() - start;
                    clip.start = start;
                    break;
                }
                case Zone::right:
                {
                    double end = owner.snap (orig.end() + dt, snapping);
                    const double maxEnd = takeLength > 0.0 ? orig.start + (takeLength - orig.sourceStart) : orig.end();
                    end = juce::jlimit (orig.start + minimum, std::max (orig.start + minimum, maxEnd), end);
                    clip.length = end - orig.start;
                    break;
                }
            }

            if (drag.duplicate)
                edited.clips.push_back (clip);
            else
                edited.clips[(size_t) drag.hit.index] = clip;
        }
        else if (drag.hit.kind == Kind::song)
        {
            // The grid catches the song's first bar, not its first sample.
            const double downbeat = session.getSongDownbeat();
            edited.songStart = owner.snap (drag.origin.songStart + downbeat + dt, snapping) - downbeat;
        }
        else if (drag.hit.kind == Kind::loop)
        {
            const auto& o = drag.origin;
            switch (drag.hit.zone)
            {
                case Zone::body:
                {
                    const double delta = std::max (-o.loopStart, owner.snap (o.loopStart + dt, snapping) - o.loopStart);
                    edited.loopAnchor = o.loopAnchor + delta;
                    edited.loopStart = o.loopStart + delta;
                    if (! o.isLoopOpenEnded())
                        edited.loopEnd = o.loopEnd + delta;
                    break;
                }
                case Zone::left:
                {
                    const double limit = (o.isLoopOpenEnded() ? drag.loopVisibleEnd : o.loopEnd) - minimum;
                    edited.loopStart = juce::jlimit (0.0, limit, owner.snap (o.loopStart + dt, snapping));
                    break;
                }
                case Zone::right:
                    edited.loopEnd = std::max (o.loopStart + minimum, owner.snap (drag.loopVisibleEnd + dt, snapping));
                    break;
            }
        }

        drag.edited = edited;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag.active && drag.moved && drag.edited != drag.origin)
        {
            const bool clip = drag.hit.kind == Kind::clip;
            const auto name = drag.hit.kind == Kind::song ? utf8 ("D\xc3\xa9placer le morceau")
                              : drag.duplicate ? utf8 ("Dupliquer la r\xc3\xa9gion")
                              : drag.hit.zone == Zone::body ? (clip ? utf8 ("D\xc3\xa9placer la r\xc3\xa9gion") : utf8 ("D\xc3\xa9placer la boucle"))
                                                            : (clip ? utf8 ("Rogner la r\xc3\xa9gion") : utf8 ("Rogner la boucle"));
            const auto edited = drag.edited;
            const bool duplicate = drag.duplicate;
            drag = {};
            session.applyEdit (edited, name);
            if (duplicate)
                session.selectClip ((int) edited.clips.size() - 1);
        }
        drag = {};
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (hitTestAt (e.position).kind == Kind::none)
            session.locate (owner.snapPlayhead (owner.xToTime (e.position.x), e.mods.isCommandDown()));
    }

private:
    enum class Kind { none, clip, loop, song, section };
    enum class Zone { body, left, right };

    struct Hit
    {
        Kind kind = Kind::none;
        int index = -1;
        Zone zone = Zone::body;
        bool operator== (const Hit& o) const { return kind == o.kind && index == o.index && zone == o.zone; }
        bool operator!= (const Hit& o) const { return ! (*this == o); }
    };

    juce::Rectangle<float> laneBounds (Track t) const
    {
        return { 0.0f, (float) owner.rowTop (t), (float) getWidth(), (float) owner.rowHeightOf (t) };
    }

    std::optional<Track> laneAt (float y) const
    {
        for (auto t : allTracks)
            if (laneBounds (t).contains (1.0f, y))
                return t;
        return std::nullopt;
    }

    juce::Rectangle<float> regionBounds (Track t, double start, double end) const
    {
        const auto lane = laneBounds (t).reduced (0.0f, 4.0f);
        const float x0 = owner.timeToX (start), x1 = owner.timeToX (end);
        return { x0, lane.getY(), std::max (2.0f, x1 - x0), lane.getHeight() };
    }

    // Right end of the loop region: its end, or the right of the view (plus
    // a margin) when it repeats until the end.
    double loopDisplayEnd (const Arrangement& a) const
    {
        if (! a.isLoopOpenEnded())
            return a.loopEnd;
        return std::max ({ owner.xToTime ((float) getWidth() + 40.0f), session.getContentEnd(), a.loopStart + session.getLoopLengthSeconds() });
    }

    Hit hitTestAt (juce::Point<float> p) const
    {
        const auto lane = laneAt (p.y);
        if (! lane.has_value())
            return {};

        auto zoneOf = [p] (juce::Rectangle<float> r, bool rightEdge)
        {
            const float edge = std::min (7.0f, r.getWidth() * 0.25f);
            if (p.x < r.getX() + edge)
                return Zone::left;
            if (rightEdge && p.x > r.getRight() - edge)
                return Zone::right;
            return Zone::body;
        };

        const auto& a = session.getArrangement();
        if (*lane == Track::song)
        {
            if (! session.hasSong())
                return {};
            const auto r = songBounds (a);
            if (! r.contains (p))
                return {};
            // The part chips along the top.
            if (p.y < r.getY() + titleStrip)
                for (int i = 0; i < (int) session.getSong().sections.size(); ++i)
                    if (sectionChip (a, i).contains (p))
                        return { Kind::section, i, Zone::body };
            return { Kind::song, -1, Zone::body };
        }
        if (*lane == Track::amp || *lane == Track::camera)
        {
            // Topmost first: later regions play (and draw) over earlier ones.
            for (int i = (int) a.clips.size(); --i >= 0;)
            {
                const auto& c = a.clips[(size_t) i];
                const auto r = regionBounds (*lane, c.start, c.end());
                if (r.contains (p))
                    return { Kind::clip, i, zoneOf (r, true) };
            }
        }
        else if (session.hasLoop())
        {
            const auto r = regionBounds (Track::loop, a.loopStart, loopDisplayEnd (a));
            if (r.contains (p))
                return { Kind::loop, -1, zoneOf (r, ! a.isLoopOpenEnded()) };
        }
        return {};
    }

    //==============================================================================
    void paintGrid (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        const double bar = session.getSecondsPerBar(), beat = session.getSecondsPerBeat();
        const double pps = owner.pixelsPerSecond;
        int every = 1;
        while (bar * pps * every < 16.0)
            every *= 2;
        const bool beats = beat * pps >= 14.0;
        const float bottom = (float) owner.lanesBottom();

        const double t0 = owner.xToTime (0.0f), t1 = owner.xToTime (bounds.getWidth());
        for (int b = std::max (0, (int) std::floor (t0 / bar)); b * bar <= t1; ++b)
        {
            if (b % every == 0)
            {
                g.setColour (theme::border.withAlpha (0.8f));
                g.drawVerticalLine ((int) owner.timeToX (b * bar), 0.0f, bottom);
            }
            if (beats)
            {
                g.setColour (theme::border.withAlpha (0.35f));
                for (int k = 1; k < session.getBeatsPerBar(); ++k)
                    g.drawVerticalLine ((int) owner.timeToX (b * bar + k * beat), 0.0f, bottom);
            }
        }
    }

    // Peak of the live peak history (the amped guitar) over [t0, t1).
    float historyPeak (double t0, double t1) const
    {
        auto& engine = session.getEngine();
        const double perSlot = engine.getHistorySlotSamples() / engine.getSampleRate();
        const auto s0 = (std::int64_t) std::floor (t0 / perSlot), s1 = std::max (s0 + 1, (std::int64_t) std::ceil (t1 / perSlot));
        float peak = 0.0f;
        for (auto s = s0; s < s1; ++s)
            peak = std::max (peak, engine.getHistoryPeak (s));
        return peak;
    }

    void paintRecording (juce::Graphics& g, Track lane, double start, const juce::String& name)
    {
        const double now = session.getPlayheadSeconds();
        auto r = regionBounds (lane, start, std::max (start + 0.05, now));
        drawRegionFrame (g, r, theme::rec, name, false, false, false);
        drawWaveform (g, r.withTrimmedTop (titleStrip).reduced (0.0f, 3.0f), getLocalBounds().toFloat(),
                      [this] (float x0, float x1) { return historyPeak (owner.xToTime (x0), owner.xToTime (x1)); }, theme::rec);
    }

    void paintGuitar (juce::Graphics& g, const Arrangement& arrangement)
    {
        const auto lane = laneBounds (Track::amp);
        const auto visible = getLocalBounds().toFloat();
        const bool muted = session.isMuted (Track::amp);
        const auto& takes = session.getTakes();

        g.saveState();
        g.reduceClipRegion (lane.toNearestInt());

        for (int i = 0; i < (int) arrangement.clips.size(); ++i)
        {
            const auto& c = arrangement.clips[(size_t) i];
            auto r = regionBounds (Track::amp, c.start, c.end());
            if (r.getRight() < 0.0f || r.getX() > visible.getRight())
                continue;

            const bool isSelected = isClipSelected (i);
            const bool hovered = hover.kind == Kind::clip && hover.index == i;
            const auto name = juce::isPositiveAndBelow (c.take, (int) takes.size()) ? takes[(size_t) c.take].name : juce::String ("?");
            drawRegionFrame (g, r, theme::ampAccent, name, isSelected, hoverAmount (Kind::clip, i), muted);

            const auto wave = r.withTrimmedTop (titleStrip).reduced (1.0f, 3.0f);
            if (const auto* media = session.getTakeMedia (c.take))
            {
                drawWaveform (g, wave, visible, [&] (float x0, float x1)
                {
                    const double a = c.sourceStart + (owner.xToTime (x0) - c.start), b = c.sourceStart + (owner.xToTime (x1) - c.start);
                    const auto p0 = (size_t) std::max (0.0, a * Session::TakeMedia::peaksPerSecond);
                    const auto p1 = std::min (media->peaks.size(), (size_t) std::max (0.0, std::ceil (b * Session::TakeMedia::peaksPerSecond)));
                    float peak = 0.0f;
                    for (auto p = p0; p < std::max (p1, p0 + 1) && p < media->peaks.size(); ++p)
                        peak = std::max (peak, media->peaks[p]);
                    return peak;
                }, isSelected ? theme::ampAccent.brighter (0.2f) : theme::ampAccent.withMultipliedAlpha (muted ? 0.5f : 1.0f));
            }
            else
            {
                g.setColour (theme::textFaint);
                g.setFont (fonts::medium (10.0f));
                g.drawText ("Audio introuvable", wave.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);
            }

            if (hovered && hover.zone != Zone::body)
                drawTrimHandle (g, r, hover.zone == Zone::left, theme::ampAccent);
        }

        if (session.isRecordingTake())
            paintRecording (g, Track::amp, session.getTakeStartSeconds(), utf8 ("\xe2\x97\x8f Prise ") + formatDuration (session.getTakeSeconds()));
        else if (arrangement.clips.empty())
            drawHint (g, lane, utf8 ("Aucune prise \xe2\x80\x94 T pour enregistrer guitare + cam\xc3\xa9ra"), theme::ampAccent);

        g.restoreState();
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
            overview.resize (800);
            looper.getOverview (overview.data(), (int) overview.size());
            overviewLength = length;
            overviewLayers = layers;
            overviewTime = now;
        }
    }

    void paintLoop (juce::Graphics& g, const Arrangement& arrangement)
    {
        auto& engine = session.getEngine();
        const auto& looper = engine.getLooper();
        const auto state = looper.getState();
        const auto lane = laneBounds (Track::loop);

        g.saveState();
        g.reduceClipRegion (lane.toNearestInt());

        if (state == Looper::State::Recording)
        {
            paintRecording (g, Track::loop, (double) engine.getLoopAnchor() / engine.getSampleRate(), utf8 ("\xe2\x97\x8f Boucle"));
        }
        else if (! session.hasLoop())
        {
            drawHint (g, lane, utf8 ("Pas encore de boucle \xe2\x80\x94 R pour enregistrer"), theme::loopAccent);
        }
        else
        {
            updateOverview();
            const double length = session.getLoopLengthSeconds();
            const double end = loopDisplayEnd (arrangement);
            const auto r = regionBounds (Track::loop, arrangement.loopStart, end);
            const bool isSelected = session.getSelection() == Session::Selection::loop;
            const bool hovered = hover.kind == Kind::loop;
            const bool muted = session.isMuted (Track::loop);
            const auto colour = state == Looper::State::Overdubbing ? theme::loopAccent.brighter (0.2f) : theme::loopAccent;

            const int layers = looper.getLayerCount();
            auto name = state == Looper::State::Overdubbing ? utf8 ("Overdub \xe2\x80\xa6 couche ") + juce::String (layers)
                                                            : "Boucle " + formatBars (length / session.getSecondsPerBar()) + utf8 (" mes. \xc2\xb7 ")
                                                                  + juce::String (layers) + (layers > 1 ? " couches" : " couche");
            if (arrangement.isLoopOpenEnded())
                name << utf8 (" \xc2\xb7 jusqu'\xc3\xa0 la fin");
            drawRegionFrame (g, r, colour, name, isSelected, hoverAmount (Kind::loop, -1), muted);

            // Notches at each repeat, like a looped region in GarageBand.
            const auto body = r.withTrimmedTop (titleStrip);
            if (length > 0.01 && length * owner.pixelsPerSecond > 6.0)
            {
                const double first = arrangement.loopAnchor + std::ceil ((arrangement.loopStart - arrangement.loopAnchor) / length + 1.0e-9) * length;
                g.setColour (colour.withAlpha (0.45f));
                for (double t = first; t < end; t += length)
                {
                    const float x = owner.timeToX (t);
                    if (x > (float) getWidth())
                        break;
                    if (x >= 0.0f)
                        g.fillRect (juce::Rectangle<float> (x - 0.5f, body.getY(), 1.0f, body.getHeight()));
                }
            }

            const int n = (int) overview.size();
            drawWaveform (g, body.reduced (1.0f, 3.0f), getLocalBounds().toFloat(), [&] (float x0, float x1)
            {
                if (length <= 0.0 || n == 0)
                    return 0.0f;
                auto phaseIndex = [&] (double t)
                {
                    const double phase = std::fmod (std::fmod (t - arrangement.loopAnchor, length) + length, length);
                    return juce::jlimit (0, n - 1, (int) (phase / length * n));
                };
                const int i0 = phaseIndex (owner.xToTime (x0)), i1 = phaseIndex (owner.xToTime (x1));
                float peak = overview[(size_t) i0];
                for (int i = i0; i != i1 && i1 > i0; ++i)
                    peak = std::max (peak, overview[(size_t) i]);
                return peak;
            }, colour.withMultipliedAlpha (muted ? 0.5f : 1.0f));

            if (arrangement.isLoopOpenEnded())
            {
                // The region bleeds off the right edge: it goes on.
                const auto fade = juce::Rectangle<float> ((float) getWidth() - 36.0f, r.getY(), 36.0f, r.getHeight());
                g.setGradientFill (juce::ColourGradient (theme::surfaceSunken.withAlpha (0.0f), fade.getX(), 0.0f,
                                                         theme::surfaceSunken.withAlpha (0.85f), fade.getRight(), 0.0f, false));
                g.fillRect (fade);
            }

            if (hovered && hover.zone != Zone::body)
                drawTrimHandle (g, r, hover.zone == Zone::left, colour);
        }

        g.restoreState();
    }

    void paintThumbnails (juce::Graphics& g, juce::Rectangle<float> area, const std::vector<juce::Image>& thumbs,
                          const std::function<int (double)>& indexAt)
    {
        if (thumbs.empty() || area.getHeight() < 6.0f)
            return;

        juce::Path clip;
        clip.addPath (roadie::theme::squircle (area, 3.0f));
        g.saveState();
        g.reduceClipRegion (clip);

        const float frameWidth = std::max (16.0f, area.getHeight() * 16.0f / 9.0f);
        const float from = std::max (area.getX(), area.getX() + std::floor ((0.0f - area.getX()) / frameWidth) * frameWidth);
        for (float x = std::max (area.getX(), from); x < std::min (area.getRight(), (float) getWidth()); x += frameWidth)
        {
            const auto frame = juce::Rectangle<float> (x, area.getY(), frameWidth, area.getHeight());
            const int index = indexAt (owner.xToTime (frame.getCentreX()));
            if (index < 0)
            {
                g.setColour (juce::Colours::black);
                g.fillRect (frame);
            }
            else
            {
                g.drawImage (thumbs[(size_t) std::min (index, (int) thumbs.size() - 1)], frame, juce::RectanglePlacement::fillDestination);
            }
            g.setColour (theme::surfaceSunken.withAlpha (0.6f));
            g.fillRect (frame.withWidth (1.0f));
        }
        g.restoreState();
    }

    void paintCamera (juce::Graphics& g, const Arrangement& arrangement)
    {
        const auto lane = laneBounds (Track::camera);
        const auto visible = getLocalBounds().toFloat();
        auto& camera = session.getCamera();
        const auto& takes = session.getTakes();

        g.saveState();
        g.reduceClipRegion (lane.toNearestInt());

        bool anyVideo = false;
        for (int i = 0; i < (int) arrangement.clips.size(); ++i)
        {
            const auto& c = arrangement.clips[(size_t) i];
            if (! juce::isPositiveAndBelow (c.take, (int) takes.size()))
                continue;
            auto r = regionBounds (Track::camera, c.start, c.end());
            if (r.getRight() < 0.0f || r.getX() > visible.getRight())
                continue;

            const auto& take = takes[(size_t) c.take];
            const bool video = take.hasVideo() && ! take.thumbnails.empty();
            anyVideo = anyVideo || video;
            const bool isSelected = isClipSelected (i);
            const bool hovered = hover.kind == Kind::clip && hover.index == i;
            drawRegionFrame (g, r, theme::camAccent, video ? take.name : take.name + utf8 (" \xc2\xb7 audio seul"), isSelected,
                             hoverAmount (Kind::clip, i), ! video);

            if (video)
            {
                const double offset = take.getVideoOffsetSeconds();
                paintThumbnails (g, r.withTrimmedTop (titleStrip).reduced (2.0f, 2.0f), take.thumbnails, [&c, offset] (double t)
                {
                    const double movieTime = c.sourceStart + (t - c.start) - offset;
                    return movieTime < 0.0 ? -1 : (int) movieTime; // one thumbnail per second of movie
                });
            }

            if (hovered && hover.zone != Zone::body)
                drawTrimHandle (g, r, hover.zone == Zone::left, theme::camAccent);
        }

        if (session.isRecordingTake() && camera.isRecording())
        {
            const double start = session.getTakeStartSeconds();
            const auto r = regionBounds (Track::camera, start, std::max (start + 0.05, session.getPlayheadSeconds()));
            drawRegionFrame (g, r, theme::rec, utf8 ("\xe2\x97\x8f REC ") + formatDuration (session.getTakeSeconds()), false, false, false);
            paintThumbnails (g, r.withTrimmedTop (titleStrip).reduced (2.0f, 2.0f), camera.getRecordingThumbnails(),
                             [start] (double t) { return (int) std::max (0.0, t - start); });
        }
        else if (! anyVideo && arrangement.clips.empty())
        {
            drawHint (g, lane, camera.isOpen() ? utf8 ("Aucune prise film\xc3\xa9" "e \xe2\x80\x94 T pour enregistrer")
                                               : utf8 ("Cam\xc3\xa9ra \xc3\xa9teinte \xe2\x80\x94 clique pour l'activer"),
                      theme::camAccent);
        }

        g.restoreState();
    }

    juce::Rectangle<float> songBounds (const Arrangement& a) const
    {
        return regionBounds (Track::song, a.songStart, a.songStart + std::max (0.1, session.getSongLength()));
    }

    // The chip of part `index` in the song's title strip.
    juce::Rectangle<float> sectionChip (const Arrangement& a, int index) const
    {
        const auto& sections = session.getSong().sections;
        const auto r = songBounds (a);
        const auto range = session.getSongSection (index);
        const float x0 = std::max (r.getX(), owner.timeToX (a.songStart + range.getStart()));
        const float x1 = std::min (r.getRight(), owner.timeToX (a.songStart + range.getEnd()));
        const auto font = fonts::semibold (10.0f);
        const float w = std::min (x1 - x0 - 2.0f, juce::GlyphArrangement::getStringWidth (font, sections[(size_t) index].label) + 14.0f);
        return { x0 + 2.0f, r.getY() + 1.0f, std::max (0.0f, w), titleStrip - 2.0f };
    }

    void paintSong (juce::Graphics& g, const Arrangement& arrangement)
    {
        const auto lane = laneBounds (Track::song);
        const auto colour = theme::songAccent;

        g.saveState();
        g.reduceClipRegion (lane.toNearestInt());

        if (! session.hasSong())
        {
            drawHint (g, lane, session.isImportingSong() ? utf8 ("Analyse du morceau\xe2\x80\xa6")
                                                         : utf8 ("Pas de morceau \xe2\x80\x94 glisse un MP3, M4A ou WAV, ou clique ici"),
                      colour);
            g.restoreState();
            return;
        }

        const auto& song = session.getSong();
        const auto r = songBounds (arrangement);
        const bool muted = session.isMuted (Track::song);
        drawRegionFrame (g, r, colour, {}, false, hover.kind == Kind::song ? 1.0f : 0.0f, muted);

        // Part boundaries and chips; the one cycling is lit.
        const auto cycle = session.getCycleRange();
        for (int i = 0; i < (int) song.sections.size(); ++i)
        {
            const auto& section = song.sections[(size_t) i];
            const auto range = session.getSongSection (i);
            const float x = owner.timeToX (arrangement.songStart + range.getStart());
            if (i > 0 && x > r.getX() && x < r.getRight())
            {
                g.setColour (colour.withAlpha (0.5f));
                g.fillRect (juce::Rectangle<float> (x - 0.5f, r.getY(), 1.0f, r.getHeight()));
            }
            const auto chip = sectionChip (arrangement, i);
            if (chip.getWidth() < 10.0f)
                continue;
            const bool cycling = session.isCycleOn() && std::abs (cycle.getStart() - std::max (0.0, arrangement.songStart + range.getStart())) < 0.01
                                 && std::abs (cycle.getEnd() - (arrangement.songStart + range.getEnd())) < 0.01;
            const bool hovered = hover.kind == Kind::section && hover.index == i;
            g.setColour (cycling ? theme::exportAccent : colour.withAlpha (hovered ? 0.85f : 0.55f));
            roadie::theme::fillSquircle (g, chip, 4.0f);
            g.setColour (cycling || hovered ? theme::onAccent : theme::text);
            g.setFont (fonts::semibold (10.0f));
            g.drawText (section.label, chip.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
        }

        // The title after the chips when there is room, else at the right.
        const auto body = r.withTrimmedTop (titleStrip);
        if (const auto* media = session.getSongMedia())
        {
            drawWaveform (g, body.reduced (1.0f, 3.0f), getLocalBounds().toFloat(), [&] (float x0, float x1)
            {
                const double a = owner.xToTime (x0) - arrangement.songStart, b = owner.xToTime (x1) - arrangement.songStart;
                const auto p0 = (size_t) std::max (0.0, a * Session::SongMedia::peaksPerSecond);
                const auto p1 = std::min (media->peaks.size(), (size_t) std::max (0.0, std::ceil (b * Session::SongMedia::peaksPerSecond)));
                float peak = 0.0f;
                for (auto p = p0; p < std::max (p1, p0 + 1) && p < media->peaks.size(); ++p)
                    peak = std::max (peak, media->peaks[p]);
                return peak;
            }, colour.withMultipliedAlpha (muted ? 0.5f : 1.0f));
        }

        if (body.getHeight() > 16.0f)
        {
            auto label = song.title + utf8 (" \xc2\xb7 ") + juce::String (song.bpm * song.speed, 1).replaceCharacter ('.', ',') + " BPM";
            if (song.speed < 0.999)
                label << utf8 (" \xc2\xb7 ") << juce::roundToInt (song.speed * 100.0) << utf8 ("\xc2\xa0%");
            if (song.semitones != 0)
                label << utf8 (" \xc2\xb7 ") << (song.semitones > 0 ? "+" : "") << song.semitones << " dt";
            if (session.isRenderingSong())
                label << utf8 (" \xc2\xb7 calcul\xe2\x80\xa6");
            const auto font = fonts::semibold (10.5f);
            const float w = juce::GlyphArrangement::getStringWidth (font, label) + 14.0f;
            auto pill = juce::Rectangle<float> (std::max (r.getX(), 0.0f) + 6.0f, body.getBottom() - 18.0f, w, 15.0f);
            g.setColour (theme::surfaceSunken.withAlpha (0.75f));
            roadie::theme::fillSquircle (g, pill, 7.5f);
            g.setColour (theme::text);
            g.setFont (font);
            g.drawText (label, pill.reduced (7.0f, 0.0f), juce::Justification::centredLeft, true);
        }

        g.restoreState();
    }

    bool isClipSelected (int index) const
    {
        if (drag.active && drag.moved && drag.duplicate)
            return index == (int) drag.edited.clips.size() - 1;
        return session.getSelectedClip() == index;
    }

    //==============================================================================
    void showMenu (Hit hit, std::optional<Track> lane, double clickedTime)
    {
        juce::PopupMenu menu;
        auto item = [&menu] (const juce::String& text, const juce::String& shortcut, bool enabled, std::function<void()> action)
        {
            juce::PopupMenu::Item i (text);
            i.shortcutKeyDescription = shortcut;
            i.isEnabled = enabled;
            i.action = std::move (action);
            menu.addItem (i);
        };
        auto* timeline = &owner;
        auto* s = &session;
        const double playhead = session.getPlayheadSeconds();

        if (hit.kind == Kind::clip)
        {
            const auto c = session.getArrangement().clips[(size_t) hit.index];
            item (utf8 ("Couper \xc3\xa0 la t\xc3\xaate de lecture"), utf8 ("\xe2\x8c\x98T"), playhead > c.start + 0.05 && playhead < c.end() - 0.05,
                  [s] { s->splitAtPlayhead(); });
            item ("Dupliquer", utf8 ("\xe2\x8c\x98" "D"), true, [s] { s->duplicateSelection(); });
            item (utf8 ("R\xc3\xa9-amplifier avec l'ampli actuel"), {}, s->canReampTake (c.take), [s, c] { s->reampTake (c.take); });
            item ("Supprimer", utf8 ("\xe2\x8c\xab"), true, [s] { s->deleteSelection(); });
            menu.addSeparator();
            item (utf8 ("Cr\xc3\xa9" "er un cycle sur la r\xc3\xa9gion"), {}, true, [s, c] { s->setCycleRange (c.start, c.end()); s->setCycleOn (true); });
            item (utf8 ("Exporter cette r\xc3\xa9gion\xe2\x80\xa6"), {}, true, [timeline] { if (timeline->onExportRequested) timeline->onExportRequested(); });
        }
        else if (hit.kind == Kind::loop)
        {
            const auto a = session.getArrangement();
            const double end = loopDisplayEnd (a);
            item (utf8 ("Boucler jusqu'\xc3\xa0 la fin"), {}, ! a.isLoopOpenEnded(), [s] { s->setLoopOpenEnded(); });
            item (utf8 ("Arr\xc3\xaater la boucle \xc3\xa0 la t\xc3\xaate de lecture"), {}, playhead > a.loopStart + 0.05 && playhead < end,
                  [s, a, playhead]
                  {
                      auto edited = a;
                      edited.loopEnd = playhead;
                      s->applyEdit (edited, utf8 ("Arr\xc3\xaater la boucle"));
                  });
            menu.addSeparator();
            item (utf8 ("Cr\xc3\xa9" "er un cycle sur la boucle"), {}, true, [s, a]
                  {
                      s->setCycleRange (a.loopStart, a.loopStart + s->getLoopLengthSeconds());
                      s->setCycleOn (true);
                  });
            item (utf8 ("Exporter cette r\xc3\xa9gion\xe2\x80\xa6"), {}, true, [timeline] { if (timeline->onExportRequested) timeline->onExportRequested(); });
            menu.addSeparator();
            item ("Effacer la boucle", utf8 ("\xe2\x87\xa7\xe2\x8c\xab"), true, [s] { s->clearLoop(); });
            item ("Supprimer la piste Looper", {}, true, [timeline] { timeline->confirmRemoveLooperTrack(); });
        }
        else if (hit.kind == Kind::song || hit.kind == Kind::section || (lane == Track::song && session.hasSong()))
        {
            const double songBpm = session.getSong().bpm;
            item (utf8 ("Tempo \xc3\x97" "2 (") + juce::String (songBpm * 2.0, 1).replaceCharacter ('.', ',') + " BPM)", {}, songBpm * 2.0 <= 240.0,
                  [s, songBpm] { s->setSongTempo (songBpm * 2.0); });
            item (utf8 ("Tempo \xc3\xb7" "2 (") + juce::String (songBpm / 2.0, 1).replaceCharacter ('.', ',') + " BPM)", {}, songBpm / 2.0 >= 40.0,
                  [s, songBpm] { s->setSongTempo (songBpm / 2.0); });
            item (utf8 ("Caler la premi\xc3\xa8re mesure sur la mesure 2"), {}, true, [s] { s->placeSongOnBar2(); });
            menu.addSeparator();
            if (hit.kind == Kind::section)
                item (utf8 ("R\xc3\xa9p\xc3\xa9ter cette partie"), "", true, [s, index = hit.index] { s->cycleSection (index); });
            item (utf8 ("Remplacer le morceau\xe2\x80\xa6"), {}, true, [timeline] { if (timeline->onImportSong) timeline->onImportSong(); });
            item (utf8 ("Remplacer par un lien YouTube\xe2\x80\xa6"), {}, ! session.isDownloadingSong(), [timeline] { if (timeline->onImportYouTube) timeline->onImportYouTube(); });
            item ("Retirer le morceau", {}, true, [s] { s->removeSong(); });
        }
        else
        {
            if (lane == Track::song)
            {
                item (utf8 ("Choisir un morceau\xe2\x80\xa6"), {}, ! session.isImportingSong(), [timeline] { if (timeline->onImportSong) timeline->onImportSong(); });
                item (utf8 ("Importer depuis YouTube\xe2\x80\xa6"), {}, ! session.isImportingSong() && ! session.isDownloadingSong(),
                      [timeline] { if (timeline->onImportYouTube) timeline->onImportYouTube(); });
            }
            if (lane != Track::loop && lane != Track::song)
            {
                juce::PopupMenu takesMenu;
                const auto& takes = session.getTakes();
                for (int i = 0; i < (int) takes.size(); ++i)
                    takesMenu.addItem (takes[(size_t) i].name + (session.isTakeUsed (i) ? juce::String() : utf8 (" (hors timeline)")),
                                       [s, i, clickedTime] { s->insertTake (i, clickedTime); });
                if (takes.empty())
                    takesMenu.addItem (utf8 ("Aucune prise enregistr\xc3\xa9" "e"), false, false, [] {});
                menu.addSubMenu (utf8 ("Ins\xc3\xa9rer une prise ici"), takesMenu);
            }
            item (utf8 ("Placer la t\xc3\xaate de lecture ici"), {}, ! session.isRecordingTake(), [s, clickedTime] { s->locate (clickedTime); });
        }

        menu.addSeparator();
        item (session.isMagnetOn() ? utf8 ("\xe2\x9c\x93 Aimant de la t\xc3\xaate de lecture") : utf8 ("Aimant de la t\xc3\xaate de lecture"), {}, true,
              [s] { s->setMagnetOn (! s->isMagnetOn()); });

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    // The region under the mouse fades in while the previous one fades out.
    void setHover (const Hit& hit)
    {
        if (hit.kind != hover.kind || hit.index != hover.index)
        {
            fadingOut = hover;
            fadeOut.reset (hover.kind != Kind::none);
            fadeIn.reset (false);
        }
        hover = hit;
    }

    float hoverAmount (Kind kind, int index)
    {
        if (kind == hover.kind && (kind == Kind::loop || index == hover.index))
            return fadeIn.get (! drag.active);
        if (kind == fadingOut.kind && (kind == Kind::loop || index == fadingOut.index))
            return fadeOut.get (false);
        return 0.0f;
    }

    Timeline& owner;
    Session& session;
    Hit hover, fadingOut;
    roadie::ui::HoverFade fadeIn { *this }, fadeOut { *this };

    struct Drag
    {
        bool active = false, moved = false, duplicate = false;
        Hit hit;
        Arrangement origin, edited;
        double downTime = 0.0, loopVisibleEnd = 0.0;
    };
    Drag drag;

    std::vector<float> overview;
    int overviewLength = -1, overviewLayers = -1;
    double overviewTime = 0.0;
};

//==============================================================================
// Playhead and the hover guide, over the ruler and the lanes.
class Timeline::Overlay : public juce::Component
{
public:
    explicit Overlay (Timeline& o) : owner (o) { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        const float top = (float) rulerHeight;

        if (owner.hoverX >= 0.0f && ! owner.arrange->isDragging())
        {
            g.setColour (theme::text.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> (std::round (owner.hoverX), Ruler::bandHeight + 4.0f, 1.0f, (float) getHeight()));
        }

        const float x = owner.timeToX (owner.session.getPlayheadSeconds());
        if (x < -6.0f || x > (float) getWidth() + 6.0f)
            return;

        g.setColour (owner.session.isRecordingTake() ? theme::rec : theme::text);
        g.fillRect (juce::Rectangle<float> (x - 0.75f, top - 1.0f, 1.5f, (float) getHeight() - top + 1.0f));
        juce::Path head;
        head.addTriangle (x - 6.0f, top - 9.0f, x + 6.0f, top - 9.0f, x, top - 1.0f);
        g.fillPath (head);
    }

private:
    Timeline& owner;
};

//==============================================================================
// Floating monitor: the live camera (DIRECT), or the arrangement's video
// under the playhead while it plays back or right after a locate (LECTURE).
//==============================================================================
Timeline::Timeline (Session& s) : session (s)
{
    for (auto t : allTracks)
    {
        headers.push_back (std::make_unique<TrackHeader> (*this, session, t));
        addAndMakeVisible (*headers.back());
    }

    master = std::make_unique<MasterHeader> (session);
    addAndMakeVisible (*master);

    zoom = std::make_unique<ZoomControl> (*this);
    addAndMakeVisible (*zoom);
    ruler = std::make_unique<Ruler> (*this, session);
    addAndMakeVisible (*ruler);
    arrange = std::make_unique<ArrangeView> (*this, session);
    addAndMakeVisible (*arrange);

    addLooperButton.setFontSize (12.0f);
    addLooperButton.setTooltip (utf8 ("Ajoute une piste Looper au projet"));
    addLooperButton.onClick = [this] { session.setLooperTrackVisible (true); };
    addAndMakeVisible (addLooperButton);

    scrollbar.setAutoHide (false);
    scrollbar.addListener (this);
    addAndMakeVisible (scrollbar);

    overlay = std::make_unique<Overlay> (*this);
    addAndMakeVisible (*overlay);

    headerSplitter.onDragStart = [this] { headerWidthAtDragStart = headerWidth; };
    headerSplitter.onDrag = [this] (int delta) { setHeaderWidth (headerWidthAtDragStart + delta); };
    headerSplitter.onDragEnd = [this] { if (onHeaderWidthChanged) onHeaderWidthChanged (headerWidth); };
    headerSplitter.onDoubleClick = [this]
    {
        setHeaderWidth (defaultHeaderWidth);
        if (onHeaderWidthChanged)
            onHeaderWidthChanged (headerWidth);
    };
    addAndMakeVisible (headerSplitter);
}

Timeline::~Timeline()
{
    scrollbar.removeListener (this);
}

void Timeline::confirmRemoveLooperTrack()
{
    if (! session.hasLoop() && session.getEngine().getLooper().getState() == Looper::State::Empty)
    {
        session.removeLooperTrack();
        return;
    }
    auto* s = &session;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (
                                      juce::MessageBoxIconType::WarningIcon, utf8 ("Supprimer la piste Looper ?"),
                                      utf8 ("La boucle enregistr\xc3\xa9" "e sur cette piste sera effac\xc3\xa9" "e."), "Supprimer", "Annuler", this),
                                  [s] (int result)
                                  {
                                      if (result == 1)
                                          s->removeLooperTrack();
                                  });
}

bool Timeline::isShown (Track track) const
{
    return track != Track::loop || session.isLooperTrackVisible();
}

int Timeline::rowTop (Track track) const noexcept { return rowTops[(size_t) track]; }
int Timeline::rowHeightOf (Track track) const noexcept { return rowHeights[(size_t) track]; }
int Timeline::lanesBottom() const noexcept { return rowTop (Track::song) + rowHeightOf (Track::song); }

void Timeline::setTrackHeight (Track track, int height)
{
    trackHeights[(size_t) track] = juce::jlimit (20, 320, height);
    resized();
    repaint();
}

int Timeline::getTrackHeight (Track track) const { return trackHeights[(size_t) track]; }

void Timeline::zoomTracks (float factor)
{
    for (auto& h : trackHeights)
        h = juce::jlimit (20, 320, juce::roundToInt ((float) h * factor));
    resized();
    repaint();
    if (onTrackHeightsChanged)
        onTrackHeightsChanged();
}

juce::String Timeline::getTrackHeightsState() const
{
    juce::StringArray parts;
    for (auto h : trackHeights)
        parts.add (juce::String (h));
    return parts.joinIntoString (",");
}

void Timeline::setTrackHeightsState (const juce::String& state)
{
    const auto parts = juce::StringArray::fromTokens (state, ",", {});
    if (parts.size() == (int) trackHeights.size())
        for (size_t i = 0; i < trackHeights.size(); ++i)
            trackHeights[i] = juce::jlimit (20, 320, parts[(int) i].getIntValue());
    resized();
}

void Timeline::setHeaderWidth (int width)
{
    width = juce::jlimit (minHeaderWidth, maxHeaderWidth, width);
    if (width != headerWidth)
    {
        headerWidth = width;
        resized();
        repaint();
    }
}

juce::Point<int> Timeline::getLanePoint (Track track, double seconds) const
{
    return { headerWidth + juce::roundToInt (timeToX (seconds)), rulerHeight + rowTop (track) + rowHeightOf (track) / 2 };
}

juce::Point<int> Timeline::getRulerPoint (double seconds, bool cycleBand) const
{
    return { ruler->getX() + juce::roundToInt (timeToX (seconds)), cycleBand ? (int) Ruler::bandHeight / 2 : rulerHeight - 8 };
}

void Timeline::setSelectedTrack (Track track)
{
    selected = track;
    for (auto& h : headers)
        h->repaint();
    arrange->repaint();
}

double Timeline::getVisibleLength() const noexcept
{
    return (double) laneWidth() / pixelsPerSecond;
}

double Timeline::getGridStep() const
{
    const double beat = session.getSecondsPerBeat();
    if (beat * pixelsPerSecond >= 14.0)
        return beat;
    double step = session.getSecondsPerBar();
    while (step * pixelsPerSecond < 14.0)
        step *= 2.0;
    return step;
}

double Timeline::snapPlayhead (double seconds, bool freely) const
{
    seconds = std::max (0.0, seconds);
    if (freely || ! session.isMagnetOn())
        return seconds;
    const double step = getGridStep();
    const double nearest = std::round (seconds / step) * step;
    return std::abs (nearest - seconds) * pixelsPerSecond <= 9.0 ? std::max (0.0, nearest) : seconds;
}

double Timeline::snap (double seconds, bool enabled) const
{
    if (! enabled)
        return std::max (0.0, seconds);
    const double step = getGridStep();
    return std::max (0.0, std::round (seconds / step) * step);
}

double Timeline::getScrollableLength() const
{
    const double content = std::max ({ session.getContentEnd(), session.getPlayheadSeconds(), session.getCycleRange().getEnd(),
                                       8.0 * session.getSecondsPerBar() });
    return std::max (content + 4.0 * session.getSecondsPerBar(), viewStart + getVisibleLength());
}

void Timeline::setView (double start, double pps)
{
    pixelsPerSecond = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond, pps);
    viewStart = std::max (0.0, start);
    updateScrollbar();
    repaintView();
}

void Timeline::zoomAround (double anchorSeconds, float anchorX, double factor)
{
    const double pps = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond, pixelsPerSecond * factor);
    setView (anchorSeconds - (double) anchorX / pps, pps);
}

void Timeline::zoomBy (double factor)
{
    const double playhead = session.getPlayheadSeconds();
    const float x = timeToX (playhead);
    if (x >= 0.0f && x <= (float) laneWidth())
        zoomAround (playhead, x, factor);
    else
        zoomAround (xToTime ((float) laneWidth() * 0.5f), (float) laneWidth() * 0.5f, factor);
}

void Timeline::zoomToFit()
{
    const double length = std::max (session.getContentEnd(), 8.0 * session.getSecondsPerBar()) * 1.12;
    setView (0.0, (double) laneWidth() / length);
}

void Timeline::userScrolled()
{
    lastUserScroll = nowSeconds();
}

void Timeline::updateScrollbar()
{
    scrollbar.setRangeLimits (0.0, getScrollableLength(), juce::dontSendNotification);
    scrollbar.setCurrentRange (viewStart, getVisibleLength(), juce::dontSendNotification);
}

void Timeline::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    userScrolled();
    viewStart = std::max (0.0, newRangeStart);
    repaintView();
}

void Timeline::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto local = e.getEventRelativeTo (arrange.get()).position;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomAround (xToTime (local.x), local.x, std::pow (1.6, (double) wheel.deltaY * 4.0));
        return;
    }

    const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    userScrolled();
    setView (viewStart - (double) delta * 400.0 / pixelsPerSecond, pixelsPerSecond);
}

void Timeline::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    const auto local = e.getEventRelativeTo (arrange.get()).position;
    zoomAround (xToTime (local.x), local.x, (double) scaleFactor);
}

void Timeline::followPlayhead()
{
    if (! session.isPlaying() || arrange->isDragging() || nowSeconds() - lastUserScroll < 2.0)
        return;

    const double playhead = session.getPlayheadSeconds();
    const float x = timeToX (playhead);
    if (x > (float) laneWidth() * 0.92f || x < 0.0f)
        setView (playhead - getVisibleLength() * 0.08, pixelsPerSecond);
}

void Timeline::setHoverX (float x)
{
    if (! juce::approximatelyEqual (x, hoverX))
    {
        hoverX = x;
        overlay->repaint();
    }
}

void Timeline::repaintView()
{
    ruler->repaint();
    arrange->repaint();
    overlay->repaint();
}

void Timeline::refresh()
{
    // The Looper track comes and goes (added by the player, or by recording a loop).
    if ((rowHeights[(size_t) Track::loop] > 0) != isShown (Track::loop))
    {
        resized();
        repaint();
        lastSignature.clear();
    }
    if (selected == Track::loop && ! isShown (Track::loop))
        setSelectedTrack (Track::amp);

    if (! viewInitialised && getWidth() > headerWidth)
    {
        viewInitialised = true;
        zoomToFit();
    }

    for (auto& h : headers)
        h->refreshState();
    if (master->isVisible())
        master->refresh();

    // A new song changes the length of everything: show it all.
    const auto songKey = session.hasSong() ? session.getSong().file.getFullPathName() : juce::String();
    if (songKey != shownSong)
    {
        shownSong = songKey;
        if (viewInitialised && songKey.isNotEmpty())
            zoomToFit();
    }

    followPlayhead();

    auto signature = arrange->signature();
    if (arrange->isLive() || signature != lastSignature)
    {
        lastSignature = std::move (signature);
        arrange->repaint();
        ruler->repaint();
        updateScrollbar();
    }

    overlay->repaint();
    zoom->repaint();
}

void Timeline::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (surfaceSunken);

    // Corner above the headers (the zoom control lives here).
    auto corner = juce::Rectangle<int> (0, 0, headerWidth, rulerHeight);
    g.setColour (surface);
    g.fillRect (corner);
    g.setColour (border);
    g.drawHorizontalLine (rulerHeight - 1, 0.0f, (float) headerWidth);
    g.drawVerticalLine (headerWidth - 1, 0.0f, (float) getHeight());

    g.setColour (textFaint);
    g.setFont (fonts::semibold (9.5f).withExtraKerningFactor (0.06f));
    g.drawText ("PISTES", corner.reduced (15, 0), juce::Justification::centredLeft, false);

    // Below the headers: the empty part of the column.
    g.setColour (surface.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<int> (0, rulerHeight + lanesBottom(), headerWidth - 1, getHeight()));
}

void Timeline::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (rulerHeight);
    zoom->setBounds (top.removeFromLeft (headerWidth - 1).removeFromRight (96).reduced (8, 8));
    ruler->setBounds (top);

    auto lanes = area.withTrimmedLeft (headerWidth);
    scrollbar.setBounds (lanes.removeFromBottom (scrollbarHeight));
    arrange->setBounds (lanes);

    // Each track at its height; when they don't all fit (the thin strip of
    // Jouer), they shrink together, never below 20 px.
    // The Master row comes last, when there is room for it and the tracks.
    const bool looperShown = isShown (Track::loop);
    const int addRow = looperShown ? 0 : addRowHeight;
    int shownCount = 0;
    for (auto t : allTracks)
        shownCount += isShown (t) ? 1 : 0;
    const bool showMaster = area.getHeight() - scrollbarHeight > shownCount * 26 + addRow + MasterHeader::preferredHeight;
    const int available = area.getHeight() - scrollbarHeight - addRow - (showMaster ? MasterHeader::preferredHeight : 0);
    int wanted = 0;
    for (auto t : allTracks)
        wanted += isShown (t) ? trackHeights[(size_t) t] : 0;
    const float scale = wanted > available ? (float) available / (float) wanted : 1.0f;
    int y = 0;
    for (auto t : allTracks)
    {
        rowHeights[(size_t) t] = isShown (t) ? std::max (20, (int) std::floor ((float) trackHeights[(size_t) t] * scale)) : 0;
        rowTops[(size_t) t] = y;
        y += rowHeights[(size_t) t];
    }

    auto headerColumn = area.removeFromLeft (headerWidth - 1);
    for (auto& h : headers)
    {
        h->setVisible (isShown (h->track));
        h->setBounds (headerColumn.getX(), headerColumn.getY() + rowTop (h->track), headerColumn.getWidth(), rowHeightOf (h->track));
    }
    addLooperButton.setVisible (! looperShown);
    addLooperButton.setBounds (juce::Rectangle<int> (headerColumn.getX() + 12, headerColumn.getY() + lanesBottom() + 5,
                                                     std::min (headerColumn.getWidth() - 24, addLooperButton.getIdealWidth() + 16), 28));
    master->setVisible (showMaster);
    master->setBounds (headerColumn.getX(), headerColumn.getY() + lanesBottom() + addRow, headerColumn.getWidth(), MasterHeader::preferredHeight);

    overlay->setBounds (getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedBottom (scrollbarHeight));
    headerSplitter.setBounds (headerWidth - 1 - roadie::ui::Splitter::thickness / 2, rulerHeight,
                              roadie::ui::Splitter::thickness, getHeight() - rulerHeight);
    updateScrollbar();
}
