#include "Project.h"
#include "I18n.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace
{
constexpr int formatVersion = 2;
constexpr int maxSavedThumbnails = 24;

juce::String relativePath (const juce::File& file, const juce::File& base)
{
    return file == juce::File() ? juce::String() : file.getRelativePathFrom (base);
}

juce::File resolve (const juce::var& value, const juce::File& base)
{
    const auto path = value.toString();
    if (path.isEmpty())
        return {};
    return juce::File::isAbsolutePath (path) ? juce::File (path) : base.getChildFile (path);
}

// The project's own media (takes, loop, song) always live inside its folder.
// A project.json that points elsewhere (a shared or hand-edited project) is
// not followed: deleting a take or the song would otherwise reach any file.
juce::File resolveInside (const juce::var& value, const juce::File& base)
{
    const auto file = resolve (value, base);
    return file == juce::File() || file.isAChildOf (base) ? file : juce::File();
}

// A number from project.json: finite and within `lo…hi`, else `fallback`.
double number (const juce::var& value, double fallback, double lo, double hi)
{
    if (value.isVoid() || value.isUndefined())
        return fallback;
    const double d = (double) value;
    return std::isfinite (d) ? juce::jlimit (lo, hi, d) : fallback;
}

constexpr double maxTime = 1.0e6;   // seconds on the timeline
constexpr double maxDb = 100.0;
constexpr int maxListSize = 20000;  // takes / regions: far beyond any real project
constexpr juce::int64 maxLoopSamples = 60 * 192000; // the Looper's own 60 s cap, at the highest common rate

bool writeWav (const juce::File& file, const float* samples, int numSamples, double sampleRate)
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

    const float* channels[1] = { samples };
    return writer->writeFromFloatArrays (channels, 1, numSamples);
}

bool readWav (const juce::File& file, std::vector<float>& samples, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return false;

    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > maxLoopSamples || reader->sampleRate <= 0.0)
        return false;

    juce::AudioBuffer<float> buffer (1, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, false);
    samples.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
    sampleRate = reader->sampleRate;
    return true;
}

juce::File thumbnailFolder (const Take& take)
{
    return take.audioFile.getSiblingFile (take.audioFile.getFileNameWithoutExtension());
}
} // namespace

double Arrangement::getContentEnd (bool hasLoop) const
{
    double end = 0.0;
    for (const auto& c : clips)
        end = std::max (end, c.end());
    if (hasLoop)
        for (const auto& c : loopClips)
            end = std::max (end, c.end());
    return end;
}

juce::File Project::getDefaultProjectsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Roadie").getChildFile ("Projects");
}

Project Project::createUntitled()
{
    Project p;
    const auto now = juce::Time::getCurrentTime();
    p.name = "Session " + now.formatted ("%Y-%m-%d %H-%M");
    p.folder = getDefaultProjectsFolder().getNonexistentChildFile (p.name, extension, false);
    p.name = p.folder.getFileNameWithoutExtension();
    return p;
}

int Project::getNextTakeNumber() const
{
    int highest = 0;
    for (const auto& t : takes)
        highest = std::max (highest, t.name.getTrailingIntValue());
    // Never reuse a number whose files are still on disk (e.g. a deleted take).
    int n = highest + 1;
    while (getTakeFile (n, ".wav").exists() || getTakeFile (n, ".mov").exists())
        ++n;
    return n;
}

juce::File Project::getTakeFile (int number, const juce::String& extensionWithDot) const
{
    return getTakesFolder().getChildFile ("take-" + juce::String (number) + extensionWithDot);
}

