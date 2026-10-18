# Alpha behavior choices

This is the alpha-relevant reading map for the candidate based on `a187a947`,
not another branch audit or an inventory of every debug trace. Keep the defaults
below. Async shaders and the archived hitch/triple diagnostic branches are
deferred; they are not prerequisites for this alpha. Branch disposition and the
release gates are in [the alpha checklist](../../docs/ALPHA-CHECKLIST.md).

Evidence in these tables is **source**, unless explicitly stated otherwise.
The base's [consolidation fixtures](../../docs/CONSOLIDATION-VALIDATION.md) remain
historical evidence for that revision. The combined alpha still needs its own
build and menu -> driving -> exit/relaunch, settings/save and requested rendering
checks. Neither a settings message nor a formatter test proves in-game behavior.

## Startup environment report

Beside the startup settings lines, `runtime.log` now contains either
`diagnostics: none` or `diagnostics: N set: NAME=value; ...`.
The allowlist in [startup_diagnostics.cpp](../source/startup_diagnostics.cpp)
covers the 91 literal `OR2_*`/`DBCE_*` readers found in this base's Windows/shared
production source, including `.inc` files. Unknown names, including unrelated credential variables, are
ignored. This line records **presence**, not whether a value enables a feature:
many readers activate even for an empty value or `0`.

Names are sorted and deduplicated, at most 32 are shown, and the line is capped
at 2048 bytes with an explicit omitted count. Only exact `0`, `1`, `true`,
`false`, `on` and `off` values can be shown; path/identifier readers are always
redacted, other values are redacted, and empty values are labeled `<empty>`.
The collector copies at most 25 bytes per recognized value. It does not mutate
the environment or change any existing gate. Test/playback fencing still clears
its switches before this report, as before.

**Offline evidence:** `test_startup_diagnostics` passed 15 pure checks with
ClangCL `/std:c++17 /W4 /WX`: empty/unknown input, ordering, case/deduplication,
zero/empty presence, redaction, injection, count and total-length bounds, and
the included render-frame/network readers. The
desktop executable integration and actual startup line remain build/runtime
pending. A report with omitted names cannot establish the exact allowed set.
`none` establishes only that no recognized environment override was found; it
does not establish default INI settings or a stock-game qualification.

## Opt-in settings relevant to the alpha

Reader references below are in `windows/source` unless the full path is given.
“Logged” describes the existing effective-choice messages, not proof of behavior.

