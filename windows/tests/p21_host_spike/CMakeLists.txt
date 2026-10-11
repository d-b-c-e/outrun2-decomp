cmake_minimum_required(VERSION 3.20)
project(p21_host_spike LANGUAGES CXX)
# Standalone design proof (windows/docs/P21-SHUTDOWN.md); not part of the OutRunDesktop build.
add_executable(p21_host_spike test.cpp)
target_compile_features(p21_host_spike PRIVATE cxx_std_17)
target_compile_definitions(p21_host_spike PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(p21_host_spike PRIVATE /W4 /WX /EHsc)
