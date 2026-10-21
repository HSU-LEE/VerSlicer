#!/usr/bin/env bash
# Measure the assistant against the golden cases with the real model.
#   start  write the requests to build/assistant_eval_requests.txt and launch
#          the dev build with VERSLICER_EVAL_LOG set (the log is reset)
#   grade  score the recorded replies (ASSISTANT_EVAL_MIN_PASS=80 to fail below 80%)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/$(uname -m)-dev}"
EVAL_BIN="${BUILD_DIR}/src/dev-utils/assistant_eval_test.app/Contents/MacOS/assistant_eval_test"
APP_BIN="${BUILD_DIR}/src/verslicer.app/Contents/MacOS/verslicer"
LOG="${ROOT_DIR}/build/assistant_eval_log.jsonl"
REQUESTS="${ROOT_DIR}/build/assistant_eval_requests.txt"

[ -x "${EVAL_BIN}" ] || { echo "Build the tests first: ./scripts/run_orca_tools_test.sh build" >&2; exit 1; }

case "${1:-}" in
start)
    [ -x "${APP_BIN}" ] || { echo "Build the app first: ninja -C ${BUILD_DIR} verslicer" >&2; exit 1; }
    ASSISTANT_EVAL_LIST=1 "${EVAL_BIN}" > "${REQUESTS}"
    : > "${LOG}"
    echo "$(wc -l < "${REQUESTS}" | tr -d ' ') requests in ${REQUESTS}"
    echo "Replies are logged to ${LOG}. Run '$0 grade' when done."
    VERSLICER_EVAL_LOG="${LOG}" "${APP_BIN}" >/dev/null 2>&1 &
    ;;
grade)
    [ -s "${LOG}" ] || { echo "No replies recorded in ${LOG}. Run '$0 start' first." >&2; exit 1; }
    ASSISTANT_EVAL_LOG="${LOG}" "${EVAL_BIN}"
    ;;
*)
    echo "usage: $0 start|grade" >&2
    exit 2
    ;;
esac
