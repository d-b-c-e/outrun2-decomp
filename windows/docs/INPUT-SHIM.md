# Input shim: architecture

**Draft r1, 2026-10-10. Author zero (Claude). Reviewer Astra (romi). For review, not adopted.**

This is the design for a shared input path: one portfolio-owned profile per game, shared code that reads the devices
and resolves the bindings, and a small game adapter that hands standard actions to the game. OutRun 2006 (decomp)
is the reference adapter. The design is decomp-local and opt-in until it has been reviewed and shown to work again
in a second existing game. Then the core moves to the toolkit with a version and pinned hashes (Astra 8815). No
standard changes until then; section 12 lists the amendments promotion would need.

Owner direction, 2026-10-10: "design a shim for each game where we can have standard input, mapping, and
configuration that mostly sits outside the game's native methods (e.g. leaving its config files mostly alone)". Since
this could become the foundation for every game, it is reviewed in rounds before the core is built. The review log is
at the end.

## 1. Why

Today each game gets a Wheelkit storage adapter that writes the game's own binding store (STD-033 item 2). Most of
the cost is in STD-035's sixteen writer rules: foreign file formats, product-keyed tables, cloud saves, kept bindings
that collide, the consumer's required set, bounded parsers for binary stores. Every new game pays it again, and the
game's own config can still drift underneath.

The shim moves the binding out of the game:

- Wheelkit writes one file per game, in the toolkit's own `ini-controls-1` form.
- The runtime only reads it.
- Shared code resolves it against the devices.
- The adapter writes actions, not bindings, into the game.

The game's saved mappings are never written.

## 2. Goals and non-goals

Goals:

- One profile format, one writer (Wheelkit), one reader, for every shim game.
- Every binding read from its own device (STD-036 by construction).
- Test injection, the observer and recording implemented once, in the core. Adapters do not reimplement them.
- An adapter small enough to review in one sitting: about 150 lines for OutRun.

Non-goals:

- Replacing a game's physics, menus or input semantics. The adapter feeds the highest input level that keeps the
  game's own processing; OutRun's 453860 dead zone and quantisation still run, for example.
- OS-level input, virtual-device drivers or focus changes. vJoy poisoned DirectInput on this rig, and ViGEm is
  retired.
- A stable binary ABI in r1 (Astra 8815). This is a source seam first.
- Deterministic physics replay (Astra 8823).

## 3. Terms

| Term | Meaning |
|---|---|
| Profile | The `[Controls]` section, `ini-controls-1` Schema 1, unchanged from `controls-contract.md`. |
| Device source | Code that enumerates, opens and reads physical devices. DirectInput first. |
| Epoch | One open of one device instance. It ends at close, a failed read or a disconnect. |
| ActionFrame | One tick's result: every contract action's value, availability and edges. |
| Adapter | The game-specific code: what the game supports, frame to game input, game state, fences. |
| Test latch | Set for the life of the process as soon as a test session is requested (section 9). |

## 4. Layers

```
dbce-controls.ini ─► Profile reader ─┐
                                     ▼
devices ─► Device source ─► test channel ─► Resolver ─► ActionFrame ─► Adapter ─► game input
   (one epoch per open)   (raw samples, before     │                    │
                           any binding resolves)   │                    ├─► game state signals
                                                   ▼                    ▼
                                  Recorder queue ◄─┴────────────────────┤
                                  Observer (shared memory) ◄────────────┘
```

**Core**, game-agnostic:

- profile reader;
- device sources;
- test channel and test latch;
- resolver and ActionFrame;
- start report;
- observer;
- recorder.

**Adapter**, per game:

- capability declaration;
- frame sink and native-input merge;
- state signals;
- fences;
- its own extra report lines.

The seam is a C++17 source interface (sections 6 to 8). The core has no game headers. The adapter uses the core
through that interface only.

## 5. Profile file and discovery

- **File:** `dbce-controls.ini` beside the game executable (Astra 8815). It holds `[Controls]` exactly as
  `controls-contract.md` section 5 defines it, Schema 1. The core ignores other sections, so later metadata can sit
  there.
- **Writer:** Wheelkit only. It replaces the whole file atomically (a uniquely named temporary sibling, then
  `ReplaceFileW` with a backup name, or a no-replace `MoveFileExW` when the file is new). A reader therefore sees the
  old file or the new one, never a mix. The runtime never writes it, with the one exception in Q1.
