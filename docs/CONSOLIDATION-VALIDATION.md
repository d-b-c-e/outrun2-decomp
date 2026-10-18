# Consolidation validation — October 18, 2026

Code revision: `ff901274c4e9d92123c9fd6f8008f76829071164`.
Subsequent publication edits are documentation only.

## Build

A fresh, isolated Windows x64 Release build of `OutRunDesktop` succeeded using
Visual Studio 2022 Build Tools, ClangCL 19.1.5 and Windows SDK 10.0.26100.0.
The existing pinned zlib/Vorbis/FFmpeg dependency builds were reused read-only.
No build cache or executable from another source checkout was substituted.

Configure with the command in [windows/README.md](../windows/README.md), using
`build-review` as the build directory and your dependency paths. Then:

```powershell
cmake --build build-review --config Release --target OutRunDesktop test_triple_screens test_fence test_desktop_controls test_telemetry test_fine_steering test_d3d9_resource_lifetime --parallel 4
ctest --test-dir build-review -C Release --output-on-failure -R '^(triple_screens|test_fence|desktop_controls|telemetry|fine_steering|d3d9_resource_lifetime|perf_route_gate|perf_harness)$'
```

The build produced deprecation, unused-code and indentation warnings in existing
source; it was not warning-free. There were no build errors.

Local executable SHA-256 (not uploaded as a release):
`A632A634A70E00153ABBEF5F51A6C3AC516F083325496E30C39F8ED9794E95DE`.

## Results

| Check | Result |
| --- | --- |
| Desktop Release compile/link | PASS |
| `desktop_controls` | PASS |
| `telemetry` | PASS |
| `fine_steering` | PASS |
| `triple_screens` | PASS |
| `test_fence` | PASS |
| `perf_harness` | PASS |
| `perf_route_gate` | PASS |
| `d3d9_resource_lifetime`, no `OR2_EXE` | Expected SKIP (77), after asset-independent checks; no null crash |
| Resource-lifetime test with a deliberately nonexistent EXE | Expected FAIL (1), with loader diagnostic; not silently skipped |
| `python -I -B windows/tests/perf/test_route_gate.py` | All cases passed |
| `python -I -B windows/tests/shim_first_loop/test_run_timing.py` | Zero failures |

CTest selected eight cases: seven passed and one explicitly skipped, zero
unexpected failures (10 seconds). This is a focused consolidation check, not the
entire repository test suite. The valid-retail-EXE portion of the resource test
was not rerun. No game was launched, no rendered scene or physical device was
tested, and other platform builds were not repeated.

## Publication audit

- Refreshed `origin` and `upstream`; upstream `e1e1c59` is an ancestor.
- Audited 107 worktrees: no uncommitted files at the initial snapshot. Existing
  worktrees, branches and installed games were preserved.
- Compared side branches using patch identity, application checks and current
  source, avoiding duplicate or superseded changes.
- Inspected 428 newly unpublished blob versions: no binary payloads, game
  executable/archive/image candidates or common credential signatures found.
- Preserved original local-only ancestry through the consolidated branch and
  13 archive heads; see [BRANCH-INVENTORY.md](BRANCH-INVENTORY.md).

Build logs and local executables remain in the ignored `build-review` directory.
Public evidence here describes exactly what ran; historical roadmap receipts do
not imply this combined candidate passed new gameplay or physical acceptance.
