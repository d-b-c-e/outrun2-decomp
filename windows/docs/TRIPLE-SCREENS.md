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

## Questions for review

1. Order: the planar span (2) first as the parity floor and fallback, then the three views (4)? Or skip straight to
   4 and keep planar only as the fallback?
2. Step 3: a centre viewport for all 2D, or the HUD width variable?
3. Step 4 math: transcribe the managed calculator here with the toolkit fixtures, or build the native ABI first?
4. Does STD-030 require the selector in the F6 panel now, or is the INI key enough until the panel lands (ROADMAP 2a)?
