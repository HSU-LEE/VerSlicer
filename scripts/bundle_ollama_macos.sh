#!/usr/bin/env bash
# Cache a portable Ollama binary and the qwen2.5:7b model, then optionally
# copy them into a VerSlicer.app so a Mac can run the AI assistant offline.
#
# Usage:
#   ./scripts/bundle_ollama_macos.sh
#   ./scripts/bundle_ollama_macos.sh --into /path/to/VerSlicer.app

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
VENDOR="${OLLAMA_VENDOR_DIR:-${PROJECT_DIR}/build/ollama-vendor}"
BIN="${VENDOR}/ollama"
MODELS="${VENDOR}/models"
MANIFEST="${MODELS}/manifests/registry.ollama.ai/library/qwen2.5/7b"
MODEL_TAG="${OLLAMA_BUNDLE_MODEL:-qwen2.5:7b}"
SERVE_HOST="${OLLAMA_BUNDLE_HOST:-127.0.0.1:11435}"
INTO_APP=""

while [ $# -gt 0 ]; do
    case "$1" in
        --into)
            INTO_APP="${2:-}"
            shift 2
            ;;
        -h|--help)
            sed -n '2,8p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

mkdir -p "${VENDOR}"

copy_ollama_binary() {
    if [ -x "${BIN}" ]; then
        return 0
    fi
    local src=""
    if [ -x "/Applications/Ollama.app/Contents/Resources/ollama" ]; then
        src="/Applications/Ollama.app/Contents/Resources/ollama"
    elif [ -x "/usr/local/bin/ollama" ] && [ ! -L "/usr/local/bin/ollama" ]; then
        src="/usr/local/bin/ollama"
    fi
    if [ -n "${src}" ]; then
        echo "Copying Ollama binary from ${src}"
        cp -p "${src}" "${BIN}"
        chmod +x "${BIN}"
        return 0
    fi

    local url="https://github.com/ollama/ollama/releases/latest/download/ollama-darwin.tgz"
    local tgz="${VENDOR}/ollama-darwin.tgz"
    echo "Downloading Ollama from ${url}"
    curl -fL --retry 3 -o "${tgz}" "${url}"
    tar -xzf "${tgz}" -C "${VENDOR}"
    if [ ! -x "${BIN}" ]; then
        local found
        found="$(find "${VENDOR}" -type f -name ollama -perm +111 | head -1 || true)"
        if [ -z "${found}" ]; then
            echo "error: ollama binary not found after download" >&2
            exit 1
        fi
        cp -p "${found}" "${BIN}"
        chmod +x "${BIN}"
    fi
}

pull_model() {
    if [ -e "${MANIFEST}" ]; then
        echo "Model already cached: ${MODEL_TAG}"
        return 0
    fi
    mkdir -p "${MODELS}"
    echo "Pulling ${MODEL_TAG} into ${MODELS} (several GB, one time)..."
    local log="${VENDOR}/serve.log"
    OLLAMA_MODELS="${MODELS}" OLLAMA_HOST="${SERVE_HOST}" "${BIN}" serve >"${log}" 2>&1 &
    local pid=$!
    cleanup_serve() {
        if kill -0 "${pid}" 2>/dev/null; then
            kill "${pid}" 2>/dev/null || true
            wait "${pid}" 2>/dev/null || true
        fi
    }
    trap cleanup_serve RETURN

    local ready=0
    for _ in $(seq 1 90); do
        if curl -sf "http://${SERVE_HOST}/api/tags" >/dev/null; then
            ready=1
            break
        fi
        if ! kill -0 "${pid}" 2>/dev/null; then
            echo "error: bundled ollama serve exited early. Log:" >&2
            tail -n 40 "${log}" >&2 || true
            exit 1
        fi
        sleep 1
    done
    if [ "${ready}" -ne 1 ]; then
        echo "error: ollama serve did not become ready on ${SERVE_HOST}" >&2
        tail -n 40 "${log}" >&2 || true
        exit 1
    fi

    OLLAMA_HOST="${SERVE_HOST}" "${BIN}" pull "${MODEL_TAG}"
    if [ ! -e "${MANIFEST}" ]; then
        echo "error: pull finished but manifest is missing: ${MANIFEST}" >&2
        exit 1
    fi
}

install_into_app() {
    local app="$1"
    if [ ! -d "${app}/Contents" ]; then
        echo "error: not an app bundle: ${app}" >&2
        exit 1
    fi
    local dest="${app}/Contents/Resources/ollama"
    rm -rf "${dest}"
    mkdir -p "${dest}"
    cp -p "${BIN}" "${dest}/ollama"
    chmod +x "${dest}/ollama"
    echo "Copying model into app bundle..."
    mkdir -p "${dest}/models"
    if ! cp -cR "${MODELS}/." "${dest}/models/" 2>/dev/null; then
        ditto "${MODELS}" "${dest}/models"
    fi
    echo "Bundled Ollama into ${dest}"
    du -sh "${dest}" "${dest}/models" || true
}

copy_ollama_binary
pull_model

if [ -n "${INTO_APP}" ]; then
    install_into_app "${INTO_APP}"
else
    echo "Ollama vendor cache ready:"
    echo "  ${BIN}"
    echo "  ${MANIFEST}"
    du -sh "${MODELS}" || true
fi
