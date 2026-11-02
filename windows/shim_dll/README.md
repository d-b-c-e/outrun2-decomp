# dbce_shim.dll: the shared input shim for managed game adapters

The reviewed Core and DInputProvider (`../shim`) and ProviderHost (`../host`), behind the
experimental C ABI in `dbce_shim.h` (ABI 2, not a stable-ABI promise; Astra 16982/17050). The first consumer is the
#DRIVE Rally mod (C#, loaded like the toolkit's `WheelFfb.dll`). OutRunDesktop links the same sources directly.

- The game's input thread opens a handle and makes every call on it. Each tick returns one Core frame: the normalized
  action values, statuses and edges, the frame a game adapter consumes. Binding resolution, normalization, rearm and
  availability stay in Core; a managed adapter never re-maps devices.
- Fixed layout: `dbce_shim_frame` is 296 bytes, naturally aligned and asserted at build time. `dbce_shim_frame_size()`
  lets the caller check its own declaration. Results are bounded codes; text goes into caller buffers (truncated,
  terminated, with the needed length).
- Lifetime: `dbce_shim_stop` runs the host's bounded P21 stop on the input thread. A resolved stop frees the host. An
  unresolved one leaves it alive until process exit, never torn down under a running worker. The module pins itself
  at the first open, so no FreeLibrary can unload code a worker may still run.
- Version 1 is ordinary (not latched): no control worker, no capture, never a force effect, non-exclusive reads.
  The next seams are latched test sessions (the existing control worker and journal) and the shared recorder (the
  frame producer with game channels from the adapter).

Build and test from the toolkit root with `pwsh native/shim/build_test.ps1`.
`test_shim_abi` runs the ABI over fake COM and checks the built DLL's exports. `test_shim_bridge` runs the copied-input
bridge: its provider, Core over it, and its exports in memory.

## Declared extras (extension 2, additive)

A game can declare destinations beyond the 31 common actions, such as Truckful's camera look stick or a nitro button.
`dbce_shim_extras.h` declares six exports. The design and its reviews are in the hub's
`artifacts/extra-actions-design-20261014`.

- **Unchanged:** ABI 2, the 296-byte frame, the 31 actions and every existing export. A handle opened without a
  declaration behaves exactly as before.
- **Declaration.** ASCII text of at most 4096 bytes, passed with its length to `dbce_shim_open_extras`:
  - a `game <id>` line;
  - an optional `contexts observed|advisory <names>` line;
  - one `<game>.<name> digital|pedal|centred|stick circle|square [contexts=...]` line per extra.
  At most 32 scalar slots, a stick taking two.
