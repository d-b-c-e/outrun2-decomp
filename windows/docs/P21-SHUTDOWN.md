# P21: shutdown and host lifetime (design proposal, r2)

**Proposal r2, 2026-10-11. Author nene (Claude), for Astra's review (hcom 11139; r1 review 11773). zero is away; this
fills the "Core shutdown gate" left open in INPUT-SHIM.md section 6. Source only: no integration, no device read, no
launch. Based on `input-shim` `d956e20` and the P20 recorder spike (`codex/shim-recorded-frames` `24e27bd`).**

r2 replaces r1's D4. r1 let the host call `ExitProcess` itself when a worker was unresolved. That can deadlock:
`ExitProcess` terminates the other threads and then runs every DLL's detach code, and if a terminated thread held a
lock that detach code takes, the exit deadlocks (Microsoft documents this for
[`ExitProcess`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess);
compare [`TerminateProcess`](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess),
which notifies no DLL). A passing fixture cannot show that deadlock is absent. `fflush` can also block.

r2 makes three changes:
- An unresolved shutdown is **tainted**, and only a supervisor outside the process may end it by force.
- **Sealing requires a resolved host shutdown and an externally observed normal exit**, not the worker's report alone:
  the provider can fail after the writer has completed.
- D9 no longer calls a budgeted stop from a destructor.

r2.1 (Astra 11867) adds three things:
- The recorder resolves only when its worker has actually joined, a fact carried separately from the outcome.
- The unresolved path writes nothing to `stdout` or `runtime.log`.
- A hanging synchronous report write is stated as a known limit for ordinary processes.

## 1. What is already decided, and what is open

INPUT-SHIM.md section 6 fixes the shutdown **order**:
1. a final neutral frame;
2. force stop (`none` for OutRun);
3. run end;
4. recorder drain, bounded to 1 s;
5. read leases released after the input thread is out of `tick()`, waiting at most 200 ms, otherwise leaked;
6. workers stopped with a bounded join;
7. DirectInput released.

The provider (slice 3.1) keeps its state alive for every call already started. What it still needs is an owner that
never destroys it while one of its threads may still start a call. That is "the core's shutdown gate, still open".

P20 leaves P21 these requirements (recorder lifecycle README, `24e27bd`):
- **No teardown beside a live worker.** "Normal process/CRT teardown with an unresolved worker is not qualified. P21
  must keep the host and CRT alive until the worker exits, or isolate the writer in its own process."
- **Owned shutdown.** P21 owns producer and controller shutdown, and bounds idle and termination. It cannot depend on
  destructors running by accident.
- **No supervisor finish.** "If the input thread is stuck, P21 cannot substitute a supervisor finish."
- **No interference.** Do not cancel the OS I/O, close the handle from another thread, or kill the worker.
- **Finalizer host.** The production finalizer host and its deadline are still to be chosen.

## 2. Facts about the OutRun host (read from `d956e20`)

- The shim is compiled into `OutRunDesktop.exe` (`windows/CMakeLists.txt`). It is an EXE, never unloaded.
- `wWinMain` (`windows/source/window_desktop.cpp`) runs in this order:
  1. the fence;
  2. reopen stdout and stderr onto `runtime.log`, unbuffered;
  3. `winrt::init_apartment()` (MTA);
  4. `or2_xbox_main(...)`;
  5. `wheel_input_shutdown()`;
  6. `return`, which runs the CRT `exit()`: atexit handlers and static destructors run while every other thread is
     still running. Then `ExitProcess`.
- `WM_CLOSE` sets `closed`, and the game loop ends. No game tick follows a close.
- `src/` and `windows/source/` call no `exit`, `ExitProcess`, `TerminateProcess`, `abort` or `quick_exit`.
- The input filter runs on the main thread. **The shim's input thread, the P20 producer, is the main thread**, the
  same thread that returns from `or2_xbox_main`.
