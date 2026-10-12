# Triple screens for the desktop build (design and status)

Targets, in order:
- the portfolio standards: STD-022 (one selector: Off / Surround / Separate monitors), STD-015 (both topologies),
  STD-007 (three views: off-axis centre, sides yawed by the panel angle, HUD on the centre) and STD-019 (menus on the
  centre, sides black outside gameplay);
- the parity floor: the Tweaks fork's `[Triple] Screens`, which is a planar span only (its TOOLKIT-ADOPTION: STD-007 /
  015 / 022 partial, STD-019 unchecked).

Status 2026-10-12:
- steps 1 and 2 (the selector, the unstretched 16:9 frame, the planar span) are in integration and checked at the rig;
- step 4 is a default-off spike, not yet run in the game (see "The spike as built" below).

## What the mod does (the parity floor)

- `Screens = Off` and `Screens = Surround` (its default) leave the window as before. On a Surround desktop, Windows
  reports one display (e.g. 7680x1440), so the game is already one wide view.
- `Screens = Separate monitors`: one borderless window over three equal side-by-side monitors (no gap or overlap; the
  pure `TripleSpanFromMonitors` over `EnumDisplayMonitors`), with the game resolution set to the span. Anything else
  logs why and stays on the primary monitor.
- Both wide modes are **planar**: one wide frustum, the same picture as the game's own Surround mode (toolkit
  `engine-routes.md` section 3). Menus stay 4:3 in the centre with `UILetterboxing = 1`.

## Where the decomp stands (verified in source)

- **Window** (`windows/source/window_desktop.cpp` `native_window`): borderless over the primary monitor, or a 1280x720
  window with `--windowed`. No span.
- **Aspect is a boolean**, 4:3 or 16:9, in shared code (also built for the Switch / PS5 forks):
  - `enhancements/video.cpp` `resolution_at(height, widescreen)`;
  - `settings_rows.cpp` (the Aspect row);
  - `gpu_backend.cpp` (options.ini `widescreen`);
  - `runtime/game_host.cpp:427`: the PC width, 640 or 480*16/9, goes to `PcSceneRenderer::set_screen` (FOV 483C10,
    Hor+), to `g_pc_view_half_width` (car and object culling, incl. ScreenEdgeCullFix) and to the camera's aspect +B8.
- **The projection already takes an off-axis shift.** `camera_perspective_404310` (`src/driving/pc_camera.cpp`)
  builds `D3DXMatrixPerspectiveOffCenterRH` from the fov, the aspect, near, far and the camera's shift_x / shift_y
  (+C4 / +C8). It writes the frustum globals 95BF40 and the four culling planes 95BF58 from the same bounds.
  `camera_view_482f20` builds the view from the eye and the angles, and `camera_project_484bd0` runs both.
  - A side view is therefore the same camera with a yaw before 482F20's rotations and a shift_x.
  - Culling follows by construction.
- **The HUD** is the game's own 2D flush (42D710 / pc_sprite_2d), laid out from [740C8C] / [740C90]. At a span width,
  edge-anchored pieces land on the outer monitors. UIScalingMode / UILetterboxing are still unported (ROADMAP).
- **The toolkit's projection math** is managed only (`Dbce.TripleScreen.Core`: `TripleRigBuilder`,
  `ProjectionCalculator`, `OffAxisProjection`). The native C ABI is a design gate (`docs/native-bridge.md`), not code.
  The status contract may report `three-projections` only from observed distinct render passes.

## Proposal (each step a small reviewed commit, A/B before any default)

1. **Selector and window.** No rendering change.
   - `[Triple] Screens = Off | Surround | Separate monitors` (case-insensitive; "Separate" accepted). Default Off
     until step 4 is qualified. Surround then changes nothing on a single display.
   - Port `TripleSpanFromMonitors` as a pure function with a unit test: three equal monitors in any order, a gap, an
     overlap, unequal sizes or heights, two monitors, four monitors.
   - `native_window` takes the span when Separate is chosen and the window is borderless. Otherwise it uses the
     primary monitor and logs why.
   - A status line at start: mode, span or reason, and "next start" when changed in game.
