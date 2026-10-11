# P21: shutdown and host lifetime (design proposal)

**Proposal, 2026-10-11. Author nene (Claude), for Astra's review (hcom 11139). zero is away; this fills the "Core
shutdown gate" left open in INPUT-SHIM.md section 6. Source only: no code, no integration, no device read, no launch.
Based on `input-shim` `d956e20` and the P20 recorder spike (`codex/shim-recorded-frames` `24e27bd`).**

## 1. What is already decided, and what is open

INPUT-SHIM.md section 6 fixes the shutdown **order**:
1. a final neutral frame;
2. force stop (`none` for OutRun);
3. run end;
4. recorder drain, bounded to 1 s;
5. read leases released once the input thread is out of `tick()`, waiting at most 200 ms, otherwise leaked;
6. workers stopped with a bounded join;
7. DirectInput released.

The provider (slice 3.1) keeps its state alive for any call already started. What it still needs is an owner that
never destroys it while one of its threads may still **start** a call: "the core's shutdown gate, still open".

P20 leaves P21 these requirements (recorder lifecycle README, 24e27bd):
- **Teardown.** "Normal process/CRT teardown with an unresolved worker is not qualified. P21 must keep the host and
  CRT alive until the worker exits, or isolate the writer in its own process."
- **Ownership.** P21 owns producer and controller shutdown, and bounds idle and termination. It cannot depend on
  destructors running by accident.
- **No supervisor finish.** "If the input thread is stuck, P21 cannot substitute a supervisor finish."
- **No cancellation.** Do not cancel the OS I/O, close the handle from another thread, or kill the worker.
- **Finalizer host.** The production finalizer host and its deadline are still to be chosen.

This note proposes the host object, the stop sequence with budgets, the exit decision, the emergency paths, the
finalizer host and the rules for future DLL hosts, plus a proof plan (P21a-h).

## 2. Facts about the OutRun host (read from `d956e20`)

- **EXE host.** The shim is compiled into `OutRunDesktop.exe` (`windows/CMakeLists.txt`: `add_executable(OutRunDesktop
  WIN32 ... shim/parse.cpp)`). It is never a DLL, so it is never unloaded.
- **`wWinMain`** (`windows/source/window_desktop.cpp`) runs in this order:
  1. the fence decides;
  2. stdout and stderr are reopened onto `runtime.log`, unbuffered;
  3. `winrt::init_apartment()` (MTA);
  4. `or2_xbox_main(...)`;
  5. `wheel_input_shutdown()`;
  6. `return result`, which runs the CRT `exit()`: atexit handlers and static destructors run while every other
     thread is still running, then `ExitProcess`.
- **Window close.** `WM_CLOSE` sets `closed`. `pump_window_events()` then returns false and the game loop ends, so no
  game tick follows a close.
- **No other exit.** `src/` and `windows/source/` contain no call to `exit`, `ExitProcess`, `TerminateProcess`,
  `abort` or `quick_exit` (a local function named `exit` in `frontend_license_chooser.cpp` is unrelated).
- **Thread.** The game's input filter is installed in `wWinMain` and runs on the main thread. **The shim's input
  thread (the P20 producer) is the main thread**, the same thread that returns from `or2_xbox_main`.
- **Game files.** They are written with a temp file plus explicit flush or close (for example `video.cpp:80`,
  `frontend_profiles.cpp`). No static stream relies on CRT teardown to flush, which matters for D4.
- **Crashes.** `SetUnhandledExceptionFilter(crash_report)` writes the crash to `runtime.log`, and the process then ends.

Windows behavior relied on:
- `ExitProcess` terminates every other thread before any `DLL_PROCESS_DETACH`. The detach code then runs on the
  exiting thread alone.
- `DllMain` runs under the loader lock and must never wait for a thread.
- A thread killed by `ExitProcess` can leave a lock orphaned. Since Windows Vista, entering an orphaned critical
  section during process exit ends the process instead of deadlocking; the CRT and loader locks taken at detach are
  critical sections, not SRW locks.
  - **This is to be proven by fixture P21e, not assumed.** Our own `std::mutex` objects (SRW) must never be taken by
    detach code; in an EXE none is.

## 3. Hazards

