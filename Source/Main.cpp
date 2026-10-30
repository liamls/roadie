#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "I18n.h"

#include <iostream>

namespace
{
// Verification tool (--demo): a project with two synthetic takes (plucked
// notes, no video) and a two-bar loop, so snapshots show real regions
// without recording anything.
bool writeDemoWav (const juce::File& file, const std::vector<float>& samples, double sampleRate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    juce::WavAudioFormat wav;
    auto writer = stream != nullptr ? wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                       .withSampleRate (sampleRate)
                                                                       .withNumChannels (1)
                                                                       .withBitsPerSample (24))
                                    : nullptr;
    const float* channels[1] = { samples.data() };
    return writer != nullptr && writer->writeFromFloatArrays (channels, 1, (int) samples.size());
}

std::vector<float> plucks (double seconds, double sampleRate, double interval, double baseHz, int seed)
{
    std::vector<float> out ((size_t) (seconds * sampleRate));
    juce::Random random (seed);
    for (double t0 = 0.0; t0 < seconds; t0 += interval)
    {
        const double hz = baseHz * std::pow (2.0, random.nextInt (7) / 12.0);
        const float level = 0.25f + 0.6f * random.nextFloat();
        for (size_t i = (size_t) (t0 * sampleRate); i < out.size(); ++i)
        {
            const double t = (double) i / sampleRate - t0;
            if (t > interval * 1.8)
                break;
            out[i] += level * (float) (std::exp (-t * 3.5) * std::sin (juce::MathConstants<double>::twoPi * hz * t));
        }
    }
    return out;
}

juce::File createDemoProject (const juce::File& parent)
{
    const double sr = 48000.0;
    auto project = Project::createUntitled();
    project.name = "Demo";
    project.folder = parent.getChildFile (project.name + Project::extension);
    project.folder.deleteRecursively();
    project.folder.createDirectory();
    project.bpm = 100.0;

    const double bar = 2.4;
    for (int n = 1; n <= 2; ++n)
    {
        Take take;
        take.name = Project::getTakeName (n);
        take.audioFile = project.getTakeFile (n, ".wav");
        take.audioFile.getParentDirectory().createDirectory();
        take.durationSeconds = n == 1 ? 4.0 * bar : 3.0 * bar;
        writeDemoWav (take.audioFile, plucks (take.durationSeconds, sr, n == 1 ? 0.3 : 0.6, n == 1 ? 196.0 : 147.0, n), sr);
        project.takes.push_back (take);
    }

    project.arrangement.clips = { { 0, 0.0, 0.0, 4.0 * bar }, { 1, 5.0 * bar, 0.0, 3.0 * bar }, { 0, 8.0 * bar, bar, 2.0 * bar } };
    {
        Clip loopRegion;
        loopRegion.start = 2.0 * bar;
        loopRegion.length = 7.0 * bar;
        project.arrangement.loopClips = { loopRegion };
    }
    project.cycleOn = true;
    project.cycleStart = 5.0 * bar;
    project.cycleEnd = 7.0 * bar;

    juce::String error;
    project.save (plucks (2.0 * bar, sr, 0.15, 330.0, 7), sr, error);
    return project.folder;
}

// Verification tool (--demo-song, --selftest-edit): a stereo backing track
// at 92 BPM — half a second of silence, an 8-bar hi-hat intro, then kick,
// snare and a bass line — so the import, the analysis and the song lane can
// be checked without shipping music.
juce::File writeDemoSong (const juce::File& parent)
{
    const double sr = 44100.0, bpm = 92.0, beat = 60.0 / bpm, offset = 0.5;
    const int bars = 24, introBars = 8;
    std::vector<float> left ((size_t) ((offset + bars * 4 * beat + 1.0) * sr)), right (left.size());
    juce::Random random (11);
    auto hit = [&] (double t, double seconds, float pan, auto voice)
    {
        for (size_t i = (size_t) (t * sr); i < left.size() && i < (size_t) ((t + seconds) * sr); ++i)
        {
            const float v = voice ((double) i / sr - t);
            left[i] += v * (1.0f - pan);
            right[i] += v * (1.0f + pan);
        }
    };
    for (int b = 0; b < bars; ++b)
        for (int k = 0; k < 8; ++k)
        {
            const double t = offset + (b * 4 + k * 0.5) * beat;
            hit (t, 0.05, -0.4f, [&] (double u) { return 0.1f * (random.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-u * 80.0); });
            if (b < introBars || k % 2 != 0)
                continue;
            const int beatInBar = k / 2;
            if (beatInBar % 2 == 0)
                hit (t, 0.25, 0.0f, [&] (double u) { return (beatInBar == 0 ? 0.8f : 0.5f) * (float) (std::sin (juce::MathConstants<double>::twoPi * 55.0 * u) * std::exp (-u * 14.0)); });
            else
                hit (t, 0.15, 0.1f, [&] (double u) { return 0.4f * (random.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-u * 30.0); });
            hit (t, beat * 0.9, 0.0f, [&] (double u) { return 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * (b % 4 < 2 ? 82.4 : 110.0) * u); });
        }

    const auto file = parent.getChildFile ("Roadie demo song.wav");
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    juce::WavAudioFormat wav;
    auto writer = stream != nullptr ? wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (16))
                                    : nullptr;
    const float* channels[2] = { left.data(), right.data() };
    if (writer != nullptr)
        writer->writeFromFloatArrays (channels, 2, (int) left.size());
    return file;
}

// Verification tool (--selftest-edit): real mouse events, injected into the
// window's peer as if they came from the system, so they go through JUCE's
// whole dispatch (hit testing, drag thresholds, modifiers, double-clicks).
// They share the system mouse: the real one moving over the window, or any
// window above it, spoils a gesture (reported as such when it is detectable).
struct MouseRobot
{
    explicit MouseRobot (juce::Component& w) : window (w) {}

