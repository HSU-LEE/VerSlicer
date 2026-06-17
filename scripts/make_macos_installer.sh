#!/usr/bin/env bash
# Build a distributable VerSlicer .dmg for other Macs (no dev environment required).
#
# Usage:
#   ./scripts/make_macos_installer.sh              # package existing Release build
#   ./scripts/make_macos_installer.sh --build      # rebuild app, then package
#   ./scripts/make_macos_installer.sh --build-only # only prepare portable .app
#
# Environment (optional, for signing / notarization — see package_macos_dmg.sh):
#   ARCH, CONFIG, APPLE_SIGNING_IDENTITY, APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARCH="${ARCH:-arm64}"
CONFIG="${CONFIG:-Release}"

DO_BUILD=0
BUILD_ONLY=0
for arg in "$@"; do
    case "${arg}" in
        --build) DO_BUILD=1 ;;
        --build-only) BUILD_ONLY=1 ;;
        -h|--help)
            sed -n '2,12p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "Unknown option: ${arg}" >&2
            exit 1
            ;;
    esac
done

SOURCE_APP="${PROJECT_DIR}/build/${ARCH}/src/${CONFIG}/verslicer.app"
PORTABLE_APP="${PROJECT_DIR}/build/${ARCH}/dist/VerSlicer.app"

if [ "${DO_BUILD}" -eq 1 ]; then
    echo "Building VerSlicer (${ARCH}, ${CONFIG})..."
    "${PROJECT_DIR}/build_release_macos.sh" -s -x -a "${ARCH}" -c "${CONFIG}"
fi

if [ ! -d "${SOURCE_APP}" ]; then
    echo "error: ${SOURCE_APP} not found. Run with --build or build manually." >&2
    exit 1
fi

"${SCRIPT_DIR}/prepare_macos_app_bundle.sh" "${SOURCE_APP}" "${PORTABLE_APP}"

if [ "${BUILD_ONLY}" -eq 1 ]; then
    echo "Portable app ready (no .dmg): ${PORTABLE_APP}"
    exit 0
fi

VERSION="$(grep -E '^set\(SoftFever_VERSION' "${PROJECT_DIR}/version.inc" 2>/dev/null \
    | sed -E 's/.*"([^"]+)".*/\1/' || echo "0.0.0")"
OUT_DMG="${PROJECT_DIR}/build/${ARCH}/dist/VerSlicer-macOS-${ARCH}-${VERSION}.dmg"

"${SCRIPT_DIR}/package_macos_dmg.sh" "${PORTABLE_APP}" "${OUT_DMG}"

echo ""
echo "Installer ready:"
echo "  ${OUT_DMG}"
echo ""
echo "Copy this .dmg to another Mac, open it, drag VerSlicer to Applications."
echo "For AI chat, install Ollama separately: https://ollama.com/download"
