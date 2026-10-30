# Changelog

Format inspired by [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions follow `MAJOR.MINOR.PATCH` ([SemVer](https://semver.org/)); `0.x` while the product is in beta.

## [Unreleased]

### Fixed
- **Security**: a `project.json` can no longer point a take, the loop or the song at a file outside the project folder (deleting a take or the song could otherwise trash any file); its numbers (tempo, volumes, regions…) are validated on load. A damaged `project.json` no longer replaces the good `.bak`.
- Importing a song no longer stays "in progress" forever when a project is opened meanwhile, and a failed copy keeps the previous song; re-amping checks the write before replacing the take, and a take deleted meanwhile no longer shifts the result; a discarded take also removes its dry file.
- Clip export: cancelling during the set-up is honoured, a second export request is answered instead of ignored, an audio-insert error is reported.
- Audio: a take stopped before it started no longer moves the playhead later; a diverging capture can't send NaN to the output; oversized `.nam` / WAV files are refused instead of exhausting memory; tempo analysis, tuner and sync guard against zero / invalid sample rates and bars.
- Camera: bounded memory for the take thumbnails, bounds-checked microphone buffer.
- Release workflow: the manual version input no longer goes through the shell script, and the build job is read-only.

## [0.2.0] - 2026-10-30

### Added
- **English / French interface**: English by default, French from View › Language (applied on the next launch). Strings go through `tr ("…")`, translations live in `Source/I18nFrench.cpp`.
- **Autosave** ~3 s after every change (at most 30 s under continuous changes, never during a take or an export).
- **Project backup copy** (`project.json.bak`): reopened automatically if `project.json` is unreadable; distinct error messages.
- **YouTube import**: download progress, precise error messages (private video, unavailable, no connection, timeout).
- **macOS distribution**: `Tools/package_macos.sh` (hardened runtime, notarisation, DMG), yt-dlp pinned by version and SHA-256, `THIRD_PARTY_NOTICES.md`.
- GitHub CI (Linux tests + macOS arm64 / Intel build and tests) and a release workflow.

### Changed
- **The same editing on every track**: the Looper and the song are now made of regions like the guitar. Select a stretch by dragging, delete it (with or without closing the gap), separate it, split with ✂ / `⌘T`, trim, move by the title strip, `⌥`-drag to duplicate — on Guitar, Video, Looper and Song alike. A loop cut in two keeps its phase on each side; right-click an empty lane to place the loop or the song again. Older projects open as before.
- **Looper: 4 bars by default**, and a new loop is one pass long (4 bars) instead of running to the end: pull its right edge, or right-click › Loop to the end, to extend it. “Free” is still in the length menu.
- **Editing is more direct**: click a region and a ✂ appears on the playhead (same as `⌘T`), with a 🗑; dragging over a region's body selects a stretch like text, to delete it (with or without closing the gap) or separate it into a region of its own; a region is now moved by its title strip. The yellow cycle band has a ✕ and a right-click menu to delete it.
- **Library: delete instead of hide**: right-click › Delete… (with a confirmation) moves a capture or a song file to the Trash, or removes a take and its regions. Songs outside `Music/Roadie/Songs` only leave the list.
- The song no longer shows Intro / A / B parts (the cycle zone does the same job); old projects keep opening, the parts are ignored.
- Roadie is released under the **AGPLv3**.
- The documentation is split into `docs/`; the README becomes a landing page.
- Take names are now "Take N" (was "Prise N"), new projects go in `Music/Roadie/Projects` and the songs folder is `Music/Roadie/Songs` (the old `Morceaux` folder is still scanned). Existing projects are untouched.
- Documentation is now in English.

## Before the beta

The earlier history (audio prototype, looper, synced camera, clip export, DAW interface, editing, Play / Arrange workspaces, backing songs) is summarised in the [roadmap](ROADMAP.md).
