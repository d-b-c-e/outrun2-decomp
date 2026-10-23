# Native H-pattern source integration — October 23, 2026

This branch reapplies the accepted source fix `ba07f67` onto freshly fetched
`origin/main` at `7dbeb019f71e32a4cfcb5845939d649aa162941c`. It excludes the
21 retained predecessor commits from `fleet/eight-hour-outrun-20261022`.
The original candidate and its worktree remain preserved. Its handoff was in a
predecessor-only document; this note carries the relevant integration evidence.

For coordinator request 335715, the separate local canonical-baseline port is
`98fe1580f497c59b825fd5437cf7d9989049c3be`, directly on `235bbb3`. Its
`windows/docs/NATIVE-HPATTERN-INTEGRATION-20261023.md` records all 21 excluded
commits and the distinct 4/459 divergence between `235bbb3` and remote main.
It imports zero dependencies, retains the older APIs, and was compile-checked
without repeating the accepted fixture batch. The original patch's context does
not apply unchanged there; both handoff options remain local.

Native input now uses the existing shared `HPattern` gearbox. Both backends
require exactly one held gate and a valid consumed gear word (car +0x208, 1-6),
release on scene changes, suspend while paused, and wait up to 250 ms for a gear
response before retrying twice. This replaces Native's highest-gate selection
and four rapid pulses, which could shift on invalid reads or exhaust before a
slow response. Sequential bindings, calibration and force settings are unchanged.

The live [upstream main](https://github.com/r4dius/outrun2-decomp/commits/main)
was checked through GitHub and `git ls-remote`: `e1e1c592` (October 7), already
ancestral. No upstream merge or new dependency is required.

Focused offline checks on this integration base: **PASS**, 1,457 checks in the
existing adapter fixture, compiled with MSVC x64 `/std:c++17 /W4 /WX`.
`wheel_input.cpp` also compiled with MSVC `/W4 /WX`. ClangCL `/W4` compiled it
with the existing partial `LegacyDevices` aggregate initializer warning; no
ClangCL warning-clean result is claimed. An existing shadowed adapter loop
variable was renamed for the MSVC warning-as-error check.

The fixture covers 60/120 Hz, delayed response, ambiguous/invalid/missing-car
input, pause/resume, new-race reset, pulse release and retry exhaustion. Existing
adapter tests continue to cover the shim/core-to-game-input path. Evidence and
exact compiler commands are in this worktree's ignored `build-focused/`:
`check.cmd`, `check.log`, `compile-native-clang.cmd`, `native-clang.log`.
For an existing configured build, use target `test_outrun_adapter` and CTest
filter `^outrun_adapter$`.

No full game link, installation, device polling, screen/rig use or physical
acceptance was performed. Runtime YK3 and Wheelkit Play/Apply qualification
remain open. No owner settings, Fleet holds or shared resources were changed.
