# Opt-in native gamepad vibration

This separate successor preserves qualified `bfaba433`. It adds ordinary body
rumble and trigger commands to the gamepad already selected by the existing WGI
input reader. Both modes default Off; the wheel FFB model, delivery and tuning
are unchanged. No second input reader, XInput index discovery, HID output or new
worker is introduced.

```ini
[Controls]
VibrationMode = 0
VibrationStrength = 7
VibrationControllerId = 0
ImpulseVibrationMode = 0
ImpulseVibrationLeftMultiplier = 0.25
ImpulseVibrationRightMultiplier = 0.25
```

Modes are 0 Off, 1 Normal, 2 Swap left/right, 3 Merge using the larger channel.
Strength is an integer 0..10. Trigger gains are finite numbers 0..1. Body mapping
precedes trigger mapping, then each trigger gain applies. Trigger output requires
body vibration enabled; zero strength silences all motors. Strict invalid settings
disable output. F6 Controls offers next-start gamepad mode, with strength, trigger
mode and gains under Advanced, through the existing one-key writer/failed-save
rollback. Its separate status reports current output eligibility.

Controller ID 0 means the native input reader's selected pad. Arbitrary indices
1..4 are unsupported and refuse output: WGI selection respects the existing input
profile's exclusion/ownership rule. This is an explicit adaptation of Tweaks'
index setting, not a claim that WGI order is an XInput user index.

The existing backend can command four fields through
[WGI GamepadVibration](https://learn.microsoft.com/en-us/uwp/api/windows.gaming.input.gamepadvibration?view=winrt-26100).
It cannot prove independent physical trigger motors; Microsoft documents that a
driver may approximate channels on a device with fewer motors. This source adds
separate left/right trigger commands, without claiming a particular device supports
them independently. No controller is opened or exercised during qualification.

## Calculation and lifecycle

The PC port has no original Xbox pad-output routine. The portable calculation
adapts Tweaks' Xbox-derived `CalcVibrationValues` (Xbox 0x114C60) from
`src/hooks_forcefeedback.cpp`, inspected in the local Tweaks fork at `51acc201`.
It reads the existing local car's speed/delta, gear change, native flags, road-edge
distances, surface/slip and collision state. It reuses the existing surface LUT;
the optional transient-state correction stays local and never writes car bytes.
Collision-peer speed is used only for an active, bounded native peer car.
Unknown peers are not synthesized. It retains source envelope limits, pulse holds
and the low-speed timeout, with finite/range guards. This is a portable adaptation,
not a bit-identical Xbox machine-code or physical-feel qualification.

The existing input filter supplies the same owned native state on game ticks.
Only active mode-16 gameplay with the local car, no pause, exact foreground game,
closed F6 and the selected pad can deliver. Menu/loading/results, pause, F6,
focus loss, disconnect/selection change and exit reset/zero output. Tests,
recording, playback and DBCE_FFB_MUTE suppress it. The HWND focus-loss/close paths
and exception-safe game-return cleanup also stop the held effect.

Retargeting zeros the previous actual writer before handover. Failed positive
writes attempt zero and latch; a failed zero prevents target handover. The desktop
consumer stops for the process after a write failure. Zero cleanup can retry;
it cannot resume positive output silently. An unused/default-Off target receives
no output API call at all. Stop delivery is best effort if a driver rejects it;
there is no new watchdog or physical stop acceptance claim.

Affected qualification passes at `18582ba4`: cached ClangCL Release desktop and
the new target compile; only `gamepad_vibration` runs, passing 706 checks (1/1
CTest, zero skipped). It covers settings, native envelope cases, output mapping,
gate closures, retargeting, disconnect and driver failures. It performs no
controller enumeration, input read, physical output or game launch. Prior passing
presentation/audio suites remain retained. Rendered F6 saving, hardware routing,
body/trigger feel and physical stop behavior remain unqualified.

Original evidence is hub artifacts/outrun-gamepad-vibration-20261017/build-01.
The desktop is 24,015,360 bytes, SHA-256
`427A35E26F7D9FE6D49C27D29D2A00DA501AB1B756F4F15917BD9E8F870A6E05`.
Compiler parallelism was 1; no allocation failure occurred. Owned children closed,
process query was safe, environment restored and exact lease released/literally
absent at 2026-10-17T03:40:11.1581544Z. Hazu received the direct actual safe handoff
before documentation. Canonical source and the owner installation are unchanged.
