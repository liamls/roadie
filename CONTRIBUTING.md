# Contributing to Roadie

Thanks for your interest. Roadie is a young project (beta): feedback from a guitarist who uses it is worth as much as code.

## Before you start

- **Report a bug or suggest an idea**: open an [issue](https://github.com/liamls/roadie/issues/new/choose). For a bug, include your version, your machine, your audio interface and your camera.
- **Security vulnerability**: don't open it in public, see [SECURITY.md](SECURITY.md).
- **License**: every contribution is released under the [AGPLv3](LICENSE), like the rest of the project.

## Building and testing

```bash
git clone --recursive https://github.com/liamls/roadie.git && cd roadie
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Roadie RoadieLooperTests RoadieDspTests RoadieEngineTests -j
ctest --test-dir build --output-on-failure
```

Always in `Release` (in Debug, the NAM engine saturates the CPU). Details and UI verification tools (`--snapshot`, `--selftest-edit`): [docs/building.md](docs/building.md).

## Repository rules

- **The audio thread never blocks**: no allocation, blocking lock or I/O in the callback. Exchanges with the interface go through FIFOs, atomics or `TryLock`s.
- **A test for every behaviour**: JUCE-free logic (looper, DSP, pure rules) goes in `Tests/LooperTests.cpp` or `Tests/DspTests.cpp` (a few seconds, no dependencies); anything touching the real-time chain, files or export goes in `Tests/EngineTests.cpp`.
- **The interface only talks to `Session`**: every arrangement edit goes through `applyEdit` (undoable). See [docs/architecture.md](docs/architecture.md).
- **Every visible string is English and goes through `tr ("…")`** (`Source/I18n.h`). The French translation lives in `Source/I18nFrench.cpp`, keyed by the exact English text: add the pair there when you add a string. Uppercase labels with `toUpperFrench`, never `toUpperCase()` (it mangles accents on macOS).
- **Documentation and code comments are in English.**
- **Style**: follow the neighbouring code (naming, comment density). Comments explain the *why*.
- **Check it by eye**: for an interface change, attach a screenshot (`Roadie.app --snapshot=/tmp/ui.png --demo`), light and dark if the theme is involved.

## Commits and pull requests

- Message: `<gitmoji>(<scope>) <summary in the imperative or past tense>`, for example `🐛(timeline) the playhead no longer jumps on zoom`. Common gitmojis: ✨ feature, 🐛 fix, ♻️ refactor, 🧪 tests, 📝 docs, 🔧 config, 🚀 release, ⬆️ dependencies.
- One PR = one topic. Fill in the PR template; CI must be green (`CI OK`). PRs are merged with **squash**.
- No binaries or `.nam` / IR captures in the repository (each capture belongs to its author).

## Releasing (maintainers)

[docs/releasing.md](docs/releasing.md).
