#!/usr/bin/env bash
# Copy verslicer.app into a portable VerSlicer.app (real Resources, no dev symlinks).
#
# Usage:
#   ./scripts/prepare_macos_app_bundle.sh [source.app] [output.app]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARCH="${ARCH:-arm64}"
CONFIG="${CONFIG:-Release}"

SOURCE_APP="${1:-${PROJECT_DIR}/build/${ARCH}/src/${CONFIG}/verslicer.app}"
OUT_APP="${2:-${PROJECT_DIR}/build/${ARCH}/dist/VerSlicer.app}"
RESOURCES_SRC="${PROJECT_DIR}/resources"

if [ ! -d "${SOURCE_APP}" ]; then
    echo "error: source app not found: ${SOURCE_APP}" >&2
    echo "Build first: ./build_release_macos.sh -s -x" >&2
    exit 1
fi

if [ ! -d "${RESOURCES_SRC}" ]; then
    echo "error: resources directory not found: ${RESOURCES_SRC}" >&2
    exit 1
fi

echo "Preparing portable app bundle..."
echo "  source: ${SOURCE_APP}"
echo "  output: ${OUT_APP}"

rm -rf "${OUT_APP}"
mkdir -p "$(dirname "${OUT_APP}")"
ditto --noqtn --noextattr "${SOURCE_APP}" "${OUT_APP}"

RES="${OUT_APP}/Contents/Resources"
if [ -L "${RES}" ]; then
    TARGET="$(readlink "${RES}")"
    if [ ! -d "${TARGET}" ]; then
        TARGET="${RESOURCES_SRC}"
    fi
    echo "  materializing Resources from ${TARGET}"
    rm "${RES}"
    cp -R "${TARGET}" "${RES}"
elif [ ! -d "${RES}" ]; then
    echo "  copying Resources from ${RESOURCES_SRC}"
    cp -R "${RESOURCES_SRC}" "${RES}"
fi

# Fail on symlinks that point outside the bundle (would break on another Mac).
while IFS= read -r -d '' link; do
    rel="${link#${OUT_APP}/}"
    target="$(readlink "${link}" || true)"
    if [[ "${target}" != /* ]]; then
        continue
    fi
    case "${target}" in
        "${OUT_APP}"/*) continue ;;
    esac
    echo "error: bundle contains external symlink ${rel} -> ${target}" >&2
    echo "Re-run prepare after a clean Release build." >&2
    exit 1
done < <(find "${OUT_APP}" -type l -print0)

xattr -cr "${OUT_APP}" 2>/dev/null || true
find "${OUT_APP}" -name '.DS_Store' -delete 2>/dev/null || true

if [ ! -f "${OUT_APP}/Contents/MacOS/verslicer" ]; then
    echo "error: executable missing in bundle" >&2
    exit 1
fi

if [ ! -f "${RES}/Icon.icns" ]; then
    echo "error: bundled Resources look incomplete (Icon.icns missing)" >&2
    exit 1
fi

echo "Done: ${OUT_APP}"
du -sh "${OUT_APP}"
