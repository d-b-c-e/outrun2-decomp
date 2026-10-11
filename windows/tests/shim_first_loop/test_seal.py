"""Offline check of run.py's seal step (no game, device or lease): a p21_combined child stands in for the game process
(ProviderHost<ShimRecorder> over fake COM, desktop layout: host report in the run folder, capture inside it). It is
spawned with Popen and sealed through run.py's own creation_time and seal, as a live run would be.

    py -3 -B test_seal.py <p21_combined.exe> <P21Finalizer.dll> <new result folder>
"""
import pathlib, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import run

exe, dll, out = (pathlib.Path(a).resolve() for a in sys.argv[1:4])
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
print("PASS runner seal step: observed exit and held identity sealed the capture;", seal["output"].splitlines()[-1])
