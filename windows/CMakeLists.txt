# Windows desktop build (windows/): the Xbox port's PC configuration
# (outrun_xbox_core: Direct3D 11, XAudio2, Windows.Gaming.Input) as a desktop
# game, plus wheels, pedals, shifters and button boxes through DirectInput,
# bound by the wheel toolkit's controls contract (vendor/controls). Built with
# the Xbox PC configuration (-DOR2_XBOX=ON, clang-cl); see windows/README.md.
add_executable(OutRunDesktop WIN32 source/window_desktop.cpp source/desktop_settings.cpp source/wheel_input.cpp
    source/controls_store.cpp source/test_injection.cpp source/test_fence.cpp shim/parse.cpp
    source/shim_input.cpp source/outrun_adapter.cpp shim/core.cpp shim/dinput_provider.cpp shim/wheelffb_bridge.cpp
    source/session_request.cpp source/ordinary_recording.cpp source/startup_state.cpp
    source/telemetry_forza.cpp source/telemetry_live.cpp source/ffb_outrun.cpp source/ffb_settings.cpp source/ffb_live.cpp
    source/ffb_producer2.cpp source/ffb_shadow.cpp source/ffb_shadow_live.cpp source/ffb_player_model.cpp
    source/ffb_device.cpp source/desktop_panel.cpp source/hud_toggle.cpp vendor/native/panel/dbce_panel.cpp vendor/native/panel/dbce_panel_input.cpp
    vendor/native/third_party/imgui/imgui.cpp vendor/native/third_party/imgui/imgui_draw.cpp vendor/native/third_party/imgui/imgui_tables.cpp
    vendor/native/third_party/imgui/imgui_widgets.cpp vendor/native/third_party/imgui/backends/imgui_impl_dx11.cpp)
target_include_directories(OutRunDesktop PRIVATE source shim host host/recorder vendor/controls vendor/session vendor/telemetry vendor/wheelffb
    vendor/native/panel vendor/native/third_party/imgui)
target_link_libraries(OutRunDesktop PRIVATE outrun_xbox_core shell32 dinput8 dxguid bcrypt ws2_32)
# A PDB with the public symbols, to name the function of a crash address.
target_link_options(OutRunDesktop PRIVATE /DEBUG /STACK:16777216)

# Standalone command publisher. No Core/game/device execution and no shim DLL ABI change.
add_executable(control_publish host/control_publish.cpp)
target_include_directories(control_publish PRIVATE host shim vendor/controls)
target_compile_features(control_publish PRIVATE cxx_std_17)
target_compile_definitions(control_publish PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000002)
target_compile_options(control_publish PRIVATE /W4 /WX /EHsc)

