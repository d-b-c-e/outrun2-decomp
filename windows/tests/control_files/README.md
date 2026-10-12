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
Publishers now use the shared `control_publish.hpp` helper: FileRenameInfoEx with
REPLACE_IF_EXISTS and POSIX_SEMANTICS, after closing the sibling's write handle.
The original held-reader ReplaceFileW test missed its incompatible target write
access during concurrent opens. Two actual SnapshotFile readers now race 2000
whole-file replacements; a separately held snapshot survives 400 replacements.
No reader sharing is relaxed and no fallback deletes the active name. See
[publisher requirements and failure behavior](../../docs/CONTROL-PUBLISHER.md).

Microsoft documents the relevant [CreateFile sharing rules](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
and [ReplaceFile access requirements](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).
The publisher uses neither ReplaceFileW nor its unsupported WRITE_THROUGH flag.

Byte bounds do **not** bound filesystem latency. Calls run on the control worker
and that worker still needs the reviewed shared-lifetime/OS-thread-deadline host.
The journal sink in this proof is memory-only; identity-bearing persistent logs,
final drain, capture completion, actual desktop wiring and a live game remain
unqualified. No game, device or owner settings are touched.