    juce::Component& window;
    juce::int64 time = juce::Time::currentTimeMillis();
    bool covered = false; // another window was over the point: JUCE drops such clicks
    bool direct = false;  // this gesture goes straight to the components (window not on screen)
    bool reportedDirect = false;

    // Direct mode: the gesture's state, as JUCE's MouseInputSource keeps it.
    juce::Component::SafePointer<juce::Component> pressed;
    juce::Point<int> downPoint; // in the window: a component that moves under the drag (a divider) keeps its distance
    juce::int64 downTime = 0, lastClickTime = 0;
    juce::Point<int> lastClickPoint;
    int clicks = 0;
    bool dragged = false, buttonDown = false;

    // A locked or sleeping screen hides the window: the system would drop
    // every click. The gesture is then handed to the components directly
    // (same events, same order), and the log says so.
    void startGesture (juce::Component& target, juce::Point<int> p)
    {
        const auto position = window.getLocalPoint (&target, p).toFloat() * juce::Desktop::getInstance().getGlobalScaleFactor();
        auto* peer = window.getPeer();
        direct = peer == nullptr || ! peer->contains (position.roundToInt(), true);
        if (direct && ! reportedDirect)
        {
            reportedDirect = true;
            std::cerr << "[selftest-edit] the window is not on screen (screen locked or asleep?): "
                         "gestures go straight to the components" << std::endl;
        }
    }

    void sendDirect (juce::Point<int> windowPoint, juce::ModifierKeys mods)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time (time);
        const bool down = mods.isLeftButtonDown();

        if (down && ! buttonDown)
        {
            // Press: the deepest component under the point takes the gesture.
            auto* hit = window.getComponentAt (windowPoint);
            pressed = hit;
            if (hit == nullptr)
                return;
            clicks = (time - lastClickTime < 400 && windowPoint.getDistanceFrom (lastClickPoint) < 4) ? clicks + 1 : 1;
            downPoint = windowPoint;
            downTime = time;
            dragged = false;
            buttonDown = true;
            const auto local = hit->getLocalPoint (&window, windowPoint).toFloat();
            hit->mouseDown (juce::MouseEvent (source, local, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, hit, hit, now,
                                              local, juce::Time (downTime), clicks, false));
            return;
        }

        if (pressed == nullptr)
        {
            buttonDown = down;
            return;
        }
        auto* c = pressed.getComponent();
        const auto local = c->getLocalPoint (&window, windowPoint).toFloat();
        const auto downPosition = c->getLocalPoint (&window, downPoint).toFloat();

        if (down)
        {
            dragged = dragged || local.getDistanceFrom (downPosition) > 4.0f;
            c->mouseDrag (juce::MouseEvent (source, local, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, now,
                                            downPosition, juce::Time (downTime), clicks, dragged));
            return;
        }

        if (buttonDown)
        {
            buttonDown = false;
            const juce::MouseEvent up (source, local, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, now,
                                       downPosition, juce::Time (downTime), clicks, dragged);
            c->mouseUp (up);
            if (! dragged && clicks == 2 && pressed != nullptr)
                c->mouseDoubleClick (up);
            lastClickTime = time;
            lastClickPoint = windowPoint;
        }
    }

    void send (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods)
    {
        // JUCE reads the keyboard modifiers (⌥, ⌘) from the global state, and
        // the peer works in native points: the interface scale is undone here.
        juce::ModifierKeys::currentModifiers = mods;
        if (direct)
        {
            sendDirect (window.getLocalPoint (&target, p), mods);
        }
        else
        {
            const auto position = window.getLocalPoint (&target, p).toFloat() * juce::Desktop::getInstance().getGlobalScaleFactor();
            if (auto* peer = window.getPeer())
            {
                covered = covered || ! peer->contains (position.roundToInt(), true);
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, position, mods,
                                        juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, time += 40);
            }
        }
        time += 40;
        juce::ModifierKeys::currentModifiers = {};
    }

    void drag (juce::Component& target, juce::Point<int> from, juce::Point<int> to, juce::ModifierKeys keys = {})
    {
        startGesture (target, from);
        const auto down = keys.withFlags (juce::ModifierKeys::leftButtonModifier);
        send (target, from, keys);
        send (target, from, down);
        for (int i = 1; i <= 12; ++i)
            send (target, from + (to - from) * i / 12, down);
        send (target, to, keys);
        time += 1000; // the next press is never a double-click
    }

    void doubleClick (juce::Component& target, juce::Point<int> p)
    {
        startGesture (target, p);
        const juce::ModifierKeys down (juce::ModifierKeys::leftButtonModifier);
        for (auto mods : { juce::ModifierKeys(), down, juce::ModifierKeys(), down, juce::ModifierKeys() })
            send (target, p, mods);
        time += 1000;
    }
};
} // namespace

class RoadieApplication : public juce::JUCEApplication
{
public:
    RoadieApplication() = default;

