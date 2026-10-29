#include "Session.h"

#include "SongAnalysis.h"
#include "TimeStretch.h"
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

bool writeWav (const juce::File& file, std::initializer_list<const std::vector<float>*> channels, double sampleRate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels ((int) channels.size())
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return false;

    std::vector<const float*> data;
    for (const auto* channel : channels)
        data.push_back (channel->data());
    return writer->writeFromFloatArrays (data.data(), (int) data.size(), (int) (*channels.begin())->size());
}

bool writeMonoWav (const juce::File& file, const std::vector<float>& samples, double sampleRate)
{
    return writeWav (file, { &samples }, sampleRate);
}

bool writeStereoWav (const juce::File& file, const Session::StereoMix& mix, double sampleRate)
{
    return writeWav (file, { &mix.left, &mix.right }, sampleRate);
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

// Decodes any format the system reads (MP3, M4A/AAC, WAV, AIFF, FLAC, CAF)
// to stereo; a mono file goes to both sides.
bool readStereo (const juce::File& file, std::vector<float>& left, std::vector<float>& right, double& sampleRate, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        error = juce::String::fromUTF8 ("Impossible de lire ") + file.getFileName()
                + juce::String::fromUTF8 (". Les titres d'Apple Music ou de Spotify sont prot\xc3\xa9g\xc3\xa9s : il faut un MP3, M4A, WAV, AIFF ou FLAC.");
        return false;
    }
    if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 20.0 * 60.0))
    {
        error = juce::String::fromUTF8 ("Morceau trop long (20 minutes au plus).");
        return false;
    }

    const int length = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buffer (2, length);
    reader->read (&buffer, 0, length, 0, true, true);
    if (reader->numChannels < 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, length);
    left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + length);
    right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + length);
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
    setLatencyAdjustmentMs (s.getDoubleValue ("latencyAdjustmentMs", 0.0));
    armedGuitar = s.getBoolValue ("armGuitar", true);
    countInOn = s.getBoolValue ("countIn", true);
    loopCountIn = s.getBoolValue ("loopCountIn", true);
    loopBars = s.getIntValue ("loopBars", 0);
    magnetOn = s.getBoolValue ("magnet", true);
    armedVideo = s.getBoolValue ("armVideo", true);
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
    s.setValue ("latencyAdjustmentMs", latencyAdjustmentMs);
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
        setLatencyAdjustmentMs (latencyAdjustmentMs);
        reloadAllTakeMedia();
        pushArrangementToEngine();
        pushCycleToEngine();
        loadSongMedia();
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
            calibrationState = autoCalibration ? CalibrationState::idle : CalibrationState::failed;
            calibrationMessage = utf8 ("Le micro de la cam\xc3\xa9ra n'a rien envoy\xc3\xa9 \xe2\x80\x94 v\xc3\xa9rifie l'acc\xc3\xa8s au micro.");
            autoCalibration = false;
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
                if (project.captureFile != namFile)
                    markModified();
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
    markModified();
    sendChangeMessage();
}

void Session::clearImpulseResponse()
{
    markModified();
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
        case Knob::gate:   return { -90.0f, -20.0f };
        case Knob::bass:
        case Knob::mid:
        case Knob::treble:
        case Knob::presence:
        default:           return { -ToneStack::maxGainDb, ToneStack::maxGainDb };
    }
}

void Session::setKnob (Knob knob, float db)
{
    markModified();
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
        case Knob::gate:     amp.getGate().setThresholdDb (db); project.gateThresholdDb = db; break;
    }
}

float Session::getKnob (Knob knob) const
{
    return knobs[(size_t) knob];
}

void Session::setGateOn (bool shouldBeOn)
{
    markModified();
    project.gateOn = shouldBeOn;
    engine.getAmp().getGate().setEnabled (shouldBeOn);
    sendChangeMessage();
}

void Session::setBypassed (AmpEngine::Block block, bool bypassed)
{
    markModified();
    engine.getAmp().setBypassed (block, bypassed);
    switch (block)
    {
        case AmpEngine::Block::capture: project.bypassCapture = bypassed; break;
        case AmpEngine::Block::eq:      project.bypassEq = bypassed; break;
        case AmpEngine::Block::cab:     project.bypassCab = bypassed; break;
    }
    sendChangeMessage();
}

void Session::setMasterVolume (float db)
{
    markModified();
    project.masterVolumeDb = juce::jlimit (-60.0f, 6.0f, db);
    engine.setMasterGain (dbToGain (project.masterVolumeDb));
}

void Session::setArmed (Track track, bool armed)
{
    if (track == Track::amp)
        armedGuitar = armed;
    else if (track == Track::camera)
        armedVideo = armed;
    getSettings().setValue ("armGuitar", armedGuitar);
    getSettings().setValue ("armVideo", armedVideo);
    sendChangeMessage();
}

bool Session::isArmed (Track track) const
{
    return track == Track::amp ? armedGuitar : (track == Track::camera && armedVideo);
}

//==============================================================================
void Session::setBpm (double newBpm)
{
    markModified();
    bpm = juce::jlimit (40.0, 240.0, newBpm);
    engine.getMetronome().setBpm (bpm);
    sendChangeMessage();
}

double Session::getBpm() const { return bpm; }

void Session::setBeatsPerBar (int beats)
{
    markModified();
    beatsPerBar = juce::jlimit (2, 7, beats);
    engine.getMetronome().setBeatsPerBar (beatsPerBar);
    sendChangeMessage();
}

int Session::getBeatsPerBar() const { return beatsPerBar; }

