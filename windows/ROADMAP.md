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
   - Fine steering: source finding (2026-10-13, deferred by sana 61131), nothing built.
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

| Section | Settings | Status | Notes |
| --- | --- | --- | --- |
| Performance | FramerateLimit, FramerateLimitMode, VSync | 🟡 | upstream: 60 / 120 fps row, presents at the monitor rate |
| | FramerateFastLoad | 🟡 | Default-off mode 2 source candidate: existing frame-latency wait/Present bypass only in START before stage 65, one loading update per frame at 60/120 Hz, live restoration and existing-clock rebase on exit. Focused transition/config fixtures authored but unrun; no build/runtime or load-time claim. Modes 1/3 unsupported. [Source contract](docs/FAST-LOAD.md). |
| | FramerateUnlockExperimental | ⬜ | |
| | SingleCoreAffinity | ➖ | a workaround for the original |
| Controls | SteeringDeadZone | ✅ | |
| | UseNewInput, ControllerHotPlug | 🟡 | gamepads via Windows.Gaming.Input (hot-plug); ordinary DirectInput rescans every two seconds and reopens the saved instance GUID, built but not checked at the rig |
| | DefaultManualTransmission | 🟡 | off by default as in Tweaks: after 4DD250 (frontend_transmission_init_4dd250, an exact reconstruction) the transmission owner's +E4 = 0 / +E8 = 1, the carousel's (+34) previous / current index, as Tweaks' detour, so confirming stores Manual in 830374; off leaves every byte as 4DD250 left it (tests/test_default_transmission.cpp). `[Misc] DefaultManualTransmission`: Tweaks' `[Controls]` here is the ini-controls-1 section, where an unknown key is an invalid binding (dbce_controls.hpp). F6 Controls and Wheelkit's Controls page offer it as "Manual transmission by default" (the next start). The 2026-10-14 observed A/B in hub `artifacts/outrun-transmission-default-20261014` showed AUTOMATIC with the setting off and MANUAL with it on; manual shifting and physical feel remain unqualified. |
| | HudToggleKey | 🟡 | `[Misc] HudToggleKey` (Tweaks keeps it in `[Controls]`, the controls contract here), unset by default as in Tweaks; Tweaks' names (a letter or digit by the layout, F1-F24, HOME, NUMPAD0-9 ...), matched by key position in the window's keyboard. Each release flips NaviPub_Disp's flag 8447F8 (Tweaks' 4447F8), which the race HUD applies just before 4BEB00; the game resets it at each race's init, and a press before it is dropped. The key is not consumed (the game and its menus still see it, as Tweaks' GetAsyncKeyState). F6, F8 and F11 are refused, and nothing toggles while the panel is open; a clear of the window's keyboard (focus loss, the panel opening or closing) is not a release, so the key waits for its own. Only keys the game's window receives count; it need not be in front. Replay: presentation only, outside the tape; off in recording, playback and test processes, so no tape is made or played with a toggled HUD (`windows/source/hud_toggle.hpp`, `tests/test_hud_toggle_navi.cpp`). The 2026-10-14 observed run in hub `artifacts/outrun-hud-toggle-20261014` (candidate 214d785, a muted private-copy race with F9 posted to the game's window) showed the race HUD visible, hidden after the first release and visible after the second, while the race kept running; that is presentation only, with no recording or input-response claim. |
| | Vibration\*, ImpulseVibration\* | ⬜ | gamepad rumble / trigger motors |
| DirectInput | device, axes, inverts, sensitivity, buttons, POV bindings | ✅ | canonical `[Controls]` (`ini-controls-1`); the Tweaks keys are imported once with Tweaks' scaling |
| | ThrottleDeviceGuid, BrakeDeviceGuid | ✅ | each binding names its own device instance |
| DirectInput.Calibration | Steering/Throttle/Brake Min/Center/Max | 🟡 | one-time import into existing schema-2 `cal=`: steering importer840d82a passed the 101-check `desktop_controls` fixture and selected wheel_input.cpp compile (hub `artifacts/outrun-calibration-offline-20261015`; no desktop link/install). Throttle/brake import is now in source, focused cases unbuilt/unrun: independent devices and minimum/maximum endpoints, center validated but unused for pedal scaling, inversion once, whole-count rounding. Disabled calibration preserves the full-range route; invalid enabled endpoints or nonzero/invalid legacy pedal deadzone refuse only that binding with a note (the canonical contract cannot encode that deadzone curve). No existing canonical binding is rewritten. Installed341B already reads canonical schema 2 from3708a05; both new legacy importer slices are excluded from that executable. |
| DirectInput.Shifter | sequential | ✅ | |
| | hpattern gears 1-6 | 🟡 | pulses toward the gate's gear (car +208); R: the game has no reverse |
| DirectInput.Aux | DeviceGuid, buttons | ✅ | any action may sit on any device (one binding per action) |
| Telemetry | Enable, SharedMemName | 🟡 | step 5: `[Telemetry] Enabled`, `SharedMemName`, `Layout`; reception confirmed, per-field accuracy open |
| FFB | all | 🟡 | step 4: built and tested offline; no owner package ships WheelFfb.dll yet |
| Graphics | resolution, AA, widescreen | ✅ | upstream rows (Options > Settings) |
| | UIScalingMode | 🟡 | windows/docs/UI-SCALING.md: 2 (keep centered, the centered 4:3) by default here; 0 stretches the 2D over the 16:9 frame, or over a span's center panel only; 1 (Tweaks' Online Arcade) is partial: the 2D mapping widens at the 4:3 scale to the frame's edges (or the center panel's), and NaviPub_Disp's HUD is spaced to them with Tweaks' unspaced parts (rival labels / markers, hearts, LAN markers, and from the level / lap / countdown on). Step 2b adds time attack's forced sides, the put_scroll positions (time attack, rank, REV, heart number) and the ghost gap. Step 2c adds C2C scores, mission and GF request bubbles, and an 18-site source bridge for translated GF bubbles and rank positions; C2C goal results remain unported. Unit-tested (test_triple_screens; the device path in test_d3d11_vertex_path). Seen in game on the race route only (UI-SCALING.md, Step 4): the HUD moves ±240 px at 2560x1440 with no clipping. A start-camera "1P" banner drawn past the 4:3 edge appears in mode 1 only. Time attack and C2C have not been seen |
| | UILetterboxing | 🟡 | 1 by default as in Tweaks (0 none, 1 outside the race, 2 always), only with UIScalingMode 1 or 2 and while the 2D is in the centered 4:3: black bars outside it, at the device's own 4:3 edges, under the badge and the F6 panel (desktop_panel.cpp, ClearView). F6 Cameras (Advanced) and Wheelkit's Cameras page offer it as Off / Outside the race (default) / Always. Tweaks' rule: outside GAME, SMPAUSEMENU, GOAL, TIMEUP, OUTRUNMILES, TRYAGAIN, and START before its stage 65 (pc_screen.hpp); tests/test_letterbox.cpp. Seen at 16:9 in a disposable copy, 2026-10-13 (hub `artifacts/outrun-letterbox-20261013/check-run1`): 2 bars menus, intro and race at the 4:3 edges; 1 and 0 leave the race at full 16:9. Not seen: 1's bars against 0 on a non-race screen with content outside the 4:3, the badge or F6 on top, UIScalingMode 1, a span |
| | AnisotropicFiltering, ReflectionResolution, TransparencySupersampling | ✅ | `[Graphics]`, off by default; anisotropy only on samplers linear in both min and mag (the HUD stays sharp); alpha-to-coverage only under MSAA |
| | DisableVehicleLODs, DisableStageCulling | ✅ | off by default. With all five options on (2026-10-12 A/B, n = 1 vs 2): GPU draws about 2.1x and 1 to 2.5 % of race frames miss vsync; not attributed per option |
| | ScreenEdgeCullFix | 🟡 | on by default as in Tweaks: the object test 4F0E40 (cones, course objects, gates) takes the widened widescreen bounds 4F02D0 (cars) already had; identical at 4:3; not yet seen in game. Source-only follow-up exposes the existing flag as "Widescreen object visibility" in F6 Advanced Cameras and Wheelkit @6 Cameras, next-start only. These controls are unbuilt/unrun during the owner pause; the runtime algorithm is unchanged. |
| | DrawDistanceIncrease/Behind | 🟡 | off by default; Tweaks' 44DF6D merge (dedup, exclusions with the offset-0 exemption, Canyon limit) in 44DE10, a lods.ini read only, queues grown as DrawBufferExtension; unit-tested, not yet seen in game |
| | (renderer) D3D9 point sprites | 🟡 | not a Tweaks key: the port drew no POINTLIST at all, so sparks (source 3) and effect sources 8-10 were invisible; drawn per the D3D9 docs since b978928; unit-tested, visual check pending |
| | FixZBufferPrecision | 🟡 | off by default; Tweaks' near-plane rule on the main race camera (1.0 chase/bumper, 0.25 in-car, 0.2 360SP, 0.1 switching/outside GAME) and 0.05 for the 40BE70 passes (projection only: 484BD0's view part is stateful); unit-tested, not yet seen in game |
| | CarBaseShadowOpacity | 🟡 | 1.0 by default as in Tweaks: the race car's flat base shadow (CalcPeraShadow, nulled on the PC) restored at Tweaks' three sites (469EB4 race, 46AC76 / 46B766 car select) with the PC's own alpha; unit-tested |
| | UseHiDefCharacters | 🟡 | on by default as in Tweaks: 487EE0 / 487F50 (and the native 488090 reader) return MH00 for the 6549B0 entry 0 and the O2SP constant, LH00 / GH00 for the 6549C8 entries 3 / 4, and LH00 / GH00_USA for the O2SP constants; CHR_DR_GH00(_USA).bin from Media (it has the hair animations) when present, else Chr; runtime.log names it. Unit-tested (test_car_base_shadow); not yet seen in game. RestoreJPClarissa: see Misc |
| | SceneTextureReplacement, TextureBaseFolder | ✅ | off by default; the Tweaks pack layout and names (`<bank>/<index>_<XXH32>_<w>x<h>.dds`), the game's own create request; files the device's DDS reader would refuse are never offered (stock kept). A create the device still refuses falls back to stock in the loader, but the device's error latch ends the run at present, so a genuine allocation failure can remain terminal. In game (2026-10-12, owner pack copied, muted): 344 of 834 replaced at 60 fps, 0 device errors; runtime.log lists per bank what was replaced and which banks the pack has no folder for (that race's stage, `cs_CS_PALM_pmt`, is one: the pack has `cs_CS_PALM_T_pmt`). Cube maps with 2D pack dumps stay stock |
| | UITextureReplacement | 🟡 | off by default, as SceneTextureReplacement here (Tweaks' INI has both on); same folder and index as the scene textures. The sprite banks' XST textures under `<XST stem>/` (`spr_etc_xst`, `spr_sprani_game_cvt_Exst`), named as Tweaks dumps them: `<index>_<XXH32>_<w>x<h>.dds` (index = the textures the bank created before it, Tweaks' CurrentTextureIdx), then `<XXH32>_<w>x<h>.dds`; Tweaks' FXT hash remaps. A replaced texture keeps the stock size as the texel space of the XST image tables and SPRANI frames: footage and 42C0F0 UVs over the stock size, ID3DXSprite source rectangles moved onto the replacement with the scale reduced by the same ratio, so positions, centring and alpha stay as stock (unit-tested: 2x, 3x, unequal 3x/2x, flipped, rotated). Offline against an owner Tweaks pack (read only, the retail check of test_pc_sprite_replacement): all 257 UI files matched, 0 kept stock. In game (2026-10-12, muted A/B, owner pack's UI folders copied, 2560x1440): 160 of 176 sprite textures looked up on the menu-to-race path replaced, 0 kept stock; menu and HUD closeups match stock positions, sizes, centring and alpha with sharper art; race 60.0 fps in both, max frame 20.2 ms vs 21.6 ms stock. Costs: the front-end bank's pack work runs on the main thread when the bank loads (1.6-1.9 s with a warm file cache, 8.6 s cold offline), and private memory rises 0.83 -> 4.23 GB, mostly because the device keeps a DDS and an RGBA8 copy of every texture besides the RGBA8 GPU texture. That pack's prompt atlas shows gamepad buttons where stock shows keys (pack content; Tweaks uses the same base file without a pad type). Source-only, unbuilt: Tweaks' six-hash pad prompt-folder precedence and first non-PC latch, using a conservative presentation hint from the already-selected physical WGI pad (unknown/no selected pad stays generic); see TEXTURE-REPLACEMENT.md. No input/isolation/owner-pack changes. Not built: UITextureExtract. The START loading picture (bank 0x2F, drawn by the preview renderer) takes the same lookup under `spr_SPRANI_LOADING_CVT_Exst/`: a replaced texture is decoded to RGBA8 by the device reader and carried in the in-memory loading pack (OR2LDP3: at most 128 MB for the pack and 16384 texels a side), the stock size kept as the crops' texel space; with the option off, or no file for any of its textures, the pack is the stock OR2LDP2 bytes. The pack is rebuilt from the game files at each start, so the option takes effect at the next start and nothing is kept between runs. Unit-tested (synthetic packs; the retail START pack with one replaced texture, off and no-match byte-identical); not yet run in game |
| | (scene and UI) pixel formats | ✅ | A8B8G8R8 / X8B8G8R8 pack files are read as A8R8G8B8 (R and B swapped), as Tweaks' loader converts them; about half of the UI files in the owner pack use it |
| | TextureMatchAnyFolder (not a Tweaks key) | 🟡 | off by default: a texture with no file under Tweaks' names takes a same-name file (XXH32 and size of the stock DDS by the pack's naming, any index prefix) from elsewhere in the pack when it is the only one or all such files are byte-identical; differing files are ambiguous and keep stock. Built for stage variants: Tweaks looks only in the bank's own folder and the root, so a pack made on cs_CS_PALM_T_pmt leaves cs_CS_PALM_pmt stock. Offline over every PMT bank against an owner Tweaks pack (tools/texture_pack_coverage.cpp, read only): Tweaks' names replace 1,277 of 14,395 textures; with this option 3,335 (cs_CS_PALM 0 -> 154 of 166, the _R / _T variants of the covered stages 118-154 each), 300 ambiguous kept stock. Unit-tested (test_pc_pmt_loader); not yet seen in game |
| | (scene) cube maps with 2D pack files | ➖ | the request is a cube, the file a 2D image: kept stock (16 in the owner pack) |
| | SceneTextureExtract, EnableTextureCache | ⬜ | |
| | Texture CPU copies (port memory, not a Tweaks key) | ✅ | `e167f58`: a file texture releases its decoded RGBA copy once the D3D11 device has uploaded it (UpdateSubresource copies before it returns), keeping its DDS levels; any CPU reader decodes a level again, and a CPU-written texture is retained for good (file textures cannot be locked, so that rule is defensive). `OR2_KEEP_TEXTURE_RGBA=1` keeps the old lifetime. Measured by deli (one exe, settled race, muted; 2026-10-12): HD UI private bytes 4,250 -> 3,711 MB (-539 MB, 371 textures released, 544.6 MB), stock 829 -> 791 MB; no timing difference visible. Only textures actually drawn are released. **Follow-up, deferred (sana 45486):** decode RGBA lazily at the first upload so never-drawn pack textures hold only their DDS levels; it moves the decode to first use and needs a first-draw hitch qualification. BC direct upload is a separate follow-up |
| | UseNewTextureAllocator | ➖ | |
| Audio | AllowHorn | 🟡 | on by default as in Tweaks (`[Audio] AllowHorn`): while the controls contract's own `horn` action is held in GAME, SE 0x1BC is queued looping every frame on the race SE queue 9563E8 (Tweaks' AudioHooks_Update), and stopped on release or on leaving GAME (race_sound_runtime.cpp native_race_horn, at 453BB0's input step on the final input, after a consumed playback replaced it; the stop stays pending until the queue takes it). Not the X switch: in 5A7B50[0] the device button behind it (0x8) is shift-down, so the horn is its own consumed action (shim adapter and native wheel path), with no record bit, recorded as `final.horn` (optional when decoding older rows). A Tweaks INI's `ButtonX` imports as `horn`. Fixture cases added (adapter, capture, controls import, the SE queue and the replay seam), their execution pending; not packaged until Wheelkit accepts the 20-action capability; not yet heard in game |
| | AllowWAV, AllowFLAC | 🟡 | Both default false here (Tweaks true). Same-stem enabled existing FLAC > WAV > original Ogg in Sound, case-insensitive lookup; selected read/codec/format failure refuses without lower-priority fallback. Optional isolated static FFmpeg 7.1.1 WAV PCM U8/S16LE/S24LE/S32LE/F32LE and FLAC, mono/stereo8–192kHz; source-rate S16/full-gain mono duplication/linear48kHz mix with exact integer source phase. d71f1ba offline PASS:18,807,664 owned numeric checks, focused CTest1/1 and three affected production compiles; original no-FFmpeg51-check pass retained (hub music continuation3292E6EB). Input64MiB, bounded metadata/packets/frames/PCM/work and owned AVIO/decoder cleanup. Actual Sound lookup, cumulative music link, audible/runtime/performance/full Tweaks parity pending. Current Bink-only prefix and qualified746A untouched; CD switcher/override outside this slice. Source follow-up: native F6 Advanced Controls and Wheelkit @6 expose the two explicit next-start flags, default off; native F6 reports decoder availability. These new rows are unbuilt/unrun and do not promote audio/runtime qualification. |
| | AllowFLACLoopPoints (optional config, not a Tweaks key) | 🟡 | Default false; new source/focused fixture additions UNRUN. With valid native FLAC tags and caller loop=true, play intro0..end-exclusive then repeat start..end-exclusive, retaining continuous integer mix phase. Explicit END is exclusive. LENGTH deliberately preserves observed Tweaks arithmetic end=start+length-1, requiring LENGTH>=2; it overrides END regardless of tag order. Last duplicate per key wins only if every recognized value is valid strict unsigned decimal (no sign/space/junk/overflow); incomplete tags, multiple comment blocks and invalid/unknown ranges refuse. Require known total and0<=start<end<=total; exact source-sample timestamps, codec flush, bounded backward seek/discard/trim, no approximate boundary fallback. Caller loop=false validates tags but plays the full file, intentionally preserving this API rather than Tweaks Read's unconditional custom loop. Whole-file untagged WAV/FLAC and Vorbis behavior retained. No new UI knob or owner INI write; full parity/runtime/audible acceptance pending. |
| CDSwitcher, CDTracks | in-race track switching, track list | ⬜ | |
| Window | WindowedBorderless, hidden cursor, DPI | ✅ | borderless over the primary monitor |
| | AutoDetectResolution | 🟡 | on by default as in Tweaks (its GameDefaultConfigOverride): while options.ini holds no resolution, the render resolution takes the supported height nearest the display's (the primary monitor's; 720 for --windowed) instead of the port's fixed 1920x1440 default; a resolution in options.ini (Settings writes one on any change there) always wins; off, or a latched (test) run, keeps 1920x1440; no display mode is changed (enhancements/video.hpp, xbox/source/main.cpp, window_desktop.cpp). F6 Cameras (Advanced) and Wheelkit's Cameras page offer it as "Resolution from the display". tests/test_auto_resolution.cpp. As first built (123a975) it acted only on an unset height the host never has, a no-op: observation A1 found no log line; the host now records whether options.ini chose a resolution. Seen in game on bb469d7 (A2, a 1440 display, so branch and log only): with no resolution in options.ini it logs the display's height 1440; an explicit resolution, off and other heights are covered by the test only |
| | WindowPositionX/Y | ⬜ | not exact here: the borderless window fills a monitor (the primary, or the span), so a position would choose a monitor rather than place a corner; deferred as a separate "choose monitor" option (sana 58967) |
| Triple | Screens | ✅ | the planar span, as Tweaks: one wide Hor+ view over three equal monitors, HUD and menus centred; Off keeps an unstretched 16:9. Projected views (beyond Tweaks): a default-off spike, see step 3 |
| Misc | RestoreJPClarissa | 🟡 | off by default as in Tweaks: Tweaks' five patches, each at its decomp counterpart. 487F50 (and the native 488090 reader): the O2SP constants G00_USA 6 -> G00 5 (GH00 7 with hi-def characters) and, in mode 3, GH00_USA 8 -> GH00 7. Arcade ending: motion set 3 -> 2 where 4527F0's branches join (Tweaks' 4529A5 hook), 5A4618 entry 1 0xCF -> 0xCE (obj_chr_gal_pmt), and 4B5A80's force of kind 0xE to 0xF skipped (4B5AE6). Unit-tested (test_car_base_shadow); not yet seen in game |
| | DisableCountdownTimer | 🟡 | off by default as in Tweaks: the race clock's decrement flag (.data 637911, the PC's Sumo_CountdownTimerEnable; its only readers 450B03 / 450B4B are race_manager.cpp's two time_decrement_637911 tests) is 0 when on. No launch parameter (Tweaks' -OuttaTime). The focused `countdown_timer` CTest exercises both flag values across 0x10/0x12/0x13 and saved variant 4 (10 cases; production timer, scripted services). Not yet seen in game |
| | SkipIntroLogos | ➖ | the decomp already starts without the intro logos: the native runtime's 6319A1 (Sumo_IntroLogosEnable, 1 in the PC's .data) starts at 0, so Tweaks' SkipIntroLogos = true is its behaviour always; showing the logos is not built (read from the source, not checked in game) |
| | ShowOutRunMilesOnMenu | ✅ | on by default as in Tweaks: 441370's not signed in status (4413E2: string 0x474, "Not Signed In") reads "Not Signed In - OutRun Miles: N", Tweaks' "%s - %s %d" with string 0x21D and the active license's Miles (+24 of 7C23E0, the float the showroom reads) truncated to an int (pc_screen.hpp, native_runtime.cpp native_state2_ui_services); the signed in branch is unchanged; `[Misc] ShowOutRunMilesOnMenu = false` restores the plain status; tests/test_signin_text.cpp. Seen on MAIN MENU ("Not Signed In - OutRun Miles: 0", and the plain line with the opt-out) in a disposable copy, 2026-10-13 (hub `artifacts/outrun-signin-miles-20261013/menu-run1`); a license with Miles above 0 not yet seen |
| | AllowCharacterSelection | 🟡 | off by default as in Tweaks: the license editor's menu (frontend_license_editor.cpp, 4DDC50's menu branch) reaches its character item from the name (Down) and the nationality (Up) through 4DD990(5), where the PC skips it; the item's own branch (confirm opens the character grid) is the PC's. Fixture cases on and off pass in test_frontend_license_editor (1926 checks, c76d6d2; hub `artifacts/outrun-matrix-trace-20261014/candidate-fixtures/batch-0057`); not yet seen in game |
| | EnableHollyCourse2 | 🟡 | 3 by default as Tweaks' shipped INI (0 off; 1 unlocked with Holly 1; 2 once Holly 1 reaches A; 3 as 2 and shown only then; pc_screen.hpp). Holly's MIX 2 set is group 7 over the fourth course lane: its grade (4E82A0), unlock (4E8410), requests (4EA3C0 / 4EA610, with the PC's fourth title and footer entries) and set-up (4EEB50: as group 6 with Req_Course_MIX_PSP) are in. The lane is optional: absent or malformed, group 7 stays locked and unrequestable. No save change. The racer menu (frontend_categories.cpp) has Tweaks' eighth tile, its 5CE400 navigation edits (mode 3: only once unlocked), the bottom frame moved to +674 and group 7's texts; the PC's tables already hold the eighth entries. Fixtures pass (f7011e1; hub `artifacts/outrun-matrix-trace-20261014/candidate-fixtures/holly-0129`): frontend_categories, frontend_progress and its owned run (the retail fourth lane is valid, its 15 course types shared with no other lane), reconstructed_driving; not yet seen in game |
| | EnableLevelSelect | 🟡 | Default-off source, unbuilt/unrun: first frontend key27 instead of0 at445756; existing translated level-menu factory/widgets/renderer retained. Native4E7800 preserves66-row ordering, navigation/wrap/back and key28 confirmation; local native SwitchNow0x100 alias joins0x10, with original classification tables. Course words reach both translated and live native state. Shift alias now uses the normalized window keyboard and a separate final.levelSelectShift consumed recording/replay channel (older rows released); existing native input fields remain intact. Live publisher absent in test/playback, F6 blocks it, focus loss clears it; global asynchronous key semantics are not reproduced. Return history and off path unchanged. Focused source fixture additions unrun; full route/rendered/runtime replay parity pending. See windows/docs/LEVEL-SELECT.md. |
| | RandomHighwayAnimSets | 🟡 | Default-off Tweaks local-mode replacement in source: choose0..3 before44D410's C2C branch, table and event conditions, including when the real mode is3/4; forced override returns first. Actual game mode and game RNG stay unchanged. Independent deterministic v1 stream0x4F523253 replaces Tweaks' per-call hardware entropy for cold-start recording consistency; no runtime/replay equality claim. Takes precedence over the separate Rows option. Focused production-selector fixture source is unbuilt/unrun; no build/install/game. See docs/RANDOM-HIGHWAY-ANIM-SETS.md. |
| | RandomHighwayAnimationRows (not a Tweaks key) | 🟡 | distinct default-off table-row-only variant in source, fixture unbuilt/unrun. When RandomHighwayAnimSets is off, forced rendition and native C2C mode3/4 return before Rows randomness advances; event conditions retain the actual mode. Other modes choose one of four existing rows. v1: independent xorshift32(13,17,5), initial state0x4F523252 once per process, top two bits after each advance; retained across area/menu calls, no game RNG consumption. If Sets is on it takes precedence and Rows does not advance. Existing cold-start recording snapshots preserve Misc and pin the executable; no runtime replay equality or arbitrary midprocess restore is claimed. No link/install. |
| | DemonwareServerOverride, ProtectLoginData | ⬜ | online play; upstream has the LAN layer |
| Overlay, WheelSettings | in-game overlay, F6 wheel page | 🟡 | the F6 panel (docs/F6-PANEL.md): Controls, FFB (strength, effects, Stop FFB, the wheel output at the next start), Cameras, Telemetry, Help; Tweaks' own overlay windows are not reproduced |
| Bugfixes | FixLensFlarePath, FixIncorrectShading | 🟡 | on by default as in Tweaks. FixLensFlarePath: 44A630's offsets read from `Media\lens_flare_offset.bin` (where the game ships it) when present, else `common\`; runtime.log names the source. FixIncorrectShading: 40E140's TWOSIDE lighting entries (73F04C / 73F050) link the DOUBLE fragments (61FB48 / 61FC88), unit-tested in test_pc_shader_programs. Neither yet seen in game |
| | FixFullPedalChecks | ✅ | on by default as in Tweaks: 453720 / 453750 read accelerator and brake 250..254 as 255 (race_input.hpp), for the full-press checks such as the backfire; the stored channels, edges and consumed capture are unchanged; `[Bugfixes] FixFullPedalChecks = false` restores the PC's own; tests/test_pedal_volume.cpp |
| | FixPegasusClopping | ✅ | on by default as in Tweaks: the Pegasus event 44BB30 starts the clop loop (SND_LOOP \| 0x8D) and SndOff_PEGA 44BCC0 stopped only 0x8C; 44BCC0 now also requests SND_STOP \| 0x8D (pc_screen.hpp, race_area.cpp); `[Bugfixes] FixPegasusClopping = false` restores the single stop; tests/test_pegasus_sound.cpp. Not yet heard at the rig |
| | PreventDESTSaveCorruption | ➖ | not reachable here (bounded path audit, 2026-10-13): the game-side DirectInput is pc_input_devices.cpp's fake one, which enumerates one pad and the keyboard (create_403de0), so at most 2 device records of 0xB0 are written from 7C211C, below the 0x2C4 before the 7C23E0 license; the real devices never reach that enumeration, and native_pc_input_create_403de0's created guard keeps a repeated init from adding records |
| | HideOnlineSigninText | ✅ | off by default as in Tweaks' INI (its b3c1825; its code default is still true). Tweaks' five patches at their decomp counterparts: 443256's jump, so 443110 skips the 4400D3 sign-in box's open (44327C) and commit (443286), and 441370's status text returns at its first test, as Tweaks' RET there does (44327C is the only open of +C48); the 446510 caption and the 446A80 / 446B20 icon of a help-bar slot whose caption is 664 (SIGN-IN) are left out (frontend_slot_captions_446a50, the r081 slot open's skip_caption). On, it also hides ShowOutRunMilesOnMenu's line, as in Tweaks. tests/test_signin_text.cpp and test_driving's r081 checks. Seen on MAIN MENU in a disposable copy, 2026-10-13: no status text or box, the help bar without SIGN-IN and its F1 icon, SELECT, LICENSE and BACK kept (hub `artifacts/outrun-signin-miles-20261013/menu-run1`); other menus' help bars not checked |
| | FixC2CRankings | ➖ | always on here: the decomp has no launcher, so every site the PC gates on its launcher check (OpenEventA of the protection's event, which the Steam build never creates; Tweaks creates it) takes the present path: the nine record inserts and the ghost save write 4168D0 do not reproduce the check, and the translated ghost module's 465EC0 / 4168D0 calls answer as present (race_ghosts_runtime.cpp). 40E888 (the Direct3D error box title) is not run. tests/launcher_gate_audit.py keeps every gated site accounted for |
| | FixRightSideBunkiAnimations | 🟡 | on by default as in Tweaks: the right-side branch model's 44F6DC store of the culling flag at +0x90 is skipped (`race_area.cpp`); off preserves the PC store, and the left-side path is unchanged. The `bunki_animation` fixture exercises all four combinations through the production area object loop. Not yet seen in game |
| | FixParticleRendering | 🟡 | in source since b8c9f8b, on by default as in Tweaks (pc_screen.hpp): draw_418cf0's screen mapping used the full width/height where the Xbox code had half (grass off screen; race_particles_draw.cpp halves sx, sy and the size at 419009 as Tweaks does), and the Metropolis fireworks (44DE10 object 0x630001 had no node list; race_area.cpp draws 0x1A000A with a fixed list as Tweaks does). Not yet seen at the rig |
