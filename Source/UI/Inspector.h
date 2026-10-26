#pragma once

#include "../Session.h"
#include "Widgets.h"

// Bottom contextual panel (.inspector): the controls of the selected track,
// with that module's accent bar on the left.
class Inspector : public juce::Component
{
public:
    explicit Inspector (Session& session);
    ~Inspector() override;

    void show (Session::Track track);
    Session::Track getShown() const noexcept { return shown; }

    // Called by the main window's UI timer.
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredHeight = 118;

private:
    class AmpPanel;
    class LooperPanel;
    class CameraPanel;

    Session& session;
    Session::Track shown = Session::Track::amp;
    std::unique_ptr<AmpPanel> ampPanel;
    std::unique_ptr<LooperPanel> looperPanel;
    std::unique_ptr<CameraPanel> cameraPanel;
};
