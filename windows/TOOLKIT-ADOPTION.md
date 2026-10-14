# Toolkit standards adoption

Which entries of the wheel toolkit's standards ledger
(`E:\Source\toolkits\dbce-wheel-mod-toolkit\STANDARDS.md`) the desktop build has
brought in. Update a row in the same commit that adopts it. Statuses: `adopted`,
`partial`, `pending`, `n/a` (say why), `unchecked`. Seeded 2026-10-10.

| Standard | Title | Status | Notes |
|---|---|---|---|
| STD-001 | One mod per game | partial | Owner, 2026-10-10: a separate product (`outrun-2006-decomp`) beside `dbce-mods-outrun-2006` for now; the decomp will eventually replace the Tweaks mod entirely. The one-time Tweaks `[DirectInput]` import is the settings migration path. |
| STD-002 | Recording and playback from launch | n/a | Superseded by STD-012. |
| STD-003 | Normalized FFB strength | pending | No force feedback yet. |
| STD-004 | Consistent settings UX | pending | No F6 panel yet. |
| STD-005 | Camera numpad layout | pending | |
| STD-006 | Camera step sizes are settings | pending | |
| STD-007 | Triple screens in one wide window | pending | Per-panel projection planned (ROADMAP step 3). |
| STD-008 | Display changes: game applies once | unchecked | |
| STD-009 | Dashboard telemetry matches the HUD | partial | The speed source is the HUD's +0x1F8 base and the packet byte fixture checks km/h to m/s; no attended numeric HUD/receiver comparison. See `docs/TELEMETRY-FIELDS.md`. |
| STD-010 | Install the latest build for testing | unchecked | |
| STD-011 | Work lands on main | adopted | Reviewed branches merge to `main` of the fork. |
| STD-012 | Reproduce the route and preserve original signals | pending | Upstream has `autoplay.txt` / `replay.txt` pad scripts; no tick or state contract yet. |
| STD-013 | The installed build launches plainly | partial | `OutRunDesktop.exe` boots from its folder (windowed run 2026-10-10); plain borderless launch from LaunchBox not yet checked. |
| STD-014 | Request reciprocal review when progress stalls | adopted | Astra reviews the decomp's source slices. |
| STD-015 | Triples on Surround and separate monitors | pending | |
| STD-016 | Forza Horizon telemetry on by default | pending | |
| STD-017 | Hide empty settings pages | pending | |
| STD-018 | Handling changes never reach online scores | pending | Offline only so far; upstream has a LAN layer. Result/score guard for injected runs agreed in principle with Astra, not yet implemented. |
| STD-019 | Centre menus and gameplay HUD | pending | |
| STD-020 | Standard feature checklist | partial | Grid below. |
| STD-021 | art of rally is the FFB reference | pending | |
| STD-022 | One triple-screen selector | pending | |
| STD-023 | Frame-rate readout in settings and log | unchecked | Upstream offers 60 / 120 fps; no readout audit. |
| STD-024 | Optional on-screen frame-rate counter | unchecked | |
| STD-025 | One shared force model | pending | |
| STD-026 | One force model for players | pending | |
| STD-027 | Independent steering and effect strengths | pending | |
| STD-028 | Complete raw registry preservation for game tests | n/a | The build keeps no registry state (settings, cache, saves and log are in its folder). |
| STD-029 | New functionality ships installed and turned on | partial | Wheel input is on by default; the rest is unbuilt. |
| STD-030 | The F6 panel comes first | pending | Next slice after the controls contract (toolkit `native/panel`, D3D11). |
| STD-031 | art of rally's reference level | pending | |
| STD-032 | Wheelkit reads the portfolio's own records | partial | `product.json` and the `controls:` start line in `runtime.log`; Wheelkit catalog entry pending (Astra). |
| STD-033 | Rig profile controls reach the game, and a test proves it | partial | Canonical `[Controls]` (`ini-controls-1`, vendored `dbce_controls.hpp`), start line with named unavailable actions, one-time legacy import, test injection + observer. Configured only: input observed and owner acceptance pending. |
| STD-034 | A rig profile Apply is qualified by a closed loop | pending | Harness design with Astra. |
| STD-035 | Writers and test injection: identity, provenance and failure rules | partial | Instance-GUID-only matching; injection gated by session, applied `[Controls]` and a force-free build; awaiting Astra's review. |
| STD-036 | Every profile binding reaches the game from its own device | partial | Each binding is read from its own device instance; unverified at the rig. |

## Feature checklist (STD-020)

| STR | PED | HB | DEV | H | SEQ | GLY | FFB | NRM | TEL | SUR | SPN | DSP | MNU |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 🧪 | 🧪 | — | 🧪 | 🧪 | 🧪 | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |

🧪 implemented, not yet driven at the wheel · — the game has no handbrake · ❌ not yet.
STR: the steering word reaches the 453BB0 record unchanged; the original game still applies its own 0.0078 dead zone
and quantises to -127..127 (453860). H: OutRun 2006 has no neutral or reverse gear.