    const juce::String getApplicationName() override { return "Roadie"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // One instance only: two would fight over the audio interface and the camera.
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        // Verification runs leave the user's settings as they found them
        // (theme, panel sizes, last project…): saved here, put back on quit.
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        {
            // The interface language is fixed for the whole run: read before any window exists.
            juce::PropertiesFile settings (Session::getSettingsOptions());
            i18n::setLanguage (i18n::loadSavedLanguage (settings));
        }
        for (const auto& arg : args)
            verificationRun = verificationRun || arg.startsWith ("--snapshot=") || arg.startsWith ("--selftest-");
        if (verificationRun)
        {
            settingsFile = Session::getSettingsOptions().getDefaultFile();
            settingsBackup = settingsFile.getSiblingFile (settingsFile.getFileName() + ".verification");
            hadSettings = settingsFile.existsAsFile() && settingsFile.copyFileTo (settingsBackup);

            // Never film the user behind their back: the camera stays off
            // unless the run asks for it (--camera, --selftest-take).
            if (! args.contains ("--camera") && ! args.joinIntoString (" ").contains ("--selftest-take"))
            {
                juce::PropertiesFile settings (Session::getSettingsOptions());
                settings.setValue ("cameraEnabled", false);
                settings.saveIfNeeded();
            }
        }

        mainWindow.reset (new MainWindow (getApplicationName()));

        // Verification tool: `Roadie --snapshot=/tmp/ui.png [--track=loop|camera|amp] [--export] [--demo]
        // [--scale=0.9] [--no-library] [--no-inspector] [--workspace=play|edit] [--library=captures|songs|takes]` renders the window into a PNG after it has
        // settled, then quits — UI checks without screen-recording permissions or a human. None of
        // these options is kept in the settings.
        for (const auto& arg : args)
        {
            if (arg.startsWith ("--snapshot="))
            {
                // The target is overwritten: only ever an image file.
                const juce::File target (arg.fromFirstOccurrenceOf ("=", false, false).unquoted());
                if (target.hasFileExtension (".png"))
                    scheduleSnapshot (target, args);
                else
                    std::cerr << "[snapshot] the target must be a .png file" << std::endl;
            }
            if (arg.startsWith ("--selftest-take="))
                scheduleTakeSelfTest (juce::File (arg.fromFirstOccurrenceOf ("=", false, false).unquoted()));
            if (arg == "--selftest-edit")
                scheduleEditSelfTest();
        }
    }

