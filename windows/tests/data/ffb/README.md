# OutRun force model goldens (`test_ffb_outrun`)

Expected output of the OutRun 2006 mod's **production** force calculation (FFBProfile = legacy, its compiled
defaults), from its own offline fixture. `source/ffb_outrun` must reproduce each byte for byte.

- Mod: [d-b-c-e/dbce-mods-outrun-2006](https://github.com/d-b-c-e/dbce-mods-outrun-2006) at `34b204031fa2dae41a84c8f2c8d1dd934d673642`.
  `src/ffb_calculation.inl` e8ad14281339097c, `src/hooks_dinputffb.cpp` 29fcbfbeb688d9de,
  `tools/tests/signal_calculation_fixture.cpp` f63b02c3b0979068.
- `signal-legacy-v1.expected` is the mod's own golden (`tools/tests/data/`), byte for byte; the fixture built from
  the commit above reproduces it.
- The other cases: `gen_cases.py` writes each `<name>.csv` in the fixture's input schema and runs the fixture on it
  (x86 MSVC, built from the mod's sources as its `Test-SignalCalculation.ps1` does). At that commit the fixture needs
  a link stub for `RemapInject::Observe` (referenced only by `FFB::ProcessMutedSignals`, which it never calls). The
  fixture's stdout is text mode, so CRLF is turned into the LF its own comparison uses.
- Periodic output (hardware sine slots) is not in the goldens: the fixture prints its CF-fallback run. The test checks
  the periodic envelopes against their formulas instead.

| Case | Input (sha-256, 16) | Expected (sha-256, 16) |
| --- | --- | --- |
| `crash-speed-drop` | `crash-speed-drop.csv` c8904b9fd41be04c | 5e43bcad430dc871 |
| `contact-edge` | `contact-edge.csv` c18f7a36763f6da6 | 2152d879b5d33c47 |
| `gear-shifts` | `gear-shifts.csv` 99911f82837592d3 | e3e07a23089ed199 |
| `splash-water` | `splash-water.csv` 5d5b2f65405788dc | 3620afc0a8691f5c |
| `drift-idle` | `drift-idle.csv` 94e59575e4e6f68a | f9ac6173132877a5 |
| `mixed-drive` | `mixed-drive.csv` 4c09f4a4e797cf1b | f8aa5a154c21b482 |
| `signal-legacy-v1` | (generated in the test: the mod's Test-SignalCalculation.ps1 rows) | 86d1ed036cb9bea2 |