2. **Planar span (the mod's parity floor).**
   - The aspect becomes a value rather than a boolean. When Screens is not Off and the window is wider than 16:9,
     the PC width follows the window (480 x width / height), going to set_screen, g_pc_view_half_width and +B8 as
     now, and to `resolution_at`.
   - Tests: resolution_at and the 483C10 FOV at 48:9 against the closed form.
   - This is also the fallback when the rig is unknown or invalid.
3. **Centre-only 2D** (STD-019, needed by both 2 and 4).
   - The 2D layers (the HUD in race, and the menus, loading and the F6 panel always) draw in the centre third, at
     the single-screen aspect. The sides are black outside gameplay.
   - Open question: a centre viewport for the whole 2D pass, or the HUD's own width variable held at the centre's
     width? To settle with a read of pc_sprite_2d's [740C8C] uses before code.
4. **Three projected views** (STD-007).
   - Inputs: rig panels (width, bezel or gap, yaw) and the eye distance, as the toolkit's rig definition.
   - Three passes per frame, each in its third of the span: the centre with its off-axis shift, the sides with their
     yaw and shift. Each pass runs 484BD0 with that panel's +B8 / +C4 / yaw, then culls and draws with its own
     frustum.
   - The game's single-view state (the camera's +140 / +1C0, the reflection and shadow passes, the particles' screen
     mapping) is computed once from the centre, unless a pass needs its own.
   - The math: transcribe `ProjectionCalculator` for the neutral bounds, using the toolkit's numerical fixtures
     (seams, asymmetric rigs) as unit tests here. Alternatively, build the toolkit's native ABI first with this
     decomp as its first fixture consumer. That choice is for review.
   - Status `three-projections` only from three observed passes per frame (a counter in runtime.log).

## Cost and risk

- Steps 2 and 4: 3x the pixels at the same height, plus MSAA.
- Step 4 also replays the scene's draw lists three times. CPU replay and draw calls roughly triple; the GPU fill
  stays the same as step 2.
- The A/B rule applies: on the perf copy, 16:9 against the span against three views, muted, before any default.
- Per-view state that is not per-view today: the car reflection cube, stencil shadows, lens flare, particles' screen
  mapping (draw_418cf0), and ScreenEdgeCullFix's widened bounds. Each needs a check at 48:9.
- Only the rig's real three monitors test Separate monitors (owner-attended, STD-015 "verify both topologies at the
  rig"). A muted unattended run can check passes, counters and screenshots, not the angles.

## Review outcome (nene 30120 / 30125, romi 30139, 2026-10-12)

- **Order**: 2 then 4. Keep step 2 minimal; it carries prerequisites (aspect as a value, the span window).
  - Then an early step-4 spike at the rig, spike-first: the pinned toolkit fixture rig hard-coded, three passes, a
    pass counter.
  - The owner's geometry (romi): 32 inch 1500R, visible chord 708.4 x 398.5 mm, eye 660 mm, sides 70 degrees, gap 8
    mm. Name any difference from the fixture.
  - No rig UI or per-view polish before the first picture.
- **2D**: the centre's viewport and the centre's layout extent together.
  - Before the pass split, classify every reader of [740C8C] / [740C90] as per-pass or centre-only: the 2D flush,
    418CF0 particles, the flare, and any others.
- **Math**: one isolated, self-contained translation of the pinned toolkit geometry.
  - Shape it like the future native ABI's neutral outputs (per-panel bounds from rig + eye).
  - Test it against the toolkit's fixture values to 1e-9.
  - No new native ABI first.
- **Panel**: the INI key complies now (STD-022). The F6 selector and status are needed before full parity is claimed.
- **Default** Off, INI only, for the spike (performance first). An explicit selection is preserved. No silent owner-INI
  or default migration. The owner install stays untouched until a visual / perf result.
- **Off on a wide window** is an unstretched single view: 16:9 centred, black sides.
- **Window**:
  - per-monitor-DPI physical pixels (negative x for the left monitor);
  - no display-mode or topology change;
  - the swap chain follows the client size.
  - The first rig spike captures a frame (WGC) in whatever window mode it uses.
- **Logs**:
  - actual passes and per-pass CPU from the first spike;
  - while gated (menus, rig invalid), a periodic line saying why the views are off;
  - status "planar" for step 2, "three-projections" only from counted passes.
- **Budget**: "PC scene render_frame" was 5.5 ms per frame (P5-lat2). Three plain replays are about 16.5 ms, so they
  would miss 60 fps unless side passes cull more, or the replay is shared or parallel. Measure before any default.
- **STD-019 coverage**: also the title movie and the loading pictures (the START loading picture is a full-screen 2D
  draw).
- **Unattended checks**: source angles and rendered seams can be checked unattended. Physical comfort waits for the
  owner.