void Session::setMetronomeOn (bool shouldBeOn)
{
    markModified();
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
void Session::recordLoop()
{
    project.looperTrack = true;
    markModified();

    // Pressed during the count-in: cancel it.
    if (engine.isLoopScheduled())
    {
        engine.cancelLoopSchedule();
        notify (utf8 ("D\xc3\xa9" "compte annul\xc3\xa9."));
        sendChangeMessage();
        return;
    }

    if (engine.getLooper().getState() == Looper::State::Empty && (loopCountIn || loopBars > 0))
    {
        const double bar = getSecondsPerBar(), beat = getSecondsPerBeat();
        const double now = getPlayheadSeconds();
        double start = now;
        if (loopCountIn)
        {
            // A bar of clicks: up to the next bar line, at least one beat away.
            start = std::ceil (now / bar - 1.0e-6) * bar;
            if (start - now < beat * 0.9)
                start += bar;
            engine.getMetronome().setCountInEnd (toSamples (start));
        }
        engine.scheduleLoopRecord (toSamples (start), loopBars > 0 ? toSamples (start + loopBars * bar) : AudioEngine::noLocate);
        engine.play();
        sendChangeMessage();
        return;
    }

    engine.recordLoop();
    sendChangeMessage();
}

void Session::setLoopCountInOn (bool shouldBeOn)
{
    loopCountIn = shouldBeOn;
    getSettings().setValue ("loopCountIn", loopCountIn);
    sendChangeMessage();
}

void Session::setLoopBars (int bars)
{
    loopBars = juce::jlimit (0, 32, bars);
    getSettings().setValue ("loopBars", loopBars);
    sendChangeMessage();
}

void Session::removeLooperTrack()
{
    engine.cancelLoopSchedule();
    engine.getLooper().requestClear();
    project.looperTrack = false;
    if (selection == Selection::loop)
        selection = Selection::none;
    markModified();
    sendChangeMessage();
}

void Session::setMagnetOn (bool shouldBeOn)
{
    magnetOn = shouldBeOn;
    getSettings().setValue ("magnet", magnetOn);
    sendChangeMessage();
}

bool Session::isHiddenInLibrary (const juce::String& kind, const juce::String& key) const
{
    return juce::StringArray::fromLines (const_cast<Session&> (*this).getSettings().getValue ("hidden." + kind)).contains (key);
}

void Session::setHiddenInLibrary (const juce::String& kind, const juce::String& key, bool shouldBeHidden)
{
    auto list = juce::StringArray::fromLines (getSettings().getValue ("hidden." + kind));
    list.removeEmptyStrings();
    if (shouldBeHidden)
        list.addIfNotAlreadyThere (key);
    else
        list.removeString (key);
    getSettings().setValue ("hidden." + kind, list.joinIntoString ("\n"));
    sendChangeMessage();
}

int Session::countHiddenInLibrary (const juce::String& kind) const
{
    if (kind == "takes")
        return (int) std::count_if (project.takes.begin(), project.takes.end(), [] (const Take& t) { return t.hidden; });
    auto list = juce::StringArray::fromLines (const_cast<Session&> (*this).getSettings().getValue ("hidden." + kind));
    list.removeEmptyStrings();
    return list.size();
}

void Session::setTakeHidden (int takeIndex, bool shouldBeHidden)
{
    if (! juce::isPositiveAndBelow (takeIndex, (int) project.takes.size()))
        return;
    project.takes[(size_t) takeIndex].hidden = shouldBeHidden;
    markModified();
    sendChangeMessage();
}
void Session::undo()            { engine.getLooper().requestUndo(); markModified(); sendChangeMessage(); }
void Session::clearLoop()       { engine.getLooper().requestClear(); markModified(); sendChangeMessage(); }

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
    markModified();
    project.cycleStart = std::max (0.0, std::min (start, end));
    project.cycleEnd = std::max (start, end);
    pushCycleToEngine();
    sendChangeMessage();
}

