cmake_minimum_required(VERSION 3.20)
project(p21_recorder_integration LANGUAGES CXX)
# P21 host + reviewed P20 recorder integration proof (windows/docs/P21-SHUTDOWN.md); not part of OutRunDesktop.
if(NOT TOOLKIT_ROOT)
  message(FATAL_ERROR "Set TOOLKIT_ROOT to the reviewed toolkit")
endif()
if(SHIM_SOURCE_ROOT)
  set(ROOT "${SHIM_SOURCE_ROOT}")
else()
  get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
add_executable(p21_recorder_integration integration.cpp
  ${ROOT}/windows/source/outrun_adapter.cpp ${ROOT}/windows/shim/core.cpp
  ${ROOT}/windows/shim/parse.cpp ${ROOT}/src/input/dual_sense.cpp)
target_compile_features(p21_recorder_integration PRIVATE cxx_std_17)
target_compile_definitions(p21_recorder_integration PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(p21_recorder_integration PRIVATE
  ${ROOT}/windows/source ${ROOT}/windows/shim ${ROOT}/windows/tests
  ${ROOT}/windows/vendor/controls ${ROOT}/src ${TOOLKIT_ROOT}/native/session
  ${ROOT}/windows/tests/recorder_lifecycle_spike)
target_link_libraries(p21_recorder_integration PRIVATE bcrypt)
if(MSVC)
  target_compile_options(p21_recorder_integration PRIVATE /W4 /WX)
endif()
