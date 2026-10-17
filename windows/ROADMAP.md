# Windows desktop build: roadmap

Goal: a native OutRun 2006 for a wheel rig with **feature parity with
OutRun2006Tweaks** (plus the force feedback, telemetry and triple-screen work of
the d-b-c-e Tweaks fork), built into the decomp's source instead of hooked into
the original EXE. Tweaks cannot be *ported*: it patches the original machine
code at fixed addresses and wraps its Direct3D 9 calls, and none of that code
runs here. Its settings file, texture-pack and music layouts are kept
compatible, so existing configurations and packs carry over.

Fork: https://github.com/d-b-c-e/outrun2-decomp (`main` = this work;
`upstream` = r4dius/outrun2-decomp). Our code stays in `windows/` and a few
narrow hooks in shared files, so upstream drops rebase cleanly. Generic fixes
go upstream as small pull requests (first: `tweaks-exe`).

## Order of work

1. ✅ **Retail EXE.** The Tweaks `OR2006C2C.exe` (the DVD build without its
   protection) maps to the Steam image (`0af17f5`, PR candidate).
2. 🟡 **Wheel input on the toolkit's controls contract** (STD-033): canonical
   `[Controls]` (`ini-controls-1`) that Wheelkit writes, one-time import of the
   Tweaks `[DirectInput]` keys, H-pattern, test injection + observer for
   Wheelkit's closed-loop tests (with Astra). Ordinary-play hot-plug discovery
   is in source, awaiting a rig check. Next: Astra's review, a run at the rig,
   the closed-loop qualification (STD-034).
2a. 🟡 **F6 panel** (STD-030: the panel comes first): toolkit `native/panel` on
   our own D3D11 renderer, standard pages, each later feature adds its controls.
   - In code (`source/desktop_panel.cpp`): the Help, Controls (status), Force (strength, invert, output; F8 stops),
     Telemetry and Cameras (the triple selector and projection) pages.
   - All five pages were seen rendered in game, muted, in disposable copies (render evidence only):
     - Help: hub `artifacts/outrun-f6-help-20261012`.
     - Controls, FFB, Cameras, Telemetry on 06e5e19, Native backend: hub `artifacts/outrun-f6-pages-20261012`.
     - All five on ac68a6f, Shim backend with a pad, `[FFB] Output = wheel` without WheelFfb.dll: hub
       `artifacts/outrun-candidate-ac68a6f-20261013/regression/f6-ac68a6f-run1`. Controls reads "Shim (the shared
       input shim, [Input] Backend = Shim)" with the profile device live. FFB reads "Wheel output at the next
       start" On and, for This session, "not running: this process is force-muted (DBCE_FFB_MUTE)" (UI wording
       only).
   - Not checked: saving through the panel by keyboard or mouse (write_key is unit-tested), a three-screen
     span, the physical wheel. `docs/F6-PANEL.md`'s status lags the code.
3. 🟡 **Triple screens** (`windows/docs/TRIPLE-SCREENS.md`): `[Triple] Screens` Off / Surround / Separate monitors
   with the planar span (the Tweaks parity floor) is done and seen at the rig. True per-screen projection
   (STD-007/015/019/022) is a default-off spike (`OR2_TRIPLE_VIEWS=1`): one game render replicated by a geometry shader
   into three viewports, each with its clip correction from the toolkit rig math. First rig pictures 2026-10-12: three
   projections with the side sky. Open: culling for the union of the three frusta, the F6 selector, the owner's
   physical check. The primary-centered RacingRow adapter admits the three-monitor row with a fourth dashboard wholly
   above or below it. The 2026-10-14 muted private-copy run on the current four-display layout placed the client exactly
   on the 7680x1440 racing row in 199/199 visible samples, with no dashboard intersection, and rendered an active planar
   race (`dbce-project-mgmt/artifacts/outrun-racing-row-20261014`). This does not qualify three angled views.
4. 🟡 **Force feedback**: the force model of the Tweaks fork
   (`dbce-mods-outrun-2006`, profiles in `force-profiles.ini`), fed from the
   car state in the decomp's own structures instead of game-memory hooks;
   DirectInput effects on the wheel opened exclusively.
   - Built and tested offline:
     - the model;
     - the delivery, which loads `WheelFfb.dll` from the game folder only with `[FFB] Output = wheel`, outside test
       runs and without `DBCE_FFB_MUTE`;
     - the shim's WheelFfb read-slot bridge.
     The tests are test_ffb_outrun, test_ffb_delivery, test_ffb_wiring and test_wheelffb_bridge. The muted
     integration passed (hub `artifacts/outrun-bridge-muted-20261012`).
   - Packaged: the force add-on carries WheelFfb.dll (hub `artifacts/outrun-ffb-addon-20261012`).
     - It was installed beside the owner's b08f143 on 2026-10-12, with the force off (no `[FFB] Output`), and
       re-pinned to the owner's ac68a6f on 2026-10-13 (the same DLL, still no `[FFB] Output`).
     - Its muted copy check logged the refusal under `DBCE_FFB_MUTE` and did not load the DLL.
   - F6 saves the wheel output for the next start (f67f196). It is in the owner's ac68a6f TEST install and was
     seen rendered muted, without the add-on (f6-ac68a6f-run1 above); saving it through the panel is not
     checked in game.
   - Not yet: the physical output on the wheel, and the feel against the art of rally reference (attended).
5. 🟡 **Telemetry**: Forza Horizon layout, on by default, HUD-matched (STD-009/016,
   toolkit `native/forza`), plus the Tweaks fork's shared memory for its SimHub plugin; see `README.md` Telemetry.
   - 2026-10-12: SimHub logged valid OutRun data on port 8000 during a shim run.
   - The owner confirmed reception on the wheel's screen through SimHub, forwarded to Pit House (hub
     `docs/2026-10-12-session-release-progress.md`, 20:16).
   - Per-field accuracy is not yet fully qualified against the live HUD and receiver.
   - Source/retained-capture review (2026-10-13): HUD speed, gears and GAME-consumed input channels have traced
     producers; Forza steering now uses the consumed channel and unsupported lateral acceleration is zero. The
     shared-memory v1 raw-offset ABI stays byte-compatible, with its misleading steering/lateral/impact labels
     documented. RPM is synthetic; surface classification and physical receiver interpretation remain unqualified.
     See `docs/TELEMETRY-FIELDS.md`. Reception alone remains short of field acceptance.
6. ⬜ **Tweaks parity** (table below) and the original game's limits the
   decomp can lift, e.g. steering quantised to -127..127 in 453860.
   - Fine steering: 🟡 `[Misc] FineSteering` (not a Tweaks key), default off, in source since 2026-10-17 (zola,
     after the 2026-10-13 finding below, deferred then by sana 61131). The slice as below: 453860 hands its unstepped
     steering value out on the analogue path only, and in GAME, where 49FAD0 wrote current[0] << 8, race_player_car
     writes `pc_input_fine_steer_202` instead. Read at start; a latched process keeps it off; runtime.log names the
     choice either way and a recording's startup INI carries it to its playback. F6 Controls Advanced saves it for
     the next start. `test_fine_steering` (21 checks: every 16-bit steering word, the PC channels and filter
     byte-identical, the digital path untouched) passes. Grant OUTRUN-FINE-STEERING-BUILD-01 (kobe 233347): the cached
     ClangCL Release build of `c4b4cc3` and CTest fine_steering, pedal_volume, consumed_replay and ffb_producer2
     passed, 4/4 (exe `5DF1B607`; hub artifacts/outrun-fine-steering-20261017/build-01). A muted run and the physical
     feel at the rig are open; the owner-attended recorded session is the selected runtime route. Wheelkit has no row
     yet.
     - Where the steps come from: race_input.cpp:91 stores cvttss2si(steer*127) after the dead zone and the filter.
       OperationInput 49fad0 (pc_common_control.cpp:2189) then writes the car's +0x202 as volume0 << 8.
     - The physics readers of +0x202 are continuous in that i16, with 0x7f00 as full lock: pc_steering.cpp:57 and
       :78 (the 0x7000 correction), :106 (|+0x202| / 32512), pc_common_control.cpp:93, :237 and :1560.
     - The wheel arrives at 16 bits (shim record word 0x0E = lround(n*32767)).
     - The legacy FFB adapter still reads car +0x1D0/+0x1D4 as steering/rate, although those fields are speed
       deltas; Guru's separate source audit is at hub `artifacts/outrun-ffb-source-audit-20261013`. Since `6777eb3`,
       Forza telemetry instead takes the GAME-consumed `analog_7d6810.current[0]` channel; shared-memory v1 still
       copies raw +0x1D0 for byte compatibility. The game writes the consumed steering channel to car +0x202.
     - step_native_gameplay (pc_vehicle_control.cpp:293, clamped -128..127) has no caller.
     - The slice: on the analog path only, +0x202 = lround(steer*0x7f00), clamped. At k/127 that equals the PC's k<<8.
       current[0] and the consumed capture stay byte-identical. A latched run forces it off, and a recorded session
       names it.
