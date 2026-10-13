# Input shim: architecture

**Draft r3.1, 2026-10-10. Author zero (Claude). Reviewer Astra (romi). Accepted for the stage-1 source slice (Astra, r3 review); not adopted as a standard.**

r3 closes Astra's r2 review (`dbce-project-mgmt/inbox/2026-10-10-romi-input-shim-r2-review.md`): eight findings,
cited as R2-F1 to R2-F8, and the decisions on N1-N8. The r1 findings are cited as R1-F1 to R1-F10. Section 18 logs
the rounds. Section 17 is the proof matrix, staged by implementation slice. Each slice is reviewed before the next
one starts (R2-F8).

This is a design for a shared input path: one portfolio-owned profile per game, shared code that owns the devices and
resolves the bindings, and a game adapter that hands standard actions to the game. OutRun 2006 (decomp) is the
reference adapter. The design is decomp-local and opt-in. It moves to the toolkit, versioned and pinned by hash, only
after review and a second existing game, which is #DRIVE Rally after its current qualification (Astra 8815, R1 Q5).
No standard changes until then. Native-store writers and the existing companion remain valid backends; the shim is
an additional backend, not a replacement.

Owner direction, 2026-10-10: "design a shim for each game where we can have standard input, mapping, and
configuration that mostly sits outside the game's native methods (e.g. leaving its config files mostly alone)", and
"do heavy iterative architecture reviews with Astra on this please - since it could be the foundation for literally
all games, it's very important to get the details right".

## 1. Why

Today each game gets a Wheelkit storage adapter that writes the game's own binding store (STD-033 item 2). Most of
the cost is in STD-035's sixteen writer rules: foreign formats, product-keyed tables, cloud saves, colliding kept
bindings, the consumer's required set, bounded parsers for binary stores. Every new game pays it again.

The shim keeps the binding in one portfolio-owned file per game. Shared code owns the devices and resolves the
binding. The adapter writes actions, not bindings, into the game, and the game's saved mappings are never written.

## 2. Goals and non-goals

Goals:
- One profile format and one writer transaction (section 5), used by both Wheelkit and the in-game F6 panel, against
  one authoritative file per game.
- One input provider per game process. It owns every device handle; other mods in the process consume its frames
  and never open a second reader (R1-F6).
- Every binding read from its own device (STD-036). Qualification still needs multi-device evidence (R1-F8).
- Test runs, provenance, observation and recording implemented once, in the core.

Non-goals:
- Replacing a game's physics, menus or input semantics. The adapter feeds the highest input level that keeps the
  game's own processing.
- OS-level input, virtual-device drivers or focus changes.
- A stable binary ABI before the second adapter. A minimal experimental bridge for the managed proof may be needed,
  but never a second resolver implementation by default (R1 Q10).
- Deterministic physics replay.
- Online and anti-cheat games: the core runs in-process (STD-018).
- A line-count target for adapters. State, fencing and diagnostics are never cut to stay small (R1 closing note).

**Failure policy has two separate halves** (R2-F2):

- **Ordinary process** (no test request). If the core fails to start, for example because DirectInput is unavailable
  or the profile is absent or unreadable, the game runs on its own native input and never crashes. The adapter treats
  an absent core as "nothing available" and reports it.
- **Test-requested process** (latched, section 9). It never falls back to native driving, the owner's saves or the
  network:
  - if the fences cannot be verified, the process refuses to start the game;
  - if the core or the profile fails after the fences are verified, the process enters a verified **inert** state.

  Both cases are defined in section 9.

## 3. Terms

| Term | Meaning |
|---|---|
| Profile | The `[Controls]` section, `ini-controls-1` Schema 1, unchanged from `controls-contract.md`. |
| Provider | The single owner of every input device handle in the process (section 6). |
| Lease | The provider's grant of one device to one use: read, or force (exclusive). |
| Epoch | One open of one device identity. It ends at a failed read, a disconnect or a close. Numbers increase per identity and are never reused. |
| Sample | One read attempt of one epoch: valid or failed, with its timestamps and its injection marks. |
| Owned action | An applied action whose binding the provider resolves (section 7). |
| Frame | One tick's resolution: the samples used, every action's value, status and source sample, and edges. |
| Run | One test session, from the latch to completion, expiry or failure (section 9). |
| Batch | One accepted version of the command file, identified by session, batch number and content hash. |
| Adapter | The game-specific code: capability, interception point, frame sink, state signals, fences. |

## 4. Layers and threads

```
dbce-controls.ini ─► profile reader ─┐                       game input thread (tick)
                                     ▼                       ─────────────────────────
provider worker ──► epoch table ──► read samples ─► test channel ─► resolver ─► frame ─► adapter sink ─► game
 (enumerate, open,   (handoff by     (Poll + Get-   (deliver batch    │            │        (interception
  rescan, close)      generation)     DeviceState)   into samples)    │            │         point, R1-F3)
                                                                      ▼            ▼
                                                     observer (shared memory) ◄── adapter state signals
                                                     recorder queue ──► recorder worker ──► session_writer
inject.on / inject.txt ─► control worker ─► command generation (to the tick)    status queue ─► control worker ─► runtime.log
```

| Thread | Does | Never does |
|---|---|---|
| Game input thread | `tick()`, which: adopts the latest epoch table and command generation; reads every owned epoch; validates the whole frame (section 9); delivers injection; resolves; runs the adapter sink; publishes the observer (it is the single writer of the observer and its event ring); queues status changes and one recorder snapshot. | Enumerate, open, close, rescan, or read, parse or write a file. |
| Provider worker | Initial enumeration and opens; hot-plug rescans (bounded per pass); bounded reacquisition; closes after the input thread lets go. | Read samples for frames. |
| Control worker | Polls and reads the session and command files, parses and validates batches against an immutable snapshot of the profile and device capabilities, and hands the input thread an immutable **command generation**. Drains the status queue into `runtime.log` JSON lines. | Touch device objects or the observer. |
| Recorder worker | Owns `session_writer`, drains the recorder queue. | Run in any game callback. |

**Rules:**
- No file I/O on the tick (R2-F6).
- **Times are recorded separately:** the control worker stamps acceptance, the input thread stamps activation (when it
  adopts a generation), and each sample stamps its own delivery.
- **Handoffs between threads** use C++-memory-model-correct primitives:
  - an atomic pointer to immutable generations, retired only after the input thread acknowledges;
  - bounded queues with mutexes or atomics;
  - never a counter around unsynchronized mutable objects.
- **Status changes** are coalesced (the latest per action or device within 100 ms) into a bounded queue. Overflow is
  counted and reported even when recording is off.
- **Reads can block** in some drivers, so the design makes no "adds no latency" claim (R1-F5). Every sample records
  how long its read took and its age at resolution, and the status counts reads over 2 ms.
- **Epochs** reach the input thread as an immutable table generation. The worker requests a close, the input thread
  acknowledges at its next tick, and only then is the device released. No COM device object is ever used by two
  threads at once.
- **Still to confirm live:** whether DirectInput accepts a device opened on the worker and read on the input thread
  (section 17, slice 4).

## 5. Profile file, writer transaction, import, discovery

**File.** `dbce-controls.ini` beside the game executable. It holds `[Controls]` exactly as `controls-contract.md`
section 5 defines, Schema 1, plus any other sections and keys, which every writer preserves (R1-F6).

**Reader.** Read once at start in one bounded read (64 KiB), strict UTF-8 with an optional BOM.
- A second `[Controls]` or a duplicate action key refuses the whole profile and names both lines.
- An invalid binding line is reported and skipped, per the contract.
- A missing file binds nothing. The game runs on native input and the status says why.
- The shim reads at start only. Edits apply at the next launch (R1 Q7). A future live reload must swap the whole
  profile atomically and then follow the rearm rules (section 6).

