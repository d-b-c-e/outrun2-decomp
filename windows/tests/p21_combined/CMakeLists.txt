cmake_minimum_required(VERSION 3.20)
project(p21_combined LANGUAGES CXX)
if(NOT TOOLKIT_ROOT)
  message(FATAL_ERROR "Set TOOLKIT_ROOT to the reviewed toolkit")
endif()
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(p21_combined main.cpp ${ROOT}/windows/source/outrun_adapter.cpp
  ${ROOT}/windows/shim/core.cpp ${ROOT}/windows/shim/parse.cpp
  ${ROOT}/windows/shim/dinput_provider.cpp ${ROOT}/src/input/dual_sense.cpp)
target_compile_features(p21_combined PRIVATE cxx_std_17)
target_compile_definitions(p21_combined PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_include_directories(p21_combined PRIVATE ${ROOT}/windows/source ${ROOT}/windows/shim ${ROOT}/windows/host
  ${ROOT}/windows/tests ${ROOT}/windows/vendor/controls ${ROOT}/src
  ${ROOT}/windows/vendor/session ${ROOT}/windows/host/recorder)
target_link_libraries(p21_combined PRIVATE bcrypt dinput8 dxguid)
target_compile_options(p21_combined PRIVATE /W4 /WX)
