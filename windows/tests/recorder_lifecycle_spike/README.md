# Recorder worker lifecycle proof (P20, offline fixture)

This isolated spike follows the recording contract audit at `beba6b1`. It is not
a live recorder, a game hook, a native toolkit change, or a portable replay
bundle. No game launch, device access or force output is involved.

The proof targets a single producer and a single worker. A fixed-size numeric
packet stands in for a copied input snapshot. The worker owns the synchronous
sink and is the only caller of its open/write/stop/abandon/destructor paths.
Production integration still needs the actual Frame/event/adapter snapshots,
complete profile and epoch bundle, bounded serialization preflight, and P21's
Core shutdown coordination.

Run from a clean committed checkout, using a new result directory:

```powershell
py -3 -I -B windows/tests/recorder_lifecycle_spike/run.py `
  E:/Source/dbce-project-mgmt/artifacts/recorder-lifecycle-new `
  --toolkit E:/Source/toolkits/dbce-wheel-mod-toolkit --mutations
```

`worker.hpp` is prototype code inside the fixture, not a runtime dependency.
`test.cpp` exercises fake sinks with deterministic barriers. `native.cpp` runs
the existing synchronous native writer on the worker, and the managed fixture
uses the production SessionReader. All builds/evidence go below the result;
tracked source and toolkit input hashes must remain unchanged. The reviewed
native writer hash is enforced. Failed mutations are separate executables built
from private copies, never edits to the reviewed checkout.

## Implemented lifecycle

- Preallocate the ring before producer access. Count `capacity * sizeof(Packet)`
  against its payload memory budget, including the unused fixed-size fields.
  The worker retains its occupied ring slot until a synchronous write returns;
  it never copies that slot to an unaccounted second queue. Thread stacks, the
  writer's bounded encoding buffer and metadata are separate budgets.
- `push` has one producer, bound to its thread on first use. Foreign or concurrent
  calls are refused and sticky. A try-only entry flag protects token bookkeeping;
  producer tokens must not be destroyed or moved while anyone is using them.
  It performs bounded validation and copies into an
  available slot; it never waits for the worker, allocates memory, or writes a
  file. Slots use SPSC acquire/release publication. Full capacity, invalid
  sequence/time/value or worker failure is sticky and counted. Subsequent
  producer calls are refused and counted, not silently omitted from evidence.
- Source sequence begins at the declared first sequence. Source times must be
  nonnegative and strictly increasing. They are copied, never replaced by the
  worker's time. The fixture's packet codec is not a new production wire format.
- Only the producer calls `finish(last_source_sequence)`, after its last push.
  The declared final source sequence must equal the accepted and written tail;
  silently skipping trailing frames is refused. The caller must take this value
  from the source, not reconstruct it from recorder acceptance. That ordered end signal
  means no frame can arrive after the worker sees the ring drained. A supervisor
  may cancel or time out but cannot invent an end-of-producer signal while a
  callback might still enqueue. Missing producer quiescence prevents completion.
- The worker calls stop only after the ordered producer end and a clean drain.
  Any ingress loss or failure abandons the current native v1 writer, leaving no
  completion footer. Counts/reason remain in the outer report. A future reviewed
  native counted-loss API may preserve a normally closed but lossy stream; that
  does not make it a lossless replay.
- A controller drain wait has a deadline. Timeout permanently withholds outer
  completion. If a worker was already in a synchronous stop/flush, it may return
  success and leave a complete inner file later; it cannot change TimedOut to
  Complete. Readers must require the outer completion seal. Do not cancel the
  OS I/O, close its handle from another thread, or kill the worker.
- Worker state and the sink outlive a timed-out facade. The detached worker owns
  its shared state until it finishes. This is admissible only where the code
  module also remains loaded (this prototype's executable). It is not an unload
  strategy for a game DLL. Producer tokens hold their own State reference and
  can only report refusal after cancellation; no reference back to a destroyed
  facade is needed. All sink-owned data must also have independent lifetime.
  **Normal process/CRT teardown with an unresolved worker is not qualified.**
  Even a self-contained sink can use CRT locale/allocator state. P21 must keep
  the host and CRT alive until the worker exits, or isolate the writer in its own
  process. These tests unblock and await cleanup before process exit. A process
  killed before cleanup leaves an unsealed, incomplete recording.
- The first terminal failure wins. A successful terminal transition occurs only
  after sink stop and destruction have completed; the final accepted/written
  counts then match. A timeout or cancellation already published cannot be
  overwritten by the worker. Reports during unresolved shutdown are snapshots,
  not final counts.

`attempted = accepted + queue_full + invalid + after_failure + after_end +
wrong_producer` after calls quiesce. Once producer and worker are both done,
`accepted = written + discarded`. Reports retain the first rejected source
sequence/time/reason, declared tail, last written tail, and the sink's own reason.
Counters from a still-running worker are observations, not a final receipt.
Post-end refusals remain diagnostic and cannot extend an already completed tape.
Sink open/write/stop/reason exceptions are contained; abandon and destructors
are required to be noexcept. A native duration/file/row limit can already leave
an incomplete inner footer; its reason is preserved rather than relabelled as
an upstream drop.

## Required evidence

1. Normal drain preserves source sequences/times and exact values; no sink call
   occurs on the producer thread. Mutating the caller's data after push cannot
   change a queued packet.
2. A barrier inside write allows the producer to fill the remaining ring without
   waiting. The next push records overflow; once resumed, the sink is abandoned,
   never stopped. Memory use remains fixed.
3. Sequence gaps, duplicate/backward times, non-finite values and a wrong width
   fail explicitly. Empty/missing end never yields a qualified recording.
4. Open, write and stop failures/throws leave no completion. Cleanup stays on the
   worker. Calls after producer finish are counted without extending the tape.
5. A barrier inside stop plus a short drain deadline produces sticky timeout.
   Destroy the facade, then resume the worker: no freed facade access, no outer
   completion even if the fake sink says its late footer is complete.
6. Negative controls remove sticky timeout, overflow failure, copied ownership,
   or producer-end ordering and must be detected by the relevant fixture.
7. A real native v1 writer success/abandon pair is read by the existing managed
   SessionReader. Existing `beba6b1` codec/enum evidence is unchanged.

The native bridge additionally exercises `durationLimit` and an actual native
stop delayed past timeout. The latter creates a complete inner file which the
production reader accepts, while the outer worker outcome stays ineligible.

**Still outside this proof:** an actual atomic, hash-bound outer seal and its
reader, seal-write failure handling, profile/caps/event/adapter bundle, maximum
serialized byte preflight and per-input/physics clock joining. `may_seal` only
reports worker eligibility; it does not write a seal or assert portable replay.
The fixture's receipts hash the evidence for review, not as a runtime recording
bundle. A production tape must end with the final neutral/run-end frame before
producer finish; this generic packet fixture does not determine game neutrality.
If the input thread is stuck, P21 cannot substitute a supervisor finish.

No launch, installation, merge into the game runtime or promotion to the shared
toolkit follows from these tests alone. The second game remains the reuse gate.
