# F6 panel for the desktop build (plan, for review; no code yet)

Targets: STD-030 (the panel comes first), STD-004 (the standard shape: Simple / Advanced, the page names and order,
F8 stops FFB), STD-017 (empty pages hidden), STD-022 (the triple selector), STD-023 (frame-rate readout), and the
owner's ask for clean options consistent with F6 and Wheelkit. No new UI framework: the toolkit's own native panel.
Force output stays Off by default, and the panel probes no hardware.

## What exists

- **Toolkit** `native/panel` (Dear ImGui, vendored MIT):
  - `dbce_panel.h/.cpp`: the frame, the standard widgets and the pages in the standard order. The mod draws each
    page body; [Panel] View / Scale / Page are kept in the mod's ini. On a three-screen span the panel sits on the
    centre screen.
  - `dbce_panel_input.h/.cpp`: a thread-local WH_GETMESSAGE hook on the game window's thread. F6 toggles, F8 stops
    FFB, Esc closes. While open, keyboard, mouse and raw input go to the panel. No window-procedure change.
  - `dbce_frame_stats.hpp`: STD-023.
  - `dbce_overlay_d3d11.*`: a DXGI Present hook for injected mods. **Not used here**: the decomp owns its renderer.
- **The OutRun 2006 mod's panel** (`dbce-mods-outrun-2006` `src/overlay/wheel_settings.cpp`) is the option and label
  reference. Its pages are Setup, Controls, FFB, Cameras, Telemetry and Help. On the FFB page: Strength, then
  Centering, Damping, Cornering load, Grip loss, Weight transfer, Wall impact, Gear shift, Road texture, Tire slip,
  Engine idle, the lateral noise floor and Invert force, all on the mod's own [FFB] keys, which the decomp already reads
  (`ffb_settings`).
- **The decomp's keys** (the file Wheelkit reads, `OutRunDesktop.ini`):
  - [Controls] (Wheelkit's, STD-033), [Wheel] SteeringSensitivity / SteeringDeadZone;
  - [FFB] (the mod's names, plus Output = none | wheel);
  - [Telemetry] Enabled / Port / Layout;
  - [Window] FrameLatency / WindowedBorderless, [Graphics] / [Bugfixes] (the Tweaks keys);
  - [Triple] Screens (zero's triples step 1).

## Proposal (small reviewed commits; a first picture at the rig before polish, spike-first)

1. **Vendor and a first frame.**
   - Copy `native/panel` (panel, input, frame stats) and `native/third_party/imgui` (core plus `imgui_impl_dx11`)
     byte-exact into `windows/vendor/panel` and `windows/vendor/imgui`: pinned hashes in a README, `-text`.
   - `windows/source/desktop_panel.{hpp,cpp}`: its own ImGui context and DX11 backend. Each frame it runs the same
     sequence the toolkit overlay runs (Prepare / BuildFonts, NewFrame, DrainInput, Draw, Render), restoring the
     render targets. NoteFrame and PollDev run on every frame, open or closed.
   - The **draw slot** is zero's file: a function pointer in `D3D11Display::present`, called with device, context,
     back-buffer RTV and size just before the screenshot and Present. Null means nothing happens. Drawing before the
     screenshot lets a dev-channel screenshot show the panel.
   - AttachInput on the game window at the first frame.
   - Only the Help page: mod name and version, the UI scale (panel-owned), frame stats.
   - A muted visual check through the dev channel ("open help") and a WGC capture.
2. **Pages, each control saving at once to `OutRunDesktop.ini`** (one-key update through the existing ini writer;
   STD-017 hides pages without controls):
   - **Cameras**: the STD-022 selector (Off / Surround / Separate monitors), its status and "applies at next start";
     frame rate (STD-023); FrameLatency under Advanced.
   - **Telemetry**: On/Off, Layout (Horizon 324 default / FM7 311, the owner's open question), Port, status.
   - **FFB**: Strength and, under Advanced, the mod's sliders with its labels and keys, plus a status row (output
     off / waiting / open / released and why, from ffb_device).
     - The **Output** choice is not offered in v1 (physical output is owner-attended). F8 / Stop FFB is wired, since
       it only stops: it writes Output = none and releases the device.
     - Changes apply to the running model **only through a quiesce first** (romi 30317). ffb_configure resets its
       state by assignment, so a live re-read calls ffb_shutdown (PanicStop, FreeDirectInput, unload) and then
       configures again, never ffb_configure alone. v1 may simply say "applies at next start" for anything that
       would reopen the device.
   - **Controls**: status only (profile, revision, applied / unsupported, steering device connected), plus steering
     sensitivity and deadzone. Bindings stay Wheelkit's; the toolkit panel has no binding capture yet.
3. **Input while open** (UX-03): the desktop input filter neutralises the game's input sample (pad, keyboard, wheel
   buttons and axes) while `dbce::panel::IsOpen()`. The race keeps running, as in the mod.

## Rules

- **Latched (test) processes**: no saves (the fence: the game folder is read-only there); the panel opens through the
  dev channel only. A write-audit entry (latched-skip) for the save.
- **Hot reconfigure** of FFB: quiesce first (above). Of the triples: "next start" (zero's design).
- **Shim backend**: the force output needs the connected steering device's authoritative identity, and today only
  the Native backend provides it (`wheel_input_steer_device`). The FFB status row says so, and the gates keep
  waiting. An identity accessor for the shim is separate work (romi 30317).
- **STD-019**: drawn on the centre screen, as the toolkit already does on a span.

## Files

Mine:
- new `windows/source/desktop_panel.{hpp,cpp}`;
- `windows/vendor/panel`, `windows/vendor/imgui`;
- `windows/CMakeLists.txt` (sources);
- one configure call in `window_desktop.cpp` (outside zero's [Triple] block and the `native_window` body);
- the input neutralisation in `wheel_input.cpp`;
- `write_audit.json`.

Zero's: the draw slot in `xbox/source/d3d11_display.cpp`.

## Questions for review

1. The FFB Output choice: hidden in v1 (as proposed), or shown disabled with "owner-attended"?
2. Page 1 first, with a muted visual check before pages 2-3, or all pages in one review?
3. The mod has a Setup page (profile apply, device list): is it out of scope until Wheelkit's flow is settled?
