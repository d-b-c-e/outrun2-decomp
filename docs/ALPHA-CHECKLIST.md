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

| Criterion | Offline evidence on `main` | Runtime: historical / this candidate |
| --- | --- | --- |
| Boot, menus, actual driving | Consolidation build and focused CTests, 7 pass and 1 expected skip ([validation](CONSOLIDATION-VALIDATION.md)) | Planar race on the racing row, 199/199 samples (2026-10-14); F6 pages rendered (ac68a6f) / **pending** |
| Controls and settings persistence | `desktop_controls`: refused reads, failed publishes and duplicate sections leave the file intact; one-time Tweaks import | F6 saving by keyboard or mouse never checked in game / **pending**: save, exit, relaunch, value applied |
| Rendering, including the requested triples modes | `triple_screens` and the traffic outer-edge culling fixture (`594609f`) | `Screens` Off/Surround/Separate planar seen at the rig; `Views = Angled` gave three projections with documented start-area limits (2026-10-12) / **pending** for both |
| Exit and relaunch | Settings apply at the next start; shutdown is documented in [P21-SHUTDOWN](../windows/docs/P21-SHUTDOWN.md) | **pending**: normal exit (LB+RB+Menu or window close), clean relaunch, no stranded process or force |
| No known crash or save regression | Resource-lifetime null read fixed (`ff90127`); upstream #2 not reproduced by the `c2c_goal_sequence` fixture | No open crash report found (hcom search, 2026-10-14..18) / **pending**: a race finish with save and reload |

A physical check of wheel feel, force output, telemetry fields and angled triples is
a separate, owner-attended gate. A pass here does not imply it.

## Disposition of unmerged work

| Item | Decision | Reason |
| --- | --- | --- |
| Cumulative `54b2c46`, angled traffic culling `594609f`, resource fixture `e87299a` | Integrated (in `main`) | Consolidation of 2026-10-18 |
| Request stack, loading picture `da4d126`, publisher `b173a48`, level-menu test `95a84bd`, prompt identity, route observation, presentation sound, countdown | No action | Equivalent changes are already in `main` ([review](UPSTREAM-REVIEW.md)) |
| Controls rescan `235bbb3` | No action | Superseded by change-driven rescans and safer settings writes |
| Copied-input bridge and profile inspection | No action | Owned by the toolkit's `native/shim`; the fork keeps its reviewed pin |
| Startup diagnostic-environment report (S1) | **Fix**, Samo | Production reads about 120 `OR2_*` switches, several change behaviour, and no startup line lists which are set. One bounded line naming recognized switches only; values that could hold credentials or private paths are omitted |
| Alpha opt-ins table (S2), `windows/docs/ALPHA-EXPERIMENTS.md` | **Fix**, Samo | Testers need the default and evidence level of the opt-ins that matter for the alpha. Not an inventory of every switch |
| Async shader compiles (`c057309`..) | Defer | Diverges from the current device; needs a re-port and a cold-cache A/B on the rig. Not alpha-critical |
| Hitch-probe and triple-projection diagnostics | Defer (archive) | Measurement only, default off; not a demonstrated improvement |
| Slow-frame counter `1d9ca5e` | Defer | Kept on its diagnostic branch for the author (kobe 264530); the perf parser already tolerates its absence |
| Test-only refactor of startup settings | Defer | Defaults are explicit literals; the risk to the startup path outweighs the gain |

## Before an alpha tag

1. Merge the fix commits (S1, S2) into this branch; focused offline tests pass.
2. One serialized build of the integration tip, recorded with its exe hash. The
   packaged executable must come from that build, never from an older candidate.
3. One batched candidate session, following Hule's gate matrix, records every
   pending row above as pass, fail or not run, together with the `diagnostics:` line.
4. Install and rollback instructions are verified against that package.
5. Kobe reviews the evidence; only then is a tag or release considered.
