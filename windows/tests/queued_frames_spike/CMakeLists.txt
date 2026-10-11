cmake_minimum_required(VERSION 3.20)
project(queued_frames_spike LANGUAGES CXX)
if(NOT TOOLKIT_ROOT)
  message(FATAL_ERROR "Set TOOLKIT_ROOT to the reviewed toolkit")
endif()
if(SHIM_SOURCE_ROOT)
  set(ROOT "${SHIM_SOURCE_ROOT}")
else()
  get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
add_executable(queued_frames_spike native.cpp
  ${ROOT}/windows/source/outrun_adapter.cpp ${ROOT}/windows/shim/core.cpp
  ${ROOT}/windows/shim/parse.cpp ${ROOT}/src/input/dual_sense.cpp)
target_compile_features(queued_frames_spike PRIVATE cxx_std_17)
target_compile_definitions(queued_frames_spike PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(queued_frames_spike PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  ${ROOT}/windows/source ${ROOT}/windows/shim ${ROOT}/windows/tests
  ${ROOT}/windows/vendor/controls ${ROOT}/src ${ROOT}/windows/vendor/session
  ${ROOT}/windows/host/recorder ${ROOT}/windows/host)
target_link_libraries(queued_frames_spike PRIVATE bcrypt)
if(MSVC)
  target_compile_options(queued_frames_spike PRIVATE /W4 /WX)
endif()
