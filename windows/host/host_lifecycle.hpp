#pragma once
// Host-only lifecycle primitives extracted from the reviewed P21 shutdown proof.
// Provider/Core workers must not own process termination or teardown policy.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace dbce::shim::hosting {
using Clock = std::chrono::steady_clock;
constexpr DWORD ForcedExitCode = 0xDBCE0021;

// A function returning does not prove its thread has finished DLL/FLS/TLS exit
// callbacks. Wait for the OS thread handle by deadline before calling join().
inline bool join_by(std::thread& t, Clock::time_point deadline) {
    if (!t.joinable()) return true;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    const DWORD ms = left <= 0 ? 0 : left >= 0x7FFFFFFE ? 0x7FFFFFFE : DWORD(left);
    if (WaitForSingleObject(static_cast<HANDLE>(t.native_handle()), ms) == WAIT_OBJECT_0) { t.join(); return true; }
    return false;
}
enum class RecOutcome : int { Open, Draining, Complete, TimedOut, Cancelled };
inline const char* name(RecOutcome o) {
    switch (o) { case RecOutcome::Open: return "Open"; case RecOutcome::Draining: return "Draining"; case RecOutcome::Complete: return "Complete";
                 case RecOutcome::TimedOut: return "TimedOut"; default: return "Cancelled"; }
}
struct LifetimeReport {
    bool resolved = false;
    std::vector<std::string> unresolved;
    RecOutcome recorder = RecOutcome::Open;
    bool recorder_joined = false;
    long long elapsed_us = 0;
};
}