7. Upstream bugs, as pull requests: #2 30-second hang at the end of a race,
   #3 rival drift score stays at zero, #4 level impossible to win.
   - #2: source pass 2026-10-13 (nene; sana 61501). The symptom is NOT confirmed on this build; nothing changed.
     - Repro: Coast 2 Coast > Flagman 1 > Novice; about 30 s from GOAL to OutRun Miles, and no flagman rank.
     - Mechanism (race_end_modes.cpp):
       - mode19_init_49c9c0 sets the GOAL countdown to 0x4B0 (20 s) and shortens it only for variants 2 to 5.
       - mode19_control_49cc50 has no input skip and leaves for mode 34 when the countdown is below 0.
       - For C2C (variant 6) only gad_goal_498670 case 6 shortens it: rows 3 s, the rank sprite 5C2184[grade] and
         voice 0xA8, the grade voice at 4 s, then at 6 s 43F900(0x3C), so GOAL lasts about 7 s.
       - A display that does not reach that step gives exactly the issue's symptoms.
     - Every service on that path is ported (race_end_runtime.cpp:646-690, 956). A display exception, though, is only a
       soft fault (race_end_runtime.cpp:1010-1016) counted in the HUD report, so a faulting step would stall silently.
     - Diagnostic recipe: finish one C2C mission naturally, then quit. The exit log (OutRunXbox.log, xbox/source/main.cpp:624
       native_race_hud_report) lists "missing (first frame): pc@frame xN".
       - If the rank shows and GOAL ends in about 7 s, #2 does not reproduce here.
       - Otherwise the PC names the call to fix.
     - A bounded synthetic entry through the native runtime harness could test the display sequencing without a drive.
       It must be labelled separately from a natural finish.
     - Source sequencing fixture 2026-10-13 (sana 78006; `tests/test_c2c_goal_sequence.cpp`, ctest `c2c_goal_sequence`,
       skipped without `OR2_EXE`). Setup:
       - It runs the production mode 19 init and control, the goal camera control `camera_override_control_4874a0` and
         the GAD_PUB GOAL display in the PC frame order. The order is 43FA20, then 43FAB0, then the display 449050, one
         tick per display, over the EXE image's tables.
       - The services are a recorder: mode globals as in `race_end_runtime.cpp`, chosen mission answers, and sound
         requests that finish at once.
       - Eight branch-distinct cases pass. In each, the rank sprite (5C2184[grade]) appears at frame 181 and the
         countdown is cut to 0x3C at 360.
       - GOAL then ends at frame 601, about 10 s, once the scene 3 goal camera script clears `82E7D8` (op 30, frame 600).
         `mode19_control_49cc50` leaves only when that word is clear. Without the goal camera (the suspend branch) it
         ends at 421, about 7 s. The exit mode is 0x21, or 0x22 with 43F860 set.
       - A SYNTHETIC rank sprite request that fails at every display gives #2's shape: no rank, no cut, and an exit at
         1200 frames (20 s).
       - Scope: these cases did not reproduce #2 with the modeled service responses. A faulting display service,
         different service timing or bad data could still cause it. Real
         sprite, sound and mission services and a natural Flagman 1 Novice finish are not qualified by this. They need
         the diagnostic recipe above.

## Tweaks parity

By section of `OutRun2006Tweaks.ini`. ✅ done · 🟡 partial · ⬜ to do · ➖ not
needed in a native build.

Source-only Advanced Cameras exposure follow-up, October 16: alongside ScreenEdgeCullFix,
F6 and Wheelkit @6 now offer the existing AnisotropicFiltering, ReflectionResolution,
TransparencySupersampling, DisableVehicleLODs, DisableStageCulling, DrawDistanceIncrease,
DrawDistanceBehind and FixZBufferPrecision settings. Native defaults and accepted ranges
are retained; edits save for the next start through the existing writers. The draw-distance
help distinguishes planar zero from the angled-view 64-section fallback. No rendering
algorithm changed. After owner handback, native5c491fb built successfully; Wheelkit
product1b139e9/test-sourcee5d0dbe passed16 affected cases (14 screen-edge cases, one
numeric read/preservation fixture with16 boundary inputs, one catalog preservation case).
Private catalog generation matched all53 bundled OutRun rows. Original logs/result are
in hub artifacts/outrun-graphics-settings-build-20261016/build-01. Rendered controls,
F6 persistence and visual effects remain unqualified; music175 passes were retained
without repetition and do not validate the graphics additions. UIScalingMode's
partial layout and texture-pack configuration remain separate work.

Source-only menus/license follow-up, October 16: F6 Advanced Controls and Wheelkit @6
Controls expose the existing AllowCharacterSelection (Misc, Off), ShowOutRunMilesOnMenu
(Misc, On) and HideOnlineSigninText (Bugfixes, Off) choices, saved for the next start.
Hide sign-in also hides the menu Miles line; the independent saved Miles choice is kept.
Menu navigation, sign-in rendering and license/save consumers are unchanged. Native
103e44f built successfully; Wheelkit92c8b75 passed the two sign-in/Miles preservation
cases and changed catalog assertion (3/3), and private generation/parsing matched all56
bundled OutRun rows. Original result/logs: hub artifacts/outrun-menu-settings-build-20261016/build-01.
Rendered controls/persistence remain unqualified; earlier graphics/music passes stand.

Character-appearance exposure follow-up: F6 Advanced Controls and Wheelkit @6 Controls
offer the existing UseHiDefCharacters (Graphics, On) and RestoreJPClarissa (Misc, Off)
choices for the next start. These are independent saved policies; model selection and
asset loading are unchanged. Native315b561 built successfully; Wheelkit91ac650 passed
the four cross-section preservation cases and changed catalog assertion (5/5), with
private generation/parsing matching all58 bundled OutRun rows. Wheelkit was qualified
and fast-forwarded into clean canonical main from5efc986; the native candidate remains
private. Original logs/result: hub artifacts/outrun-character-appearance-build-20261016/build-01.
Rendered appearance and F6 persistence remain unqualified; prior suite passes stand.

Holly MIX 2 availability source follow-up, October 16: F6 Advanced Controls and
Wheelkit @6 Controls expose existing Misc.EnableHollyCourse2 modes 0..3 through
one descriptive choice. Default 3 shows and unlocks MIX 2 at Holly 1 grade A;
mode 2 shows the locked tile earlier, mode 1 unlocks with Holly 1 and 0 is Off.
All modes retain the valid fourth-course-lane gate and do not edit license progress.
The native startup consumer and its integer-prefix/fallback behavior are unchanged;
Wheelkit reads the same effective value without rewriting the saved INI. Explicit
edits reuse existing one-key writers and apply at the next start, outside rig profiles.
Offline qualification passed on native dd38948 / Wheelkit 609885e: incremental
OutRunDesktop built successfully and the six affected settings/catalog cases passed
(17 integer grammar/boundary inputs in one read fixture, four save/backup roundtrips,
one catalog preservation assertion; zero failed/skipped). Private @6 generation and
parsing matched all 60 bundled OutRun rows. Native app B18AD831 (23,819,776 bytes);
original logs, result.json and affected.trx are in hub
artifacts/outrun-holly-settings-build-20261016/build-01. The exact shared lease was
released with environment restored and owned processes quiescent. One initial
worktree-path typo stopped preflight before lease/build and was corrected; the
original failed evidence is retained. No game/install/provider/publication. Earlier
music/graphics/menu/character passes stand; rendered F6, persistence and the actual
unlocked course route remain unqualified.

Highway animation setting source follow-up, October 16: the existing default-off
Misc.RandomHighwayAnimSets option is exposed on F6 Advanced Controls and Wheelkit
@6 Controls as Random highway animation sets. An explicit edit saves only that key
for the next game start. Help retains experimental status, event conditions and
precedence over the separate row-only setting without rewriting it. The selector,
process random stream, actual race mode, recording and all game consumers are unchanged.
The selector and Rows compatibility checks already passed on c5d3a9f (2679 and 245
checks; hub artifacts/outrun-cumulative-shift-highway-20261015/QUALIFICATION.json).
Their source and fixtures remain identical here, so those results are retained;
original cleanup/readback failure and exit-only closure remain honestly recorded.
Offline qualification passed on native 5a0cf26 / Wheelkit acd77b1: incremental
OutRunDesktop built successfully, the two save/backup/preserved-Rows cases and
existing catalog preservation assertion passed (3/3, zero failed/skipped), and
private @6 generation/parsing matched all 61 bundled OutRun rows. Native app
D6AFCC24 (23,820,800 bytes). Original logs/result/TRX:
hub artifacts/outrun-highway-settings-build-20261016/build-01. The exact lease
was released with environment restored and owned processes quiescent. Qualified
Wheelkit source is on main; native remains private. No game/install/provider/publication.
Rendered controls, persistence, animations and runtime replay remain unqualified.

