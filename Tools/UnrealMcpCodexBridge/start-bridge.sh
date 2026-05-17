#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUN_BIN="${UEVOLVE_BUN_BIN:-}"
if [[ -z "$BUN_BIN" && -x "$SCRIPT_DIR/runtime/bun" ]]; then
  export UEVOLVE_BUN_BIN="$SCRIPT_DIR/runtime/bun"
  BUN_BIN="$UEVOLVE_BUN_BIN"
fi
if [[ -z "$BUN_BIN" ]]; then
  BUN_BIN="bun"
fi

exec "$BUN_BIN" run --cwd "$SCRIPT_DIR" src/index.ts
