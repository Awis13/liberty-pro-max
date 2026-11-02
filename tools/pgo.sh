#!/bin/sh
# Profile-guided build of the lifted code (RECOMP_PGO): an instrumented
# build plays the benchmark route (the test save, out of the safehouse,
# max settings in run/mac/data), its counts are merged, and the release
# build is redone with them. Re-run after the lifter or the code changes.
# A visible window, no sound; about 25 minutes.
set -e
P=$(cd "$(dirname "$0")/.." && pwd)
R=$P/run/mac
mkdir -p "$R/pgo"
rm -f "$R"/pgo/*.profraw
cd "$P"
cmake --preset release -DRECOMP_PGO=generate > run/cfg.log
nice -n 10 cmake --build --preset release -j6 --target gtaiv > run/build-mac.log
cd "$R"
env LLVM_PROFILE_FILE="$R/pgo/gtaiv-%p.profraw" SDL_AUDIODRIVER=dummy GTAIV_BACKGROUND=1 \
  GTAIV_VSYNC=0 GTAIV_GFX=metal GTAIV_TEST_INPUT="$R/input-walk-slow.txt" XDG_DATA_HOME="$R/data" \
  timeout -k 30 220 ../../out/build/release/gtaiv --game-dir ../game > pgo/run.log 2>&1 || true
xcrun llvm-profdata merge -o "$R/pgo/gtaiv.profdata" "$R"/pgo/*.profraw
cd "$P"
cmake --preset release -DRECOMP_PGO="$R/pgo/gtaiv.profdata" > run/cfg.log
nice -n 10 cmake --build --preset release -j6 --target gtaiv > run/build-mac.log
echo "pgo: built with $R/pgo/gtaiv.profdata"
