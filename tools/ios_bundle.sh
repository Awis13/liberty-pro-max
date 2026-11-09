#!/bin/zsh
# ios_bundle.sh [device id]
# Turns the iPad build (cmake --preset iphoneos), or with PLATFORM=tvos the
# Apple TV one (cmake --preset appletvos), into a signed Liberty Pro Max.app
# and, given a device id, installs it:
#   - the runtime's dylibs and SDL3 into Frameworks/;
#   - the icon through an asset catalog (ICONS/icon1024.png, tvOS: ICONS/tvos/);
#   - the provisioning profile (PROFILE) and entitlements, then the signature.
# The game's files are not in the bundle (15 GB): tools/ios_push_game.py
# copies them into the app's Documents/game over the cable.
# Machine settings come from the environment (or tools/local.env):
#   GTAIV_APPLE_BUILD  the root of the Apple builds (default out/apple):
#                      build-ios, prefix-ios, profiles/libertypromax.mobileprovision
#                      and the app made here (out/)
#   BUILD, PREFIX, OUT, PROFILE  any one of those, set apart
# The app's bundle identifier is the profile's (its App ID without the team).
#   SIGN_ID  the signing identity's SHA-1 (default: one in the profile)
#   ICONS    the icons (default out/icons)
set -e
here=${0:A:h:h}
device=$1
[[ -f $here/tools/local.env ]] && . $here/tools/local.env
w=${GTAIV_APPLE_BUILD:-$here/out/apple}
if [[ ${PLATFORM:-ios} == tvos ]]; then
  : ${BUILD:=$w/build-tvos} ${PREFIX:=$w/prefix-tvos} ${OUT:=$w/out-tvos}
  : ${PROFILE:=$w/profiles/libertypromax-tvos.mobileprovision}
  entitlements=$here/port/ios/LibertyProMax-tvOS.entitlements
else
  : ${BUILD:=$w/build-ios} ${PREFIX:=$w/prefix-ios} ${OUT:=$w/out}
  : ${PROFILE:=$w/profiles/libertypromax.mobileprovision}
  entitlements=$here/port/ios/LibertyProMax.entitlements
