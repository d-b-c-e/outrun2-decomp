# F6 player settings

Source successor of fork main `a73e8e3`. The October 18 owner policy and Tori's bounded review (hcom 275436) replace the long player catalogue with essential tasks. The shared panel widgets, keyboard navigation, Stop FFB/F8, Help links, current-session separation and one-key failed-save rollback remain in use. Opening either view does not migrate settings or enable force.

## Reduced inventory

The previous catalogue contained **76 editable options across conditional branches**: Controls 27 (including two trigger gains), FFB 16, Cameras 30, Telemetry 3. These counts exclude status/help rows and shared panel preferences; not all conditional branches appeared at once.

| Player page | Simple | Selective Advanced additions |
|---|---|---|
| Controls | Edit bindings (with draft Sequential/H-pattern choice); steering sensitivity; steering dead zone; start the game transmission menu on Manual | Telemetry enable |
| FFB | Enabled at next start; steering Strength; concise current-session status | Independent Shift cue, only for a saved setup that supports it |
| Cameras / Display | Gameplay display; screen layout; projection only for a spanning layout | Black bars |
| Cameras / Quality | Graphical Tweaks: Off/Low/Medium/High/Ultra/Custom; expandable change preview; explicit Apply and Undo | Four individual overrides, visible only with the breakdown expanded: HD menus/HUD, HD cars/stages, filtering, reflection detail |
| Help | Existing shared Help, issue action and small repository links | Same |

The ordinary UI has **9** setting controls at most with details closed (including conditional projection), or **16** with Advanced and its four quality overrides expanded. Apply/Undo and Show/Hide are commands, not additional tuning settings. The routine Telemetry tab is removed; its basic enable remains discoverable in Controls Advanced. Gamepad vibration is omitted because this UI cannot prove a supported selected controller; config support remains.

The binding-host continuation adds **Controls > Edit bindings** as a real saved draft flow. It uses the shared `ProfileDraft`/`CaptureFlow`/guarded Windows store at toolkit source `3ade9cf`, with Schema 2 explicitly enabled. The editor offers action groups, explicit current-device selection, Listen, Clear, input inversion, supported shifter mode, Save and Cancel. Normal menu counts exclude the dedicated editing flow and its commands.

Accepted observations are copies of the ordinary shared Core's existing physical sample plus its adopted same-epoch identity/capabilities. There is no new tick, reader, device open or enumeration. Observation copying/capture work runs only while the binding editor is visible. Latched/injected/stale/failed reads, missing interface identity, epoch mismatch, recording, playback, lost foreground and a hidden/closed Controls page cannot capture. Cached read sequences do not refresh freshness. A reconnect never silently replaces an explicitly selected device. A rebound axis gets no invented measured endpoints; unchanged steering/pedal calibration stays intact.

Save performs the shared fixed-path expected-byte replacement/backup/readback and applies only at restart. A pre-replacement conflict preserves the draft and external edits; uncertain post-replacement verification requires restart with backup/draft kept. Cancel discards local changes only. Closing F6 or changing pages stops listening while retaining the draft. The active input profile and backend are never reconfigured by the editor.

Capture is limited to devices already owned by the active shared input host. Add a first or different/unbound device through Wheelkit; Native-backend sessions explain the shared-setup requirement rather than starting another reader. Tests/playback and recording do not enable the editor. This source flow still needs native compilation, synthetic fixtures and later rendered/physical acceptance; its existence is not runtime proof.

## Transmission and force

Shifter input is changed inside the binding draft and saved as exactly `[Controls] Transmission = Sequential | H-pattern`, with the shared store's normal revision update. Existing binding lines, measured `cal=` endpoints, schema and other sections stay unchanged. H-pattern supports gears 1-6; reverse, higher gears and clutch are unavailable. Choosing Manual in the game is separate: `[Misc] DefaultManualTransmission` only preselects that menu.

Producer2 is the only current wheel-force method. **There is no model selector or Legacy effect control in any F6 view**, including developer mode. The existing model parser and Legacy software trace compatibility are untouched. Legacy/unknown setup cannot be enabled through the ordinary output control. The control writes only Output; it never silently chooses a model or backend. Wheelkit owns the explicit shared-Shim migration preview and rollback. Force strength and physical polarity/feel remain separate backend/attended work.

