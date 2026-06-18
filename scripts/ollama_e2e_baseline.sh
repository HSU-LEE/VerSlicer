#!/usr/bin/env bash
# Optional E2E baseline against local Ollama (llama3.2). Skips when OLLAMA_E2E != 1.
set -euo pipefail

if [[ "${OLLAMA_E2E:-0}" != "1" ]]; then
  echo "ollama_e2e_baseline: skipped (set OLLAMA_E2E=1 to run)"
  exit 0
fi

HOST="${OLLAMA_HOST:-http://127.0.0.1:11434}"
MODEL="${OLLAMA_MODEL:-llama3.2:latest}"

curl -sf "${HOST}/api/tags" >/dev/null || { echo "Ollama not reachable at ${HOST}"; exit 1; }

planner_prompt='Output JSON only: {"intent":"test","candidate_keys":["layer_height"],"message":"ok"}'
planner_body=$(jq -n --arg m "$MODEL" --arg p "$planner_prompt" \
  '{model:$m, stream:false, messages:[{role:"user",content:$p}]}')

echo "=== Planner baseline (${MODEL}) ==="
planner_out=$(curl -sf "${HOST}/api/chat" -d "$planner_body")
echo "$planner_out" | jq -r '.message.content // .error // .' | head -c 400
echo

resolver_prompt='Return JSON: {"message":"ok","actions":[{"type":"set_config","preset":"print","options":{"layer_height":0.2}}]}'
resolver_body=$(jq -n --arg m "$MODEL" --arg p "$resolver_prompt" \
  '{model:$m, stream:false, messages:[{role:"user",content:$p}]}')

echo "=== Resolver baseline (${MODEL}) ==="
resolver_out=$(curl -sf "${HOST}/api/chat" -d "$resolver_body")
echo "$resolver_out" | jq -r '.message.content // .error // .' | head -c 400
echo

echo "ollama_e2e_baseline: done"
