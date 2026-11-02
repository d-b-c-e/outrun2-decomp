# Control journal: the concrete bounded sink (design, r2)

Design only, for review before any code. r1 was 393bf9c (Astra 16253); r2 answers Astra's review
16294, points 1-8. It completes the `control::Io` sink half (`append`, `finish`) for INPUT-SHIM
section 12 and the batch log (section 9) on top of the typed statuses of `a1c2895`. The read half
already exists (`windows/host/control_snapshot.hpp`).

The only change outside the sink is one Core snapshot operation (section 4). Pump, the host lifetime
and every installed path are unchanged. Nothing here authorizes a runtime test or an install.

## 1. Where: a per-process run folder, never `runtime.log` (amends N6)

In OutRun, `runtime.log` is the game's stdio redirection (`window_desktop.cpp:110-112`): `stdout`
`freopen(..., "w")` and `stderr` `"a"` on the same path, both unbuffered. `stdout` is not in append
mode and writes at its own handle position, so a third writer's lines would be overwritten. The
shim also may not use stdio (R1) or the CRT lock (D2).

The journal is `controls-journal.jsonl`, created with `CREATE_NEW` inside a folder that belongs to
this process alone. It is never `CREATE_ALWAYS` and never a fixed shared name (Astra 16294 point 2).

- **Latched:** the fenced run folder `plan.run_root` (`run-<nonce>-<pid>`, created `CreateDirectoryW`
  new by the fence).
- **Ordinary:** a new `controls-<pid>-<created>` folder under the dbce folder (`fence::dbce_folder()`),
  created new the same way. A folder or file that already exists means the journal is refused, and
  nothing is overwritten.

Parent-path verification stays a host duty, as for the fence. Retention and a "latest" pointer are
separate later UX. N6 becomes: "bounded JSON lines in a per-process `controls-journal.jsonl`, with
process, run, profile and build identity and gap counters; no other status file". The human start
line stays adapter-owned in `runtime.log`. `write_audit` gains classified entries for the sink's
`CreateDirectoryW` and `CreateFileW`.

## 2. Identity: one immutable object, built once, shared with the observer

The journal never builds its own identity (point 1). A single `RunIdentity` holds:
- the observer's `Identity` fields: `process`, `process_start` (creation FILETIME), `nonce`,
  `profile_devices`, `executable`, `build`, `profile`;
- the profile revision;
- the core and adapter version strings.

It is constructed once, off the input tick (on the control worker while admission waits), and
before both the observer header and the journal header are published. The observer and the journal
both receive the same `shared_ptr<const RunIdentity>`.

- `executable` is SHA-256 of the fenced executable file identity. It is computed once, by handle, from
  the same opened file the fence and observer refer to, never by an independent late pathname read.
- `build` is the adapter's versioned signal-schema SHA (observer_domain.hpp), a different field. The
  journal records both. The reader joins each to the observer's own field and never compares
  `executable` with `build`.
- `profile` and the revision come from the exact bytes Core parsed: the identity builder receives
  those bytes, it doesn't re-read the file.

Every line carries `pid`, `created`, `nonce` (latched only), `profileSha256` and
`build` = {`core`, `adapter`, `executableSha256`, `buildSha256`}.

October 12 copied-provider addition: bridge latched journals also carry immutable
`inputSource: "external-physical-input@1"` on every v1 row. The strict reader
requires the same supported value throughout when present in the header. Existing
direct-provider journals omit it. This identifies the physical provider used for
admission evidence; it does not label physical input as injection or constitute
a per-sample provenance capture. Bridge CAPTURE remains refused.

## 3. Lines and field limits from the input contracts (point 3)

One JSON object per line (UTF-8, `\n`). Each has `v:1`, `type`, the identity, `seq` (contiguous
from 0) and `drain` (the drain number that produced it; 0 for the header rows).

- `header` (seq 0): mode, limits, the human start line and `devices: n`.
- `device` rows (seq 1..n, one per profile device): index, instance and product GUID text (38 bytes
  each), and name. Header rows are never one oversized line; there are at most 31 devices, because
  ActionCount is 31 and each device is bound by at least one action.
- `status`: one per `StatusChange`: `us`, `of`, `index`, `status`, `reason`, `device`, `epoch`, copied
  unchanged.
- `event`: one per `Event`: `eseq`, `us`, `kind` (number and name), `device`, `epoch`, `batch`,
  `detail`. This is the batch log.
- `counters`: the cumulative values at the drain (see section 6).
- `end`: the last line (see section 5).

Field limits, derived from the producers rather than assumed:

