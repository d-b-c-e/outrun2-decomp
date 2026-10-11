# P21 host lifetime proof (offline design fixture)

An executable design proof for `windows/docs/P21-SHUTDOWN.md` r2, section 6. It is not production shim code: the
provider, control and recorder workers are fakes that block on gates where the real ones can (a driver call, a file
stop/close/hash). No device, game, file outside the result folder, or launch is involved. It is not part of the
OutRunDesktop build.

```powershell
cmake -S windows/tests/p21_host_spike -B <build> -G "Visual Studio 17 2022" -A x64
cmake --build <build> --config Release
<build>\Release\p21_host_spike.exe <new empty result folder>
```

## Rows and what they show

- **P21a.** A quiet host stops resolved, with a complete recorder. Nothing waits. The host report
  (`host-shutdown.json`) and the worker's own `worker.tsv` both carry the capture nonce. A second `stop()` returns
  the first report.
- **P21b.** A provider blocked in a driver call, with a device in flight, is unresolved: `worker provider` and
  `lease leaked`, within the budgets. The host is then destroyed with the worker still blocked; released afterwards,
  the worker finishes on its own shared state.
- **P21c.** A recorder blocked in stop times out (`TimedOut`). A later finish never becomes `Complete`, and the host
  report stays `unresolved`.
- **P21d.** `stop()` called off the producer (input) thread cancels the recorder. It is never a supervisor finish.
- **P21i.** Destroying a host without `stop()` returns at once, and the shutdown file never reads `resolved`.
- **P21h.** The seal rule: of the 8 combinations of worker `Complete`, host `resolved` and a normal externally
  observed exit, only all three together seals. A report from another capture refuses, and so do a nonzero exit,
  another process, or a missing host report.
- **P21e** (supervised child processes, ended by the parent through its own process handle):
  - A resolved child returns through normal CRT teardown with exit 0.
  - An ordinary child with a stuck worker ends itself with `TerminateProcess`, labelled forced (`0xDBCE0021`).
  - A latched child with a stuck worker writes `unresolved` and parks. Only the supervisor ends it.
- **P21f.** `exit()` called inside the game runs the `atexit` hook's `stop()` once, and `exit` then continues
  (exit 7). With a stuck worker in a latched process it parks for the supervisor instead.

Budgets are scaled down for the fixture (150/100/100 ms; production 1000/200/200 ms). The supervisor waits for the
stop budget plus 2 s.

## Negative controls (each run as a private mutation, then reverted)

- A latched child that returns instead of parking fails P21e.
- A seal rule that ignores the host report fails P21h.
- A late finish allowed to become `Complete` fails P21c.
- A destructor that runs a budgeted stop fails P21i.
- `finish()` allowed off the producer thread fails P21d.

## What this does not show

It does not show the absence of a DLL-detach deadlock. No in-process path relies on that: an unresolved latched
process parks, and only the external supervisor ends it.

It also does not cover:
- the real provider, COM or DirectInput (that is P28);
- the real P20 writer;
- the teardown audit (P21g);
- `WM_ENDSESSION`.
