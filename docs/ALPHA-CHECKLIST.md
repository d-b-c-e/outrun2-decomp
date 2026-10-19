# Windows alpha candidate checklist

Working branch `demo/outrun-alpha-20261018`, from public `main` `a187a94`. This is a
candidate, not a release: there is no alpha tag or release until the candidate's
evidence has been reviewed. Lanes: integration and this checklist (Demo),
stabilization fixes (Samo), packaging and regression gates
([ALPHA-READINESS.md](ALPHA-READINESS.md), Hule).

## Criteria

"Historical" means seen on an earlier build, recorded in
[the roadmap](../windows/ROADMAP.md). It does not qualify this candidate. Every
runtime row stays **pending** until one serialized candidate session records it.

| Criterion | Offline evidence | Runtime (the runs below) |
| --- | --- | --- |
| Boot, menus, actual driving | Candidate build: 17 focused tests pass, 1 expected skip | **PASS (A)**: the game's own trace shows the license created and saved at 15.1 s and GAME entered at 33.2 s, followed by a 60 s weave drive. Telemetry: 4007 packets, top HUD speed 60.37 m/s (about 217 km/h), gear 4, throttle 255. **Pending**: heading change. **Inconclusive** (run-03): brake response against coasting |
| Controls and settings persistence | `desktop_controls`: refused reads, failed publishes and duplicate sections leave the file intact; one-time Tweaks import | **Partial**: A created `OutRunDesktop.ini`; both later launches read the same file unchanged, with no second import. Synthetic keyboard input steered both ways (consumed steering -127..127). **Pending**: saving through F6 (in run-03 the F6 key never reached the window), a changed value applied after relaunch, wheel or pad bindings |
| Rendering, including the requested triples modes | `triple_screens` and the traffic outer-edge culling fixture (`594609f`) | **Qualified for alpha on `A92CDCB6`** (render-deploy-01, kobe 270778): the game's own full-span capture shows widescreen, the HD HUD and three `Views = Angled` projections. The start-grid wedges are documented, cause unproven. **Pending**: the default single view's picture as a gate (A drove it without asserting a picture) |
| Exit and relaunch | Settings apply at the next start; shutdown is documented in [P21-SHUTDOWN](../windows/docs/P21-SHUTDOWN.md) | **PASS**: all three launches closed cleanly (exit 0, not forced) with owned processes gone. The cold relaunch (run-02) loaded the saved license and drove again |
| No known crash or save regression | Resource-lifetime null read fixed (`ff90127`); upstream #2 not reproduced by the `c2c_goal_sequence` fixture | **PASS for the license save**: no crash in three launches. A wrote `License1.dat` (1040 B) and `common.dat` (68584 B); run-02 loaded them, and both files stayed byte-identical. **Pending**: a race finish with its records saved and reloaded |

### Candidate run-01 (2026-10-18)

- **Candidate:** source `289ade5`. Build passed in 8 min 4 s. The exe is `OutRunDesktop.exe`, SHA-256 `1929D25FACE3270F237E79F64A22F772F19C89A1EC2D7BC058A9D9D1CB92E064`, 24021504 bytes.
- **Tests:** all 18 selected tests were found. 17 passed and `d3d9_resource_lifetime` skipped (no `OR2_EXE`), with 0 failures.
- **Lease and data:** Hule held the lease from 19:40:16Z to 19:53:10Z and used a fresh disposable copy. Owner source files were unchanged afterwards.
- **Diagnostics line:** both launches logged exactly the five declared names: `DBCE_FFB_MUTE`, the runner's two frontend-trace variables and the two telemetry variables. No force or vibration output.
- **Run B failure (first and only):** the `existing-license-loaded` wait timed out after 20 s at the title (mode 32, child 0, profiles never queried).
  - Cause: the runner. Its existing-license branch sends no title or menu input before waiting for the load; the title prefix runs only on the fresh path.
  - Not evidence of save corruption: A's save files are on disk.
  - Fixed in the runner only: Samo `10722ca`, merged as `48bd5b9`. The game exe was not rebuilt.
