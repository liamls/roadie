#pragma once

#include <juce_graphics/juce_graphics.h>

#include <vector>

// A Roadie project is a plain folder, "<Nom>.roadie/":
//
//   project.json     settings (tempo, ampli, caméra), takes, arrangement
//   loop.wav         the looper content, 24-bit mono
//   song/            the backing track, copied as imported (MP3, M4A, WAV…)
//   takes/           take-N.wav (the mix), take-N.mov (camera), take-N/ thumbnails
//   exports/         rendered clips
//
// A folder keeps big media out of the JSON, stays readable with any file
// manager, and lets takes be written straight to their final place while
// recording (nothing to copy at save time). Paths inside the project are
// stored relative; the capture and IR live in the user's library, so they
// are stored absolute.
struct Take
{
    juce::String name;
    bool hidden = false; // kept in the project, but out of the Prises list
    juce::File audioFile;
    juce::File videoFile;
    juce::File dryFile; // the guitar before the amp (takes recorded since re-amping exists): lets the amp change after the fact

    // Host-clock (mach_absolute_time) capture times of the first audio sample
    // and the first video frame — only meaningful relative to each other.
    double audioStartHostSeconds = 0.0;
    double videoStartHostSeconds = 0.0;

    // Sync calibration in force when the take was recorded (see SyncAnalysis.h).
    double syncLagSeconds = 0.0;

    double durationSeconds = 0.0;
    juce::Time createdAt;
    std::vector<juce::Image> thumbnails;

    bool hasVideo() const { return videoFile.existsAsFile() && videoStartHostSeconds > 0.0; }

    // Where the first frame lands on the take's audio timeline.
    double getVideoOffsetSeconds() const
    {
        return videoStartHostSeconds - (audioStartHostSeconds - syncLagSeconds);
    }
};

// One region of the timeline: a slice of a take — its audio, and its video
// when it has one — placed somewhere on the arrangement. Moving, trimming or
// splitting a region never touches the take's files.
struct Clip
{
    int take = -1;            // index in Project::takes
    double start = 0.0;       // timeline position, seconds
    double sourceStart = 0.0; // offset into the take's audio, seconds
    double length = 0.0;      // seconds

    double end() const { return start + length; }
    bool operator== (const Clip& o) const
    {
        return take == o.take && juce::exactlyEqual (start, o.start) && juce::exactlyEqual (sourceStart, o.sourceStart)
               && juce::exactlyEqual (length, o.length);
    }
};

// A part of the backing track (Intro, A, B…), in the song file's time.
struct SongSection
{
    double start = 0.0, end = 0.0;
    juce::String label;
};

// The backing track: a file in the project's song/ folder, its tempo and
// where its first bar starts (detected, then corrected by the player).
struct Song
{
    juce::File file;
    juce::String title;
    double bpm = 0.0, detectedBpm = 0.0;
    double firstDownbeat = 0.0; // seconds into the file
    double duration = 0.0;
    std::vector<SongSection> sections;
    double speed = 1.0; // practice: 0.5 … 1.0
    int semitones = 0;  // transposition, −6 … +6

    bool isSet() const { return file != juce::File(); }
};

// What the timeline edits (and what ⌘Z undoes): the regions of the Guitare
// track, the Looper region and where the song sits. Where regions overlap, the later one in the
// list plays (and shows its video), like a punch-in over an older take.
struct Arrangement
{
    std::vector<Clip> clips;

    // The loop's first sample sits at `loopAnchor` on the timeline; the loop
    // repeats from there, audible between loopStart and loopEnd (< 0: until
    // the end of the arrangement).
    double loopAnchor = 0.0;
    double loopStart = 0.0;
    double loopEnd = -1.0;

    // Timeline position of the song's first sample (negative: it starts
    // before the timeline, its first bar on bar 2).
    double songStart = 0.0;

    bool isLoopOpenEnded() const { return loopEnd < 0.0; }
    // End of the last region, or of the bounded loop region.
    double getContentEnd (bool hasLoop) const;

    bool operator== (const Arrangement& o) const
    {
        return clips == o.clips && juce::exactlyEqual (loopAnchor, o.loopAnchor) && juce::exactlyEqual (loopStart, o.loopStart)
               && juce::exactlyEqual (loopEnd, o.loopEnd) && juce::exactlyEqual (songStart, o.songStart);
    }
    bool operator!= (const Arrangement& o) const { return ! (*this == o); }
};

struct Project
{
    juce::String name;
    juce::File folder;

    double bpm = 100.0;
    int beatsPerBar = 4;
    bool metronomeOn = false;

    juce::File captureFile;
    juce::File irFile;
    float gainDb = 0.0f, bassDb = 0.0f, midDb = 0.0f, trebleDb = 0.0f, presenceDb = 0.0f, volumeDb = 0.0f;
    bool gateOn = false;
    float gateThresholdDb = -60.0f;
    bool bypassCapture = false, bypassEq = false, bypassCab = false;
    float masterVolumeDb = 0.0f;

    juce::String cameraDevice;
    int loopLayers = 0;
    bool looperTrack = false; // the Looper track is only there once added (or once a loop exists)

    std::vector<Take> takes;
    Arrangement arrangement;

    // Cycle (zone de répétition) and track volumes: view/mix state, not
    // part of the undo history.
    bool cycleOn = false;
    double cycleStart = 0.0, cycleEnd = 0.0;
    float guitarVolumeDb = 0.0f, loopVolumeDb = 0.0f, songVolumeDb = -3.0f;

    Song song;

    static constexpr const char* extension = ".roadie";

    // Set by load() when project.json was unreadable and project.json.bak was used instead.
    bool recoveredFromBackup = false;

    juce::File getProjectFile() const { return folder.getChildFile ("project.json"); }
    juce::File getBackupFile() const { return folder.getChildFile ("project.json.bak"); }
    juce::File getLoopFile() const { return folder.getChildFile ("loop.wav"); }
    juce::File getTakesFolder() const { return folder.getChildFile ("takes"); }
    juce::File getExportsFolder() const { return folder.getChildFile ("exports"); }
    juce::File getSongFolder() const { return folder.getChildFile ("song"); }

    // Next free take number, then "Prise 3" and take-3.wav / take-3.mov.
    // Compute the number once per take: the files it names count as taken
    // as soon as they exist.
    int getNextTakeNumber() const;
    juce::String getNextTakeName() const { return getTakeName (getNextTakeNumber()); }
    juce::File getNextTakeFile (const juce::String& extensionWithDot) const { return getTakeFile (getNextTakeNumber(), extensionWithDot); }
    static juce::String getTakeName (int number) { return "Prise " + juce::String (number); }
    juce::File getTakeFile (int number, const juce::String& extensionWithDot) const;

    static juce::File getDefaultProjectsFolder();
    static Project createUntitled();

    bool save (const std::vector<float>& loop, double loopSampleRate, juce::String& error) const;
    static bool load (const juce::File& folderOrJson, Project& result,
                      std::vector<float>& loop, double& loopSampleRate, juce::String& error);

    // Copies the whole folder to `newFolder` (save as…) and points the
    // project at it.
    bool moveTo (const juce::File& newFolder, juce::String& error);
};
