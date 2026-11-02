# Copied physical input provider, source candidate 2026-10-11

Based on `a9e16010e4d8909b71d1090a8266551ce74acb14`, isolated branch
`codex/art-shim-bridge`. Intended source: toolkit snapshot commit `ecc49bd`,
which reads Art's existing WheelFfb-owned handle. No installed DLL changed.

Review these paths in order:

1. `shim_dll/dbce_shim_bridge.h`: additive extension version 1; descriptors
   144 bytes and samples 232 bytes at pack 8. Base ABI stays 2, frame 296,
   open arguments 128; no existing export is removed or repurposed.
2. `shim/bridge_provider.*`: bounded copied buffers, memory leases keyed by
   globally unique source generation, exact GUID/product/capability identity,
   immutable generation metadata, strictly advancing original sample sequence
   and non-regressing QPC interval. Missing samples are Pending; duplicate,
   future, failed-generation or malformed batches clear delivery and refuse.
   No DirectInput/COM call, callback, Acquire, worker device recovery or force
   operation exists in this provider. Only the external owner may recover.
3. `shim/shim.hpp` and `shim/core.cpp`: optional `Provider::source_read`
   provenance replaces the measured copy interval with the physical source's
   original QPC start/end. Core retains source generation/sequence separately
   from its own epoch/sequence and uses original age for freshness. Default
   false keeps existing DirectInput timing and resolver behavior unchanged.
4. `host/provider_host.hpp`, `shim/dinput_provider.hpp`: the existing host
   accepts `ProviderOwner` with the same shutdown report. Its normal constructor
   still creates DInputProvider; control, recorder and bounded P21 shutdown
   phases remain shared. The copied provider's release only clears memory.
5. `shim_dll/dbce_shim.cpp`: bridge open/devices/tick routes use copied buffers
   and the existing owner-thread/spent-handle registry. Base tick refuses a
   bridge handle; bridge calls refuse normal handles. No call to DirectInput8Create
   occurs in bridge open. Ordinary and fenced LATCHED opens are admitted (see
   the October 12 addition below). CAPTURE still refuses: the recording format
   does not carry external source sample provenance.

Original HRESULTs and validity travel in the extension sample. Core consumes
the original classified results; the caller must retain the original buffer
when it needs HRESULT evidence. Validity false after identity/reentry invalidation
can accompany successful HRESULTs; that contradictory-to-valid-read buffer is
refused, never treated as neutral or fresh. No new recording schema is claimed.

Validation completed before the performance-build hold:

- ClangCL x64 release `dbce_shim`, `test_shim`, `test_shim_abi`, `test_shim_bridge`
  built with the existing /W4 /WX settings.
- CTest `shim`, `shim_abi`, `shim_bridge`: 3/3 pass. Bridge run reported 39
  assertions (worker-ready loop can change count). Tests prove original age
  becomes Stale, copied buffers cannot be edited by caller, exact provenance,
  omission/repeat/future/time regression, new/retired generation, failed read,
  no provider recovery, base ABI constants, flag refusal, owner thread, and
  resolved shutdown. Actual C ABI bridge open/ticks use memory only.
- Art's managed binding successfully ran this actual DLL through Core and its
  axis sink under .NET8 and installed Unity 2019 Mono, with a fake source using
  QueryPerformanceCounter directly. Mono Stopwatch had a different origin;
  its stale samples were correctly rejected by Core freshness before correcting
  the fixture. Production snapshot timestamps come from native QPC.
- After the performance hold released, the full default P21 provider-host
  harness passed at source commit `d46096c40fa3806bdde6223212af07b541163b5f`:
  129 supervised host checks, 84 DInputProvider checks, clean teardown lexer
  and compiler audits, and their mutation controls. Receipt:
  `E:/Source/research/.worktrees/art-shim-bridge-evidence/p21-20261011/receipt.json`.
  Optional extra `--mutations` host-defect cases were not run.

The built x64 source candidate SHA-256 is
`C4D76DA58E62867F938FC1388AC72632C480C3ECD6A83BB03279841E11E89E10`.
It is build evidence only, not a release pin or a deployed artifact. No native
device, game, force, runtime capture or saved-profile activation was attempted.

Before integration: assign reviewed native candidate provenance, then complete
Art identity-based Rewired isolation and explicit startup/profile Apply.
Existing Art menu/shifter/shortcut consumers stay legacy; only five driving
axes have a shared-frame sink. The first rig work must be serialized, backed up,
muted and exact-restorable; accepted Art v2 force tune and captures stay fixed.

## Exact owned interface paths (additive follow-on)

The direct-provider metadata change `f49cfc5` is included here as `1e88feb`.
`DeviceInfo.interface_path` flows through the adopted epoch into the additive
`interfacePath` device-report JSON field. An unadopted/failed epoch reports an
empty path. The copied provider uses the same Core field and thread ownership.

New optional export `dbce_shim_bridge_devices_with_paths(handle, devices, count,
paths)` copies the original 144-byte metadata batch and a corresponding array
of nonempty UTF-16 paths in one atomic submission. Each path is at most 259 code
units and must convert strictly to UTF-8. Metadata, including path, is immutable
within a source generation. Bad/missing/changed paths refuse the entire batch;
no partial metadata is published. The source must obtain each path from the
same qualified owned handle/generation, not infer it from enumeration labels.

Base ABI 2, extension version 1, both wire structs and all old exports are
unchanged. Original `dbce_shim_bridge_devices` continues to publish unavailable
(empty) paths. It is compatible but cannot supply an exact cross-namespace
DirectInput/RawInput identity proof. No new device/force ownership is added.