- **Reader:**
  - Read once at start, in one bounded read: 64 KiB, strict UTF-8 with an optional BOM.
  - A second `[Controls]` section or a duplicate action key refuses the whole profile and names both lines.
  - Anything else follows the contract: an invalid line is reported and skipped; an unknown key is reported.
- **Role** (contract section 5a): the canonical binding store for this game's wheel controls, and the only store the
  shim reads. An omitted action means the shim supplies nothing for it. The game's native input (pad, keyboard)
  follows the adapter's merge policy (section 8).
- **Absent file:** nothing is bound. The start line says `controls: nothing applied via ini-controls-1: no
  dbce-controls.ini`, and the game runs on its native input.
- **The game's own files are never written by the core.** For OutRun that means `OutRunDesktop.ini`,
  `OutRun2006Tweaks.ini`, `options.ini` and the saves.
- **Discovery:** `<exe stem>.product.json` beside the executable (Astra 6295). For OutRun it is
  `OutRunDesktop.product.json`, copied from `windows/product.json` at build or deploy. It declares:
  - `productId`;
  - `controlsStore: "shim-ini-controls-1"` plus the profile file name;
  - the shim core version and the adapter id;
  - the injection folder and the observer name and version.

  Wheelkit requires the marker plus the executable identity before it offers to write.
- **Legacy import:** the Tweaks `[DirectInput]` keys turned into a `dbce-controls.ini`. It runs once, never replaces
  an existing `dbce-controls.ini` and never modifies its sources (Astra 8815). Who triggers it is Q1. The mapping
  itself (Tweaks button to action, axis scaling, POV numbers, shifter precedence) is already written and
  unit-checked in `controls_store.cpp`. It moves to shared fixture vectors either way, so a Wheelkit implementation
  would run the same cases.

## 6. Device source

```cpp
struct AxisCaps { bool present; long min, max; };          // the range read back after the open request
struct ObjectCaps { AxisCaps axis[8]; std::bitset<128> button; bool pov[4]; };
struct DeviceInfo { std::string dev, prod, name; unsigned type; bool forceFeedback; };
class DeviceSource {                                        // one per API; DirectInput first
public:
    virtual std::vector<DeviceInfo> enumerate() = 0;        // attached now
    virtual OpenResult open(const DeviceInfo&) = 0;          // identity checked before and after; caps; a new epoch
    virtual ReadResult read(Epoch, RawState&) = 0;           // one coherent snapshot, or failed (the epoch ends)
    virtual void close(Epoch) = 0;
};
```

- **Identity** (STD-035 item 6):
  - The instance GUID matches, and the product GUID equals the binding's `prod`.
  - Both are checked in the listing and again after the open (`GetDeviceInfo`).
  - A product mismatch is a status (section 7), never a fallback to another instance.
- **Objects:**
  - After `SetDataFormat(c_dfDIJoystick2)`, every object is checked by its DIJOYSTATE2 offset: `GetObjectInfo` for
    existence, and for axes also `GetProperty(DIPROP_RANGE)` after the 0..65535 request (Astra 6313-1).
  - An axis whose range cannot be read, or whose max is not above its min, is unqualified.
  - Object counts are never turned into contiguous indices: a wheel with three axes may have X, Z and Rz.
  - Raw axis samples are rescaled from the range read back, as `controls-contract.md` section 3 allows.
  - The injection range function returns the same read-back values.
- **Epochs** (STD-035 item 7):
  - A failed read ends the epoch, and that device's outputs are released in the same tick: axes to rest, digital up
    with one up edge.
  - A device comes back only by enumerating by identity again, as a new epoch.
  - Running test samples never cross epochs.
- **Reuse** (Astra 8815: reuse robust lifecycle code where feasible). Lift WheelFfb's proven reader parts into this
  source:
  - the per-offset range readback;
  - POV low-word centring, with invalid angles neutral;
  - one coherent snapshot per read;
  - the read-through-the-force-handle rule for the wheel the force path owns.

  Do not wrap the WheelFfb DLL itself: it is process-global, uses index-based slots and couples to its force state.
  This is Q4.
- **A fake source for tests** scripts listings, opens and reads:
  - range refusal and unreadable ranges;
  - missing objects;
  - twin products;
  - an identity change during the open;
  - failed reads and disconnects mid-hold.
- **Later sources:** SDL, opened by USB identity as Slipstream does, plus XInput and HID. Contract bindings are
  DirectInput objects today, so each new source needs a declared object mapping. That is out of scope for r1.

