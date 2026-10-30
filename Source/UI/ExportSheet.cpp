#include "ExportSheet.h"
#include "../I18n.h"

using namespace roadie;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

juce::String formatDuration (double seconds)
{
    const int total = (int) seconds;
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

juce::String formatPrecise (double seconds)
{
    const int tenths = juce::roundToInt (seconds * 10.0);
    return juce::String (tenths / 600) + ":" + juce::String ((tenths / 10) % 60).paddedLeft ('0', 2) + "," + juce::String (tenths % 10);
}
} // namespace

// Segmented format choice with a mini frame showing the aspect (.seg-btn).
class ExportSheet::FormatButton : public juce::Button
{
public:
    FormatButton (const juce::String& text, bool portrait) : juce::Button (text), isPortrait (portrait)
    {
        setWantsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool isOver, bool) override
    {
        using namespace theme;
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const bool selected = getToggleState();

        g.setColour (selected ? exportAccent.withAlpha (0.10f) : (isOver ? surfaceRaised.brighter (0.05f) : surfaceRaised));
        roadie::theme::fillSquircle (g, bounds, radiusMd);
        g.setColour (selected ? exportAccent : border);
        roadie::theme::drawSquircle (g, bounds, radiusMd, 1.0f);

        auto content = bounds.reduced (8.0f, 9.0f);
        auto label = content.removeFromBottom (16.0f);
        const auto frame = isPortrait ? juce::Rectangle<float> (18.0f, 30.0f) : juce::Rectangle<float> (32.0f, 18.0f);
        auto swatch = frame.withCentre (content.getCentre());
        g.setColour (surfaceSunken);
        roadie::theme::fillSquircle (g, swatch, 3.0f);
        g.setColour (selected ? exportAccent : border);
        roadie::theme::drawSquircle (g, swatch, 3.0f, 1.0f);

        g.setColour (selected ? theme::text : textMuted);
        g.setFont (fonts::semibold (12.5f));
        g.drawText (getButtonText(), label, juce::Justification::centred, false);
    }

private:
    bool isPortrait;
};

ExportSheet::ExportSheet (Session& s)
    : session (s),
      exportButton (tr ("Export"), ui::PillButton::Variant::solid, theme::exportAccent, icons::Icon::exportArrow),
      audioButton (tr ("Audio only (.wav)"), ui::PillButton::Variant::neutral, theme::exportAccent, icons::Icon::waveform),
      revealButton (tr ("Show in Finder"), ui::PillButton::Variant::neutral, theme::exportAccent, icons::Icon::folder)
{
    setWantsKeyboardFocus (true);

    rangeBox.onChange = [this]
    {
        titleEditor.setText (defaultTitle(), false);
        updateTrimRange();
        updateEnablement();
    };
    addAndMakeVisible (rangeBox);

    verticalButton = std::make_unique<FormatButton> (tr ("Vertical · 9:16"), true);
    horizontalButton = std::make_unique<FormatButton> (tr ("Horizontal · 16:9"), false);
    verticalButton->onClick = [this] { vertical = true; verticalButton->setToggleState (true, juce::dontSendNotification); horizontalButton->setToggleState (false, juce::dontSendNotification); };
    horizontalButton->onClick = [this] { vertical = false; verticalButton->setToggleState (false, juce::dontSendNotification); horizontalButton->setToggleState (true, juce::dontSendNotification); };
    verticalButton->setToggleState (true, juce::dontSendNotification);
    verticalButton->setTooltip ("Reels, Shorts, TikTok");
    horizontalButton->setTooltip ("YouTube, ordinateur");
    addAndMakeVisible (*verticalButton);
    addAndMakeVisible (*horizontalButton);

    trimSlider.setColour (juce::Slider::trackColourId, theme::exportAccent);
    trimSlider.setColour (juce::Slider::backgroundColourId, theme::surfaceSunken);
    trimSlider.setColour (juce::Slider::thumbColourId, theme::exportAccent);
    trimSlider.setWantsKeyboardFocus (false);
    trimSlider.setTooltip (tr ("Trim: drag the two handles to keep the best moment"));
    trimSlider.onValueChange = [this] { repaint (trimLabel.withHeight (trimLabel.getHeight() + 2)); };
    addAndMakeVisible (trimSlider);

    titleEditor.setFont (fonts::medium (13.5f));
    titleEditor.setIndents (12, 9);
    titleEditor.onReturnKey = [this] { startExport(); };
    titleEditor.onEscapeKey = [this] { close(); };
    addAndMakeVisible (titleEditor);

    burnInToggle.setButtonText (tr ("Burn the title into the video"));
    mirrorToggle.setButtonText (tr ("Mirror image (like the monitor)"));
    songToggle.setButtonText (tr ("With the song (unchecked: guitar only)"));
    songToggle.setTooltip (tr ("Some platforms mute protected tracks: export the guitar only in that case"));
    songToggle.setToggleState (true, juce::dontSendNotification);
    songToggle.onClick = [this] { updateEnablement(); };
    for (auto* t : { &burnInToggle, &mirrorToggle, &songToggle })
    {
        t->setColour (juce::ToggleButton::tickColourId, theme::exportAccent);
        t->setWantsKeyboardFocus (false);
        addAndMakeVisible (t);
    }

    exportButton.setFontSize (14.5f);
    exportButton.onClick = [this] { startExport(); };
    addAndMakeVisible (exportButton);

    audioButton.setFontSize (12.5f);
    audioButton.setTooltip (tr ("Copies the take's mix as a .wav, without video"));
    audioButton.onClick = [this] { exportAudio(); };
    addAndMakeVisible (audioButton);

    revealButton.setFontSize (12.5f);
    revealButton.onClick = [this] { if (lastExport.exists()) lastExport.revealToUser(); };
    addChildComponent (revealButton);

    closeButton.setTooltip (tr ("Close (Esc)"));
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);
}

