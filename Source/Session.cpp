#include "Session.h"

#include "SyncAnalysis.h"

namespace
{
constexpr double calibrationSeconds = 5.0;
constexpr double noticeSeconds = 4.0;

double nowSeconds()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

juce::String utf8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}

bool writeMonoWav (const juce::File& file, const std::vector<float>& samples, double sampleRate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return false;

    const float* channels[1] = { samples.data() };
    return writer->writeFromFloatArrays (channels, 1, (int) samples.size());
}

float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }

bool readMonoWav (const juce::File& file, std::vector<float>& samples, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    samples.assign ((size_t) reader->lengthInSamples, 0.0f);
    float* channels[1] = { samples.data() };
    reader->read (channels, 1, 0, (int) reader->lengthInSamples);
    sampleRate = reader->sampleRate;
    return true;
}

std::vector<float> resample (const std::vector<float>& input, double fromRate, double toRate)
{
    if (fromRate <= 0.0 || toRate <= 0.0 || std::abs (fromRate - toRate) < 0.5)
        return input;

    const double ratio = fromRate / toRate;
    std::vector<float> output ((size_t) ((double) input.size() / ratio));
    juce::LagrangeInterpolator interpolator;
    interpolator.process (ratio, input.data(), output.data(), (int) output.size());
    return output;
}
} // namespace

juce::PropertiesFile::Options Session::getSettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Roadie";
    options.filenameSuffix = ".settings";
    options.folderName = "Roadie";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

Session::Session()
{
    settings.setStorageParameters (getSettingsOptions());

    auto& s = getSettings();

    // NAM captures are trained at 48 kHz, and the model itself costs
    // single-digit % CPU even at tiny buffers (Tools/nam_bench.cpp), so ask
    // for 48 kHz / 64 samples: the negotiated default is the real latency driver.
    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.sampleRate = 48000.0;
    preferred.bufferSize = 64;

    const auto savedAudio = s.getXmlValue ("audioDevice");
    deviceManager.initialise (1, 2, savedAudio.get(), true, {}, &preferred);
    if (savedAudio == nullptr)
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.sampleRate = 48000.0;
        setup.bufferSize = 64;
        deviceManager.setAudioDeviceSetup (setup, true);
    }
    deviceManager.addAudioCallback (&engine);
    mediaSampleRate = engine.getSampleRate();

    juce::Array<juce::File> folders;
    for (const auto& path : juce::StringArray::fromLines (s.getValue ("libraryFolders")))
        if (path.isNotEmpty())
            folders.add (juce::File (path));
    if (folders.isEmpty())
        folders = CaptureLibrary::getDefaultFolders();
    library.onChanged = [this] { sendChangeMessage(); };
    library.setFolders (folders);

    syncLagSeconds = s.getDoubleValue ("syncLagSeconds", 0.0);
    calibrated = s.getBoolValue ("calibrated", false);

    const juce::File lastProject (s.getValue ("lastProject"));
    if (! (lastProject.isDirectory() && openProject (lastProject)))
        newProject();

    if (s.getBoolValue ("cameraEnabled", false) && CameraCapture::isSupported())
        openCamera (s.getValue ("cameraDevice"));

    startTimerHz (20);
}

Session::~Session()
{
    stopTimer();
    *alive = false;

    if (recordingTake)
    {
        engine.stopTake();
        recordingTake = false;
    }

    captureEngineStateIntoProject();
    if (hasProjectOnDisk() || ! project.takes.empty() || engine.getLooper().getRecordedLengthSamples() > 0)
        saveProject();

    saveSettings();
    loaderPool.removeAllJobs (true, 5000);
    exporter.cancel();
    camera.close();
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
}

void Session::saveSettings()
{
    auto& s = getSettings();

    juce::StringArray folders;
    for (const auto& f : library.getFolders())
        folders.add (f.getFullPathName());
    s.setValue ("libraryFolders", folders.joinIntoString ("\n"));
    s.setValue ("syncLagSeconds", syncLagSeconds);
    s.setValue ("calibrated", calibrated);
    s.setValue ("lastProject", project.folder.getFullPathName());

    if (auto audioState = deviceManager.createStateXml())
        s.setValue ("audioDevice", audioState.get());

    s.saveIfNeeded();
}

//==============================================================================
void Session::timerCallback()
{
    // A device change (sample rate) invalidates the decoded takes.
    if (std::abs (engine.getSampleRate() - mediaSampleRate) > 0.5)
    {
        mediaSampleRate = engine.getSampleRate();
        reloadAllTakeMedia();
        pushArrangementToEngine();
        pushCycleToEngine();
    }

    if (engine.getLoopRecordCount() != knownLoopRecordCount)
        syncNewLoop();

    updatePreview();

    if (calibrationState == CalibrationState::listening)
    {
        if (engine.isCalibrationCaptureComplete() && camera.isMicCaptureComplete())
            finishCalibration();
        else if (nowSeconds() - calibrationStartedAt > calibrationSeconds + 4.0)
        {
            camera.stopMicCapture();
            calibrationState = CalibrationState::failed;
            calibrationMessage = utf8 ("Le micro de la cam\xc3\xa9ra n'a rien envoy\xc3\xa9 \xe2\x80\x94 v\xc3\xa9rifie l'acc\xc3\xa8s au micro.");
            sendChangeMessage();
        }
    }

    if (notice.isNotEmpty() && nowSeconds() - noticeTime > noticeSeconds)
    {
        notice.clear();
        sendChangeMessage();
    }
}

void Session::syncNewLoop()
{
    // A loop just started recording: it plays from its first sample on,
    // open-ended. Edits recorded before it can't restore its placement.
    knownLoopRecordCount = engine.getLoopRecordCount();
    const double anchor = (double) engine.getLoopAnchor() / engine.getSampleRate();
    project.arrangement.loopAnchor = anchor;
    project.arrangement.loopStart = anchor;
    project.arrangement.loopEnd = -1.0;
    ++loopGeneration;
    sendChangeMessage();
}

