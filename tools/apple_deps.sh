#!/bin/zsh
# apple_deps.sh
# Builds what the iPad build links against into $GTAIV_APPLE_BUILD/prefix-ios
# (default out/apple); PLATFORMS=tvos (or "ios tvos") the Apple TV's into
# prefix-tvos:
#   SDL3 3.4.18           as a dylib (it goes into the app's Frameworks)
#   FFmpeg 9.0.2          static, only what the game uses: the Bink and MP3
#                         decoders and demuxers, swscale
#   glslang 16.6.0        static (HLSL to SPIR-V for the shaders)
#   SPIRV-Cross           static, vulkan-sdk-1.4.363.0 (SPIR-V to Metal)
# and fetches LLVM's Demangle sources (llvmorg-22.1.8) into
# $GTAIV_APPLE_BUILD/src/llvm-demangle, which the build compiles itself.
# Needs Xcode (DEVELOPER_DIR, or the selected one), CMake, Ninja, git, curl.
# The presets `iphoneos` and `appletvos` (CMakePresets.json) then build the
# game against them; they expect the default GTAIV_APPLE_BUILD, out/apple.
set -euo pipefail
here=${0:A:h:h}
[[ -f $here/tools/local.env ]] && . $here/tools/local.env
w=${GTAIV_APPLE_BUILD:-$here/out/apple}
src=$w/src
platforms=(${=PLATFORMS:-ios})
mkdir -p $src
cd $src

[[ -d SDL3-3.4.18 ]] || curl -fsSL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz | tar xz
[[ -d ffmpeg-9.0.2 ]] || curl -fsSL https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz | tar xJ
[[ -d glslang ]] || git clone -q --depth 1 --branch 16.6.0 https://github.com/KhronosGroup/glslang
[[ -d spirv-cross ]] || git clone -q --depth 1 --branch vulkan-sdk-1.4.363.0 https://github.com/KhronosGroup/SPIRV-Cross spirv-cross
if [[ ! -d llvm-demangle ]]; then
  mkdir llvm-demangle
  for f in Demangle.cpp DLangDemangle.cpp ItaniumDemangle.cpp MicrosoftDemangle.cpp \
           MicrosoftDemangleNodes.cpp RustDemangle.cpp; do
    curl -fsSL -o llvm-demangle/$f \
      https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-22.1.8/llvm/lib/Demangle/$f
  done
fi

for p in $platforms; do
  case $p in
    ios)  system=iOS  sdk=iphoneos  minflag=-miphoneos-version-min=17.0 ;;
    tvos) system=tvOS sdk=appletvos minflag=-mappletvos-version-min=17.0 ;;
    *) echo "unknown platform $p" >&2; exit 2 ;;
  esac
  prefix=$w/prefix-$p
  cmake_common=(-G Ninja -DCMAKE_SYSTEM_NAME=$system -DCMAKE_OSX_ARCHITECTURES=arm64
                -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=$prefix)
  echo "== $p: SDL3"
  cmake -S SDL3-3.4.18 -B $w/deps-$p/sdl3 $cmake_common -DSDL_SHARED=ON -DSDL_STATIC=OFF \
        -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF > $w/deps-$p-sdl3.log
  cmake --build $w/deps-$p/sdl3 >> $w/deps-$p-sdl3.log && cmake --install $w/deps-$p/sdl3 > /dev/null

  echo "== $p: glslang, SPIRV-Cross"
  cmake -S glslang -B $w/deps-$p/glslang $cmake_common -DBUILD_SHARED_LIBS=OFF \
        -DENABLE_GLSLANG_BINARIES=OFF -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DBUILD_EXTERNAL=OFF \
        -DENABLE_HLSL=ON > $w/deps-$p-glslang.log
  cmake --build $w/deps-$p/glslang >> $w/deps-$p-glslang.log && cmake --install $w/deps-$p/glslang > /dev/null
  cmake -S spirv-cross -B $w/deps-$p/spvc $cmake_common -DSPIRV_CROSS_CLI=OFF \
        -DSPIRV_CROSS_ENABLE_TESTS=OFF -DSPIRV_CROSS_SHARED=OFF > $w/deps-$p-spvc.log
  cmake --build $w/deps-$p/spvc >> $w/deps-$p-spvc.log && cmake --install $w/deps-$p/spvc > /dev/null

  echo "== $p: FFmpeg"
  mkdir -p $w/deps-$p/ffmpeg
  (cd $w/deps-$p/ffmpeg &&
   $src/ffmpeg-9.0.2/configure --prefix=$prefix --enable-cross-compile --target-os=darwin \
     --arch=aarch64 --cc="$(xcrun --sdk $sdk -f clang)" --sysroot="$(xcrun --sdk $sdk --show-sdk-path)" \
     --extra-cflags="-arch arm64 $minflag" --extra-ldflags="-arch arm64 $minflag" \
     --enable-static --disable-shared --disable-programs --disable-doc --disable-everything \
     --disable-network --disable-autodetect --enable-avcodec --enable-avformat --enable-avutil \
     --enable-swscale --enable-decoder=mp3,mp3float,bink,binkaudio_rdft,binkaudio_dct \
     --enable-parser=mpegaudio --enable-demuxer=bink,mp3 --enable-protocol=file --disable-asm \
     > $w/deps-$p-ffmpeg.log &&
   make -j"$(sysctl -n hw.ncpu)" >> $w/deps-$p-ffmpeg.log && make install > /dev/null)
  # "-framework X" as -Wl,-framework,X: CMake's de-duplication of a library
  # list drops repeated "-framework" words and leaves the bare names.
  sed -i '' -E 's/-framework ([A-Za-z]+)/-Wl,-framework,\1/g' $prefix/lib/pkgconfig/*.pc
  echo "== $p: done -> $prefix"
done
