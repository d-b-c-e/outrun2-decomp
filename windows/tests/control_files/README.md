# Win32 command snapshots (offline)

Run `py -3 -B run.py <fresh result directory>`. This uses only new fixture files
and fake input devices. Production `SnapshotFile` reads the actual on-disk
session and batches into the production Pump/Core and checks delivered actions,
TTL release, full-byte hash provenance and terminal completion.

The reader opens a single regular disk-file handle, with no write sharing, and
checks its size before allocating or reading (512 bytes for a session, at most
4096 for a batch). It distinguishes not-found, empty, oversize and access errors.
It refuses leaf reparse points, directories and alternate streams. The enclosing
path must already be an entry-fence-verified directory; this helper alone does
not protect against hostile replacement of a parent directory.

An existing writer refuses the open, and a held snapshot refuses a new writer.
Delete sharing permits `ReplaceFileW`: a held snapshot still reads the complete
old object while a new open reads the complete replacement. The fixture checks
that exact race, not a stat-then-reopen approximation. A polling producer should
publish by same-volume `ReplaceFileW` (or retry a failed atomic rename later),
never truncate the active command file in place. On this machine `MoveFileExW`
with replacement returned access denied while the destination was open despite
delete sharing; `ReplaceFileW` passed. No fallback deletes the active name.

Microsoft documents the relevant [CreateFile sharing rules](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
and [ReplaceFile access requirements](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).
The source uses no unsupported `REPLACEFILE_WRITE_THROUGH` flag.

Byte bounds do **not** bound filesystem latency. Calls run on the control worker
and that worker still needs the reviewed shared-lifetime/OS-thread-deadline host.
The journal sink in this proof is memory-only; identity-bearing persistent logs,
final drain, capture completion, actual desktop wiring and a live game remain
unqualified. No game, device or owner settings are touched.
