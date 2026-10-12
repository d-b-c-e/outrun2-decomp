# Vendored: wheel toolkit WheelFfb C ABI

From [d-b-c-e/dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit)
`native/wheelffb/include/` at `d4683f9` (2026-10-12, native 0.11.0: `GetReadSlotCaps` and `read_slot_caps.h`), byte for
byte, MIT (`LICENSE` here). Do not edit; re-vendor from the toolkit and update this line and the pins together.

| File | SHA-256 | Role |
| --- | --- | --- |
| `wheelffb.h` | `cc574711c19c6fffb1cc6087840c3d748e221ee0b680ea441d210698aa8e6d76` | the export list, `WheelFfbApi` and the runtime loader (`WheelFfb_Load`), used by `windows/source/ffb_device` and `windows/shim/wheelffb_bridge` |
| `external_ffb.h` | `c081ce91cf7bfc09aba65892eb8daa2a41c3387b074b2a657244930faa1c2745` | included by `wheelffb.h` (the external-owner ABI; not used here) |
| `read_slot_caps.h` | `a8bf24478b3d427a28c8d610906ba2aeca80dae70a58e39ef7534ab2659b6fb7` | included by `wheelffb.h`: `ReadSlotCapsV1`, what `GetReadSlotCaps` reads back (the shim's WheelFfb bridge) |

The DLL itself is not vendored: `ffb_device` loads `WheelFfb.dll` (x64) from the game folder at run time, only when
`[FFB] Output = wheel`, and refuses one older than 0.9.0.