fi
: ${ICONS:=$here/out/icons}
# Default identity: a valid one whose certificate is in the profile.
if [[ -z $SIGN_ID ]]; then
  certs=$(security cms -D -i $PROFILE | python3 -I -c 'import plistlib,sys,hashlib
for c in plistlib.loads(sys.stdin.buffer.read())["DeveloperCertificates"]: print(hashlib.sha1(c).hexdigest().upper())')
  for id in $(security find-identity -v -p codesigning | awk '/Apple Development/ {print $2}'); do
    if print -r -- $certs | grep -qx $id; then SIGN_ID=$id; break; fi
  done
fi
[[ -n $SIGN_ID ]] || { echo "no Apple Development signing identity; set SIGN_ID" >&2; exit 1; }
[[ -d $BUILD/gtaiv.app ]] || { echo "build first: $BUILD/gtaiv.app" >&2; exit 1; }

app=$OUT/LibertyProMax.app
mkdir -p $OUT
rsync -a --delete --exclude Frameworks $BUILD/gtaiv.app/ $app/

# Runtime libraries: the build's dylibs (install names @rpath/...) and SDL.
mkdir -p $app/Frameworks
rsync -a --delete $BUILD/x86recomp/librecomp_*.dylib $PREFIX/lib/libSDL3.0.dylib $app/Frameworks/

# The launcher's logo (the icon's artwork).
[[ -f $ICONS/icon1024.png ]] && cp $ICONS/icon1024.png $app/LauncherLogo.png

# Icon: an asset catalog compiled by actool, its keys merged in.
icons=$OUT/icon
mkdir -p $icons
rm -f $icons/partial.plist
if [[ ${PLATFORM:-ios} == tvos ]]; then
  if [[ -d $ICONS/tvos/Assets.xcassets ]]; then
    xcrun actool $ICONS/tvos/Assets.xcassets --compile $app --platform appletvos \
      --minimum-deployment-target 17.0 --app-icon AppIcon --target-device tv \
      --output-partial-info-plist $icons/partial.plist \
      --errors --warnings > $icons/actool.log
  else
    echo "no tvOS icons in $ICONS/tvos (tools/make_tvos_icon.py): the app has none"
  fi
elif [[ ! -f $ICONS/icon1024.png ]]; then
  echo "no icons in $ICONS (tools/make_ios_icon.py): the app has none"
else
mkdir -p $icons/Assets.xcassets/AppIcon.appiconset
cp $ICONS/icon1024.png $icons/Assets.xcassets/AppIcon.appiconset/icon1024.png
cat > $icons/Assets.xcassets/Contents.json <<'JSON'
{"info":{"author":"xcode","version":1}}
JSON
cat > $icons/Assets.xcassets/AppIcon.appiconset/Contents.json <<'JSON'
{"images":[{"filename":"icon1024.png","idiom":"universal","platform":"ios","size":"1024x1024"}],
 "info":{"author":"xcode","version":1}}
JSON
xcrun actool $icons/Assets.xcassets --compile $app --platform iphoneos \
    --minimum-deployment-target 17.0 --app-icon AppIcon --target-device iphone \
    --target-device ipad --output-partial-info-plist $icons/partial.plist \
    --errors --warnings > $icons/actool.log
fi
[[ -f $icons/partial.plist ]] && /usr/libexec/PlistBuddy -c "Merge $icons/partial.plist" $app/Info.plist > /dev/null

# Signature: the profile's app and team identifiers into the entitlements
# (nothing team-specific lives in the repository), the dylibs first.
cp $PROFILE $app/embedded.mobileprovision
ent=$OUT/entitlements.plist
cp $entitlements $ent
prof_ent=$OUT/profile-entitlements.plist
security cms -D -i $PROFILE | plutil -extract Entitlements xml1 -o $prof_ent -
# The bundle identifier: the profile's App ID without its team prefix.
appid=$(/usr/libexec/PlistBuddy -c "Print :application-identifier" $prof_ent)
/usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier ${appid#*.}" $app/Info.plist
for k in application-identifier com.apple.developer.team-identifier; do
  v=$(/usr/libexec/PlistBuddy -c "Print :$k" $prof_ent)
  /usr/libexec/PlistBuddy -c "Delete :$k" $ent 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Add :$k string $v" $ent
done
# iCloud Drive for the saves (src/save_cloud.mm): the app's own container,
# iCloud.<bundle id>, shown in iCloud Drive as "Liberty Pro Max" (and so on
# the Mac too). Left out when the profile has no iCloud (the saves then
# stay on the device).
container=iCloud.${appid#*.}
if /usr/libexec/PlistBuddy -c "Print :com.apple.developer.ubiquity-container-identifiers" $prof_ent > /dev/null 2>&1; then
  sed -i '' "s/iCloud\.\$(PRODUCT_BUNDLE_IDENTIFIER)/$container/g" $ent
  /usr/libexec/PlistBuddy -c "Delete :NSUbiquitousContainers" $app/Info.plist 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Add :NSUbiquitousContainers dict" \
    -c "Add :NSUbiquitousContainers:$container dict" \
    -c "Add :NSUbiquitousContainers:$container:NSUbiquitousContainerIsDocumentScopePublic bool true" \
    -c "Add :NSUbiquitousContainers:$container:NSUbiquitousContainerName string Liberty Pro Max" \
    -c "Add :NSUbiquitousContainers:$container:NSUbiquitousContainerSupportedFolderLevels string Any" \
    $app/Info.plist
else
  for k in com.apple.developer.icloud-container-identifiers com.apple.developer.icloud-services \
           com.apple.developer.ubiquity-container-identifiers; do
    /usr/libexec/PlistBuddy -c "Delete :$k" $ent 2>/dev/null || true
  done
fi
# Every library must be built for this platform: dyld refuses an iOS one
# on tvOS only at launch, with the app already installed.
want=${${PLATFORM:-ios}:u}
for d in $app/Frameworks/*.dylib; do
  have=$(vtool -show-build $d | awk '/platform/ {print $2; exit}')
  [[ $have == $want ]] || { echo "$d: built for $have, not $want" >&2; exit 1; }
done
for d in $app/Frameworks/*.dylib; do
  codesign --force --sign $SIGN_ID --timestamp=none $d
done
codesign --force --sign $SIGN_ID --entitlements $ent --timestamp=none \
         --generate-entitlement-der $app
codesign --verify --strict $app
echo "signed: $app ($(du -sh $app | cut -f1))"
if [[ -n $device ]]; then
  xcrun devicectl device install app --device $device $app
fi
