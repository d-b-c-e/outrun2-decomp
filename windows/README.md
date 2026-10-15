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
`modMenu` and `muteFfb` are not mapped yet: F6 opens the panel, and its Force page sets the output). At start
`runtime.log` gets the standard line
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
The ordinary wheel path rescans controllers when Windows reports a device change,
and every two seconds only while a bound control has no open, connected device
(a rescan stalls the frame it runs in; source/wheel_rescan.hpp). A configured
device connected after launch opens under its saved instance GUID; disconnects
release its handle and neutralize its actions. Live rig qualification is pending.

H-pattern: each gate holds its gear; the build pulses shift up / down, one every
other frame, until the car's gear (+208) matches, and stops after four pulses
without a change (top gear, automatic transmission).

**Input backend** (`[Input] Backend`, default `Native`):
- `Native` is the wheel path described above.
- `Shim` (in development; not yet the owner's default) reads the `[Controls]` profile through the shared input shim:
  the same Core, DirectInput provider and `ini-controls-1` profile Wheelkit writes for DRIVE's shim. The native wheel
  path is then not started, so the profile's devices are read by the shim alone.
- On either backend the gamepad and the desktop keyboard keep their own native paths: in the race the last device used
  drives, and Enter, Esc and the arrows work the menus.
- `runtime.log` gets the shim's start line (`controls: profile ... via shim-ini-controls-1: applied ...`), and the
  game folder gets `host-shutdown.json` at exit.

**Telemetry** (`[Telemetry]`, on by default; toolkit STD-016 / STD-009):
- A Forza "Data Out" packet to `127.0.0.1:Port` (default 8000) every game tick during START, GAME, the pause menu,
  GOAL and TIMEUP, and a parked packet (IsRaceOn 0, the same engine constants) at about 10 Hz in the menus. The
  layout is Forza Horizon's 324 bytes (SimHub: a Forza Horizon 5 game on that port); `Layout = FM7` sends the
  311-byte FM7 Dash of the OutRun 2006 mod instead.
- Speed is the HUD's (car +0x1F8 km/h; mph only changes the display). Gear 0 is sent as 1. RPM is synthesized from
  speed and gear as the mod does, since OutRun has none. In GAME, steering, accelerator and brake are the game's
  consumed 7D6810 channels, whichever device drives. START/pause/goal/time-up inputs are neutral. Forza's lateral
  acceleration stays zero (unavailable): the previously used car +0x264/+0x268 fields are road-edge distances.
- The mod's 76-byte shared-memory block (`OutRun2006TelemetryData`, version 1) under `SharedMemName`
  (`OutRun2006Telemetry`; `none` turns it off), for its SimHub plugin. The decomp has no pad rumble, so the two
  vibration fields stay 0. Version 1 preserves the mod's raw-offset bytes, including legacy names that are misleading:
  `steeringAngle` is speed delta, `lateralG1/2` are road-edge distances, and `impactForce` is prior speed. Consumers
  must not treat those three fields as physical steering, lateral acceleration or impacts. See [the field review](docs/TELEMETRY-FIELDS.md).
- `Enabled = false` (or the mod's `Enable = false`) turns it off. A latched test process never sends. `OR2_TELEMETRY=0`
  turns it off for unattended runs (`tests/perf/run_perf.py` sets it unless a run asks otherwise), and
  `OR2_TELEMETRY_PORT=N` sends to another port for a test's own listener. `runtime.log` names the layout and port at
  start, and the packet counts at exit.

Test injection (development only, for Wheelkit's closed-loop tests): see
`source/test_injection.hpp`. It arms only with a valid
`%LOCALAPPDATA%\dbce\outrun2006-decomp\inject.on` session and an applied
`[Controls]`, and this build sends no force.

In the race the wheel feeds the device record of 453BB0 directly: the
full-range steering word (index 0x0E), the 0..255 accelerator (0x0D) and brake
(0x0C), shift up / down and view, as joystick configuration 5A7B50[0] reads
them. The original game then applies its own small dead zone and quantises the
steering to -127..127.

Telemetry is described above. For the F6 panel, the triple screens and force feedback, see the roadmap (steps 2a, 3
and 4). Force feedback is built and tested offline. No owner package ships WheelFfb.dll yet, so no installed build
sends force. Standards adoption: [TOOLKIT-ADOPTION.md](TOOLKIT-ADOPTION.md); product
record for Wheelkit: [product.json](product.json).
