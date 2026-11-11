# Windows desktop build (windows/): the Xbox port's PC configuration
# (outrun_xbox_core: Direct3D 11, XAudio2, Windows.Gaming.Input) as a desktop
# game. Wheels, pedals, shifters, the F6 panel and telemetry come from the
# Wheelkit runtime, wheelkit.dll beside the program (source/wheelkit_runtime.cpp,
# through wheelkit.h). Built with the Xbox PC configuration (-DOR2_XBOX=ON,
# clang-cl); see windows/README.md.
#
# WHEELKIT_SDK: a wheelkit checkout whose runtime/native/sdk has been built (pwsh build.ps1 -Only runtime/native/sdk).
# The header is compiled against and build/x64/wheelkit.dll is copied beside OutRunDesktop.exe; nothing of the runtime
# is committed here.
set(WHEELKIT_SDK "" CACHE PATH "A built wheelkit checkout (runtime/native/sdk: wheelkit.h and build/x64/wheelkit.dll)")
set(WHEELKIT_SDK_DIR "${WHEELKIT_SDK}/runtime/native/sdk")
if(NOT EXISTS "${WHEELKIT_SDK_DIR}/wheelkit.h")
    message(FATAL_ERROR "WHEELKIT_SDK must name a wheelkit checkout (no ${WHEELKIT_SDK_DIR}/wheelkit.h): -DWHEELKIT_SDK=<path>")
endif()
add_executable(OutRunDesktop WIN32 source/window_desktop.cpp source/startup_monitor.cpp source/startup_diagnostics.cpp source/desktop_settings.cpp
    source/wheelkit_runtime.cpp source/hpattern.cpp source/letterbox.cpp source/telemetry_forza.cpp source/gamepad_vibration_live.cpp
    source/login_protection.cpp source/camera_controls.cpp source/hud_toggle.cpp source/cd_switcher.cpp source/level_select_input.cpp)
target_include_directories(OutRunDesktop PRIVATE source "${WHEELKIT_SDK_DIR}")
target_link_libraries(OutRunDesktop PRIVATE outrun_xbox_core shell32 comdlg32 bcrypt ws2_32 crypt32)
# A PDB with the public symbols, to name the function of a crash address.
target_link_options(OutRunDesktop PRIVATE /DEBUG /STACK:16777216)
if(EXISTS "${WHEELKIT_SDK_DIR}/build/x64/wheelkit.dll")
    add_custom_command(TARGET OutRunDesktop POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WHEELKIT_SDK_DIR}/build/x64/wheelkit.dll" "$<TARGET_FILE_DIR:OutRunDesktop>/wheelkit.dll")
else()
    message(WARNING "${WHEELKIT_SDK_DIR}/build/x64/wheelkit.dll is not built: OutRunDesktop runs without the wheel, F6 and telemetry")
endif()