**Writer transaction.** Wheelkit (C#) and F6 (C++) run the same transaction, with shared fixture vectors (R1-F6, Q2).
Cooperating writers are serialized; an editor that does not cooperate is detected and never silently overwritten
(R2-F1).

1. **Canonical target.**
   - Open the target's folder and refuse a reparse point on the file or any folder above it (STD-035 item 12).
   - Take `GetFinalPathNameByHandleW` with `VOLUME_NAME_GUID`, so drive letters, `subst` and 8.3 names alias to one
     name, and fold its case, because NTFS here is case-insensitive.
2. **Writer lock.**
   - A named mutex, `Local\DbceControlsWriter-<SHA-256 of the canonical path>`, held from step 3 to step 10.
   - The wait is bounded to 2 s; a timeout returns `busy` and writes nothing.
   - `WAIT_ABANDONED` means a writer died while holding the lock. It counts as acquired, and recovery runs first: it
     reports the target hash and leftover files, deletes orphan temporary files and keeps every backup.
   - An editor that does not use the lock (Notepad, say) is not serialized. Steps 7 and 9 detect it.
3. **Base.** Read the target's bytes, or note that it is absent, and hash them (SHA-256). The caller passes the base it
   showed the user. If the two differ, the result is `conflict` and nothing is written.
4. **Format.** Refuse when the file declares a format version this writer cannot preserve.
5. **New bytes.** Replace only `[Controls]`. Every other section, unknown keys inside `[Controls]`, comments, the BOM
   and line endings stay byte for byte.
6. **Temporary sibling.**
   - Created with `CREATE_NEW` under a unique name, written, `FlushFileBuffers`, closed.
   - Read back and compared with the intended bytes.
7. **Recheck.** Re-read the target. Its hash must still be the base; otherwise `conflict`, the temporary file is
   deleted and the target is untouched.
8. **Commit.**
   - **Target exists:** `ReplaceFileW(target, temp, backup, 0)`. `REPLACEFILE_WRITE_THROUGH` is not supported (R3
     review). Durability comes from flushing the temporary file in step 6, and nothing more is claimed. The backup is
     named `<file>.bak-<UTC>-<base hash prefix>-<nonce>` and holds the displaced bytes. Failures follow the documented
     codes:
     - `ERROR_UNABLE_TO_REMOVE_REPLACED` or `ERROR_UNABLE_TO_MOVE_REPLACEMENT`: the target is unchanged. Delete the
       temporary file and report the failure.
     - `ERROR_UNABLE_TO_MOVE_REPLACEMENT_2`: the original now sits at the backup name. Move it back without replacing
       anything. If that fails, report where the settings are and touch nothing more.
   - **Target absent:** `MoveFileExW(temp, target, MOVEFILE_WRITE_THROUGH)` without `REPLACE_EXISTING`. If the file now
     exists, something else created it in the meantime, so the result is `conflict` and the temporary file is deleted.
9. **Displacement check.** The backup's hash must equal the base. If it does not, an editor that ignores the lock wrote
   between step 7 and step 8, and the result is `conflict`.
   - There is **no automatic rollback** (R3 review): a rollback would repeat the same check-then-replace race.
   - Every file is kept: the target (now this writer's bytes), the backup (the other editor's bytes) and their hashes.
   - Recovery is explicit, chosen by a person.
   - The design does not claim that an editor ignoring the lock can never be overwritten for a moment. It claims
     only that this is detected, reported, and that nothing is lost.
10. **Verify** that the target bytes equal the new bytes.
11. **Backups and temporary files are kept** unless their ownership and current hash are proven. An abandoned mutex
    does not authorize deleting files that merely match a name pattern. Automatic pruning is deferred. Then the lock
    is released.
12. **Result:** one of `applied`, `conflict`, `busy` or `failed`, with the new hash and the names of the backups.

The fixture vectors include:
- interleaved writers driven by test barriers at every step;
- an editor that ignores the lock writing between steps 7 and 8;
- an abandoned lock;
- a timeout;
- alias paths: case, an 8.3 name, a refused junction;
- a race to create a missing file;
- a forced replacement failure;
- preserved unknown sections;
- a CRLF file with a BOM.

`Revision` stays Wheelkit's profile content hash (contract section 5). The status reports the file hash too, so a
reader can tell which bytes were applied.

**Legacy import** (Tweaks `[DirectInput]` to `dbce-controls.ini`) is **explicit** (R1 Q1):
- It is started from Wheelkit, or from a standalone opt-in importer that runs the same vectors.
- It shows a preview first: the proposed bytes, every note, and every Tweaks key that is not carried over.
- It is all-or-nothing and no-clobber: it creates the file only when none exists (step 8, target absent).
- It never modifies the Tweaks or OutRun files.
- A missing `dbce-controls.ini` is not consent to import.

The mapping already written in `controls_store.cpp` (button to action, axis scaling, POV numbers, an explicit shifter
device taking precedence) becomes those vectors.

**Discovery.** `<exe stem>.product.json` beside the executable (`OutRunDesktop.product.json`, copied from
`windows/product.json`). It declares:
- `productId`;
- `controlsStore: "shim-ini-controls-1"` and the file name;
- the core and adapter ids and versions;
- the status format version;
- the test folder and the observer name and version.

Wheelkit needs this marker plus the executable identity before it offers to write.

## 6. Provider: ownership, identity, epochs, rearm

**One owner** (R1-F4). The provider holds every device handle in the process. It grants leases:
- **Read lease.** A non-exclusive, background handle. This is the only lease OutRun needs: it has no force.
- **Force lease** (reserved). An exclusive handle for the force-feedback wheel. While it exists, that wheel is read
  through the same handle, because a second open of a force device is never made.
- **Bridge provider**, an alternative implementation for consumers that already own the wheel through WheelFfb.
  WheelFfb stays the owner, and the provider reads through it by instance identity. A consumer that adopts the shim
  never opens a second exclusive path.
  - Built for OutRun (2026-10-12, `shim/wheelffb_bridge`, toolkit native 0.11.0 `GetReadSlotCaps`). The force wheel
    has one owner at a time. Before the force output opens, and after it releases, the DirectInput provider reads it
    on its own non-exclusive handle, like any device. While WheelFfb holds it as its force device, the provider reads
    it through WheelFfb's read slot on the exclusive handle. The identity, objects and ranges come from that handle
    (`GetReadSlotCaps`), and Core checks them like any open.
  - The hand-over runs on the game thread inside the delivery's open and release (`ffbdev::Owner`) and never waits
    for the worker or a driver (romi 32201/32222).
    - The open first retires the provider's wheel handle (its reads are Gone, so the epoch ends). The worker closes
      it when Core hands it back. Until then, and while a provider open of the wheel is in flight, the owner answers
      "not yet": the delivery keeps waiting and asks again with each race sample, and leaving the race cancels it.
    - Wheel opens are refused while the library opens the wheel. Core then reopens it through the read slot as a new
      epoch.
  - The read slot is prepared by the worker once per shared phase, outside the bridge's lock. Later reopens reuse it
    with no library call.
  - Release and shutdown change the phase and invalidate the shared handle at once. A worker still preparing the
    slot closes it when it returns.
  - The bridge's lock is never held across a driver or library call.
  - `library_quiet()` counts workers in before they look at the phase, and the force output unloads WheelFfb only
    when it is quiet, never under a call.
  - The library gate (romi 32320): WheelFfb serializes its exports, so the worker holds the gate while it prepares
    the slot and the delivery only tries it.
    - A sample that cannot enter makes no library call. The library's hold watchdog, which does not wait, silences
      the wheel meanwhile.
    - A release that cannot enter is retained (no PanicStop or FreeDirectInput, the library kept loaded, F6 says
      "stopping") and completes at the next shutdown or the exit, which does not wait either.
  - Known limit: the delivery's own synchronous driver calls (the open's InitDirectInput, a command's
    SetDeviceForcesXY) run on the game thread, as without the bridge.

Proven reader behavior is reused behind this boundary (R1 Q4). Copying WheelFfb's reader code would not share its
handle, so the design never claims that it does.

**Shutdown order.** The game is made safe first; disk comes last (R2-F5).

1. **Stop consumption.** The adapter delivers one final neutral frame to the game, with every owned action at rest and
   held digital actions released. Then it stops ticking. If the game's input thread has already stopped, no frame can
   be delivered, and the report says so.
2. **Stop physical effects.** Force leases stop and unload their effects gracefully. A force process is never
   hard-killed, which would strand an effect on the wheel. OutRun has no force lease, so the step is recorded as `none`.
3. **End the test run,** recording its final state.
4. **Drain the recorder,** bounded to 1 s. Past that the capture is marked incomplete.
5. **Release the read leases,** but only once the input thread has acknowledged that it is out of `tick()`.
   - Shutdown waits a bounded 200 ms for a tick that is still running. It never releases a handle that may still be in
     use.
   - If the acknowledgement never comes (the input thread is stuck or gone in an unknown state), the leases and their
     COM objects are deliberately leaked, process exit reclaims them, and shutdown reports `unresolved: input thread`.
6. **Stop the workers.** Each is told to stop and joined with a bounded wait. A worker that does not stop is never
   waited on forever, and never left pointing at freed state (R3 review):
   - everything it can touch (queues, generations, log handles, adapter callbacks) is owned through a shared
     reference the worker itself holds;
   - module unload is refused until the worker is quiet;
   - the report says `unresolved: worker <name>; process exit required`.
7. **Release DirectInput** if every lease was released. Then report the shutdown result: `complete`, or `unresolved`
   naming the steps.

Lease objects stay alive until their ownership has actually been released.

**Identity** (STD-035 item 6). The instance GUID must match and the product GUID must equal the binding's `prod`. Both
are checked in the listing and again after the open (`GetDeviceInfo`). A product mismatch is a status, never a
fallback to another instance.

**Objects.** After `SetDataFormat(c_dfDIJoystick2)`, every bound object is checked by its DIJOYSTATE2 offset:
- existence with `GetObjectInfo`;
- for axes, the range with `GetProperty(DIPROP_RANGE)` after the 0..65535 request (R1 finding on 35a8aa3).

An unreadable or empty range makes the axis unqualified. Object counts are never turned into contiguous indices.
Samples are rescaled from the range read back, and the injection range function returns the same values. This is
configuration evidence, not proof of acquisition, a successful read or an observed action (R1-F8).

**A read is two calls, classified separately** (N2, R3 review). `Poll` asks the device to update; it does not fill a
state buffer. `GetDeviceState` fills it.
- The buffer is initialized before the state read: axes 0, buttons up, POVs centred (`0xFFFFFFFF`).
- A sample is admitted only when the complete state read succeeds.
- Each call's own result is classified where it is made, and the sample records both results.

| Result (of `Poll`, or of `GetDeviceState`) | Meaning | Action |
|---|---|---|
| any `SUCCEEDED` code (`DI_OK`, `S_FALSE`, `DI_NOEFFECT`, ...) | that call succeeded | after `Poll`: go on to the state read; after `GetDeviceState`: admit the sample |
| `E_PENDING` (data not yet available) | transient | no sample. The epoch stays open and the device is read again next tick. Its actions are neutral (`read-pending`) and rearm, so a held button never returns as a new press. Continuous pending beyond `pending_limit_us` (1 s) ends the epoch by reacquisition. The run is tracked by an explicit flag, not by a zero timestamp, since 0 is a valid clock reading. A test run fails on it (strict freshness). Slice 3 review, finding 4. |
| `DIERR_INPUTLOST`, `DIERR_NOTACQUIRED` | acquisition lost | no sample; end the usable epoch; neutral this frame; bounded reacquisition of the same verified handle (at most 3 tries over 100 ms); success opens a **new epoch** with rearm; otherwise close and rescan |
| `DIERR_UNPLUGGED` and anything else | device gone or unknown | no sample; end the epoch; neutral; close; rescan by identity |

**Provider admission and lifetime** (slice 3 review, findings 1 to 3).
- **Admission.** One 64-bit word per slot holds the generation and `CLOSED`, `RESERVED` and reader bits. A read is admitted by one compare-exchange that checks the handle's generation and the open state and adds a reader, so a stale handle is never admitted to a reused slot. A slot is published only after its device is stored. A close is one compare-exchange from open, no readers, to closing.
- **Stop.** `shutdown()` stops admission at once: reads, reacquisitions, opens and listings are refused afterwards, and an admission racing the stop backs out. Idle devices are released. DirectInput is kept while an open or a listing is in flight. An open publishes its slot and then looks for the stop. The publication, the stop flag and shutdown's read of the slot are all sequentially consistent, so an open in flight either fails and releases its own device, or published first and is closed and counted by shutdown. It never returns a handle that shutdown missed (slice 3.1 review).
- **Handles.** A handle is the slot's generation shifted left by 8, OR'd with the slot number. A slot out of range, or any bit above the 32-bit generation, is misuse, and is never truncated into a valid handle.
- **Lifetime.** All mutable state is shared. Every call holds a reference for its whole duration, so destroying the provider never frees what a read still inside a driver call will touch. What shutdown could not release is released when the last call leaves. The owner must not destroy the provider while one of its threads may still start a call; that is the core's shutdown gate, still open.

