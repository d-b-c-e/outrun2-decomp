# UI scaling for the desktop build (implementation and visual qualification note)

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
5. `UILetterboxing` (**built**, 9214ba3, sana 56327; not yet seen in game). Tweaks' D3DEndScene bars:
   - black left and right of the centred 4:3 over the finished frame;
   - 1 (the default) outside the race only, 2 always, 0 none, and only with UIScalingMode 1 or 2;
   - "the race" is Tweaks' is_in_game: 78026C GAME, SMPAUSEMENU, GOAL, TIMEUP, OUTRUNMILES or TRYAGAIN, or START
     once its stage 8367A8 is 65.
   Here:
   - the desktop present overlay clears the two rectangles (ClearView) before the badge and the F6 panel;
   - they end at the device's own 4:3 edges, so no 2D pixel is covered (Tweaks' half-pixel quads overlap one);
   - no bars while the 2D is not in the centred 4:3 (a 4:3 output, or UIScalingMode 0).

Triple screens: the HUD stays on the centre monitor in both planar and three-view modes, which is STD-007. With
three views, mode 1's spacing applies to the centre panel's 16:9 only.

## Step 2a (accepted, sana 38916; built: see the ROADMAP row and the commit)

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
     that shifts the layer matrix's m[12] by ± Tweaks' HUD amount (see below): detect moves x < 213 left and x ≥ 426 right; left / right force
     the direction.
3. NaviPub_Disp's scope, with its own disables, placed by the calls in 4BEB00..4BEE40 of the EXE:
   - on (detect) from 4BEB31: score, clocks, time attack, OutRun, Heart Attack, LAN and mission bodies, then speed,
     needle, gear, route, stage banner, warning (re-asserted at 4BEDBE);
   - unspaced inside it:
     - the rival labels / markers (4BEB8E .. 4BEBAF);
     - the heart service 462CD0 (4BEBE1 .. 4BEBE6);
     - the LAN markers (4BEC83 .. 4BEC88);
   - off from 4BEDE0: level, split, ghost, lap, countdown, and the 4BEDF0 tail (its own guard, since the decomp
     reaches it early for mode != 16).
   - Guards (`UiSpacingGuard`, platform/ui_spacing.hpp) restore the scope on every return.
   - DispTimeAttack2D's forced sides, the put_scroll positions, ghost gap and C2C come in later slices, so mode 1 is
     **partial**: the log note says so.
4. Mode 1 applies (no fallback). The default stays 2 until the visual check (step 4). Pool sprites drawn by 428170
   capture the scope current then (off), as Tweaks' global would be.
5. Tests:
   - `test_triple_screens`: parse (1 is partial), the rectangle and spacing at 16:9, at 21:9 and on a span's centre
     panel (4:3 plus both spacings = the centre panel, never past it), the side rule, and the guard's nesting;
   - `test_d3d11_vertex_path`, on WARP:
     - an in-range pretransformed quad has the same pixels with and without spacing;
     - one left of the 4:3 is clipped in mode 2 and drawn at the same scale in mode 1;
     - one beyond the extension stays clipped.

