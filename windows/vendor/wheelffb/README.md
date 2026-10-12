# Vendored: wheel toolkit WheelFfb C ABI

From [d-b-c-e/dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit)
`native/wheelffb/include/` at `6d90b4540ce4277d40ff0327a14977284c077c45` (2026-10-10; unchanged at `ee12f00`), byte for
byte, MIT (`LICENSE` here). Do not edit; re-vendor from the toolkit and update this line and the pins together.

| File | SHA-256 | Role |
| --- | --- | --- |
| `wheelffb.h` | `2f41d69972ac884e84df2a0abeac7e821d358955a314d9e67f633e36d564f9dd` | the export list, `WheelFfbApi` and the runtime loader (`WheelFfb_Load`), used by `windows/source/ffb_device` |
| `external_ffb.h` | `c081ce91cf7bfc09aba65892eb8daa2a41c3387b074b2a657244930faa1c2745` | included by `wheelffb.h` (the external-owner ABI; not used here) |

The DLL itself is not vendored: `ffb_device` loads `WheelFfb.dll` (x64) from the game folder at run time, only when
`[FFB] Output = wheel`, and refuses one older than 0.9.0.
