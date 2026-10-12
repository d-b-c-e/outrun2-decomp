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
   Wheelkit's closed-loop tests (with Astra). Next: Astra's review, a run at the
   rig, the closed-loop qualification (STD-034), hot-plug.
2a. ⬜ **F6 panel** (STD-030: the panel comes first): toolkit `native/panel` on
   our own D3D11 renderer, standard pages, each later feature adds its controls.
3. 🟡 **Triple screens** (`windows/docs/TRIPLE-SCREENS.md`): `[Triple] Screens` Off / Surround / Separate monitors
   with the planar span (the Tweaks parity floor) is done and seen at the rig. True per-screen projection
   (STD-007/015/019/022) is a default-off spike (`OR2_TRIPLE_VIEWS=1`): one game render replicated by a geometry shader
   into three viewports, each with its clip correction from the toolkit rig math. First rig pictures 2026-10-12: three
   projections with the side sky. Open: culling for the union of the three frusta, the F6 selector, the owner's
   physical check.
4. ⬜ **Force feedback**: the force model of the Tweaks fork
   (`dbce-mods-outrun-2006`, profiles in `force-profiles.ini`), fed from the
   car state in the decomp's own structures instead of game-memory hooks;
   DirectInput effects on the wheel opened exclusively.
5. ⬜ **Telemetry**: Forza Horizon layout, on by default, HUD-matched (STD-009/016,
   toolkit `native/forza`); the Tweaks fork's shared memory as well if the SimHub
   plugin still needs it.
6. ⬜ **Tweaks parity** (table below) and the original game's limits the
   decomp can lift, e.g. steering quantised to -127..127 in 453860.
7. Upstream bugs, as pull requests: #2 30-second hang at the end of a race,
   #3 rival drift score stays at zero, #4 level impossible to win.

## Tweaks parity

By section of `OutRun2006Tweaks.ini`. ✅ done · 🟡 partial · ⬜ to do · ➖ not
needed in a native build.

