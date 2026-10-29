// Tests for the JUCE-free DSP helpers: tone stack, metronome, sync onset
// detection and the CPU load word. Build and run (no JUCE needed):
//   clang++ -std=c++20 -O2 -ISource Tests/DspTests.cpp Source/ToneStack.cpp \
//     Source/SyncAnalysis.cpp Source/Metronome.cpp -o /tmp/dsp_tests && /tmp/dsp_tests

#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "CpuLoad.h"
#include "Metronome.h"
#include "SyncAnalysis.h"
#include "ToneStack.h"

static double db (double gain) { return 20.0 * std::log10 (gain); }

static void testToneStack()
{
    ToneStack tone;
    tone.prepare (48000.0);

    std::vector<float> buf (64, 0.0f);
    tone.process (buf.data(), (int) buf.size()); // applies the (flat) coefficients
    assert (std::abs (db (tone.getMagnitudeAt (1000.0))) < 0.05);

    tone.setGainDb (ToneStack::bass, 12.0f);
    tone.process (buf.data(), (int) buf.size());
    printf ("bass +12: 40 Hz %.1f dB, 5 kHz %.1f dB\n", db (tone.getMagnitudeAt (40.0)), db (tone.getMagnitudeAt (5000.0)));
    assert (db (tone.getMagnitudeAt (40.0)) > 10.0);
    assert (std::abs (db (tone.getMagnitudeAt (5000.0))) < 1.0);

    tone.setGainDb (ToneStack::bass, 0.0f);
    tone.setGainDb (ToneStack::treble, -12.0f);
    tone.process (buf.data(), (int) buf.size());
    printf ("treble -12: 10 kHz %.1f dB, 100 Hz %.1f dB\n", db (tone.getMagnitudeAt (10000.0)), db (tone.getMagnitudeAt (100.0)));
    assert (db (tone.getMagnitudeAt (10000.0)) < -10.0);
    assert (std::abs (db (tone.getMagnitudeAt (100.0))) < 1.0);

    tone.setGainDb (ToneStack::treble, 0.0f);
    tone.setGainDb (ToneStack::mid, 8.0f);
    tone.process (buf.data(), (int) buf.size());
    assert (std::abs (db (tone.getMagnitudeAt (750.0)) - 8.0) < 0.5);

    tone.setGainDb (ToneStack::presence, 50.0f); // clamped to the max
    assert (tone.getGainDb (ToneStack::presence) == ToneStack::maxGainDb);

    // A sine through a boosted chain must stay finite (no unstable poles).
    std::vector<float> sine (48000);
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = 0.5f * (float) std::sin (2.0 * 3.14159265 * 220.0 * (double) i / 48000.0);
    tone.process (sine.data(), (int) sine.size());
    for (float s : sine)
        assert (std::isfinite (s) && std::abs (s) < 10.0f);
}

static std::vector<float> noiseWithImpulse (double sampleRate, double seconds, double impulseAt, float noiseLevel, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-noiseLevel, noiseLevel);
    std::vector<float> v ((size_t) (sampleRate * seconds));
    for (auto& s : v)
        s = dist (rng);

    // Decaying burst, like a clap or a muted strum.
    const auto start = (size_t) (impulseAt * sampleRate);
    for (size_t i = 0; i < (size_t) (0.05 * sampleRate) && start + i < v.size(); ++i)
        v[start + i] += 0.8f * (float) std::exp (-(double) i / (0.005 * sampleRate)) * (i % 2 == 0 ? 1.0f : -1.0f);
    return v;
}

