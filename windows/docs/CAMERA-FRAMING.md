# Native camera framing — November 2, 2026

This is built source and offline fixture evidence. It is not installed or visually accepted. The existing installed Decomp stays at 2dd9886 / executable 62E5531E.

## Player controls

During ordinary play, F7 switches the current native player camera between Game and Realistic framing when three angled views are active. Numpad + zooms in, minus zooms out, and 5 resets that camera/set's FOV offset to the calculation. F6 Cameras offers the same commands. The label gains `(Custom)` when the active FOV offset is nonzero; a camera/set/reset notice appears for about three seconds near the bottom center.

The three normal views in `race_camera_view_switch_484a40` are 0 bumper, 1 in-car, and 2 chase. `camera_fix_znear` and the model-specific in-car table in `race_camera_eye_look_485590` corroborate these identities. Other entries are authored start/goal/cinematic views and retain the game FOV. There is no newly invented bonnet camera.

| Native view | New angled-triple start | Single screen / Planar |
|---|---|---|
| Bumper (0) | Realistic | Game |
| In-car (1; `Cockpit` keys) | Realistic | Game |
| Chase (2) | Game | Game |

Realistic uses `2 atan(PanelWidthMm / 2 / EyeDistanceMm / (16/9))`. EyeDistanceMm is the distance to the plane through the center panel edges, with no sagitta subtraction. The one borderless window remains the presentation for Surround and separate monitors. A FOV wider than seat-true stretches the side screens even with angle correction.

## Settings and compatibility

Only an explicit action saves, through the byte-preserving writer; loading never migrates/reformats owner files. `[Camera]` keys:

| Key family | Values / ownership |
|---|---|
| `BumperRealistic`, `CockpitRealistic`, `ChaseRealistic` | true / false, saved separately by F7 or F6; defaults above |
| `BumperGameFovOffset`, `CockpitGameFovOffset`, `ChaseGameFovOffset` | Vertical degrees added to each game's calculated FOV, shared across cars |
| `BumperRealisticFovOffset`, `CockpitRealisticFovOffset`, `ChaseRealisticFovOffset` | Vertical degrees added to the measured-seat calculation, shared across cars |

An explicit legacy `[Triple] MatchGameFov` seeds each absent per-camera choice (true -> Game, false -> Realistic). An explicit new camera key wins. On a single screen/Planar only the Game offset applies; absent offsets preserve the original FOV and horizontal projection values. Switching sets retains both offsets. A failed save reports `Camera setting not saved` and retains the previous active value. UTF-16/malformed files refuse writes; the native reader supports UTF-8 BOM and ordinary byte INIs.

The camera projector recalculates from authored `+AC` first, then applies the FOV adjustment to `+A0/+A4`. It never writes the offset back to `+AC`, so dynamic chase FOV cannot compound each tick. The callback is armed only for the main race/goal camera and cleared afterward. Interpolation replays preserve the tick's projection planes/panel cache while updating the view.

Hotkeys use the established own-key release edge and suppress panel/focus clears. A held driving key or H-pattern binding does not block a camera release. Shift+F7 does **not** save the ordinary set choice; the separate developer comparison is still pending. New camera hotkeys are disabled in test, recording and playback processes to preserve their existing presentation contract. The existing global physical-FOV path remains available to those processes.

## Evidence and remaining work

- Release `OutRunDesktop` and the named fixture targets built with VS2022 / ClangCL / OR2_XBOX.
- `desktop_controls`, `desktop_bindings`, `camera_controls`, `camera_render_binding`, `triple_screens`, `screen_edge_cull`, `outrun_adapter`: **7/7 pass**. `triple_screens`: **1,213 checks**; real controller fixture: **22 checks**. Camera binding reads the existing `OR2006C2C.cache` through OR2_EXE; it skips without that external fixture.
- `camera_controls` uses real controller/settings code with inert internal keyboard/runtime records and a temporary INI, replacing only the input-chain/panel-open boundaries. It creates no window/device/game. It covers separate sets/cameras, zoom/reset, a held unrelated key, Shift+F7, panel/focus suppression, explicit legacy/new preference precedence, and failed-publication retention.
- Private logs: `work/validation-20261102/camera-final-{build,tests}.log`; additional writer logs `settings-{build,tests}.log`. No physical input, FFB, overlay image, driving or owner acceptance follows from these passes.
- Additional `write_audit` and its selftest fail on **30 identical baseline findings**, reproduced in a clean detached fccf6d6 worktree (before the writer/camera changes). New/changed writer calls have explicit classifications; baseline/current normalized finding sets match. Logs: `write-audit-{current,baseline-fccf6d6}.log` and the corresponding `write-audit-selftest-*` logs. Seven focused passes are not a clean full-suite/sandbox result; the pre-existing audit drift needs a separate source trace before any latched qualification.

Remaining STD-041..049 work: per-car position offsets and full numpad layout; model-derived mounted positions/body hiding/near clip/smoothing; prove last-camera restore through the native license `+D8` start preference; configurable camera bindings and F6 sliders; session-only developer comparison; curve/source-eye compositor and N-display mapping. Legacy Planar/Angled choices remain exposed and defaults are not flipped while custom programs/particles and rendered projection are unqualified. Wheelkit must not advertise a new per-camera capability until an installed candidate/receipt declares it.
