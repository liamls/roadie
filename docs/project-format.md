# Project format

A project is a `.roadie` folder you can browse in the Finder.

A project is a **folder** `<Name>.roadie/` (in `~/Music/Roadie/Projects` by default):

```
Evening riff.roadie/
├── project.json   tempo, time signature, metronome, amp (capture, IR, knobs), camera, takes, arrangement
├── loop.wav       the loop, 24-bit mono WAV
├── song/          the backing song, copied as imported
├── takes/         take-N.wav (the amped guitar), take-N.mov (the camera), take-N/ (thumbnails)
└── exports/       the rendered clips
```

**Version 2** (editing): `project.json` carries an `arrangement` object — the regions (`clips`: the take *by name*, `start` on the timeline, `sourceStart` in the take, `length`, in seconds), the loop's placement (`loopAnchor`, `loopStart`, `loopEnd`, < 0 = to the end), the cycle zone (`cycleOn`, `cycleStart`, `cycleEnd`) and the track volumes in dB. A version 1 project is migrated when opened: its takes are laid end to end, each aligned to a bar.

Compatible additions (older projects open as they are): `song` (file in `song/`, title, detected and chosen tempo, `firstDownbeat`, duration, speed, transposition, volume), `arrangement.loopClips` and `arrangement.songClips` (the Looper's and the song's regions: `start`, `sourceStart`, `length`; earlier `loopAnchor` / `loopStart` / `loopEnd` are converted on opening), `arrangement.songStart` (undoable with ⌘Z), `arrangement.masterVolumeDb`, and in `amp` the noise gate (`gateOn`, `gateThresholdDb`) and the bypasses (`bypassCapture`, `bypassEq`, `bypassCab`).

Take names are plain text stored in `project.json` ("Take 3" for projects created now, "Prise 3" in projects made by earlier versions); they are never translated.

Why a folder: the heavy media stay out of the JSON, everything stays readable in the Finder, and takes are written straight to their place while recording (nothing to copy when saving). Internal paths are relative and must stay inside the project folder (Roadie ignores a take, loop or song that points elsewhere); the capture and IR are absolute (they live in the user's library). The project is saved automatically ~3 s after every change (at most 30 s under continuous changes, never during a take or an export), after every take and on close; each save keeps the previous one in `project.json.bak`, which Roadie reopens if `project.json` is unreadable; `⌘S`, `⇧⌘S` (save as, copies the folder), `⌘O`, `⌘N`; a `.roadie` folder can also be reopened by drag and drop.