void Session::updatePreview()
{
    // Which take's movie is under the playhead, and where in it?
    const double t = getPlayheadSeconds();
    const auto& clips = project.arrangement.clips;
    const Take* take = nullptr;
    double movieTime = 0.0;

    for (auto it = clips.rbegin(); it != clips.rend(); ++it)
    {
        if (t < it->start || t >= it->end())
            continue;
        if (juce::isPositiveAndBelow (it->take, (int) project.takes.size()))
        {
            const auto& candidate = project.takes[(size_t) it->take];
            if (candidate.hasVideo())
            {
                take = &candidate;
                movieTime = t - it->start + it->sourceStart - candidate.getVideoOffsetSeconds();
            }
        }
        break; // the top-most region decides, video or not
    }

    const bool wanted = take != nullptr && ! recordingTake
                        && (! camera.isOpen() || engine.isTransportRunning() || wasRecentlyLocated());
    if (wanted)
        preview.request (take->videoFile, movieTime);

    if (wanted != previewing)
    {
        previewing = wanted;
        if (! wanted)
            preview.clear();
    }
}

juce::String Session::getNotice() const
{
    return notice;
}

void Session::notify (const juce::String& message)
{
    notice = message;
    noticeTime = nowSeconds();
    sendChangeMessage();
}

//==============================================================================
void Session::loadCapture (const juce::File& namFile)
{
    if (! namFile.existsAsFile())
    {
        notify (utf8 ("Capture introuvable : ") + namFile.getFileName());
        return;
    }

    loadingCapture = true;
    sendChangeMessage();

    std::weak_ptr<bool> weak = alive;
    loaderPool.addJob ([this, weak, namFile]
    {
        juce::String error;
        const bool ok = engine.getAmp().loadModel (namFile, error);
        auto info = CaptureLibrary::describe (namFile);

        juce::MessageManager::callAsync ([this, weak, namFile, ok, error, info]
        {
            if (weak.expired())
                return;

            loadingCapture = false;
            if (ok)
            {
                project.captureFile = namFile;
                captureTag = info.tag;
            }
            else
            {
                notify (utf8 ("Capture illisible : ") + error);
            }
            sendChangeMessage();
        });
    });
}

juce::String Session::getCaptureName() const
{
    return engine.getAmp().isLoaded() ? engine.getAmp().getModelName() : juce::String();
}

juce::String Session::getCaptureTag() const
{
    return engine.getAmp().isLoaded() ? captureTag : juce::String();
}

void Session::loadImpulseResponse (const juce::File& irFile)
{
    if (! irFile.existsAsFile())
        return;

    engine.getAmp().loadImpulseResponse (irFile);
    project.irFile = irFile;
    sendChangeMessage();
}

void Session::clearImpulseResponse()
{
    engine.getAmp().clearImpulseResponse();
    project.irFile = juce::File();
    sendChangeMessage();
}

juce::String Session::getIrName() const
{
    return engine.getAmp().isIrLoaded() ? engine.getAmp().getIrName() : juce::String();
}

void Session::addCaptureFolder (const juce::File& folder)
{
    library.addFolder (folder);
    saveSettings();
}

juce::Range<float> Session::getKnobRange (Knob knob)
{
    switch (knob)
    {
        case Knob::gain:   return { -20.0f, 20.0f };
        case Knob::volume: return { -40.0f, 12.0f };
        case Knob::bass:
        case Knob::mid:
        case Knob::treble:
        case Knob::presence:
        default:           return { -ToneStack::maxGainDb, ToneStack::maxGainDb };
    }
}

void Session::setKnob (Knob knob, float db)
{
    db = getKnobRange (knob).clipValue (db);
    knobs[(size_t) knob] = db;

    auto& amp = engine.getAmp();
    switch (knob)
    {
        case Knob::gain:     amp.setInputGainDb (db); break;
        case Knob::volume:   amp.setOutputVolumeDb (db); break;
        case Knob::bass:     amp.getToneStack().setGainDb (ToneStack::bass, db); break;
        case Knob::mid:      amp.getToneStack().setGainDb (ToneStack::mid, db); break;
        case Knob::treble:   amp.getToneStack().setGainDb (ToneStack::treble, db); break;
        case Knob::presence: amp.getToneStack().setGainDb (ToneStack::presence, db); break;
    }
}

float Session::getKnob (Knob knob) const
{
    return knobs[(size_t) knob];
}

//==============================================================================
void Session::setBpm (double newBpm)
{
    bpm = juce::jlimit (40.0, 240.0, newBpm);
    engine.getMetronome().setBpm (bpm);
    sendChangeMessage();
}

double Session::getBpm() const { return bpm; }

void Session::setBeatsPerBar (int beats)
{
    beatsPerBar = juce::jlimit (2, 7, beats);
    engine.getMetronome().setBeatsPerBar (beatsPerBar);
    sendChangeMessage();
}

int Session::getBeatsPerBar() const { return beatsPerBar; }

void Session::setMetronomeOn (bool shouldBeOn)
{
    metronomeOn = shouldBeOn;
    engine.getMetronome().setEnabled (shouldBeOn);
    sendChangeMessage();
}

bool Session::isMetronomeOn() const { return metronomeOn; }

double Session::getSamplesPerBar() const
{
    return 60.0 / bpm * engine.getSampleRate() * beatsPerBar;
}

//==============================================================================
void Session::recordLoop()      { engine.recordLoop(); sendChangeMessage(); }
void Session::undo()            { engine.getLooper().requestUndo(); sendChangeMessage(); }
void Session::clearLoop()       { engine.getLooper().requestClear(); sendChangeMessage(); }

