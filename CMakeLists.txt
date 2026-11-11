cmake_minimum_required(VERSION 3.25)
project(liberty_pro_max LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)  # the Mac launcher (src/launcher_mac.mm)
endif()

set(CMAKE_CXX_STANDARD 20)
# iPad: the runtime's dylibs go into the app's Frameworks.
if(CMAKE_SYSTEM_NAME MATCHES "^(iOS|tvOS)$")
  set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
  set(CMAKE_INSTALL_RPATH "@executable_path/Frameworks")
  set(CMAKE_INSTALL_NAME_DIR "@rpath")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# x86recomp's runtime and HLE modules are shared libraries: rebuilding one
# does not relink the game.
set(CMAKE_LINK_DEPENDS_NO_SHARED ON)

# x86recomp: the lifter, x87 model and runtime (vendor/x86recomp, the
# commit pinned in X86RECOMP, on Awis13/x86recomp's `liberty` branch).
set(X86RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/x86recomp"
    CACHE PATH "x86recomp source tree")
set(DXVK_NATIVE_DIR "" CACHE PATH "dxvk-native install prefix (opt-in d3d9 backend)")

enable_testing()
# GTA IV's lifted code: ThinLTO, and SSE arithmetic as the host's (no
# NaN-exact checks: the game reads neither NaN payloads nor MXCSR flags).
# Both measured: 58-69 -> 71-83 fps on the benchmark street with PGO.
set(RECOMP_LTO ON CACHE BOOL "ThinLTO of lifted code")
set(RECOMP_SSE_HOST ON CACHE BOOL "Host SSE arithmetic in lifted code")
add_subdirectory("${X86RECOMP_DIR}" x86recomp)

