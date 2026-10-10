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
run. **`[Controls]` is the game's only binding store**, in the wheel toolkit's
`ini-controls-1` form (STD-033, `vendor/controls`): `Schema = 1`, `Profile`,
`Revision`, `Transmission` and one line per action, e.g.

```ini
steer = axis 0 dev={d71b8350-...} prod={0006346e-...} range=0..65535 rest=33265 travel=-1 name="MOZA R12 Base"
```

Wheelkit writes it when it applies a rig profile. Devices are matched by their
instance GUID only. Mapped actions: `steer`, `throttle`, `brake`, `shiftUp`,
`shiftDown`, `gear1`..`gear6` (with `Transmission = H-pattern`), `confirm`,
`back`, `start`, `navUp/Down/Left/Right`, `camera`. Others are reported as
unavailable with the reason (no clutch, handbrake, neutral or reverse in the game;
no F6 panel or force feedback yet). At start `runtime.log` gets the standard line
`controls: profile "<name>" revision <rev> via ini-controls-1: applied n (...); unsupported m (...); invalid k (...)`.

Without a `[Controls]` section, the OutRun2006Tweaks `[DirectInput]`,
`[DirectInput.Shifter]` and `[DirectInput.Aux]` keys (this file's, else the
folder's `OutRun2006Tweaks.ini`) are imported **once** at the first start with
their devices connected: a copy of the file is kept as
`OutRunDesktop.ini.pre-controls-import` and `[ControlsImport]` records it. Tweaks'
`ButtonX` / `ButtonY` have no contract action and are not imported. `[Wheel]
SteeringSensitivity` and `SteeringDeadZone` (0..0.9) shape the steering after the
binding.

`runtime.log` lists every controller (instance and product GUIDs, name, type,
axes, buttons, hats, force feedback); for the first two minutes it also logs
every button pressed and the axes moved on the opened devices.

H-pattern: each gate holds its gear; the build pulses shift up / down, one every
other frame, until the car's gear (+208) matches, and stops after four pulses
without a change (top gear, automatic transmission).

Test injection (development only, for Wheelkit's closed-loop tests): see
`source/test_injection.hpp`. It arms only with a valid
`%LOCALAPPDATA%\dbce\outrun2006-decomp\inject.on` session and an applied
`[Controls]`, and this build sends no force.

In the race the wheel feeds the device record of 453BB0 directly: the
full-range steering word (index 0x0E), the 0..255 accelerator (0x0D) and brake
(0x0C), shift up / down and view, as joystick configuration 5A7B50[0] reads
them. The original game then applies its own small dead zone and quantises the
steering to -127..127.

Not yet: force feedback, telemetry, the F6 panel, multiple screens (see the
roadmap). Standards adoption: [TOOLKIT-ADOPTION.md](TOOLKIT-ADOPTION.md); product
record for Wheelkit: [product.json](product.json).