## Step 4 mechanism (proposal for review, 2026-10-12): one game render, three viewports

Step 2 is checked at the rig (2026-10-12, hub `artifacts/outrun-perf-20261011/triples-20261012`):
- one borderless 7680x1440 window over the three monitors;
- planar 48:9 at 60 fps, with CPU render_frame +2.3 % and about 11 ms of GPU/vsync slack left;
- the menus and HUD on the centre monitor.

The rig math is in `windows/source/triple_rig.hpp`, matching the toolkit's values to 1e-9 (`6d53a07`).

**Why not run the scene three times.** `render_frame` runs the game's own frame, 449050, and its display callbacks.
They are not all pure:
- the particle draws consume the shared CRT random state (`native_race_effects.cpp:299`);
- the per-frame counters advance;
- the race-area hooks keep their own state.

Three runs per frame would need an audit of every display callback, and still cost three times the 4.4 ms CPU (nene's
budget point).

**Proposal: replicate on the GPU.** The game renders once, from the centre panel's camera. Each back-buffer draw is
emitted into three viewports by a geometry shader.
- **The clip position is one value in both shader paths.** The translated vertex programs write it at
  `d3d11_d3d9.cpp:334` (`o.p`, after the D3D9 pixel-centre fix). The fixed-function path passes through positions
  the CPU has already transformed to clip space (generated `ffp` `vs_main`: `o.p=v.p`).
- **Per panel i, a 4x4 correction** `C[i] = P_c^-1 · R(yaw_i) · P_i` (row vectors):
  - P_c is the centre projection the game used, from the 404310 frustum 95BF40 and near/far;
  - P_i is the panel's off-axis projection from `panel_bounds` at the same near/far;
  - R turns the centre's view space to the panel's.
  - `clip_i = clip_c · C[i]` is exact for any vertex, whatever produced clip_c. C[centre] is the identity.
- **The geometry shader** takes the pipeline's `Varying` unchanged and emits each primitive three times, each with
  `p · C[i]`, `SV_ViewportArrayIndex = i` and a scissor per viewport. Three variants (triangles, lines, points); point
  sprites are already triangles.
- **Bound only for draws into the back buffer.** The reflection cube, shadows and other render-to-texture passes stay
  single.
- **Pretransformed (rhw) draws stay single, in the centre viewport only**: the HUD, menus, and 418CF0's screen-space
  particles.
- **The camera**: the game's own camera is the centre panel, at 16:9 with its own vertical FOV +A0.
  - Matched-FOV rig: `eye_distance_for_vertical_fov(panel height, A0)`, recomputed every frame (A0 changes with the
    camera mode), so the centre looks exactly as the 16:9 game does today.
  - The sides then meet it at the seams by construction (toolkit property, tested).
- **Culling**: the game culls for the centre frustum only, so the side views would be empty at their edges.
  - For the spike: stage culling and object culling off while three views are on (the existing DisableStageCulling
    path; `g_pc_view_half_width` wide).
  - Afterwards: an object is visible if it is in any of the three frusta (the union exceeds 180 degrees, so no single
    frustum can stand in for it).
- **What stays the centre's**:
  - fog depth: vertex fog from the centre's view z, so a slight mismatch at far sides;
  - the lens flare and glare (415630);
  - the screen-space particles;
  - the reflection cube.
  - All acceptable for a spike, and to be judged at the rig.
- **Cost**:
  - CPU about 1x (one game render), plus the culling-off draws;
  - GPU vertex work 3x plus the geometry-shader pass;
  - pixel work about the planar span's (three 2560x1440 panels). That measured about 11 ms of slack at 60 fps.
- **Status**: `three-projections` only from counting, per frame, the draws emitted through the three-view geometry
  shader. runtime.log also gives per-pass counts.
- **Feature level**: 11.0 on this device (`b000` in runtime.log); viewport arrays and the geometry shader are core
  there.

Open points for review:
- (a) GPU replication versus three scene runs;
- (b) the spike's culling-off versus doing the union first;
- (c) whether the clip_fix half-pixel offset should move after the correction (it is in NDC per viewport, and
  every viewport is the same size).

### The spike as built (`2a5ebbf`, review fixes `bb91f82`; default off, not yet run in the game)

