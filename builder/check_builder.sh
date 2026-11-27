#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

checks=$(mktemp -d)
trap 'rm -rf "$checks"' EXIT

sources=()
for source in builder/Sources/*.swift; do
  [[ $source == builder/Sources/App.swift ]] || sources+=("$source")
done
xcrun swiftc -warnings-as-errors -swift-version 5 -parse-as-library \
  "${sources[@]}" builder/Checks/GameInputCheck.swift -o "$checks/GameInputCheck"
"$checks/GameInputCheck" "$@"
