#pragma once

#include "../Session.h"
#include "Widgets.h"

// The video monitor, a panel of the layout (no longer a floating window).
// In Jouer it shows the camera live, mirrored like a selfie view, with the
// 9:16 and 16:9 framing guides; while the arrangement plays back (or right
// after a locate) it shows the take under the playhead. In Monter the
// camera is off: it only ever shows the takes. The picture keeps its
// proportions whatever the panel's shape (black bars around it).
class Monitor : public juce::Component,
                public juce::SettableTooltipClient
{
public:
    explicit Monitor (Session& session);

    // Monter: playback only, the camera is switched off.
    void setPlaybackOnly (bool shouldBePlaybackOnly);
    bool isPlaybackOnly() const noexcept { return playbackOnly; }

    std::function<void()> onCameraClicked; // click on a switched-off camera
    std::function<void()> onDoubleClicked; // enlarge / restore the panel

    // Count-in shown over the picture (beats left, 0 = none).
    void setCountIn (int beatsLeft);

    // Called by the main window's UI timer.
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    // Where the picture lands: the largest `aspect` rectangle inside `area`.
    static juce::Rectangle<float> fitPicture (juce::Rectangle<float> area, float aspect);

    static constexpr int barHeight = 26;

private:
    bool isLive() const;

    Session& session;
    bool playbackOnly = false;
    int countIn = 0;
    int lastFrame = -1, lastPreviewFrame = -1;
    bool wasPreviewing = false;
};
