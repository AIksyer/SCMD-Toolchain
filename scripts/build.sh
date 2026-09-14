#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
CONFIG="${1:-Debug}"
case "$CONFIG" in
  Debug|debug) PRESET=clang-debug; FLAVOR=debug ;;
  Release|release) PRESET=clang-release; FLAVOR=release ;;
  Sanitize|sanitize) PRESET=clang-sanitize; FLAVOR=sanitize ;;
  *) echo "usage: $0 [Debug|Release|Sanitize]" >&2; exit 2 ;;
esac

cmake --preset "$PRESET"
cmake --build --preset "$PRESET" --target scmdc scmdsim

DIST="$ROOT/dist/$FLAVOR"
test -x "$DIST/scmdc" || { echo "error: missing $DIST/scmdc" >&2; exit 1; }
test -x "$DIST/scmdsim" || { echo "error: missing $DIST/scmdsim" >&2; exit 1; }

ctest --preset "$PRESET"

echo
printf '[SCMD] Build tree:   %s\n' "$ROOT/out/build/$PRESET"
printf '[SCMD] Distribution: %s\n' "$DIST"
printf '[SCMD] Tools:\n       %s\n       %s\n' "$DIST/scmdc" "$DIST/scmdsim"
