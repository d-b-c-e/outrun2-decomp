# Queued Core-frame composition proof

This isolated P20 fixture joins the two earlier proofs: actual Core and OutRun
adapter observations now cross the reviewed recorder queue, the native toolkit
Writer, the production managed SessionReader and ArtifactSeal, and replay through
the real Core/adapter. Devices and game state are scripted fakes. Nothing is
installed, no native device API is called, and no game or physical force is used.

```powershell
py -3 -I -B windows/tests/queued_frames_spike/run.py `
  E:/Source/dbce-project-mgmt/artifacts/queued-frames-new `
  --toolkit E:/Source/toolkits/dbce-wheel-mod-toolkit --mutations
```

The runner requires committed clean source, pins all source bytes plus the used
toolkit files before and after, and writes every build/output to a new result
directory. The native writer is the unchanged reviewed `A9DE5D49...4D19DB` pin.
Native and managed builds treat warnings as errors. Mutations use private copies.

## What this establishes

- Sixteen actual two-device Core frames, each with all 360 prior frame channels
  and all 15 numeric OutRun adapter channels, enter the worker as owned packets.
  The native writer is held on its first row while the producer finishes every
  tick. Producer packet storage is overwritten after each push; every original
  value must survive. The bounded ring contains 16 packets / 131456 payload bytes.
- Poll Pending, State Pending, failure, reopen, held-button rearm, fresh press,
  navigation and a race/menu transition are exercised. This fixture explicitly
  adopts Call::Pending=4 under `shim-frame-audit-2-pending-unadopted`. Unknown Call
  values are refused. The historical `session_spike` codec remains unchanged;
  `frame_codec.hpp` is a bounded fixture copy with the explicit enum extension.
- Whole-file validation precedes export. A fresh nonce plus the worker's closed
  file size/hash joins its result to the source, exact profile bytes and complete
  five-file inventory in the existing seal. Interrupted/lossy results cannot be
  qualified by the inner footer. The report comes from the actual quiescent
  worker, not a report invented by the managed verifier.
- Raw replay passes recorded physical states and call outcomes to a fresh Core
  using the fixture's known identities/caps and deterministic clock. Its actions,
  epochs, edges and provenance must match. Action replay applies decoded frames
  through a fresh OutRun adapter using the original scripted GameView. Typed
  comparisons check every Frame/DeviceSample/ActionValue field, NativeInputState
  field and adapter signal independently of the serialized channel comparison.
- Inverted steering and a wrong shift button change raw replay, so the resolver
  is exercised. A deliberately wrong adapter output word is detected too.
- A separate ten-frame source window begins at sequence 7. It validates
  `tail - first + 1 == written`, exact times, hashes and typed snapshots only.
  It does **not** claim mid-session resolver/adapter state restoration.
- Schema preflight includes the adapter channels: eight full devices plus these
  signals exceed 1024 and are refused, without silently dropping fields. The
  known schema has a conservative row bound using exact escaped key lengths,
  32 bytes per finite binary64 value and fixed JSON/sequence/time reserves.
  Metadata and footer each have an 8192-byte reserve. The requested frame bound
  is checked by division to avoid overflow. Six hundred seconds at 60 Hz does
  not fit the default 64 MiB and is refused before opening the writer. The
  managed proof independently recalculates the bound and checks actual rows.

Eight refusal controls cover a swapped source, first/last mismatch, timeout,
upstream drop, changed profile, an extra sealed member and a changed report.
Three implementation mutants must fail at their intended boundary: omitted
queue copy, the old codec refusing Pending, and a changed serialized adapter word.
The broader lifecycle fault matrix remains in `recorder_lifecycle_spike` at
reviewed `a1e964e`; this composition does not replace it.

## Remaining production work

This is `dbce.shim.queued-frames-spike@1`, **not a portable replay bundle**. The
profile is exact, but device-slot/caps/epoch histories, full event/batch journals,
the rearm policy and game context are still fixture knowledge. EventKind::ReadPending
is not yet serialized as an event journal. Mid-session state, source/replay clock
semantics, final neutral consumption and recording control-surface integration
remain open. There is no second-game reuse result and no trajectory replay.

The producer's channel map allocation is bounded by this fixture's known schema,
but the current snapshot encoding is not a real-time allocation-free API. The
production host must preflight rate/duration, profile/metadata and worst-case
variable device/adapter schemas together; this fixed schema is only a demonstrator.
The native identity helper and managed envelope here are fixture-local until a
production snapshot contract is selected; no stable API is being duplicated.

The managed finalizer is synchronous. File handles deny ordinary write/delete
sharing and ancestors reject reparse points; this is not hostile-directory or
authorship authentication. A party able to forge the entire worker report and
bundle is outside this integrity claim. P21 must still choose the runtime host,
bounded finalization and unresolved-worker/CRT exit policy. Do not put these
detached workers into a game on the strength of this offline proof alone.