void Session::togglePlay()
{
    // Space during a take ends it, like stopping the tape.
    if (recordingTake)
    {
        stopAll();
        return;
    }
    engine.togglePlay();
    sendChangeMessage();
}

void Session::returnToStart()
{
    locate (project.cycleOn && engine.isTransportRunning() ? project.cycleStart : 0.0);
}

void Session::stopAll()
{
    if (recordingTake)
        toggleTakeRecording();
    engine.stopAll();
    sendChangeMessage();
}

void Session::toggleOverdub()
{
    engine.play();
    engine.getLooper().requestOverdub();
    sendChangeMessage();
}

double Session::getPlayheadSeconds() const
{
    return (double) engine.getTransportPosition() / engine.getSampleRate();
}

void Session::locate (double seconds)
{
    if (recordingTake)
        return; // the take's place on the timeline is its start position
    engine.locate (toSamples (std::max (0.0, seconds)));
    lastLocateTime = nowSeconds();
    sendChangeMessage();
}

bool Session::wasRecentlyLocated() const
{
    return nowSeconds() - lastLocateTime < 2.0;
}

void Session::jumpBars (int bars)
{
    const double bar = getSecondsPerBar();
    const double current = getPlayheadSeconds() / bar;
    const double target = bars > 0 ? std::floor (current + 1.0e-6) + bars : std::ceil (current - 1.0e-6) + bars;
    locate (std::max (0.0, target) * bar);
}

void Session::setCycleRange (double start, double end)
{
    project.cycleStart = std::max (0.0, std::min (start, end));
    project.cycleEnd = std::max (start, end);
    pushCycleToEngine();
    sendChangeMessage();
}

void Session::setCycleOn (bool shouldBeOn)
{
    // First use: cycle the region under the playhead, or four bars from it.
    if (shouldBeOn && project.cycleEnd - project.cycleStart < 0.05)
    {
        const double bar = getSecondsPerBar();
        const double from = std::floor (getPlayheadSeconds() / bar) * bar;
        project.cycleStart = from;
        project.cycleEnd = from + 4.0 * bar;
        if (const int c = getSelectedClip(); c >= 0)
        {
            project.cycleStart = project.arrangement.clips[(size_t) c].start;
            project.cycleEnd = project.arrangement.clips[(size_t) c].end();
        }
    }
    project.cycleOn = shouldBeOn;
    pushCycleToEngine();
    sendChangeMessage();
}

void Session::pushCycleToEngine()
{
    engine.setCycle (project.cycleOn, toSamples (project.cycleStart), toSamples (project.cycleEnd));
}

std::int64_t Session::toSamples (double seconds) const
{
    return (std::int64_t) std::llround (seconds * engine.getSampleRate());
}

void Session::toggleTakeRecording()
{
    if (finishingTake)
        return;

    if (! recordingTake)
    {
        if (! project.getTakesFolder().createDirectory())
        {
            notify (utf8 ("Impossible de cr\xc3\xa9" "er le dossier du projet."));
            return;
        }

        const int number = project.getNextTakeNumber();
        pendingTake = {};
        pendingTake.name = Project::getTakeName (number);
        pendingTake.audioFile = project.getTakeFile (number, ".wav");
        pendingTake.createdAt = juce::Time::getCurrentTime();

        engine.play();
        if (! engine.startTake (pendingTake.audioFile))
        {
            notify (utf8 ("Impossible d'\xc3\xa9" "crire la prise sur le disque."));
            return;
        }

        if (camera.isOpen() && ! muted[(size_t) Track::camera])
        {
            pendingTake.videoFile = project.getTakeFile (number, ".mov");
            if (! camera.startRecording (pendingTake.videoFile))
                pendingTake.videoFile = juce::File();
        }

        recordingTake = true;
        takeStartedAt = nowSeconds();
        sendChangeMessage();
        return;
    }

    recordingTake = false;
    finishingTake = true;
    pendingTakeStart = getTakeStartSeconds();

    auto& recorder = engine.getTakeRecorder();
    pendingTake.audioStartHostSeconds = recorder.getFirstSampleHostSeconds();
    pendingTake.durationSeconds = recorder.getRecordedSeconds();
    pendingTake.syncLagSeconds = calibrated ? syncLagSeconds : 0.0;
    engine.stopTake();

    if (pendingTake.videoFile != juce::File())
    {
        std::weak_ptr<bool> weak = alive;
        camera.stopRecording ([this, weak] (bool ok, double firstFrameHostSeconds)
        {
            if (! weak.expired())
                finishTake (ok, firstFrameHostSeconds);
        });
    }
    else
    {
        finishTake (false, 0.0);
    }

    sendChangeMessage();
}

void Session::finishTake (bool videoOk, double firstFrameHostSeconds)
{
    finishingTake = false;

    if (videoOk && firstFrameHostSeconds > 0.0)
    {
        pendingTake.videoStartHostSeconds = firstFrameHostSeconds;
        pendingTake.thumbnails = camera.getRecordingThumbnails();
    }
    else
    {
        if (pendingTake.videoFile != juce::File())
            pendingTake.videoFile.deleteFile();
        pendingTake.videoFile = juce::File();
    }

    if (pendingTake.durationSeconds < 0.2)
    {
        pendingTake.audioFile.deleteFile();
        pendingTake.videoFile.deleteFile();
        notify (utf8 ("Prise trop courte, ignor\xc3\xa9" "e."));
        return;
    }

    const bool withVideo = pendingTake.hasVideo();
    project.takes.push_back (std::move (pendingTake));
    pendingTake = {};

    const int takeIndex = (int) project.takes.size() - 1;
    loadTakeMedia (takeIndex);

    // The take lands where it was played.
    Clip clip;
    clip.take = takeIndex;
    clip.start = pendingTakeStart;
    clip.length = project.takes.back().durationSeconds;
    auto edited = project.arrangement;
    edited.clips.push_back (clip);
    applyEdit (edited, "Enregistrer " + project.takes.back().name);
    selectClip ((int) project.arrangement.clips.size() - 1);

    // Takes already sit in the project folder: write project.json right away
    // so nothing recorded is ever "unsaved".
    saveProject();
    notify (project.takes.back().name + (withVideo ? utf8 (" enregistr\xc3\xa9" "e (audio + vid\xc3\xa9o)")
                                                   : utf8 (" enregistr\xc3\xa9" "e (audio)")));
}

