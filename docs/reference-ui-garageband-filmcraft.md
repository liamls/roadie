# UI and simple features reference — GarageBand and filmcraft

A standalone document, reusable in any audio, video or editing application project. It describes:

- **A.** how GarageBand and filmcraft organise their interface;
- **B.** a catalogue of **simple features** (little code, big comfort), each with its reference behaviour;
- **C.** for the record, the more ambitious features spotted;
- **D.** the evidence and how to find it again.

Written on 2026-10-29.

## Sources and limits

| | GarageBand | filmcraft |
|---|---|---|
| Version | 10.4.14 (build 6648), `/Applications/GarageBand.app` | commit `8d925e9` of 2026-10-28, <https://github.com/storytold/filmcraft> |
| Nature | Proprietary Apple application (consumer DAW) | Free Premiere Pro-style video editor, in Rust + egui, MIT / Apache-2.0 license |
| Method | **Static** analysis of the bundle: `Info.plist`, frameworks, interface files (`.nib`), interface texts (`Localizable.strings`, 2,945 labels), help cards (`QuickHelp.plist`, 500 cards), Objective-C class names (2,127). The application was neither launched nor decompiled. | Reading the repository's source code and documentation |
| What we know | What each interface element does, according to the help texts Apple ships | What the code does, and how |
| Reuse | **Ergonomic inspiration only**: copy neither icons, images nor texts | Ideas **and** algorithms reusable with attribution (MIT / Apache-2.0) |

Identifiers in square brackets, like `[TRA_074]`, refer to GarageBand's `QuickHelp.plist` cards; paths like `crates/…` refer to the filmcraft repository (details in part D).

---

## A. Interface organisation

### A1. GarageBand: a single window, collapsible zones around the tracks

```
┌ Control bar ────────────────────────────────────────────────────────────────────────────────┐
│ Library · Smart Controls · Editors       │ ⏮ ⏪ ⏩ ■ ▶ ● ⟲ │ LCD display │ Tuner · Count-in ·   │
│                                          │                 │             │ Metronome · Volume   │
│                                          │                 │             │ Note Pad · Loops · Help│
├ Library ─────┬ Global tracks (Arrangement, Movie, Tempo, Transposition, Master) ──┬ Loops ───┤
│ (sounds/     │ Track     │ Ruler: yellow cycle, locators, playhead                │ Media    │
│  patches)    │ headers   │ Regions (audio, MIDI, Drummer, take folders)           │ Notes    │
├──────────────┴───────────┴ Smart Controls  or  Editor (audio, piano roll…) ───────┴──────────┤
```

**Control bar** (keys `StrTransportBtns` and `StrViewBtns`)

- **On the left**, the zone switches: Library, Smart Controls, Editors.
- **In the centre**, the transport: start, back one bar, forward one bar, stop, play, record, cycle.
- **The LCD display** shows the position (bars.beats or hours:min:s), tempo, key and time signature. Each value is changed by **dragging it vertically**, or by **double-clicking to type it** `[TRA_019, TRA_020, TRA_028, TRA_030]`. A menu switches the display **and** the ruler between bars and time `[TRA_071]`.
- **Right after**, the playing tools: tuner, count-in, metronome, master volume.
- **On the right**, the other zones: Note Pad, Loop Browser, Media Browser, Quick Help.

**Global tracks** (above the tracks, each can be shown or hidden) `[GMT_005 to GMT_009]`

- Arrangement: section markers (Intro, Verse…).
- Movie: thumbnails aligned to the ruler.
- Transposition and Tempo.
- Master: the master volume.

**Track header**, whose displayed elements you choose (`TrackHeaderConfig.nib`, `[GMT_010]`)

- Mute, solo, record arm, input monitoring, lock, volume, pan, "groove track".
- Double-click the name to rename `[TRL_054]`, ctrl-click the icon to change it `[TRL_055]`.

**Ruler** `[COM_006 to COM_015]`

- Divisions in bars or beats. A click in the **bottom** of the ruler places the playhead.
- The **cycle zone** is a yellow band at the top: drag it to move it and pull its edges to resize it. Its two edges are the left and right **locators**.
- A **project end marker** moves back or forward by itself while recording.