| Setting | Default | Reader (`file:line`) | Does runtime.log name the choice? | Evidence |
| --- | --- | --- | --- | --- |
| `[Triple] Screens`, `Views`, physical rig keys / `MatchGameFov` | Off, Planar; fixture rig with FOV matching until configured | `window_desktop.cpp:365`, `:375`, `:396` | Applied span/projection/rig or refusal; no applied span by default | Source; historical triple fixture, live angled traffic proof pending |
| `[Performance] FramerateUnlockExperimental`, `FramerateLimit` | false, 0; ordinary existing pacing retained | `window_desktop.cpp:446`; `src/enhancements/general_presentation.hpp:10` | Effective frame-rate output; uncapped experiment named at `xbox/source/main.cpp:339`; not every saved key echoed | Source |
| `[Performance] FramerateLimitMode`, `VSync` | 0, empty (preserve existing policy) | `window_desktop.cpp:453`; `src/enhancements/general_presentation.hpp:28` | Effective CPU cap/limiter/Present interval named at `xbox/source/d3d11_display.cpp:267`; invalid choices logged | Source |
| `[Performance] FramerateFastLoad` | 0; only 1/2 accepted, loading frames only | `window_desktop.cpp:456` | Enabled mode and refusal logged; runtime-unqualified label retained | Source |
| `[Misc] FineSteering` | false; PC's 255 steering steps | `window_desktop.cpp:518` | Both stock/fine choices named; latched exclusion named | Source; historical fine-steering fixture, physical acceptance separate |
| `[Graphics] TransparencySupersampling`, `DisableVehicleLODs`, `DisableStageCulling`, `FixZBufferPrecision` | All false | `window_desktop.cpp:465`, `:470`, `:625` | Each enabled choice named | Source |
| `[Graphics] AnisotropicFiltering`, `ReflectionResolution`, `DrawDistanceIncrease/Behind`, `CarBaseShadowOpacity` | 0, 128, 0/0, 1 | `window_desktop.cpp:427`, `:436`, `:479`, `:502` | Nondefault/refused choices named | Source |
| `[Graphics] SceneTextureReplacement`, `UITextureReplacement`, `TextureMatchAnyFolder`, `SceneTextureExtract`, `UITextureExtract`, `EnableTextureCache` | All false; base folder `textures` | `window_desktop.cpp:632` | Configuration/refusal/exclusion messages; cache and cross-folder choices logged when applicable | Source; pack/render acceptance pending |
| `[Misc] RandomHighwayAnimationRows`, `RandomHighwayAnimSets` | Both false; sets take precedence | `window_desktop.cpp:539`, `:546` | Each enabled variant names its version/initial state | Source |
| `[Misc] EnableLevelSelect`, `AllowCharacterSelection`, `RestoreJPClarissa`, `DefaultManualTransmission`, `DisableCountdownTimer`; `[Bugfixes] HideOnlineSigninText` | All false | `window_desktop.cpp:552`, `:560`, `:565`, `:574`, `:600`, `:602` | Enabled choices named | Source |
| `[Misc] HudToggleKey` | Empty, no toggle | `hud_toggle.cpp:31` | Configured/refused/excluded key named | Source |
| `[Audio] AllowWAV`, `AllowFLAC`, `AllowFLACLoopPoints` | All false; loop-point choice alone has no effect | `window_desktop.cpp:589` | Combined message only when WAV/FLAC replacement is enabled | Source; asset/runtime acceptance pending |
| `[CDSwitcher] SwitcherEnable`, `SwitcherShuffleTracks`, `SwitcherDisplayTitle` | All false; track/key/font values are suboptions | `cd_switcher.cpp:34`, `:44`, `:50` | Active switcher, title and refusal named; ignored suboptions need not log | Source |
| `[FFB] Output`, `Model`; `[FFBProducer2]` gains | none, `outrun-legacy@1`; producer model is opt-in | `ffb_settings.cpp:44`; `ffb_player_model.cpp:20`; `ffb_live.cpp:200` | Selected model/output or refusal; no dedicated default wheel-output line | Source; unattended runs must remain muted |
| `[Controls] VibrationMode`, `ImpulseVibrationMode` and their suboptions | Both 0; output off | `gamepad_vibration_live.cpp:57` | Off/configured status named, effective values not all echoed | Source; physical output pending |
| `[Misc] ProtectLoginData` | false | `login_protection.cpp:25` | Off/opt-in configured named; DPAPI runtime-unqualified label retained | Source; save/login acceptance pending |
| `[Misc] DemonwareServerOverride` | Empty; Windows online transport off | `window_desktop.cpp:680` | Configured/refused override named without needing to disclose its address | Source; online service not qualified |
| `[Telemetry] EngineRpm` | Synthetic; Game is opt-in | `telemetry_forza.cpp:30`; `telemetry_live.cpp:86` | Effective synthetic/game RPM named | Source; game field/physical consumer acceptance separate |
| `[Input] Backend` | Native; Shim remains opt-in | `window_desktop.cpp:659`; `shim_input.hpp:30` | Controls summary names the active backend | Source; binding persistence/runtime gate still required |

Intentional parity defaults are **not** newly enabled experiments: bundled
particle/pedal/shading/lens-flare/Pegasus/bunki fixes, high-definition characters
and menu miles are on (`window_desktop.cpp:508-570`); Holly course 2 defaults to
mode 3 (`:579`), UI scaling to 2 and letterboxing to 1 (`:608`, `:614`). Existing
messages often name only deviations. Demo explicitly retained these defaults.
The `OR2_SMOKE_TAIL` shortcut is also the established stall-fix path, not an
alpha experiment to disable.

## Environment overrides to exclude or declare in an alpha run

All recognized names below are now named by the S1 line when set (subject to its
explicit output cap); values may be redacted. They are not new user-facing
settings. The table groups changes that can invalidate a menu/driving/render or
timing comparison, rather than documenting every trace switch.

