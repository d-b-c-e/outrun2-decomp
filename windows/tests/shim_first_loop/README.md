# The input shim's first in-game loop (live, coordinated only)

`run.py` drives one latched `OutRunDesktop.exe` (built with the shim wiring: `windows/source/shim_input.cpp`)
through `inject.txt`. It first presses confirm and start until the game reaches a race, then holds steer right and
left with throttle, then sends `end`. It reads the shim's own `runtime.log` lines for the mode and the values: the
record words the adapter wrote and the words the game consumed.

Run it only when a live launch is coordinated (Astra, owner). It uses the existing protections:
- the hub rig lease and owner-idle gate (`tools/Rig-Lease.ps1`, 5 minutes);
- the test fence (`inject.on` latches; the game folder is only read; saves, options and logs go to the run folder);
- the build's absence of force feedback;
- no OS input and no foregrounding: it closes the game with `WM_CLOSE` to its own window and opens it `--windowed`;
- cleanup of `inject.on` and `inject.txt` on every exit path;
- the supervisor rule for a parked, unresolved latched process (P21).

It never touches the owner's game folder; it runs from a dedicated test copy.

```
py -3 -B run.py --candidate <OutRunDesktop.exe> --prepare-from E:\Games\SaveBackups\Outrun   # once: builds E:\Games\OutRunShimTest
py -3 -B run.py --candidate <OutRunDesktop.exe> --finalizer <P21Finalizer.dll> --replay-tool <capture_replay.exe>     --result <fresh dir> --check-only                                                       # preflight, no launch
py -3 -B run.py ... --result <fresh dir>                                                     # R1: the first loop, captured
py -3 -B run.py ... --result <fresh dir> --replay-of <passing R1 result>                     # R3: the cold replay run
```

The test profile in the test copy binds the R12's real axes (steer 0, throttle 2, brake 5, as in
readonly-provider-live-03). Menu actions go to otherwise unused R12 buttons (20-24), injected only.

Receipt: the run folder (`runtime.log`, `controls-journal.jsonl` with the strict reader's verdict,
`host-shutdown.json`), the modes seen, the largest record and consumed steer and accelerator values, and
`result` pass/fail with every unmet condition in `failures` (the exit code is nonzero on fail).

Capture and replay (Astra 16553/16754):
- R1: the run's capture (`run\capture`) is sealed in the evidence copy by the reviewed P21 finalizer: the final host
  report is copied in and `exit.json` is written only for an exit this runner observed, with the identity it held.
- R2: `capture_replay` replays every sealed row's recorded Frame and GameView through a fresh adapter; the signals must
  match (the native-input field is reported, not claimed).
- R3 (`--replay-of`): a second, cold latched run plays the first run's recorded Frame actions at the adapter boundary
  from the live race entry, while the live session is Running (`source/outrun_replay.hpp`; the test copy's ini names the
  tape for that run only). `compare_replay.py` reports intended and consumed agreement and race progression against the
  first run; the verdict requires the whole tape played and the race progressing. Adapter/action replay: not physical
  input, not a raw-to-Core replay, no route claim.

First-loop limits:
- no observer mapping;
- the identity's executable and schema hashes are reported as null.
