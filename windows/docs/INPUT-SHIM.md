# Input shim: architecture

**Draft r2, 2026-10-10. Author zero (Claude). Reviewer Astra (romi). For review, not adopted.**

r2 answers Astra's r1 review (`dbce-project-mgmt/inbox/2026-10-10-romi-input-shim-r1-review.md`): ten findings,
cited below as R1-F1 to R1-F10, and the decisions on Q1-Q11. It also folds in the author's own notes. Section 18
logs the rounds. Section 17 is the first proof matrix: what the fake provider and the OutRun adapter must
demonstrate before anything runs on the rig.

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

If the core fails to start (DirectInput unavailable, or an unreadable profile), the game runs on its own input and
never crashes. The adapter treats an absent core as "nothing available" and reports it.

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
```

| Thread | Does | Never does |
|---|---|---|
| Game input thread | `tick()`: adopt the latest epoch table, read open epochs, deliver injection, resolve, run the adapter sink, publish the observer, enqueue one recorder snapshot. | Enumerate, open, close, rescan or touch files. |
| Provider worker | Initial enumeration and opens, hot-plug rescans (bounded per pass), closes after the input thread lets go. | Read samples for frames. |
| Recorder worker | Owns `session_writer`, drains the queue. | Run in any game callback. |

Reads can block in some drivers, so there is no "adds no latency" claim (R1-F5). Every sample records how long its
read took and its age at resolution, and the status counts reads over 2 ms. An epoch is handed to the input thread as
an immutable table generation. A close is requested by the worker, acknowledged by the input thread at its next tick,
and only then released. A COM device object is never used by two threads at once. Whether DirectInput accepts a
device opened on the worker and read on the input thread is confirmed live before the design is final (section 17,
L1).

## 5. Profile file, writer transaction, import, discovery

**File.** `dbce-controls.ini` beside the game executable. It holds `[Controls]` exactly as `controls-contract.md`
section 5 defines, Schema 1, plus any other sections and keys, which every writer preserves (R1-F6).

**Reader.** Read once at start in one bounded read (64 KiB), strict UTF-8 with an optional BOM.
- A second `[Controls]` or a duplicate action key refuses the whole profile and names both lines.
- An invalid binding line is reported and skipped, per the contract.
- A missing file binds nothing. The game runs on native input and the status says why.
- r1 and r2 read at start only. Edits apply at the next launch (R1 Q7). A future live reload must swap the whole
  profile atomically and then follow the rearm rules (section 6).

**Writer transaction**, the same for Wheelkit (C#) and F6 (C++), with shared fixture vectors (R1-F6, Q2):
1. Read the file's bytes and hash them (SHA-256): this is the base.
2. Refuse when the file declares a format version this writer cannot preserve.
3. Build the new bytes: replace only `[Controls]`. Every other section, unknown keys inside `[Controls]`, comments,
   the BOM and line endings stay byte for byte.
4. Write a uniquely named temporary sibling, flush it, and read it back to compare.
5. Re-read the destination. If its hash is no longer the base, refuse with a conflict, so the other frontend's edit
   is never lost.
6. Replace with `ReplaceFileW` and a backup name, so that on any failure the original is restorable. When the file is
   new, create it with a no-replace `MoveFileExW`. Then verify the destination bytes, remove the backup and report the
   new hash.
7. The fixture vectors include a conflict, a forced replacement failure (the destination held open), preserved
   unknown sections, a CRLF file with a BOM, and a missing file.

`Revision` stays Wheelkit's profile content hash (contract section 5). The status reports the file hash too, so a
reader can tell which bytes were applied.

**Legacy import** (Tweaks `[DirectInput]` to `dbce-controls.ini`) is **explicit** (R1 Q1):
- It is started from Wheelkit, or from a standalone opt-in importer that runs the same vectors.
- It shows a preview first: the proposed bytes, every note, and every Tweaks key that is not carried over.
- It is all-or-nothing and no-clobber: it creates the file only when none exists, through step 6.
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

Proven reader behavior is reused behind this boundary (R1 Q4). Copying WheelFfb's reader code would not share its
handle, so the design never claims that it does.

**Shutdown order:**
1. The adapter stops ticking.
2. A test run ends: owned actions go neutral and a last frame is published.
3. The recorder stops, with a bounded flush (1 s). Past the deadline the capture is incomplete.
4. Force leases stop and unload their effects gracefully. A force process is never hard-killed.
5. Read leases are unacquired and released.
6. The worker is joined, and DirectInput is released.

**Identity** (STD-035 item 6). The instance GUID must match and the product GUID must equal the binding's `prod`. Both
are checked in the listing and again after the open (`GetDeviceInfo`). A product mismatch is a status, never a
fallback to another instance.

**Objects.** After `SetDataFormat(c_dfDIJoystick2)`, every bound object is checked by its DIJOYSTATE2 offset:
- existence with `GetObjectInfo`;
- for axes, the range with `GetProperty(DIPROP_RANGE)` after the 0..65535 request (R1 finding on 35a8aa3).

An unreadable or empty range makes the axis unqualified. Object counts are never turned into contiguous indices.
Samples are rescaled from the range read back, and the injection range function returns the same values. This is
configuration evidence, not proof of acquisition, a successful read or an observed action (R1-F8).

**Epochs** (STD-035 item 7, R1-F5):
- A failed read ends the epoch at once (N2). Its owned actions are neutral in the same frame, and each held digital
  action gets one up edge.
- The worker reopens only the exact identity, as a new epoch.
- Rescans run on device-arrival notifications, plus every 2 s while a bound device is missing. Each pass is bounded
  to one enumeration and at most four opens.

**Rearm at every epoch start**, the first open and every reopen (R1-F5, N1):
- **Digital** actions: an object held at the epoch's first valid read is masked until it has been read released once.
  A reconnect never manufactures a press.
- **Pedal** actions (`throttle`, `brake`, `clutch`, `handbrake`): neutral until the normalized value has been at or
  below 0.05 once in the epoch. A held pedal never resumes throttle.
- **`steer`**: live at once. It is an absolute position, and holding it at centre would be wrong.
- Each action reports `rearming` until it is ready. A test run never continues across an epoch change (section 9).

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
    ActionStatus status;             // live, rearming, unavailable(reason), unsupported, invalid, unbound
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

- **Provenance** (R1-F1). Every action names its source sample. Every sample names its device, epoch and sequence,
  plus the batch and content hash of exactly the objects that carried a delivered value. A later physical read
  carries no mark, so the observer can never tag it with the last batch. Acceptance, refusal, delivery failure, epoch
  begin and end (with the device and the reason), drops and run-state changes are separate events (section 9). They
  are not frame fields.
- **Normalization** is exactly the contract's (`dbce_controls.hpp`, shared vectors).
- **Statuses:**

| Status | Meaning | Start-line group |
|---|---|---|
| `live` | applied, object verified, epoch open, rearmed | applied |
| `rearming` | applied, waiting for release or rest (section 6) | applied |
| `unsupported` | the adapter declares the game has no such action, with the reason | unsupported |
| `invalid` | the contract parser refused the line, with the reason | invalid |
| `device-missing` / `product-mismatch` / `object-missing` / `range-unqualified` / `opening` | configured but not available, with the reason | unavailable |
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
   - Unrelated native input (keyboard, pad) is preserved.
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

**States** (R1-F2):

```
NONE ──(inject.on present at start)──► LATCHED ──(admission passes)──► RUNNING ──► COMPLETED | EXPIRED
                                          │                               │
                                          └───────(any failure)───────────┴──────► FAILED