- Game files are written through a temporary file with an explicit flush or close.
- The crash filter writes to `runtime.log`; the process then ends.
- The runner already supervises the game from outside, bound to its identity. `owned-process.json` holds the PID and
  start time, and `owned-process-exit.json` holds the exit code. The display watchdog follows the same process.

## 3. Hazards

| # | Hazard | Where |
|---|---|---|
| H1 | CRT exit (atexit, static destructors, stream close) runs while a shim worker is alive and touches CRT or static state | return from `wWinMain` |
| H2 | A worker blocks without bound: DirectInput `EnumDevices`/`Acquire`/`Poll` in a driver; a file write, flush or close; hashing | provider, control and recorder workers |
| H3 | Destroying state that a blocked or starting call still uses | the provider's shutdown gate |
| H4 | stdio from a worker: `stdout` is `runtime.log`, which CRT exit closes; `fflush` can block | any `printf` |
| H5 | A supervisor fabricates the producer's end, so a tape with a missing tail seals | recorder |
| H6 | An exit that skips the host's stop: a crash, an external kill, a foreign `ExitProcess`, logoff | any |
| H7 | Shutdown without bound: the owner closes the game and it never exits | any |
| H8 | DLL hosts (the second adapter): unload with a live worker; joins in `DllMain`; the host does not own process exit | future |
| H9 | Ending a process from inside it with work unresolved: `ExitProcess` can deadlock in DLL detach; `fflush` can block | r1 D4 |

## 4. Decisions

**D1. One host object, owned by `wWinMain`.**
- `ShimHost` is a local (`std::unique_ptr`) created after the fence and before `or2_xbox_main`. It is never static.
- No shim object that owns a thread is static.
- Every worker holds a `std::shared_ptr` to everything it touches: queues, generations, the provider state, the
  recorder `State` and sink, and its own status file. The provider and the P20 worker already do. A worker never
  reaches freed host state, whenever it finishes.

**D2. Workers never touch CRT streams or process-global mutable statics** (H1, H4).
- Status lines go from the status queue to the control worker's own file, `shim-status.jsonl`, opened once with its
  own handle. Workers never use `stdout`.
- The host writes its shutdown report through its own pre-opened handle with `WriteFile`. It never calls `printf` or
  `fflush` on a path that might block behind a worker.
- A teardown audit, an extension of zero's whole-file lexer in `write_audit`, refuses three things in worker-reachable
  shim code: stdio; function-local `static` and namespace-scope objects with dynamic destructors; `std::thread::detach`
  outside the host.

**D3. `stop()` runs on the input thread, once, with budgets** (H3, H5, H7). It is called after `or2_xbox_main`
returns, on the main thread, which is the producer and input thread. It is idempotent: a second call returns the
first report. Phases and budgets:
0. **Mark.** Write `host-shutdown.json` `{state: "stopping", nonce, started}` through the host's handle, before
   anything can block. A process that never gets further is visibly unresolved.
1. **Consumption.** The adapter stops. In OutRun no game tick follows, so the report says
   `neutral: not delivered (game loop ended)`. A stop requested while the game still ticks delivers the neutral frame
   on the next tick.
2. **Force:** `none`.
3. **Run end.** The input thread records the run's final state as an event.
4. **Recorder, at most 1000 ms.**
   - The producer (this thread) pushes the final run-end frame and calls `finish(last_sequence)`.
   - `Controller::drain(1000 ms)` includes the inner close and the closed-file size and SHA (P20 v2).
   - r2.2 (integration with the reviewed P20 composition): the reviewed `report()` member writer turns the worker's
     **final, quiescent** report into `worker.tsv`. It runs on the producer thread, and only after the drain returned
     an eligible outcome **and the worker thread was joined**. The same applies to `profile.ini`, `bundle.meta` and
     `budget.tsv`.
   - No member is written for an unjoined or ineligible recorder. A host that never joined the worker therefore never
     produces a worker report. Writing it from the host does not add trust: D8 still needs the host's resolved report
     and the external exit observation.
   - The drain returns the outcome **and whether the worker thread actually finished and was joined**, as separate
     facts (r2.1, Astra 11867). A recorder resolves only when both hold: outcome `Complete` and worker joined.
   - The worker can publish `Complete` and then block writing its own report. A `Complete` outcome is not a
     finished worker, and the first-terminal rule keeps `Complete` even after the deadline.
   - So "worker not joined" is reported and makes the host `unresolved` on its own.