## 7. Resolver, statuses and ActionFrame

Each profile entry gets exactly one status. It is reported per action and counted in the start line:

| Status | Meaning | Start line group |
|---|---|---|
| `applied` | the game supports it and its object was verified on its device | applied |
| `unsupported` | the adapter declares the game has no such action (with the reason) | unsupported |
| `invalid` | the contract parser refused the line (with the reason) | invalid |
| `device-missing` | no attached device has the instance | unavailable *(new group)* |
| `product-mismatch` | the instance exists with another product | unavailable |
| `object-missing` | the device has no such axis, button or hat | unavailable |
| `range-unqualified` | the axis range is unreadable or empty | unavailable |

The start line, emitted once the devices have been checked (Astra 6313-2: no syntactically applied phantom
objects):

```
controls: profile "<p>" revision <r> via shim-ini-controls-1: applied n (ids); unsupported m (ids); invalid k (ids); unavailable u (ids)
```

It is followed by one line per non-applied action naming its status and reason. The added `unavailable` group
is an extension of the STD-033 line, and Wheelkit's reader must accept it (Q11).

```cpp
struct ActionValue { float value; bool available; bool injected; };   // steer -1..1, pedals 0..1, digital 0 / 1
struct ActionFrame {
    std::uint64_t seq;            // one per resolve
    std::int64_t  readUs;         // QueryPerformanceCounter microseconds at the device reads (monotonic)
    ActionValue   action[ActionCount];  // fixed index per contract action id
    std::uint64_t down, up;       // digital edges against the previous frame, one bit per digital action
    std::uint32_t batch;          // the last accepted test batch, 0 none
    std::uint32_t epochEvents;    // a device epoch began or ended in this frame
};
```

- Normalization is exactly the contract's (`dbce_controls.hpp`, shared vectors).
- An unavailable action reads its rest value (steer 0, pedal 0, digital up). A held digital action whose device
  goes away produces one up edge (neutral release).
- Every action reads its own device, so a second device needs no merging (STD-036).
- An object bound to two actions is reported. It is refused only when the adapter declares the pair exclusive.
  Wheelkit's writer already refuses collisions (STD-035 item 3).

## 8. Adapter contract

An adapter:

1. **Declares** for every contract action whether the game supports it, or why not. Those reasons are the
   `unsupported` group.
2. **Ticks** the core exactly once per game input sample, on the game's input thread, and consumes that frame there.
   This is a pull model: reads happen at the game's input point, which adds no latency or thread hop, and recording
   and observation share that clock.
3. **Merges** with native input according to a per-action policy it declares: override, max-merge or native-only.
   - It guarantees **no duplicate consumer**: one physical device never reaches the game through two paths.
   - Where the game can also read the wheel natively, the adapter closes that path, by overriding after the game
     resolves its own input or by hiding the device from it. It never does this by rewriting the game's saved
     bindings.
4. **Releases to neutral** on stop, scene change and disconnect. No input stays held, and each release gives one
   edge.
5. **Supplies state signals:** named numeric channels with units for the observer and recorder (speed, gear, the
   game's consumed steering). It names what it cannot observe, and states the frame offset of each signal, e.g.
   "consumed as of the previous tick".
6. **Implements the fences**, keyed on the core's test latch. They are required before any injected run (Astra
   8815):
   - saves, results and scores go to a run copy or are refused;
   - network and LAN are off;
   - option writes are refused;
   - no force path exists (section 10).
7. **Reports** its fence states at start, next to the controls start line.

## 9. Test channel, observer, recorder