# The bindings store, the legacy import, the settings writer and test injection, without a game or devices.
add_executable(test_desktop_controls tests/test_desktop_controls.cpp source/controls_store.cpp source/desktop_settings.cpp source/test_injection.cpp)
target_include_directories(test_desktop_controls PRIVATE source vendor/controls)
target_compile_features(test_desktop_controls PRIVATE cxx_std_17)
target_compile_definitions(test_desktop_controls PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(test_desktop_controls PRIVATE dxguid)
add_test(NAME desktop_controls COMMAND test_desktop_controls)

# Forza telemetry and the OutRun 2006 mod's shared-memory block (source/telemetry_forza): settings, car fields, packets.
add_executable(test_telemetry tests/test_telemetry.cpp source/telemetry_forza.cpp source/desktop_settings.cpp)
target_include_directories(test_telemetry PRIVATE source vendor/telemetry)
target_compile_features(test_telemetry PRIVATE cxx_std_17)
target_compile_definitions(test_telemetry PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
add_test(NAME telemetry COMMAND test_telemetry)

# OutRun's own force model (source/ffb_outrun, "outrun-legacy@1") against the OutRun 2006 mod's production calculation:
# its fixture's goldens, byte for byte (tests/data/ffb/README.md). Software values only; no device.
add_executable(test_ffb_outrun tests/test_ffb_outrun.cpp source/ffb_outrun.cpp source/ffb_settings.cpp source/desktop_settings.cpp source/ffb_device.cpp)
target_include_directories(test_ffb_outrun PRIVATE source vendor/controls vendor/wheelffb)
target_compile_definitions(test_ffb_outrun PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_features(test_ffb_outrun PRIVATE cxx_std_17)
add_test(NAME ffb_outrun COMMAND test_ffb_outrun ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ffb)

# Producer-semantic, software-only @2 preview. The fixture calls the game's own input and transmission producers,
# then samples the previous completed car tick; it links no wheel/device path and changes no runtime model selection.
add_executable(test_ffb_producer2 tests/test_ffb_producer2.cpp source/ffb_producer2.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/race_input.cpp)
target_include_directories(test_ffb_producer2 PRIVATE source ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_ffb_producer2 PRIVATE outrun_driving)
target_compile_features(test_ffb_producer2 PRIVATE cxx_std_17)
add_test(NAME ffb_producer2 COMMAND test_ffb_producer2)

# The optional post-control diagnostic's gating and failure isolation, with a fake CSV writer and no game/device.
add_executable(test_ffb_shadow tests/test_ffb_shadow.cpp source/ffb_shadow.cpp source/ffb_producer2.cpp)
target_include_directories(test_ffb_shadow PRIVATE source ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_ffb_shadow PRIVATE cxx_std_17)
add_test(NAME ffb_shadow COMMAND test_ffb_shadow)

# Explicit player selection, independent producer gains and the real shadow request logic through a fake software sink.
# No WheelFfb or native output path is linked into this fixture.
add_executable(test_ffb_player_model tests/test_ffb_player_model.cpp source/ffb_player_model.cpp
    source/ffb_shadow.cpp source/ffb_producer2.cpp source/desktop_settings.cpp)
target_include_directories(test_ffb_player_model PRIVATE source ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(test_ffb_player_model PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_features(test_ffb_player_model PRIVATE cxx_std_17)
add_test(NAME ffb_player_model COMMAND test_ffb_player_model)

# F4, the delivery gates (source/ffb_device) against a fake WheelFfb library: identity, the accepted zero, recovery,
# release and shutdown order, and silence whenever a gate is closed. No device and no DLL.
add_executable(test_ffb_delivery tests/test_ffb_delivery.cpp source/ffb_device.cpp)
target_include_directories(test_ffb_delivery PRIVATE source vendor/wheelffb)
target_compile_definitions(test_ffb_delivery PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_features(test_ffb_delivery PRIVATE cxx_std_17)
add_test(NAME ffb_delivery COMMAND test_ffb_delivery)

# The F6 panel's input hook (vendor/native/panel/dbce_panel_input) on a real window: posted keys pass while closed
# (except F6, and F8 when Stop FFB is offered), go to the panel while open, Esc closes, Alt+Tab always passes.
add_executable(test_panel_input tests/test_panel_input.cpp vendor/native/panel/dbce_panel.cpp vendor/native/panel/dbce_panel_input.cpp
    vendor/native/third_party/imgui/imgui.cpp vendor/native/third_party/imgui/imgui_draw.cpp vendor/native/third_party/imgui/imgui_tables.cpp
    vendor/native/third_party/imgui/imgui_widgets.cpp)
target_include_directories(test_panel_input PRIVATE vendor/native/panel vendor/native/third_party/imgui)
target_compile_definitions(test_panel_input PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_features(test_panel_input PRIVATE cxx_std_17)
add_test(NAME panel_input COMMAND test_panel_input)

# F4b's load gates with the real ffb_live.cpp (romi 30155): a fake WheelFfb.dll (tests/fake_wheelffb.cpp) beside the test
# in their own folder, never beside OutRunDesktop.exe. Muted, latched, default or unbound: it must never attach.
add_library(fake_wheelffb SHARED tests/fake_wheelffb.cpp)
set_target_properties(fake_wheelffb PROPERTIES OUTPUT_NAME WheelFfb RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ffb_wiring/$<CONFIG>)
target_compile_definitions(fake_wheelffb PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(fake_wheelffb PRIVATE ole32)
add_executable(test_ffb_wiring tests/test_ffb_wiring.cpp source/ffb_live.cpp source/ffb_settings.cpp source/ffb_outrun.cpp
    source/ffb_device.cpp source/desktop_settings.cpp source/ffb_player_model.cpp source/ffb_shadow.cpp source/ffb_producer2.cpp
    source/ffb_shadow_live.cpp)
target_include_directories(test_ffb_wiring PRIVATE source vendor/controls vendor/wheelffb ${PROJECT_SOURCE_DIR}/xbox/source)
target_compile_definitions(test_ffb_wiring PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_features(test_ffb_wiring PRIVATE cxx_std_17)
# outrun_native_runtime's clang-cl objects call the compiler-rt builtins (128-bit division), as in xbox/CMakeLists.txt.
file(GLOB OR2_WIRING_CLANG_BUILTINS "${CMAKE_GENERATOR_INSTANCE}/VC/Tools/Llvm/x64/lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib")
target_link_libraries(test_ffb_wiring PRIVATE outrun_native_runtime ${OR2_WIRING_CLANG_BUILTINS})
set_target_properties(test_ffb_wiring PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ffb_wiring/$<CONFIG>)
add_dependencies(test_ffb_wiring fake_wheelffb)
add_test(NAME ffb_wiring COMMAND test_ffb_wiring)

# The input shim's core (windows/shim, docs/INPUT-SHIM.md), slice 1: pure C++17 against a scripted device provider.
add_executable(test_shim tests/test_shim.cpp shim/parse.cpp shim/core.cpp)
target_include_directories(test_shim PRIVATE shim tests vendor/controls)
target_compile_features(test_shim PRIVATE cxx_std_17)
target_compile_options(test_shim PRIVATE /W4)
add_test(NAME shim COMMAND test_shim)
# The shim's steering wheel for force feedback (source/shim_steer.hpp): the steer binding joined to the real Core's
# device reports over the scripted provider.
add_executable(test_shim_steer tests/test_shim_steer.cpp shim/parse.cpp shim/core.cpp)
target_include_directories(test_shim_steer PRIVATE source shim tests vendor/controls)
target_compile_features(test_shim_steer PRIVATE cxx_std_17)
target_compile_options(test_shim_steer PRIVATE /W4)
add_test(NAME shim_steer COMMAND test_shim_steer)
# The ordinary session request (source/session_request): the strict parser and what each refusal means.
add_executable(test_session_request tests/test_session_request.cpp source/session_request.cpp shim/parse.cpp)
target_include_directories(test_session_request PRIVATE source shim vendor/controls)
target_compile_features(test_session_request PRIVATE cxx_std_17)
target_compile_options(test_session_request PRIVATE /W4)
target_compile_definitions(test_session_request PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
add_test(NAME session_request COMMAND test_session_request)
# A recording's startup snapshot (startup_state.hpp; sana 37135/37176): the seal, the inventory, the member check,
# the private home's seed, and the playback settings (the recording's, with the rig's [Window] / [Triple]).
add_executable(test_startup_state tests/test_startup_state.cpp source/startup_state.cpp source/ordinary_recording.cpp source/session_request.cpp source/desktop_settings.cpp
    source/outrun_adapter.cpp shim/parse.cpp shim/core.cpp ${PROJECT_SOURCE_DIR}/src/input/dual_sense.cpp)
target_include_directories(test_startup_state PRIVATE shim host host/recorder source vendor/controls vendor/session ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_startup_state PRIVATE cxx_std_17)
target_compile_options(test_startup_state PRIVATE /W4 /WX)
target_compile_definitions(test_startup_state PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(test_startup_state PRIVATE bcrypt)
add_test(NAME startup_state COMMAND test_startup_state)

# The OutRun 2006 shim adapter (source/outrun_adapter), slice 2: the game's pad adapter, the shim core and the adapter
# on fakes, into the NativeInputState the game consumes.
add_executable(test_outrun_adapter tests/test_outrun_adapter.cpp source/outrun_adapter.cpp shim/parse.cpp shim/core.cpp
    ${PROJECT_SOURCE_DIR}/src/input/dual_sense.cpp)
target_include_directories(test_outrun_adapter PRIVATE shim tests source vendor/controls ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_outrun_adapter PRIVATE cxx_std_17)
target_compile_options(test_outrun_adapter PRIVATE /W4)
add_test(NAME outrun_adapter COMMAND test_outrun_adapter)
# The native controls consumer for Wheelkit's Apply Controls fixtures (sana 40461/40764): OutRunDesktop.ini read as an
# ordinary start reads it (Ini, the backend choice, the shim profile bytes, read_profile, outrun_capability), as JSON.
add_executable(outrun_controls_consumer tests/controls_consumer.cpp source/desktop_settings.cpp source/outrun_adapter.cpp
    shim/parse.cpp shim/core.cpp ${PROJECT_SOURCE_DIR}/src/input/dual_sense.cpp)
target_include_directories(outrun_controls_consumer PRIVATE shim tests source vendor/controls ${PROJECT_SOURCE_DIR}/src)
target_compile_features(outrun_controls_consumer PRIVATE cxx_std_17)
target_compile_options(outrun_controls_consumer PRIVATE /W4)
target_compile_definitions(outrun_controls_consumer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)

# R2 (Astra 16754): the adapter/action replay of a sealed OutRunDesktop capture, offline (a tool, given a capture).
add_executable(capture_replay tests/capture_replay.cpp source/outrun_adapter.cpp shim/parse.cpp shim/core.cpp
    ${PROJECT_SOURCE_DIR}/src/input/dual_sense.cpp)
target_include_directories(capture_replay PRIVATE shim host host/recorder source vendor/controls vendor/session ${PROJECT_SOURCE_DIR}/src)
target_compile_features(capture_replay PRIVATE cxx_std_17)
target_compile_options(capture_replay PRIVATE /W4 /WX)
target_link_libraries(capture_replay PRIVATE bcrypt)

# R3 (Astra 16754): the replay tape (source/outrun_replay.hpp) on synthetic latched frames through the real adapter/codec.
add_executable(test_outrun_replay tests/test_outrun_replay.cpp source/outrun_adapter.cpp shim/parse.cpp shim/core.cpp
    ${PROJECT_SOURCE_DIR}/src/input/dual_sense.cpp)
target_include_directories(test_outrun_replay PRIVATE shim host host/recorder source vendor/controls vendor/session ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_outrun_replay PRIVATE cxx_std_17)
target_compile_options(test_outrun_replay PRIVATE /W4 /WX)
target_link_libraries(test_outrun_replay PRIVATE bcrypt)
add_test(NAME outrun_replay COMMAND test_outrun_replay)
# Capture contract @2 (an ordinary recording): the consumed input and the keyboard channels, @1 rows unchanged.
add_executable(test_outrun_capture2 tests/test_outrun_capture2.cpp source/outrun_adapter.cpp shim/parse.cpp shim/core.cpp
    ${PROJECT_SOURCE_DIR}/src/input/dual_sense.cpp)
target_include_directories(test_outrun_capture2 PRIVATE shim host host/recorder source vendor/controls vendor/session ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_outrun_capture2 PRIVATE cxx_std_17)
target_compile_options(test_outrun_capture2 PRIVATE /W4 /WX)
target_link_libraries(test_outrun_capture2 PRIVATE bcrypt)
add_test(NAME outrun_capture2 COMMAND test_outrun_capture2)
# The diagnostic key echo (sana 36438): edges only, followed by a reader while written, an existing file refused, a
# failed write sticky.
add_executable(test_key_echo tests/test_key_echo.cpp)
target_include_directories(test_key_echo PRIVATE source)
target_compile_features(test_key_echo PRIVATE cxx_std_17)
target_compile_options(test_key_echo PRIVATE /W4 /WX)
target_compile_definitions(test_key_echo PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME key_echo COMMAND test_key_echo)
# Play with Recording: the ordinary recorder over the real frame recorder and writer (one drain, the six-member
# admission, recorder-complete.json, the receipt reader) and the start's intake of a request file.
add_executable(test_ordinary_recording tests/test_ordinary_recording.cpp source/ordinary_recording.cpp source/session_request.cpp source/startup_state.cpp
    source/outrun_adapter.cpp shim/parse.cpp shim/core.cpp ${PROJECT_SOURCE_DIR}/src/input/dual_sense.cpp)
target_include_directories(test_ordinary_recording PRIVATE shim host host/recorder source vendor/controls vendor/session ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_ordinary_recording PRIVATE cxx_std_17)
target_compile_options(test_ordinary_recording PRIVATE /W4 /WX)
target_compile_definitions(test_ordinary_recording PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(test_ordinary_recording PRIVATE bcrypt)
# --cross-volume: the request in %TEMP% and its folders in the build tree, on another volume here (C: and E:; the 12:56
# pilot's failure). The test fails when the two are on one volume.
add_test(NAME ordinary_recording COMMAND test_ordinary_recording --cross-volume ${CMAKE_CURRENT_BINARY_DIR}/cross-volume)
# Consumed playback: the replay channels, the strict tape, the seam's rows then neutral, and the seam fixture through the
# race update's own readers (race_input.cpp) for the keyboard table and every joystick configuration.
add_executable(test_consumed_replay tests/test_consumed_replay.cpp source/outrun_adapter.cpp shim/parse.cpp shim/core.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/race_input.cpp ${PROJECT_SOURCE_DIR}/src/input/dual_sense.cpp)
target_include_directories(test_consumed_replay PRIVATE shim host host/recorder source vendor/controls vendor/session ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_consumed_replay PRIVATE cxx_std_17)
target_compile_options(test_consumed_replay PRIVATE /W4 /WX)
target_compile_definitions(test_consumed_replay PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(test_consumed_replay PRIVATE bcrypt)
add_test(NAME consumed_replay COMMAND test_consumed_replay)
# FixFullPedalChecks (src/platform/race_input.hpp): 453720 / 453750 pedal volumes over every word both backends write,
# current and previous, through the game's own 453860 update, with the fix on and off.
add_executable(test_pedal_volume tests/test_pedal_volume.cpp ${PROJECT_SOURCE_DIR}/src/platform/race_input.cpp)
target_include_directories(test_pedal_volume PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_pedal_volume PRIVATE cxx_std_17)
target_compile_options(test_pedal_volume PRIVATE /W4 /WX)
add_test(NAME pedal_volume COMMAND test_pedal_volume)
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
# The native wheel path's rescan policy (source/wheel_rescan.hpp): device changes and missing bindings only.
add_executable(test_wheel_rescan tests/test_wheel_rescan.cpp)
target_include_directories(test_wheel_rescan PRIVATE source)
target_compile_features(test_wheel_rescan PRIVATE cxx_std_17)
target_compile_options(test_wheel_rescan PRIVATE /W4 /WX)
add_test(NAME wheel_rescan COMMAND test_wheel_rescan)
# AutoDetectResolution (src/enhancements/video.hpp): an unset options.ini resolution takes the display's height.
add_executable(test_auto_resolution tests/test_auto_resolution.cpp)
target_include_directories(test_auto_resolution PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_auto_resolution PRIVATE cxx_std_17)
target_compile_options(test_auto_resolution PRIVATE /W4 /WX)
add_test(NAME auto_resolution COMMAND test_auto_resolution)
# The companion's device-free stand-in game (tests/session_companion/test_companion.ps1): the real intake and recorder,
# synthetic rows, closed only by its close file. Not a ctest: the companion test needs the managed finalizer built.
add_executable(session_game_fixture tests/session_companion/session_game_fixture.cpp source/ordinary_recording.cpp source/session_request.cpp source/startup_state.cpp
    source/outrun_adapter.cpp shim/parse.cpp shim/core.cpp ${PROJECT_SOURCE_DIR}/src/input/dual_sense.cpp)
target_include_directories(session_game_fixture PRIVATE shim host host/recorder source vendor/controls vendor/session ${PROJECT_SOURCE_DIR}/src)
target_compile_features(session_game_fixture PRIVATE cxx_std_17)
target_compile_options(session_game_fixture PRIVATE /W4 /WX)
target_compile_definitions(session_game_fixture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(session_game_fixture PRIVATE bcrypt)

# The shared input shim as a DLL for managed game adapters (shim_dll/dbce_shim.h, experimental ABI 1; Astra 16982/17050):
# the reviewed Core, DInputProvider and ProviderHost, unchanged, behind a C ABI.
add_library(dbce_shim SHARED shim_dll/dbce_shim.cpp shim/core.cpp shim/parse.cpp shim/dinput_provider.cpp)
target_include_directories(dbce_shim PRIVATE shim_dll shim host host/recorder vendor/session vendor/controls)
target_compile_features(dbce_shim PRIVATE cxx_std_17)
target_compile_definitions(dbce_shim PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(dbce_shim PRIVATE /W4 /WX)
target_link_libraries(dbce_shim PRIVATE dinput8 dxguid bcrypt)
add_executable(test_shim_abi tests/test_shim_abi.cpp shim_dll/dbce_shim.cpp shim/core.cpp shim/parse.cpp shim/dinput_provider.cpp)
target_include_directories(test_shim_abi PRIVATE shim_dll shim host host/recorder vendor/session tests vendor/controls)
target_compile_features(test_shim_abi PRIVATE cxx_std_17)
target_compile_definitions(test_shim_abi PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS DBCE_SHIM_BUILD)
target_compile_options(test_shim_abi PRIVATE /W4 /WX)
target_link_libraries(test_shim_abi PRIVATE dinput8 dxguid bcrypt)
add_dependencies(test_shim_abi dbce_shim)
add_test(NAME shim_abi COMMAND test_shim_abi $<TARGET_FILE:dbce_shim>)
# The vendored host test (tests/test_provider_host.cpp): the real ProviderHost over fake COM with a Schema 2 profile,
# at controls_reads 2 (ordinary and latched values Core applies, as shim_input passes ControlsReads) and refused at the
# default (STD-037 item 4; calibration 9a run1).
add_executable(test_provider_host tests/test_provider_host.cpp shim/core.cpp shim/parse.cpp shim/dinput_provider.cpp)
target_include_directories(test_provider_host PRIVATE shim host host/recorder vendor/session tests vendor/controls)
target_compile_features(test_provider_host PRIVATE cxx_std_17)
target_compile_definitions(test_provider_host PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(test_provider_host PRIVATE /W4 /WX)
target_link_libraries(test_provider_host PRIVATE dinput8 dxguid bcrypt)
add_test(NAME provider_host COMMAND test_provider_host)

# The test fence (source/test_fence), slice 2b: a temporary game tree, the real game-free writers (options, profiles,
# the EXE setup, the log), success and fault paths, the complete game-tree inventory compared before and after.
add_executable(test_triple_screens tests/test_triple_screens.cpp)
target_include_directories(test_triple_screens PRIVATE source ${PROJECT_SOURCE_DIR}/xbox/source ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(test_triple_screens PRIVATE outrun_native_runtime)
target_compile_features(test_triple_screens PRIVATE cxx_std_17)
target_compile_options(test_triple_screens PRIVATE /W4 /WX)
add_test(NAME triple_screens COMMAND test_triple_screens)

add_executable(test_fence tests/test_fence.cpp source/test_fence.cpp shim/parse.cpp)
target_include_directories(test_fence PRIVATE source shim vendor/controls ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_fence PRIVATE cxx_std_17)
target_compile_options(test_fence PRIVATE /W4)
target_compile_definitions(test_fence PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(test_fence PRIVATE outrun_native_runtime)
add_test(NAME test_fence COMMAND test_fence)
# The write and home() audit, and its own mutations.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME write_audit COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/write_audit.py ${PROJECT_SOURCE_DIR})
    add_test(NAME write_audit_selftest COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/write_audit.py ${PROJECT_SOURCE_DIR} --selftest)
# The shim files under windows/ mirror the wheel toolkit's native/shim, pinned by vendor/shim.lock.json (vendor/SHIM.md).
add_test(NAME shim_vendor_pin COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shim_vendor_pin.py)
# FixC2CRankings: every launcher-gated site of the EXE takes the launcher-present path (tests/launcher_gate_audit.py).
add_test(NAME launcher_gate_audit COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_gate_audit.py ${PROJECT_SOURCE_DIR})
add_test(NAME shim_first_loop_timing COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/shim_first_loop/test_run_timing.py)
add_test(NAME shim_first_loop_consumed_compare COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/shim_first_loop/test_compare_consumed.py)
add_test(NAME controls_consumer COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_controls_consumer.py
    $<TARGET_FILE:outrun_controls_consumer> ${CMAKE_CURRENT_SOURCE_DIR}/tests/controls_consumer)
# The first loop's publish through the shared native publisher (b173a48): its own fixture (a fresh folder, removed
# first), and the control worker's snapshot open reading throughout (live r1-01's failure; ReplaceFileW the control).
add_test(NAME shim_first_loop_publish_clean COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_CURRENT_BINARY_DIR}/publish-fixture)
set_tests_properties(shim_first_loop_publish_clean PROPERTIES FIXTURES_SETUP publish_fixture)
add_test(NAME shim_first_loop_publish COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/shim_first_loop/test_publish.py
    $<TARGET_FILE:control_publish> ${CMAKE_CURRENT_BINARY_DIR}/publish-fixture)
set_tests_properties(shim_first_loop_publish PROPERTIES FIXTURES_REQUIRED publish_fixture)
add_test(NAME shim_first_loop_publish_reader COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/shim_first_loop/test_publish_reader.py
    $<TARGET_FILE:control_publish>)
add_test(NAME perf_harness COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/test_run_perf.py)
add_test(NAME perf_observed_keys COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/test_observed_keys.py)
add_test(NAME perf_route_gate COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/test_route_gate.py)
    # P21g: the teardown audit of the worker-reachable shim code, and its mutations.
    add_test(NAME teardown_audit COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/teardown_audit.py ${PROJECT_SOURCE_DIR})
    add_test(NAME teardown_audit_mutations COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/teardown_audit.py ${PROJECT_SOURCE_DIR} --mutations)
    # The compiler's half (clang-cl: exit-time destructors by type, and what the compiled objects call).
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        add_test(NAME teardown_objects COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/teardown_objects.py ${PROJECT_SOURCE_DIR} --clang-cl ${CMAKE_CXX_COMPILER})
        add_test(NAME teardown_objects_mutations COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/teardown_objects.py ${PROJECT_SOURCE_DIR} --clang-cl ${CMAKE_CXX_COMPILER} --mutations)
    endif()
endif()

# The DirectInput provider (shim/dinput_provider), slice 3: through the core, over fake COM devices.
add_executable(test_dinput_provider tests/test_dinput_provider.cpp shim/dinput_provider.cpp shim/parse.cpp shim/core.cpp)
target_include_directories(test_dinput_provider PRIVATE shim tests vendor/controls)
target_compile_features(test_dinput_provider PRIVATE cxx_std_17)
target_compile_options(test_dinput_provider PRIVATE /W4)
target_compile_definitions(test_dinput_provider PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(test_dinput_provider PRIVATE dinput8 dxguid)
add_test(NAME dinput_provider COMMAND test_dinput_provider)
# The WheelFfb bridge provider (shim/wheelffb_bridge): the hand-over of the force wheel between the provider's own
# handle and WheelFfb's read slot, through the real Core and DInputProvider over fake COM and a fake read-slot API.
add_executable(test_wheelffb_bridge tests/test_wheelffb_bridge.cpp shim/wheelffb_bridge.cpp shim/dinput_provider.cpp shim/parse.cpp shim/core.cpp
    source/ffb_device.cpp)
target_include_directories(test_wheelffb_bridge PRIVATE shim host source tests vendor/controls vendor/wheelffb)
target_compile_features(test_wheelffb_bridge PRIVATE cxx_std_17)
target_compile_options(test_wheelffb_bridge PRIVATE /W4)
target_compile_definitions(test_wheelffb_bridge PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(test_wheelffb_bridge PRIVATE dinput8 dxguid)
add_test(NAME wheelffb_bridge COMMAND test_wheelffb_bridge)