5. **Leases, at most 200 ms.** `DInputProvider::shutdown(200)` stops admission and releases idle devices. No tick is
   in flight: the input thread is the caller.
6. **Workers, one shared 200 ms deadline.** The provider and control workers are told to stop and joined. The
   control worker closes `shim-status.jsonl` before it exits.
7. **DirectInput** is released only when no lease and no worker is unresolved. Otherwise it is leaked.
8. **Report.** Rewrite `host-shutdown.json` with `{state: "resolved" | "unresolved", nonce, steps, workers, recorder
   outcome, elapsed}`.
- The total is at most about 1.45 s plus the report writes. Every phase starts even when an earlier one timed out.

**D4. Exit (r2): a resolved shutdown returns normally; an unresolved one is tainted and never ends itself in a test
process** (H1, H9).
- **Resolved:** every worker joined and nothing is leaked. Destroy the host and return from `wWinMain`. CRT exit runs
  with no shim thread alive.
- **Unresolved, test-requested (latched) process.** The capture is tainted and is never sealed (D8).
  - The host must not run CRT teardown beside the live worker, and must not end itself with `ExitProcess`.
  - So the main thread **parks**: it waits on an event that is never set and never returns, with the report already
    written.
  - The external, identity-bound supervisor sees `host-shutdown.json` `unresolved` (or `stopping` beyond its bound,
    or no change of state). It ends the process by its own handle with `TerminateProcess`, labelled
    `forced: unresolved host shutdown`, and records the exit it observes.
  - The runner already holds that handle and identity (section 2). A forced end is an outside fact, never a silent
    in-process exit.
- **Unresolved, ordinary (owner) process.** There is no capture and no supervisor; a prompt exit matters more than
  CRT teardown.
  - The host writes the report and calls `TerminateProcess(GetCurrentProcess(), ForcedExitCode)` (distinct code
    `0xDBCE0021`).
  - This notifies no DLL, so no detach code can deadlock on a lock the stuck worker held.
  - It is acceptable (Astra 11867) only because this source-only, input-only shim owns its EXE, holds no active
    force, and the game's saves are flushed when written. No evidence is at stake.
  - It is labelled `forced` in `host-shutdown.json` only.
  - It loses unflushed CRT buffers, which hold nothing here, and skips DLL cleanup, which the OS reclaims.
  - No supervisor observes an owner process; the report is the only record.
- **Nothing on the unresolved path writes to `stdout` or `runtime.log`**: no `printf`, no `fflush`, no CRT stream.
  The only write is the host report, a synchronous `WriteFile` on a handle opened at host start.
- No in-process path calls `ExitProcess` after an unresolved stop.
- **Limit: that report write can itself hang** (H2).
  - In a latched process this is covered: the supervisor sees no state change past its bound and ends the process,
    labelled forced. The capture stays tainted.
  - An ordinary process has **no hard exit-time guarantee** if the report write or an earlier phase hangs. The host
    bounds every wait it controls, but cannot bound a blocked synchronous `WriteFile`. This is recorded as a known
    limit, not claimed away.

**D5. No I/O cancellation, no closing handles from another thread, no `TerminateThread`** (P20 rule).
- The budgets plus D4 cover file and driver hangs.

