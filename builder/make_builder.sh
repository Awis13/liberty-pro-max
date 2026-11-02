#!/bin/zsh
# make_builder.sh: makes "Liberty Pro Max Builder.app", the app that builds
# the game on a Mac with nothing but Apple's Command Line Tools.
#
#   GTAIV_GAME_DIR=/path/to/game builder/make_builder.sh
#
# Needs what ./install.sh needs (Homebrew's cmake, ninja, glslang,
# spirv-cross, llvm@22) and full Xcode (SwiftUI). The game folder is only
# used to run the reference build the recipe is taken from: nothing of the
# game goes into the Builder.
#
#   1. SDL3, FFmpeg, glslang, SPIRV-Cross for macOS from source
#      (tools/apple_deps.sh, PLATFORMS=macos): no Homebrew in what ships;
#   2. the reference build against them (out/kit): lifter, runtime, game;
#   3. the kit: lifter, 7-Zip, x86n.py, symbols, manifest, the port's and the
#      runtime's sources, static libraries, the app without its executable,
#      and the recipe (builder/make_recipe.py);
#   4. the Builder (SwiftUI) with the kit in its Resources, signed
#      (SIGN_ID, default ad hoc), zipped into out/builder.
set -euo pipefail
here=${0:A:h:h}
cd $here
[[ -f tools/local.env ]] && . tools/local.env
: ${GTAIV_GAME_DIR:?set GTAIV_GAME_DIR to the folder with GTAIV.exe (for the reference build)}
w=${GTAIV_APPLE_BUILD:-$here/out/apple}
deps=$w/prefix-macos
kitbuild=$here/out/kit
outdir=$here/out/builder
sevenzip=26.05

say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }

say "macOS libraries from source"
[[ -f $deps/lib/libSDL3.0.dylib ]] || PLATFORMS=macos tools/apple_deps.sh

say "Reference build"
PKG_CONFIG_LIBDIR=$deps/lib/pkgconfig PKG_CONFIG_SYSROOT_DIR= cmake -S . -B $kitbuild -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DCMAKE_PREFIX_PATH=$deps -DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew \
  -DRECOMP_GLSLANG=/opt/homebrew/bin/glslangValidator -DRECOMP_SPIRV_CROSS=/opt/homebrew/bin/spirv-cross \
  -DRECOMP_LLVM_ROOT=/opt/homebrew/opt/llvm@22 -DRECOMP_LLVM_DEMANGLE_SRC=$w/src/llvm-demangle \
  -DRECOMP_LTO=ON -DRECOMP_SSE_HOST=ON -DRECOMP_HOST_CPU=apple-m1 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
cmake --build $kitbuild --target recomp_lift
LIFTER=$kitbuild/x86recomp/tools/lifter/recomp_lift tools/lift.sh
cmake -S . -B $kitbuild > /dev/null  # the lifted files listed
cmake --build $kitbuild
for f in $kitbuild/x86recomp/*.dylib $kitbuild/x86recomp/tools/lifter/recomp_lift; do
  if otool -L $f | tail -n +2 | grep -q /opt/homebrew; then
    echo "$f links Homebrew:" >&2; otool -L $f >&2; exit 1
  fi
done

say "Kit"
kit=$outdir/kit
rm -rf $outdir && mkdir -p $kit/bin $kit/tools $kit/manifest $kit/include $kit/lib $kit/app
cp $kitbuild/x86recomp/tools/lifter/recomp_lift $kit/bin/
curl -fsSL https://github.com/ip7z/7zip/releases/download/$sevenzip/7z${sevenzip//./}-mac.tar.xz | tar xJ -C $kit/bin 7zz License.txt
mv $kit/bin/License.txt $kit/bin/7zz-License.txt
cp tools/x86n.py $kit/tools/
cp manifest/gtaiv.manifest manifest/native.list $kit/manifest/
cp -R symbols $kit/symbols
cp -R src $kit/src
rsync -a vendor/x86recomp/src/ $kit/include/runtime/  # headers, and lifted_table.cpp
cp -R $deps/include $kit/include/deps
cp $deps/lib/*.a $kit/lib/
app="$kit/app/Liberty Pro Max.app"
ditto "$kitbuild/Liberty Pro Max.app" "$app"
rm -f "$app/Contents/MacOS/gtaiv"
cp $deps/lib/libSDL3.0.dylib "$app/Contents/Frameworks/"
# No run paths of this machine in what ships.
for f in "$app"/Contents/Frameworks/*.dylib $kit/bin/recomp_lift; do
  otool -l "$f" | awk '/LC_RPATH/ {r=1} r && /path / {print $2; r=0}' | while read -r rp; do
    [[ $rp == @* ]] || install_name_tool -delete_rpath "$rp" "$f"
  done
done
# The lifter loads librecomp_image.dylib from the app's Frameworks.
install_name_tool -add_rpath "@executable_path/../app/Liberty Pro Max.app/Contents/Frameworks" $kit/bin/recomp_lift
python3 builder/make_recipe.py $kitbuild $here $deps $kit

say "Builder"
dev=${XCODE_DEVELOPER_DIR:-$(xcode-select -p)}
b="$outdir/Liberty Pro Max Builder.app"
mkdir -p "$b/Contents/MacOS" "$b/Contents/Resources"
DEVELOPER_DIR=$dev xcrun swiftc -O -parse-as-library -target arm64-apple-macos14.0 -swift-version 5 \
  builder/Sources/*.swift -o "$b/Contents/MacOS/Builder"
cp builder/Info.plist "$b/Contents/Info.plist"
mv $kit "$b/Contents/Resources/kit"
# SIGN_ID: a Developer ID Application identity (timestamped, for
# notarization: NOTARY_PROFILE, a notarytool keychain profile); default ad hoc.
# (The hardened runtime only with a Developer ID: it makes the lifter refuse
# the runtime's dylibs unless they carry the same team, which ad hoc has not.)
sign=${SIGN_ID:--}
[[ $sign == - ]] && opts=(--timestamp=none) || opts=(--timestamp --options runtime)
find "$b/Contents/Resources/kit" -type f -print0 | while IFS= read -r -d '' f; do
  file -b "$f" | grep -q '^Mach-O' && codesign --force $opts --sign "$sign" "$f"
done
codesign --force $opts --sign "$sign" "$b"
codesign --verify --strict "$b"
(cd $outdir && ditto -c -k --keepParent "Liberty Pro Max Builder.app" "Liberty Pro Max Builder.zip")
if [[ -n ${NOTARY_PROFILE:-} ]]; then
  xcrun notarytool submit "$outdir/Liberty Pro Max Builder.zip" --keychain-profile $NOTARY_PROFILE --wait
  xcrun stapler staple "$b"
  (cd $outdir && rm -f "Liberty Pro Max Builder.zip" &&
   ditto -c -k --keepParent "Liberty Pro Max Builder.app" "Liberty Pro Max Builder.zip")
fi
echo "builder: $outdir/Liberty Pro Max Builder.zip ($(du -sh "$outdir/Liberty Pro Max Builder.zip" | cut -f1))"
