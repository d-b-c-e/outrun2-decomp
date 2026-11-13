# Windows desktop build

`OutRunDesktop.exe`: the Xbox port's PC configuration (Direct3D 11, XAudio2,
Windows.Gaming.Input; `xbox/source`) as a desktop game. Wheels, pedals,
shifters, the F6 panel and telemetry come from the Wheelkit runtime,
`wheelkit.dll` beside the program. Where this is going: [ROADMAP.md](ROADMAP.md).

## Build

Same toolchain and dependencies as the Xbox PC configuration (`xbox/README.md`):
clang-cl, `-DOR2_XBOX=ON`, zlib, Ogg/Vorbis and the minimal FFmpeg. Visual Studio
2022 Build Tools work as well as 2026 (generator `"Visual Studio 17 2022"`).
`WHEELKIT_SDK` names a checkout of the Wheelkit repository whose runtime is built
(`pwsh build.ps1 -Only runtime/native/sdk` there): the build compiles against its
`runtime/native/sdk/wheelkit.h` and copies `build/x64/wheelkit.dll` beside the
program. Nothing of the runtime is committed here.

```powershell
cmake -S . -B build-xbox-pc -G "Visual Studio 17 2022" -A x64 -T ClangCL -DOR2_XBOX=ON `
  "-DZLIB_INCLUDE_DIR=<deps>/zlib/include" "-DZLIB_LIBRARY=<deps>/zlib/lib/zlibstatic.lib" `
  "-DOR2_XBOX_VORBIS_ROOT=<deps>/vorbis" "-DOR2_XBOX_FFMPEG_ROOT=<deps>/ffmpeg" `
  "-DWHEELKIT_SDK=<wheelkit checkout>"
