#!/usr/bin/env bash
# Package Verslicer.app into a distributable .dmg for other Macs.
#
# Without Apple Developer ID + notarization, macOS may still show
# "unidentified developer" — users can Right-click → Open once.
# Unsigned/partially-signed builds often show "damaged" on other machines;
# this script applies a proper deep signature and optional notarization.
#
# Environment (optional):
#   APPLE_SIGNING_IDENTITY  — e.g. "Developer ID Application: Your Name (TEAMID)"
#   APPLE_ID                — Apple ID email (for notarization)
#   APPLE_TEAM_ID           — Team ID
#   APPLE_APP_PASSWORD      — app-specific password for notarytool
#   SKIP_NOTARIZE=1         — skip notarization even if credentials are set
#
# Usage:
#   ./scripts/package_macos_dmg.sh [path/to/Verslicer.app] [output.dmg]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ENTITLEMENTS="${PROJECT_DIR}/scripts/disable_validation.entitlements"
ARCH="${ARCH:-arm64}"

DEFAULT_APP="${PROJECT_DIR}/build/${ARCH}/OrcaSlicer/Verslicer.app"
APP_PATH="${1:-${DEFAULT_APP}}"
OUT_DMG="${2:-}"

if [ ! -d "${APP_PATH}" ]; then
    echo "error: app bundle not found: ${APP_PATH}" >&2
    echo "Build first: ./build_release_macos.sh -s -x" >&2
    exit 1
fi