    // Verification tool: `Roadie --selftest-edit` opens the demo project and
    // edits it with the mouse and the keyboard like a user would — move, trim
    // and ⌥-duplicate regions, move the loop, draw a cycle, ⌘Z / ⇧⌘Z, drag the
    // dividers, fold the panels — checking the result of each gesture. Prints
    // one line per check on stderr; the exit code is the number of failures.
    // Uses a demo project in the temp folder; the settings are restored.
    void scheduleEditSelfTest()
    {
        auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
        if (content == nullptr)
            return;

        auto& session = content->getSession();
        session.openProject (createDemoProject (juce::File::getSpecialLocation (juce::File::tempDirectory)));
        session.locate (0.0);
        // Region editing is Monter's job: the timeline is roomy there.
        content->setWorkspace (MainComponent::Workspace::edit, false);
        content->resetLayout();
        content->getTimeline().zoomToFit();

        // Clicks only land where Roadie's window is the frontmost one.
        juce::Process::makeForegroundProcess();
        mainWindow->setAlwaysOnTop (true);
        mainWindow->toFront (true);
        content->disableTooltips();

        using Track = Session::Track;
        const double bar = session.getSecondsPerBar(); // 2.4 s in the demo
        auto robot = std::make_shared<MouseRobot> (*mainWindow);
        auto& timeline = content->getTimeline();
        auto failures = std::make_shared<int> (0);
        auto check = [failures, robot] (bool ok, const juce::String& what)
        {
            std::cerr << "[selftest-edit] " << (ok ? "ok    " : "FAILED") << " " << what
                      << (ok || ! robot->covered ? "" : " (another window covers Roadie's: a permission prompt?)") << std::endl;
            if (! ok)
                ++*failures;
        };
        auto near = [] (double a, double b) { return std::abs (a - b) < 0.011; };
        auto clips = [&session] { return session.getArrangement().clips; };
        auto key = [content] (int code, juce::ModifierKeys mods = {}, juce::juce_wchar text = 0)
        {
            content->keyPressed (juce::KeyPress (code, mods, text));
        };
        const auto cmd = juce::ModifierKeys (juce::ModifierKeys::commandModifier);
        const auto original = session.getArrangement();

        std::vector<std::function<void()>> steps;

        steps.push_back ([=, &session, &timeline]
        {
            const double from = 6.5 * bar;
            robot->drag (timeline, timeline.getRegionGrabPoint (Track::amp, from), timeline.getRegionGrabPoint (Track::amp, from - bar));
            check (near (clips()[1].start, 4.0 * bar) && near (clips()[1].length, 3.0 * bar),
                   "drag a region one bar to the left: Take 2 at " + juce::String (clips()[1].start, 2) + " s");
            check (session.getSelectedClip() == 1, "the moved region is selected");
            key ('Z', cmd);
            check (near (clips()[1].start, 5.0 * bar), "cmd-Z puts it back");
        });

        steps.push_back ([=, &timeline]
        {
            const auto c = clips()[1];
            const auto edge = timeline.getLanePoint (Track::amp, c.end()) - juce::Point<int> (3, 0);
            robot->drag (timeline, edge, edge - juce::Point<int> (juce::roundToInt (bar * timeline.getPixelsPerSecond()), 0));
            check (near (clips()[1].start, c.start) && near (clips()[1].length, c.length - bar),
                   "trim the right edge by one bar: length " + juce::String (clips()[1].length, 2) + " s (edge at " + edge.toString()
                       + " in a timeline of " + timeline.getBounds().toString() + ", window " + content->getBounds().toString()
                       + ", " + juce::String (timeline.getPixelsPerSecond(), 1) + " px/s)");
        });

        steps.push_back ([=, &timeline]
        {
            const auto c = clips()[1];
            const auto edge = timeline.getLanePoint (Track::amp, c.start) + juce::Point<int> (3, 0);
            robot->drag (timeline, edge, edge + juce::Point<int> (juce::roundToInt (bar * timeline.getPixelsPerSecond()), 0));
            const auto t = clips()[1];
            check (near (t.start, c.start + bar) && near (t.sourceStart, c.sourceStart + bar) && near (t.end(), c.end()),
                   "trim the left edge by one bar: start " + juce::String (t.start, 2) + " s, source +" + juce::String (t.sourceStart, 2) + " s");
        });

        steps.push_back ([=, &session, &timeline]
        {
            const auto count = clips().size();
            const double from = 0.5 * bar;
            robot->drag (timeline, timeline.getRegionGrabPoint (Track::amp, from), timeline.getRegionGrabPoint (Track::amp, from + 9.0 * bar),
                         juce::ModifierKeys (juce::ModifierKeys::altModifier));
            const auto after = clips();
            check (after.size() == count + 1 && near (after.back().start, 9.0 * bar) && after.back().take == 0 && near (after[0].start, 0.0),
                   "alt-drag duplicates the region (" + juce::String ((int) after.size()) + " regions)");
            check (session.getSelectedClip() == (int) after.size() - 1, "the copy is selected");
        });

        // Dragging over a region's body selects a stretch, like text: ⌘T
        // separates it, ⌫ deletes it.
        steps.push_back ([=, &session, &timeline]
        {
            const auto before = clips();
            const auto c = before[0];
            const double a = c.start + 0.25 * c.length, b = c.start + 0.5 * c.length;
            robot->drag (timeline, timeline.getLanePoint (Track::amp, a), timeline.getLanePoint (Track::amp, b));
            const auto range = session.getSelectedRange();
            check (session.getSelection() == Session::Selection::range && range.getLength() > 0.2,
                   "a drag over a region's body selects a stretch: " + juce::String (range.getStart(), 2) + " to " + juce::String (range.getEnd(), 2) + " s");

            key ('T', cmd);
            const auto cut = clips();
            const int piece = session.getSelectedClip();
            check (cut.size() == before.size() + 2 && piece >= 0 && near (cut[(size_t) piece].start, range.getStart())
                       && near (cut[(size_t) piece].end(), range.getEnd()),
                   "cmd-T separates the stretch into a region of its own (" + juce::String ((int) cut.size()) + " regions)");
            key ('Z', cmd);
            check (clips().size() == before.size(), "cmd-Z joins it back");

            robot->drag (timeline, timeline.getLanePoint (Track::amp, a), timeline.getLanePoint (Track::amp, b));
            const auto again = session.getSelectedRange();
            key (juce::KeyPress::backspaceKey);
            bool covered = false;
            for (const auto& k : clips())
                covered = covered || (k.take == c.take && k.start < again.getStart() + 0.1 && k.end() > again.getEnd() - 0.1);
            check (! covered && session.getSelection() == Session::Selection::none && clips().size() == before.size() + 1,
                   "delete removes the stretch and leaves the rest (" + juce::String ((int) clips().size()) + " regions)");
            key ('Z', cmd);
            check (clips().size() == before.size(), "cmd-Z puts it back");
        });

        steps.push_back ([=, &session, &timeline]
        {
            const auto a = session.getArrangement();
            const double from = a.loopClips[0].start + 1.5 * bar;
            robot->drag (timeline, timeline.getRegionGrabPoint (Track::loop, from), timeline.getRegionGrabPoint (Track::loop, from + bar));
            const auto b = session.getArrangement();
            check (b.loopClips.size() == 1 && near (b.loopClips[0].start, a.loopClips[0].start + bar) && near (b.loopClips[0].length, a.loopClips[0].length)
                       && near (b.loopClips[0].sourceStart, a.loopClips[0].sourceStart),
                   "drag the loop region one bar: start " + juce::String (b.loopClips[0].start, 2) + " s, same phase");
        });

        // The same editing works on the Looper track: select a stretch, delete
        // it, and the loop plays on either side.
        steps.push_back ([=, &session, &timeline]
        {
            const auto before = session.getArrangement().loopClips;
            const double a = before[0].start + 1.0 * bar, b = before[0].start + 2.0 * bar;
            robot->drag (timeline, timeline.getLanePoint (Track::loop, a), timeline.getLanePoint (Track::loop, b));
            const auto range = session.getSelectedRange();
            check (session.getSelection() == Session::Selection::range && session.getSelectedRangeTrack() == Track::loop && range.getLength() > 0.2,
                   "a drag over the loop region selects a stretch: " + juce::String (range.getStart(), 2) + " to " + juce::String (range.getEnd(), 2) + " s");
            key (juce::KeyPress::backspaceKey);
            const auto after = session.getArrangement().loopClips;
            check (after.size() == 2 && near (after[0].end(), range.getStart()) && near (after[1].start, range.getEnd())
                       && near (after[1].sourceStart, before[0].sourceStart + (range.getEnd() - before[0].start)),
                   "delete cuts a hole in the loop (" + juce::String ((int) after.size()) + " regions), the right part keeps its phase");
            key ('Z', cmd);
            check (session.getArrangement().loopClips.size() == 1, "cmd-Z closes the hole");
        });

        steps.push_back ([=, &session, &timeline]
        {
            robot->drag (timeline, timeline.getRulerPoint (1.0 * bar, true), timeline.getRulerPoint (3.0 * bar, true));
            const auto cycle = session.getCycleRange();
            check (session.isCycleOn() && near (cycle.getStart(), bar) && near (cycle.getEnd(), 3.0 * bar),
                   "draw a cycle zone in the ruler: " + juce::String (cycle.getStart(), 2) + " to " + juce::String (cycle.getEnd(), 2) + " s");
            robot->drag (timeline, timeline.getRulerPoint (4.5 * bar, false), timeline.getRulerPoint (5.0 * bar, false));
        });

        steps.push_back ([=, &session]
        {
            // Checked a step later: the audio thread applies a locate on its next block.
            check (near (session.getPlayheadSeconds(), 5.0 * bar), "dragging in the ruler moves the playhead");

            for (int i = 0; i < 4; ++i)
                key ('Z', cmd);
            check (session.getArrangement() == original && ! session.canUndoEdit(), "4 x cmd-Z go back to the original arrangement");
            key ('Z', cmd.withFlags (juce::ModifierKeys::shiftModifier));
            check (near (clips()[1].length, 2.0 * bar), "shift-cmd-Z redoes the first trim");
        });

        steps.push_back ([=, &timeline]
        {
            const int before = timeline.getHeaderWidth();
            const juce::Point<int> handle (before - 1, Timeline::rulerHeight + 40);
            robot->drag (timeline, handle, handle + juce::Point<int> (40, 0));
            check (timeline.getHeaderWidth() == before + 40, "track header divider: " + juce::String (timeline.getHeaderWidth()) + " px");
            robot->doubleClick (timeline, juce::Point<int> (timeline.getHeaderWidth() - 1, Timeline::rulerHeight + 40));
            check (timeline.getHeaderWidth() == Timeline::defaultHeaderWidth, "double-click: default width");
        });

        steps.push_back ([=]
        {
            const auto library = content->getLibraryBounds();
            const juce::Point<int> handle (library.getRight(), library.getCentreY());
            robot->drag (*content, handle, handle + juce::Point<int> (60, 0));
            check (content->getLibraryBounds().getWidth() == library.getWidth() + 60,
                   "library divider: " + juce::String (content->getLibraryBounds().getWidth()) + " px");

            const auto inspector = content->getInspectorBounds();
            const juce::Point<int> top (inspector.getCentreX(), inspector.getY());
            robot->drag (*content, top, top - juce::Point<int> (0, 40));
            check (content->getInspectorBounds().getHeight() == inspector.getHeight() + 40,
                   "inspector divider: " + juce::String (content->getInspectorBounds().getHeight()) + " px");
        });

        steps.push_back ([=, &timeline]
        {
            key ('Y', {}, 'y');
            check (! content->isLibraryShown() && timeline.getX() == 0, "Y hides the library");
            key ('B', {}, 'b');
            check (! content->isInspectorShown() && timeline.getBottom() == content->getHeight(), "B hides the inspector");
            key ('Y', {}, 'y');
            key ('B', {}, 'b');
            check (content->isLibraryShown() && content->isInspectorShown(), "Y and B show them again");
        });

        // Workspaces: the switch, the dividers of Jouer, folding, memory per
        // workspace, the enlarged panel, ● from Monter.
        using Workspace = MainComponent::Workspace;
        auto saved = std::make_shared<int> (0);

        steps.push_back ([=]
        {
            check (content->getMonitorBounds().getX() > content->getTimelineBounds().getRight() - 10 && content->getStageBounds().isEmpty(),
                   "Arrange: the monitor sits right of the timeline, no stage");
            check (content->getMonitor().isPlaybackOnly() && ! content->getSession().getCamera().isOpen(), "Arrange: the monitor is playback only, camera off");
            *saved = content->getLibraryBounds().getWidth();
            key ('1', cmd, '1');
        });

        steps.push_back ([=]
        {
            const auto stage = content->getStageBounds(), monitorArea = content->getMonitorBounds(), strip = content->getTimelineBounds();
            check (content->getWorkspace() == Workspace::play && ! stage.isEmpty() && monitorArea.getRight() <= stage.getX() + 4
                       && strip.getY() >= monitorArea.getBottom() - 4,
                   "cmd-1: Play, the monitor above the timeline strip, the stage on the right");
            check (! content->getMonitor().isPlaybackOnly(), "Play: the monitor is live again");

            const juce::Point<int> sideHandle (stage.getX(), stage.getCentreY());
            robot->drag (*content, sideHandle, sideHandle - juce::Point<int> (40, 0));
            check (content->getStageBounds().getWidth() == stage.getWidth() + 40,
                   "stage divider: " + juce::String (content->getStageBounds().getWidth()) + " px");

            const auto stripNow = content->getTimelineBounds();
            const juce::Point<int> stripHandle (stripNow.getCentreX(), stripNow.getY());
            robot->drag (*content, stripHandle, stripHandle - juce::Point<int> (0, 30));
            check (content->getTimelineBounds().getHeight() == stripNow.getHeight() + 30,
                   "timeline strip divider: " + juce::String (content->getTimelineBounds().getHeight()) + " px");
        });

        steps.push_back ([=]
        {
            const auto library = content->getLibraryBounds();
            const juce::Point<int> handle (library.getRight(), library.getCentreY());
            robot->drag (*content, handle, juce::Point<int> (library.getX() + 30, handle.y));
            check (! content->isLibraryShown() && content->getLibraryBounds().isEmpty(),
                   "dragged below its minimum, the library folds away (from " + library.toString() + ", now " + content->getLibraryBounds().toString() + ")");
            robot->drag (*content, juce::Point<int> (3, handle.y), juce::Point<int> (250, handle.y));
            check (content->isLibraryShown() && content->getLibraryBounds().getWidth() == 247,
                   "the grip pulls it back: " + juce::String (content->getLibraryBounds().getWidth()) + " px");
            robot->doubleClick (*content, juce::Point<int> (content->getLibraryBounds().getRight(), handle.y));
            check (content->getLibraryBounds().getWidth() == 212, "double-click on the divider: default width");
            robot->drag (*content, juce::Point<int> (212, handle.y), juce::Point<int> (262, handle.y));
            key ('2', cmd, '2');
        });

        steps.push_back ([=]
        {
            check (content->getWorkspace() == Workspace::edit && content->getLibraryBounds().getWidth() == *saved,
                   "each workspace keeps its sizes (Arrange: " + juce::String (content->getLibraryBounds().getWidth()) + " px)");
            key ('1', cmd, '1');
        });

        steps.push_back ([=]
        {
            check (content->getLibraryBounds().getWidth() == 262, "back in Play: " + juce::String (content->getLibraryBounds().getWidth()) + " px");
            content->toggleMaximisedPanel (&content->getMonitor());
        });
        steps.push_back ([] {}); // the 200 ms transition, with room to spare on a busy machine

        steps.push_back ([=]
        {
            const auto m = content->getMonitorBounds();
            check (m.getX() == 0 && m.getRight() == content->getWidth() && m.getBottom() == content->getHeight()
                       && content->getLibraryBounds().isEmpty() && content->getStageBounds().isEmpty(),
                   "the enlarged monitor takes the whole window (" + m.toString() + ")");
            key (juce::KeyPress::escapeKey);
        });

        steps.push_back ([=]
        {
            check (content->getMaximisedPanel() == nullptr && ! content->getStageBounds().isEmpty(), "Esc puts the panels back");
            content->resetLayout();
            key ('2', cmd, '2');
        });

        steps.push_back ([=, &session]
        {
            const auto takes = session.getTakes().size();
            key ('T', {}, 't');
            check (content->getWorkspace() == Workspace::play, "T in Arrange goes back to Play to record");
            juce::Timer::callAfterDelay (150, [=, &session]
            {
                check (session.isRecordingTake(), "...and the take starts (camera off: right away)");
                check (session.getCountInBeatsLeft() > 0, "a bar of count-in first: " + juce::String (session.getCountInBeatsLeft()) + " beats left");
                const double target = session.getPlayheadSeconds() + session.getCountInBeatsLeft() * session.getSecondsPerBeat();
                juce::Timer::callAfterDelay (3200, [=, &session]
                {
                    check (session.getCountInBeatsLeft() == 0, "the count-in is over");
                    key ('T', {}, 't');
                    juce::Timer::callAfterDelay (100, [=, &session]
                    {
                        check (session.getTakes().size() == takes + 1, "T again ends the take");
                        const auto& clip = session.getArrangement().clips.back();
                        check (clip.sourceStart > session.getSecondsPerBar() * 0.9 && std::abs (clip.start - target) < session.getSecondsPerBeat(),
                               "its region starts after the count-in (" + juce::String (clip.start, 2) + " s, " + juce::String (clip.sourceStart, 2)
                                   + " s of count-in trimmed)");
                    });
                });
            });
        });

        for (int i = 0; i < 13; ++i)
            steps.push_back ([] {}); // the take above: count-in, recording, finishing

        // Morceau: import, analysis, bar 2, playback, parts, tempo, export.
        steps.push_back ([=, &session]
        {
            session.stopAll();
            session.setMetronomeOn (true);
            session.importSong (writeDemoSong (juce::File::getSpecialLocation (juce::File::tempDirectory)));
            check (session.isImportingSong(), "the song is imported in the background");
        });
        for (int i = 0; i < 8; ++i)
            steps.push_back ([] {}); // decoding and analysis

        steps.push_back ([=, &session]
        {
            const auto& song = session.getSong();
            check (session.hasSong() && song.file.isAChildOf (session.getProject().folder) && song.file.existsAsFile(),
                   "the song is copied into the project: " + song.file.getFileName());
            check (std::abs (song.bpm - 92.0) < 0.3 && std::abs (session.getBpm() - song.bpm) < 0.001,
                   "tempo detected and applied: " + juce::String (song.bpm, 2) + " BPM");
            const double barTwo = session.getSecondsPerBar();
            check (std::abs (session.getSongStart() + song.firstDownbeat - barTwo) < 0.001 && std::abs (song.firstDownbeat - 0.5) < 0.03,
                   "its first bar (at " + juce::String (song.firstDownbeat, 3) + " s) lands on bar 2");
            check (! session.isMetronomeOn(), "the metronome is off: the song is the click");
            check (session.getSongMedia() != nullptr, "the song is decoded for playback");
            session.locate (barTwo * 9.5);
            session.togglePlay();
        });

        steps.push_back ([=, &session]
        {
            check (session.getEngine().getSongPeak() > 0.05f, "the song plays: peak " + juce::String (session.getEngine().getSongPeak(), 2));
            session.setMuted (Track::song, true);
        });

        steps.push_back ([=, &session]
        {
            check (session.getEngine().getSongPeak() < 1.0e-6f, "M mutes the song");
            session.setMuted (Track::song, false);
            session.stopAll();
        });

        steps.push_back ([=, &session]
        {
            const double before = session.getSong().bpm;
            session.setSongTempo (before * 2.0);
            check (std::abs (session.getBpm() - before * 2.0) < 0.001
                       && std::abs (session.getSongStart() + session.getSong().firstDownbeat - session.getSecondsPerBar()) < 0.001,
                   "x2: the tempo doubles and the first bar stays on bar 2");
            session.setSongTempo (before);
            const double start = session.getSongStart();
            session.nudgeSong (0.01);
            check (std::abs (session.getSongStart() - start - 0.01) < 1.0e-9, "+10 ms nudges the song");
            key ('Z', cmd);
            check (std::abs (session.getSongStart() - start) < 1.0e-9, "cmd-Z undoes the nudge");

            Session::ExportRequest request;
            request.startSeconds = session.getSecondsPerBar() * 10.0;
            request.endSeconds = request.startSeconds + 2.0;
            request.title = "With song";
            juce::String error;
            const auto with = session.exportAudio (request, error);
            request.includeSong = false;
            request.title = "Guitar only";
            const auto without = session.exportAudio (request, error);
            auto peakOf = [] (const juce::File& f)
            {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
                if (reader == nullptr)
                    return -1.0f;
                juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
                return buffer.getNumChannels() == 2 ? buffer.getMagnitude (0, buffer.getNumSamples()) : -2.0f;
            };
            const float withPeak = peakOf (with), withoutPeak = peakOf (without);
            check (withPeak > 0.05f && withoutPeak >= 0.0f && withoutPeak < withPeak * 0.5f,
                   "stereo export with the song (" + juce::String (withPeak, 2) + ") and guitar only (" + juce::String (withoutPeak, 2) + ")");
        });

        // Practice: slower, transposed; the grid follows.
        auto songLengthBefore = std::make_shared<double> (0.0);
        steps.push_back ([=, &session]
        {
            *songLengthBefore = session.getSongMedia() != nullptr ? (double) session.getSongMedia()->audio->left.size() : 0.0;
            session.setSongSpeed (0.75);
            const double barTwo = session.getSecondsPerBar();
            check (std::abs (session.getBpm() - session.getSong().bpm * 0.75) < 0.001
                       && std::abs (session.getSongStart() + session.getSongDownbeat() - barTwo) < 0.001,
                   "75 %: the grid slows down to " + juce::String (session.getBpm(), 1) + " BPM, the first bar stays on bar 2");
            check (session.isRenderingSong(), "the slower song renders in the background");
        });
        for (int i = 0; i < 10; ++i)
            steps.push_back ([] {});
        steps.push_back ([=, &session]
        {
            const double length = session.getSongMedia() != nullptr ? (double) session.getSongMedia()->audio->left.size() : 0.0;
            check (! session.isRenderingSong() && std::abs (length / *songLengthBefore - 1.0 / 0.75) < 0.01,
                   "the slowed song is " + juce::String (length / *songLengthBefore, 3) + " times longer (expect 1.333)");
            session.setSongSpeed (1.0);
            session.setSongSemitones (2);
        });
        for (int i = 0; i < 10; ++i)
            steps.push_back ([] {});
        steps.push_back ([=, &session]
        {
            const double length = session.getSongMedia() != nullptr ? (double) session.getSongMedia()->audio->left.size() : 0.0;
            check (! session.isRenderingSong() && std::abs (length / *songLengthBefore - 1.0) < 0.01 && session.getSong().semitones == 2,
                   "+2 semitones keeps the length and the tempo");
            session.setSongSemitones (0);
        });

        // Arming, the chain's blocks, the master.
        steps.push_back ([=, &session]
        {
            session.setArmed (Track::amp, false);
            key ('T', {}, 't');
            check (! session.isRecordingTake(), "a disarmed Guitar track does not record");
            session.setArmed (Track::amp, true);
            session.setBypassed (AmpEngine::Block::eq, true);
            session.setGateOn (true);
            check (session.getEngine().getAmp().isBypassed (AmpEngine::Block::eq) && session.getEngine().getAmp().getGate().isEnabled(),
                   "the EQ block can be switched off, the gate on");
            session.setBypassed (AmpEngine::Block::eq, false);
            session.setGateOn (false);
            session.setMasterVolume (-6.0f);
            check (std::abs (session.getProject().masterVolumeDb + 6.0f) < 1.0e-4f, "the master volume is kept in the project");
            session.setMasterVolume (0.0f);
        });

        steps.push_back ([=, &timeline]
        {
            const int before = timeline.getTrackHeight (Track::loop);
            const auto lane = timeline.getLanePoint (Track::loop, 0.0);
            const juce::Point<int> edge (40, lane.y + timeline.getTimelineLaneHeight (Track::loop) / 2 - 2);
            robot->drag (timeline, edge, edge + juce::Point<int> (0, -20));
            check (timeline.getTrackHeight (Track::loop) == before - 20,
                   "dragging the bottom of a track header: " + juce::String (timeline.getTrackHeight (Track::loop)) + " px");
        });

        auto next = std::make_shared<std::function<void (size_t)>>();
        *next = [this, steps, next, failures, robot] (size_t index)
        {
            robot->covered = false;
            if (index >= steps.size())
            {
                std::cerr << "[selftest-edit] " << (*failures == 0 ? juce::String ("all passed") : juce::String (*failures) + " failed") << std::endl;
                setApplicationReturnValue (*failures);
                systemRequestedQuit();
                return;
            }
            steps[index]();
            juce::Timer::callAfterDelay (300, [next, index] { (*next) (index + 1); });
        };

        // Let the window settle (first layout, zoom to fit) before the first gesture.
        juce::Timer::callAfterDelay (1500, [next] { (*next) (0); });
    }