cmake --build build-xbox-pc --config Release --target OutRunDesktop
```

Output: `build-xbox-pc\windows\Release\OutRunDesktop.exe` and `wheelkit.dll`.

## Install and run

Copy `OutRunDesktop.exe` and `wheelkit.dll` into the game folder (beside
`OR2006C2C.EXE`: Steam, or a retail copy with the OutRun2006Tweaks EXE) and start
it from there, or start it from anywhere with `--data=<game folder>`. The game
folder must be writable: the port keeps `OR2006C2C.cache`, `options.ini` and the
saves in it; `OutRunDesktop.ini` and `runtime.log` are beside the program.
Without `wheelkit.dll` the game runs with the keyboard and gamepad and says so in
`runtime.log`.

- A borderless window over the primary monitor; `--windowed` (or
  `[Window] WindowedBorderless = false`) opens a 1280x720 window instead.
- The borderless client area hides the mouse cursor by default
  (`[Window] WindowedHideMouseCursor`).
- Gamepads work as on the Xbox (A / Menu confirm, LB+RB+Menu exits).

## The Wheelkit runtime (`source/wheelkit_runtime.cpp`)

The loader and the call sites; everything else is the runtime's (Wheelkit
repository, `runtime/native/sdk/README.md`).

- **Input.** `[Controls]` in `OutRunDesktop.ini` is the Wheelkit profile (Wheelkit
  writes it). The runtime reads the devices and hands the game a snapshot each
  input tick; the game maps it as before: the full-range steering word (index
  0x0E), the 0..255 accelerator (0x0D) and brake (0x0C), shift up / down, view,
  start, the menu edges and features, the horn, and the camera actions.
  `[Wheel] SteeringSensitivity` and `SteeringDeadZone` shape the steering. With
  `Transmission = H-pattern` the gearbox (`source/hpattern.hpp`) pulses shift up
  / down toward the held gate; OutRun has gears 1-6 only. The keyboard and pad
  keep their own paths; the last device used drives the race.
- **F6.** The runtime's panel, drawn on the game's own Direct3D 11 device in the
  present overlay, with the pages in `source/wheelkit_panel.inc`; each row the
  game owns saves one key of `OutRunDesktop.ini` and applies at the next start.
  While it is open the game gets a neutral input sample.
- **Telemetry.** The runtime sends the Forza Horizon packet to
  `127.0.0.1:[Telemetry] Port` (8000); `[Telemetry] Enabled` (written by
  Wheelkit from My rig's Telemetry; no F6 row) turns it off. The
  game fills the car state each tick: the HUD speed (car +0x1F8 km/h), the gear
  (+0x208), the engine speed (synthesised from speed and gear, or with
  `[Telemetry] EngineRpm = Game` the HUD tachometer +0x20C with the car's idle and
  redline), and in GAME the consumed steering, accelerator and brake (7D6810).
  Field provenance: [docs/TELEMETRY-FIELDS.md](docs/TELEMETRY-FIELDS.md).
- **Triple screens.** The runtime decides which screens apply and where the
  window opens (`wk_triples_get`): `[Triple] Screens` when the player saved a
  choice (Off, Surround, Separate monitors), otherwise what the displays are, so
  three screens are used without being asked for. Menus, the HUD, movies and F6
  stay on the center screen. Three screens are always three projected views,
  with no setting (issue outrun-2006-0003: the one wide view the build used to
  default to is gone): the runtime's matrices (`wk_triples_views`) for the
  screens' measurements (`PanelWidthMm`, `PanelHeightMm`, `EyeDistanceMm`,
  `LeftYawDegrees`, `RightYawDegrees`, `BezelMm`, as Wheelkit writes them; unset
  keys take the size Windows reports, eye 660 mm, bezel 8 mm and sides facing
  the seat). A span whose measurements cannot be used gets one 16:9 view on the
  center screen (`source/triple_screens.hpp` `triple_draw`). The screens are
  flat in the views: `CurveRadiusMm` is not applied. How the views are drawn
  (one render replicated into three viewports, the culling and the stage
  sections) is this game's (`source/triple_screens.hpp`, `xbox/source`). A
  display chosen with `[Window] MonitorDevicePath` is used alone. The curve
  prototype (`source/curve_*.hpp`, `source/triple_rig.hpp`) has no switch any
  more and is left for removal with its renderer half.
- **Cameras.** The game's views stay the game's (bumper, cockpit, chase), and a
  Bonnet view is added to its cycle after the bumper
  (`source/camera_controls.cpp`). The runtime keeps, per view, the framing (Game
  or Realistic, F7, on Angled views) and the saved offsets (numpad + and - zoom
  by 2 degrees, 8/2 9/3 4/6 move by 1 cm per car, 5 resets) in `[Cameras]` and
  `[CameraPoses]`, and names the view in a toast (`wk_cameras_frame`). The tilt
  keys, the key rebinding and the Shift+F7 comparison are gone.
- **Force feedback** is the car's own steering load (`source/outrun_force.hpp`):
  DrivingControl's SideForce (5019C0) and friction circle (5026F0) leave each
  wheel's side force at wheel record +0xAC, its grip at +0xC0 and its sliding
  scale at +0xE4, in the player's body work 82E7F0 (wheel records of 0xF4 bytes
  at +0x258 front left, +0x34C front right, +0x440, +0x534). The front pair's
  side force over its grip, times 0.30 and faded in from 5 to 20 km/h, goes to
  `wk_ffb_set_force` each tick in GAME; the runtime owns the device, the
  player's on/off (F6, F8), Steering strength, the fence and the direction per
  wheel. The sign comes from the source (the header says how) and is checked on
  every drive; force is on by default. Every drive logs "ffb probe: ..." lines
  (every 30 s of driving and at the end of a race): the load's p50, p95 and
  peak above 12 km/h, and in grip corners how often the load is against the
  steering, which is the sign check: reversed over 300 such corners, the force
  stops for the run. The older centring spring from steering and speed
  ([docs/FFB-PRODUCER2.md](docs/FFB-PRODUCER2.md)) is not carried.
- **Graphics.** One option, Wheelkit's app option Graphics (`[Options]
  graphics`: off, low, medium, high (the default), ultra, custom), sets four
  Tweaks keys of `[Graphics]` for the run (`source/graphics_options.hpp`); custom
  keeps the file's own. Tweaks texture packs in `<game folder>/textures/load`
  load whenever present (the `spr_*` folders for menus and the HUD, the others
  for stages, cars and objects); a saved `SceneTextureReplacement` or
  `UITextureReplacement` decides where set. The runtime's log says how many
  pictures the pack has at the start and how many were replaced at the end.
- **Unattended tests** use the runtime's switches in
  `%LOCALAPPDATA%\dbce\outrun-2006\`: `ffb-fence.on`, and `inject.on` with
  `inject.txt` for test input; F6's dev channel is `panel-dev.txt` there.

## Research references

Decomp, recomp and reimplementation directories, saved at the owner's request (2026-10-17):
- [Unbaked agent collection](https://unbaked.games/collections/agents/), part of a broader index. For programmatic
  discovery use https://unbaked.games/api/games.json and https://unbaked.games/llms.txt. Its
  `outrun-2006-coast-2-coast` entry (matching decomp) points at this repository's upstream, `r4dius/outrun2-decomp`.
- [recomp.fyi](https://recomp.fyi/), a similar directory. On 2026-10-17 it had no OutRun 2006 entry.

Both are discovery leads: check any project claim at its upstream repository.