# The settings reader and writer, without a game or devices.
add_executable(test_fast_load_config tests/test_fast_load_config.cpp source/desktop_settings.cpp)
target_include_directories(test_fast_load_config PRIVATE source ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_fast_load_config PRIVATE cxx_std_17)
target_compile_definitions(test_fast_load_config PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(test_fast_load_config PRIVATE /W4 /WX)
add_test(NAME fast_load_config COMMAND test_fast_load_config)

add_executable(test_desktop_startup_settings tests/test_desktop_startup_settings.cpp source/desktop_settings.cpp)
target_include_directories(test_desktop_startup_settings PRIVATE source)
target_compile_features(test_desktop_startup_settings PRIVATE cxx_std_17)
target_compile_definitions(test_desktop_startup_settings PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
add_test(NAME desktop_startup_settings COMMAND test_desktop_startup_settings)

# Synthetic identities/rectangles only: no active monitor query, window, topology change or game.
add_executable(test_startup_monitor tests/test_startup_monitor.cpp)
target_include_directories(test_startup_monitor PRIVATE source ${PROJECT_SOURCE_DIR}/xbox/source ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_startup_monitor PRIVATE cxx_std_17)
target_compile_options(test_startup_monitor PRIVATE /W4 /WX)
add_test(NAME startup_monitor COMMAND test_startup_monitor)

# Pure startup diagnostic formatting only: no game, environment mutation, device or window.
add_executable(test_startup_diagnostics tests/test_startup_diagnostics.cpp source/startup_diagnostics.cpp)
target_include_directories(test_startup_diagnostics PRIVATE source)
target_compile_features(test_startup_diagnostics PRIVATE cxx_std_17)
target_compile_options(test_startup_diagnostics PRIVATE /W4 /WX)
target_compile_definitions(test_startup_diagnostics PRIVATE _CRT_SECURE_NO_WARNINGS)
add_test(NAME startup_diagnostics COMMAND test_startup_diagnostics)

# Real host callback/PcSound, synthetic silent PCM and fake output/presenter; no device or game.
add_executable(test_presentation_sound ${PROJECT_SOURCE_DIR}/tests/test_presentation_sound.cpp)
target_link_libraries(test_presentation_sound PRIVATE outrun_xbox_core)
target_compile_options(test_presentation_sound PRIVATE /W4 /WX)
add_test(NAME presentation_sound COMMAND test_presentation_sound)
add_test(NAME presentation_controls_sound COMMAND test_presentation_sound --vsync-controls)
add_executable(test_gamepad_vibration ${PROJECT_SOURCE_DIR}/tests/test_gamepad_vibration.cpp)
target_include_directories(test_gamepad_vibration PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_gamepad_vibration PRIVATE cxx_std_17)
target_compile_options(test_gamepad_vibration PRIVATE /W4 /WX)
add_test(NAME gamepad_vibration COMMAND test_gamepad_vibration)

# Real shared profile I/O on private synthetic fixtures; fake transforms only.
add_executable(test_desktop_login_protection ${PROJECT_SOURCE_DIR}/tests/test_login_protection.cpp)
target_link_libraries(test_desktop_login_protection PRIVATE outrun_xbox_core)
target_compile_options(test_desktop_login_protection PRIVATE /W4 /WX)
add_test(NAME desktop_login_protection COMMAND test_desktop_login_protection)
add_executable(test_desktop_profile_writes ${PROJECT_SOURCE_DIR}/tests/test_frontend_profiles.cpp)
target_link_libraries(test_desktop_profile_writes PRIVATE outrun_xbox_core)
target_compile_options(test_desktop_profile_writes PRIVATE /W4 /WX)
add_test(NAME desktop_profile_writes COMMAND test_desktop_profile_writes)
add_executable(test_desktop_network_sockets ${PROJECT_SOURCE_DIR}/tests/test_network_sockets.cpp)
target_link_libraries(test_desktop_network_sockets PRIVATE outrun_xbox_core)
target_compile_options(test_desktop_network_sockets PRIVATE /W4 /WX)
add_test(NAME desktop_network_sockets COMMAND test_desktop_network_sockets)

# FixFullPedalChecks (src/platform/race_input.hpp): 453720 / 453750 pedal volumes over every word both backends write,
# current and previous, through the game's own 453860 update, with the fix on and off.
add_executable(test_pedal_volume tests/test_pedal_volume.cpp ${PROJECT_SOURCE_DIR}/src/platform/race_input.cpp)
target_include_directories(test_pedal_volume PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_pedal_volume PRIVATE cxx_std_17)
target_compile_options(test_pedal_volume PRIVATE /W4 /WX)
add_test(NAME pedal_volume COMMAND test_pedal_volume)
# FineSteering (src/platform/race_input.hpp): the unstepped +0x202 word over every steering word, through the game's own
# 453860 update; the PC's channels unchanged and the digital path untouched.
add_executable(test_fine_steering tests/test_fine_steering.cpp ${PROJECT_SOURCE_DIR}/src/platform/race_input.cpp)
target_include_directories(test_fine_steering PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_fine_steering PRIVATE cxx_std_17)
target_compile_options(test_fine_steering PRIVATE /W4 /WX)
add_test(NAME fine_steering COMMAND test_fine_steering)
# FixPegasusClopping (src/platform/pc_screen.hpp): 44BCC0's exact sound requests with the fix on and off.
add_executable(test_pegasus_sound tests/test_pegasus_sound.cpp)
target_include_directories(test_pegasus_sound PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_pegasus_sound PRIVATE cxx_std_17)
target_compile_options(test_pegasus_sound PRIVATE /W4 /WX)
add_test(NAME pegasus_sound COMMAND test_pegasus_sound)
# HideOnlineSigninText and ShowOutRunMilesOnMenu (src/platform/pc_screen.hpp): the exact sites and the status text.
add_executable(test_signin_text tests/test_signin_text.cpp)
target_include_directories(test_signin_text PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_signin_text PRIVATE cxx_std_17)
target_compile_options(test_signin_text PRIVATE /W4 /WX)
add_test(NAME signin_text COMMAND test_signin_text)
# UILetterboxing (src/platform/pc_screen.hpp): Tweaks' rule and the bars at the device's 4:3 edges.
add_executable(test_letterbox tests/test_letterbox.cpp)
target_include_directories(test_letterbox PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_letterbox PRIVATE cxx_std_17)
target_compile_options(test_letterbox PRIVATE /W4 /WX)
add_test(NAME letterbox COMMAND test_letterbox)
# DefaultManualTransmission (src/platform/pc_screen.hpp): the carousel indices after 4DD250 and the confirmation.
add_executable(test_default_transmission tests/test_default_transmission.cpp)
target_include_directories(test_default_transmission PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_default_transmission PRIVATE cxx_std_17)
target_compile_options(test_default_transmission PRIVATE /W4 /WX)
add_test(NAME default_transmission COMMAND test_default_transmission)
# [Misc] HudToggleKey (source/hud_toggle.hpp, src/platform/pc_screen.hpp): key names, release edge, policy, the flip.
add_executable(test_hud_toggle tests/test_hud_toggle.cpp)
target_include_directories(test_hud_toggle PRIVATE source ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_hud_toggle PRIVATE cxx_std_17)
target_compile_options(test_hud_toggle PRIVATE /W4 /WX)
add_test(NAME hud_toggle COMMAND test_hud_toggle)
# Level Select's real normalized desktop publisher, with synthetic backend/panel owners.
add_executable(test_level_select_input tests/test_level_select_input.cpp source/level_select_input.cpp)
target_include_directories(test_level_select_input PRIVATE source ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/xbox/source)
target_compile_features(test_level_select_input PRIVATE cxx_std_17)
target_compile_options(test_level_select_input PRIVATE /W4 /WX /EHsc)
add_test(NAME level_select_input COMMAND test_level_select_input)
# AutoDetectResolution (src/enhancements/video.hpp): an unset options.ini resolution takes the display's height.
add_executable(test_auto_resolution tests/test_auto_resolution.cpp)
target_include_directories(test_auto_resolution PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_auto_resolution PRIVATE cxx_std_17)
target_compile_options(test_auto_resolution PRIVATE /W4 /WX)
add_test(NAME auto_resolution COMMAND test_auto_resolution)

add_executable(test_triple_screens tests/test_triple_screens.cpp)
target_include_directories(test_triple_screens PRIVATE source ${PROJECT_SOURCE_DIR}/xbox/source ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_triple_screens PRIVATE outrun_native_runtime)
target_compile_features(test_triple_screens PRIVATE cxx_std_17)
target_compile_options(test_triple_screens PRIVATE /W4 /WX)
add_test(NAME triple_screens COMMAND test_triple_screens)
add_executable(test_curved_panel_warp tests/test_curved_panel_warp.cpp)
target_include_directories(test_curved_panel_warp PRIVATE source)
target_compile_features(test_curved_panel_warp PRIVATE cxx_std_17)
target_compile_options(test_curved_panel_warp PRIVATE /W4 /WX)
add_test(NAME curved_panel_warp COMMAND test_curved_panel_warp "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/curved-panel-warp.tsv")
add_executable(test_curved_panel_mesh tests/test_curved_panel_mesh.cpp)
target_include_directories(test_curved_panel_mesh PRIVATE source)
target_compile_features(test_curved_panel_mesh PRIVATE cxx_std_17)
target_compile_options(test_curved_panel_mesh PRIVATE /W4 /WX)
add_test(NAME curved_panel_mesh COMMAND test_curved_panel_mesh)
add_executable(test_curve_scene_profile tests/test_curve_scene_profile.cpp)
target_include_directories(test_curve_scene_profile PRIVATE source ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_curve_scene_profile PRIVATE cxx_std_17)
add_test(NAME curve_scene_profile COMMAND test_curve_scene_profile)
add_executable(test_curve_native_eye tests/test_curve_native_eye.cpp)
target_include_directories(test_curve_native_eye PRIVATE source)
target_compile_features(test_curve_native_eye PRIVATE cxx_std_17)
add_test(NAME curve_native_eye COMMAND test_curve_native_eye)
add_executable(test_curve_inner_rect tests/test_curve_inner_rect.cpp)
target_include_directories(test_curve_inner_rect PRIVATE source)
target_compile_features(test_curve_inner_rect PRIVATE cxx_std_17)
add_test(NAME curve_inner_rect COMMAND test_curve_inner_rect)
add_executable(test_curve_compositor_shader tests/test_curve_compositor_shader.cpp)
target_include_directories(test_curve_compositor_shader PRIVATE source)
target_compile_features(test_curve_compositor_shader PRIVATE cxx_std_17)
target_compile_options(test_curve_compositor_shader PRIVATE /W4 /WX)
add_test(NAME curve_compositor_shader COMMAND test_curve_compositor_shader)
add_executable(test_camera_controls tests/test_camera_controls.cpp source/camera_controls.cpp source/desktop_settings.cpp)
target_include_directories(test_camera_controls PRIVATE source ${PROJECT_SOURCE_DIR}/xbox/source ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_camera_controls PRIVATE outrun_native_runtime)
target_compile_features(test_camera_controls PRIVATE cxx_std_17)
target_compile_definitions(test_camera_controls PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(test_camera_controls PRIVATE /W4 /WX)
add_test(NAME camera_controls COMMAND test_camera_controls)
find_program(OR2_PWSH_EXECUTABLE NAMES pwsh)
if(OR2_PWSH_EXECUTABLE)
    add_test(NAME candidate_install COMMAND ${OR2_PWSH_EXECUTABLE} -NoProfile -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/Test-CandidateInstall.ps1)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME candidate_package COMMAND ${Python3_EXECUTABLE} -I -B ${PROJECT_SOURCE_DIR}/tools/alpha/test_package_candidate.py)
    # FixC2CRankings: every launcher-gated site of the EXE takes the launcher-present path (tests/launcher_gate_audit.py).
    add_test(NAME launcher_gate_audit COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_gate_audit.py ${PROJECT_SOURCE_DIR})
    add_test(NAME perf_harness COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/test_run_perf.py)
    add_test(NAME perf_observed_keys COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/test_observed_keys.py)
    add_test(NAME perf_route_gate COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/test_route_gate.py)
endif()
