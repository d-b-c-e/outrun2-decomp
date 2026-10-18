# OutRun 2006 contribution review

Prepared October 18, 2026 for review against
[r4dius/outrun2-decomp](https://github.com/r4dius/outrun2-decomp).

## Starting point

The fork's `main` is the consolidated review entry point. The named branch
`review/upstream-consolidated-20261018` preserves the same publication milestone.
Upstream `e1e1c59` was fetched and is already an ancestor: no upstream conflict
resolution or upstream history rewrite was needed. The previously published fork
main was `c604e30`; the cumulative local Windows branch was `54b2c46`.

Please review or cherry-pick by subsystem rather than assuming the entire fork
must be accepted at once. Existing authorship and commit history are retained.
No pull request or message to the upstream author is implied by this publication.

## Change map

| Area | Source and reading order | Review scope |
| --- | --- | --- |
| Desktop host and configuration | `windows/source`, [Windows guide](../windows/README.md), `windows/CMakeLists.txt` | Windows desktop build, DirectInput devices, canonical controls, legacy binding import, saved configuration and F6 settings. |
| Wheels and force feedback | `windows/source/wheel_input.cpp`, `ffb_*.cpp`, `windows/vendor` | Native and opt-in shared-input backends; software force model and optional external delivery. Device/physical acceptance is separate from fixture results. |
| Display and triples | [Triple-screen notes](../windows/docs/TRIPLE-SCREENS.md), [display selection](../windows/docs/GAMEPLAY-DISPLAY.md), `xbox/source/d3d11_*`, `src/platform/race_traffic.*` | Separate-monitor/Surround placement, angled-view rendering and traffic-car outer-edge culling. Experimental switches and visual limitations remain documented. |
| Rendering and native reconstruction | `src/platform`, `src/driving`, `tests` | Matrix-stack recovery, request-manager service routing, scene/texture handling, draw-list and resource tests. Shared code changes need non-Windows review too. |
| Tweaks-compatible enhancements | [Windows roadmap](../windows/ROADMAP.md), `src/enhancements`, `windows/docs` | UI/texture replacement, music/CD behavior, startup and fast-load controls, display pacing, vibration and related settings. Individual settings have different evidence levels. |
| Telemetry and fine steering | [telemetry provenance](../windows/docs/TELEMETRY-FIELDS.md), `windows/source/telemetry_forza.cpp`, `src/platform/race_input.cpp`, `windows/tests/test_fine_steering.cpp` | Forza-format packets, shared-memory compatibility, opt-in game RPM and fine analog steering. Fine steering changes input precision and remains opt-in. |
| Networking | [network transport](../windows/docs/NETWORK-TRANSPORT.md), [login protection](../windows/docs/ONLINE-LOGIN-PROTECTION.md) | Opt-in DNS/UDP supplier and profile protections; not a claim of restored online service. |
| Test and recording tools | `windows/tests`, `windows/host`, `windows/tools` | Offline fixtures, bounded recording/replay and diagnostic tooling; not required for ordinary gameplay. |

## Consolidation decisions

The cumulative branch supplies the main feature set. The angled-view traffic
branch (`594609f`, including `0cc40ec`) is merged with its focused fixtures and
bounded opt-in rendering diagnostics. It keeps traffic visible until its sphere
is outside both side-view outer edges; the ordinary planar path is unchanged.
This merge does not establish a new live triple-screen driving result.

The resource-lifetime fixture repair (`e87299a`) is also included: unbound shader
data raises the existing exception instead of dereferencing null; the test runs
asset-independent checks and reports an explicit skip when no game EXE is supplied.

Side branches are compared both by patch identity and current implementation.
Many older request-manager and renderer commits were already cherry-picked with
different hashes; replaying those branches wholesale would duplicate or regress
newer work. The [branch inventory](BRANCH-INVENTORY.md) records preserved tips.
Archive branches retain alternative/experimental history for inspection and are
not advertised as additional release-ready features.

Specific reconciliation results:

- The request-manager stack, loading-image replacement, countdown fixture,
  route observation, presentation-sound work and prompt-identity optimization
  already have equivalent changes in the cumulative branch. `948dab7` differs
  from its integrated equivalent `b7dbd23` by a comment, not behavior.
- The older `235bbb3` controls/rescan branch is superseded by stronger file-read
  and replacement handling plus device-change/missing-binding rescans. Its
  older periodic-rescan implementation is not restored.
- The publisher fix `b173a48` is already integrated as `cc6f0c6`.
- Art/shared-shim bridge experiments belong to the shared toolkit; this fork
  retains its reviewed vendor pin rather than creating another shim variant.
- Older async-shader, hitch-profiling and triple diagnostic branches remain
  available on the fork for research. They are not merged merely because they
  exist; a measurement branch is not a demonstrated production improvement.

## Validation and limitations

See [the consolidation validation record](CONSOLIDATION-VALIDATION.md) for exact
commands and results. Compilation and offline fixtures do not prove physical
wheel feel, rendered triples, game progression, or another platform's build.

Existing roadmap entries contain historical test receipts and local evidence
paths. Those receipts remain tied to their original candidates; local paths are
not dependencies for building this fork. Use source and repeatable fixtures for
independent review. The October 18 consolidation does not rerun gameplay or
change an installed game, save, display topology or hardware profile.

Some advanced switches remain experimental, and some diagnostic branches are
intentionally archived rather than enabled in this candidate. See the per-feature
documents for defaults and known gaps. The original upstream status notes still
apply; complete decompilation and full Tweaks parity are not claimed.

## Assets and attribution

Players must supply their own game and supported executable. No game executable,
asset pack, local save, private test capture or generated release binary is added
by this consolidation. Existing third-party notices and vendored licenses remain
in place; see [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) and
`windows/vendor`. No project-wide licensing change is made here.

The publication audit checked newly uploaded objects for binary/game payloads
and common credential signatures. That bounded scan is not a general security or
licensing certification.
