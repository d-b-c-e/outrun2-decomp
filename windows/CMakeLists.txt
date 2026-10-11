# Windows desktop build (windows/): the Xbox port's PC configuration
# (outrun_xbox_core: Direct3D 11, XAudio2, Windows.Gaming.Input) as a desktop
# game, plus wheels, pedals, shifters and button boxes through DirectInput,
# bound by the wheel toolkit's controls contract (vendor/controls). Built with
# the Xbox PC configuration (-DOR2_XBOX=ON, clang-cl); see windows/README.md.
add_executable(OutRunDesktop WIN32 source/window_desktop.cpp source/desktop_settings.cpp source/wheel_input.cpp
    source/controls_store.cpp source/test_injection.cpp source/test_fence.cpp shim/parse.cpp)
target_include_directories(OutRunDesktop PRIVATE source shim vendor/controls)
target_link_libraries(OutRunDesktop PRIVATE outrun_xbox_core shell32 dinput8 dxguid)
# A PDB with the public symbols, to name the function of a crash address.
target_link_options(OutRunDesktop PRIVATE /DEBUG /STACK:16777216)

# The bindings store, the legacy import, the settings writer and test injection, without a game or devices.
add_executable(test_desktop_controls tests/test_desktop_controls.cpp source/controls_store.cpp source/desktop_settings.cpp source/test_injection.cpp)
target_include_directories(test_desktop_controls PRIVATE source vendor/controls)
target_compile_features(test_desktop_controls PRIVATE cxx_std_17)
target_compile_definitions(test_desktop_controls PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(test_desktop_controls PRIVATE dxguid)
add_test(NAME desktop_controls COMMAND test_desktop_controls)

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
    # P21g: the teardown audit of the worker-reachable shim code, and its mutations.
    add_test(NAME teardown_audit COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/teardown_audit.py ${PROJECT_SOURCE_DIR})
    add_test(NAME teardown_audit_mutations COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/teardown_audit.py ${PROJECT_SOURCE_DIR} --mutations)
endif()

# The DirectInput provider (shim/dinput_provider), slice 3: through the core, over fake COM devices.
add_executable(test_dinput_provider tests/test_dinput_provider.cpp shim/dinput_provider.cpp shim/parse.cpp shim/core.cpp)
target_include_directories(test_dinput_provider PRIVATE shim tests vendor/controls)
target_compile_features(test_dinput_provider PRIVATE cxx_std_17)
target_compile_options(test_dinput_provider PRIVATE /W4)
target_compile_definitions(test_dinput_provider PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(test_dinput_provider PRIVATE dinput8 dxguid)
add_test(NAME dinput_provider COMMAND test_dinput_provider)
