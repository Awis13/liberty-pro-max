# macOS shader preparation

Open **Shaders** in the startup window and choose **Precompile shaders** to do
shader compilation before playing. You need a complete GTA IV 1.0.8.0 install
and Apple's Metal Toolchain. Install the toolchain in Xcode Settings → Components,
or run `xcodebuild -downloadComponent MetalToolchain`.

Preparation is optional. New render states and world streaming can still cause
stalls during gameplay. **Prepared** means the current shader and recipe set has
finished; it does not mean the game has encountered every possible pipeline.

## Preparation and recovery

The worker reports Discovering, Compiling shaders and Building pipelines. Counts
include completed attempts; failures appear separately with an error log. Cancel
keeps completed cache entries. Run preparation again to verify those entries and
retry unfinished work. Missing files, compiler failures, unwritable storage and
less than 1 GiB of free cache-volume space produce a recovery message.

The launcher runs the worker in another process so the UI stays responsive. Play
and cache cleaning wait for the worker to stop. Closing the launcher cancels it,
with a two-second kill fallback. Changing the game folder refreshes status with
current options and discards responses from the old folder.

The default cache is `~/Library/Caches/gtaiv/metal`. You can choose another parent
folder in the Shaders tab. **Clean cache…** removes its `metal` subtree after
confirmation. The adjacent `metal.lock` coordinates workers and remains in place.
Cleaning the cache forces subsequent compilation; it does not touch game files
or saves.

## Game rules and renderer ownership

The pin in `X86RECOMP` identifies the dependency repository, branch and commit.
CMake consumes that source directly. x86recomp owns Metal compilation, persistent
libraries and archives, integrity checks, runtime recording and startup preload.

The game supplies a `MetalPreparationProfile` from `src/shader_profile_mac.mm`.
Its discovery code reads loose `common/shaders/win32_30*/*.fxc` files and named
resources in `GTAIV.exe`. Embedded effects can differ from loose files; both
contribute to preparation. Shader payloads come from the selected installation.

`src/metal_baseline_mac.h` selects candidate render states from effect techniques
and passes. Pipeline identity includes shader pairs, attachment formats, depth,
blending and write masks. The baseline covers common deferred, shadow, reflection,
forward and post-processing passes, plus selected cross-effect stencil pairs.
It prepares renderer-owned clears, copies, SMAA and TAA through x86recomp. Other
combinations enter the cache as the game draws them.

A source-derived profile identity invalidates preparation after game-rule changes.
The game executable and the standalone `recomp_shader_prepare` worker use the same
profile, so their discovery and recipe policy agree.

## Cache compatibility and runtime use

Build-specific state lives under `metal/v1/<scope>`. The scope includes game
content, renderer/translator source, profile policy, render settings, OS, GPU and
selected compiler/SDK identity. Relevant changes select a new scope and require
preparation again.

Compiled artifacts also live under `metal/artifacts/v1/<compatibility>`. Their
keys describe exact sources and pipeline descriptors; compatible artifacts can
survive source-build and profile-policy changes. Preparation status, translations,
observations and startup catalogs remain specific to their scope. Reusing a
compiled artifact does not mark a new scope Prepared.

The runtime preloads the baseline catalog for first play and selects observed
states on subsequent starts. It records newly used combinations and saves
libraries and archives away from drawing. A damaged or unavailable entry falls
back to runtime compilation. Source-built and offline-loaded libraries use
separate archives because Metal can produce different IR for them.

## Apple GPU archiver workspaces

Metal creates disposable GPU archiver workspaces while building archives. The
bundled app reclaims its own `com.apple.gpuarchiver` contents at idle startup and
after process teardown. A shared lifecycle lock prevents cleanup while another
launcher, game or preparation worker uses that bundle identity.

The exit helper waits for the parent to close its lifetime pipe and for a kernel
exit notification. Cleanup rejects symlinks and stays within the owned bundle
namespace. It preserves reusable shader-cache entries, Apple's `com.apple.metal`
cache and other applications' workspaces. Plain command-line tools skip this
bundle-specific maintenance.

## Build and tests

```sh
cmake --preset release -DGTAIV_SHADER_TESTS=ON -DGTAIV_LAUNCHER_TESTS=ON -DGTAIV_RUNTIME_TESTS=ON
cmake --build --preset release
ctest --test-dir out/build/release --output-on-failure
```

Metal and launcher tests need a logged-in macOS session and a Metal GPU. They
use original synthetic fixtures and isolated caches. Game tests cover discovery,
preparation/retry, launcher lifecycle, embedded effects, archiver cleanup and the
native frame-limit hook. x86recomp supplies runtime-cache, recipe-reuse, allocation,
COM/WMI and frame-pacer regressions.

The app and `out/build/release/recomp_shader_prepare` accept the same commands:

```sh
./out/build/release/recomp_shader_prepare --shader-preparation status --game-dir "$GAME_DIR" --cache-root "$CACHE_PARENT"
./out/build/release/recomp_shader_prepare --shader-preparation prepare --game-dir "$GAME_DIR" --cache-root "$CACHE_PARENT"
./out/build/release/recomp_shader_prepare --shader-preparation clean --cache-root "$CACHE_PARENT"
```

`RECOMP_METAL_PREPARE_JOBS=1..32` limits preparation concurrency without changing
cache compatibility. `RECOMP_SHADER_CACHE_REPORT=1` enables diagnostic cache logs.
Keep detailed logging separate from performance measurements. See
[benchmarking](benchmarking.md) for capture tools and output locations.
