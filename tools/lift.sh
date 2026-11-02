#!/bin/bash
# lift.sh: the game's code, from your own GTAIV.exe (1.0.8.0), translated to
# C++ in generated/: the lifted executable (recomp_lift, with the symbols in
# symbols/ and this port's changes in manifest/gtaiv.manifest) and the hot
# functions listed in manifest/native.list (tools/x86n.py, generated/native/).
# Run it after the first build (which makes the lifter) and again after
# changing a manifest; then build again. Takes a few minutes.
#   GTAIV_GAME_DIR: the game's folder (the one with GTAIV.exe), from the
#   environment or tools/local.env.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$here/tools/local.env" ] && . "$here/tools/local.env"
: "${GTAIV_GAME_DIR:?set GTAIV_GAME_DIR to the folder with GTAIV.exe}"
exe="$GTAIV_GAME_DIR/GTAIV.exe"
[ -f "$exe" ] || { echo "no GTAIV.exe in $GTAIV_GAME_DIR"; exit 1; }
want=3d90e7c516fa450ca002e5031e62c0f66b404590f33e6cc9793b0da4fffbfd0f
have=$(shasum -a 256 "$exe" | cut -d' ' -f1)
[ "$have" = "$want" ] || { echo "GTAIV.exe is not version 1.0.8.0 (SHA-256 $have, want $want)"; exit 1; }
lift="${LIFTER:-$here/out/build/release/x86recomp/tools/lifter/recomp_lift}"
[ -x "$lift" ] || { echo "build first (cmake --preset release && cmake --build --preset release): no $lift"; exit 1; }

echo "lift.sh: lifting GTAIV.exe"
(cd "$GTAIV_GAME_DIR" && "$lift" --exe GTAIV.exe --symbols "$here/symbols/gtaiv" \
  --manifest "$here/manifest/gtaiv.manifest" --out "$here/generated" "$@")

echo "lift.sh: translating $(wc -l < "$here/manifest/native.list" | tr -d ' ') hot functions"
mkdir -p "$here/generated/native"
export GTAIV_EXE="$exe"
# A line: start, last byte (inclusive), name; x86n.py takes the end exclusive.
while read -r start last name; do
  printf '%s %08X %s\n' "$start" $((0x$last + 1)) "$name"
done < "$here/manifest/native.list" |
  xargs -P "$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || echo 4)" -L 1 sh -c \
    'python3 "$0/tools/x86n.py" "$1" "$2" "$3" > "$0/generated/native/$1.inc"' "$here"
echo "lift.sh: done"