**Regions**

- Types: audio, MIDI, Drummer, **take folders** (a number at the top left picks the take) `[ARR_050]`.
- A "looped" part is drawn differently from the original region `[ARR_059 to ARR_062]`.
- The pointer changes with the hovered zone: resize (edge), **loop** (top right corner, drag to the right), **join** (between two adjacent regions, moves the transition) `[TOO_029 to TOO_031]`.

**Bottom zone: Smart Controls** `[SMA_001 to SMA_037]`

- The track sound's main controls, as big knobs.
- A **Track** button and a **Master** button. The Master has three sections: Effects, Output, EQ.
- A **Compare** button plays the saved setting, a **Bypass** button disables the effect, and a menu loads or saves settings.
- A "plug-ins" pane on the side to add, remove or open effects, with the recording level and the input choice.
- For a guitar track: Amp Designer, Pedalboard, Tuner, Noise Gate, feedback protection, automatic level.

**Library** (left) `[LIB_001 to LIB_028]`

- Categories, then the list of sounds, with a search that keeps recent searches.
- Save the current sound as a new sound; delete a sound (irreversible, the help says so).
- A sound not yet downloaded is greyed out, with an arrow to download it.

**Loop Browser** (right) `[LPB_*]`

- Two views: keyword columns, or keyword buttons you combine. A Reset button and favourites.
- Preferences: show only loops close to the project's key (within ±2 semitones) `[PFS_FilterResultsCheckbox]`, and show each loop's original tempo and key.

**Quick Help** `[GMH_*, TRA_069]`, classes `MAQuickHelp*`

- Hovering an element shows its name and what it does.
- Two presentations to choose from: **balloons** near the pointer, or a movable **floating window**.
- "Coaching tips" per major screen area.

**Preferences** (one interface file per tab)

- General, Audio, Metronome (click volume and tone), Loops, Advanced, My Info (composer, artist, album, used when sharing).

### A2. filmcraft: panels you dock wherever you want, and workspaces

**38 pt header bar** (`header.rs`)

- On the left: Home and the **Import / Edit / Export** modes.
- In the centre: the document title.
- On the right: the workspace name and quick actions.
- On macOS, menus are in the native menu bar.

**Workspaces and panels** (`dock.rs`)

- 28 panels you dock as tabs or leave floating.
- 9 ready-made workspaces: Editing, Assembly, Color, Effects, Audio, Captions and Graphics, Learning, Review, All Panels.
- Workspaces menu (`panels/workspaces.rs`): go back to the saved layout, save changes, save as a new workspace, rename or delete.
- A built-in workspace can be modified then reset, but not deleted.
- Layouts are in `workspaces.json`, next to the preferences.

**Timeline** (`panels/timeline.rs`)

- A **big timecode** of the playhead at the top left, with switches beside it.
- The ruler carries markers, in and out points and a **render bar** (green, yellow or red).
- Video tracks are above (V1 at the bottom), audio tracks below (A1 at the top).
- Headers are on the left, and a scroll bar at the bottom also serves as zoom.
- **Zoom and scrolling are animated** (exponential damping) and **anchored under the pointer**.
- Each gesture produces **a single command**: it undoes in one go and can be replayed.
- Snapping at **9 px** (`const SNAP_PX`): on cuts, the playhead, markers and in and out points. The dragged playhead doesn't snap to itself, otherwise it would "stick" in place.

**Track header** (`timeline/headers.rs`)

- Target, source patch, sync lock, lock, mute, solo, enabled or not, colour.

**Source and Program monitors** (`panels/monitor.rs`, `panels/monitor_view.rs`)

- The picture is decoded at the **smaller** of two resolutions: the playback one and the screen's.
- During a scrub, the nearest cached frame is shown while waiting for the right one, so **no black flash**.
- Display options: magnification from 10 to 1600%, guides pulled from the rulers, snapping to guides, edges and centre, checkerboard for transparency.

**Tools panel** (`panels/tools.rs`)

- A column of tools. Grouped tools carry a small triangle; right-click, long press or press-and-drag opens the group.

**Export mode** (`panels/export_mode.rs`), in three columns

