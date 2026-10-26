// Integration tests for the real-time engine and the project/export
// plumbing, driven by a fake audio device (no interface, camera or human
// needed). Built by CMake as RoadieEngineTests; run with `ctest`.

#include <juce_gui_basics/juce_gui_basics.h>

#include <cassert>
#include <cmath>
#include <cstdio>

#include "AudioEngine.h"
#include "ClipExporter.h"
#include "Project.h"
#include "SyncAnalysis.h"

#if JUCE_MAC
 #include "TestMovie.h"
#endif

#define CHECK(cond)                                                                 \
    do                                                                              \
    {                                                                               \
        if (! (cond))                                                               \
        {                                                                           \
            std::printf ("FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__);        \
            std::exit (1);                                                          \
        }                                                                           \
    } while (false)

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;

// Minimal device: AudioEngine only reads its format and latencies.
class FakeDevice : public juce::AudioIODevice
{
public:
    FakeDevice() : juce::AudioIODevice ("Fake", "Fake") {}
    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override { return { "In" }; }
    juce::Array<double> getAvailableSampleRates() override { return { sampleRate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { blockSize }; }
    int getDefaultBufferSize() override { return blockSize; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return 1; }
    int getOutputLatencyInSamples() override { return 32; }
    int getInputLatencyInSamples() override { return 32; }
};

// Drives the engine like CoreAudio would: fixed-size blocks, host timestamps.
struct Rig
{
    AudioEngine engine;
    FakeDevice device;
    std::uint64_t hostNs = 1'000'000'000;
    std::int64_t samplesFed = 0;
    std::vector<float> lastOutput;

    Rig() { engine.audioDeviceAboutToStart (&device); }

    // Feeds `seconds` of input produced by `source(sampleIndex)`; returns the peak output.
    float run (double seconds, const std::function<float (std::int64_t)>& source)
    {
        float input[blockSize], left[blockSize], right[blockSize];
        const float* inputs[] = { input };
        float* outputs[] = { left, right };
        float peak = 0.0f;
        lastOutput.clear();

        for (int done = 0; done < (int) (seconds * sampleRate); done += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
                input[i] = source (samplesFed + i);

            juce::AudioIODeviceCallbackContext context;
            context.hostTimeNs = &hostNs;
            engine.audioDeviceIOCallbackWithContext (inputs, 1, outputs, 2, blockSize, context);

            for (int i = 0; i < blockSize; ++i)
            {
                CHECK (std::isfinite (left[i]) && left[i] == right[i]);
                peak = std::max (peak, std::abs (left[i]));
                lastOutput.push_back (left[i]);
            }

            samplesFed += blockSize;
            hostNs += (std::uint64_t) (blockSize / sampleRate * 1.0e9);
        }
        return peak;
    }
};

float sine (std::int64_t n, double hz = 220.0, float level = 0.3f)
{
    return level * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) n / sampleRate);
}

juce::File tempDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("roadie-engine-tests");
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

void pumpMessages (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}

//==============================================================================
void testLooperThroughEngine()
{
    Rig rig;
    auto& looper = rig.engine.getLooper();

    // Dry input passes through to both outputs.
    CHECK (rig.run (0.1, [] (auto n) { return sine (n); }) > 0.25f);

    rig.engine.recordLoop();
    rig.run (1.0, [] (auto n) { return sine (n); });
    CHECK (looper.getState() == Looper::State::Recording);
    rig.engine.recordLoop(); // close the loop
    rig.run (0.01, [] (auto) { return 0.0f; });
    CHECK (looper.getState() == Looper::State::Playing);
    std::printf ("loop length %.3f s\n", looper.getLoopLengthSeconds());
    CHECK (std::abs (looper.getLoopLengthSeconds() - 1.0) < 0.01);

    // Transport restarted with the loop, so bar/beat and the playhead line up.
    CHECK (rig.engine.getTransportPosition() > (std::int64_t) sampleRate);

    // With silent input, the loop plays back on its own…
    CHECK (rig.run (0.5, [] (auto) { return 0.0f; }) > 0.25f);

    // …and muting the loop silences it while the live input still passes.
    rig.engine.setLoopMuted (true);
    CHECK (rig.run (0.2, [] (auto) { return 0.0f; }) < 1.0e-6f);
    CHECK (rig.run (0.2, [] (auto n) { return sine (n); }) > 0.25f);
    rig.engine.setLoopMuted (false);
    rig.engine.setGuitarMuted (true);
    CHECK (rig.run (0.2, [] (auto) { return 0.0f; }) > 0.25f);
    rig.engine.setGuitarMuted (false);

    // Pause freezes the transport, play resumes it.
    rig.engine.togglePlay();
    rig.run (0.05, [] (auto) { return 0.0f; });
    const auto frozen = rig.engine.getTransportPosition();
    CHECK (! rig.engine.isTransportRunning());
    CHECK (rig.run (0.2, [] (auto) { return 0.0f; }) < 1.0e-6f);
    CHECK (rig.engine.getTransportPosition() == frozen);
    rig.engine.togglePlay();
    CHECK (rig.run (0.2, [] (auto) { return 0.0f; }) > 0.25f);

    // Overdub a second layer, then undo it.
    rig.engine.recordLoop(); // Playing → Overdubbing
    rig.run (1.0, [] (auto n) { return sine (n, 440.0, 0.2f); });
    CHECK (looper.getLayerCount() == 2);
    rig.engine.recordLoop(); // Overdubbing → Playing
    rig.run (0.01, [] (auto) { return 0.0f; });
    rig.engine.getLooper().requestUndo();
    rig.run (0.01, [] (auto) { return 0.0f; });
    CHECK (looper.getLayerCount() == 1);
    std::printf ("looper through engine: OK\n");
}

void testMetronomeIsNotRecorded (const juce::File& dir)
{
    Rig rig;
    rig.engine.getMetronome().setBpm (120.0);
    rig.engine.getMetronome().setEnabled (true);
    rig.engine.play(); // the click is locked to the transport position — silent while stopped

    // Clicks reach the monitor output…
    CHECK (rig.run (1.0, [] (auto) { return 0.0f; }) > 0.1f);

    // …but never the take.
    const auto wav = dir.getChildFile ("click-take.wav");
    CHECK (rig.engine.startTake (wav));
    rig.run (1.0, [] (auto) { return 0.0f; });
    rig.engine.stopTake();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));
    CHECK (reader != nullptr);
    juce::AudioBuffer<float> buffer (1, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, false);
    CHECK (buffer.getMagnitude (0, 0, buffer.getNumSamples()) < 1.0e-4f);
    std::printf ("metronome kept out of takes: OK\n");
}

