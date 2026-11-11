#!/bin/bash
# Shader-compile stutter on this Mac, cold and warm: the game loads a save,
# stands at a fixed place (GTAIV_BENCH), plays for a fixed time and is
# stopped; tools/shader_bench_report.py then sums what its log says
# (pipelines compiled while drawing, the runtime's PERF counts, hitches,
# fps, the time to the first scene). macOS only: the iPad is measured
# otherwise, not by this script.
#
#   tools/shader_bench.sh cold|warm|both [options]
#
#   cold  the shader caches emptied first; warm  kept from the last run;
#   both  cold, then warm.
#
# The player's own caches are never touched. The game runs from a copy of
# the app in the work folder whose bundle id is the original's plus
# ".bench" (signed again, ad hoc): macOS keeps its Metal caches by bundle
# id ($(getconf DARWIN_USER_CACHE_DIR)/<id>.bench/com.apple.metal,
# com.apple.metalfe, com.apple.gpuarchiver). The runtime keeps its own
# (metal-msl-v1.bin, metal-pipelines-v1.bin) in the user's Caches folder
# as NSSearchPathForDirectoriesInDomains gives it, which follows
# CFFIXED_USER_HOME (not HOME): the game gets the work folder's home/ as
# that, so they are home/Library/Caches/x86recomp. Saves and settings are
# in the work folder's data/ (XDG_DATA_HOME), kept between runs but for the
# save, copied in every time. A cold run deletes those cache folders only,
# and says which.
#
# The run goes through the farm (a measurement: it waits its turn), hidden
# and muted, vsync off. The variables it sets are listed in env.txt; any
# other in the caller's environment reaches the game (RECOMP_UPSCALER,
# GTAIV_TEST_INPUT for a route, ...).
#
# The default scene (GTAIV_BENCH=-421.9,1494,18.9,90,13,1) is the street
# of the earlier speed measurements: outdoors with traffic, at 13:00 in
# clear weather, the player standing facing east.
#
# Options:
#   --app PATH          the app (default out/build/release/Liberty Pro Max.app)
#   --game-dir DIR      the game's files
#   --save FILE         the save loaded (as SGTA405, GTAIV_CONTINUE)
#   --bench SCENE       x,y,z,heading[,hour,weather] for GTAIV_BENCH
#   --play SECONDS      how long it plays from the scene on (120)
#   --start-timeout S   longest from the game's start to the scene (900)
#   --width W --height H  the resolution (1920x1080)
#   --work DIR          the app copy, home/ and data/
#   --out DIR           each run's folder goes in here
#   --who NAME          the farm's name for the job (shader-bench)
#
# Output: <out>/<date>-<mode>/ with game.log, summary.json, summary.txt,
# env.txt and run.txt (for both: cold/ and warm/ each, and summary.txt with
# the two side by side).
set -u

REPO=$(cd "$(dirname "$0")/.." && pwd)
FARM=${SHADER_BENCH_FARM:-/Volumes/USB-Samsung/Games/farm/farm}  # (another for tests)
APP="$REPO/out/build/release/Liberty Pro Max.app"
GAME_DIR="/Volumes/USB-Samsung/Games/GTAIV-ipad-bcd/Grand Theft Auto IV"
SAVE="/Volumes/USB-Samsung/Games/scratch/episodes/saves-dl/x/GTA IV and EFLC 100%/IV/SGTA400"
BENCH="-421.9,1494,18.9,90,13,1"
PLAY=120
START_TIMEOUT=900
WIDTH=1920
HEIGHT=1080
OUT=/Volumes/USB-Samsung/Games/scratch/shader-bench
WORK=$OUT/work
WHO=shader-bench
# The saves' folder in the data folder (src/save_paths.h) and the profile.
SAVES_REL="gtaiv/drive_c/Documents and Settings/Player/Local Settings/Application Data/Rockstar Games/GTA IV/savegames/user_ee000000deadc0de"

die() { echo "shader_bench: $*" >&2; exit 1; }
usage() { sed -n '2,/^set -u/p' "$0" | sed -e '/^set -u/d' -e 's/^# \{0,1\}//'; exit "${1:-0}"; }

[ "$(uname -s)" = Darwin ] || die "macOS only"
[ $# -ge 1 ] || usage 1
MODE=$1; shift
case $MODE in cold|warm|both) ;; -h|--help) usage 0 ;; *) usage 1 ;; esac
while [ $# -gt 0 ]; do
  [ $# -ge 2 ] || die "$1: no value"
  case $1 in
    --app) APP=$2 ;;
    --game-dir) GAME_DIR=$2 ;;
    --save) SAVE=$2 ;;
    --bench) BENCH=$2 ;;
    --play) PLAY=$2 ;;
    --start-timeout) START_TIMEOUT=$2 ;;
    --width) WIDTH=$2 ;;
    --height) HEIGHT=$2 ;;
    --work) WORK=$2 ;;
    --out) OUT=$2 ;;
    --who) WHO=$2 ;;
    *) die "unknown option $1" ;;
  esac
  shift 2
