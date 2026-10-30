// Tests for the JUCE-free DSP helpers: tone stack, metronome, sync onset
// detection and the CPU load word. Build and run (no JUCE needed):
//   clang++ -std=c++20 -O2 -ISource Tests/DspTests.cpp Source/ToneStack.cpp \
//     Source/SyncAnalysis.cpp Source/Metronome.cpp -o /tmp/dsp_tests && /tmp/dsp_tests

#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <tuple>
#include <vector>

#include "Autosave.h"
#include "CpuLoad.h"
#include "Metronome.h"
#include "NoiseGate.h"
#include "SongAnalysis.h"
#include "SyncAnalysis.h"
#include "TimeStretch.h"
#include "ToneStack.h"
#include "Tuner.h"

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

static void testSyncFromPlaying()
{
    // Eight seconds of strumming on the interface; the camera's microphone
    // hears the same strums 70 ms later (as timestamps tell it), through a
    // noisy room, and started listening 0.3 s after the interface.
    const double sr = 48000.0;
    std::mt19937 random (5);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    std::vector<float> a ((size_t) (8.0 * sr), 0.0f), b ((size_t) (8.0 * sr), 0.0f);
    const double strums[] = { 0.8, 1.9, 2.4, 3.7, 4.1, 5.6, 6.2, 7.3 };
    const double interfaceStart = 1000.0, cameraStart = 1000.3, lag = 0.070;
    for (auto t : strums)
    {
        for (int i = 0; i < (int) (0.25 * sr); ++i)
        {
            const float env = std::exp (-12.0f * (float) i / (float) sr);
            const float tone = env * std::sin (2.0f * 3.14159265f * 196.0f * (float) i / (float) sr);
            const auto ia = (size_t) (t * sr) + (size_t) i;
            // Event at interface time t; the camera heard it at host time (interfaceStart + t - lag).
            const auto ib = (size_t) ((interfaceStart + t - lag - cameraStart) * sr) + (size_t) i;
            if (ia < a.size()) a[ia] += 0.4f * tone;
            if (ib < b.size()) b[ib] += 0.15f * tone;
        }
    }
    for (auto& v : b)
        v += 0.01f * noise (random);

    const auto r = roadie::sync::measureLagFromPlaying ({ a.data(), (int) a.size(), sr, interfaceStart }, { b.data(), (int) b.size(), sr, cameraStart });
    printf ("sync from playing: ok=%d lag %.1f ms (expect +70)\n", r.ok, r.interfaceLagSeconds * 1000.0);
    assert (r.ok && std::abs (r.interfaceLagSeconds - lag) < 0.006);

    // Silence on the camera side: nothing to match, no verdict.
    std::vector<float> quiet (b.size(), 0.0f);
    assert (! roadie::sync::measureLagFromPlaying ({ a.data(), (int) a.size(), sr, interfaceStart }, { quiet.data(), (int) quiet.size(), sr, cameraStart }).ok);
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

    // Count-in: metronome off, clicks only before the count-in end.
    Metronome c;
    c.prepare (48000.0);
    c.setBpm (120.0);
    c.setCountInEnd (48000); // two beats
    std::vector<float> counted (96000, 0.0f);
    for (int start = 0; start < (int) counted.size(); start += 64)
        c.process (counted.data() + start, 64, start, true);
    int countClicks = 0, gap = 1000;
    for (float s : counted)
    {
        if (s != 0.0f && gap >= 1000)
            ++countClicks;
        gap = s == 0.0f ? gap + 1 : 0;
    }
    printf ("count-in: %d clicks (expect 2)\n", countClicks);
    assert (countClicks == 2);
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

// A synthetic drum track: silence, a quiet hi-hat intro, then kick / snare /
// hats, louder with a bass line in the last part.
static std::vector<float> drumTrack (double sr, double bpm, double offset, int introBars, int bars)
{
    const double beat = 60.0 / bpm;
    std::vector<float> out ((size_t) ((offset + bars * 4 * beat + 1.0) * sr), 0.0f);
    std::mt19937 random (7);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    auto hit = [&] (double t, double seconds, auto voice)
    {
        for (size_t i = (size_t) (t * sr); i < out.size() && i < (size_t) ((t + seconds) * sr); ++i)
            out[i] += voice ((double) i / sr - t);
    };

    for (int b = 0; b < bars; ++b)
        for (int k = 0; k < 8; ++k)
        {
            const double t = offset + (b * 4 + k * 0.5) * beat;
            hit (t, 0.05, [&] (double u) { return 0.12f * noise (random) * (float) std::exp (-u * 80.0); }); // hat
            if (b < introBars || k % 2 != 0)
                continue;
            const int beatInBar = k / 2;
            if (beatInBar == 0 || beatInBar == 2) // kick, louder on the one
                hit (t, 0.25, [&] (double u) { return (beatInBar == 0 ? 0.9f : 0.55f) * (float) (std::sin (2.0 * 3.14159265 * 55.0 * u) * std::exp (-u * 14.0)); });
            else // snare
                hit (t, 0.15, [&] (double u) { return 0.45f * noise (random) * (float) std::exp (-u * 30.0); });
            if (b >= bars - 8) // bass line in the last part
                hit (t, beat * 0.9, [&] (double u) { return 0.35f * (float) std::sin (2.0 * 3.14159265 * 82.4 * u); });
        }
    return out;
}

static void testSongAnalysis()
{
    const double sr = 44100.0, bpm = 92.0, offset = 0.73;
    const auto track = drumTrack (sr, bpm, offset, 8, 40);
    const auto a = roadie::song::analyse (track.data(), (int) track.size(), sr, 4);
    printf ("song: %.2f BPM (expect 92), first downbeat %.3f s (expect %.3f), confidence %.2f, %d sections:",
            a.bpm, a.firstDownbeatSeconds, offset + 8 * 4 * 60.0 / bpm * 0.0, a.confidence, (int) a.sections.size());
    for (const auto& s : a.sections)
        printf (" %s@%.1f", s.label.c_str(), s.startSeconds);
    printf ("\n");
    assert (a.ok);
    assert (std::abs (a.bpm - bpm) < 0.3);
    // The intro has no kick: the bass decides the one from the groove, on the same grid.
    const double barSeconds = 4.0 * 60.0 / bpm;
    const double phaseError = std::fmod (std::abs (a.firstDownbeatSeconds - offset) + barSeconds * 0.5, barSeconds) - barSeconds * 0.5;
    assert (std::abs (phaseError) < 0.03);
    assert (a.firstDownbeatSeconds < offset + barSeconds); // the first bar, not a later one

    // The intro (8 bars of hats) is its own part, the groove comes after it.
    assert (a.sections.size() >= 2);
    const double grooveStart = offset + 8 * barSeconds;
    bool found = false;
    for (const auto& s : a.sections)
        found = found || std::abs (s.startSeconds - grooveStart) < barSeconds * 1.01;
    assert (found);
    assert (a.sections.front().label == "Intro");

    // Tap tempo: the median interval, a stray tap ignored.
    assert (roadie::song::tempoFromTaps ({ 0.0, 0.5, 1.0 }) == 0.0);
    const double tapped = roadie::song::tempoFromTaps ({ 0.0, 0.652, 1.304, 1.957, 2.4, 2.609, 3.261 });
    printf ("tap tempo: %.1f BPM (expect 92)\n", tapped);
    assert (std::abs (tapped - 92.0) < 0.6);
}

static void testNoiseGate()
{
    NoiseGate gate;
    gate.prepare (48000.0);
    gate.setThresholdDb (-40.0f);

    // Off: untouched.
    std::vector<float> hiss (4800, 0.003f); // ≈ −50 dBFS
    gate.process (hiss.data(), (int) hiss.size());
    assert (hiss.back() == 0.003f);

    // On: hiss below the threshold fades out once the hold is over…
    gate.setEnabled (true);
    for (int i = 0; i < 10; ++i)
    {
        std::fill (hiss.begin(), hiss.end(), 0.003f);
        gate.process (hiss.data(), (int) hiss.size());
    }
    printf ("gate: hiss after 1 s %.6f (expect ~0)\n", hiss.back());
    assert (std::abs (hiss.back()) < 1.0e-5f && gate.getOpenness() < 0.01f);

    // …a note above it opens it within a few milliseconds…
    std::vector<float> note (480);
    for (size_t i = 0; i < note.size(); ++i)
        note[i] = 0.3f * (float) std::sin (2.0 * 3.14159265 * 220.0 * (double) i / 48000.0);
    gate.process (note.data(), (int) note.size());
    const float expected = 0.3f * (float) std::sin (2.0 * 3.14159265 * 220.0 * 400.0 / 48000.0);
    assert (std::abs (note[400] - expected) < 1.0e-4f && gate.getOpenness() > 0.99f);

    // …and a note decaying just under the threshold doesn't chatter (hysteresis).
    std::vector<float> tail (4800, 0.008f); // −42 dBFS: under −40, above −46
    gate.process (tail.data(), (int) tail.size());
    assert (tail.back() > 0.0079f);
}

static void testTuner()
{
    // A string-like tone: fundamental plus decaying harmonics, slightly flat.
    auto pluck = [] (double hz)
    {
        std::vector<float> v (2048);
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double t = (double) i / 48000.0;
            double sample = 0.0;
            for (int h = 1; h <= 6; ++h)
                sample += std::sin (2.0 * 3.14159265358979 * hz * h * t) / h;
            v[i] = (float) (0.2 * sample);
        }
        return v;
    };

    const auto lowE = pluck (82.41 * std::pow (2.0, -10.0 / 1200.0)); // E2, 10 cents flat
    const auto e = roadie::tuner::detect (lowE.data(), (int) lowE.size(), 48000.0);
    printf ("tuner: %s%d %+.1f cents (%.2f Hz), expect E2 -10\n", e.note.c_str(), e.octave, e.cents, e.hz);
    assert (e.ok && e.note == "E" && e.octave == 2 && std::abs (e.cents + 10.0) < 3.0);

    const auto a = pluck (110.0);
    const auto r = roadie::tuner::detect (a.data(), (int) a.size(), 48000.0);
    assert (r.ok && r.note == "A" && r.octave == 2 && std::abs (r.cents) < 2.0);

    const auto highE = pluck (329.63);
    const auto h = roadie::tuner::detect (highE.data(), (int) highE.size(), 48000.0);
    assert (h.ok && h.note == "E" && h.octave == 4 && std::abs (h.cents) < 2.0);

    std::vector<float> silence (2048, 0.0f);
    assert (! roadie::tuner::detect (silence.data(), (int) silence.size(), 48000.0).ok);

    // Accuracy: the six open strings (and E5), harmonics with random phases,
    // at both usual rates — within half a cent of the truth.
    for (double hz : { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 659.26 })
        for (double rate : { 44100.0, 48000.0 })
        {
            std::vector<float> v (2048);
            for (size_t i = 0; i < v.size(); ++i)
            {
                double sample = 0.0;
                for (int k = 1; k <= 8; ++k)
                    sample += std::sin (2.0 * 3.14159265358979 * hz * k * (double) i / rate + k) * std::exp (-0.3 * k);
                v[i] = (float) (0.2 * sample);
            }
            const auto reading = roadie::tuner::detect (v.data(), (int) v.size(), rate);
            const double truth = 1200.0 * std::log2 (hz / (440.0 * std::pow (2.0, std::round (12.0 * std::log2 (hz / 440.0)) / 12.0)));
            assert (reading.ok && std::abs (reading.cents - truth) < (hz > 600.0 ? 1.0 : 0.5));
        }

    // A plucked string (Karplus-Strong: noisy attack, harmonics that decay at
    // different rates), read 100 ms after the pluck like a player would: the
    // right note and octave. (The synthesis itself is only tuned to a few
    // cents, hence the loose tolerance; the accuracy is checked above.)
    for (const auto& [hz, note, octave] : { std::tuple<double, const char*, int> { 82.41, "E", 2 }, { 110.0, "A", 2 }, { 146.83, "D", 3 },
                                            { 196.0, "G", 3 }, { 246.94, "B", 3 }, { 329.63, "E", 4 } })
    {
        const double sr = 48000.0;
        const double period = sr / hz;
        const int length = (int) period;
        const double fraction = period - length; // fractional delay: allpass-free linear tuning
        std::mt19937 random (3);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        std::vector<float> line ((size_t) length + 2);
        for (auto& v : line)
            v = noise (random) * 0.5f;
        std::vector<float> string ((size_t) (0.25 * sr));
        size_t index = 0;
        float previous = 0.0f;
        for (auto& out : string)
        {
            const float current = line[index];
            const float next = line[(index + 1) % line.size()];
            out = current;
            // Average of two neighbours (the KS low-pass), fractional delay by interpolation.
            const float averaged = 0.996f * 0.5f * (current + next);
            line[index] = averaged * (float) (1.0 - fraction) + previous * (float) fraction;
            previous = averaged;
            index = (index + 1) % (size_t) length;
        }
        const auto reading = roadie::tuner::detect (string.data() + (size_t) (0.1 * sr), 2048, sr);
        printf ("plucked %s%d: %s%d %+.1f cents\n", note, octave, reading.note.c_str(), reading.octave, reading.cents);
        assert (reading.ok && reading.note == note && reading.octave == octave && std::abs (reading.cents) < 15.0);
    }
}

