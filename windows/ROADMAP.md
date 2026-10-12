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
3. ⬜ **Triple screens**: true per-screen projection (triple toolkit math, STD-007/015/019/022). Per panel a yaw offset in
   `camera_view_482f20` and a matching off-centre frustum in
   `camera_perspective_404310` (it already builds `D3DXMatrixPerspectiveOffCenterRH`
   and the culling planes); the scene closure of `Presenter::set_pc_scene` drawn
   three times into three viewports; the HUD on the centre panel.
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
| | UIScalingMode, UILetterboxing | ⬜ | |
| | AnisotropicFiltering, ReflectionResolution, TransparencySupersampling | ✅ | `[Graphics]`, off by default; anisotropy only on samplers linear in both min and mag (the HUD stays sharp); alpha-to-coverage only under MSAA |
| | DisableVehicleLODs, DisableStageCulling | ✅ | off by default. With all five options on (2026-10-12 A/B, n = 1 vs 2): GPU draws about 2.1x and 1 to 2.5 % of race frames miss vsync; not attributed per option |
| | DrawDistanceIncrease/Behind, ScreenEdgeCullFix | ⬜ | readable culling code now |
| | FixZBufferPrecision | 🟡 | off by default; Tweaks' near-plane rule on the main race camera (1.0 chase/bumper, 0.25 in-car, 0.2 360SP, 0.1 switching/outside GAME) and 0.05 for the 40BE70 passes (projection only: 484BD0's view part is stateful); unit-tested, not yet seen in game |
| | UseHiDefCharacters, CarBaseShadowOpacity | ⬜ | |
| | SceneTextureReplacement, TextureBaseFolder | ✅ | off by default; the Tweaks pack layout and names (`<bank>/<index>_<XXH32>_<w>x<h>.dds`), the game's own create request; files the device's DDS reader would refuse are never offered (stock kept). A create the device still refuses falls back to stock in the loader, but the device's error latch ends the run at present, so a genuine allocation failure can remain terminal. In game (2026-10-12, owner pack copied, muted): 344 of 834 replaced at 60 fps, 0 device errors; runtime.log lists per bank what was replaced and which banks the pack has no folder for (that race's stage, `cs_CS_PALM_pmt`, is one: the pack has `cs_CS_PALM_T_pmt`). Cube maps with 2D pack dumps stay stock |
| | UI texture replacement, extract, cache | ⬜ | the pack's `spr_*_xst` folders |
| | UseNewTextureAllocator | ➖ | |
| Audio | AllowHorn, AllowWAV, AllowFLAC | ⬜ | |
| CDSwitcher, CDTracks | in-race track switching, track list | ⬜ | |
| Window | WindowedBorderless, hidden cursor, DPI | ✅ | borderless over the primary monitor |
| | WindowPositionX/Y, AutoDetectResolution | ⬜ | |
| Triple | Screens | ⬜ | step 3 |
| Misc | SkipIntroLogos, DisableCountdownTimer, EnableLevelSelect, EnableHollyCourse2, RestoreJPClarissa, ShowOutRunMilesOnMenu, AllowCharacterSelection, RandomHighwayAnimSets | ⬜ | |
| | DemonwareServerOverride, ProtectLoginData | ⬜ | online play; upstream has the LAN layer |
| Overlay, WheelSettings | in-game overlay, F6 wheel page | ⬜ | |
| Bugfixes | FixPegasusClopping, FixRightSideBunkiAnimations, FixC2CRankings, PreventDESTSaveCorruption, FixLensFlarePath, FixIncorrectShading, FixParticleRendering, FixFullPedalChecks, HideOnlineSigninText | ⬜ | the reconstruction reproduces the original bugs; fix each in source behind its setting |
