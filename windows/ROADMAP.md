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
2. 🟡 **Wheel input** (this folder, first cut): DirectInput wheel, separate
   pedal devices, shifter and aux devices, Tweaks `[DirectInput]` keys. Next:
   test at the rig, H-pattern gears, calibration, hot-plug of devices added
   after launch.
3. ⬜ **Triple screens**: true per-screen projection. Per panel a yaw offset in
   `camera_view_482f20` and a matching off-centre frustum in
   `camera_perspective_404310` (it already builds `D3DXMatrixPerspectiveOffCenterRH`
   and the culling planes); the scene closure of `Presenter::set_pc_scene` drawn
   three times into three viewports; the HUD on the centre panel.
4. ⬜ **Force feedback**: the force model of the Tweaks fork
   (`dbce-mods-outrun-2006`, profiles in `force-profiles.ini`), fed from the
   car state in the decomp's own structures instead of game-memory hooks;
   DirectInput effects on the wheel opened exclusively.
5. ⬜ **Telemetry**: the Tweaks fork's shared-memory layout (`[Telemetry]
   SharedMemName`), so the existing SimHub plugin works unchanged.
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
| DirectInput | device, axes, inverts, sensitivity, buttons, POV bindings | ✅ | same keys and scaling as Tweaks |
| | ThrottleDeviceGuid, BrakeDeviceGuid | ✅ | by GUID (`...DeviceName` not read) |
| DirectInput.Calibration | Steering/Throttle/Brake Min/Center/Max | ⬜ | |
| DirectInput.Shifter | sequential | ✅ | |
| | hpattern gears 1-6, R | ⬜ | needs the current gear from the car state |
| DirectInput.Aux | DeviceGuid, buttons | ✅ | |
| Telemetry | Enable, SharedMemName | ⬜ | step 5 |
| FFB | all | ⬜ | step 4 |
| Graphics | resolution, AA, widescreen | ✅ | upstream rows (Options > Settings) |
| | UIScalingMode, UILetterboxing | ⬜ | |
| | AnisotropicFiltering, ReflectionResolution, TransparencySupersampling | ⬜ | renderer options |
| | DrawDistanceIncrease/Behind, DisableVehicleLODs, DisableStageCulling, ScreenEdgeCullFix | ⬜ | readable culling code now |
| | UseHiDefCharacters, CarBaseShadowOpacity, FixZBufferPrecision | ⬜ | |
| | Scene/UI texture replacement, extract, cache | ⬜ | keep the Tweaks pack layout |
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
