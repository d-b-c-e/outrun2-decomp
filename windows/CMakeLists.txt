# Windows desktop build (windows/): the Xbox port's PC configuration
# (outrun_xbox_core: Direct3D 11, XAudio2, Windows.Gaming.Input) as a desktop
# game, plus wheels, pedals, shifters and button boxes through DirectInput
# with OutRun2006Tweaks-compatible settings. Built with the Xbox PC
# configuration (-DOR2_XBOX=ON, clang-cl); see windows/README.md.
add_executable(OutRunDesktop WIN32 source/window_desktop.cpp source/desktop_settings.cpp source/wheel_input.cpp)
target_include_directories(OutRunDesktop PRIVATE source)
target_link_libraries(OutRunDesktop PRIVATE outrun_xbox_core shell32 dinput8 dxguid)
# A PDB with the public symbols, to name the function of a crash address.
target_link_options(OutRunDesktop PRIVATE /DEBUG /STACK:16777216)
