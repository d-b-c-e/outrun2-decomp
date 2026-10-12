# Play with Recording (native side)

An ordinary OutRunDesktop session on the shim backend (`[Input] Backend = Shim`) can record what the game consumed,
on request. The managed saved-while-alive seal and its receipt are in `RECORDING-COMPLETION.md` (romi's
`outrun-recording-completion` line). This file covers what the game process does. Source:
`source/session_request.{hpp,cpp}`, `source/ordinary_recording.{hpp,cpp}`, `source/shim_input.cpp`,
`source/window_desktop.cpp`, `source/desktop_panel.cpp` (the badge). Tests: `tests/test_session_request.cpp`,
`tests/test_outrun_capture2.cpp`, `tests/test_ordinary_recording.cpp`.

## The request

Wheelkit (or a runner) writes `%LOCALAPPDATA%\dbce\outrun2006-decomp\session-request.txt` before the launch. The format
is in `session_request.hpp`: schema line, `requestId`, `action`, `expiresUnix` (at most 10 minutes ahead), `result`,
`recording`. An unlatched process takes it at start, before the game:

| Request | What happens |
|---|---|
| none | ordinary play |
| record, parses, Backend = Shim | moved to `<result>\session-request.txt`; `<recording>` made; recording armed |
| record, Backend = Native, or the folders refused | moved into its result folder, `status.json` failed; ordinary play without a capture |
| record that does not parse (expired, bad path, ...) | kept as `.refused-<pid>-<n>` beside itself; ordinary play without a capture |
| playback, or unreadable | never ordinary play: `status.json` failed when its result folder is known, exit code 4 before the game |

A request is read once: moved or set aside, never left for a second launch. A latched (test) process never takes it.

## While recording

- One row per input tick, contract `dbce.shim.outrun-capture@2`. The filter builds the row; the hook after 453BB0
  (`platform::native_input_consumed_hook`, after the PC layer's arbitration and the race update) adds the input the
  game consumed (`final.*`) and pushes it. A tick that misses the hook is never a row and fails the capture.
- Stops: F11 (its press), `<result>\stop.request` holding the request id (polled every 30 ticks), the limit
  (60 Hz x 300 s rows, or the rows' span reaching 299 s), and the game's exit.
- The REC badge (top right of the centre 16:9 view, drawn with the F6 panel closed too): `REC m:ss (max 4:59)  F11 stops`,
  then `REC saving...`, `REC complete, verifying`, `REC saved` or `REC failed: <why>`.
- `<result>\status.json` (`outrun.session-status@1`), replaced in one step on each change: `recording`, `saving`,
  `complete`, `saved` or `failed`, with the reason, the stop, the process id and creation FILETIME.

## Saving

At the stop the producer declares its last row. One drain runs on a saver thread (60 s). If that thread cannot start,
the exit's wait or the host's stop runs the drain inline, still only once. The host's later stop reads the cached result.
`recorder-complete.json` (`outrun.recorder-complete@1`) is written into the capture only when:

- the drain completed and joined;
- every row offered was accepted and written, and nothing was refused or dropped;
- the tail is final;
- the capture holds exactly the six members, `bundle.meta` is contract @2, and `profile.ini` is the profile Core parsed.

The proof carries:

- the counters;
- the budget (hz, seconds, maxBytes, slots), as the recorder used it;
- the profile, exe, request and process identities;
- the six members' bytes and SHA-256;
- `artifactSha256`.

Anything else is `failed`, with no proof, and the host's stop is not Complete.

`complete` is all the native side claims. `saved` needs the managed receipt `<result>\recording-verified.json`. It must be
a flat JSON object that names:

- this request, this process (id and creation time), this exe and this artifact;
- the SHA-256 of `capture\recording-complete.tsv` as it is on disk;
- `recordingSaved: true`.

At the exit the process waits up to 65 s for the saver. Wheelkit seals while the process is alive.
`test_ordinary_recording --finalizer <P21Finalizer.exe>` runs that whole wire offline: a native capture, the managed seal
against the live test process, and the native receipt check.

## Not in this slice

- Playback: this build refuses a playback request and exits.
- Menu, car and stage checkpoints.
- Any runtime or rig evidence: no game has run this code yet.
