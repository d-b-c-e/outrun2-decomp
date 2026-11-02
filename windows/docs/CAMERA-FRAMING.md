# Native camera framing — November 2, 2026

This is built source and offline fixture evidence. It is not installed or visually accepted. The existing installed Decomp stays at 2dd9886 / executable 62E5531E.

## Player controls

During ordinary play, F7 switches the current native player camera between Game and Realistic framing when three angled views are active. Numpad + zooms in, minus zooms out, and 5 resets that camera/set's FOV offset to the calculation. F6 Cameras offers the same commands. The label gains `(Custom)` when the active FOV offset is nonzero; a camera/set/reset notice appears for about three seconds near the bottom center.

The three native views in `race_camera_view_switch_484a40` are 0 bumper, 1 in-car, and 2 chase. The November 2 extension adds **Bonnet as its own view**, giving the ordinary cycle **bumper -> bonnet -> cockpit -> chase**. It uses the native bumper slot with a separate logical identity/render transform; native cinematic/table slots >=3 are not hijacked. The cockpit remains available and its unadjusted pose stays native. An absent last-camera setting preserves the game's start/default view.

| Native view | New angled-triple start | Single screen / Planar |
|---|---|---|
| Bumper (0) | Realistic | Game |
| In-car (1; `Cockpit` keys) | Realistic | Game |
| Chase (2) | Game | Game |
| Bonnet (logical 3) | Game | Game |

Realistic uses `2 atan(PanelWidthMm / 2 / EyeDistanceMm / (16/9))`. EyeDistanceMm is the distance to the plane through the center panel edges, with no sagitta subtraction. The one borderless window remains the presentation for Surround and separate monitors. A FOV wider than seat-true stretches the side screens even with angle correction.

## Settings and compatibility

Only an explicit action saves, through the byte-preserving writer; loading never migrates/reformats owner files. `[Camera]` keys:

| Key family | Values / ownership |
|---|---|
| `BumperRealistic`, `CockpitRealistic`, `ChaseRealistic` | true / false, saved separately by F7 or F6; defaults above |
| `BonnetRealistic` | Separate new view, defaults Game; legacy MatchGameFov does not override its new default |
| `LastCamera` | `Bumper`, `Bonnet`, `Cockpit`, `Chase`; saved by the ordinary view-cycle action and restored at stage start. Absent/`Default` keeps the native license preference |
| `BumperGameFovOffset`, `CockpitGameFovOffset`, `ChaseGameFovOffset` | Vertical degrees added to each game's calculated FOV, shared across cars |
| `BumperRealisticFovOffset`, `CockpitRealisticFovOffset`, `ChaseRealisticFovOffset` | Vertical degrees added to the measured-seat calculation, shared across cars |

An explicit legacy `[Triple] MatchGameFov` seeds each absent **native** per-camera choice (true -> Game, false -> Realistic). An explicit new camera key wins. Bonnet has its own `BonnetGameFovOffset` / `BonnetRealisticFovOffset` keys. On a single screen/Planar only the Game offset applies; absent offsets preserve the original FOV and horizontal projection values. Switching sets retains both offsets. A failed save reports `Camera setting not saved` and retains the previous active value. UTF-16/malformed files refuse writes; the native reader supports UTF-8 BOM and ordinary byte INIs.

## Model-derived placements

`platform/camera_mount_fit.hpp` ports Art `CameraMountFit`/`CameraMountMemory` at fc23361c. The body comes from the first object token of the native 5B2F68 layout (the actual main exterior-body draw), not a union of wheel, light, accessory, shadow or effect objects. The fitter decodes the existing PMT read-only, slices triangles at three center-strip x planes into 40 top-surface samples, then walks from the front using Art's 0.35 rise/metre windscreen threshold. A front-face climb above 0.9 m selects the cab-over roof edge. Body plausibility checks reject unsuitable geometry; Art's explicit box fallback remains when the profile has no windscreen step.