- **Private package:** `outrun-desktop-private-alpha-289ade5-windows-x64.zip`, SHA-256 `ec08d51f054baa81c0fb6257f84df429dbb49b23227a136537d6d1af6d374db1`, 14 members read back. Its manifest marks it pending and private-only.

### Candidate run-02, cold relaunch only (2026-10-18)

- **Setup:** same exe hash, runner tree `055aa651` (from `48bd5b9`), the same disposable copy holding run A's save. Hule held the lease from 19:59:44Z to 20:00:50Z; runtime was about 46 s.
- **Result: PASS.**
  - The game's trace showed the title, then title advance and license loaded at 6.7 s (`PC profiles queried=1 loaded=1`).
  - The menus followed, and GAME was entered at 24.2 s with a 15 s drive. Telemetry: 1260 packets, top speed 26.9 m/s, throttle 255.
  - No save was written (`saves=0`), and the save, INI and exe baselines were unchanged afterwards.
- **Diagnostics:** the same five declared names.

### Render tools preparation, render-tools-01 (2026-10-18)

- **Session:** build only. Hule held the lease from 20:20:07Z to 20:22:06Z. Pins (candidate `289ade5`, toolkit `5cb9953`) matched before and after; the candidate exe was unchanged.
- **Tools staged privately:**
  - `control_publish.exe`: SHA-256 `66aaa6cd…9becbc`
  - `capture_replay.exe`: SHA-256 `ba760dcd…99f02f22`
  - `P21Finalizer.dll`: SHA-256 `4457fece…a8e3c20`
  - Native imports are system DLLs plus the VC runtime only.
- **Render copy:** a dedicated copy, `OutRunAlpha-hule-render-20261018-01`, with its exact PROFILE INI.
- **Checks:** both the single-view and angled-view no-launch checks passed (`--check-only` exit 0, no problems).
- **Cleanup:** the dotnet compiler server was closed with `dotnet build-server shutdown`. Future dotnet steps inside a lease use `UseSharedCompilation=false`.
- No picture was taken. Rendering acceptance is still pending.

Runner for the functional slot: integration `7030594`, `windows/tests/perf` tree
`c6154bb6`. It adds the opt-in `--brake-coast` (Samo `a71c073`); default routes are unchanged.

### Candidate run-03, functional (2026-10-18)

- **Setup:** same exe, existing license, one cold launch. Hule held the lease from 20:26:52Z to 20:28:53Z.
- **Route PASS:** the saved license loaded, all five pedal phases completed and the game exited 0. The INI and both saves were restored exactly.
- **Brake vs coast: INCONCLUSIVE.**
  - Coast: 50.4 to 16.0 m/s. Brake (255 witnessed): 39.3 to 0.0 m/s, overlapping the coast over 16.0–39.3.
  - The coast screenshot shows the car on the beach or off-road, so a surface difference or collision could account for the gap.
- **Screenshots (default single view):** car menu, race/HUD and the scene at the end of the pedal phases. Scene and UI are 4:3, pillarboxed in the 2560x1440 window: `options.ini` had no `widescreen`, and no HD pack or replacement was configured.
- **F6 Horn persistence: PENDING.** The supported window capture worked, but the F6 key press never reached the window (one retry, same failure). No widget edit and no second launch were made.

### Consolidated build, main-build-01 (2026-10-18)

