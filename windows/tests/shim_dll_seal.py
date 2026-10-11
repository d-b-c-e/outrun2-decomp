"""dbce_shim.dll's capture sealed out of process under the generic contract (Astra 18002). No game, device or force.

    py -3 -B shim_dll_seal.py <test_shim_abi.exe> <P21Finalizer.dll> <new result folder>

The test executable's --capture-child runs an ordinary capture over fake COM (the shared frame recorder, game channels
paired by frame seq) and stops resolved. This script supervises it like a game: it holds the process identity from the
spawn, then seals through shim_first_loop's own seal() (the final host report copied in, exit.json for the observed
exit, P21Finalizer --seal-export). A copy whose bundle.meta names another game must be refused.
"""
import pathlib, shutil, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "shim_first_loop"))
import run

exe, finalizer, out = (pathlib.Path(a).resolve() for a in sys.argv[1:4])
if out.exists(): raise SystemExit("Use a new result folder")
(out / "run").mkdir(parents=True)
proc = subprocess.Popen([str(exe), "--capture-child", str(out / "run")])
created = run.creation_time(int(proc._handle))
proc.wait(timeout=120)
if proc.returncode != 0: raise SystemExit("FAIL capture child exited %s" % proc.returncode)

other = out / "other-game"
shutil.copytree(out / "run", other / "run")
meta = other / "run" / "capture" / "bundle.meta"
meta.write_bytes(meta.read_bytes().replace(b"game=dbce-shim-abi", b"game=other-game"))

receipt = {"forced": False, "errors": []}
run.seal(out, proc, created, receipt, finalizer)
seal = receipt.get("seal")
if not (isinstance(seal, dict) and seal["exit"] == 0 and "PASS validated export" in seal["output"]):
    raise SystemExit("FAIL generic capture not sealed: %r" % seal)
export = (out / "validated.tsv").read_text()
if "game.speed 12.5" not in export or "game.valid 0" not in export or "game.valid 1" not in export:
    raise SystemExit("FAIL the export lacks the game channels")

refused = {"forced": False, "errors": []}
run.seal(other, proc, created, refused, finalizer)   # the host report and exit observation are in place; the seal refuses
q = subprocess.run(["dotnet", str(finalizer), "--seal-export", str(other / "run" / "capture"), str(other / "again.tsv")], capture_output=True, text=True)
first = ((q.stdout + q.stderr).strip().splitlines() or [""])[0]
if refused.get("seal", {}).get("exit") == 0 or q.returncode == 0 or "known frame contract" not in first:
    raise SystemExit("FAIL a capture naming another game was not refused for its game: %s" % first)
# LATCHED|CAPTURE (Astra 18330): the latched run's capture, its journal beside it, sealed the same way.
latched = out / "latched"
(latched / "run").mkdir(parents=True)
lp = subprocess.Popen([str(exe), "--latched-capture-child", str(latched / "run")])
lcreated = run.creation_time(int(lp._handle))
lp.wait(timeout=180)
if lp.returncode != 0: raise SystemExit("FAIL latched capture child exited %s" % lp.returncode)
lreceipt = {"forced": False, "errors": []}
run.seal(latched, lp, lcreated, lreceipt, finalizer)
lseal = lreceipt.get("seal")
if not (isinstance(lseal, dict) and lseal["exit"] == 0 and "PASS validated export" in lseal["output"]):
    raise SystemExit("FAIL latched capture not sealed: %r" % lseal)
lexport = (latched / "validated.tsv").read_text()
if "action.steer.injected 1" not in lexport or "game.valid 0" not in lexport:
    raise SystemExit("FAIL the latched export lacks the injected action or the unconsumed final row")
print("PASS dbce_shim capture sealed under the generic contract;", seal["output"].splitlines()[-1], "; another game id refused;",
      "latched capture sealed:", lseal["output"].splitlines()[-1])
