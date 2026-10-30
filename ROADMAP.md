# Roadmap

**Stage: closed beta (macOS).** The core of the product is complete; what's left is validating it on real hardware and distributing it properly.

## Done

| Milestone | Contents |
|---|---|
| Audio prototype | Audio interface, NAM captures, IRs, ~10 ms latency, CPU load |
| Looper | Record → loop → overdub, undo, clear; metronome locked to the transport |
| Webcam + sync | AVFoundation capture, audio + video takes, sync calibration ([details](docs/sync-audio-video.md)) |
| Clip export | H.264/AAC MP4 (hardware encoder), 9:16 or 16:9, trim, burned-in title, audio-only WAV |
| Interface | DAW-style: library, tracks, playhead, camera monitor, inspector, dark and light themes |
| Editing | Editable regions, cycle, zoom, undo/redo (200 levels), collapsible panels |
| Play / Arrange | Two workspaces, backing songs (tempo, parts, slow down, transpose, YouTube import), signal chain with noise gate, Master, tuner, count-in |
| Robustness | Autosave ~3 s after every edit, recovery from `project.json.bak`, engine tests (looper, DSP, real-time chain, export) |
| Distribution | Packaging script (signing, notarisation, DMG), GitHub CI and release workflow |
| Languages | English by default, French translation (View › Language) |

## Before opening the beta

- [ ] **Test with a real audio interface** (Scarlett): actual latency and compensation (a take played on a click must land on the grid), sync calibration, a long filmed take, a real song, the tuner with a guitar.
- [ ] Apple Silicon: first build and first real-world test (CI already builds arm64).
- [ ] Developer ID certificate + notarisation (see [Releasing](docs/releasing.md)).

## Next

1. **Tempo map**: a song played without a click drifts against the fixed grid; a tempo map would follow it.
2. **Several Looper tracks**, loop length quantised to the bar, choosing a part of the loop in the export.
3. **Camera and export outside macOS**: Media Foundation (Windows), V4L2 / GStreamer (Linux) behind `CameraCapture`. The audio engine and the interface already build on Linux, without a camera.
4. **Art-direction fonts** (Plus Jakarta Sans + Poppins, OFL) bundled to get the exact look outside macOS.
5. **Separating the guitar from the original song**, the ideal tool for covers (big project).