| Field | Bound | Source |
|---|---|---|
| device name | 260 UTF-16 units means at most 780 UTF-8 bytes raw (3 per unit); at most 1560 escaped when every unit is a control character | DirectInput `tszProductName`/`tszInstanceName` (MAX_PATH) |
| GUID text | 38 | `DeviceInfo.dev`/`prod` |
| status reason | base reasons (constant strings) plus `device_reason` (GUID + name + provider reason) plus fixed prefixes, at most 2 KiB escaped | `track_status`, `device_reason` |
| event detail | GUID, `call_name`, batch hash and counts, or a `bounded_diagnostic` (512 raw, at most 3072 escaped) | `emit` call sites |
| start line | the adapter's start line: one per non-live action, at most 31 lines, each bounded the same way as a status reason. It is split into `startline` rows if it exceeds the line bound | section 12 |

The line bound is 8 KiB, about 4.5 KiB over the largest legitimate escaped field plus the identity.
A producer field above its contract bound is a sink error (the append returns false), never a
silent truncation. Tests cover each maximum (section 8).

**Chunked writes.** A full drain (1024 events plus 256 statuses plus counters) can reach several MiB.
`append` renders whole lines into a 64 KiB chunk buffer and calls `WriteFile` each time the next line
would not fit. It returns true only after every chunk of the supplied Journal was fully written.
Any short or failed write latches the sink failed. After that it never retries and never writes a
successful end.

## 4. One atomic drain (point 4)

Today Pump's `drain()` calls `drain_events`, `drain_status` and the counter getters under separate
lock acquisitions. An input tick between them can put a status for epoch 2 into this drain while its
`EpochBegin` lands in the next one. Add one Core operation:

    struct JournalCut { std::vector<Event> events; std::vector<StatusChange> status;
                        std::uint64_t status_dropped, status_coalesced, events_dropped, control_events_dropped; };
    JournalCut Core::drain_journal();   // takes m_ once; the queues and their counters form one consistent cut

Every producer already takes `m_` per record (`emit`, `queue_status`), so a cut taken under one
acquisition contains a prefix of each queue's production order. The input thread emits
`EpochBegin` when it adopts an epoch, before `track_status` can queue a status for that epoch, so a
cut never holds such a status without its event in the same or an earlier cut.

Pump's `drain()` and `finish()` use `drain_journal()`. The old getters stay for the existing
tests. The reader works at drain granularity: within one drain, line order is not causal; across
drains it is. Missing event-time fields are never repaired from current device state.

Test: an interleaved producer and drainer, a real input-thread tick racing a worker drain in a
loop. Every cut must satisfy the reader's epoch rule.

## 5. Writing, finish and ownership (points 6 and 7)

- **Opened on the worker, never in the constructor.** The constructor takes only the folder decision,
  the `RunIdentity` and the limits. The first `append` (or `finish`) on the owned control worker
  creates the folder or file and writes the header rows. So every handle operation (create, write,
  flush, close) happens on the worker, under the host's `control_ms` deadline and its unresolved
  policy for any blocked call.
- **Startup failure.** If the worker never started, no handle exists and the destructor has nothing
  to do. If the worker started and was abandoned (unresolved), the detached worker remains the last
  owner of its shared state (D9), and its own exit path closes. No destructor on another thread
  touches a handle. Nothing here promises that a synchronous `CloseHandle` can't block.
- **finish().** If no failure has latched, it writes `counters`, then `end {complete:true}`, then
  `FlushFileBuffers`, then `CloseHandle`, and is true only if all of them succeed. After a latched
  failure it still attempts cleanup (close) but writes no success marker. It returns false, and runs
  only once.
- **Durability.** An `end` line written before `FlushFileBuffers` succeeds cannot by itself prove
  durable completion. The qualification reader therefore also requires the host report's
  `controlComplete` and `resolved` plus the supervisor's observed normal exit (the P21 three-fact rule).
- **Empty drains** (point 8). A drain with no records and counters unchanged writes nothing and
  increments `emptyDrainsSkipped`. That comparison excludes `emptyDrainsSkipped` itself.
- **Caps.** 8 MiB latched, 2 MiB ordinary, with room reserved for the final `counters` and `end`
  lines. In a **latched** run, crossing the cap writes the reserved `end {complete:false, reason:"cap"}`,
  then fails, which correctly makes the run unresolved and failed.
  **Ordinary play is a policy question, not settled here.** A failed pump means
  `control_complete=false`, then unresolved, then the reviewed forced exit at shutdown. So a sink
  failure in ordinary play would force-exit the game at the end of a session. Two options for review:
  - (A) **Spike limitation:** document that an ordinary cap or write failure is unresolved, ending in
    the forced exit, and size the cap so it is unreachable in practice. The 2 MiB cap with empty-drain
    skipping is about 8000 status or event lines; we'd measure a long ordinary session to confirm.
  - (B) **Explicit counted-drop policy:** an ordinary-only `Io` mode in which the sink, after its cap,
    accepts and counts (`journalDropped`) without writing, and the reserved tail reports the count.
    This changes the Io contract ("accepted means written") for ordinary play only. It must be
    written into INPUT-SHIM section 12 and the host README, and latched runs keep complete-or-fail.
  My recommendation is A for the first slice (no contract change), with B only if measurement shows
  the cap is reachable.

## 6. Counters: explicit availability (point 8)

