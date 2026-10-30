#!/bin/bash
# Builds, signs (hardened runtime), notarises and packs Roadie into a DMG.
#
#   Tools/package_macos.sh                         # local test: ad-hoc signature, no notarisation
#   ROADIE_SIGN_IDENTITY="Developer ID Application: Nom (TEAMID)" \
#   ROADIE_NOTARY_PROFILE=roadie-notary \
#     Tools/package_macos.sh                       # release: signed, notarised, stapled
#
# One-time notarisation setup (stores the credentials in the keychain):
#   xcrun notarytool store-credentials roadie-notary --apple-id <id> --team-id <TEAMID> --password <app-specific password>
#
# Notarisation without a keychain profile (CI): set ROADIE_NOTARY_APPLE_ID, ROADIE_NOTARY_TEAM_ID and
# ROADIE_NOTARY_PASSWORD (app-specific password) instead of ROADIE_NOTARY_PROFILE.
# ROADIE_VERSION_LABEL overrides the version in the file name (e.g. 0.2.0-rc.1).
#
# Output: dist/Roadie-<version>-<arm64|x86_64>.dmg (+ the same name with .sha256)
set -euo pipefail

cd "$(dirname "$0")/.."
IDENTITY=${ROADIE_SIGN_IDENTITY:--}
PROFILE=${ROADIE_NOTARY_PROFILE:-}
NOTARY_ARGS=()
if [[ -n "$PROFILE" ]]; then
    NOTARY_ARGS=(--keychain-profile "$PROFILE")
elif [[ -n "${ROADIE_NOTARY_APPLE_ID:-}" ]]; then
    NOTARY_ARGS=(--apple-id "$ROADIE_NOTARY_APPLE_ID" --team-id "${ROADIE_NOTARY_TEAM_ID:?}" --password "${ROADIE_NOTARY_PASSWORD:?}")
fi
VERSION=$(sed -n 's/^project(Roadie VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
BUILD_DIR=${ROADIE_BUILD_DIR:-build-release}
APP="$BUILD_DIR/Roadie_artefacts/Release/Roadie.app"
DIST=dist
LABEL=${ROADIE_VERSION_LABEL:-$VERSION}
DMG="$DIST/Roadie-$LABEL-$(uname -m).dmg"

if [[ "$IDENTITY" == "-" ]]; then
    echo "==> Ad-hoc signature: for local testing only (Gatekeeper will refuse this DMG on another Mac)."
    [[ ${#NOTARY_ARGS[@]} -eq 0 ]] || { echo "Notarisation needs a Developer ID identity (ROADIE_SIGN_IDENTITY)." >&2; exit 1; }
fi

echo "==> Build Release $VERSION"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DROADIE_CODESIGN_IDENTITY="$IDENTITY"
cmake --build "$BUILD_DIR" --target Roadie -j

echo "==> Signature (hardened runtime)"
# Inside out: the embedded tool first, then the app (no --deep, each part gets its own entitlements).
TIMESTAMP=(--timestamp)
[[ "$IDENTITY" == "-" ]] && TIMESTAMP=(--timestamp=none)
codesign --force --options runtime "${TIMESTAMP[@]}" --entitlements Tools/macos/yt-dlp.entitlements \
    --sign "$IDENTITY" "$APP/Contents/Resources/yt-dlp"
codesign --force --options runtime "${TIMESTAMP[@]}" --entitlements Tools/macos/Roadie.entitlements \
    --sign "$IDENTITY" "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

echo "==> Tests"
cmake --build "$BUILD_DIR" --target RoadieLooperTests RoadieDspTests RoadieEngineTests -j
ctest --test-dir "$BUILD_DIR" --output-on-failure

if [[ ${#NOTARY_ARGS[@]} -gt 0 ]]; then
    echo "==> Notarisation de l'app"
    mkdir -p "$DIST"
    ditto -c -k --keepParent "$APP" "$DIST/Roadie-notarize.zip"
    xcrun notarytool submit "$DIST/Roadie-notarize.zip" "${NOTARY_ARGS[@]}" --wait
    rm -f "$DIST/Roadie-notarize.zip"
    xcrun stapler staple "$APP"
fi

echo "==> DMG"
mkdir -p "$DIST"
STAGE=$(mktemp -d)
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "Roadie $LABEL" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGE"

if [[ "$IDENTITY" != "-" ]]; then
    codesign --force --sign "$IDENTITY" --timestamp "$DMG"
    if [[ ${#NOTARY_ARGS[@]} -gt 0 ]]; then
        echo "==> Notarisation du DMG"
        xcrun notarytool submit "$DMG" "${NOTARY_ARGS[@]}" --wait
        xcrun stapler staple "$DMG"
        spctl --assess --type open --context context:primary-signature -v "$DMG"
    fi
fi

(cd "$DIST" && shasum -a 256 "$(basename "$DMG")" > "$(basename "$DMG").sha256")
echo "==> $DMG"
cat "$DMG.sha256"