double Session::getTakeSeconds() const
{
    return recordingTake ? nowSeconds() - takeStartedAt : 0.0;
}

double Session::getTakeStartSeconds() const
{
    return (double) engine.getTakeStartPosition() / engine.getSampleRate();
}

//==============================================================================
void Session::setMuted (Track track, bool shouldBeMuted)
{
    muted[(size_t) track] = shouldBeMuted;
    updateMuteState();
}

bool Session::isMuted (Track track) const { return muted[(size_t) track]; }

void Session::setSolo (Track track, bool solo)
{
    soloed[(size_t) track] = solo;
    updateMuteState();
}

bool Session::isSolo (Track track) const { return soloed[(size_t) track]; }

void Session::updateMuteState()
{
    const bool guitarSolo = soloed[(size_t) Track::amp];
    const bool loopSolo = soloed[(size_t) Track::loop];

    engine.setGuitarMuted (muted[(size_t) Track::amp] || (loopSolo && ! guitarSolo));
    engine.setLoopMuted (muted[(size_t) Track::loop] || (guitarSolo && ! loopSolo));
    sendChangeMessage();
}

void Session::setTrackVolume (Track track, float db)
{
    db = juce::jlimit (-60.0f, 6.0f, db);
    if (track == Track::amp)
    {
        project.guitarVolumeDb = db;
        engine.setGuitarGain (dbToGain (db));
    }
    else if (track == Track::loop)
    {
        project.loopVolumeDb = db;
        engine.setLoopGain (dbToGain (db));
    }
}

float Session::getTrackVolume (Track track) const
{
    return track == Track::amp ? project.guitarVolumeDb : (track == Track::loop ? project.loopVolumeDb : 0.0f);
}

//==============================================================================
void Session::applyEdit (const Arrangement& edited, const juce::String& actionName)
{
    if (edited == project.arrangement)
        return;

    undoStack.push_back ({ project.arrangement, loopGeneration, actionName });
    if (undoStack.size() > 200)
        undoStack.erase (undoStack.begin());
    redoStack.clear();

    project.arrangement = edited;
    if (selection == Selection::clip && ! juce::isPositiveAndBelow (selectedClip, (int) edited.clips.size()))
        clearSelection();
    pushArrangementToEngine();
    sendChangeMessage();
}

void Session::undoEdit()
{
    if (undoStack.empty())
        return;

    auto state = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back ({ project.arrangement, loopGeneration, state.name });

    // A loop recorded since then keeps its own placement.
    if (state.loopGeneration != loopGeneration)
    {
        state.arrangement.loopAnchor = project.arrangement.loopAnchor;
        state.arrangement.loopStart = project.arrangement.loopStart;
        state.arrangement.loopEnd = project.arrangement.loopEnd;
    }

    project.arrangement = state.arrangement;
    clearSelection();
    pushArrangementToEngine();
    notify ("Annuler : " + state.name);
}

void Session::redoEdit()
{
    if (redoStack.empty())
        return;

    auto state = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back ({ project.arrangement, loopGeneration, state.name });

    if (state.loopGeneration != loopGeneration)
    {
        state.arrangement.loopAnchor = project.arrangement.loopAnchor;
        state.arrangement.loopStart = project.arrangement.loopStart;
        state.arrangement.loopEnd = project.arrangement.loopEnd;
    }

    project.arrangement = state.arrangement;
    clearSelection();
    pushArrangementToEngine();
    notify (utf8 ("R\xc3\xa9tablir : ") + state.name);
}

void Session::selectClip (int index)
{
    if (juce::isPositiveAndBelow (index, (int) project.arrangement.clips.size()))
    {
        selection = Selection::clip;
        selectedClip = index;
    }
    else
    {
        selection = Selection::none;
        selectedClip = -1;
    }
    sendChangeMessage();
}

void Session::selectLoopRegion()
{
    selection = hasLoop() ? Selection::loop : Selection::none;
    selectedClip = -1;
    sendChangeMessage();
}

void Session::clearSelection()
{
    selection = Selection::none;
    selectedClip = -1;
    sendChangeMessage();
}

bool Session::splitAtPlayhead()
{
    const double t = getPlayheadSeconds();
    auto edited = project.arrangement;
    const double minimum = 0.05;

    // The selected region, else every region under the playhead.
    std::vector<int> targets;
    if (const int c = getSelectedClip(); c >= 0)
        targets.push_back (c);
    else
        for (int i = 0; i < (int) edited.clips.size(); ++i)
            targets.push_back (i);

    int split = 0;
    for (auto it = targets.rbegin(); it != targets.rend(); ++it)
    {
        auto& clip = edited.clips[(size_t) *it];
        if (t <= clip.start + minimum || t >= clip.end() - minimum)
            continue;

        Clip right = clip;
        right.start = t;
        right.sourceStart = clip.sourceStart + (t - clip.start);
        right.length = clip.end() - t;
        clip.length = t - clip.start;
        edited.clips.insert (edited.clips.begin() + *it + 1, right);
        ++split;
    }

    if (split == 0)
    {
        notify (utf8 ("Place la t\xc3\xaate de lecture sur une r\xc3\xa9gion pour la couper."));
        return false;
    }

    const int keep = getSelectedClip();
    applyEdit (edited, split > 1 ? utf8 ("Couper les r\xc3\xa9gions") : utf8 ("Couper la r\xc3\xa9gion"));
    if (keep >= 0)
        selectClip (keep + 1); // the right-hand part, like GarageBand
    return true;
}

