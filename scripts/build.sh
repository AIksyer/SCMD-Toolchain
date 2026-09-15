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
cmake --build --preset "$PRESET" --target scmd_tools

DIST="$ROOT/dist/$FLAVOR"
test -x "$DIST/scmdc" || { echo "error: missing $DIST/scmdc" >&2; exit 1; }
test -x "$DIST/scmdsim" || { echo "error: missing $DIST/scmdsim" >&2; exit 1; }
for tool in vcs16as vcs16run vcs16dump vcs16scmd; do
  test -x "$DIST/$tool" || { echo "error: missing $DIST/$tool" >&2; exit 1; }
done

ctest --preset "$PRESET"

echo
printf '[SCMD] Build tree:   %s\n' "$ROOT/out/build/$PRESET"
printf '[SCMD] Distribution: %s\n' "$DIST"
printf '[SCMD] Tools:\n'
for tool in scmdc scmdsim vcs16as vcs16run vcs16dump vcs16scmd; do printf '       %s/%s\n' "$DIST" "$tool"; done