- Bonnet: center line, 10 cm ahead of the detected windscreen base and 8 cm above the bonnet. Cab-over: the roof front edge, 8 cm above its surface.
- Bumper: center line, 5 cm ahead of the body front, halfway up the front face with height capped at 45 cm above the body bottom. Native visibility bits hide the player's own body/wheels; Bonnet keeps the exterior visible.
- Both use a 5 cm projection near plane; the authored camera near value is preserved. Reflection/effect passes retain/restore the actual projection/frusta.
- Position/pitch offsets live in `[Camera]` keys `Car<model>.<camera><Game|Realistic><Side|Up|Forward|Pitch>Offset` (metres/degrees). They are independent per stock model, camera and set. FOV remains shared across cars. Reset clears the current pose offsets and FOV offset atomically; it returns to the model's calculation, not a baked absolute position.

Mounts and offsets modify only the render view matrix before it is stored. Authored/smoothed eye, target and angle fields are not rewritten, so paused projections cannot compound an offset. The view follows the displayed car matrix, including display-only interpolation. Queue eye/target metadata comes from the resulting inverse view. Missing/unsupported geometry retains native placement and labels it explicitly.

Offline evidence: all **30/30 stock exterior bodies** yield valid profiled fits (102 checks including synthetic/invalid-input controls); the real controller/software-renderer fixture checks separate Bonnet/cockpit cycling, per-car/set offsets, reset, body visibility, native cinematic isolation, actual cached PMT consumption, 5 cm projection, repeated paused frames and interpolation without altering authored camera fields. No retail geometry bytes or extracted models were committed. These are calculated starts, not a visual claim that every detected slope is the actual windscreen; physical calibration remains owner-attended.

The camera projector recalculates from authored `+AC` first, then applies the FOV adjustment to `+A0/+A4`. It never writes the offset back to `+AC`, so dynamic chase FOV cannot compound each tick. The callback is armed only for the main race/goal camera and cleared afterward. Interpolation replays preserve the tick's projection planes/panel cache while updating the view.

Hotkeys use the established own-key release edge and suppress panel/focus clears. A held driving key or H-pattern binding does not block a camera release. Shift+F7 does **not** save the ordinary set choice; the separate developer comparison is still pending. New camera hotkeys are disabled in test, recording and playback processes to preserve their existing presentation contract. The existing global physical-FOV path remains available to those processes.

## Adjustment keys and migration

The full STD-005/048 keyboard layout is live on the current camera: 8/2 forward/back, 9/3 up/down, 4/6 left/right, 7/1 look down/up, + zoom in, minus zoom out, 5 reset. Camera-space translations and visible tilt direction are checked through the real view-matrix consumer. F7 retains its separate own-key release edge. A held adjustment starts repeating after 350 ms at at most 10 actions/second; release adds no extra step after repeating. Focus/panel clears suppress the action until its own key is released. Reset never repeats.

F6 Cameras > Adjustment bindings offers each key and Unbound, plus Restore numpad defaults. Custom shortcuts may use the existing named keyboard keys; unsupported/reserved or duplicate adjustment assignments are refused with an error. F6/F7/F8/F11/Escape remain protected. Keyboard shortcuts still reach the game; wheel-button camera adjustments are not added by this keyboard-only increment. F6 Advanced exposes the four unconstrained position/pitch offsets and per-press steps: move 0.02 m, tilt 1 degree, FOV 2 degrees by default. Step keys are `MoveStepMetres`, `TiltStepDegrees`, `FovStepDegrees`; resetting these or bindings does not reset the player's pose/FOV offsets.

Bindings use `[Camera] KeyUp`, `KeyDown`, `KeyForward`, `KeyBack`, `KeyLeft`, `KeyRight`, `KeyPitchDown`, `KeyPitchUp`, `KeyFovUp` (widen), `KeyFovDown` (narrow), `KeyReset`. `NUMPAD`, `Numpad` and Unity `Keypad` aliases are understood. A complete saved set exactly equal to an earlier family default (including either original/interim tilt ordering or the old +/- direction) migrates to the current layout in memory. Partial/custom sets retain every explicit assignment. Loading writes nothing and reports migration in the log/F6; the first explicit binding edit saves the full migrated set atomically to avoid mixed defaults on reload. Fixtures cover unchanged/custom/default sets, all eleven action consumers, rebinding/conflict refusal, independent car/set storage, default scopes, hold timing and visible look-down direction.