void testTakeRecording (const juce::File& dir)
{
    Rig rig;
    const auto wav = dir.getChildFile ("take-1.wav");
    CHECK (rig.engine.startTake (wav));
    rig.run (2.0, [] (auto n) { return sine (n); });
    const double first = rig.engine.getTakeRecorder().getFirstSampleHostSeconds();
    const double recorded = rig.engine.getTakeRecorder().getRecordedSeconds();
    rig.engine.stopTake();

    std::printf ("take: %.3f s, first sample at host %.6f s\n", recorded, first);
    CHECK (std::abs (recorded - 2.0) < 0.01);
    // Host time of the first sample = callback stamp − 2 blocks − input latency.
    const double expected = 1.0 - (2.0 * blockSize + 32.0) / sampleRate;
    CHECK (std::abs (first - expected) < 1.0e-6);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));
    CHECK (reader != nullptr);
    CHECK (reader->sampleRate == sampleRate);
    CHECK (std::abs ((double) reader->lengthInSamples / sampleRate - 2.0) < 0.01);
    std::printf ("take recording: OK\n");
}

void testCalibrationCapture()
{
    Rig rig;
    rig.engine.startCalibrationCapture (1.0);
    CHECK (! rig.engine.isCalibrationCaptureComplete());

    // A "strum" at 0.4 s into the capture.
    const auto start = rig.samplesFed;
    rig.run (1.1, [start] (auto n)
    {
        const auto t = n - start - (std::int64_t) (0.4 * sampleRate);
        return t >= 0 && t < 2000 ? 0.8f * (float) std::exp (-(double) t / 200.0) * (t % 2 == 0 ? 1.0f : -1.0f) : 0.001f * (float) ((n * 7919) % 13 - 6);
    });
    CHECK (rig.engine.isCalibrationCaptureComplete());

    double firstHost = 0.0;
    const auto capture = rig.engine.getCalibrationCapture (firstHost);
    CHECK ((int) capture.size() == (int) sampleRate);
    const int onset = roadie::sync::findOnset (capture.data(), (int) capture.size(), sampleRate);
    std::printf ("calibration onset at %.4f s (expect 0.4)\n", onset / sampleRate);
    CHECK (std::abs (onset / sampleRate - 0.4) < 0.001);
    std::printf ("calibration capture: OK\n");
}

