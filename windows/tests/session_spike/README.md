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
is loaded. Results never overwrite prior evidence. The decomp tree and relevant
toolkit subtrees must be committed and clean, and are rechecked after the run.
`source-pins.before.json` hashes every tracked decomp file and the relevant toolkit
writer/reader/build files; `audit-receipt.json` pins that manifest, commits, binaries
and evidence. All native and managed build outputs (including the toolkit's)
are under `<Result>/build`; generated tapes are in `<Result>/capture`.

## What passes

Twelve copied frames from two fake devices go through the existing native writer
to `dbce.wheel.session` v1, the actual managed reader, and a lossless numeric
export back to C++. There are 360 channels per two-device frame, preserving all
128 button bytes, all axes/POVs, physical and delivered values, source indices,
epochs, call outcomes, validity, timestamps, injection marks, action statuses and
edges. A failed shifter read and a held shift after reopening are included.

- Every decoded channel equals the original frame.
- Raw replay through a fresh real core reproduces all twelve frames, including
  rearm, epochs and edges. These twelve frames have **physical input only**, no
  injected values. The fake provider and its deterministic clock are replayed;
  no hardware is read. Exact clock equality is fixture evidence only, not a live
  acceptance rule. Real runs compare semantic outputs separately from original
  source clocks and newly observed replay clocks; do not copy timestamps to pass.
- Two intentionally wrong profiles (inverted steering and a different shifter
  button) produce mismatches, proving this raw replay exercises the actual binding
  resolver instead of simply returning the saved actions. These are deliberate
  fixture mutations; live replay must refuse an unexpected profile mismatch.
- Action replay through the real OutRun adapter compares every `NativeInputState`
  field and every adapter signal under the same **fixed fake game state**, with
  neutral native-pad input. Every bound action is stimulated, including navLeft;
  its menu edge occurs exactly on frame 9. This remains a fixed race-context
  fixture, not a menu-navigation or scene-transition qualification.
- A separate actual core injection frame preserves both devices' object marks and
  batch `999999999999999`, the parser's current 15-digit maximum.
- Eleven C++ corruption controls cover invalid status, missing raw channel,
  source index, negative/fractional ID limbs, extra channels, unknown edge bits
  and sample times outside the frame. Five managed limb controls cover fractions,
  sign, range and non-finite values.
- Existing v1 counted-drop and gap-marker variants are accepted by the production
  reader, while lossless qualification refuses them; invalid counters/custom
  stop reasons are rejected. No writer/reader API has been changed.
- Abandoned and oversized files are rejected by the production managed reader.

This does not establish route playback, mid-session checkpoints, game physics,
the real DirectInput provider, worker shutdown, queue behaviour, or P20 as a whole.
It does not exercise every lifecycle transition. No second game has proven reuse.
The fixture's caps, identity mapping, profile and fixed `GameView` are known test
inputs; this is not yet a self-contained portable replay bundle.

## Design findings before the production recorder

1. **Upstream loss is uncounted by this native API.** `drop-then-stop.jsonl` deliberately
   omits one producer frame, then calls `stop()`. Both writer and generic reader
   report completed, zero drops, because their own row sequence is contiguous.
   The core sequence exposes the gap. In v1 **completed means normally closed,
   not lossless**. The format, managed recorder and reader already support counted
   drops with a completed footer and gap markers. The native pin hard-codes those
   counts to zero and has no marker method. Target a compatible native API that
   accepts caller-reported rejection counts and marker records, tested against
   today's reader; no format change is needed. Lossless qualification must refuse
   nonzero drops and verify final producer counts and first/last sequences (not
   merely internal gaps). Section 10's promise that every drop makes the footer
   incomplete must be corrected. An interim `abandon()` on ingress/event loss is
   safe but leaves no footer and keeps the reason/count only in outer run evidence.
   A flush timeout has no adopted completion; an invented `upstreamDrop` footer
   reason is rejected by today's reader. Do not disguise counted loss as a
   successful lossless replay, or normal stream closure as proof of completeness.

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
   JSON size also needs a declared maximum before recording starts. The measured
   two-device rows are about 10 KiB: at a hypothetical 60 Hz, 64 MiB lasts only
   about 110 seconds, not 600. Eight-device rows approach 29 KiB / about 39 seconds.
   `budget.json` measures this fixture and refuses a 600-second estimate under the
   default; a larger explicitly selected file limit can fit (writer maximum 1 GiB).
   Production preflight must use a defensible worst-case serialized row size,
   declared tick-rate bound, metadata/footer reserve and requested duration, not
   this fixture's observed maximum. Refuse or disclose a shorter capture before
   starting, and retain the runtime file-size limit. Never silently trim data.

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
   depends on the observed car/gear and adapter state. Ordinary play can also
   merge an unrelated pad with the shim; its pre-consumer input/edges and adapter
   version must be captured to reproduce that merge. Do not claim it reproduces
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
The follow-up `shim-recording-audit-20261010-04` adds the two wrong-profile
controls: inverted steering differs on 12 frames, the wrong shift button on two.
Three steering mismatches are the one-LSB centre asymmetry; the other nine are
deflected input. The corrected fixture asserts the exact frames: steering 0-11,
shift 2-3. Nene independently reproduced clean `43782f8`, matching the original
-04 numeric export byte-for-byte. Earlier -03/-04 were run before committing:
their `sourceCommit` is only the base, and their partial source-file hashes are
not complete provenance. Keep them as historical exploration evidence, not clean
source receipts. Use a new run of the corrected committed harness for review.
