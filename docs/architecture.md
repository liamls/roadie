# Architecture

How Roadie is built, and why.

```
Guitar ─► AmpEngine (gain → NAM → EQ → IR → volume) ─┬─► direct ──────────────┬─► mix ─► Metronome ─► output
                                                     ├─► TakeRecorder (WAV)   │
                                                     └─► Looper ─► loop ──────┤
                 take regions (played at the transport position) ─────────────┘
Camera ─► CameraCapture (AVFoundation) ─► preview + .mov take (H.264), timestamped on the same clock as the audio
```

**Engine (no UI)**
- **`AudioEngine`** — registered directly as an `AudioIODeviceCallback` (not through `AudioAppComponent`) to receive CoreAudio's host timestamp. Orchestrates the chain above and a **linear transport** (play, stop, playhead placement, cycle zone disabled during a take), with blocks split at the cycle and loop-region boundaries. Regions reach the audio thread without a blocking lock (pending → active → retired); the topmost region wins when they overlap. Per-track mute and volume, meters, peak history (to draw what is being recorded), calibration capture. The callback never blocks: loading a project loop = `SpinLock` on the message side, `TryLock` on the audio side (a block of silence at worst).
- **`AmpEngine`** — `.nam` capture (NeuralAmpModelerCore), level normalisation from the capture's loudness (−18 dB, like the NAM plugin: switching captures doesn't jump the volume), `ToneStack`, IR (`juce::dsp::Convolution`), gain/volume. Thread-safe model swap.
- **`ToneStack`** — Bass / Mid / Treble / Presence (4 RBJ biquads), no JUCE dependency.
- **`Looper`** — mono, real-time safe, no JUCE dependency. Loop output kept separate from the direct signal (for mute/solo), layer counter, loading from a project; the loop follows the playhead (`setPlayhead`), aligned to its anchor on the timeline.
- **`Metronome`** — locked to the transport position (the loop's first beat lands on the bar's first beat), accented first beat, mixed into the monitoring only.
- **`TakeRecorder`** — writes the amped guitar to WAV through a lock-free FIFO (`ThreadedWriter`), notes the host time of the first sample and the take's position on the timeline.
- **`SyncAnalysis`** — attack detection and offset computation between two timestamped captures, no JUCE dependency.
- **`CameraCapture`** (`_mac.mm`) — direct AVFoundation: camera list (built-in, USB, iPhone via Continuity Camera), permissions, reduced preview, H.264 recording through `AVAssetWriter`, thumbnails, the camera's microphone for calibration.
- **`ClipExporter`** (`_mac.mm`) — AVFoundation composition: the mix of the exported range + each region's video (segments shifted by the sync offset, gaps in black), 9:16 / 16:9 crop, mirror, title (rendered with CoreText), `AVAssetExportSession` export (H.264/AAC, hardware encoder).
- **`VideoPreview`** (`_mac.mm`) — video frames under the playhead (`AVAssetImageGenerator`) for the monitor in PLAYBACK mode; stacked requests are merged, late frames dropped.
- **`CpuLoad`** — the audio CPU load as a word (Low / Normal / High): rolling average over ~2 s, thresholds at 35% and 70% with a dead band, no JUCE dependency.
- **`Project`** — project format ([project-format.md](project-format.md)). **`CaptureLibrary`** — background scan of the capture folders, `.nam` metadata.
- **`Session`** — the non-visual controller: audio device (48 kHz / 64 samples requested), project, takes, transport in seconds, **arrangement** (every edit goes through `applyEdit`: 200 undo levels, protected when a new loop was recorded in the meantime), selection, split / delete / duplicate / insert a take, takes' audio cache (peaks at 100 per second), offline mixdown (`renderMix`), calibration, range export, persistent settings (`~/Library/Application Support/Roadie/Roadie.settings`). The UI only talks to it.
- **`I18n`** — the interface language. Visible strings are English and go through `tr ("…")`; `Source/I18nFrench.cpp` maps each English string to its French text. The language is read from the settings (`language`: `en` by default, `fr`) before any window exists, so changing it in View › Language applies on the next launch. `i18n::percent()` and `i18n::decimalPoint()` follow the language for number formatting.

Later additions: **`SongAnalysis`** (tempo by autocorrelation of a three-band attack envelope, then fitting a grid over the whole song; first beat from the kick drum; parts by bar-by-bar novelty, no longer shown), **`TimeStretch`** (offline WSOLA on both channels at once, then resampling to transpose), **`NoiseGate`** (before the capture, 6 dB hysteresis), **`Tuner`** (YIN) — all without JUCE. `AudioEngine` mixes in **stereo** (guitar, loop and click in the centre, the song with its stereo, then the Master) and places takes, loops and overdubs one round-trip latency earlier (the one reported by the interface, plus the Audio Settings correction). Switching the NAM capture goes through a pointer handed back to the main thread: no more blocks without amp.