void testNamCapture (const juce::File& namFile)
{
    Rig rig;
    juce::String error;
    const bool loaded = rig.engine.getAmp().loadModel (namFile, error);
    if (! loaded)
        std::printf ("NAM load error (%s): %s\n", namFile.getFullPathName().toRawUTF8(), error.toRawUTF8());
    CHECK (loaded);
    CHECK (rig.engine.getAmp().isLoaded());

    const double startMs = juce::Time::getMillisecondCounterHiRes();
    const float peak = rig.run (2.0, [] (auto n) { return sine (n, 110.0, 0.2f); });
    const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - startMs;
    std::printf ("NAM %s: peak %.3f, 2 s of audio processed in %.0f ms (%.1f%% of real time)\n",
                 namFile.getFileName().toRawUTF8(), peak, elapsedMs, elapsedMs / 20.0);
    CHECK (peak > 0.01f && peak < 4.0f);

    // Tone stack actually changes the sound.
    rig.engine.getAmp().getToneStack().setGainDb (ToneStack::treble, -12.0f);
    rig.engine.getAmp().getToneStack().setGainDb (ToneStack::presence, -12.0f);
    const float darker = rig.run (1.0, [] (auto n) { return sine (n, 110.0, 0.2f); });
    CHECK (std::isfinite (darker) && darker > 0.0f);
    std::printf ("NAM capture + tone stack: OK\n");
}

void testProjectRoundTrip (const juce::File& dir)
{
    Project p;
    p.folder = dir.getChildFile (juce::String ("Riff du soir") + Project::extension);
    p.name = "Riff du soir";
    p.bpm = 130.0;
    p.beatsPerBar = 3;
    p.metronomeOn = true;
    p.gainDb = 6.5f;
    p.trebleDb = -3.0f;
    p.loopLayers = 2;

    Take take;
    take.name = p.getNextTakeName();
    CHECK (take.name == "Prise 1");
    take.audioFile = p.getNextTakeFile (".wav");
    take.audioFile.getParentDirectory().createDirectory();
    take.audioFile.replaceWithText ("placeholder");
    take.audioStartHostSeconds = 100.0;
    take.videoStartHostSeconds = 100.25;
    take.syncLagSeconds = 0.03;
    take.durationSeconds = 12.5;
    take.createdAt = juce::Time::getCurrentTime();
    juce::Image thumb (juce::Image::RGB, 320, 180, true);
    take.thumbnails.push_back (thumb);
    p.takes.push_back (take);
    CHECK (p.getNextTakeName() == "Prise 2");

    std::vector<float> loop ((size_t) sampleRate, 0.25f);
    juce::String error;
    CHECK (p.save (loop, sampleRate, error));

    Project loaded;
    std::vector<float> loadedLoop;
    double loadedRate = 0.0;
    CHECK (Project::load (p.folder, loaded, loadedLoop, loadedRate, error));
    CHECK (loaded.name == "Riff du soir");
    CHECK (loaded.bpm == 130.0 && loaded.beatsPerBar == 3 && loaded.metronomeOn);
    CHECK (std::abs (loaded.gainDb - 6.5f) < 1.0e-4f && std::abs (loaded.trebleDb + 3.0f) < 1.0e-4f);
    CHECK (loaded.loopLayers == 2 && loadedRate == sampleRate && loadedLoop.size() == loop.size());
    CHECK (std::abs (loadedLoop[1000] - 0.25f) < 1.0e-4f);
    CHECK (loaded.takes.size() == 1 && loaded.takes[0].thumbnails.size() == 1);
    CHECK (std::abs (loaded.takes[0].getVideoOffsetSeconds() - 0.28) < 1.0e-9);

    // Save as: everything follows to the new folder.
    auto moved = loaded;
    const auto newFolder = dir.getChildFile (juce::String ("Copie") + Project::extension);
    CHECK (moved.moveTo (newFolder, error));
    CHECK (moved.takes[0].audioFile.isAChildOf (newFolder) && moved.takes[0].audioFile.existsAsFile());
    std::printf ("project round trip: OK\n");
}

