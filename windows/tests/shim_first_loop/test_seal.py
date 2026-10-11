"""Offline check of run.py's seal step (no game, device or lease): a p21_combined child stands in for the game process
(ProviderHost<ShimRecorder> over fake COM, desktop layout: host report in the run folder, capture inside it). It is
spawned with Popen and sealed through run.py's own creation_time and seal, as a live run would be.

    py -3 -B test_seal.py <p21_combined.exe> <P21Finalizer.dll> <capture_replay.exe> <new result folder>
"""
import pathlib, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import run

exe, dll, tool, out = (pathlib.Path(a).resolve() for a in sys.argv[1:5])
if out.exists(): raise SystemExit("Use a new result folder")
(out / "run").mkdir(parents=True)
proc = subprocess.Popen([str(exe), "--child", "resolved", str(out / "run" / "capture")])
created = run.creation_time(int(proc._handle))
proc.wait(timeout=60)
receipt = {"forced": False, "errors": []}
run.seal(out, proc, created, receipt, dll)
seal = receipt.get("seal")
if proc.returncode != 0 or not created or not isinstance(seal, dict) or seal["exit"] != 0 or "PASS validated export" not in seal["output"]:
    raise SystemExit("FAIL seal step: exit %s created %s seal %r" % (proc.returncode, created, seal))
run.replay(out, receipt, tool)
if not isinstance(receipt.get("replay"), dict) or receipt["replay"]["exit"] != 0 or '"result":"pass"' not in receipt["replay"]["output"]:
    raise SystemExit("FAIL replay step: %r" % receipt.get("replay"))
# Astra 16889 (2): a sealed capture's fresh export equals the one sealing wrote; capture_replay refuses a mixed-up pair.
again = out / "read-export.tsv"; run.read_export(dll, out / "run" / "capture", again)
if again.read_bytes() != (out / "validated.tsv").read_bytes(): raise SystemExit("FAIL --read-export differs from the sealing export")
blocks = (out / "validated.tsv").read_text().split("ROW ")
(out / "short.tsv").write_text("ROW " + "ROW ".join(blocks[1:9]))
q = subprocess.run([str(tool), str(out / "run" / "capture"), str(out / "short.tsv")], capture_output=True, text=True)
if q.returncode == 0 or "not this capture's" not in q.stderr: raise SystemExit("FAIL capture_replay accepted another export: %r" % q.stderr)
# The verdict: this receipt passes; each unmet condition alone fails it.
good = dict(receipt, phase="end", exitCode=0, host={"state": "resolved"}, journal={"qualifies": True})
if run.verdict(dict(good)): raise SystemExit("FAIL verdict refused a passing receipt: %r" % run.verdict(dict(good)))
for change in [dict(phase="steer-left"), dict(exitCode=1), dict(forced=True), dict(host={"state": "unresolved"}), dict(journal={"qualifies": False}),
               dict(seal={"exit": 1, "output": ""}), dict(seal="no capture"), dict(replay={"exit": 1, "output": ""}), dict(replay="not sealed"),
               dict(errors=["cleanup inject.on: not our bytes"])]:
    if not run.verdict(dict(good, **change)): raise SystemExit("FAIL verdict passed %r" % change)
# R3: a complete playback with race progression passes; an incomplete playback, a missing comparison or no progression fails.
words = {w: "9/9" for w in ("recordSteer", "recordAccel", "recordBrake", "recordButtons")}
cmp = {"tapeRows": 9, "playedRows": 9, "contiguous": True, "intended": words, "progression": {"replayInGameRows": 9, "replayMaxSpeed": 12.5}}
r3 = dict(good, r3={"playbackComplete": True, "playbackEnd": "end line", "compare": cmp})
if run.verdict(dict(r3)): raise SystemExit("FAIL verdict refused a passing R3 receipt: %r" % run.verdict(dict(r3)))
for change in [dict(playbackComplete=False), dict(compare="comparison failed"),
               dict(compare=dict(cmp, progression={"replayInGameRows": 9, "replayMaxSpeed": 0.0})),
               dict(compare=dict(cmp, progression={"replayInGameRows": 8, "replayMaxSpeed": 12.5})),
               dict(compare=dict(cmp, contiguous=False)), dict(compare=dict(cmp, playedRows=8)),   # Astra 16889 (1)
               dict(compare=dict(cmp, intended=dict(words, recordSteer="0/9"))), dict(compare=dict(cmp, intended={}))]:
    if not run.verdict(dict(r3, r3=dict(r3["r3"], **change))): raise SystemExit("FAIL R3 verdict passed %r" % change)
m = run.REPLAY_END.search("input shim: replay end at live frame 900: 9 of 9 tape rows played (tape complete)")
if not (m and m.group(2) == m.group(3) == "9" and m.group(4) == "tape complete"): raise SystemExit("FAIL replay end line not parsed")
print("PASS runner seal, replay and verdict: observed exit and held identity sealed the capture;", seal["output"].splitlines()[-1],
      "; replay", receipt["replay"]["output"])
