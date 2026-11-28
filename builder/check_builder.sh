#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

checks=$(mktemp -d)
trap 'rm -rf "$checks"' EXIT

compile_check() {
  local name=$1 source
  local sources=()
  for source in builder/Sources/*.swift; do
    [[ $source == builder/Sources/App.swift ]] || sources+=("$source")
  done
  xcrun swiftc -warnings-as-errors -swift-version 5 -parse-as-library \
    "${sources[@]}" "builder/Checks/$name.swift" -o "$checks/$name"
}

case "${1:-}" in
  "")
    compile_check GameInputCheck
    compile_check RadioRestorationCheck
    "$checks/GameInputCheck"
    "$checks/RadioRestorationCheck"
    ;;
  game)
    shift
    compile_check GameInputCheck
    "$checks/GameInputCheck" "$@"
    ;;
  radio)
    shift
    compile_check RadioRestorationCheck
    "$checks/RadioRestorationCheck" "$@"
    ;;
  *)
    echo "Usage: builder/check_builder.sh [game PATCH_8 [GAME_FOLDER] | radio DATA1_DAT RESTORATION_RPF]" >&2
    exit 2
    ;;
esac
