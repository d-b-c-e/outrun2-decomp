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
constexpr DWORD ForcedExitCode = 0xDBCE0021;   // ordinary process, unresolved stop (D4)

// A worker's "done" precedes its thread's end: thread-exit work (thread_local and FLS destructors, DLL thread-detach
// code) runs after the function returns, and std::thread::join waits for all of it without a bound (Astra 13545). So
// the thread object itself is waited for against the same absolute deadline; then join, which returns at once. On a
// timeout the thread stays owned and joinable, never detached here, so a later call waits again and is true only once
// the end is proven (Astra 13978); destructors detach whatever still runs (D9).
inline bool join_by(std::thread& t, Clock::time_point deadline) {
    if (!t.joinable()) return true;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    const DWORD ms = left <= 0 ? 0 : left >= 0x7FFFFFFE ? 0x7FFFFFFE : DWORD(left);
    if (WaitForSingleObject(static_cast<HANDLE>(t.native_handle()), ms) == WAIT_OBJECT_0) { t.join(); return true; }
    return false;
}
// The P20 outcome subset P21 needs: first terminal wins; a timeout or cancellation is never overwritten.
enum class RecOutcome : int { Open, Draining, Complete, TimedOut, Cancelled };
inline const char* name(RecOutcome o) {
    switch (o) { case RecOutcome::Open: return "Open"; case RecOutcome::Draining: return "Draining"; case RecOutcome::Complete: return "Complete";
                 case RecOutcome::TimedOut: return "TimedOut"; default: return "Cancelled"; }
}
struct LifetimeReport {
    bool resolved = false;
    std::vector<std::string> unresolved;
    RecOutcome recorder = RecOutcome::Open;
    bool recorder_joined = false;               // independent of the outcome (Astra 11867)
    long long elapsed_us = 0;
};
}
