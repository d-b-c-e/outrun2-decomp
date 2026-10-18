# Windows alpha candidate: package and qualification

This is the packaging and test route for the private Windows x64 alpha lane.
The integration lead records the selected revision, feature disposition and
acceptance in [ALPHA-CHECKLIST.md](ALPHA-CHECKLIST.md). Creating a ZIP is not
release approval. No public tag or binary release follows from this document.

The starting point is public `a187a9477c976d87de629618bb439a309fdbfed2`.
Its [consolidation record](CONSOLIDATION-VALIDATION.md) reports a Release build
and seven passing focused CTests, with the asset-dependent resource-lifetime
portion skipped. Those results belong to code `ff901274`, not a later alpha.
Boot-to-driving, settings persistence, rendered triples, exit/relaunch and
physical controls remain pending on the actual integrated candidate.

## Freeze one candidate and prepare one build batch

Use a clean private checkout of the integration lead's exact commit. Preserve
existing worktrees, archive heads, owner game folders, saves and settings.
Configure with the existing [Windows guide](../windows/README.md), using a new
build folder, Visual Studio's generator with `-A x64 -T ClangCL`, and the pinned
zlib/Ogg/Vorbis/FFmpeg dependency builds. Do not fetch, rebuild dependencies or
substitute another checkout's EXE implicitly. Record source commit, dependency
pins/hashes, compiler/SDK, configure command and complete build logs privately.

A heavy build needs the coordinator's serialized handoff and existing shared
lease. Build the product and affected tests together. The following is the
baseline batch; add only tests justified by the integration's changed behavior:

```powershell
cmake --build build-alpha --config Release --parallel 4 --target OutRunDesktop test_desktop_controls test_triple_screens test_fence test_telemetry test_fine_steering test_d3d9_resource_lifetime test_fast_load_config test_wheel_rescan test_startup_monitor test_ffb_outrun test_ffb_delivery test_ffb_wiring test_gamepad_vibration test_desktop_profile_writes
ctest --test-dir build-alpha -C Release --output-on-failure -R '^(desktop_controls|triple_screens|test_fence|telemetry|fine_steering|d3d9_resource_lifetime|fast_load_config|wheel_rescan|startup_monitor|ffb_outrun|ffb_delivery|ffb_wiring|gamepad_vibration|desktop_profile_writes|shim_first_loop_timing|perf_route_gate|perf_harness)$'
```

Verify selected tests were found; zero selected tests is not a pass. The FFB
wiring test's fake DLL belongs only in its dedicated test output, never in the
package. Resource-lifetime SKIP 77 without `OR2_EXE` is an explicit asset gap;
repeat its valid-retail-EXE portion in the granted batch if it is needed for
the changed renderer. Preserve failed checks and warnings rather than relabeling
a focused batch as a full suite or a warning-free build.

## Package exact bytes, with no owner data

There was no Windows candidate packager at the starting revision. The small
allowlist tool now packages only the named AMD64 `OutRunDesktop.exe`, this
guide, the repository/component notices, four explicitly supplied dependency
license texts and a manifest. It never scans a game or build directory for
payload files. No INI, save, cache, capture, retail EXE, test executable,
`WheelFfb.dll`, debug symbol or arbitrary extra DLL is admitted.

Run the lightweight synthetic tests without a build or rig lease:

```powershell
python -I -B tools/alpha/test_package_candidate.py
```

After the candidate build, record the actual EXE SHA-256 in its build receipt.
Inspect normal and delay-load PE imports using the installed toolchain (for
example `dumpbin /DEPENDENTS` and `/IMPORTS`). Record Windows/VC runtime
prerequisites and check they exist on the target. If an unexpected non-system
DLL is required, stop and resolve its origin and packaging explicitly; do not
copy every DLL from a build folder. Header checks do not prove dependency closure.
The force-output library is optional and separately qualified; its omission
means this package makes no physical-force delivery claim.

From a clean source checkout, using that recorded hash and dependency license
texts from the exact pinned sources used by the build:

```powershell
$candidateCommit = git rev-parse HEAD
python -I -B tools/alpha/package_candidate.py --source-root . --source-commit $candidateCommit --exe <build-alpha/windows/Release/OutRunDesktop.exe> --exe-sha256 <64-character-build-receipt-hash> --zlib-license <zlib-source/LICENSE> --ogg-license <ogg-source/COPYING> --vorbis-license <vorbis-source/COPYING> --ffmpeg-license <ffmpeg-source/COPYING.LGPLv2.1> --output <new-private-output/candidate.zip>
```

Replace bracketed arguments with actual paths; they are placeholders. Keep the
output outside the source tree or in its ignored build output. The tool refuses
dirty/mismatched source, mismatched EXE bytes, wrong PE architecture/DLLs,
missing notices, reparse/symlink inputs and an existing output archive. It uses
fixed archive metadata and stored entries, then reads every member back. Identical
input bytes and revision produce identical ZIP bytes. The manifest contains
hashes and relative member names, with no local paths, operator profile or times.

The supplied hash is an operator's binding to a separately reviewed build receipt;
the tool cannot prove which source produced an EXE. Archive verification and PE
headers also do not establish executable behavior, complete static-dependency
notices, LGPL relinking/source requirements or public distribution clearance.
Review those against the actual dependency tuple before any public release.
Preserve any failed partial ZIP as evidence; choose a new output for a retry.