Review (romi 30680 / 30784, nene 30684 / 30802): (a) GPU replication, (b) culling off for the spike, (c) the fix is
undone before the correction and re-applied after it, with the new w.
- **Switch**: `OR2_TRIPLE_VIEWS=1` with a planar span (`[Triple] Screens = Separate monitors` over three equal
  monitors). OR2_ variables are cleared in test processes. Without it nothing changes.
- **Corrections**: `triple_views` composes C[i] in double on the CPU, per projection (cached), for the toolkit's
  fixture rig with a matched FOV. A projection it cannot take (not a centred perspective) is refused, and the draw
  stays single on the centre third.
- **Geometry shader**: triangles only (lists and strips). Those are the only draws the device makes: draw_native takes
  lists and strips, flush draws a list, and points and lines are already not drawn. Any other topology would stay
  single, counted as "not triangles". Its Varying is taken from the generated vertex source, not copied.
- **Which draws replicate**: back-buffer draws whose clip position is a view-space point times the current projection.
  - FFP draws: the CPU's world x view x projection.
  - Vertex programs whose constants hold M x P, with P the current projection and M affine. The current world x view x
    projection is tried first; then any affine M, such as a sky drawn with the camera's rotation but not its
    translation.
  - This is a heuristic. A program whose constants factor that way but project otherwise would be replicated wrongly.
    runtime.log names each program once with what it matched.
  - Pretransformed draws, programs with no such matrix, and projections the rig refuses stay single on the centre.
    Each is counted separately in the exit line, and each refused projection is logged once.
- **Tests**:
  - `test_triple_screens`: corrections against a direct per-panel projection to 1e-9, through the fix;
  - `test_d3d11_vertex_path`: the replication on WARP through both vertex paths, each third against the single view;
    one geometry-shader mutation survives, see `bb91f82`.
- **Exit line** (runtime.log): draws replicated over frames, and the single ones by reason.

Known limits, judged at the rig, before any default (nene 30684):
- fog and billboard axes stay the centre's (vertex fog from the centre's view z, sprites facing the centre camera);
- the side panels miss the screen-space (rhw) particles, which land on the centre only;
- objects behind the eye plane stay culled: `g_pc_view_half_width` passes everything in front, not the union of the
  three frusta (needed beyond 180 degrees);
- the eye distance is the matched-FOV one; a physical eye distance is a later option, under the F6 selector.

The `three-projections` status needs counted passes plus a WGC check of the seams, not counts alone.

**First rig picture** (2026-10-12 12:54, `bb91f82`, perf copy, muted; hub `artifacts/outrun-perf-20261011/triples-20261012`):
- 60.0 fps over 1973 race frames, max 35.0 ms; 0 device errors.
- Over 2095 frames: 1,533,365 draws replicated. Single on the centre: 203,723 pretransformed, 707,861 "custom" (not yet
  split by reason), 1,119,793 other targets.
- **Seams**: in the race the side panels continue the scene with their yawed projections (grandstands left, buildings
  right). Road, fences and buildings carry through both seams, folding at each one; the 8 mm fixture bezel is skipped.
  Physical straightness is for the owner to judge.
- **Omission: the side sky was black.** Its draws were among the custom ones. The affine match above is the candidate
  fix, with the reasons now counted.
- **HUD and menus**: on the centre only. In the menus the side panels are black.
- Captures were PrintWindow and a GDI copy of the composed desktop, not WGC.

**Second picture** (13:06, `be3c520`, the affine match):
- **The side sky draws**, with clouds continuous across the seams.
- 2,251,414 draws replicated, 2,023,516 of them through the affine match (the world is in the constants only, not in
  SetTransform). 3,519 single: programs without the projection. 0 projections refused.
- 24 programs, every match at c64.
- Remaining edge: track sections are chosen for the centre camera, so a side panel's ground can end at a hard line
  against the sky. The culling union is still open.
- One program flipping between outcomes flooded runtime.log (max frame 109 ms); `67d9cbd` logs each outcome once.

**Side sky flicker (F10 and F11, 13:46-13:51):**
- Captures every 2 s showed the side sky black in most frames. C5 and C6's frames had happened to land on good ones.
- The recent-projection fallback (`a4e64aa`, `45c43e4`) matched nothing.
- F11's dump identified the cause. The sky program's constants are the camera's P x the view rotation (no translation),
  but their depth row is 1.1e-4 off the camera's (near term -0.099991 against -0.100002). That sits just outside the
  affine test's 1e-4 tolerance, and drifts in and out frame by frame.