Under triples the HUD stays on the centre panel (ext from the centre panel's 16:9), as STD-007.

**Correction (after 2b): the HUD moves by Tweaks' amount, not by the margin.**
- Tweaks' AddSpriteSpacing adds margin_px / screen_scale.x to a 640-space x, and its mode-1 sprite path draws that x
  at screen_scale.y (the draw_sprite_custom_matrix_mid and D3DXMatrixTransformation2D hooks).
- On screen that is margin x scaleY / scaleX: in these units 320 (1 - 4 / (3A)), i.e. 80 at 16:9 (three quarters of
  the 106.7 margin) and 137.1 at 21:9.
- `ui_scaling_rect` returns it as `.hud`, and pc_screen.hpp `g_pc_ui_spacing` carries it for layer_429350 and the
  put_scroll fixes.
- The device extension stays the full margin (`.spacing`), so nothing moved is clipped.
- 2a and 2b as first committed moved by the margin; fixed before integration.

## Step 2b (sana 40119; built; review pending)

Each site placed by the call it precedes in the EXE (objdump of OR2006C2C.exe around each Tweaks address):
- **put_scroll positions** (`put_shift()`: the spacing rounded, as Tweaks' integer AddSpriteSpacing; 0 outside mode 1):
  - DispTimeAttack2D `stages_4be4b0`: all fifteen 42D280 calls move right. They are 12 source sites, because the
    `stage_label` lambda is called from two places.
  - DispRank `position_4b9d80`: all eight 42D280 calls move right.
  - DispGearPosition (REV) `gear_4b9010`: two 42D280 calls and one 42D5F0 move left.
  - DispTempHeartNum `request_row_4bb9e0`: the 42CC00 text position moves right.
  - PutGhostGapInfo `banner_4bddb0`: the name x moves (102, the left banner, left; otherwise right).
  - Its sub `record_row_4bdac0`: the float copy of f behind x0 moves (4.0 left, otherwise right), unrounded. Later
    uses of f are unchanged, as in the EXE.
- **Scopes, as Tweaks sets its global** (no restore inside; `UiSpacingGuard` restores at the display's exit, early
  returns included):
  - DispGhostGap `banners_4be020`: banner 0 forced left, banner 1 forced right, set before each call.
  - DispTimeAttack2D: the sector list forced right (4BE4BC), off for the target marker (4BE4C1), forced left for the
    car / position badges (4BE4E7), detect from 4BE575.
  - dispMarkerCheck `line_of_sight_4ba0e0`: off at entry, not restored, so the marker drawn after it is unspaced.
  - NaviPub_Disp re-enables at 4BEDBE before speed / needle / gear / route / banner.
  - DispTimeAttackGoal `navi_sector_banners_4bea50`: the banners' last side carries over to the sector list; a guard
    restores the scope it found.
- **Not in 2b:**
  - the C2C speech bubbles, in translated code (`F_4fc830` ... `F_4fe760`) and in `mission_manager_control_496a30`;
  - the GF request bubble (`banner_open_460970` / `banner_460ad0`);
  - the C2C goal results (481A40, not ported).
- **Evidence:** a build plus the unchanged default path (`put_shift()` is 0 outside mode 1); test_triple_screens 591
  and test_d3d11_vertex_path 407 pass. No HUD fixture exists for these routines, so mode 1's rendered positions await
  the visual check (step 4).
## Step 2c: the remaining C2C sites (hand-reconstructed and translated positions built; 481A40 not ported)

From objdump of OR2006C2C.exe around each site; "R" is right by the spacing, unrounded for floats and rounded for
ints.
- **Scores, hand-reconstructed** (`race_hud_navi.cpp`), put_scroll R through `put_shift()` as in 2b (**built**):
  - "test your slipstream": 4BD32E, the one 42D280 call in `scores_4bd230`;
  - "don't lose your girlfriend": 4BD397 / 4BD414 / 4BD472, the three 42D280 calls in `scores_4bd340`.
- **Mission-manager speech bubbles, hand-reconstructed** (`mission_manager.cpp`, `mission_manager_control_496a30`):
  - the seven sites (496AC7, 496B14, 496B39, 496B94, 496BE1, 496C10, 496C6A) each move the float on top of the stack,
    which is the bubble's x of 170.0 (0x432A0000);
  - four are 465860's x (the decomp's `configure(..., 170.0f, ...)`) and three are 4653C0's x (`motion`: 4653C0(170,
    -80, 0.2, 0));
  - the decomp calls `motion` four times (lines 339 / 340 / 344 / 345), against three hooked 4653C0 sites in the EXE:
    the second element's two calls share the tail at 496C54 .. 496C6A, so all four are hooked.
  - **Built:** the four `configure(..., 170.0f + HUD shift, ...)` and `motion`'s 4653C0 x (floats, unrounded).
- **The GF request bubble, hand-reconstructed** (`race_traffic_heart_*.inc`):
  - 460A21 (`banner_open_460970` +B1): a float x in xmm0;
  - 460D40 / 460FBC (`banner_460ad0` +270 / +4EC): float x in xmm0; at 460FBC Tweaks also writes it back to [esp].
  - **Built:** x 176 ([5B02A4]) plus the HUD shift in the open (460A21) and reopen (460FBC) stores, and in their
    translations (Tweaks writes the moved xmm0 over the pushed 176). The bobbing centre 7F8BBC also moves (460D40).