# The game: runtime + HLE + lifted code (generated/, produced by
# recomp_lift from the user's own install; see README.md).
recomp_add_lifted(gtaiv_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(TARGET gtaiv_lifted)
  add_executable(gtaiv src/main.cpp src/game_args.cpp src/language.cpp src/paul.cpp src/integrity.cpp src/diagnostics.cpp src/widescreen.cpp src/crt_math.cpp
    src/bench.cpp src/fixes.cpp src/jobs.cpp src/options.cpp src/perf.cpp src/episodes.cpp src/mods.cpp src/threads.cpp src/native.cpp src/pad_icons.cpp
    src/community_fixes.cpp
    src/high_fps_fixes.cpp
    src/update_folder.cpp
    src/ff_shaders.cpp
    src/coronas.cpp
    src/camera_controls.cpp src/shadows_lights.cpp src/render_misc.cpp src/gameplay_misc.cpp src/stability.cpp src/vehicles_misc.cpp
    src/camera_controls2.cpp src/tree_shadows.cpp src/render_misc2.cpp src/ff_timecycext.cpp src/gameplay_misc2.cpp src/stability2.cpp
    $<TARGET_OBJECTS:gtaiv_lifted>)
  if(RECOMP_LTO)
    # (The lifted code's -mcpu travels in its bitcode.)
    target_link_options(gtaiv PRIVATE -flto=thin "LINKER:-cache_path_lto,${CMAKE_BINARY_DIR}/lto-cache")
  endif()
  if(RECOMP_PGO STREQUAL "generate")
    target_link_options(gtaiv PRIVATE -fprofile-instr-generate)
    target_compile_definitions(gtaiv PRIVATE GTAIV_PGO_GENERATE)
  endif()
  # The game's hot functions translated to C++ by tools/x86n.py from the
  # player's GTAIV.exe (tools/lift.sh writes them to generated/native/).
  target_include_directories(gtaiv PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")
  target_link_libraries(gtaiv PRIVATE recomp_runtime recomp_dsound recomp_bink
                        recomp_dinput8 recomp_xinput recomp_d3d9 recomp_xlive
                        recomp_dbghelp recomp_wmvcore)
  set_target_properties(gtaiv PROPERTIES ENABLE_EXPORTS ON)  # names in fault backtraces
  if(APPLE)
    # The system's preferred languages (main.cpp).
    target_link_libraries(gtaiv PRIVATE "-framework CoreFoundation")
  endif()
  if(RECOMP_IOS)
    # The .app with its Info.plist; tools/ios_bundle.sh adds the runtime
    # dylibs (Frameworks/), the icon, and signs it. The game's files go into
    # its Documents over the cable (tools/ios_push_game.py).
    if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
      set(GTAIV_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/port/ios/Info-tvOS.plist")
    else()
      set(GTAIV_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/port/ios/Info.plist")
    endif()
    set_target_properties(gtaiv PROPERTIES
      MACOSX_BUNDLE ON
      MACOSX_BUNDLE_INFO_PLIST "${GTAIV_INFO_PLIST}"
      INSTALL_RPATH "@executable_path/Frameworks"
      BUILD_WITH_INSTALL_RPATH ON)
    # The settings screen before the game (Objective-C++, ARC).
    target_sources(gtaiv PRIVATE src/launcher_ios.mm)
    set_source_files_properties(src/launcher_ios.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS")  # saves through iCloud Drive (tvOS has none)
      target_sources(gtaiv PRIVATE src/save_cloud.mm src/touch_ios.mm)
      set_source_files_properties(src/save_cloud.mm src/touch_ios.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
      target_link_libraries(gtaiv PRIVATE "-framework QuartzCore")
    endif()
    target_link_libraries(gtaiv PRIVATE "-framework UIKit" "-framework Foundation" "-framework CoreGraphics")
  elseif(APPLE)
    # The app's launcher window (Objective-C++, ARC).
    target_sources(gtaiv PRIVATE src/launcher_mac.mm src/save_sync_mac.mm src/save_cloud.mm)
    set_source_files_properties(src/launcher_mac.mm src/save_sync_mac.mm src/save_cloud.mm
                                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(gtaiv PRIVATE "-framework AppKit")
    # "Liberty Pro Max.app" next to the plain executable: a bundle that says
    # it is a game (macos/Info.plist), so that macOS turns Game Mode on
    # while it runs full screen in front. Its executable is the same build;
    # x86recomp's libraries go in Contents/Frameworks so the app can be
    # copied to /Applications. Signed (ad hoc) together with the bundle.
    # Its icon is out/icons/AppIcon.icns when that has been made
    # (tools/make_ios_icon.py --mac, from the player's game files).
    set_property(TARGET gtaiv APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
    set(app "${CMAKE_BINARY_DIR}/Liberty Pro Max.app")
    add_custom_command(TARGET gtaiv POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${app}/Contents/MacOS" "${app}/Contents/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist"
              "${app}/Contents/Info.plist"
      # (Removed first: a running copy keeps its file.)
      COMMAND ${CMAKE_COMMAND} -E rm -f "${app}/Contents/MacOS/gtaiv"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:gtaiv>" "${app}/Contents/MacOS/gtaiv"
      COMMAND sh -c "cp -f '${CMAKE_BINARY_DIR}'/x86recomp/*.dylib '${app}/Contents/Frameworks/' && codesign --force --sign - '${app}/Contents/Frameworks/'*.dylib"
      COMMAND sh -c "[ ! -f '${CMAKE_CURRENT_SOURCE_DIR}/out/icons/AppIcon.icns' ] || { mkdir -p '${app}/Contents/Resources' && cp -f '${CMAKE_CURRENT_SOURCE_DIR}/out/icons/AppIcon.icns' '${app}/Contents/Resources/'; }"
      COMMAND codesign --force --sign - "${app}"
      VERBATIM)
  endif()
endif()

# Opt-in unit tests of the port's plain C++ (no game files needed):
# cmake -DGTAIV_TESTS=ON, then ctest.
option(GTAIV_TESTS "Build the port's unit tests" OFF)
if(GTAIV_TESTS AND NOT RECOMP_IOS)
  add_executable(gtaiv_game_args_test tests/game_args_test.cpp src/game_args.cpp)
  target_include_directories(gtaiv_game_args_test PRIVATE src)
  add_test(NAME game_args COMMAND gtaiv_game_args_test)
  add_executable(gtaiv_first_menu_test tests/first_menu_test.cpp)
  target_include_directories(gtaiv_first_menu_test PRIVATE src)
  add_test(NAME first_menu COMMAND gtaiv_first_menu_test)
  add_executable(gtaiv_save_paths_test tests/save_paths_test.cpp)
  target_include_directories(gtaiv_save_paths_test PRIVATE src)
  add_test(NAME save_paths COMMAND gtaiv_save_paths_test)
  add_executable(gtaiv_night_shadow_slots_test tests/night_shadow_slots_test.cpp)
  target_include_directories(gtaiv_night_shadow_slots_test PRIVATE src)
  target_compile_features(gtaiv_night_shadow_slots_test PRIVATE cxx_std_20)
  add_test(NAME night_shadow_slots COMMAND gtaiv_night_shadow_slots_test)
  add_executable(gtaiv_shadow_anticull_test tests/shadow_anticull_test.cpp)
  target_include_directories(gtaiv_shadow_anticull_test PRIVATE src)
  target_compile_features(gtaiv_shadow_anticull_test PRIVATE cxx_std_20)
  add_test(NAME shadow_anticull COMMAND gtaiv_shadow_anticull_test)
  add_executable(gtaiv_language_test tests/language_test.cpp)
  target_include_directories(gtaiv_language_test PRIVATE src)
  add_test(NAME language COMMAND gtaiv_language_test)
  # The tools' Python tests (tests/test_*.py), so that ctest runs them too.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME python_tools COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests)
  endif()
  if(APPLE)
    add_executable(gtaiv_launcher_options_test tests/launcher_options_test.mm)
    target_include_directories(gtaiv_launcher_options_test PRIVATE src)
    target_compile_options(gtaiv_launcher_options_test PRIVATE -fobjc-arc)
    target_link_libraries(gtaiv_launcher_options_test PRIVATE "-framework Foundation")
    add_test(NAME launcher_options COMMAND gtaiv_launcher_options_test)
  endif()
endif()

# Opt-in native application-loop regression tests.
option(GTAIV_LAUNCHER_TESTS "Build macOS launcher integration tests" OFF)
if(APPLE AND NOT RECOMP_IOS AND GTAIV_LAUNCHER_TESTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(gtaiv_launcher_quit_test tests/launcher_quit_test.mm)
  target_include_directories(gtaiv_launcher_quit_test PRIVATE src)
  target_compile_options(gtaiv_launcher_quit_test PRIVATE -fobjc-arc)
  target_link_libraries(gtaiv_launcher_quit_test PRIVATE "-framework AppKit")
  foreach(action keyboard dock window)
    add_test(NAME launcher_quit_${action}
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_quit_test.py"
        "$<TARGET_FILE:gtaiv_launcher_quit_test>" ${action})
    set_tests_properties(launcher_quit_${action} PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
  endforeach()
endif()