## Private install and rollback

1. Verify the ZIP hash against the packaging result, extract into a new staging
   folder, and verify every `candidate-manifest.json` member's size and SHA-256.
   Keep the staged archive immutable. Do not extract over the owner installation.
2. Prepare a new disposable copy of the player's own supported game folder;
   retain the retail `OR2006C2C.EXE` and data privately. Copy only the candidate
   `OutRunDesktop.exe` from staging into that copy. Never replace the retail EXE
   or install a test `WheelFfb.dll`. Record candidate and retail hashes.
3. Ordinary gameplay writes beside the game: `OutRunDesktop.ini`, `options.ini`,
   `SaveGame`, `OR2006C2C.cache`, `runtime.log`, and diagnostics. Preserve an
   independent pre-run copy of the disposable copy's settings/save files plus
   an inventory of absent paths. First-run legacy controls import can create
   `OutRunDesktop.ini.pre-controls-import`. Use fresh settings first; use copied
   owner settings only in a separately identified compatibility case.
4. After testing, release inputs and close only the owned process through the
   existing runner. Confirm process closure, latch cleanup, unchanged owner
   hashes and restored display placement/topology before releasing the lease.
   Retain crash/log/save evidence. A latched run redirects writes and cannot
   prove ordinary-home save/settings behavior by itself.
5. Rollback for a private candidate means resume the untouched previous game
   folder/launcher route. Preserve the new test copy and its evidence; no recursive
   deletion or in-place downgrade is needed. An eventual owner installation needs
   a separate reviewed file receipt/backup; it is not performed by this tool.

## One batched runtime milestone

The coordinator gives fresh owner notice and a serialized runtime handoff. State
visible-window/focus/input use, CPU/GPU occupancy, device access and affected
files before launch. Unattended force/motion and vibration stay muted. Keep the
lease through cleanup; an old queue position or passing build is no runtime grant.

| Gate | Existing route and required observation | Status on this new alpha |
| --- | --- | --- |
| Boot/menu to driving | `windows/tests/perf/run_perf.py` with the existing state-gated `route_gate.py` plan, or the supported `shim_first_loop/run.py` route. Reach GAME with GO and observe motion/speed/heading response to throttle, steering, brake and release; menu arrival or nonzero inputs alone do not pass driving. | Pending |
| Input backend and settings | Offline `desktop_controls`, `wheel_rescan`, force and vibration gates first. In the disposable ordinary home, choose actual supported bindings/settings, save, close and cold relaunch; compare persisted values and unknown-section preservation. Test selected Native/Shim paths explicitly. | Pending |
| Default rendering | Default single view: menu, title/loading, driving, pause/HUD, exit. No crash, corrupt geometry or unintended changes from risky experiments. | Pending |
| Requested triples | Existing bounded `shim_first_loop/run.py --render screens=separate,views=angled,frames=300,every=4,after=420,scale=4 --drive 35`, with required pinned publisher/replay/finalizer inputs. Verify three distinct views, seams, centered HUD/menu, traffic passing into side views and return to Off. Planar/span is a separate named mode, not proof of angled triples. | Pending |
| Save/exit/relaunch | Ordinary-home create/update of the test profile, clean exit, cold launch and successful reload. Hash untouched owner saves; preserve crash/shutdown logs. Repeat input/settings checks only if affected by a failure. | Pending |
| Clean shutdown/restore | Owned PID/birth identity closed, inputs released, test latch removed, files/displays restored, lease actually released. Unknown cleanup retains custody and blocks the next run. | Pending |

Use `shim_first_loop/run.py --check-only` with the exact candidate, fresh result,
test-copy and required tool pins before the granted launch. Its synthetic/replay
input proves only its observed response, not physical wheel feel. Its fixed menu
script and recorded words may need a state route correction before actual driving;
do not repeat an unchanged failed launch. Use the existing observed-license/perf
route where a state-gated path is needed. The old five-minute idle language in
historical runner docs is superseded by current coordinator policy; retain the
owner notice, lease, mute and restoration guards.

Current automation gaps are ordinary-home persistence across two real launches,
brake/release vehicle response beyond input counters, and visual traffic-overtake
acceptance in angled side panels. Existing parsers/fence/math tests do not fill
those gaps. Do not add a second gameplay automation framework: bind observations
from the existing routes to the exact candidate and preserve pending cases.

Report one milestone: source/EXE/ZIP hashes; selected checks with explicit skips;
runtime outcomes and evidence; actual start/end; cleanup/release; unresolved
regressions. The integration lead then decides whether evidence supports a
semi-stable alpha candidate. Publishing remains a separate reviewed step.

The selected stabilization patch also adds a startup diagnostic-environment
line. Once that patch is integrated, require `diagnostics: none` for an ordinary
default run, or only the exact variables declared in the test handoff (including
`DBCE_FFB_MUTE` for a muted run). Unexpected diagnostic switches invalidate a
default-path claim. Do not clear them globally or change the owner's environment;
prepare the owned child environment and record its difference in private evidence.
The formatter's offline test is owned by the stabilization worker; include its
new `startup_diagnostics` CTest in the candidate batch after integration.