static void testSync()
{
    // Interface at 48 kHz, camera mic at 44.1 kHz, both hearing the same event.
    const double eventHostTime = 100.750;
    const double interfaceStart = 100.0, cameraStart = 100.2;
    const double trueLag = 0.023; // interface timestamps are 23 ms late

    auto a = noiseWithImpulse (48000.0, 3.0, eventHostTime - interfaceStart + trueLag, 0.01f, 1);
    auto b = noiseWithImpulse (44100.0, 3.0, eventHostTime - cameraStart, 0.03f, 2);

    auto result = roadie::sync::measureLag ({ a.data(), (int) a.size(), 48000.0, interfaceStart },
                                            { b.data(), (int) b.size(), 44100.0, cameraStart });
    printf ("sync lag: measured %.2f ms (expect %.2f ms)\n", result.interfaceLagSeconds * 1000.0, trueLag * 1000.0);
    assert (result.ok);
    assert (std::abs (result.interfaceLagSeconds - trueLag) < 0.001);

    // Pure noise: nothing to lock onto.
    auto silent = noiseWithImpulse (48000.0, 2.0, 10.0, 0.01f, 3);
    assert (roadie::sync::findOnset (silent.data(), (int) silent.size(), 48000.0) == -1);
}

static void testMetronome()
{
    Metronome m;
    m.prepare (48000.0);
    m.setBpm (120.0); // 24000 samples per beat
    m.setEnabled (true);

    // Process in small blocks like a real device would.
    std::vector<float> out (96000, 0.0f);
    for (int start = 0; start < (int) out.size(); start += 64)
        m.process (out.data() + start, 64, start, true);

    // A click is a burst of non-zero samples after a long silent gap.
    int clicks = 0, silentRun = 1000;
    for (float s : out)
    {
        if (s != 0.0f && silentRun >= 1000)
            ++clicks;
        silentRun = s == 0.0f ? silentRun + 1 : 0;
    }
    printf ("metronome: %d clicks over 4 beats\n", clicks);
    assert (clicks == 4);

    // Stopped transport: silence.
    std::vector<float> quiet (4800, 0.0f);
    m.process (quiet.data(), (int) quiet.size(), 96000, false);
    for (float s : quiet)
        assert (s == 0.0f);
}

static void testCpuLoad()
{
    constexpr double frame = 1.0 / 30.0; // the UI timer
    auto run = [] (CpuLoad& meter, double load, double seconds)
    {
        for (double t = 0.0; t < seconds; t += frame)
            meter.push (load, frame);
    };

    CpuLoad meter;
    run (meter, 0.20, 3.0);
    assert (meter.getLevel() == CpuLoad::Level::low);

    // A single spike doesn't change the word: it is averaged over ~2 s.
    meter.push (1.0, frame);
    assert (meter.getLevel() == CpuLoad::Level::low);

    // A real change does, within a few seconds.
    run (meter, 0.50, 1.0);
    printf ("cpu 20 %% -> 50 %%: average %.2f after 1 s\n", meter.getAverage());
    assert (meter.getLevel() == CpuLoad::Level::low);
    run (meter, 0.50, 4.0);
    assert (meter.getLevel() == CpuLoad::Level::normal);
    run (meter, 0.90, 6.0);
    assert (meter.getLevel() == CpuLoad::Level::high);

    // Hysteresis: just under a threshold keeps the current word.
    assert (CpuLoad::classify (0.36, CpuLoad::Level::normal) == CpuLoad::Level::normal);
    assert (CpuLoad::classify (0.36, CpuLoad::Level::low) == CpuLoad::Level::low);
    assert (CpuLoad::classify (0.68, CpuLoad::Level::high) == CpuLoad::Level::high);
    assert (CpuLoad::classify (0.68, CpuLoad::Level::normal) == CpuLoad::Level::normal);
    assert (CpuLoad::classify (0.30, CpuLoad::Level::normal) == CpuLoad::Level::low);
    assert (CpuLoad::classify (0.75, CpuLoad::Level::normal) == CpuLoad::Level::high);
    assert (CpuLoad::classify (0.10, CpuLoad::Level::high) == CpuLoad::Level::low);
}

int main()
{
    testToneStack();
    testSync();
    testMetronome();
    testCpuLoad();
    printf ("ALL DSP TESTS PASSED\n");
    return 0;
}
