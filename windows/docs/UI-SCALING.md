# UI scaling for the desktop build (design note, for review; no code yet)

Target: Tweaks parity for `[Graphics] UIScalingMode` and `UILetterboxing` (ROADMAP: to do).

## What Tweaks does (dbce-mods-outrun-2006 `hooks_uiscaling.cpp`, `OutRun2006Tweaks.ini`)

`UIScalingMode` (Tweaks default 1):
- **0**: the game's own UI, stretched to the screen's aspect.
- **2, "keep centred"**: the 640x480 UI at one uniform scale, the smaller of the x and y screen scales, centred.
  - `D3DXMatrixTransformation2D` (439400): sprite scale and translation are rescaled uniformly, then centred.
  - `draw_sprite_custom` (42A556) and the `custom_matrix_multi` cases (42B1E2 / 42B53E / 42BB2F): the vertex x is
    rescaled and centred.
  - `Calc3D2D` (449940): the 3D-to-2D markers (1st / 2nd ...) are rescaled in x.
- **1, "Online Arcade"** (the default): mode 2, plus a spacing step, so the HUD reaches the widescreen edges without
  stretching.
  - Around chosen HUD routines a scope flag is set and cleared again (mid-hooks at their entry and exit). Inside the
    scope, the hook at `drawFootage` (4293F3) moves the layer matrix's x translation (m[12], the 640-wide UI's x):
    - by `-spacing / screen_scale.x` when x < 213 (the left third);
    - by `+spacing / screen_scale.x` when x >= 426 (the right third);
    - with `spacing = (screen_width - 640 * scale_y) / 2`.
  - The scopes:
    - NaviPub_Disp (4BEB31 / 4BEDBE on, 4BEDE0 / 4BEE21 off);
    - hearts (4BEBE1 / 4BEBE6) and C2C hearts / fruit (481B76, 4BECBA, 4BECE0; 481A86 / 481A8B);
    - rival markers (4BEB8E / 4BEBAF; online 4BEC83 / 4BEC88; dispMarkerCheck 4BA0E0 off);
    - DispTimeAttack2D, with forced left / right (4BE4BC, 4BE4C1, 4BE4E7, 4BE575).
  - `put_scroll` positions: fifteen in DispTimeAttack2D (4BE5CD ... 4BE81C) and eight in DispRank (4B9F3A ... 4BA052)
    move right by the spacing.
  - Ghost gap sprites (PutGhostGapInfo, DispGhostGap) and the time-attack goal get their own fixes.
- `UILetterboxing` (default 1): black bars outside the 4:3 UI in menus, as Tweaks configures them. 2 means always.

## Where the decomp stands

- All pretransformed (rhw) 2D already lands in the centred 4:3 at 16:9: `set_ui_rect` and `apply_state` in
  `xbox/source/d3d11_d3d9.cpp`. That is Tweaks' mode 2, and it is what the owner sees today.
- The HUD is reconstructed per element in `src/platform/race_hud_navi.cpp`, with names by address (ranks_4b99e0,
  position_4b9d80, speed_4bcf10, gear_4b9010, lap_4b9630, marker_4bbc70 ...). It is not one NaviPub_Disp body with
  Tweaks' instruction addresses, so each Tweaks scope has to be mapped onto these functions.
- `layer_429350` in `src/platform/pc_sprite_2d.cpp` is drawFootage's equivalent: it holds Tweaks' 4293F3, and the layer
  matrix it draws under is where the spacing shift goes.
- `project_449940` in `race_particles_effects.cpp` is Calc3D2D.

## Proposal (small reviewed steps; default as Tweaks only after an in-game A/B)

1. `[Graphics] UIScalingMode` read, with 2 (the decomp's current look) as the default until step 3 is seen in game.
   Mode 0 drops the UI rectangle (stretched).
2. Mode 1's spacing:
   - a scope flag (`g_pc_ui_spacing`: off / detect / left / right) set by the reconstructed HUD functions that
     correspond to Tweaks' scopes;
   - `layer_429350` applies the x shift when the flag is on.
   Each scope is mapped from a Tweaks address to the decomp function holding it, in a table in this note, before code.
3. The put_scroll positions, the ghost gap and the time-attack goal, as Tweaks.
4. A muted visual check at 16:9 across race, time attack and C2C screens, against Tweaks frames if the owner has them.
   Then the default becomes 1, as Tweaks.
5. `UILetterboxing`.

Triple screens: the HUD stays on the centre monitor in both planar and three-view modes, which is STD-007. With
three views, mode 1's spacing applies to the centre panel's 16:9 only.