bool Session::deleteSelection()
{
    if (const int c = getSelectedClip(); c >= 0)
    {
        auto edited = project.arrangement;
        edited.clips.erase (edited.clips.begin() + c);
        clearSelection();
        applyEdit (edited, utf8 ("Supprimer la r\xc3\xa9gion"));
        return true;
    }
    return false;
}

bool Session::duplicateSelection()
{
    const int c = getSelectedClip();
    if (c < 0)
        return false;

    auto edited = project.arrangement;
    auto copy = edited.clips[(size_t) c];
    copy.start = copy.end();
    edited.clips.push_back (copy);
    applyEdit (edited, utf8 ("Dupliquer la r\xc3\xa9gion"));
    selectClip ((int) edited.clips.size() - 1);
    return true;
}

void Session::insertTake (int takeIndex, double atSeconds)
{
    if (! juce::isPositiveAndBelow (takeIndex, (int) project.takes.size()))
        return;

    Clip clip;
    clip.take = takeIndex;
    clip.start = std::max (0.0, atSeconds);
    clip.length = getTakeLength (takeIndex);
    auto edited = project.arrangement;
    edited.clips.push_back (clip);
    applyEdit (edited, utf8 ("Ins\xc3\xa9rer ") + project.takes[(size_t) takeIndex].name);
    selectClip ((int) edited.clips.size() - 1);
}

void Session::setLoopOpenEnded()
{
    auto edited = project.arrangement;
    edited.loopEnd = -1.0;
    applyEdit (edited, utf8 ("Boucler jusqu'\xc3\xa0 la fin"));
}

bool Session::isTakeUsed (int takeIndex) const
{
    for (const auto& c : project.arrangement.clips)
        if (c.take == takeIndex)
            return true;
    return false;
}

bool Session::hasLoop() const
{
    const auto& looper = engine.getLooper();
    return looper.getRecordedLengthSamples() > 0 && looper.getState() != Looper::State::Recording;
}

double Session::getLoopLengthSeconds() const
{
    return engine.getLooper().getLoopLengthSeconds();
}

double Session::getContentEnd() const
{
    return project.arrangement.getContentEnd (hasLoop());
}

const Session::TakeMedia* Session::getTakeMedia (int takeIndex) const
{
    if (! juce::isPositiveAndBelow (takeIndex, (int) takeMedia.size()) || takeMedia[(size_t) takeIndex].audio == nullptr)
        return nullptr;
    return &takeMedia[(size_t) takeIndex];
}

double Session::getTakeLength (int takeIndex) const
{
    if (const auto* media = getTakeMedia (takeIndex))
        return (double) media->audio->size() / media->sampleRate;
    return juce::isPositiveAndBelow (takeIndex, (int) project.takes.size()) ? project.takes[(size_t) takeIndex].durationSeconds : 0.0;
}

void Session::loadTakeMedia (int takeIndex)
{
    if (! juce::isPositiveAndBelow (takeIndex, (int) project.takes.size()))
        return;
    if ((int) takeMedia.size() < (int) project.takes.size())
        takeMedia.resize (project.takes.size());

    TakeMedia media;
    std::vector<float> samples;
    double rate = 0.0;
    if (readMonoWav (project.takes[(size_t) takeIndex].audioFile, samples, rate))
    {
        // Peaks at the file's rate, playback copy at the engine's.
        const int perPeak = std::max (1, (int) std::round (rate / TakeMedia::peaksPerSecond));
        media.peaks.reserve (samples.size() / (size_t) perPeak + 1);
        for (size_t i = 0; i < samples.size(); i += (size_t) perPeak)
        {
            float peak = 0.0f;
            for (size_t j = i; j < std::min (samples.size(), i + (size_t) perPeak); ++j)
                peak = std::max (peak, std::abs (samples[j]));
            media.peaks.push_back (peak);
        }

        media.sampleRate = engine.getSampleRate();
        media.audio = std::make_shared<const std::vector<float>> (resample (samples, rate, media.sampleRate));
    }
    takeMedia[(size_t) takeIndex] = std::move (media);
}

void Session::reloadAllTakeMedia()
{
    takeMedia.clear();
    takeMedia.resize (project.takes.size());
    for (int i = 0; i < (int) project.takes.size(); ++i)
        loadTakeMedia (i);
}

void Session::pushArrangementToEngine()
{
    auto list = std::make_shared<AudioEngine::PlaybackList>();
    const double sr = engine.getSampleRate();
    for (const auto& clip : project.arrangement.clips)
    {
        const auto* media = getTakeMedia (clip.take);
        if (media == nullptr)
            continue;

        AudioEngine::PlaybackClip p;
        p.audio = media->audio;
        p.start = toSamples (clip.start);
        p.sourceStart = (std::int64_t) std::llround (clip.sourceStart * sr);
        p.length = toSamples (clip.length);
        list->push_back (p);
    }
    engine.setPlaybackClips (std::move (list));

    const auto& a = project.arrangement;
    engine.setLoopPlacement (toSamples (a.loopAnchor), toSamples (a.loopStart), a.loopEnd < 0.0 ? -1 : toSamples (a.loopEnd));
}