- **Source:** `4652a18`, which is the `289ade5` candidate plus the toolkit panel and controls re-vendor (Samo `a7c48df`). Hule held the lease from 20:50:16Z to 20:59:18Z.
- **Build PASS:** configure, compile, test discovery, CTest, PE imports and candidate metadata all exited 0.
- **Tests:** 19 discovered and run (now including `panel_input` and `startup_diagnostics`). 18 passed; `d3d9_resource_lifetime` skipped as expected without `OR2_EXE`.
- **Exe:** `OutRunDesktop.exe`, SHA-256 `A92CDCB6468D27082970F7D14D8B909A4A346A14A77608AB0367F7296AE234AF`, 24023040 bytes.
- **Install tuple:** generated by the existing generator (`make_package_text.py` `49ABE2F2…`): tuple `87B3BE6D…`, capabilities `5BB46931…`. It was checked against disposable copies only; the owner's files were unchanged and nothing is installed yet.
- **Status:** this exe is the deployment candidate. None of the earlier runtime runs used it; the deployment smoke is its first runtime.

### Combined slot combined-01 (2026-10-18): private intended-config check

- **Setup:** exe `A92CDCB6`, in a fresh disposable copy with the intended settings. Hule held the lease from 21:07:56Z to 21:10:49Z. One private launch; the route passed with exit 0.
- **The game's own log proves the intended configuration:**
  - 257 UI assets indexed, UI replacement on, scene replacement off. At runtime, 164/181 UI textures were replaced, loading screens 4/5, meter 2/2.
  - World aspect 1.778 (widescreen).
  - A 7680x1440 window at (-2560,0), with `Screens = Separate monitors` and three projected Angled views.
  - Telemetry off, no force output, and only the five declared diagnostic variables.
  - UI asset receipt `FDB2CAF1…`: 209 historical, 44 supplement and 4 newer START files, 2507988532 B.
- **Visual gate: INCOMPLETE (capture coverage).** The supported desktop capture returned only the left 2560x1440 tile, so the HD centre, the HUD and the three views with their seams could not be inspected. No product defect is inferred.
- **No deployment:** nothing was applied to the owner's install or LaunchBox, and no second launch ran.
- **Next:** use the game's own `shim_first_loop --render` back-buffer capture, which covers the full span with fixture rig geometry equal to the live profile. Hule is adding a render-only `[Graphics]` whitelist to that runner.

### Render slot render-deploy-01 (2026-10-18): the game's own full-width capture

- **Setup:** exe `A92CDCB6`, harness `a70e369` (merged as `81f3a15`). The private test profile carried the pinned four-key HD overlay over the staged 257 assets, with an existing license. Hule held the lease from 21:38:02Z to 21:40:39Z.
- **Capture PASS:**
  - 60 back-buffer frames at 3840x720 (the full 7680x1440 span at half scale). Seal and replay passed.
  - Scripted existing-license route to the beach course; normal exit 0.
  - The owner's 16 files were verified unchanged and the profile was restored.
- **Frames 0, 30 and 59:** the centred HD HUD and START are sharp. Three distinct views progress from 0 to 109 km/h, folding through both seams. Widescreen and three projections are confirmed visually.
- **Finding: black wedges on the start grid only (frame 0).** One sits under the grandstand at the outer left, the other beyond the ground past the fence at the outer right. Both are filled by frame 30.
  - This matches the documented limit in [TRIPLE-SCREENS](../windows/docs/TRIPLE-SCREENS.md) (start-grid wedges). With `MatchGameFov = true` the side panels look up to 121° from the heading, beyond the stock camera.
  - The 2026-10-12 coverage runs and object census (archived `claude/triple-projection-f6`) found no pixel written there under the tested overrides. Shader discard was ruled out, and every enumerated stage and environment object was submitted.
  - The cause is **not proven**: missing content, viewport or depth clipping, unloaded banks and prop placement remain uninstrumented.
  - Samo confirmed the same outlines and placement as the historical shots (N1, RC1, RC2). There is no new location or shape, and frames 30 and 59 fill both. That is not a pixel-identity claim.
  - No evidence-driven code correction is justified.
- **No overall visual PASS was given in that slot, so nothing was installed.** The stop receipt is preserved.
- **Later disposition (kobe 270778):** the HD, widescreen and Separate/Angled visual evidence is **qualified for this alpha**, with the start-grid black wedges documented.
  - The wedges match the history and are absent from moving frames.
  - Their cause is unproven, so they are not classified as a non-defect.
  - Install and real-route smoke follow in a fresh slot.

