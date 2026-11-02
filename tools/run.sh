#!/bin/bash
# Build the changed parts and run the game once in the private Xvnc :80.
# Usage: run.sh <name> [seconds] [-- game args]   -> run/<name>.log
# Take ~/labs/sfctl/game.lock first (the rig runs one game at a time).
set -u
name=$1; secs=${2:-60}; shift; [ $# -gt 0 ] && shift
cd ~/labs/gtaiv/repo || exit 1
cmake --build --preset release --target gtaiv > ../logs/build-iter.log 2>&1 || { grep -E "error|FAILED" ../logs/build-iter.log | head -20; exit 1; }
pgrep -f "Xvnc :80" > /dev/null || { Xvnc :80 -geometry 1280x720 -depth 24 -SecurityTypes None -localhost -rfbport 5980 -rendernode /dev/dri/renderD128 > ~/labs/gtaiv/logs/xvnc80.log 2>&1 & sleep 2; }
cd ~/labs/gtaiv/run
export DISPLAY=:80 SDL_AUDIODRIVER=dummy GTAIV_BACKGROUND=1 XDG_DATA_HOME=$HOME/labs/gtaiv/run/data
timeout "$secs" ../repo/out/build/release/gtaiv --game-dir ~/labs/gtaiv/game "$@" > "$name.log" 2>&1
echo "rc=$?"
grep -v "^/home\|^\.\./repo" "$name.log" | tail -40