1. The destinations and the queue.
2. The settings: name, folder, preset, format, then Video, Audio, etc. sections.
3. The preview, the range, the scaling, a **summary with the estimated file size**, and the "Send to queue" and "Export" buttons.

**Home / Import** (`panels/import_mode.rs`)

- Big cards to import media, and the recent projects.

**Useful small panels**

- **History** and **Markers** (`panels/misc.rs`).
- **Events**: warnings and errors, filterable (`panels/events.rs`).
- **Progress**: background tasks, with a Cancel button.
- **Timecode**: one or several lines of your choice, like position, duration or remaining time (`panels/timecode.rs`).
- **Project notes**, stored in the project (`panels/notes.rs`).
- **Media browser**: tree and list or thumbnails, with **hover scrub** (`panels/media_browser.rs`).

**Settings** (`panels/settings.rs`)

- Categories on the left, fields on the right, and at the bottom Help, Reset, Cancel, OK.
- Changes go into a **draft**, applied only on OK.

**Keyboard shortcuts** (`panels/shortcuts_dialog.rs`, ⌥⌘K)

- A drawn keyboard where assigned keys are coloured, and a list of commands with search. You click a cell then press the keys.
- Conflicts are flagged. Presets can be managed, imported and exported.
- Changes apply immediately; Cancel restores the original state.

**Technical foundation of the interface**

- **Design tokens**: every widget reads its colours and sizes from `Tokens`, so a theme applies everywhere (`theme.rs`).
- **Translation**: the texts in the code stay in English and act as keys; one `.tsv` catalogue per language; an untranslated text shows in English (`i18n.rs`).

### A3. Shared principles, to take over as they are

1. **A temporal centre** (tracks and ruler) **surrounded by collapsible zones**: a source on the left, contextual settings at the bottom or right, a library, each with its button and shortcut.
2. **A contextual inspector**: what is selected decides what the settings zone shows.
3. **Every action is a command** with a label, a shortcut, a menu and an undo step. You get "Undo X" labels, the history, editable shortcuts and scripting for free.
4. **Directly manipulable numeric values**: drag vertically to change, double-click to type, double-click a knob to return to the default value.
5. **Colours have a fixed meaning**: yellow for the cycle and alignment guides, red for recording, and one colour per track type.
6. **Feedback that doesn't flicker**: the best available picture rather than a black screen, and smoothed values.
7. **Help where you're looking**: quick help on hover, rather than a manual.

---

## B. Catalogue of simple features

Each entry gives the **reference behaviour** and the **source**. All are "small": one component, one option, or one more command on an existing data model.

### B1. Transport and time

| Feature | Reference behaviour | Source |
|---|---|---|
| Count-in before recording | None, 1 bar or 2 bars, with a switch in the bar; option "metronome only during count-in" | GB `[TRA_074, GMR_001 to GMR_003]`, "… only During Count-In" |
| Visual count-in | A big on-screen countdown, plus a **pre-roll** (playing before the start) and a **post-roll** (after the end) | FC `panels/voiceover.rs` |
| Stop that returns to the start | Stop while stopped returns the playhead to the start, or to the left locator if the cycle is on; Play starts from the left locator in cycle mode | GB `[TRA_014, TRA_015]` |
| Back or forward one bar | One button or key per bar | GB `[TRA_012, TRA_013]` |
| Bars ↔ time display | A menu switches the display **and** the ruler at once | GB `[TRA_071]` |
| Playhead follow (Catch) | A switch: the view follows the playhead during playback and recording | GB `[COM_003]` |
| Project end | An end marker that grows while recording, used as the default range | GB `[COM_014]` |
| Adjustable metronome | Click volume and tone in the preferences | GB `[PFS_MetronomeVolumeSlider, PFS_MetronomeToneSlider]` |
| Stepped locators | Place the left or right locator at the rounded playhead position | GB "Set Left/Right Locator by Rounded Playhead" |
| Timecode panel | Several lines: position, duration, in-out duration, remaining time | FC `panels/timecode.rs` |

### B2. Recording and input