static void testTimeStretch()
{
    // A 220 Hz tone, 2 s: at 75 % it lasts 2.67 s and keeps its pitch;
    // up 2 semitones at full speed it lasts 2 s and sounds at 247 Hz.
    const double sr = 48000.0;
    std::vector<float> tone ((size_t) (2.0 * sr));
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.3f * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * (double) i / sr);

    auto pitchOf = [sr] (const std::vector<float>& v)
    {
        const auto r = roadie::tuner::detect (v.data() + v.size() / 2, 2048, sr);
        return r.ok ? r.hz : 0.0;
    };
    auto rms = [] (const std::vector<float>& v)
    {
        double e = 0.0;
        for (size_t i = v.size() / 4; i < v.size() * 3 / 4; ++i)
            e += (double) v[i] * v[i];
        return std::sqrt (e / (double) (v.size() / 2));
    };

    std::vector<float> l, r;
    roadie::stretch::process (tone, tone, sr, 0.75, 0, l, r);
    printf ("stretch 75%%: %.2f s, %.1f Hz, rms %.3f (expect 2.67 s, 220 Hz, 0.212)\n", l.size() / sr, pitchOf (l), rms (l));
    assert (std::abs (l.size() / sr - 2.0 / 0.75) < 0.01 && std::abs (pitchOf (l) - 220.0) < 1.5 && std::abs (rms (l) - 0.212) < 0.03);

    // On a real groove the beat survives the stretch: the slowed drum track
    // still analyses at 75 % of its tempo, with the same parts.
    {
        const double drumRate = 44100.0;
        const auto drums = drumTrack (drumRate, 92.0, 0.73, 8, 40);
        std::vector<float> slowLeft, slowRight;
        roadie::stretch::process (drums, drums, drumRate, 0.75, 0, slowLeft, slowRight);
        const auto slow = roadie::song::analyse (slowLeft.data(), (int) slowLeft.size(), drumRate, 4);
        printf ("stretched groove: %.2f BPM (expect 69.00), first downbeat %.3f s (expect %.3f), %d parts\n",
                slow.bpm, slow.firstDownbeatSeconds, 0.73 / 0.75, (int) slow.sections.size());
        assert (slow.ok && std::abs (slow.bpm - 69.0) < 0.3);
        assert (slow.sections.size() >= 2 && slow.sections.front().label == "Intro");
    }

    roadie::stretch::process (tone, tone, sr, 1.0, 2, l, r);
    printf ("transpose +2: %.2f s, %.1f Hz (expect 2.00 s, 246.9 Hz)\n", l.size() / sr, pitchOf (l));
    assert (std::abs (l.size() / sr - 2.0) < 0.01 && std::abs (pitchOf (l) - 246.94) < 2.0);
}

