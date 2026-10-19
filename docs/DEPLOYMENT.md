# Consolidated Windows build: deployment record

The owner's goal (2026-10-18) was one OutRunDesktop build on the fork's `main`. It
carries every completed Wheelkit-integration and Tweaks-parity change, installed on
the rig with a recoverable backup. This is a private rig deployment, not a public
release or tag. Test evidence is in [ALPHA-CHECKLIST.md](ALPHA-CHECKLIST.md).

## What is installed (consolidated 2026-10-18 21:57:30Z; force correction 2026-10-19 02:17:54Z)

| Item | Value |
| --- | --- |
| Install | `E:/Games/OutRunDecompSmoke` (the owner's maintained decomp install) |
| Executable | `OutRunDesktop.exe`, SHA-256 `00491F111BB03A99C38051133CA37A363B648CF98FAD52FDFB6F6128E115B7C3`, 24028160 B, built from `e4f4d0c` (producer-build-01). Fork `main`'s product source (`src`, `xbox`, `windows`) is identical to it. It replaced `A92CDCB6…` (from `4652a18`), which differs only by the force-model correction. **No runtime has been run on this exe yet** |
| Settings | `OutRunDesktop.ini` SHA-256 `C2D4AD15…`, unchanged by the force update: the owner's file with `[Graphics]` and `[Triple]` sections appended. The owner's controls, saves and unknown keys are kept byte for byte, and there is no `[FFB]` section, so force is off. `session-capabilities.json` `7D713A1A…` carries the build-owned `ffb` marker, from install tuple `BF693C39…`, receipt `OutRunDesktop.install-e4f4d0c-20261019-021500.json`. The earlier `5BB46931…` came from tuple `87B3BE6D…` |
| HD interface | `[Graphics] UITextureReplacement = true`, `TextureBaseFolder = textures-consolidated-20261018-01`. That folder holds 257 UI DDS copied read-only from the owner's Tweaks pack (receipt `FDB2CAF1…`). Scene replacement is off |
| Widescreen | `options.ini` `widescreen=1`, `resolution=2560x1440`, `antialiasing=msaa4` (the owner's existing values) |
| Triple screens | `[Triple] Screens = Separate monitors`, `Views = Angled`, rig geometry from the live Wheelkit profile, `MatchGameFov = true`. There is no curvature warp, so the 1500R curve is not modelled |
| 2D layout | `UIScalingMode = 2` (centred 4:3 UI) |
| Backup | Force update: `OutRunDesktop.exe.e4f4d0c-20261019-021500` (`A92CDCB6…`) and `session-capabilities.json.e4f4d0c-20261019-021500` (`5BB46931…`) beside the game. Consolidation: `.dbce-backups/consolidated-20261018-02` (26 files, journal `deployment.json`). The failed first attempt's `-01` is kept as evidence |
| LaunchBox | Its own entry "OutRun 2006: Coast 2 Coast (Decomp)", ID `cab44f53-70b5-4b72-86f2-b64fc1f4a8b9`, in the Outrun Collection playlist. The force update did not touch LaunchBox, so the entry's Version text still reads "decomp main e2ad163 (exe A92CDCB6468D)" (cosmetic). The Tweaks and Redux entries and their Stream Deck key are unchanged |

**Launching.** Use the new LaunchBox entry, or run
`E:\Games\OutRunDecompSmoke\OutRunDesktop.exe` from its own folder. F6 opens the settings
panel; LB+RB+Menu exits.

**Rolling back.** Close LaunchBox and the game first.
- **The force update only (back to `A92CDCB6`):** inside a serialized lease, run the retained updater from the update package (hub `.local/outrun-alpha-hule-20261018/producer-build-01/metadata/retained-update/Update-QualifiedOwner.ps1`, SHA-256 `9B8CB3DF…`) with `-Action Rollback -Game E:/Games/OutRunDecompSmoke -Package <that folder> -TupleSha256 BF693C39BE805B9B6CB17837241A9245661EFDE8D629646553860DA3ED01DE44 -Tag e4f4d0c-20261019-021500` and the lease path and token. It restores the backed-up exe and capabilities and leaves the INI alone.
  - It also refuses unless the INI still equals the install-time `C2D4AD15`. Since the owner's F6 save on 2026-10-18 the INI is `F1CB0F84`, so this scripted rollback now refuses.
  - Manual fallback, with the game closed: copy `OutRunDesktop.exe.e4f4d0c-20261019-021500` over `OutRunDesktop.exe` and `session-capabilities.json.e4f4d0c-20261019-021500` over `session-capabilities.json`. Then check `A92CDCB6…` and `5BB46931…`, and set the e4f4d0c receipt's `state` to `rolled-back`, as the updater would. The 4652a18 receipt then again owns `A92CDCB6`/`5BB46931`.
- **The whole consolidation:** run the deployer's `-Action Rollback` (hub `.local/outrun-alpha-hule-20261018/Deploy-Consolidated.ps1`, SHA-256 `A64A9483…`) inside a serialized lease. It restores the backed-up exe, INI and capabilities through the retained updater and removes `textures-consolidated-20261018-01`. It refuses if an owner file has changed since the install.
- **The LaunchBox entry:** run `scripts/add-outrun-decomp.py --root <LaunchBox root> --rollback Data/Platforms/Windows.xml.bak-outrun-decomp-20261018-165729.receipt.json`. This works only while the XML still matches its post-apply hashes. After LaunchBox's next normal exit rewrites the XML, delete the entry inside LaunchBox instead.

## Known limitations and open gates

- **Start-grid wedges (Angled triples).** While the car stands on the start grid, the outer edges of both side panels show black wedges: below the grandstand on the left, and beyond the ground on the right. They fill once the car moves.
  - The same wedges were seen on 2026-10-12, and [TRIPLE-SCREENS](../windows/docs/TRIPLE-SCREENS.md) documents them. With `MatchGameFov = true` the side panels look up to 121° from the heading.
  - Their cause is not proven: missing content, or clipping not yet instrumented.
  - The owner keeps `MatchGameFov = true`. `Planar` views do not show them.
- **Real-route smoke through LaunchBox: open.**
  - During the install slot LaunchBox stayed on "Populating Games" for the whole bounded wait, so no game was launched through it.
  - Its log stops after a Steam play-time sync. `RunLibraryScanOnStartup` and ROM auto-import are on; a startup scan or a hidden prompt are the candidates, neither proven.
- **Force feedback: the correction is installed, and force is off.**
  - The installed INI has no `[FFB]` section, so no force is computed or sent.
  - The correction `e4f4d0c` (Samo; kobe 272265, approach (c)) is installed:
    - The old default model (`outrun-legacy@1`) read car `+0x1D0`/`+0x1D4` as steering and steering rate, but those fields hold speed deltas ([roadmap](../windows/ROADMAP.md) step 4). It is now a software trace-only reference, refused before `WheelFfb.dll` loads even with `Output = wheel`.
    - Wheel output needs an explicit `[FFB] Model = outrun-producer@2` (centering and shift cues from the consumed steering). F6 has a separate next-start model choice, and Output never selects a model.
  - To try force: F6 > force page > Force model = Centering and shift, plus Wheel output On, then restart the game. Feel, polarity, onset and stop are not yet qualified; that is an owner-attended check.
- **Wheelkit 0.6.49 (installed) does not know about the marker yet.**
  - Its "Wheel output at the next start" row still reads as if it works with the default model, while the game now refuses legacy output with a clear F6 status and log line.
  - This fails closed. Wheelkit 0.6.53 (below) fixes the wording once installed.
- **Not yet qualified on this build:**
  - any runtime of `00491F11`; the runtime evidence is from `A92CDCB6` and `289ade5`, which differ from it only in the force path;
  - physical wheel feel and force;
  - saving through F6 and applying it on relaunch;
  - brake response against coasting;
  - a race finish with its records saved and reloaded;
  - the Wheelkit 0.6.53 install (below).

## Deployment history

| Slot | Outcome |
| --- | --- |
| main-build-01 | Build PASS: 19 focused tests, 18 pass, 1 expected skip |
| combined-01 | Configuration proven by log. Visual gate incomplete: the desktop capture covered only the left screen |
| render-deploy-01 | The game's own full-width capture: HD, widescreen and three views. It stopped on the start-grid wedges; later qualified (kobe 270778) with them documented |
| installed-smoke-01 | A journal-save sharing violation (holder unknown); rolled back, owner unchanged. The deployer was fixed to write the journal atomically |
| installed-smoke-02 | **Installed**, readback exact; LaunchBox UI smoke pending. The real `Core\LaunchBox.exe` outlived the stub close and was recovered by a normal close, with the XML unchanged |
| LaunchBox UI smoke 03 | Stopped before entry: the capture provider returned the wrong window. The real-route launch is still open |
| producer-build-01 | Build PASS of the force correction `e4f4d0c`: exe `00491F11`, 7/7 focused tests, exact `ffb` marker. Tuple `BF693C39` assembled |
| producer-install-01 | **Installed** 2026-10-19 (lease 02:17:38Z-02:17:54Z): exe and capabilities only, a new receipt, and exactly one matching installed receipt. INI, saves, assets and LaunchBox are unchanged. No game run |

## Source coverage

What `main` contains:
- the cumulative branch `54b2c46`, angled traffic culling `594609f` and the resource fixture `e87299a`;
- every in-source row of the [roadmap](../windows/ROADMAP.md) Tweaks table: texture replacement, music, CD switcher, fast load, pacing and VSync, vibration, level select, Holly, highway sets, compatibility fixes, telemetry RPM, fine steering, login protection, the opt-in network, triples, and the F6 pages;
- from this consolidation: the startup diagnostics line, the toolkit F6 panel fixes and controls overloads (re-vendored from toolkit `5cb9953`, which is not on the toolkit's origin), runner fixes, the HD render-capture harness, and `tools/alpha`;
- the force-model correction `e4f4d0c`, with its capability marker (installed as `00491F11`). Its `src`, `xbox` and `windows` trees are identical on `main`.

Not merged, each with its reason:

| Item | Reason |
| --- | --- |
| Toolkit shim extensions 2–4 and the copied-input bridge | Large divergence. The shim backend is opt-in (Native is the default), and no OutRun consumer uses the extensions. Keep the reviewed pin `d26776a` |
| Async shader compiles | Needs a re-port and a cold-cache A/B on the rig |
| Hitch-probe and triple-projection diagnostics; slow-frame counter `1d9ca5e` | Measurement only; left on their branches |
| Controls rescan `235bbb3`; older request and loading branches | Superseded or already equivalent ([review](UPSTREAM-REVIEW.md), [inventory](BRANCH-INVENTORY.md)) |

OutRun2006Tweaks and the d-b-c-e Tweaks fork with force feedback (`dbce-mods-outrun-2006`)
are a separate architecture: a DLL on the original `OR2006C2C.EXE`. They are not merged,
and their LaunchBox entries are unchanged.

## Wheelkit

Every completed OutRun settings branch is in `dbce-wheelkit` local `main` `1248cb2`.
The installed Wheelkit `0.6.49+c4a4e6d` lacks five OutRun commits:
- the Fine steering and Engine RPM rows (`f7cb7d8`, with `184b4bd` and `10d3c6d`);
- the loading-only VSync mode (`6d36997`);
- borderless cursor visibility (`5dd4219`).

The decomp reads all of these keys, so they can be set in `OutRunDesktop.ini` or F6 until then.

The private successor `0.6.52+0c1c24f` carries only those five commits.
- Qualified: 278/278 focused tests; the app publish and the 69-page and 109-page smoke checks passed.
- Package `5F3A3C63…`; evidence in hub `artifacts/wheelkit-outrun-0652-vunu-20261018`.
- **It is not installed.** The upgrade waits for the owner to close Wheelkit normally, then uses Wheelkit's own installer route.

The proposed consolidated successor is `0.6.53+7b801ac` (Vunu, on the same `0c1c24f` base). It adds the force UI that matches the correction.
- With the build-owned `ffb` marker, trusted only through the hash-bound exe/capabilities/receipt tuple, it shows the model choice and marks Legacy as trace only. It also refuses `Output = wheel` without `outrun-producer@2`.
- Without the marker, as on the current install, it behaves as before.
- Tests: 262/262 focused tests at `8a5f634`, plus one focused case for the `7b801ac` hardening.
- **Packaged (2026-10-19, lease 02:12:46Z-02:14:40Z):**
  - `Wheelkit-0.6.53-private-7b801ac22b23-win-x64.zip`, SHA-256 `1405DB3234878B123E17EAB3B2231C86A5F5C9B02008A4EA82E962F7AD3400AD`, 518/518 members verified. App `0.6.53+7b801ac`.
  - The 69-page and 109-page smoke checks passed.
  - Evidence: hub `artifacts/wheelkit-outrun-0653-vunu-20261018/package-01`.
  - Not yet seen: the marker-present force UI (the generic smoke does not cover it).
- **Not installed.** It is preferred over 0.6.52 (whose ZIP `5F3A3C63…` is unchanged and stays the fallback). It waits for the owner to close Wheelkit normally.
