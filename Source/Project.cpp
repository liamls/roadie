#include "Project.h"

#include <juce_audio_formats/juce_audio_formats.h>

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
    if (hasLoop && ! isLoopOpenEnded())
        end = std::max (end, loopEnd);
    return end;
}

juce::File Project::getDefaultProjectsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Roadie").getChildFile ("Projets");
}

Project Project::createUntitled()
{
    Project p;
    const auto now = juce::Time::getCurrentTime();
    p.name = "Session du " + now.formatted ("%d-%m-%Y %Hh%M");
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
        error = juce::String::fromUTF8 ("Impossible de cr\xc3\xa9" "er le dossier du projet.");
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
            error = juce::String::fromUTF8 ("\xc3\x89" "criture de la boucle impossible.");
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
        take->setProperty ("hidden", t.hidden);
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
        juce::Array<juce::var> sectionList;
        for (const auto& section : song.sections)
        {
            auto object = std::make_unique<juce::DynamicObject>();
            object->setProperty ("start", section.start);
            object->setProperty ("end", section.end);
            object->setProperty ("label", section.label);
            sectionList.add (object.release());
        }
        songObject->setProperty ("sections", sectionList);
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
    arrangementObject->setProperty ("loopAnchor", arrangement.loopAnchor);
    arrangementObject->setProperty ("loopStart", arrangement.loopStart);
    arrangementObject->setProperty ("loopEnd", arrangement.loopEnd);
    arrangementObject->setProperty ("songStart", arrangement.songStart);
    arrangementObject->setProperty ("cycleOn", cycleOn);
    arrangementObject->setProperty ("cycleStart", cycleStart);
    arrangementObject->setProperty ("cycleEnd", cycleEnd);
    arrangementObject->setProperty ("guitarVolumeDb", guitarVolumeDb);
    arrangementObject->setProperty ("loopVolumeDb", loopVolumeDb);
    arrangementObject->setProperty ("masterVolumeDb", masterVolumeDb);
    root->setProperty ("arrangement", arrangementObject.release());

    const auto json = juce::JSON::toString (juce::var (root.release()));
    if (! getProjectFile().replaceWithText (json))
    {
        error = juce::String::fromUTF8 ("\xc3\x89" "criture de project.json impossible.");
        return false;
    }
    return true;
}

