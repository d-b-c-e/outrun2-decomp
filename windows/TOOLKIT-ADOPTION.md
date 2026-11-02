# Toolkit standards adoption

Which entries of the wheel toolkit's standards ledger
(`E:\Source\toolkits\dbce-wheel-mod-toolkit\STANDARDS.md`) the desktop build has
brought in. Update a row in the same commit that adopts it. Statuses: `adopted`,
`partial`, `pending`, `n/a` (say why), `unchecked`. Seeded 2026-10-10.

| Standard | Title | Status | Notes |
|---|---|---|---|
| STD-001 | One mod per game | partial | Owner, 2026-10-10: a separate product (`outrun-2006-decomp`) beside `dbce-mods-outrun-2006` for now; the decomp will eventually replace the Tweaks mod entirely. The one-time Tweaks `[DirectInput]` import is the settings migration path. |
| STD-002 | Recording and playback from launch | n/a | Superseded by STD-012. |
| STD-003 | Normalized FFB strength | pending | A default-off force path is built, but physical output and cross-game strength have not been qualified; see `ROADMAP.md` step 4. |
| STD-004 | Consistent settings UX | partial | The F6 panel's five pages and Stop FFB/F8 path are in source; all pages were rendered muted in disposable copies. Keyboard/mouse saving and physical force remain unqualified; see `docs/F6-PANEL.md`. |
| STD-005 | Camera numpad layout | partial | Full 8/2 9/3 4/6 7/1 +/- 5 layout, keyboard rebinding and untouched-default migration built/tested through actual pose/FOV consumers. Custom sets are preserved. Physical/UI acceptance and wheel-button adjustment bindings remain open; docs/CAMERA-FRAMING.md. |
| STD-006 | Camera step sizes are settings | partial | MoveStepMetres/TiltStepDegrees/FovStepDegrees default .02/1/2; F6 Advanced step controls and defaults command, actual consumer fixtures. Rendered control interaction remains unqualified. |
| STD-007 | Triple screens in one wide window | partial | The planar span was seen at the rig. The default-off projected-view spike is not accepted as three independently rendered, angle-correct views; see `ROADMAP.md` step 3. |
| STD-008 | Display changes: game applies once | unchecked | |
| STD-009 | Dashboard telemetry matches the HUD | partial | The speed source is the HUD's +0x1F8 base and the packet byte fixture checks km/h to m/s; no attended numeric HUD/receiver comparison. See `docs/TELEMETRY-FIELDS.md`. |
| STD-010 | Install the latest build for testing | unchecked | |
| STD-011 | Work lands on main | partial | The current reviewed integration line is `codex/outrun-wheel-parity` at `6777eb3`, not `main`; upstream work still needs its own review. |
| STD-012 | Reproduce the route and preserve original signals | partial | Ordinary shim recording has consumed-input contract @2, a saved-while-alive pilot and offline fixtures; consumed replay has an input-tick seam and fixture. Neither proves a deterministic complete route or owner-accepted automated playback. See `docs/PLAY-WITH-RECORDING.md` and `ROADMAP.md`. |
| STD-013 | The installed build launches plainly | partial | `OutRunDesktop.exe` boots from its folder (windowed run 2026-10-10); plain borderless launch from LaunchBox not yet checked. |
| STD-014 | Request reciprocal review when progress stalls | adopted | Astra reviews the decomp's source slices. |
| STD-015 | Triples on Surround and separate monitors | partial | Surround wide-window and separate-monitor planar span are in source; a planar span was seen at the rig. Angle-correct projection, per-display output and full topology acceptance remain open (`ROADMAP.md` step 3). |
| STD-016 | Forza Horizon telemetry on by default | adopted | Source defaults to enabled Horizon 324-byte packets; SimHub and the owner's wheel screen received the earlier stream. This says nothing about every field after `6777eb3`; see STD-009 and `docs/TELEMETRY-FIELDS.md`. |
| STD-017 | Hide empty settings pages | partial | All currently enabled F6 pages have content (`source/desktop_panel.cpp`); source configures only those pages. Page visibility was seen muted, while keyboard/mouse interaction is not qualified (`docs/F6-PANEL.md`). |
| STD-018 | Handling changes never reach online scores | pending | Offline only so far; upstream has a LAN layer. Result/score guard for injected runs agreed in principle with Astra, not yet implemented. |
| STD-019 | Center menus and gameplay HUD | partial | The planar path centers 4:3 menus/HUD in source; F6 was rendered on a single screen. A three-screen F6 run, full black-side behavior and projected views remain unqualified (`ROADMAP.md` steps 2a-3). |
| STD-020 | Standard feature checklist | partial | The source/evidence-scoped grid below includes all current portfolio columns. |
| STD-021 | art of rally is the FFB reference | pending | |
| STD-022 | One triple-screen selector | partial | F6 Cameras shows Off / Surround / Separate monitors, plus a separate experimental projection choice. Rendered muted; saving through the panel and projected views remain unqualified (`docs/F6-PANEL.md`). |
| STD-023 | Frame-rate readout in settings and log | adopted | Toolkit `NoteFrame` is called from `desktop_panel.cpp`; the Help/Cameras readout and runtime log were seen in the muted panel checks (`docs/F6-PANEL.md`). This is a display readout, not a frame-pacing acceptance claim. |
| STD-024 | Optional on-screen frame-rate counter | partial | `[Panel] ShowFrameRate` is Off by default. F6 Cameras and Wheelkit can save it; when On, the D3D11 overlay draws FPS at the top right of the center game view while F6 is closed. Release desktop build and Wheelkit's focused setting write pass; rendered placement and measured accuracy remain unverified. |
| STD-025 | One shared force model | pending | |
| STD-026 | One force model for players | pending | |
| STD-027 | Independent steering and effect strengths | pending | |
| STD-028 | Complete raw registry preservation for game tests | n/a | The build keeps no registry state (settings, cache, saves and log are in its folder). |
| STD-029 | New functionality ships installed and turned on | partial | Wheel input and Horizon telemetry default on in source; F6 pages are built and rendered in disposable copies. Force output defaults on in source since 2026-11-01 (absent Output and Model: wheel, producer@2; not yet built into an install or felt at the wheel); projected views remain default-off; the current `6777eb3` telemetry correction has no owner install or live field check. |
| STD-030 | The F6 panel comes first | partial | Native toolkit panel is integrated with the owned D3D11 renderer and all five pages rendered muted; controls/save interaction and a three-screen-span check remain (`docs/F6-PANEL.md`). |
| STD-031 | art of rally's reference level | pending | |
| STD-032 | Wheelkit reads the portfolio's own records | partial | `product.json` and the `controls:` start line in `runtime.log`; Wheelkit catalog entry pending (Astra). |
| STD-033 | Rig profile controls reach the game, and a test proves it | partial | Canonical `[Controls]` (`ini-controls-1`, vendored `dbce_controls.hpp`), start line with named unavailable actions, one-time legacy import, test injection + observer. Configured only: input observed and owner acceptance pending. |
| STD-034 | A rig profile Apply is qualified by a closed loop | pending | Harness design with Astra. |
| STD-035 | Writers and test injection: identity, provenance and failure rules | partial | Instance-GUID-only matching; injection gated by session, applied `[Controls]` and a force-free build; awaiting Astra's review. |
| STD-036 | Every profile binding reaches the game from its own device | partial | Each binding is read from its own device instance; unverified at the rig. |
| STD-041 | Corrected triples and camera framing sets | partial | Angled replication, any-panel node admission and native per-camera Game/Realistic FOV sets are built/tested; legacy Planar/Angled remains. Curve/source-eye mapping and developer comparison are pending. No new rendered acceptance. |
| STD-042 | Adjustable, remembered and named camera views | partial | Bumper/Bonnet/cockpit/chase have saved FOV/pose offsets, F6 commands and vertical-FOV slider/calculation readback, zoom/reset and notices. LastCamera restores at native START preference points without license edits; absent keeps native default. Slider stores an offset, preserves dynamic FOV/pose and retains active value on failure; controller fixtures pass. Rendered acceptance remains open (docs/CAMERA-FRAMING.md). |
| STD-043 | Own-key release wait and held H-pattern truth | partial | Own-key camera release/focus/panel fixtures pass. Cache-backed provider->adapter->camera filter->native453BB0->manual transmission fixture checks held gates, F7/zoom/custom key, up/down, pause/resume and at-target stability. Fix prevents camera/desktop shortcut keys from handing a coasting wheel to keyboard driving; ordinary driving keys and wheel retake retained. Physical gearbox/rig acceptance remains open. |
| STD-044 | Owned settings edits preserve file bytes | partial | write_key/write_keys now preserve UTF-8 BOM, individual line endings, spelling/spacing/comments and absent final newline; malformed/ambiguous input refuses, unique CREATE_NEW temps publish atomically and clean only owned files. Existing writer/bindings fixtures pass. UTF-16 refuses because the native reader does not support it; other product writers still need an ownership audit. |
| STD-045 | FFB On by default, My Rig writes master switch | partial | Installed 2dd9886 defaults absent Output/Model to wheel/producer@2; physical admission and Wheelkit closed-loop Apply remain open. Preserve explicit saved Off; no unattended force qualification. |
| STD-046 | Per-camera starts and offsets on the calculation | partial | Bumper/cockpit start Realistic, chase/new Bonnet Game on Angled; native starts retain explicit legacy tuning. Bonnet/Bumper fit actual exterior PMT (30/30), per-car/set pose reset; mounted0.06s rotation smoothing, rigid eye, >20deg snap and pause/replay checks pass. Bumper submits shadows only, all30 layouts retain shadow calls. Full keyboard adjustments/last-camera restore built/tested. Single screen stays Game; live calibration/acceptance open. |
| STD-047 | One spanning window and panel-edge-plane eye distance | partial | Source already uses one borderless span for Surround/separate monitors and direct EyeDistanceMm without sagitta subtraction. Current source requires matching equal panel layouts; curved/N-display mapping and runtime topology acceptance remain open. |
| STD-048 | Numpad + zooms in, minus zooms out | partial | Native camera hotkeys + narrow and minus widen the FOV; configurable keyboard bindings, untouched-family-default migration and real controller/save fixtures pass. Partial/custom sets are retained. Physical-key/visual qualification remains open. |
| STD-049 | Cab-over bonnet at roof front edge | partial | Art's front-face/roof-edge detection is ported with a synthetic cab-over mesh fixture. Thirty stock OutRun bodies fit offline; the stock roster does not provide a cab-over for rendered qualification. |

