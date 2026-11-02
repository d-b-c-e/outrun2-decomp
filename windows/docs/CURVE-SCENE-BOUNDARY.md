# Curved scene boundary groundwork — November 2, 2026

This increment identifies draw ownership; it does not enable curved output or change the current flat/angled presentation. Source and CPU fixtures cannot qualify the appearance of a world effect, HUD or compositor.

## Traced sequence

| Native consumer | Ownership and required treatment |
|---|---|
| `frame_render_449050`, layers 0..8 | World draws, course passes, reflection generation, vehicles, particles, glare and shadow darkening occur before the explicit 2D flush. A world RHW draw remains a world screen effect, not HUD. Reflection/other render targets remain offscreen work. |
| `42D710(0, 0x15)` | The explicit 2D queue flush. `game_host.cpp` calls `Presenter::scene_to_frame` first; XboxPresenter routes it to `xbox_renderer_scene_to_frame` / `D3D11D3D9Device::finish_scene`. Current operation is MSAA resolve or FXAA, followed by native sprite/HUD/menu drawing at full resolution. This is the candidate place to finish/composite a curved world source. |
| `49F4D0` after `42D710`, when `84A318 == 2` | Car-selection 3D overlay over the frontend's 2D layer. It must retain its authored ordering and must not be mistaken for a restarted race world or sent through a curved race source. |
| Desktop F6 / camera notice | Drawn by the desktop panel after the game scene. Keep outside the curved world. |

RHW is a storage format, not a semantic boundary. `race_particles_draw.cpp` emits projected smoke/effect quads, while `pc_shadow_volume.cpp::shadow_darken_422740` emits pretransformed stencil darkening. Both precede the HUD. Conversely, a late frontend mesh uses ordinary XYZ geometry after the HUD. No first-RHW or Present heuristic can separate these correctly.

## Implemented ownership metadata

`PcD3D9Device` carries an explicit draw phase. A scope at `frame_render_449050` starts World, changes to Hud immediately before the `42D710` leaf, and scopes the later `49F4D0` as FrontendOverlay. The actual 2D flush also scopes Hud, including direct/control-time flushes outside the frame loop. Nested calls and exceptions restore their caller's phase. Unclassified ownership remains explicit.

The shared domain classifier combines that phase, RHW/XYZ storage and **logical backbuffer** identity: WorldGeometry, WorldScreenEffect, Hud, FrontendOverlay, Offscreen or Unclassified. D3D11 triple-binding decisions count these domains and print totals at exit alongside existing fallback counts. This is metadata only; it does not reroute a target, change a viewport/shader, recover a culled particle or enable a warp. An actual-frame software fixture verifies World -> Hud -> FrontendOverlay order, world-RHW/HUD-XYZ negative controls, offscreen exclusion, nesting and exception restoration. GPU counter values still require the later attended run.

Validation: Release desktop builds. Eight focused camera/render/shadow/audit checks pass; actual-frame fixture **34 checks**, plus the direct sprite fixture **296 checks** (real sprite/UP consumers assert Hud and preserve an enclosing frontend phase). The sprite fixture requires `OR2_EXE` for native renderer constants and reports skip 77 without it. Existing controller/software-renderer **155**, shadow **544** and retail body fitter **102** checks also pass. No GPU/window/game/device was started. Private evidence is under `work/validation-20261102/curve-boundary-*`; the first build log retains a mistyped nonexistent target, corrected in `curve-boundary-final-build.log`.

## Pending compositor work and evidence limits

- A future world source needs gameplay/mode admission, a logical-backbuffer identity separate from the physical offscreen texture, matching depth/stencil and MSAA handling, explicit resolve/composite before `42D710`, and restoration of the HUD/frontend target, viewport and shaders. Current triple target checks use the `BackBuffer` sentinel; replacing only the physical target would silently disable replication.
- World screen particles are still built from the center projection and CPU-rejected at finite `clip.w <= 0`. Classification cannot recover them. Preserve world identity/coordinates through admission and build per-panel/source-eye effects before claiming angled or curved correctness.
- Shadow darkening must cover the world source and preserve stencil semantics. World RHW is currently kept on the center by the GPU compatibility path; it is still unqualified for corrected side views.
- Unmatched custom programs, offscreen reflections and fallback counts remain separate work. Keep current Planar/Angled choices and defaults until rendered evidence establishes compatibility.

Future owner-attended capture: use the frozen candidate and qualified muted disposable route, same scene/camera/resolution in Planar and Angled, and `OR2_TRIPLE_TRACE=1`. Include smoke behind the center eye but inside a side view, dust/sparks/glare, player/traffic shadows, pause/replay, and frontend car select after returning from a race. Capture full span and center HUD/menu closeups plus draw-domain/fallback counters and frame timings. Verify F6/notice stays after the world composite and exact owner/display restoration before lease release. No such capture occurred overnight.