bool Project::load (const juce::File& folderOrJson, Project& result,
                    std::vector<float>& loop, double& loopSampleRate, juce::String& error)
{
    const auto folder = folderOrJson.isDirectory() ? folderOrJson : folderOrJson.getParentDirectory();
    const auto json = juce::JSON::parse (folder.getChildFile ("project.json"));

    if (json["format"].toString() != "roadie-project")
    {
        error = juce::String::fromUTF8 ("Ce dossier n'est pas un projet Roadie.");
        return false;
    }

    Project p;
    p.folder = folder;
    p.name = folder.getFileNameWithoutExtension();
    p.bpm = (double) json.getProperty ("bpm", 100.0);
    p.beatsPerBar = (int) json.getProperty ("beatsPerBar", 4);
    p.metronomeOn = (bool) json.getProperty ("metronome", false);

    const auto amp = json["amp"];
    p.captureFile = resolve (amp["capture"], folder);
    p.irFile = resolve (amp["ir"], folder);
    p.gainDb = (float) amp.getProperty ("gainDb", 0.0);
    p.bassDb = (float) amp.getProperty ("bassDb", 0.0);
    p.midDb = (float) amp.getProperty ("midDb", 0.0);
    p.trebleDb = (float) amp.getProperty ("trebleDb", 0.0);
    p.presenceDb = (float) amp.getProperty ("presenceDb", 0.0);
    p.volumeDb = (float) amp.getProperty ("volumeDb", 0.0);
    p.gateOn = (bool) amp.getProperty ("gateOn", false);
    p.gateThresholdDb = (float) amp.getProperty ("gateThresholdDb", -60.0);
    p.bypassCapture = (bool) amp.getProperty ("bypassCapture", false);
    p.bypassEq = (bool) amp.getProperty ("bypassEq", false);
    p.bypassCab = (bool) amp.getProperty ("bypassCab", false);

    p.cameraDevice = json["camera"]["device"].toString();

    loop.clear();
    loopSampleRate = 0.0;
    const auto loopFile = resolve (json["loop"]["file"], folder);
    if (loopFile.existsAsFile() && readWav (loopFile, loop, loopSampleRate))
        p.loopLayers = (int) json["loop"].getProperty ("layers", 1);
    // Older projects had the track from the start.
    p.looperTrack = (bool) json["loop"].getProperty ("track", p.loopLayers > 0);

    if (const auto* takeList = json["takes"].getArray())
    {
        for (const auto& t : *takeList)
        {
            Take take;
            take.name = t["name"].toString();
            take.hidden = (bool) t["hidden"];
            take.audioFile = resolve (t["audio"], folder);
            take.videoFile = resolve (t["video"], folder);
            take.dryFile = resolve (t["dry"], folder);
            take.audioStartHostSeconds = (double) t["audioStartHost"];
            take.videoStartHostSeconds = (double) t["videoStartHost"];
            take.syncLagSeconds = (double) t["syncLagSeconds"];
            take.durationSeconds = (double) t["duration"];
            take.createdAt = juce::Time::fromISO8601 (t["createdAt"].toString());

            auto thumbs = thumbnailFolder (take).findChildFiles (juce::File::findFiles, false, "*.jpg",
                                                                 juce::File::FollowSymlinks::no);
            thumbs.sort();
            for (const auto& thumb : thumbs)
                if (auto image = juce::ImageFileFormat::loadFrom (thumb); image.isValid())
                    take.thumbnails.push_back (image);

            if (take.audioFile.existsAsFile())
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
            clip.start = (double) c["start"];
            clip.sourceStart = (double) c["sourceStart"];
            clip.length = (double) c["length"];
            if (clip.take >= 0 && clip.length > 0.0)
                p.arrangement.clips.push_back (clip);
        }
        p.arrangement.loopAnchor = (double) arrangement.getProperty ("loopAnchor", 0.0);
        p.arrangement.loopStart = (double) arrangement.getProperty ("loopStart", 0.0);
        p.arrangement.loopEnd = (double) arrangement.getProperty ("loopEnd", -1.0);
        p.arrangement.songStart = (double) arrangement.getProperty ("songStart", 0.0);
        p.cycleOn = (bool) arrangement.getProperty ("cycleOn", false);
        p.cycleStart = (double) arrangement.getProperty ("cycleStart", 0.0);
        p.cycleEnd = (double) arrangement.getProperty ("cycleEnd", 0.0);
        p.guitarVolumeDb = (float) arrangement.getProperty ("guitarVolumeDb", 0.0);
        p.loopVolumeDb = (float) arrangement.getProperty ("loopVolumeDb", 0.0);
        p.masterVolumeDb = (float) arrangement.getProperty ("masterVolumeDb", 0.0);
    }
    else
    {
        // Version 1 had no timeline: lay the takes end to end, each on a bar.
        const double bar = 60.0 / std::max (1.0, p.bpm) * p.beatsPerBar;
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
        p.song.file = resolve (songObject["file"], folder);
        p.song.title = songObject["title"].toString();
        p.song.bpm = (double) songObject.getProperty ("bpm", p.bpm);
        p.song.detectedBpm = (double) songObject.getProperty ("detectedBpm", p.song.bpm);
        p.song.firstDownbeat = (double) songObject["firstDownbeat"];
        p.song.duration = (double) songObject["duration"];
        if (const auto* sectionList = songObject["sections"].getArray())
            for (const auto& section : *sectionList)
                p.song.sections.push_back ({ (double) section["start"], (double) section["end"], section["label"].toString() });
        p.songVolumeDb = (float) songObject.getProperty ("volumeDb", -3.0);
        p.song.speed = juce::jlimit (0.5, 1.0, (double) songObject.getProperty ("speed", 1.0));
        p.song.semitones = juce::jlimit (-6, 6, (int) songObject.getProperty ("semitones", 0));
        if (! p.song.file.existsAsFile())
            p.song = {};
    }

    result = std::move (p);
    return true;
}

bool Project::moveTo (const juce::File& newFolder, juce::String& error)
{
    if (newFolder == folder)
        return true;

    if (folder.isDirectory() && ! folder.copyDirectoryTo (newFolder))
    {
        error = juce::String::fromUTF8 ("Copie du projet impossible.");
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
