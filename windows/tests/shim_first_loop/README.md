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
py -3 -B run.py --candidate <OutRunDesktop.exe> --result <fresh dir> --check-only            # preflight, no launch
py -3 -B run.py --candidate <OutRunDesktop.exe> --result <fresh dir>                         # the live run
```

The test profile in the test copy binds the R12's real axes (steer 0, throttle 2, brake 5, as in
readonly-provider-live-03). Menu actions go to otherwise unused R12 buttons (20-24), injected only.

Receipt: the run folder (`runtime.log`, `controls-journal.jsonl` with the strict reader's verdict,
`host-shutdown.json`), the modes seen, and the largest record and consumed steer and accelerator values.

First-loop limits:
- no recorder or replay yet;
- no observer mapping;
- the identity's executable and schema hashes are reported as null.