## Feature checklist (STD-020)

| STR | PED | HB | DEV | H | SEQ | GLY | FFB | NRM | TEL | SUR | SPN | DSP | MNU | CAM | F6 | PLN | ONL | REC | PLY | INS |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 🔧 | 🔧 | — | 🔧 | 🔧 | 🔧 | ❌ | 🔧 | ❌ | 🔧 | 🔧 | 🔧 | ❌ | 🔧 | ❔ | 🔧 | 🔧 | ❌ | 🔧 | 🔧 | ❌ |

Marks follow toolkit STD-020: 🔧 partial, ❌ missing, ❔ unchecked, — not applicable. None of these marks claims owner
acceptance. The game has no handbrake or reverse gear. STR still passes through the game's dead zone and -127..127
quantization; H has gates 1-6 but no reverse. FFB is built/default-off and has no qualified physical output or
normalization. TEL has source/packet tests and earlier reception, but the `6777eb3` field correction has no live
receiver comparison (`docs/TELEMETRY-FIELDS.md`). SUR/SPN have a planar span and an unqualified projected-view spike;
DSP has no independent per-display output (`ROADMAP.md` step 3). F6 pages were rendered muted but keyboard/mouse
saving is unqualified (`docs/F6-PANEL.md`). REC has a consumed-input capture pilot; PLY has an offline input-tick
fixture, not a proved complete route (`docs/PLAY-WITH-RECORDING.md`). PLN is a folder boot, not a plain
Steam/Stream Deck acceptance; ONL has no result guard; INS has no player installer with backup and uninstall.