- **GF speech bubbles, rank emoji, rank text: translated code** (`bulk_tr.cpp` `F_4fc830`, `F_4fc9a0`, `F_4fcd40`,
  `F_4fd4e0`, `F_4fe760`; 18 Tweaks hook sites).
  - **Built in source:** `c2c_ui_spacing.hpp` maps each hook to its external 465860/4653C0 call's return PC and x argument.
    `race_translated.cpp` adds the mode-1 HUD shift to that float when constructing `PcRaceCall`. The exceptional
    4FD60C hook becomes 4653C0 argument 1 after an intervening push; the other 4653C0 sites use argument 0 and the
    465860 sites use argument 5. Unmatched return PCs and targets, and modes 0/2, keep all argument bits unchanged.
  - `bulk_tr.cpp` is generated and remains unedited; the call-boundary mapping survives its regeneration. The focused
    `test_triple_screens` fixture checks all 18 mappings and negative controls. C2C rendered positions await step 4.
  - **Offline evidence, 2026-10-14 (`b22673f`):** isolated VS 2022 ClangCL `OR2_XBOX=ON` Release build of
    `test_triple_screens` and `OutRunDesktop` passed; CTest `triple_screens` passed 1/1. Logs are in the coordination
    hub at `artifacts/outrun-c2c-ui-20261014/{configure,build,focused-test}.log`. The fixture exercises the bridge
    helper with the source-derived call table; it does not execute a real translated caller or verify rendered C2C UI.
  - **Real-dispatch evidence, 2026-10-14 (`9479e1e`):** `test_triple_screens` rebuilt and passed CTest 1/1 with the
    actual generated `F_4FC830` caller routed through `translated_call` and the production external dispatcher. Its
    first three 465860 calls shift the x argument by 80 in mode 1; all other arguments and the nearby unlisted 4653C0
    call are unchanged. Logs: `artifacts/outrun-c2c-ui-20261014/{real-dispatch-build,real-dispatch-test}.log` in the
    coordination hub. Other translated callers and rendered C2C placement still await qualification.
- **C2C hearts / fruit** (481A86 / 481A8B / 481B76): scope switches inside 481A40, the C2C goal results, which the
  decomp does not run yet (`EndUnported`). Nothing to do until that path is ported.
## Step 4, race route only: mode 1 against mode 2 (sana 40590 / 40633; 2026-10-12)

- **Setup:** one bounded run per mode with the same exe (6547538, `ui1`), muted, in the perf copy. Window
  2560x1440 (16:9, so the UI is 3 px per unit and spacing is 80 units), ordinary FOV. Route: OutRun Palm Beach start,
  about 20 s of driving.
- **Captures:**
  - PrintWindow shots at t+30 / 40 / 50 s;
  - the 60 fps clip, which holds the window at half scale.
- **Method:** each element is cut from the mode-2 shot and searched for in the mode-1 shot. The search covers ±400 px
  across and ±6 px down, by minimum mean absolute difference. For each HUD element the best match scores at least
  1.8 times better than the best position more than 8 px away. The car moves with the scene, so its control match
  is weaker at t+50: 16.6 against 19.7.
- **Displacement, measured at t+40 and t+50 (both agree):**

  | Element | Mode 2 x (px) | Moved | Mode 1 x (px) |
  |---|---|---|---|
  | Total Time label + timer | 470 | −240 | 230 |
  | position map | 476 | −240 | 236 |
  | speedometer dial | 480 | −240 | 240 |
  | km/h | 876 | −240 | 636 |
  | gear row | 516 | −240 | 276 |
  | Score label | 1630 | +240 | 1870 |
  | Score bar (to its end) | 1850..2050 | +240 | 2090..2290 |
  | centre Time | 1050 | 0 | 1050 |
  | player car (control) | 1020 | 0 | 1020 |

  - −240 / +240 px is the expected 80 units at 3 px per unit, with no vertical change.
