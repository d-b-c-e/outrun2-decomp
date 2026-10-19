# Vendored: wheel toolkit native panel and its Dear ImGui

From [d-b-c-e/dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit) `native/` at
`28303111df0ad83be77c46e7587dce7c6e534ca2` on `samo/panel-preference-save-guard-20261019` for
`panel/dbce_panel.cpp`, `panel/dbce_panel.h`, `panel/dbce_panel_preferences.hpp` and
`panel/test/test_preferences.cpp`; `panel/dbce_panel_input.cpp` stays at
`5cb9953158907c6bcce51b488e19186f4a7ac6f6` (2026-10-19); the other files remain at `bb46a5e` / `ee12f00`, byte for byte, in the same layout (the panel
includes `../third_party/imgui/`). The toolkit code is MIT (`LICENSE` here); Dear ImGui 1.91.6 WIP is MIT
(`third_party/imgui/LICENSE.txt`, provenance in `third_party/imgui/PROVENANCE.md`). Do not edit; re-vendor from the
toolkit and update this table together. Only the DX11 backend is copied, and not the toolkit overlay (`dbce_overlay_*`):
the desktop build draws the panel from its own renderer (docs/F6-PANEL.md).

The refreshed files include foreground-root ownership before releasing cursor clipping (`6e69cc2`),
Stop FFB persistence-feedback ordering (`10d6c63`), Help report actions before repository text links
(`c89e382`), and the additive passive status-line API (`3200202` / `a00d7b0`). The status-line API is
unused by this consumer; this refresh does not enable recording or other host behavior.

The panel guard successor is local at the exact branch/pin above, based on toolkit local master
`5cb9953`. Its initial four-line ancestor `c6ac539` was briefly pushed to that private branch;
the coordinator removed the remote ref with an exact lease and verified its absence (hcom277232).
This ref removal does not guarantee object purging. `2830311` has not been pushed; toolkit
master, tags and releases were unchanged. At the previous refresh, tracked/live origin/master
was `b695fbf`, 106 commits behind the local base. All four successor files match their pinned
Git blobs exactly (LF bytes, preserved by this repository's `-text` vendor attributes).
The optional guard holds preference persistence while a binding draft is open; navigation
remains available and a null guard preserves existing consumers. Compilation, disposable-fixture
execution and rendered acceptance remain separate checks.

| File | SHA-256 |
| --- | --- |
| `panel/dbce_frame_stats.hpp` | `50e5e8e37c6d2b7477a46ca12dedde31d9135be080df689e39503104027d64be` |
| `panel/dbce_panel.cpp` | `d46ec3109bb579e3d91ae3849eff329198c22d4a25855083c00fe3513b975db5` |
| `panel/dbce_panel.h` | `c4835d4833d6c8590b4af94c6f139645b80ab771a82cc69e61f602c04afe7144` |
| `panel/dbce_panel_preferences.hpp` | `9e3f5fef26b76a41e2257510fc33daeae45a244fc599f931983c0df0e976128a` |
| `panel/test/test_preferences.cpp` | `5349cb53b3a185d9c5d70d99d994432421b623d18b2f7feb8414f30039fd1263` |
| `panel/dbce_panel_input.cpp` | `0b4e0067cfeafb3ff1ae57fd993520e8d72cee94df0b8cdc85803ae1da287ab2` |
| `panel/dbce_panel_input.h` | `e0ea85167608ad9a1d5ef2af7837fdea9dee10fb6bce98f2542cd97b42809a39` |
| `third_party/imgui/backends/imgui_impl_dx11.cpp` | `5d76d6a60a6aafa4f41be30e2637e82d76dce65de796ff638604b48c9cf8917b` |
| `third_party/imgui/backends/imgui_impl_dx11.h` | `6c62cc541e332b75c3de607188dc0b5f4916f68804cd10e3aa11200ce8f8d144` |
| `third_party/imgui/imconfig.h` | `b1c34ce402feed3cfa13192aa7665986feb49ff48cac463713cdb83c75f97ac7` |
| `third_party/imgui/imgui.cpp` | `f2b1c128f06e55a64d342b903efee8c8c82ddcd4da18035afef61e58bf45e98f` |
| `third_party/imgui/imgui.h` | `ccf915ea00ffd085bb650dec80edc01024b54e37fd6e2e7f812657eddf17ebef` |
| `third_party/imgui/imgui_draw.cpp` | `f5873ef053aebb42733fddfff6e49e76e3776f8ba356c4dde651c2e9e43207ea` |
| `third_party/imgui/imgui_internal.h` | `9866cdb61f40d0afdb201818c7e0f1c2bd3a197cefe07a463dba875863ccf8a7` |
| `third_party/imgui/imgui_tables.cpp` | `48161b4bb4223f905f679898b663c1912e2fd1ae965497902b1ed95bf5557a95` |
| `third_party/imgui/imgui_widgets.cpp` | `b635eb8a127bea9eb6a6bfc35baaa1f9f46cdf193a9e8c67e94086c405d8f439` |
| `third_party/imgui/imstb_rectpack.h` | `bb53504995e983d54b1ae06ea727f0b39647e5e205b4bf7da01343953974951c` |
| `third_party/imgui/imstb_textedit.h` | `d9deebe52c76276c42ca030e4ab92ea145552d2aa7e92fd7dea27ffc64eb7166` |
| `third_party/imgui/imstb_truetype.h` | `941a95c9f36771aab45062e977596785aa9cfaa508a523f6c34ace84b5916780` |
| `third_party/imgui/LICENSE.txt` | `44abf9b2498d127928abb6e23b4cda06f6ec3577b34b0c9d4d0672f38b3d6293` |
| `third_party/imgui/PROVENANCE.md` | `c447e707972b96e9652db181be258d59c5464fbad33042862533e4f17d0be0f6` |