```

- **LATCHED** is set at process start, in the entry point, before the game initializes saves or network. It is set
  whatever the session file contains: refused, stale and expired files latch too, and the latch lasts for the
  process.
  - From LATCHED on, owned actions are neutral except for injected samples during RUNNING.
  - The fences stay in force until exit.
  - A latched process never returns owned devices to physical control.
  - Owner trap: a stale `inject.on` disables the wheel. Mitigations: a loud start line (`TEST PROCESS: ... wheel input
    disabled; saves redirected`), Wheelkit warning that the switch is present, and the harness's cleanup removing it
    (STD-034 item 7). See N4.
- **Admission**, all required:
  - a strict session file: exactly one `nonce=` (8-64 alphanumeric) and one `expires=` (at most 1 h ahead), no
    unknown or duplicate keys, strict UTF-8, at most 512 B;
  - Schema 1 with at least one applied action;
  - every adapter fence `verified`;
  - no force path (section 11).

  Otherwise FAILED.
- **Batches.** Each version of `inject.txt` is one batch:
  - it starts with `nonce=<session>` and `batch=<n>`, where `n` is strictly greater than the last accepted batch
    (N5); then at most 32 commands; at most 4096 B;
  - it is validated whole against a scratch injection table: grammar, bindings, objects, ranges and capacity;
  - a valid batch replaces every running sample atomically, and the acceptance event carries the session, `n`, the
    content hash (SHA-256 of the bytes), the command count and the time;
  - the same content under the same `n` is a duplicate. It is reported and ignored (R1 Q3).
- **FAILED** is terminal for the process. Triggers:
  - a refused batch;
  - an unreadable, oversize or wrong-nonce command file;
  - a delivery failure of an injected object (unreadable range, missing object, failed read);
  - an epoch end on any device the run has injected into.

  Running samples are dropped and owned actions are neutral (one up edge each). The fences stay and the event is
  logged. A new file never rearms the process. Physical control never returns, not even for the axis the injection
  was holding.
- **COMPLETED** happens on a batch whose only command is `end`. **EXPIRED** happens at the session expiry. Both drop
  running samples and leave owned actions neutral for the rest of the process.
- **During RUNNING**, objects not injected read the real device, as `controls-contract.md` section 6 says. The idle
  gate keeps the rig unattended (N3).
- **Stimuli.** Qualification uses `inject raw` built independently from the intended profile's raw objects
  (STD-034 item 3). `inject action` resolves through the applied mapping and is for navigation only. Each acceptance
  event records which kinds a batch used.
- **Observer v2:** `Local\Dbce<Product>InjectObserve-<nonce>`, created by this process or refused. Seqlock,
  versioned, with its layout documented beside the product (`windows/docs/OBSERVER.md`). It carries:
  - run state, latch and fence states;
  - the last frame: seq, tick time, and per action its value, status, injected flag and source sample;
  - that frame's sample table: device, epoch, seq, read end, validity and the injection mark;
  - the adapter's state signals, each with its frame seq and offset;
  - an event ring of 64 entries with an increasing event sequence, so a reader detects overrun by a gap.

  The join is explicit: batch to the first sample that carries its mark, to the frame that used that sample, to the
  state signals published for that frame seq.

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

- Force uses the force lease (section 6). OutRun has none yet.
- A latched process creates no force path (STD-034 item 4). The environment mute `DBCE_FFB_MUTE` is the second
  interlock (STD-035 item 8). Both must cover the stock game's producers and those of other mods in the process, not
  only the shim's own.
- No-force qualification mutes delivery. It does not delete the producer. Force calculation and telemetry stay
  available with delivery muted.

## 12. Status and reporting

R1-F8, Q11.

- **Human start line,** once the provider has checked the devices:
  `controls: profile "<p>" revision <r> via shim-ini-controls-1: applied n (ids); unsupported m (ids); invalid k
  (ids); unavailable u (ids)`. One line follows per non-live action, with its status and reason. The provider token
  `shim-ini-controls-1` is new, and a reader that knows only `ini-controls-1` must treat it as unknown, which means
  not configured. Astra tests this against Wheelkit's reader at its pinned commit: an unknown provider or status must
  fail closed and never keep a green badge.
- **Structured status:** `controls-status {"v":1,...}` JSON lines in `runtime.log`, written at start and on every
  change. They carry:
  - per action: id, status, reason, device and epoch;
  - per device: identity, name, status, epoch and the objects verified;
  - the profile revision and file hash;
  - the core and adapter versions;
  - the run state and the fences;
  - read-timing counters.

  Whether a status file is also needed is N6.
- **Evidence stays apart** (STD-033 item 4). Verified objects are configuration evidence. Live status is
  availability. Neither is an observed game response.

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
| WGI `Gamepad` | `xbox/source/platform_api.cpp:24` | wheels and the DS-8X are not expected to be Gamepads | Verified at run time: at start, log every WGI Gamepad and RawGameController with VID/PID. If an owned identity appears as a Gamepad, `read_pad` drops it before the pad adapter, and the adapter reports it. |
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
- Neutral and reverse are `unsupported` (the game has none). Forward-gear chasing never stands in for them.

**State signals:**
- mode and in-gameplay;
- speed (car +1C4, game units);
- gear (+208);
- the consumed 7D6810 steer, accelerator and brake (offset: the previous tick);
- the record words and buttons written (offset: this tick).

**Fences under the latch** (R1-F9). Each has an admission self-check: a writer that resolves outside the run folder
refuses admission. The run folder is `%LOCALAPPDATA%\dbce\outrun2006-decomp\run-<nonce>\`.

| Writer | Source | Under the latch | Self-check |
|---|---|---|---|
| Saves, licences, results, ghosts | `diag.save_directory` (`xbox/source/main.cpp:195`), from which `frontend_save_directory` comes; `license_owners->persistence` (`game_host.cpp:1155`), `race_end_runtime.cpp:933/1154` and `race_ghosts_runtime.cpp:72` all derive from it | redirected to the run folder, seeded from a copy of the owner's SaveGame | the effective `frontend_save_directory` equals the run folder |
| `options.ini` | `main.cpp:106`, `:308`; `gpu_backend.cpp:31` writes it | redirected to a run copy | the effective options path is in the run folder |
| `crash.txt` | `main.cpp:177` | redirected | path check |
| `runtime.log` (stdout) | window entry point | redirected to the run folder, so the owner's log is untouched | path check |
| `trace.txt`, `replay.bin` | `game_host.cpp:91`, `:117` (development hooks) | refused (hooks disabled) | the hooks are off |
| EXE image cache | `exe_setup_prepare` | kept: derived and deterministic, declared in the fence report | declared |
| LAN | `native_network_set_platform` (`main.cpp:168`) | not installed: offline | the platform pointer is null |
| Force | no producer in the decomp (no `CreateEffect` or vibration in `src`, `xbox/source`, `windows/source`; WGI `Gamepad.Vibration` never set) | n/a | grep evidence in the receipt |

The audit is repeated whenever upstream changes are merged. Any new writer either joins the table or refuses
admission.

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
| Q2 | One writer transaction for both frontends, one file, revision and hash checks. No mandatory Wheelkit. |
| Q3 | Whole-batch replace with identity and deduplication. An invalid batch fails the run and leaves owned actions neutral. |
| Q4 | Reuse proven behavior behind an explicit provider boundary. No promise of shared force handles. |
| Q5 | Second game: DRIVE (class B), after its current qualification, as an opt-in isolated spike. Class C is research only. |
| Q6 | Game and adapter shaping as now. Record the effective values and avoid a double dead zone. Any canonical shaping needs versioning. |
| Q7 | Start-only. A future reload swaps the whole profile and then rearms. |
| Q8 | Exact identity, bounded rescan, new epoch, neutral rearm, no silent test continuation. |
| Q9 | Audit Wheelkit's existing session lifecycle first. Ordinary recording and test runs stay distinct. |
| Q10 | No stable ABI before the second adapter. An experimental bridge is allowed; a second resolver is not. |
| Q11 | Versioned structured status plus a compatible line. Old readers fail closed. Unavailable stays separate. |

**Open for round 2:**

| # | Question | Proposal |
|---|---|---|
| N1 | Rearm thresholds | Digital: read released once. Pedal: normalized value at or below 0.05 once. Steer: live at once. |
| N2 | Is every failed read an epoch end, including a transient `DIERR_INPUTLOST`? | Yes: end at once, then reopen through the worker (the safe reading of STD-035 item 7). |
| N3 | Do non-injected objects of owned devices stay physical during RUNNING? | Yes, per contract section 6, with the idle gate. The alternative is to neutralize every non-injected owned action during a run. |
| N4 | A stale or expired `inject.on` latches and disables the wheel | Accept it (fail closed), with a loud start line, a Wheelkit warning and harness cleanup. |
| N5 | Batch identity | `batch=<n>` required after the nonce, strictly increasing; content hash in the acceptance event. |
| N6 | Structured status channel | JSON lines in `runtime.log` first. A status file only if Wheelkit needs live availability. |
| N7 | Provider token | `shim-ini-controls-1`, so old readers fail closed. |
| N8 | Seeding the run folder's SaveGame | A copy of the owner's SaveGame, so menus behave as usual. The alternative is empty, which shows first-run screens. |

## 17. First proof matrix

Levels:
- **U**: a unit test against the fake provider (no devices, no game);
- **A**: an adapter fixture (OutRun sink and state machine on fake frames and a fake car word);
- **W**: a Wheelkit-side test (Astra);
- **L**: a live injected run under the lease and idle gate, after a source PASS;
- **O**: the owner at the wheel.

Nothing at L or O runs before U and A pass and the source is reviewed.

| # | Requirement | Proof | Level |
|---|---|---|---|
| P1 | Identity checked in the listing and after the open; a product mismatch is a status | fake listing with twins and a mismatched product; an identity change between listing and open | U |
| P2 | Objects verified by offset; non-contiguous axes | fake device with X, Z and Rz only; a binding on Y gives `object-missing` | U |
| P3 | Range read back per axis; unqualified axes refused; rescale from the read-back range | fake ranges: refused request, unreadable, empty, -32768..32767 | U |
| P4 | A failed read ends the epoch, owned actions neutral in the same frame, one up edge | fake read failure while a button and a pedal are held | U |
| P5 | A reopen gives a new epoch; held digital masked until released; pedal neutral until at rest; steer live | fake reconnect with the button held and the pedal at 0.8 | U |
| P6 | The worker never blocks the tick; close handoff | fake slow enumeration and open; tick timing; close requested during a tick | U |
| P7 | Statuses and start line after the device checks; the `unavailable` group | profile spanning every status | U |
| P8 | Structured status JSON on start and on change | as P7, plus P4 and P5 transitions | U |
| P9 | Old readers fail closed on `shim-ini-controls-1` | Wheelkit reader at its pinned commit against fixture log lines | W |
| P10 | Writer transaction: preservation, conflict, replacement failure, BOM and CRLF, new file | shared fixture vectors run by the C++ writer (F6) and the C# writer | U, W |
| P11 | Import: explicit, preview, all-or-nothing, no-clobber, sources untouched | owner Redux values (35a8aa3 vectors), an existing target refused, a failure midway | U |
| P12 | Latch at start, whatever the session file says; never cleared | missing, stale, malformed and valid `inject.on` | U |
| P13 | Admission requires strict session, Schema 1, every fence verified, no force | fake adapter with one unverified fence | U |
| P14 | Batch validated whole; replace atomic; duplicate ignored; `batch=<n>` monotonic | 33 commands; a bad 5th line; the same content twice; a lower `n` | U |
| P15 | Refused batch, delivery failure or epoch end means FAILED: neutral, terminal, no rearm on new files | each trigger, then a valid file | U |
| P16 | `end` gives COMPLETED; expiry gives EXPIRED; owned actions neutral afterwards; physical input never returns | fake clock | U |
| P17 | Provenance: an action names its sample, a sample names its epoch and seq and the batch of injected objects only; no mark on a later physical read | injected steer, then expiry, then a physical read | U |
| P18 | Observer join: batch to sample to frame to state signal; ring overrun detected | fake adapter signals; 70 events | U |
| P19 | Recorder: one producer, bounded queue, overflow makes the capture incomplete, flush deadline, metadata pins | fake slow writer; validated by the managed `SessionReader` | U |
| P20 | OutRun sink: steer word, pedals max-merged with the pad, pedals without steering, button bits and edges | fake frames into `NativeInputState` | A |
| P21 | OutRun H-pattern: pulse, release, deadline, retries, ambiguous gates, unavailable gear, impossible gear | fake car gear word responding late, never or twice | A |
| P22 | OutRun fences self-check: save, options, crash, log in the run folder; LAN off; hooks off | admission fixture on the host configuration | A |
| P23 | No duplicate consumer: WGI Gamepad and RawGameController listing at start | live start-up log on the rig, read only | L |
| P24 | Threading: a device opened on the worker and read on the input thread | live read-only probe, no game input | L |
| P25 | Closed loop (STD-034): staged wrong bindings, Wheelkit Apply, raw stimuli, game words move, exact restore | Astra's harness | L |
| P26 | Multi-device: steering and pedals from the R12, gears from the DS-8X, each from its own device | P25 with DS-8X raw stimuli | L |
| P27 | The owner drives it | at the rig | O |

## 18. Review log

| Round | Date | By | Change |
|---|---|---|---|
| r1 | 2026-10-10 | zero | First draft (`6a53b95`), within Astra's constraints 8795, 8815 and 8823 and addressing the 35a8aa3 findings. |
| r1 review | 2026-10-10 | Astra | Ten findings plus answers to Q1-Q11 (`inbox/2026-10-10-romi-input-shim-r1-review.md`, hcom 8993). |
| r2 | 2026-10-10 | zero | Every finding addressed: per-sample provenance and an event ring (F1); a terminal FAILED state with owned actions neutral and no rearm (F2); interception before the consumer, per-API audit, no after-the-fact duplicate claims (F3); one provider with read and force leases, a bridge provider and a shutdown order (F4); a thread model, read timing, rearm rules and bounded rescans (F5); one writer transaction for F6 and Wheelkit, unknown content preserved, explicit import (F6); one producer, emission versus source time, bounds, metadata, lifecycle audit first (F7); versioned structured status, old readers failing closed, STD-036 needing evidence (F8); fence self-checks gating admission and the OutRun writer audit table (F9); an H-pattern state machine on a monotonic clock (F10). Added the author's notes (enumeration off the input thread, window, background policy, one state-signal seam, core failure means native input), questions N1-N8 and the first proof matrix. |