static void testAutosavePolicy()
{
    AutosavePolicy a;
    assert (! a.isDue (100.0, false)); // nothing to save

    a.markChanged (10.0);
    assert (! a.isDue (11.0, false));                                  // still within the quiet period
    assert (a.isDue (10.0 + AutosavePolicy::quietSeconds, false));     // quiet long enough
    assert (! a.isDue (20.0, true));                                   // busy (recording / exporting): wait

    // A drag that keeps changing things: debounced, but capped.
    AutosavePolicy b;
    for (double t = 0.0; t < 29.0; t += 0.5)
    {
        b.markChanged (t);
        assert (! b.isDue (t, false));
    }
    b.markChanged (30.0);
    assert (b.isDue (30.0, false)); // 30 s since the first change

    b.markSaved();
    assert (! b.isDue (100.0, false) && ! b.hasPending());
    b.markChanged (200.0);
    assert (! b.isDue (201.0, false)); // the cap restarts from the new first change
    printf ("autosave policy: OK\n");
}

int main()
{
    testAutosavePolicy();
    testTimeStretch();
    testSyncFromPlaying();
    testTuner();
    testNoiseGate();
    testSongAnalysis();
    testToneStack();
    testSync();
    testMetronome();
    testCpuLoad();
    printf ("ALL DSP TESTS PASSED\n");
    return 0;
}