void Session::setCycleOn (bool shouldBeOn)
{
    markModified();
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
        if (! armedGuitar)
        {
            notify (utf8 ("Arme la piste Guitare (son bouton \xe2\x97\x8f) pour enregistrer une prise."));
            return;
        }
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

        // Count-in: back a bar before the playhead (before the timeline's
        // first bar when it sits at 0), clicking through it while already
        // recording. The take's region starts where the playhead was.
        countInTarget = -1.0;
        std::int64_t from = AudioEngine::noLocate;
        if (countInOn)
        {
            const double bar = getSecondsPerBar();
            countInTarget = getPlayheadSeconds();
            from = toSamples (countInTarget - bar);
            engine.getMetronome().setCountInEnd (toSamples (countInTarget));
        }

        engine.play();
        pendingTake.dryFile = project.getTakeFile (number, ".dry.wav");
        if (! engine.startTake (pendingTake.audioFile, from, pendingTake.dryFile))
        {
            notify (utf8 ("Impossible d'\xc3\xa9" "crire la prise sur le disque."));
            return;
        }

        if (camera.isOpen() && armedVideo)
        {
            pendingTake.videoFile = project.getTakeFile (number, ".mov");
            if (! camera.startRecording (pendingTake.videoFile))
                pendingTake.videoFile = juce::File();
        }

        recordingTake = true;
        takeStartedAt = nowSeconds();
        sendChangeMessage();

        // Not synchronised yet: learn it from this take, from the first
        // seconds of playing — no clap, no wizard.
        if (! calibrated && camera.isOpen() && ! pendingTake.videoFile.getFullPathName().isEmpty())
            startAutoCalibration();
        return;
    }

    if (autoCalibration && calibrationState == CalibrationState::listening)
    {
        // Stopped before enough was heard: forget it, the next take tries again.
        camera.stopMicCapture();
        engine.startCalibrationCapture (0.0);
        calibrationState = CalibrationState::idle;
        autoCalibration = false;
    }

    recordingTake = false;
    finishingTake = true;
    pendingTakeStart = getTakeStartSeconds();
    engine.getMetronome().setCountInEnd (-1);

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

    // The take lands where it was played. One started at 0 begins a round
    // trip before the timeline, and a count-in is in the file but not in
    // the region: both are trimmed off.
    Clip clip;
    clip.start = pendingTakeStart;
    clip.length = pendingTake.durationSeconds;
    const double regionStart = std::max (0.0, countInTarget);
    const bool stoppedInCountIn = countInTarget >= 0.0 && clip.start + clip.length <= regionStart + 0.2;
    countInTarget = -1.0;
    if (clip.start < regionStart)
    {
        clip.sourceStart = regionStart - clip.start;
        clip.length -= clip.sourceStart;
        clip.start = regionStart;
    }

    if (clip.length < 0.2)
    {
        pendingTake.audioFile.deleteFile();
        pendingTake.videoFile.deleteFile();
        pendingTake = {};
        notify (stoppedInCountIn ? utf8 ("Prise arr\xc3\xaat\xc3\xa9" "e pendant le d\xc3\xa9" "compte, ignor\xc3\xa9" "e.")
                                 : utf8 ("Prise trop courte, ignor\xc3\xa9" "e."));
        return;
    }

    const bool withVideo = pendingTake.hasVideo();
    project.takes.push_back (std::move (pendingTake));
    pendingTake = {};

    const int takeIndex = (int) project.takes.size() - 1;
    loadTakeMedia (takeIndex);
    clip.take = takeIndex;

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

bool Session::canReampTake (int takeIndex) const
{
    return ! reamping && ! recordingTake && juce::isPositiveAndBelow (takeIndex, (int) project.takes.size())
           && project.takes[(size_t) takeIndex].dryFile.existsAsFile() && engine.getAmp().isLoaded();
}

void Session::reampTake (int takeIndex)
{
    if (! canReampTake (takeIndex))
    {
        notify (utf8 ("Cette prise n'a pas de son sec : seules les prises enregistr\xc3\xa9" "es depuis cette version peuvent \xc3\xaatre r\xc3\xa9-amplifi\xc3\xa9" "es."));
        return;
    }

    reamping = true;
    notify (utf8 ("R\xc3\xa9-amplification de ") + project.takes[(size_t) takeIndex].name + utf8 ("\xe2\x80\xa6"));
    sendChangeMessage();

    // The amp exactly as it is now, rebuilt off to the side.
    struct Settings
    {
        juce::File capture, ir;
        std::array<float, 7> knobsCopy {};
        bool gateOn = false, bypassCapture = false, bypassEq = false, bypassCab = false;
    } settings;
    auto& liveAmp = engine.getAmp();
    settings.capture = liveAmp.getModelFile();
    settings.ir = liveAmp.isIrLoaded() ? liveAmp.getIrFile() : juce::File();
    for (size_t i = 0; i < settings.knobsCopy.size() && i < knobs.size(); ++i)
        settings.knobsCopy[i] = knobs[i];
    settings.gateOn = liveAmp.getGate().isEnabled();
    settings.bypassCapture = liveAmp.isBypassed (AmpEngine::Block::capture);
    settings.bypassEq = liveAmp.isBypassed (AmpEngine::Block::eq);
    settings.bypassCab = liveAmp.isBypassed (AmpEngine::Block::cab);

    const auto dryFile = project.takes[(size_t) takeIndex].dryFile;
    const auto target = project.takes[(size_t) takeIndex].audioFile;
    const double sr = engine.getSampleRate();

    std::weak_ptr<bool> weak = alive;
    loaderPool.addJob ([this, weak, takeIndex, settings, dryFile, target, sr]
    {
        juce::String error;
        std::vector<float> samples;
        double rate = 0.0;
        bool ok = readMonoWav (dryFile, samples, rate);
        if (! ok)
            error = utf8 ("Son sec illisible.");

        if (ok)
        {
            AmpEngine amp;
            constexpr int block = 512;
            amp.prepare (rate, block);
            ok = amp.loadModel (settings.capture, error);
            if (ok)
            {
                amp.setInputGainDb (settings.knobsCopy[(size_t) Knob::gain]);
                amp.setOutputVolumeDb (settings.knobsCopy[(size_t) Knob::volume]);
                amp.getToneStack().setGainDb (ToneStack::bass, settings.knobsCopy[(size_t) Knob::bass]);
                amp.getToneStack().setGainDb (ToneStack::mid, settings.knobsCopy[(size_t) Knob::mid]);
                amp.getToneStack().setGainDb (ToneStack::treble, settings.knobsCopy[(size_t) Knob::treble]);
                amp.getToneStack().setGainDb (ToneStack::presence, settings.knobsCopy[(size_t) Knob::presence]);
                amp.getGate().setEnabled (settings.gateOn);
                amp.getGate().setThresholdDb (settings.knobsCopy[(size_t) Knob::gate]);
                amp.setBypassed (AmpEngine::Block::capture, settings.bypassCapture);
                amp.setBypassed (AmpEngine::Block::eq, settings.bypassEq);
                amp.setBypassed (AmpEngine::Block::cab, settings.bypassCab);
                if (settings.ir != juce::File())
                {
                    amp.loadImpulseResponse (settings.ir);
                    // The convolution swaps its response in from process(): feed it silence until it has.
                    std::vector<float> silence (block, 0.0f);
                    for (int i = 0; i < 500 && ! amp.isIrReady(); ++i)
                    {
                        amp.process (silence.data(), block);
                        juce::Thread::sleep (10);
                    }
                }

                // A few blocks of silence first: the capture picks up and settles.
                std::vector<float> warm (block, 0.0f);
                for (int i = 0; i < 4; ++i)
                    amp.process (warm.data(), block);

                for (size_t i = 0; i < samples.size(); i += block)
                    amp.process (samples.data() + i, (int) std::min<size_t> (block, samples.size() - i));

                // Written beside, then swapped in: a failed write never costs the take.
                const auto temp = target.getSiblingFile (target.getFileNameWithoutExtension() + ".reamp.wav");
                temp.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = temp.createOutputStream();
                if (stream != nullptr)
                    if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24)))
                    {
                        const float* channel[1] = { samples.data() };
                        writer->writeFromFloatArrays (channel, 1, (int) samples.size());
                        writer.reset();
                        ok = temp.moveFileTo (target);
                    }
                    else
                        ok = false;
                else
                    ok = false;
                if (! ok && error.isEmpty())
                    error = utf8 ("\xc3\x89" "criture impossible.");
            }
        }

        juce::MessageManager::callAsync ([this, weak, takeIndex, ok, error]
        {
            if (weak.expired())
                return;
            reamping = false;
            if (ok)
            {
                loadTakeMedia (takeIndex);
                pushArrangementToEngine();
                markModified();
                notify (project.takes[(size_t) takeIndex].name + utf8 (" r\xc3\xa9-amplifi\xc3\xa9" "e avec ") + engine.getAmp().getModelName() + ".");
            }
            else
            {
                notify (utf8 ("R\xc3\xa9-amplification impossible : ") + error);
            }
            sendChangeMessage();
        });
    });
}

