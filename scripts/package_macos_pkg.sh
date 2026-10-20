#!/usr/bin/env bash
# Package VerSlicer.app into a .pkg installer. The Installer screens
# (scripts/pkg/*.html) hold the Terminal fix as selectable, copyable text, and
# apps installed by Installer do not get the download quarantine flag.
#
# Usage:
#   ./scripts/package_macos_pkg.sh [path/to/VerSlicer.app] [output.pkg]
# Environment (optional):
#   APPLE_INSTALLER_IDENTITY — "Developer ID Installer: Your Name (TEAMID)" to sign the .pkg

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARCH="${ARCH:-arm64}"
ENTITLEMENTS="${PROJECT_DIR}/scripts/disable_validation.entitlements"

APP_PATH="${1:-${PROJECT_DIR}/build/${ARCH}/dist/VerSlicer.app}"
VERSION="$(grep -E '^set\(VERSLICER_VERSION' "${PROJECT_DIR}/version.inc" 2>/dev/null \
    | sed -E 's/.*"([^"]+)".*/\1/' || echo "0.0.0")"
OUT_PKG="${2:-${PROJECT_DIR}/build/${ARCH}/dist/VerSlicer-macOS-${ARCH}-${VERSION}.pkg}"

if [ ! -d "${APP_PATH}" ]; then
    echo "error: app bundle not found: ${APP_PATH}" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

echo "Staging VerSlicer.app..."
mkdir -p "${WORK}/root"
ditto --noqtn --noextattr "${APP_PATH}" "${WORK}/root/VerSlicer.app"
xattr -cr "${WORK}/root/VerSlicer.app" 2>/dev/null || true
SIGN_ARGS=(--force --deep --options runtime --sign -)
[ -f "${ENTITLEMENTS}" ] && SIGN_ARGS+=(--entitlements "${ENTITLEMENTS}")
codesign "${SIGN_ARGS[@]}" "${WORK}/root/VerSlicer.app"

# Install exactly where we put it, even if another VerSlicer.app exists elsewhere.
pkgbuild --analyze --root "${WORK}/root" "${WORK}/components.plist" >/dev/null
/usr/libexec/PlistBuddy -c "Delete :0:BundleIsRelocatable" "${WORK}/components.plist" 2>/dev/null || true
/usr/libexec/PlistBuddy -c "Add :0:BundleIsRelocatable bool false" "${WORK}/components.plist"

pkgbuild --root "${WORK}/root" \
    --component-plist "${WORK}/components.plist" \
    --identifier com.verslicer.app \
    --version "${VERSION}" \
    --install-location /Applications \
    "${WORK}/VerSlicer-component.pkg" >/dev/null

mkdir -p "${WORK}/resources"
cp "${PROJECT_DIR}/scripts/pkg/welcome.html" "${PROJECT_DIR}/scripts/pkg/conclusion.html" "${WORK}/resources/"

cat > "${WORK}/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>VerSlicer ${VERSION}</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="never" require-scripts="false" hostArchitectures="${ARCH}"/>
    <domains enable_localSystem="true"/>
    <choices-outline><line choice="default"/></choices-outline>
    <choice id="default" title="VerSlicer"><pkg-ref id="com.verslicer.app"/></choice>
    <pkg-ref id="com.verslicer.app" version="${VERSION}" onConclusion="none">VerSlicer-component.pkg</pkg-ref>
</installer-gui-script>
XML

mkdir -p "$(dirname "${OUT_PKG}")"
rm -f "${OUT_PKG}"
PB_ARGS=(--distribution "${WORK}/distribution.xml" --resources "${WORK}/resources" --package-path "${WORK}")
[ -n "${APPLE_INSTALLER_IDENTITY:-}" ] && PB_ARGS+=(--sign "${APPLE_INSTALLER_IDENTITY}")
productbuild "${PB_ARGS[@]}" "${OUT_PKG}"

echo "Done: ${OUT_PKG}"
ls -lah "${OUT_PKG}"