| Feature | Reference behaviour | Source |
|---|---|---|
| Tuner | Button in the bar; Standard, Drop D, Open D, whole step down tunings | GB `[TRA_073, SMA_019]`, key `LearnToPlayTunerStrings` |
| Input monitoring | You hear the input of a **non-armed** track, to set levels or rehearse | GB `[INS_023]` |
| Feedback protection | If feedback appears, monitoring is **cut immediately**, with a message; monitoring "without protection" remains possible | GB `[SMA_025]`, `GBFeedbackProtectionPanel.nib` |
| Automatic level | Lowers the level to avoid feedback, raises it if too low | GB `[SMA_017]` |
| Noise gate | Simple settings to reduce background noise | GB `[SMA_020]` |
| Input level and source | A level slider; a mono ↔ stereo button then a source menu | GB `[SMA_016, SMA_018]` |
| Recording resolution | A checkbox (24-bit) that sets the resolution of new files | GB `[PFS_24BitCheckbox]` |
| Microphone access denied | An "Allow access" button that opens the right System Settings pane directly | GB `[PFS_AllowAccessButton]` |

### B3. Region editing

| Feature | Reference behaviour | Source |
|---|---|---|
| Split at the playhead | Splits the selected regions at the playhead position | GB `[LMT_010]`, FC "add edit" ⌘K |
| Join | Merges contiguous regions; a message if nothing can be joined | GB `[LMT_011]`, "no joinable Regions are selected" |
| Trim to the playhead | Brings the region's start or end to the playhead | GB "Trim Region Start/End to Playhead Position"; FC `Q` / `W` |
| Delete closing the gap | Deletes the selection and moves the following regions back | GB `[LMT_008]`, FC "ripple delete" |
| Paste replacing | Pastes at the playhead, replacing what is there | GB `[LMT_006]` |
| Toggleable snapping | A "Snap to Grid" switch; a 9 px threshold in filmcraft | GB `[LMT_012]`, FC `SNAP_PX` |
| Alignment guides | During a move, a yellow line appears when an edge aligns with another region | GB `[LMT_013]` |
| Loop a region | Top right corner, "loop" pointer, drag to the right; the looped part is drawn differently | GB `[TOO_030, ARR_059]` |
| Reverse playback | A checkbox per region | GB `[AUD_21]` |
| Rename | Rename a region or a track (double-click the name) | GB `[CMT_001, TRL_054]` |
| Lock a track | A locked track can no longer be edited or recorded | GB `[TRL_009]`, FC `timeline.track.*.locked` |
| Track colour or icon | Changed by right-click (or ctrl-click) | GB `[TRL_055, GMT_038]`, FC `timeline.track.*.color` |
| Track with duplicated settings | New track with the selected track's settings | GB `[GMT_002]` |

### B4. Navigation and display

| Feature | Reference behaviour | Source |
|---|---|---|
| Animated zoom under the pointer | Damped animation, and the point under the pointer stays fixed | FC `panels/timeline.rs` |
| Horizontal zoom slider, vertical zoom | A slider in the corner, plus vertical zoom of the tracks | GB `[COM_018]`, "Vertical Zoom" |
| Waveform zoom | Enlarge the waveforms' amplitude without changing the volume | GB "Waveform Zoom" |
| Show or hide each zone | One command per zone (library, editor, automation…), each with a shortcut | GB: 22 "Show/Hide …" commands |
| Configure the header | Choose the commands visible in track headers | GB `[GMT_010]` |
| Video thumbnail resolution | High (default) or Low, for modest machines | GB `[PFS_MovieResolutionButtons]` |
| Hover scrub | Scrub through a thumbnail by hovering across its width | FC `panels/media_browser.rs`, `project_views.rs` |
| Sound while scrubbing | A short fragment of sound plays at each playhead move; a single audio stream is opened for the whole gesture | FC `scrub.rs` |
| No black flash | During a scrub, show the nearest cached frame while waiting for the right one | FC `panels/monitor.rs` |
| Maximised panel | One key enlarges the panel under the pointer, the same restores it | FC `panels/keyboard.rs` |

### B5. Mixing and output