### Install slot installed-smoke-01 (2026-10-18): deployment script defect, rolled back

- **Setup:** Hule held the lease from 21:47:17Z to 21:48:25Z. It reused the accepted capture; target `E:/Games/OutRunDecompSmoke`.
- **Steps that completed:** the deployer backed up 26 owner files to `.dbce-backups/consolidated-20261018-01`, then began staging UI assets with the journal saved after each file. 82 were copied and verified, plus one copy intent, out of 257.
- **First failure:** a journal save (`Set-Content`, needing exclusive access) found `deployment.json` held by another process. The holder is unknown.
- **Recovery:** the deployer rolled back at 21:48:22Z (journal state `rolled-back`) and removed the staged assets. The owner's files were verified unchanged.
  - No exe, INI or capabilities change was made, and there was no game launch.
  - LaunchBox was not touched (no registration receipt).
  - The `-01` backup folder stays on the target as evidence.
- **Correction (Hule), `Deploy-Consolidated.ps1` SHA-256 `A64A9483E978D4234D61E1563E91600C027D92EEBBDBBF980BF3D60BB403ED9D`:**
  - **Writer:** a unique `CreateNew` temp file in the same folder, the complete JSON flushed to disk, then an atomic `File.Replace` (`File.Move` for the first write), with no delete-then-write gap.
  - **Retries:** only sharing and lock errors are retried, up to 10 x 150 ms, clipped to the original work deadline.
  - **Recovery:** it still rolls back when the journal itself cannot be written.
  - **Tests:** first write; a held reader followed by an atomic replace; a persistent lock that refuses at the deadline and keeps the old JSON; partial copy; refusal after an edited asset or INI; rollback with an unwritable journal. All pass.
- **Next:** a fresh `-02` backup and a fresh install grant. No product rebuild.

### Install slot install-smoke-02 (2026-10-18): DEPLOYED; LaunchBox UI smoke pending

- **Setup:** wrapper `B031376D…`, corrected deployer `A64A9483…`. Hule held the lease from 21:54:15Z to 22:00:23Z.
- **Install PASS (applied 21:57:30Z) on `E:/Games/OutRunDecompSmoke`:**
  - Fresh backup `.dbce-backups/consolidated-20261018-02` with 26 files; `-01` is preserved.
  - 257/257 UI assets staged and verified.
  - Installed and read back exactly: exe `A92CDCB6`, INI `C2D4AD15`, capabilities `5BB46931`.
  - Owner settings, saves, controls, force and unknown keys preserved. Deployment scope verified, with no journal temps left.
- **LaunchBox route registered:** entry `cab44f53-70b5-4b72-86f2-b64fc1f4a8b9`, "OutRun 2006: Coast 2 Coast (Decomp)", with the relative `ApplicationPath` to the Smoke exe and one Outrun Collection row.
  - 7 art files created.
  - Receipt `Data/Platforms/Windows.xml.bak-outrun-decomp-20261018-165729.receipt.json`; both XML files matched their post-apply hashes at readback.
  - The Tweaks and Redux entries are unchanged.
- **Real-route smoke: PENDING.** The owned LaunchBox stayed on its "Populating Games" splash through the bounded wait (to 22:00:09Z). No game launch, no input, no alternate route.
- **Closure gap, corrected:** after the first release, `Core\LaunchBox.exe` (PID 12008, started 21:57:31Z by the owned launcher stub, and its child 13048) was still running. Closing the repo-root stub had not closed the real frontend.
  - Custody recovery: Hule held the lease from 22:01:31Z to 22:02:43Z, inside the original deadline. 12008 was closed normally at 22:02:14Z and 13048 exited normally.
  - No LaunchBox, BigBox or OutRun process remains, and the owner's 16 files read back exactly.
  - `Windows.xml` and the Outrun playlist still exactly match their post-apply hashes. The owner's edits and the new entry are intact, and the registration receipt can still roll back.
  - Record: hub `installed-smoke-02/custody-recovery.json`. The original stub-only quiescence claim is superseded, not rewritten.
  - Future runners must watch the `Core` descendants, not the root stub.

