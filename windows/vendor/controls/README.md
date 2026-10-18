# Vendored: wheel toolkit controls contract

From [d-b-c-e/dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit)
`native/controls/dbce_controls.hpp` at `5cb9953158907c6bcce51b488e19186f4a7ac6f6` (2026-10-18;
controls schema 2, STD-037, plus shape-based `fits` / `normalize` overloads; the old action-name
signatures remain wrappers). The inject headers are unchanged since `4ead5369a77d`. All are MIT
(`LICENSE` here). Do not edit; re-vendor from the toolkit and update this line.

This is a selective copy from toolkit local `master`, not a published origin commit. At this
refresh, tracked and live `origin/master` were `b695fbfcde4dfae4af5f1a0cabe3715ba0cdd428`, 106
commits behind the local pin; those toolkit commits are not published by this refresh. File
contents match the pinned Git blobs, with this repository's normal text checkout line endings.
Hashes below use canonical LF bytes (normalize CRLF to LF before checking).

| File | SHA-256 (LF; first 16) | Role |
| --- | --- | --- |
| `dbce_controls.hpp` | `e7c1a41b4bf4359b` | STD-033 binding, text form, normalization, `[Controls]` section (`ini-controls-1`; schema 2 `cal=` when a reader opts in, STD-037) |
| `dbce_inject.hpp` | `6e1569579053aeca` | test-injection command grammar |
| `dbce_inject_table.hpp` | `1d33f3e85f25c20c` | running injections and their delivery into one device read |

Contract: `docs/controls-contract.md` in the toolkit. The shared vectors
(`native/controls/controls-vectors.txt`) are the toolkit's own tests of these files.
