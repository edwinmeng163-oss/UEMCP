#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
exec bun run --cwd "." src/index.ts