- That column of M meets only P's depth row, so it bears only on clip z. Clip x, y and w, and so the side placement, are
  exact for any M. The tolerance is now 1e-3 relative.

**F12 (13:55, `5e2552d`):**
- The side sky draws in all nine frames from t40 to t56.
- Unmatched draws fell to 44, all of them the sky program at moments when the camera is turned. There its w scale was
  0.9989 (1.1e-3 off), while its projective terms were 1e-6.
- The w scale (row 3 of M's column 3) only scales the depth row, and the side panels then get the sky's own depth row.
  It now only needs to be finite and between 0.5 and 2. Rows 0..2 stay at 1e-3 relative.
- The recent-projection fallback never matched in F10, F11 or F12, so it is removed (sana 35380/35428, nene 35423).
- F12's timing is not qualified: a peer's build and ctest overlapped the run.

**F13 (14:01, `53ae4cc`):** 0 unmatched vertex-program draws, and the side sky draws in all nine frames from t40 to
t56.

### Owner selection: `[Triple] Views` (sana 35626)
- `[Triple] Views = Planar | Angled`, below the selector. The default is Planar, the look so far.
- Angled turns on three projected views (experimental): the fixture rig (32 in 16:9 panels, sides at 70 degrees, 8 mm
  gaps) with the eye matched to the game's FOV. It is not the owner's measured rig yet.
- Read once at start. It applies only in a normal process with the borderless window over a span wider than 16:9;
  otherwise runtime.log and the status name the reason.
- OR2_TRIPLE_VIEWS=1 remains a developer request for the same thing. Both the lods.ini read and the window use the one
  request.
- Status (`triple_screens_status()`): requested and applied projection, and the refusal.
- F6 wording, for the Cameras page: "Projection: One wide view / Three projected views (experimental)".
- Angled does not become the default for Surround / Separate monitors, as STD-022 implies, until the owner has checked
  it physically.

## Readers of the screen size [740C8C] / [740C90] (for the step-4 pass split)

In the native runtime, the memory words are a read-only constant 640.0 / 480.0 (`native_race_effects.cpp:283`,
`map_const`). The camera reads its own copy, `PcCameraScreen`, which `set_screen` updates.

| Reader | What | Class |
| --- | --- | --- |
| 483C10 `camera_fov_483c10` | FOV from `PcCameraScreen` (Hor+) | per pass (each view's width) |
| 484EE0 / game_host +B8 | the camera aspect | per pass |
| 418CF0 `race_particles_draw.cpp` (2 sites) and its size at 419009 | screen mapping of the screen-space particles from the current view-projection, with the constant 640/480 | per pass through the matrices. Its output is pretransformed (rhw), and the `ui_rect` that keeps the HUD in the centred 4:3 also applies to rhw batches: check where these land at 16:9 today and at the span |
| 415630 (from 414F00, the glare in layer 6: `pc_frame_render.cpp:130`) | glare and flare, divides by [740C8C] | per pass |
| 40E4D0 (from 418080) | **writes** both words | not on the native path while they are mapped constant. Verify before step 4 |
| 2D flush 42D710 / pc_sprite_2d | its own 640x480 layout, no read of the words | centre only (`ui_rect`) |

## Presentation path today (read 2026-10-12, shapes steps 1 and 2)

- `renderer_present.inc` composes a fixed 1280x720 virtual frame:
  - the PC frame centred at 16:9 or 4:3;
  - the title movie at 4:3;
  - the frontend and loading quads.
- `D3D11Display::clear` maps that frame over the **whole** swap chain (`d3d11_display.cpp:123`), so any window that
  is not 16:9 is stretched. Off on a 48:9 window is a 3x stretch today.
- Step 1 fits the frame into the centred 16:9 rectangle of the swap chain (black elsewhere); for three equal monitors
  that is exactly the centre one. Step 2 widens only the PC frame to the full window during gameplay.
- The PC device already draws pretransformed (2D) batches into a centred 4:3 of its back buffer (`ui_rect`,
  `d3d11_d3d9.cpp:455`). Under a planar span the race HUD therefore stays inside the centre monitor with no change.
  418CF0's screen-space particles go through the same path, and their placement at the span needs a check.
- The width the camera sees is re-applied every frame from `game_host.cpp:427`, so it can follow gameplay versus
  menus. The back buffer size cannot follow cheaply: `resize_back_buffer` is not per-mode. In step 2, menus therefore
  keep the full-width 3D background until the gating is designed (STD-019 open item).