ExportSheet::~ExportSheet() = default;

juce::Range<double> ExportSheet::computeRange (RangeChoice choice) const
{
    if (choice == RangeChoice::cycle && session.isCycleOn())
        return session.getCycleRange();

    if (choice == RangeChoice::selection)
    {
        if (const auto extent = session.getSelectionExtent(); extent.getLength() > 0.0)
            return extent;
    }

    return { 0.0, session.getContentEnd() };
}

void ExportSheet::populateRangeChoices()
{
    const RangeChoice previous = (RangeChoice) juce::jlimit (0, 2, rangeBox.getSelectedId() - 1);

    rangeBox.clear (juce::dontSendNotification);
    rangeBox.addItem (tr ("Whole song · ") + formatDuration (computeRange (RangeChoice::wholeSong).getLength()),
                       (int) RangeChoice::wholeSong + 1);

    if (session.isCycleOn())
        rangeBox.addItem (tr ("Cycle zone · ") + formatDuration (computeRange (RangeChoice::cycle).getLength()),
                           (int) RangeChoice::cycle + 1);

    if (session.getSelection() != Session::Selection::none)
        rangeBox.addItem (tr ("Selection · ") + formatDuration (computeRange (RangeChoice::selection).getLength()),
                           (int) RangeChoice::selection + 1);

    // Prefer what was already chosen if it's still on offer; otherwise the
    // most specific thing available (selection > cycle > whole song) is
    // almost always what the user meant to export.
    int toSelect = (int) RangeChoice::wholeSong + 1;
    if (session.getSelection() != Session::Selection::none)
        toSelect = (int) RangeChoice::selection + 1;
    else if (session.isCycleOn())
        toSelect = (int) RangeChoice::cycle + 1;
    if (rangeBox.indexOfItemId ((int) previous + 1) >= 0)
        toSelect = (int) previous + 1;

    rangeBox.setSelectedId (toSelect, juce::dontSendNotification);
}

juce::String ExportSheet::defaultTitle() const
{
    return session.getProjectName();
}

void ExportSheet::open()
{
    populateRangeChoices();

    titleEditor.setText (defaultTitle(), false);
    updateTrimRange();
    if (status != Status::exporting)
    {
        status = Status::idle;
        statusMessage.clear();
        revealButton.setVisible (false);
    }

    updateEnablement();
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    resized();
}

void ExportSheet::close()
{
    setVisible (false);
    if (onClose)
        onClose();
}

void ExportSheet::updateTrimRange()
{
    const double duration = computeRange (selectedRangeChoice()).getLength();

    trimSlider.setEnabled (duration > 0.5);
    trimSlider.setRange (0.0, std::max (0.5, duration), 0.1);
    trimSlider.setMinAndMaxValues (0.0, std::max (0.5, duration), juce::dontSendNotification);
    repaint();
}

