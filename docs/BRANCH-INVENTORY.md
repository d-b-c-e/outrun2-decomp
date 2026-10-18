# Preserved branch inventory

Snapshot: October 18, 2026. The audit found 154 local commits not reachable from
any fetched origin branch before consolidation. All 107 existing worktrees were
clean. Only committed code/documentation history is published; ignored builds,
game files and private evidence remain local.

The consolidated branch is the review entry point. Archive refs preserve original
commit identities and experiments, including changes already incorporated under
different hashes. An archive is **not** a recommendation to merge its entire tree.

## Archive heads

These heads collectively preserve local side histories outside the cumulative
branch. Existing published branches remain intact.

| Archive ref | Original tip |
| --- | --- |
| `archive/2026-10-18/claude/desktop-graphics-parity` | `235bbb3d6d6d1077867316534056234a9728ebf7` |
| `archive/2026-10-18/codex/art-shim-profile-info` | `5b96ba8f3d649f353e2aad1aac8342f4334e0ad0` |
| `archive/2026-10-18/codex/shim-publisher-fix` | `b173a4887a7c317146e82fefc80c88c97efd3088` |
| `archive/2026-10-18/guru/outrun-countdown-fixture` | `258d3ce2721e41c5ad6ba0c61a431caec295b63e` |
| `archive/2026-10-18/kiki/loading-hd-retail-fixtures` | `d70995fe07e4bd6900d59fb310098ea2f18e8b62` |
| `archive/2026-10-18/kiki/outrun-prompt-identity-off-20261016` | `551a4bc26447f89ea714a7e11d1dd345d661847d` |
| `archive/2026-10-18/kiki/outrun-request-car-draw` | `7ef49eb278d12417d7d0c1a366ce19ad8dcf68f8` |
| `archive/2026-10-18/kiki/outrun-request-hud` | `76fdd72b6781431278a385c3160561bb2acbd9d3` |
| `archive/2026-10-18/livo/outrun-level-menu-render-gate-20261015` | `95a84bdcf20ab4efb19787c5125f1b9082edbfc4` |
| `archive/2026-10-18/lola/outrun-native-route-observation-20261016` | `9b4c9e368356865d89cd7b8dc538342a4bb213d3` |
| `archive/2026-10-18/niru/outrun-47ec00` | `ab4f5cce3c40ac01e1e9974a77a72029b2ff4c3a` |
| `archive/2026-10-18/puma/outrun-presentation-sound-20261017` | `4428bfba6ba7e81a46af1306a8bb4c0c88ce7b8c` |
| `archive/2026-10-18/valo/outrun-449470` | `3fc074d5c841a1b2dbe132149976298b394148ca` |

## Previously unpublished branch tips

Each row is covered by the consolidated history or by the indicated archive.
The commit count is per branch and overlaps other rows; do not add the counts.