| Switch/group | Default when absent | Reader (`file:line`) | Does runtime.log name the choice? | Evidence |
| --- | --- | --- | --- | --- |
| `OR2_NOCOURSE`, `OR2_NO_OBJECTS`, `OR2_SHADOW_OFF`, `OR2_NOCUBE` | Normal course/objects/shadows/cube paths | `src/platform/race_course_passes.cpp:760`; `race_traffic_runtime.cpp:1112`; `src/runtime/game_host.cpp:1219`, `:67` | S1 presence; do not allow in normal rendering proof | Source |
| `OR2_D3D11_CPU_VERTEX`, `OR2_KEEP_TEXTURE_RGBA` | Native vertex path; no retained RGBA experiment | `xbox/source/d3d11_d3d9.cpp:137`, `:23` | S1; RGBA requires exactly `1`, CPU override is presence-based | Source |
| `OR2_XBOX_RENDER_BEGIN/END/EVERY` | Rasterize every frame | `xbox/source/renderer_present.inc:8-13` | S1; host-test renderer can skip actual rasterization outside the chosen frame set | Source |
| `OR2_TRIPLE_VIEWS`, `OR2_TRIPLE_AHEAD/BEHIND` | INI/default projection and normal view-union policy | `windows/source/window_desktop.cpp:378`, `:171` | S1 plus applied triple status; do not substitute for requested INI-mode proof | Source |
| `OR2_FRAME_LATENCY`, `OR2_XBOX_REFRESH_HZ` | Saved latency; actual display refresh | `xbox/source/d3d11_display.cpp:31`, `:254` | S1; latency override/refusal also named | Source |
| `OR2_HOST_FIXEDCLOCK`, `OR2_HOST_FRAME_HZ`, `OR2_HOST_PCM`, `OR2_HOST_STALL` | Existing host clock/audio policy and stall limit | `src/runtime/game_host.cpp:1116`, `:200`, `:752`; `xbox/source/main.cpp:314` | S1; these are not ordinary-driving timing evidence | Source |
| `OR2_HOST_UNLOCK`, `OR2_HOST_VARIANT`, `OR2_HOST_MODE`, `OR2_HOST_CAMERA`, `OR2_HOST_GOAL`, `OR2_HOST_GOAL_COURSE/SCORE`, `OR2_HOST_SKIP_ENDING` | Native license/menu/camera/progression | `src/runtime/game_host.cpp:686-778`, `:1118` | S1; exclude from normal progression/save checks | Source |
| `OR2_HOST_OVERLAY` | Real overlay data | `src/runtime/game_host.cpp:713` | S1; this switch supplies fabricated diagnostic overlay numbers | Source |
| `OR2_DEV_SCRIPTED_RECOVERY`, `OR2_680AD4_OLD` | Normal recovery/current native counter path | `src/platform/race_player_car.cpp:589`; `native_runtime.cpp:2593` | S1; diagnostic alternate behavior must be declared | Source |
| `OR2_NET_TR` | Native network handler selection | `src/platform/pc_network_native.inc:61` | S1; retains translated handlers for `all` or selected PCs, not merely a log switch | Source |
| `OR2_SMOKE_TAIL` | Stall-fix shortcut on; `0` selects the slow measurement path | `src/platform/race_particles_cars.cpp:169` | S1 only when set; default path preserved | Source; existing fix retained |
| `DBCE_FFB_MUTE` | Existing saved output policy | `windows/source/ffb_settings.cpp:50` | S1; nonempty except `0` mutes, output gates also report refusals | Source; required safety setting for unattended validation |
| `OR2_TELEMETRY`, `OR2_TELEMETRY_PORT` | INI telemetry policy/port | `windows/source/telemetry_live.cpp:63` | S1 plus effective telemetry output; private listener/port must be declared | Source |
| `OR2_EXE` | Ordinary game-image discovery/cache path | `src/system/exe_image.cpp:246` | S1 with value redacted; not permission to replace owner assets | Source |
| `OR2_EXE_TRACE`, `OR2_FFB_TRACE`, `OR2_FFB_PRODUCER2_SHADOW_TRACE`, `OR2_ROUTE_STATE`, `OR2_FRONTEND_STATE_TRACE`, `OR2_FRONTEND_RUN_ID`, `OR2_XBOX_SCREENSHOT_DIR` | No requested diagnostic file output | `src/system/exe_image.cpp:39`; `windows/source/ffb_live.cpp:230`; `window_desktop.cpp:700`; `src/runtime/game_host.cpp:939`; `windows/source/desktop_panel.cpp:50`, `:89`; `xbox/source/d3d11_display.cpp:193` | S1 redacts paths/IDs; file output and frame capture can affect timing | Source |

Other recognized trace/dump switches are likewise named by S1. They can affect
logging cost even if they do not change gameplay. Use the existing runner's
explicit environment record and normal safety gates; do not silently broaden its
allowlist. Keep existing installed files, settings, saves, captures and archives
intact. The next useful evidence is the integrated build and one bounded driving
batch described in [alpha readiness](../../docs/ALPHA-READINESS.md), not another
diagnostic inventory or a per-setting game launch.
