"""The input shim's first in-game loop: one latched OutRunDesktop run driven through inject.txt.

Live, on the rig, only when coordinated (Astra/owner). It reuses the existing protections: the hub's rig lease and
owner-idle gate (tools/Rig-Lease.ps1), the test fence (inject.on latches; the game folder is only read; saves,
options and logs go to the run folder), and this build's absence of any force feedback. It never touches the owner's
game folder: it runs from a dedicated test copy (`--prepare` builds it from a source folder). It sends no OS input
and never foregrounds anything: commands go through inject.txt, and the game is closed with WM_CLOSE to its own
window. inject.on / inject.txt are removed on every exit path.

Workload: confirm/start presses until the game reaches a race (mode 16/18, read from the shim's runtime.log lines),
then steer and throttle holds, then `end`. Evidence: the run folder's runtime.log (the shim's record/consumed lines),
controls-journal.jsonl (the strict reader) and host-shutdown.json. The capture (the run folder's capture subfolder) is sealed in the
evidence copy by the reviewed P21 finalizer (--finalizer): the final host report is copied into it and this runner writes
exit.json only for an exit it observed, with the process identity it held from the spawn.
"""
import argparse, ctypes, ctypes.wintypes as wt, hashlib, json, os, pathlib, random, re, shutil, string, subprocess, sys, time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "control_journal")); import read_journal
DBCE = pathlib.Path(os.environ["LOCALAPPDATA"]) / "dbce" / "outrun2006-decomp"
HUB_LEASE = pathlib.Path(r"E:\Source\dbce-project-mgmt\tools\Rig-Lease.ps1")
# Test-only bindings on the rig's real DirectInput identities (readonly-provider-live-03): the axes as the owner's
# profile has them, the menu actions on otherwise unused R12 buttons. Physical presses of these buttons are not
# admitted objects during the run (latched: physical input stays test-neutral).
WHEEL = "dev={d71b8350-61b7-11f1-8001-444553540000} prod={0006346e-0000-0000-0000-504944564944}"
PROFILE = "\n".join([
    "[Controls]", "Schema = 1", "Profile = ShimFirstLoop", "Revision = t1", "Transmission = Sequential",
    "steer = axis 0 %s range=0..65535 rest=32768 travel=+1" % WHEEL,
    "throttle = axis 2 %s range=0..65535 rest=0 travel=+1" % WHEEL,
    "brake = axis 5 %s range=0..65535 rest=0 travel=+1" % WHEEL,
    "confirm = button 20 %s" % WHEEL, "start = button 21 %s" % WHEEL, "back = button 22 %s" % WHEEL,
    "navUp = button 23 %s" % WHEEL, "navDown = button 24 %s" % WHEEL, ""])
SIGNAL = re.compile(r"input shim: frame (\d+) run (\w+) mode (\d+)( race)? record steer (-?\d+) accel (\d+) brake (\d+) "
                    r"buttons ([0-9a-f]+) consumed steer (-?\d+) accel (-?\d+) brake (-?\d+)")

user32 = ctypes.WinDLL("user32", use_last_error=True); kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()


def replace_file(path, data):
    """Atomic publish of inject files: a sibling temp, then ReplaceFileW (works while the shim holds the old handle
    with FILE_SHARE_DELETE; MoveFileEx(REPLACE_EXISTING) is refused then, Astra 15795). New file: a plain rename."""
    path = pathlib.Path(path); tmp = path.with_name(path.name + ".tmp-" + "".join(random.choices(string.ascii_lowercase, k=8)))
    tmp.write_bytes(data)
    if path.exists():
        for _ in range(20):
            if kernel32.ReplaceFileW(str(path), str(tmp), None, 0, None, None): return
            time.sleep(0.05)
        tmp.unlink(missing_ok=True); raise OSError("ReplaceFileW failed: %d" % ctypes.get_last_error())
    os.rename(tmp, path)


def windows_of(pid):
    found = []
    def cb(h, _):
        p = wt.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h): found.append(h)
        return True
    user32.EnumWindows(EnumWindowsProc(cb), 0)
    return found


