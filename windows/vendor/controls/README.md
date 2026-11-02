# Vendored: wheel toolkit controls contract

From [d-b-c-e/dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit)
`native/controls/dbce_controls.hpp` at `02c2547461832ed64096045730f52603cc5713f3` (2026-11-02,
reviewed append-only camera actions31..41, controls schemas1/2). The original31 action indexes and
shape/normalization behavior remain unchanged. Native Core/observer source pin is in ../shim.lock.json.
The DLL facade remains ABI2/31/296; no consumer DLL or WheelFfb pin is changed by this source refresh.
The inject headers remain unchanged since4ead5369a77d. All are MIT (`LICENSE` here).
Do not edit; re-vendor from the toolkit and update this line.

Hashes use canonical LF bytes (normalize CRLF to LF):

| File | SHA-256 (LF; first16) | Role |
| --- | --- | --- |
| `dbce_controls.hpp` | `d8056b211b643d0b` | STD-033/037/048 controls plus eleven Digital camera actions |
| `dbce_inject.hpp` | `6e1569579053aeca` | unchanged injection command grammar |
| `dbce_inject_table.hpp` | `1d33f3e85f25c20c` | unchanged injection delivery |

Contract: `docs/controls-contract.md` in the toolkit. The shared vectors
(`native/controls/controls-vectors.txt`) are the toolkit's own tests of these files.
