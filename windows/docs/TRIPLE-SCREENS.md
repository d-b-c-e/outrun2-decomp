# Triple screens for the desktop build (design, for review; no code yet)

Targets, in order:
- the portfolio standards: STD-022 (one selector: Off / Surround / Separate monitors), STD-015 (both topologies),
  STD-007 (three views: off-axis centre, sides yawed by the panel angle, HUD on the centre) and STD-019 (menus on the
  centre, sides black outside gameplay);
- the parity floor: the Tweaks fork's `[Triple] Screens`, which is a planar span only (its TOOLKIT-ADOPTION: STD-007 /
  015 / 022 partial, STD-019 unchecked).

Status 2026-10-12: design only.

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