bool Project::save (const std::vector<float>& loop, double loopSampleRate, juce::String& error) const
{
    if (! folder.createDirectory())
    {
        error = tr ("Couldn't create the project folder.");
        return false;
    }

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "roadie-project");
    root->setProperty ("version", formatVersion);
    root->setProperty ("name", name);
    root->setProperty ("bpm", bpm);
    root->setProperty ("beatsPerBar", beatsPerBar);
    root->setProperty ("metronome", metronomeOn);

    auto amp = std::make_unique<juce::DynamicObject>();
    amp->setProperty ("capture", captureFile.getFullPathName());
    amp->setProperty ("ir", irFile.getFullPathName());
    amp->setProperty ("gainDb", gainDb);
    amp->setProperty ("bassDb", bassDb);
    amp->setProperty ("midDb", midDb);
    amp->setProperty ("trebleDb", trebleDb);
    amp->setProperty ("presenceDb", presenceDb);
    amp->setProperty ("volumeDb", volumeDb);
    amp->setProperty ("gateOn", gateOn);
    amp->setProperty ("gateThresholdDb", gateThresholdDb);
    amp->setProperty ("bypassCapture", bypassCapture);
    amp->setProperty ("bypassEq", bypassEq);
    amp->setProperty ("bypassCab", bypassCab);
    root->setProperty ("amp", amp.release());

    auto camera = std::make_unique<juce::DynamicObject>();
    camera->setProperty ("device", cameraDevice);
    root->setProperty ("camera", camera.release());

    auto loopObject = std::make_unique<juce::DynamicObject>();
    loopObject->setProperty ("track", looperTrack);
    if (! loop.empty())
    {
        if (! writeWav (getLoopFile(), loop.data(), (int) loop.size(), loopSampleRate))
        {
            error = tr ("Couldn't write the loop.");
            return false;
        }
        loopObject->setProperty ("file", relativePath (getLoopFile(), folder));
        loopObject->setProperty ("layers", loopLayers);
    }
    else
    {
        getLoopFile().deleteFile();
    }
    root->setProperty ("loop", loopObject.release());

    juce::Array<juce::var> takeList;
    for (const auto& t : takes)
    {
        auto take = std::make_unique<juce::DynamicObject>();
        take->setProperty ("name", t.name);
        take->setProperty ("audio", relativePath (t.audioFile, folder));
        take->setProperty ("video", t.videoFile.existsAsFile() ? relativePath (t.videoFile, folder) : juce::String());
        take->setProperty ("dry", t.dryFile.existsAsFile() ? relativePath (t.dryFile, folder) : juce::String());
        take->setProperty ("audioStartHost", t.audioStartHostSeconds);
        take->setProperty ("videoStartHost", t.videoStartHostSeconds);
        take->setProperty ("syncLagSeconds", t.syncLagSeconds);
        take->setProperty ("duration", t.durationSeconds);
        take->setProperty ("createdAt", t.createdAt.toISO8601 (true));
        takeList.add (take.release());

        // Thumbnails for the camera lane, written once.
        const auto thumbs = thumbnailFolder (t);
        if (! t.thumbnails.empty() && ! thumbs.isDirectory() && thumbs.createDirectory())
        {
            const int count = std::min ((int) t.thumbnails.size(), maxSavedThumbnails);
            for (int i = 0; i < count; ++i)
            {
                const auto& image = t.thumbnails[(size_t) ((size_t) i * t.thumbnails.size() / (size_t) count)];
                juce::JPEGImageFormat jpeg;
                jpeg.setQuality (0.7f);
                juce::FileOutputStream out (thumbs.getChildFile (juce::String (i).paddedLeft ('0', 2) + ".jpg"));
                if (out.openedOk())
                    jpeg.writeImageToStream (image.rescaled (160, juce::jmax (1, image.getHeight() * 160 / juce::jmax (1, image.getWidth()))), out);
            }
        }
    }
    root->setProperty ("takes", takeList);

    if (song.isSet())
    {
        auto songObject = std::make_unique<juce::DynamicObject>();
        songObject->setProperty ("file", relativePath (song.file, folder));
        songObject->setProperty ("title", song.title);
        songObject->setProperty ("bpm", song.bpm);
        songObject->setProperty ("detectedBpm", song.detectedBpm);
        songObject->setProperty ("firstDownbeat", song.firstDownbeat);
        songObject->setProperty ("duration", song.duration);
        songObject->setProperty ("volumeDb", songVolumeDb);
        songObject->setProperty ("speed", song.speed);
        songObject->setProperty ("semitones", song.semitones);
        root->setProperty ("song", songObject.release());
    }

    // Clips name their take: indices would shift if a take went missing.
    juce::Array<juce::var> clipList;
    for (const auto& c : arrangement.clips)
    {
        if (! juce::isPositiveAndBelow (c.take, (int) takes.size()))
            continue;
        auto clip = std::make_unique<juce::DynamicObject>();
        clip->setProperty ("take", takes[(size_t) c.take].name);
        clip->setProperty ("start", c.start);
        clip->setProperty ("sourceStart", c.sourceStart);
        clip->setProperty ("length", c.length);
        clipList.add (clip.release());
    }

    auto arrangementObject = std::make_unique<juce::DynamicObject>();
    arrangementObject->setProperty ("clips", clipList);
    auto pieces = [] (const std::vector<Clip>& list)
    {
        juce::Array<juce::var> out;
        for (const auto& c : list)
        {
            auto piece = std::make_unique<juce::DynamicObject>();
            piece->setProperty ("start", c.start);
            piece->setProperty ("sourceStart", c.sourceStart);
            piece->setProperty ("length", c.length);
            out.add (piece.release());
        }
        return out;
    };
    arrangementObject->setProperty ("loopClips", pieces (arrangement.loopClips));
    arrangementObject->setProperty ("songClips", pieces (arrangement.songClips));
    arrangementObject->setProperty ("songStart", arrangement.songStart);
    arrangementObject->setProperty ("cycleOn", cycleOn);
    arrangementObject->setProperty ("cycleStart", cycleStart);
    arrangementObject->setProperty ("cycleEnd", cycleEnd);
    arrangementObject->setProperty ("guitarVolumeDb", guitarVolumeDb);
    arrangementObject->setProperty ("loopVolumeDb", loopVolumeDb);
    arrangementObject->setProperty ("masterVolumeDb", masterVolumeDb);
    root->setProperty ("arrangement", arrangementObject.release());

    const auto json = juce::JSON::toString (juce::var (root.release()));
    // The last good file stays next to the new one: a write cut short (power, crash)
    // or a hand edit gone wrong never costs the whole project
    // (only a readable one: a damaged file must not replace the good backup).
    if (juce::var existing; getProjectFile().existsAsFile()
                            && juce::JSON::parse (getProjectFile().loadFileAsString(), existing).wasOk()
                            && existing["format"].toString() == "roadie-project")
        getProjectFile().copyFileTo (getBackupFile());

    if (! getProjectFile().replaceWithText (json))
    {
        error = tr ("Couldn't write project.json.");
        return false;
    }
    return true;
}