### LaunchBox UI smoke 03 (2026-10-18): no entry, capture provider unreliable

- **Prepared:** UI-only wrapper `C5314730…` with the Core-custody adapter `54FE02D8…` (15 synthetic, 68 and 25 helper fixtures pass). Readiness requires the visible game browser to be proven before any input.
- **Pre-launch check:** the supported capture provider was aimed at a known window (an Explorer window, identified correctly through accessibility). Its screenshots showed a different window's content (a Firefox new tab), twice, with no API error. Reliable selected-target capture was not established, so the slot stopped before LaunchBox, as the grant requires.
- **Nothing ran:** no lease, launcher, game or input; all frontend and game processes were absent. Evidence: hub `provider-preflight-03/result.json` (`F489389D…`).
- **Status:** the installation is unchanged and complete. The real-route gate is still open, waiting for a provider-owned capture correction or another explicitly chosen route (for example, owner-attended confirmation).

### Force correction build, producer-build-01 (2026-10-19)

- **Why:** the legacy force model reads car `+0x1D0`/`+0x1D4` as steering and steering rate, but their producers write speed deltas (`pc_common_control.cpp:2329-2332`), and `+0x264`/`+0x268` hold road-edge distances (Samo's source findings). kobe 272265 put this correction ahead of any feel acceptance.
- **Change:** `e4f4d0c` (Samo, approach (c), kobe 272782), reviewed by Demo.
  - Legacy is a software trace-only reference and is refused before `WheelFfb.dll` loads, even with a saved `Output = wheel`.
  - Only an explicit `[FFB] Model = outrun-producer@2` can drive the wheel, through the unchanged gated delivery.
  - F6 offers the model choice and writes only `Model`; Output stays independent.
  - One header (`ffb_capability.hpp`) holds the policy for both the refusal and the controls consumer's `ffb` marker.
- **Build:** Hule held the lease from 02:02:24Z to 02:10:10Z on a clean isolated tree of `e4f4d0c`. Configure, compile, discovery, CTest, the native consumer, the read-only owner readiness and the metadata all passed.
  - Exe `00491F111BB03A99C38051133CA37A363B648CF98FAD52FDFB6F6128E115B7C3`, 24028160 B. Consumer `47490D8A…`.
  - 7/7 focused tests: ffb_outrun, ffb_producer2, ffb_shadow, ffb_player_model, ffb_delivery, ffb_wiring, controls_consumer.
  - The consumer reported the exact marker, and the assembled `session-capabilities.json` (`7D713A1A…`) carries it.
  - Tuple `BF693C39…` against the installed baseline (`A92CDCB6`, INI `C2D4AD15`, capabilities `5BB46931`, receipt for `4652a18`).
- **Not done:** no install, no game, no force output. Hule's evidence is `producer-build-01/result.json`. Installing it is a separate, reviewed slot.

### Force correction install, producer-install-01 (2026-10-19)

- **Grant and plan:** kobe 273824. Wrapper `Install-ProducerCandidate.ps1` `B33A1339…` and plan `D30121B3…`, reviewed by Demo. Hule held the lease from 02:17:38Z to 02:17:54Z.
- **Steps, all passed:** owner preflight, then the retained updater's Check and Apply, then the installed-pair readback.
  - The preflight took a fresh baseline and found a unique old receipt, with no game or frontend running. It pinned the LaunchBox XML and the entry.
- **Installed:**
  - exe `00491F11…`;
  - capabilities `7D713A1A…`, carrying the exact `ffb` marker;
  - receipt `OutRunDesktop.install-e4f4d0c-20261019-021500.json`, the only matching installed receipt;
  - backups `*.e4f4d0c-20261019-021500` of `A92CDCB6` and `5BB46931`.
- **Unchanged:** INI `C2D4AD15` (no `[FFB]`, so force is off), saves, options, assets, LaunchBox and the historical receipts.
- **Not done:** no game, UI or force. State `installed-not-runtime-tested`. Evidence: hub `producer-install-01/result.json`.

### Owner drive on `00491F11` (2026-10-18, owner-attended)

- **Report** (kobe 275276): the game ran well and the wheel controls worked. The force was felt but very weak, and it seemed to push outward.
- **Configuration**, read-only:
  - INI `F1CB0F84`, saved by the owner's F6 at 22:26: `[FFB] Model = outrun-producer@2`, `Output = wheel`, default strengths (steering 50%, shift 50%), no `[Input]` (Native backend).
  - The `runtime.log` of that session shows the R12 instance opened in the race: 1274 commands, 5 accepted zeros, 0 recoveries. Physical output is **present**. This was the first delivered producer@2 torque.
- **Direction: cause found in source.**
  - The model's centring force pushes toward positive steer, and that steer has the sign of DirectInput X (to the right).
  - The force reached `SetDeviceForcesXY` unchanged. On the MOZA R12 a positive x turns the wheel left, as two earlier measurements on this rig found.
  - So the spring was delivered as anti-centring.
  - Correction: `02808e8` (branch `demo/ffb-r12-sign-20261019`). It drives only a wheel with a measured direction (the R12, -1) and refuses others before any device call. The focused offline checks pass. It is not built, merged or installed.
- **Strength:** this is the provisional profile by design, not a scaling defect. The only terms are a spring of `0.30 x steer x min(v/30 m/s, 1)` at 50% and a shift pulse. For example, a 20% steer at 100 km/h gives about 5.6% of full scale. Defaults stay unchanged until the corrected direction is felt.
- **Shifter:** Manual drove as sequential because `[Controls] Transmission = Sequential`. Native acts on the H-pattern gates (gear1..6) only with `Transmission = H-pattern` (kobe 275448). Physical H-pattern is unverified.

## Disposition

The installed exe is `00491F11` (built from `e4f4d0c` in producer-build-01, installed in
producer-install-01). It is `A92CDCB6` plus the force-model correction, on the owner's
`E:/Games/OutRunDecompSmoke`, as a **private alpha** ([DEPLOYMENT](DEPLOYMENT.md)). It is
**not** ready for a tag or public release. The owner has driven `00491F11` once (above: wheel
controls worked, force present but inverted and weak). The automated runtime evidence below
comes from `A92CDCB6` and `289ade5`.

- **Proven on the earlier `289ade5` exe**, which lacks only the toolkit panel and controls
  re-vendor: a fresh license through menus to actual driving, clean exit, and a cold
  relaunch that reloads the saved license and drives again, with no crash and no save change.
- **Proven on `A92CDCB6`:**
  - the intended configuration, by the game's log (combined-01);
  - HD, widescreen and Angled triples, in the game's own capture (render-deploy-01);
  - an exact install readback (install-smoke-02).
- The private package (`ec08d51f…`) holds the older `289ade5` exe.

The remaining work splits into two tiers.

**Playable alpha.** General readiness; muted, private, synthetic input is enough.

| Gate | State |
| --- | --- |
| Boot, menus, actual driving | PASS (A, run-02) |
| Clean exit and cold relaunch | PASS |
| License save and reload, bytes preserved | PASS |
| No crash | PASS over three launches |
| Brake: speed drops faster than when coasting | Inconclusive (run-03): the brake and coast speed ranges overlap, and the coast may have left the road |
| Steering changes heading | Pending: no existing route observes heading |
| A value changed in F6 survives a relaunch | Pending: in run-03 the F6 key never reached the window |
| Default single view shows a correct picture | Pending |
| A race finish saves and reloads its records | Pending |
| Installed with a backup and an exact readback | PASS (install-smoke-02) |
| Launch through the LaunchBox entry | Open: UI smoke 03 stopped before entry (the capture provider returned the wrong window) |

**Rig features.** Qualified separately; these do not block a clearly labelled
muted private alpha.

| Feature | State |
| --- | --- |
| Rendered triples (`Screens = Separate`, `Views = Angled`) | Qualified for alpha (render-deploy-01, kobe 270778), start-grid wedges documented; the physical check is owner-attended |
| Physical wheel, pedals and bindings | Owner drive 2026-10-18: the wheel controls worked. H-pattern is not active (`Transmission = Sequential`), and physical H-pattern is pending |
| Force feel | Owner drive 2026-10-18: output present, felt weak and pushing outward. The direction cause is found, and the correction `02808e8` is unbuilt. Pending: a corrected build, then an attended turn-and-release check at speed |
| Telemetry field accuracy against a receiver; gamepad vibration | Pending |
| Wheelkit 0.6.52 install | Waits for the owner to quit Wheelkit normally |

A physical check of wheel feel, force output, telemetry fields and angled triples is
a separate, owner-attended gate. A pass here does not imply it.

## Disposition of unmerged work

| Item | Decision | Reason |
| --- | --- | --- |
| Cumulative `54b2c46`, angled traffic culling `594609f`, resource fixture `e87299a` | Integrated (in `main`) | Consolidation of 2026-10-18 |
| Request stack, loading picture `da4d126`, publisher `b173a48`, level-menu test `95a84bd`, prompt identity, route observation, presentation sound, countdown | No action | Equivalent changes are already in `main` ([review](UPSTREAM-REVIEW.md)) |
| Controls rescan `235bbb3` | No action | Superseded by change-driven rescans and safer settings writes |
| Copied-input bridge and profile inspection | No action | Owned by the toolkit's `native/shim`; the fork keeps its reviewed pin |
| Startup diagnostic-environment report (S1, `cbb2ac2` + `5e29915`) | **Fixed** (merged `6ece3c8`) | Production reads 91 `OR2_*`/`DBCE_*` switches, several change behaviour, and no startup line listed which are set. Now one bounded line names the recognized switches that are set; values that could hold credentials or private paths are redacted. Formatter test 15/15 offline; desktop integration pending the candidate build |
| Alpha opt-ins table (S2, `aadc2a3`) | **Fixed** (merged `6ece3c8`) | [ALPHA-EXPERIMENTS](../windows/docs/ALPHA-EXPERIMENTS.md): defaults and evidence level of the opt-ins that matter for the alpha. All defaults kept |
| Async shader compiles (`c057309`..) | Defer | Diverges from the current device; needs a re-port and a cold-cache A/B on the rig. Not alpha-critical |
| Hitch-probe and triple-projection diagnostics | Defer (archive) | Measurement only, default off; not a demonstrated improvement |
| Slow-frame counter `1d9ca5e` | Defer | Kept on its diagnostic branch for the author (kobe 264530); the perf parser already tolerates its absence |
| Test-only refactor of startup settings | Defer | Defaults are explicit literals; the risk to the startup path outweighs the gain |

## Before an alpha tag

1. Done: the fix commits (S1, S2) and the packaging checks are merged. The formatter
   test (15/15) and the packaging tests (8/8) passed offline before merge.
2. Done: main-build-01 built the integration tip `4652a18` (exe `A92CDCB6`), and the
   installed exe is that build. A packaged executable must also come from it, never from
   an older candidate.
3. One batched candidate session, following Hule's gate matrix, records every
   pending row above as pass, fail or not run, together with the `diagnostics:` line.
4. Install verified for the deployed build (install-smoke-02, through the deployer, not
   the private package). Rollback is documented in [DEPLOYMENT](DEPLOYMENT.md) and has
   not been exercised on the installed state.
5. Kobe reviews the evidence; only then is a tag or release considered.
