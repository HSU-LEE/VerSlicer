#!/usr/bin/env bash
# Stop, rebuild, and run ORCA_TOOLS dev-utils tests (Ollama + MakerWorld).
#
# Usage:
#   ./scripts/run_orca_tools_test.sh stop          # stop running builds, clean PCH tmp
#   ./scripts/run_orca_tools_test.sh build         # build test targets only
#   ./scripts/run_orca_tools_test.sh test          # run ctest (no build)
#   ./scripts/run_orca_tools_test.sh               # stop stale builds, build, test
#
# Env:
#   BUILD_DIR   default: build/$(uname -m) under repo root
#   JOBS        parallel jobs after libslic3r PCH (default: ncpu)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="$(uname -m)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/${ARCH}}"
CONFIG="${CONFIG:-Release}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"

TEST_TARGETS=(
    ollama_assistant_test
    ollama_pipeline_test
    makerworld_search_test
)

stop_builds() {
    echo "Stopping cmake/ninja builds under ${BUILD_DIR}..."
    pkill -TERM -f "cmake --build.*${BUILD_DIR}" 2>/dev/null || true
    pkill -TERM -f "ninja.*build-Release.ninja" 2>/dev/null || true
    pkill -TERM -f "ninja.*build-Debug.ninja" 2>/dev/null || true
    sleep 1
    if pgrep -f "cmake --build.*${BUILD_DIR}|ninja.*build-(Release|Debug).ninja" >/dev/null 2>&1; then
        echo "Force-stopping remaining build processes..."
        pkill -KILL -f "cmake --build.*${BUILD_DIR}" 2>/dev/null || true
        pkill -KILL -f "ninja.*build-Release.ninja" 2>/dev/null || true
        pkill -KILL -f "ninja.*build-Debug.ninja" 2>/dev/null || true
    fi
    cleanup_pch_tmp
    echo "Build stopped."
}

cleanup_pch_tmp() {
    if [[ -d "${BUILD_DIR}" ]]; then
        find "${BUILD_DIR}" -name "*.pch.tmp" -delete 2>/dev/null || true
    fi
}

require_build_dir() {
    if [[ ! -d "${BUILD_DIR}" ]]; then
        echo "Build dir not found: ${BUILD_DIR}" >&2
        echo "Configure first, e.g. ./build_release_macos.sh -s -x" >&2
        exit 1
    fi
}

build_tests() {
    require_build_dir
    cleanup_pch_tmp
    cd "${BUILD_DIR}"

    echo "Building libslic3r PCH (single-threaded)..."
    cmake --build . --config "${CONFIG}" --target libslic3r -j1

    echo "Building test targets (-j${JOBS})..."
    cmake --build . --config "${CONFIG}" --target "${TEST_TARGETS[@]}" -j"${JOBS}"
}

run_tests() {
    require_build_dir
    cd "${BUILD_DIR}"
    ctest -C "${CONFIG}" \
        -R 'ollama_assistant_test|ollama_pipeline_test|makerworld_search_test' \
        --output-on-failure
}

usage() {
    sed -n '3,9p' "$0" | sed 's/^# \?//'
}

cmd="${1:-all}"
case "${cmd}" in
    stop)
        stop_builds
        ;;
    build)
        build_tests
        ;;
    test|run)
        run_tests
        ;;
    all|"")
        stop_builds
        build_tests
        run_tests
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        echo "Unknown command: ${cmd}" >&2
        usage >&2
        exit 1
        ;;
esac
