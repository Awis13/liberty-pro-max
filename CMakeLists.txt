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
# repository and commit pinned in X86RECOMP).
set(X86RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/x86recomp"
    CACHE PATH "x86recomp source tree")
set(DXVK_NATIVE_DIR "" CACHE PATH "dxvk-native install prefix (opt-in d3d9 backend)")

enable_testing()
# GTA IV's lifted code: ThinLTO, and SSE arithmetic as the host's (no
# NaN-exact checks: the game reads neither NaN payloads nor MXCSR flags).
# Both measured: 58-69 -> 71-83 fps on the benchmark street with PGO.
set(RECOMP_LTO ON CACHE BOOL "ThinLTO of lifted code")
set(RECOMP_SSE_HOST ON CACHE BOOL "Host SSE arithmetic in lifted code")
option(GTAIV_RUNTIME_TESTS "Build guest runtime regression tests" OFF)
option(GTAIV_SHADER_TESTS "Build macOS shader preparation integration tests" OFF)
set(RECOMP_RUNTIME_TESTS ${GTAIV_RUNTIME_TESTS})
set(RECOMP_METAL_TESTS ${GTAIV_SHADER_TESTS})
add_subdirectory("${X86RECOMP_DIR}" x86recomp)

if(APPLE AND NOT RECOMP_IOS)
  add_library(gtaiv_shader_profile STATIC src/shader_profile_mac.mm)
  target_include_directories(gtaiv_shader_profile PUBLIC src)
  target_compile_options(gtaiv_shader_profile PRIVATE -fobjc-arc -std=c++20)
  target_link_libraries(gtaiv_shader_profile PUBLIC recomp_gfx "-framework Foundation" "-framework Metal")
  set(_profile_identity "")
  foreach(_input shader_profile_mac.mm shader_profile_mac.h metal_baseline_mac.h metal_effects_mac.h)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/src/${_input}" _input_hash)
    string(APPEND _profile_identity "${_input_hash}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/${_input}")
  endforeach()
  string(SHA256 _profile_identity "${_profile_identity}")
  target_compile_definitions(gtaiv_shader_profile PRIVATE GTAIV_METAL_PROFILE_ID="${_profile_identity}")
  add_executable(recomp_shader_prepare src/shader_prepare_main.cpp)
  target_link_libraries(recomp_shader_prepare PRIVATE gtaiv_shader_profile)
endif()

