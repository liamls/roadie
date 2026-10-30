# Security

Roadie reads files from the outside (`.nam` captures, impulse responses, audio songs, `.roadie` projects, videos) and launches an external program (yt-dlp) for the YouTube import. Treat these inputs as potentially hostile.

## Reporting a vulnerability

Don't post an exploit, a booby-trapped file or a detailed crash in a public issue.

Use GitHub's private reporting: the **Security › Report a vulnerability** tab of the [liamls/roadie](https://github.com/liamls/roadie/security/advisories/new) repository. Include:

- the version (or commit) and your macOS version;
- the affected component (parsing a `.nam` file, song import, opening a project, export…);
- the impact and the steps to reproduce;
- the crash log, with no personal paths.

The report stays private while it's being fixed; details are only published after the fix.

## Scope

- File reading: `.nam`, `.wav` IRs, MP3 / M4A / WAV / AIFF / FLAC, `project.json`, videos (memory corruption, hangs, excessive resource use).
- YouTube import: the URL is passed to yt-dlp after `--`, never to a shell. Any command injection or write outside the temporary folder is a vulnerability.
- Signing and distribution: an unsigned binary or one signed with the wrong identity, an embedded yt-dlp that doesn't match the fingerprint pinned in `CMakeLists.txt`.

Out of scope: a `.nam` capture that sounds bad, or non-compliance with YouTube's terms of service (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