- **Session:** `inject.on` and `inject.txt` under `%LOCALAPPDATA%\dbce\<productId>\`, per contract section 6,
  STD-034 item 5 and STD-035 item 9.
  - Raw samples are delivered inside device-source reads, before the resolver. A test therefore exercises the
    profile, the resolver and the adapter.
  - `inject action` resolves through the applied bindings.
- **Latch, at process start:**
  - It is set as soon as `inject.on` is seen at start, before any other check, whether the session then arms or is
    refused. It is never cleared, not at expiry or after a refusal (Astra review, item 4).
  - It is decided before the game initializes its saves or network, so adapter fences are in place before the first
    write.
  - For OutRun that means in the desktop entry point, before the game host starts, not at the first input sample as
    in 35a8aa3.
- **Strict session file:**
  - exactly one `nonce=` and one `expires=`;
  - unknown keys and duplicates are refused;
  - strict UTF-8, bounded.
- **Batches:**
  - Each `inject.txt` version is one batch.
  - It is parsed and validated whole first: size, count, grammar, bindings and table capacity, against a scratch
    table.
  - A valid batch then replaces every running sample.
  - A refused batch drops every running sample (fail closed) and logs why.
  - Nothing is ever partially accepted (Astra review, item 4). Replace versus add is Q3.
- **Observer:**
  - `Local\Dbce<Product>InjectObserve-<nonce>`, created by this process or refused. Seqlock, versioned, with its
    layout documented next to the product (Astra 6295: keep the decomp's own name and version).
  - It carries:
    - frame `seq` and `readUs`;
    - the batch id;
    - per-action injected flags;
    - the raw samples of bound axes;
    - the adapter's sink values and state signals, each with the frame seq it belongs to.
  - The clock join is explicit. A harness matches a batch to the first frame that carries it, then to the state
    signal published for that frame's seq.
- **Recorder:**
  - It uses the toolkit's `native/session/session_writer.h` (`dbce.wheel.session` version 1), pinned by commit and
    hash (Astra 8823).
  - One worker thread owns the writer.
  - The input thread pushes copied snapshots into a bounded single-producer queue, with their original timestamps.
  - Overflow counts drops and records the gap. Samples are never manufactured.
  - Channels: the raw bound objects, ActionFrame values, adapter state signals.
  - Off unless a recording session is requested (Q9).
- **Replay:** raw replay feeds the device source and exercises the bindings. Action replay feeds the adapter. Neither
  claims deterministic physics. Pose replay stays game-specific.

## 10. Force feedback (later, but it shapes r1)

- When a game gets force feedback, the core owns the wheel's device handle. Force needs an exclusive acquire, and the
  wheel is then read through that same handle, as WheelFfb does. The device source therefore has a mode for "opened
  by the force path, read through its handle".
- The test latch means no force path is ever created (STD-034 item 4). The environment mute `DBCE_FFB_MUTE` is the
  second interlock (STD-035 item 8). The force model itself (`native/forcemodel`) is unchanged.

## 11. Interception classes across the portfolio

| Class | How the adapter reaches the game | Game config | Duplicate-consumer guard | Portfolio examples |
|---|---|---|---|---|
| **A: source or mod-owned sink** | writes actions into the game's input variables | untouched | our build has no other wheel path | OutRun decomp, Daytona and F-Zero recomps; BepInEx mods that already read devices (art of rally, Woden, Horizon Chase Turbo, iRacing Arcade) |
| **B: engine action override** | replaces engine-level action values after native resolution (Rewired, Unity Input System, InControl) | untouched | per-action override, never added; device hidden from the engine if needed | #DRIVE Rally, Karting Superstars |
| **C: in-process virtual device** | a proxy presents a fixed virtual controller whose objects are the actions | bound once to the virtual device, or the game's default for it | the proxy hides the physical devices | DirectInput: V-Rally 4, Rush Rally 3, DiRT/GRID, Gravel; SDL: CannonBall, Slipstream |
| D: out of process | companion process to a virtual pad | — | — | Sonic CrossWorlds today; not a shim target |

- Class A is the reference (this adapter).
- Class C is the boldest claim. It holds only for a game proven to take the virtual device without a native-store
  write per profile.
- Class A in Unity needs the core from C#. That means either a C ABI (deferred by Astra 8815) or the managed twin of
  the contract (`Dbce.Wheel.Input`). This is the main portability risk (Q5, Q10).

## 12. Standards impact (on promotion only)

- **STD-033 item 2** ("each game keeps one store; never a second competing store"). In a shim game,
  `dbce-controls.ini` is the one store for wheel controls. The native store stays as the game left it, but must not
  be a competing path for the same devices, and section 8 item 3 is what makes that true. Amendment needed: "a game
  may keep its controls in the shim store instead of its native store when its adapter proves no duplicate
  consumer".
- **STD-035:** for shim games the writer rules shrink to one format and one writer. Items 6 to 10 (runtime
  injection) are implemented once in the core.
- **STD-034:** unchanged. Configured is the core's start line; observed is game state after raw injection.
- **STD-036:** met by construction.
- **STD-030 (F6 panel):** the controls page of a shim game would edit `dbce-controls.ini`, which clashes with
  "Wheelkit owns writes" (Q2).

## 13. OutRun 2006 reference adapter

- **Tick:** `xbox_runtime::input_filter`, after the pad adapter, once per 453BB0 input sample.
- **Sink** (game units):
  - steer becomes record word 0x0E (`lround(n * 32767)`, full range; the game's 453860 then applies its own 0.0078
    dead zone and -127..127 quantisation);
  - throttle and brake become 0x0D and 0x0C (`lround(n * 255)`), max-merged with the pad;
  - `pc_input_layer = false` whenever any of the three is available. An unavailable channel keeps the pad's record
    value, so pedals work without the steering device (Astra review, item 1);
  - buttons become the `buttons_04` bits, menu edges and frontend features, as in 35a8aa3.
- **H-pattern:**
  - Shift pulses go toward the held gate's gear, reading car word +208.
  - One pulse every other tick; it stops after four pulses without a change.
  - The game has no neutral or reverse.
- **Coexistence:**
  - The Xbox core reads only `Windows.Gaming.Input` Gamepads (`platform_api.cpp`). Wheels and the DS-8X are not
    WGI Gamepads, so there is no second wheel path. At the rig, verify this with a start-up line listing WGI
    gamepads.
  - Pad and keyboard keep working.
- **State signals:**
  - mode and in-gameplay;
  - speed (car +1C4);
  - gear (+208);
  - the consumed 7D6810 steer, accelerator and brake (as of the previous tick);
  - the record values written.
- **Fences under the latch:**
  - the game's writable state normally lives in the game folder (`application_home`: cache, saves, options and logs).
    Under the latch, a run folder under the injection folder takes the saves and the options:
    - `diag.save_directory` (xbox `main.cpp`, now `home + "/SaveGame"`) goes to the run folder, seeded with a copy of
      the owner's SaveGame so menus behave normally;
    - the `options.ini` path (`main.cpp` 106 and 308) goes to a run copy;
    - the EXE image cache (`exe_setup_prepare`) stays where it is. It is derived and deterministic, and it is named in
      the fence report;
    - `runtime.log` stays (it is the evidence);
  - the LAN platform is not installed (`native_network_set_platform` skipped), so the game is offline;
  - the build has no force path.
- **Unsupported, with reasons:**
  - clutch, handbrake, neutral and reverse: not in the game;
  - gear7 and gear8;
  - select;
  - lookBack, reset and horn: not mapped yet;
  - modMenu: until the F6 panel;
  - muteFfb: until force feedback.

## 14. Open questions for review

| # | Question | Proposal |
|---|---|---|
| Q1 | Who triggers the legacy import? | Runtime, only when `dbce-controls.ini` is absent and a Tweaks `[DirectInput]` exists. It creates the file through a temporary file and a no-replace move, and the log names it. Alternative: Wheelkit runs the same vectors and writes the file itself, so the runtime never writes. |
| Q2 | F6 panel and Wheelkit both editing controls | F6 shows the profile read-only, plus live capture that hands bindings to Wheelkit. Alternative: F6 writes through the same atomic protocol with a revision check. |
| Q3 | A new batch: replace or add to running samples? A refused batch: drop all or keep? | Replace; drop all (fail closed). |
| Q4 | Device lifecycle reuse | Lift WheelFfb's reader parts into a source-level DirectInput source with a fake twin; don't wrap the DLL. |
| Q5 | The second game for promotion | One that tests portability, not the same class: a class-B (Rewired) or a C++ class-C proxy game rather than another recomp. Astra's call, DRIVE-aware. |
| Q6 | Steering sensitivity and dead zone | Game options, applied by the adapter, read from the game's own settings, for now. A core-level shaping block would need a contract change. |
| Q7 | Live profile reload on a new revision | Not in r1: read at start only. |
| Q8 | Hot-plug | Reopen by identity on device arrival, as a new epoch, and report it. Bounded rescan while a bound device is missing. |
| Q9 | How recording is requested | A `record.on` session file beside `inject.on`, with its own bounds. |
| Q10 | When the C ABI comes | After the second game shows what crosses the seam. |
| Q11 | The `unavailable` group in the start line | Accept it in Wheelkit's reader and keep it separate from `unsupported`. |

## 15. Review log

| Round | Date | By | Change |
|---|---|---|---|
| r1 | 2026-10-10 | zero | First draft. It works within Astra's constraints (8795, 8815, 8823) and addresses the 35a8aa3 findings (6313 and the inbox review). |
