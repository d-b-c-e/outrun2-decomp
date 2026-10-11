# Read-only provider probe (P29)

Qualified read-only scope below. Build with CMake / ClangCL in Release. The executable
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

## October 11 live evidence

Source `2750b86`, executable SHA-256
`101e50f4742a16c2e3df6f97893420e8a11eef40756bd3d8fc1516ac647f3cb4`,
passed at the rig under the shared lease and owner-idle gate. No game was open,
and no injection or force implementation is linked. The same host opened on its
worker and read on the main input thread. The hidden window stayed hidden.

| Device | Fresh reads | First/last Core frame | Median/P95/max read microseconds |
|---|---:|---|---|
| R12 | 295 | 6/300 | 4 / 43 / 524 |
| DS-8X | 295 | 6/300 | 1 / 1 / 3 |

There were 300 ticks with 31 action rows each, no failed or stale observed
sample, and maximum read ages of 4/1 microseconds. Shutdown resolved in
9.241 ms, released both devices and DirectInput, held none, and the process
exited zero. These are at-rest observations at the probe's sleep-paced rate,
not high-rate or gameplay performance acceptance.

Private hub evidence: `artifacts/readonly-provider-live-02`,
`readonly-provider-live-plan-02`, `readonly-provider-live-02-independent.json`
and `analyze_readonly_provider.py`. Exact executable/profile copies are retained.
Peer source review nene14953 passed; the clean host regression has 129 host,
84 provider checks and all six host mutations. The supervisor also passes 40
fake-child checks (normal, nonzero, wrong PID/time, unresolved input/release,
and a forced timeout); that private fixture uses a disjoint dummy lease.

The first live attempt is retained failed: its profile reused the fake-COM
fixture's synthetic DS-8X product GUID. The provider reported product-mismatch
and opened only the R12, then shut down normally. The second uses the DS-8X
product observed in the contemporaneous game inventory. Its synthetic shifter
binding still names raw button index 11, which does not exist on this device;
the resolver correctly reports object-missing. P29 establishes device reads,
not this binding. P30 must inventory all three devices' actual objects/ranges
and WGI paths before any game-input qualification.

## P30 source candidate

The probe now retains `objects.tsv` from a copied Core device report at the
last input tick. It names the bound identity that the provider verified at open,
epoch and state, plus each of the 8 axis, 128 button and 4 POV offsets. Axes
include the actual range-readback flag and limits. Closed/unadopted epochs
advertise no capabilities. The report contains no native handle, reads no worker
fields, performs no I/O and is only callable on the input thread or after it
stopped. Six regression assertions cover missing/open/lost state, sparse ranges,
identity and copy isolation; the complete Core suite passes 146 checks.

The profile must have no invalid bindings and one to four distinct devices.
The final valid-stream/release counts must equal that profile's count;
`Run-Probe.ps1 -ExpectedDevices` also verifies the exact release count. Its
timeout path retains whatever redirected output has completed after closure.

`wgi_inventory --list-only <new folder>` is separate and read-only. Three WGI
metadata listings at one-second intervals retain RawGameController names,
NonRoamableIds, VID/PID and object counts, Gamepad conversion, and all Gamepads.
It calls no reading or vibration setter and opens no window. Run after the
provider probe closes, under the same lease and a bounded external supervisor.
VID/PID corroborates products; it does not prove a DirectInput instance join or
the future game's pre-consumer exclusion. C++/WinRT is compiled as C++20 because
the local SDK's C++17 coroutine header refuses Clang; the Core remains C++17.

This extension has built with warnings as errors. P30 live qualification and
independent source review are pending; the earlier P29 evidence remains pinned
to its original executable and source.
