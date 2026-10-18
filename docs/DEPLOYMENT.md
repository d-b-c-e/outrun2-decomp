# Consolidated Windows deployment: coverage and dependencies

Goal (owner, 2026-10-18): one deployed OutRunDesktop build on the fork's `main`. It
should carry every completed Wheelkit-integration and Tweaks-parity change, be
installed on the rig's existing route with a recoverable backup, and be smoke-tested
there. This is not a public release or tag. Test evidence is in
[ALPHA-CHECKLIST.md](ALPHA-CHECKLIST.md).

## Source coverage

| Scope | Where it is | Notes |
| --- | --- | --- |
| Completed Windows feature work | `main` `a187a94` | See below |
| Startup diagnostics line, alpha docs, packaging tool, runner fixes | Integration branch `demo/outrun-alpha-20261018` | The runner fixes are the cold-relaunch title advance and the opt-in brake/coast phases |
| Toolkit F6 panel and controls header | **Missing.** Allocated to Samo: re-vendor from toolkit `5cb9953` | Panel fixes: cursor-clip release only by the foreground root (`6e69cc2`), FFB state on a failed save (`10d6c63`), Help ordering, a passive status API. Controls: additive overloads only. `5cb9953` is not on the toolkit's origin |

What `main` `a187a94` already contains:
- the cumulative branch `54b2c46`, angled traffic culling `594609f` and the resource fixture `e87299a`;
- every row of the [roadmap](../windows/ROADMAP.md) Tweaks table that is in source: texture replacement, music, CD switcher, fast load, pacing and VSync, vibration, level select, Holly, highway sets, compatibility fixes, telemetry RPM, fine steering, login protection, the opt-in network, triples (planar span plus the angled views), and the F6 pages.

Not merged, each with its reason:

| Item | Reason |
| --- | --- |
| Toolkit shim extensions 2–4 and the copied-input bridge | Hundreds of lines of divergence. The decomp has its own host files, its shim backend is opt-in (Native is the default), and nothing in OutRun uses the new extensions. Keep the reviewed pin `d26776a` |
| Async shader compiles | Needs a re-port and a cold-cache A/B on the rig |
| Hitch-probe and triple-projection diagnostics | Measurement only |
| Slow-frame counter `1d9ca5e` | Left on its branch for the author |
| Controls rescan `235bbb3` | Superseded |
| Older request and loading branches | Already equivalent in `main` ([review](UPSTREAM-REVIEW.md), [inventory](BRANCH-INVENTORY.md)) |

**Separate architecture, not merged.** OutRun2006Tweaks and the d-b-c-e Tweaks
fork with force feedback (`dbce-mods-outrun-2006`) are a DLL on the original
`OR2006C2C.EXE`. Their LaunchBox entries ("(Tweaks)" and "(Redux)") stay as they are.

## Wheelkit (separate repository)

Every completed OutRun settings branch in `dbce-wheelkit` is integrated in its
local `main` `1248cb2` (2026-10-17). The tip that contains them all,
`haru/outrun-fastload-mode1-20261017` `6d36997`, is an ancestor of that main. The
decomp's readers for every such row are in this fork's `main`.

The rig's installed Wheelkit, `0.6.49+c4a4e6d`, lacks 5 of those OutRun commits:

| Commit | Change | Decomp key it writes |
| --- | --- | --- |
| `f7cb7d8` (+ `184b4bd` docs, `10d3c6d` catalog test) | Fine steering and Engine RPM rows | `[Misc] FineSteering`, `[Telemetry] EngineRpm` |
| `6d36997` | The loading-only VSync-kept mode | `[Performance] FramerateFastLoad` |
| `5dd4219` | Borderless cursor visibility | `[Window] WindowedHideMouseCursor` |

Without them the owner can still set these keys in OutRunDesktop.ini or through F6.
Picking them up means a new Wheelkit build from local `main`, and that build also
carries 43 non-OutRun commits (trucking, Sonic, SCS and others). It therefore needs
Wheelkit's own qualification and installer route. Wheelkit's `origin/main` is 305
commits behind local `main` (unpushed).

## Asset and configuration dependencies

The source defaults are the conservative test path. They are not the deployed
presentation; the deployment sets the following.

| Feature | Required setting | Assets | State today |
| --- | --- | --- | --- |
| HD interface | `[Graphics] UITextureReplacement = true`; `TextureBaseFolder` pointing at the pack | The owner's existing pack at `Games/Windows/Outrun 2006 Coast to Coast/OutRun2006 Coast 2 Coast (Tweaks)/textures/load`: 32 UI banks, 257 DDS (about 2.5 GB), including the frontend, HUD and loading banks. Provenance: hub `artifacts/outrun-ui-textures-20261012/pack-copy-manifest.csv`, and `artifacts/outrun-course-loading-runtime-20261016` for START | Off by default; no decomp folder has `textures/` |
| HD scene textures | `[Graphics] SceneTextureReplacement = true` (Tweaks enables both) | The same pack's scene banks | Off; memory cost to be measured in the smoke |
| Widescreen | `options.ini` `widescreen=1`, `resolution=2560x1440` | none | Source default 4:3. The owner's `E:/Games/OutRunDecompSmoke` already has `widescreen=1`; the LaunchBox "(Decomp)" folder does not |
| 2D UI layout | `[Graphics] UIScalingMode`: decomp default `2` (centred 4:3). Tweaks' default `1` widens the HUD but has documented gaps ([UI-SCALING](../windows/docs/UI-SCALING.md)) | none | Keep `2` unless the owner chooses `1` |
| Triples | `[Triple] Screens = Separate monitors`, `Views = Angled`, plus the rig keys | none (needs the racing-row span) | Off by default. Angled rendering is not yet qualified on this candidate |
| Controls, FFB | The owner's existing `[Controls]` from Wheelkit and `[FFB]` choice, preserved | `WheelFfb.dll` only if the owner already uses the force add-on | Physical feel is owner acceptance |

Rules:
- Never copy or modify the owner's originals.
- Keep packs and game files out of git and out of the private ZIP.
- Prefer a relative `TextureBaseFolder` to the sibling Tweaks folder over copying 3 GB, if the reader accepts it. Otherwise copy only the UI banks named in the manifest.

## Deploy target, backup and rollback

Hule owns the receipt. Read-only facts so far:

- LaunchBox `Windows.xml` has no entry that launches `OutRunDesktop.exe`. Its two OutRun 2006 entries run `OR2006C2C.EXE` (Tweaks, Redux).
- The folder `Games/Windows/Outrun 2006 Coast to Coast/OutRun2006 Coast 2 Coast (Decomp)/` holds an Oct 10 `OutRunDesktop.exe` (20919296 B), with `options.ini` set to `reflections=1` only.
- The newer owner route is `E:/Games/OutRunDecompSmoke`: Oct 15 exe, 23613952 B, `widescreen=1`. Its historical update script and rollback pair are in hub `artifacts/outrun-qualified-deployment-20261015/`.

The target is chosen from those records, then backed up (exe, INI, `options.ini`,
saves) before the exact validated artifact is installed. Rollback restores the
backed-up exe and INI.