//==============================================================================
//==============================================================================
void Session::openCamera (const juce::String& deviceName)
{
    cameraOpening = true;
    cameraError.clear();
    sendChangeMessage();

    std::weak_ptr<bool> weak = alive;
    camera.open (deviceName, [this, weak] (bool ok, juce::String error)
    {
        if (weak.expired())
            return;

        cameraOpening = false;
        cameraError = ok ? juce::String() : error;
        getSettings().setValue ("cameraEnabled", ok);
        if (ok)
            getSettings().setValue ("cameraDevice", camera.getDeviceName());
        sendChangeMessage();
    });
}

void Session::closeCamera()
{
    if (recordingTake)
        toggleTakeRecording();
    camera.close();
    getSettings().setValue ("cameraEnabled", false);
    sendChangeMessage();
}

juce::String Session::getCameraDescription() const
{
    if (! camera.isOpen())
        return cameraOpening ? utf8 ("Ouverture\xe2\x80\xa6") : utf8 ("Cam\xc3\xa9ra \xc3\xa9teinte");

    auto text = camera.getDeviceName();
    if (camera.getFrameHeight() > 0)
        text << utf8 (" \xc2\xb7 ") << camera.getFrameHeight() << "p";
    return text;
}

void Session::startCalibration()
{
    if (! camera.isOpen())
    {
        calibrationState = CalibrationState::failed;
        calibrationMessage = utf8 ("Active d'abord la cam\xc3\xa9ra.");
        sendChangeMessage();
        return;
    }

    calibrationState = CalibrationState::starting;
    calibrationMessage = utf8 ("Pr\xc3\xa9paration du micro de la cam\xc3\xa9ra\xe2\x80\xa6");
    sendChangeMessage();

    std::weak_ptr<bool> weak = alive;
    camera.startMicCapture (calibrationSeconds, [this, weak] (bool ok, juce::String error)
    {
        if (weak.expired())
            return;

        if (! ok)
        {
            calibrationState = CalibrationState::failed;
            calibrationMessage = error;
            sendChangeMessage();
            return;
        }

        engine.startCalibrationCapture (calibrationSeconds);
        calibrationState = CalibrationState::listening;
        calibrationStartedAt = nowSeconds();
        calibrationMessage = utf8 ("\xc3\x80 toi : un accord \xc3\xa9touff\xc3\xa9 bien sec, une seule fois.");
        sendChangeMessage();
    });
}

void Session::finishCalibration()
{
    double interfaceStart = 0.0, cameraStart = 0.0, cameraRate = 0.0;
    const auto interfaceAudio = engine.getCalibrationCapture (interfaceStart);
    const auto cameraAudio = camera.getMicCapture (cameraStart, cameraRate);
    camera.stopMicCapture();

    const auto result = roadie::sync::measureLag ({ interfaceAudio.data(), (int) interfaceAudio.size(), engine.getSampleRate(), interfaceStart },
                                                  { cameraAudio.data(), (int) cameraAudio.size(), cameraRate, cameraStart });

    if (result.ok)
    {
        syncLagSeconds = result.interfaceLagSeconds;
        calibrated = true;
        calibrationState = CalibrationState::done;
        calibrationMessage = utf8 ("Synchronis\xc3\xa9" "e \xe2\x80\x94 d\xc3\xa9" "calage de ")
                             + juce::String (std::abs (syncLagSeconds * 1000.0), 0) + utf8 (" ms corrig\xc3\xa9.");
        saveSettings();
    }
    else
    {
        calibrationState = CalibrationState::failed;
        calibrationMessage = utf8 ("Aucune attaque nette entendue des deux c\xc3\xb4t\xc3\xa9s \xe2\x80\x94 r\xc3\xa9" "essaie plus fort, pr\xc3\xa8s de l'ordinateur.");
    }
    sendChangeMessage();
}

//==============================================================================
std::vector<float> Session::renderMix (double start, double end, double sampleRate) const
{
    const auto total = (size_t) std::max<std::int64_t> (0, (std::int64_t) std::llround ((end - start) * sampleRate));
    std::vector<float> guitar (total, 0.0f), loop (total, 0.0f);
    const auto at = [&] (double seconds) { return (std::int64_t) std::llround ((seconds - start) * sampleRate); };

    // Regions: the top-most one plays where they overlap, as in the engine.
    std::vector<char> covered (total, 0);
    const auto& clips = project.arrangement.clips;
    for (auto it = clips.rbegin(); it != clips.rend(); ++it)
    {
        const auto* media = getTakeMedia (it->take);
        if (media == nullptr)
            continue;

        const auto source = std::abs (media->sampleRate - sampleRate) < 0.5 ? *media->audio
                                                                              : resample (*media->audio, media->sampleRate, sampleRate);
        const auto from = std::max<std::int64_t> (0, at (it->start));
        const auto to = std::min<std::int64_t> ((std::int64_t) total, at (it->end()));
        const auto offset = (std::int64_t) std::llround (it->sourceStart * sampleRate) - at (it->start);
        for (auto i = from; i < to; ++i)
        {
            if (covered[(size_t) i] != 0)
                continue;
            covered[(size_t) i] = 1;
            const auto s = i + offset;
            if (s >= 0 && s < (std::int64_t) source.size())
                guitar[(size_t) i] = source[(size_t) s];
        }
    }

    // The loop, repeating from its anchor across its region.
    if (hasLoop())
    {
        double loopRate = 0.0;
        const auto content = resample (copyLoop (loopRate), loopRate, sampleRate);
        const auto& a = project.arrangement;
        const auto length = (std::int64_t) content.size();
        if (length > 0)
        {
            const auto from = std::max<std::int64_t> (0, at (a.loopStart));
            const auto to = a.loopEnd < 0.0 ? (std::int64_t) total : std::min<std::int64_t> ((std::int64_t) total, at (a.loopEnd));
            const auto anchor = at (a.loopAnchor);
            for (auto i = from; i < to; ++i)
                loop[(size_t) i] = content[(size_t) ((((i - anchor) % length) + length) % length)];
        }
    }

    const bool guitarSolo = soloed[(size_t) Track::amp], loopSolo = soloed[(size_t) Track::loop];
    const float g = (muted[(size_t) Track::amp] || (loopSolo && ! guitarSolo)) ? 0.0f : dbToGain (project.guitarVolumeDb);
    const float l = (muted[(size_t) Track::loop] || (guitarSolo && ! loopSolo)) ? 0.0f : dbToGain (project.loopVolumeDb);

    std::vector<float> mix (total);
    for (size_t i = 0; i < total; ++i)
        mix[i] = juce::jlimit (-1.0f, 1.0f, guitar[i] * g + loop[i] * l);
    return mix;
}