**D6. Emergency and abnormal exits** (H6).
- **`exit()` called inside the game.** None exists today. A hook registered at host start runs `stop(ExitCalled)`
  with the same budgets and then applies D4. It is `std::atexit` with a trivially destructible atomic pointer.
  - In a latched process, an unresolved stop parks inside the hook.
  - A resolved stop lets `exit()` continue.
- **`WM_ENDSESSION`** (`wParam` TRUE), optional. `stop(SessionEnd)` runs inside Windows' end-session allowance and
  the D4 rules apply.
- **A crash, an external kill, or a foreign `ExitProcess`.** Nothing of ours runs. `host-shutdown.json` stays
  `stopping` or absent, so the capture is tainted. Its seal is refused even when `worker.tsv` looks complete.

**D7. Never a supervisor finish** (H5).
- `finish()` is called only by the producer thread, and `stop()` checks that it runs on the producer thread.
- If it does not, the recorder is cancelled: `Cancelled`, never `Complete`.

**D8. Sealing (r2): out of process, and only with three independent facts.** The runner's finalizer
(`CompletionBundle`/`ArtifactSeal`, which writes `complete.tsv`) seals only when all three hold:
1. `worker.tsv` is `Complete`, with the capture's nonce and its closed size and SHA (P20 v2);
2. `host-shutdown.json` is `resolved`, written by the host after every join, and carries the same nonce;
3. the supervisor observed a normal exit of the owned process: matching identity, exit code 0, no forced end.

Any one missing, mismatched or forced refuses. A `Complete` writer report does not seal by itself, because the
provider or control worker may be unresolved after the writer finished. No sealing happens inside the game's shutdown
budget.

**D9 (r2). Destructors never wait, and never stop.**
- `~ShimHost` neither calls `stop()` nor joins nor waits.
- Destroying a host whose `stop()` has not returned is a defect. The destructor:
  - sets a `destroyed_without_stop` flag in shared state;
  - leaves every worker running on its own shared ownership: no join, no release, no detach of a joinable thread
    other than the host's documented detach of unresolved workers;
  - writes nothing.
  - `host-shutdown.json` is then never `resolved`, so the capture cannot seal.
- In `wWinMain`, `stop()` always runs first, and `host.reset()` comes only after a resolved stop.

**D10. Idle bound.**
- A capture the host never stops still ends at the writer's limit (600 s) as `WriterLimit`.
- The provider and control loops are paced and wait on a stop event.

**D11. Rules for DLL hosts** (the second adapter; H8).
- (a) Pin the module at host start with `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, ...)`. Where pinning is
  unacceptable, use `FreeLibraryAndExitThread`.
- (b) Never stop or join in `DllMain`. Stop from the game's own quit hook on its own thread; the adapter names that
  hook.
- (c) A guest DLL does not own process exit, so there is no in-process qualification writer in a DLL host.
  - The in-process side only produces into a bounded shared-memory ring: no worker thread, no file I/O.
  - A recorder process owned by the runner drains the ring, writes the capture and seals it.
  - A dead or absent reader, or overflow, makes the capture incomplete, and that is counted.
- OutRun, an EXE that owns its exit, keeps the in-process P20 worker under D3, D4 and D8.
- DRIVE's managed in-process recorder (`Dbce.Wheel.Recording`) runs inside BepInEx and Unity, so its exit is not
  ours. It is a **separate audit item**. Nothing here changes or rewrites it.

## 5. API sketch (for review, not code)

