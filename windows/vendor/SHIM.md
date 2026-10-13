# Vendored: the shared input shim

The canonical source is [d-b-c-e/dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit)
`native/shim`, at the commit in `shim.lock.json`. It moved there on 2026-10-13 from this repository's own files
(outrun2-decomp `24dc0f80`, whose blobs it keeps).

The files listed in `shim.lock.json` stay at their old paths under `windows/`. Every CMake target, include root and
test harness therefore builds them unchanged. They are mirrors, not a second copy to maintain:
- ctest `shim_vendor_pin` (`tests/test_shim_vendor_pin.py`) fails if any of them differs from its pinned toolkit blob.
- To change the shim, edit the toolkit, review it there, and then re-vendor:

  ```powershell
  py -3 -I windows/tools/sync_shim.py --toolkit E:/Source/toolkits/dbce-wheel-mod-toolkit --commit <sha>
  ```

  The script writes the exact blobs and rewrites the lock. It reports paths the toolkit dropped; remove those with
  `git rm` after review. Then `git add`, and run `git checkout --` on unchanged files to restore their CRLF working
  copies.

The mirrored folders are `shim/`, `host/` (with `recorder/`), `shim_dll/`, `tests/` (the shim's own tests and fakes)
and `docs/INPUT-SHIM.md`, `docs/CONTROL-JOURNAL.md`.

Game-owned files beside the mirrors stay here and are edited here as usual:
- `shim/wheelffb_bridge.*`, OutRun's WheelFfb read-slot bridge;
- `host/control_publish.*`, `host/observer_mapping.hpp`, `host/README.md`;
- the adapter `source/shim_input.cpp` and `source/outrun_adapter.*`;
- every other test and the renderer.

The controls and session headers the shim uses are this folder's existing toolkit copies, `controls/` and `session/`.