A `counters` line carries `asOf:"drain"` and these cumulative values from the same cut:
- `statusDropped`, `statusCoalesced`, `eventsDropped`, `controlEventsDropped`;
- `batchesOffered`, `identicalSkipped`;
- `emptyDrainsSkipped`.

`recorderOverruns` and `observerPublished` each appear as `{available:false}` until their producer
is wired, never as a fabricated 0. Once available, they are the producer's own cumulative values.

## 7. Reader (qualification side), strict

- The header and the n device rows come first, `seq` is contiguous, every line is valid UTF-8 JSON
  within 8 KiB, and identity fields on every line equal the header.
- A status `device` is -1 or a valid device row index.
- **Epochs:** a status or event with a nonzero epoch e for device d needs an `EpochBegin(d, e)` in
  the same or an earlier drain. Epoch 0 statuses (missing, opening, product-mismatch before any open)
  need none (point 5). Per device, epochs never decrease across drains.
- Counters never decrease. A counter with `available:false` is reported as unavailable, not as zero.
- **Terminal:** in a latched journal, exactly one terminal `RunChanged` **event** (completed, failed:
  or expired), the authoritative ordered record. The matching Run `StatusChange` is corroborating,
  not counted separately (point 5). A run that completed before closure needs no extra closure
  transition.
- **Completeness is not success.** `end {complete:true}` means the journal is complete. A complete
  journal whose terminal is `failed:` is a complete failed record, and is reported as such.
  Qualification as a passing test also needs the observer, the host report and the supervisor (the
  three-fact rule).
- A torn last line, a missing `end`, `complete:false`, or a latched failure all mean incomplete.

## 8. Proof plan (offline, real files in a temporary folder, no game)

Cases:
- **Round trip:** header, device rows, status, event, counters and end through the reader.
- **Creation and refusal:** `CREATE_NEW` refuses an existing file and an existing folder, in both
  modes. Creation happens on the worker; the constructor touches nothing.
- **Escaping:** quote, backslash, controls, non-BMP and invalid UTF-8 (refused). Each field at its
  maximum legitimate escaped size (a 260-unit name, the largest reason and detail) stays within
  8 KiB. A field above its bound is refused.
- **Full queues:** a Journal with 1024 events plus 256 statuses at maximum field sizes, written in
  chunks, read back complete.
- **Short-write seam:** a write fault in the middle of a chunk latches failure. There is no success
  end, the reader refuses, and finish still closes and returns false.
- **Caps:** latched, the reserved `end cap`; ordinary per the chosen policy.
- **Empty drains:** skipped, the count reported on the next written line, and the skip counter
  excluded from the change test.
- **finish:** runs once; appends after finish are refused.
- **Concurrent reader:** a `FILE_SHARE_READ|WRITE` tail sees whole lines only.
- **Identity drift:** one line that differs is refused.
- **Atomic drain race:** an interleaved producer and drainer, with the epoch rule satisfied in every
  cut (section 4).
- **Counter availability:** unavailable recorder or observer counters are reported as unavailable.

Mutants:
- no `end` line;
- `seq` not checked;
- a partial chunk accepted;
- identity omitted;
- `emptyDrainsSkipped` included in the change test;
- the cap ignored;
- the split drain (events, then statuses) restored, caught by the race test;
- the epoch-0 exemption removed, caught as a false rejection.

Integration case: the control-host child with this sink in the resolved and write-failed modes. In
both, the host report's `controlComplete` and the journal's `end` agree.

## 9. Out of scope

- The concrete read half.
- The observer and recorder counter producers: they're wired in, never invented.
- Wheelkit's reader (P9b), ordinary-journal retention, and any install.

## 10. Implemented slice and what is deferred (Astra 16313, 16337, 16347)

Implemented:
- `windows/host/control_journal.hpp`: `JournalFile` and `FileIo`;
- `Core::drain_journal()`, which the Pump's drain and finish use;
- `windows/tests/control_journal`: the offline cases plus the strict reader, with no mutant harness (see its README).

Ordinary play follows option A, as an explicit spike-only limitation. The 2 MiB cap is finite and can be
exhausted (the cap case proves it). A sink failure ends an ordinary process through the reviewed forced
exit. That is not player behaviour, and a production policy (B) needs its own review. Headroom is still to
be measured over a long ordinary session.

Deferred until after the first live shim result, none of them prerequisites for it:
- the shared `JournalIdentity` builder (the fenced executable hashed by handle, with the identity shared with
  the observer). Until then, any identity a caller supplies is reported as given;
- the observer publish-sequence and recorder-overrun counters, which stay `{available:false}`;
- `write_audit` scope for `windows/host`, needed when the host is linked into OutRunDesktop.exe;
- the control-host child integration with the file sink;
- the P31 qualification joins (observer, host report, supervisor);
- ordinary-journal retention, a latest pointer, Wheelkit's reader (P9b), and per-device read timings;
- the journal mutant suites.