    bool verificationRun = false, hadSettings = false;
    juce::File settingsFile, settingsBackup;

    // Verification tool: `Roadie --selftest-take=/tmp/dir` opens the camera,
    // records a 3 s take (audio + video) into a project created in /tmp/dir,
    // exports it to MP4 and prints what happened on stderr, then quits.
    void scheduleTakeSelfTest (const juce::File& folder)
    {
        auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
        if (content == nullptr)
            return;

        auto& session = content->getSession();
        auto log = [] (const juce::String& line) { std::cerr << "[selftest] " << line << std::endl; };
        session.openCamera ({});
        selfTestStart = juce::Time::getMillisecondCounter();
        selfTestStep = 0;

        selfTestTick = [this, &session, folder, log]
        {
            const auto elapsed = juce::Time::getMillisecondCounter() - selfTestStart;
            auto next = [this] { juce::Timer::callAfterDelay (100, selfTestTick); };

            switch (selfTestStep)
            {
                case 0: // camera up and streaming
                    if (session.getCamera().getFrameCounter() > 30)
                    {
                        log ("camera: " + session.getCameraDescription());
                        session.saveProjectAs (folder.getChildFile ("Selftest.roadie"));
                        session.toggleTakeRecording();
                        log ("take started, recording video: " + juce::String ((int) session.getCamera().isRecording()));
                        selfTestStep = 1;
                        selfTestStart = juce::Time::getMillisecondCounter();
                    }
                    else if (elapsed > 30000 || session.getCameraError().isNotEmpty())
                    {
                        log ("camera failed: " + session.getCameraError());
                        return systemRequestedQuit();
                    }
                    return next();

                case 1: // 3 s take
                    if (elapsed > 3000)
                    {
                        session.toggleTakeRecording();
                        selfTestStep = 2;
                        selfTestStart = juce::Time::getMillisecondCounter();
                    }
                    return next();

                case 2: // take finalised
                    if (! session.isFinishingTake() && ! session.getTakes().empty())
                    {
                        const auto& take = session.getTakes().back();
                        log ("take: " + take.name + ", audio " + juce::String (take.durationSeconds, 2) + " s, video "
                             + juce::String ((int) take.hasVideo()) + " (" + juce::String (take.videoFile.getSize() / 1024) + " KB), "
                             + juce::String ((int) take.thumbnails.size()) + " thumbnails, video offset "
                             + juce::String (take.getVideoOffsetSeconds() * 1000.0, 1) + " ms");

                        // The take just landed as a clip on the timeline (finishTake()
                        // selects it) — export that clip's range.
                        Session::ExportRequest request;
                        request.endSeconds = take.durationSeconds;
                        const auto& arrangement = session.getArrangement();
                        const int clipIndex = session.getSelectedClip();
                        if (clipIndex >= 0 && clipIndex < (int) arrangement.clips.size())
                        {
                            const auto& clip = arrangement.clips[(size_t) clipIndex];
                            request.startSeconds = clip.start;
                            request.endSeconds = clip.end();
                        }
                        request.title = "Selftest";
                        request.burnInTitle = true;
                        selfTestStep = 3;
                        session.exportClip (request, [this, log] (bool ok, juce::String message, juce::File file)
                        {
                            log (ok ? "export ok: " + file.getFullPathName() + " (" + juce::String (file.getSize() / 1024) + " KB)"
                                    : "export failed: " + message);
                            selfTestStep = 4;
                        });
                    }
                    else if (elapsed > 15000)
                    {
                        log ("take never finished");
                        return systemRequestedQuit();
                    }
                    return next();

                case 3: // exporting
                    return next();

                default:
                    return systemRequestedQuit();
            }
        };
        juce::Timer::callAfterDelay (500, selfTestTick);
    }

