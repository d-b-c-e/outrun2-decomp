cmake_minimum_required(VERSION 3.20)
project(shim_session_spike LANGUAGES CXX)
if(NOT TOOLKIT_ROOT)
  message(FATAL_ERROR "Set TOOLKIT_ROOT to the reviewed toolkit checkout")
endif()
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(shim_session_spike spike.cpp
  ${ROOT}/windows/source/outrun_adapter.cpp
  ${ROOT}/windows/shim/core.cpp ${ROOT}/windows/shim/parse.cpp
  ${ROOT}/src/input/dual_sense.cpp)
target_compile_features(shim_session_spike PRIVATE cxx_std_17)
target_compile_definitions(shim_session_spike PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(shim_session_spike PRIVATE
  ${ROOT}/windows/source ${ROOT}/windows/shim ${ROOT}/windows/tests
  ${ROOT}/windows/vendor/controls ${ROOT}/src ${TOOLKIT_ROOT}/native/session)
if(MSVC)
  target_compile_options(shim_session_spike PRIVATE /W4)
endif()