## Graphical Tweaks

These are explicit visual profiles, not measured performance guarantees. Off disables the optional settings in this table and keeps the game's graphics. HD levels require installed packs. The levels differ in actual supported settings:

| Setting | Off | Low | Medium | High | Ultra |
|---|---:|---:|---:|---:|---:|
| Texture filtering | 0 | 2 | 4 | 8 | 16 |
| Reflection cube edge | 128 | 128 | 256 | 512 | 1024 |
| Transparency supersampling (requires game MSAA) | Off | Off | Off | On | On |
| Full-detail distant cars | Off | Off | Off | Off | On |
| HD cars/stages | Off | Off | Off | On | On |
| HD menus/HUD | Off | Off | On | On | On |
| Replacement texture cache | Off | Off | Off | On | On |

Only these seven existing `[Graphics]` keys are managed by the presets. Draw distance and depth/culling overrides stay in developer/config access because they interact with accepted three-view visibility. Compatibility fixes, geometry, display choice, presentation pacing, texture extraction, controls, calibration and force are outside the presets.

Custom is inferred when the saved effective values differ from all defined profiles. Selecting a level previews it; nothing is written until Apply. Individual edits save their existing keys and recompute the profile label. The complete before/after breakdown can be expanded in either view. Unknown keys and sections remain intact.

Apply prepares `<OutRunDesktop.ini>.graphics-undo.ini`, then writes all seven settings in one existing section-replacement transaction. The snapshot records exact prior values **and absence** plus the expected applied profile. Undo survives restart when that profile still matches; an intervening change to a managed key refuses Undo. Unrelated changes remain preserved. A failed main-file publish retains the old file and restores the prior Undo snapshot, or reports a backup recovery error. Successful Undo retains its rollback sidecar. No settings are applied to the running game; restart is required.

## Developer/config access and checks

`[Panel] DeveloperOptions = true` is a manual opt-in, default false. It restores the specialist Controls/Cameras/Telemetry catalogue; `[Panel] View = Advanced` alone does not. Force model selection and Legacy tuning stay out of F6. Historical Legacy trace calculations and keys remain supported by their existing readers, preserving old evidence and rollback compatibility without offering a second wheel model.

`desktop_bindings` fixture source exercises accepted envelopes, real capture flows, explicit device selection/reconnect refusal, focus/read loss, Save/Cancel, exact backups, calibration preservation and external-file conflicts using synthetic samples and fresh disposable files only. Exact new shared header bytes are pinned; shared-owner qualification is separate, and historical de17086/a817ed7 qualification is not reused.

Focused fixture source extends `test_desktop_controls` to check distinct profiles, Custom/no rewrite on open, atomic multi-key Apply, restore of absent keys and spellings, restart-safe Undo serialization, publish failure, changed-key refusal, preservation of controls/calibration/force/triples/unknown fields, and a transmission-only schema-2 write. Native compilation, fixture execution and rendered saving still require the coordinated slot; no game, owner INI or installation was changed by this source work.


Menu checkpoint `cf52537` subsequently passed Demo's standalone `test_desktop_controls` (215 checks) and panel syntax check on candidate `86c7e2a`; full application/build and rendering were still pending. The later OutRun binding-host source remains uncompiled/unrun until the combined slot; shared-header qualification is maintained separately by Vunu. Separate panel correction `1d5234d` exposes actual force refusal reasons and points setup only to Wheelkit; it contains no binding-host changes.

Shared header `3ade9cf` (SHA256 `FC3489B97D11F0B24A51193286515CFD91BA80E4C05A7ABBD0F01F153020D432`) separately passed MSVC x64 C++17 `/O2 /MT /W4 /WX /EHsc` and all 63 shared draft/store fixture checks. Receipt: hub `artifacts/wheelkit-outrun-shared-transition-vunu-20261018/RESULT.md`. This qualifies the shared component only; OutRun host/capture/render/physical acceptance remains separate.