    std::function<void()> selfTestTick;
    juce::uint32 selfTestStart = 0;
    int selfTestStep = 0;

    void scheduleSnapshot (const juce::File& target, const juce::StringArray& args)
    {
        auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
        if (content == nullptr)
            return;

        if (args.contains ("--demo"))
        {
            auto& session = content->getSession();
            session.openProject (createDemoProject (juce::File::getSpecialLocation (juce::File::tempDirectory)));
            session.selectClip (1);
            session.locate (6.0 * session.getSecondsPerBar());
            for (const auto& arg : args)
            {
                if (arg == "--demo-song")
                    session.importSong (writeDemoSong (juce::File::getSpecialLocation (juce::File::tempDirectory)));
                else if (arg.startsWith ("--demo-song="))
                    session.importSong (juce::File (arg.fromFirstOccurrenceOf ("=", false, false).unquoted()));
            }
        }

        for (const auto& arg : args)
        {
            if (arg.startsWith ("--track="))
                content->selectTrackByName (arg.fromFirstOccurrenceOf ("=", false, false));
            if (arg == "--light")
                content->setLightAppearance (true);
            if (arg == "--dark")
                content->setLightAppearance (false);
            if (arg == "--export")
                juce::Timer::callAfterDelay (1500, [content] { content->showExportSheet(); });
            if (arg.startsWith ("--scale="))
                juce::Desktop::getInstance().setGlobalScaleFactor (arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
            if (arg == "--no-library")
                content->setLibraryShown (false, false);
            if (arg == "--no-inspector")
                content->setInspectorShown (false, false);
            if (arg.startsWith ("--library="))
                content->showLibraryTab (arg.endsWith ("takes") ? LibraryPanel::Tab::takes
                                         : arg.endsWith ("songs") ? LibraryPanel::Tab::songs : LibraryPanel::Tab::captures);
            if (arg.startsWith ("--workspace="))
                content->setWorkspace (arg.endsWith ("edit") ? MainComponent::Workspace::edit : MainComponent::Workspace::play, false);
        }

        // --camera: open the webcam first and wait (up to 30 s, the time to
        // answer the permission prompt) until frames are flowing.
        const bool withCamera = args.contains ("--camera");
        if (withCamera)
            content->getSession().openCamera ({});

        auto shoot = [this, content, target]
        {
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            std::cerr << "[snapshot] scale " << juce::Desktop::getInstance().getGlobalScaleFactor() << ", window "
                      << content->getWidth() << "x" << content->getHeight() << ", screen "
                      << (display != nullptr ? display->logicalBounds.toNearestInt().toString() : juce::String ("?")) << std::endl;
            const auto image = content->createComponentSnapshot (content->getLocalBounds(), true, 2.0f);
            target.deleteFile();
            juce::FileOutputStream out (target);
            juce::PNGImageFormat png;
            if (out.openedOk())
                png.writeImageToStream (image, out);
            out.flush();
            systemRequestedQuit();
        };

        snapshotWaitStart = juce::Time::getMillisecondCounter();
        waitForSnapshot = [this, content, withCamera, shoot]
        {
            const auto elapsed = juce::Time::getMillisecondCounter() - snapshotWaitStart;
            const auto& camera = content->getSession().getCamera();
            const bool ready = elapsed > 3000 && (! withCamera || camera.getFrameCounter() > 30
                                                  || content->getSession().getCameraError().isNotEmpty() || elapsed > 30000);
            if (ready)
                shoot();
            else
                juce::Timer::callAfterDelay (250, waitForSnapshot);
        };
        juce::Timer::callAfterDelay (250, waitForSnapshot);
    }

    std::function<void()> waitForSnapshot;
    juce::uint32 snapshotWaitStart = 0;

    void shutdown() override
    {
        mainWindow = nullptr; // saves the settings

        if (verificationRun)
        {
            if (hadSettings)
                settingsBackup.moveFileTo (settingsFile);
            else
                settingsFile.deleteFile();
        }
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->toFront (true);
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::String name)
            : DocumentWindow (name, juce::Colour (0xff181613), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);

            auto* content = new MainComponent();
            content->onTitleChanged = [this] (const juce::String& title) { setName (title); };
            setContentOwned (content, true);

            setResizable (true, false);
            setResizeLimits (980, 600, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (RoadieApplication)
