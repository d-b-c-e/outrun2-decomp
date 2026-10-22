# Standalone controls helper: resolved private packaging input

Resolution date: 2026-10-22. This supplies the engineering input for
`01M5JA1EC75XE3HKXAJC5A0WZ0` and packaging successor
`01M5JA1EC75XE3HKXAJC5A0WZ1`. Select the existing qualified **6cb66313d9e0**
helper below. No owner path preference, new helper, rebuild or installer is
needed to make that selection. Parent acceptance and OutRun package delivery
remain separate.

## Exact existing artifact

Private input, outside this repository:

```text
E:/Source/dbce-project-mgmt/artifacts/shared-helper-hct-outrun-20261015/delivery-r1/helper/StandaloneModSetup-private-6cb66313d9e0-win-x64.zip
```

| Identity | Exact value |
| --- | --- |
| Producer source | `6cb66313d9e0cd1e0cea41b49827feb992d73c2c` |
| ZIP SHA256 | `DFA510EF8DBAD597A710CF6DD7D8C3B85C4F18B15B6A51AFE561E12F5FC3221E` |
| `bundle-manifest.json` SHA256 | `635A8703F86384CAEA394B91A2AE8B05E369FD10234546AF4D2D185996DBA395` |
| Membership | 528 ZIP members: 527 payload files plus the manifest; no extra members |
| Platform | Self-contained `win-x64`; no full Wheelkit installation required |

The adjacent `.zip.sha256` agrees. Read-only inspection on the resolution date
rehashes the ZIP and every manifest-listed member, checking both size and SHA256;
all agree, with no unlisted members. Important member pins are:

| Member | SHA256 |
| --- | --- |
| `DbceModSetup.exe` (GUI) | `ED15134845BDEB1C578331A1F0CFDA2B777491F68F6C85A10C9384E6A68FED3D` |
| `DbceModSetup.dll` | `39AB4422924C3A731A0B243A4EE98E01C926C7E75C4A6ED471B93542CE886635` |
| `Wheelkit.StandaloneSetup.exe` (CLI) | `031E4175EE8391EFE589E264635D68472FB51F9F374F27C14DB638E179454541` |
| `Wheelkit.StandaloneSetup.dll` | `9AC9651E2EB355A33493F96F18BB997B454610818396D42FFF589481C1C9C09D` |
| `Wheelkit.Core.dll` | `8DB66BC390A53C6374017D300A03A36428A04AA54B065A6B3F619D449B066DDD` |
| `Start-ModSetup.ps1` | `130B7FA3774DAB92B0C0E52761432AD2BE25517FB524C54FDC1B59581D5261B4` |
| `catalog-v6.json` | `23E4F87B2D1DF574DF8B1374D2B8FEF3E344CB054CEC59136AFB5A4551C861CD` |

Provenance is the retained toolkit handoff
`docs/2026-10-15-shared-helper-delivery.md`, the manifest's producer identity,
and hub `artifacts/shared-helper-hct-outrun-20261015/delivery-r1/publish.log`.
That log records one GUI and CLI publish from the named producer checkout.
The retained `helper/standalone-build-6cb66313d9e0/{gui,cli,stage}` managed
outputs match the archived Core, GUI and CLI assembly hashes above. This is
retained build provenance, not a new reproducible-build claim.

Under the same hub artifact root, `delivery-r2/delivery-tuple.json` has SHA256
`A37F94EED91669EBB70B8732ECD3ACE3906D964EAB3010B4FA74D9FDFE480316`;
`delivery-r2/result.json` has SHA256
`A4CF7A2004E230A8B59B107FC380AD82F3813F34A8A5332C74F25A4369734C47`.
Both were rehashed unchanged. The existing qualification retains 1,131 integrity
checks and 163 continuation checks, including extracted OutRun CLI Preview,
explicit Apply and zero-change second Preview. The original PowerShell alias
failure remains in delivery-r1; delivery-r2 completed only the pending CLI work.
Do not republish, relabel an older ZIP or repeat that qualification for this input
resolution. The older source-only OutRun handoff's pending-publication statement
is superseded by this specific delivery evidence.

## Capability and installation admission

The qualified OutRun fixture uses the following existing private inputs under
`E:/Source/dbce-project-mgmt/artifacts/outrun-qualified-deployment-20261015/`:

