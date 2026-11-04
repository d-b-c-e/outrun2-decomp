# Startup default settings: preserve existing and competing files

Follow-up to coordinator509233, scoped to the next unowned non-Steam OutRun2006
Decomp STD044 gap. Base main0c894a5 already qualifies ordinary write_key/write_keys
byte preservation and no-op handling; those algorithms are unchanged. The older
primaryb6b3810 view is historical and was not used as the implementation base.

Actual startup load_desktop_settings used fopen(wb) after Ini::load failed. A
file held with GENERIC_READ/FILE_SHARE_WRITE denies another read but permits a
writer, so a failed read could truncate saved settings. Creation also needed to
respect another creator between initial absence and publication. This source
fix leaves existing/uncertain paths untouched and uses the existing flushed,
unique CREATE_NEW staging writer with create-only publication. The ordinary
settings publisher retains its existing ReplaceFile behavior.

Win32 sharing rules: [Creating and Opening Files](https://learn.microsoft.com/en-us/windows/win32/fileio/creating-and-opening-files).
Create-only publication omits MOVEFILE_REPLACE_EXISTING, so an existing
destination wins: [MoveFileExA](https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-movefileexa).
Current Context7 documentation tools were unavailable; official Win32 docs were
used. This changes no initial defaults, controls, FFB/backend or renderer policy.

New test_desktop_startup_settings calls the actual startup loader using only
disposable paths. It covers missing/read-only startup, ordinary/legacy defaults,
existing mixed BOM/line endings/no final newline and saved FFB Off, exact
creation/write ticks/attributes, unreadable-but-writable files, failed publication,
deterministic competing creation, a directory sentinel, and owned-temp cleanup.
The creation publisher uses the same existing function-pointer test-seam pattern
as ordinary settings publication. No renderer, device, game or owner file is used.

## Focused qualification complete

Tested production source `adc90dec5615c62a8453cf5d8225f3985cab60bc`.
Two actual CMake C++ configurations/direct inert G++ links/Ninja dry-runs passed
under CPU509592 in5.851s. The inert outputs were never executed;71 observed
identities/11 actual tool pins. Compiler and native fixture work was separately
admitted. Original and corrected loader were compiled against the same compiler.

| Interval | Actual outcome |
| --- | --- |
| CPU509639 / cpu01 | Build refusal before any fixture: added recipe-Werror promoted existing misleading-indentation warning. Raw diagnostics retained; recipe changed to match target policy, all warnings stay visible. |
| CPU509682 / cpu02 | Genuine original loader regression RED: original48-byte sentinel became998 bytes after a denied read. Fixed startup fixture19 checks PASS. Aggregate desktop_controls compile then refused on unchanged MSVC-specific ifstream(wstring) in test_injection.cpp; full aggregate never ran. |
| CPU509803 / cpu03 | Remaining-only include wrapper failed link: MinGW PE still required symbols from unused controls/injector sections despite GC. This was a harness assumption; no fixture ran or product defect claimed. |
| CPU509844 / cpu04 |82 existing settings writer/graphics-preset checks PASS in5.743s. Only these two actual functions were byte-extracted into a minimal harness, with source/span hashes verified. No injector, game/device or substitute writer was run. |

The final interval reused the original RED, fixed startup19 and setup passes;
none was repeated. Existing unrelated controls/renderer passing evidence is
preserved. The broad MSVC controls aggregate remains unrun in this batch.
Baseline misleading-indentation and fixture initializer warnings remain visible
in raw outputs; this task did not silently repair unrelated source or claim a
warning-free full build.

All intervals closed normally with own jobs empty/held handles released and
independent birth-aware ALL-image scoped OS audits BEFORE exact named lease
release. Observed counts6/31/10/12 for cpu01..04. No forced termination or custody
remains. Full outer packaged-pwsh descendant-job coverage stays unqualified.

Required retained evidence (outside the finished worktree):
`E:/Source/game-modding/dbce-mods-outrun-2006/build/startup-settings-qualification-20261103`.
All155 raw/source files were SHA-256 copied and verified,4,205,882 bytes.
`relocation-manifest.json` SHA256
`53B9BDE310FDA8DE68FA18B42CB567815500CFE6A1C8EA062B9A7D897C74DB06`
maps historical absolute paths to preserved archive files without rewriting raw
evidence. Original refusals, qualifiers/plans, real toolchain checks, fixture
executables/output and exact tested source are retained. See qualification.json
there for full result/audit/binary hashes and evidence limits.

No full game build, SDL/UI/device, owner settings, package, installation, push
or live request. Source and fixtures alone do not establish installed behavior
or owner acceptance. STD044 remains partial for other product writers. The
historical primaryb6b3810/renderer sidecar and frozen runtime candidates remain
untouched. This source increment is local only in the public repository.