void Session::setCountInOn (bool shouldBeOn)
{
    countInOn = shouldBeOn;
    getSettings().setValue ("countIn", countInOn);
    sendChangeMessage();
}

int Session::getCountInBeatsLeft() const
{
    if (engine.isLoopScheduled() && engine.getLoopStartAt() != AudioEngine::noLocate)
    {
        const double left = (double) engine.getLoopStartAt() / engine.getSampleRate() - getPlayheadSeconds();
        return left > 0.0 ? (int) std::ceil (left / getSecondsPerBeat() - 1.0e-6) : 0;
    }
    if (! recordingTake || countInTarget < 0.0)
        return 0;
    const double left = countInTarget - getPlayheadSeconds();
    return left > 0.0 ? (int) std::ceil (left / getSecondsPerBeat() - 1.0e-6) : 0;
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
    // A soloed track silences the others that aren't soloed.
    const bool anySolo = soloed[(size_t) Track::amp] || soloed[(size_t) Track::loop] || soloed[(size_t) Track::song];
    auto silent = [this, anySolo] (Track t) { return muted[(size_t) t] || (anySolo && ! soloed[(size_t) t]); };
    engine.setGuitarMuted (silent (Track::amp));
    engine.setLoopMuted (silent (Track::loop));
    engine.setSongMuted (silent (Track::song));
    sendChangeMessage();
}

void Session::setTrackVolume (Track track, float db)
{
    markModified();
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
    else if (track == Track::song)
    {
        project.songVolumeDb = db;
        engine.setSongGain (dbToGain (db));
    }
}

