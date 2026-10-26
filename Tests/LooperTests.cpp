#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

#include "Looper.h"

// Feeds one block of constant live input and returns the loop's own playback.
static std::vector<float> block (Looper& l, float value, int size = 100)
{
    std::vector<float> in ((size_t) size, value);
    std::vector<float> out ((size_t) size, -1.0f);
    l.process (in.data(), out.data(), size);
    return out;
}

static float sum (const std::vector<float>& v)
{
    float s = 0.0f;
    for (float x : v) s += x;
    return s;
}

int main()
{
    Looper looper;
    looper.prepare (1000.0, 1.0); // 1000 Hz "sample rate", 1s max loop -> easy maths

    assert (looper.getState() == Looper::State::Empty);
    assert (sum (block (looper, 1.0f)) == 0.0f); // empty looper outputs silence, never the live input

    // Record 300 samples of value 1.0 (3 blocks of 100)
    looper.requestRecord();
    for (int i = 0; i < 3; ++i)
        assert (sum (block (looper, 1.0f)) == 0.0f); // nothing to play back while recording
    assert (looper.getState() == Looper::State::Recording);

    looper.requestRecord(); // stop recording -> Playing, length fixed at 300
    block (looper, 0.0f);   // consumes the trigger, one block of playback
    assert (looper.getState() == Looper::State::Playing);
    assert (looper.getLayerCount() == 1);
    double lenSec = looper.getLoopLengthSeconds();
    printf ("loop length: %.3f s (expect ~0.300s, first block already played back)\n", lenSec);
    assert (lenSec > 0.29 && lenSec < 0.31);

    // Playback only contains the loop, whatever the live input is.
    float playSum = sum (block (looper, 0.25f));
    printf ("sum of playback block (recorded=1.0, expect ~100): %.2f\n", playSum);
    assert (playSum > 95.0f && playSum < 105.0f);

    // Restart puts the playhead back at the top of the loop.
    looper.requestRestart();
    block (looper, 0.0f, 10);
    assert (looper.getPlayheadFraction() > 0.03f && looper.getPlayheadFraction() < 0.04f);

    // Overdub: add 0.5 on top for one full loop pass (3 blocks), then stop.
    looper.requestOverdub();
    for (int i = 0; i < 3; ++i)
        block (looper, 0.5f);
    assert (looper.getState() == Looper::State::Overdubbing);
    assert (looper.getLayerCount() == 2);
    looper.requestOverdub();
    block (looper, 0.0f);
    assert (looper.getState() == Looper::State::Playing);

    // Fresh overdub + undo cycle: the 9.0 pass must disappear.
    looper.requestOverdub();
    for (int i = 0; i < 3; ++i) block (looper, 9.0f); // slam the loop with noise
    looper.requestOverdub();
    block (looper, 0.0f);
    assert (looper.getLayerCount() == 3);

    looper.requestUndo();
    block (looper, 0.0f); // consumes the undo trigger
    assert (looper.getLayerCount() == 2);

    auto afterUndo = block (looper, 0.0f);
    float maxVal = *std::max_element (afterUndo.begin(), afterUndo.end());
    printf ("max sample after undo (expect ~1.5, undo should have reverted the 9.0 overdub): %.2f\n", maxVal);
    assert (maxVal < 5.0f && maxVal > 1.0f);

    // Overview reflects the loop content.
    std::vector<float> peaks (10);
    looper.getOverview (peaks.data(), (int) peaks.size());
    assert (peaks[0] > 1.0f && peaks[9] > 1.0f);

    // Stop is not a toggle.
    looper.requestStop();
    block (looper, 0.0f);
    looper.requestStop();
    block (looper, 0.0f);
    assert (looper.getState() == Looper::State::Stopped);

    // A device restart at the same rate keeps the loop.
    looper.prepare (1000.0, 1.0);
    assert (looper.getRecordedLengthSamples() == 300);

    looper.requestClear();
    block (looper, 0.0f);
    assert (looper.getState() == Looper::State::Empty);
    assert (looper.getLoopLengthSeconds() == 0.0);
    assert (looper.getLayerCount() == 0);

    // Loading a saved loop (project open) leaves it ready to play.
    std::vector<float> saved (200, 0.5f);
    looper.setContent (saved.data(), (int) saved.size(), 4);
    assert (looper.getState() == Looper::State::Playing);
    assert (looper.getLayerCount() == 4);
    float loadedSum = sum (block (looper, 0.0f));
    printf ("sum after loading a saved loop (expect 50): %.2f\n", loadedSum);
    assert (loadedSum > 49.0f && loadedSum < 51.0f);

    // The head follows the timeline: setPlayhead wraps into the loop.
    std::vector<float> ramp (100);
    for (int i = 0; i < 100; ++i)
        ramp[(size_t) i] = (float) i;
    looper.setContent (ramp.data(), 100, 1);
    looper.setPlayhead (-30); // 70 in the loop
    std::vector<float> in (3, 0.0f), out (3, 0.0f);
    looper.process (in.data(), out.data(), 3);
    assert (out[0] == 70.0f && out[2] == 72.0f);
    looper.setPlayhead (250); // 50
    looper.process (in.data(), out.data(), 1);
    assert (out[0] == 50.0f);

    printf ("ALL LOOPER LOGIC TESTS PASSED\n");
    return 0;
}