| Feature | Reference behaviour | Source |
|---|---|---|
| Automatic fade out | A command creates a fade out through master automation | GB `[GMM_002]` |
| Normalised export | Export at maximum level without distortion (0 dBFS) | GB `[PFS_AutoNormalizeCheckbox]` |
| Loudness measurement | BS.1770-4 / EBU R128: momentary (400 ms), short-term (3 s), integrated, true peak | FC `crates/audio-dsp/src/loudness.rs` |
| Held peaks | Meters in dB with peak hold | FC `panels/meters.rs` |
| Mute, solo, solo safe | Solo Safe (meaning inferred from the name): a track that stays audible despite a solo elsewhere | GB `[TRL_060, TRL_061]` |
| Export presets | Groups (Source, Web & social, Broadcast…); "Vertical 1080×1920, fill, 12 Mbps"; favourites; custom presets, built-in names being reserved | FC `crates/export/src/presets.rs` |
| Estimated size | The export summary shows the file size before rendering | FC `panels/export_mode.rs` |
| Background export | Progress and Cancel, you keep working during the render | FC `panels/events.rs` (Progress panel) |
| Metadata | Composer, artist, album, entered once in the preferences | GB `[PFS_MyInfo_01 to 04]` |

### B6. Project and robustness

| Feature | Reference behaviour | Source |
|---|---|---|
| Undo labels | "Undo Move Region" rather than "Undo" | GB "Undo %@" |
| History panel | First line "Opening" (the state at open), then one line per step; steps to redo are greyed out; clicking a line goes back to it; a menu offers "Delete redo steps" and "Clear history" | FC `panels/misc.rs` |
| Project notes | Free text saved **in** the project; a typing session = one undo step | GB Note Pad `[TRA_060]`, FC `panels/notes.rs` |
| Versioned autosave | Every 5 min if something changed; keeps the last 20 timestamped copies | FC `docs/project-files.md` |
| Recovery journal | Snapshot written about 1 s after the last edit (at most every 5 s); on restart after a crash, offer **Recover / Later / Ignore** | FC `docs/project-files.md` ("Crash recovery") |
| Atomic writes | Project, autosaves and preferences: write to a hidden temporary file in the **same folder**, `fsync`, rename over the target, then `fsync` the folder | FC `docs/project-files.md` ("Saving is atomic") |
| Crash log | Thread, location and call stack written to a file; the last crash is kept for an in-app error window | FC `crash.rs` |
| Events panel | Warnings and errors, filterable by level | FC `panels/events.rs` |
| Reset alerts | A button that shows again the alerts hidden by "Don't show again" | GB `[PFS_ResetWarningsButton]` |
| Home | Recent projects, and big "new" or "import" cards | FC `panels/import_mode.rs` |

### B7. Help and customisation

| Feature | Reference behaviour | Source |
|---|---|---|
| Quick help | Balloons at the pointer, or a floating window; one card per element (title + one sentence) | GB `[GMH_001 to GMH_009]`, `QuickHelp.plist` |
| Shortcut editor | Drawn keyboard, search, flagged conflicts, presets, import and export | FC `panels/shortcuts_dialog.rs` |
| Workspaces | Named layouts, saveable and resettable | FC `panels/workspaces.rs` |
| Preferences with a draft | Changes only apply on OK; Reset per category | FC `panels/settings.rs` |
| Design tokens | All colours and sizes in a single theme object | FC `theme.rs` |
| Progressive translation | The English text acts as the key; one `.tsv` catalogue per language; fallback to English | FC `i18n.rs` |

---

## C. More ambitious (for the record)