- **Clipping:**
  - **Normal HUD:** every moved element is fully drawn. None reaches a window edge; the extremes are 230 px on the
    left and 2290 px on the right.
  - **New in mode 1: a "1P" banner in the start camera:**
    - Red "1P" at x 2251..2348, y 180..240.
    - A grey bar runs from about 2320 px to the window's right edge (2560), where the window cuts it off.
    - It holds that position through the countdown camera (every clip frame from the camera cut to the HUD's
      arrival) and is gone once the race HUD is up.
    - In mode 2 it appears in no frame of the same phase. That fits an element that sits past the 4:3 right edge
      (643.7..676 units if unshifted). Mode 2's 4:3 viewport hides it, and mode 1's full-screen extension shows it. A
      +80 shift alone would have left it on screen in mode 2.
    - **Source trace (sana GO 40933 a; static, no code change):**
      - **Owner:** the START display. GAD_PUB display 4998C0, mode 13, calls 499A30 (`gad_start_499a30`,
        `race_end_modes.cpp`).
      - **When it runs:** only with 43F860 = [780260], the console flag that the SUMO_FE init sets. That holds in
        this build's frontend races.
      - **What it draws:** the player-list animation 0x490001 at (0, 0), layer 4. Its frame is the START frame
        count minus 50, capped at 5 per player; the player count 8367F0 comes from 456D60, which is 1 offline.
      - **Single player:** the frame stops at 5, and the function returns before the per-player icons
        (`players <= 1`). The player list's "1P" row is left where frame 5 puts it, just past the 640 edge.
      - **Spacing:** the draw is queued outside NaviPub_Disp, with `ui_spacing_scope()` off, so it is unshifted.
        That is consistent with the measured position.
    - **Tweaks (code reading, no run):**
      - No `hooks_uiscaling.cpp` site touches 499A30.
      - Its replacement for draw_sprite_custom places every sprite at scaleY·x plus the 4:3 offset
        (`hooks_uiscaling.cpp`, the KeepCentered / OnlineArcade branch), and nothing clips to the 4:3 area.
      - So Tweaks' modes 1 and 2 should both draw this "1P" at x 2251 at 2560x1440. Vanilla stretches 640 to the
        screen edge, which keeps it off screen.
      - In the decomp, mode 1 matches that reading, and mode 2's 4:3 viewport hides it, as vanilla does.
    - **Not confirmed:**
      - frame 5's layer positions in 0x490001 (no sprani dump was made);
      - a Tweaks frame of the start camera.
- **Limits:**
  - one route, one run per mode, one aspect;
  - not exercised:
    - time attack (sectors, rank, REV, ghost gap, goal banners);
    - C2C (missions, bubbles, scores);
    - rivals, LAN, records, the heart meter;
  - no performance or frame-time reading.
- **Evidence:** the project hub's `artifacts/outrun-perf-20261011/ui-scaling-20261012/` holds:
  - shots, logs and the 1P crop;
  - the two measuring scripts, `hud_shift.py` and `red_bbox.py`.

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
| 4FC84E ... 4FE8B1 C2C GF bubbles, rank emoji, rank text (18) | `F_4fc830`, `F_4fc9a0`, `F_4fcd40`, `F_4fd4e0`, `F_4fe760`, bulk_tr.cpp | translated | source bridge in `race_translated.cpp` and the 18-site return-PC table in `c2c_ui_spacing.hpp`; generated code unchanged, visual check pending |
| 460A21 set_icon_work (GF request bubble) | `banner_open_460970` +B1, race_traffic_heart_setup.inc | hand | the heart code lives in `race_traffic_*.inc` (a first scan of .cpp files missed it) |
| 460D40 / 460FBC ctrl_icon_work | `banner_460ad0` +270 / +4EC, race_traffic_heart_meter.inc | hand | the function's PC extent is to be confirmed |
| 481A86 / 481A8B C2C fruit, 481B76 C2C hearts | inside 481A40, the C2C request manager's GOAL results (7F9460 method) | **not ported** | race_end_runtime.cpp runs it only if translated, and it is not (`EndUnported` "neither native nor translated"); nothing to map until that path is ported |

- **Open:** C2C rendered positions and goal results (481A40 is not ported); the translated positional bridge is in source.
