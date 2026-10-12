# P21 host + reviewed P20 recorder integration proof (offline)

This proof joins the P21 host lifetime (`windows/docs/P21-SHUTDOWN.md` r2.1) to the reviewed P20 recorder, from
`codex/shim-recorded-frames` `24e27bd`:
- the worker `recorder_lifecycle_spike/worker.hpp`, with one addition: its thread is now joinable, `join_until`;
- the real native `dbce::session::Writer` (toolkit `native/session/session_writer.h`, pinned
  `a9de5d49...`) and the worker's closed-file size and SHA;
- actual Core/OutRun adapter frames from fake devices, composed exactly as `queued_frames_spike/native.cpp` does. That
  file is compiled here unchanged, with its `main` renamed: the 382-channel schema, the preflight bounds, `Identity`,
  `QueuedSink` and the `report()` member writer.

No device, game, installation or launch.

```powershell
py -3 -I -B windows/tests/p21_recorder_integration/run.py <new result folder> --toolkit E:/Source/toolkits/dbce-wheel-mod-toolkit --mutations
```

## What runs

The parent is the identity-bound supervisor. It runs four child host lifetimes, each with the production budgets
(1000/200/200 ms):

| Mode | Child | Expected |
|---|---|---|
| `resolved` | 16 actual frames; `stop()` on the producer thread: finish, drain, the worker thread joined, then members written from the quiescent report | `host-shutdown.json` resolved; normal return through CRT teardown, exit 0; the finalizer seals and reads all 16 frames |
| `writer-stuck` | the real writer blocked in its first write | recorder `TimedOut`, not joined; no members; parks; the supervisor ends it (`0xDBCE0F0F`); refused |
| `provider-stuck` | recorder complete and joined, full `worker.tsv` and stream; the provider blocked with a device in flight | `lease leaked`, `worker provider`; parks; the supervisor ends it; **refused although its worker evidence validates on its own** |
| `ordinary-provider-stuck` | the same in an ordinary process | ends itself, `0xDBCE0021`, labelled forced; refused |

The supervisor writes `exit.json` for each capture: the PID and creation time of the process it spawned, the exit code,
and whether it forced the end.
- It writes the file only after an exit it actually observed.
- Every process wait is bounded, including a 5 s wait after `TerminateProcess`.
- A failed wait, `TerminateProcess` or `GetExitCodeProcess` call fails the proof, as does a child still alive after
  the bounded post-terminate wait. A failed run writes no exit observation and ends a still-running child without
  waiting.
- A zero PID or creation time (a failed `GetProcessTimes`) is refused. The host also does not resolve without its own
  nonzero identity.

**Its file I/O is not a hard bound.** Polling `host-shutdown.json` and writing `exit.json` are synchronous reads and
writes, and either can block. In this proof, `run.py`'s per-command timeout (300 s) is the outer fixture guard.

The finalizer (`managed/`) seals only with all three facts bound together:
- the reviewed queued-frames validation, copied verbatim: worker report layout, nonce, closed bytes, profile, footer,
  382-channel schema and row bound;
- the host report: `resolved`, the same nonce, recorder `Complete` and joined, nothing unresolved;
- the supervisor's observation: the same nonzero PID and creation time as the host reported, exit 0, not forced.

Each field is read with its type checked. A missing or mistyped field refuses with its name, never with a parser
exception or a default value.

All seven members go into `complete.tsv`. On copies of the resolved capture it refuses 13 targeted mutations, each
for its own reason:
- host unresolved;
- recorder not joined;
- a host report from another capture;
- forced exit;
- nonzero exit;
- another process's exit;
- missing exit observation;
- missing host report;
- a sealed exit observation changed after sealing;
- zero PID and creation time in both reports, matching each other;
- an exit observation without a creation time;
- a host report without a PID;
- a mistyped `forced` field.

`run.py --mutations` adds five negative controls, each required to fail at its intended check:
- a child that returns instead of parking;
- a host that ignores the provider and the lease;
- a finalizer that ignores the host state;
- a finalizer that ignores the exit observation;
- a finalizer that accepts matching zero identities.

## Not shown

It does not show:
- the real provider, COM or DirectInput (P28);
- a hosted game process;
- a detach-deadlock absence, which nothing relies on;
- `WM_ENDSESSION`;
- the P21g audit;
- portable replay.

The capture contract is still the fixture's `dbce.shim.queued-frames-spike@2`.

## Packaged consumed-input comparison

The finalizer also serves the ordinary recording companion. It accepts
`--compare-recordings <reference-capture> <replay-capture> <new-output.json>`.
Both captures must have valid `dbce.shim.outrun-capture@2` recording seals and
completion reports from the same executable. The replay report must name
`action=playback`, `stoppedBy=tape-end` and `sourceSealSha256` matching the
reference seal. The reference must have no action (legacy) or `action=record`.
Older reports without an action remain
ordinary recordings; they cannot use the tape-end stop reason.

Before launch, `--export-playback <capture> <request> <exe-hash> <pid>
<creation-file-time> <new-tape>` performs the ordinary sealed export checks and
requires complete finite observation/pose channels, boolean-valued pose validity
and a moved active car. An unsuitable source refuses before writing a tape.
`--read-playback-recording` takes the same arguments but writes an admission
receipt instead: `outrun.recording-verified@1` with `playbackEligible=true` and
`pose` (validRows, pathLength, range). Use it after save to determine whether the
descriptor may advertise playback. Admission failure uses exit 1, its reason on
stderr and no output receipt. `--export-recording` / `--read-recording` retain
their existing analysis/saved-recording semantics and do not grant eligibility.

`ConsumedComparison.cs` ports `shim_first_loop/compare_consumed.py` at `86f4640`:
complete loss-free streams, each tape row once in order, exact supplied `final.*`
inputs, and game observations plus active-car motion. Speed tolerance is 0.01,
position 0.001 in native units, vec20 0.0001 and rotation 0.00001. Discrete values
are exact. Pose validity must be 0 or 1; pose values compare when both rows have
the active car. The source and replay must each show that car moving. A neutral
tail is allowed only after all supplied rows. Input-only references are refused
by the product command rather than promoted to a gameplay pass.

Output is `outrun.consumed-comparison@1`, including both seal hashes. Exit codes:
0 = valid and matching supplied inputs and observations, 2 = valid comparison
with divergent input or observations, 1 = invalid evidence or command failure.
On early seal/identity failure there is no comparison file. `followed` describes
observations only: the caller must also require every fed row equal. The tool
does not establish normal game exit or unchanged owner files; the companion
checks those before issuing its terminal result.

`--compare-consumed-sources <reference.jsonl> <replay.jsonl> <new-output.json>` is
read-only analysis for the earlier private proofs. Its output is explicitly
unsealed, with no seal hashes, and is not accepted as product qualification.
It may describe legacy input-only captures honestly. Comparison reads at most
100,000 samples per file and one GiB per source; outputs never overwrite inputs
or existing files. Strict JSON rejects non-finite literals and duplicate fields.

`test_consumed_comparison.py --dll <finalizer.dll>` cross-checks the production
managed command against all 41 cases of the reviewed Python corpus. Optional
repeatable `--pair <reference.jsonl> <replay.jsonl>` also compares existing private
proofs without modifying them. Valid cases compare result fields as well as exit
classification; invalid cases require refusal, since strict JSON rejects bare
NaN earlier than Python's parser. `test_recording_completion.py` additionally
exercises both recording seals, replay completion, matched motion, input and pose
divergence, executable mismatch, tampering and input-only refusal through the
actual product comparison command. These are fixtures, not live gameplay tests.