| Local branch at snapshot | Tip | Unpublished ancestry | Preserved in |
| --- | --- | ---: | --- |
| `claude/desktop-graphics-parity` | `235bbb3d6d6d` | 1 | `archive/2026-10-18/claude/desktop-graphics-parity` |
| `codex/art-shim-bridge` | `f38b85566251` | 5 | `archive/2026-10-18/codex/art-shim-profile-info` |
| `codex/art-shim-profile-info` | `5b96ba8f3d64` | 6 | `archive/2026-10-18/codex/art-shim-profile-info` |
| `codex/shim-publisher-fix` | `b173a4887a7c` | 6 | `archive/2026-10-18/codex/shim-publisher-fix` |
| `dine/outrun-periodic-source-20261016` | `ee3d28650091` | 34 | consolidated history |
| `guru/outrun-countdown-fixture` | `258d3ce2721e` | 1 | `archive/2026-10-18/guru/outrun-countdown-fixture` |
| `guru/outrun-request-base` | `90f41bbcc115` | 49 | `archive/2026-10-18/niru/outrun-47ec00` |
| `kiki/loading-hd-retail-fixtures` | `d70995fe07e4` | 3 | `archive/2026-10-18/kiki/loading-hd-retail-fixtures` |
| `kiki/outrun-prompt-identity-off-20261016` | `551a4bc26447` | 1 | `archive/2026-10-18/kiki/outrun-prompt-identity-off-20261016` |
| `kiki/outrun-request-car-draw` | `7ef49eb278d1` | 39 | `archive/2026-10-18/kiki/outrun-request-car-draw` |
| `kiki/outrun-request-hud` | `76fdd72b6781` | 40 | `archive/2026-10-18/kiki/outrun-request-hud` |
| `livo/outrun-level-menu-render-gate-20261015` | `95a84bdcf20a` | 3 | `archive/2026-10-18/livo/outrun-level-menu-render-gate-20261015` |
| `lola/outrun-character-appearance-20261016` | `f5d47c802a18` | 13 | consolidated history |
| `lola/outrun-fallback-record-binding-20261016` | `06470a54f710` | 3 | consolidated history |
| `lola/outrun-horn-setting-20261016` | `de5a8d67b1a5` | 4 | consolidated history |
| `lola/outrun-hud-toggle-setting-20261016` | `e19a60363df5` | 5 | consolidated history |
| `lola/outrun-menu-settings-20261016` | `a9421d4d9006` | 11 | consolidated history |
| `lola/outrun-music-format-settings-20261016` | `715d0f860373` | 6 | consolidated history |
| `lola/outrun-native-help-textlinks-20261016` | `77f69376fd3c` | 2 | consolidated history |
| `lola/outrun-native-route-batch-20261016` | `6a9a43dbade9` | 67 | consolidated history |
| `lola/outrun-native-route-observation-20261016` | `9b4c9e368356` | 1 | `archive/2026-10-18/lola/outrun-native-route-observation-20261016` |
| `lola/outrun-screen-edge-setting-20261016` | `1b163e778ac9` | 9 | consolidated history |
| `meze/outrun-triple-car-cull-20261017` | `c0e246d3d2df` | 64 | consolidated history |
| `niru/outrun-47ec00` | `ab4f5cce3c40` | 53 | `archive/2026-10-18/niru/outrun-47ec00` |
| `niru/outrun-5217c0` | `c76724a6a242` | 52 | `archive/2026-10-18/niru/outrun-47ec00` |
| `nova/outrun-fast-load-mode1-20261017` | `6a9a43dbade9` | 67 | consolidated history |
| `nova/outrun-network-consumer-20261017` | `81572d54806b` | 65 | consolidated history |
| `nova/outrun-triple-car-cull-20261017` | `594609f1957e` | 65 | consolidated history |
| `puma/outrun-presentation-sound-20261017` | `4428bfba6ba7` | 51 | `archive/2026-10-18/puma/outrun-presentation-sound-20261017` |
| `review/upstream-consolidated-20261018` | `54b2c466cc15` | 72 | consolidated history |
| `todo/outrun-c2c-goal-spacing-20261016` | `224ac7ea5c0a` | 28 | consolidated history |
| `todo/outrun-calibration-reconciliation-20261017` | `7e9ba455aac9` | 50 | consolidated history |
| `todo/outrun-cd-f6-settings-20261016` | `4b52b7d51829` | 35 | consolidated history |
| `todo/outrun-cd-startup-shuffle-20261016` | `f2a3150f7964` | 33 | consolidated history |
| `todo/outrun-cd-stick-combos-20261016` | `d1095960f7b4` | 31 | consolidated history |
| `todo/outrun-compatibility-settings-20261016` | `1eeb376b765a` | 19 | consolidated history |
| `todo/outrun-cursor-visibility-20261016` | `e6accd73ea97` | 37 | consolidated history |
| `todo/outrun-fast-load-setting-20261016` | `d7775b88e922` | 26 | consolidated history |
| `todo/outrun-framerate-boundary-20261016` | `701235e4b5b7` | 38 | consolidated history |
| `todo/outrun-gamepad-vibration-20261017` | `03b66ac64eb6` | 61 | consolidated history |
| `todo/outrun-general-presentation-20261017` | `0551df85649b` | 55 | consolidated history |
| `todo/outrun-graphics-compatibility-20261016` | `0b67b28e7504` | 21 | consolidated history |
| `todo/outrun-highway-setting-20261016` | `06e3a8c6acf6` | 17 | consolidated history |
| `todo/outrun-holly-availability-20261016` | `e0b842eb5041` | 15 | consolidated history |
| `todo/outrun-level-select-controls-20261017` | `528f6e11d0ab` | 47 | consolidated history |
| `todo/outrun-network-consumer-20261017` | `81572d54806b` | 65 | consolidated history |
| `todo/outrun-network-parity-20261017` | `3ba9081d1850` | 63 | consolidated history |
| `todo/outrun-pacing-vsync-20261017` | `bfaba433d5c2` | 58 | consolidated history |
| `todo/outrun-scene-extraction-20261016` | `f6d625878110` | 41 | consolidated history |
| `todo/outrun-startup-monitor-20261017` | `8ab44dfd2c6d` | 49 | consolidated history |
| `todo/outrun-texture-cache-20261017` | `c6f3bbd5db8f` | 45 | consolidated history |
| `todo/outrun-texture-settings-20261016` | `5cdfd6b0ae36` | 24 | consolidated history |
| `todo/outrun-ui-extraction-20261016` | `77b264787135` | 43 | consolidated history |
| `valo/outrun-449470` | `3fc074d5c841` | 30 | `archive/2026-10-18/valo/outrun-449470` |
| `valo/outrun-loading-hd` | `da4d126dfa76` | 1 | `archive/2026-10-18/kiki/loading-hd-retail-fixtures` |
| `zola/outrun-fine-steering-20261017` | `efe43642070e` | 69 | consolidated history |
| `zola/outrun-telemetry-rpm-20261017` | `54b2c466cc15` | 72 | consolidated history |
