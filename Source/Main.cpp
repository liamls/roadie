#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"

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
    project.name = juce::String::fromUTF8 ("D\xc3\xa9mo");
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
    project.arrangement.loopAnchor = 2.0 * bar;
    project.arrangement.loopStart = 2.0 * bar;
    project.arrangement.loopEnd = 9.0 * bar;
    project.cycleOn = true;
    project.cycleStart = 5.0 * bar;
    project.cycleEnd = 7.0 * bar;

    juce::String error;
    project.save (plucks (2.0 * bar, sr, 0.15, 330.0, 7), sr, error);
    return project.folder;
}

// Verification tool (--selftest-edit): real mouse events, injected into the
// window's peer as if they came from the system, so they go through JUCE's
// whole dispatch (hit testing, drag thresholds, modifiers, double-clicks).
// They share the system mouse: the real one moving over the window, or any
// window above it, spoils a gesture (reported as such when it is detectable).
struct MouseRobot
{
    juce::Component& window;
    juce::int64 time = juce::Time::currentTimeMillis();
    bool covered = false; // another window was over the point: JUCE drops such clicks

    void send (juce::Component& target, juce::Point<int> p, juce::ModifierKeys mods)
    {
        // JUCE reads the keyboard modifiers (⌥, ⌘) from the global state, and
        // the peer works in native points: the interface scale is undone here.
        juce::ModifierKeys::currentModifiers = mods;
        const auto position = window.getLocalPoint (&target, p).toFloat() * juce::Desktop::getInstance().getGlobalScaleFactor();
        if (auto* peer = window.getPeer())
        {
            covered = covered || ! peer->contains (position.roundToInt(), true);
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, position, mods,
                                    juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, time += 40);
        }
        juce::ModifierKeys::currentModifiers = {};
    }

    void drag (juce::Component& target, juce::Point<int> from, juce::Point<int> to, juce::ModifierKeys keys = {})
    {
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
        for (const auto& arg : args)
            verificationRun = verificationRun || arg.startsWith ("--snapshot=") || arg.startsWith ("--selftest-");
        if (verificationRun)
        {
            settingsFile = Session::getSettingsOptions().getDefaultFile();
            settingsBackup = settingsFile.getSiblingFile (settingsFile.getFileName() + ".verification");
            hadSettings = settingsFile.existsAsFile() && settingsFile.copyFileTo (settingsBackup);
        }

        mainWindow.reset (new MainWindow (getApplicationName()));

        // Verification tool: `Roadie --snapshot=/tmp/ui.png [--track=loop|camera|amp] [--export] [--demo]
        // [--scale=0.9] [--no-library] [--no-inspector]` renders the window into a PNG after it has
        // settled, then quits — UI checks without screen-recording permissions or a human. None of
        // these options is kept in the settings.
        for (const auto& arg : args)
        {
            if (arg.startsWith ("--snapshot="))
                scheduleSnapshot (juce::File (arg.fromFirstOccurrenceOf ("=", false, false).unquoted()), args);
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

        // Clicks only land where Roadie's window is the frontmost one.
        juce::Process::makeForegroundProcess();
        mainWindow->setAlwaysOnTop (true);
        mainWindow->toFront (true);
        content->disableTooltips();

        using Track = Session::Track;
        const double bar = session.getSecondsPerBar(); // 2.4 s in the demo
        auto robot = std::make_shared<MouseRobot> (MouseRobot { *mainWindow });
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
            robot->drag (timeline, timeline.getLanePoint (Track::amp, from), timeline.getLanePoint (Track::amp, from - bar));
            check (near (clips()[1].start, 4.0 * bar) && near (clips()[1].length, 3.0 * bar),
                   "drag a region one bar to the left: Prise 2 at " + juce::String (clips()[1].start, 2) + " s");
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
                   "trim the right edge by one bar: length " + juce::String (clips()[1].length, 2) + " s");
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
            robot->drag (timeline, timeline.getLanePoint (Track::amp, from), timeline.getLanePoint (Track::amp, from + 9.0 * bar),
                         juce::ModifierKeys (juce::ModifierKeys::altModifier));
            const auto after = clips();
            check (after.size() == count + 1 && near (after.back().start, 9.0 * bar) && after.back().take == 0 && near (after[0].start, 0.0),
                   "alt-drag duplicates the region (" + juce::String ((int) after.size()) + " regions)");
            check (session.getSelectedClip() == (int) after.size() - 1, "the copy is selected");
        });

        steps.push_back ([=, &session, &timeline]
        {
            const auto a = session.getArrangement();
            const double from = a.loopStart + 1.5 * bar;
            robot->drag (timeline, timeline.getLanePoint (Track::loop, from), timeline.getLanePoint (Track::loop, from + bar));
            const auto b = session.getArrangement();
            check (near (b.loopStart, a.loopStart + bar) && near (b.loopAnchor, a.loopAnchor + bar) && near (b.loopEnd, a.loopEnd + bar),
                   "drag the loop region one bar: start " + juce::String (b.loopStart, 2) + " s");
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
                        session.saveProjectAs (folder.getChildFile ("Autotest.roadie"));
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
                        request.title = "Autotest";
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
            : DocumentWindow (name, juce::Colour (0xff16171b), DocumentWindow::allButtons)
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
