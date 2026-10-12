# Whole-file control publication

`host/control_publish.hpp` is the native publisher paired with the unchanged
`SnapshotFile` reader. `control_publish.exe <target>` exposes that same helper to
Python and other hosts, taking exact binary bytes on stdin (at most 4096).
It runs off the game/input thread; it does not load a game or open any device.

Write a new same-directory file with CREATE_NEW, close the write handle, then
open **only that temporary file** with DELETE/SYNCHRONIZE access. Publish with
`SetFileInformationByHandle(FileRenameInfoEx)` and
`FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS`.
There is no target write-open. An already-open snapshot retains its old object;
the next open sees a complete replacement. The rename handle carries no write
access, including during the short interval after publication and before close.

`ReplaceFileW` is unsuitable for this protocol: its target access can collide
with SnapshotFile's deliberate omission of FILE_SHARE_WRITE. A held-reader-only
test previously passed but missed concurrent reader opens. Retrying the writer
cannot repair a reader that already reported a sharing violation. Neither reader
sharing nor Core's refusal policy changes here. There is no delete-then-rename,
in-place truncate, MoveFileEx or ReplaceFile fallback.

## Platform and failure behavior

Minimum API target: **Windows 10 version 1607 (build 14393, RS1), or Windows
Server 2016**, with a filesystem supporting the extended POSIX rename semantics.
The Windows SDK gates `FileRenameInfoEx` on `NTDDI_WIN10_RS1`; the target is set
explicitly for both CLI and fixture. This does not claim testing on the minimum
OS or every filesystem. Local NTFS is the qualification target; remote shares
and filter drivers may refuse or block operations.

Microsoft documents the [Win32 rename structure](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info),
the [POSIX replacement semantics and RS1 structure](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information),
and [ReplaceFile's access requirements](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).

Unsupported rename returns an explicit Win32 error (for example 50/87); the
publisher never selects an older replacement algorithm. Failure preserves the
active name and attempts deletion only of its own newly created sibling. A
cleanup error is appended to the original error and left for the supervisor;
unknown paths are never recursively removed. Bounds limit bytes, not OS call
latency. The caller still owns its deadline and process cleanup. Publication is
atomic visibility, not a promise of power-loss durability or metadata/ACL copying.

Parent directories must already be owned and entry-fence verified by the caller.
The helper does not create directories, follow the target to edit its content,
or provide a hostile-process security boundary. It rejects alternate streams
and oversized payloads. Concurrent publishers remain the caller's coordination
responsibility; Core still enforces nonce and batch order.

## Build and reuse

Build `control_publish` beside `OutRunDesktop`, or independently:

```powershell
cmake -S windows/tests/control_files -B build-control-files -G "Visual Studio 17 2022" -A x64 -T ClangCL
cmake --build build-control-files --config Release
```

The first-loop runner looks for a candidate sibling, a runner sibling, then the
documented `build-xbox-pc/windows/Release` and `build-control-files/Release`
locations, in that order. `--publisher-tool` overrides the lookup. An explicit
missing helper never falls back. Preflight refuses a missing helper before the
lease/process and writes its path/hash into the run plan. Each publication checks
the pinned hash again. The helper is a new standalone tool; Core, the shim DLL,
and their ABI do not change.

## Offline qualification

`windows/tests/control_files` exercises held and genuinely concurrent production
SnapshotFile readers, whole old/new objects with different lengths, unchanged
in-place-writer refusal, actual Pump/Core action delivery, unsupported syscall
refusal, directory refusal, byte bounds, temporary cleanup and released handles.
Its existing reader mutations still check bounds, sharing and leaf handling.
`shim_first_loop/test_publish.py` calls the actual Python runner adapter and CLI
with binary/Unicode-path fixtures, tests missing-helper preflight and refusal,
then removes only its exact newly created fixture files/directories.

No game, device, force or DLL deployment is needed for these checks. A passing
offline publication proof is not a new live input/replay qualification.