Gameplay/audio compatibility setting batch, October 16: F6 Advanced Controls and
Wheelkit @6 Controls expose existing Bugfixes.FixFullPedalChecks and FixPegasusClopping
as Full pedal press check fix / Pegasus sound loop fix. Both remain On by default;
explicit edits save one key for the next start, outside rig profiles. No pedal mapping,
calibration, stored input, recording, event sound consumer or algorithm changes.
The original pedal_volume and pegasus_sound passes are retained (hub
artifacts/outrun-fix-pegasus-20261013/run-054105/ctest.log); neither consumer nor
fixture changes in this batch. Native 9f5486d incremental OutRunDesktop built
successfully; Wheelkit 29f5301 passed all four save/exact-backup cases and the
existing catalog preservation assertion (5/5, zero failed/skipped). Private @6
catalog generation/parsing matched all 63 bundled OutRun rows. Native app
5DA8A909 (23,823,360 bytes). Original logs/result/TRX: hub
artifacts/outrun-compatibility-settings-build-20261016/build-01. The exact lease
was released after owned-process cleanup and environment restoration; no old
consumer suite was rerun. Lola owns canonical Wheelkit integration; native stays
private. Rendered controls, F6 persistence, physical pedal behavior and audible
Pegasus behavior remain unqualified by these changes. No game/install/provider/publication.

Remaining graphics compatibility exposure batch, October 16: F6 Advanced Cameras
and Wheelkit @6 Cameras expose existing FixLensFlarePath, FixIncorrectShading,
FixParticleRendering and FixRightSideBunkiAnimations (Bugfixes, all default On),
and CarBaseShadowOpacity (Graphics, default 1). Shadow presets 0/25/50/75/100/150/200%
reuse Choice with Keep saved value; custom text remains intact, including values
above 2 and spellings outside the preset vocabulary. Common 1.0/1.00 defaults show
100% without rewriting. The native finite/nonnegative consumer remains authoritative
and uncapped; no parser, rendering, camera, asset loading, report or framework change.
All writes are explicit, one key and next-start; rig profiles do not target them.
Native a6b700e incremental OutRunDesktop built successfully; Wheelkit c47a43d passed
all 17 affected settings/catalog cases, zero failed/skipped (eight Boolean On/Off
roundtrips, seven preset writes, one read/Keep fixture with 13 inputs and one catalog
assertion). Private @6 generation/parsing matched all 69 bundled OutRun rows. Native
app 480B94D2 (23,828,992 bytes); original logs/result/TRX: hub
artifacts/outrun-graphics-compatibility-build-20261016/build-01. Exact lease released
after owned-process cleanup and environment restoration, before docs/integration.
Qualified Wheelkit source is on main; native remains private. No consumer suite was
repeated. Rendered F6/persistence and visual effects remain unqualified; no game,
install, provider retry or publication.

Texture replacement exposure batch, October 16: F6 Advanced Cameras exposes the
existing Graphics.SceneTextureReplacement and UITextureReplacement flags, both
Off by default and explicit one-key edits for the next start. The saved
TextureBaseFolder is shown without editing it; the existing consumer alone
validates game-relative paths, falls back to textures for invalid paths and
loads <game folder>/<base folder>/load. Folder, lookup, pack selection, decoder,
prompt routing, geometry and rendering algorithms are unchanged. Help notes
loading/memory costs and pack-dependent prompt artwork. Lola owns the matching
Wheelkit @6 Cameras catalog/writer exposure and its affected preservation checks.
Prior loader/pack/render evidence remains scoped to its original candidates;
this addition has no new runtime/asset acceptance. Native 553fdb1 incremental
OutRunDesktop built successfully; Wheelkit 82faef2 passed all five affected
settings/catalog cases (four explicit one-key save/exact-backup cases preserving
folder and other-toggle policy, plus the catalog assertion; zero failed/skipped).
Private @6 catalog generation/parsing matched all 71 bundled OutRun rows. Native
app 278B44A9 (23,832,064 bytes); original logs/result/TRX: hub
artifacts/outrun-texture-settings-build-20261016/build-01. Exact lease released
after owned-process cleanup and environment restoration, before docs/integration.
No loader suite was repeated. Lola owns Wheelkit docs/ordinary integration;
native remains private. Rendered F6/persistence, pack cold-loading and runtime
visual behavior remain unqualified. No game, pack download, owner install,
provider retry, native canonical merge or public push.

Loading performance exposure, October 16: existing Performance.FramerateFastLoad
is offered in F6 Advanced Cameras as Fast loading: Off (default) / Loading only
(experimental), mapping exactly to 0/2 for the next game start. Only literal 2
shows enabled after normal INI whitespace/comment parsing; unsupported modes 1/3
and other text remain Off, without rewriting the file on read. Lola owns matching
Wheelkit @6 Cameras raw/composite rows and its affected settings/catalog checks.
The fast-load consumer, frame loop, wait/Present gate, clock, recording and normal
driving behavior are unchanged. Existing fast_load 361 and fast_load_config 44
checks passed on product 2984189 (hub
artifacts/outrun-fast-load-prompts-cumulative-kiki-20261015/QUALIFIED-HOST.md);
their source/header/fixtures remain identical and results are retained without
rerunning. Native d35ee48 incremental OutRunDesktop built successfully; Wheelkit
c36aaeb passed all four affected cases (one read/preservation fixture with 12
inputs, two explicit one-key/exact-backup writes and the catalog assertion; zero
failed/skipped). Private @6 generation/parsing matched all 73 bundled OutRun rows.
Native app B872B574 (23,833,088 bytes); original logs/result/TRX: hub
artifacts/outrun-fast-load-setting-build-20261016/build-01. Exact lease released
after owned-process cleanup and environment restoration, before docs/integration.
Lola owns Wheelkit docs/ordinary canonical integration; native remains private.
Rendered F6/persistence, loading speedup and runtime transition behavior remain
unqualified. No game, owner install, provider retry or public push.

C2C GOAL spacing source follow-up, October 16: the existing canonical 481A40
request-display reconstruction is reused from the GAD GOAL 46C220 service instead
of the obsolete translated-only rejection. Original bytes/Tweaks hook boundaries
pin the fruit virtual call's off/detect transition and the 481B76 unspaced heart
tail. Optional metadata on the existing request capture retains each deferred
draw's scope; existing NAVI/GAD row, digit, SPRANI and model services replay it.
No generic draw-record, renderer, camera, game clock, RNG, save or settings-contract
change. Modes 0/2 remain unshifted; default UIScalingMode remains 2. Only the
existing course_asset_pack fixture is extended for the real GOAL route, unchanged
stock submissions/frame advancement and scope/fault cleanup. Cached ClangCL
Release build (`OutRunDesktop` + `test_course_asset_pack`) and only
`owned_request_course_phase` pass at `e4d9886` (1/1, zero skipped). Hub logs:
`artifacts/outrun-c2c-goal-spacing-20261016/build-01`; executable SHA-256
`fa72409308227f596016b58ae6e83f74a577cafac969f3baaa61007b29b26e76`.
Lease released, children quiescent and process environment restored. No runtime
or rendered-result claim. See docs/UI-SCALING.md.

