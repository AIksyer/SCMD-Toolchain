#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
for t in clang clang++ cmake ninja; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 1; }
done
if [[ ! -d .git ]] && command -v git >/dev/null; then git init; fi
cmake --preset clang-debug
echo "[SCMD] Project initialized. Run ./scripts/build.sh"
echo "[SCMD] Build outputs will be written to dist/debug/"
