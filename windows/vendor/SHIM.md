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

## 2026-11-02 camera-action source refresh

The established sync script advances d26776a to reviewed toolkit02c2547461832ed64096045730f52603cc5713f3 (66 exact mirrors). The intervening canonical optional extras/snapshot/candidate DLL sources and their tests/docs are included with their existing opt-in behavior; OutRun supplies no new declaration/backend. Game-owned WheelFfb bridge/control-publish/observer files were verified byte-unchanged. No installed DLL or managed consumer repin is implied.

Core42/observer3 preserves original31 action indexes, reads old observer2 and wholly absent recording tails, and rejects partial camera extensions. DLL ABI2 stays31/296 and old capture channels, skipping unsupported camera bindings without opening their devices or refusing shared profiles. Native CMake builds the vendored camera_actions/main.cpp against its existing include roots/fixtures as camera_contract_compat; the mirrored standalone toolkit harness/generator retain their original toolkit layout assumptions. See docs/DIRECT-CAMERA-ACTIONS.md for native consumer qualification and open physical gates.
