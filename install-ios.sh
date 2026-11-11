#!/bin/zsh
# install-ios.sh [--ipad | --iphone | --tv] TEAM_ID [BUNDLE_ID]
# Builds Liberty Pro Max for the iPad or iPhone connected by cable (the iPad
# when both are; --ipad or --iphone says which. --tv: the Apple TV paired
# with Xcode), installs it and copies the game's files (15 GB) into it. Run
# ./install.sh first: this build uses the game's code it translated
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

tv=0 want=
case ${1:-} in
  --tv) tv=1; shift ;;
  --ipad) want=iPad; shift ;;
  --iphone) want=iPhone; shift ;;
esac
team=${1:-}; [[ -n $team ]] || die "give your Apple developer team id: ./install-ios.sh [--ipad | --iphone | --tv] TEAM_ID"
if (( tv )); then
  p=tvos kind="Apple TV" preset=appletvos profile=libertypromax-tvos data=Library/Caches
  bundle=${2:-io.github.${(L)team}.libertypromax.tv}
else
  # (kind: which of the two is found below.)
  p=ios kind=${want:-iPad or iPhone} preset=iphoneos profile=libertypromax data=Documents
  bundle=${2:-io.github.${(L)team}.libertypromax}
fi
[[ -f tools/local.env ]] && . tools/local.env
[[ -n ${GTAIV_GAME_DIR:-} && -f $GTAIV_GAME_DIR/GTAIV.exe ]] || die "run ./install.sh first (it remembers the game folder)"
[[ -d generated/native ]] || die "run ./install.sh first: no translated game code in generated/"
xcode-select -p | grep -q Xcode.app || die "the $kind build needs Xcode (not only the Command Line Tools)"

say "$kind"
# The record of the files already sent (ios_push_game.py's STATE): the
# iPad's is push-ios.state, the iPhone's push-iphone.state (below), the Apple
# TV's push-tvos.state. Keep the names: a new name would send all 15 GB again.
state=out/apple/push-$p.state
# The first available physical device whose line in devicectl's list matches
# $1 (a regular expression): its identifier.
find_device() {
  xcrun devicectl list devices 2>/dev/null | awk -v k="$1" '$0 ~ k && /available/ && !/unavailable/ && /physical/ {for (i=1;i<=NF;i++) if ($i ~ /^[0-9A-F]{8}-[0-9A-F]{16}$/) print $i}' | head -1
}
if (( tv )); then
  device=$(find_device 'Apple TV')
else
  # By the model's code ("iPad16,10", "iPhone18,1"), not the name its owner
  # gave it.
  ipad=$(find_device 'iPad[0-9]+,[0-9]+') iphone=$(find_device 'iPhone[0-9]+,[0-9]+')
  [[ $want == iPhone ]] && ipad=
  [[ $want == iPad ]] && iphone=
  if [[ -n $ipad ]]; then
    kind=iPad device=$ipad
    [[ -n $iphone ]] && echo "An iPad and an iPhone are connected: installing on the iPad (--iphone for the iPhone)."
  elif [[ -n $iphone ]]; then
    # (Its own record of the files sent: the iPad's says nothing about it.)
    kind=iPhone device=$iphone state=out/apple/push-iphone.state
  else
    device=
  fi
fi
[[ -n $device ]] || die "no $kind found (iPad, iPhone: cable, unlocked; Apple TV: paired in Xcode > Devices; Developer Mode on)"
echo "$kind $device, app $bundle"

say "Build tools and the $kind's libraries"
brew install xcodegen
python3 -m venv out/venv && out/venv/bin/pip -q install pillow
[[ -d out/apple/prefix-$p ]] || PLATFORMS=$p tools/apple_deps.sh

say "Signing profile"
# (A profile made before this device was registered does not list it, and
# the app would not install: made again then.)
made=out/apple/profiles/$profile.mobileprovision
if [[ ! -f $made ]] || { (( ! tv )) && ! security cms -D -i $made 2>/dev/null | grep -q "<string>$device</string>"; }; then
  PLATFORM=$p tools/ios_profile.sh $team $bundle $device
fi

say "Icon, from the game's own logo"
out/venv/bin/python -I tools/make_ios_icon.py "$GTAIV_GAME_DIR" out/icons
if (( tv )); then
  out/venv/bin/python -I tools/make_tvos_icon.py out/icons
fi

say "The game for $kind"
cmake --preset $preset > /dev/null
cmake --build --preset $preset

say "Installing"
PLATFORM=$p BUILD=out/apple/build-$p PREFIX=out/apple/prefix-$p ICONS=out/icons tools/ios_bundle.sh $device

say "The game's files (15 GB; on the Apple TV over the network, use Ethernet)"
DEST=$data/game STATE=$state python3 tools/ios_push_game.py "$GTAIV_GAME_DIR" $device $bundle

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
  if [[ $kind == iPhone ]]; then
    echo "Open Liberty Pro Max on the iPhone, press Play. A controller or a keyboard plays it best: the touch controls are the iPad's, small on a phone."
  else
    echo "Open Liberty Pro Max on the iPad, press Play. The touch controls, a controller or a keyboard play it."
  fi
fi