bool Project::load (const juce::File& folderOrJson, Project& result,
                    std::vector<float>& loop, double& loopSampleRate, juce::String& error)
{
    const auto folder = folderOrJson.isDirectory() ? folderOrJson : folderOrJson.getParentDirectory();
    const auto parseFile = [] (const juce::File& file)
    {
        juce::var parsed;
        if (file.existsAsFile() && juce::JSON::parse (file.loadFileAsString(), parsed).wasOk() && parsed.getDynamicObject() != nullptr)
            return parsed;
        return juce::var();
    };

    auto json = parseFile (folder.getChildFile ("project.json"));
    bool fromBackup = false;
    if (json["format"].toString() != "roadie-project")
    {
        const bool hadFile = folder.getChildFile ("project.json").existsAsFile();
        auto backup = parseFile (folder.getChildFile ("project.json.bak"));
        if (backup["format"].toString() == "roadie-project")
        {
            json = backup;
            fromBackup = true;
        }
        else
        {
            error = hadFile ? tr ("project.json is unreadable (damaged file) and no backup could replace it.")
                            : tr ("This folder isn't a Roadie project.");
            return false;
        }
    }

    Project p;
    p.folder = folder;
    p.recoveredFromBackup = fromBackup;
    p.name = folder.getFileNameWithoutExtension();
    p.bpm = number (json["bpm"], 100.0, 40.0, 240.0);
    p.beatsPerBar = juce::jlimit (2, 7, (int) number (json["beatsPerBar"], 4.0, 2.0, 7.0));
    p.metronomeOn = (bool) json.getProperty ("metronome", false);

    const auto amp = json["amp"];
    p.captureFile = resolve (amp["capture"], folder);
    p.irFile = resolve (amp["ir"], folder);
    p.gainDb = (float) number (amp["gainDb"], 0.0, -maxDb, maxDb);
    p.bassDb = (float) number (amp["bassDb"], 0.0, -maxDb, maxDb);
    p.midDb = (float) number (amp["midDb"], 0.0, -maxDb, maxDb);
    p.trebleDb = (float) number (amp["trebleDb"], 0.0, -maxDb, maxDb);
    p.presenceDb = (float) number (amp["presenceDb"], 0.0, -maxDb, maxDb);
    p.volumeDb = (float) number (amp["volumeDb"], 0.0, -maxDb, maxDb);
    p.gateOn = (bool) amp.getProperty ("gateOn", false);
    p.gateThresholdDb = (float) number (amp["gateThresholdDb"], -60.0, -maxDb, maxDb);
    p.bypassCapture = (bool) amp.getProperty ("bypassCapture", false);
    p.bypassEq = (bool) amp.getProperty ("bypassEq", false);
    p.bypassCab = (bool) amp.getProperty ("bypassCab", false);

    p.cameraDevice = json["camera"]["device"].toString();

    loop.clear();
    loopSampleRate = 0.0;
    const auto loopFile = resolveInside (json["loop"]["file"], folder);
    if (loopFile.existsAsFile() && readWav (loopFile, loop, loopSampleRate))
        p.loopLayers = (int) number (json["loop"]["layers"], 1.0, 1.0, 64.0);
    // Older projects had the track from the start.
    p.looperTrack = (bool) json["loop"].getProperty ("track", p.loopLayers > 0);

    if (const auto* takeList = json["takes"].getArray())
    {
        for (const auto& t : *takeList)
        {
            if ((int) p.takes.size() >= maxListSize)
                break;
            Take take;
            take.name = t["name"].toString();
            take.audioFile = resolveInside (t["audio"], folder);
            take.videoFile = resolveInside (t["video"], folder);
            take.dryFile = resolveInside (t["dry"], folder);
            take.audioStartHostSeconds = number (t["audioStartHost"], 0.0, 0.0, 1.0e12);
            take.videoStartHostSeconds = number (t["videoStartHost"], 0.0, 0.0, 1.0e12);
            take.syncLagSeconds = number (t["syncLagSeconds"], 0.0, -10.0, 10.0);
            take.durationSeconds = number (t["duration"], 0.0, 0.0, maxTime);
            take.createdAt = juce::Time::fromISO8601 (t["createdAt"].toString());

            if (! take.audioFile.existsAsFile())
                continue;

            auto thumbs = thumbnailFolder (take).findChildFiles (juce::File::findFiles, false, "*.jpg",
                                                                 juce::File::FollowSymlinks::no);
            thumbs.sort();
            for (const auto& thumb : thumbs)
            {
                if (take.thumbnails.size() >= (size_t) maxSavedThumbnails)
                    break;
                if (auto image = juce::ImageFileFormat::loadFrom (thumb); image.isValid())
                    take.thumbnails.push_back (image);
            }
            p.takes.push_back (std::move (take));
        }
    }

    const auto arrangement = json["arrangement"];
    if (const auto* clipList = arrangement["clips"].getArray())
    {
        for (const auto& c : *clipList)
        {
            Clip clip;
            const auto takeName = c["take"].toString();
            for (size_t i = 0; i < p.takes.size(); ++i)
                if (p.takes[i].name == takeName)
                    clip.take = (int) i;
            clip.start = number (c["start"], 0.0, -maxTime, maxTime);
            clip.sourceStart = number (c["sourceStart"], 0.0, -maxTime, maxTime);
            clip.length = number (c["length"], 0.0, 0.0, maxTime);
            if (clip.take >= 0 && clip.length > 0.0 && (int) p.arrangement.clips.size() < maxListSize)
                p.arrangement.clips.push_back (clip);
        }
        auto readPieces = [&arrangement] (const char* key, std::vector<Clip>& out)
        {
            if (const auto* list = arrangement[key].getArray())
                for (const auto& c : *list)
                {
                    Clip clip;
                    clip.start = number (c["start"], 0.0, -maxTime, maxTime);
                    clip.sourceStart = number (c["sourceStart"], 0.0, -maxTime, maxTime);
                    clip.length = number (c["length"], 0.0, 0.0, maxTime);
                    if (clip.length > 0.0 && (int) out.size() < maxListSize)
                        out.push_back (clip);
                }
        };
        readPieces ("loopClips", p.arrangement.loopClips);
        readPieces ("songClips", p.arrangement.songClips);

        // Earlier projects: one loop region (anchor, start, end — open-ended
        // when end < 0) and the song as a whole from songStart.
        if (arrangement.hasProperty ("loopStart") && ! arrangement.hasProperty ("loopClips") && ! loop.empty() && loopSampleRate > 0.0)
        {
            const double length = (double) loop.size() / loopSampleRate;
            const double anchor = number (arrangement["loopAnchor"], 0.0, -maxTime, maxTime);
            const double start = number (arrangement["loopStart"], 0.0, -maxTime, maxTime);
            const double end = number (arrangement["loopEnd"], -1.0, -1.0, maxTime);
            Clip clip;
            clip.start = start;
            clip.sourceStart = std::fmod (std::fmod (start - anchor, length) + length, length);
            double stop = end;
            if (stop < 0.0)
            {
                stop = start + length;
                for (const auto& c : p.arrangement.clips)
                    stop = std::max (stop, c.end());
            }
            clip.length = std::max (0.05, stop - start);
            p.arrangement.loopClips.push_back (clip);
        }
        p.arrangement.songStart = number (arrangement["songStart"], 0.0, -maxTime, maxTime);
        p.cycleOn = (bool) arrangement.getProperty ("cycleOn", false);
        p.cycleStart = number (arrangement["cycleStart"], 0.0, 0.0, maxTime);
        p.cycleEnd = number (arrangement["cycleEnd"], 0.0, 0.0, maxTime);
        p.guitarVolumeDb = (float) number (arrangement["guitarVolumeDb"], 0.0, -maxDb, maxDb);
        p.loopVolumeDb = (float) number (arrangement["loopVolumeDb"], 0.0, -maxDb, maxDb);
        p.masterVolumeDb = (float) number (arrangement["masterVolumeDb"], 0.0, -maxDb, maxDb);
    }
    else
    {
        // Version 1 had no timeline: lay the takes end to end, each on a bar.
        const double bar = 60.0 / p.bpm * p.beatsPerBar;
        double position = 0.0;
        for (size_t i = 0; i < p.takes.size(); ++i)
        {
            Clip clip;
            clip.take = (int) i;
            clip.start = position;
            clip.length = p.takes[i].durationSeconds;
            p.arrangement.clips.push_back (clip);
            position = std::ceil ((clip.end() + 0.001) / bar) * bar;
        }
    }

    if (const auto songObject = json["song"]; songObject.isObject())
    {
        p.song.file = resolveInside (songObject["file"], folder);
        p.song.title = songObject["title"].toString();
        p.song.bpm = number (songObject["bpm"], p.bpm, 40.0, 240.0);
        p.song.detectedBpm = number (songObject["detectedBpm"], p.song.bpm, 40.0, 240.0);
        p.song.firstDownbeat = number (songObject["firstDownbeat"], 0.0, 0.0, maxTime);
        p.song.duration = number (songObject["duration"], 0.0, 0.0, maxTime);
        p.songVolumeDb = (float) number (songObject["volumeDb"], -3.0, -maxDb, maxDb);
        p.song.speed = number (songObject["speed"], 1.0, 0.5, 1.0);
        p.song.semitones = juce::jlimit (-6, 6, (int) number (songObject["semitones"], 0.0, -6.0, 6.0));
        if (! p.song.file.existsAsFile())
            p.song = {};
    }

    // A song with no regions of its own (earlier projects): all of it, from songStart.
    if (p.song.isSet() && p.arrangement.songClips.empty())
    {
        Clip clip;
        clip.start = p.arrangement.songStart;
        clip.length = p.song.duration / p.song.speed;
        if (clip.length > 0.0)
            p.arrangement.songClips.push_back (clip);
    }

    result = std::move (p);
    return true;
}

bool Project::moveTo (const juce::File& newFolder, juce::String& error)
{
    if (newFolder == folder)
        return true;
    if (newFolder.isAChildOf (folder))
    {
        error = tr ("Couldn't copy the project.");
        return false;
    }

    if (folder.isDirectory() && ! folder.copyDirectoryTo (newFolder))
    {
        error = tr ("Couldn't copy the project.");
        return false;
    }
    newFolder.createDirectory();

    for (auto& t : takes)
    {
        t.audioFile = newFolder.getChildFile (t.audioFile.getRelativePathFrom (folder));
        if (t.videoFile != juce::File())
            t.videoFile = newFolder.getChildFile (t.videoFile.getRelativePathFrom (folder));
        if (t.dryFile != juce::File())
            t.dryFile = newFolder.getChildFile (t.dryFile.getRelativePathFrom (folder));
    }

    if (song.isSet())
        song.file = newFolder.getChildFile (song.file.getRelativePathFrom (folder));

    folder = newFolder;
    name = newFolder.getFileNameWithoutExtension();
    return true;
}
