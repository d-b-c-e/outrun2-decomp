# Windows desktop build (windows/): the Xbox port's PC configuration
# (outrun_xbox_core: Direct3D 11, XAudio2, Windows.Gaming.Input) as a desktop
# game, plus wheels, pedals, shifters and button boxes through DirectInput,
# bound by the wheel toolkit's controls contract (vendor/controls). Built with
# the Xbox PC configuration (-DOR2_XBOX=ON, clang-cl); see windows/README.md.
add_executable(OutRunDesktop WIN32 source/window_desktop.cpp source/desktop_settings.cpp source/wheel_input.cpp
    source/controls_store.cpp source/test_injection.cpp source/test_fence.cpp shim/parse.cpp
    source/shim_input.cpp source/outrun_adapter.cpp shim/core.cpp shim/dinput_provider.cpp
    source/telemetry_forza.cpp source/telemetry_live.cpp)
target_include_directories(OutRunDesktop PRIVATE source shim host host/recorder vendor/controls vendor/session vendor/telemetry)
target_link_libraries(OutRunDesktop PRIVATE outrun_xbox_core shell32 dinput8 dxguid bcrypt ws2_32)
# A PDB with the public symbols, to name the function of a crash address.
target_link_options(OutRunDesktop PRIVATE /DEBUG /STACK:16777216)

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

# The input shim's core (windows/shim, docs/INPUT-SHIM.md), slice 1: pure C++17 against a scripted device provider.
add_executable(test_shim tests/test_shim.cpp shim/parse.cpp shim/core.cpp)
target_include_directories(test_shim PRIVATE shim tests vendor/controls)
target_compile_features(test_shim PRIVATE cxx_std_17)
target_compile_options(test_shim PRIVATE /W4)
add_test(NAME shim COMMAND test_shim)

# The OutRun 2006 shim adapter (source/outrun_adapter), slice 2: the game's pad adapter, the shim core and the adapter
# on fakes, into the NativeInputState the game consumes.
add_executable(test_outrun_adapter tests/test_outrun_adapter.cpp source/outrun_adapter.cpp shim/parse.cpp shim/core.cpp
    ${PROJECT_SOURCE_DIR}/src/input/dual_sense.cpp)
target_include_directories(test_outrun_adapter PRIVATE shim tests source vendor/controls ${PROJECT_SOURCE_DIR}/src)
target_compile_features(test_outrun_adapter PRIVATE cxx_std_17)
target_compile_options(test_outrun_adapter PRIVATE /W4)
add_test(NAME outrun_adapter COMMAND test_outrun_adapter)

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

# The test fence (source/test_fence), slice 2b: a temporary game tree, the real game-free writers (options, profiles,
# the EXE setup, the log), success and fault paths, the complete game-tree inventory compared before and after.
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
add_test(NAME shim_first_loop_timing COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/shim_first_loop/test_run_timing.py)
add_test(NAME shim_first_loop_publish COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/shim_first_loop/test_publish.py)
add_test(NAME perf_harness COMMAND ${Python3_EXECUTABLE} -I -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/perf/test_run_perf.py)
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
