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
   Wheelkit's closed-loop tests (with Astra). Ordinary-play hot-plug discovery
   is in source, awaiting a rig check. Next: Astra's review, a run at the rig,
   the closed-loop qualification (STD-034).
2a. 🟡 **F6 panel** (STD-030: the panel comes first): toolkit `native/panel` on
   our own D3D11 renderer, standard pages, each later feature adds its controls.
   - In code (`source/desktop_panel.cpp`): the Help, Controls (status), Force (strength, invert, output; F8 stops),
     Telemetry and Cameras (the triple selector and projection) pages.
   - All five pages were seen rendered in game, muted, in disposable copies (render evidence only):
     - Help: hub `artifacts/outrun-f6-help-20261012`.
     - Controls, FFB, Cameras, Telemetry on 06e5e19, Native backend: hub `artifacts/outrun-f6-pages-20261012`.
     - All five on ac68a6f, Shim backend with a pad, `[FFB] Output = wheel` without WheelFfb.dll: hub
       `artifacts/outrun-candidate-ac68a6f-20261013/regression/f6-ac68a6f-run1`. Controls reads "Shim (the shared
       input shim, [Input] Backend = Shim)" with the profile device live. FFB reads "Wheel output at the next
       start" On and, for This session, "not running: this process is force-muted (DBCE_FFB_MUTE)" (UI wording
       only).
   - Not checked: saving through the panel by keyboard or mouse (write_key is unit-tested), a three-screen
     span, the physical wheel. `docs/F6-PANEL.md`'s status lags the code.
3. 🟡 **Triple screens** (`windows/docs/TRIPLE-SCREENS.md`): `[Triple] Screens` Off / Surround / Separate monitors
   with the planar span (the Tweaks parity floor) is done and seen at the rig. True per-screen projection
   (STD-007/015/019/022) is a default-off spike (`OR2_TRIPLE_VIEWS=1`): one game render replicated by a geometry shader
   into three viewports, each with its clip correction from the toolkit rig math. First rig pictures 2026-10-12: three
   projections with the side sky. Open: culling for the union of the three frusta, the F6 selector, the owner's
   physical check.
4. 🟡 **Force feedback**: the force model of the Tweaks fork
   (`dbce-mods-outrun-2006`, profiles in `force-profiles.ini`), fed from the
   car state in the decomp's own structures instead of game-memory hooks;
   DirectInput effects on the wheel opened exclusively.
   - Built and tested offline:
     - the model;
     - the delivery, which loads `WheelFfb.dll` from the game folder only with `[FFB] Output = wheel`, outside test
       runs and without `DBCE_FFB_MUTE`;
     - the shim's WheelFfb read-slot bridge.
     The tests are test_ffb_outrun, test_ffb_delivery, test_ffb_wiring and test_wheelffb_bridge. The muted
     integration passed (hub `artifacts/outrun-bridge-muted-20261012`).
   - Packaged: the force add-on carries WheelFfb.dll (hub `artifacts/outrun-ffb-addon-20261012`).
     - It was installed beside the owner's b08f143 on 2026-10-12, with the force off (no `[FFB] Output`), and
       re-pinned to the owner's ac68a6f on 2026-10-13 (the same DLL, still no `[FFB] Output`).
     - Its muted copy check logged the refusal under `DBCE_FFB_MUTE` and did not load the DLL.
   - F6 saves the wheel output for the next start (f67f196). It is in the owner's ac68a6f TEST install and was
     seen rendered muted, without the add-on (f6-ac68a6f-run1 above); saving it through the panel is not
     checked in game.
   - Not yet: the physical output on the wheel, and the feel against the art of rally reference (attended).