| # | Hazard | Where |
|---|---|---|
| H1 | CRT exit (atexit, static destructors, stream close) runs while a shim worker is alive and touches CRT or static state | returning from `wWinMain` |
| H2 | A worker blocks with no bound: DirectInput `EnumDevices`/`Acquire`/`Poll` inside a driver; a file write, flush or close (antivirus, slow disk); hashing the closed capture | provider, control, recorder workers |
| H3 | Destroying state that a blocked or starting call still uses | the provider's shutdown gate |
| H4 | stdio from a worker: `stdout` is `runtime.log`, closed by CRT exit | any worker `printf` |
| H5 | A supervisor fabricates the producer's end, so a tape with a missing tail seals | recorder |
| H6 | An exit that skips the host's stop: a crash, an external kill, `ExitProcess` by others, logoff | any |
| H7 | Shutdown with no bound: the owner closes the game and it does not exit | any |
| H8 | DLL hosts (the second adapter): unload with a live worker; joins in `DllMain`; no ownership of process exit | future |

## 4. Decisions

**D1. One host object, owned by `wWinMain`.**
- `ShimHost` is a local (`std::unique_ptr`) created after the fence and before `or2_xbox_main`. It is never a
  static. Nothing in the shim that owns a thread is static.
- Every worker holds a `std::shared_ptr` to all it touches: queues, generations, the provider state, the recorder
  `State` and sink, and its own status file. The provider and the P20 worker already do. A worker therefore never
  reaches freed host state, whenever it finishes.

**D2. Workers never touch CRT streams or process-global mutable statics** (H1, H4).
- Status lines go from the status queue to the control worker's own file, `shim-status.jsonl` (in the run folder
  when latched, beside `runtime.log` otherwise). It is opened once with its own handle. Workers never use `stdout`.
- The main thread prints the shutdown summary to `stdout` (`runtime.log`).
- A teardown audit, extending zero's whole-file lexer in `write_audit`, refuses in worker-reachable shim code:
  - `printf`/`std::cout`/`stdout`/`stderr`;
  - function-local `static`, and namespace-scope objects with dynamic destructors;
  - `std::thread::detach` outside the host.

**D3. `stop()` runs on the input thread, once, with budgets** (H3, H5, H7).
- It is called after `or2_xbox_main` returns, on the main thread, which is the producer and input thread. It is
  idempotent; a second call returns the first report.
- Phases and budgets:
  1. **Consumption.** The adapter is marked stopped. In OutRun no game tick follows, so the report says
     `neutral: not delivered (game loop ended)`. A stop requested while the game still ticks (a future run end
     inside the game) delivers the neutral frame on the next tick.
  2. **Force:** `none`.
  3. **Run end.** The input thread records the run's final state (section 9) as an event.
  4. **Recorder, at most 1000 ms.**
     - The producer (this thread) pushes the final run-end frame and calls `finish(last_sequence)`, so the tape ends
       on the run end.
     - `Controller::drain(1000 ms)` covers the inner close and the closed-file size and SHA (P20 v2).
     - The worker writes `worker.tsv` itself, as its last act before `worker_done`. The main thread never writes a
       report.
  5. **Leases, at most 200 ms.** `DInputProvider::shutdown(200)` stops admission and releases idle devices. No tick
     is in flight, because the input thread is the caller.
  6. **Workers, a shared 200 ms.** The provider and control workers are told to stop and joined against one
     deadline. The control worker flushes and closes `shim-status.jsonl` before it exits.
  7. **DirectInput** is released only when no lease and no worker is unresolved. Otherwise it is leaked.
  8. **Report.** `complete`, or `unresolved:` followed by the steps and workers. It goes to `stdout` and to the
     returned `StopReport`.
- The total is at most about 1.45 s plus logging; it never waits past a deadline. Each phase starts even if an
  earlier one timed out: a timed-out recorder does not hold back the lease and worker phases.

**D4. The exit decision: CRT teardown never runs while a shim worker is alive** (H1).
- **Resolved** (every worker joined): destroy the host and return from `wWinMain` normally. CRT exit then runs with
  no shim thread.
- **Unresolved:** `exit_unresolved(code)` on the main thread:
  1. write the report;
  2. `fflush(stdout)` and `fflush(stderr)`;
  3. `ExitProcess(code)` directly, skipping the CRT `exit()`, atexit and static destructors.
