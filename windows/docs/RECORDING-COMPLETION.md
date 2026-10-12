# Saving an ordinary OutRun recording while the game stays open

The ordinary capture uses `dbce.shim.outrun-capture@2`. It retains the existing
session writer, Core/adapter fields and P21 stream validation. It adds the final
input snapshot after game/keyboard arbitration: all 35 `final.*` / `keyboard.*`
channels must be present. This is input evidence, not deterministic motion replay.

After Stop, the producer declares its last frame and stops submitting. The native
saver drains and joins the worker once, closes all six streams, and writes
`capture/recorder-complete.json` using create-new. It records the request and
process identity, completion/loss counters, time and byte budget, and sorted
hashes for exactly `budget.tsv`, `bundle.meta`, `devices.json`, `profile.ini`,
`source.jsonl`, `worker.tsv`. `artifactSha256` hashes UTF-8 lines
`name\tbytes\tsha256\n` in that ordinal order. The recording's independent
`captureNonce` stays in the worker and stream; `runNonce` names this request.

The managed finalizer holds all members read-only and applies the existing
footer, frame sequence, schema, worker-count, hash and no-loss validation. It
also checks the report against the supervisor's expected request, executable
SHA-256, PID and creation FILETIME. It writes `capture/recording-complete.tsv`
using the toolkit's additive `ArtifactSeal.CompleteNamed` API. That seal covers
the six members and `recorder-complete.json`. Existing `complete.tsv` retains
its separate meaning: host shutdown resolved and the same process exited normally.
For contract 2, that later process seal also includes the recording report/seal.
Contract 1 and generic capture admission are unchanged.

```powershell
dotnet P21Finalizer.dll --seal-recording <capture-dir> <request-id> <exe-sha256> <pid> <creation-filetime> <result-dir>/recording-verified.json
dotnet P21Finalizer.dll --read-recording <capture-dir> <request-id> <exe-sha256> <pid> <creation-filetime> <new-receipt-path>
```

Request is 32 lower-case hex, executable SHA is 64 lower-case hex, PID and
creation time are nonzero unsigned decimal. Receipts must be new files outside
the capture directory. The JSON `outrun.recording-verified@1` receipt carries
those four identities, `artifactSha256`, `sealSha256`, `recordingSaved: true`,
`rows`, and an explicit scope: process exit is not asserted. The UI remains
Saving / complete-not-verified until this matching receipt exists. A failure
never produces a success receipt. A seal is never overwritten; after a receipt
write failure, use `--read-recording` to validate it again and write a fresh receipt.

Device-free checks:

```powershell
dotnet build windows/tests/p21_recorder_integration/managed/P21Finalizer.csproj -c Release -p:ToolkitRoot=<toolkit-with-named-seal>
py -3 windows/tests/p21_recorder_integration/test_recording_completion.py <resolved-fixture-capture> <new-result> --dll <built-P21Finalizer.dll>
py -3 windows/tests/p21_recorder_integration/run.py <new-result> --toolkit <toolkit-with-named-seal> --mutations
```

The new completion scenarios synthesize explicit contract-2 copies from a small
P21 fixture; they are protocol tests, not native producer or game evidence. They
cover save/read while alive, missing final/keyboard channels, tampered members,
loss/timeouts/identity mismatches, and a later normal versus forced/wrong-process
exit. The P21 suite continues to use the actual native writer and supervised
fixture processes. Native ordinary capture, UI Saved feedback, and a real captured
drive replay need their own runtime evidence before being advertised as ready.
