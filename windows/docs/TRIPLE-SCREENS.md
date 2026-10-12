# Triple screens for the desktop build (design, for review; no code yet)

Parity target: the OutRun 2006 Tweaks mod's `[Triple] Screens` (dbce-mods-outrun-2006 `OutRun2006Tweaks.ini`,
`src/triple_span.hpp`, `src/hooks_misc.cpp`) under the portfolio rule STD-022 (one selector: Off / Surround /
Separate monitors). Status 2026-10-12: design only.

## What the mod does (the parity reference)

- `Screens = Off`: the window as before.
- `Screens = Surround` (its default): also the window as before. On a Surround desktop, Windows reports one display
  (e.g. 7680x1440), so the game is already one wide view.
- `Screens = Separate monitors`: one borderless window over three equal side-by-side monitors (no gap or overlap; the
  pure `TripleSpanFromMonitors` over `EnumDisplayMonitors`), with the game resolution set to the span. Anything else
  logs why and stays on the primary monitor.
- Both wide modes are **planar**: one wide projection, not angle-correct side views. Menus stay 4:3 in the centre with
  `UILetterboxing = 1`.

## Where the decomp stands (verified in source)

- Window (`windows/source/window_desktop.cpp` `native_window`): borderless over the primary monitor, or a 1280x720
  window with `--windowed`. No span.
- Aspect is a boolean, 4:3 or 16:9, in shared code (also built for the Switch / PS5 forks):
  - `enhancements/video.cpp` `resolution_at(height, widescreen)` (the 3D resolution width from the height);
  - `settings_rows.cpp` (the Aspect row: "4:3", "16:9");
  - `gpu_backend.cpp` (options.ini `widescreen=0/1`);
  - `runtime/game_host.cpp:427`: the PC screen width 640 or 480*16/9 goes to `PcSceneRenderer::set_screen` (FOV
    483C10, Hor+), to `g_pc_view_half_width` (car and object culling, incl. ScreenEdgeCullFix) and to the camera's
    aspect +B8.
- The 3D camera, the projection and the culling follow that one width. A 48:9 width should therefore give the mod's
  planar wide view with no further 3D work, once the width can be other than 640 or 853.3.
- The in-race HUD is the game's own 2D flush (42D710 / pc_sprite_2d). It lays out from the screen variables
  ([740C8C] / [740C90]): edge-anchored pieces follow the width, centred ones the centre. At 48:9 the edge pieces
  would land on the outer monitors. UIScalingMode / UILetterboxing are still unported (ROADMAP).

## Proposal (three steps, each a small reviewed commit)

1. **Selector and window** (no rendering change).
   - `[Triple] Screens = Off | Surround | Separate monitors` (case-insensitive; "Separate" accepted), default
     Surround as the mod.
   - Port `TripleSpanFromMonitors` as a pure function with a unit test (three equal monitors in any order, a gap, an
     overlap, unequal sizes or heights, two or four monitors).
   - `native_window` takes the span rect when Separate is chosen and the window is borderless; otherwise the
     primary monitor, with a "why" line in runtime.log.
   - A status line at start: mode, span or reason.
2. **Aspect follows the window when wide.**
   - Replace the boolean by an aspect value: 4:3, 16:9, or "window" (width = 480 x window width / height).
   - "window" applies only with Screens = Surround or Separate and a window wider than 16:9; Off keeps today's
     choice.
   - `resolution_at` takes the aspect; the 3D resolution width follows (e.g. 1440 high gives 7680 wide).
   - game_host passes the width to `set_screen`, `g_pc_view_half_width` and +B8 as now.
   - The settings row shows "Window" while it applies.
   - Tests: resolution_at and the camera FOV (483C10) for 48:9 against the closed form.
3. **HUD on the centre screen** (STD-022 "HUD centring").
   - The 2D flush draws into a centre viewport of the middle third (16:9 inside it, or 4:3 with letterboxing, as
     the 16:9 path does today).
   - The 3D scene keeps the full width.
   - Needs a closer read of how pc_sprite_2d places [740C8C]-relative pieces before committing to "viewport" versus
     "scale the 2D width back to 16:9". This is the open design question.

## Cost and risk

- 3x the pixels at the same height, plus MSAA. The A/B rule applies: measure 16:9 against the span on the perf copy
  before any default.
- Planar 48:9 stretches the outer thirds, exactly as the mod does. Angle-correct side views (three cameras) would be
  a separate, larger item (three projections or a multi-view pass); out of scope for parity.
- The reflection cube, the stencil shadows and the particles take their sizes from the viewport or [740C90]. The
  point sprites already use the viewport height. Each should get a quick check at 48:9.
- Separate monitors needs the rig's real three-monitor setup for its only true test (owner-attended, or a
  three-monitor virtual layout if the toolkit provides one).

## Questions for review

1. Default: Surround (the mod's), which changes nothing on a single display. Agree?
2. Step 3: centre viewport for the whole 2D layer, or adapt the HUD's own width variable? (A read of pc_sprite_2d
   follows before code.)
3. Does any other portfolio decision (Wheelkit/F6 shared settings, STD-030) require the selector in a panel now, or is
   the INI key enough for parity?
