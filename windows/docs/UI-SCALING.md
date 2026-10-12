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

1. **Built (sana GO 38509):** `[Graphics] UIScalingMode` read, with 2 (the decomp's current look) as the default until step 3 is seen in game; `display_fit.hpp` `parse_ui_scaling_mode` / `ui_scaling_rect`, the device's ui rect taking an aspect. 1 and invalid values log a line and apply 2.
   Mode 0 drops the UI rectangle (stretched).
2. Mode 1's spacing:
   - a scope state (off / detect / left / right) that the reconstructed HUD functions set and clear where Tweaks'
     scopes begin and end (the map below);
   - **captured in each `SpraniDraw` record** when `sprani_draw_428a10` records it, and applied per layer by
     `layer_429350` when the record is rendered. The decomp's HUD draws are deferred: `N::emit` -> `sprani_draw_428a10`
     only records the matrix; `pc_sprani_render_429460` -> `layer_429350` runs later (race_hud_runtime.cpp). So a
     global flag read at render time, as first written here, would see the wrong scope.
   - Tweaks' rule per layer, at render: detect moves m[12] by -spacing below x 213 and +spacing from x 426; left /
     right force the direction.
   Each scope is mapped from a Tweaks address to the decomp function holding it, in the table below, before code.
3. The put_scroll positions, the ghost gap and the time-attack goal, as Tweaks.
4. A muted visual check at 16:9 across race, time attack and C2C screens, against Tweaks frames if the owner has them.
   Then the default becomes 1, as Tweaks.
5. `UILetterboxing`.

Triple screens: the HUD stays on the centre monitor in both planar and three-view modes, which is STD-007. With
three views, mode 1's spacing applies to the centre panel's 16:9 only.

## Step 2a, proposed slice (for review; no code yet)

**A finding that step 2 must handle first.** With the centred 4:3 rectangle, the 2D viewport and scissor cover only
that rectangle.
- `d3d11_d3d9.cpp` maps a pretransformed vertex from the game's 640-wide viewport to NDC on the CPU (around line 351),
  then the D3D11 viewport places the NDC range in the 4:3 area.
- A HUD element moved by the spacing (up to 106.7 UI units at 16:9) would leave NDC [-1, 1] and be clipped.

So mode 1 needs a wider 2D mapping at the same scale:
- the D3D11 viewport and scissor span the frame, or a span's centre panel;
- the CPU NDC mapping uses an extended logical width, 640 + 2·ext, offset by ext, where ext = (aspect · 480 − 640) / 2
  (106.7 at 16:9);
- unscoped 2D lands where mode 2 puts it: same scale, same centre, up to mode 2's whole-pixel rounding of the 4:3 left
  edge (exact at 1920 / 2560 wide).

**Slice 2a:**
1. Device: a "wide 4:3" 2D rectangle for mode 1, as above (`ui_scaling_rect` grows a third state; mode 2 and 0 keep
   their arithmetic).
2. Scope capture:
   - a current scope (off / detect / left / right) that the reconstructed HUD sets and clears;
   - `SpraniDraw` gains a `spacing` field, filled when `sprani_draw_428a10` records the draw;
   - `pc_sprani_render_429460` passes the record's value to `layer_429350`. After `matrix_429220`, as Tweaks' 4293F3,
     that shifts the layer matrix's m[12] by ±ext: detect moves x < 213 left and x ≥ 426 right; left / right force
     the direction.
3. First scope only: NaviPub_Disp's own on / off (4BEB31 / 4BEDBE on, 4BEDE0 / 4BEE21 off) in
   `navi_pub_display_4beb00_impl` / `mission_4bed13`. That covers the race HUD's speed, gear, time and route elements.
   The disables inside it (rivals, hearts, markers), DispTimeAttack2D's forced sides, the put_scroll positions, ghost
   gap and C2C come in later slices.
4. Mode 1 then applies (no fallback). The default stays 2 until the visual check (step 4).
5. Tests:
   - the side rule and the extended mapping as pure functions, fixture-tested at 16:9 and on a span's centre panel;
   - modes 2 and 0 unchanged (the existing checks);
   - test_d3d11_vertex_path for the device path.

Under triples the HUD stays on the centre panel (ext from the centre panel's 16:9), as STD-007.

## Scope map: Tweaks' sites -> the decomp (first pass, 2026-10-12)

- **Method:** every `create_mid` / `create_inline` site in `hooks_uiscaling.cpp` (107 sites). Each is matched to the
  decomp function whose name ends at the nearest PC address at or below it.
  - For the instruction-level translations (`bulk_tr.cpp`), the match is checked by the address comments in the
    function's body.
  - Offsets are from that function's start.
  - "Hand" = a per-element reconstruction (names by address); "translated" = `bulk_tr.cpp`'s instruction-level code
    (`F_xxxxxx`, `Cpu& c`).
- The two scripts that produced it (`ui_scope_map.py`, `tr_owner.py`, read-only) are committed with step 2.

| Tweaks sites (handler) | Decomp holder | Kind | Note |
|---|---|---|---|
| 4293F3 drawFootage (applies the shift) | `layer_429350` +A3, pc_sprite_2d.cpp | hand | runs at render, from the recorded draws |
| 42A556 draw_sprite_custom_matrix (mode 2 centring) | `draw_42a3a0` +1B6, pc_sprite_2d.cpp | hand | the decomp centres rhw 2D in d3d11_d3d9 already |
| 42B1E2 / 42B53E / 42BB2F custom_matrix_multi cases | `mask_42a800`, pc_sprite_2d.cpp | hand | the PC function runs to 42BE60 (quad_42be60) |
| 439400 D3DXMatrixTransformation2D (inline) | `pc_d3dx_transformation_2d`, pc_sprite_2d.cpp | hand | |
| 449940 Calc3D2D | `project_449940`, race_particles_effects.cpp | hand | exact |
| 4BEB31 on, 4BEDBE on, 4BEDE0 / 4BEE21 off (NaviPub_Disp) | `navi_pub_display_4beb00_impl`; 4BEDxx in `mission_4bed13`, race_hud_navi.cpp | hand | the decomp splits NaviPub_Disp into parts |
| 4BEB8E / 4BEBAF rival, 4BEC83 / 4BEC88 online rival | `navi_pub_display_4beb00_impl` | hand | around rival_markers_4bb640 / navi_rival_labels_4bad20 / lan_markers_4bb300 |
| 4BEBE1 / 4BEBE6 hearts, 4BECBA / 4BECE0 C2C hearts | `navi_pub_display_4beb00_impl` | hand | the heart displays go through `s.heart(...)` services |
| 4BEA64 off (TimeAttackGoal) | `navi_sector_banners_4bea50` +14 | hand | |
| 4BA0E0 off (dispMarkerCheck) | `line_of_sight_4ba0e0` +0 | hand | exact |
| 4BE4BC right, 4BE4C1 off, 4BE4E7 left, 4BE575 on (DispTimeAttack2D) | `stages_4be4b0` | hand | |
| 4BE5CD ... 4BE9A3 put_scroll right (15) | `stages_4be4b0` +11D ... +4F3 | hand | |
| 4B9F3A ... 4BA052 put_scroll right (8, DispRank) | `position_4b9d80` +1BA ... +2D2 | hand | |
| 4B9096 / 4B90B3 / 4B90F6 put_scroll left (REV) | `gear_4b9010` +86 ... +E6 | hand | Tweaks rounds this spacing (REV light offset) |
| 4BDE3A PutGhostGapInfo; 4BDAE8 its sub | `banner_4bddb0` +8A; `record_row_4bdac0` +28 | hand | |
| 4BE045 / 4BE083 left, 4BE067 / 4BE0A5 right (DispGhostGap) | `banners_4be020` | hand | |
| 4BBA89 DispTempHeartNum ("-") | `request_row_4bb9e0` +A9 | hand | |
| 4BD32E slipstream; 4BD397 / 4BD414 / 4BD472 don't-lose-GF | `scores_4bd230` +FE; `scores_4bd340` | hand | C2C |
| 496AC7 ... 496C6A C2C speech bubbles (7) | `mission_manager_control_496a30`, mission_manager.cpp | hand | |
| 4FC84E ... 4FE8B1 C2C GF bubbles, rank emoji, rank text (16) | `F_4fc830`, `F_4fc9a0`, `F_4fcd40`, `F_4fd4e0`, `F_4fe760`, bulk_tr.cpp | translated | an adjustment would sit at the translated instruction (its esp+0 / +4 argument) |
| 460A21 set_icon_work (GF request bubble) | `banner_open_460970` +B1, race_traffic_heart_setup.inc | hand | the heart code lives in `race_traffic_*.inc` (a first scan of .cpp files missed it) |
| 460D40 / 460FBC ctrl_icon_work | `banner_460ad0` +270 / +4EC, race_traffic_heart_meter.inc | hand | the function's PC extent is to be confirmed |
| 481A86 / 481A8B C2C fruit, 481B76 C2C hearts | inside 481A40, the C2C request manager's GOAL results (7F9460 method) | **not ported** | race_end_runtime.cpp runs it only if translated, and it is not (`EndUnported` "neither native nor translated"); nothing to map until that path is ported |

- **Open before step 2:** the translated C2C bubbles (`F_4fc830` ... `F_4fe760`): an adjustment in generated code, or
  a reconstruction first.
