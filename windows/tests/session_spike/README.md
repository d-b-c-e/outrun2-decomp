# Shim recording contract audit — offline fixture only

This is executable design evidence, not a recorder installed in OutRun. It adds
no hooks, device access, force output, game launch or production API. It reuses
the real `Core`, OutRun `Adapter`, existing fake-provider fixture, native session
writer and managed `SessionReader`.

Run from this checkout, with a new result directory:

```powershell
./windows/tests/session_spike/Run-Spike.ps1 `
  -ToolkitRoot E:/Source/toolkits/dbce-wheel-mod-toolkit `
  -Result E:/Source/dbce-project-mgmt/artifacts/shim-recording-audit-new
```

Requires MSVC/CMake and .NET 8. The script pins the native writer by SHA-256
`A9DE5D49C55B9B4BE464FB173178D20A97972EACFF500AEE0267E30FB44D19DB`
(introduced at toolkit `85147b8`, unchanged at audited `4ead536`). No native DLL
is loaded. Results never overwrite prior evidence. `audit-receipt.json` records
source/executable/evidence hashes. Build output stays in ignored `build-*`/`bin`/`obj`.

## What passes

Twelve copied frames from two fake devices go through the existing native writer
to `dbce.wheel.session` v1, the actual managed reader, and a lossless numeric
export back to C++. There are 360 channels per two-device frame, preserving all
128 button bytes, all axes/POVs, physical and delivered values, source indices,
epochs, call outcomes, validity, timestamps, injection marks, action statuses and
edges. A failed shifter read and a held shift after reopening are included.

- Every decoded channel equals the original frame.
- Raw replay through a fresh real core reproduces all twelve frames, including
  rearm, epochs and edges. The fake provider is replayed; no hardware is read.
- Action replay through the real OutRun adapter reproduces its steering, pedal,
  shift, frontend and record-word outputs under the same **fixed fake game state**.
- A separate actual core injection frame preserves both devices' object marks and
  batch `999999999999999`, the parser's current 15-digit maximum.
- Invalid status, missing raw channel, invalid source index, negative ID limb and
  unknown extra channel are refused by the fixture decoder.
- Abandoned and oversized files are rejected by the production managed reader.

This does not establish route playback, mid-session checkpoints, game physics,
the real DirectInput provider, worker shutdown, queue behaviour, or P20 as a whole.
It does not exercise every lifecycle transition. No second game has proven reuse.
The fixture's caps, identity mapping, profile and fixed `GameView` are known test
inputs; this is not yet a self-contained portable replay bundle.

## Design findings before the production recorder

1. **Upstream loss is invisible to this writer.** `drop-then-stop.jsonl` deliberately
   omits one producer frame, then calls `stop()`. Both writer and generic reader
   report completed, zero drops, because their own row sequence is contiguous.
   The recorded core sequence exposes the gap. This is a caller-integration
   counterexample, not a defect in the synchronous writer's documented scope.
   Section 10's promise of gap markers and an incomplete footer is not available
   from this pin: it has neither a marker API nor an upstream-count/abort-footer
   API. The immediate compatible policy is sticky capture failure plus `abandon()`
   (missing footer), retaining the failure reason and dropped count in run evidence.
   Do not call `stop()` after an ingress drop, event loss or drain timeout. Check
   final producer counts and first/last sequence as well as internal gaps: a drop
   before the first row or after the last row is otherwise invisible. A future
   toolkit API change needs native and managed-reader compatibility tests together;
   inventing a custom footer reason alone fails today's reader.

2. **Numeric precision must be explicit.** All v1 channel values are doubles.
   `ids.jsonl` shows a standalone uint64 value of 9007199254740993 rounded down
   if cast to one double; two uint32 limbs round-trip, including UINT64_MAX.
   This is a wire-boundary test, not a reachable batch-parser defect: current
   batch IDs have at most 15 decimal digits and are exactly representable.
   Hash bytes must stay bytes/limbs, never an arbitrary 64-bit double. Either
   encode limbs, or declare and validate an exact-integer bound for each field.
   Never serialize a missing/invalid observation as a valid zero.