float Session::getTrackVolume (Track track) const
{
    switch (track)
    {
        case Track::amp:  return project.guitarVolumeDb;
        case Track::loop: return project.loopVolumeDb;
        case Track::song: return project.songVolumeDb;
        case Track::camera:
        default:          return 0.0f;
    }
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
    markModified();
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

bool Session::isLooperTrackVisible() const
{
    return project.looperTrack || engine.getLooper().getState() != Looper::State::Empty;
}

void Session::setLooperTrackVisible (bool shouldBeVisible)
{
    if (! shouldBeVisible && engine.getLooper().getState() != Looper::State::Empty)
    {
        notify (utf8 ("Efface la boucle avant de retirer la piste Looper."));
        return;
    }
    if (project.looperTrack == shouldBeVisible)
        return;
    project.looperTrack = shouldBeVisible;
    if (! shouldBeVisible && selection == Selection::loop)
        selection = Selection::none;
    markModified();
    sendChangeMessage();
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
    const double end = project.arrangement.getContentEnd (hasLoop());
    return hasSong() ? std::max (end, project.arrangement.songStart + getSongLength()) : end;
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
    engine.setSongStart (toSamples (a.songStart));
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
    cameraSuspended = false;
    getSettings().setValue ("cameraEnabled", false);
    sendChangeMessage();
}

void Session::suspendCamera()
{
    if (! camera.isOpen() || recordingTake || finishingTake)
        return;
    suspendedCameraDevice = camera.getDeviceName();
    cameraSuspended = true;
    camera.close(); // the light goes off; "cameraEnabled" stays as it was
    sendChangeMessage();
}

void Session::resumeCamera()
{
    if (! cameraSuspended)
        return;
    cameraSuspended = false;
    if (! camera.isOpen() && ! cameraOpening)
        openCamera (suspendedCameraDevice);
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

void Session::startAutoCalibration()
{
    if (calibrationState == CalibrationState::starting || calibrationState == CalibrationState::listening)
        return;
    autoCalibration = true;
    calibrationState = CalibrationState::starting;
    std::weak_ptr<bool> weak = alive;
    camera.startMicCapture (8.0, [this, weak] (bool ok, juce::String)
    {
        if (weak.expired())
            return;
        if (! ok || ! recordingTake)
        {
            if (ok)
                camera.stopMicCapture();
            autoCalibration = false;
            calibrationState = CalibrationState::idle;
            return;
        }
        engine.startCalibrationCapture (8.0);
        calibrationState = CalibrationState::listening;
        calibrationStartedAt = nowSeconds();
    });
}

void Session::finishCalibration()
{
    double interfaceStart = 0.0, cameraStart = 0.0, cameraRate = 0.0;
    const auto interfaceAudio = engine.getCalibrationCapture (interfaceStart);
    const auto cameraAudio = camera.getMicCapture (cameraStart, cameraRate);
    camera.stopMicCapture();

    const roadie::sync::Capture interfaceCapture { interfaceAudio.data(), (int) interfaceAudio.size(), engine.getSampleRate(), interfaceStart };
    const roadie::sync::Capture cameraCapture { cameraAudio.data(), (int) cameraAudio.size(), cameraRate, cameraStart };
    const bool automatic = autoCalibration;
    autoCalibration = false;
    const auto result = automatic ? roadie::sync::measureLagFromPlaying (interfaceCapture, cameraCapture)
                                  : roadie::sync::measureLag (interfaceCapture, cameraCapture);

    if (automatic && ! result.ok)
    {
        // Nothing clear enough in ordinary playing: say nothing, keep the manual button.
        calibrationState = CalibrationState::idle;
        sendChangeMessage();
        return;
    }

    if (result.ok)
    {
        syncLagSeconds = result.interfaceLagSeconds;
        calibrated = true;
        calibrationState = automatic ? CalibrationState::idle : CalibrationState::done;
        if (automatic)
            notify (utf8 ("Cam\xc3\xa9ra synchronis\xc3\xa9" "e automatiquement (") + juce::String (std::abs (syncLagSeconds * 1000.0), 0) + " ms).");
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
//==============================================================================
bool Session::isSongFile (const juce::File& file)
{
    if (! file.hasFileExtension (".mp3;.m4a;.aac;.wav;.aif;.aiff;.flac;.caf;.mp4"))
        return false;
    // A WAV or AIFF can also be a cab IR: those last a second or less.
    if (file.hasFileExtension (".wav;.aif;.aiff"))
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        return reader != nullptr && reader->sampleRate > 0.0 && reader->lengthInSamples > (juce::int64) (reader->sampleRate * 4.0);
    }
    return true;
}

bool Session::isYouTubeUrl (const juce::String& text)
{
    const auto url = juce::URL (text.trim());
    const auto host = url.getDomain().toLowerCase();
    return (url.toString (true).startsWithIgnoreCase ("http://") || url.toString (true).startsWithIgnoreCase ("https://"))
           && (host == "youtu.be" || host == "youtube.com" || host.endsWith (".youtube.com"));
}

void Session::importSongFromUrl (const juce::String& rawUrl)
{
    const auto url = rawUrl.trim();
    if (importingSong || downloadingSong)
        return;
    if (! isYouTubeUrl (url))
    {
        notify (utf8 ("Ce n'est pas un lien YouTube."));
        return;
    }

    juce::File tool;
    for (const char* candidate : { "/opt/homebrew/bin/yt-dlp", "/usr/local/bin/yt-dlp", "/usr/bin/yt-dlp" })
        if (juce::File (candidate).existsAsFile())
        {
            tool = juce::File (candidate);
            break;
        }
    if (tool == juce::File())
        if (const auto local = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/bin/yt-dlp"); local.existsAsFile())
            tool = local;
    if (tool == juce::File())
    {
        notify (utf8 ("Il manque yt-dlp : installe-le avec \xc2\xab brew install yt-dlp \xc2\xbb, puis r\xc3\xa9" "essaie."));
        return;
    }

    downloadingSong = true;
    notify (utf8 ("T\xc3\xa9l\xc3\xa9" "chargement de l'audio YouTube\xe2\x80\xa6"));
    sendChangeMessage();

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("roadie-youtube-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    std::weak_ptr<bool> weak = alive;
    loaderPool.addJob ([this, weak, tool, url, folder]
    {
        juce::File result;
        juce::String error;
        if (folder.createDirectory())
        {
            // The URL is one argument after "--": never read as an option, never through a shell.
            juce::StringArray args { tool.getFullPathName(), "-f", "bestaudio[ext=m4a]/bestaudio", "--no-playlist", "--no-progress",
                                     "--no-warnings", "-o", folder.getChildFile ("%(title).80s.%(ext)s").getFullPathName(),
                                     "--print", "after_move:filepath", "--", url };
            juce::ChildProcess process;
            if (process.start (args))
            {
                const auto output = process.readAllProcessOutput();
                if (process.waitForProcessToFinish (10 * 60 * 1000) && process.getExitCode() == 0)
                {
                    const auto lines = juce::StringArray::fromLines (output.trim());
                    if (lines.size() > 0)
                        result = juce::File (lines[lines.size() - 1].trim());
                }
                if (! result.existsAsFile())
                    error = utf8 ("Le t\xc3\xa9l\xc3\xa9" "chargement a \xc3\xa9" "chou\xc3\xa9 (lien priv\xc3\xa9, vid\xc3\xa9o indisponible, ou yt-dlp \xc3\xa0 mettre \xc3\xa0 jour).");
                else if (! result.hasFileExtension (".m4a;.mp4;.mp3;.wav;.aac"))
                    error = utf8 ("Format audio non lisible (") + result.getFileExtension() + utf8 (") \xe2\x80\x94 installe ffmpeg : brew install ffmpeg.");
            }
            else
            {
                error = utf8 ("Impossible de lancer yt-dlp.");
            }
        }

        juce::MessageManager::callAsync ([this, weak, result, error, folder]
        {
            if (weak.expired())
            {
                folder.deleteRecursively();
                return;
            }
            downloadingSong = false;
            if (error.isEmpty() && result.existsAsFile())
            {
                importSong (result);
                // importSong copies into the project asynchronously; give it time to read the file.
                juce::Timer::callAfterDelay (120000, [folder] { folder.deleteRecursively(); });
            }
            else
            {
                notify (error.isNotEmpty() ? error : utf8 ("Le t\xc3\xa9l\xc3\xa9" "chargement a \xc3\xa9" "chou\xc3\xa9."));
                folder.deleteRecursively();
            }
            sendChangeMessage();
        });
    });
}

void Session::importSong (const juce::File& audioFile)
{
    if (importingSong)
        return;
    if (! audioFile.existsAsFile())
    {
        notify (utf8 ("Fichier introuvable : ") + audioFile.getFileName());
        return;
    }

    importingSong = true;
    notify (utf8 ("Analyse de \xc2\xab ") + audioFile.getFileNameWithoutExtension() + utf8 (" \xc2\xbb\xe2\x80\xa6"));

    // The copy lives in the project, so the project stays complete.
    const auto songFolder = project.getSongFolder();
    const auto destination = audioFile.isAChildOf (songFolder) ? audioFile : songFolder.getChildFile (audioFile.getFileName());
    const double engineRate = engine.getSampleRate();
    const int beatsPerBarNow = beatsPerBar;
    const int generation = ++songLoadGeneration;

    std::weak_ptr<bool> weak = alive;
    loaderPool.addJob ([this, weak, audioFile, songFolder, destination, engineRate, beatsPerBarNow, generation]
    {
        juce::String error;
        std::vector<float> left, right;
        double fileRate = 0.0;
        const bool ok = readStereo (audioFile, left, right, fileRate, error);

        roadie::song::Analysis analysis;
        auto media = std::make_shared<SongMedia>();
        if (ok)
        {
            std::vector<float> mono (left.size());
            for (size_t i = 0; i < mono.size(); ++i)
                mono[i] = 0.5f * (left[i] + right[i]);
            analysis = roadie::song::analyse (mono.data(), (int) mono.size(), fileRate, beatsPerBarNow);

            const int perPeak = std::max (1, (int) std::round (fileRate / SongMedia::peaksPerSecond));
            for (size_t i = 0; i < mono.size(); i += (size_t) perPeak)
            {
                float peak = 0.0f;
                for (size_t j = i; j < std::min (mono.size(), i + (size_t) perPeak); ++j)
                    peak = std::max (peak, std::abs (mono[j]));
                media->peaks.push_back (peak);
            }

            auto audio = std::make_shared<AudioEngine::SongAudio>();
            audio->left = resample (left, fileRate, engineRate);
            audio->right = resample (right, fileRate, engineRate);
            media->audio = audio;
            media->original = audio;
            media->sampleRate = engineRate;

            if (destination != audioFile)
            {
                songFolder.createDirectory();
                // One song per project: the previous copy goes.
                for (const auto& old : songFolder.findChildFiles (juce::File::findFiles, false))
                    old.deleteFile();
                if (! audioFile.copyFileTo (destination))
                    error = utf8 ("Copie du morceau dans le projet impossible.");
            }
        }
        const double duration = ok ? (double) left.size() / fileRate : 0.0;

        juce::MessageManager::callAsync ([this, weak, ok, error, analysis, media, destination, duration, generation, audioFile]
        {
            if (weak.expired() || generation != songLoadGeneration)
                return;
            importingSong = false;
            if (! ok || error.isNotEmpty())
            {
                notify (error);
                sendChangeMessage();
                return;
            }

            // Remembered for the library's Morceaux tab (the original, not the copy).
            if (! audioFile.isAChildOf (project.folder))
            {
                auto recent = juce::StringArray::fromLines (getSettings().getValue ("recentSongs"));
                recent.removeString (audioFile.getFullPathName());
                recent.insert (0, audioFile.getFullPathName());
                recent.removeEmptyStrings();
                while (recent.size() > 12)
                    recent.remove (recent.size() - 1);
                getSettings().setValue ("recentSongs", recent.joinIntoString ("\n"));
            }

            Song song;
            song.file = destination;
            song.title = destination.getFileNameWithoutExtension();
            song.duration = duration;
            song.detectedBpm = analysis.ok ? analysis.bpm : bpm;
            song.bpm = song.detectedBpm;
            song.firstDownbeat = analysis.ok ? analysis.firstDownbeatSeconds : 0.0;
            for (const auto& section : analysis.sections)
                song.sections.push_back ({ section.startSeconds, section.endSeconds, juce::String (section.label) });
            project.song = song;
            songMedia = *media;
            pushSongToEngine();

            // The song is the click now.
            setBpm (song.bpm);
            setMetronomeOn (false);
            placeSongOnBar2();
            saveProject();
            notify (analysis.ok ? juce::String (song.bpm, 1).replaceCharacter ('.', ',') + utf8 (" BPM d\xc3\xa9tect\xc3\xa9 \xe2\x80\x94 1re mesure cal\xc3\xa9" "e sur la mesure 2")
                                : utf8 ("Tempo non d\xc3\xa9tect\xc3\xa9 : tape-le avec Taper"));
            sendChangeMessage();
        });
    });
    sendChangeMessage();
}

void Session::loadSongMedia()
{
    const int generation = ++songLoadGeneration;
    if (! project.song.isSet())
    {
        songMedia = {};
        pushSongToEngine();
        return;
    }

    const auto file = project.song.file;
    const double engineRate = engine.getSampleRate();
    std::weak_ptr<bool> weak = alive;
    loaderPool.addJob ([this, weak, file, engineRate, generation]
    {
        juce::String error;
        std::vector<float> left, right;
        double fileRate = 0.0;
        auto media = std::make_shared<SongMedia>();
        const bool ok = readStereo (file, left, right, fileRate, error);
        if (ok)
        {
            const int perPeak = std::max (1, (int) std::round (fileRate / SongMedia::peaksPerSecond));
            for (size_t i = 0; i < left.size(); i += (size_t) perPeak)
            {
                float peak = 0.0f;
                for (size_t j = i; j < std::min (left.size(), i + (size_t) perPeak); ++j)
                    peak = std::max ({ peak, std::abs (left[j]), std::abs (right[j]) });
                media->peaks.push_back (peak);
            }
            auto audio = std::make_shared<AudioEngine::SongAudio>();
            audio->left = resample (left, fileRate, engineRate);
            audio->right = resample (right, fileRate, engineRate);
            media->audio = audio;
            media->original = audio;
            media->sampleRate = engineRate;
        }

        juce::MessageManager::callAsync ([this, weak, ok, error, media, generation]
        {
            if (weak.expired() || generation != songLoadGeneration)
                return;
            if (! ok)
                notify (error);
            songMedia = ok ? *media : SongMedia {};
            pushSongToEngine();
            if (ok && (std::abs (project.song.speed - 1.0) > 1.0e-6 || project.song.semitones != 0))
                renderSongVariant();
            sendChangeMessage();
        });
    });
}

juce::Range<double> Session::getSongSection (int index) const
{
    if (! juce::isPositiveAndBelow (index, (int) project.song.sections.size()))
        return {};
    const auto& section = project.song.sections[(size_t) index];
    return { section.start / project.song.speed, section.end / project.song.speed };
}

void Session::renderSongVariant()
{
    if (songMedia.original == nullptr)
        return;

    renderingSong = true;
    const int generation = ++songRenderGeneration;
    const auto original = songMedia.original;
    const double rate = songMedia.sampleRate, speed = project.song.speed;
    const int semitones = project.song.semitones;
    std::weak_ptr<bool> weak = alive;
    loaderPool.addJob ([this, weak, original, rate, speed, semitones, generation]
    {
        auto audio = std::make_shared<AudioEngine::SongAudio>();
        roadie::stretch::process (original->left, original->right, rate, speed, semitones, audio->left, audio->right);

        // Peaks of what plays: the rendered audio is in timeline time already.
        std::vector<float> peaks;
        const int perPeak = std::max (1, (int) std::round (rate / SongMedia::peaksPerSecond));
        for (size_t i = 0; i < audio->left.size(); i += (size_t) perPeak)
        {
            float peak = 0.0f;
            for (size_t j = i; j < std::min (audio->left.size(), i + (size_t) perPeak); ++j)
                peak = std::max ({ peak, std::abs (audio->left[j]), std::abs (audio->right[j]) });
            peaks.push_back (peak);
        }

        juce::MessageManager::callAsync ([this, weak, audio, peaks, generation]
        {
            if (weak.expired() || generation != songRenderGeneration)
                return;
            renderingSong = false;
            songMedia.audio = audio;
            songMedia.peaks = peaks;
            pushSongToEngine();
            sendChangeMessage();
        });
    });
    sendChangeMessage();
}

void Session::setSongSpeed (double speed)
{
    if (! project.song.isSet())
        return;
    project.song.speed = juce::jlimit (0.5, 1.0, std::round (speed * 20.0) / 20.0);
    markModified();
    // The grid follows: the slower tempo, the first bar still on bar 2.
    setBpm (project.song.bpm * project.song.speed);
    placeSongOnBar2();
    renderSongVariant();
    notify (juce::String (juce::roundToInt (project.song.speed * 100.0)) + utf8 ("\xc2\xa0% \xc2\xb7 ")
            + juce::String (project.song.bpm * project.song.speed, 1).replaceCharacter ('.', ',') + " BPM");
}

void Session::setSongSemitones (int semitones)
{
    if (! project.song.isSet())
        return;
    project.song.semitones = juce::jlimit (-6, 6, semitones);
    markModified();
    renderSongVariant();
}

void Session::pushSongToEngine()
{
    engine.setSong (songMedia.audio);
    engine.setSongStart (toSamples (project.arrangement.songStart));
    engine.setSongGain (dbToGain (project.songVolumeDb));
}

void Session::removeSong()
{
    if (! project.song.isSet())
        return;
    ++songLoadGeneration;
    project.song.file.deleteFile();
    project.song = {};
    songMedia = {};
    pushSongToEngine();
    markModified();
    saveProject();
    notify (utf8 ("Morceau retir\xc3\xa9"));
}

void Session::placeSongOnBar2()
{
    if (! project.song.isSet())
        return;
    auto edited = project.arrangement;
    edited.songStart = getSecondsPerBar() - getSongDownbeat();
    applyEdit (edited, utf8 ("Caler le morceau"));
    pushSongToEngine();
}

void Session::setSongTempo (double newBpm)
{
    if (! project.song.isSet() || newBpm <= 0.0)
        return;
    newBpm = juce::jlimit (40.0, 240.0, newBpm);
    project.song.bpm = newBpm;
    setBpm (newBpm * project.song.speed);
    placeSongOnBar2();
    notify (juce::String (newBpm, 1).replaceCharacter ('.', ',') + " BPM");
}

void Session::tapSongTempo()
{
    const double now = nowSeconds();
    if (! taps.empty() && now - taps.back() > 2.0)
        taps.clear(); // a pause starts a new count
    taps.push_back (now);
    // Taps follow what plays: back to the song's own tempo.
    if (const double tapped = roadie::song::tempoFromTaps (taps); tapped > 0.0)
        setSongTempo (tapped / project.song.speed);
    sendChangeMessage();
}

void Session::nudgeSong (double seconds)
{
    if (! project.song.isSet())
        return;
    auto edited = project.arrangement;
    edited.songStart += seconds;
    applyEdit (edited, utf8 ("D\xc3\xa9" "caler le morceau"));
    pushSongToEngine();
}

void Session::cycleSection (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) project.song.sections.size()))
        return;
    const auto range = getSongSection (index);
    const double start = project.arrangement.songStart + range.getStart(), end = project.arrangement.songStart + range.getEnd();
    // Clicking the part already cycling turns the cycle off.
    if (project.cycleOn && std::abs (project.cycleStart - start) < 0.01 && std::abs (project.cycleEnd - end) < 0.01)
    {
        setCycleOn (false);
        return;
    }
    setCycleRange (std::max (0.0, start), end);
    setCycleOn (true);
}

void Session::setLatencyAdjustmentMs (double ms)
{
    latencyAdjustmentMs = juce::jlimit (-50.0, 100.0, ms);
    engine.setLatencyAdjustmentSamples ((int) std::lround (latencyAdjustmentMs * 0.001 * engine.getSampleRate()));
    sendChangeMessage();
}

Session::StereoMix Session::renderMix (double start, double end, double sampleRate, bool includeSong) const
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

    const bool anySolo = soloed[(size_t) Track::amp] || soloed[(size_t) Track::loop] || soloed[(size_t) Track::song];
    auto gainOf = [&] (Track t, float db) { return (muted[(size_t) t] || (anySolo && ! soloed[(size_t) t])) ? 0.0f : dbToGain (db); };
    const float g = gainOf (Track::amp, project.guitarVolumeDb);
    const float l = gainOf (Track::loop, project.loopVolumeDb);
    const float s = includeSong ? gainOf (Track::song, project.songVolumeDb) : 0.0f;

    StereoMix mix;
    mix.left.resize (total);
    mix.right.resize (total);
    for (size_t i = 0; i < total; ++i)
        mix.left[i] = mix.right[i] = guitar[i] * g + loop[i] * l;

    // The song, in stereo, from its place on the timeline.
    if (s > 0.0f && songMedia.audio != nullptr)
    {
        const auto& audio = *songMedia.audio;
        const bool sameRate = std::abs (songMedia.sampleRate - sampleRate) < 0.5;
        const auto left = sameRate ? audio.left : resample (audio.left, songMedia.sampleRate, sampleRate);
        const auto right = sameRate ? audio.right : resample (audio.right, songMedia.sampleRate, sampleRate);
        const auto offset = at (project.arrangement.songStart);
        for (size_t i = 0; i < total; ++i)
        {
            const auto source = (std::int64_t) i - offset;
            if (source >= 0 && source < (std::int64_t) left.size())
            {
                mix.left[i] += left[(size_t) source] * s;
                mix.right[i] += right[(size_t) source] * s;
            }
        }
    }

    const float master = dbToGain (project.masterVolumeDb);
    for (size_t i = 0; i < total; ++i)
    {
        mix.left[i] = juce::jlimit (-1.0f, 1.0f, mix.left[i] * master);
        mix.right[i] = juce::jlimit (-1.0f, 1.0f, mix.right[i] * master);
    }
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
    if (! writeStereoWav (soundtrack, renderMix (request.startSeconds, request.endSeconds, sr, request.includeSong), sr))
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
    if (! writeStereoWav (output, renderMix (request.startSeconds, request.endSeconds, sr, request.includeSong), sr))
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
    project.gateThresholdDb = knobs[(size_t) Knob::gate];
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
    setKnob (Knob::gate, project.gateThresholdDb);
    setGateOn (project.gateOn);
    engine.getAmp().setBypassed (AmpEngine::Block::capture, project.bypassCapture);
    engine.getAmp().setBypassed (AmpEngine::Block::eq, project.bypassEq);
    engine.getAmp().setBypassed (AmpEngine::Block::cab, project.bypassCab);
    setMasterVolume (project.masterVolumeDb);

    if (project.captureFile.existsAsFile() && project.captureFile != engine.getAmp().getModelFile())
        loadCapture (project.captureFile);

    if (project.irFile.existsAsFile())
        loadImpulseResponse (project.irFile);
    else
        clearImpulseResponse();

    setTrackVolume (Track::amp, project.guitarVolumeDb);
    setTrackVolume (Track::loop, project.loopVolumeDb);
    setTrackVolume (Track::song, project.songVolumeDb);
    loadSongMedia();
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
    next.gateOn = project.gateOn;
    next.gateThresholdDb = project.gateThresholdDb;
    next.bypassCapture = project.bypassCapture;
    next.bypassEq = project.bypassEq;
    next.bypassCab = project.bypassCab;
    project = std::move (next);

    engine.stopAll();
    engine.loadLoop ({}, 0);
    engine.returnToStart();
    applyProjectToEngine();
    modified = false;
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
    modified = false;
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
    modified = false;
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
