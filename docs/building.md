# Building Roadie

Dependencies, building, tests and verification tools.

### Dependencies

**macOS** (tested on macOS 15, Intel): the **Command Line Tools** are enough (`xcode-select --install`), Xcode isn't needed. Plus CMake:
```bash
brew install cmake
```
On an Intel Mac running macOS 15, Homebrew has no prebuilt CMake and compiles it from source (~20 min): that's normal, it isn't stuck.

**Linux** (tested on Ubuntu 24.04, before moving to the Mac):
```bash
sudo apt-get install -y cmake libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev \
  libfreetype6-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libfontconfig-dev libxi-dev
```
The camera and video export are disabled there (`Source/Platform_stub.cpp`); everything else works.

**Windows**: Visual Studio 2022 + CMake. Not tested yet; same situation as Linux for the camera.

### Building

```bash
git submodule update --init --recursive   # JUCE + NeuralAmpModelerCore (and their submodules)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Roadie -j
open build/Roadie_artefacts/Release/Roadie.app
```

⚠️ **Always build in `Release`**. In Debug, the NAM engine (Eigen matrix maths) runs unoptimised and saturates the CPU — the first cause of crackling encountered.

⚠️ If `git submodule update` is interrupted, `libs/NeuralAmpModelerCore` can end up on the right commit but **with no files checked out** (happened on the first clone on the Mac). Fix with:
```bash
git -C libs/NeuralAmpModelerCore checkout -f --detach HEAD && git submodule update --init --recursive
```

**Signing (macOS)**: the build signs the app ad hoc (post-build step). Without a signature, macOS refuses the camera (and may refuse the microphone) **without even showing the prompt**. Each ad hoc build is a new identity for macOS, which may ask for permissions again after a rebuild; to avoid that, sign with an Apple Development certificate:
```bash
cmake -B build -DROADIE_CODESIGN_IDENTITY="Apple Development: …"
```

### Tests and verification tools

```bash
cmake --build build --target RoadieLooperTests RoadieDspTests RoadieEngineTests -j
ctest --test-dir build --output-on-failure
```

| Target | What it checks | JUCE? |
|---|---|---|
| `RoadieLooperTests` | Looper: record / play / overdub / undo / restart / clear, loading a saved loop | no (a few seconds) |
| `RoadieDspTests` | EQ (curves), metronome (clicks, count-in), attack detection and sync offset measurement, CPU label, the **autosave rule** (3 s of quiet, 30 s cap, never during a take), **song analysis** (tempo, first beat, parts, tap tempo, on synthetic drums), noise gate, tuner (YIN), time stretching and transposition | no |
| `RoadieEngineTests` | The whole real-time chain through a fake audio device: loop, mute/solo, pause, overdub, metronome never recorded, WAV take + timestamp, **latency compensation** (take, loop, count-in skip), stereo / mono output, Master and gate, **song playback** in stereo, tuner input, calibration capture, a real NAM capture (and switching it during playback), project round trip with a song, **damaged project.json** (recovery from `project.json.bak`, distinct error messages), **MP4 export** 9:16 / 16:9 / timeline range with burned-in title (from a synthetic video), and the **English / French** translation table | yes |

Tools:
- `build/RoadieNamBench <capture.nam> <buffer> <sampleRate>` — CPU load of a capture (measurements at the bottom of [architecture.md](architecture.md)).
- `Roadie.app --snapshot=/tmp/ui.png [--demo] [--demo-song[=file]] [--workspace=play|edit] [--library=captures|songs|takes] [--track=amp|loop|camera|song] [--export] [--light|--dark] [--camera] [--scale=0.9] [--no-library] [--no-inspector]` — renders the window into a PNG then quits: check the interface without a screenshot (the terminal isn't allowed to record the screen). `--demo` opens a demo project (two synthetic takes, a loop, a cycle) to see regions without filming anything; `--demo-song` imports a synthetic song into it (drums and bass at 92 BPM), `--demo-song=/path/song.m4a` a real file (path without spaces: the command line is split on spaces). The camera stays off during these checks, except with `--camera`. Launch through `open -W Roadie.app --args …` so the camera/microphone permissions are requested on Roadie's behalf. The interface language is the saved one (English unless changed in View › Language).
- `Roadie.app --selftest-edit` — opens the demo project and edits it like a user would, with **real mouse events** injected into the window (the whole JUCE chain: grab zones, drag thresholds, ⌥, double-click): move, trim both edges, duplicate with ⌥, move the loop, draw a cycle, place the playhead in the ruler, ⌘Z ×4 then ⇧⌘Z, the splitters, Y and B; then the workspaces (⌘1 / ⌘2, camera off in Arrange, each Play splitter, folding and reopening a panel, sizes remembered per workspace, enlarged panel, ● from Arrange with its count-in), the song (import, tempo, aligning on bar 2, playback, M, clicking a part, ×2, nudge and ⌘Z, export with / without, 75% and +2 semitones), arming, the amp blocks, the Master and a track's height (60 checks, ~40 s). One line per check on stderr; exit code = number of failures. The window comes to the front (tooltips off): **don't touch the mouse during the ~5 s of the test**. The robot shares the system mouse: a real movement over the window, or a window passing in front (permission prompt, notification), makes gestures fail — rerun in that case. If the window isn't on screen at all (locked session, sleeping display), the robot hands the gestures straight to the components (same events, same order) and says so in the log: the logic is verified, not the delivery by macOS.
- `Roadie.app --selftest-take=/tmp/folder` — opens the camera, records a 3 s take (audio + video) in a project created in that folder, exports it as MP4 and prints the result on stderr. **It really films**: delete the folder afterwards.

These tools save `Roadie.settings` at startup and put it back on exit: theme, panel sizes, last opened project… stay the user's.