| Input | Relative path | SHA256 |
| --- | --- | --- |
| Executable | `run-02/retained-update/OutRunDesktop.exe` | `341BC049E4CEF0DBAAF7303AEB083D122AA0FEA19CC58D47C57FA48E174AF882` |
| Capability | `run-02/retained-update/session-capabilities.json` | `0F41E1FDC700446DE471350D4CE75F83CBA28FCAC9973BE6D39EE311935C8315` |
| Deployment tuple | `run-02/retained-update/deployment-tuple.json` | `F2AEBC03873DEFE7348ADAAA84BBACE6C8D90DEE1F01415AE0ABDA1A26D37E88` |
| Actual installed receipt | `owner-install-01/OutRunDesktop.install-d86f1fc-20261015-053000.json` | `A2874A80FA72827641D8BB0808163F1FF9F85286388827465BA7C8DF156E5494` |

All four were rehashed unchanged on the resolution date. The tuple identifies
OutRun source `d86f1fc19e9b7788e25cd720a5e1be143f185237`; it is not the current
OutRun source/build. Its historical `standaloneOutRunEntryQualified: false`
predates the helper qualification above and must not be rewritten. The receipt
and fixture profiles are evidence inputs, not distributable owner data.

At the helper producer source, `StandaloneModSetup.Unavailable` and
`OutRunSessionContract.OpenDisplaySettings` admit game ID `outrun-2006-decomp`
only with the selected game's executable, `dbce.session-capabilities@1` whose
`gameBuildSha256` matches those executable bytes, and settings contract
`outrun.display-settings@1` for `OutRunDesktop.ini`. Exactly one matching
`OutRunDesktop.install-*.json` must declare schema `outrun.owner-install@1`,
state `installed`, the same `installedExeSha256` and
`session.capabilitiesSha256`. Missing, changed or ambiguous admission refuses.
The game must be closed. These settings/controls operations do not require the
separate recording companion/finalizer admission.

`OutRunDecompShimControls.ReadCapability` additionally requires
`ini-controls-1`, adapter `outrun-decomp-ini-controls-1`, section `Controls`,
and backend `[Input] Backend=Shim` in that same INI. It accepts the canonical
19-action partition or 20 actions with declared horn, with the complete disjoint
unsupported-action table. Schema2 calibration requires declared schemas `[1,2]`;
older Schema1 declarations retain their limits. The retained capability declares
20 actions, horn, gears 1-6 and `[1,2]`. INI, executable, capability and receipt
dependencies remain pinned across Preview/Apply. Apply selects Shim for the
next launch, preserves unrelated wheel/force/display settings and still requires
AT/MT selection in the game. File qualification does not prove physical controls.

For an already admitted, closed installation, the existing extracted launcher is:

```powershell
pwsh -NoProfile -File <helper-directory>/Start-ModSetup.ps1 -Game outrun-2006-decomp -GameDir <verified-game-directory>
```

This is a setup command, not an installer or packaging command. The helper's
existence does not admit a new executable without matching capability and actual
installation provenance; never copy the retained owner's receipt to bless a
different build.

## Private package route

`tools/alpha/package_candidate.py` supports an optional helper route. It admits
only the exact ZIP and bundle-manifest pins above, all 528 exact members and
every payload size/hash. It refuses unsafe Windows archive paths, aliases,
links, encrypted entries and oversized inputs. It reads every output member
back, retaining the existing deterministic archive and private-only fences.
It reads named inputs only; it never sweeps neighboring build/helper folders.
No game assets, saves, owner INIs, profiles, credentials, install receipts or
force DLLs are added. The helper does not silently enable force.

From a clean committed checkout, supply the **current** separately qualified
EXE, its hash, matching capability bytes/hash and explicit committed launcher.
No current build/capability inputs were supplied for this source change, so no
real candidate archive or installed delivery is claimed. The old 341B fixture
and its owner's receipt must never be substituted to admit a new build.

```powershell
$candidateCommit = git rev-parse HEAD
python -I -B tools/alpha/package_candidate.py --source-root . --source-commit $candidateCommit --exe <current-build/OutRunDesktop.exe> --exe-sha256 <current-build-sha256> --zlib-license <zlib-source/LICENSE> --ogg-license <ogg-source/COPYING> --vorbis-license <vorbis-source/COPYING> --ffmpeg-license <ffmpeg-source/COPYING.LGPLv2.1> --helper-zip E:/Source/dbce-project-mgmt/artifacts/shared-helper-hct-outrun-20261015/delivery-r1/helper/StandaloneModSetup-private-6cb66313d9e0-win-x64.zip --setup-launcher tools/alpha/Setup-Controls.ps1 --capabilities <current-build/session-capabilities.json> --capabilities-sha256 <current-capability-sha256> --output tools/alpha/build/private-candidate.zip
```