5. 🟡 **Telemetry**: Forza Horizon layout, on by default, HUD-matched (STD-009/016,
   toolkit `native/forza`), plus the Tweaks fork's shared memory for its SimHub plugin; see `README.md` Telemetry.
   - 2026-10-12: SimHub logged valid OutRun data on port 8000 during a shim run.
   - The owner confirmed reception on the wheel's screen through SimHub, forwarded to Pit House (hub
     `docs/2026-10-12-session-release-progress.md`, 20:16).
   - Per-field accuracy is not yet checked.
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
| | UseNewInput, ControllerHotPlug | 🟡 | gamepads via Windows.Gaming.Input (hot-plug); ordinary DirectInput rescans every two seconds and reopens the saved instance GUID, built but not checked at the rig |
| | DefaultManualTransmission, HudToggleKey | ⬜ | |
| | Vibration\*, ImpulseVibration\* | ⬜ | gamepad rumble / trigger motors |
| DirectInput | device, axes, inverts, sensitivity, buttons, POV bindings | ✅ | canonical `[Controls]` (`ini-controls-1`); the Tweaks keys are imported once with Tweaks' scaling |
| | ThrottleDeviceGuid, BrakeDeviceGuid | ✅ | each binding names its own device instance |
| DirectInput.Calibration | Steering/Throttle/Brake Min/Center/Max | ⬜ | contract `range` / `rest` / `travel` cover pedal endpoints; steering calibration open |
| DirectInput.Shifter | sequential | ✅ | |
| | hpattern gears 1-6 | 🟡 | pulses toward the gate's gear (car +208); R: the game has no reverse |
| DirectInput.Aux | DeviceGuid, buttons | ✅ | any action may sit on any device (one binding per action) |
| Telemetry | Enable, SharedMemName | 🟡 | step 5: `[Telemetry] Enabled`, `SharedMemName`, `Layout`; reception confirmed, per-field accuracy open |
| FFB | all | 🟡 | step 4: built and tested offline; no owner package ships WheelFfb.dll yet |
| Graphics | resolution, AA, widescreen | ✅ | upstream rows (Options > Settings) |
| | UIScalingMode | 🟡 | windows/docs/UI-SCALING.md: 2 (keep centred, the centred 4:3) by default here; 0 stretches the 2D over the 16:9 frame, or over a span's centre panel only; 1 (Tweaks' Online Arcade) is partial (step 2a): the 2D mapping widens at the 4:3 scale to the frame's edges (or the centre panel's), and NaviPub_Disp's HUD is spaced to them with Tweaks' unspaced parts (rival labels / markers, hearts, LAN markers, and from the level / lap / countdown on). Step 2b adds time attack's forced sides, the put_scroll positions (time attack, rank, REV, heart number) and the ghost gap; the C2C bubbles remain. Unit-tested (test_triple_screens; the device path in test_d3d11_vertex_path). Seen in game on the race route only (UI-SCALING.md, Step 4): the HUD moves ±240 px at 2560x1440 with no clipping. A start-camera "1P" banner drawn past the 4:3 edge appears in mode 1 only. Time attack and C2C have not been seen |
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
| | UITextureReplacement | 🟡 | off by default, as SceneTextureReplacement here (Tweaks' INI has both on); same folder and index as the scene textures. The sprite banks' XST textures under `<XST stem>/` (`spr_etc_xst`, `spr_sprani_game_cvt_Exst`), named as Tweaks dumps them: `<index>_<XXH32>_<w>x<h>.dds` (index = the textures the bank created before it, Tweaks' CurrentTextureIdx), then `<XXH32>_<w>x<h>.dds`; Tweaks' FXT hash remaps. A replaced texture keeps the stock size as the texel space of the XST image tables and SPRANI frames: footage and 42C0F0 UVs over the stock size, ID3DXSprite source rectangles moved onto the replacement with the scale reduced by the same ratio, so positions, centring and alpha stay as stock (unit-tested: 2x, 3x, unequal 3x/2x, flipped, rotated). Offline against an owner Tweaks pack (read only, the retail check of test_pc_sprite_replacement): all 257 UI files matched, 0 kept stock. In game (2026-10-12, muted A/B, owner pack's UI folders copied, 2560x1440): 160 of 176 sprite textures looked up on the menu-to-race path replaced, 0 kept stock; menu and HUD closeups match stock positions, sizes, centring and alpha with sharper art; race 60.0 fps in both, max frame 20.2 ms vs 21.6 ms stock. Costs: the front-end bank's pack work runs on the main thread when the bank loads (1.6-1.9 s with a warm file cache, 8.6 s cold offline), and private memory rises 0.83 -> 4.23 GB, mostly because the device keeps a DDS and an RGBA8 copy of every texture besides the RGBA8 GPU texture. That pack's prompt atlas shows gamepad buttons where stock shows keys (pack content; Tweaks uses the same base file without a pad type). Not built: UITextureExtract, Tweaks' pad-type prompt folders. The START loading picture (bank 0x2F) is drawn by the preview renderer and stays stock |
| | (scene and UI) pixel formats | ✅ | A8B8G8R8 / X8B8G8R8 pack files are read as A8R8G8B8 (R and B swapped), as Tweaks' loader converts them; about half of the UI files in the owner pack use it |
| | TextureMatchAnyFolder (not a Tweaks key) | 🟡 | off by default: a texture with no file under Tweaks' names takes a same-name file (XXH32 and size of the stock DDS by the pack's naming, any index prefix) from elsewhere in the pack when it is the only one or all such files are byte-identical; differing files are ambiguous and keep stock. Built for stage variants: Tweaks looks only in the bank's own folder and the root, so a pack made on cs_CS_PALM_T_pmt leaves cs_CS_PALM_pmt stock. Offline over every PMT bank against an owner Tweaks pack (tools/texture_pack_coverage.cpp, read only): Tweaks' names replace 1,277 of 14,395 textures; with this option 3,335 (cs_CS_PALM 0 -> 154 of 166, the _R / _T variants of the covered stages 118-154 each), 300 ambiguous kept stock. Unit-tested (test_pc_pmt_loader); not yet seen in game |
| | (scene) cube maps with 2D pack files | ➖ | the request is a cube, the file a 2D image: kept stock (16 in the owner pack) |
| | SceneTextureExtract, EnableTextureCache | ⬜ | |
| | Texture CPU copies (port memory, not a Tweaks key) | ✅ | `e167f58`: a file texture releases its decoded RGBA copy once the D3D11 device has uploaded it (UpdateSubresource copies before it returns), keeping its DDS levels; any CPU reader decodes a level again, and a CPU-written texture is retained for good (file textures cannot be locked, so that rule is defensive). `OR2_KEEP_TEXTURE_RGBA=1` keeps the old lifetime. Measured by deli (one exe, settled race, muted; 2026-10-12): HD UI private bytes 4,250 -> 3,711 MB (-539 MB, 371 textures released, 544.6 MB), stock 829 -> 791 MB; no timing difference visible. Only textures actually drawn are released. **Follow-up, deferred (sana 45486):** decode RGBA lazily at the first upload so never-drawn pack textures hold only their DDS levels; it moves the decode to first use and needs a first-draw hitch qualification. BC direct upload is a separate follow-up |
| | UseNewTextureAllocator | ➖ | |
| Audio | AllowHorn, AllowWAV, AllowFLAC | ⬜ | AllowHorn (Tweaks: SND 0x1BC looped while switch X is held in GAME) needs its own consumed action and recording channel: in 5A7B50[0] the device button behind switch X (0x8) is also shift-down, so it must not be reused. Deferred until the common shim session path carries such an action |
| CDSwitcher, CDTracks | in-race track switching, track list | ⬜ | |
| Window | WindowedBorderless, hidden cursor, DPI | ✅ | borderless over the primary monitor |
| | WindowPositionX/Y, AutoDetectResolution | ⬜ | |
| Triple | Screens | ✅ | the planar span, as Tweaks: one wide Hor+ view over three equal monitors, HUD and menus centred; Off keeps an unstretched 16:9. Projected views (beyond Tweaks): a default-off spike, see step 3 |
| Misc | RestoreJPClarissa | 🟡 | off by default as in Tweaks: Tweaks' five patches, each at its decomp counterpart. 487F50 (and the native 488090 reader): the O2SP constants G00_USA 6 -> G00 5 (GH00 7 with hi-def characters) and, in mode 3, GH00_USA 8 -> GH00 7. Arcade ending: motion set 3 -> 2 where 4527F0's branches join (Tweaks' 4529A5 hook), 5A4618 entry 1 0xCF -> 0xCE (obj_chr_gal_pmt), and 4B5A80's force of kind 0xE to 0xF skipped (4B5AE6). Unit-tested (test_car_base_shadow); not yet seen in game |
| | DisableCountdownTimer | 🟡 | off by default as in Tweaks: the race clock's decrement flag (.data 637911, the PC's Sumo_CountdownTimerEnable; its only readers 450B03 / 450B4B are race_manager.cpp's two time_decrement_637911 tests) is 0 when on. No launch parameter (Tweaks' -OuttaTime). Not yet seen in game |
| | SkipIntroLogos | ➖ | the decomp already starts without the intro logos: the native runtime's 6319A1 (Sumo_IntroLogosEnable, 1 in the PC's .data) starts at 0, so Tweaks' SkipIntroLogos = true is its behaviour always; showing the logos is not built (read from the source, not checked in game) |
| | ShowOutRunMilesOnMenu | 🟡 | on by default as in Tweaks: 441370's not signed in status (4413E2: string 0x474, "Not Signed In") reads "Not Signed In - OutRun Miles: N", Tweaks' "%s - %s %d" with string 0x21D and the active license's Miles (+24 of 7C23E0, the float the showroom reads) truncated to an int (pc_screen.hpp, native_runtime.cpp native_state2_ui_services); the signed in branch is unchanged; `[Misc] ShowOutRunMilesOnMenu = false` restores the plain status; tests/test_signin_text.cpp. Not yet seen in game |
| | EnableLevelSelect, EnableHollyCourse2, AllowCharacterSelection, RandomHighwayAnimSets | ⬜ | |
| | DemonwareServerOverride, ProtectLoginData | ⬜ | online play; upstream has the LAN layer |
| Overlay, WheelSettings | in-game overlay, F6 wheel page | 🟡 | the F6 panel (docs/F6-PANEL.md): Controls, FFB (strength, effects, Stop FFB, the wheel output at the next start), Cameras, Telemetry, Help; Tweaks' own overlay windows are not reproduced |
| Bugfixes | FixLensFlarePath, FixIncorrectShading | 🟡 | on by default as in Tweaks. FixLensFlarePath: 44A630's offsets read from `Media\lens_flare_offset.bin` (where the game ships it) when present, else `common\`; runtime.log names the source. FixIncorrectShading: 40E140's TWOSIDE lighting entries (73F04C / 73F050) link the DOUBLE fragments (61FB48 / 61FC88), unit-tested in test_pc_shader_programs. Neither yet seen in game |
| | FixFullPedalChecks | ✅ | on by default as in Tweaks: 453720 / 453750 read accelerator and brake 250..254 as 255 (race_input.hpp), for the full-press checks such as the backfire; the stored channels, edges and consumed capture are unchanged; `[Bugfixes] FixFullPedalChecks = false` restores the PC's own; tests/test_pedal_volume.cpp |
| | FixPegasusClopping | ✅ | on by default as in Tweaks: the Pegasus event 44BB30 starts the clop loop (SND_LOOP \| 0x8D) and SndOff_PEGA 44BCC0 stopped only 0x8C; 44BCC0 now also requests SND_STOP \| 0x8D (pc_screen.hpp, race_area.cpp); `[Bugfixes] FixPegasusClopping = false` restores the single stop; tests/test_pegasus_sound.cpp. Not yet heard at the rig |
| | PreventDESTSaveCorruption | ➖ | not reachable here (bounded path audit, 2026-10-13): the game-side DirectInput is pc_input_devices.cpp's fake one, which enumerates one pad and the keyboard (create_403de0), so at most 2 device records of 0xB0 are written from 7C211C, below the 0x2C4 before the 7C23E0 license; the real devices never reach that enumeration, and native_pc_input_create_403de0's created guard keeps a repeated init from adding records |
| | HideOnlineSigninText | 🟡 | off by default as in Tweaks' INI (its b3c1825; its code default is still true). Tweaks' five patches at their decomp counterparts: 443256's jump, so 443110 skips the 4400D3 sign-in box's open (44327C) and commit (443286), and 441370's status text returns at its first test, as Tweaks' RET there does (44327C is the only open of +C48); the 446510 caption and the 446A80 / 446B20 icon of a help-bar slot whose caption is 664 (SIGN-IN) are left out (frontend_slot_captions_446a50, the r081 slot open's skip_caption). On, it also hides ShowOutRunMilesOnMenu's line, as in Tweaks. tests/test_signin_text.cpp and test_driving's r081 checks. Not yet seen in game |
| | FixRightSideBunkiAnimations, FixC2CRankings | ⬜ | the reconstruction reproduces the original bugs; fix each in source behind its setting |
| | FixParticleRendering | 🟡 | in source since b8c9f8b, on by default as in Tweaks (pc_screen.hpp): draw_418cf0's screen mapping used the full width/height where the Xbox code had half (grass off screen; race_particles_draw.cpp halves sx, sy and the size at 419009 as Tweaks does), and the Metropolis fireworks (44DE10 object 0x630001 had no node list; race_area.cpp draws 0x1A000A with a fixed list as Tweaks does). Not yet seen at the rig |
