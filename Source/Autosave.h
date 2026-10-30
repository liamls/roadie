#pragma once

#include <algorithm>

// When to save the project on its own after an edit. No JUCE, so it is tested
// in a few seconds: a quiet period after the last change (a fader drag is one
// save, not sixty), but never longer than maxWaitSeconds under continuous
// changes, so a crash costs at most a few seconds of work.
class AutosavePolicy
{
public:
    static constexpr double quietSeconds = 3.0;
    static constexpr double maxWaitSeconds = 30.0;

    void markChanged (double now) noexcept
    {
        if (! pending)
            firstChange = now;
        lastChange = now;
        pending = true;
    }

    // busy: recording, exporting… moments when writing the project would compete with the audio.
    bool isDue (double now, bool busy) const noexcept
    {
        if (! pending || busy)
            return false;
        return now - lastChange >= quietSeconds || now - firstChange >= maxWaitSeconds;
    }

    void markSaved() noexcept { pending = false; }
    bool hasPending() const noexcept { return pending; }

private:
    bool pending = false;
    double firstChange = 0.0, lastChange = 0.0;
};
