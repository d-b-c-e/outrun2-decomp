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

Qualification pending: two tiny real C++ toolchain setup smokes, then separate
CPU GO for a genuine original startup-loader regression, this new fixture and
the existing desktop_controls fixture. No full game build, SDL/UI/device,
owner settings, package, installation, push or live request. Preserve passing
unrelated renderer/controls evidence. Source and fixtures alone do not establish
installed behavior or owner acceptance. STD044 remains partial for other writers.
