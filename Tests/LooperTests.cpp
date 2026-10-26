#include <cassert>
#include <cstdio>
#include <vector>

#include "Looper.h"

static void block (Looper& l, std::vector<float>& buf, float value)
{
    std::fill (buf.begin(), buf.end(), value);
    l.process (buf.data(), (int) buf.size());
}

int main()
{
    Looper looper;
    looper.prepare (1000.0, 1.0); // 1000 Hz "sample rate", 1s max loop -> easy maths
    std::vector<float> buf (100, 0.0f);

    assert (looper.getState() == Looper::State::Empty);

    // Record 300 samples of value 1.0 (3 blocks of 100)
    looper.requestRecord();
    for (int i = 0; i < 3; ++i)
        block (looper, buf, 1.0f);
    assert (looper.getState() == Looper::State::Recording);

    looper.requestRecord(); // stop recording -> Playing, length fixed at 300
    block (looper, buf, 0.0f); // consumes the trigger, one block of playback
    assert (looper.getState() == Looper::State::Playing);
    double lenSec = looper.getLoopLengthSeconds();
    printf ("loop length: %.3f s (expect ~0.300s, first block already played back)\n", lenSec);
    assert (lenSec > 0.29 && lenSec < 0.31);

    // Playback: with silent live input, output should now contain the recorded 1.0 content.
    std::vector<float> play (100, 0.0f);
    looper.process (play.data(), 100);
    float sum = 0.0f;
    for (float v : play) sum += v;
    printf ("sum of playback block (silent input, expect ~100 since recorded=1.0): %.2f\n", sum);
    assert (sum > 95.0f);

    // Overdub: add 0.5 on top for one full loop pass (3 blocks), then stop.
    looper.requestOverdub();
    for (int i = 0; i < 3; ++i)
        block (looper, buf, 0.5f);
    assert (looper.getState() == Looper::State::Overdubbing);
    looper.requestOverdub();
    block (looper, buf, 0.0f);
    assert (looper.getState() == Looper::State::Playing);

    // Undo should NOT be available anymore (consumed), but let's test it reverted before this call.
    // Re-check via a fresh overdub + undo cycle:
    looper.requestOverdub();
    for (int i = 0; i < 3; ++i) block (looper, buf, 9.0f); // slam the loop with noise
    looper.requestOverdub();
    block (looper, buf, 0.0f);

    looper.requestUndo();
    block (looper, buf, 0.0f); // consumes the undo trigger

    std::vector<float> afterUndo (100, 0.0f);
    looper.process (afterUndo.data(), 100);
    float maxVal = 0.0f;
    for (float v : afterUndo) maxVal = std::max (maxVal, v);
    printf ("max sample after undo (expect NOT ~9-10, undo should have reverted the 9.0 overdub): %.2f\n", maxVal);
    assert (maxVal < 5.0f);

    looper.requestClear();
    block (looper, buf, 0.0f);
    assert (looper.getState() == Looper::State::Empty);
    assert (looper.getLoopLengthSeconds() == 0.0);

    printf ("ALL LOOPER LOGIC TESTS PASSED\n");
    return 0;
}
