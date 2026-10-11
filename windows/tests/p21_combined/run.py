"""Offline real-provider + real-recorder composition; no game, COM initialization or devices."""
import argparse
import hashlib
import json
import math
import pathlib
import shutil
import subprocess
from writer_pin import verify_writer

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("result", type=pathlib.Path)
parser.add_argument("--toolkit", type=pathlib.Path, required=True)
args = parser.parse_args()
here = pathlib.Path(__file__).resolve().parent
repo = here.parents[2]
toolkit = args.toolkit.resolve()
result = args.result.resolve()
if result.exists():
    raise SystemExit("Use a new result directory")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(root, *arguments):
    return subprocess.check_output(["git", "-C", str(root), *arguments], text=True).strip()


def pin(root, scopes):
    if git(root, "status", "--porcelain", "--", *scopes):
        raise RuntimeError("Uncommitted source: " + str(root))
    return {"commit": git(root, "rev-parse", "HEAD"), "sha256": {
        p: sha(root / p) for p in git(root, "ls-files", "--", *scopes).splitlines()}}


tk_scopes = ["native/session", "dotnet/Dbce.Wheel.Recording", "dotnet/Dbce.Wheel.Playback/ArtifactSeal.cs", "Directory.*", "global.json", "NuGet.Config"]
before = {"source": pin(repo, ["."]), "toolkit": pin(toolkit, tk_scopes)}
writer_pin = verify_writer((toolkit / "native/session/session_writer.h").read_bytes())
if before["toolkit"]["sha256"]["native/session/session_writer.h"] != writer_pin["rawSha256"]:
    raise RuntimeError("Native writer changed during preflight")
if before["source"]["sha256"]["windows/vendor/session/session_writer.h"] != "a9de5d49c55b9b4be464fb173178d20a97972eacff500aee0267e30fb44d19db":
    raise RuntimeError("Unreviewed vendored native writer")   # the build compiles the vendored copy (R1a)
result.mkdir(parents=True)
(result / "writer-pin.json").write_text(json.dumps(writer_pin, indent=2))
(result / "source-before.json").write_text(json.dumps(before, indent=2))
commands = []


def run(name, command, expected=0):
    outcome = subprocess.run([str(x) for x in command], cwd=repo, capture_output=True, text=True, timeout=300)
    (result / (name + ".log")).write_text(outcome.stdout + outcome.stderr)
    commands.append({"name": name, "command": [str(x) for x in command], "exit": outcome.returncode})
    if expected is not None and outcome.returncode != expected:
        raise RuntimeError(name + " failed; see log")
    return outcome


run("writer-pin-tests", ["py", "-3", "-B", here / "test_writer_pin.py", toolkit / "native/session/session_writer.h"])
run("configure", ["cmake", "-S", here, "-B", result / "build", "-G", "Visual Studio 17 2022", "-T", "ClangCL", "-A", "x64", "-DTOOLKIT_ROOT=" + str(toolkit)])
run("build", ["cmake", "--build", result / "build", "--config", "Release"])
run("finalizer-build", ["dotnet", "build", here.parent / "p21_recorder_integration/managed/P21Finalizer.csproj", "-c", "Release", "--artifacts-path", result / "managed", "-p:ToolkitRoot=" + str(toolkit)])
exe = result / "build/Release/p21_combined.exe"
dll = result / "managed/bin/P21Finalizer/release/P21Finalizer.dll"
run("supervised", [exe, result / "runs"])
shutil.copytree(result / "runs", result / "runs-unsealed")   # as the supervisor left them, for the negative control
accepted = []
refused = []
for mode in ["resolved", "discovery", "release-within", "writer-stuck", "provider-stuck", "ordinary-provider-stuck", "release-stuck"]:
    directory = result / "runs" / mode
    counters = json.loads((directory / "provider-counters.json").read_text())
    if counters["creates"] != 2 or counters["reads"] < 32 or counters["polls"] < 32 or any(counters[k] != 0 for k in ("forceCalls", "releaseDuringCall", "afterRelease")):
        raise RuntimeError("Real provider calls/lifetime invalid: " + mode)
    outcome = run("seal-" + mode, ["dotnet", dll, "--seal-export", directory / "capture", directory / "validated.tsv"], None)
    if mode in ("resolved", "discovery", "release-within"):
        if outcome.returncode:
            raise RuntimeError("Resolved capture refused: " + mode)
        if counters["inputRefs"] != 0:
            raise RuntimeError("Resolved DirectInput was not released")
        run("replay-" + mode, [exe, "--replay", directory / "capture"])
        accepted.append(mode)
    else:
        reason = "bundle.meta" if mode == "writer-stuck" else "host shutdown resolved"
        if mode == "writer-stuck" and (directory / "capture/worker.tsv").exists():
            raise RuntimeError("Timed-out writer published a quiescent worker report")
        if not outcome.returncode or reason not in outcome.stderr or (directory / "capture/complete.tsv").exists():
            raise RuntimeError("Unresolved capture not refused at its intended boundary: " + mode)
        refused.append(mode)

# Change a recorded field, leaving the independent producer snapshot intact.
mutation = result / "negative-replay"
shutil.copytree(result / "runs/resolved", mutation)
path = mutation / "validated.tsv"
text = path.read_text()
old = next((line for line in text.splitlines(keepends=True) if line.startswith("action.steer.value ")), None)
if old is None:
    raise RuntimeError("Exact-replay mutation anchor missing")
changed = math.nextafter(float(old.split()[1]), math.inf)
path.write_text(text.replace(old, "action.steer.value " + repr(changed) + "\n", 1))
mutant = run("changed-channel", [exe, "--replay", mutation / "capture"], None)
if mutant.returncode == 0 or "preserve every real-provider producer channel" not in mutant.stderr:
    raise RuntimeError("Changed channel was not detected")

# A desktop host report naming another run never seals (the runNonce binding).
other = result / "other-run-host"
shutil.copytree(result / "runs-unsealed/resolved", other)
report = other / "capture/host-shutdown.json"
run_nonce = json.loads(report.read_text())["nonce"]
report.write_text(report.read_text().replace('"nonce":"%s"' % run_nonce, '"nonce":"p21otherrun0"', 1))
foreign = run("other-run-host", ["dotnet", dll, "--seal-export", other / "capture", other / "validated.tsv"], None)
if foreign.returncode == 0 or "host report names this capture" not in foreign.stderr:
    raise RuntimeError("Host report from another run was not refused")

after = {"source": pin(repo, ["."]), "toolkit": pin(toolkit, tk_scopes)}
if before != after:
    raise RuntimeError("Source changed during proof")
receipt = {"sourceCommit": before["source"]["commit"], "toolkitCommit": before["toolkit"]["commit"], "sourceVerified": True,
    "scope": "offline: same P21 host, real DInputProvider/Core/OutRun adapter and real recorder/session writer over fake COM; no actual game, devices, force or install",
    "sealedAndReplayed": accepted, "unresolvedRefused": refused, "changedChannelRefused": True, "otherRunHostRefused": True, "commands": commands,
    "sha256": {str(p.relative_to(result)): sha(p) for p in (result / "runs").rglob("*") if p.is_file()}}
(result / "receipt.json").write_text(json.dumps(receipt, indent=2))
print("PASS: three sealed real-provider/recorder originals (one with device discovery and loss/reopen) and action replays; four unresolved refusals; exact channel mutation; another run's host report refused;", result / "receipt.json")
