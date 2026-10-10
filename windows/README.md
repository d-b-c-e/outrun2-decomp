# Windows desktop build

`OutRunDesktop.exe`: the Xbox port's PC configuration (Direct3D 11, XAudio2,
Windows.Gaming.Input; `xbox/source`) as a desktop game, with wheels, pedals,
shifters and button boxes through DirectInput. Where this is going:
[ROADMAP.md](ROADMAP.md).

## Build

Same toolchain and dependencies as the Xbox PC configuration (`xbox/README.md`):
clang-cl, `-DOR2_XBOX=ON`, zlib, Ogg/Vorbis and the minimal FFmpeg. Visual Studio
2022 Build Tools work as well as 2026 (generator `"Visual Studio 17 2022"`).

```powershell
cmake -S . -B build-xbox-pc -G "Visual Studio 17 2022" -A x64 -T ClangCL -DOR2_XBOX=ON `
  "-DZLIB_INCLUDE_DIR=<deps>/zlib/include" "-DZLIB_LIBRARY=<deps>/zlib/lib/zlibstatic.lib" `
  "-DOR2_XBOX_VORBIS_ROOT=<deps>/vorbis" "-DOR2_XBOX_FFMPEG_ROOT=<deps>/ffmpeg"
cmake --build build-xbox-pc --config Release --target OutRunDesktop
```

Output: `build-xbox-pc\windows\Release\OutRunDesktop.exe`.

## Install and run

Copy `OutRunDesktop.exe` into the game folder (beside `OR2006C2C.EXE`: Steam, or a
retail copy with the OutRun2006Tweaks EXE) and start it from there. The folder
must be writable: the port keeps `OR2006C2C.cache`, `options.ini`, `runtime.log`
and the saves in it.

- A borderless window over the primary monitor; `--windowed` (or
  `[Window] WindowedBorderless = false`) opens a 1280x720 window instead.
- Gamepads work as on the Xbox (A / Menu confirm, LB+RB+Menu exits).

## Wheel settings

`OutRunDesktop.ini` beside the executable, written with comments on the first
run. Its sections and keys are those of `OutRun2006Tweaks.ini`, so the
`[DirectInput]`, `[DirectInput.Shifter]` and `[DirectInput.Aux]` sections of a
Tweaks install can be pasted in unchanged; without `OutRunDesktop.ini`, a
`OutRun2006Tweaks.ini` in the folder is read instead.

| Key | Meaning |
| --- | --- |
| `[DirectInput] UseDirectInputRemap` | wheel input on (default true) |
| `DeviceGuid` | `auto` (the force-feedback controller with the most axes, gamepads skipped) or a GUID |
| `SteeringAxis`, `AccelerationAxis`, `BrakeAxis` | 0=X 1=Y 2=Z 3=Rx 4=Ry 5=Rz 6=Slider0 7=Slider1 |
| `...Invert`, `SteeringSensitivity` | as in Tweaks (pedals: raw / 65535, inverted when set) |
| `ThrottleDeviceGuid`, `BrakeDeviceGuid` | separate pedal devices (empty: the wheel) |
| `ButtonA` .. `ButtonSelRight` | 0..127, or 128 + hat*4 + direction (0 up, 1 right, 2 down, 3 left); -1 unbound |
| `[DirectInput.Shifter] DeviceGuid`, `GearMode` | a shifter on its own device; `sequential` uses `ButtonGearUp/Down` |
| `[DirectInput.Aux] DeviceGuid`, `Button*` | a button box or second rim, ORed with the wheel's buttons |
| `[Controls] SteeringDeadZone` | 0..0.9 |

`runtime.log` lists every controller (GUID, name, type, axes, buttons, force
feedback) and the device bound to each role; for the first two minutes it also
logs every button pressed and the wheel's axes when they move, to find the
numbers above.

In the race the wheel feeds the device record of 453BB0 directly: the
full-range steering word (index 0x0E), the 0..255 accelerator (0x0D) and brake
(0x0C), shift up / down and view, as joystick configuration 5A7B50[0] reads
them. The original game then applies its own small dead zone and quantises the
steering to -127..127.

Not yet: H-pattern shifting (use the paddles), calibration, force feedback,
telemetry, multiple screens (see the roadmap).
