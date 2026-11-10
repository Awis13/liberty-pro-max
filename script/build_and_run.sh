#!/bin/bash
# Build and launch the macOS app. Optional: --verify [GAME_DIR].
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
verify=0
if [ "${1:-}" = --verify ]; then verify=1; shift; fi
app="$here/out/build/release/Liberty Pro Max.app"
# Stop only this checkout's app, if a previous run is still open.
pkill -TERM -f "^$app/Contents/MacOS/gtaiv([[:space:]]|$)" 2>/dev/null || true
cmake --preset release
cmake --build --preset release --target gtaiv
args=()
if [ $# -gt 0 ]; then args=(--args --game-dir "$1"); fi
open -n "$app" "${args[@]}"
if [ "$verify" = 1 ]; then
  for ((i=0; i<20; i++)); do
    if pgrep -f "^$app/Contents/MacOS/gtaiv([[:space:]]|$)" >/dev/null; then exit 0; fi
    sleep 0.25
  done
  echo "The app did not remain running." >&2
  exit 1
fi