- `ExitProcess` terminates the stuck worker before any DLL detach, so no teardown code runs alongside it.
- What is lost: unflushed CRT stream buffers. `runtime.log` is unbuffered (`_IONBF`); game files are flushed when
  written (section 2); `shim-status.jsonl` may lose its tail, and the report line in `runtime.log` covers that.
- The recording is incomplete by construction: no `worker.tsv`, and no inner footer if the writer was stuck. The
  finalizer (D8) refuses it.
- This satisfies P20's "keep the host and CRT alive until the worker exits": CRT teardown never starts while the
  worker exists, because the process ends together with it.

**D5. No I/O cancellation, no closing handles from another thread, no `TerminateThread`** (P20 rule).
- The bound plus D4 covers file and driver hangs alike. `CancelSynchronousIo` could cancel an unrelated I/O on that
  thread, and does not reach a hang inside a DirectInput driver call.

**D6. Emergency and abnormal exits** (H6).
- **`exit()` called inside the game.** None exists today. A hook registered at host start
  (`std::atexit`, holding a trivially destructible atomic pointer, not a smart pointer) runs `stop(ExitCalled)` with
  the same budgets, then `exit_unresolved` if needed. It runs before static destructors registered earlier;
  statics constructed later are destroyed first, so shim code relies on none (D2).
- **`WM_ENDSESSION`** (logoff or shutdown, `wParam` TRUE). Optionally, the window procedure runs `stop(SessionEnd)`
  under the same budgets, inside Windows' end-session allowance, because the process may be ended as soon as the
  message returns.
- **Crash, external kill, or someone else's `ExitProcess`.** Nothing runs. The capture lacks `worker.tsv` (and its
  footer, if the writer was mid-write), so the finalizer refuses it. Completion is never claimed without the worker's
  own report.

**D7. Never a supervisor finish** (H5).
- `finish()` is called only by the producer thread. `stop()` checks that it runs on the producer thread.
- If it does not (a future host, or the `atexit` path on another thread), the recorder is cancelled: `Cancelled`,
  never `Complete`, so the capture is incomplete. It is never finished on the producer's behalf.

**D8. The finalizer runs out of process, after the game exits.**
- In the game process, the worker writes `source.jsonl`, `bundle.meta` and `worker.tsv` (nonce, closed size and SHA,
  counts).
- The runner seals afterwards (the existing managed `CompletionBundle`/`ArtifactSeal`, which writes `complete.tsv`).
  It refuses a missing member, a mismatched nonce or hash, or a report that is not `Complete`.
- No sealing or member hashing other than the worker's own closed-file hash is spent inside the game's shutdown
  budget. The seal is not code loaded into OutRun (P20 README).

**D9. Destructors never wait.**
- `~ShimHost` never joins. If `stop()` was not called, it calls `stop(Destructor)` with the same budgets and
  records that as a defect. `wWinMain` calls `stop()` first, so this is a fallback.
- Worker-owned state outlives the facade (P20). A joinable `std::thread` is never destroyed: a worker that did not
  finish is detached by the host and exits with the process (D4).

**D10. Idle bound.**
- A capture the host never stops still ends at the writer's limit (600 s, `Limits.seconds`) as `WriterLimit`.
- Provider and control loops are paced (the 2 s rescan, command polling) and wait on a stop event, so they never
  spin.

**D11. Rules for DLL hosts** (the second adapter; H8).
- (a) **Pin the module at host start** with `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, ...)`, so worker code
  is never unmapped. `FreeLibraryAndExitThread` is the alternative where pinning is unacceptable.
