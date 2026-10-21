#!/usr/bin/env bash
# Build and run ORCA_TOOLS dev-utils tests (Assistant, MakerWorld, mesh, planner).
#
# Usage:
#   ./scripts/run_orca_tools_test.sh stop          # stop running builds, clean PCH tmp
#   ./scripts/run_orca_tools_test.sh build         # build test targets only
#   ./scripts/run_orca_tools_test.sh test          # run ctest (no build)
#   ./scripts/run_orca_tools_test.sh               # incremental build, then test
#
# Env:
#   BUILD_DIR   default: build/<arch>-dev if configured, else build/<arch>
#   CONFIG      Debug for the dev tree, Release otherwise
#   JOBS        parallel jobs (default: RAM-sized cap)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT_DIR}/scripts/lib/ccache_and_jobs.sh"
enable_ccache_env || true
ARCH="$(uname -m)"
if [ -z "${BUILD_DIR:-}" ]; then
    if [ -f "${ROOT_DIR}/build/${ARCH}-dev/build.ninja" ]; then
        BUILD_DIR="${ROOT_DIR}/build/${ARCH}-dev"
    else
        BUILD_DIR="${ROOT_DIR}/build/${ARCH}"
    fi
fi
if [[ "${BUILD_DIR}" == *-dev ]]; then
    CONFIG="${CONFIG:-Debug}"
else
    CONFIG="${CONFIG:-Release}"
fi
JOBS="${JOBS:-$(macos_default_build_jobs)}"

TEST_TARGETS=(
    assistant_test
    assistant_pipeline_test
    assistant_apply_golden_test
    assistant_benchmark_test
    bambu_wiki_search_test
    makerworld_search_test
    model_search_ranker_test
    mesh_assist_test
    print_planner_test
    assistant_eval_test
)
TEST_REGEX="^($(IFS='|'; echo "${TEST_TARGETS[*]}"))\$"

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

    # Native target_precompile_headers (CMake >= 3.16) orders PCH before
    # consumers, so the old -j1 libslic3r prelude is not required.
    echo "Building test targets (-j${JOBS}, ${CONFIG})..."
    cmake --build . --config "${CONFIG}" --target "${TEST_TARGETS[@]}" -j"${JOBS}"
}

run_tests() {
    require_build_dir
    cd "${BUILD_DIR}"
    ctest -C "${CONFIG}" \
        -R "${TEST_REGEX}" \
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
