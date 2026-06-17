#!/usr/bin/env bash
# Install Ollama.app from a .dmg (bundled with VerSlicer or downloaded).
# Usage: install_ollama_macos.sh /path/to/Ollama.dmg

set -euo pipefail

DMG="${1:-}"
if [ -z "${DMG}" ] || [ ! -f "${DMG}" ]; then
    echo "error: Ollama.dmg not found: ${DMG}" >&2
    exit 1
fi

install_app() {
    local src="$1"
    local dest="$2"
    rm -rf "${dest}"
    ditto --noqtn "${src}" "${dest}"
    xattr -cr "${dest}" 2>/dev/null || true
}

MOUNT=""
cleanup() {
    if [ -n "${MOUNT}" ] && [ -d "${MOUNT}" ]; then
        hdiutil detach "${MOUNT}" -quiet 2>/dev/null || true
    fi
}
trap cleanup EXIT

echo "Mounting Ollama installer..."
ATTACH_OUT="$(hdiutil attach -nobrowse -readonly -quiet "${DMG}")"
MOUNT="$(echo "${ATTACH_OUT}" | tail -n1 | sed -E 's/.*(\/Volumes\/[^[:space:]]+).*/\1/')"
if [ -z "${MOUNT}" ] || [ ! -d "${MOUNT}" ]; then
    echo "error: could not mount ${DMG}" >&2
    exit 2
fi

SRC_APP="${MOUNT}/Ollama.app"
if [ ! -d "${SRC_APP}" ]; then
    SRC_APP="$(find "${MOUNT}" -maxdepth 2 -name 'Ollama.app' -type d | head -1)"
fi
if [ -z "${SRC_APP}" ] || [ ! -d "${SRC_APP}" ]; then
    echo "error: Ollama.app not found in disk image" >&2
    exit 3
fi

DEST="/Applications/Ollama.app"
if ! install_app "${SRC_APP}" "${DEST}" 2>/dev/null; then
    echo "Installing to ~/Applications (no admin access to /Applications)..."
    mkdir -p "${HOME}/Applications"
    DEST="${HOME}/Applications/Ollama.app"
    install_app "${SRC_APP}" "${DEST}"
fi

hdiutil detach "${MOUNT}" -quiet
MOUNT=""

echo "Launching Ollama..."
open "${DEST}" || true

for _ in $(seq 1 30); do
  if [ -x "${DEST}/Contents/Resources/ollama" ]; then
    if "${DEST}/Contents/Resources/ollama" list >/dev/null 2>&1; then
      echo "Ollama is running."
      exit 0
    fi
  fi
  if curl -fsS "http://127.0.0.1:11434/api/tags" >/dev/null 2>&1; then
    echo "Ollama API is reachable."
    exit 0
  fi
  sleep 1
done

echo "Ollama installed; waiting for background start (open the menu-bar icon if needed)."
exit 0
