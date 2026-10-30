# Third-party components

Roadie bundles or compiles the following components.

| Component | License | Where |
|---|---|---|
| [JUCE](https://juce.com) 9.0.4 | AGPLv3 **or** JUCE commercial license | `libs/JUCE` (submodule), linked into the app |
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) v0.6.0 | MIT (© 2023 Steven Atkinson) | `libs/NeuralAmpModelerCore`, compiled into the app |
| [Eigen](https://eigen.tuxfamily.org) | MPL 2.0 | dependency of NeuralAmpModelerCore |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | dependency of NeuralAmpModelerCore |
| [yt-dlp](https://github.com/yt-dlp/yt-dlp) 2026.10.26 | Unlicense (standalone binary: see also the licenses of the Python components it bundles) | `Roadie.app/Contents/Resources/yt-dlp`, launched as a separate process |

## Before distributing

- **Roadie's license: AGPLv3** (see `LICENSE`), compatible with JUCE's AGPLv3. Consequence: anyone who receives the application (including the beta DMG) must be able to obtain the corresponding source code. Include a link to the repository in the beta notes, and keep the repository up to date with what is distributed. Switching later to a JUCE commercial license would allow closing the source (Roadie's license would then have to change, which requires being the sole rights holder).
- **`.nam` captures and impulse responses**: Roadie doesn't bundle them; each capture belongs to its author and has its own terms.
- **yt-dlp**: importing from a YouTube link downloads audio from a service whose terms of service generally don't allow downloading. Keep it for personal use (practising a song you own or have the right to use), and mention it in the beta notes. The bundled version is pinned and verified by SHA-256 fingerprint (see `CMakeLists.txt`); YouTube changes often, so expect regular updates.
