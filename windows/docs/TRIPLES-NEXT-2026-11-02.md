# Next projected-triples work — November 2, 2026

This is a source-based implementation plan for the native Decomp, not a renderer acceptance result. The abandoned Tweaks mod is not a delivery target. Its `docs/ABANDONED-FOR-DECOMP.md` preserves useful camera/layout findings, but its RVA hooks, injected DLL and historical rig frames do not qualify this port.

## What already exists

| Concern | Native consumer | Established source behavior |
|---|---|---|
| Camera state | `src/driving/pc_camera.cpp`, `camera_project_484bd0` | Builds perspective from camera +A0 FOV, +B8 aspect, +BC/+C0 near/far, +C4/+C8 shifts, then advances the view via `camera_view_482f20` and writes +140 / inverse +1C0. |
| Camera/frame lifecycle | `src/platform/pc_scene_renderer.cpp`, `note_camera_replay`, `scene_effect_projection`, `render_frame` | Owns stateful camera interpolation, projection save/restore and display callbacks. Re-running the entire frame per panel is unsafe: display callbacks and particle randomness are stateful. |
| Rig math | `windows/source/triple_rig.hpp`, `triple_views`, `triple_apply` | Builds homogeneous clip corrections `inverse(Pcenter) * Rpanel * Ppanel` in camera view space. This already carries camera pitch/roll through the center view; do not port the Tweaks Euler `TurnInView` helper or duplicate its gimbal-lock risk. |
| Native GPU path | `xbox/source/d3d11_d3d9.cpp`, `TripleGsMain`, `triple_bind`, `triple_native_standard` | Replicates each admitted triangle into three viewports, undoing/reapplying D3D9 half-pixel fix. Back-buffer standard/affine projections are classified; custom/unmatched draws fall back to center; non-backbuffer targets stay single. |
| UI/window geometry | `windows/source/window_desktop.cpp`; `src/runtime/game_host.cpp` | The selected row spans one window. Angled keeps center 16:9 and widens legacy screen culling; `ui_rect_` keeps RHW content on center. No new display topology switch is needed in source. |
| Traffic visibility | `src/platform/race_traffic.cpp`, `triple_outer_edges`, `triple_sphere_hidden` | Has an angled-view visibility route beyond the center-eye near test, with existing numerical cases in `windows/tests/test_triple_screens.cpp`. |
| Ordinary scene nodes | `src/platform/pc_render_queue.cpp`, node visibility / 404Cxx path | Still rejects bounding spheres against the center camera near and side planes. Widening `g_pc_view_half_width` does not remove this center near-plane rejection. |

The old mod's camera-at-tick and GetViewport observations identify things to check, not addresses to hook in the port. Its legacy +A0/+B8/+C4 field meanings are reflected by the native functions above. Native viewport/scissor state and the render queue must be checked directly; there is no reason to add an original-EXE D3D9 wrapper here.

## Smallest next source increment

Keep the existing one-simulation-frame / GPU-replication design. Start with **ordinary scene-node culling**, not a second rendering architecture or default flip:

1. Trace the node's sphere into center-camera view space and enumerate every center near/side-plane rejection feeding scene admission. Use the same panel projection/rig inputs as `g_pc_triple_corrections`; do not reconstruct a different seat or assume a single wide frustum covers more than 180 degrees.
2. For Angled only, admit an object if its bound intersects any panel frustum. Preserve Off/Planar exactly. Reuse/extend the native traffic/rig geometry where its coordinate/near/far conventions agree; do not assume the traffic outer-edge approximation handles all node bounds and LOD cases without proof.
3. Add actual-consumer fixture cases: a node behind the center eye but inside a side panel survives; outside every panel is culled; at the near-plane boundary it does not pop; Off/Planar output is unchanged; non-finite/singular geometry follows the existing safe fallback. Test pitched/rolled camera transforms and both pixel-center fixes using the existing direct-matrix equivalence cases. Keep LOD computation valid when center depth is behind the eye: use a defined visible-panel depth, not a negative center-depth shortcut.
4. Inventory stage culling, reflections/shadows, custom vertex programs, flare and RHW particles. GPU replication cannot recover a primitive already removed by CPU culling, and center-only fallback counters must remain honest. Do not attribute the historical start-grid wedges to this finding without an actual image/counter experiment.

Offline checks to reuse: `test_triple_screens` / CTest `triple_screens`, `test_camera_render_binding` / `camera_render_binding`, and `test_screen_edge_cull` / `screen_edge_cull`. Build named targets; `ALL_BUILD` currently includes an unrelated Switch compile target needing `arpa/inet.h` on Windows. Target dependencies may build the large native runtime; keep any competing performance run serialized and retain the resulting build cache.

## Framing, curve and runtime sequence

After visibility is correct, adopt current STD-041/042/046-049 (the November 2 portfolio camera roadmap): always corrected presentation; Game/Realistic sets per mounted camera; chase Game FOV plus its saved offset; normal F7 switches saved framing sets and Shift+F7 is session-only developer comparison. Current `[Triple] Views = Planar/Angled` and `MatchGameFov` are legacy controls and must be migrated deliberately, preserving explicit owner tuning. No default or owner-INI change is made by this plan. For the actual native camera types, use the settled starting sets on triples (cockpit Realistic, bonnet Game, bumper Realistic, chase Game), preserve explicit custom tuning, and keep single-screen framing game-owned. Resolve native camera identity/presets before assuming all those camera names exist. Tweaks are offsets on calculated starts; numpad 5 resets, + zooms in, - zooms out, and overlays identify camera/set/custom state. Car-model-derived placement and smoothing/near-clip values are Art-calibrated starting points to qualify on this game, not constants to transplant blindly.

Curve is a separate increment. Use the shared native geometry/compositor, with EyeDistanceMm measured to the plane through the center panel edges (STD-047: no sagitta subtraction), and the measured-eye/source-eye mapping for virtual Game FOV. Keep one borderless spanning window for both Surround and separate monitors; future N-display slicing is a separate planned capability, not a reason to add per-display windows. Establish an explicit scene-complete / HUD boundary and offscreen world source first. Merely warping Present, or assuming the first RHW draw is the HUD (particles also use RHW), would warp UI or omit later world draws. The logical BackBuffer classification in `triple_bind` must remain correct if physical world rendering moves to another target.

Before a future live slot: freeze the exact package and dependencies, make a disposable runtime and fresh owner-state backups, force output Off through the qualified test route, then request GO through guna. Compare Planar versus Angled at identical resolution, camera, scene and motion. Record actual replicated draw/fallback counters, source frames, window placement, fresh game-only images and CPU/GPU cost. Qualify Surround and separate monitors separately; preserve HUD/menu/viewport restoration and run cleanup immediately. No game was launched for this plan, and no rig or device acceptance follows from static math or fixture passes.

## Offline baseline collected in this worktree

`ctest --test-dir build-xbox-pc -C Release --output-on-failure -R '^(outrun_adapter|triple_screens)$' passed 2/2 after building only those named targets. `triple_screens` reports 1,153 checks. This establishes current numerical/contract baseline only; no new visibility fix, rendered views, performance or hardware acceptance was tested. The full Windows suite was not rerun. Private log: `work/validation-20261102/adapter-triples.log`.
