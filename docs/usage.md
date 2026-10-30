# Usage

The full workflow, then every shortcut. The interface is in English by default; View › Language switches to French (applied on the next launch).

**Two workspaces.** **Play** (⌘1) for recording: big camera monitor, Looper pedal and mixer on the right, the timeline as a strip below, the amp chain at the bottom. **Arrange** (⌘2) for editing: the timeline takes over, the monitor and inspector move to the right, the camera is off (indicator off) and comes back with Play. ● from Arrange switches to Play and records as soon as the camera is ready.

1. **Amp**: pick a capture in the Captures tab (scanned folders: `~/Music/Roadie/Captures` and `~/Music`, `+` to add more; or drag and drop a `.nam`). The chain reads left to right: Input (gain, noise gate), Capture, EQ, Cab (`.wav` IR), Output; a block's switch bypasses it.
2. **Song**: drag an MP3, M4A, WAV, AIFF or FLAC onto the window (or ⌘I, or the Songs tab). Roadie copies it into the project, detects its tempo and first bar, sets the project tempo, turns the metronome off and places the first bar on bar 2 (bar 1 is the count-in). Fix it if needed in the Song panel: ×2, ÷2, Tap, ±10 ms, Bar 2. To practise: −5% (down to 50%) and ±1 semitone. Apple Music and Spotify tracks are protected and can't be played.
3. **Looper**: the pedal (or `R`) records, loops, then adds layers; Remove Layer (`U`), Clear (`⇧⌫`). Overdubs are shifted by the latency.
4. **Camera**: Video track → choose the camera → Turn on. **Calibrate** once per audio interface + camera combination: one dry, muted chord.
5. **Take**: ● or `T` after a one-bar count-in (can be turned off in Transport); records the armed tracks (the ● of Guitar and Video). The take is placed where you played it relative to what you heard, and saved to the project immediately.
6. **Arrange**: drag a region anywhere on it (⌘ for free movement, ⌥ duplicates), pull its edges; click a region then ✂ (or `⌘T`) cuts it at the playhead, 🗑 or `⌫` deletes it; ⇧-drag over a region's body to select a stretch like text (on any track, Looper and Song included): `⌫` deletes it, ✂ / `⌘T` separates it into a region of its own to move, right-click offers “Delete and close the gap”; ✕ on the yellow cycle band (or right-click) deletes the cycle; in the library, right-click › Delete… moves a capture or song file to the Trash and removes a take with its regions; dragging the song re-aligns it (on its first bar). Dragging the top of the ruler draws a cycle. `⌘Z` / `⇧⌘Z`.
7. **Export** (`⌘E`): range, format, title, and **with the song** or guitar only → stereo MP4 in `exports/`; or audio only as WAV. The approximate size is shown.

**Layout.** Every boundary between panels can be dragged (double-click: default size); dragged below its minimum, a panel folds away and leaves a handle to reopen it. `Y` library, `B` inspector, `` ` `` enlarges the panel under the mouse (Esc to go back), the bottom of a track header sets its height (⌥⌘↑ / ⌥⌘↓ for all). Each workspace keeps its own layout; View › Reset Layout. Interface size from 80 to 125%.

## Shortcuts

| Action | Shortcut |
|---|---|
| Play / pause | Space |
| Return to start | Return |
| Stop everything | Esc |
| Play / Arrange workspace | ⌘1 / ⌘2 |
| Looper: record → loop → overdub | R |
| Overdub | O |
| Remove layer | U |
| Clear the loop | Shift + Backspace |
| Record / stop a take (audio + camera) | T |
| Metronome | M |
| Cycle zone | C |
| Previous / next bar | ← / → |
| Split the region at the playhead (or separate the selected stretch) | ⌘T |
| Duplicate / delete the selected region | ⌘D / ⌫ |
| Undo / redo an edit | ⌘Z / ⇧⌘Z |
| Zoom out / in | ⌘← / ⌘→ (or pinch, or ⌘ + wheel) |
| Library / Inspector | Y / B |
| Enlarge the panel under the mouse / go back | ` / Esc |
| Taller / shorter tracks | ⌥⌘↑ / ⌥⌘↓ |
| Amp / Looper / Camera / Song panel | 1 / 2 / 3 / 4 |
| Import a song | ⌘I |
| Export | ⌘E |
| New / Open / Save / Save As | ⌘N / ⌘O / ⌘S / ⇧⌘S |
| Audio settings | ⌘, |
| Theme: automatic (system, default) / light / dark | View › Theme; ⇧⌘L toggles light ↔ dark |
| Language: English (default) / French | View › Language (applied on the next launch) |
| Tempo | drag on the Tempo display (Shift = fine), double-click to type |
| Reset a knob, a fader (0 dB), a splitter | Double-click or ⌥-click |
| Fine adjustment of a knob or fader | ⇧ while dragging (and the wheel) |
| Clear a clip indicator | click the meter |
| Interface size (80–125%) | View menu |

With the mouse, in the timeline: drag a region (⌘ for free movement, ⌥ duplicates), pull its edges (↔ cursor) to trim it, right-click for the menu; clicking or dragging in the ruler places the playhead (snapped to the beat, ⌘ for free); dragging in its top band draws, moves or stretches the cycle zone, a click turns it on or off.
