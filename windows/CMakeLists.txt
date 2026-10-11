# Windows desktop build (windows/): the Xbox port's PC configuration
# (outrun_xbox_core: Direct3D 11, XAudio2, Windows.Gaming.Input) as a desktop
# game, plus wheels, pedals, shifters and button boxes through DirectInput,
# bound by the wheel toolkit's controls contract (vendor/controls). Built with
# the Xbox PC configuration (-DOR2_XBOX=ON, clang-cl); see windows/README.md.
add_executable(OutRunDesktop WIN32 source/window_desktop.cpp source/desktop_settings.cpp source/wheel_input.cpp
    source/controls_store.cpp source/test_injection.cpp)
target_include_directories(OutRunDesktop PRIVATE source vendor/controls)
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
target_compile_options(test_shim PRIVATE /W4)
add_test(NAME shim COMMAND test_shim)