**Epochs** (STD-035 item 7, R1-F5):
- Ending an epoch neutralizes its owned actions in the same frame, with one up edge for each held digital action.
- A test run fails on any epoch end (section 9) and never resumes silently.
- A reopen is only ever of the exact identity, as a new epoch.
- Rescans run on device-arrival notifications, plus every 2 s while a bound device is missing. Each pass is bounded
  to one enumeration and at most four opens.

**Becoming live** (R2-F7). An action is `live` only after a successful, fresh read in the current epoch: within
100 ms, and not merely after verifying its objects and opening its epoch. Then its rearm rule must be met. A single
invalid read can never rearm anything.

**Rearm rules at every epoch start** (the first open and every reopen; R1-F5, N1, R2-F7). Each action's rule is chosen
deliberately and reported with its threshold:

| Action | Rule | Why |
|---|---|---|
| digital (`shiftUp`, `gear1`..`8`, menus, `camera`, ...) | masked until read released once | a reconnect never manufactures a press, a shift or a gate |
| `steer` | live on the first fresh valid read | absolute position; holding it at centre would be wrong |
| `throttle` | neutral until at or below 0.05 (normalized) once | a held throttle must never resume driving |
| `handbrake` | neutral until at or below 0.05 once | a sudden rear lock at speed is the hazard |
| `brake` | live on the first fresh valid read | absolute; a held brake is the safe direction and must not be suppressed |
| `clutch` | live on the first fresh valid read | absolute; a held clutch disengages drive (the safe direction) |

Each action reports `rearming` until it is ready, with the condition it is waiting for.

**Which source the rules read** (R3 review):
- Rearm conditions and edge history read only **physical** values, and only in an **ordinary** process.
- In a latched process, physical values are observed but feed no rearm or edge state. A physically held button can
  never rearm or release an injected action.
- Injected values are delivered without rearm masking. The test run starts from WAITING (neutral), so there is no held
  state to mask.
- Edges in a latched process are computed against the previously delivered action value, where `test-neutral` counts
  as released or at rest.
- Counterexamples: physically held with the injection neutral, and the converse, each across startup and across an
  epoch loss. An epoch loss fails the run, and the injected hold is released with one up edge.

**Hot-plug policy:** exact identity, bounded rescans, a new epoch, neutral rearm, no silent test continuation (R1 Q8).

## 7. Samples, resolver, statuses, frame provenance

```cpp
struct InjectionMark { std::uint64_t batch; std::uint8_t hash[8]; std::uint8_t axes; std::bitset<128> buttons; std::uint8_t povs; };
struct DeviceSample {
    std::uint32_t device;            // the provider's identity index, stable for the process
    std::uint32_t epoch;             // per identity, increasing, never reused
    std::uint64_t seq;               // per identity, every read attempt
    std::int64_t  readStartUs, readEndUs;   // QueryPerformanceCounter microseconds
    bool valid;                      // the read succeeded
    RawState state;                  // axes in the read-back range, buttons, POVs (valid samples only)
    InjectionMark injected;          // objects that actually carried a delivered value in THIS read; empty otherwise
};
struct ActionValue {
    float value;                     // steer -1..1, pedal 0..1, digital 0 / 1; rest when not live
    ActionStatus status;             // live, rearming, test-neutral, unavailable(reason), unsupported, invalid, unbound
    std::uint16_t sample;            // index of its source sample in this frame, or none
    bool injected;                   // its bound object carried a delivered value in that sample
};
struct Frame {
    std::uint64_t seq;
    std::int64_t  tickUs;            // when tick() began
    std::uint64_t runEvents;         // the event-ring sequence at this frame (section 9)
    SampleTable   samples;           // fixed capacity: one per bound device read this tick
    ActionValue   action[ActionCount];
    std::uint64_t down, up;          // digital edges against the previous frame
};
```

- **Published enum values** (slice 3 review). `Call` (`Ok` 0, `AcquisitionLost` 1, `Gone` 2, `NotCalled` 3, `Pending` 4) and `EventKind` (`EpochBegin` 0 to `RunChanged` 8, `ReadPending` 9) are part of the observer and recorder contract. They are append-only, never renumbered, and checked by `static_assert`.
- **Provenance** (R1-F1).
  - Every action names its source sample.
  - Every sample names its device, epoch and sequence, plus the batch number and a hash prefix for exactly the objects
    that carried a delivered value.
  - The full SHA-256 for each batch number is in the batch log (section 9), which outlives the observer's event ring.
  - A later physical read carries no mark, so the observer can never tag it with the last batch.
  - Acceptance, refusal, delivery failure, epoch begin and end (with the device and the reason), drops and run-state
    changes are separate events (section 9), not frame fields.
- **Normalization** is exactly the contract's (`dbce_controls.hpp`, shared vectors).
- **Statuses:**

| Status | Meaning | Start-line group |
|---|---|---|
| `live` | applied, object verified, a fresh valid read in this epoch, rearmed | applied |
| `rearming` | applied, fresh read, waiting for its rearm condition (section 6) | applied |
| `test-neutral` | a latched process with no delivered injected value for this action in this frame. Neutral is generated at the action layer, not as a raw sample, and the real raw value is still observed (section 9). | applied |
| `unsupported` | the adapter declares the game has no such action, with the reason | unsupported |
| `invalid` | the contract parser refused the line, with the reason | invalid |
| `device-missing` / `product-mismatch` / `object-missing` / `range-unqualified` / `opening` / `read-pending` (opened, no valid read yet) / `read-failed` (epoch ended, reopening) | configured but not available, with the reason | unavailable |
| `unbound` | omitted or empty in the profile | not counted |

An object bound to two actions is reported. It is refused only when the adapter declares the pair exclusive.
Wheelkit's writer already refuses collisions (STD-035 item 3).

## 8. Adapter contract

An adapter:

1. **Declares** for every contract action whether the game supports it, or the reason it does not.
2. **Names its interception point**, the place where it hands the frame to the game. That point must come before
   every consumer of the actions it owns (R1-F3).
3. **Audits every native input path** the game has (each API: DirectInput, XInput, RawInput, HID, WGI, SDL, an
   engine's own reader) and states how owned devices are excluded from each *before* the game consumes them: hidden at
   the API, filtered in the native reader, or absent by construction.
   - An override applied after the game has resolved its input is never claimed as duplicate protection. A native
     button may already have confirmed, paused, reset or shifted.
   - Max-merge cannot remove a duplicate wheel contribution from an aggregate that has lost device identity.
   - Unrelated native input (keyboard, pad) is preserved in ordinary play. In a latched process the adapter suppresses
     its gameplay contribution before the consumer. It keeps an explicit stop path (window close, the pad's exit
     combination) and reports the native values it suppressed (section 9).
   - Duplicate exposure is verified at run time, never assumed from a product or type label (R1-F9). For example, the
     adapter lists every native device that matches an owned VID/PID.
4. **Declares a merge policy per action** (override, max-merge or native-only) between owned values and unrelated
   native input, and declares the context, edge and repeat semantics with the point where each is observed. Example:
   OutRun's menus act on frontend-feature edges, while the race reads held `buttons_04` bits.
5. **Neutralizes** the owned contribution on stop, scene change, epoch end and test failure. It never resurrects stale
   native wheel input.
6. **Supplies the window** that cooperative levels need, and declares its background policy: whether owned devices
   drive the game while it is unfocused.
7. **Supplies state signals** through one interface: named numeric channels with units, each tagged with the frame
   seq it belongs to and its offset (e.g. "as consumed in the previous tick"). It names what it cannot observe. The
   same interface feeds the observer, the recorder and, later, telemetry (STD-016) and the force model, so a game has
   one state source rather than three.
8. **Implements and self-checks its fences** (section 9). Each fence reports `verified` with evidence, such as the
   effective path, the absent pointer or the grep result. Anything missing or ambiguous refuses admission (R1-F9).
9. **Declares derived inputs**, if any (e.g. steering into menu left and right). They are never fed back as actions.

## 9. Test runs: latch, admission, batches, failure, observation

**States** (R1-F2, R2-F2 to R2-F4):

```
NONE ──(inject.on exists at start)──► LATCHED ──► fences verified? ──no──► REFUSED (game never initializes)
                                                       │yes
                                                       ▼
                       core/profile/admission ok? ──no──► INERT (game runs offline, all gameplay input neutral)
                                                       │yes
                                                       ▼
                                   WAITING (neutral, bounded) ──first batch──► RUNNING ──► COMPLETED | EXPIRED
                                       │                                          │
                                       └────────────(any failure)─────────────────┴──────► FAILED
```

**LATCHED.**
- Decided first in the entry point: the only test is whether `inject.on` exists. That check is independent of the
  profile, the provider and the core, so a profile or core failure cannot bypass it.
- The latch is set whatever the file contains: refused, stale and expired files latch too.
- It lasts for the life of the process. A latched process never returns any wheel-class action to physical control,
  and never uses the owner's saves or the network.
- Then the adapter installs and self-checks its fences, before the game initializes.

**REFUSED.** A fence cannot be verified, or the run folder cannot be made. The process does not initialize the game.
It writes the reason to `%LOCALAPPDATA%\dbce\<productId>\refused-<pid>.txt` and exits with a distinct code. No owner
file is touched.

**INERT.** The fences are verified, but the core, the profile, the provider or admission failed. The game runs with
the fences in place, offline, with redirected saves.
- Every action the adapter supports is neutral, whatever the profile does or does not name. An invalid or missing
  profile therefore cannot shrink the owned set and slip past neutralization (R2-F2).
- Native gameplay input is suppressed too.
- The observer reports the reason, and the run is FAILED.

**Admission**, all required:
- a strict session file: exactly one `nonce=` (8-64 alphanumeric) and one `expires=` (at most 1 h ahead), no unknown
  or duplicate keys, strict UTF-8, at most 512 B;
- Schema 1 with at least one applied action;
- every adapter fence `verified`;
- no force path (section 11);
- every owned device `live` or `rearming` after its first fresh read.

The **admitted workload** is the set of owned devices at admission. All of them are required for the rest of the run,
including any the run has not injected into yet (R2-F4).

**WAITING.** Admitted, no batch yet. Every owned action is `test-neutral`. If no valid first batch arrives within
120 s, the run is FAILED (`no command`).

**Batches** (N5, R2-F4). The control worker reads each new version of `inject.txt`.
- **Header:** `nonce=<session>` then `batch=<n>`, then at most 32 commands, at most 4096 B.
- **Order of checks:**
  1. Wrong nonce: FAILED.
  2. Compute the full SHA-256 of the bytes.
  3. `n` equal to the last accepted batch, with the same hash: an exact duplicate. Reported and ignored, and the
     running samples keep their deadlines (no TTL extension).
  4. `n` equal to the last accepted batch with different bytes, or `n` lower than it: FAILED.
  5. `n` higher: validate the whole batch against a scratch injection table built from the immutable profile and the
     device capabilities (grammar, bindings, objects, ranges, capacity).
  6. Valid: the batch becomes an immutable command generation in a bounded first-in, first-out handoff of 8
     generations. Any refusal, or a full handoff: FAILED.
- **Activation.** The input thread activates at most one generation per tick, in order. From that tick it replaces
  every running sample atomically. Per-object deadlines (`ms=`) count from activation.
- **No generation disappears** (R3 review). Every accepted batch gets exactly one of three events:
  - activated, at that tick;
  - superseded before activation, which happens only when the run ends (FAILED, COMPLETED or EXPIRED) while it was
    still queued;
  - refused.

  A single latest pointer is never used, so a short button press cannot be silently erased. A superseded or never
  activated command never counts as delivered.
- **Batch log.** Every accepted, duplicate and refused batch is written to the batch log, a `runtime.log` JSON line with
  `n`, the full hash, the command count and kinds, and the acceptance and activation times. The observer's ring may
  overwrite its entry; the log keeps the mapping.

**Frame boundary in a test run** (R2-F4).
- Each tick reads and validates every device in the admitted workload before any value reaches the sink. A device
  passes when its read is valid, its epoch is unchanged and every injected object for it was delivered with a readable
  range.
- Only if all of them pass are the injected values published. If any fails, the whole frame goes to the sink neutral
  and the run is FAILED, so an injected action on the first device never executes when the second device fails in the
  same tick.
- Ordinary play keeps per-device independence. One device failing neutralizes only its own actions, so pedals still
  work without the steering device.

**RUNNING.**
- An owned action takes a value only from an injected object delivered in this frame. Otherwise it is `test-neutral`,
  generated at the action layer, never as a fake raw sample claiming delivery (N3, R2-F3).
- Real raw values of non-injected objects are still read and observed, for diagnosis. They never reach an action.
  A held physical pedal, hardware noise or another controller therefore cannot drive the game.
- Native gameplay input (the pad, the keyboard) is suppressed before the consumer and observed. Any non-neutral native
  value during RUNNING is reported as an event and invalidates the qualification. The stop path is kept: window close
  and the pad's exit combination.
- When an object's `ms=` runs out, its action returns to `test-neutral`, never to physical input.

**FAILED** is terminal for the process. Its triggers:
- a refused, regressed or conflicting batch;
- an unreadable or oversize command file after the first committed batch;
- the waiting timeout;
- a delivery failure;
- an epoch end on any device in the admitted workload;
- a frame-boundary failure;
- INERT.

On FAILED, running samples are dropped and every owned action is neutral, with one up edge each. The fences stay and
the event is logged. A new file never rearms the process.

**COMPLETED and EXPIRED.** COMPLETED is a batch whose only command is `end`. EXPIRED is the session expiry, `expires=`.
Both drop running samples and leave every owned action neutral for the rest of the process. Per-object expiry,
session expiry and `end` are distinct events, and none of them resumes physical input.

**Stimuli.** Qualification uses `inject raw`, built independently from the intended profile's raw objects (STD-034
item 3). `inject action` resolves through the applied mapping and is for navigation only. The batch log records which
kinds each batch used.

