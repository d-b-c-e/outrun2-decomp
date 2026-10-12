# dbce_shim.dll: the shared input shim for managed game adapters

The reviewed Core and DInputProvider (`windows/shim`) and ProviderHost (`windows/host`), behind the
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

Build: the `dbce_shim` target in `windows/CMakeLists.txt` (with the Xbox/PC configure, `-DOR2_XBOX=ON`). Test:
`test_shim_abi` (CTest `shim_abi`) runs the ABI over fake COM and checks the built DLL's exports.

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