All four helper arguments are required together. The capability hash must match
the supplied bytes, and `gameBuildSha256` must match the supplied EXE; settings
and controls must name the supported OutRun contracts. The helper retains its
full action/backend/schema and actual-install admission checks at setup time.
Packaging does not manufacture an installation receipt or install anything.
Keep all generated candidates and evidence under the ignored `tools/alpha/build/`
directory. Create that directory before invoking the command. Existing output
ZIPs are never replaced.

Omit all four helper arguments for the unchanged no-helper route documented in
[the alpha guide](../../docs/ALPHA-READINESS.md). That route adds neither helper,
launcher nor capability and retains the original game/F6 and separately
qualified Wheelkit configuration paths.

## Private placement and controls setup

The package layout is exact and relative to the selected private game folder:

```text
<private-game-directory>/OutRunDesktop.exe
<private-game-directory>/session-capabilities.json
<private-game-directory>/Setup-Controls.ps1
<private-game-directory>/Extras/StandaloneModSetup/bundle-manifest.json
<private-game-directory>/Extras/StandaloneModSetup/Start-ModSetup.ps1
<private-game-directory>/Extras/StandaloneModSetup/DbceModSetup.exe
<private-game-directory>/Extras/StandaloneModSetup/<remaining exact helper payload>
```

Use only an authorized private copy with the owner's legitimately supplied game
data. The existing private installation/update route must place the current EXE
and matching capability in that folder and create its actual installed receipt;
this ZIP is not an installer. Preserve existing game files and configuration.
The exactly one matching `OutRunDesktop.install-*.json` receipt stays beside the
EXE, outside the package; its requirements are above. A writable game directory,
Windows x64, PowerShell 7 (`pwsh`) and a closed game are prerequisites. The
self-contained helper needs no full Wheelkit installation.

From the selected admitted private copy, run:

```powershell
pwsh -NoProfile -File "<private-game-directory>/Setup-Controls.ps1"
```

This entry verifies the pinned helper manifest and every exact helper payload
before handing the selected game directory to the existing `Start-ModSetup.ps1`.
It refuses changed, missing or extra helper files; it does not install, repair,
write bindings itself or bypass the helper's capability/receipt admission.

For a fresh or all-unbound profile, use this standalone helper's device discovery
and binding capture to create a profile, select the intended devices and calibrate
steering/pedals. For an existing profile, use the helper's existing profile import
and review its bindings/calibration rather than replacing it with defaults.
Choose the supported transmission; H-pattern maps gears 1-6, and the game's
AT/MT choice still has to agree. Unsupported actions remain unavailable with
their reasons. Schema2 calibration requires the capability's `[1,2]` declaration;
Schema1-only builds keep their existing limits.

Use **Preview** to inspect the proposed changes and selected game folder. Use
**Apply** only after reviewing them; it uses the existing backup/write/readback
path, preserves unrelated display/wheel/force settings and selects Shim for the
next launch. **Cancel** leaves the preview unapplied; it does not undo an earlier
Apply. Close setup and restart the game to load the saved profile. Current F6
capture edits devices already owned by shared input; it does not establish a
first-device capture route for a fresh/all-unbound profile. Use the helper for
that setup, then F6 for supported edits on the owned devices.

If admission fails, close the game and check that the selected private folder has
the current EXE, matching capability, supported controls contract and exactly one
matching actual install receipt. Use the existing installation/update recovery
route for mismatches; never edit or copy an old owner receipt to make it pass.
If helper integrity fails, restore the exact helper payload from the verified
private package, without mixing versions or adding neighboring files. For a bad
applied profile, close the game and restore the backup reported by the existing
helper, then Preview again. Preserve saved calibration and unrelated settings.

The retained helper qualification and old archive evidence above are unchanged.
This source candidate adds synthetic fixture source for the dependent Claude
game-free checks; it does not repeat old helper publish/CLI qualification.
No real helper execution, game, UI, input/device/force, installation, download,
public distribution or physical acceptance is claimed.