Session::ExportRequest ExportSheet::buildRequest() const
{
    const auto range = computeRange (selectedRangeChoice());

    Session::ExportRequest request;
    request.vertical = vertical;
    request.title = titleEditor.getText();
    request.burnInTitle = burnInToggle.getToggleState();
    request.mirror = mirrorToggle.getToggleState();
    request.includeSong = ! session.hasSong() || songToggle.getToggleState();

    // An untouched slider means "the whole range" (no narrower trim).
    const bool trimmed = trimSlider.getMinValue() > 0.05 || trimSlider.getMaxValue() < trimSlider.getMaximum() - 0.05;
    request.startSeconds = range.getStart() + (trimmed ? trimSlider.getMinValue() : 0.0);
    request.endSeconds = trimmed ? range.getStart() + trimSlider.getMaxValue() : range.getEnd();
    return request;
}

void ExportSheet::updateEnablement()
{
    const auto range = computeRange (selectedRangeChoice());
    const bool hasContent = range.getLength() > 0.5;
    const bool hasVideo = hasContent && session.hasVideoBetween (range.getStart(), range.getEnd());
    const bool busy = status == Status::exporting;

    exportButton.setEnabled (hasVideo && ! busy && ClipExporter::isVideoExportSupported());
    audioButton.setEnabled (hasContent && ! busy);
    rangeBox.setEnabled (! busy && rangeBox.getNumItems() > 0);

    if (! hasContent && status == Status::idle)
        statusMessage = tr ("Record a take first: the ● button or the T key.");
    else if (hasContent && ! hasVideo && status == Status::idle)
        statusMessage = tr ("This range has no video (camera off) — export the audio only.");
    else if (status == Status::idle)
    {
        // What to expect: about 1.2 MB per second at this quality.
        const auto request = buildRequest();
        const double seconds = request.endSeconds - request.startSeconds;
        statusMessage = juce::String (juce::roundToInt (seconds)) + tr (" s · about ")
                        + juce::String (std::max (1, juce::roundToInt (seconds * 1.2))) + tr ("\u00a0MB")
                        + (session.hasSong() && ! songToggle.getToggleState() ? tr (" · guitar only") : juce::String());
    }
    repaint();
}

void ExportSheet::startExport()
{
    if (! exportButton.isEnabled())
        return;

    const auto request = buildRequest();

    status = Status::exporting;
    statusMessage = tr ("Encoding the video…");
    revealButton.setVisible (false);
    updateEnablement();

    juce::Component::SafePointer<ExportSheet> safe (this);
    session.exportClip (request, [safe] (bool ok, juce::String message, juce::File file)
    {
        if (safe == nullptr)
            return;
        safe->status = ok ? Status::done : Status::failed;
        safe->statusMessage = ok ? tr ("Clip exported: ") + file.getFileName() : message;
        safe->lastExport = file;
        safe->revealButton.setVisible (ok);
        safe->updateEnablement();
        safe->resized();
        if (ok)
            safe->session.notify (tr ("Clip exported: ") + file.getFileName());
    });
}

void ExportSheet::exportAudio()
{
    juce::String error;
    const auto file = session.exportAudio (buildRequest(), error);
    if (file.existsAsFile())
    {
        status = Status::done;
        statusMessage = tr ("Audio exported: ") + file.getFileName();
        lastExport = file;
        revealButton.setVisible (true);
    }
    else
    {
        status = Status::failed;
        statusMessage = error;
    }
    updateEnablement();
    resized();
}

void ExportSheet::refresh()
{
    if (status == Status::exporting)
        repaint (progressArea.expanded (2));
}

void ExportSheet::mouseDown (const juce::MouseEvent& e)
{
    if (! sheet.contains (e.getPosition()) && status != Status::exporting)
        close();
}

bool ExportSheet::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        startExport();
        return true;
    }
    return true; // modal: swallow the looper shortcuts while the sheet is open
}

