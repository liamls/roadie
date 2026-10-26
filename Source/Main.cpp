#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"

#include <iostream>

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
        mainWindow.reset (new MainWindow (getApplicationName()));

        // Verification tool: `Roadie --snapshot=/tmp/ui.png [--track=loop|camera|amp] [--export]`
        // renders the window into a PNG after it has settled, then quits —
        // UI checks without screen-recording permissions or a human.
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        for (const auto& arg : args)
        {
            if (arg.startsWith ("--snapshot="))
                scheduleSnapshot (juce::File (arg.fromFirstOccurrenceOf ("=", false, false).unquoted()), args);
            if (arg.startsWith ("--selftest-take="))
                scheduleTakeSelfTest (juce::File (arg.fromFirstOccurrenceOf ("=", false, false).unquoted()));
        }
    }

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
        }

        // --camera: open the webcam first and wait (up to 30 s, the time to
        // answer the permission prompt) until frames are flowing.
        const bool withCamera = args.contains ("--camera");
        if (withCamera)
            content->getSession().openCamera ({});

        auto shoot = [this, content, target]
        {
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
        mainWindow = nullptr;
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