**Interface** (`Source/UI/`) — two workspaces in one window, chosen with the **Play | Arrange** switch at the top left (⌘1 / ⌘2): `TopBar` (switch, transport, bar/tempo/meter display, metronome, tuner, project name and its state, a latency · CPU pill that opens the audio settings, Export), `LibraryPanel` (Captures / Songs / Takes tabs + search), `Monitor` (built-in monitor: live with 9:16 / 16:9 guides and an on-screen count-in in Play, playback only in Arrange), `StagePanel` (Play: Looper pedal and Song / Guitar / Loop mixer), `Timeline` (video band on top, Guitar, Looper, Song, Master; each track's height is adjustable), `Inspector` (the amp's signal chain, Looper, Camera with a calibration wizard, Song; stacked and scrolling in Arrange's narrow column), `ExportSheet`, `Widgets` (including a console-curve `Fader`, a `Meter` with peak and clip indicator, `SegmentedControl`, a collapsible `Splitter`) and `Icons`. `MainComponent` lays out the current workspace's panels (per-workspace sizes, folding, enlarged panel, 200 ms transition), turns the camera off in Arrange and routes shortcuts, menus and drag and drop.

### Repository layout

| Path | Contents |
|---|---|
| `Source/` | Engine (no UI), session, formats; `Source/UI/` for the panels; `*_mac.mm` for AVFoundation, `Platform_stub.cpp` for other systems; `I18n*.cpp` for the languages |
| `Tests/` | Looper, DSP and engine tests; `TestMovie_mac.mm` builds a synthetic video |
| `Tools/` | `nam_bench.cpp`, CPU-load measurement of a capture |
| `docs/` | `design-direction.md` (visual direction), `reference-ui-garageband-filmcraft.md` (GarageBand / filmcraft ergonomics study), `screenshot.png` |
| `site/` | `index.html`, a standalone presentation page (no build dependency) |
| `libs/` | JUCE and NeuralAmpModelerCore submodules |

### Submodules

- `libs/JUCE` — pinned to `9.0.4`. ⚠️ JUCE is dual-licensed AGPLv3 / commercial: Roadie is released under the **AGPLv3** (`LICENSE`); a closed distribution would need a commercial JUCE license.
- `libs/NeuralAmpModelerCore` — pinned to `v0.6.0`. Compiled as an **OBJECT** library `RoadieNAM` shared by the app and the tests (see "Notable technical decisions"). Nested dependencies: Eigen, `nlohmann/json`, `AudioDSPTools` (present but **unused** — `juce::dsp::Convolution` handles the IR).

## Notable technical decisions

| Decision | Reason |
|---|---|
| No LTO | ~35 min of linking for a marginal gain (Eigen/NAM templates); `-O3` alone is enough |
| `NAM_SAMPLE_FLOAT` | JUCE buffers are `float`; otherwise NAM works in `double` |
| 48 kHz / 64 samples requested at startup | NAM captures are trained at 48 kHz; the system default (~81 ms under PipeWire) was the real source of latency. Adjustable in Audio Settings |
| NAM as an **OBJECT** library, not STATIC | Each architecture (WaveNet, LSTM, Slimmable…) registers itself through a static initialiser that nothing references: in a static archive, the linker drops them → "No config parser registered for architecture" (bug caught by `RoadieEngineTests`) |
| Defensive splitting in `AmpEngine::process` / `AudioEngine` | Some backends (PipeWire recovering from an xrun) enlarge blocks without `prepareToPlay`; NAM/Convolution would write out of bounds (asserts are off in Release) |
| Camera through direct AVFoundation rather than `juce::CameraDevice` | JUCE only records video, timestamps the take at the time of the *request* rather than the first frame, draws the preview through a native `NSView` (can't draw over it) and has no Linux backend |
| Export through AVFoundation rather than FFmpeg | Hardware encoder, nothing to bundle or compile (no FFmpeg bottle for Intel/macOS 15), crop and title in the same composition |
| Title rendered as a CoreText image, not `CATextLayer` | `CATextLayer` comes out empty through `AVVideoCompositionCoreAnimationTool` on macOS |
| One-page "DAW-style" interface (mockup) rather than the page navigation first considered | The chosen mockup shows everything at once (tracks + contextual inspector): selecting a track changes the inspector and the accent colour |
| Avenir Next (macOS system font) instead of Plus Jakarta Sans / Poppins | Same family (warm geometric grotesque), nothing to bundle and no license to check; simulated tabular figures (`roadie::fonts::drawTabular`) |
| Label uppercasing done by hand (`toUpperFrench`) | `juce::String::toUpperCase()` turns "è" into "Î" on macOS |
| English source strings + a French table (`tr`) | English is the default and the language of the code; a table keyed by the English text keeps call sites readable and falls back to English for anything untranslated. The language is fixed at start-up because most labels are set once in constructors |
| `Looper`, `ToneStack`, `Metronome`, `SyncAnalysis` without JUCE | Testable in a few seconds with a plain compiler |
| Ad hoc signing after the build | macOS refuses the camera to unsigned apps without showing a prompt (Intel binaries aren't signed by the linker) |
| Interface size through `Desktop::setGlobalScaleFactor` | Everything is hand-drawn in logical coordinates: one factor scales everything (menus and tooltips included), the window keeps its on-screen size and shows more timeline when you shrink |
| Hover states: one `HoverFade` per target, driven by `paint`, a single shared timer | ~120 ms fade without a `Timer` per control; the timer only runs while a fade is moving |
| CPU as a word (Low / Normal / High) rather than a percentage | The instantaneous percentage flickered; ~2 s average and a ±3% dead band around the thresholds, the percentage stays in the tooltip |
| Gesture self-test through `ComponentPeer::handleMouseEvent` | Events go through the whole JUCE chain like system ones. Two traps: the peer works in native points (multiply by the interface scale) and JUCE reads ⌥ / ⌘ from `ModifierKeys::currentModifiers`, not from the event |

NAM measurements on this Mac (i5-1038NG7, `RoadieNamBench`, 48 kHz): Fender "Slimmable" captures ~17% of a core at 64 samples (19% at 32, 16% at 256); standard WaveNet ~21% at 64. Noticeably more than on the Linux PC (6-8%), but the 64-sample buffer stays comfortable.
