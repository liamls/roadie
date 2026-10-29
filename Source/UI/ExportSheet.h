#pragma once

#include "../Session.h"
#include "Widgets.h"

// Modal export sheet (.sheet) over a dimmed backdrop: pick a range (whole
// song, cycle zone, or the current selection), a format (vertical 9:16 for
// Reels/Shorts/TikTok, horizontal 16:9 for YouTube), a title, then render
// the MP4 — or just the range's audio.
class ExportSheet : public juce::Component
{
public:
    explicit ExportSheet (Session& session);
    ~ExportSheet() override;

    std::function<void()> onClose;

    void open();
    void close();
    void refresh(); // progress while exporting

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class FormatButton;

    // The three things a user might mean by "export this": the whole
    // arrangement, the cycle zone, or whatever is currently selected.
    enum class RangeChoice { wholeSong, cycle, selection };
    juce::Range<double> computeRange (RangeChoice) const;
    RangeChoice selectedRangeChoice() const noexcept { return (RangeChoice) juce::jlimit (0, 2, rangeBox.getSelectedId() - 1); }
    void populateRangeChoices();

    void startExport();
    void exportAudio();
    void updateTrimRange();
    Session::ExportRequest buildRequest() const;
    void updateEnablement();
    juce::String defaultTitle() const;

    Session& session;
    juce::ComboBox rangeBox;
    std::unique_ptr<FormatButton> verticalButton, horizontalButton;
    juce::TextEditor titleEditor;
    juce::Slider trimSlider { juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox };
    juce::ToggleButton burnInToggle, mirrorToggle, songToggle;
    roadie::ui::PillButton exportButton, audioButton, revealButton;
    roadie::ui::IconButton closeButton { "Close", roadie::icons::Icon::close };

    enum class Status { idle, exporting, done, failed };
    Status status = Status::idle;
    juce::String statusMessage;
    juce::File lastExport;
    bool vertical = true;

    juce::Rectangle<int> sheet, rangeLabel, formatLabel, trimLabel, titleLabel, statusArea, progressArea;
};
