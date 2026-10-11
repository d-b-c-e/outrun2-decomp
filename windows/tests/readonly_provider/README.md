# Read-only provider probe (P29)

Source candidate only. Build with CMake / ClangCL in Release. The executable
requires an explicit `--read-only-device-probe <profile.ini> <new result>`.
Do not run it without the shared rig lease and owner-idle gate.

This is a standalone probe, with no game, game adapter, injection admission or
force implementation. Its hidden owned window is never shown or focused. The
existing bounded provider host opens on its worker; the main thread performs
300 actual Core ticks with the normal 100 ms freshness bound. Physical bytes,
call outcomes, timestamps and action status are retained after resolved shutdown.
There is no session recorder; its no-capture slot has nothing to drain or seal.
No `complete.tsv` can be produced.

Run under an identity-bound external process supervisor with a 20-second maximum,
including initialization, reads and teardown; a forced exit must fail. P29's
cross-thread acceptance and read latency are the only intended runtime claims.
No driving, controls acceptance, reconnect, game integration or full P30
object/WGI inventory is established. The provider's nonexclusive per-open range
and deadzone requests use the same existing code as the fake-COM qualification.

`Run-Probe.ps1` is that supervisor. Its caller owns the rig lease and idle gate;
pass the reviewed executable/profile SHA-256 values and the lease path/token.
It retains the exact executable and profile before launch, checks the child's PID
and creation time against its normal-shutdown report, and saves `receipt.json`.
An unresolved close keeps the receipt failed and requires retaining the lease.
The host's recorder slot is deliberately lazy; its no-capture completion field
means nothing was queued, not that a recording was made.

The first host regression on `9e8b727` completed its cases and mutations but
refused its final source pin because this supervisor was added during that run.
That result is retained as an invalid whole-run receipt; re-run from a clean
commit before using the candidate. The combined recorder regression completed
with its source pin intact before that edit.

The host constructor now accepts an owned HWND and freshness bound; existing
offline callers keep their prior defaults. This does not change production code.
