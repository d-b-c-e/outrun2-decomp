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

The host constructor now accepts an owned HWND and freshness bound; existing
offline callers keep their prior defaults. This does not change production code.