**Owner-visible latch** (N4). A latched process shows an unmistakable in-game banner: "TEST MODE: wheel disabled,
saves redirected; close the game and clear the test request in Wheelkit".
- Wheelkit shows the same explanation, and offers an explicit **clear test request** action.
- That action is available only while the game is closed, and only for a request that an owner receipt identifies.
  - The receipt is a separate file, `inject.receipt`, written by the harness next to `inject.on`, with its hash pinned
    in the harness's run record.
  - `inject.on` itself stays strict: only `nonce=` and `expires=`, and unknown keys are still refused (R3 review). The
    game never needs to read the receipt.
- An unknown active request is never deleted automatically.

**Observer v2:** `Local\Dbce<Product>InjectObserve-<nonce>`, created by this process or refused. Versioned; layout in
`windows/docs/OBSERVER.md`. It carries:
- run state, latch and fence states;
- the last frame: seq, tick time, and per action its value, status, injected flag and source sample;
- the frame's sample table: device, epoch, seq, read end, validity, injection mark (batch number plus hash prefix),
  and the raw values;
- suppressed native input;
- the adapter's state signals, each with its frame seq and offset;
- an event ring of 64 entries with an increasing event sequence, so a reader detects overrun by a gap.

How it is written (R2-F6):
- The input thread is the only writer.
- A seqlock, staying in C++17 (`std::atomic_ref` is C++20; R3 review):
  - the sequence and every payload word are `std::atomic<std::uint32_t>` members of the mapped struct, with relaxed
    payload stores between release and acquire fences;
  - they are created by placement-new over the zeroed view once, by the creating process;
  - each is checked at compile time to be lock-free and address-free (`is_always_lock_free`), 4-byte aligned, at fixed
    offsets.
- The sequence wraps modulo 2^32. Readers compare equal even values before and after a copy, so a wrap is harmless.
- The mapping lives until the process ends. Readers never write.
- The writer never waits on readers, so a reader dying cannot stall the game.
- This is a deferred platform gate: the observer transport is reviewed separately before it is accepted.

Proof required (section 17): a concurrent reader thread checks a payload checksum for torn reads, a reader process is
killed mid-read, and the ring is overrun.

The join is explicit: batch number (full hash in the batch log), to the first sample carrying its mark, to the frame
that used that sample, to the state signals published for that frame seq.

## 10. Recording

R1-F7, Q9.

- **One designated producer**, the game input thread, assembles a copied snapshot per tick: frame values, sample
  metadata, and adapter state signals with their own source seq and time. It pushes the snapshot into a bounded
  single-producer queue. Future producers (a physics thread, the force model) each get their own bounded ingress.
  They are not added to this queue.
- **Times.** The row time is the emission time: the producer's `tickUs`, monotonic by construction. Source sample
  times and frame seqs are channels (`src.state.frameSeq`, `src.state.us`). A late signal never carries an older row
  time.
- **Bounds:**
  - the queue holds 1024 rows and 4 MiB;
  - `session_writer`'s limits apply (600 s, 64 MiB, 256 KiB per record);
  - stop flushes for at most 1 s.

  Overflow counts the drops and writes a gap marker when there is space. Any drop or a flush timeout makes the footer
  incomplete. Ticks are never manufactured.
- **Metadata:**
  - the profile revision and file hash;
  - device identities with their read-back ranges;
  - device policies (rearm, background);
  - core and adapter versions and commits;
  - effective tuning (sensitivity, dead zone);
  - units per channel;
  - whether the process is ordinary or a test run.
- **Writer:** `native/session/session_writer.h`, pinned by commit and hash.
- **Control surface: open.** Wheelkit's existing recording and playback runner lifecycle (Wheelkit
  `docs/RECORDING-PLAYBACK.md`) is audited first, and reused where it fits. No `record.on` is invented before that
  audit. Ordinary recording, test admission and pose replay stay separate modes.

## 11. Force feedback

R1-F4.

- Force goes through the bridge provider (section 6) on the shim backend; OutRun has no force lease of its own.
  - `shim_input_steer()` (source/shim_steer.hpp) is the identity the delivery gates on (romi 31452/31707). It joins
    the steer binding's instance and product (from the same profile bytes Core parses) to Core's device report (open,
    epoch), and `live` requires this tick's steer action to come from a valid, fresh, not injected sample of that
    device in that epoch.
  - The delivery opens only with live steering in an epoch E0. It sends zeros only until the steer is live in a new
    epoch E1, read through the force handle; if that has not happened within 2 s, it releases.
  - It releases on any later epoch change, and sends a zero whenever the steering is not live.
  - A WheelFfb older than 0.11.0 refuses the output with Input = Shim. Without the bridge's hand-over (for example,
    the shim host did not start), the delivery never opens.
- A latched process creates no force path (STD-034 item 4). The environment mute `DBCE_FFB_MUTE` is the second
  interlock (STD-035 item 8). Both must cover the stock game's producers and those of other mods in the process, not
  only the shim's own.
- No-force qualification mutes delivery. It does not delete the producer. Force calculation and telemetry stay
  available with delivery muted.

## 12. Status and reporting

R1-F8, Q11.

- **Human start line,** once the provider has checked the devices:
  `controls: profile "<p>" revision <r> via shim-ini-controls-1: applied n (ids); unsupported m (ids); invalid k
  (ids); unavailable u (ids)`. One line follows per non-live action, with its status and reason. It is for people
  reading the log. Wheelkit 0.6.15 (`b1197ce`) has **no** reader for controls start lines or runtime status (Astra P9,
  `inbox/2026-10-10-romi-shim-wheelkit-p9.md`), so nothing in Wheelkit reads this line today, and no UI integration
  is claimed from it.
  - What is proven: the released Wheelkit writer refuses the new capability. With the released Core DLL, the
    `shim-ini-controls-1` receipt yields zero targets and no configured badges, while the legacy OutRun receipt yields
    26 targets.
  - What remains: a reviewed, versioned runtime-status reader in Wheelkit (P9b).