done
for n in "$PLAY" "$START_TIMEOUT" "$WIDTH" "$HEIGHT"; do
  case $n in ''|*[!0-9]*) die "not a number: $n" ;; esac
done
APP=${APP%/}
[ -d "$APP/Contents/MacOS" ] || die "no app at $APP (build it first)"
[ -d "$GAME_DIR" ] || die "no game folder at $GAME_DIR"
[ -f "$SAVE" ] || die "no save at $SAVE"
[ -x "$FARM" ] || die "no farm at $FARM"
mkdir -p "$WORK" "$OUT" || die "cannot make $WORK or $OUT"
WORK=$(cd "$WORK" && pwd)
OUT=$(cd "$OUT" && pwd)

EXE=$(plutil -extract CFBundleExecutable raw "$APP/Contents/Info.plist") || die "no CFBundleExecutable"
ID=$(plutil -extract CFBundleIdentifier raw "$APP/Contents/Info.plist") || die "no CFBundleIdentifier"
BENCH_ID=$ID.bench
BENCH_APP="$WORK/app/$(basename "$APP")"
BIN="$BENCH_APP/Contents/MacOS/$EXE"
HOME_DIR=$WORK/home
DATA=$WORK/data
RECOMP_CACHE=$HOME_DIR/Library/Caches/x86recomp
DCACHE=$(getconf DARWIN_USER_CACHE_DIR) || die "no DARWIN_USER_CACHE_DIR"
DCACHE=${DCACHE%/}
METAL_CACHE=$DCACHE/$BENCH_ID