void ExportSheet::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (juce::Colours::black.withAlpha (0.55f));

    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 40, { 0, 20 }).drawForRectangle (g, sheet);
    g.setColour (surface);
    roadie::theme::fillSquircle (g, sheet.toFloat(), radiusLg);
    g.setColour (border);
    roadie::theme::drawSquircle (g, sheet.toFloat().reduced (0.5f), radiusLg, 1.0f);

    g.setColour (text);
    g.setFont (fonts::display (17.0f));
    g.drawText (tr ("Export Clip"), sheet.reduced (20).removeFromTop (26), juce::Justification::centredLeft, false);

    ui::drawSectionLabel (g, tr ("Range"), rangeLabel.toFloat());
    ui::drawSectionLabel (g, "Format", formatLabel.toFloat());
    ui::drawSectionLabel (g, tr ("Trim"), trimLabel.toFloat());
    if (trimSlider.isEnabled())
    {
        const double a = trimSlider.getMinValue(), b = trimSlider.getMaxValue();
        g.setColour (textMuted);
        g.setFont (fonts::semibold (12.0f));
        fonts::drawTabular (g, formatPrecise (a) + utf8 (" \xe2\x86\x92 ") + formatPrecise (b) + utf8 (" \xc2\xb7 ")
                                   + juce::String (b - a, 1).replaceCharacter ('.', i18n::decimalPoint()) + " s",
                            trimLabel.toFloat(), juce::Justification::centredRight);
    }
    ui::drawSectionLabel (g, tr ("Clip title"), titleLabel.toFloat());

    if (status == Status::exporting)
    {
        const float progress = session.getExporter().getProgress();
        auto bar = progressArea.toFloat();
        g.setColour (surfaceSunken);
        roadie::theme::fillSquircle (g, bar, bar.getHeight() * 0.5f);
        g.setColour (exportAccent);
        roadie::theme::fillSquircle (g, bar.withWidth (std::max (bar.getHeight(), bar.getWidth() * progress)), bar.getHeight() * 0.5f);
        g.setColour (textMuted);
        g.setFont (fonts::semibold (12.0f));
        fonts::drawTabular (g, statusMessage + " " + juce::String (juce::roundToInt (progress * 100.0f)) + " %",
                            statusArea.toFloat(), juce::Justification::centredLeft);
    }
    else if (statusMessage.isNotEmpty())
    {
        g.setColour (status == Status::failed ? ink (warn) : (status == Status::done ? ink (ok) : textMuted));
        g.setFont (fonts::semibold (12.0f));
        g.drawFittedText (statusMessage, statusArea, juce::Justification::centredLeft, 2);
    }
}

void ExportSheet::resized()
{
    const int width = std::min (440, getWidth() - 40);
    songToggle.setVisible (session.hasSong());
    const int height = session.hasSong() ? 634 : 610;
    sheet = getLocalBounds().withSizeKeepingCentre (width, std::min (height, getHeight() - 40));

    auto area = sheet.reduced (20);
    auto header = area.removeFromTop (26);
    closeButton.setBounds (header.removeFromRight (26).withSizeKeepingCentre (26, 26));
    area.removeFromTop (16);

    rangeLabel = area.removeFromTop (16);
    area.removeFromTop (8);
    rangeBox.setBounds (area.removeFromTop (32));
    area.removeFromTop (16);

    formatLabel = area.removeFromTop (16);
    area.removeFromTop (8);
    auto formats = area.removeFromTop (72);
    verticalButton->setBounds (formats.removeFromLeft ((formats.getWidth() - 8) / 2));
    formats.removeFromLeft (8);
    horizontalButton->setBounds (formats);
    area.removeFromTop (16);

    trimLabel = area.removeFromTop (16);
    area.removeFromTop (4);
    trimSlider.setBounds (area.removeFromTop (26));
    area.removeFromTop (14);

    titleLabel = area.removeFromTop (16);
    area.removeFromTop (8);
    titleEditor.setBounds (area.removeFromTop (36));
    area.removeFromTop (10);
    burnInToggle.setBounds (area.removeFromTop (24));
    mirrorToggle.setBounds (area.removeFromTop (24));
    if (session.hasSong())
        songToggle.setBounds (area.removeFromTop (24));
    area.removeFromTop (14);

    exportButton.setBounds (area.removeFromTop (44));
    area.removeFromTop (10);

    auto secondary = area.removeFromTop (32);
    audioButton.setBounds (secondary.removeFromLeft (audioButton.getIdealWidth()));
    revealButton.setBounds (secondary.removeFromRight (revealButton.getIdealWidth()));
    area.removeFromTop (12);

    progressArea = area.removeFromTop (6);
    area.removeFromTop (6);
    statusArea = area.removeFromTop (34);
}
