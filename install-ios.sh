#!/bin/zsh
# install-ios.sh [--tv] TEAM_ID [BUNDLE_ID]
# Builds Liberty Pro Max for the iPad connected by cable (--tv: the Apple TV
# paired with Xcode), installs it and copies the game's files (15 GB) into
# it. Run ./install.sh first: this build uses the game's code it translated
# (generated/).
#   TEAM_ID    your Apple developer team (Xcode > Settings > Accounts)
#   BUNDLE_ID  the app's identifier (default io.github.<team>.libertypromax,
#              .libertypromax.tv for the Apple TV)
# Needs a paid Apple developer account signed in to Xcode, the device in
# Developer Mode.
set -euo pipefail
here=${0:A:h}
cd $here
say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\n\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

tv=0
[[ ${1:-} == --tv ]] && { tv=1; shift; }
team=${1:-}; [[ -n $team ]] || die "give your Apple developer team id: ./install-ios.sh [--tv] TEAM_ID"
if (( tv )); then
  p=tvos kind="Apple TV" preset=appletvos profile=libertypromax-tvos data=Library/Caches
  bundle=${2:-io.github.${(L)team}.libertypromax.tv}
else
  p=ios kind=iPad preset=iphoneos profile=libertypromax data=Documents
  bundle=${2:-io.github.${(L)team}.libertypromax}
fi
[[ -f tools/local.env ]] && . tools/local.env
[[ -n ${GTAIV_GAME_DIR:-} && -f $GTAIV_GAME_DIR/GTAIV.exe ]] || die "run ./install.sh first (it remembers the game folder)"
[[ -d generated/native ]] || die "run ./install.sh first: no translated game code in generated/"
xcode-select -p | grep -q Xcode.app || die "the $kind build needs Xcode (not only the Command Line Tools)"

say "$kind"
device=$(xcrun devicectl list devices 2>/dev/null | awk -v k="$kind" 'index($0, k) && /available/ && /physical/ {for (i=1;i<=NF;i++) if ($i ~ /^[0-9A-F]{8}-[0-9A-F]{16}$/) print $i}' | head -1)
[[ -n $device ]] || die "no $kind found (iPad: cable, unlocked; Apple TV: paired in Xcode > Devices; Developer Mode on)"
echo "device $device, app $bundle"

say "Build tools and the $kind's libraries"
brew install xcodegen
python3 -m venv out/venv && out/venv/bin/pip -q install pillow
[[ -d out/apple/prefix-$p ]] || PLATFORMS=$p tools/apple_deps.sh

say "Signing profile"
[[ -f out/apple/profiles/$profile.mobileprovision ]] || PLATFORM=$p tools/ios_profile.sh $team $bundle $device

if (( tv )); then
  say "Layered Apple TV icon"
  out/venv/bin/python -I tools/make_tvos_icon.py out/icons
else
  say "Icon, from the game's own logo"
  out/venv/bin/python -I tools/make_ios_icon.py "$GTAIV_GAME_DIR" out/icons
fi

say "The game for $kind"
cmake --preset $preset > /dev/null
cmake --build --preset $preset

say "Installing"
PLATFORM=$p BUILD=out/apple/build-$p PREFIX=out/apple/prefix-$p ICONS=out/icons tools/ios_bundle.sh $device

say "The game's files (15 GB; on the Apple TV over the network, use Ethernet)"
DEST=$data/game STATE=out/apple/push-$p.state python3 tools/ios_push_game.py "$GTAIV_GAME_DIR" $device $bundle

mods="$HOME/Library/Application Support/gtaiv/drive_c/mods"
if [[ -d $mods ]]; then
  say "Your mods"
  (cd $mods && find . -type f) | while read -r f; do
    xcrun devicectl device copy to --device $device --domain-type appDataContainer \
      --domain-identifier $bundle --source "$mods/${f#./}" \
      --destination "$data/gtaiv/drive_c/mods/${f#./}" --timeout 600 > /dev/null
  done
fi

say "Done"
if (( tv )); then
  echo "Open Liberty Pro Max on the Apple TV (it starts the game directly). A controller plays it."
else
  echo "Open Liberty Pro Max on the iPad, press Play. A controller or a keyboard plays it."
fi