| Feature | Source |
|---|---|
| Take folders and comping the best passages, cycle recording that stacks takes | GB `[ARR_050, ARR_053]`, classes `NcMultiTakeFolderManager*` |
| Automatic recording over a selected range | GB "Autopunch Range" |
| Arrangement markers you reorder (8 bars by default) | GB `[GLO_026, GLO_028]` |
| Flex Time and Flex Pitch, audio quantisation, groove track | GB `[TRL_023 to TRL_053, AUD_004 to AUD_009]` |
| Automation in Read / Latch / Touch / Write | GB `[TRL_042]`, FC README |
| Syncing recordings by their sound (two-pass GCC-PHAT correlation) | FC `crates/audio-dsp/src/sync.rs` |
| Multicam: angle grid, keys 1 to 9 to cut | FC `panels/multicam.rs` |
| Remix: refit a piece of music to a target duration by cutting on beats | FC `crates/audio-dsp/src/remix.rs` |
| Automatic ducking of the music under the voice | FC `crates/audio-dsp/src/ducking.rs` |
| Burned-in SRT, WebVTT, SCC subtitles; Whisper transcription | FC `crates/captions`, `crates/speech` |
| Shot-change detection in a video | GB class `MAMovieSceneCutDetector` |
| Project interchange: FCPXML, FCP7 XML, OTIO, EDL, AAF | GB classes `MAFinalCutXML*`, FC `crates/interchange` |
| Driving everything by commands: more than 650 commands, JSON channel, MCP, an automation identifier per widget | FC `docs/control-protocol.md`, README |

---

## D. Evidence and how to find it again

### GarageBand

- **Identity**: `Info.plist` read with REA (evidence `ev_3da38ccd51d77696b1b2085a72ddd70252076ac0392c29022cca792ec5b3debb`). It shows `com.apple.garageband10`, version 10.4.14 (6648), `.band` documents, the main class `CLgApplication` and the microphone, Photos and motion permissions.
- **Telling frameworks**: `MAVideo` (playback, thumbnails, shot detection, FCPXML), `MAMixer`, `MALoopBrowser`, `MAPlaySurface`, `MAHarmony`, `MAMachineLearning`, `MAGenerativeInstruments` (Session Players), `MAWorkspace` (Smart Controls rendering).
- **Interface files** in `Resources/Base.lproj` and `Resources/en.lproj`: `MovieWindow`, `TrackHeaderConfig`, `GBTrackInspector`, `SmartControlsInspector`, `Library`, `GBFeedbackProtectionPanel`, `Prefs{Global,Audio,Metronome,Loops,Advanced,MyInfo}`, `TimeSignatureCustom`, `ExportSongToDiskAccessoryView`.
- **Classes from the symbol table**: `LgTunerWindowObserver`, `GBFeedbackProtectionPanel`, `RegionMultiTakeFolderManager`, `MovieWindowController`, `MAQuickHelpViewInBalloonWindow`, `CLgSmartControlsInspectorView`.

To reproduce everything (read-only, run from a working folder):

```bash
R=/Applications/GarageBand.app/Contents/Resources/en.lproj
plutil -convert json -o quickhelp.json "$R/QuickHelp.plist"          # 500 help cards: title + text per element
plutil -convert json -o strings.json "$R/Localizable.strings"        # 2,945 labels; StrTransportBtns|||…, StrViewBtns|||…
ls /Applications/GarageBand.app/Contents/Resources/Base.lproj        # interface files (.nib) per screen
nm -arch x86_64 /Applications/GarageBand.app/Contents/MacOS/GarageBand | grep -o 'OBJC_CLASS_\$_[A-Za-z0-9_]*' | sort -u
```

Help card prefixes:
- `TRA`: transport bar; `TRL`: track headers; `COM`: ruler and zoom; `ARR`: regions.
- `SMA`: Smart Controls; `INS`: channel strip; `LIB`: library; `LPB`: Loop Browser.
- `LMT`: editing commands; `TOO`: pointers; `GLO`: global tracks; `PFS`: preferences.
- `GM*`: menus (`F` File, `E` Edit, `T` Track, `M` Mix, `R` Record, `S` Share, `V` View, `H` Help).

### filmcraft

```bash
git clone --depth 1 https://github.com/storytold/filmcraft.git
```

- **Interface**: `crates/ui-egui/src/` (each file starts with an explanation in `//!`), including `dock.rs` (panels and workspaces), `header.rs`, `panels/*.rs`, `theme.rs` and `i18n.rs`.
- **Audio processing**: `crates/audio-dsp/src/` (`loudness.rs`, `sync.rs`, `ducking.rs`, `remix.rs`).
- **Project and saves**: `docs/project-files.md`.
- **Architecture and commands**: `docs/architecture.md`, `docs/control-protocol.md`.
