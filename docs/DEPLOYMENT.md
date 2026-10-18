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
| HD interface | `[Graphics] UITextureReplacement = true`. `TextureBaseFolder` must name a folder inside the install: the reader refuses `..`, drive letters and leading slashes (`window_desktop.cpp:640-646`) | The owner's existing pack at `Games/Windows/Outrun 2006 Coast to Coast/OutRun2006 Coast 2 Coast (Tweaks)/textures/load`: 32 UI banks, 257 DDS (about 2.5 GB), including the frontend, HUD and loading banks. Provenance: hub `artifacts/outrun-ui-textures-20261012/pack-copy-manifest.csv`, and `artifacts/outrun-course-loading-runtime-20261016` for START | Off by default; no decomp folder has `textures/` |
| HD scene textures | `[Graphics] SceneTextureReplacement = true` (Tweaks enables both) | The same pack's scene banks | Off; memory cost to be measured in the smoke |
| Widescreen | `options.ini` `widescreen=1`, `resolution=2560x1440` | none | Source default 4:3. The owner's `E:/Games/OutRunDecompSmoke` already has `widescreen=1`; the LaunchBox "(Decomp)" folder does not |
| 2D UI layout | `[Graphics] UIScalingMode`: decomp default `2` (centred 4:3). Tweaks' default `1` widens the HUD but has documented gaps ([UI-SCALING](../windows/docs/UI-SCALING.md)) | none | Keep `2` unless the owner chooses `1` |
| Triples | `[Triple] Screens = Separate monitors`, `Views = Angled`, plus the rig keys | none (needs the racing-row span) | Off by default. Angled rendering is not yet qualified on this candidate |
| Controls, FFB | The owner's existing `[Controls]` from Wheelkit and `[FFB]` choice, preserved | `WheelFfb.dll` only if the owner already uses the force add-on | Physical feel is owner acceptance |

Rules:
- Never modify the owner's originals; read them only.
- Keep packs and game files out of git and out of the private ZIP.
- Copy into the install's `textures/load` from the Tweaks pack (read only), with no link or path bypass:
  - the 209 DDS (10 banks, 1,965,124,564 B) named in the historical `pack-copy-manifest.csv`;
  - the remaining 48 compatible UI files, enumerated by exact name and recorded in a supplemental copy/hash receipt during staging;
  - the newer START assets that their completed provenance covers.
  Each file's inclusion and provenance is recorded as it actually is.

First deployment choices:
- UI replacement on; scene replacement off. Scene replacement is the next toggle once the smoke shows memory headroom.
- `widescreen=1` at 2560x1440 (already set in the target).
- `[Triple] Screens = Separate monitors`, `Views = Angled`, with the rig keys. The live Wheelkit rig profile is 3x2560x1440, separate, 32-inch, eye 660 mm, sides 70°, bezel 8 mm, 1500R curve, mapped through the existing geometry mapper. The decomp has no curvature warp, so no curved-rendering claim is made. `MatchGameFov` stays `true` (the default; the eye is matched to the game's FOV). The owner's recorded feedback of 2026-10-12 (`3dd434e`, archived branch `claude/triple-projection-f6`) is that a narrowed FOV is not wanted and `true` stays the default. `false` (the physical eye) is an owner choice for a later attended check. Fall back to `Planar` if the smoke shows an angled defect.
- `UIScalingMode` stays `2`.
- The owner's `[Controls]` (native Moza triple bindings), saves and unknown keys are preserved byte for byte.

## Deploy target, backup and rollback

Hule owns the receipt. Read-only facts so far:

- LaunchBox `Windows.xml` has no entry that launches `OutRunDesktop.exe`. Its two OutRun 2006 entries run `OR2006C2C.EXE` (Tweaks, Redux).
- The folder `Games/Windows/Outrun 2006 Coast to Coast/OutRun2006 Coast 2 Coast (Decomp)/` holds an Oct 10 `OutRunDesktop.exe` (20919296 B), with `options.ini` set to `reflections=1` only.
- The newer owner route is `E:/Games/OutRunDecompSmoke`: Oct 15 exe, 23613952 B, `widescreen=1`. Its historical update script and rollback pair are in hub `artifacts/outrun-qualified-deployment-20261015/`.

**Decision (kobe 269317): the target is `E:/Games/OutRunDecompSmoke`.**
- Hule's read-only receipt is hub `.local/outrun-alpha-hule-20261018/deployment-readiness.json`. The current exe is `341bc049…`, matching the installed `d86f1fc` record and its capabilities pair. 16 baseline files are pinned, including saves and companions.
- Before install, a new backup goes to `.dbce-backups/consolidated-20261018-01`; the existing rollback receipts are kept.
- The session-capabilities/install tuple is generated fresh from the new build with the existing generator, not by hand-editing the old updater's pinned hashes.
- Rollback restores the backed-up exe, INI, `options.ini` and capabilities, and removes the staged `textures/load`.

**Launch route.** The real launch path gets its own LaunchBox entry, added with
the Launchbox-Racing tooling `scripts/add-outrun-decomp.py`. It is published alone on
`d-b-c-e/LaunchboxRacingRedux` branch `demo/outrun-decomp-launch-route`, head `d1bd873`
on `origin/master` `a04a6fa`. The branch changes only this script and its offline test
`tests/test-add-outrun-decomp.py`. Run it against the live root with
`--root <LaunchBox root>`; the live checkout's own files are not changed.
- It registers "OutRun 2006: Coast 2 Coast (Decomp)", runs `OutRunDesktop.exe` in the target via a relative `ApplicationPath`, and adds it to the Outrun Collection playlist.
- It is a dry run by default.
- `--apply` needs `--expect-sha256` (the deployed exe). It refuses while LaunchBox or BigBox runs, and refuses a duplicate title or resolved target. The process check fails closed: the tasklist CSV is parsed strictly, and any unreadable, malformed or empty list refuses.
- It records both XML backups and their post-apply hashes in the receipt before writing either file.
- `--rollback RECEIPT` restores each XML file only while it is in its backup state or its exact post-apply state, so a partial write is recoverable and later edits are kept. It removes only the media the run created.
- The Tweaks and Redux entries and their Stream Deck key are unchanged.

**Wheelkit.** Vunu's isolated successor of the installed `c4a4e6d` carries only
the five OutRun commits, under a unique private version. It is installed through
Wheelkit's own route, with its own qualification and rollback.
