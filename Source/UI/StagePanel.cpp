#include "StagePanel.h"

using namespace roadie;
using Track = Session::Track;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

juce::String formatBars (double bars)
{
    const double rounded = std::round (bars * 2.0) / 2.0;
    return std::abs (rounded - std::round (rounded)) < 0.01 ? juce::String ((int) std::round (rounded))
                                                             : juce::String (rounded, 1).replaceCharacter ('.', ',');
}
} // namespace

//==============================================================================
// The pedal: a big rounded target, its label saying what the next press does.
class StagePanel::Pedal : public juce::Button
{
public:
    explicit Pedal (Session& s) : juce::Button ("Looper"), session (s)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (utf8 ("P\xc3\xa9" "dale de boucle : enregistrer \xe2\x86\x92 boucler \xe2\x86\x92 overdub (R)"));
        onClick = [this] { session.recordLoop(); };
    }

    void refresh()
    {
        const auto& looper = session.getEngine().getLooper();
        const auto signature = juce::String (session.getCountInBeatsLeft()) + "|" + juce::String ((int) looper.getState()) + "|" + juce::String (looper.getPlayheadFraction(), 2)
                               + "|" + juce::String (looper.getLayerCount()) + "|" + juce::String (looper.getRecordedLengthSamples() / 4800);
        if (signature != shown)
        {
            shown = signature;
            repaint();
        }
    }

    void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
    {
        using namespace theme;
        const auto& looper = session.getEngine().getLooper();
        const auto state = looper.getState();
        const bool recording = state == Looper::State::Recording;
        const bool overdubbing = state == Looper::State::Overdubbing;
        const auto accent = recording ? rec : loopAccent;
        const float hover = fade.get (isOver);

        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        if (isDown)
            g.addTransform (juce::AffineTransform::scale (0.98f, 0.98f, bounds.getCentreX(), bounds.getCentreY()));

        g.setColour (accent.withAlpha ((recording || overdubbing ? 0.22f : 0.1f) + 0.05f * hover));
        roadie::theme::fillSquircle (g, bounds, radiusLg);
        g.setColour (accent.withAlpha (recording || overdubbing ? 0.9f : 0.45f + 0.25f * hover));
        roadie::theme::drawSquircle (g, bounds.reduced (0.5f), radiusLg, recording || overdubbing ? 2.0f : 1.0f);

        // Ring: the loop going round (or a pulsing dot while recording).
        const bool compact = bounds.getHeight() < 96.0f;
        const float ringSize = compact ? bounds.getHeight() - 24.0f : std::min (bounds.getHeight() * 0.5f, 96.0f);
        auto ringArea = compact ? juce::Rectangle<float> (ringSize, ringSize).withCentre ({ bounds.getX() + 12.0f + ringSize * 0.5f, bounds.getCentreY() })
                                : juce::Rectangle<float> (ringSize, ringSize).withCentre ({ bounds.getCentreX(), bounds.getY() + 14.0f + ringSize * 0.5f });
        const float thickness = std::max (3.0f, ringSize * 0.07f);
        juce::Path track;
        track.addCentredArc (ringArea.getCentreX(), ringArea.getCentreY(), ringSize * 0.5f - thickness, ringSize * 0.5f - thickness, 0.0f, 0.0f,
                             juce::MathConstants<float>::twoPi, true);
        g.setColour (border);
        g.strokePath (track, juce::PathStrokeType (thickness));

        const bool hasLoop = looper.getRecordedLengthSamples() > 0 && ! recording;
        if (hasLoop && state != Looper::State::Stopped)
        {
            juce::Path progress;
            progress.addCentredArc (ringArea.getCentreX(), ringArea.getCentreY(), ringSize * 0.5f - thickness, ringSize * 0.5f - thickness, 0.0f, 0.0f,
                                    juce::MathConstants<float>::twoPi * looper.getPlayheadFraction(), true);
            g.setColour (overdubbing ? rec : loopAccent);
            g.strokePath (progress, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const auto icon = recording ? icons::Icon::loop
                        : state == Looper::State::Empty ? icons::Icon::record
                        : overdubbing ? icons::Icon::check
                                      : icons::Icon::plus;
        const auto iconColour = state == Looper::State::Empty ? ink (rec) : ink (accent);
        if (recording)
        {
            const bool blinkOn = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
            g.setColour (rec.withAlpha (blinkOn ? 0.35f : 0.15f));
            g.fillEllipse (ringArea.reduced (ringSize * 0.22f));
        }
        icons::draw (g, icon, ringArea.reduced (ringSize * 0.3f), iconColour, 2.4f);

        // What the next press does, then the loop's state.
        const int countInLeft = state == Looper::State::Empty ? session.getCountInBeatsLeft() : 0;
        const auto action = recording ? juce::String ("Boucler")
                          : countInLeft > 0 ? utf8 ("D\xc3\xa9" "compte\xe2\x80\xa6 ") + juce::String (countInLeft)
                          : state == Looper::State::Empty ? juce::String ("Enregistrer une boucle")
                          : overdubbing ? utf8 ("Fin d'overdub")
                                        : juce::String ("Overdub");
        juce::String detail;
        if (countInLeft > 0)
            detail = utf8 ("R pour annuler");
        else if (recording)
            detail = session.getLoopBars() > 0 ? utf8 ("Enregistrement\xe2\x80\xa6 se ferme tout seul") : utf8 ("Enregistrement\xe2\x80\xa6 R pour boucler");
        else if (hasLoop)
        {
            const int layers = looper.getLayerCount();
            detail = juce::String (layers) + (layers > 1 ? " couches" : " couche") + utf8 (" \xc2\xb7 ")
                     + formatBars (looper.getRecordedLengthSamples() / std::max (1.0, session.getSamplesPerBar())) + " mes."
                     + (state == Looper::State::Stopped ? utf8 (" \xc2\xb7 arr\xc3\xaa" "t\xc3\xa9" "e") : juce::String());
        }
        else
            detail = utf8 ("Touche R \xc2\xb7 cal\xc3\xa9" "e sur le tempo");

        auto textArea = compact ? bounds.withTrimmedLeft (ringArea.getRight() - bounds.getX() + 10.0f).reduced (0.0f, 8.0f)
                                : bounds.withTop (ringArea.getBottom() + 6.0f).reduced (10.0f, 6.0f);
        textArea = textArea.withSizeKeepingCentre (textArea.getWidth(), std::min (textArea.getHeight(), 40.0f));
        const auto justification = compact ? juce::Justification::centredLeft : juce::Justification::centred;
        g.setColour (theme::text);
        g.setFont (fonts::display (15.0f));
        g.drawText (action, textArea.removeFromTop (20.0f), justification, true);
        g.setColour (textMuted);
        g.setFont (fonts::medium (11.5f));
        g.drawText (detail, textArea, justification, true);
    }

private:
    Session& session;
    juce::String shown;
    ui::HoverFade fade { *this };
};

//==============================================================================
// One source of the balance: name, fader + meter, mute.
class StagePanel::Strip : public juce::Component
{
public:
    Strip (Session& s, Track t, const juce::String& n, juce::Colour a)
        : session (s), track (t), name (n), accent (a),
          fader (a, ui::Fader::Orientation::vertical), meter (ui::Meter::Orientation::vertical), mute (false, a)
    {
        fader.onChange = [this] (float db) { session.setTrackVolume (track, db); };
        mute.onClick = [this] { session.setMuted (track, mute.getToggleState()); };
        mute.setTooltip ("Couper " + name);
        addAndMakeVisible (fader);
        addAndMakeVisible (meter);
        addAndMakeVisible (mute);
    }

    void refresh()
    {
        if (! fader.isMouseButtonDown())
            fader.setValue (session.getTrackVolume (track));
        mute.setToggleState (session.isMuted (track), juce::dontSendNotification);
        auto& engine = session.getEngine();
        meter.setLevel (track == Track::amp ? engine.getGuitarPeak() : (track == Track::loop ? engine.getLoopPeak() : engine.getSongPeak()));
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.setColour (session.isMuted (track) ? textFaint : theme::text);
        g.setFont (fonts::semibold (11.5f));
        g.drawText (name, getLocalBounds().removeFromTop (18), juce::Justification::centred, true);
        g.setColour (accent);
        roadie::theme::fillSquircle (g, getLocalBounds().removeFromTop (22).removeFromBottom (2).withSizeKeepingCentre (18, 2).toFloat(), 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (26);
        mute.setBounds (area.removeFromBottom (22).withSizeKeepingCentre (22, 18));
        area.removeFromBottom (6);
        auto column = area.withSizeKeepingCentre (std::min (area.getWidth(), 52), area.getHeight());
        meter.setBounds (column.removeFromRight (8).reduced (0, 8));
        column.removeFromRight (2);
        fader.setBounds (column);
    }

private:
    Session& session;
    const Track track;
    juce::String name;
    juce::Colour accent;
    ui::Fader fader;
    ui::Meter meter;
    ui::MuteSoloButton mute;
};

//==============================================================================
StagePanel::StagePanel (Session& s)
    : session (s),
      undoLayerButton ("Retirer la couche", ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::undo),
      clearButton ("Effacer", ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::clear),
      addLooperButton ("Ajouter une piste Looper", ui::PillButton::Variant::tinted, theme::loopAccent, icons::Icon::plus),
      countInButton (utf8 ("D\xc3\xa9" "compte"), ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::metronome),
      barsButton ("Libre", ui::PillButton::Variant::neutral, theme::loopAccent, icons::Icon::loop)
{
    countInButton.setTooltip (utf8 ("Une mesure de clics avant le d\xc3\xa9" "but de la boucle"));
    countInButton.onClick = [this] { session.setLoopCountInOn (! session.isLoopCountInOn()); };
    barsButton.setTooltip (utf8 ("Longueur de la boucle : libre (tu la fermes avec R) ou un nombre de mesures"));
    barsButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem ("Libre", true, session.getLoopBars() == 0, [this] { session.setLoopBars (0); });
        for (int bars : { 1, 2, 4, 8, 16 })
            menu.addItem (juce::String (bars) + (bars > 1 ? " mesures" : " mesure"), true, session.getLoopBars() == bars,
                          [this, bars] { session.setLoopBars (bars); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&barsButton));
    };
    for (auto* b : { &countInButton, &barsButton })
    {
        b->setFontSize (12.0f);
        addAndMakeVisible (b);
    }

    addLooperButton.setFontSize (13.0f);
    addLooperButton.setTooltip (utf8 ("Ajoute la piste Looper au projet (elle n'y est pas par d\xc3\xa9" "faut)"));
    addLooperButton.onClick = [this] { session.setLooperTrackVisible (true); };
    addChildComponent (addLooperButton);
    looperShown = session.isLooperTrackVisible();

    pedal = std::make_unique<Pedal> (session);
    addAndMakeVisible (*pedal);

    undoLayerButton.setTooltip (utf8 ("Retire la derni\xc3\xa8re couche d'overdub (U)"));
    undoLayerButton.onClick = [this] { session.undo(); };
    clearButton.setTooltip (utf8 ("Efface toute la boucle (Maj + Retour arri\xc3\xa8re)"));
    clearButton.onClick = [this] { session.clearLoop(); };
    for (auto* b : { &undoLayerButton, &clearButton })
    {
        b->setFontSize (12.0f);
        addAndMakeVisible (b);
    }

    strips.push_back (std::make_unique<Strip> (session, Track::song, "Morceau", theme::songAccent));
    strips.push_back (std::make_unique<Strip> (session, Track::amp, "Guitare", theme::ampAccent));
    strips.push_back (std::make_unique<Strip> (session, Track::loop, "Boucle", theme::loopAccent));
    for (auto& strip : strips)
        addAndMakeVisible (*strip);

    splitter.onDragStart = [this] { heightAtDragStart = balanceHeight; };
    splitter.onDrag = [this] (int delta) { setBalanceHeight (heightAtDragStart - delta); };
    splitter.onDragEnd = [this] { if (onBalanceHeightChanged) onBalanceHeightChanged (balanceHeight); };
    splitter.onDoubleClick = [this]
    {
        setBalanceHeight (defaultBalanceHeight);
        if (onBalanceHeightChanged)
            onBalanceHeightChanged (balanceHeight);
    };
    addAndMakeVisible (splitter);
}

StagePanel::~StagePanel() = default;

void StagePanel::setBalanceHeight (int height)
{
    // Kept as asked; resized() gives the pedal at least a 44 pt target with
    // its two buttons when the column is short.
    balanceHeight = std::max (minBalanceHeight, height);
    if (getHeight() > 0)
        balanceHeight = std::min (balanceHeight, std::max (minBalanceHeight, getHeight() - 150));
    resized();
}

void StagePanel::refresh()
{
    // No Looper track by default: until it is added, a button stands in for
    // the pedal and the Balance has no strip for it.
    if (const bool visible = session.isLooperTrackVisible(); visible != looperShown)
    {
        looperShown = visible;
        resized();
        repaint();
    }
    const auto& looper = session.getEngine().getLooper();
    undoLayerButton.setEnabled (looper.isUndoAvailable() && looper.getState() != Looper::State::Overdubbing);
    clearButton.setEnabled (looper.getState() != Looper::State::Empty);
    pedal->refresh();
    countInButton.setVariant (session.isLoopCountInOn() ? ui::PillButton::Variant::tinted : ui::PillButton::Variant::neutral);
    const auto barsText = session.getLoopBars() == 0 ? juce::String ("Libre") : juce::String (session.getLoopBars()) + (session.getLoopBars() > 1 ? " mes." : " mes.");
    if (barsButton.getButtonText() != barsText)
    {
        barsButton.setButtonText (barsText);
        resized();
    }
    for (auto& strip : strips)
        strip->refresh();
}

void StagePanel::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (surface);
    g.setColour (border);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
    g.drawHorizontalLine (splitter.getBounds().getCentreY(), 0.0f, (float) getWidth());

    ui::drawSectionLabel (g, "Looper", looperHeader.toFloat());
    ui::drawSectionLabel (g, "Balance", balanceHeader.toFloat());
}

