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

Command publication uses the shared native `control_publish.exe`, with its path
and SHA256 pinned in the receipt plan. Build that target alongside the candidate,
or use `--publisher-tool <path>`. Known sibling/build locations are checked in a
fixed order; missing tool means preflight refusal before lease or launch. There
is no ReplaceFileW fallback. See [control publisher](../../docs/CONTROL-PUBLISHER.md)
for the Windows 10 1607/Server 2016 API minimum, filesystem requirements and the
offline concurrent SnapshotFile regression.

```
py -3 -B run.py --candidate <OutRunDesktop.exe> --prepare-from <a game folder with OR2006C2C.exe> --game <new test folder>   # once per candidate
py -3 -B run.py --candidate <OutRunDesktop.exe> --finalizer <P21Finalizer.dll> --replay-tool <capture_replay.exe>     --result <fresh dir> --check-only                                                       # preflight, no launch
py -3 -B run.py ... --result <fresh dir>                                                     # R1: the first loop, captured
py -3 -B run.py ... --result <fresh dir> --replay-of <passing R1 result>                     # R3: the cold replay run
```

The test profile in the test copy binds the R12's real axes (steer 0, throttle 2, brake 5, as in
readonly-provider-live-03). Menu actions go to otherwise unused R12 buttons (20-26), injected only.

Menus: the fence copies a test copy's saves into the fresh run folder. The default script expects an empty bank
("create new license"); it is not the route for an existing license. R1 and
R3 play `MENU_SCRIPT`, zero's mapped path from perf runs 1-3, in seconds after the game window appears:
- title;
- CREATE NEW LICENSE, three navDowns to DONE;
- SINGLE PLAYER, navRight to OUTRUN;
- the OUTRUN2 course, the first car, automatic, the first music.
  Twelve seconds after the default script ends, if the race has not started, the former start/confirm presses resume (40 in
all). Each press is recorded with its time and whether it was scripted.

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
- R3 plays only a tape recorded by the same candidate: the first run's `plan.candidateSha256` must equal this run's.
  Its wall-clock bound comes from the first run's own race, not an assumed frame rate. R1 records `raceSeconds`, from
  race entry to the end batch, and R3 ends at the later of `2 x raceSeconds + 30` and the former `tape rows / 30 + 15`.
  `raceSeconds` must be finite and positive.
- Timing: `--seconds` bounds startup, from launch to race entry. From race entry the race has its own budget: R1's two
  3.5 s steer holds, or R3's bound, plus 15 s. A late race entry therefore never cuts the race short. The run's
  overall bound is the sum, and `inject.on` expires 120 s after it. `test_run_timing.py` (ctest
  `shim_first_loop_timing`) checks these decisions on a fake clock, with the former single deadline as a mutation
  control.
  A test run's shader cache (`home()/shadercache`, the run folder) is always cold, so both runs compile their
  programs at the menus and at race entry.
- `--prepare-from` refuses an existing test folder. For a new candidate, prepare a new folder; the preflight refuses a
  test copy whose `OutRunDesktop.exe` is not the candidate. The owner's save backups are never a source.

  For HD UI capture with an existing license, use the same R1 `--render` path:

  ```powershell
  py -3 -B run.py ... --render screens=separate,views=angled,frames=60,every=4,after=120,scale=2 --drive 20 --seconds 60 --render-presentation render-presentation.ini --existing-license --check-only
  ```

  `render-presentation.ini` must contain exactly these four keys and no other section/defaults:

  ```ini
  [Graphics]
  UITextureReplacement = true
  SceneTextureReplacement = false
  TextureBaseFolder = textures-consolidated-20261018-01
  UIScalingMode = 2
  ```

  Stage the verified DDS pack under that **test game's** folder, with `load` below it. The folder name must be a
  single local name (letters, digits, underscore or hyphen). The test game's INI must still be byte-exact `PROFILE`.
  After admission the runner temporarily appends only those Graphics keys, verifies the input/profile hashes and
  restores `PROFILE` on exit, using the existing `--horn` restore boundary. Neither option is admitted for replay,
  consumed replay or horn checks, or without `--render`. No native code, input binding, latch, force or capture limit changes.

  `--existing-license` requires exactly the two nonempty bank files, `License1.dat` and `common.dat`. The fence
  hash-verifies their copies plus `options.ini` into the run home. The existing-license script uses title, single
  player, OutRun, course, car, transmission, music and START; it never creates/names/saves a license. Unlike the
  default script, it sends no fallback presses: no race within 12 seconds after the final step fails the run.
  This schedule derives from the observed ordinary route; it still needs its own fenced runtime qualification.

  The render request owns the fixture rig (32-inch 16:9, eye 660 mm, sides 70 degrees, gap 8 mm, MatchGameFov true).
  Compare that with the live intended geometry before claiming equivalence. Preserve intended widescreen/resolution
  in the test copy's `options.ini`; the fence copies that file, rather than reading options from the owner's install.
  Full-width PPM frames are game back-buffer output. Review the views, seams and HUD after closure; indexing or a
  replacement count alone does not prove HD appearance. A passing fixture is not installed-route or physical acceptance.

  Focused offline checks: `py -3 -B test_render_presentation.py` and `py -3 -B test_run_timing.py`.

  First-loop limits:
- no observer mapping;
- the identity's executable and schema hashes are reported as null.