def creation_time(handle):
    times = [wt.FILETIME() for _ in range(4)]
    if not kernel32.GetProcessTimes(wt.HANDLE(handle), *[ctypes.byref(t) for t in times]): return 0
    return (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime


def seal(out, proc, created, receipt, finalizer):
    """P21: the worker's report (in the capture), the host's final report and an observed normal exit by the same
    process. A missing capture or an exit this runner did not observe is reported, never sealed."""
    cap = out / "run" / "capture"
    if not cap.exists(): receipt["seal"] = "no capture"; return
    if proc is None or proc.poll() is None or not created: receipt["seal"] = "no observed exit"; return
    host = (out / "run" / "host-shutdown.json").read_bytes()
    with open(cap / "host-shutdown.json", "xb") as f: f.write(host)
    with open(cap / "exit.json", "xb") as f:
        f.write(json.dumps(dict(pid=proc.pid, creationTime=created, exitCode=proc.returncode & 0xFFFFFFFF, forced=receipt["forced"])).encode())
    q = subprocess.run(["dotnet", str(finalizer), "--seal-export", str(cap), str(out / "validated.tsv")], capture_output=True, text=True, timeout=300)
    receipt["seal"] = dict(exit=q.returncode, output=(q.stdout + q.stderr).strip()[-600:])


def replay(out, receipt, tool):
    """R2: the adapter/action replay of the sealed capture (capture_replay): every recorded Frame and GameView through a
    fresh Adapter must give the recorded signals. Only a sealed capture is replayed."""
    if not (isinstance(receipt.get("seal"), dict) and receipt["seal"].get("exit") == 0): receipt["replay"] = "not sealed"; return
    q = subprocess.run([str(tool), str(out / "run" / "capture"), str(out / "validated.tsv")], capture_output=True, text=True, timeout=600)
    receipt["replay"] = dict(exit=q.returncode, output=(q.stdout + q.stderr).strip()[-600:])


def verdict(receipt):
    """Astra 16766: diagnostics are saved either way; any unmet condition fails the run."""
    failures = ["error: %s" % e for e in receipt["errors"]]
    if receipt.get("phase") != "end": failures.append("workload unfinished (phase %s)" % receipt.get("phase"))
    if receipt.get("forced") or receipt.get("exitCode") != 0:
        failures.append("exit not normal (code %r, forced %r)" % (receipt.get("exitCode"), receipt.get("forced")))
    if (receipt.get("host") or {}).get("state") != "resolved": failures.append("host shutdown not resolved")
    if not (receipt.get("journal") or {}).get("qualifies"): failures.append("control journal does not qualify")
    seal_result = receipt.get("seal")
    if not (isinstance(seal_result, dict) and seal_result.get("exit") == 0): failures.append("capture not sealed: %s" % str(seal_result)[:200])
    replay_result = receipt.get("replay")
    if not (isinstance(replay_result, dict) and replay_result.get("exit") == 0): failures.append("adapter replay did not reconstruct: %s" % str(replay_result)[:300])
    receipt["result"] = "fail" if failures else "pass"; receipt["failures"] = failures
    return failures


def lease(action, purpose=""):
    cmd = ["pwsh", "-NoProfile", "-File", str(HUB_LEASE), "-" + action]
    if action == "Enter": cmd += ["-Purpose", purpose, "-IdleMinutes", "5"]
    q = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if q.returncode != 0: raise RuntimeError("rig lease %s refused: %s" % (action, (q.stdout + q.stderr).strip()[-400:]))


def prepare(source, game, candidate):
    if game.exists(): raise SystemExit("the test folder already exists: %s" % game)
    shutil.copytree(source, game)
    shutil.copy2(candidate, game / "OutRunDesktop.exe")
    (game / "OutRunDesktop.ini").write_text(PROFILE, encoding="utf-8", newline="\n")
    print("prepared %s (exe %s, profile %s)" % (game, sha(game / "OutRunDesktop.exe")[:16], sha(game / "OutRunDesktop.ini")[:16]))


def main():
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument("--game", type=pathlib.Path, default=pathlib.Path(r"E:\Games\OutRunShimTest"))
    a.add_argument("--candidate", type=pathlib.Path, required=True)
    a.add_argument("--prepare-from", type=pathlib.Path, help="build --game as a copy of this game folder first")
    a.add_argument("--result", type=pathlib.Path, help="a fresh result folder (runs and preflight)")
    a.add_argument("--finalizer", type=pathlib.Path, help="the reviewed P21Finalizer.dll (p21_recorder_integration/managed)")
    a.add_argument("--replay-tool", type=pathlib.Path, help="capture_replay.exe (R2), built with the candidate")
    a.add_argument("--seconds", type=int, default=180)
    a.add_argument("--check-only", action="store_true", help="preflight only: no lease, no files, no launch")
    o = a.parse_args()
    if o.prepare_from: prepare(o.prepare_from, o.game, o.candidate); return
    if not o.result: a.error("--result is required for a run or a preflight")
    out = o.result.resolve()
    problems = []
    if out.exists(): problems.append("result folder exists")
    if not (o.game / "OR2006C2C.exe").exists(): problems.append("no game data in %s (use --prepare-from)" % o.game)
    if not (o.game / "OutRunDesktop.exe").exists() or sha(o.game / "OutRunDesktop.exe") != sha(o.candidate):
        problems.append("the test folder's OutRunDesktop.exe is not the candidate")
    ini = o.game / "OutRunDesktop.ini"
    if not ini.exists() or ini.read_text(encoding="utf-8") != PROFILE:
        problems.append("the test folder's OutRunDesktop.ini is not the test profile")
    if not o.finalizer or not o.finalizer.exists(): problems.append("no --finalizer P21Finalizer.dll to seal the capture")
    if not o.replay_tool or not o.replay_tool.exists(): problems.append("no --replay-tool capture_replay.exe to replay the capture")
    for f in ("inject.on", "inject.txt"):
        if (DBCE / f).exists(): problems.append("leftover %s in %s" % (f, DBCE))
    running = subprocess.run(["tasklist", "/FI", "IMAGENAME eq OutRunDesktop.exe", "/NH"], capture_output=True, text=True).stdout
    if "OutRunDesktop.exe" in running: problems.append("OutRunDesktop is already running")
    plan = dict(game=str(o.game), candidate=str(o.candidate), candidateSha256=sha(o.candidate) if o.candidate.exists() else None,
                finalizer=str(o.finalizer), finalizerSha256=sha(o.finalizer) if o.finalizer and o.finalizer.exists() else None,
                replayTool=str(o.replay_tool), replayToolSha256=sha(o.replay_tool) if o.replay_tool and o.replay_tool.exists() else None,
                dbce=str(DBCE), seconds=o.seconds, problems=problems)
    if o.check_only or problems:
        print(json.dumps(plan, indent=1)); sys.exit(1 if problems else 0)
    out.mkdir(parents=True)
    receipt = dict(plan=plan, started=time.strftime("%Y-%m-%dT%H:%M:%S%z"), presses=[], batches=[], forced=False, errors=[],
                   injection="inject action <id> <n>: the shim resolves each action through the test profile's declared binding "
                             "inverse into a raw sample of that bound R12 object; not a physical hardware trial (Astra 16520)")
    nonce = "".join(random.choices(string.ascii_letters + string.digits, k=16))
    proc = None; run_root = None; leased = False; created = 0
    published = {}                               # only files this invocation wrote, with their last bytes
    def publish(name, data):
        replace_file(DBCE / name, data); published[name] = data
    try:
        lease("Enter", "OutRun shim first loop (latched, no force)"); leased = True
        DBCE.mkdir(parents=True, exist_ok=True)
        publish("inject.on",("nonce=%s\nexpires=%d\n" % (nonce, int(time.time()) + o.seconds + 120)).encode())
        proc = subprocess.Popen([str(o.game / "OutRunDesktop.exe"), "--windowed"], cwd=str(o.game))
        receipt["pid"] = proc.pid; created = creation_time(int(proc._handle)); receipt["creationTime"] = created
        run_root = DBCE / ("run-%s-%d" % (nonce, proc.pid)); log = run_root / "runtime.log"
        deadline = time.time() + o.seconds; seen = 0; mode = None; race = False; n = 0; last_press = 0; lines = []
        def batch(commands):
            nonlocal n
            n += 1; body = "nonce=%s\nbatch=%d\n%s\n" % (nonce, n, "\n".join(commands))
            publish("inject.txt", body.encode()); receipt["batches"].append(dict(n=n, commands=commands, at=time.time()))
        phase = "menus"; phase_at = 0
        while time.time() < deadline and proc.poll() is None:
            time.sleep(0.2)
            if log.exists():
                text = log.read_text(encoding="utf-8", errors="replace"); new = text.splitlines()[seen:]; seen += len(new); lines += new
                for l in new:
                    m = SIGNAL.search(l)
                    if m: mode = int(m.group(3)); race = m.group(4) is not None
            if phase == "menus":
                if race: phase, phase_at = "steer-right", time.time(); batch(["inject action steer 0.6 ms=3000", "inject action throttle 1 ms=3000"]); continue
                if time.time() - last_press > 1.5 and len(receipt["presses"]) < 40:
                    action = "start" if len(receipt["presses"]) % 3 == 0 else "confirm"
                    batch(["inject action %s 1 ms=200" % action]); receipt["presses"].append(dict(action=action, mode=mode)); last_press = time.time()
            elif phase == "steer-right" and time.time() - phase_at > 3.5:
                phase, phase_at = "steer-left", time.time(); batch(["inject action steer -0.6 ms=3000", "inject action throttle 1 ms=3000"])
            elif phase == "steer-left" and time.time() - phase_at > 3.5:
                phase = "end"; batch(["end"]); break
        receipt["phase"] = phase; receipt["race"] = race
        time.sleep(1.0)
        if proc.poll() is None:
            for h in windows_of(proc.pid): user32.PostMessageW(h, 0x0010, 0, 0)   # WM_CLOSE: the game's own close path
            try: proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                # A latched process whose stop did not resolve parks for its supervisor (P21): this runner ends it.
                receipt["forced"] = True; proc.kill(); proc.wait(timeout=15)
        receipt["exitCode"] = proc.returncode
    except Exception as e:
        receipt["errors"].append(repr(e))
        if proc and proc.poll() is None:
            for h in windows_of(proc.pid): user32.PostMessageW(h, 0x0010, 0, 0)
            try: proc.wait(timeout=30)
            except subprocess.TimeoutExpired: receipt["forced"] = True; proc.kill()
    finally:
        try:
            try:
                # Only while our lease is held, and only the files this invocation published with exactly our bytes: a
                # refused lease or another caller's files are never touched (Astra 16520).
                if leased:
                    for f, data in published.items():
                        try:
                            if (DBCE / f).read_bytes() == data: (DBCE / f).unlink()
                            else: receipt["errors"].append("cleanup %s: not our bytes, left in place" % f)
                        except FileNotFoundError: pass
                        except OSError as e: receipt["errors"].append("cleanup %s: %r" % (f, e))
            finally:
                try:
                    if run_root and run_root.exists(): shutil.copytree(run_root, out / "run", dirs_exist_ok=True)
                except Exception as e: receipt["errors"].append("evidence copy: %r" % e)
        finally:
            if leased:
                try: lease("Exit")
                except Exception as e: receipt["errors"].append(repr(e))
        try: seal(out, proc, created, receipt, o.finalizer)
        except Exception as e: receipt["errors"].append("seal: %r" % e)
        try: replay(out, receipt, o.replay_tool)
        except Exception as e: receipt["errors"].append("replay: %r" % e)
        try:
            if (out / "run" / "controls-journal.jsonl").exists(): receipt["journal"] = read_journal.read(str(out / "run" / "controls-journal.jsonl"))
            if (out / "run" / "host-shutdown.json").exists(): receipt["host"] = json.loads((out / "run" / "host-shutdown.json").read_text())
        except Exception as e: receipt["errors"].append("evidence read: %r" % e)
        sig = [SIGNAL.search(l) for l in (out / "run" / "runtime.log").read_text(encoding="utf-8", errors="replace").splitlines()] if (out / "run" / "runtime.log").exists() else []
        sig = [m for m in sig if m]
        receipt["modes"] = sorted({int(m.group(3)) for m in sig})
        receipt["maxRecordSteer"] = max([abs(int(m.group(5))) for m in sig], default=0)
        receipt["maxConsumedSteer"] = max([abs(int(m.group(9))) for m in sig], default=0)
        receipt["maxConsumedAccel"] = max([int(m.group(10)) for m in sig], default=0)
        failures = verdict(receipt)
        (out / "receipt.json").write_text(json.dumps(receipt, indent=1, default=str))
        print(json.dumps({k: receipt.get(k) for k in ("result", "failures", "pid", "exitCode", "forced", "phase", "race", "modes", "maxRecordSteer",
                                                     "maxConsumedSteer", "maxConsumedAccel", "seal", "replay")}, indent=1))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
