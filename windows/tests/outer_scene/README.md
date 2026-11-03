# Outer start-scene black regions — offline investigation

Task501882, based on the unchanged frozen9a10ace executable D3620377 and attempt-02 images. No game/device/GPU run, build, owner-file write, public push or new live GO is part of this investigation.

## Finding

Missing **WorldCoverage stencil darkening is not a supported cause**. Exposed rear-side scene coverage is the leading explanation, but the retained data cannot distinguish missing/unsubmitted triangles from scene-specific clipping. It does **not** justify calling the black regions expected or defect-free. No renderer, content, default-camera or projection change is proposed as a proven fix.

The smallest justified change now is the pinned pixel reproducer below, correction of the earlier visual description, and this bounded diagnostic proposal. A synthetic triangle or a pixel count cannot prove that the shipped stage has a surface on those rays. There is no render candidate worth another live GO yet.

## Actual frame assertions

`attempt-02.json` pins all12 source PPM hashes and dimensions; copyrighted frames remain in ignored local evidence. `pixel_fixture.py` checks frame identities and3840x720 RGB shape, visible center/adjacent controls, initial black areas, progressive fill, and view-direction bounds. A passing fixture **reproduces and characterizes the gaps**; it is not a repaired-renderer pass.

ROIs in the retained half-resolution image are left `[0,500,420,645)` and right `[3350,390,3840,495)`. They are fixed sample regions, not a claim that every black texel in the full frame is a hole.

| Frame | Left exact-black pixels | Right exact-black pixels |
|---|---:|---:|
| 0 | 40,775 | 20,329 |
| 1 | 39,823 | 19,793 |
| 2 | 37,590 | 19,167 |
| 3 | 33,186 | 17,981 |
| 4 | 26,427 | 16,381 |
| 5 | 16,925 | 14,491 |
| 6 | 4,283 | 12,385 |
| 7 | 793 | 10,187 |
| 8 | 0 | 7,957 |
| 9 | 0 | 5,766 |
| 10 | 0 | 3,644 |
| 11 | 0 | 2,046 |

This corrects the initial first/last visual description: the left ROI is filled byframe8; the right shrinks89.9%. The frames are moving views of the start, not a persistent equal-size black mask.

The log reports64.4° vertical FOV,16:9 panels,32inch/70°/8mm, MatchGameFov. The actual matched eye is approximately316.39mm, not the nominal660mm input. An independent analytic check of `triple_rig.hpp::rig_panels` gives all initial ROI black-pixel directions behind the center eye plane: left absolute yaw104.81–120.84°, right101.27–120.84°. This remains true over the entire64.35–64.45° log-rounding interval and a conservative±2capture-pixel sampling allowance. These are valid side-panel directions. Their being behind center is not itself a clipping failure or a guarantee that the stage contains triangles there.

```powershell
py -3.14 -B windows/tests/outer_scene/pixel_fixture.py `
  --frames build/provider-free/attempt-02/r1/run/frames `
  --out build/outer-scene/pixel-report.json
```

Result:7/7 assertions PASS. The fixture uses installed NumPy/Pillow only for read-only pixel analysis; no desktop/provider/device imports.

## Consumer trace and alternatives

| Hypothesis | What is established | What remains unknown |
|---|---|---|
| (a) Missing WorldCoverage on side panels | `pc_shadow_volume.cpp::darken_4220f0` tags only the native stencil-darkening strip. Its packet has black RGB and maximum alpha0x5f/255, uses SRCALPHA/INVSRCALPHA, and cannot supply missing scenery. `pc_world_coverage.hpp` repeats identity coverage; `triple_bind` disables HUD spacing and invalidates scene constants afterward. Runtime1976coverage draws equals1976AREA displays; WorldScreenEffect1976. Missing this pass would omit darkening, not fill a clear gap. | These counters do not establish every stencil pixel or the next XYZ restore; neither is a reason to add more darkening to black scenery. |
| (b) Projection/fallback/clip bounds | Runtime0projection refusals,0unmatched final programs,0unclassified domains; all21programs classified. Ordinary nodes use the any-panel frusta. Native GPU VS->GS applies `inverse(Pcenter)*Rpanel*Ppanel` before hardware clipping. World CPU triangles defer center depth/W clipping in this candidate; fixed Off/HUD/offscreen paths retain their own behavior. No global side viewport failure is visible: scenery appears beside/above the gaps, including other behind-center rays. | A counter cannot exclude rejection/geometry placement in a particular draw. There is no actual same-frame shaded triangle or depth sample for the gap pixels. |
| (c) Un-rendered geometry exposed by this fixture | Gaps lie solely in the far rear-side field and shrink as the car leaves the start. Historical runs at the same type of extreme matched-FOV fixture found the same outlines and no filled bulk after whole-stage/raw-coverage probes. | Absence of authored content is not proved: triangle-level coverage, per-instance placement and other loaded banks were not completely identified. Do not label the result expected/non-defective or change camera defaults to hide it. |

Source sites: `src/platform/pc_shadow_volume.cpp` (packet initialization and4220F0), `src/platform/pc_world_coverage.hpp`, `xbox/source/d3d11_d3d9.cpp` (`TripleGsMain`, `triple_bind`, `draw_native`), `src/platform/pc_soft_d3d9.cpp::triangle`, `src/platform/pc_render_queue.cpp::node_visible`, and `windows/source/triple_rig.hpp::triple_views`.

Historical context was re-read from hub `artifacts/outrun-perf-20261011/triples-20261012/{README.md,wedge_rays.py,census.py,N1-nodedump/wedge_rays-out.txt,census/census-out.txt}`. RC2 left81.4%/91.9% of the sampled historical wedges unwritten even with whole-stage/raw-state overrides. Historical node spheres intersected rays, but a sphere is not a triangle. The old investigation explicitly left depth/viewport/unknown content unresolved. Those old runs are context, not a fresh measurement of D3620377. The historical census also corrects its displayed stage fromBEAC toPALM; current `world source: Stage/BEAC` is a bootstrap diagnostic label (`game_host.cpp`), not a per-frame displayed-bank witness. No current PALM identity is inferred solely from the old run.

## Smallest next diagnostic, before a behavior fix

The capture contains recorded controls and seven GameInput channels; it has no camera matrices, per-draw PMT/mesh identity or shaded triangles for the PPM time. Source-level correctness plus a final RGB image cannot identify whether an unwritten ray had a triangle to draw. Repeating the historical toggles or changing WorldCoverage would not resolve that missing evidence.

If further engineering is assigned, extend the existing game-owned frame capture for **one** captured frame with a bounded, default-off sidecar: exact frame/present identity, camera projection/view, per-panel corrections/viewport/scissor, and actual submitted world clip-position triangles tagged by draw/PMT/mesh and phase. Use the existing CPU vertex-program decoder as a diagnostic observer, without issuing duplicate GPU draws. Mark any overflow/incomplete record unusable; preserve normal state and release/mute contracts. Then an offline replay can assert ray/triangle intersections and clip-plane outcomes for the exact black ROI pixels. Known intersecting, outside, behind-center, near/far crossing, and empty-geometry cases are the necessary fixture controls. Such a diagnostic still requires its own candidate/build/qualified live grant; it has not been implemented or run here.

If triangles intersect only before a wrong center clip, fix that consumer and assert the exact triangles/pixels offline. If correctly clipped submitted triangles miss those rays, inspect actual bank/instance placement; if the authored stage has no surface there, filling it is a content decision, not a WorldCoverage fix. Keep the fixture and owner acceptance separate in either case.