```cpp
struct StopBudget { int recorder_ms = 1000, leases_ms = 200, workers_ms = 200; };
enum class StopReason : std::uint8_t { GameReturned, ExitCalled, SessionEnd };
enum class Resolution : std::uint8_t { Resolved, Unresolved };
struct StopReport {
    Resolution resolution{};
    std::vector<std::string> unresolved;   // "recorder: TimedOut", "worker provider", "lease 2", ...
    bool neutral_delivered{};
    recorder::Outcome recorder{};
    std::int64_t elapsed_us{};
};
class ShimHost {
public:
    static std::unique_ptr<ShimHost> start(const HostConfig&) noexcept;   // input thread
    StopReport stop(StopReason, StopBudget = {}) noexcept;                // input thread only; idempotent; bounded
    [[noreturn]] void park_for_supervisor() noexcept;                     // latched + unresolved (D4)
    [[noreturn]] void forced_exit() noexcept;                             // ordinary + unresolved: TerminateProcess (D4)
    ~ShimHost();                                                          // never stops, never waits (D9)
};
```

`wWinMain`, after the fence:

```cpp
auto host = ShimHost::start(config);
const int result = or2_xbox_main(argc, argv);
if (host) {
    const auto report = host->stop(StopReason::GameReturned);
    if (report.resolution == Resolution::Unresolved) {
        if (plan.latched) host->park_for_supervisor();   // tainted; the runner ends it, labelled forced
        host->forced_exit();                              // owner process: prompt, labelled forced
    }
    host.reset();                                         // resolved only
}
outrun::desktop::wheel_input_shutdown();
return result;
```

## 6. Proof plan (offline; P21 rows in section 17)

| Row | Shows | Fixture |
|---|---|---|
| P21a | Order and budgets: the producer pushes the run end and finishes, then the drain, leases and joins; `resolved`; elapsed under the budget plus a margin | prototype host, fake workers with barriers |
| P21b | A provider worker blocked in a fake driver call gives `unresolved: worker provider`. Leases and DirectInput are leaked, never released under it. Elapsed stays bounded | same |
| P21c | A recorder blocked in write or stop gives `TimedOut` and `unresolved`. Releasing it later cannot make the host report `resolved` | same |
| P21c2 | The recorder publishes `Complete`, then blocks writing `worker.tsv`. The host is `unresolved` with "worker not joined"; the late `Complete` report still cannot seal | report-write gate |
| P21d | No supervisor finish: `stop()` off the producer thread gives `Cancelled`, never `Complete` | same |
| P21e | The supervisor (separate processes): a latched child with a stuck worker writes `unresolved` and parks. The parent ends it by its own handle and records `forced`. An ordinary child exits with `0xDBCE0021`. A resolved child exits 0 through normal CRT teardown | child EXE plus parent supervisor |
| P21f | `exit()` inside the game: the hook runs `stop()` once, a second stop returns the first report, and D4 applies | child EXE |
| P21g | Teardown audit: no stdio, no dynamic-destructor statics, no `detach` outside the host in worker-reachable code. Mutations are caught | `write_audit` extension |
| P21h | The seal rule: of the 8 combinations of worker `Complete` x host `resolved` x normal external exit, only all-three seals. A swapped nonce in either report refuses | finalizer rule fixture |
| P21i | Destruction without `stop()` returns at once (no wait), and the shutdown file never reads `resolved` | prototype host |

| P21j | Integration with the reviewed P20 recorder (`windows/tests/p21_recorder_integration`): actual Core/Adapter frames, the real native writer and closed-file seal, the worker thread joined, supervised child processes, and the out-of-process finalizer sealing only the resolved capture | `run.py --mutations` |

P28's rerun of P21 through the real provider over fake COM objects stays as written.

## 7. Questions for Astra

1. **D4, ordinary process.** Is a self-`TerminateProcess` acceptable for the owner's process when a driver call hangs
   at exit, or would you rather the host return normally and accept the teardown risk there?
2. **D2.** Is `shim-status.jsonl` separate from `runtime.log` acceptable?
3. **Budgets.** Is 1000/200/200 ms right, or should the recorder's budget scale with the bytes written (the 64 MiB
   hash ceiling)?
4. **Supervisor bound.** How long should the runner wait after `stopping` or `unresolved` before a forced end?
   I propose the stop budget plus 2 s.
5. **`WM_ENDSESSION`.** Wire it now, or record the gap?