3. **Preflight the complete schema and byte budget.** This full-fidelity frame
   fits eight device samples (1020 channels) without adapter signals; nine is
   1130 and is refused by the writer's 1024-channel bound. The core can have more
   devices than the recording row supports. Add the adapter/force channels before
   checking the budget. Do not silently trim buttons or device samples. The 4 MiB
   queue bound must count actual owned snapshot memory, not just row count; a
   1024-row limit is not a guarantee that 1024 full snapshots fit. The current
   JSON size also needs a declared maximum before recording starts.

4. **An exact profile hash alone is insufficient for raw replay.** Pin the exact
   profile bytes, device-slot identities, every epoch's verified objects/ranges,
   rearm policy, core/adapter version and shaping settings. The fixture embeds
   only a small ASCII profile; production profiles may be UTF-8 and 64 KiB while
   this writer's metadata text is ASCII and limited to 4096 characters. Retain
   exact files in a sealed bundle, or review a bounded encoding; do not truncate
   or transliterate. A device's new epoch can have different caps. Include the
   complete event/batch journal and its loss counters, not just the last event
   number or the eight-byte batch-hash prefix carried by the sample.

5. **Starting mid-session needs a replay policy.** This proof captures from the
   core's first tick. Replaying a held button/throttle from a fresh core invokes
   rearm, which can differ from the live core when recording began. Initially
   classify such tapes as signal/action captures unless a complete startup or a
   validated resolver checkpoint is present. H-pattern action replay additionally
   depends on the observed car/gear and adapter state. Do not claim it reproduces
   the sink from actions alone. A prerecorded gear or pose is not proof of live
   transmission/physics response.

6. **Keep the three replay purposes distinct.** Raw replay exercises bindings and
   resolver behaviour; action replay exercises the game adapter; pose replay
   supplies repeatable presentation. Live raw replay must enter a newly fenced,
   muted test run with fresh admission/batches. Old nonce/expiry/epoch IDs are
   evidence, never live authorization. Ordinary and latched frames have different
   neutral/rearm rules, so expected comparisons must name their mode. Force and
   telemetry calculations stay observable while physical outputs remain muted.

7. **Keep clocks honest.** `tick_us` is tick start, `boundary_us` follows reads;
   neither is the recorder worker's write time. Copy both, retain adapter/physics
   source sequence/time separately, and never give late data an invented fresh
   source time. Strictly increasing row times remain mandatory. Source clock
   changes and duplicated timestamps fail capture rather than adding epsilon.

8. **A one-second drain wait is not cancellable file I/O.** This writer calls
   synchronous `WriteFile` and `FlushFileBuffers`. A stalled worker cannot be
   safely destroyed or have its handle closed from another thread to enforce
   the deadline. Report unresolved/incomplete, retain ownership until it returns,
   and never publish the outer completion seal after a timeout, even if a late
   worker writes its own complete footer. Fake slow-writer checks must cover this
   late completion race. Do not claim a bounded OS write or forcefully kill a
   thread using the writer. This fixture does not implement the worker/drain.

## Wheelkit lifecycle audit

The current `dbce-wheelkit/docs/RECORDING-PLAYBACK.md` and four implemented flows
already provide the useful UI vocabulary: normal Play, Play with recording and
Play recording, independent supervised runs, muted output, game-specific start
context, process ownership, restoration and a sealed recording library. Reuse
those concepts and the existing format. Do not add an ad hoc `record.on` surface.

The existing flows require a Steam game copy and a stage/Art supervisor. OutRun
decomp is a separate non-Steam product, and the shim has no runtime session runner,
start-state recipe or route replay. It needs an explicit capability and a reviewed
product-specific lifecycle bridge; enabling the existing Play recording option
on the strength of this codec fixture would be incorrect. Signals-only captures
remain distinct from playable route recordings. The native writer remains a
worker-owned synchronous sink; admission, copied bounded ingress, sticky failure,
neutral stop and bounded drain still need implementation and tests.

Observed result: October 10, 2026, private evidence
`dbce-project-mgmt/artifacts/shim-recording-audit-20261010-03`. The first draft's
attempt to send an over-15-digit batch was correctly refused; final fixture uses
the parser's true limit and keeps the >2^53 test explicitly synthetic.