#if JUCE_MAC
void testClipExport (const juce::File& dir)
{
    // A 3 s camera-like movie and a 3 s take whose audio started 0.5 s before it.
    const auto movie = dir.getChildFile ("cam.mov");
    CHECK (roadie::test::writeTestMovie (movie, 640, 360, 3.0));

    Rig rig;
    const auto wav = dir.getChildFile ("mix.wav");
    CHECK (rig.engine.startTake (wav));
    rig.run (3.0, [] (auto n) { return sine (n); });
    rig.engine.stopTake();

    for (const bool vertical : { true, false })
    {
        ClipExportSettings settings;
        settings.audioFile = wav;
        // The movie starts 0.5 s into the take: the head of the clip is black.
        settings.segments.push_back ({ movie, -0.5, 0.0, 3.0 });
        settings.vertical = vertical;
        settings.title = "Riff du soir";
        settings.burnInTitle = true;
        settings.outputFile = dir.getChildFile (vertical ? "clip-vertical.mp4" : "clip-horizontal.mp4");

        ClipExporter exporter;
        bool finished = false, ok = false;
        juce::String message;
        exporter.start (settings, [&] (bool success, juce::String m) { finished = true; ok = success; message = m; });

        for (int i = 0; i < 600 && ! finished; ++i)
            pumpMessages (50);

        if (! ok)
            std::printf ("export error: %s\n", message.toRawUTF8());
        CHECK (finished && ok);

        const auto info = roadie::test::inspectMovie (settings.outputFile);
        std::printf ("export %s: %.2f s, %dx%d, audio=%d\n", vertical ? "9:16" : "16:9",
                     info.durationSeconds, info.width, info.height, (int) info.hasAudio);
        CHECK (info.hasVideo && info.hasAudio);
        CHECK (std::abs (info.durationSeconds - 3.0) < 0.1);
        CHECK (vertical ? (info.width == 1080 && info.height == 1920) : (info.width == 1920 && info.height == 1080));

        // The burnt-in title shows up as near-white glyphs in the bottom-left
        // corner (the synthetic movie has no white there).
        const int titlePixels = roadie::test::countBrightPixels (settings.outputFile, 1.5, { 0.0f, 0.85f, 0.6f, 0.15f });
        std::printf ("title pixels: %d\n", titlePixels);
        CHECK (titlePixels > 200);
    }
    // Two regions with a gap between them (black), as an arrangement exports.
    {
        ClipExportSettings settings;
        settings.audioFile = wav;
        settings.segments.push_back ({ movie, 0.0, 0.0, 1.0 });
        settings.segments.push_back ({ movie, 1.5, 2.0, 1.0 });
        settings.outputFile = dir.getChildFile ("clip-segments.mp4");

        ClipExporter exporter;
        bool finished = false, ok = false;
        exporter.start (settings, [&] (bool success, juce::String) { finished = true; ok = success; });
        for (int i = 0; i < 600 && ! finished; ++i)
            pumpMessages (50);
        CHECK (finished && ok);

        const auto info = roadie::test::inspectMovie (settings.outputFile);
        std::printf ("segmented export: %.2f s (expect 3.0)\n", info.durationSeconds);
        CHECK (info.hasVideo && std::abs (info.durationSeconds - 3.0) < 0.1);
    }
    std::printf ("clip export: OK\n");
}
#endif
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto dir = tempDir();

    testLooperThroughEngine();
    testMetronomeIsNotRecorded (dir);
    testTakeRecording (dir);
    testCalibrationCapture();
    testProjectRoundTrip (dir);

    // Path to a .nam capture (defaults to the one shipped with NAM's sources).
    const juce::File nam (argc > 1 ? juce::String (argv[1]) : juce::String (ROADIE_TEST_NAM));
    testNamCapture (nam);

   #if JUCE_MAC
    testClipExport (dir);
   #endif

    dir.deleteRecursively();
    std::printf ("ALL ENGINE TESTS PASSED\n");
    return 0;
}