- (b) **Never stop or join in `DllMain`.** Stop runs from a quit hook on the game's own thread, which the adapter
  must name (for a Unity game, the engine's quit callback on the main thread).
- (c) **A guest DLL does not own process exit,** so D4's escape is not available. Hence no in-process qualification
  writer in a DLL host. The in-process side only produces into a bounded shared-memory ring, with no worker thread
  and no file I/O. A recorder process owned by the runner drains it and writes and seals the capture.
  - A dead or absent reader, or overflow, makes the capture incomplete and is counted.
  - The P20 `Sink` boundary already allows this: the shared-memory ring is just another sink.
- OutRun, an EXE that owns its exit, keeps the in-process P20 worker.

## 5. API sketch (for review, not code)

```cpp
struct StopBudget { int recorder_ms = 1000, leases_ms = 200, workers_ms = 200; };
enum class StopReason : std::uint8_t { GameReturned, ExitCalled, SessionEnd, Destructor };
struct StopReport {
    bool resolved{};                       // every worker joined, nothing leaked
    std::vector<std::string> unresolved;   // "recorder: TimedOut", "worker provider", "lease 2", ...
    bool neutral_delivered{};
    recorder::Outcome recorder{};           // the P20 outcome at the end of phase 4
    std::int64_t elapsed_us{};
};
class ShimHost {
public:
    // Input thread. Null: no shim (an ordinary process falls back to native input); a latched process is inert.
    static std::unique_ptr<ShimHost> start(const HostConfig&) noexcept;
    // Input thread only; idempotent; never blocks past the budgets.
    StopReport stop(StopReason, StopBudget = {}) noexcept;
    ~ShimHost();                            // never joins (D9)
};
[[noreturn]] void exit_unresolved(int code) noexcept;   // report, fflush, ExitProcess (D4)
```

`wWinMain`, after the fence:

```cpp
auto host = ShimHost::start(config);                     // before or2_xbox_main
const int result = or2_xbox_main(argc, argv);            // the game loop; ends on WM_CLOSE
const auto report = host ? host->stop(StopReason::GameReturned) : StopReport{true};
outrun::desktop::wheel_input_shutdown();                 // legacy path, ordinary process only
if (!report.resolved) exit_unresolved(result);           // D4: no CRT teardown with a live worker
host.reset();
return result;
```

## 6. Proof plan (offline fixtures; P21 rows in section 17)

| Row | Shows | Fixture |
|---|---|---|
| P21a | Order and budgets: on the producer thread, the final run-end frame is pushed and finished, then the drain, leases and joins; `complete`; elapsed under the budget plus 50 ms | fake provider, control and recorder workers with barriers |
| P21b | A provider worker blocked in a fake `EnumDevices` makes the result `unresolved: worker provider`. Leases and DirectInput are leaked, never released under it. The time stays bounded and the exit decision is the escape | fake COM with a blocking call |
| P21c | A recorder blocked in write or stop gives `TimedOut`: no `worker.tsv`, the escape, and the out-of-process finalizer refuses. Releasing it later does not change the outcome | P20 sink barrier |
| P21d | No supervisor finish: `stop()` off the producer thread gives `Cancelled`, never `Complete` | thread-identity check |
| P21e | CRT teardown, run as separate child processes. Negative control: a worker stuck on a static and the main thread returning normally makes the poisoned static access observable. Escape path: the same stuck worker leaves a clean exit code and no access. Also a worker killed while holding a CRT critical section (heap or stdio): the escape path ends promptly, which is the Windows claim in section 2 | child EXEs |
| P21f | `exit()` inside the game: the `atexit` hook runs `stop()` once, a second stop returns the first report, and the budgets hold | child EXE |
| P21g | Teardown audit: no stdio, no dynamic-destructor statics, no `detach` outside the host in worker-reachable shim code. Its mutations are caught | `write_audit` extension |
| P21h | Ordinary process: no profile, or DirectInput unavailable, means no workers or a bounded failure, then a normal return and native input | fake provider |

P28's rerun of P21 through the real provider over fake COM objects stays as written.

## 7. Questions for Astra

1. **D4's escape.** It skips CRT and static teardown only when something is unresolved. I found no game state that
   depends on that teardown (section 2). Accept it, or prefer a longer bounded wait before escaping?
2. **D2's status file.** Is `shim-status.jsonl` separate from `runtime.log` acceptable, or should status lines go
   through the main thread?
3. **Budgets.** 1000/200/200 ms. Is 1000 ms enough for the close plus the closed-file hash at the 64 MiB ceiling on
   a slow disk, or should the recorder's budget scale with the bytes written?
4. **D11 (c).** Out-of-process writing as the rule for DLL hosts. DRIVE's managed recorder (`Dbce.Wheel.Recording`
   `SessionRecorder.Stop(timeout)`) is in-process today. Its process exit is BepInEx/Unity's, not ours, so the same
   reasoning applies to it, and its existing evidence path is worth re-examining separately.
5. **WM_ENDSESSION (D6).** Worth wiring now, or record the gap?
