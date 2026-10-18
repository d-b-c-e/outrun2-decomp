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

| Criterion | Offline evidence | Runtime on this candidate (run-01, below) |
| --- | --- | --- |
| Boot, menus, actual driving | Candidate build: 17 focused tests pass, 1 expected skip | **PASS (A)**: the game's own trace shows the license created and saved at 15.1 s and GAME entered at 33.2 s, followed by a 60 s weave drive. Telemetry: 4007 packets, top HUD speed 60.37 m/s (about 217 km/h), gear 4, throttle 255. **Pending**: heading change and brake response (not in the telemetry route) |
| Controls and settings persistence | `desktop_controls`: refused reads, failed publishes and duplicate sections leave the file intact; one-time Tweaks import | **Partial**: A created `OutRunDesktop.ini`; both later launches read the same file unchanged, with no second import. Synthetic keyboard input steered both ways (consumed steering -127..127). **Pending**: saving through F6, a changed value applied after relaunch, wheel or pad bindings |
| Rendering, including the requested triples modes | `triple_screens` and the traffic outer-edge culling fixture (`594609f`) | **Pending**: A drove the default single view but asserted no picture. Triples were excluded from this slot and remain a separate gate (historical: planar modes seen at the rig; `Views = Angled` gave three projections on 2026-10-12) |
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

## Disposition

The 289ade5 exe is a **private alpha candidate**. It is suitable for an
owner-attended trial from the private package. It is **not** ready for a tag or
public release. Proven on this exe: a fresh license through menus to actual
driving, clean exit, and a cold relaunch that reloads the saved license and
drives again, with no crash and no save change.

The remaining work splits into two tiers.

**Playable alpha.** General readiness; muted, private, synthetic input is enough.

| Gate | State |
| --- | --- |
| Boot, menus, actual driving | PASS (A, run-02) |
| Clean exit and cold relaunch | PASS |
| License save and reload, bytes preserved | PASS |
| No crash | PASS over three launches |
| Brake: speed drops faster than when coasting | Pending: next functional batch (Hule) |
| Steering changes heading | Pending: next batch, if an existing route observes heading |
| A value changed in F6 survives a relaunch | Pending: next batch, if an existing UI route can drive the panel |
| Default single view shows a correct picture | Pending |
| A race finish saves and reloads its records | Pending |

**Rig features.** Qualified separately; these do not block a clearly labelled
muted private alpha.

| Feature | State |
| --- | --- |
| Rendered triples (`Screens = Separate`, `Views = Angled`) | Pending: a separately scoped render pass (`shim_first_loop/run.py --render`), with its own fences |
| Physical wheel, pedals and bindings | Pending: owner-attended |
| Force feel | Owner acceptance |
| Telemetry field accuracy against a receiver; gamepad vibration | Pending |

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
2. One serialized build of the integration tip, recorded with its exe hash. The
   packaged executable must come from that build, never from an older candidate.
3. One batched candidate session, following Hule's gate matrix, records every
   pending row above as pass, fail or not run, together with the `diagnostics:` line.
4. Install and rollback instructions are verified against that package.
5. Kobe reviews the evidence; only then is a tag or release considered.