## Last-camera restore

The ordinary view-cycle action saves only the logical camera name. At the native single-player START timer `0x12b` (`race_camera_start_485250`) and LAN START timer `0x167` (`race_camera_start_lan_485300`), a scoped callback substitutes that choice where the game normally copies license `+D8` / `7C24B8` into camera `+34A`. The license bytes are not rewritten. Bonnet restores native slot 0 plus its separate logical flag. Absent, `Default` or invalid settings retain the native preference; invalid names are logged. Loading and restoring write nothing. Test/record/playback processes do not install the callback.

A failed last-camera save still changes the current view, displays `last camera not saved`, and retains the previously saved choice for the next stage. This differs from pose/FOV editing, where a failed save retains the prior active value. Fixtures cover reload, failed publication, native-default fallback, the actual software-rendered single-player START route and the LAN function with inert records; no network session is started.

## Evidence and remaining work

- Release `OutRunDesktop` and the named fixture targets built with VS2022 / ClangCL / OR2_XBOX.
- `desktop_controls`, `desktop_bindings`, `camera_controls`, `camera_render_binding`, `triple_screens`, `screen_edge_cull`, `outrun_adapter`: **7/7 pass**. `triple_screens`: **1,213 checks**; real controller fixture: **22 checks**. Camera binding reads the existing `OR2006C2C.cache` through OR2_EXE; it skips without that external fixture.
- `camera_controls` uses real controller/settings code with inert internal keyboard/runtime records and a temporary INI, replacing only the input-chain/panel-open boundaries. It creates no window/device/game. It covers separate sets/cameras, zoom/reset, a held unrelated key, Shift+F7, panel/focus suppression, explicit legacy/new preference precedence, and failed-publication retention.
- Private logs: `work/validation-20261102/camera-final-{build,tests}.log`; additional writer logs `settings-{build,tests}.log`. No physical input, FFB, overlay image, driving or owner acceptance follows from these passes.
- Additional `write_audit` and its selftest initially failed on **30 identical baseline findings**, reproduced in a clean detached fccf6d6 worktree. After tracing actual call destinations/latch guards, updating moved/changed classifications and excluding the standalone vendored panel fixture (not linked into OutRunDesktop), both now **pass 2/2**: **242 classifications**, **43 mutation checks plus read-only controls**, zero findings/failures. New guard checks cover preferences, texture extraction, camera saves, trace sinks and report export. Baseline/failure logs remain in `work/validation-20261102/write-audit-*`; final output is `write-audit-reconciled-{ctest,detail}.log`. The disposable baseline checkout was clean and removed after retaining its evidence. This source tripwire is not runtime sandbox or full-suite proof.
- Final camera increment: Release desktop and named targets built; `camera_controls|camera_render_binding|camera_mount_fit|pc_scene_renderer|triple_screens|write_audit|write_audit_selftest` **7/7 pass**. The real controller plus cached F50/software-renderer run passes **109 checks** (standalone controller 91); the retail fitter remains **102 checks / 30 profiled bodies**. Final audit: **246 classifications / 44 mutations**, zero findings/failures. Logs: `camera-restore-{build,renderer-inert,ctest}.log` under the same ignored validation folder. The earlier seven-fixture and audit counts above describe their own checkpoints.

Remaining STD-041..049 work: independent rotation smoothing/calibration, F6 FOV slider, wheel-button adjustment bindings, session-only developer comparison, curve/source-eye compositor and N-display mapping. Legacy Planar/Angled choices remain exposed and defaults are not flipped while custom programs/particles and rendered projection are unqualified. Wheelkit must not advertise a new per-camera capability until an installed candidate/receipt declares it.
