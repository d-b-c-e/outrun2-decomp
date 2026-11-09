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
narrow hooks in shared files, so upstream drops rebase cleanly.

## Tweaks parity checklist

By section of `OutRun2006Tweaks.ini`. Status is one of three values:

- **done** — built and seen or heard in the game.
- **built** — in source and unit-tested, but not yet confirmed in a live game.
- **missing** — not built; "(native)" means the native port already behaves the
  way Tweaks' patch aims for, so nothing needs rebuilding.

Defaults match Tweaks unless a row says otherwise. The *owner check* column is
what a human still has to confirm for a **built** row.

| Feature | Status | Owner check |
| --- | --- | --- |
| **Performance** | | |
| FramerateLimit, FramerateLimitMode, VSync | built | pacing/scanout vs tearing and VRR |
| FramerateFastLoad | built | load-time reduction in a real game |
| FramerateUnlockExperimental | built | smoothness/pacing at the unlock cap |
| SingleCoreAffinity | missing | (native: no affinity workaround needed) |
| **Controls** | | |
| SteeringDeadZone | done | |
| UseNewInput, ControllerHotPlug | built | wheel hot-plug discovery at the rig |
| DefaultManualTransmission | built | Manual stays over restarts (off by default as Tweaks) |
| HudToggleKey | done | |
| Vibration\*, ImpulseVibration\* | built | gamepad rumble/trigger on a physical pad |
| **DirectInput** | | |
| device, axes, inverts, sensitivity, buttons, POV | done | |
| ThrottleDeviceGuid, BrakeDeviceGuid | done | |
| Calibration: steering/pedal min/center/max | built | one-time Tweaks import and steering endpoints |
| Shifter: sequential | done | |
| Shifter: h-pattern gears 1-6 | built | gate-to-gear shifting on the shifter |
| Aux: DeviceGuid, buttons | done | |
| **Telemetry** | | |
| Enable, SharedMemName, Layout | built | per-field accuracy against the HUD and receiver |
| **FFB** | | |
| force model and gated wheel output | built | physical force feel, onset, polarity and stop |
| **Graphics** | | |
| resolution, AA, widescreen | done | default 16:9; Options > Settings aspect rows |
| UIScalingMode | built | modes 1/2 spacing, time attack and C2C (default 2, not Tweaks' 1: mode 1 is partial) |
| UILetterboxing | built | bars between modes 0/1/2 and on a span |
| AnisotropicFiltering, ReflectionResolution, TransparencySupersampling | done | |
| DisableVehicleLODs, DisableStageCulling | done | |
| ScreenEdgeCullFix | built | widescreen edge visibility |
| DrawDistanceIncrease, DrawDistanceBehind | built | draw distance from `lods.ini` |
| D3D9 point sprites (`POINTLIST`) | built | sparks and effect sources visible |
| FixZBufferPrecision | built | near-plane quality |
| CarBaseShadowOpacity | built | car base shadow at Tweaks' sites |
| UseHiDefCharacters | built | hi-def characters and hair animations |
| SceneTextureReplacement, TextureBaseFolder | done | |
| UITextureReplacement | built | UI pack sharp, prompt-folder precedence |
| (scene and UI) pixel formats | done | |
| TextureMatchAnyFolder (not a Tweaks key) | built | stage variants covered by a same-name pack file |
| SceneTextureExtract | built | extracted DDS on a real game (off by default) |
| EnableTextureCache | built | cold UI stall and memory (default off, not Tweaks' on/1280 MiB) |
| Texture CPU copies | done | |
| UseNewTextureAllocator | missing | (native) |
| **Audio** | | |
| AllowHorn | built | horn sounds in the race |
| AllowWAV, AllowFLAC | built | replaced tracks are audible (default on as Tweaks) |
| AllowFLACLoopPoints (not a Tweaks key) | built | FLAC loop points (off by default) |
| CDSwitcher, CDTracks | built | in-race track switching and titles |
| **Window** | | |
| WindowedBorderless, DPI | done | |
| WindowedHideMouseCursor | built | cursor stays hidden (default on) |
| AutoDetectResolution | done | |
| WindowPositionX/Y | built | chosen monitor fills correctly (`MonitorDevicePath`) |
| **Triple** | | |
| Screens (Off/Surround/Separate) | done | |
| **Misc** | | |
| RestoreJPClarissa | built | Japanese Clarissa and arcade ending (off by default) |
| DisableCountdownTimer | built | race clock stands still (off by default) |
| SkipIntroLogos | missing | (native: intro logos are already skipped) |
| ShowOutRunMilesOnMenu | done | |
| AllowCharacterSelection | built | character grid reachable (off by default) |
| EnableHollyCourse2 | built | Holly's MIX 2 tile unlocks (default 3 as Tweaks) |
| EnableLevelSelect | built | level select on start (off by default) |
| RandomHighwayAnimSets | built | random highway animations (off by default) |
| RandomHighwayAnimationRows (not a Tweaks key) | built | row-only variant (off by default) |
| ProtectLoginData | built | encrypted login save (off by default) |
| DemonwareServerOverride | built | server/DNS override consumed |
| **Overlay, WheelSettings** | | |
| in-game overlay, F6 wheel page | built | F6 panel pages; Tweaks' own overlay windows are not reproduced |
| **Bugfixes** | | |
| FixLensFlarePath, FixIncorrectShading | built | lens flare and shading (default on) |
| FixFullPedalChecks | done | |
| FixPegasusClopping | done | |
| PreventDESTSaveCorruption | missing | (native: the fake DirectInput path cannot reach it) |
| HideOnlineSigninText | done | |
| FixC2CRankings | missing | (native: always the present path) |
| FixRightSideBunkiAnimations | built | right-side bunki animation (default on) |
| FixParticleRendering | built | grass and particle rendering (default on) |