# The game: runtime + HLE + lifted code (generated/, produced by
# recomp_lift from the user's own install; see README.md).
recomp_add_lifted(gtaiv_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(TARGET gtaiv_lifted)
  add_executable(gtaiv src/main.cpp src/paul.cpp src/integrity.cpp src/diagnostics.cpp src/widescreen.cpp src/crt_math.cpp
    src/bench.cpp src/fixes.cpp src/jobs.cpp src/options.cpp src/perf.cpp src/episodes.cpp src/mods.cpp src/threads.cpp src/native.cpp
    src/community_fixes.cpp
    src/high_fps_fixes.cpp src/frame_pacing.cpp
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
                        recomp_dbghelp)
  set_target_properties(gtaiv PROPERTIES ENABLE_EXPORTS ON)  # names in fault backtraces
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
    target_sources(gtaiv PRIVATE src/launcher_mac.mm src/shader_preparation_mac.mm src/metal_archiver_mac.mm src/save_sync_mac.mm src/save_cloud.mm)
    set_source_files_properties(src/launcher_mac.mm src/shader_preparation_mac.mm src/metal_archiver_mac.mm src/save_sync_mac.mm src/save_cloud.mm
                                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(gtaiv PRIVATE gtaiv_shader_profile "-framework AppKit")
    # "Liberty Pro Max.app" next to the plain executable: a bundle that says
    # it is a game (macos/Info.plist), so that macOS turns Game Mode on
    # while it runs full screen in front. Its executable is the same build;
    # x86recomp's libraries go in Contents/Frameworks so the app can be
    # copied to /Applications. Signed (ad hoc) together with the bundle.
    set_property(TARGET gtaiv APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
    set(app "${CMAKE_BINARY_DIR}/Liberty Pro Max.app")
    # Ninja may skip relinking after a dylib changes without changing exports.
    # Refresh the bundled renderer and report providers when their implementation
    # changes too, even when their exported symbols stay the same.
    set_property(TARGET gtaiv APPEND PROPERTY LINK_DEPENDS
      "$<TARGET_FILE:recomp_gfx>" "$<TARGET_FILE:recomp_xinput>"
      "$<TARGET_FILE:recomp_runtime>")
    add_custom_command(TARGET gtaiv POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${app}/Contents/MacOS" "${app}/Contents/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist"
              "${app}/Contents/Info.plist"
      # (Removed first: a running copy keeps its file.)
      COMMAND ${CMAKE_COMMAND} -E rm -f "${app}/Contents/MacOS/gtaiv"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:gtaiv>" "${app}/Contents/MacOS/gtaiv"
      COMMAND sh -c "cp -f '${CMAKE_BINARY_DIR}'/x86recomp/*.dylib '${app}/Contents/Frameworks/' && codesign --force --sign - '${app}/Contents/Frameworks/'*.dylib"
      COMMAND codesign --force --sign - "${app}"
      VERBATIM)
  endif()
endif()

# Opt-in HLE regressions require neither game files nor a GPU.
if(GTAIV_RUNTIME_TESTS)
  add_executable(gtaiv_native_frame_limit_test tests/native_frame_limit_test.cpp src/frame_pacing.cpp)
  target_compile_options(gtaiv_native_frame_limit_test PRIVATE -UNDEBUG)
  target_link_libraries(gtaiv_native_frame_limit_test PRIVATE recomp_runtime)
  foreach(limit 0 60 144)
    add_test(NAME native_frame_limit_${limit} COMMAND gtaiv_native_frame_limit_test ${limit} 1)
  endforeach()
  foreach(limit unset invalid -1 nan inf)
    add_test(NAME native_frame_limit_${limit} COMMAND gtaiv_native_frame_limit_test ${limit} 0)
  endforeach()
endif()

# Opt-in native application-loop regression tests.
option(GTAIV_LAUNCHER_TESTS "Build macOS launcher integration tests" OFF)
if(APPLE AND NOT RECOMP_IOS AND GTAIV_LAUNCHER_TESTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(gtaiv_launcher_quit_test tests/launcher_quit_test.mm src/shader_preparation_mac.mm)
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

# Opt-in integration tests need a logged-in macOS session and a Metal GPU.
if(APPLE AND NOT RECOMP_IOS AND GTAIV_SHADER_TESTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(gtaiv_metal_archiver_test tests/metal_archiver_test.mm src/metal_archiver_mac.mm)
  target_include_directories(gtaiv_metal_archiver_test PRIVATE src)
  target_compile_options(gtaiv_metal_archiver_test PRIVATE -fobjc-arc -std=c++20)
  target_link_libraries(gtaiv_metal_archiver_test PRIVATE "-framework Foundation" "-framework Metal")
  add_library(gtaiv_metal_archiver_probe SHARED tests/metal_archiver_probe.cpp)
  add_dependencies(gtaiv_metal_archiver_test gtaiv_metal_archiver_probe)
  add_test(NAME metal_archiver
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/metal_archiver_test.py"
      "$<TARGET_FILE:gtaiv_metal_archiver_test>" "$<TARGET_FILE:gtaiv_metal_archiver_probe>")
  set_tests_properties(metal_archiver PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  add_executable(gtaiv_metal_effects_test tests/metal_effects_test.cpp)
  target_include_directories(gtaiv_metal_effects_test PRIVATE src)
  target_compile_options(gtaiv_metal_effects_test PRIVATE -UNDEBUG)
  add_test(NAME metal_embedded_effects COMMAND gtaiv_metal_effects_test)
  add_test(NAME shader_preparation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_preparation_test.py"
      "$<TARGET_FILE:recomp_shader_prepare>")
  set_tests_properties(shader_preparation PROPERTIES TIMEOUT 300)
  add_test(NAME shader_capture_harness
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_capture_test.py")
  set_tests_properties(shader_capture_harness PROPERTIES TIMEOUT 20)
  add_test(NAME performance_measurement_harness
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/performance_compare_test.py")
  set_tests_properties(performance_measurement_harness PROPERTIES TIMEOUT 20)
  add_executable(gtaiv_launcher_shader_test tests/launcher_shader_test.mm src/shader_preparation_mac.mm)
  target_include_directories(gtaiv_launcher_shader_test PRIVATE src)
  target_compile_options(gtaiv_launcher_shader_test PRIVATE -fobjc-arc)
  target_link_libraries(gtaiv_launcher_shader_test PRIVATE "-framework AppKit")
  add_test(NAME launcher_folder_refresh COMMAND gtaiv_launcher_shader_test --folder-refresh)
  set_tests_properties(launcher_folder_refresh PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
  add_test(NAME launcher_option_environment COMMAND gtaiv_launcher_shader_test --options)
  set_tests_properties(launcher_option_environment PROPERTIES TIMEOUT 10 RUN_SERIAL TRUE)
  add_test(NAME launcher_shader_lifecycle COMMAND gtaiv_launcher_shader_test)
  set_tests_properties(launcher_shader_lifecycle PROPERTIES TIMEOUT 20)
  add_test(NAME launcher_shader_presentation COMMAND gtaiv_launcher_shader_test --presentation)
  set_tests_properties(launcher_shader_presentation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  foreach(scenario navigation idle launch status)
    add_test(NAME launcher_${scenario} COMMAND gtaiv_launcher_shader_test --launcher ${scenario})
    set_tests_properties(launcher_${scenario} PROPERTIES TIMEOUT 10 RUN_SERIAL TRUE)
  endforeach()
endif()