- **Profile.** The same `dbce-controls.ini` carries an `[Extras]` section after `[Controls]`, holding `Schema`, `Game`,
  `Declaration` (the declaration's SHA-256) and an optional `Revision`. Pinned DLLs ignore the section.
  - Each centred component is either one axis, or a `.neg`/`.pos` pair of buttons or hat directions.
  - A conflicting or malformed extra is invalid on its own and never changes the 31.
  - Extras-only devices come after the common device table.
- **One tick.** `dbce_shim_tick_extras(handle, contexts, frame, extras)` reads the devices once for both frames.
  `dbce_shim_tick` refuses an extras handle, and `dbce_shim_extras` only reads the cached frame by seq.
- **Contexts.** An observed context that is not active gives `ContextInactive` (14), and the extra rearms when the
  context returns. Advisory contexts never gate.
- **Status.** `dbce_shim_extras_info` reports each extra's plan, slots and inputs, for F6 and diagnostics.
- **`acceptedSchema`** in `extras_info` is the highest `[Extras]` Schema the open reads. It is 1 through the ABI, so a
  writer emits Schema 1 and no `cal=`; a Schema 2 section is refused as a whole, never half-applied.
- **Scope.** Ordinary input only, on x86 and x64. No latched control, injection or capture of extras yet, so no
  recording claims to hold them.

`test_extras` covers the grammar, the plan and Core's extras tick. `test_shim_abi` covers the exports on both
architectures.

## Accepted raw-input snapshot (extension 3, additive)

An ordinary handle can show its F6 binding editor the raw values Core just read, so a pressed button or a moved axis
can be captured without a second reader (sana 124000, for DRIVE). `dbce_shim_snapshot.h` declares three exports:
`dbce_shim_snapshot_abi_version`, `dbce_shim_snapshot_sizes` and `dbce_shim_snapshot`.

- **Unchanged:** ABI 2, the 296-byte frame, the bridge and extras extensions and every existing export. A DLL without
  the exports offers no snapshot, and the caller reports capture unavailable.
- **Source.** The device reads of the frame the last `dbce_shim_tick` or `dbce_shim_tick_extras` returned: the reads
  that resolved its actions, plus Core's device table. The DLL keeps a pointer to Core's own frame, so ticks copy
  nothing. The getter makes no tick and no device, provider, file or force call.
- **Handles.** Ordinary input only: `dbce_shim_open`, `dbce_shim_open_extras`, or `dbce_shim_open2` without
  `DBCE_SHIM_LATCHED` (a capture-only handle included; its pending row stays the caller's to record). Latched and
  bridge handles get `DBCE_SHIM_REFUSED`. Present on x86 too.
- **Same frame.** `frame_seq` must be the last successfully ticked frame, otherwise `DBCE_SHIM_STALE` and nothing is
  written.
  - A tick that returns `DBCE_SHIM_REFUSED` (Core may have ticked) leaves no snapshot until the next successful tick.
  - A tick refused before Core ticks (`DBCE_SHIM_BAD_ARGUMENT` or `DBCE_SHIM_WRONG_THREAD`) changes nothing: the prior
    frame's snapshot stays byte-identical.
  - The same seq is the same observation: repeated calls are byte-identical.
  - `stale` is Core's verdict at that frame's `boundary_us`, not at the call.
- **Buffers.** The caller sets the header's three struct sizes.
  - When the seq matches, the header gets the frame, the clocks, `fresh_us` and both counts.
  - A short capacity gives `DBCE_SHIM_BAD_ARGUMENT` with neither array written.
  - A table above 64 devices is refused, never truncated.
- **Device rows (192 bytes).**
  - Table index, epoch and flags (`OPENED`, `PATH`).
  - The caps Core read back: axis, range, button and POV masks, and the read-back minimum and maximum. All are zero
    unless the epoch is open.
  - The instance and product GUID text exactly as the profile's `dev=`/`prod=` write them.
  - Names, status text and the interface path stay in `dbce_shim_device_reports`; join on index, epoch and instance.
- **Reads (224 bytes).**
  - Index, epoch, `device_seq` (one per read, rising across epochs) and the read times.
  - Core's call classification (`DBCE_SHIM_CALL_*`), never an HRESULT. Ordinary reads have no source generation.
  - `valid`, `stale` and `ended`.
  - The raw axes, the DirectInput button bytes and the POVs (hundredths of a degree, `0xFFFFFFFF` centred).
  - An invalid read carries the initial values only: axes 0, buttons up, POVs centred.
- **`ended`** comes from the table, never from the calls: the device row no longer has that read's epoch open. A
  pending read past Core's limit ends its epoch while its call still says `PENDING`.
- **Limit.** Only devices the profile binds are read, since Core opens no other. A first device comes through
  extension 4's explicit candidate instead.

`test_shim_snapshot` runs on both architectures with fake COM. It checks:
- zero added device calls across repeated snapshots;
- raw values, caps and identity against the fake's script;
- STALE handling and all-or-nothing buffers;
- a pending read, the pending timeout that ends an epoch, an unplug, and the reopens with new epochs;
- Core's stale verdict, and an extras handle.

On x64 it also checks a capture handle and the latched and bridge refusals. With the DLL path it checks the three
exports.

## First-device capture candidate (extension 4, additive)

Extension 3 can only offer devices the profile binds, so an all-unbound profile gives an in-game editor nothing to
capture. Extension 4 lets the editor bind a first device without external setup (sana 213587). It reads one explicitly
selected controller beside Core, through the same provider and tick. `dbce_shim_candidate.h` declares six exports:

- `dbce_shim_candidate_abi_version`
- `dbce_shim_candidate_list`
- `dbce_shim_candidate_select`
- `dbce_shim_candidate_release`
- `dbce_shim_candidate_report`
- `dbce_shim_candidate_snapshot`

- **Unchanged:** ABI 2 and its frame, extensions 1 to 3 and every existing export.
  - Core's device table stays the profile's, and Core never resolves an action from the candidate.
  - Extension 3's rows and counts never include it, so a strict extension 3 consumer is unaffected.
  - The candidate never enters the journal or the recorder.
- **Explicit.** No listing or open happens before `dbce_shim_candidate_list` or `dbce_shim_candidate_select`. Both only
  queue a request for the host's existing provider worker (`shim/candidate.hpp`, serviced from the same worker loop as
  `Core::service_devices`). The listing and the open results reach the owner thread at its next tick.
- **One reader.** The worker lists and opens through the host's own provider, never a force front. The DirectInput
  provider opens background and non-exclusive, and makes no force-feedback call.
  - The open is checked like Core's: attached, the product as listed, the identity read back from the new handle, and
    unreadable ranges marked.
  - The owner thread reads an open candidate once per successful ordinary tick, after Core.
- **Selection.** One candidate at a time; a new select replaces it. Malformed GUID text gives `BAD_ARGUMENT`. An
  instance in Core's table gives `REFUSED`, because extension 3 already offers it. `why` names the reason.
- **Report.** The report is JSON as of the last tick:
  - the listing: `seq`, `pending`, and the devices with instance, product, name and a `bound` flag;
  - the candidate: `state` (none, opening, open, lost or refused), identity, interface path, epoch, `opened` and the
    reason.
- **Snapshot.** The snapshot uses extension 3's structs and capacity rules for the tick that returned `frame_seq`;
  any other seq gives `STALE`.
  - Zero or one device row, at index 0, with `OPENED`, `PATH` and the masks and ranges only while open.
  - Zero or one read: the epoch, `device_seq`, both calls, `valid`, `stale` (always 0, since it was read this frame),
    `ended` and the raw state.
  - `boundary_us` is the read's end.
  - The getter makes no device call and reads no clock.
- **Lifetime.** Any of these releases the candidate:
  - `dbce_shim_candidate_release`;
  - a read that ends its epoch: gone, acquisition lost, or pending past 1 s. There is no reacquisition; it reports
    `lost` until selected again;
  - 2 s of ticks with no matching `dbce_shim_candidate_snapshot` call;
  - `dbce_shim_stop`, through the existing provider shutdown, which releases any handle still open.

  The worker closes released handles, and a late open result of a replaced or released selection is closed unused.
- **Handles.** Ordinary only, as extension 3: latched and bridge handles get `REFUSED`. Owner thread only. Present on
  x86 too.

`test_shim_candidate` runs on both architectures. It checks:
- the pure `Candidate` over the scripted provider: inert passes, the listing, the refusals, the open and its raw read,
  one read per tick, the 2 s bound, replacement, early and late release, loss without reacquisition, the pending limit,
  and the worker's own refusals (detached, wrong product, an identity change during the open);
- the C ABI over fake COM: nothing listed or opened for an all-unbound profile until asked, owner-thread and GUID
  refusals, the row and read against the fake's script, no Core sample or action and no extension 3 row from the
  candidate, a release, an unplug, a stop with the candidate open (no force call, DirectInput released), and a bound
  profile's refusal and `bound` flag with a second device as the candidate.

On x64 it also checks the latched and bridge refusals. With the DLL path it checks the six exports.

## x86 build: ordinary only

`pwsh native/shim/build_test.ps1 -Arch x86` builds `build/x86-mt/dbce_shim.dll` for 32-bit games, such as Horizon
Chase Turbo on Unity's Mono 2.0. It uses the same clang-cl for `i686-pc-windows-msvc` under vcvars32.

It serves the ordinary path only: Core, the DirectInput provider and ProviderHost, opened with `dbce_shim_open`. The
control journal, its observer transport (`observer_transport.hpp`, qualified for x64 only, assert unchanged) and the
frame recorder are not compiled in (`shim/ordinary_only.hpp`, `DBCE_SHIM_ORDINARY_ONLY`). The x64 build is unchanged.

| | x86 |
| --- | --- |
| Exports | The same 20 names, undecorated `__cdecl`. An x86 caller must declare every function as cdecl; in C# that is `[UnmanagedFunctionPointer(CallingConvention.Cdecl)]`, because a delegate defaults to StdCall. |
| `dbce_shim_open`, `tick`, `start_report`, `device_reports`, `profile_info`, `profile_action_info`, `stop` | As on x64. |
| `dbce_shim_open2` | Always `DBCE_SHIM_REFUSED`, with the reason in `why`, before any DirectInput is created. |
| `dbce_shim_bridge_open` | Always `DBCE_SHIM_REFUSED`, with the reason. With no bridge handle, the other bridge calls refuse every handle (`DBCE_SHIM_BAD_ARGUMENT`), as a direct handle does on x64. |
| `dbce_shim_record` | `DBCE_SHIM_REFUSED` for every live handle. |
| `dbce_shim_open_args` | 80 bytes, which `dbce_shim_open_args_size()` reports. The 128-byte struct is x64's; nothing on x86 accepts it. |
| `dbce_shim_frame`, `dbce_shim_action`, bridge structs | Identical to x64 (296, 8, 144 and 232 bytes, with the same offsets). |
| `dbce_shim_snapshot` (extension 3) | As on x64, for ordinary handles. Its header, device and read structs are 56, 192 and 224 bytes, with the same offsets. |
| `dbce_shim_candidate_*` (extension 4) | As on x64, for ordinary handles, with extension 3's structs. |

`test_shim`, `test_dinput_provider` and `test_provider_host` run their ordinary cases on x86. `test_shim_abi` runs
the ordinary cases plus the x86 refusals, and checks the built DLL's exports, its `open_args` size and its open2
refusal. The receipt (`build/x86-mt/build-receipt.json`) records `arch` and `ordinaryOnly`, for a reviewed pin.

## Pure profile inspection (optional export)

`dbce_shim_profile_action_info(profile, len, action, text, len, needed)` (since `5b96ba8`) reads one action's
configured binding from exact profile bytes, using Core's parser and binding admission (`plan_profile`). It has no
handle, host, provider, file or device. The handle-based `dbce_shim_profile_info` is unchanged. Profiles are read at
controls schema 1, as by the DLL's hosts, so a Schema 2 profile is reported invalid. The identity it returns is
configured, not evidence of connected hardware (`../docs/INPUT-SHIM-PROFILE-INSPECTION.md`).

## Copied-input bridge (extension 1, additive)

The same DLL also serves a consumer that already owns its device handle and copies each read in. Art's native reader
(the toolkit's `WheelFfb.dll`) is the first such consumer. `dbce_shim_bridge.h` declares seven exports:

- `dbce_shim_bridge_abi_version`, `dbce_shim_bridge_device_size` and `dbce_shim_bridge_sample_size`;
- `dbce_shim_bridge_open`;
- `dbce_shim_bridge_devices` and `dbce_shim_bridge_devices_with_paths`;
- `dbce_shim_bridge_tick`.

The design and its checks are in `../docs/COPIED-INPUT-BRIDGE.md` (copied from the OutRun branch; see `../PROVENANCE.md`).

- **Unchanged:** base ABI 2, the frame, the open arguments and every existing export. A bridge handle refuses
  `dbce_shim_tick`, and a direct handle refuses the bridge calls.
- **Flags:**
  - `DBCE_SHIM_LATCHED` (since `f38b855`) takes the base ABI's verified fences and process force mute. It writes the
    same control journal with `inputSource: "external-physical-input@1"` on every row. Copied input stays physical
    evidence; only accepted control batches are injected.
  - `DBCE_SHIM_CAPTURE` is refused: the capture format has no external sample provenance.
  - Any other flag is a bad argument.
- **Journal reader:** `../tests/control_journal` builds the production journal sink and reads it back with the strict
  reader, which accepts a header `inputSource` only with that exact value on every row.
- **No device calls:** `shim/bridge_provider` holds memory leases only. It makes no DirectInput or COM call and never
  reacquires; the external owner recovers its device.
- **Device adoption:** the bridge goes through the same Core as DirectInput, including the absent-device rescan.
  - A device batch published after an empty scan is adopted at the next scheduled scan
    (`CoreConfig::missing_rescan_us`, 250 ms by default). That is a scheduling delay, not a wall-clock guarantee.
  - A lost handle is closed and looked for again at once.
  - Freshness uses the source's original read time, not the copy time.

## Opening is asynchronous: tick before admitting input

Successful `dbce_shim_open` or `dbce_shim_open2` creates the host; it does **not** mean a
device is open or that its identity has been adopted. The worker opens devices, then a
`dbce_shim_tick` on the owning input thread adopts those handles into Core. Reading
`dbce_shim_device_reports` only reads the current snapshot. Waiting and polling reports
without ticking cannot complete this handoff.

A game adapter's startup sequence is:

1. Open on its input thread, verify Core's profile result, and enter an opening state.
2. On each input update, call `dbce_shim_tick` on that same thread and check its result.
   Keep game-facing shim output neutral while opening, including any virtual-pad
   output; do not block the game thread waiting for the device worker.
3. Read device reports **after** the tick. Validate the required owned devices and their
   current identities, complete the consumer's isolation checks, then admit input.
   An opened device still does not make every action Live: honor Core's action statuses
   and neutral/rearm rules. An empty interface path is still unavailable identity.
4. Bound opening with a consumer-defined deadline. Stop on the owning thread on timeout,
   native failure or a changed game/input context; report the specific reason. Keep the
   window, profile and game-state checks active throughout opening, not just at Open.

An unopened initial report is expected, not proof that a connected wheel is absent.
This affected iRacing's immediate post-Open ownership check (2026-10-13); the correction
belongs in ordinary startup as well as diagnostics. The contract is the same for both
Open calls; test injection must remain a separate, explicitly gated capability.

Existing source fixtures demonstrate the sequence without hardware:
`native/shim/tests/test_dinput_provider.cpp::test_path_epochs` publishes a worker-opened handle and
checks that only the next Core tick adopts its path;
`native/shim/tests/test_shim_abi.cpp::test_lifecycle` observes an empty initial report, ticks until
ordinary input is Live, then observes the owned device. These fixtures establish the
native contract, not a consumer's in-game qualification.

## Device report interface identity

`dbce_shim_device_reports` adds `interfacePath` to each JSON device row. It is the exact UTF-8
`DIPROP_GUIDANDPATH` value read from the provider's existing owned DirectInput handle; no extra device is opened or
acquired to obtain it. The fixed C ABI structs, frame layout, action list and ABI version stay unchanged.

- Before the first input tick adopts an open, and whenever that epoch is missing or read-failed, the string is empty.
  Call after `dbce_shim_tick` to observe adopted device state; the report immediately after open is only a startup snapshot.
- An absent, failed, unterminated or invalid UTF-16 property query produces an empty string. Legacy input still works.
  Empty means identity unavailable, never permission to match by name, product alone, enumeration index or a GUID
  belonging to another input API.
- Successful reacquisition of the same owned handle carries its path into the new epoch. A closed handle's path is
  discarded; a new open replaces it even when the new property query returns no path.
- Rewired's Windows RawInput instance GUID differs from DirectInput's. Consumers can join the live path and product to
  the exact RawInput extension path/product, using their existing `\\?\` / `\??\` path-prefix normalization. Require a
  unique match and do not merge native input until isolation is established. Do not retain a path from an older epoch.

`test_shim`, `test_dinput_provider` and `test_shim_abi` cover worker publication, loss/reopen, same-handle reacquisition,
failed metadata reads, UTF-8 preservation and JSON escaping. COM counters prove one device open and no effect calls.
