# Releasing

CI checks every PR; a release is triggered by pushing a tag. Nothing is signed or notarised without a certificate: the release says so in its notes.

## In short

1. On `main`, with CI green. Update `CHANGELOG.md` (rename "Unreleased" to the version) and the version in `CMakeLists.txt` (`project(Roadie VERSION X.Y.Z …)`, no suffix).
2. Tag and push:
   ```bash
   git tag v0.2.0 && git push origin v0.2.0
   ```
   Tags are plain `vX.Y.Z`. While the major version is `0`, the release is marked as a **pre-release** on GitHub (beta); from `1.0.0` it is a normal release.
3. The **Release** workflow builds one DMG per architecture (`arm64` and `x86_64`), signs, notarises if the secrets exist, and publishes the release with generated notes, the DMGs and their `.sha256` files.

The workflow refuses to publish if the tag doesn't match the version in `CMakeLists.txt` (a `-rc.N` style suffix is ignored in the comparison, which allows dry runs).

Dry run without publishing anything: Actions › Release › *Run workflow*, with a version label (`0.2.0-rc.1`). The DMGs can then be downloaded as artifacts of the run.

## GitHub secrets (Settings › Secrets and variables › Actions)

| Secret | Contents |
|---|---|
| `APPLE_CERTIFICATE` | The "Developer ID Application" certificate exported as `.p12`, encoded: `base64 -i certificate.p12 \| pbcopy` |
| `APPLE_CERTIFICATE_PASSWORD` | The password chosen when exporting the `.p12` |
| `KEYCHAIN_PASSWORD` | Any password: it protects the runner's temporary keychain |
| `APPLE_ID` | The Apple ID of the developer account |
| `APPLE_PASSWORD` | An [app-specific password](https://support.apple.com/102654) (not the account password) |
| `APPLE_TEAM_ID` | The team ID (10 characters, developer.apple.com › Membership) |

Without `APPLE_CERTIFICATE`, the DMGs are ad hoc signed: they install for testing (right-click › Open), but macOS rejects them by default on another Mac.

## Doing the same locally

```bash
Tools/package_macos.sh        # local test: ad hoc signature, DMG in dist/ (rejected by Gatekeeper on another Mac)
ROADIE_SIGN_IDENTITY="Developer ID Application: Name (TEAMID)" ROADIE_NOTARY_PROFILE=roadie-notary Tools/package_macos.sh
```

Prerequisites for the signed version: a "Developer ID Application" certificate in the keychain, and a notarisation profile created once with `xcrun notarytool store-credentials roadie-notary --apple-id … --team-id … --password <app-specific password>`. The script builds Release in `build-release/` (`ROADIE_BUILD_DIR` to reuse another), signs yt-dlp then the app (hardened runtime), runs the tests, notarises and staples the app then the DMG, and writes `dist/Roadie-<version>-<arch>.dmg` with its `.sha256`. The version comes from `project(Roadie VERSION …)` in `CMakeLists.txt`.

yt-dlp is **pinned** (version and SHA-256 in `CMakeLists.txt`): to update it, change both using the `yt-dlp_macos` line of the release's `SHA2-256SUMS`, then delete `libs/yt-dlp/yt-dlp`. It has its own entitlements (`Tools/macos/yt-dlp.entitlements`) because it's a PyInstaller executable. Component licenses and caveats: `THIRD_PARTY_NOTICES.md`.