- **Structured status:** `controls-status {"v":1,...}` JSON lines in `runtime.log`, written at start and on every
  change. They carry:
  - per action: id, status, reason, device and epoch;
  - per device: identity, name, status, epoch and the objects verified;
  - the profile revision and file hash;
  - the core and adapter versions;
  - the run state and the fences;
  - read-timing counters.

  These are bounded, queued and coalesced lines written by the control worker. There is no second persistent status
  file until a consumer needs one (N6). Each line carries the process id and start time, the run nonce (when
  latched), the profile file hash and the build (core and adapter versions, and the executable's SHA-256). Each line
    also carries gap counters: status lines coalesced or dropped and recorder overruns. The journal records the
    observer's published sequence; the observer reader reports its own missed snapshots/events. A seqlock writer
    cannot know what any particular reader missed. Qualification joins those separate reports.
- **Evidence stays apart** (STD-033 item 4, R2-F7):
  - verified objects are configuration evidence;
  - `live` is current availability;
  - neither is an observed game response.

  The start line is a human summary, not test proof. Final evidence joins the structured status, the batch log and
  the observer for one process, run, profile and build, and it reports every gap.

## 13. Interception classes: candidates, not proof

| Class | How the adapter reaches the game | Game config | Pre-consumer guard to prove | Candidates |
|---|---|---|---|---|
| **A: source or mod-owned sink** | writes actions into the game's input variables | untouched | our build has no other path for owned devices; verified per API at run time | OutRun decomp (reference); Daytona and F-Zero recomps; BepInEx mods that already read devices |
| **B: engine action override** | replaces engine action values at the engine's input stage (Rewired, Unity Input System, InControl) | untouched | owned devices removed from the engine's controller set before it produces actions, for every API the engine reads (Rewired also reads RawInput/HID/XInput) | #DRIVE Rally (second game, after its qualification); Karting Superstars |
| **C: in-process virtual device** | a proxy presents a fixed virtual controller | bound once to it, or its default | physical devices hidden from every API the game uses | research hypothesis only (R1 Q5): DirectInput and SDL games |
| D: out of process | companion process to a virtual pad | — | — | Sonic CrossWorlds today; not a shim target |

The class-A Unity mods and the class-B adapter need the core from C#. That takes the experimental bridge (Q10) or the
managed contract twin (`Dbce.Wheel.Input`) for parsing only. There is never a second resolver. DRIVE is where that
gets settled.

## 14. Standards impact (on promotion only)

- **STD-033 item 2.** In a shim game, `dbce-controls.ini` is the one store for wheel controls. The native store stays
  as the game left it and must not be a competing path for owned devices, which section 8 items 2 and 3 must prove.
  Amendment: "a game may keep its controls in the shim store instead of its native store when its adapter proves
  pre-consumer exclusion of owned devices on every native path".
- **STD-035.** The writer rules shrink to the section 5 transaction. Items 6 to 10 (runtime) are implemented once in
  the core.
- **STD-034.** Unchanged. Configured is the status, observed is game state after raw injection, accepted is the owner.
- **STD-036.** The shim reads each binding from its own device, but that is not qualification. It needs multi-device
  execution evidence (R1-F8).
- **STD-030.** F6 and Wheelkit share the section 5 transaction. F6 controls are never made read-only (R1-F6).

## 15. OutRun 2006 reference adapter

**Interception point.** `xbox_runtime::input_filter`. It runs after the pad adapter (`DualSenseInput::update`) and
before 453BB0 consumes `NativeInputState`. Nothing in the game reads the actions earlier.

**Native path audit** (R1-F3, R1-F9):

| Path | Source | Owned devices | How excluded |
|---|---|---|---|
| WGI `Gamepad` | `xbox/source/platform_api.cpp` `read_pad` | wheels and the DS-8X are not expected to be Gamepads; a pad bound in the profile is (C1h run1: a DualShock 4) | Until 2026-10-12 only `select_pad` and its fixture existed; `read_pad` read the first Gamepad, so a shim-read DualShock 4 was read twice (C1h run1: its Options paused the race, its R1 added a shift). Now the ordinary shim sets `xbox_runtime::gamepad_choice` (shim_input.cpp): `read_pad` asks it for the pad to read, given each Gamepad's VID/PID (RawGameController, cached per pad), and `select_pad` leaves out every pad of a product the profile binds (by product, not instance) and any pad whose identity cannot be read. Unset (the Xbox, Backend = Native, no profile): the first pad, as before. Each change of the pads or the choice is logged ("pad: ..."). test_outrun_adapter test_owned_pad_path runs the seam (owned_products, select_pad, the pad adapter, the adapter) with owned, unrelated and unidentified pads and the C1h gate buttons. On the rig the log line corroborates the choice; it is not the proof. |
| Keyboard | none in the Xbox desktop build (no `WM_KEY*` / `GetAsyncKeyState` in `xbox/source`, `windows/source`) | — | absent |
| PC DirectInput layer (406FA0) | emulated from `pc_pad`, which is built from the WGI pad | — | no real device behind it |

**Sink:**
- `steer`: record word 0x0E = `lround(n * 32767)`, overriding the pad's steering when live. 453860 then applies its
  0.0078 dead zone and its quantisation.
- `throttle`, `brake`: 0x0D and 0x0C = `lround(n * 255)`, max-merged with the pad's triggers, which are an unrelated
  device.
- `pc_input_layer = false` whenever any of the three is live. A channel that is not live keeps the pad's record value,
  so pedals work without the steering device (R1 review of 35a8aa3, item 1).
- Buttons go to the `buttons_04` bits (race, held), menu edges and frontend features (menus, down edges), as in
  35a8aa3.

**Background policy:** owned devices drive the game while it is unfocused, as a cabinet needs. The policy is declared
in the status.

**H-pattern** (R1-F10). A state machine on a monotonic clock, not frame counts:
- The target is the gear of exactly one held gate. With none held it is idle. With two or more held it is
  `ambiguous`: no action, logged once per hold.
- Gear state comes from car +208. Outside a race or without a car, the gear is `unavailable` and the machine waits.
- Each step:
  - a shift pulse held for one tick, then released for at least one tick;
  - wait for the consumed gear to change, with a 250 ms deadline;
  - on change, the next step; on the deadline, retry the step at most twice;
  - then stop with `gear target not reached (gear g, target t)`, logged once until the gate changes.
- Impossible gears (above the car's top gear) end the same way, because the car's gear count has not been read yet.
- The observed gear must be a real gear, 1-6. A missing or corrupt word (0, 7, `0xFFFFFFFF`) is `unavailable` and
  never causes a pulse.
- The tick after any pulse is released, even when the gate, the direction or the state changes in between.
- Neutral and reverse are `unsupported` (the game has none). Forward-gear chasing never stands in for them.
- A scene change (any mode change) resets the gearbox, and its own tick sends no pulse, so every pulse is followed by a
  released tick. A fresh sequence starts on the next tick (slice 2).

**State signals:**
- mode and in-gameplay;
- speed (car +1C4, game units);
- gear (+208);
- the consumed 7D6810 steer, accelerator and brake (offset: the previous tick);
- the record words and buttons written (offset: this tick).

**Fences under the latch** (R1-F9, R2-F8, N8; **slice 2b proposal, r3.2**). The game normally reads its data and writes
its state in one folder: `application_home` returns the game folder as both `retail_root` and `rt::home()`.

**Mechanism: split home from data.** Under the latch, the entry point gives the host two folders:
- data is still read from the game folder (`retail_root`);
- `rt::home()` becomes the run folder.

Most writers derive from `home()` or from the save folder (`home + "/SaveGame"`), so they move with that one switch.
The source audit classifies every writer, and the few that write into the game folder are each handled explicitly.
Admission's self-check confirms the effective `home()`, save folder and log path are inside the run folder. A writer
that resolves anywhere else refuses admission.

**Run folder.** `%LOCALAPPDATA%\dbce\outrun2006-decomp\run-<nonce>-<pid>\`.
- It is created exclusively (`CreateDirectoryW` fails if it exists), and the folder and its parents refuse reparse
  points.
- It is seeded with a verified copy of the owner's SaveGame and `options.ini`: real copies, never links.
- The copy is bounded (at most 256 files and 64 MiB), every path is validated to stay inside, and every file's
  SHA-256 is pinned in `run.json`.
- An empty, first-run SaveGame is a separate qualification case.

**Every file-writing call site** (inventory 2026-10-10, regex audit of `src`, `xbox/source`, `windows/source`,
`windows/shim`):

| Writer | Source | Base path | Under the latch |
|---|---|---|---|
| Profiles and licences | `frontend_profiles.cpp:17-31` (temp, backup, rename), `:219` (remove) | save folder | redirected |
| Rankings | `race_end_runtime.cpp:937` | save folder | redirected |
| Ghosts and records | `race_ghosts_runtime.cpp:43` | save folder | redirected |
| `options.ini` | `video.cpp:77-78` (temp and rename), `main.cpp:108` (default file) | `home()` | redirected (seeded copy) |
| `crash.txt` | `main.cpp:177` | `home()` | redirected; `home()` is the run folder before the terminate handler is installed (no early crash writes the launch or current folder) |
| Result log | `game_host.cpp:1212` | `home()` | redirected |
| `runtime.log` (stdout, stderr) | `window_desktop.cpp:95-96` | the executable's folder | redirected: the latched entry point opens it in the run folder |
| `trace.txt`, `replay.bin` | `game_host.cpp:91`, `:118` | `home()` | redirected, and the development hooks are refused (`autoplay.txt` and `replay.txt` are not read in a latched process) |
| EXE image cache | `exe_image.cpp:182-186` | game folder (`setup_screen.cpp:115`), then `home()` | the game folder is only read (`exe_setup_prepare(..., game_dir_read_only)`): an existing cache is loaded read-only, and one built for another range table is verified and loaded but never rewritten (`exe_image_load_cache(..., rewrite_in_place=false)`, nene 10191); a needed regeneration goes to the run folder (2b review: no game-folder write at all) |
| Former-cache migration | `setup_screen.cpp:102-109` (renames and removes in old cache folders) | game folder | never run on the game folder when `game_dir_read_only`; the run-folder call stays inside the run folder |
| Write probe and SaveGame folder | `platform_api.cpp:41`, `:67` | game folder | `application_home` returns `latched_home()` before probing, after refusing a data root other than the fenced folder (`latched_game_root()`: no `--data=` or drive search can make the seed and the data disagree); the fence created and seeded the run folder's SaveGame |
| PNG and frame dumps | `pc_soft_d3d9.cpp:662` (no call site; `write_png` is a watched wrapper), `d3d11_display.cpp:147` (`OR2_XBOX_SCREENSHOT_DIR`) | environment-chosen | off: the latched entry point clears every `OR2_*` variable before anything runs |
| `OR2_EXE_TRACE` | `exe_image.cpp:39` | environment-chosen | off (every `OR2_*` variable cleared) |
| `OutRunDesktop.ini` creation and writes | `desktop_settings.cpp:131-144` (35a8aa3) | game folder | not created (`load_desktop_settings(folder,false)`); the 35a8aa3 wheel path (its legacy import writes) is not configured in a latched process |
| LAN (and its AF_UNIX `unlink`) | `native_network_set_platform` (`main.cpp:171`) | — | not installed when `latched_home()` is set: offline |
| `home=` argument | `set_home` (`game_host.cpp:54`) | — | the Switch build's setter; no call site in the desktop build, and it is a watched wrapper |
| Force | none in the decomp (no `CreateEffect` or vibration; WGI `Gamepad.Vibration` never set) | — | n/a, grep evidence |

**Fail-closed seeding** (nene 10191): every listing or inspection failure refuses the run. Only `ERROR_FILE_NOT_FOUND` means absent, and a listing must end on `ERROR_NO_MORE_FILES`. A refusal is never written through a link: `write_refusal` re-checks, and the process exits with 4 when it cannot record the refusal. Nor is it written into anything already at its name (nene 10329): a link or folder named `refused-<pid>.txt` refuses and is left as it was, an ordinary file there (a stale note, or a hard link) loses only its name, and the note is always created new, so a name planted between the check and the create fails the create.

**Runner duties** (nene 10191, design note on P12): a leftover `inject.on` fences every later ordinary launch. So the harness removes `inject.on` at the end of every run, checks for a leftover before an ordinary launch, and defines retention and cleanup for refused run folders, which keep partial copies of the owner's saves.

**Proof** (R2-F8):
- **Source audit, a build-time refusal.** `windows/tests/write_audit.py` scans for every write-capable call: `fopen`,
  `freopen` or `_wfopen` with a write mode, `ofstream`, `fstream`, `CreateFile*`, `MoveFile*`, `ReplaceFile*`,
  `DeleteFile*`, `rename`, `remove`, `std::filesystem` mutations, `CreateDirectory*`.
  - Each call is matched against this table by file and a normalized snippet, not by line, so line moves do not
    break it.
  - An unclassified site fails the test. The audit reruns on every upstream merge.
  - A call counts as read only only from the argument at its own mode or flags position (2b.2 audit review): the
    `fopen` mode literal, the descriptor flags made only of known read-only flags including `O_RDONLY`, a stream mode
    made only of `ios::in` and `ios::binary`, in each case names joined by single bitwise ORs (a logical `||`, a
    conditional or any other operator is a candidate). A read-only token in the filename, a read-only flag OR'd with
    anything unknown, or an argument count the function does not take leaves the call a candidate.
  - The snippet is the whole call: every call on the line is followed to its closing parenthesis, so a destination on
    any later line is part of the key. A call that does not close within 100 lines is `INCOMPLETE`, an error that no
    entry can classify.
  - `windows/tests/audit_mutations.py` breaks each argument and joining rule in a copy of the audit and requires the
    self-test to fail.
- **Actual writes, in a fixture.** A host-configuration fixture applies the latched split to a temporary game tree and
  drives the real writer functions that can run without the game: `write_options` (`video.cpp`), the profile writer
  (`frontend_profiles.cpp`), the result log, the crash breadcrumb and the `runtime.log` redirection. It then checks
  that every byte landed in the run folder and that the game tree is byte-identical.
- **What remains for the fenced live run.** Writers that need the running game (rankings, ghosts) are proven there by
  the harness's before-and-after snapshot of the game folder (STD-034 item 7).

**Unsupported, with reasons:**
- clutch, handbrake, neutral and reverse: not in the game;
- gear7 and gear8;
- select;
- lookBack, reset and horn: not mapped yet;
- modMenu: until the F6 panel;
- muteFfb: until force feedback.

## 16. Decisions and remaining questions

**Decided in round 1** (Astra's answers, adopted):

| # | Decision |
|---|---|
| Q1 | Explicit, previewable, all-or-nothing, no-clobber import from Wheelkit or an opt-in importer. Never automatic. |
| Q2 | One writer transaction for both frontends, one file, revision and hash checks, and a cross-process writer lock (section 5). No mandatory Wheelkit. |
| Q3 | Whole-batch replace with identity and deduplication. An invalid batch fails the run and leaves owned actions neutral. |
| Q4 | Reuse proven behavior behind an explicit provider boundary. No promise of shared force handles. |
| Q5 | Second game: DRIVE (class B), after its current qualification, as an opt-in isolated spike. Class C is research only. |
| Q6 | Game and adapter shaping as now. Record the effective values and avoid a double dead zone. Any canonical shaping needs versioning. |
| Q7 | Start-only. A future reload swaps the whole profile and then rearms. |
| Q8 | Exact identity, bounded rescan, new epoch, neutral rearm, no silent test continuation. |
| Q9 | Audit Wheelkit's existing session lifecycle first. Ordinary recording and test runs stay distinct. |
| Q10 | No stable ABI before the second adapter. An experimental bridge is allowed; a second resolver is not. |
| Q11 | Versioned structured status plus a compatible line. Old readers fail closed. Unavailable stays separate. |

**Decided in round 2** (Astra's answers, adopted):

| # | Decision | Where |
|---|---|---|
| N1 | Digital waits for release. Steer, brake and clutch go live on a fresh valid read. Throttle and handbrake wait for rest. Thresholds, freshness and reasons are recorded. One invalid read never rearms. | section 6 |
| N2 | A read failure ends the usable epoch and neutralizes. A transient loss may reacquire the same verified handle, but as a new epoch with rearm. Results are classified by `SUCCEEDED`, not by nonzero. A test fails. | section 6 |
| N3 | During a test, owned actions are neutral unless an injected value was delivered (`test-neutral`), and real raw values are observed. Native gameplay input is suppressed and observed. Ordinary play is unchanged. This is a versioned shim test policy. | section 9 |
| N4 | Latch safely, with an unmistakable in-game banner and a Wheelkit explanation. The receipt-bound clear action works only while the game is closed. An unknown request is never deleted automatically. | section 9 |
| N5 | Nonce, increasing `batch=<n>`, full SHA-256. An exact duplicate is ignored without extending TTLs. The same `n` with other bytes, or a regression, fails. First-file wait and per-object expiry are specified. | section 9 |
| N6 | Bounded, queued JSON lines in `runtime.log`, with process, run, profile and build identity and gap counters. No extra status file yet. | section 12 |
| N7 | `shim-ini-controls-1`. Astra checked Wheelkit: it has no runtime-status reader yet, and the released writer refuses the new receipt (P9a, proven). A versioned runtime-status reader is future work (P9b). | section 12 |
| N8 | A verified, bounded copy of the owner's saves and options in an exclusively created run folder, with hashes pinned and no links. First-run is a separate case. | section 15 |

**Constants reviewed with the code, not here:** the 100 ms freshness window, the 0.05 rest threshold, three
reacquisition tries over 100 ms, the 120 s first-batch wait, the 250 ms gear deadline, the 2 s writer-lock wait, the
200 ms shutdown wait, and the queue sizes. Every one of them is reported in the status or the recorder metadata.

## 17. Proof matrix, staged by slice

Each slice is written as tests and code, reviewed, and accepted before the next slice starts (R2-F8). Levels:
- **U**: a unit test on pure code and the fake provider;
- **A**: an adapter fixture;
- **W**: a Wheelkit-side test (Astra);
- **L**: on the rig under the lease and idle gate;
- **O**: the owner.

**Slice 1: pure core and fake provider.** No DirectInput, no game.

| # | Requirement | Proof | Level |
|---|---|---|---|
| P1 | Identity in the listing and after the open; a product mismatch is a status | fake twins, mismatched product, an identity change between listing and open | U |
| P2 | Objects by offset; non-contiguous axes | fake device with X, Z and Rz only; a binding on Y gives `object-missing` | U |
| P3 | Per-axis range readback; unqualified axes refused; rescale from the read-back range | refused request, unreadable, empty, -32768..32767 | U |
| P4 | Read classification; epoch end neutral in the same frame with one up edge; bounded reacquisition opens a new epoch | `S_FALSE` used as valid; `INPUTLOST` while a button and pedals are held; `UNPLUGGED` | U |
| P5 | Rearm per action: digital masked; throttle and handbrake wait for rest; brake, clutch and steer live on a fresh read; nothing goes live without a fresh valid read | reconnect with each action held; a stale read; one invalid read | U |
| P6 | The worker never blocks the tick; close handoff and acknowledgement | fake slow enumeration and open; a close requested mid-tick | U |
| P7 | Statuses, including `read-pending`, `read-failed` and `test-neutral`; start line after the device checks | a profile spanning every status | U |
| P8 | Structured status: coalesced, bounded, identity-pinned; overflow counted with recording off | a status storm | U |
| P9a | The released Wheelkit writer refuses the shim capability | released 0.6.15 Core DLL: the `shim-ini-controls-1` receipt yields 0 targets and no configured badges (Astra, `artifacts/wheelkit-shim-p9`) | W, **proven** |
| P9b | A versioned runtime-status reader joins process and run, profile revision, provider and installed build; unknown, missing or stale status clears runtime verification | fixtures: unavailable and rearming, a stale success from an earlier run, an unknown status or provider, a changed build or profile, missing observations | W, future integration |
| P10 | Writer transaction: interleaved writers with barriers (lost-update counterexample), an editor that ignores the lock between recheck and commit, abandoned lock, timeout, aliases, missing-file race, replacement failure, preservation, BOM and CRLF | shared vectors and barrier tests, C++ (F6) and C# | U, W |
| P11 | Import: explicit, preview, all-or-nothing, no-clobber, sources untouched | owner Redux vectors; an existing target; a failure midway | U |
| P12 | Latch from `inject.on` existence alone; never cleared; failed-core and invalid-profile latched starts go INERT with every supported action neutral; a fence failure is REFUSED before game init | missing, stale, malformed and valid `inject.on`; core init failure; no profile | U |
| P13 | Admission requires strict session, Schema 1, every fence verified, no force, fresh reads; the admitted workload is fixed | fake adapter with one unverified fence | U |
| P14 | Batches: header and order of checks; exact duplicate ignored without TTL extension; same `n` with other bytes fails; regression fails; 33 commands; a bad 5th line; first-batch timeout | each case | U |
| P15 | Frame boundary: a second-device failure in the same tick, so no injected action from the first device reaches the sink; epoch loss of a device not yet injected fails the run | two fake devices | U |
| P16 | RUNNING: a held physical pedal outside the injection set stays `test-neutral` and its raw value is observed; native input is suppressed and flagged; per-object expiry gives `test-neutral` | fake physical holds and a fake native pad | U |
| P17 | FAILED, COMPLETED and EXPIRED are terminal and neutral; physical input never returns; a new file never rearms | each trigger, then a valid file | U |
| P18 | Provenance: action to sample to batch number to full hash in the batch log; no mark on a later physical read | injected steer, expiry, physical read | U |
| P19 | Observer: single writer; a concurrent reader's torn-read checksum; a reader killed mid-read; ring overrun detected; the join | reader thread; a child reader process; 70 events | U |
| P20 | Recorder: one producer, emission versus source times, bounded rows and bytes, overflow makes the capture incomplete, flush deadline, metadata pins | fake slow writer; managed `SessionReader` | U |
| P21 | Shutdown order: neutral frame, then force stop (`none` here), then run end, then recorder drain, then leases; with the input thread stopped: no hang, no release of an in-use handle, `unresolved` reported | fake stuck input thread | U |

**Slice 2: OutRun adapter on fakes.** No devices, no game window. It is split in two: 2a covers the consumer side (P22,
P23, P25, P26); 2b covers the fences and the run folder (P24, P27).

| # | Requirement | Proof | Level |
|---|---|---|---|
| P22 | Sink: steer word, pedals max-merged with the pad, pedals without steering, button bits and edges | fake frames into `NativeInputState` | A |
| P23 | H-pattern: pulse, release, deadline, retries, ambiguous gates, unavailable gear, impossible gear | fake gear word responding late, never or twice | A |
| P24 | Fences by actual writes: save, `write_options`, crash, log land in the run folder; LAN not installed; dev hooks refused; an undeclared writer fails the source audit | fixture driving the real writers; source-audit test | A |
| P25 | Pre-consumer exclusion: the same owned identity offered through a fake WGI Gamepad and through the provider reaches the game once | fake WGI pad plus fake provider | A |
| P26 | Ordinary native-only start: no profile, the pad drives the game; unrelated pad input merges in ordinary play; latched with no profile goes INERT | host fixture | A |
| P27 | Run folder: exclusive creation, bounded and verified copy, no links, hashes pinned | fixture on a temp tree with a junction | A |

**Slice 3: real DirectInput provider.** Against fake COM devices, as WheelFfb's `injection_test` does.

| # | Requirement | Proof | Level |
|---|---|---|---|
| P28 | P1-P6 and P21 rerun through the real provider code over fake COM objects | fake COM devices | U |

**Slice 4: read-only rig.** Under the lease and idle gate, with no game input and no force.

| # | Requirement | Proof | Level |
|---|---|---|---|
| P29 | A device opened on the worker and read on the input thread; read-time distribution | read-only probe | L |
| P30 | The R12, DS-8X and stalk: identities, objects and ranges as read back; WGI listing corroborates P25 | read-only probe | L |

**Slice 5: fenced game run** (Astra's harness).

| # | Requirement | Proof | Level |
|---|---|---|---|
| P31 | Closed loop (STD-034): staged wrong bindings, Wheelkit Apply, raw stimuli, game words move, exact restore | harness | L |
| P32 | Multi-device: steering and pedals from the R12, gears from the DS-8X, each from its own device | P31 with DS-8X raw stimuli | L |
| P33 | The owner drives it | at the rig | O |

## 18. Review log

| Round | Date | By | Change |
|---|---|---|---|
| r1 | 2026-10-10 | zero | First draft (`6a53b95`), within Astra's constraints 8795, 8815 and 8823 and addressing the 35a8aa3 findings. |
| r1 review | 2026-10-10 | Astra | Ten findings plus answers to Q1-Q11 (`inbox/2026-10-10-romi-input-shim-r1-review.md`, hcom 8993). |
| r2 | 2026-10-10 | zero | `6b89385`. Every r1 finding addressed; questions N1-N8 and a first proof matrix added. |
| r2 review | 2026-10-10 | Astra | Eight findings plus answers to N1-N8 (`inbox/2026-10-10-romi-input-shim-r2-review.md`, hcom 9109). |
| r3 | 2026-10-10 | zero | Closes the r2 findings: a cross-process writer lock with displacement check and kept backups (F1); a test request latched from file existence alone, with REFUSED before game init and a verified INERT state (F2); one RUNNING policy, `test-neutral` at the action layer with native input suppressed and observed (F3); a frame boundary over the admitted workload, the batch order of checks, the first-batch wait, per-object expiry and the batch log with full hashes (F4); a shutdown order that is safe first and writes to disk last, with a stopped input thread handled (F5); a control worker for file I/O and status, memory-model-correct handoffs and an observer seqlock with torn-read proof (F6); `live` needing a fresh read, per-action rearm rules and identity-pinned evidence (F7); the matrix staged into five slices with the additions requested (F8). N1-N8 adopted. |
| r3 review | 2026-10-10 | Astra | Go-ahead for slice 1 (pure core and fake provider), with four corrections: a strict session file versus owner metadata; `Poll` versus the state read; no generation disappearing; physical versus injected rearm sources. Platform gates deferred: writer rollback race, the `ReplaceFileW` flag, observer atomics in C++17, detached-worker lifetime (`inbox/2026-10-10-romi-input-shim-r3-review.md`, hcom 9203). |
| r3.1 | 2026-10-10 | zero | The four corrections and the deferred platform gates written into sections 5, 6 and 9. |
| slice 1 | 2026-10-10 | zero | Pure core and fake provider: `windows/shim/` (`shim.hpp`, `parse.cpp`, `core.cpp`, `sha256.hpp`), tests `windows/tests/test_shim.cpp` with `shim_fake_provider.hpp`, 112 checks, clean at `/W4`. Covers P1-P8 and P12-P18 plus the four r3 corrections. Mutation check (`windows/tests/shim_mutations.py`): 15 of 17 single-rule breakages caught; the other 2 are unreachable guards kept as defence in depth (terminal finality, delivery outside RUNNING). Not in this slice: P9 (Astra), P10 and P11 (writer and import, platform gate), P19-P21 (observer, recorder, shutdown, platform gates). |
| P9 result | 2026-10-10 | Astra | Wheelkit has no generic controls runtime-status reader. The released writer refuses the shim receipt (P9a, proven). P9b, a versioned reader with freshness, build and profile checks, is future integration. UI integration is not claimed from a log token (`inbox/2026-10-10-romi-shim-wheelkit-p9.md`, hcom 9351). |
| slice 1 review | 2026-10-10 | Astra | Four contract defects reproduced against `fe11a2b`: decisions used the tick's start time (stale input and expired injection got through after a slow read); a delayed admission extended the session; a profile with zero usable objects was admitted; the control-to-input event inbox was unbounded (hcom 9541). |
| slice 1.1 | 2026-10-10 | zero | Fixes: the decision time is re-read after every device read, and stale samples fail a test (ordinary play: `stale`, unavailable); the session deadline is latched on the monotonic clock when admission is requested, and repeats never refresh it; admission requires at least one usable binding; the control inbox is bounded (64), duplicates are coalesced into one counted event per tick, refusals after the first are counted, and an overflow fails the run while generations stay accounted for. 134 checks. Mutation check 23 of 25 caught (the same 2 unreachable guards). Astra's four reproductions now show each defect gone. |
| slice 2a | 2026-10-10 | zero | OutRun adapter on fakes (`windows/source/outrun_adapter.{hpp,cpp}`, `windows/tests/test_outrun_adapter.cpp`, 61 checks, clean at `/W4`). The chain is the game's own: fake pad, then `ps5::DualSenseInput`, then a shim frame from the core and fake provider, then the adapter, into `NativeInputState`. Covers P22 (sink, max-merge with the pad, pedals without steering, bits and edges), P23 (H-pattern on a monotonic clock: deadline, retries, ambiguous, unavailable, impossible, scene-change reset with a released tick), P25 (`select_pad` drops an owned WGI identity before the pad adapter; one consumer), P26 (native-only start, coexistence, latched and inert suppression), plus release and the stop frame. Mutation check (`windows/tests/mutations.py`, which replaces `shim_mutations.py`): adapter 15 of 16 caught; the miss is a redundant flag set that suppression already covers. P24 and P27 follow in 2b. |
| r3.2 proposal | 2026-10-10 | zero | Slice 2b fence design: split `home()` (the run folder) from data (`retail_root`, the game folder) under the latch; a complete inventory of file writers, each classified (redirected, kept and declared, skipped, refused); a source-audit refusal matched by snippet, not line; a fixture driving the real writers that need no game; the game-dependent writers left to the live run's snapshot. For review before coding. |
| slice 2a review | 2026-10-10 | Astra | Two findings: impossible gear words (0, 7, `UINT32_MAX`) still pulsed; a gate change right after a pulse skipped the released tick. The 2b design was approved with requirements: no game-folder write, not even the EXE cache; latch before any writer; a conservative audit with its own mutations; actual production writers through success and fault paths; readers audited so assets are never redirected (hcom 9823, `inbox/2026-10-10-romi-outrun-adapter-review.md`). |
| slice 2a.1 | 2026-10-10 | zero | Gear words outside 1-6 are `unavailable`, with no pulse, before or during a sequence. A released tick follows every pulse across gate changes, reversals and resets (`pulsed_last_` survives `reset()`; the adapter's scene-change tick counts as the released one). 109 adapter checks; adapter mutations 17 of 18 caught (the same redundant flag); Astra's reproducer copy reports both defects gone. |
| slice 2b | 2026-10-10 | zero | The fence as approved, with the 2b-review requirements. `windows/source/test_fence.{hpp,cpp}`: latch from `inject.on` existence alone; an exclusive, link-refusing, bounded and hash-verified run folder seeded with SaveGame and `options.ini`; `run.json` with both roots and the effective paths; `refused-<pid>.txt` beside `inject.on`; every `OR2_*` variable cleared (the CRT and the process). Host: `latched_home()`; `application_home` returns it before the probe and mkdir; `exe_setup_prepare(..., game_dir_read_only)`; LAN not installed; the entry point latches before `runtime.log` is opened, writes the log to the run folder, and creates no settings and no wheel path. Audit (`windows/tests/write_audit.py` and `.json`): 93 classified entries over writers and `home()` readers, 10 ordered guard checks, self-test 13 mutations. Fixture (`windows/tests/test_fence.cpp`, 42 checks): real `write_options`, the profile writer, `exe_setup_prepare` read-only (contrasted with ordinary migration), the log; exists, junction, linked test folder, bounds, a save held open mid-copy, first run, a stale session and root spellings; the game tree's complete inventory is byte-identical in every case. Fence mutations 9 of 10 caught (the miss is a copy-verification branch no fixture can trigger). Found and fixed an upstream bug: `write_options` used `std::rename`, which does not replace on Windows, so options never saved after the first launch. |
| slice 2b audit review | 2026-10-10 | Astra | The audit missed `fopen_s`, `_wfopen_s`, `_open` with write flags and a stream `.open(path, mode)` with a variable mode, and kept a multiline call approved after its destination changed on the next line (hcom 10163, `inbox/2026-10-10-romi-outrun-fence-review.md`). nene reviews the fence paths independently. |
| slice 2b.1 | 2026-10-10 | zero | Audit: the secure fopen family and CRT descriptor opens (`_open`, `_wopen`, `_sopen[_s]`, `_wsopen[_s]`, `_creat`, `open`) are flagged unless visibly read-only. Every member `.open()` is flagged unless its mode is visibly `ios::in` only; non-file opens are classified `not-a-file`. Each key is the whole call, joined over continuation lines. 117 entries (93 to 117: the new opens plus re-keyed multiline calls); self-test 18 mutations, plus a multiline destination control and read-only counterparts that are not flagged. Astra's five counterexamples, inverted, are all caught. |
| slice 2b fence review | 2026-10-10 | nene | Six findings (hcom 10191): the read-only setup still rewrote a mismatched-range game cache in place; a `/*` inside a string literal blinded the audit to 12 writer and `home()` lines; an access-denied subfolder gave a partial seed with no refusal and no `run.json`; a refusal was written through a test-folder junction into the game folder; the seed root and the data root could differ; the unguarded `<filesystem>` in shared code. No ordinary-mode regression from `bcbbddf`, and the existing `test_enhancements` overwrite case fails without it. |
| slice 2b.2 | 2026-10-10 | zero | The cache loader takes `rewrite_in_place`, and setup passes `!game_dir_read_only` (a test-only verification hook allows a well-formed, mismatched-id cache fixture: read-only, untouched; rewrite allowed, rewritten). The audit has a whole-file, string-aware lexer (strings, chars, raw strings, digit separators) and 132 entries; the self-test has 23 mutations, including a writer after a `"/*"` string, a quote char and a raw string, and removed guards (data root, cache flag). Seeding fails closed: listing and inspection errors refuse, including a long-path `SaveGame` that no privilege can bypass; the access-denied case runs where deny rules are effective. `write_refusal` never writes through a link (a junction to the game tree, tested). `latched_game_root()` and the data-root refusal; `home()` is set before the terminate handler. `write_options` uses `std::filesystem::rename` on Windows only and `std::rename` elsewhere. test_fence 63 checks; fence mutations 13 of 14 (the miss is the copy-verification branch). |
| slice 2b.2 review | 2026-10-10 | Astra, nene | nene closed the seed, listing, refusal-link and portability items (hcom 10329; test_fence 65 of 65 with an effective deny rule) and left two non-blocking notes: the skip comment should say the privilege is enabled and print its state, and `write_refusal` with `CREATE_ALWAYS` would follow a file symlink planted at its name. Astra closed the cache, root and terminate items (hcom 10311, 10340; `inbox/2026-10-10-romi-outrun-fence-rereview.md`) and left two audit gaps for 2b approval: read-only flags were searched across all arguments, so `_open(p,_O_RDONLY|mode)`, `s.open(p,std::ios::in|mode)` and `_open("O_RDONLY",mode)` passed; and a call over 12 continuation lines was keyed only through its 13th line. |
| slice 2b.3 | 2026-10-10 | zero | The audit reads only the argument at each function's own mode or flags position, with argument counts checked, a whitelist of read-only descriptor flags and stream modes, and any unknown token a candidate. Every call is followed to its closing parenthesis, structurally on the blanked text, so a call opened after a `)` on the same line is joined too; a call that does not close within 100 lines is `INCOMPLETE` and unclassifiable. The lexer keeps a CRLF inside a raw string aligned. Self-test 34 mutations, including Astra's three flag cases, an approved call changed after 14 continuation lines, a call past the bound, a call that never closes, and read-only controls (`_sopen_s`, `_fsopen`, `freopen` on stdin); `audit_mutations.py` 8 of 8; Astra's reproducer, inverted, catches all its cases. The tree's 131 other keys are unchanged; `O_RDONLY|O_DIRECTORY` (POSIX folder opens in `files.cpp`) is read only. nene's notes: the skip line prints SeBackupPrivilege's state (enabled on this rig), and `write_refusal` refuses a link or folder at its name, removes an ordinary file's name, and creates with `CREATE_NEW` (fixtures: a hard link to a game file, a stale note, a folder, a file symlink, and a hard link planted between check and create through a test-only hook). test_fence 76 checks; audit 133 entries; fence mutations 16 of 17 (the miss is still the copy-verification branch). |
| slice 2b.3 review | 2026-10-10 | Astra, nene | nene: `write_refusal` and its fixtures pass with no findings (hcom 10574; test_fence 78 of 78 with both the access-denied and the file-symlink cases running). Astra: all 12 earlier probes now detected and the long-call gap closed, with one remaining grammar defect (hcom 10566): splitting on `|` lost the difference between bitwise and logical OR, so `_open(path,_O_RDONLY || _O_BINARY)`, which is 1 (`_O_WRONLY`), passed as read only. |
| slice 2b.4 | 2026-10-10 | zero | A flags or mode argument is exempt only when it fully matches names joined by single bitwise ORs, with optional parentheses; `||`, a conditional or any other operator leaves the call a candidate. Self-test 37 mutations (a logical OR in descriptor flags and in a stream mode, flags in a conditional); `audit_mutations.py` 9 of 9, including the 1a2d2e9 behaviour. |
| slice 3 | 2026-10-10 | zero | The DirectInput provider (`windows/shim/dinput_provider.{hpp,cpp}`) on branch `input-shim-slice3`, separate from the 2b candidate (Astra 10108), over fake COM (`windows/tests/shim_fake_com.hpp`). Listing by instance and product text; open by instance with `c_dfDIJoystick2`, background and non-exclusive, the range and dead zone requested and every object checked by offset; axis ranges read back; identity read back from the new handle (`GetDeviceInfo`); every failed open releases its handle. `Poll` and `GetDeviceState` are classified separately, and nothing is copied from a failed read. Each slot has an atomic state (readers in, or closing), so a close is refused during a read. Slots are reused under generation-tagged handles, so a stale handle never reaches a new device. Shutdown waits a bounded time and leaks (never releases) a device still being read, and reports it. No force-feedback call at all. `windows/tests/test_dinput_provider.cpp`: 47 checks, through the core (statuses, epochs, reacquisition and its refusal, rescans, two devices with independent failure, the worker never blocking the tick) and directly (classification, GUIDs, caps, no copy on failure, close during a read, stale handles, shutdown with a read in progress). Provider mutations 9 of 9 caught. |
| slice 3 review | 2026-10-10 | Astra, nene | Changes requested (hcom 10239, 10273; `inbox/2026-10-10-romi-outrun-provider-review.md`): the generation check was separate from reader admission (a stale handle reached a reused slot); a read could touch provider storage freed after a timed-out shutdown; shutdown did not stop admission; `E_PENDING` was treated as a gone device and recreated every tick. |
| slice 3.1 | 2026-10-10 | zero | Atomic admission and publication per slot word; all state shared and held by every call (the provider destroyed and its page fenced mid-read: no fault); shutdown is terminal for new calls and keeps DirectInput for an open in flight; `Call::Pending` with a bounded retry, neutral and rearm, strict in a test run; published enum values. test_dinput_provider 68 checks, with an admission-hook test of the exact close-and-reopen interleaving. Provider mutations 16 of 16 caught. |
| slice 3.1 review | 2026-10-10 | nene, Astra | No blocking findings. nene (hcom 10389, slot words): the protocol is correct, and a stress probe of 20000 reopen cycles found 0 defects against 2caf5ba, while the same probe caught the original ABA in 0785e77. Two requests: a deterministic stale-close fixture, and refusing handle bits above the generation. One note: an open that passed its stop check could publish after shutdown's final pass and return a handle shutdown never counted. Astra (hcom 10429, lifetime, stop and Pending; `artifacts/outrun-provider-review-2caf5ba`): six protected-page lifetime cases, terminal-stop COM exclusion and the Pending rearm all pass. One low Core defect: a pending run starting at clock 0 used 0 as "none", so with a 100 us limit and 101 us later it emitted two ReadPending events and never reacquired. |
| slice 3.2 | 2026-10-10 | zero | Core: an explicit `pending` flag per device (cleared at the epoch start, on a valid read and at the epoch end). Provider: the open publishes and then checks the stop, either taking its slot back or leaving the device to shutdown, which closed and counted it (no second release). Shutdown reads slot words sequentially consistently. A `decode()` refuses handle bits above 40. Test-only open hooks before and after publication. Fixtures: the stale close (nene's window c); high-bit handles (misuse, device untouched); a whole shutdown run inside each hook window (before: shutdown sees a reserved slot, the open releases its device; after: shutdown releases and counts it, the open fails, `after_release` 0); clock-0 pending against a nonzero origin (one ReadPending, epoch end, one reacquisition). test_shim 140, test_dinput_provider 84 (20 consecutive runs clean), test_outrun_adapter 109. Mutations: provider 19 of 19; core 24 of 26, the two misses being the documented unreachable guards; the two epoch-start rearm patterns, ambiguous since slice 3 added the pending rearm, are made unique and caught again. |
| slice 3.2 review | 2026-10-10 | Astra, nene | Both pass at `2da3845`, with no findings. Astra (hcom 10650; `artifacts/outrun-provider-review-2da3845`): zero and nonzero clock origins each emit one ReadPending, expire and reacquire once, plus 30 checks of its own (valid-read reset, Poll/State transitions, the exact timeout boundary, a pending first read in a new epoch). nene (hcom 10718): the slot-word stress (20000 reopen cycles, 0 defects) and a new probe of 2000 random open-versus-shutdown interleavings, in which every created device was released exactly once and every ok handle read Gone after shutdown; the seq_cst publication and stop exclude store-buffer reordering, and exactly one side releases. Slice 2b closed at `fca362e` (Astra 10672). |
| consolidation | 2026-10-10 | zero | Slice 3 (`2da3845`) merged into `input-shim` on top of 2b.4 (`fca362e`), approved as source consolidation only (Astra 10728): no live integration, install, device read or launch. The merged audit has one new finding, the provider header's device `open`, classified not-a-file (134 entries). P20 and P21 and the published enum values stay explicit boundaries. |