std::vector<ClipExportSettings::VideoSegment> Session::getVideoSegments (double start, double end) const
{
    // Sweep the regions top-most first: each one shows its video where no
    // region above it covers the range already.
    std::vector<ClipExportSettings::VideoSegment> segments;
    std::vector<juce::Range<double>> covered;
    const auto& clips = project.arrangement.clips;

    for (auto it = clips.rbegin(); it != clips.rend(); ++it)
    {
        const juce::Range<double> visible (std::max (start, it->start), std::min (end, it->end()));
        if (visible.isEmpty())
            continue;

        // Pieces of `visible` not under any region above.
        std::vector<juce::Range<double>> pieces { visible };
        for (const auto& c : covered)
        {
            std::vector<juce::Range<double>> next;
            for (const auto& p : pieces)
            {
                if (p.getEnd() <= c.getStart() || p.getStart() >= c.getEnd())
                {
                    next.push_back (p);
                    continue;
                }
                if (p.getStart() < c.getStart())
                    next.push_back ({ p.getStart(), c.getStart() });
                if (p.getEnd() > c.getEnd())
                    next.push_back ({ c.getEnd(), p.getEnd() });
            }
            pieces = std::move (next);
        }
        covered.push_back (visible);

        if (! juce::isPositiveAndBelow (it->take, (int) project.takes.size()))
            continue;
        const auto& take = project.takes[(size_t) it->take];
        if (! take.hasVideo())
            continue;

        for (const auto& p : pieces)
        {
            if (p.getLength() < 1.0 / 60.0)
                continue;
            ClipExportSettings::VideoSegment segment;
            segment.file = take.videoFile;
            segment.atSeconds = p.getStart() - start;
            segment.durationSeconds = p.getLength();
            segment.sourceStartSeconds = p.getStart() - it->start + it->sourceStart - take.getVideoOffsetSeconds();
            segments.push_back (segment);
        }
    }

    std::sort (segments.begin(), segments.end(), [] (const auto& a, const auto& b) { return a.atSeconds < b.atSeconds; });
    return segments;
}

bool Session::hasVideoBetween (double start, double end) const
{
    return ! getVideoSegments (start, end).empty();
}

void Session::exportClip (const ExportRequest& request, std::function<void (bool, juce::String, juce::File)> onDone)
{
    if (request.endSeconds - request.startSeconds < 0.1)
    {
        onDone (false, utf8 ("Rien \xc3\xa0 exporter dans cette plage."), {});
        return;
    }

    auto segments = getVideoSegments (request.startSeconds, request.endSeconds);
    if (segments.empty())
    {
        onDone (false, utf8 ("Pas de vid\xc3\xa9o dans cette plage \xe2\x80\x94 exporte l'audio seul."), {});
        return;
    }

    const auto folder = project.getExportsFolder();
    folder.createDirectory();
    const auto name = juce::File::createLegalFileName (request.title.trim().isNotEmpty() ? request.title.trim() : project.name);
    const auto output = folder.getNonexistentChildFile (name, ".mp4", false);

    // The soundtrack: the arrangement's mix over the range.
    const double sr = engine.getSampleRate();
    const auto soundtrack = folder.getChildFile (".mix-" + juce::String (juce::Time::currentTimeMillis()) + ".wav");
    if (! writeMonoWav (soundtrack, renderMix (request.startSeconds, request.endSeconds, sr), sr))
    {
        onDone (false, utf8 ("\xc3\x89" "criture du mixage impossible."), {});
        return;
    }

    ClipExportSettings clip;
    clip.audioFile = soundtrack;
    clip.segments = std::move (segments);
    clip.vertical = request.vertical;
    clip.mirror = request.mirror;
    clip.title = request.title.trim();
    clip.burnInTitle = request.burnInTitle;
    clip.outputFile = output;

    exporter.start (clip, [onDone, output, soundtrack] (bool ok, juce::String message)
    {
        soundtrack.deleteFile();
        onDone (ok, message, output);
    });
}

juce::File Session::exportAudio (const ExportRequest& request, juce::String& error)
{
    if (request.endSeconds - request.startSeconds < 0.05)
    {
        error = utf8 ("Rien \xc3\xa0 exporter dans cette plage.");
        return {};
    }

    const auto folder = project.getExportsFolder();
    folder.createDirectory();
    const auto name = juce::File::createLegalFileName (request.title.trim().isNotEmpty() ? request.title.trim() : project.name);
    const auto output = folder.getNonexistentChildFile (name, ".wav", false);

    const double sr = engine.getSampleRate();
    if (! writeMonoWav (output, renderMix (request.startSeconds, request.endSeconds, sr), sr))
    {
        error = utf8 ("\xc3\x89" "criture du fichier audio impossible.");
        return {};
    }
    return output;
}