void StagePanel::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (1);
    const int shownBalance = juce::jlimit (minBalanceHeight, std::max (minBalanceHeight, getHeight() - 150), balanceHeight);

    auto balance = area.removeFromBottom (shownBalance);
    splitter.setBounds (0, balance.getY() - ui::Splitter::thickness / 2, getWidth(), ui::Splitter::thickness);

    auto looper = area.reduced (14, 10);
    looperHeader = looper.removeFromTop (18);
    looper.removeFromTop (6);
    auto options = looper.removeFromBottom (30);
    looper.removeFromBottom (8);
    auto buttons = looper.removeFromBottom (44);
    const int half = (buttons.getWidth() - 8) / 2;
    undoLayerButton.setBounds (buttons.removeFromLeft (half));
    clearButton.setBounds (buttons.removeFromRight (half));
    looper.removeFromBottom (8);
    {
        const int half2 = (options.getWidth() - 8) / 2;
        countInButton.setBounds (options.removeFromLeft (half2));
        barsButton.setBounds (options.removeFromRight (half2));
    }
    countInButton.setVisible (looperShown);
    barsButton.setVisible (looperShown);
    pedal->setBounds (looper);
    pedal->setVisible (looperShown);
    undoLayerButton.setVisible (looperShown);
    clearButton.setVisible (looperShown);
    addLooperButton.setVisible (! looperShown);
    addLooperButton.setBounds (looper.withSizeKeepingCentre (std::min (looper.getWidth(), addLooperButton.getIdealWidth() + 16), 44));

    balance = balance.reduced (14, 10);
    balanceHeader = balance.removeFromTop (18);
    balance.removeFromTop (4);
    const int numStrips = looperShown ? (int) strips.size() : (int) strips.size() - 1;
    const int stripWidth = std::min (76, balance.getWidth() / std::max (1, numStrips));
    auto row = balance.withSizeKeepingCentre (stripWidth * numStrips, balance.getHeight());
    for (auto& strip : strips)
    {
        const bool shownStrip = looperShown || strip.get() != strips.back().get();
        strip->setVisible (shownStrip);
        if (shownStrip)
            strip->setBounds (row.removeFromLeft (stripWidth));
    }
}
