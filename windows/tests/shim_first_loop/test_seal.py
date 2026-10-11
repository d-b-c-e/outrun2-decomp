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
# The verdict: this receipt passes; each unmet condition alone fails it.
good = dict(receipt, phase="end", exitCode=0, host={"state": "resolved"}, journal={"qualifies": True})
if run.verdict(dict(good)): raise SystemExit("FAIL verdict refused a passing receipt: %r" % run.verdict(dict(good)))
for change in [dict(phase="steer-left"), dict(exitCode=1), dict(forced=True), dict(host={"state": "unresolved"}), dict(journal={"qualifies": False}),
               dict(seal={"exit": 1, "output": ""}), dict(seal="no capture"), dict(replay={"exit": 1, "output": ""}), dict(replay="not sealed"),
               dict(errors=["cleanup inject.on: not our bytes"])]:
    if not run.verdict(dict(good, **change)): raise SystemExit("FAIL verdict passed %r" % change)
print("PASS runner seal, replay and verdict: observed exit and held identity sealed the capture;", seal["output"].splitlines()[-1],
      "; replay", receipt["replay"]["output"])