APP_NAME="$(basename "${APP_PATH}" .app)"
VERSION="$(grep -E '^set\(SoftFever_VERSION' "${PROJECT_DIR}/version.inc" 2>/dev/null | sed -E 's/.*"([^"]+)".*/\1/' || echo "0.0.0")"
OUT_DIR="$(cd "$(dirname "${APP_PATH}")" && pwd)"
if [ -z "${OUT_DMG}" ]; then
    OUT_DMG="${OUT_DIR}/${APP_NAME}-macOS-${ARCH}-${VERSION}.dmg"
fi

STAGING="${OUT_DIR}/.dmg_staging_$$"
DMG_TMP="${OUT_DIR}/.dmg_tmp_$$.dmg"

cleanup() {
    rm -rf "${STAGING}" "${DMG_TMP}" 2>/dev/null || true
}
trap cleanup EXIT

resolve_signing_identity() {
    if [ -n "${APPLE_SIGNING_IDENTITY:-}" ]; then
        echo "${APPLE_SIGNING_IDENTITY}"
        return
    fi
    local found
    found="$(security find-identity -v -p codesigning 2>/dev/null \
        | grep 'Developer ID Application' \
        | head -1 \
        | sed -E 's/^[[:space:]]*[0-9]+[[:space:]]+([0-9A-F]+)[[:space:]]+"(.+)"$/\2/' || true)"
    if [ -n "${found}" ]; then
        echo "${found}"
        return
    fi
    echo "-"
}

check_bundle_portable() {
    echo "Checking embedded library paths..."
    local bad=0
    while IFS= read -r -d '' f; do
        if ! file -b "${f}" | grep -q 'Mach-O'; then
            continue
        fi
        while IFS= read -r dep; do
            case "${dep}" in
                @*|/usr/lib/*|/System/*|/Library/Apple/*) continue ;;
            esac
            if [[ "${dep}" == /opt/* || "${dep}" == /Users/* || "${dep}" == *homebrew* ]]; then
                echo "  non-portable: ${f#${APP_PATH}/} -> ${dep}"
                bad=1
            fi
        done < <(otool -L "${f}" 2>/dev/null | tail -n +2 | awk '{print $1}')
    done < <(find "${APP_PATH}/Contents" -type f -print0)

    if [ "${bad}" -ne 0 ]; then
        echo "error: bundle links to machine-specific libraries; rebuild with bundled deps." >&2
        exit 1
    fi
    echo "  OK (no Homebrew/build-machine paths in Mach-O binaries)"
}

sign_file() {
    local target="$1"
    local identity="$2"
    local args=(--force --options runtime)
    if [ -f "${ENTITLEMENTS}" ]; then
        args+=(--entitlements "${ENTITLEMENTS}")
    fi
    if [ "${identity}" != "-" ]; then
        args+=(--timestamp)
    fi
    args+=(--sign "${identity}" "${target}")
    codesign "${args[@]}" 2>/dev/null || codesign --force --sign "${identity}" "${target}"
}

sign_app_bundle() {
    local app="$1"
    local identity="$2"

    echo "Signing ${app} (identity: ${identity})..."

    local sign_args=(--force --options runtime)
    if [ -f "${ENTITLEMENTS}" ]; then
        sign_args+=(--entitlements "${ENTITLEMENTS}")
    fi
    if [ "${identity}" != "-" ]; then
        sign_args+=(--timestamp)
        # Developer ID: sign nested Mach-O binaries before the bundle.
        while IFS= read -r -d '' f; do
            sign_file "${f}" "${identity}"
        done < <(find "${app}/Contents" -type f -print0 | while IFS= read -r -d '' f; do
            if file -b "${f}" | grep -q 'Mach-O'; then
                printf '%s\0' "${f}"
            fi
        done)
        sign_args+=(--deep)
    else
        sign_args+=(--deep)
    fi
    sign_args+=(--sign "${identity}" "${app}")
    codesign "${sign_args[@]}"

    codesign --verify --deep --strict --verbose=2 "${app}"
    echo "  codesign verify: OK"
}

strip_attributes() {
    local path="$1"
    xattr -cr "${path}" 2>/dev/null || true
}

notarize_dmg() {
    local dmg="$1"
  if [ "${SKIP_NOTARIZE:-}" = "1" ]; then
        return 0
    fi
    if [ -z "${APPLE_ID:-}" ] || [ -z "${APPLE_TEAM_ID:-}" ] || [ -z "${APPLE_APP_PASSWORD:-}" ]; then
        echo "Notarization skipped (set APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD to enable)."
        return 0
    fi
    if [ "${SIGN_ID}" = "-" ]; then
        echo "Notarization requires a Developer ID certificate (not ad-hoc)."
        return 0
    fi

    echo "Submitting to Apple notarization (may take several minutes)..."
    xcrun notarytool submit "${dmg}" \
        --apple-id "${APPLE_ID}" \
        --team-id "${APPLE_TEAM_ID}" \
        --password "${APPLE_APP_PASSWORD}" \
        --wait

    xcrun stapler staple "${dmg}"
    xcrun stapler validate "${dmg}"
    echo "  notarization + staple: OK"
}

SIGN_ID="$(resolve_signing_identity)"
check_bundle_portable

echo "Preparing staging folder..."
rm -rf "${STAGING}"
mkdir -p "${STAGING}"
ditto --noqtn --noextattr "${APP_PATH}" "${STAGING}/${APP_NAME}.app"
strip_attributes "${STAGING}/${APP_NAME}.app"

sign_app_bundle "${STAGING}/${APP_NAME}.app" "${SIGN_ID}"

ln -sfn /Applications "${STAGING}/Applications"

echo "Creating disk image..."
rm -f "${OUT_DMG}" "${DMG_TMP}"
hdiutil create \
    -volname "${APP_NAME}" \
    -srcfolder "${STAGING}" \
    -ov \
    -format UDRW \
    "${DMG_TMP}" >/dev/null

# Compress to read-only UDZO (standard distribution format).
hdiutil convert "${DMG_TMP}" -format UDZO -o "${OUT_DMG}" >/dev/null
strip_attributes "${OUT_DMG}"

if [ "${SIGN_ID}" != "-" ]; then
    echo "Signing disk image..."
    codesign --force --sign "${SIGN_ID}" "${OUT_DMG}"
fi

notarize_dmg "${OUT_DMG}"

echo ""
echo "Done: ${OUT_DMG}"
ls -lah "${OUT_DMG}"

if [ "${SIGN_ID}" = "-" ]; then
    echo ""
    echo "NOTE: Ad-hoc signed — other Macs may block the app."
    echo "  • Best fix: Apple Developer ID + notarization (set env vars above)."
    echo "  • One-time workaround on the other Mac:"
    echo "      xattr -dr com.apple.quarantine /Applications/${APP_NAME}.app"
    echo "    or Right-click the app → Open."
elif [ -z "${APPLE_ID:-}" ]; then
    echo ""
    echo "Signed with Developer ID. For frictionless install on other Macs,"
    echo "also set APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD and re-run."
fi
