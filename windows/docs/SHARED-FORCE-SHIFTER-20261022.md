# Shared force timing and shifter source handoff

Source-only Fleet attempt `01M5HSHA1J2E0WKF8QFS4PQHW9`, October 22, 2026.
Base: `7dbeb019f71e32a4cfcb5845939d649aa162941c`, identical to the locally
available `origin/main` ref and containing `235bbb3`. No network refresh was made.
Read the current deployment record, F6 player-settings handoff, local session
notes and the portfolio OutRun profile before editing.

## Findings

The companion Tweaks defect recorded at `51acc20` is absent from the current
decomp shared path. `ffb_outrun.hpp` records why that old shared calculation was
not ported: its crash and shift countdowns advanced only in the Legacy branch,
leaving the shared branch repeatedly submitting the event. That reference is
historical; Legacy is now trace-only, not an alternative wheel model.

Current wheel requests use `ffb_player_model.cpp` -> shadow ->
`ffb_producer2.cpp::Stream::step` -> the pinned shared `dbce::force::Model`.
The producer compares consecutive consumed gear words, updates its remembered
gear after every computed sample, and supplies `gear_shift` on the transition
only. The shared model advances its own shift and impact envelopes in `compute`
independently of Legacy. Producer@2 intentionally has no measured crash channel;
it supplies zero impact. No production timer correction or new crash signal was
needed. Normalized model direction, gains, device direction mapping and vendor
pins are unchanged.

Shared and Native input both use the canonical `[Controls] Transmission` value.
The shared binding draft saves `H-pattern` or `Sequential` with a new Revision,
then requires restart; Native's direct F6 choice writes only Transmission.
`wheel_input.cpp` applies the six native gate bindings only for `H-pattern`;
`shim_input.cpp` passes the parsed saved mode to `Adapter`, which has the same
mode gate. The accepted owner snapshot was explicitly Sequential, explaining
its sequential behavior. No mismatch was demonstrated in this explicit save/load
path, so no product configuration, F6 controls or bindings were changed. Choosing
Manual in the game remains separate from selecting the shifter input mode.

## Regression source and dependent execution

- `test_ffb_producer2`: at 60 and 120 Hz, submit one shift/impact, require expiry
  and quiet event flags, then repeat the event and require the same envelope.
  Shift pulses must balance to zero; synthetic impacts retain normalized sign
  and magnitude. The production Stream also receives two separated consumed
  gear transitions and must emit one input edge per transition, with equal
  finite-duration force envelopes and no invented impact.
- `test_desktop_bindings`: save H-pattern then Sequential through fresh real
  editor drafts into a disposable INI. Re-read each with Native's controls parser
  and the shared profile parser, preserve all six gate bindings, sequential
  bindings, measured calibration, backend and force settings, and feed the saved
  mode into the real adapter with synthetic frames. H-pattern must pulse toward
  a held gate; Sequential must ignore that gate; explicit shift bindings remain
  usable. Its existing CMake target now includes the pure adapter and parser.

No build, configure, test or typecheck was executed by this source task.
Only source inspection and `git diff --check` were performed. The dependent
Claude task owns execution. In its configured isolated build directory:

```powershell
cmake --build <build> --config Release --target test_ffb_producer2 test_desktop_bindings
ctest --test-dir <build> -C Release -R '^(ffb_producer2|desktop_bindings)$' --output-on-failure
```

Use the established Windows ClangCL/dependency setup in `windows/README.md`.
These checks create no window or device output. The fixtures do not exercise
Native DirectInput polling or physical gear response. No install, owner settings,
saves, graphics or machine-local agent settings were changed. Physical FFB
polarity/strength, shared-backend binding capture and H-pattern acceptance remain
attended runtime gates; offline success cannot establish them.