Targeted CTest `shim`, `shim_abi`, `shim_bridge` passes 3/3. The copied suite
passes 46 checks, including atomic path validation, immutable generations,
copied input buffers, Core path reporting and JSON publication. Art's fake-source
actual DLL tests pass 431 checks on .NET8 and Unity Mono, exercising both the
original metadata export and the new path export. No real path/device was opened.

## Fenced latched control (2026-10-12)

`dbce_shim_bridge_open` now admits `DBCE_SHIM_LATCHED` through the existing
`open_impl` and `ProviderHost` control worker. Exact base checks remain: both
session/batch paths, `fences_verified == 1`, `force_absent == 1`, nonce-bound
session admission, and the existing expiry/readiness rules. These bits attest
the consumer's verified process-latched force mute and fences; the DLL cannot
independently prove game-side mute/score guards. Art must qualify those before
live use. Unknown flag bits return BAD_ARGUMENT; CAPTURE and LATCHED|CAPTURE
return REFUSED. No ordinary/direct-provider admission rule changes.

Copied snapshots remain **physical** evidence, with original source generation,
sequence, QPC interval and physical values preserved in Core. During a latched
run those values alone produce TestNeutral. Only an accepted nonce-bound control
batch produces injected actions. Completed/failed/expired runs stay neutral and
`suppress_native` stays true. The existing P21 stop performs the final neutral
tick, control journal drain, worker join and memory-only bridge release.

Bridge control journals use the existing v1 rows with the optional immutable
identity field `inputSource: "external-physical-input@1"` on every row. The exact
Controls SHA-256 and profile device identities still come from Core's parse;
executable/build hashes remain explicitly unavailable as before. Direct-provider
journals omit the field and retain their format. The strict reader rejects an
unknown external source or a missing/changed field later in a copied journal.
This is a control/status journal, **not** a physical sample recording: it does
not serialize every copied sample's source generation/sequence/interval. Capture
therefore remains unavailable until a separately reviewed versioned format does.

Offline validation: ClangCL /W4 /WX native targets build; CTest shim, shim_abi
and shim_bridge pass 3/3. The bridge run reports 77 checks (readiness-loop count
can vary), including exact fence values/missing paths, unknown/capture flags,
actual-Core physical versus injected provenance, actual ABI control-worker
admission/batch/end, terminal suppression, complete journal and resolved stop.
The journal harness passes 366 checks; its reader also checks optional external
source identity and drift. No game, DirectInput device, force, install or capture
was opened. The earlier DLL hash above is historical, not this new candidate.

## Ordinary copied input with declared extras, October 16

The additive `dbce_shim_bridge_open_extras` entry requires a nonempty existing
C4 declaration (at most 4096 bytes), reads controls schema 1, and refuses latched
or capture flags. `dbce_shim_bridge_tick_extras` accepts the same copied sample
batch and observed-context mask and returns both common and extras frames from
one Core tick. No new device reader, force owner, callbacks or ABI layout exists.
Base ABI 2, bridge ABI 1, extras ABI 1 and every existing export are preserved.
Both new exports refuse x86, matching the existing copied bridge admission.

The copied ProviderHost forwards the declaration to the existing Core planner.
An extras bridge handle refuses base tick, ordinary extras tick and old bridge
tick; callers must provide their physical sample batch through the new entry.
Unknown contexts or malformed frame sizes refuse before sample sequence/Core
advancement. Existing sequence/age/identity/capability rules, memory-only bounded
stop and spent-handle refusal apply unchanged. Read-only `dbce_shim_extras`
returns the same accepted frame and edges. Capturing extras or latched extras
remains unsupported. This source alone does not adopt F-Zero's native snapshot
candidate or qualify physical FFB/game response.

### Focused bridge/extras qualification

Product `d92656657a4322a37db9cfb82073350ed8d01dcc` built x64 `/MT` with
ClangCL `/W4 /WX` and passed 224 existing ABI checks. The new copied fixture
initially held its digital extra from first open and correctly remained in
release-required rearm. That failed run is retained. Test-only
`c1e2ddb502309c9a611d0e971355a9af1e1f6ad8` releases before pressing; the
copied provider/Core/ABI fixture passed 101 checks. No product or old x64 DLL/ABI
batch was repeated. The first x86 DLL/ABI batch then passed 177 checks, including
refusal of both copied extras exports before a provider exists.

Actual DLLs: x64 `83DD742F015FD384DC2A9BEE894ED8DCCCA0E204947E4FB158C3D276025888C2`,
x86 `7DE31B3EA192498D00A2813BC329BACD5598C08789ABE1E01FC72B9DAF944B47`.
Both machine/import/static-CRT checks and exact export/header checks passed:
29 existing baseline exports plus two additive entries, none removed. The x64
binary compiled at d926566 was retained; its generated final receipt names the
qualification HEAD c1e2ddb. Only the fixture differs between those commits.
`qualified-bridge.json` explicitly records both identities instead of treating
the fixture correction as a product rebuild.

Original hub evidence: `artifacts/bridge-extras-lola-20261016/run-01` (failed
fixture; built x64 ABI passed) and `run-02` (corrected copied fixture and x86).
All 35 owned identities closed; queries safe, scope zero and environment restored.
The exact lease released at `2026-10-17T00:57:16.7859330Z` before docs/handoff.
No hardware, real force, game, owner installation or runtime acceptance occurred.
