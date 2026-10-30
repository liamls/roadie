<p align="center">
  <img src="docs/logo/roadie-icon.svg" width="112" alt="Roadie">
</p>

<h1 align="center">Roadie</h1>

<p align="center">
  <b>Plug in. Play. Ship the clip.</b><br>
  Amp, looper, synced camera, editing and export: the whole backstage of the guitarist who films,<br>
  in a single app. No more juggling a DAW, OBS and an amp simulator.
</p>

<p align="center">
  <a href="https://github.com/liamls/roadie/actions/workflows/ci.yml"><img alt="CI" src="https://img.shields.io/github/actions/workflow/status/liamls/roadie/ci.yml?branch=main&style=flat-square&label=CI"></a>
  <img alt="macOS 15" src="https://img.shields.io/badge/macOS-15-1f1f1f?style=flat-square&logo=apple&logoColor=white">
  <img alt="Status: beta" src="https://img.shields.io/badge/status-beta-f9853b?style=flat-square">
  <a href="LICENSE"><img alt="AGPLv3 license" src="https://img.shields.io/badge/license-AGPLv3-3a3a3a?style=flat-square"></a>
  <img alt="C++20 · JUCE" src="https://img.shields.io/badge/C%2B%2B20-JUCE-47dab4?style=flat-square">
</p>

<p align="center">
  <a href="https://github.com/liamls/roadie/releases/latest"><b>Download</b></a> ·
  <a href="#the-workflow-in-five-steps">Workflow</a> ·
  <a href="#whats-in-the-box">Features</a> ·
  <a href="#building">Build</a> ·
  <a href="docs/usage.md">Shortcuts</a> ·
  <a href="ROADMAP.md">Roadmap</a>
</p>

<p align="center">
  <img src="docs/screenshot.png" alt="Roadie: capture library, Guitar, Looper and Camera tracks, timeline with regions, amp at the bottom" width="100%">
</p>

---

## The workflow in five steps

1. **Plug in.** Pick a [NAM](https://www.neuralampmodeler.com) amp capture and a cab impulse response, then dial in your tone: gain, noise gate, EQ, volume. Expect latency around 10 ms with an audio interface.
2. **Import a song** (file or YouTube link): Roadie detects the tempo and the first bar. Slow it down to 50%, transpose it, loop any stretch with the cycle zone.
3. **Play.** Record a loop, stack layers, then start an audio + video **take** after a count-in. The camera is aligned to the sound through a measured calibration.
4. **Edit** like in GarageBand: move, trim, split and duplicate regions, draw a cycle, undo anything with ⌘Z.
5. **Ship the clip**: vertical 9:16 or horizontal 16:9 MP4, burned-in title, with or without the song, or audio only as WAV.

## What's in the box

<table>
  <tr>
    <td width="33%" valign="top">
      <h4>🎸 A real amp</h4>
      NAM captures (WaveNet, LSTM, Slimmable), Bass / Mid / Treble / Presence EQ, IR cab, noise gate, per-block bypass. Levels are normalised: switching captures doesn't jump the volume.
    </td>
    <td width="33%" valign="top">
      <h4>🔁 Looper and songs</h4>
      Record → loop → overdub, undo a layer, metronome locked to the transport. MP3 / M4A / WAV / AIFF / FLAC backing songs with detected tempo and parts.
    </td>
    <td width="33%" valign="top">
      <h4>🎥 Synced camera</h4>
      Webcam, USB camera or iPhone (Continuity Camera). A 5-second calibration measures the offset between your audio interface and the camera, then applies it to every take.
    </td>
  </tr>
  <tr>
    <td width="33%" valign="top">
      <h4>✂️ DAW-style editing</h4>
      Editable regions, grid snapping, cycle zone, zoom, 200 undo levels. Two workspaces: <b>Play</b> (monitor, Looper pedal) and <b>Arrange</b> (timeline-first).
    </td>
    <td width="33%" valign="top">
      <h4>🎬 One-click export</h4>
      H.264 / AAC with the Mac's hardware encoder, 9:16 or 16:9, trim to the range you want, burned-in title, size estimate before you start.
    </td>
    <td width="33%" valign="top">
      <h4>🛡️ Your takes are safe</h4>
      Autosave after every change, project backup copy, takes written straight to disk while recording. A project is a plain folder you can browse in the Finder.
    </td>
  </tr>
</table>

Dark or light interface, collapsible and resizable panels, interface size from 80 to 125%, built-in tuner, level meters with a clip indicator. English by default, with a French translation (View › Language).

## Download

Releases are on the [Releases](https://github.com/liamls/roadie/releases) tab. Open the `.dmg`, drag Roadie into Applications, and allow camera and microphone access on first launch.

> [!NOTE]
> The beta is **macOS only** for now (the camera and export go through AVFoundation). The audio engine and the interface build on Linux, without a camera; Windows hasn't been tested yet. See the [roadmap](ROADMAP.md).

> [!WARNING]
> Importing from a YouTube link relies on [yt-dlp](https://github.com/yt-dlp/yt-dlp) and is meant for personal use only: downloading is generally not allowed by YouTube's terms of service. Apple Music and Spotify tracks are protected and can't be played.

## Building

```bash
brew install cmake
git clone --recursive https://github.com/liamls/roadie.git
cd roadie
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Roadie -j
open build/Roadie_artefacts/Release/Roadie.app
```

The Command Line Tools are enough (no Xcode needed). **Always build in `Release`**: in Debug, the NAM engine saturates the CPU. Tests, UI verification tools and troubleshooting: [docs/building.md](docs/building.md).

```bash
cmake --build build --target RoadieLooperTests RoadieDspTests RoadieEngineTests -j
ctest --test-dir build --output-on-failure
```

## Going further

| | |
|---|---|
| [Usage and shortcuts](docs/usage.md) | The full workflow and every keyboard and mouse shortcut |
| [Architecture](docs/architecture.md) | The real-time engine, the session, the interface, and the technical decisions |
| [Audio / video sync](docs/sync-audio-video.md) | How the guitar and the picture stay aligned |
| [Project format](docs/project-format.md) | The `.roadie` folder |
| [Building](docs/building.md) · [Releasing](docs/releasing.md) | Build, tests, signing, notarisation, DMG, CI |
| [Roadmap](ROADMAP.md) · [Changelog](CHANGELOG.md) | Where things stand, what's next |
| [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) | Repository rules, reporting a vulnerability |

## License

Roadie is released under the **[AGPLv3](LICENSE)**. It builds on [JUCE](https://juce.com) (AGPLv3), [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (MIT), Eigen, nlohmann/json and yt-dlp: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). `.nam` captures and IRs are not included; each one belongs to its author.