std::vector<float> Session::copyLoop (double& sampleRate) const
{
    // Read while the audio thread may be overdubbing: a torn float at worst,
    // never an out-of-range access (the length only grows while recording).
    const auto& looper = engine.getLooper();
    sampleRate = looper.getSampleRate();
    const int length = looper.getRecordedLengthSamples();
    const auto& buffer = looper.getBufferForExport();
    return std::vector<float> (buffer.begin(), buffer.begin() + std::min (length, (int) buffer.size()));
}

bool Session::exportLoopTo (const juce::File& wavFile, juce::String& error)
{
    double sampleRate = 0.0;
    const auto loop = copyLoop (sampleRate);
    if (loop.empty())
    {
        error = utf8 ("La boucle est vide.");
        return false;
    }
    if (! writeMonoWav (wavFile, loop, sampleRate))
    {
        error = utf8 ("\xc3\x89" "criture du fichier impossible.");
        return false;
    }
    return true;
}

//==============================================================================
void Session::captureEngineStateIntoProject()
{
    project.bpm = bpm;
    project.beatsPerBar = beatsPerBar;
    project.metronomeOn = metronomeOn;
    project.gainDb = knobs[(size_t) Knob::gain];
    project.bassDb = knobs[(size_t) Knob::bass];
    project.midDb = knobs[(size_t) Knob::mid];
    project.trebleDb = knobs[(size_t) Knob::treble];
    project.presenceDb = knobs[(size_t) Knob::presence];
    project.volumeDb = knobs[(size_t) Knob::volume];
    project.captureFile = engine.getAmp().isLoaded() ? engine.getAmp().getModelFile() : juce::File();
    project.irFile = engine.getAmp().isIrLoaded() ? engine.getAmp().getIrFile() : juce::File();
    if (camera.isOpen())
        project.cameraDevice = camera.getDeviceName();
    project.loopLayers = engine.getLooper().getLayerCount();
}

void Session::applyProjectToEngine()
{
    setBpm (project.bpm);
    setBeatsPerBar (project.beatsPerBar);
    setMetronomeOn (project.metronomeOn);
    setKnob (Knob::gain, project.gainDb);
    setKnob (Knob::bass, project.bassDb);
    setKnob (Knob::mid, project.midDb);
    setKnob (Knob::treble, project.trebleDb);
    setKnob (Knob::presence, project.presenceDb);
    setKnob (Knob::volume, project.volumeDb);

    if (project.captureFile.existsAsFile() && project.captureFile != engine.getAmp().getModelFile())
        loadCapture (project.captureFile);

    if (project.irFile.existsAsFile())
        loadImpulseResponse (project.irFile);
    else
        clearImpulseResponse();

    setTrackVolume (Track::amp, project.guitarVolumeDb);
    setTrackVolume (Track::loop, project.loopVolumeDb);
    undoStack.clear();
    redoStack.clear();
    selection = Selection::none;
    selectedClip = -1;
    knownLoopRecordCount = engine.getLoopRecordCount();
    reloadAllTakeMedia();
    pushArrangementToEngine();
    pushCycleToEngine();
}

void Session::newProject()
{
    if (recordingTake)
        toggleTakeRecording();

    captureEngineStateIntoProject();
    if (hasProjectOnDisk())
        saveProject();

    // A new project keeps the current tone (capture, IR, knobs, tempo): the
    // player usually starts the next idea with the sound they already have.
    auto next = Project::createUntitled();
    next.bpm = project.bpm;
    next.beatsPerBar = project.beatsPerBar;
    next.metronomeOn = project.metronomeOn;
    next.captureFile = project.captureFile;
    next.irFile = project.irFile;
    next.gainDb = project.gainDb;
    next.bassDb = project.bassDb;
    next.midDb = project.midDb;
    next.trebleDb = project.trebleDb;
    next.presenceDb = project.presenceDb;
    next.volumeDb = project.volumeDb;
    project = std::move (next);

    engine.stopAll();
    engine.loadLoop ({}, 0);
    engine.returnToStart();
    applyProjectToEngine();
    sendChangeMessage();
}

bool Session::openProject (const juce::File& folderOrJson)
{
    Project loaded;
    std::vector<float> loop;
    double loopRate = 0.0;
    juce::String error;

    if (! Project::load (folderOrJson, loaded, loop, loopRate, error))
    {
        notify (error);
        return false;
    }

    if (recordingTake)
        toggleTakeRecording();

    if (hasProjectOnDisk() && loaded.folder != project.folder)
    {
        captureEngineStateIntoProject();
        saveProject();
    }

    project = std::move (loaded);
    engine.stopAll();
    engine.loadLoop (resample (loop, loopRate, engine.getSampleRate()), project.loopLayers);
    engine.returnToStart();
    applyProjectToEngine();

    getSettings().setValue ("lastProject", project.folder.getFullPathName());
    sendChangeMessage();
    return true;
}

bool Session::saveProject()
{
    captureEngineStateIntoProject();

    double loopRate = 0.0;
    const auto loop = copyLoop (loopRate);

    juce::String error;
    if (! project.save (loop, loopRate, error))
    {
        notify (error);
        return false;
    }

    getSettings().setValue ("lastProject", project.folder.getFullPathName());
    sendChangeMessage();
    return true;
}

bool Session::saveProjectAs (const juce::File& destination)
{
    auto folder = destination.hasFileExtension (Project::extension) ? destination
                                                                     : destination.withFileExtension (Project::extension);
    if (folder.exists() && folder != project.folder)
        folder = folder.getParentDirectory().getNonexistentChildFile (folder.getFileNameWithoutExtension(), Project::extension, false);

    juce::String error;
    if (! project.moveTo (folder, error))
    {
        notify (error);
        return false;
    }

    if (saveProject())
    {
        notify (utf8 ("Projet enregistr\xc3\xa9 : ") + project.name);
        return true;
    }
    return false;
}
