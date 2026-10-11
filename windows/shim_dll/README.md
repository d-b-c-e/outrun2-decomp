# dbce_shim.dll: the shared input shim for managed game adapters

The reviewed Core and DInputProvider (`windows/shim`) and ProviderHost (`windows/host`), unchanged, behind the
experimental C ABI in `dbce_shim.h` (ABI 1, not a stable-ABI promise; Astra 16982/17050). The first consumer is the
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