# Deletes a folder of the bench's own (the app copy, the bench home's
# runtime cache, the bench bundle id's Metal caches), nothing else.
bench_rm() {
  local p=$1
  case $BENCH_ID in *.bench) ;; *) die "refusing: bundle id $BENCH_ID" ;; esac
  [ "$BENCH_ID" != "$ID" ] || die "refusing: bench id is the original's"
  case $p in
    "$WORK"/app/*|"$RECOMP_CACHE"|"$METAL_CACHE"/*) ;;
    *) die "refusing to delete $p" ;;
  esac
  [ -e "$p" ] || return 0
  echo "shader_bench: deleting $p"
  rm -rf -- "$p" || die "could not delete $p"
}

# The app copy with the bench's bundle id, made from the build every time.
prepare_app() {
  mkdir -p "$WORK/app"
  bench_rm "$BENCH_APP"
  ditto "$APP" "$BENCH_APP" || die "could not copy $APP"
  plutil -replace CFBundleIdentifier -string "$BENCH_ID" "$BENCH_APP/Contents/Info.plist" || die "plist"
  codesign --force --sign - "$BENCH_APP" 2> /dev/null || die "could not sign $BENCH_APP"
  [ -x "$BIN" ] || die "no executable $BIN"
  echo "shader_bench: app copy $BENCH_APP ($BENCH_ID)"
}

empty_caches() {
  bench_rm "$RECOMP_CACHE"
  local d
  for d in com.apple.metal com.apple.metalfe com.apple.gpuarchiver; do
    bench_rm "$METAL_CACHE/$d"
  done
}

prepare_save() {
  local dir="$DATA/$SAVES_REL" f
  mkdir -p "$dir" || die "cannot make $dir"
  for f in "$dir"/SGTA*; do
    [ -e "$f" ] && rm -f -- "$f"
  done
  cp "$SAVE" "$dir/SGTA405" || die "could not copy the save"
}

FARM_PID=
GAME_PID=

# The game the farm started for this run: its child, this executable.
find_game() {
  local p
  for p in $(pgrep -P "$FARM_PID"); do
    if [ "$(ps -o comm= -p "$p" | sed 's/ *$//')" = "$BIN" ]; then
      GAME_PID=$p
      return 0
    fi
  done
  return 1
}

# Stops this run's game only (by its PID, checked to still be the farm's
# child running the bench executable), then waits for the farm to finish.
stop_game() {
  if [ -n "$GAME_PID" ] && kill -0 "$GAME_PID" 2> /dev/null &&
     [ "$(ps -o ppid= -p "$GAME_PID" | tr -d ' ')" = "$FARM_PID" ]; then
    echo "shader_bench: stopping the game (PID $GAME_PID)"
    kill -TERM "$GAME_PID"
    for _ in $(seq 1 60); do
      kill -0 "$GAME_PID" 2> /dev/null || break
      sleep 0.25
    done
    if kill -0 "$GAME_PID" 2> /dev/null; then
      echo "shader_bench: the game ignored SIGTERM: SIGKILL"
      kill -KILL "$GAME_PID"
    fi
  elif [ -n "$FARM_PID" ] && kill -0 "$FARM_PID" 2> /dev/null; then
    # Still waiting in the farm's queue: it leaves the queue on SIGINT.
    kill -INT "$FARM_PID"
  fi
  [ -n "$FARM_PID" ] && wait "$FARM_PID" 2> /dev/null
  FARM_PID=
  GAME_PID=
}

trap 'echo "shader_bench: interrupted" >&2; stop_game; exit 130' INT TERM

# One bench at a time per work folder (the next one would replace the app
# copy under the running game).
LOCK=$WORK/.lock
mkdir "$LOCK" 2> /dev/null || die "another bench uses $WORK (else remove $LOCK)"
trap 'rmdir "$LOCK" 2> /dev/null' EXIT

# $1 cold|warm, $2 the run's folder: summary.json and summary.txt.
report() {
  python3 -I "$REPO/tools/shader_bench_report.py" "$2/game.log" --json "$2/summary.json" --label "$1" \
    | tee "$2/summary.txt"
}

# One run: $1 cold|warm, $2 its folder.
run_one() {
  local kind=$1 dir=$2 log=$2/game.log t0 now scene_t=
  mkdir -p "$dir" "$HOME_DIR" || die "cannot make $dir"
  [ "$kind" = cold ] && empty_caches
  prepare_save
  {
    echo "kind=$kind"
    echo "date=$(date '+%F %T')"
    echo "app=$APP"
    echo "bundle_id=$BENCH_ID"
    echo "port=$(git -C "$REPO" rev-parse --short HEAD 2> /dev/null)"
    echo "runtime=$(git -C "$REPO/vendor/x86recomp" rev-parse --short HEAD 2> /dev/null)"
    echo "game_dir=$GAME_DIR"
    echo "save=$SAVE"
    echo "bench=$BENCH"
    echo "resolution=${WIDTH}x$HEIGHT"
    echo "play_s=$PLAY"
    echo "runtime_cache=$RECOMP_CACHE"
    echo "metal_cache=$METAL_CACHE"
  } > "$dir/run.txt"

  export CFFIXED_USER_HOME=$HOME_DIR XDG_DATA_HOME=$DATA
  export GTAIV_HIDDEN=1 GTAIV_BACKGROUND=1 RECOMP_AUDIO_MUTE=1 GTAIV_VSYNC=0 GTAIV_GFX=metal
  export GTAIV_SKIP_INTRO=1 GTAIV_SAVE_SYNC=0 GTAIV_CONTINUE=1
  export GTAIV_PERF=1 GTAIV_HITCH=50 GTAIV_FPS_REPORT=10 GTAIV_LOAD_LOG=1 GTAIV_BENCH=$BENCH
  unset GTAIV_LOG  # it would take the game's log away from game.log
  env | grep -E '^(GTAIV_|RECOMP_|CFFIXED_USER_HOME=|XDG_DATA_HOME=)' | sort > "$dir/env.txt"

  echo "shader_bench: $kind run, log $log"
  (cd "$dir" && exec "$FARM" run --kind measure --who "$WHO" -- "$BIN" --no-launcher \
    --game-dir "$GAME_DIR" -- -norestrictions -availablevidmem 2047 -width "$WIDTH" -height "$HEIGHT") \
    > "$log" 2>&1 &
  FARM_PID=$!
  t0=
  while :; do
    sleep 1
    now=$(date +%s)
    if ! kill -0 "$FARM_PID" 2> /dev/null; then
      wait "$FARM_PID"
      echo "shader_bench: the game ended by itself (farm exit $?); last lines:" >&2
      tail -n 15 "$log" >&2
      FARM_PID=
      GAME_PID=
      echo "shader_bench: the summary of what it logged (not a full run):" >&2
      report "$kind" "$dir"
      return 1
    fi
    if grep -q "farm: .*measurement stopped" "$log"; then
      echo "shader_bench: the farm stopped the measurement (the owner's game): run discarded" >&2
      stop_game
      return 1
    fi
    if [ -z "$GAME_PID" ]; then
      find_game || continue
      t0=$now
      echo "shader_bench: the game started (PID $GAME_PID)"
    fi
    if [ -z "$scene_t" ]; then
      if grep -q "gtaiv: bench: player placed" "$log"; then
        scene_t=$now
        echo "shader_bench: scene after $((now - t0)) s; playing $PLAY s"
      elif [ $((now - t0)) -ge "$START_TIMEOUT" ]; then
        echo "shader_bench: no scene in $START_TIMEOUT s" >&2
        stop_game
        return 1
      fi
    elif [ $((now - scene_t)) -ge "$PLAY" ]; then
      break
    fi
  done
  stop_game
  report "$kind" "$dir"
}

STAMP=$(date +%Y%m%d-%H%M%S)
RUN=$OUT/$STAMP-$MODE
mkdir -p "$RUN" || die "cannot make $RUN"
echo "shader_bench: $RUN"
prepare_app
case $MODE in
  cold|warm)
    run_one "$MODE" "$RUN" || exit 1
    ;;
  both)
    run_one cold "$RUN/cold" || exit 1
    run_one warm "$RUN/warm" || exit 1
    echo
    python3 -I "$REPO/tools/shader_bench_report.py" --table "$RUN/cold/summary.json" "$RUN/warm/summary.json" \
      | tee "$RUN/summary.txt"
    ;;
esac