| Section | Settings | Status | Notes |
| --- | --- | --- | --- |
| Performance | FramerateLimit, FramerateLimitMode, VSync | 🟡 | upstream: 60 / 120 fps row, presents at the monitor rate |
| | FramerateFastLoad, FramerateUnlockExperimental | ⬜ | |
| | SingleCoreAffinity | ➖ | a workaround for the original |
| Controls | SteeringDeadZone | ✅ | |
| | UseNewInput, ControllerHotPlug | 🟡 | gamepads via Windows.Gaming.Input (hot-plug); DirectInput devices re-acquire, new ones need a restart |
| | DefaultManualTransmission, HudToggleKey | ⬜ | |
| | Vibration\*, ImpulseVibration\* | ⬜ | gamepad rumble / trigger motors |
| DirectInput | device, axes, inverts, sensitivity, buttons, POV bindings | ✅ | canonical `[Controls]` (`ini-controls-1`); the Tweaks keys are imported once with Tweaks' scaling |
| | ThrottleDeviceGuid, BrakeDeviceGuid | ✅ | each binding names its own device instance |
| DirectInput.Calibration | Steering/Throttle/Brake Min/Center/Max | ⬜ | contract `range` / `rest` / `travel` cover pedal endpoints; steering calibration open |
| DirectInput.Shifter | sequential | ✅ | |
| | hpattern gears 1-6 | 🟡 | pulses toward the gate's gear (car +208); R: the game has no reverse |
| DirectInput.Aux | DeviceGuid, buttons | ✅ | any action may sit on any device (one binding per action) |
| Telemetry | Enable, SharedMemName | ⬜ | step 5 |
| FFB | all | ⬜ | step 4 |
| Graphics | resolution, AA, widescreen | ✅ | upstream rows (Options > Settings) |
| | UIScalingMode | 🟡 | windows/docs/UI-SCALING.md: 2 (keep centred, the centred 4:3) by default here; 0 stretches the 2D over the 16:9 frame, or over a span's centre panel only; 1 (Tweaks' Online Arcade) is partial (step 2a): the 2D mapping widens at the 4:3 scale to the frame's edges (or the centre panel's), and NaviPub_Disp's HUD is spaced to them with Tweaks' unspaced parts (rival labels / markers, hearts, LAN markers, and from the level / lap / countdown on). Step 2b adds time attack's forced sides, the put_scroll positions (time attack, rank, REV, heart number) and the ghost gap; the C2C bubbles remain. Unit-tested (test_triple_screens; the device path in test_d3d11_vertex_path); not yet seen in game |
| | UILetterboxing | ⬜ | |
| | AnisotropicFiltering, ReflectionResolution, TransparencySupersampling | ✅ | `[Graphics]`, off by default; anisotropy only on samplers linear in both min and mag (the HUD stays sharp); alpha-to-coverage only under MSAA |
| | DisableVehicleLODs, DisableStageCulling | ✅ | off by default. With all five options on (2026-10-12 A/B, n = 1 vs 2): GPU draws about 2.1x and 1 to 2.5 % of race frames miss vsync; not attributed per option |
| | ScreenEdgeCullFix | 🟡 | on by default as in Tweaks: the object test 4F0E40 (cones, course objects, gates) takes the widened widescreen bounds 4F02D0 (cars) already had; identical at 4:3; not yet seen in game |
| | DrawDistanceIncrease/Behind | 🟡 | off by default; Tweaks' 44DF6D merge (dedup, exclusions with the offset-0 exemption, Canyon limit) in 44DE10, a lods.ini read only, queues grown as DrawBufferExtension; unit-tested, not yet seen in game |
| | (renderer) D3D9 point sprites | 🟡 | not a Tweaks key: the port drew no POINTLIST at all, so sparks (source 3) and effect sources 8-10 were invisible; drawn per the D3D9 docs since b978928; unit-tested, visual check pending |
| | FixZBufferPrecision | 🟡 | off by default; Tweaks' near-plane rule on the main race camera (1.0 chase/bumper, 0.25 in-car, 0.2 360SP, 0.1 switching/outside GAME) and 0.05 for the 40BE70 passes (projection only: 484BD0's view part is stateful); unit-tested, not yet seen in game |
| | CarBaseShadowOpacity | 🟡 | 1.0 by default as in Tweaks: the race car's flat base shadow (CalcPeraShadow, nulled on the PC) restored at Tweaks' three sites (469EB4 race, 46AC76 / 46B766 car select) with the PC's own alpha; unit-tested |
| | UseHiDefCharacters | 🟡 | on by default as in Tweaks: 487EE0 / 487F50 (and the native 488090 reader) return MH00 for the 6549B0 entry 0 and the O2SP constant, LH00 / GH00 for the 6549C8 entries 3 / 4, and LH00 / GH00_USA for the O2SP constants; CHR_DR_GH00(_USA).bin from Media (it has the hair animations) when present, else Chr; runtime.log names it. Unit-tested (test_car_base_shadow); not yet seen in game. RestoreJPClarissa: see Misc |
| | SceneTextureReplacement, TextureBaseFolder | ✅ | off by default; the Tweaks pack layout and names (`<bank>/<index>_<XXH32>_<w>x<h>.dds`), the game's own create request; files the device's DDS reader would refuse are never offered (stock kept). A create the device still refuses falls back to stock in the loader, but the device's error latch ends the run at present, so a genuine allocation failure can remain terminal. In game (2026-10-12, owner pack copied, muted): 344 of 834 replaced at 60 fps, 0 device errors; runtime.log lists per bank what was replaced and which banks the pack has no folder for (that race's stage, `cs_CS_PALM_pmt`, is one: the pack has `cs_CS_PALM_T_pmt`). Cube maps with 2D pack dumps stay stock |
| | UI texture replacement, extract, cache | ⬜ | the pack's `spr_*_xst` folders |
| | UseNewTextureAllocator | ➖ | |
| Audio | AllowHorn, AllowWAV, AllowFLAC | ⬜ | |
| CDSwitcher, CDTracks | in-race track switching, track list | ⬜ | |
| Window | WindowedBorderless, hidden cursor, DPI | ✅ | borderless over the primary monitor |
| | WindowPositionX/Y, AutoDetectResolution | ⬜ | |
| Triple | Screens | ✅ | the planar span, as Tweaks: one wide Hor+ view over three equal monitors, HUD and menus centred; Off keeps an unstretched 16:9. Projected views (beyond Tweaks): a default-off spike, see step 3 |
| Misc | RestoreJPClarissa | 🟡 | off by default as in Tweaks: Tweaks' five patches, each at its decomp counterpart. 487F50 (and the native 488090 reader): the O2SP constants G00_USA 6 -> G00 5 (GH00 7 with hi-def characters) and, in mode 3, GH00_USA 8 -> GH00 7. Arcade ending: motion set 3 -> 2 where 4527F0's branches join (Tweaks' 4529A5 hook), 5A4618 entry 1 0xCF -> 0xCE (obj_chr_gal_pmt), and 4B5A80's force of kind 0xE to 0xF skipped (4B5AE6). Unit-tested (test_car_base_shadow); not yet seen in game |
| | DisableCountdownTimer | 🟡 | off by default as in Tweaks: the race clock's decrement flag (.data 637911, the PC's Sumo_CountdownTimerEnable; its only readers 450B03 / 450B4B are race_manager.cpp's two time_decrement_637911 tests) is 0 when on. No launch parameter (Tweaks' -OuttaTime). Not yet seen in game |
| | SkipIntroLogos | ➖ | the decomp already starts without the intro logos: the native runtime's 6319A1 (Sumo_IntroLogosEnable, 1 in the PC's .data) starts at 0, so Tweaks' SkipIntroLogos = true is its behaviour always; showing the logos is not built (read from the source, not checked in game) |
| | EnableLevelSelect, EnableHollyCourse2, ShowOutRunMilesOnMenu, AllowCharacterSelection, RandomHighwayAnimSets | ⬜ | |
| | DemonwareServerOverride, ProtectLoginData | ⬜ | online play; upstream has the LAN layer |
| Overlay, WheelSettings | in-game overlay, F6 wheel page | ⬜ | |
| Bugfixes | FixLensFlarePath, FixIncorrectShading | 🟡 | on by default as in Tweaks. FixLensFlarePath: 44A630's offsets read from `Media\lens_flare_offset.bin` (where the game ships it) when present, else `common\`; runtime.log names the source. FixIncorrectShading: 40E140's TWOSIDE lighting entries (73F04C / 73F050) link the DOUBLE fragments (61FB48 / 61FC88), unit-tested in test_pc_shader_programs. Neither yet seen in game |
| | FixPegasusClopping, FixRightSideBunkiAnimations, FixC2CRankings, PreventDESTSaveCorruption, FixParticleRendering, FixFullPedalChecks, HideOnlineSigninText | ⬜ | the reconstruction reproduces the original bugs; fix each in source behind its setting. FixParticleRendering (Tweaks default on) = two parts: draw_418cf0's screen mapping uses the full width/height where the Xbox code had half (grass off screen; Tweaks halves sx, sy and the size at 419009), and the Metropolis fireworks (44DE10 object 0x630001 without a node list; Tweaks draws 0x1A000A with a fixed list) |
