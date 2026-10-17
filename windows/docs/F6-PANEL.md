# F6 panel for the desktop build

The native toolkit panel is integrated with OutRun's own D3D11 present path. Help, Controls, FFB, Cameras, and Telemetry were rendered in muted disposable-copy runs at the earlier `6777eb3` checkpoint. The selected-model gain controls below are a later source change. Neither checkpoint accepts physical force, normal settings interaction, projected triples, or every telemetry field.

## Implemented behavior

- `windows/source/desktop_panel.cpp` uses the vendored `windows/vendor/native/panel` and ImGui code. It draws on the game's D3D11 renderer, calls `NoteFrame` each frame, and attaches the panel input hook to the game window. F6 toggles the panel; F8 and the header Stop FFB command call `ffb_stop_output`. The panel is disabled in playback processes, which show only the playback badge.
- While F6 is open, `desktop_panel_wrap_input_filter` supplies a neutral game-input sample and clears held desktop keys on open/close. `test_panel_input` covers the input hook. The retained visual runs posted keys to the game window and opened pages through the dev channel; they did not exercise normal keyboard/mouse settings changes.
- Every currently enabled page has content. Controls shows the Native/Shim input status, Wheelkit profile, steering sensitivity/dead zone, and the next-start manual-transmission choice. Bindings remain in Wheelkit.
- Controls Advanced also offers **In-race CD switcher**, **Shuffle CD tracks at startup**, and **Show CD track title**, matching Wheelkit. All default Off and save their independent existing `[CDSwitcher]` keys for the next start through the same one-key writer and failed-save rollback. The owner must configure `[CDTracks]`; these controls preserve its paths, titles, duplicate entries and order. They do not change this session's playback, controls, title layout or audio policy. The existing startup consumer still excludes recording, playback and tests; the playback panel stays closed and latched saves stay refused. The October 16 affected native Release build passes at `ee3d286`; paired Wheelkit `c5d0413` generation/parsing matches all 76 rows and its seven affected cases pass with zero skipped. The earlier 263 CD and 601 course checks are retained. Rendered controls and saving through F6 remain unqualified while the native desktop connection is unavailable.
- FFB shows status, the legacy Strength/Tweaks-derived advanced effect settings and invert for the default legacy model, and the next-start `[FFB] Output` choice when the add-on is present or already selected. With `outrun-producer@2` selected, it instead shows this session's applied steering/shift percentages and two independent 0–100% controls saved as integer `[FFBProducer2]` keys for the next start. These controls do not retune this session or migrate legacy keys. The output choice never starts/stops output in the current session. Stop FFB/F8 stops the current output and tries to save `Output = none` for the next start; a failed save is reported. The force path is default-off and has no owner-attended physical qualification.
- Cameras offers Off / Surround / Separate monitors, with the current-view status, and the separate experimental projection choice when triples are selected. The planar span is the Tweaks-parity behavior. The projected-view path remains default-off and is not qualified as three independently rendered, angle-correct views. Advanced choices include frame latency, letterboxing, and resolution from the display; changes save for the next start.
- Cameras Advanced adds **Extract scene textures** and **Extract UI textures**, independently Off
  by default. Both save their existing Graphics keys with the normal one-key writer and failed-save rollback for
  the next ordinary start. Tests, playback and recording suppress extraction. Help describes original DDS output
  under TextureBaseFolder/dump, preserved existing files, loaded-replacement suppression and disk/loading cost.
  UI covers sprite uploads and CPU START pack preparation; see TEXTURE-REPLACEMENT.md. Affected Release desktop
  compilation passes at `fb762e2`; the shared consumers pass three existing fixtures (782 checks, zero skipped).
  Original evidence is in hub artifacts/outrun-ui-extraction-20261016/build-01. Rendered controls and ordinary
  panel saves remain unqualified; these passes do not promote an installed candidate or physical acceptance.
- Cameras Advanced source candidate adds **Cache replacement textures** (EnableTextureCache), Off by default and
  next ordinary start only, ignored in tests/playback/recording. It uses the existing writer/rollback. Help explains
  the 128 MiB raw DDS cache, scene preload, memory cost and restart required after changing packs. This is CPU file
  caching, not GPU handle reuse. Affected desktop compilation and PMT fixture qualification are pending; no rendered
  control/save, loading-speed or memory improvement is claimed. See TEXTURE-REPLACEMENT.md.
- Telemetry offers On/Off and, under Advanced, Horizon 324-byte or FM7 311-byte layout. Source defaults to On/Horizon. Changes save for the next start. The earlier stream reached SimHub and the owner's wheel screen, but the `6777eb3` field correction has not had a live receiver/HUD comparison; see [the field review](TELEMETRY-FIELDS.md).
- A page change uses the one-key `write_key` path to `OutRunDesktop.ini`, rolls back the displayed choice if saving fails, and says it applies at the next game start. The producer gain controls use the same writer, with a reload/selection roundtrip fixture for the two independent integer keys. A latched test process saves nothing. `[Panel]` view, scale, and page are panel-owned settings. An ordinary keyboard/mouse save through the panel has not been observed in game.

## Retained evidence and limits

- October 16 paired CD controls: native `artifacts/outrun-cd-f6-settings-20261016/build-01` and Wheelkit `artifacts/outrun-cd-settings-wheelkit-20261016/build-01` in the hub retain the original logs/results and affected TRX. Native executable: 23,870,976 bytes, SHA-256 `b14814f535bcba1bbb346d34048d40feeadfe7e63383515d881a14404b254d57`. Both batches released their exact leases with children quiescent and process environment restored. No game, owner install or provider retry; matching saved rows and source/build evidence do not establish rendered controls or F6 save acceptance.
- Hub `artifacts/outrun-f6-help-20261012/README.md`: title/race Help, frame stats, open/closed frames and unchanged INI in a muted disposable copy. The race readout showed average, 1% low and worst frame; it is a readout, not a frame-pacing acceptance result.
- Hub `artifacts/outrun-f6-pages-20261012/README.md`: Controls, FFB, Cameras and Telemetry rendered muted after Help, with frame stats in Cameras. No save was exercised; the INI was unchanged.
- The Shim-backend page/status check is recorded in the hub at `artifacts/outrun-candidate-ac68a6f-20261013/regression/f6-ac68a6f-run1` and summarized in [ROADMAP.md](../ROADMAP.md) step 2a. Its muted “Wheel output at the next start” and “This session” labels prove UI rendering only.
- [TRIPLE-SCREENS.md](TRIPLE-SCREENS.md) and ROADMAP step 3 separate the rig-checked planar span from the experimental projection. Physical wheel output, a three-screen F6 interaction run, and normal keyboard/mouse saving remain open. The muted page checks do not qualify those behaviors.

## Source follow-up: small repository links (October 16)

The separate native Help candidate vendors only the shared panel header and
implementation from toolkit e8fca59. The desktop adapter supplies the OutRun
and Wheelkit repository URLs; Help uses the shared secondary font and text-link
widget, with no large GitHub buttons. URLs open only on an explicit click. The
existing Help callback and any issue-report action stay separate; this change
does not implement the still-pending native issue/report flow.

This is uncompiled source on an isolated branch based on 8203db2. The frozen
D374 course/loading runtime05 candidate stays unchanged and does not contain
these links. Native compilation, offline Help-frame review and later game
presentation remain pending. No input, force, INI, recording or device behavior
changes, owner install, browser opening or runtime test occurred here.