| Section | Settings | Status | Notes |
| --- | --- | --- | --- |
| Performance | FramerateLimit, FramerateLimitMode, VSync | 🟡 | Default retains the existing60/120 Settings row and integer-vblank pacing. Opt-in FramerateUnlockExperimental now consumes FramerateLimit0 (no CPU cap) or60..1000 using the existing host clock/sleep and Present0; legacy options framerate preserved. Qualified general-presentation candidate0551df8 retains desktop +5,553 checks. Separate successor implements FramerateLimitMode0 efficient sleep/yield or1 accurate processor-pause waiting and independent VSync0/1/2 (empty/absent preserves existing pacing), with next-start F6 choices. At7292710, affected Release desktop and two new policy/host fixtures pass465 checks (224 controls,241 sound),2/2 zero skipped. No tearing/VRR/topology changes. See docs/GENERAL-PRESENTATION.md. |
| | FramerateFastLoad | 🟡 | Default-off loading-only mode 2: existing frame-latency wait/Present bypass in START before stage 65, one loading update per frame at 60/120 Hz, live restoration and existing-clock rebase on exit. Host policy/config checks passed (361/44), retained with unchanged sources. F6/Wheelkit exposure passed its incremental native build, four affected settings/catalog checks and 73-row generation/parsing on October 16. Runtime transition and load-time improvement remain unqualified. October 17 successor `5ca57c0` adds Tweaks mode 1 (VSync kept: latency wait retained, Present at most one vblank on loading frames) with a third F6 choice and fixture cases; its build-01 passed (fast_load 533 checks, fast_load_config 48; hub artifacts/outrun-fast-load-mode1-20261017/build-01), and canonical Wheelkit `4cd57672` offers modes 0/1/2. Mode 1 runtime pacing and load time remain unqualified. Mode 3 (pumping 44FBA0 during the frame wait, in-game streaming included) is unsupported. [Source contract](docs/FAST-LOAD.md). |
| | FramerateUnlockExperimental | 🟡 | Default-off implemented ordinary consumer: zero/multitick60Hz simulation and existing display interpolation, Present0/optional CPU cap, PcSound service at the existing42F330 fixed-update callback only when enabled. Off preserves accepted60/120 and old sound cadence. F6 Cameras Advanced exposes next-start toggle/cap through existing save/rollback; tests/record/playback ignore it. Options frame rate stays preserved; active override removes the live FRAME RATE row. Release desktop and two unique focused fixtures pass (5,309 general-presentation plus 244 sound checks); actual pacing/scanout/audio/input/rendered controls remain unqualified. See docs/GENERAL-PRESENTATION.md. |
| | SingleCoreAffinity | ➖ | a workaround for the original |
| Controls | SteeringDeadZone | ✅ | |
| | UseNewInput, ControllerHotPlug | 🟡 | gamepads via Windows.Gaming.Input (hot-plug); ordinary DirectInput rescans every two seconds and reopens the saved instance GUID, built but not checked at the rig |
| | DefaultManualTransmission | 🟡 | off by default as in Tweaks: after 4DD250 (frontend_transmission_init_4dd250, an exact reconstruction) the transmission owner's +E4 = 0 / +E8 = 1, the carousel's (+34) previous / current index, as Tweaks' detour, so confirming stores Manual in 830374; off leaves every byte as 4DD250 left it (tests/test_default_transmission.cpp). `[Misc] DefaultManualTransmission`: Tweaks' `[Controls]` here is the ini-controls-1 section, where an unknown key is an invalid binding (dbce_controls.hpp). F6 Controls and Wheelkit's Controls page offer it as "Manual transmission by default" (the next start). The 2026-10-14 observed A/B in hub `artifacts/outrun-transmission-default-20261014` showed AUTOMATIC with the setting off and MANUAL with it on; manual shifting and physical feel remain unqualified. |
| | HudToggleKey | 🟡 | `[Misc] HudToggleKey` (Tweaks keeps it in `[Controls]`, the controls contract here), unset by default as in Tweaks; Tweaks' names (a letter or digit by the layout, F1-F24, HOME, NUMPAD0-9 ...), matched by key position in the window's keyboard. Each release flips NaviPub_Disp's flag 8447F8 (Tweaks' 4447F8), which the race HUD applies just before 4BEB00; the game resets it at each race's init, and a press before it is dropped. The key is not consumed (the game and its menus still see it, as Tweaks' GetAsyncKeyState). F6, F8 and F11 are refused, and nothing toggles while the panel is open; a clear of the window's keyboard (focus loss, the panel opening or closing) is not a release, so the key waits for its own. Only keys the game's window receives count; it need not be in front. Replay: presentation only, outside the tape; off in recording, playback and test processes, so no tape is made or played with a toggled HUD (`windows/source/hud_toggle.hpp`, `tests/test_hud_toggle_navi.cpp`). The 2026-10-14 observed run in hub `artifacts/outrun-hud-toggle-20261014` (candidate 214d785, a muted private-copy race with F9 posted to the game's window) showed the race HUD visible, hidden after the first release and visible after the second, while the race kept running; that is presentation only, with no recording or input-response claim. |
| | Vibration\*, ImpulseVibration\* | 🟡 | Separate default-Off successor on bfaba433: Xbox-derived native-car body calculation and WGI body/trigger commands to the existing selected pad, strict settings and next-start F6 save/rollback. Existing mute and gameplay/focus/F6/pause/disconnect/exit gates; test/record/playback excluded. Controller index 0 means selected native pad, arbitrary indices unsupported; driver may approximate trigger channels. Wheel FFB unchanged. At18582ba4, affected Release desktop and one new fake-output fixture pass706 checks (1/1 CTest, zero skipped); physical behavior/rendered saving remain unqualified. See docs/GAMEPAD-VIBRATION.md. |
| DirectInput | device, axes, inverts, sensitivity, buttons, POV bindings | ✅ | canonical `[Controls]` (`ini-controls-1`); the Tweaks keys are imported once with Tweaks' scaling |
| | ThrottleDeviceGuid, BrakeDeviceGuid | ✅ | each binding names its own device instance |
| DirectInput.Calibration | Steering/Throttle/Brake Min/Center/Max | 🟡 | Qualified one-time import into existing schema-2 cal= at cde159336593a73bab0a3d04adfe8975955c9f75: original hub artifacts/outrun-pedal-calibration-offline-20261015/run-01/result.json and ctest.log record desktop_controls175 checks,1/1 PASS plus selected wheel_input.cpp compile, exits0; no desktop link in that historical batch. Both cde1593 and steering840d82a are ancestors of current8ab44df; importer, reader, fixture and canonical contract blobs are unchanged. Raw DirectInput counts0..65535 are validated before rounding to whole counts. Steering uses min/center/max; each pedal uses min/max (span>=8192), with center validated but unused for scaling; independent devices retain their own endpoints and inversion applies once after normalization. Disabled calibration keeps the legacy full range. Invalid enabled values or nonzero/invalid legacy pedal deadzone refuse only the affected binding with a note: that curve is not representable in the existing contract. Canonical bindings are never reimported; the existing one-time marker/backup route remains. Pedal qualification was already passing; the prior unbuilt/unrun wording was stale. No new tests/build/install/runtime/physical qualification follows from this reconciliation. |
| DirectInput.Shifter | sequential | ✅ | |
| | hpattern gears 1-6 | 🟡 | pulses toward the gate's gear (car +208); R: the game has no reverse |
| DirectInput.Aux | DeviceGuid, buttons | ✅ | any action may sit on any device (one binding per action) |
| Telemetry | Enable, SharedMemName | 🟡 | step 5: `[Telemetry] Enabled`, `SharedMemName`, `Layout`; reception confirmed, per-field accuracy open |
| FFB | all | 🟡 | step 4: built and tested offline; no owner package ships WheelFfb.dll yet |
| Graphics | resolution, AA, widescreen | ✅ | upstream rows (Options > Settings) |
| | UIScalingMode | 🟡 | windows/docs/UI-SCALING.md: 2 (keep centered, the centered 4:3) by default here; 0 stretches the 2D over the 16:9 frame, or over a span's center panel only; 1 (Tweaks' Online Arcade) is partial: the 2D mapping widens at the 4:3 scale to the frame's edges (or the center panel's), and NaviPub_Disp's HUD is spaced to them with Tweaks' unspaced parts (rival labels / markers, hearts, LAN markers, and from the level / lap / countdown on). Step 2b adds time attack's forced sides, the put_scroll positions (time attack, rank, REV, heart number) and the ghost gap. Step 2c adds C2C scores, mission and GF request bubbles, and an 18-site source bridge for translated GF bubbles and rank positions; C2C GOAL now reuses the existing canonical request display with per-submission fruit/heart scope capture in source; the affected Release build and owned_request_course_phase pass at e4d9886, with rendered acceptance still open. Unit-tested (test_triple_screens; the device path in test_d3d11_vertex_path). Seen in game on the race route only (UI-SCALING.md, Step 4): the HUD moves ±240 px at 2560x1440 with no clipping. A start-camera "1P" banner drawn past the 4:3 edge appears in mode 1 only. Time attack and C2C have not been seen |
| | UILetterboxing | 🟡 | 1 by default as in Tweaks (0 none, 1 outside the race, 2 always), only with UIScalingMode 1 or 2 and while the 2D is in the centered 4:3: black bars outside it, at the device's own 4:3 edges, under the badge and the F6 panel (desktop_panel.cpp, ClearView). F6 Cameras (Advanced) and Wheelkit's Cameras page offer it as Off / Outside the race (default) / Always. Tweaks' rule: outside GAME, SMPAUSEMENU, GOAL, TIMEUP, OUTRUNMILES, TRYAGAIN, and START before its stage 65 (pc_screen.hpp); tests/test_letterbox.cpp. Seen at 16:9 in a disposable copy, 2026-10-13 (hub `artifacts/outrun-letterbox-20261013/check-run1`): 2 bars menus, intro and race at the 4:3 edges; 1 and 0 leave the race at full 16:9. Not seen: 1's bars against 0 on a non-race screen with content outside the 4:3, the badge or F6 on top, UIScalingMode 1, a span |
| | AnisotropicFiltering, ReflectionResolution, TransparencySupersampling | ✅ | `[Graphics]`, off by default; anisotropy only on samplers linear in both min and mag (the HUD stays sharp); alpha-to-coverage only under MSAA |
| | DisableVehicleLODs, DisableStageCulling | ✅ | off by default. With all five options on (2026-10-12 A/B, n = 1 vs 2): GPU draws about 2.1x and 1 to 2.5 % of race frames miss vsync; not attributed per option |
| | ScreenEdgeCullFix | 🟡 | on by default as in Tweaks: the object test 4F0E40 (cones, course objects, gates) takes the widened widescreen bounds 4F02D0 (cars) already had; identical at 4:3; not yet seen in game. Source-only follow-up exposes the existing flag as "Widescreen object visibility" in F6 Advanced Cameras and Wheelkit @6 Cameras, next-start only; the runtime algorithm is unchanged. The F6 row compiles and links in the Release desktop at 81572d5 (hub artifacts/outrun-network-consumer-20261017/build-02) and 5ca57c0 (artifacts/outrun-fast-load-mode1-20261017/build-01); the Wheelkit row is in canonical catalog-v6. No fixture targets the row; rendered F6 saving and in-game visibility remain unrun. |
| | DrawDistanceIncrease/Behind | 🟡 | off by default; Tweaks' 44DF6D merge (dedup, exclusions with the offset-0 exemption, Canyon limit) in 44DE10, a lods.ini read only, queues grown as DrawBufferExtension; unit-tested, not yet seen in game |
| | (renderer) D3D9 point sprites | 🟡 | not a Tweaks key: the port drew no POINTLIST at all, so sparks (source 3) and effect sources 8-10 were invisible; drawn per the D3D9 docs since b978928; unit-tested, visual check pending |
| | FixZBufferPrecision | 🟡 | off by default; Tweaks' near-plane rule on the main race camera (1.0 chase/bumper, 0.25 in-car, 0.2 360SP, 0.1 switching/outside GAME) and 0.05 for the 40BE70 passes (projection only: 484BD0's view part is stateful); unit-tested, not yet seen in game |
| | CarBaseShadowOpacity | 🟡 | 1.0 by default as in Tweaks: the race car's flat base shadow (CalcPeraShadow, nulled on the PC) restored at Tweaks' three sites (469EB4 race, 46AC76 / 46B766 car select) with the PC's own alpha; unit-tested |
| | UseHiDefCharacters | 🟡 | on by default as in Tweaks: 487EE0 / 487F50 (and the native 488090 reader) return MH00 for the 6549B0 entry 0 and the O2SP constant, LH00 / GH00 for the 6549C8 entries 3 / 4, and LH00 / GH00_USA for the O2SP constants; CHR_DR_GH00(_USA).bin from Media (it has the hair animations) when present, else Chr; runtime.log names it. Unit-tested (test_car_base_shadow); not yet seen in game. RestoreJPClarissa: see Misc |
| | SceneTextureReplacement, TextureBaseFolder | ✅ | off by default; the Tweaks pack layout and names (`<bank>/<index>_<XXH32>_<w>x<h>.dds`), the game's own create request; files the device's DDS reader would refuse are never offered (stock kept). A create the device still refuses falls back to stock in the loader, but the device's error latch ends the run at present, so a genuine allocation failure can remain terminal. In game (2026-10-12, owner pack copied, muted): 344 of 834 replaced at 60 fps, 0 device errors; runtime.log lists per bank what was replaced and which banks the pack has no folder for (that race's stage, `cs_CS_PALM_pmt`, is one: the pack has `cs_CS_PALM_T_pmt`). Cube maps with 2D pack dumps stay stock |
| | UITextureReplacement | 🟡 | off by default, as SceneTextureReplacement here (Tweaks' INI has both on); same folder and index as the scene textures. The sprite banks' XST textures under `<XST stem>/` (`spr_etc_xst`, `spr_sprani_game_cvt_Exst`), named as Tweaks dumps them: `<index>_<XXH32>_<w>x<h>.dds` (index = the textures the bank created before it, Tweaks' CurrentTextureIdx), then `<XXH32>_<w>x<h>.dds`; Tweaks' FXT hash remaps. A replaced texture keeps the stock size as the texel space of the XST image tables and SPRANI frames: footage and 42C0F0 UVs over the stock size, ID3DXSprite source rectangles moved onto the replacement with the scale reduced by the same ratio, so positions, centring and alpha stay as stock (unit-tested: 2x, 3x, unequal 3x/2x, flipped, rotated). Offline against an owner Tweaks pack (read only, the retail check of test_pc_sprite_replacement): all 257 UI files matched, 0 kept stock. In game (2026-10-12, muted A/B, owner pack's UI folders copied, 2560x1440): 160 of 176 sprite textures looked up on the menu-to-race path replaced, 0 kept stock; menu and HUD closeups match stock positions, sizes, centring and alpha with sharper art; race 60.0 fps in both, max frame 20.2 ms vs 21.6 ms stock. Costs: the front-end bank's pack work runs on the main thread when the bank loads (1.6-1.9 s with a warm file cache, 8.6 s cold offline), and private memory rises 0.83 -> 4.23 GB, mostly because the device keeps a DDS and an RGBA8 copy of every texture besides the RGBA8 GPU texture. That pack's prompt atlas shows gamepad buttons where stock shows keys (pack content; Tweaks uses the same base file without a pad type). Qualified offline at cumulative fb762e2 (desktop +208-check sprite fixture); runtime pending: Tweaks' six-hash pad prompt-folder precedence and first non-PC latch, using a conservative presentation hint from the already-selected physical WGI pad (unknown/no selected pad stays generic); see TEXTURE-REPLACEMENT.md. No input/isolation/owner-pack changes. UITextureExtract private candidate is default-off, next ordinary start, suppressed in test/playback/recording: original validated DDS copied through shared scene writer after successful stock XST creation or CPU START pack assembly, loaded replacements excluded. Shared names/FXT/dump/no-overwrite/dedup; original upload/pack/draw bytes retained. Native F6 extraction rows compile at fb762e2; PMT315/sprite208/retailSTART259 checks pass (3/3 existing fixtures, zero skipped). Hub artifacts/outrun-ui-extraction-20261016/build-01; exact lease released with children quiescent/environment restored. Ordinary-game extraction/rendered F6 acceptance pending; see TEXTURE-REPLACEMENT.md. The START loading picture (bank 0x2F, drawn by the preview renderer) takes the same lookup under `spr_SPRANI_LOADING_CVT_Exst/`: a replaced texture is decoded to RGBA8 by the device reader and carried in the in-memory loading pack (OR2LDP3: at most 128 MB for the pack and 16384 texels a side), the stock size kept as the crops' texel space; with the option off, or no file for any of its textures, the pack is the stock OR2LDP2 bytes. The pack is rebuilt from the game files at each start, so the option takes effect at the next start and nothing is kept between runs. Unit-tested (synthetic packs; the retail START pack with one replaced texture, off and no-match byte-identical); not yet run in game |
| | (scene and UI) pixel formats | ✅ | A8B8G8R8 / X8B8G8R8 pack files are read as A8R8G8B8 (R and B swapped), as Tweaks' loader converts them; about half of the UI files in the owner pack use it |
| | TextureMatchAnyFolder (not a Tweaks key) | 🟡 | off by default: a texture with no file under Tweaks' names takes a same-name file (XXH32 and size of the stock DDS by the pack's naming, any index prefix) from elsewhere in the pack when it is the only one or all such files are byte-identical; differing files are ambiguous and keep stock. Built for stage variants: Tweaks looks only in the bank's own folder and the root, so a pack made on cs_CS_PALM_T_pmt leaves cs_CS_PALM_pmt stock. Offline over every PMT bank against an owner Tweaks pack (tools/texture_pack_coverage.cpp, read only): Tweaks' names replace 1,277 of 14,395 textures; with this option 3,335 (cs_CS_PALM 0 -> 154 of 166, the _R / _T variants of the covered stages 118-154 each), 300 ambiguous kept stock. Unit-tested (test_pc_pmt_loader); not yet seen in game |
| | (scene) cube maps with 2D pack files | ➖ | the request is a cube, the file a 2D image: kept stock (16 in the owner pack) |
| | SceneTextureExtract | 🟡 | October 16 private candidate, default false and next ordinary start only (disabled in test/playback/recording). Save validated original scene PMT DXT1/3/5 and 32-bit RGB DDS, 2D or complete six-face cube with stored mips, under TextureBaseFolder/dump/bank/index_hash_size.dds. Shared Tweaks names/FXT remaps; successful replacements suppress dumping, stock fallback retained. Exclusive create preserves existing files; dedup and nonfatal failure counters. No GPU readback, UITextureExtract, install or owner-asset writes. Release desktop linked at9df8513; test-only cleanup correctione965695 built and pc_pmt_loader passed315 synthetic checks (1/1, zero skipped), retaining desktop without rebuild and original failed fixture compile. Hub artifacts/outrun-scene-extraction-20261016/build-01 and build-02. Exact lease released, children quiescent/environment restored. Ordinary-game extraction/rendered acceptance pending; see docs/TEXTURE-REPLACEMENT.md. |
| | EnableTextureCache | 🟡 | October 17 private candidate, defaultfalse/nextordinary and disabled in tests/playback/recording. Bounded128MiB raw replacementDDS LRU, unchanged lookup/validation/conversion/resourcecreation; one owned current-scene-bank prefetch overlaps objects and holds texture-step pending without cursoradvance. Reset/destruction joins worker; no GPUhandle/diskcache or ownerwrites. Native F6 Cache replacement textures uses existingwriter/rollback. Adaptation differs from Tweaks1280MiB/defaulttrue policy; cold UI stall/performance and rendered acceptance unqualified. Release desktop/PMT target build at efe14f3; one pc_pmt_loader CTest passes351checks,1/1 zero skipped. Original hub artifacts/outrun-texture-cache-20261017/build-01. Exact lease released with ownedchildren quiescent/environment restored. Earlier UI782checks retained, no unrelated suite repeated; see docs/TEXTURE-REPLACEMENT.md. |
| | Texture CPU copies (port memory, not a Tweaks key) | ✅ | `e167f58`: a file texture releases its decoded RGBA copy once the D3D11 device has uploaded it (UpdateSubresource copies before it returns), keeping its DDS levels; any CPU reader decodes a level again, and a CPU-written texture is retained for good (file textures cannot be locked, so that rule is defensive). `OR2_KEEP_TEXTURE_RGBA=1` keeps the old lifetime. Measured by deli (one exe, settled race, muted; 2026-10-12): HD UI private bytes 4,250 -> 3,711 MB (-539 MB, 371 textures released, 544.6 MB), stock 829 -> 791 MB; no timing difference visible. Only textures actually drawn are released. **Follow-up, deferred (sana 45486):** decode RGBA lazily at the first upload so never-drawn pack textures hold only their DDS levels; it moves the decode to first use and needs a first-draw hitch qualification. BC direct upload is a separate follow-up |
| | UseNewTextureAllocator | ➖ | |
| Audio | AllowHorn | 🟡 | on by default as in Tweaks (`[Audio] AllowHorn`): while the controls contract's own `horn` action is held in GAME, SE 0x1BC is queued looping every frame on the race SE queue 9563E8 (Tweaks' AudioHooks_Update), and stopped on release or on leaving GAME (race_sound_runtime.cpp native_race_horn, at 453BB0's input step on the final input, after a consumed playback replaced it; the stop stays pending until the queue takes it). Not the X switch: in 5A7B50[0] the device button behind it (0x8) is shift-down, so the horn is its own consumed action (shim adapter and native wheel path), with no record bit, recorded as `final.horn` (optional when decoding older rows). A Tweaks INI's `ButtonX` imports as `horn`. Fixture cases added (adapter, capture, controls import, the SE queue and the replay seam), their execution pending; not packaged until Wheelkit accepts the 20-action capability; not yet heard in game |
| | AllowWAV, AllowFLAC | 🟡 | Both default false here (Tweaks true). Same-stem enabled existing FLAC > WAV > original Ogg in Sound, case-insensitive lookup; selected read/codec/format failure refuses without lower-priority fallback. Optional isolated static FFmpeg 7.1.1 WAV PCM U8/S16LE/S24LE/S32LE/F32LE and FLAC, mono/stereo8–192kHz; source-rate S16/full-gain mono duplication/linear48kHz mix with exact integer source phase. d71f1ba offline PASS:18,807,664 owned numeric checks, focused CTest1/1 and three affected production compiles; original no-FFmpeg51-check pass retained (hub music continuation3292E6EB). Input64MiB, bounded metadata/packets/frames/PCM/work and owned AVIO/decoder cleanup. Actual Sound lookup, cumulative music link, audible/runtime/performance/full Tweaks parity pending. Current Bink-only prefix and qualified746A untouched; CD switcher/override outside this slice. Source follow-up: native F6 Advanced Controls and Wheelkit @6 expose the two explicit next-start flags, default off; native F6 reports decoder availability. The F6 rows compile and link in the Release desktop at 81572d5 (hub artifacts/outrun-network-consumer-20261017/build-02) and 5ca57c0 (artifacts/outrun-fast-load-mode1-20261017/build-01); the Wheelkit rows are in canonical catalog-v6. No fixture targets the rows; rendered F6 saving and audio/runtime qualification remain unrun. |
| | AllowFLACLoopPoints (optional config, not a Tweaks key) | 🟡 | Default false; new source/focused fixture additions UNRUN. With valid native FLAC tags and caller loop=true, play intro0..end-exclusive then repeat start..end-exclusive, retaining continuous integer mix phase. Explicit END is exclusive. LENGTH deliberately preserves observed Tweaks arithmetic end=start+length-1, requiring LENGTH>=2; it overrides END regardless of tag order. Last duplicate per key wins only if every recognized value is valid strict unsigned decimal (no sign/space/junk/overflow); incomplete tags, multiple comment blocks and invalid/unknown ranges refuse. Require known total and0<=start<end<=total; exact source-sample timestamps, codec flush, bounded backward seek/discard/trim, no approximate boundary fallback. Caller loop=false validates tags but plays the full file, intentionally preserving this API rather than Tweaks Read's unconditional custom loop. Whole-file untagged WAV/FLAC and Vorbis behavior retained. No new UI knob or owner INI write; full parity/runtime/audible acceptance pending. |
| CDSwitcher, CDTracks | in-race track switching, track list | 🟡 | Existing opt-in ordered catalog, Z/X and pad-release selection, two native race music boundaries and optional title reuse current audio/input/HUD services (5b02417/db91be6 and title successors); prior empty status was stale. October 16 adds the eight Tweaks stick-direction combination aliases from the existing native pad axes, with unchanged default controls and recording exclusion. Desktop Release build passes at96c92fd; fixture-only correction339f901 passes cd_switcher (253 synthetic checks, 1/1, zero skipped), retaining the original failed fixture compile. Startup shuffle honors existing SwitcherShuffleTracks (default false) once at startup; desktop Release and cd_switcher pass at db36dc5 (263 synthetic checks, 1/1, zero skipped), with runtime acceptance pending. The 28-track Tweaks default is shipped INI data, with no hardcoded engine fallback; owner catalog/config stay explicit. Native F6 Controls Advanced now exposes In-race CD switcher, Shuffle CD tracks at startup and Show CD track title as independent next-start booleans, all default Off, paired with matching Wheelkit rows; native Release build passes at ee3d286; paired Wheelkit c5d0413 generation/parsing matches 76 rows and 7/7 affected cases pass with zero skipped. Prior 263 CD and 601 course checks are retained. Rendered/physical acceptance remains open. See docs/CD-SWITCHER.md and docs/F6-PANEL.md. |
| Window | WindowedBorderless, DPI | ✅ | borderless over the primary monitor, native per-monitor DPI awareness |
| | WindowedHideMouseCursor | 🟡 | October 16: the existing default-true Tweaks key is now read at startup and gates the borderless WM_SETCURSOR client handler. False falls through to the existing Windows class cursor; default true retains hiding, with bordered/nonclient handling untouched. Previously the green grouped row hid that the opt-out was ignored. Affected ClangCL Release OutRunDesktop build passes at 03d124f; original result/logs/binary are in hub artifacts/outrun-cursor-visibility-20261016/build-01 (23,871,488 bytes, SHA-256 0e72e1cdc92a44e68e44d7bda9d89718aae8f6a9861b5b27c2ac1b05e2343512). Exact lease released with children quiescent and process environment restored. Prior 263 CD, 601 course and paired 7 settings cases retained; no new fixture or game run. Rendered cursor acceptance remains unqualified. No periodic/input/audio/render-loop or owner config changes. |
| | AutoDetectResolution | 🟡 | on by default as in Tweaks (its GameDefaultConfigOverride): while options.ini holds no resolution, the render resolution takes the supported height nearest the display's (the primary monitor's; 720 for --windowed) instead of the port's fixed 1920x1440 default; a resolution in options.ini (Settings writes one on any change there) always wins; off, or a latched (test) run, keeps 1920x1440; no display mode is changed (enhancements/video.hpp, xbox/source/main.cpp, window_desktop.cpp). F6 Cameras (Advanced) and Wheelkit's Cameras page offer it as "Resolution from the display". tests/test_auto_resolution.cpp. As first built (123a975) it acted only on an unset height the host never has, a no-op: observation A1 found no log line; the host now records whether options.ini chose a resolution. Seen in game on bb469d7 (A2, a 1440 display, so branch and log only): with no resolution in options.ini it logs the display's height 1440; an explicit resolution, off and other heights are covered by the test only |
| | WindowPositionX/Y | 🟡 | Planned monitor-choice adaptation now in native source: F6 Cameras Advanced Gameplay display saves [Window] MonitorDevicePath, an opaque Windows CCD target identity, for next start. Empty/absent preserves primary/CW_USEDEFAULT/existing primary-centered row. Unique active selection fills its borderless monitor or centers/fits a bordered window in its work area; Separate uses it as local center of the unchanged shared RacingRow rule. The same snapshot supplies auto-resolution height. Missing/ambiguous/cloned/query-failed identity keeps saved text, logs fallback and uses default behavior; no index/friendly-name remap or desktop topology changes. Test processes ignore it. Affected ClangCL x64 Release desktop and new startup_monitor target build at bf37a264; single CTest passes32 checks,1/1 zero skipped, original hub artifacts/outrun-startup-monitor-20261017/build-01. No live monitor query/window/game/provider/UI/owner install; live F6 saving/window placement/DPI/runtime unqualified. Wheelkit dynamic picker remains later shared UI work. See docs/GAMEPLAY-DISPLAY.md. |
| Triple | Screens | ✅ | the planar span, as Tweaks: one wide Hor+ view over three equal monitors, HUD and menus centred; Off keeps an unstretched 16:9. Projected views (beyond Tweaks): a default-off spike, see step 3 |
| Misc | RestoreJPClarissa | 🟡 | off by default as in Tweaks: Tweaks' five patches, each at its decomp counterpart. 487F50 (and the native 488090 reader): the O2SP constants G00_USA 6 -> G00 5 (GH00 7 with hi-def characters) and, in mode 3, GH00_USA 8 -> GH00 7. Arcade ending: motion set 3 -> 2 where 4527F0's branches join (Tweaks' 4529A5 hook), 5A4618 entry 1 0xCF -> 0xCE (obj_chr_gal_pmt), and 4B5A80's force of kind 0xE to 0xF skipped (4B5AE6). Unit-tested (test_car_base_shadow); not yet seen in game |
| | DisableCountdownTimer | 🟡 | off by default as in Tweaks: the race clock's decrement flag (.data 637911, the PC's Sumo_CountdownTimerEnable; its only readers 450B03 / 450B4B are race_manager.cpp's two time_decrement_637911 tests) is 0 when on. No launch parameter (Tweaks' -OuttaTime). The focused `countdown_timer` CTest exercises both flag values across 0x10/0x12/0x13 and saved variant 4 (10 cases; production timer, scripted services). Not yet seen in game |
| | SkipIntroLogos | ➖ | the decomp already starts without the intro logos: the native runtime's 6319A1 (Sumo_IntroLogosEnable, 1 in the PC's .data) starts at 0, so Tweaks' SkipIntroLogos = true is its behaviour always; showing the logos is not built (read from the source, not checked in game) |
| | ShowOutRunMilesOnMenu | ✅ | on by default as in Tweaks: 441370's not signed in status (4413E2: string 0x474, "Not Signed In") reads "Not Signed In - OutRun Miles: N", Tweaks' "%s - %s %d" with string 0x21D and the active license's Miles (+24 of 7C23E0, the float the showroom reads) truncated to an int (pc_screen.hpp, native_runtime.cpp native_state2_ui_services); the signed in branch is unchanged; `[Misc] ShowOutRunMilesOnMenu = false` restores the plain status; tests/test_signin_text.cpp. Seen on MAIN MENU ("Not Signed In - OutRun Miles: 0", and the plain line with the opt-out) in a disposable copy, 2026-10-13 (hub `artifacts/outrun-signin-miles-20261013/menu-run1`); a license with Miles above 0 not yet seen |
| | AllowCharacterSelection | 🟡 | off by default as in Tweaks: the license editor's menu (frontend_license_editor.cpp, 4DDC50's menu branch) reaches its character item from the name (Down) and the nationality (Up) through 4DD990(5), where the PC skips it; the item's own branch (confirm opens the character grid) is the PC's. Fixture cases on and off pass in test_frontend_license_editor (1926 checks, c76d6d2; hub `artifacts/outrun-matrix-trace-20261014/candidate-fixtures/batch-0057`); not yet seen in game |
| | EnableHollyCourse2 | 🟡 | 3 by default as Tweaks' shipped INI (0 off; 1 unlocked with Holly 1; 2 once Holly 1 reaches A; 3 as 2 and shown only then; pc_screen.hpp). Holly's MIX 2 set is group 7 over the fourth course lane: its grade (4E82A0), unlock (4E8410), requests (4EA3C0 / 4EA610, with the PC's fourth title and footer entries) and set-up (4EEB50: as group 6 with Req_Course_MIX_PSP) are in. The lane is optional: absent or malformed, group 7 stays locked and unrequestable. No save change. The racer menu (frontend_categories.cpp) has Tweaks' eighth tile, its 5CE400 navigation edits (mode 3: only once unlocked), the bottom frame moved to +674 and group 7's texts; the PC's tables already hold the eighth entries. Fixtures pass (f7011e1; hub `artifacts/outrun-matrix-trace-20261014/candidate-fixtures/holly-0129`): frontend_categories, frontend_progress and its owned run (the retail fourth lane is valid, its 15 course types shared with no other lane), reconstructed_driving; not yet seen in game |
| | EnableLevelSelect | 🟡 | Default-off native route with source successor F6 Controls Advanced Start with level selection, same Misc.EnableLevelSelect one-key save/rollback and next-start consumer; ClangCL x64 Release desktop and four affected fixture targets build at b65ff74; CTest passes 4/4 with zero skipped (level_select543, level_select_input92, outrun_capture2199, consumed_replay27: 861 checks), original hub artifacts/outrun-level-select-controls-20261017/build-01. No game/provider/UI/install. First frontend key27 instead of0 at445756; existing translated level-menu factory/widgets/renderer retained. Native4E7800 preserves66-row ordering, navigation/wrap/back and key28 confirmation; local native SwitchNow0x100 alias joins0x10, with original classification tables. Course words reach both translated and live native state. Shift alias now uses the normalized window keyboard and a separate final.levelSelectShift consumed recording/replay channel (older rows released); existing native input fields remain intact. Live publisher absent in test/playback, F6 blocks it, focus loss clears it; global asynchronous key semantics are not reproduced. Return history and off path unchanged. Focused offline route/input/capture/replay checks pass; full route/rendered/runtime replay parity pending. See windows/docs/LEVEL-SELECT.md. |
| | RandomHighwayAnimSets | 🟡 | Default-off Tweaks local-mode replacement in source: choose0..3 before44D410's C2C branch, table and event conditions, including when the real mode is3/4; forced override returns first. Actual game mode and game RNG stay unchanged. Independent deterministic v1 stream0x4F523253 replaces Tweaks' per-call hardware entropy for cold-start recording consistency; no runtime/replay equality claim. Takes precedence over the separate Rows option. Focused selector/Rows fixtures passed on cumulative c5d3a9f (2679/245 checks); unchanged sources retain those passes. F6/Wheelkit exposure built and passed 3 affected settings/catalog cases on October 16; no runtime/install claim. See docs/RANDOM-HIGHWAY-ANIM-SETS.md. |
| | RandomHighwayAnimationRows (not a Tweaks key) | 🟡 | distinct default-off table-row-only variant in source. The focused highway_animation_rows fixture passed 245 checks at 5506d72 (hub artifacts/outrun-highway-rows-offline-20261015/run-04, lease released) and again in the c5d3a9f cumulative focused batch (artifacts/outrun-cumulative-shift-highway-20261015/build-01, junit pass; that batch's own lease cleanup stayed pending); the test and pc_screen.hpp are unchanged from c5d3a9f to 5ca57c0. Runtime animation and replay remain unrun. When RandomHighwayAnimSets is off, forced rendition and native C2C mode3/4 return before Rows randomness advances; event conditions retain the actual mode. Other modes choose one of four existing rows. v1: independent xorshift32(13,17,5), initial state0x4F523252 once per process, top two bits after each advance; retained across area/menu calls, no game RNG consumption. If Sets is on it takes precedence and Rows does not advance. Existing cold-start recording snapshots preserve Misc and pin the executable; no runtime replay equality or arbitrary midprocess restore is claimed. No link/install. |
| | ProtectLoginData | 🟡 | Actual shared profile load/common-save consumers and Windows user-scoped DPAPI adapter in source; native default remains Off, next ordinary start only. New common writes scrub the login block after checked encrypted-sidecar publication; failures refuse plaintext fallback. F6 uses the same saved key. Desktop and two affected CTests pass130 synthetic checks (79 login,51 existing profiles) on5bb80ce; real DPAPI/rendered persistence unqualified. See docs/ONLINE-LOGIN-PROTECTION.md. |
| | DemonwareServerOverride | 🟡 | Actual DNS/IPv4 resolver consumer and lazy Windows UDP supplier in source, empty default preserves prior Windows-off behavior. Ordinary next-start only; native and translated5349B0 routes consume the override. At81572d5 (test initializer fix on ec12fe2) the cached Release desktop and the focused desktop_network_sockets fixture (actual translated resolver, injected fake DNS/UDP) pass, 1/1 (hub artifacts/outrun-network-consumer-20261017/build-02; ec12fe2's own build-01 failed only that test's /WX initializer). Connected imports have no recovered caller in the current closure and remain unsupported; no online/account acceptance or UPnP. See docs/NETWORK-TRANSPORT.md. |
| Overlay, WheelSettings | in-game overlay, F6 wheel page | 🟡 | the F6 panel (docs/F6-PANEL.md): Controls, FFB (strength, effects, Stop FFB, the wheel output at the next start), Cameras, Telemetry, Help; Tweaks' own overlay windows are not reproduced |
| Bugfixes | FixLensFlarePath, FixIncorrectShading | 🟡 | on by default as in Tweaks. FixLensFlarePath: 44A630's offsets read from `Media\lens_flare_offset.bin` (where the game ships it) when present, else `common\`; runtime.log names the source. FixIncorrectShading: 40E140's TWOSIDE lighting entries (73F04C / 73F050) link the DOUBLE fragments (61FB48 / 61FC88), unit-tested in test_pc_shader_programs. Neither yet seen in game |
| | FixFullPedalChecks | ✅ | on by default as in Tweaks: 453720 / 453750 read accelerator and brake 250..254 as 255 (race_input.hpp), for the full-press checks such as the backfire; the stored channels, edges and consumed capture are unchanged; `[Bugfixes] FixFullPedalChecks = false` restores the PC's own; tests/test_pedal_volume.cpp |
| | FixPegasusClopping | ✅ | on by default as in Tweaks: the Pegasus event 44BB30 starts the clop loop (SND_LOOP \| 0x8D) and SndOff_PEGA 44BCC0 stopped only 0x8C; 44BCC0 now also requests SND_STOP \| 0x8D (pc_screen.hpp, race_area.cpp); `[Bugfixes] FixPegasusClopping = false` restores the single stop; tests/test_pegasus_sound.cpp. Not yet heard at the rig |
| | PreventDESTSaveCorruption | ➖ | not reachable here (bounded path audit, 2026-10-13): the game-side DirectInput is pc_input_devices.cpp's fake one, which enumerates one pad and the keyboard (create_403de0), so at most 2 device records of 0xB0 are written from 7C211C, below the 0x2C4 before the 7C23E0 license; the real devices never reach that enumeration, and native_pc_input_create_403de0's created guard keeps a repeated init from adding records |
| | HideOnlineSigninText | ✅ | off by default as in Tweaks' INI (its b3c1825; its code default is still true). Tweaks' five patches at their decomp counterparts: 443256's jump, so 443110 skips the 4400D3 sign-in box's open (44327C) and commit (443286), and 441370's status text returns at its first test, as Tweaks' RET there does (44327C is the only open of +C48); the 446510 caption and the 446A80 / 446B20 icon of a help-bar slot whose caption is 664 (SIGN-IN) are left out (frontend_slot_captions_446a50, the r081 slot open's skip_caption). On, it also hides ShowOutRunMilesOnMenu's line, as in Tweaks. tests/test_signin_text.cpp and test_driving's r081 checks. Seen on MAIN MENU in a disposable copy, 2026-10-13: no status text or box, the help bar without SIGN-IN and its F1 icon, SELECT, LICENSE and BACK kept (hub `artifacts/outrun-signin-miles-20261013/menu-run1`); other menus' help bars not checked |
| | FixC2CRankings | ➖ | always on here: the decomp has no launcher, so every site the PC gates on its launcher check (OpenEventA of the protection's event, which the Steam build never creates; Tweaks creates it) takes the present path: the nine record inserts and the ghost save write 4168D0 do not reproduce the check, and the translated ghost module's 465EC0 / 4168D0 calls answer as present (race_ghosts_runtime.cpp). 40E888 (the Direct3D error box title) is not run. tests/launcher_gate_audit.py keeps every gated site accounted for |
| | FixRightSideBunkiAnimations | 🟡 | on by default as in Tweaks: the right-side branch model's 44F6DC store of the culling flag at +0x90 is skipped (`race_area.cpp`); off preserves the PC store, and the left-side path is unchanged. The `bunki_animation` fixture exercises all four combinations through the production area object loop. Not yet seen in game |
| | FixParticleRendering | 🟡 | in source since b8c9f8b, on by default as in Tweaks (pc_screen.hpp): draw_418cf0's screen mapping used the full width/height where the Xbox code had half (grass off screen; race_particles_draw.cpp halves sx, sy and the size at 419009 as Tweaks does), and the Metropolis fireworks (44DE10 object 0x630001 had no node list; race_area.cpp draws 0x1A000A with a fixed list as Tweaks does). Not yet seen at the rig |

### Experimental general presentation successor (October 17)

The October16 source-boundary diagnosis is resolved in the opt-in successor:
Performance.FramerateUnlockExperimental defaultsOff, preserving the current60/120
consumer and audio cadence. On uses Performance.FramerateLimit0 (no CPU cap) or
60..1000, Present0 plus the existing host clock/sleep, and the original60Hz
zero/multitick accumulator/interpolation. PcSound control moves to its existing
42F330 per-update callback only when enabled; PCM/movie clocks remain unchanged.
F6 exposes next-start toggle/cap after those real consumers, with the existing
save/rollback. Normal options.ini framerate remains preserved; the experimental
cap owns the rate while enabled. Tests/record/playback ignore it. No new clock,
profiler/framework, topology, tearing or VRR change. This is an adaptation rather
than full efficient/accurate-limiter/VSync parity; caps below60 are unsupported.

Release desktop compilation and both unique affected fixtures pass: 5,309
general-presentation checks at fixture-only b62d5d3 plus 244 real-host sound checks
retained from 203bcb8. Original fixture fault and its host-context lifetime fix
remain in hub artifacts/outrun-general-presentation-20261017/build-01 and build-02. Existing monitor32/calibration175/level861/cache351/
extraction782/CD/course results remain retained. Runtime smoothness, scanout,
audio/input and physical acceptance remain open. See docs/GENERAL-PRESENTATION.md.

### Limiter and VSync source successor (October 17)

Separate from qualified0551df8, FramerateLimitMode0/1 and independent VSync0/1/2 now
have actual existing-pacer/DXGI consumers and ordinary next-start F6 choices.
Absent/empty VSync preserves existing pacing; an explicit override without general
unlock retains a CPU cap at the ordinary selected rate. Tests/record/playback ignore
these keys; the existing fixed-tick simulation/audio path is reused for the new override.
At7292710, affected Release desktop and both new CTests pass465 checks (224 controls,
241 sound),2/2 zero skipped. Prior5553 checks are retained. Original evidence:
hub artifacts/outrun-pacing-vsync-20261017/build-01. Actual UI/save, CPU/pacing,
audio, scanout and physical acceptance remain open. See docs/GENERAL-PRESENTATION.md.
